#include "../src/discovery-manager.hpp"
#include "../src/stream-client.hpp"
#include <cassert>
#include <iostream>

using namespace camphone;

void test_adb_serial_allowlist_blocks_command_injection() {
    assert(DiscoveryManager::is_valid_adb_serial("R58N123ABCD") == true);
    assert(DiscoveryManager::is_valid_adb_serial("RQCX907DB8D") == true);
    assert(DiscoveryManager::is_valid_adb_serial("emulator-5554") == true);
    assert(DiscoveryManager::is_valid_adb_serial("192.168.1.50:5555") == true);
    assert(DiscoveryManager::is_valid_adb_serial("abc; calc.exe") == false);
    assert(DiscoveryManager::is_valid_adb_serial("abc && whoami") == false);
    assert(DiscoveryManager::is_valid_adb_serial("$(id)") == false);
    assert(DiscoveryManager::is_valid_adb_serial("") == false);
}

void test_adb_devices_output_parser_and_direct_createprocess() {
    const std::string raw =
        "* daemon not running; starting now at tcp:5037\r\n"
        "* daemon started successfully\r\n"
        "List of devices attached\r\n"
        "RQCX907DB8D\tdevice\r\n"
        "R58N999XXXX\tunauthorized\r\n"
        "emulator-5554\toffline\r\n\r\n";

    auto entries = DiscoveryManager::parse_adb_devices_output(raw);
    assert(entries.size() == 3);
    assert(entries[0].serial == "RQCX907DB8D" && entries[0].state == "device");
    assert(entries[1].serial == "R58N999XXXX" && entries[1].state == "unauthorized");
    assert(entries[2].serial == "emulator-5554" && entries[2].state == "offline");

    // Verify CreateProcessA executes adb.exe even when located inside "C:\Program Files\..." (with spaces!)
    std::string ver_out;
    bool ver_ok = DiscoveryManager::run_adb_command({"version"}, ver_out, 8000);
    assert(ver_ok && "run_adb_command({'version'}) failed via CreateProcessA!");
    assert(ver_out.find("Android Debug Bridge") != std::string::npos);
}

void test_capabilities_json_parser() {
    const std::string sample = R"JSON({
        "deviceModel":"Samsung S24 Ultra",
        "soc":"Snapdragon 8 Gen 3",
        "codecs":["hevc","av1","avc"],
        "hdrCapabilities":["hdr10","hlg10"],
        "lenses":[
            {"id":"0","facing":"back","focalLength35mmEq":24,"label":"Main Wide (24mm)","oisAvailable":true,"isoMin":50,"isoMax":6400,"expMinNs":100000,"expMaxNs":250000000,"maxZoom":10.0,"resolutions":[{"width":3840,"height":2160,"fps":[30,60]}]}
        ]
    })JSON";
    DeviceCapabilities caps{};
    bool ok = StreamClient::parse_capabilities_json(sample, caps);
    assert(ok);
    assert(caps.device_model == "Samsung S24 Ultra");
    assert(caps.supports_hevc == true);
    assert(caps.supports_av1 == true);
    assert(caps.supports_hdr10 == true);
    assert(!caps.lenses.empty());
    assert(caps.lenses[0].id == "0");
    assert(caps.lenses[0].label == "Main Wide (24mm)");
}

int main() {
    test_adb_serial_allowlist_blocks_command_injection();
    test_adb_devices_output_parser_and_direct_createprocess();
    test_capabilities_json_parser();
    assert(!DiscoveryManager::resolve_adb_path().empty());
    std::cout << "[PASS] OBS Plugin Discovery (CreateProcessA ADB) & StreamClient security tests passed.\n";
    return 0;
}
