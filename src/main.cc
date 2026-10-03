#include "display_manager.h"
#include <iostream>

int main() {
    dm::Manager manager;
    if (!manager.available()) { std::cerr << manager.error() << '\n'; return 2; }
    while (true) {
        const auto displays = manager.displays();
        std::cout << "== Online Displays ==\n";
        for (size_t i = 0; i < displays.size(); ++i) {
            const auto& d = displays[i];
            std::cout << '[' << i << "] ID=" << d.id << " " << d.width << 'x'
                      << d.height << (d.main ? " [Main]" : "") << '\n';
        }
        std::cout << '[' << displays.size() << "] RESTORE ALL\n["
                  << displays.size() + 1 << "] EXIT\nSelect index: ";
        std::string input;
        if (!std::getline(std::cin, input)) break;
        size_t consumed = 0;
        int selected = -1;
        try { selected = std::stoi(input, &consumed); } catch (...) {}
        if (consumed != input.size() || selected < 0 || selected > int(displays.size()) + 1) {
            std::cerr << "Invalid index\n"; continue;
        }
        if (selected == int(displays.size()) + 1) break;
        const auto result = selected == int(displays.size())
            ? manager.restoreAll() : manager.disable(displays[selected].id);
        std::cout << (result.ok ? "OK: " : "ERROR: ") << result.message << '\n';
    }
}
