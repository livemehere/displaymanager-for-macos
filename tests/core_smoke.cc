#include "display_manager.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    // Check saved records and safety guards without changing actual display settings.
    char fixture[] = "/tmp/displaymanager-test-XXXXXX";
    char* root = mkdtemp(fixture);
    require(root, "temporary directory creation failed");
    setenv("HOME", root, 1);
    const auto folder = std::filesystem::path(root) / "Library/Application Support/DisplayManager";
    std::filesystem::create_directories(folder);
    const auto state = folder / "disabled-displays.txt";
    { std::ofstream out(state); out << "not-a-display-uuid\nnot-a-display-uuid\n"; }
    try {
        {
            dm::Manager manager(dm::DisplayAccess::ReadOnly);
            require(manager.available(), manager.error().c_str());
            require(manager.disabled().size() == 1, "duplicate records must be deduplicated");
            require(!manager.disable(kCGNullDirectDisplay).ok, "invalid display must be rejected");
            require(!manager.restore("missing-record").ok, "unknown record must be rejected");
            require(!manager.restore("not-a-display-uuid").ok, "malformed UUID must be rejected before display configuration");
            require(!manager.restoreAll().ok, "unresolvable display must fail restoration");
            require(manager.disabled().size() == 1, "failed restoration must retain record");
            dm::Manager second(dm::DisplayAccess::ReadOnly);
            require(!second.available(), "simultaneous writers must be rejected");
        }
        { std::ofstream out(state, std::ios::trunc); }
        {
            dm::Manager manager(dm::DisplayAccess::ReadOnly);
            require(manager.available(), "lock must be released when manager exits");
            require(manager.restoreAll().ok, "empty restoration must succeed");
            const auto displays = manager.displays();
            size_t active = 0;
            CGDirectDisplayID last = kCGNullDirectDisplay;
            for (const auto& d : displays) if (d.active) { ++active; last = d.id; }
            if (active == 1) require(!manager.disable(last).ok, "last active display must be protected");
            std::cout << "PASS: records, invalid targets, failed restoration, locking, empty restoration";
            std::cout << (active == 1 ? ", last-display guard\n" : " (last-display guard skipped: multiple/no active displays)\n");
        }
        std::filesystem::remove_all(root);
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
