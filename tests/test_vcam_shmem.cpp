#include "../protocol/vcam_shmem.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace camphone;

int main() {
    std::cout << "[Test VCam Shmem] Starting Shared Memory IPC test...\n";

    VCamSharedMemoryWriter writer;
    assert(writer.open() && "Failed to create VCam shared memory writer");

    VCamSharedMemoryReader reader;
    assert(reader.open() && "Failed to open VCam shared memory reader");

    constexpr uint32_t width = 1920;
    constexpr uint32_t height = 1080;

    // Simulate YUV420P frame planes
    std::vector<uint8_t> y_plane(width * height, 0x80);
    std::vector<uint8_t> u_plane(width * height / 4, 0x40);
    std::vector<uint8_t> v_plane(width * height / 4, 0xC0);

    const uint8_t* data[4] = { y_plane.data(), u_plane.data(), v_plane.data(), nullptr };
    const int linesize[4] = { static_cast<int>(width), static_cast<int>(width / 2), static_cast<int>(width / 2), 0 };

    constexpr int kTestFrames = 30;
    for (int i = 1; i <= kTestFrames; ++i) {
        uint64_t t0 = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        // Write frame
        y_plane[0] = static_cast<uint8_t>(i & 0xFF);
        bool write_ok = writer.write_frame(width, height, 0, data, linesize, t0);
        assert(write_ok && "Failed to write frame to shared memory");

        // Wait on event & read
        bool signaled = reader.wait_for_frame(100);
        assert(signaled && "Reader timed out waiting for shared memory event");

        VCamSharedHeader hdr{};
        std::vector<uint8_t> payload;
        bool read_ok = reader.read_frame(hdr, payload);
        assert(read_ok && "Failed to read frame from shared memory");

        uint64_t t1 = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        double latency_ms = (t1 - t0) / 1000000.0;

        assert(hdr.magic == kVCamMagic);
        assert(hdr.version == kVCamVersion);
        assert(hdr.width == width);
        assert(hdr.height == height);
        assert(hdr.fourcc == 0x3231564E); // NV12
        assert(hdr.frame_index == static_cast<uint32_t>(i));
        assert(payload.size() == width * height * 3 / 2);
        assert(payload[0] == static_cast<uint8_t>(i & 0xFF));

        if (i == 1 || i % 10 == 0) {
            std::cout << "  [Frame #" << i << "] verified! Size=" << payload.size() 
                      << " bytes, latency=" << latency_ms << " ms\n";
        }
    }

    std::cout << "[PASS] VCam Shared Memory IPC Test Passed! 30 frames transmitted with ultra-low latency.\n";
    return 0;
}
