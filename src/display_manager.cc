#include "display_manager.h"
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/time.h>

namespace dm {
namespace {
std::string bootSession() {
    timeval boot{};
    size_t size = sizeof(boot);
    if (sysctlbyname("kern.boottime", &boot, &size, nullptr, 0) != 0) return {};
    return std::to_string(boot.tv_sec) + ":" + std::to_string(boot.tv_usec);
}
std::string keyForDisplay(CGDirectDisplayID id) {
    CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(id);
    if (!uuid) return {};
    CFStringRef string = CFUUIDCreateString(nullptr, uuid);
    CFRelease(uuid);
    char buffer[128] = {};
    bool ok = string && CFStringGetCString(string, buffer, sizeof(buffer), kCFStringEncodingUTF8);
    if (string) CFRelease(string);
    return ok ? buffer : std::string{};
}
CGDirectDisplayID displayForKey(const std::string& key) {
    // Keep support for the previous CLI display ID format.
    if (key.starts_with("DISPLAY_ID_")) {
        const auto value = key.substr(11);
        if (value.empty() || !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return kCGNullDirectDisplay;
        try {
            size_t consumed = 0;
            auto id = std::stoul(key.substr(11), &consumed);
            if (consumed == key.size() - 11 && id > 0 && id <= UINT32_MAX) return CGDirectDisplayID(id);
        } catch (...) {}
        return kCGNullDirectDisplay;
    }
    // Validate strictly because CFUUIDCreateFromString can accept malformed identifiers.
    if (!detail::isDisplayUUID(key)) return kCGNullDirectDisplay;
    CFStringRef string = CFStringCreateWithCString(nullptr, key.c_str(), kCFStringEncodingUTF8);
    if (!string) return kCGNullDirectDisplay;
    CFUUIDRef uuid = CFUUIDCreateFromString(nullptr, string);
    CFRelease(string);
    if (!uuid) return kCGNullDirectDisplay;
    auto id = CGDisplayGetDisplayIDFromUUID(uuid);
    CFRelease(uuid);
    return id;
}
std::vector<std::string> readKeys(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> keys;
    std::string key;
    while (std::getline(input, key)) {
        if (!key.empty() && key.back() == '\r') key.pop_back();
        key = detail::parseRecord(key).key;
        if (!key.empty() && std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
    }
    return keys;
}
}
Manager::Manager(DisplayAccess access) : access_(access) {
    library_ = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY);
    if (library_) setEnabled_ = reinterpret_cast<SetEnabled>(dlsym(library_, "CGSConfigureDisplayEnabled"));
    if (!setEnabled_) { error_ = "The display control API is unavailable on this version of macOS."; return; }
    const char* home = std::getenv("HOME");
    if (!home || !*home) { error_ = "Cannot find the user home directory."; return; }
    statePath_ = std::filesystem::path(home) / "Library/Application Support/DisplayManager/disabled-displays.txt";
    boot_ = bootSession();
    std::error_code ec;
    std::filesystem::create_directories(statePath_.parent_path(), ec);
    if (ec) { error_ = "Cannot create the recovery records directory: " + ec.message(); return; }
    auto lockPath = statePath_.parent_path() / "manager.lock";
    lockFile_ = open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
    if (lockFile_ == -1 || flock(lockFile_, LOCK_EX | LOCK_NB) != 0) {
        error_ = "Another DisplayManager app or dm CLI is running, or the records cannot be locked.";
        return;
    }
    bool exists = std::filesystem::exists(statePath_, ec);
    if (ec) { error_ = "Cannot inspect recovery records: " + ec.message(); return; }
    // Import legacy CLI records only when the new file does not exist.
    if (!exists) {
        auto result = save(readKeys("/tmp/disabled_displays.txt"));
        if (!result.ok) error_ = result.message;
    } else {
        std::ifstream input(statePath_);
        if (!input) error_ = "Cannot read recovery records.";
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto record = detail::parseRecord(line);
            if (!record.key.empty()) records_[record.key] = record;
        }
    }
    // Initialize the WindowServer connection and remember currently available display identifiers.
    for (const auto& display : displays()) {
        if (!display.key.empty()) records_[display.key] = {display.key, display.id, boot_};
    }
}
Manager::~Manager() {
    if (lockFile_ != -1) close(lockFile_);
    if (library_) dlclose(library_);
}
std::vector<Display> Manager::displays() const {
    uint32_t count = 0;
    if (CGGetOnlineDisplayList(0, nullptr, &count) != kCGErrorSuccess) return {};
    std::vector<CGDirectDisplayID> ids(count);
    if (count && CGGetOnlineDisplayList(count, ids.data(), &count) != kCGErrorSuccess) return {};
    ids.resize(count);
    std::vector<Display> result;
    for (auto id : ids) result.push_back({id, keyForDisplay(id), CGDisplayPixelsWide(id), CGDisplayPixelsHigh(id),
        bool(CGDisplayIsMain(id)), bool(CGDisplayIsBuiltin(id)), bool(CGDisplayIsActive(id)),
        bool(CGDisplayIsInMirrorSet(id))});
    return result;
}
std::vector<std::string> Manager::disabled() const { return readKeys(statePath_); }
Result Manager::save(const std::vector<std::string>& keys) const {
    auto temporary = statePath_;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return {false, "Cannot save recovery records."};
    for (const auto& key : keys) {
        auto record = records_.find(key);
        output << (record == records_.end() ? key : detail::serializeRecord(record->second)) << '\n';
    }
    output.close();
    if (!output) return {false, "Failed to write recovery records."};
    std::error_code ec;
    std::filesystem::rename(temporary, statePath_, ec);
    if (ec) return {false, "Failed to replace recovery records: " + ec.message()};
    return {true, {}};
}
Result Manager::configure(CGDirectDisplayID id, bool enabled) const {
    // Prevent tests from changing actual displays even if UUID resolution or safety guards fail.
    if (access_ == DisplayAccess::ReadOnly) return {false, "Display settings cannot be changed in read-only mode."};
    if (!available()) return {false, error_};
    CGDisplayConfigRef config = nullptr;
    auto error = CGBeginDisplayConfiguration(&config);
    if (error != kCGErrorSuccess) return {false, "Cannot begin display configuration: " + std::to_string(error)};
    int result = setEnabled_(config, id, enabled);
    if (result != 0) {
        CGCancelDisplayConfiguration(config);
        return {false, "Display configuration request failed: " + std::to_string(result)};
    }
    error = CGCompleteDisplayConfiguration(config, kCGConfigurePermanently);
    if (error != kCGErrorSuccess) return {false, "Failed to apply display configuration: " + std::to_string(error)};
    return {true, enabled ? "Display restored." : "Display disabled."};
}
Result Manager::disable(CGDirectDisplayID id) {
    if (!available()) return {false, error_};
    const auto current = displays();
    auto target = std::find_if(current.begin(), current.end(), [id](const auto& d) { return d.id == id; });
    if (target == current.end() || (!target->active && !target->mirrored)) return {false, "Cannot find the active display. Reopen the menu."};
    if (target->active && std::count_if(current.begin(), current.end(), [](const auto& d) { return d.active; }) <= 1)
        return {false, "The last active display cannot be disabled."};
    if (target->key.empty()) return {false, "Disable canceled because the display UUID is unavailable."};
    if (boot_.empty()) return {false, "Disable canceled because the boot identifier is unavailable for safe restoration."};
    records_[target->key] = {target->key, id, boot_};
    auto keys = disabled();
    if (std::find(keys.begin(), keys.end(), target->key) == keys.end()) keys.push_back(target->key);
    // Save a recovery record before disabling a display so restoration survives app termination.
    auto saved = save(keys);
    if (!saved.ok) return saved;
    return configure(id, false);
}
Result Manager::restore(const std::string& key) {
    if (!available()) return {false, error_};
    auto keys = disabled();
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) return {false, "Recovery record not found."};
    auto stored = records_.find(key);
    if (!detail::isDisplayUUID(key) && !key.starts_with("DISPLAY_ID_"))
        return {false, "Invalid display UUID in the recovery record."};
    detail::DisplayRecord record = stored == records_.end() ? detail::DisplayRecord{key, 0, {}} : stored->second;
    auto id = detail::resolveRecord(record, boot_, displayForKey, [](CGDirectDisplayID cached) {
        return CGDisplayIsOnline(cached) ? keyForDisplay(cached) : std::string{};
    });
    if (id == kCGNullDirectDisplay) return {false,
        "Cannot find the display. Its saved ID may be missing or may have changed since the last boot. "
        "Reconnect an external display before restoring it. For a built-in display, reboot and try again."};
    auto result = configure(id, true);
    if (!result.ok) return result;
    std::erase(keys, key);
    auto saved = save(keys);
    if (!saved.ok) return {false, "Display restored, but recovery records could not be updated: " + saved.message};
    return result;
}
Result Manager::restoreAll() {
    if (!available()) return {false, error_};
    auto keys = disabled();
    if (keys.empty()) return {true, "No displays to restore."};
    size_t succeeded = 0;
    std::string errors;
    for (const auto& key : keys) {
        auto result = restore(key);
        if (result.ok) ++succeeded;
        else errors += "\n" + result.message;
    }
    return {succeeded == keys.size(), std::to_string(succeeded) + "/" + std::to_string(keys.size()) + " displays restored" + errors};
}
}
