#include "cameraphone-source.hpp"
#include <cstdint>

#ifdef _WIN32
#define MODULE_EXPORT __declspec(dllexport)
#else
#define MODULE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

static void* g_obs_module_pointer = nullptr;

MODULE_EXPORT void obs_module_set_pointer(void* module) {
    g_obs_module_pointer = module;
}

MODULE_EXPORT void* obs_current_module(void) {
    return g_obs_module_pointer;
}

MODULE_EXPORT uint32_t obs_module_ver(void) {
    //LIBOBS_API_VER (30.0.0)
    return (30U << 24) | (0U << 16) | 0U;
}

MODULE_EXPORT bool obs_module_load(void) {
    camphone::register_cameraphone_source();
    return true;
}

MODULE_EXPORT void obs_module_unload(void) {
}

MODULE_EXPORT const char* obs_module_name(void) {
    return "CameraPhone 4K Pro";
}

MODULE_EXPORT const char* obs_module_description(void) {
    return "Zero-Trust, Zero-Freeze 4K60/120 HDR Smartphone Camera Plugin";
}

} // extern "C"
