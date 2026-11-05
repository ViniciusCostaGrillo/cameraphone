#include "../src/stream-client.hpp"
#include "../src/media-decoder.hpp"
#include "../src/diagnostic-logger.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <atomic>

using namespace camphone;

int main(int argc, char** argv) {
    std::string host = (argc > 1) ? argv[1] : "127.0.0.1";
    std::string pin  = (argc > 2) ? argv[2] : "000000";
    std::string init_codec = (argc > 3) ? argv[3] : "hevc";

    std::cout << "[E2E] Connecting OBS CameraPhone client to Android Emulator at " << host << ":4748 (pin=" << pin << ", init_codec=" << init_codec << ")...\n";

    StreamClient client;
    DeviceCapabilities caps{};
    bool caps_ok = client.fetch_capabilities(host, 4748, pin, caps);
    assert(caps_ok && "Failed to fetch /v1/capabilities from live Android Emulator JVM server!");
    std::cout << "[E2E] Fetched capabilities from Android Emulator: model='" << caps.device_model << "', lenses=" << caps.lenses.size() << "\n";

    MediaDecoder decoder;
    bool dec_ok = decoder.initialize_video(init_codec);
    assert(dec_ok && "Failed to initialize MediaDecoder with OBS Studio's avcodec-62.dll / avutil-60.dll!");

    BoundedFrameQueue v_queue;
    BoundedFrameQueue a_queue;

    StreamConfig cfg{};
    cfg.host   = host;
    cfg.port   = 4748;
    cfg.pin    = pin;
    cfg.lens   = "0";
    cfg.width  = 1920;
    cfg.height = 1080;
    cfg.fps    = 60;
    cfg.codec  = init_codec;
    cfg.hdr    = false;

    client.start_streaming(cfg, v_queue, a_queue);

    std::atomic<int> packets_received{0};
    std::atomic<int> frames_decoded{0};
    std::atomic<int> last_width{0};
    std::atomic<int> last_height{0};
    std::atomic<int> last_pix_fmt{-1};
    std::atomic<int> non_zero_y_samples{0};

    auto start_tp = std::chrono::steady_clock::now();
    while (frames_decoded.load() < 15) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_tp).count();
        if (elapsed > 5000) break;

        auto pkt_opt = v_queue.pop();
        if (pkt_opt.has_value()) {
            const FramePacket& pkt = *pkt_opt;
            packets_received++;
            decoder.decode_video_packet(pkt, [&](const DecodedVideoFrame& vf) {
                frames_decoded++;
                last_width = vf.width;
                last_height = vf.height;
                last_pix_fmt = vf.av_pix_fmt;
                if (vf.data[0] && vf.linesize[0] >= vf.width) {
                    if (vf.data[0][0] > 0 && vf.data[0][vf.width / 2] > 0) {
                        non_zero_y_samples++;
                    }
                }
            });
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // Test camera control patch, tally light, and remote diagnostic log fetch on live Android Emulator
    bool ctrl_ok = client.send_control_patch(host, 4748, pin, "{\"lens\":\"0\",\"zoom\":2.0,\"iso\":400,\"torch\":false}");
    bool tally_ok = client.send_tally_state(host, 4748, pin, "program");
    std::string android_logs;
    bool logs_ok = client.fetch_remote_logs(host, 4748, pin, android_logs);

    client.stop_streaming();
    decoder.shutdown();

    std::cout << "[E2E] Stream summary: packets_received=" << packets_received.load()
              << ", frames_decoded=" << frames_decoded.load()
              << ", resolution=" << last_width.load() << "x" << last_height.load()
              << ", av_pix_fmt=" << last_pix_fmt.load()
              << ", non_zero_y_samples=" << non_zero_y_samples.load()
              << ", ctrl_ok=" << ctrl_ok << ", tally_ok=" << tally_ok << ", logs_ok=" << logs_ok << "\n";
    std::cout << "[E2E] Android /v1/logs response: " << android_logs.substr(0, 300) << "\n";

    assert(packets_received.load() >= 15 && "Did not receive enough video packets from Android Emulator!");
    assert(frames_decoded.load() >= 10 && "MediaDecoder (avcodec-62.dll) did not decode enough frames!");
    assert(last_width.load() == 1920 && last_height.load() == 1080 && "Decoded frame resolution mismatch!");
    assert(non_zero_y_samples.load() >= 10 && "Decoded Y plane pixels were zero!");
    assert(ctrl_ok && "PATCH /v1/control failed!");
    assert(tally_ok && "PUT /v1/tally failed!");
    assert(logs_ok && android_logs.find("Emulator") != std::string::npos && "GET /v1/logs missing Emulator log entries!");

    std::cout << "[PASS] End-to-End Android Emulator <-> OBS Plugin (cameraphone.dll + avcodec-62.dll) verified!\n";
    return 0;
}
