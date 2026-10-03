#include "display_record.h"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    using namespace dm::detail;
    require(isDisplayUUID("12345678-1234-5678-9ABC-DEF012345678"), "valid UUID must be accepted");
    require(isDisplayUUID("12345678-1234-5678-9abc-def012345678"), "lowercase UUID must be accepted");
    require(!isDisplayUUID("not-a-display-uuid"), "malformed test record must be rejected");
    require(!isDisplayUUID("G2345678-1234-5678-9ABC-DEF012345678"), "non-hexadecimal UUID must be rejected");
    require(!isDisplayUUID("12345678_1234-5678-9ABC-DEF012345678"), "wrong UUID separator must be rejected");
    require(!isDisplayUUID("12345678-1234-5678-9ABC-DEF012345678-extra"), "trailing data must be rejected");
    const DisplayRecord original{"display-uuid", 42, "boot-A"};
    auto stored = parseRecord(serializeRecord(original));
    require(stored.key == original.key && stored.id == 42 && stored.boot == "boot-A", "ID and boot must survive serialization");
    auto missing = [](const std::string&) -> uint32_t { return 0; };
    auto offline = [](uint32_t) { return std::string{}; };
    require(resolveRecord(stored, "boot-A", missing, offline) == 42, "disabled display must use saved ID when UUID lookup fails");
    require(resolveRecord(stored, "boot-B", missing, offline) == 0, "cached ID must not survive reboot");
    require(resolveRecord(stored, "", missing, offline) == 0, "unknown boot must reject cached ID");
    auto otherDisplay = [](uint32_t) { return std::string{"other-uuid"}; };
    require(resolveRecord(stored, "boot-A", missing, otherDisplay) == 0, "reused ID must not restore another display");
    auto connected = [](const std::string&) -> uint32_t { return 99; };
    require(resolveRecord(stored, "boot-B", connected, otherDisplay) == 99, "current UUID lookup must take precedence over old ID");
    auto legacy = parseRecord("display-uuid");
    require(legacy.key == "display-uuid" && legacy.id == 0, "legacy UUID-only record must remain readable");
    require(resolveRecord(legacy, "boot-A", missing, offline) == 0, "legacy records must not guess an ID");
    require(parseRecord("display-uuid\tinvalid\tboot-A").id == 0, "invalid ID must be ignored");
    require(parseRecord("display-uuid\t4294967296\tboot-A").id == 0, "overflow ID must be ignored");
    std::cout << "PASS: offline restore, persistence, reboot protection, ID reuse, current lookup, legacy format\n";
}
