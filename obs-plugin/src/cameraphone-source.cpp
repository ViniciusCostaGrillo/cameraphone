#include "cameraphone-source.hpp"
#include "discovery-manager.hpp"
#include "media-decoder.hpp"
#include "stream-client.hpp"
#include <atomic>
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

enum obs_combo_type { OBS_COMBO_TYPE_INVALID, OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_TYPE_LIST };
enum obs_combo_format { OBS_COMBO_FORMAT_INVALID, OBS_COMBO_FORMAT_INT, OBS_COMBO_FORMAT_FLOAT, OBS_COMBO_FORMAT_STRING };
enum obs_text_type { OBS_TEXT_DEFAULT, OBS_TEXT_PASSWORD, OBS_TEXT_MULTILINE };

struct obs_source_frame_minimal {
    uint8_t* data[8];
    uint32_t linesize[8];
    uint32_t width;
    uint32_t height;
    uint64_t timestamp;
    int      format;
};

struct obs_source_info_minimal {
    const char* id;
    int         type;         // OBS_SOURCE_TYPE_INPUT = 0
    uint32_t    output_flags; // OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE
    const char* (*get_name)(void* type_data);
    void*       (*create)(obs_data_t* settings, obs_source_t* source);
    void        (*destroy)(void* data);
    uint32_t    (*get_width)(void* data);
    uint32_t    (*get_height)(void* data);
    void        (*get_defaults)(obs_data_t* settings);
    obs_properties_t* (*get_properties)(void* data);
    void        (*update)(void* data, obs_data_t* settings);
    void        (*activate)(void* data);
    void        (*deactivate)(void* data);
};

// Dynamic function table resolved from obs.dll at runtime
struct ObsApi {
    obs_properties_t* (*obs_properties_create)() = nullptr;
    obs_property_t*   (*obs_properties_add_list)(obs_properties_t*, const char*, const char*, int, int) = nullptr;
    size_t            (*obs_property_list_add_string)(obs_property_t*, const char*, const char*) = nullptr;
    obs_property_t*   (*obs_properties_add_text)(obs_properties_t*, const char*, const char*, int) = nullptr;
    obs_property_t*   (*obs_properties_add_bool)(obs_properties_t*, const char*, const char*) = nullptr;
    obs_property_t*   (*obs_properties_add_float_slider)(obs_properties_t*, const char*, const char*, double, double, double) = nullptr;
    obs_property_t*   (*obs_properties_add_int_slider)(obs_properties_t*, const char*, const char*, int, int, int) = nullptr;
    const char*       (*obs_data_get_string)(obs_data_t*, const char*) = nullptr;
    bool              (*obs_data_get_bool)(obs_data_t*, const char*) = nullptr;
    double            (*obs_data_get_double)(obs_data_t*, const char*) = nullptr;
    long long         (*obs_data_get_int)(obs_data_t*, const char*) = nullptr;
    void              (*obs_data_set_default_string)(obs_data_t*, const char*, const char*) = nullptr;
    void              (*obs_data_set_default_double)(obs_data_t*, const char*, double) = nullptr;
    void              (*obs_data_set_default_int)(obs_data_t*, const char*, long long) = nullptr;
    void              (*obs_source_output_video)(obs_source_t*, const obs_source_frame_minimal*) = nullptr;
    void              (*obs_register_source_s)(const obs_source_info_minimal*, size_t) = nullptr;

    static ObsApi& instance() {
        static ObsApi api = []() {
            ObsApi a{};
#ifdef _WIN32
            HMODULE h = GetModuleHandleA("obs.dll");
            if (!h) return a;
            a.obs_properties_create = reinterpret_cast<decltype(a.obs_properties_create)>(GetProcAddress(h, "obs_properties_create"));
            a.obs_properties_add_list = reinterpret_cast<decltype(a.obs_properties_add_list)>(GetProcAddress(h, "obs_properties_add_list"));
            a.obs_property_list_add_string = reinterpret_cast<decltype(a.obs_property_list_add_string)>(GetProcAddress(h, "obs_property_list_add_string"));
            a.obs_properties_add_text = reinterpret_cast<decltype(a.obs_properties_add_text)>(GetProcAddress(h, "obs_properties_add_text"));
            a.obs_properties_add_bool = reinterpret_cast<decltype(a.obs_properties_add_bool)>(GetProcAddress(h, "obs_properties_add_bool"));
            a.obs_properties_add_float_slider = reinterpret_cast<decltype(a.obs_properties_add_float_slider)>(GetProcAddress(h, "obs_properties_add_float_slider"));
            a.obs_properties_add_int_slider = reinterpret_cast<decltype(a.obs_properties_add_int_slider)>(GetProcAddress(h, "obs_properties_add_int_slider"));
            a.obs_data_get_string = reinterpret_cast<decltype(a.obs_data_get_string)>(GetProcAddress(h, "obs_data_get_string"));
            a.obs_data_get_bool = reinterpret_cast<decltype(a.obs_data_get_bool)>(GetProcAddress(h, "obs_data_get_bool"));
            a.obs_data_get_double = reinterpret_cast<decltype(a.obs_data_get_double)>(GetProcAddress(h, "obs_data_get_double"));
            a.obs_data_get_int = reinterpret_cast<decltype(a.obs_data_get_int)>(GetProcAddress(h, "obs_data_get_int"));
            a.obs_data_set_default_string = reinterpret_cast<decltype(a.obs_data_set_default_string)>(GetProcAddress(h, "obs_data_set_default_string"));
            a.obs_data_set_default_double = reinterpret_cast<decltype(a.obs_data_set_default_double)>(GetProcAddress(h, "obs_data_set_default_double"));
            a.obs_data_set_default_int = reinterpret_cast<decltype(a.obs_data_set_default_int)>(GetProcAddress(h, "obs_data_set_default_int"));
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
        decoder.initialize_video(config.codec);
        decoder.initialize_audio();
        client.fetch_capabilities(config.host, config.port, config.pin, capabilities);
        client.start_streaming(config, video_queue, audio_queue);

        decode_running.store(true);
        decode_thread = std::thread([this]() {
            auto& api = ObsApi::instance();
            while (decode_running.load()) {
                auto vpkt = video_queue.pop();
                if (vpkt.has_value()) {
                    decoder.decode_video_packet(*vpkt, [&](const DecodedVideoFrame& vf) {
                        if (api.obs_source_output_video && source) {
                            obs_source_frame_minimal of{};
                            for (int i = 0; i < 4; ++i) {
                                of.data[i] = vf.data[i];
                                of.linesize[i] = static_cast<uint32_t>(vf.linesize[i]);
                            }
                            of.width = static_cast<uint32_t>(vf.width);
                            of.height = static_cast<uint32_t>(vf.height);
                            of.timestamp = vf.pts_ns;
                            of.format = vf.is_hdr_p010 ? 20 : 2; // VIDEO_FORMAT_P010 or VIDEO_FORMAT_NV12
                            api.obs_source_output_video(source, &of);
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
    return "CameraPhone 4K Pro (USB / Wi-Fi)";
}

static void source_get_defaults(obs_data_t* settings) {
    auto& api = ObsApi::instance();
    if (!api.obs_data_set_default_string) return;
    api.obs_data_set_default_string(settings, "host", "127.0.0.1");
    api.obs_data_set_default_string(settings, "resolution", "3840x2160");
    api.obs_data_set_default_string(settings, "fps", "60");
    api.obs_data_set_default_string(settings, "codec", "hevc");
    api.obs_data_set_default_string(settings, "lens", "0");
    api.obs_data_set_default_double(settings, "zoom", 1.0);
    api.obs_data_set_default_int(settings, "iso", 100);
    api.obs_data_set_default_int(settings, "wb_kelvin", 5600);
}

static obs_properties_t* source_get_properties(void* data) {
    auto& api = ObsApi::instance();
    if (!api.obs_properties_create) return nullptr;
    obs_properties_t* props = api.obs_properties_create();

    auto* dev_list = api.obs_properties_add_list(props, "host", "Dispositivo (USB ADB / Wi-Fi)", OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
    api.obs_property_list_add_string(dev_list, "USB Cabo Direto (ADB Loopback 127.0.0.1)", "127.0.0.1");
    for (const auto& d : DiscoveryManager::list_usb_devices()) {
        api.obs_property_list_add_string(dev_list, d.display_name.c_str(), d.host.c_str());
    }

    api.obs_properties_add_text(props, "pin", "PIN de Segurança (6 dígitos)", OBS_TEXT_PASSWORD);

    auto* lens_list = api.obs_properties_add_list(props, "lens", "Lente Física", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    auto* ctx = static_cast<CameraPhoneSourceContext*>(data);
    if (ctx && !ctx->capabilities.lenses.empty()) {
        for (const auto& l : ctx->capabilities.lenses) {
            api.obs_property_list_add_string(lens_list, l.label.c_str(), l.id.c_str());
        }
    } else {
        api.obs_property_list_add_string(lens_list, "Main Wide (24mm)", "0");
        api.obs_property_list_add_string(lens_list, "Ultra-Wide (13mm)", "2");
        api.obs_property_list_add_string(lens_list, "Telephoto (70mm+)", "3");
        api.obs_property_list_add_string(lens_list, "Front Camera", "1");
    }

    auto* res_list = api.obs_properties_add_list(props, "resolution", "Resolução", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
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
    api.obs_properties_add_float_slider(props, "zoom", "Zoom Óptico/Híbrido", 1.0, 10.0, 0.1);
    api.obs_properties_add_int_slider(props, "iso", "ISO Manual", 50, 6400, 50);
    api.obs_properties_add_int_slider(props, "wb_kelvin", "Balanço de Branco (Kelvin)", 2000, 10000, 100);
    api.obs_properties_add_bool(props, "torch", "Lanterna / LED Traseiro");
    return props;
}

static void source_update(void* data, obs_data_t* settings) {
    auto* ctx = static_cast<CameraPhoneSourceContext*>(data);
    auto& api = ObsApi::instance();
    if (!ctx || !api.obs_data_get_string) return;

    const char* host = api.obs_data_get_string(settings, "host");
    const char* pin  = api.obs_data_get_string(settings, "pin");
    const char* lens = api.obs_data_get_string(settings, "lens");
    const char* res  = api.obs_data_get_string(settings, "resolution");
    const char* fps  = api.obs_data_get_string(settings, "fps");
    const char* cdc  = api.obs_data_get_string(settings, "codec");
    bool hdr         = api.obs_data_get_bool ? api.obs_data_get_bool(settings, "hdr") : false;
    double zoom      = api.obs_data_get_double ? api.obs_data_get_double(settings, "zoom") : 1.0;
    long long iso    = api.obs_data_get_int ? api.obs_data_get_int(settings, "iso") : 100;
    long long wb     = api.obs_data_get_int ? api.obs_data_get_int(settings, "wb_kelvin") : 5600;
    bool torch       = api.obs_data_get_bool ? api.obs_data_get_bool(settings, "torch") : false;

    std::string new_host = host && *host ? host : "127.0.0.1";
    std::string new_pin  = pin ? pin : "";
    std::string new_lens = lens && *lens ? lens : "0";
    std::string new_cdc  = cdc && *cdc ? cdc : "hevc";
    int new_fps          = fps && *fps ? std::atoi(fps) : 60;
    int new_w = 3840, new_h = 2160;
    if (res && std::string(res) == "2560x1440") { new_w = 2560; new_h = 1440; }
    else if (res && std::string(res) == "1920x1080") { new_w = 1920; new_h = 1080; }

    bool stream_changed = (ctx->config.host != new_host || ctx->config.pin != new_pin ||
                           ctx->config.width != new_w || ctx->config.height != new_h ||
                           ctx->config.fps != new_fps || ctx->config.codec != new_cdc ||
                           ctx->config.hdr != hdr);

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

    static obs_source_info_minimal info{};
    info.id             = "cameraphone_obs_source";
    info.type           = 0; // OBS_SOURCE_TYPE_INPUT
    info.output_flags   = (1U << 0) | (1U << 1) | (1U << 3) | (1U << 11); // VIDEO | AUDIO | ASYNC | DO_NOT_DUPLICATE
    info.get_name       = source_get_name;
    info.create         = source_create;
    info.destroy        = source_destroy;
    info.get_defaults   = source_get_defaults;
    info.get_properties = source_get_properties;
    info.update         = source_update;
    info.activate       = source_activate;
    info.deactivate     = source_deactivate;

    api.obs_register_source_s(&info, sizeof(info));
}

} // namespace camphone
