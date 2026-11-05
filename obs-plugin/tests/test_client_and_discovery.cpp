#include "../src/discovery-manager.hpp"
#include "../src/stream-client.hpp"
#include <cassert>
#include <iostream>

using namespace camphone;

void test_adb_serial_allowlist_blocks_command_injection() {
    assert(DiscoveryManager::is_valid_adb_serial("R58N123ABCD") == true);
    assert(DiscoveryManager::is_valid_adb_serial("emulator-5554") == true);
    assert(DiscoveryManager::is_valid_adb_serial("192.168.1.50:5555") == true);
    assert(DiscoveryManager::is_valid_adb_serial("abc; calc.exe") == false);
    assert(DiscoveryManager::is_valid_adb_serial("abc && whoami") == false);
    assert(DiscoveryManager::is_valid_adb_serial("$(id)") == false);
    assert(DiscoveryManager::is_valid_adb_serial("") == false);
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
    test_capabilities_json_parser();
    assert(!DiscoveryManager::resolve_adb_path().empty());
    std::cout << "[PASS] OBS Plugin Discovery & StreamClient security tests passed.\n";
    return 0;
}
