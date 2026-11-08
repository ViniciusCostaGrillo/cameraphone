#include <windows.h>
#include <initguid.h>
#include <dshow.h>
#include <atomic>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "../protocol/vcam_shmem.hpp"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// Mock downstream pin simulating NVIDIA Broadcast's DirectShow sink (IPin + IMemInputPin)
class MockBroadcastSinkPin : public IPin, public IMemInputPin {
public:
    std::atomic<LONG> ref_count{1};
    IMemAllocator* allocator = nullptr;
    AM_MEDIA_TYPE connected_mt{};
    std::atomic<int> received_samples{0};
    std::atomic<long> last_sample_size{0};
    std::atomic<long> last_actual_len{0};
    std::atomic<uint8_t> last_uv_first_byte{0};
    std::atomic<uint8_t> last_uv_end_byte{0};

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IPin) {
            *ppv = static_cast<IPin*>(this);
        } else if (riid == IID_IMemInputPin) {
            *ppv = static_cast<IMemInputPin*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count; }
    STDMETHODIMP_(ULONG) Release() override { return --ref_count; }

    // IPin
    STDMETHODIMP Connect(IPin*, const AM_MEDIA_TYPE*) override { return E_NOTIMPL; }
    STDMETHODIMP ReceiveConnection(IPin*, const AM_MEDIA_TYPE* pmt) override {
        if (!pmt) return E_POINTER;
        connected_mt = *pmt;
        if (pmt->cbFormat && pmt->pbFormat) {
            connected_mt.pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(pmt->cbFormat));
            memcpy(connected_mt.pbFormat, pmt->pbFormat, pmt->cbFormat);
        }
        return S_OK;
    }
    STDMETHODIMP Disconnect() override {
        if (connected_mt.pbFormat) {
            CoTaskMemFree(connected_mt.pbFormat);
            connected_mt.pbFormat = nullptr;
        }
        if (allocator) {
            allocator->Release();
            allocator = nullptr;
        }
        return S_OK;
    }
    STDMETHODIMP ConnectedTo(IPin**) override { return E_NOTIMPL; }
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryPinInfo(PIN_INFO*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryDirection(PIN_DIRECTION* pPinDir) override {
        if (!pPinDir) return E_POINTER;
        *pPinDir = PINDIR_INPUT;
        return S_OK;
    }
    STDMETHODIMP QueryId(LPWSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE*) override { return S_OK; }
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes**) override { return E_NOTIMPL; }
    STDMETHODIMP QueryInternalConnections(IPin**, ULONG*) override { return E_NOTIMPL; }
    STDMETHODIMP EndOfStream() override { return S_OK; }
    STDMETHODIMP BeginFlush() override { return S_OK; }
    STDMETHODIMP EndFlush() override { return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) override { return S_OK; }

    // IMemInputPin
    STDMETHODIMP GetAllocator(IMemAllocator** ppAllocator) override {
        if (!ppAllocator) return E_POINTER;
        *ppAllocator = nullptr;
        return VFW_E_NO_ALLOCATOR;
    }
    STDMETHODIMP NotifyAllocator(IMemAllocator* pAllocator, BOOL) override {
        if (allocator) allocator->Release();
        allocator = pAllocator;
        if (allocator) allocator->AddRef();
        return S_OK;
    }
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES*) override { return E_NOTIMPL; }
    STDMETHODIMP Receive(IMediaSample* pSample) override {
        if (!pSample) return E_POINTER;
        BYTE* ptr = nullptr;
        pSample->GetPointer(&ptr);
        long sz = pSample->GetSize();
        long actual = pSample->GetActualDataLength();
        last_sample_size.store(sz);
        last_actual_len.store(actual);
        if (ptr && actual >= 12441600 && sz >= 12441600) {
            // Exact offsets read by NvBroadcastCore.dll+0x2039c0 (3840 * 2160 = 8,294,400)
            last_uv_first_byte.store(ptr[3840 * 2160]);
            last_uv_end_byte.store(ptr[12441600 - 1]);
        }
        received_samples.fetch_add(1);
        return S_OK;
    }
    STDMETHODIMP ReceiveMultiple(IMediaSample**, long, long*) override { return E_NOTIMPL; }
    STDMETHODIMP ReceiveCanBlock() override { return S_OK; }
};

int main() {
    std::cout << "=== CameraPhone Virtual Camera DirectShow Verification ===" << std::endl;
    camphone::OBSStreamGuard test_guard;
    test_guard.acquire();
    Sleep(80);

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
                                        assert(count >= 8);
                                    }

                                    // Regression test for NVIDIA Broadcast 4K crash (NvBroadcastCore.dll+0x2039c0):
                                    // 1. Select Mode 2 (3840x2160 @ 30 FPS NV12) via SetFormat
                                    AM_MEDIA_TYPE* pmt_4k = nullptr;
                                    std::vector<BYTE> scc_buf(size);
                                    hr = stream_cfg->GetStreamCaps(2, &pmt_4k, scc_buf.data());
                                    assert(hr == S_OK && pmt_4k != nullptr);
                                    hr = stream_cfg->SetFormat(pmt_4k);
                                    assert(hr == S_OK);
                                    if (pmt_4k->pbFormat) CoTaskMemFree(pmt_4k->pbFormat);
                                    CoTaskMemFree(pmt_4k);

                                    // 2. Connect with pmt == nullptr (just like FilterGraph::ConnectDirect / NvBroadcastCore)
                                    MockBroadcastSinkPin sink;
                                    hr = pin->Connect(static_cast<IPin*>(&sink), nullptr);
                                    assert(SUCCEEDED(hr));

                                    // 3. Verify ConnectionMediaType preserved 3840x2160 (12,441,600 bytes) instead of resetting to 1080p!
                                    AM_MEDIA_TYPE conn_mt{};
                                    hr = pin->ConnectionMediaType(&conn_mt);
                                    assert(SUCCEEDED(hr));
                                    assert(conn_mt.lSampleSize == 12441600);
                                    if (conn_mt.pbFormat) CoTaskMemFree(conn_mt.pbFormat);

                                    // 4. Write a 1080p frame to shared memory, run filter, and verify 4K sample delivery
                                    camphone::VCamSharedMemoryWriter shmem_writer;
                                    shmem_writer.open();
                                    std::vector<uint8_t> y1080(1920 * 1080, 180);
                                    std::vector<uint8_t> uv1080((1920 * 1080) / 2, 99);
                                    const uint8_t* planes[4] = { y1080.data(), uv1080.data(), nullptr, nullptr };
                                    int strides[4] = { 1920, 1920, 0, 0 };
                                    assert(shmem_writer.write_frame(1920, 1080, 23, planes, strides, 42ULL));

                                    filter->Run(0);
                                    for (int wait_i = 0; wait_i < 50 && sink.received_samples.load() == 0; ++wait_i) {
                                        Sleep(10);
                                    }
                                    filter->Stop();
                                    pin->Disconnect();
                                    sink.Disconnect();

                                    assert(sink.received_samples.load() > 0);
                                    assert(sink.last_sample_size.load() >= 12441600);
                                    assert(sink.last_actual_len.load() == 12441600);
                                    assert(sink.last_uv_first_byte.load() == 99);
                                    assert(sink.last_uv_end_byte.load() == 99);
                                    std::cout << "     - Verified NVIDIA Broadcast 4K (3840x2160) SetFormat -> Connect(nullptr) -> Sample UV plane valid!" << std::endl;

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
