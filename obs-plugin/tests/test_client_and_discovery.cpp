#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include "../src/discovery-manager.hpp"
#include "../src/stream-client.hpp"
#include "../../protocol/vcam_shmem.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

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

void test_obs_stream_guard_arbitration_and_client_running_state() {
    assert(OBSStreamGuard::is_obs_streaming() == false);
    {
        OBSStreamGuard guard;
        guard.acquire();
        assert(OBSStreamGuard::is_obs_streaming() == true);

        // Also verify from a separate worker thread (simulating UsbBroadcastFeeder)
        bool other_thread_sees_obs = false;
        std::thread t([&]() {
            other_thread_sees_obs = OBSStreamGuard::is_obs_streaming();
        });
        t.join();
        assert(other_thread_sees_obs == true);

        guard.release();
        assert(OBSStreamGuard::is_obs_streaming() == false);
    }

    StreamClient client;
    assert(client.is_running() == false);
    assert(client.is_connected() == false);
}

void test_stream_client_handshake_and_steady_streaming() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listen_sock != INVALID_SOCKET);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // Ephemeral port
    int b = bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    assert(b == 0);
    int l = listen(listen_sock, 1);
    assert(l == 0);

    int addr_len = sizeof(addr);
    getsockname(listen_sock, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    uint16_t port = ntohs(addr.sin_port);

    std::atomic<bool> server_done{false};
    std::thread server_thread([&]() {
        SOCKET client_sock = accept(listen_sock, nullptr, nullptr);
        if (client_sock == INVALID_SOCKET) return;

        // Read HTTP request
        char req_buf[1024]{};
        recv(client_sock, req_buf, sizeof(req_buf), 0);

        // Simulate 200ms Android Camera2 + MediaCodec startup delay
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        const char* resp = "HTTP/1.1 200 OK\r\nContent-Type: application/x-camphone-stream\r\n\r\n";
        send(client_sock, resp, static_cast<int>(std::strlen(resp)), 0);

        // Send 3 video packets
        for (int i = 0; i < 3; ++i) {
            PacketHeader hdr{};
            hdr.version = kProtocolVersion;
            hdr.stream_type = StreamType::Video;
            hdr.flags = (i == 0) ? FlagKeyframe : 0;
            hdr.pts_us = static_cast<uint64_t>((i + 1) * 16666);
            hdr.payload_size = 4;
            std::array<uint8_t, kHeaderSize> wire{};
            serialize_header(hdr, wire);
            uint8_t payload[4] = {0x00, 0x00, 0x01, static_cast<uint8_t>(0x65 + i)};
            send(client_sock, reinterpret_cast<const char*>(wire.data()), static_cast<int>(wire.size()), 0);
            send(client_sock, reinterpret_cast<const char*>(payload), 4, 0);
        }

        while (!server_done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        closesocket(client_sock);
    });

    BoundedFrameQueue vq{16};
    BoundedFrameQueue aq{16};
    StreamClient client;
    StreamConfig cfg{};
    cfg.host = "127.0.0.1";
    cfg.port = port;
    cfg.pin = "usb-zero-config";

    client.start_streaming(cfg, vq, aq);
    assert(client.is_running() == true);

    // Wait up to 4000ms for connection handshake (exact logic used in UsbBroadcastFeeder)
    for (int wait_step = 0;
         wait_step < 80 && client.is_running() && !client.is_connected();
         ++wait_step) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(client.is_connected() == true);

    int received_packets = 0;
    for (int step = 0; step < 40 && received_packets < 3; ++step) {
        auto pkt = vq.pop();
        if (pkt.has_value()) {
            ++received_packets;
            assert(pkt->data.size() == 4);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    assert(received_packets == 3);
    assert(client.is_connected() == true);

    server_done.store(true);
    client.stop_streaming();
    closesocket(listen_sock);
    if (server_thread.joinable()) server_thread.join();
}

int main() {
    test_adb_serial_allowlist_blocks_command_injection();
    test_adb_devices_output_parser_and_direct_createprocess();
    test_capabilities_json_parser();
    test_obs_stream_guard_arbitration_and_client_running_state();
    test_stream_client_handshake_and_steady_streaming();
    assert(!DiscoveryManager::resolve_adb_path().empty());
    std::cout << "[PASS] OBS Plugin Discovery (CreateProcessA ADB), OBSStreamGuard & StreamClient handshake tests passed.\n";
    return 0;
}
