#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif
#endif

namespace camphone {

class DiagnosticLogger {
public:
    static std::filesystem::path log_file_path() {
        namespace fs = std::filesystem;
#ifdef _WIN32
        char appdata[MAX_PATH]{};
        DWORD len = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            fs::path dir = fs::path(appdata) / "CameraPhone";
            std::error_code ec;
            fs::create_directories(dir, ec);
            return dir / "cameraphone.log";
        }
#endif
        return fs::current_path() / "cameraphone.log";
    }

    static void log(const char* level, const char* component, const std::string& message) {
        static std::mutex mtx;
        std::lock_guard<std::mutex> lock(mtx);

        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        std::ostringstream line;
        line << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
             << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
             << "[" << level << "] [" << component << "] " << message;
        const std::string formatted = line.str();

        // 1. Write to persistent log file (%APPDATA%\CameraPhoneOBS\cameraphone-obs.log)
        std::ofstream ofs(log_file_path(), std::ios::app);
        if (ofs.is_open()) {
            ofs << formatted << "\n";
        }

#ifdef _WIN32
        // 2. Write into OBS Studio's native log if obs.dll is loaded in this process
        using fn_blog = void (*)(int, const char*, ...);
        static fn_blog s_blog = []() -> fn_blog {
            HMODULE h = GetModuleHandleA("obs.dll");
            return h ? reinterpret_cast<fn_blog>(GetProcAddress(h, "blog")) : nullptr;
        }();
        if (s_blog) {
            int obs_level = 300; // LOG_INFO
            if (std::string(level) == "ERR ") obs_level = 100; // LOG_ERROR
            else if (std::string(level) == "WARN") obs_level = 200; // LOG_WARNING
            s_blog(obs_level, "[CameraPhone][%s] %s", component, message.c_str());
        }
#endif
    }

    static void info(const char* component, const std::string& msg) { log("INFO", component, msg); }
    static void warn(const char* component, const std::string& msg) { log("WARN", component, msg); }
    static void error(const char* component, const std::string& msg) { log("ERR ", component, msg); }

    static std::string read_tail(size_t max_bytes = 12288) {
        std::ifstream ifs(log_file_path(), std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) {
            return "[Log] Arquivo de log ainda vazio.";
        }
        auto end_pos = ifs.tellg();
        if (end_pos <= 0) return "[Log] Sem eventos registrados.";
        size_t sz = static_cast<size_t>(end_pos);
        size_t start_offset = (sz > max_bytes) ? (sz - max_bytes) : 0;
        ifs.seekg(static_cast<std::streamoff>(start_offset), std::ios::beg);
        std::string buf(sz - start_offset, '\0');
        ifs.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        return buf;
    }
};

} // namespace camphone
