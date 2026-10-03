#pragma once
#include <ApplicationServices/ApplicationServices.h>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include "display_record.h"

namespace dm {
struct Display {
    CGDirectDisplayID id;
    std::string key;
    size_t width, height;
    bool main, builtin, active;
    bool mirrored;
};
struct Result { bool ok; std::string message; };
enum class DisplayAccess { Control, ReadOnly };
class Manager {
public:
    explicit Manager(DisplayAccess access = DisplayAccess::Control);
    ~Manager();
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    bool available() const { return setEnabled_ && error_.empty(); }
    const std::string& error() const { return error_; }
    std::vector<Display> displays() const;
    std::vector<std::string> disabled() const;
    Result disable(CGDirectDisplayID id);
    Result restore(const std::string& key);
    Result restoreAll();
private:
    using SetEnabled = int (*)(CGDisplayConfigRef, uint32_t, bool);
    void* library_ = nullptr;
    int lockFile_ = -1;
    SetEnabled setEnabled_ = nullptr;
    std::filesystem::path statePath_;
    std::string error_;
    DisplayAccess access_;
    std::string boot_;
    std::unordered_map<std::string, detail::DisplayRecord> records_;
    Result save(const std::vector<std::string>& keys) const;
    Result configure(CGDirectDisplayID id, bool enabled) const;
};
}
