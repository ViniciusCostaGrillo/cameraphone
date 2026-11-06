#include "stream-client.hpp"
#include "discovery-manager.hpp"
#include "diagnostic-logger.hpp"
#include <chrono>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
static void close_sock(socket_t s) { if (s != INVALID_SOCKET) closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
static void close_sock(socket_t s) { if (s >= 0) close(s); }
#endif

namespace camphone {

static void ensure_winsock() {
#ifdef _WIN32
    static bool initialized = []() {
        WSADATA wsa{};
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    (void)initialized;
#endif
}

static socket_t connect_with_timeout(const std::string& host, uint16_t port, int timeout_ms = 2000) {
    ensure_winsock();
    socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalidSocket) return kInvalidSocket;

    int nodelay = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));

#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeout_ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        close_sock(s);
        return kInvalidSocket;
    }
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_sock(s);
        return kInvalidSocket;
    }
    return s;
}

static bool recv_exact(socket_t s, uint8_t* dst, size_t len) {
    size_t total = 0;
    while (total < len) {
        int r = recv(s, reinterpret_cast<char*>(dst + total), static_cast<int>(len - total), 0);
        if (r <= 0) return false;
        total += static_cast<size_t>(r);
    }
    return true;
}

StreamClient::StreamClient() = default;
StreamClient::~StreamClient() { stop_streaming(); }

static std::string extract_json_field(const std::string& src, const std::string& key) {
    std::string pattern = "\"" + key + "\":\"";
    auto pos = src.find(pattern);
    if (pos == std::string::npos) return "";
    pos += pattern.size();
    auto end = src.find('"', pos);
    if (end == std::string::npos) return "";
    return src.substr(pos, end - pos);
}

bool StreamClient::parse_capabilities_json(const std::string& json, DeviceCapabilities& out) {
    if (json.find("\"deviceModel\"") == std::string::npos) return false;
    out.device_model = extract_json_field(json, "deviceModel");
    out.soc = extract_json_field(json, "soc");
    out.supports_hevc = json.find("\"hevc\"") != std::string::npos;
    out.supports_av1 = json.find("\"av1\"") != std::string::npos;
    out.supports_hdr10 = (json.find("\"hdr10\"") != std::string::npos) || (json.find("\"hlg10\"") != std::string::npos);

    out.lenses.clear();
    size_t cursor = json.find("\"lenses\"");
    while (cursor != std::string::npos) {
        auto id_pos = json.find("\"id\":\"", cursor);
        if (id_pos == std::string::npos) break;
        std::string sub = json.substr(id_pos, 320);
        LensInfo l{};
        l.id = extract_json_field(sub, "id");
        l.label = extract_json_field(sub, "label");
        l.facing = extract_json_field(sub, "facing");
        out.lenses.push_back(l);
        cursor = id_pos + 6;
    }
    return true;
}

bool StreamClient::fetch_capabilities(const std::string& host, uint16_t port, const std::string& pin, DeviceCapabilities& out) {
    socket_t s = connect_with_timeout(host, port, 1500);
    if (s == kInvalidSocket) {
        DiagnosticLogger::warn("StreamClient", "fetch_capabilities failed to connect to " + host + ":" + std::to_string(port));
        return false;
    }

    std::ostringstream req;
    req << "GET /v1/capabilities HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Authorization: Bearer " << pin << "\r\n"
        << "Connection: close\r\n\r\n";
    std::string rstr = req.str();
    send(s, rstr.data(), static_cast<int>(rstr.size()), 0);

    std::string resp;
    char buf[2048];
    int r = 0;
    while ((r = recv(s, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, static_cast<size_t>(r));
    }
    close_sock(s);

    if (resp.find("401 Unauthorized") != std::string::npos) {
        DiagnosticLogger::error("StreamClient", "fetch_capabilities rejected with 401 Unauthorized on " + host + " (verify Wi-Fi 6-digit PIN)");
        return false;
    }
    if (resp.find("429 Too Many Requests") != std::string::npos) {
        DiagnosticLogger::error("StreamClient", "fetch_capabilities rejected with 429 Rate Limited on " + host);
        return false;
    }
    if (resp.find("200 OK") == std::string::npos) {
        DiagnosticLogger::warn("StreamClient", "fetch_capabilities non-200 response from " + host + ":" + std::to_string(port));
        return false;
    }
    auto body_pos = resp.find("\r\n\r\n");
    if (body_pos == std::string::npos) return false;
    bool ok = parse_capabilities_json(resp.substr(body_pos + 4), out);
    if (ok) {
        DiagnosticLogger::info("StreamClient", "Connected to " + out.device_model + " (" + out.soc + ") at " + host + ":" + std::to_string(port));
    }
    return ok;
}

bool StreamClient::fetch_remote_logs(const std::string& host, uint16_t port, const std::string& pin, std::string& out_logs) {
    socket_t s = connect_with_timeout(host, port, 1500);
    if (s == kInvalidSocket) {
        return false;
    }

    std::ostringstream req;
    req << "GET /v1/logs HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Authorization: Bearer " << pin << "\r\n"
        << "Connection: close\r\n\r\n";
    std::string rstr = req.str();
    send(s, rstr.data(), static_cast<int>(rstr.size()), 0);

    std::string resp;
    char buf[4096];
    int r = 0;
    while ((r = recv(s, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, static_cast<size_t>(r));
    }
    close_sock(s);

    if (resp.find("200 OK") == std::string::npos) return false;
    auto body_pos = resp.find("\r\n\r\n");
    if (body_pos == std::string::npos) return false;
    std::string body = resp.substr(body_pos + 4);

    // Format JSON array of log strings into readable multi-line text
    std::ostringstream formatted;
    size_t pos = body.find('[');
    if (pos == std::string::npos) {
        out_logs = body;
        return true;
    }
    bool in_str = false;
    bool esc = false;
    std::string cur;
    for (size_t i = pos + 1; i < body.size(); ++i) {
        char c = body[i];
        if (!in_str) {
            if (c == '"') {
                in_str = true;
                cur.clear();
            } else if (c == ']') {
                break;
            }
        } else {
            if (esc) {
                cur.push_back(c);
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else if (c == '"') {
                in_str = false;
                formatted << "  [APK] " << cur << "\r\n";
            } else {
                cur.push_back(c);
            }
        }
    }
    out_logs = formatted.str();
    return true;
}

bool StreamClient::send_control_patch(const std::string& host, uint16_t port, const std::string& pin, const std::string& json_patch) {
    socket_t s = connect_with_timeout(host, port, 1000);
    if (s == kInvalidSocket) return false;

    std::ostringstream req;
    req << "PATCH /v1/control HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Authorization: Bearer " << pin << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << json_patch.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << json_patch;
    std::string rstr = req.str();
    send(s, rstr.data(), static_cast<int>(rstr.size()), 0);
    close_sock(s);
    return true;
}

bool StreamClient::send_tally_state(const std::string& host, uint16_t port, const std::string& pin, const std::string& state) {
    socket_t s = connect_with_timeout(host, port, 1000);
    if (s == kInvalidSocket) return false;

    std::string body = "{\"state\":\"" + state + "\"}";
    std::ostringstream req;
    req << "PUT /v1/tally HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Authorization: Bearer " << pin << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    std::string rstr = req.str();
    send(s, rstr.data(), static_cast<int>(rstr.size()), 0);
    close_sock(s);
    return true;
}

void StreamClient::start_streaming(const StreamConfig& cfg, BoundedFrameQueue& video_queue, BoundedFrameQueue& audio_queue) {
    stop_streaming();
    running_.store(true);
    worker_ = std::thread([this, cfg, &video_queue, &audio_queue]() {
        uint64_t rx_packets = 0;
        while (running_.load()) {
            socket_t s = connect_with_timeout(cfg.host, cfg.port, 2500);
            if (s == kInvalidSocket) {
                connected_.store(false);
                if (cfg.host == "127.0.0.1" && cfg.pin == "usb-zero-config") {
                    DiscoveryManager::ensure_usb_tunnel_ready(cfg.port, cfg.port, true);
                }
                for (int i = 0; i < 8 && running_.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                continue;
            }

            std::ostringstream req;
            req << "GET /v1/stream?lens=" << cfg.lens
                << "&res=" << cfg.width << "x" << cfg.height
                << "&fps=" << cfg.fps
                << "&codec=" << cfg.codec
                << "&hdr=" << (cfg.hdr ? "1" : "0")
                << " HTTP/1.1\r\n"
                << "Host: " << cfg.host << "\r\n"
                << "Authorization: Bearer " << cfg.pin << "\r\n\r\n";
            std::string rstr = req.str();
            if (send(s, rstr.data(), static_cast<int>(rstr.size()), 0) <= 0) {
                close_sock(s);
                continue;
            }

            // Register active socket for immediate unblocking on stop_streaming()
            active_sock_.store(static_cast<uintptr_t>(s));

            // Read HTTP response header until \r\n\r\n
            std::string http_hdr;
            char ch = 0;
            while (running_.load() && http_hdr.find("\r\n\r\n") == std::string::npos && http_hdr.size() < 4096) {
                if (recv(s, &ch, 1, 0) <= 0) break;
                http_hdr.push_back(ch);
            }
            if (http_hdr.find("200 OK") == std::string::npos) {
                if (http_hdr.find("429") != std::string::npos || http_hdr.find("Rate Limited") != std::string::npos) {
                    DiagnosticLogger::warn("StreamClient", "GET /v1/stream rate limited (429) on " + cfg.host + " - backing off 5s");
                    close_sock(s);
                    active_sock_.store(static_cast<uintptr_t>(kInvalidSocket));
                    connected_.store(false);
                    // Back off 5s so phone rate limit lockout can reset
                    for (int i = 0; i < 50 && running_.load(); ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    continue;
                } else if (cfg.host == "127.0.0.1" && cfg.pin == "usb-zero-config" && http_hdr.empty()) {
                    DiscoveryManager::ensure_usb_tunnel_ready(cfg.port, cfg.port, true);
                } else {
                    DiagnosticLogger::warn("StreamClient", "GET /v1/stream rejected on " + cfg.host + ": " + http_hdr.substr(0, http_hdr.find('\r')));
                }
                close_sock(s);
                active_sock_.store(static_cast<uintptr_t>(kInvalidSocket));
                connected_.store(false);
                for (int i = 0; i < 12 && running_.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                continue;
            }

            DiagnosticLogger::info("StreamClient", "Binary stream connected to " + cfg.host + ":" + std::to_string(cfg.port) + " (" + cfg.codec + " " + std::to_string(cfg.width) + "x" + std::to_string(cfg.height) + ")");
            connected_.store(true);
            std::array<uint8_t, kHeaderSize> wire_hdr{};
            while (running_.load()) {
                if (!recv_exact(s, wire_hdr.data(), kHeaderSize)) {
                    DiagnosticLogger::warn("StreamClient", "Stream socket closed or timed out waiting for packet header from " + cfg.host);
                    break;
                }
                PacketHeader pkt_hdr{};
                auto st = deserialize_header(wire_hdr, pkt_hdr);
                if (st != ParseStatus::Ok) {
                    DiagnosticLogger::error("StreamClient", "Invalid CamPhoneWire header status=" + std::to_string(static_cast<int>(st)));
                    break;
                }
                std::vector<uint8_t> payload(pkt_hdr.payload_size);
                if (pkt_hdr.payload_size > 0 && !recv_exact(s, payload.data(), pkt_hdr.payload_size)) {
                    DiagnosticLogger::warn("StreamClient", "Stream socket closed while reading payload (" + std::to_string(pkt_hdr.payload_size) + " bytes)");
                    break;
                }
                ++rx_packets;
                if (rx_packets == 1 || rx_packets % 120 == 0) {
                    DiagnosticLogger::info("StreamClient", "Received packet #" + std::to_string(rx_packets) + " type=" + std::to_string(static_cast<int>(pkt_hdr.stream_type)) + " bytes=" + std::to_string(pkt_hdr.payload_size));
                }
                FramePacket fp{pkt_hdr.stream_type, pkt_hdr.flags, pkt_hdr.pts_us, std::move(payload)};
                if (pkt_hdr.stream_type == StreamType::Video) {
                    video_queue.push(std::move(fp));
                } else if (pkt_hdr.stream_type == StreamType::Audio) {
                    audio_queue.push(std::move(fp));
                }
            }
            connected_.store(false);
            active_sock_.store(static_cast<uintptr_t>(kInvalidSocket));
            close_sock(s);
        }
    });
}

void StreamClient::stop_streaming() {
    running_.store(false);
    connected_.store(false);
    uintptr_t raw = active_sock_.exchange(static_cast<uintptr_t>(kInvalidSocket));
    if (raw != static_cast<uintptr_t>(kInvalidSocket)) {
        socket_t s = static_cast<socket_t>(raw);
#ifdef _WIN32
        shutdown(s, SD_BOTH);
        closesocket(s);
#else
        shutdown(s, SHUT_RDWR);
        close(s);
#endif
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

} // namespace camphone
