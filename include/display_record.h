#pragma once
#include <cstdint>
#include <string>

namespace dm::detail {
inline bool isDisplayUUID(const std::string& key) {
    if (key.size() != 36) return false;
    for (size_t i = 0; i < key.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (key[i] != '-') return false;
        } else {
            char c = key[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return false;
        }
    }
    return true;
}
struct DisplayRecord {
    std::string key;
    uint32_t id = 0;
    std::string boot;
};
inline DisplayRecord parseRecord(const std::string& line) {
    auto first = line.find('\t');
    if (first == std::string::npos) return {line, 0, {}};
    DisplayRecord record{line.substr(0, first), 0, {}};
    auto second = line.find('\t', first + 1);
    if (second == std::string::npos) return record;
    try {
        auto value = line.substr(first + 1, second - first - 1);
        size_t consumed = 0;
        auto id = std::stoul(value, &consumed);
        if (consumed == value.size() && id > 0 && id <= UINT32_MAX) {
            record.id = uint32_t(id);
            record.boot = line.substr(second + 1);
        }
    } catch (...) {}
    return record;
}
inline std::string serializeRecord(const DisplayRecord& record) {
    if (!record.id || record.boot.empty()) return record.key;
    return record.key + '\t' + std::to_string(record.id) + '\t' + record.boot;
}
// A disabled display may not resolve by UUID. Reuse saved IDs only within the same boot.
// Refuse restoration if the ID is known to belong to another display.
template<typename Lookup, typename Identity>
uint32_t resolveRecord(const DisplayRecord& record, const std::string& boot, Lookup lookup, Identity identity) {
    auto current = lookup(record.key);
    if (current) return current;
    if (!record.id || boot.empty() || record.boot != boot) return 0;
    auto key = identity(record.id);
    return key.empty() || key == record.key ? record.id : 0;
}
}
