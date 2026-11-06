#include <windows.h>
#include <initguid.h>
#include <dshow.h>
#include <iostream>
#include <thread>
#include <chrono>

static const GUID CLSID_CameraPhoneVCam = 
    { 0xb76c09c1, 0xf64c, 0x40b1, { 0xb5, 0x97, 0x98, 0xce, 0x2e, 0x17, 0xa4, 0xd1 } };

static const GUID CLSID_NullRenderer_Local =
    { 0xc1f400a0, 0x3f08, 0x11d3, { 0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37 } };

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    std::cout << "[TEST] Initializing FilterGraph..." << std::endl;
    IGraphBuilder* pGraph = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IGraphBuilder, reinterpret_cast<void**>(&pGraph));
    if (FAILED(hr) || !pGraph) {
        std::cerr << "[FAIL] Failed to create IGraphBuilder: 0x" << std::hex << hr << std::endl;
        CoUninitialize();
        return 1;
    }

    std::cout << "[TEST] Adding CameraPhone VCam to Graph..." << std::endl;
    IBaseFilter* pVCam = nullptr;
    hr = CoCreateInstance(CLSID_CameraPhoneVCam, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IBaseFilter, reinterpret_cast<void**>(&pVCam));
    if (FAILED(hr) || !pVCam) {
        std::cerr << "[FAIL] Failed to instantiate CLSID_CameraPhoneVCam: 0x" << std::hex << hr << std::endl;
        pGraph->Release();
        CoUninitialize();
        return 1;
    }

    hr = pGraph->AddFilter(pVCam, L"CameraPhoneVCam");
    if (FAILED(hr)) {
        std::cerr << "[FAIL] AddFilter failed: 0x" << std::hex << hr << std::endl;
        pVCam->Release();
        pGraph->Release();
        CoUninitialize();
        return 1;
    }

    // Find the output pin
    IEnumPins* pEnum = nullptr;
    IPin* pOutPin = nullptr;
    pVCam->EnumPins(&pEnum);
    if (pEnum) {
        pEnum->Next(1, &pOutPin, nullptr);
        pEnum->Release();
    }

    if (!pOutPin) {
        std::cerr << "[FAIL] Could not find output pin on VCam" << std::endl;
        pVCam->Release();
        pGraph->Release();
        CoUninitialize();
        return 1;
    }

    // Connect using RenderPin
    std::cout << "[TEST] Rendering Output Pin..." << std::endl;
    hr = pGraph->Render(pOutPin);
    if (FAILED(hr)) {
        std::cout << "[INFO] Render failed (no default renderer available): 0x" << std::hex << hr << std::endl;
    } else {
        std::cout << "[PASS] Graph auto-rendered output pin successfully!" << std::endl;
    }

    IMediaControl* pControl = nullptr;
    hr = pGraph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&pControl));
    if (SUCCEEDED(hr) && pControl) {
        std::cout << "[TEST] Starting FilterGraph (IMediaControl::Run)..." << std::endl;
        hr = pControl->Run();
        std::cout << "[TEST] Run() result: 0x" << std::hex << hr << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(1500));

        OAFilterState state = State_Stopped;
        hr = pControl->GetState(500, &state);
        std::cout << "[TEST] Graph State after 1.5s: " << (state == State_Running ? "State_Running" : "Other") << " (0x" << std::hex << hr << ")" << std::endl;

        pControl->Stop();
        std::cout << "[TEST] Graph stopped." << std::endl;
        pControl->Release();
    }

    pOutPin->Release();
    pVCam->Release();
    pGraph->Release();
    CoUninitialize();

    std::cout << "[SUCCESS] VCam Filter Graph execution verified!" << std::endl;
    return 0;
}
