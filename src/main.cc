// display_disable_and_restore_loop_exit.cpp
// Build:
//   clang++ display_disable_and_restore_loop_exit.cpp -o display_mgr \
//     -framework ApplicationServices -framework CoreFoundation -ldl
//
// Summary of behavior:
//   - Continuously lists online displays and waits for user input.
//   - [0..N-1]: Disable the selected display, on success append its UUID to /tmp/disabled_displays.txt
//   - [N]: Enable all displays recorded in /tmp/disabled_displays.txt, remove successfully restored UUIDs from the file
//   - [N+1]: Exit the program

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>

using SetEnabledWithConfigFn = int(*)(CGDisplayConfigRef /*cfg*/, uint32_t /*displayID*/, bool /*enabled*/);

static const char* kDisabledListPath = "/tmp/disabled_displays.txt";

// ----- SkyLight private API loader -----
static SetEnabledWithConfigFn resolve_CGSConfigureDisplayEnabled() {
    void* h = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY);
    if (!h) {
        std::cerr << "dlopen SkyLight failed: " << dlerror() << "\n";
        return nullptr;
    }
    auto fn = (SetEnabledWithConfigFn)dlsym(h, "CGSConfigureDisplayEnabled");
    if (fn) return fn;
    std::cerr << "CGSConfigureDisplayEnabled not found.\n";
    return nullptr;
}

// ----- UUID helpers -----
static std::string uuid_str_for_display(CGDirectDisplayID d) {
    std::string out;
    CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(d);
    if (!uuid) return out;
    CFStringRef s = CFUUIDCreateString(kCFAllocatorDefault, uuid);
    CFRelease(uuid);
    if (!s) return out;
    char buf[128];
    if (CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) out.assign(buf);
    CFRelease(s);
    return out;
}

static CGDirectDisplayID display_for_uuid_str(const std::string& us) {
    if (us.empty()) return kCGNullDirectDisplay;
    CGDirectDisplayID d = kCGNullDirectDisplay;
    CFStringRef cfStr = CFStringCreateWithCString(kCFAllocatorDefault, us.c_str(), kCFStringEncodingUTF8);
    if (!cfStr) return d;
    CFUUIDRef u = CFUUIDCreateFromString(kCFAllocatorDefault, cfStr);
    CFRelease(cfStr);
    if (!u) return d;
    d = CGDisplayGetDisplayIDFromUUID(u);
    CFRelease(u);
    return d;
}

// ----- File I/O -----
static std::vector<std::string> load_disabled_uuid_list() {
    std::vector<std::string> out;
    if (FILE* f = fopen(kDisabledListPath, "r")) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            std::string s(line);
            if (!s.empty() && s.back()=='\n') s.pop_back();
            if (!s.empty()) out.push_back(s);
        }
        fclose(f);
    }
    return out;
}

static bool append_disabled_uuid(const std::string& uuid_or_id) {
    if (uuid_or_id.empty()) return false;
    if (FILE* f = fopen(kDisabledListPath, "a")) {
        fprintf(f, "%s\n", uuid_or_id.c_str());
        fclose(f);
        return true;
    }
    return false;
}

static bool rewrite_disabled_uuid_list(const std::vector<std::string>& list) {
    if (FILE* f = fopen(kDisabledListPath, "w")) {
        for (auto& s : list) fprintf(f, "%s\n", s.c_str());
        fclose(f);
        return true;
    }
    return false;
}

// ----- Online displays -----
static std::vector<CGDirectDisplayID> get_online_displays() {
    uint32_t n = 0;
    CGGetOnlineDisplayList(0, nullptr, &n);
    std::vector<CGDirectDisplayID> ids(n);
    if (n) CGGetOnlineDisplayList(n, ids.data(), &n);
    return ids;
}

int main() {
    auto CGSConfigureDisplayEnabled = resolve_CGSConfigureDisplayEnabled();
    if (!CGSConfigureDisplayEnabled) return 2;

    while (true) {
        // 1) List online displays
        auto ids = get_online_displays();
        size_t N = ids.size();

        // 2) Print menu (+ N = Restore, N+1 = Exit)
        std::cout << "== Online Displays ==\n";
        for (size_t i = 0; i < N; ++i) {
            auto b = CGDisplayBounds(ids[i]);
            std::cout << "[" << i << "] ID=" << (uint32_t)ids[i]
                      << " bounds=(" << (int)b.origin.x << "," << (int)b.origin.y
                      << "," << (int)b.size.width << "x" << (int)b.size.height << ")"
                      << (CGDisplayIsMain(ids[i]) ? " [Main]" : "")
                      << "\n";
        }
        std::cout << "[" << N     << "] RESTORE ALL (from " << kDisabledListPath << ")\n";
        std::cout << "[" << N + 1 << "] EXIT\n";

        // 3) Read user input
        std::cout << "Select index: ";
        std::string s;
        if (!std::getline(std::cin, s)) {
            std::cout << "\nInput ended. Bye.\n";
            break;
        }
        int sel = -1; try { sel = std::stoi(s); } catch (...) { sel = -1; }
        if (sel < 0 || sel > (int)N + 1) {
            std::cerr << "Invalid index\n\n";
            continue;
        }
        if (sel == (int)N + 1) {
            std::cout << "Exit.\n";
            break;
        }

        // 4) Start configuration transaction
        CGDisplayConfigRef cfg = nullptr;
        if (CGBeginDisplayConfiguration(&cfg) != kCGErrorSuccess) {
            std::cerr << "CGBeginDisplayConfiguration failed\n\n";
            continue;
        }

        bool ok = true;

        if (sel == (int)N) {
            // ---- RESTORE ALL ----
            auto disabled = load_disabled_uuid_list();
            if (disabled.empty()) {
                std::cout << "No entries in " << kDisabledListPath << " to restore.\n";
            }

            std::unordered_set<std::string> uniq(disabled.begin(), disabled.end());
            std::unordered_set<std::string> succeeded;

            for (const auto& key : uniq) {
                CGDirectDisplayID d = kCGNullDirectDisplay;
                if (key.rfind("DISPLAY_ID_", 0) == 0) {
                    uint32_t id = (uint32_t)std::strtoul(key.c_str() + 11, nullptr, 10);
                    d = (CGDirectDisplayID)id;
                } else {
                    d = display_for_uuid_str(key);
                }
                if (d == kCGNullDirectDisplay) {
                    std::cerr << "Restore skip (unresolvable): " << key << "\n";
                    continue;
                }
                int r = CGSConfigureDisplayEnabled(cfg, (uint32_t)d, true);
                if (r != 0) {
                    std::cerr << "Enable failed for " << (uint32_t)d << " ret=" << r << "\n";
                    ok = false;
                } else {
                    succeeded.insert(key);
                }
            }

            CGError cret = CGCompleteDisplayConfiguration(cfg, kCGConfigurePermanently);
            if (cret != kCGErrorSuccess) {
                std::cerr << "CGCompleteDisplayConfiguration failed " << cret << "\n";
                ok = false;
            }

            if (!disabled.empty()) {
                std::vector<std::string> remain;
                remain.reserve(disabled.size());
                for (const auto& key : disabled) {
                    if (succeeded.find(key) == succeeded.end()) remain.push_back(key);
                }
                if (!rewrite_disabled_uuid_list(remain)) {
                    std::cerr << "Failed to update " << kDisabledListPath << "\n";
                } else {
                    std::cout << "Restored " << succeeded.size()
                              << " display(s); updated " << kDisabledListPath << "\n";
                }
            }
        } else {
            // ---- Disable a single display ----
            CGDirectDisplayID target = ids[(size_t)sel];

            int r = CGSConfigureDisplayEnabled(cfg, (uint32_t)target, false);
            if (r != 0) {
                std::cerr << "Disable failed for " << (uint32_t)target << " ret=" << r << "\n";
                ok = false;
            }

            CGError cret = CGCompleteDisplayConfiguration(cfg, kCGConfigurePermanently);
            if (cret != kCGErrorSuccess) {
                std::cerr << "CGCompleteDisplayConfiguration failed " << cret << "\n";
                ok = false;
            }

            if (ok) {
                std::string uuid = uuid_str_for_display(target);
                if (uuid.empty()) uuid = std::string("DISPLAY_ID_") + std::to_string((uint32_t)target);
                if (append_disabled_uuid(uuid)) {
                    std::cout << "Disabled and recorded: " << uuid
                              << " -> " << kDisabledListPath << "\n";
                } else {
                    std::cerr << "Disabled but failed to append to " << kDisabledListPath << "\n";
                }
            }
        }

        std::cout << (ok ? "OK" : "Some operations failed") << "\n\n";
        // Loop again
    }

    return 0;
}