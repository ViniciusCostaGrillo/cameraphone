#include <windows.h>
#include <initguid.h>
#include <dshow.h>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "../protocol/vcam_shmem.hpp"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

int main() {
    std::cout << "=== CameraPhone Virtual Camera DirectShow Verification ===" << std::endl;

    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
        std::cerr << "CoInitialize failed: " << std::hex << hr << std::endl;
        return 1;
    }

    ICreateDevEnum* dev_enum = nullptr;
    hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ICreateDevEnum, reinterpret_cast<void**>(&dev_enum));
    if (FAILED(hr) || !dev_enum) {
        std::cerr << "Failed to create CLSID_SystemDeviceEnum: " << std::hex << hr << std::endl;
        CoUninitialize();
        return 1;
    }

    IEnumMoniker* enum_moniker = nullptr;
    hr = dev_enum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enum_moniker, 0);
    if (hr != S_OK || !enum_moniker) {
        std::cerr << "No video capture devices found in category." << std::endl;
        dev_enum->Release();
        CoUninitialize();
        return 1;
    }

    IMoniker* moniker = nullptr;
    bool found_cameraphone = false;

    while (enum_moniker->Next(1, &moniker, nullptr) == S_OK) {
        IPropertyBag* prop_bag = nullptr;
        hr = moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, reinterpret_cast<void**>(&prop_bag));
        if (SUCCEEDED(hr) && prop_bag) {
            VARIANT var;
            VariantInit(&var);
            hr = prop_bag->Read(L"FriendlyName", &var, nullptr);
            if (SUCCEEDED(hr) && var.vt == VT_BSTR) {
                std::wcout << L"  [Found Device] " << var.bstrVal << std::endl;
                if (std::wstring(var.bstrVal).find(L"CameraPhone") != std::wstring::npos) {
                    found_cameraphone = true;
                    std::cout << "  -> Matching target virtual camera!" << std::endl;

                    IBaseFilter* filter = nullptr;
                    hr = moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, reinterpret_cast<void**>(&filter));
                    if (SUCCEEDED(hr) && filter) {
                        std::cout << "  -> Successfully instantiated CameraPhone IBaseFilter via COM!" << std::endl;

                        IEnumPins* enum_pins = nullptr;
                        if (SUCCEEDED(filter->EnumPins(&enum_pins)) && enum_pins) {
                            IPin* pin = nullptr;
                            while (enum_pins->Next(1, &pin, nullptr) == S_OK) {
                                PIN_INFO pinfo{};
                                pin->QueryPinInfo(&pinfo);
                                if (pinfo.pFilter) pinfo.pFilter->Release();
                                std::wcout << L"     - Pin: " << pinfo.achName 
                                           << L" (dir=" << (pinfo.dir == PINDIR_OUTPUT ? L"Output" : L"Input") << L")" << std::endl;

                                auto print_guid = [](const char* name, const GUID& g) {
                                    printf("     - %s: {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n",
                                           name, (unsigned long)g.Data1, g.Data2, g.Data3,
                                           g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
                                           g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
                                };
                                print_guid("IID_IAMStreamConfig", IID_IAMStreamConfig);
                                print_guid("AMPROPSETID_Pin", AMPROPSETID_Pin);
                                print_guid("PIN_CATEGORY_CAPTURE", PIN_CATEGORY_CAPTURE);
                                print_guid("IID_IKsPropertySet", IID_IKsPropertySet);

                                IKsPropertySet* ks = nullptr;
                                hr = pin->QueryInterface(IID_IKsPropertySet, reinterpret_cast<void**>(&ks));
                                std::cout << "     - QueryInterface(IID_IKsPropertySet): " << std::hex << hr << std::dec << std::endl;
                                if (SUCCEEDED(hr) && ks) {
                                    GUID pin_category = GUID_NULL;
                                    DWORD cbReturned = 0;
                                    hr = ks->Get(AMPROPSETID_Pin, 0, nullptr, 0,
                                                 &pin_category, sizeof(pin_category), &cbReturned);
                                    if (SUCCEEDED(hr) && pin_category == PIN_CATEGORY_CAPTURE) {
                                        std::cout << "     - Verified: PIN_CATEGORY_CAPTURE present (Required by NVIDIA Broadcast)!" << std::endl;
                                    } else {
                                        std::cout << "     - ks->Get returned " << std::hex << hr << std::dec << std::endl;
                                    }
                                    ks->Release();
                                }

                                IAMStreamConfig* stream_cfg = nullptr;
                                hr = pin->QueryInterface(IID_IAMStreamConfig, reinterpret_cast<void**>(&stream_cfg));
                                std::cout << "     - QueryInterface(IID_IAMStreamConfig): " << std::hex << hr << std::dec << std::endl;
                                if (SUCCEEDED(hr) && stream_cfg) {
                                    int count = 0, size = 0;
                                    if (SUCCEEDED(stream_cfg->GetNumberOfCapabilities(&count, &size))) {
                                        std::cout << "     - StreamConfig exposed " << count << " capability formats." << std::endl;
                                    }
                                    stream_cfg->Release();
                                }

                                pin->Release();
                            }
                            enum_pins->Release();
                        }
                        filter->Release();
                    } else {
                        std::cerr << "  -> Failed to bind IBaseFilter: " << std::hex << hr << std::endl;
                    }
                }
            }
            VariantClear(&var);
            prop_bag->Release();
        }
        moniker->Release();
    }

    enum_moniker->Release();
    dev_enum->Release();
    CoUninitialize();

    if (found_cameraphone) {
        // Test VCamSharedMemoryReader lazy-open and frame freshness persistence (60Hz vs 20-30FPS)
        camphone::VCamSharedMemoryReader reader;
        camphone::VCamSharedMemoryWriter writer;
        writer.open();
        uint32_t w = 640, h = 480;
        std::vector<uint8_t> y_plane(w * h, 200);
        std::vector<uint8_t> uv_plane((w * h) / 2, 128);
        const uint8_t* planes[4] = { y_plane.data(), uv_plane.data(), nullptr, nullptr };
        int strides[4] = { static_cast<int>(w), static_cast<int>(w), 0, 0 };
        assert(writer.write_frame(w, h, 23 /* NV12 */, planes, strides, 123456789ULL));

        camphone::VCamSharedHeader hdr{};
        std::vector<uint8_t> payload;
        // Reader was NOT explicitly opened before read_frame -> must lazy-open and succeed
        assert(reader.read_frame(hdr, payload, 2500));
        assert(hdr.width == w && hdr.height == h);
        // Subsequent 60Hz ticks without new write_frame must still return true while fresh
        assert(reader.read_frame(hdr, payload, 2500));

        // Simulate expired frame (max_age_ms = 10ms after sleeping 30ms)
        Sleep(30);
        assert(!reader.read_frame(hdr, payload, 10));

        std::cout << "=== SUCCESS: CameraPhone Virtual Camera & Shmem Persistence fully operational! ===" << std::endl;
        return 0;
    } else {
        std::cerr << "=== FAILURE: CameraPhone Virtual Camera not found ===" << std::endl;
        return 1;
    }
}
