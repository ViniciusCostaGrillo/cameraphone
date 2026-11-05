#include "cameraphone-source.hpp"
#include "discovery-manager.hpp"
#include "media-decoder.hpp"
#include "stream-client.hpp"
#include <atomic>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

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

// Minimal OBS Studio C ABI bindings loaded dynamically from obs.dll
struct obs_data;
struct obs_properties;
struct obs_property;
struct obs_source;
using obs_data_t = obs_data;
using obs_properties_t = obs_properties;
using obs_property_t = obs_property;
using obs_source_t = obs_source;

using obs_property_clicked_t = bool (*)(obs_properties_t* props, obs_property_t* property, void* data);
using obs_property_modified2_t = bool (*)(void* priv, obs_properties_t* props, obs_property_t* property, obs_data_t* settings);

enum obs_combo_type { OBS_COMBO_TYPE_INVALID, OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_TYPE_LIST };
enum obs_combo_format { OBS_COMBO_FORMAT_INVALID, OBS_COMBO_FORMAT_INT, OBS_COMBO_FORMAT_FLOAT, OBS_COMBO_FORMAT_STRING };
enum obs_text_type { OBS_TEXT_DEFAULT, OBS_TEXT_PASSWORD, OBS_TEXT_MULTILINE, OBS_TEXT_INFO };

struct obs_source_frame2_minimal {
    uint8_t* data[8];
    uint32_t linesize[8];
    uint32_t width;
    uint32_t height;
    uint64_t timestamp;
    int      format;
    int      range;
    float    color_matrix[16];
    float    color_range_min[3];
    float    color_range_max[3];
    bool     flip;
    uint8_t  flags;
    uint8_t  trc;
};

// Exact 0x198 (408-byte) OBS Studio v30 obs_source_info ABI verified via REA + Ghidra on droidcam-obs.dll
struct obs_source_info_v30 {
    const char* id;                                                   // 0x00
    int         type;                                                 // 0x08 (OBS_SOURCE_TYPE_INPUT = 0)
    uint32_t    output_flags;                                         // 0x0C (0x87 = VIDEO | AUDIO | ASYNC | DO_NOT_DUPLICATE)
    const char* (*get_name)(void* type_data);                         // 0x10
    void*       (*create)(obs_data_t* settings, obs_source_t* source);// 0x18
    void        (*destroy)(void* data);                               // 0x20
    uint32_t    (*get_width)(void* data);                             // 0x28
    uint32_t    (*get_height)(void* data);                            // 0x30
    void        (*get_defaults)(obs_data_t* settings);                // 0x38
    obs_properties_t* (*get_properties)(void* data);                  // 0x40
    void        (*update)(void* data, obs_data_t* settings);          // 0x48
    void        (*activate)(void* data);                              // 0x50
    void        (*deactivate)(void* data);                            // 0x58
    void        (*show)(void* data);                                  // 0x60
    void        (*hide)(void* data);                                  // 0x68
    uint8_t     _reserved_0x70_to_0x120[0x120 - 0x70];                // 0x70..0x11F
    int         icon_type;                                            // 0x120 (OBS_ICON_TYPE_CAMERA = 13)
    uint8_t     _reserved_0x124_to_0x198[0x198 - 0x124];              // 0x124..0x197
};
static_assert(sizeof(obs_source_info_v30) == 0x198, "obs_source_info_v30 must be exactly 0x198 (408) bytes");

// Dynamic function table resolved from obs.dll at runtime
struct ObsApi {
    obs_properties_t* (*obs_properties_create)() = nullptr;
    obs_property_t*   (*obs_properties_add_list)(obs_properties_t*, const char*, const char*, int, int) = nullptr;
    size_t            (*obs_property_list_add_string)(obs_property_t*, const char*, const char*) = nullptr;
    void              (*obs_property_list_clear)(obs_property_t*) = nullptr;
    obs_property_t*   (*obs_properties_add_text)(obs_properties_t*, const char*, const char*, int) = nullptr;
    obs_property_t*   (*obs_properties_add_bool)(obs_properties_t*, const char*, const char*) = nullptr;
    obs_property_t*   (*obs_properties_add_button2)(obs_properties_t*, const char*, const char*, obs_property_clicked_t, void*) = nullptr;
    obs_property_t*   (*obs_properties_add_float_slider)(obs_properties_t*, const char*, const char*, double, double, double) = nullptr;
    obs_property_t*   (*obs_properties_add_int_slider)(obs_properties_t*, const char*, const char*, int, int, int) = nullptr;
    obs_property_t*   (*obs_properties_get)(obs_properties_t*, const char*) = nullptr;
    void              (*obs_property_set_visible)(obs_property_t*, bool) = nullptr;
    void              (*obs_property_set_enabled)(obs_property_t*, bool) = nullptr;
    void              (*obs_property_set_description)(obs_property_t*, const char*) = nullptr;
    void              (*obs_property_set_modified_callback2)(obs_property_t*, obs_property_modified2_t, void*) = nullptr;
    const char*       (*obs_data_get_string)(obs_data_t*, const char*) = nullptr;
    bool              (*obs_data_get_bool)(obs_data_t*, const char*) = nullptr;
    double            (*obs_data_get_double)(obs_data_t*, const char*) = nullptr;
    long long         (*obs_data_get_int)(obs_data_t*, const char*) = nullptr;
    void              (*obs_data_set_string)(obs_data_t*, const char*, const char*) = nullptr;
    void              (*obs_data_set_default_string)(obs_data_t*, const char*, const char*) = nullptr;
    void              (*obs_data_set_default_double)(obs_data_t*, const char*, double) = nullptr;
    void              (*obs_data_set_default_int)(obs_data_t*, const char*, long long) = nullptr;
    obs_data_t*       (*obs_source_get_settings)(obs_source_t*) = nullptr;
    void              (*obs_data_release)(obs_data_t*) = nullptr;
    void              (*obs_source_output_video2)(obs_source_t*, const obs_source_frame2_minimal*) = nullptr;
    void              (*obs_source_output_video)(obs_source_t*, const obs_source_frame2_minimal*) = nullptr;
    void              (*obs_register_source_s)(const obs_source_info_v30*, size_t) = nullptr;

    static ObsApi& instance() {
        static ObsApi api = []() {
            ObsApi a{};
#ifdef _WIN32
            HMODULE h = GetModuleHandleA("obs.dll");
            if (!h) return a;
            a.obs_properties_create = reinterpret_cast<decltype(a.obs_properties_create)>(GetProcAddress(h, "obs_properties_create"));
            a.obs_properties_add_list = reinterpret_cast<decltype(a.obs_properties_add_list)>(GetProcAddress(h, "obs_properties_add_list"));
            a.obs_property_list_add_string = reinterpret_cast<decltype(a.obs_property_list_add_string)>(GetProcAddress(h, "obs_property_list_add_string"));
            a.obs_property_list_clear = reinterpret_cast<decltype(a.obs_property_list_clear)>(GetProcAddress(h, "obs_property_list_clear"));
            a.obs_properties_add_text = reinterpret_cast<decltype(a.obs_properties_add_text)>(GetProcAddress(h, "obs_properties_add_text"));
            a.obs_properties_add_bool = reinterpret_cast<decltype(a.obs_properties_add_bool)>(GetProcAddress(h, "obs_properties_add_bool"));
            a.obs_properties_add_button2 = reinterpret_cast<decltype(a.obs_properties_add_button2)>(GetProcAddress(h, "obs_properties_add_button2"));
            a.obs_properties_add_float_slider = reinterpret_cast<decltype(a.obs_properties_add_float_slider)>(GetProcAddress(h, "obs_properties_add_float_slider"));
            a.obs_properties_add_int_slider = reinterpret_cast<decltype(a.obs_properties_add_int_slider)>(GetProcAddress(h, "obs_properties_add_int_slider"));
            a.obs_properties_get = reinterpret_cast<decltype(a.obs_properties_get)>(GetProcAddress(h, "obs_properties_get"));
            a.obs_property_set_visible = reinterpret_cast<decltype(a.obs_property_set_visible)>(GetProcAddress(h, "obs_property_set_visible"));
            a.obs_property_set_enabled = reinterpret_cast<decltype(a.obs_property_set_enabled)>(GetProcAddress(h, "obs_property_set_enabled"));
            a.obs_property_set_description = reinterpret_cast<decltype(a.obs_property_set_description)>(GetProcAddress(h, "obs_property_set_description"));
            a.obs_property_set_modified_callback2 = reinterpret_cast<decltype(a.obs_property_set_modified_callback2)>(GetProcAddress(h, "obs_property_set_modified_callback2"));
            a.obs_data_get_string = reinterpret_cast<decltype(a.obs_data_get_string)>(GetProcAddress(h, "obs_data_get_string"));
            a.obs_data_get_bool = reinterpret_cast<decltype(a.obs_data_get_bool)>(GetProcAddress(h, "obs_data_get_bool"));
            a.obs_data_get_double = reinterpret_cast<decltype(a.obs_data_get_double)>(GetProcAddress(h, "obs_data_get_double"));
            a.obs_data_get_int = reinterpret_cast<decltype(a.obs_data_get_int)>(GetProcAddress(h, "obs_data_get_int"));
            a.obs_data_set_string = reinterpret_cast<decltype(a.obs_data_set_string)>(GetProcAddress(h, "obs_data_set_string"));
            a.obs_data_set_default_string = reinterpret_cast<decltype(a.obs_data_set_default_string)>(GetProcAddress(h, "obs_data_set_default_string"));
            a.obs_data_set_default_double = reinterpret_cast<decltype(a.obs_data_set_default_double)>(GetProcAddress(h, "obs_data_set_default_double"));
            a.obs_data_set_default_int = reinterpret_cast<decltype(a.obs_data_set_default_int)>(GetProcAddress(h, "obs_data_set_default_int"));
            a.obs_source_get_settings = reinterpret_cast<decltype(a.obs_source_get_settings)>(GetProcAddress(h, "obs_source_get_settings"));
            a.obs_data_release = reinterpret_cast<decltype(a.obs_data_release)>(GetProcAddress(h, "obs_data_release"));
            a.obs_source_output_video2 = reinterpret_cast<decltype(a.obs_source_output_video2)>(GetProcAddress(h, "obs_source_output_video2"));
            a.obs_source_output_video = reinterpret_cast<decltype(a.obs_source_output_video)>(GetProcAddress(h, "obs_source_output_video"));
            a.obs_register_source_s = reinterpret_cast<decltype(a.obs_register_source_s)>(GetProcAddress(h, "obs_register_source_s"));
#endif
            return a;
        }();
        return api;
    }
};

struct CameraPhoneSourceContext {
    obs_source_t*      source = nullptr;
    std::string        conn_mode = "usb"; // "usb" or "wifi"
    std::string        status_message = "Pronto. Selecione Cabo USB (Zero-Config) ou IP Wi-Fi.";
    StreamConfig       config{};
    DeviceCapabilities capabilities{};
    BoundedFrameQueue  video_queue{3};
    BoundedFrameQueue  audio_queue{8};
    StreamClient       client{};
    MediaDecoder       decoder{};
    std::atomic<bool>  decode_running{false};
    std::thread        decode_thread{};

    void start_pipeline() {
        stop_pipeline();
        if (conn_mode == "usb") {
            DiscoveryManager::list_usb_devices(config.port, config.port);
            config.host = "127.0.0.1";
            if (config.pin.empty()) {
                config.pin = "usb-zero-config";
            }
        }
        decoder.initialize_video(config.codec);
        decoder.initialize_audio();
        if (client.fetch_capabilities(config.host, config.port, config.pin, capabilities)) {
            status_message = "Conectado: " + capabilities.device_model + " (" + config.host + ":" + std::to_string(config.port) + ")";
        }
        client.start_streaming(config, video_queue, audio_queue);

        decode_running.store(true);
        decode_thread = std::thread([this]() {
            auto& api = ObsApi::instance();
            while (decode_running.load()) {
                auto vpkt = video_queue.pop();
                if (vpkt.has_value()) {
                    decoder.decode_video_packet(*vpkt, [&](const DecodedVideoFrame& vf) {
                        if (source && (api.obs_source_output_video2 || api.obs_source_output_video)) {
                            obs_source_frame2_minimal of{};
                            for (int i = 0; i < 4; ++i) {
                                of.data[i] = vf.data[i];
                                of.linesize[i] = static_cast<uint32_t>(vf.linesize[i]);
                            }
                            of.width = static_cast<uint32_t>(vf.width);
                            of.height = static_cast<uint32_t>(vf.height);
                            of.timestamp = vf.pts_ns;
                            of.format = vf.is_hdr_p010 ? 20 : 2; // VIDEO_FORMAT_P010 or VIDEO_FORMAT_NV12
                            if (api.obs_source_output_video2) {
                                api.obs_source_output_video2(source, &of);
                            } else {
                                api.obs_source_output_video(source, &of);
                            }
                        }
                    });
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        });
    }

    void stop_pipeline() {
        decode_running.store(false);
        client.stop_streaming();
        if (decode_thread.joinable()) {
            decode_thread.join();
        }
        decoder.shutdown();
    }
};

static const char* source_get_name(void*) {
    return "CameraPhone 4K Pro (Cabo USB / Wi-Fi IP)";
}

static void source_get_defaults(obs_data_t* settings) {
    auto& api = ObsApi::instance();
    if (!api.obs_data_set_default_string) return;
    api.obs_data_set_default_string(settings, "conn_mode", "usb");
    api.obs_data_set_default_string(settings, "usb_device", "127.0.0.1");
    api.obs_data_set_default_string(settings, "wifi_ip", "192.168.1.100");
    api.obs_data_set_default_string(settings, "pin", "");
    api.obs_data_set_default_string(settings, "resolution", "3840x2160");
    api.obs_data_set_default_string(settings, "fps", "60");
    api.obs_data_set_default_string(settings, "codec", "hevc");
    api.obs_data_set_default_string(settings, "lens", "0");
    api.obs_data_set_default_double(settings, "zoom", 1.0);
    api.obs_data_set_default_int(settings, "iso", 100);
    api.obs_data_set_default_int(settings, "wb_kelvin", 5600);
}

static bool on_conn_mode_modified(void* priv, obs_properties_t* props, obs_property_t*, obs_data_t* settings) {
    auto& api = ObsApi::instance();
    if (!api.obs_data_get_string || !api.obs_properties_get || !api.obs_property_set_visible) {
        return false;
    }
    const char* mode_c = api.obs_data_get_string(settings, "conn_mode");
    const bool is_usb = (!mode_c || std::string(mode_c) != "wifi");

    if (auto* p_usb = api.obs_properties_get(props, "usb_device")) {
        api.obs_property_set_visible(p_usb, is_usb);
    }
    if (auto* p_btn_apk = api.obs_properties_get(props, "btn_install_apk")) {
        // Button "Instalar APK no Celular" is ONLY active/visible when USB Cable mode is selected
        api.obs_property_set_visible(p_btn_apk, is_usb);
        if (api.obs_property_set_enabled) {
            api.obs_property_set_enabled(p_btn_apk, is_usb);
        }
    }
    if (auto* p_wifi = api.obs_properties_get(props, "wifi_ip")) {
        api.obs_property_set_visible(p_wifi, !is_usb);
    }
    if (auto* p_pin = api.obs_properties_get(props, "pin")) {
        // In USB mode, connection is Zero-Config (no PIN needed); in Wi-Fi mode, show PIN field
        api.obs_property_set_visible(p_pin, !is_usb);
    }
    (void)priv;
    return true;
}

static bool on_install_apk_clicked(obs_properties_t* props, obs_property_t*, void* data) {
    auto* ctx = static_cast<CameraPhoneSourceContext*>(data);
    auto& api = ObsApi::instance();
    std::string msg;
    bool ok = DiscoveryManager::install_apk_and_launch_usb(msg, 4748, 4748);
    if (ctx) {
        ctx->status_message = msg;
        if (ok) {
            ctx->conn_mode = "usb";
            ctx->config.host = "127.0.0.1";
            ctx->config.pin = "usb-zero-config";
            ctx->start_pipeline();
        }
    }
#ifdef _WIN32
    MessageBoxA(nullptr, msg.c_str(),
                ok ? "CameraPhone OBS - APK Instalado via Cabo USB" : "CameraPhone OBS - Aviso USB",
                ok ? (MB_OK | MB_ICONINFORMATION) : (MB_OK | MB_ICONWARNING));
#endif
    if (api.obs_properties_get && api.obs_property_set_description) {
        if (auto* st = api.obs_properties_get(props, "status_info")) {
            api.obs_property_set_description(st, msg.c_str());
        }
    }
    return true;
}

static bool on_connect_refresh_clicked(obs_properties_t* props, obs_property_t*, void* data) {
    auto* ctx = static_cast<CameraPhoneSourceContext*>(data);
    auto& api = ObsApi::instance();
    if (!ctx) return false;

    if (api.obs_properties_get && api.obs_property_list_clear && api.obs_property_list_add_string) {
        if (auto* dev_list = api.obs_properties_get(props, "usb_device")) {
            api.obs_property_list_clear(dev_list);
            auto devices = DiscoveryManager::list_usb_devices(4748, 4748);
            if (devices.empty()) {
                api.obs_property_list_add_string(dev_list, "USB Auto (Aguardando Celular no Cabo...)", "127.0.0.1");
            } else {
                for (const auto& d : devices) {
                    api.obs_property_list_add_string(dev_list, d.display_name.c_str(), d.host.c_str());
                }
            }
        }
    }

    ctx->start_pipeline();
    if (api.obs_properties_get && api.obs_property_set_description) {
        if (auto* st = api.obs_properties_get(props, "status_info")) {
            api.obs_property_set_description(st, ctx->status_message.c_str());
        }
    }
    return true;
}

static obs_properties_t* source_get_properties(void* data) {
    auto& api = ObsApi::instance();
    if (!api.obs_properties_create) return nullptr;
    auto* ctx = static_cast<CameraPhoneSourceContext*>(data);
    obs_properties_t* props = api.obs_properties_create();

    // 1. Connection Mode Selector (USB Cable vs Wi-Fi IP)
    auto* mode_list = api.obs_properties_add_list(
        props, "conn_mode", "Modo de Conexao", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    api.obs_property_list_add_string(mode_list, "Cabo USB (Zero-Config Automatico)", "usb");
    api.obs_property_list_add_string(mode_list, "Wi-Fi (Conectar por IP na Rede)", "wifi");
    if (api.obs_property_set_modified_callback2) {
        api.obs_property_set_modified_callback2(mode_list, on_conn_mode_modified, ctx);
    }

    // 2. USB Device Dropdown
    auto* dev_list = api.obs_properties_add_list(
        props, "usb_device", "Celular no Cabo USB", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    auto usb_devs = DiscoveryManager::list_usb_devices(4748, 4748);
    if (usb_devs.empty()) {
        api.obs_property_list_add_string(dev_list, "Automatico via Cabo USB (127.0.0.1)", "127.0.0.1");
    } else {
        for (const auto& d : usb_devs) {
            api.obs_property_list_add_string(dev_list, d.display_name.c_str(), d.host.c_str());
        }
    }

    // 3. One-Click "Instalar APK" Button (Only via USB Cable)
    if (api.obs_properties_add_button2) {
        api.obs_properties_add_button2(
            props, "btn_install_apk", "Instalar APK no Celular (Somente via Cabo USB)",
            on_install_apk_clicked, ctx);
    }

    // 4. Wi-Fi IP & PIN fields (Shown when Wi-Fi mode is selected)
    api.obs_properties_add_text(props, "wifi_ip", "IP do Celular (Wi-Fi)", OBS_TEXT_DEFAULT);
    api.obs_properties_add_text(props, "pin", "PIN de Seguranca Wi-Fi (6 digitos no App)", OBS_TEXT_PASSWORD);

    // 5. Connect / Refresh Button
    if (api.obs_properties_add_button2) {
        api.obs_properties_add_button2(
            props, "btn_connect", "Conectar / Atualizar Camera Agora",
            on_connect_refresh_clicked, ctx);
    }

    // 6. Camera Hardware & Lens Controls
    auto* lens_list = api.obs_properties_add_list(props, "lens", "Lente Fisica", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    if (ctx && !ctx->capabilities.lenses.empty()) {
        for (const auto& l : ctx->capabilities.lenses) {
            api.obs_property_list_add_string(lens_list, l.label.c_str(), l.id.c_str());
        }
    } else {
        api.obs_property_list_add_string(lens_list, "Main Wide (24mm Traseira)", "0");
        api.obs_property_list_add_string(lens_list, "Ultra-Wide (13mm Traseira)", "2");
        api.obs_property_list_add_string(lens_list, "Telephoto (70mm+ Traseira)", "3");
        api.obs_property_list_add_string(lens_list, "Camera Frontal", "1");
    }

    auto* res_list = api.obs_properties_add_list(props, "resolution", "Resolucao", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    api.obs_property_list_add_string(res_list, "4K UHD (3840x2160)", "3840x2160");
    api.obs_property_list_add_string(res_list, "QHD 1440p (2560x1440)", "2560x1440");
    api.obs_property_list_add_string(res_list, "Full HD 1080p (1920x1080)", "1920x1080");

    auto* fps_list = api.obs_properties_add_list(props, "fps", "Taxa de Quadros (FPS)", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    api.obs_property_list_add_string(fps_list, "120 FPS (High-Speed)", "120");
    api.obs_property_list_add_string(fps_list, "60 FPS", "60");
    api.obs_property_list_add_string(fps_list, "30 FPS", "30");

    auto* codec_list = api.obs_properties_add_list(props, "codec", "Codec de Hardware", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    api.obs_property_list_add_string(codec_list, "HEVC (H.265 Hardware)", "hevc");
    api.obs_property_list_add_string(codec_list, "AV1 (Hardware)", "av1");
    api.obs_property_list_add_string(codec_list, "AVC (H.264 Hardware)", "avc");

    api.obs_properties_add_bool(props, "hdr", "HDR 10-bit (HLG / BT.2020)");
    api.obs_properties_add_float_slider(props, "zoom", "Zoom Optico/Hibrido", 1.0, 10.0, 0.1);
    api.obs_properties_add_int_slider(props, "iso", "ISO Manual", 50, 6400, 50);
    api.obs_properties_add_int_slider(props, "wb_kelvin", "Balanco de Branco (Kelvin)", 2000, 10000, 100);
    api.obs_properties_add_bool(props, "torch", "Lanterna / LED Traseiro");

    return props;
}

static void source_update(void* data, obs_data_t* settings) {
    auto* ctx = static_cast<CameraPhoneSourceContext*>(data);
    auto& api = ObsApi::instance();
    if (!ctx || !api.obs_data_get_string) return;

    const char* mode_c  = api.obs_data_get_string(settings, "conn_mode");
    const char* usb_dev = api.obs_data_get_string(settings, "usb_device");
    const char* wifi_ip = api.obs_data_get_string(settings, "wifi_ip");
    const char* pin     = api.obs_data_get_string(settings, "pin");
    const char* lens    = api.obs_data_get_string(settings, "lens");
    const char* res     = api.obs_data_get_string(settings, "resolution");
    const char* fps     = api.obs_data_get_string(settings, "fps");
    const char* cdc     = api.obs_data_get_string(settings, "codec");
    bool hdr            = api.obs_data_get_bool ? api.obs_data_get_bool(settings, "hdr") : false;
    double zoom         = api.obs_data_get_double ? api.obs_data_get_double(settings, "zoom") : 1.0;
    long long iso       = api.obs_data_get_int ? api.obs_data_get_int(settings, "iso") : 100;
    long long wb        = api.obs_data_get_int ? api.obs_data_get_int(settings, "wb_kelvin") : 5600;
    bool torch          = api.obs_data_get_bool ? api.obs_data_get_bool(settings, "torch") : false;

    std::string new_mode = (mode_c && *mode_c) ? mode_c : "usb";
    std::string new_host = "127.0.0.1";
    std::string new_pin  = "usb-zero-config";

    if (new_mode == "wifi") {
        new_host = (wifi_ip && *wifi_ip) ? wifi_ip : "192.168.1.100";
        new_pin  = (pin && *pin) ? pin : "";
    } else {
        new_host = (usb_dev && *usb_dev) ? usb_dev : "127.0.0.1";
        new_pin  = (pin && *pin) ? pin : "usb-zero-config";
    }

    std::string new_lens = lens && *lens ? lens : "0";
    std::string new_cdc  = cdc && *cdc ? cdc : "hevc";
    int new_fps          = fps && *fps ? std::atoi(fps) : 60;
    int new_w = 3840, new_h = 2160;
    if (res && std::string(res) == "2560x1440") { new_w = 2560; new_h = 1440; }
    else if (res && std::string(res) == "1920x1080") { new_w = 1920; new_h = 1080; }

    bool stream_changed = (ctx->conn_mode != new_mode || ctx->config.host != new_host ||
                           ctx->config.pin != new_pin || ctx->config.width != new_w ||
                           ctx->config.height != new_h || ctx->config.fps != new_fps ||
                           ctx->config.codec != new_cdc || ctx->config.hdr != hdr);

    ctx->conn_mode     = new_mode;
    ctx->config.host   = new_host;
    ctx->config.pin    = new_pin;
    ctx->config.lens   = new_lens;
    ctx->config.width  = new_w;
    ctx->config.height = new_h;
    ctx->config.fps    = new_fps;
    ctx->config.codec  = new_cdc;
    ctx->config.hdr    = hdr;

    if (stream_changed && !new_pin.empty()) {
        ctx->start_pipeline();
    } else if (ctx->client.is_connected()) {
        std::ostringstream patch;
        patch << "{\"lens\":\"" << new_lens << "\""
              << ",\"zoom\":" << zoom
              << ",\"iso\":" << iso
              << ",\"wbKelvin\":" << wb
              << ",\"torch\":" << (torch ? "true" : "false") << "}";
        ctx->client.send_control_patch(ctx->config.host, ctx->config.port, ctx->config.pin, patch.str());
    }
}

static void* source_create(obs_data_t* settings, obs_source_t* source) {
    auto* ctx = new CameraPhoneSourceContext();
    ctx->source = source;
    source_update(ctx, settings);
    return ctx;
}

static void source_destroy(void* data) {
    auto* ctx = static_cast<CameraPhoneSourceContext*>(data);
    if (!ctx) return;
    ctx->stop_pipeline();
    delete ctx;
}

static uint32_t source_get_width(void*) {
    return 0; // Async video sources report dimensions via obs_source_output_video2 frames
}

static uint32_t source_get_height(void*) {
    return 0;
}

static void source_activate(void* data) {
    auto* ctx = static_cast<CameraPhoneSourceContext*>(data);
    if (ctx && !ctx->config.pin.empty()) {
        ctx->client.send_tally_state(ctx->config.host, ctx->config.port, ctx->config.pin, "program");
    }
}

static void source_deactivate(void* data) {
    auto* ctx = static_cast<CameraPhoneSourceContext*>(data);
    if (ctx && !ctx->config.pin.empty()) {
        ctx->client.send_tally_state(ctx->config.host, ctx->config.port, ctx->config.pin, "preview");
    }
}

void register_cameraphone_source() {
    auto& api = ObsApi::instance();
    if (!api.obs_register_source_s) return;

    static obs_source_info_v30 info{};
    std::memset(&info, 0, sizeof(info));
    info.id             = "cameraphone_obs_source";
    info.type           = 0;    // OBS_SOURCE_TYPE_INPUT
    info.output_flags   = 0x87; // OBS_SOURCE_VIDEO (1) | OBS_SOURCE_AUDIO (2) | OBS_SOURCE_ASYNC (4) | OBS_SOURCE_DO_NOT_DUPLICATE (0x80)
    info.get_name       = source_get_name;
    info.create         = source_create;
    info.destroy        = source_destroy;
    info.get_width      = source_get_width;
    info.get_height     = source_get_height;
    info.get_defaults   = source_get_defaults;
    info.get_properties = source_get_properties;
    info.update         = source_update;
    info.activate       = source_activate;
    info.deactivate     = source_deactivate;
    info.icon_type      = 13;   // OBS_ICON_TYPE_CAMERA (0x0d)

    api.obs_register_source_s(&info, sizeof(info));
}

} // namespace camphone
