#include <windows.h>
#include <dshow.h>
#include <strmif.h>
#include <amvideo.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "vcam-standby.hpp"
#include "../protocol/vcam_shmem.hpp"

// Static GUID definitions to eliminate dependencies on external GUID libraries
static const GUID CLSID_CameraPhoneVCam = 
    { 0xb76c09c1, 0xf64c, 0x40b1, { 0xb5, 0x97, 0x98, 0xce, 0x2e, 0x17, 0xa4, 0xd1 } };

static const GUID CLSID_VideoInputDeviceCategory_Local = 
    { 0x860bb310, 0x5d01, 0x11d0, { 0xbd, 0x3b, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86 } };

static const GUID MEDIATYPE_Video_Local =
    { 0x73646976, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MEDIASUBTYPE_NV12_Local =
    { 0x3231564e, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MEDIASUBTYPE_YUY2_Local =
    { 0x32595559, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID FORMAT_VideoInfo_Local =
    { 0x05589f80, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };

static const GUID PIN_CATEGORY_CAPTURE_Local =
    { 0xfb6c4281, 0x0353, 0x11d1, { 0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba } };
static const GUID AMPROPSETID_Pin_Local =
    { 0x9b00f101, 0x1567, 0x11d1, { 0xb3, 0xf1, 0x00, 0xaa, 0x00, 0x37, 0x61, 0xc5 } };
static const GUID CLSID_MemoryAllocator_Local =
    { 0x1e651cc0, 0xb199, 0x11d0, { 0x82, 0x12, 0x00, 0xc0, 0x4f, 0xc3, 0x2c, 0x45 } };

static const GUID IID_IPin_Local =
    { 0x56a86891, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID IID_IEnumPins_Local =
    { 0x56a86892, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID IID_IEnumMediaTypes_Local =
    { 0x89c31040, 0x846b, 0x11ce, { 0x97, 0xd3, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
static const GUID IID_IMediaFilter_Local =
    { 0x56a86899, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID IID_IBaseFilter_Local =
    { 0x56a86895, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID IID_IMemInputPin_Local =
    { 0x56a8689d, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID IID_IMemAllocator_Local =
    { 0x56a8689c, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID IID_IAMStreamConfig_Local =
    { 0xc6e13340, 0x30ac, 0x11d0, { 0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56 } };
static const GUID IID_IKsPropertySet_Local =
    { 0x31efac30, 0x515c, 0x11d0, { 0xa9, 0xaa, 0x00, 0xaa, 0x00, 0x61, 0xbe, 0x93 } };
static const GUID IID_IAMFilterMiscFlags_Local =
    { 0x2dd74950, 0xa890, 0x11d1, { 0xab, 0xe8, 0x00, 0xa0, 0xc9, 0x05, 0xf3, 0x75 } };
static const GUID IID_IQualityControl_Local =
    { 0x56a868a5, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };

static const wchar_t* kVCamFriendlyName = L"CameraPhone Virtual Camera";

static LONG g_server_locks = 0;
static HMODULE g_module_handle = nullptr;

static void DeleteMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return;
    if (pmt->cbFormat != 0) {
        CoTaskMemFree(pmt->pbFormat);
        pmt->cbFormat = 0;
        pmt->pbFormat = nullptr;
    }
    if (pmt->pUnk != nullptr) {
        pmt->pUnk->Release();
        pmt->pUnk = nullptr;
    }
    CoTaskMemFree(pmt);
}

static AM_MEDIA_TYPE* CreateMediaType(const AM_MEDIA_TYPE* psrc) {
    if (!psrc) return nullptr;
    auto* pmt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (!pmt) return nullptr;
    std::memcpy(pmt, psrc, sizeof(AM_MEDIA_TYPE));
    if (psrc->cbFormat != 0 && psrc->pbFormat != nullptr) {
        pmt->pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(psrc->cbFormat));
        if (pmt->pbFormat) {
            std::memcpy(pmt->pbFormat, psrc->pbFormat, psrc->cbFormat);
        } else {
            pmt->cbFormat = 0;
        }
    }
    if (pmt->pUnk) {
        pmt->pUnk->AddRef();
    }
    return pmt;
}

class CVCamFilter;

class CVCamOutputPin : public IPin, public IAMStreamConfig, public IKsPropertySet, public IQualityControl {
public:
    CVCamOutputPin(CVCamFilter* pFilter);
    virtual ~CVCamOutputPin();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPin
    STDMETHODIMP Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP Disconnect() override;
    STDMETHODIMP ConnectedTo(IPin** pPin) override;
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP QueryPinInfo(PIN_INFO* pInfo) override;
    STDMETHODIMP QueryDirection(PIN_DIRECTION* pPinDir) override;
    STDMETHODIMP QueryId(LPWSTR* Id) override;
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** ppEnum) override;
    STDMETHODIMP QueryInternalConnections(IPin** apPin, ULONG* nPin) override;
    STDMETHODIMP EndOfStream() override;
    STDMETHODIMP BeginFlush() override;
    STDMETHODIMP EndFlush() override;
    STDMETHODIMP NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) override;

    // IAMStreamConfig
    STDMETHODIMP SetFormat(AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP GetFormat(AM_MEDIA_TYPE** ppmt) override;
    STDMETHODIMP GetNumberOfCapabilities(int* piCount, int* piSize) override;
    STDMETHODIMP GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) override;

    // IKsPropertySet
    STDMETHODIMP Set(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData) override;
    STDMETHODIMP Get(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData, DWORD* pcbReturned) override;
    STDMETHODIMP QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport) override;

    // IQualityControl
    STDMETHODIMP Notify(IBaseFilter*, Quality) override { return S_OK; }
    STDMETHODIMP SetSink(IQualityControl*) override { return S_OK; }

    static constexpr int kNumCapabilities = 8;
    static constexpr long kMaxSampleBufferBytes = 3840L * 2160L * 2L; // 16.58 MB (safe for up to 4K YUY2/NV12)

    bool BuildMediaType(int index, AM_MEDIA_TYPE* pmt);
    bool CopyCurrentMediaType(AM_MEDIA_TYPE* pmt);
    bool IsConnected() const { return m_pConnectedPin != nullptr; }
    IPin* GetConnectedPin() const { return m_pConnectedPin; }
    IMemInputPin* GetMemInputPin() const { return m_pMemInputPin; }
    IMemAllocator* GetAllocator() const { return m_pAllocator; }

    void GetCurrentFormatSnapshot(uint32_t& out_w, uint32_t& out_h, bool& out_yuy2, REFERENCE_TIME& out_avg_time) const {
        std::lock_guard<std::mutex> lock(m_FormatMutex);
        out_w = m_Width;
        out_h = m_Height;
        out_yuy2 = (m_CurrentMediaType.subtype == MEDIASUBTYPE_YUY2_Local);
        out_avg_time = m_AvgTimePerFrame;
    }

    uint32_t GetCurrentWidth() const { std::lock_guard<std::mutex> lock(m_FormatMutex); return m_Width; }
    uint32_t GetCurrentHeight() const { std::lock_guard<std::mutex> lock(m_FormatMutex); return m_Height; }
    bool IsYUY2() const { std::lock_guard<std::mutex> lock(m_FormatMutex); return m_CurrentMediaType.subtype == MEDIASUBTYPE_YUY2_Local; }
    REFERENCE_TIME GetAvgTimePerFrame() const { std::lock_guard<std::mutex> lock(m_FormatMutex); return m_AvgTimePerFrame; }

private:
    void ConfigureAllocatorLocked();

    std::atomic<LONG>  m_cRef{1};
    CVCamFilter*       m_pFilter = nullptr;
    IPin*              m_pConnectedPin = nullptr;
    IMemInputPin*      m_pMemInputPin = nullptr;
    IMemAllocator*     m_pAllocator = nullptr;

    mutable std::mutex m_FormatMutex;
    AM_MEDIA_TYPE      m_CurrentMediaType{};
    uint32_t           m_Width = 1920;
    uint32_t           m_Height = 1080;
    REFERENCE_TIME     m_AvgTimePerFrame = 333333; // 30 FPS default
};

class CVCamEnumMediaTypes : public IEnumMediaTypes {
public:
    CVCamEnumMediaTypes(CVCamOutputPin* pPin, int index = 0) : m_pPin(pPin), m_Index(index) {
        if (m_pPin) m_pPin->AddRef();
    }
    virtual ~CVCamEnumMediaTypes() {
        if (m_pPin) m_pPin->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumMediaTypes_Local) {
            *ppv = static_cast<IEnumMediaTypes*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_cRef; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --m_cRef;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP Next(ULONG cMediaTypes, AM_MEDIA_TYPE** ppMediaTypes, ULONG* pcFetched) override {
        if (!ppMediaTypes) return E_POINTER;
        if (cMediaTypes != 1 && !pcFetched) return E_INVALIDARG;
        ULONG fetched = 0;
        while (fetched < cMediaTypes) {
            auto* pmt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
            if (!pmt) break;
            std::memset(pmt, 0, sizeof(AM_MEDIA_TYPE));
            bool ok = false;
            if (m_Index == 0) {
                // Always yield the active m_CurrentMediaType (set via IAMStreamConfig::SetFormat) first!
                ok = m_pPin->CopyCurrentMediaType(pmt);
            } else {
                ok = m_pPin->BuildMediaType(m_Index - 1, pmt);
            }
            if (!ok) {
                CoTaskMemFree(pmt);
                break;
            }
            ppMediaTypes[fetched++] = pmt;
            ++m_Index;
        }
        if (pcFetched) *pcFetched = fetched;
        return fetched == cMediaTypes ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG cMediaTypes) override {
        m_Index += cMediaTypes;
        return S_OK;
    }
    STDMETHODIMP Reset() override {
        m_Index = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumMediaTypes** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new CVCamEnumMediaTypes(m_pPin, m_Index);
        return S_OK;
    }

private:
    std::atomic<LONG> m_cRef{1};
    CVCamOutputPin*   m_pPin = nullptr;
    int               m_Index = 0;
};

class CVCamEnumPins : public IEnumPins {
public:
    CVCamEnumPins(IPin* pPin, int index = 0) : m_pPin(pPin), m_Index(index) {
        if (m_pPin) m_pPin->AddRef();
    }
    virtual ~CVCamEnumPins() {
        if (m_pPin) m_pPin->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumPins_Local) {
            *ppv = static_cast<IEnumPins*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_cRef; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --m_cRef;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP Next(ULONG cPins, IPin** ppPins, ULONG* pcFetched) override {
        if (!ppPins) return E_POINTER;
        if (cPins != 1 && !pcFetched) return E_INVALIDARG;
        ULONG fetched = 0;
        if (m_Index == 0 && cPins > 0) {
            ppPins[0] = m_pPin;
            m_pPin->AddRef();
            fetched = 1;
            m_Index = 1;
        }
        if (pcFetched) *pcFetched = fetched;
        return fetched == cPins ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG cPins) override {
        m_Index += cPins;
        return S_OK;
    }
    STDMETHODIMP Reset() override {
        m_Index = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumPins** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new CVCamEnumPins(m_pPin, m_Index);
        return S_OK;
    }

private:
    std::atomic<LONG> m_cRef{1};
    IPin*             m_pPin = nullptr;
    int               m_Index = 0;
};

class CVCamFilter : public IBaseFilter, public IAMFilterMiscFlags {
public:
    CVCamFilter();
    virtual ~CVCamFilter();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPersist
    STDMETHODIMP GetClassID(CLSID* pClassID) override;

    // IMediaFilter
    STDMETHODIMP Stop() override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Run(REFERENCE_TIME tStart) override;
    STDMETHODIMP GetState(DWORD dwMilliSecsTimeout, FILTER_STATE* State) override;
    STDMETHODIMP SetSyncSource(IReferenceClock* pClock) override;
    STDMETHODIMP GetSyncSource(IReferenceClock** pClock) override;

    // IBaseFilter
    STDMETHODIMP EnumPins(IEnumPins** ppEnum) override;
    STDMETHODIMP FindPin(LPCWSTR Id, IPin** ppPin) override;
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* pInfo) override;
    STDMETHODIMP JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) override;
    STDMETHODIMP QueryVendorInfo(LPWSTR* pVendorInfo) override;

    // IAMFilterMiscFlags
    STDMETHODIMP_(ULONG) GetMiscFlags() override { return 0x00000001; } // AM_FILTER_MISC_FLAGS_IS_SOURCE

    FILTER_STATE GetFilterState() const { return m_State; }

private:
    void StartWorker();
    void StopWorker();
    void WorkerLoop();

    std::atomic<LONG> m_cRef{1};
    FILTER_STATE      m_State = State_Stopped;
    IFilterGraph*     m_pGraph = nullptr;
    IReferenceClock*  m_pClock = nullptr;
    LPWSTR            m_pName = nullptr;
    CVCamOutputPin*   m_pPin = nullptr;

    std::thread       m_Worker;
    std::atomic<bool> m_WorkerRunning{false};
};

// --- CVCamOutputPin Implementation ---

CVCamOutputPin::CVCamOutputPin(CVCamFilter* pFilter) : m_pFilter(pFilter) {
    BuildMediaType(0, &m_CurrentMediaType);
}

CVCamOutputPin::~CVCamOutputPin() {
    Disconnect();
    if (m_CurrentMediaType.cbFormat != 0) {
        CoTaskMemFree(m_CurrentMediaType.pbFormat);
    }
}

STDMETHODIMP CVCamOutputPin::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPin_Local) {
        *ppv = static_cast<IPin*>(this);
    } else if (riid == IID_IAMStreamConfig_Local) {
        *ppv = static_cast<IAMStreamConfig*>(this);
    } else if (riid == IID_IKsPropertySet_Local) {
        *ppv = static_cast<IKsPropertySet*>(this);
    } else if (riid == IID_IQualityControl_Local) {
        *ppv = static_cast<IQualityControl*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CVCamOutputPin::AddRef() { return ++m_cRef; }
STDMETHODIMP_(ULONG) CVCamOutputPin::Release() {
    ULONG r = --m_cRef;
    if (r == 0) delete this;
    return r;
}

bool CVCamOutputPin::BuildMediaType(int index, AM_MEDIA_TYPE* pmt) {
    if (!pmt) return false;
    std::memset(pmt, 0, sizeof(AM_MEDIA_TYPE));

    uint32_t w = 1920, h = 1080;
    REFERENCE_TIME avg_time = 333333; // 30 FPS
    GUID subtype = MEDIASUBTYPE_NV12_Local;

    switch (index) {
    case 0: // 1080p30 NV12
        w = 1920; h = 1080; avg_time = 333333; subtype = MEDIASUBTYPE_NV12_Local; break;
    case 1: // 1080p60 NV12
        w = 1920; h = 1080; avg_time = 166666; subtype = MEDIASUBTYPE_NV12_Local; break;
    case 2: // 4K30 NV12
        w = 3840; h = 2160; avg_time = 333333; subtype = MEDIASUBTYPE_NV12_Local; break;
    case 3: // 4K60 NV12
        w = 3840; h = 2160; avg_time = 166666; subtype = MEDIASUBTYPE_NV12_Local; break;
    case 4: // 720p30 NV12
        w = 1280; h = 720; avg_time = 333333; subtype = MEDIASUBTYPE_NV12_Local; break;
    case 5: // 720p60 NV12
        w = 1280; h = 720; avg_time = 166666; subtype = MEDIASUBTYPE_NV12_Local; break;
    case 6: // 1080p30 YUY2
        w = 1920; h = 1080; avg_time = 333333; subtype = MEDIASUBTYPE_YUY2_Local; break;
    case 7: // 1080p60 YUY2
        w = 1920; h = 1080; avg_time = 166666; subtype = MEDIASUBTYPE_YUY2_Local; break;
    default:
        return false;
    }

    pmt->majortype = MEDIATYPE_Video_Local;
    pmt->subtype = subtype;
    pmt->bFixedSizeSamples = TRUE;
    pmt->bTemporalCompression = FALSE;
    pmt->formattype = FORMAT_VideoInfo_Local;
    pmt->cbFormat = sizeof(VIDEOINFOHEADER);
    pmt->pUnk = nullptr;

    auto* vih = static_cast<VIDEOINFOHEADER*>(CoTaskMemAlloc(sizeof(VIDEOINFOHEADER)));
    if (!vih) return false;
    std::memset(vih, 0, sizeof(VIDEOINFOHEADER));
    vih->AvgTimePerFrame = avg_time;
    vih->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih->bmiHeader.biWidth = w;
    vih->bmiHeader.biHeight = h;
    vih->bmiHeader.biPlanes = 1;
    vih->bmiHeader.biBitCount = (subtype == MEDIASUBTYPE_NV12_Local) ? 12 : 16;
    vih->bmiHeader.biCompression = (subtype == MEDIASUBTYPE_NV12_Local) ? 0x3231564E : 0x32595559;
    vih->bmiHeader.biSizeImage = (w * h * vih->bmiHeader.biBitCount) / 8;

    pmt->lSampleSize = vih->bmiHeader.biSizeImage;
    pmt->pbFormat = reinterpret_cast<BYTE*>(vih);
    return true;
}

bool CVCamOutputPin::CopyCurrentMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return false;
    std::lock_guard<std::mutex> lock(m_FormatMutex);
    auto* copy = CreateMediaType(&m_CurrentMediaType);
    if (!copy) return false;
    std::memcpy(pmt, copy, sizeof(AM_MEDIA_TYPE));
    CoTaskMemFree(copy);
    return true;
}

void CVCamOutputPin::ConfigureAllocatorLocked() {
    if (!m_pMemInputPin) return;

    long required_bytes = std::max<long>(static_cast<long>(m_CurrentMediaType.lSampleSize), kMaxSampleBufferBytes);
    ALLOCATOR_PROPERTIES prop{}, actual{};
    prop.cBuffers = 4;
    prop.cbBuffer = required_bytes;
    prop.cbAlign = 1;

    if (!m_pAllocator) {
        m_pMemInputPin->GetAllocator(&m_pAllocator);
    }

    bool allocator_ok = false;
    if (m_pAllocator) {
        m_pAllocator->Decommit();
        if (SUCCEEDED(m_pAllocator->SetProperties(&prop, &actual)) &&
            actual.cbBuffer >= static_cast<long>(m_CurrentMediaType.lSampleSize)) {
            allocator_ok = true;
        } else {
            m_pAllocator->Release();
            m_pAllocator = nullptr;
        }
    }

    if (!allocator_ok) {
        if (SUCCEEDED(CoCreateInstance(CLSID_MemoryAllocator_Local, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_IMemAllocator_Local, reinterpret_cast<void**>(&m_pAllocator))) && m_pAllocator) {
            m_pAllocator->SetProperties(&prop, &actual);
        }
    }

    if (m_pAllocator) {
        m_pMemInputPin->NotifyAllocator(m_pAllocator, FALSE);
        m_pAllocator->Commit();
    }
}

STDMETHODIMP CVCamOutputPin::Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) {
    if (!pReceivePin) return E_POINTER;
    if (m_pConnectedPin) return VFW_E_ALREADY_CONNECTED;

    if (pmt) {
        if (pmt->majortype != GUID_NULL && pmt->majortype != MEDIATYPE_Video_Local) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
        if (pmt->subtype != GUID_NULL &&
            pmt->subtype != MEDIASUBTYPE_NV12_Local &&
            pmt->subtype != MEDIASUBTYPE_YUY2_Local) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
        if (pmt->formattype == FORMAT_VideoInfo_Local && pmt->pbFormat && pmt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
            if (QueryAccept(pmt) != S_OK) {
                return VFW_E_TYPE_NOT_ACCEPTED;
            }
            SetFormat(const_cast<AM_MEDIA_TYPE*>(pmt));
        }
    }

    // First try connecting with m_CurrentMediaType (which preserves any format set via IAMStreamConfig::SetFormat!)
    AM_MEDIA_TYPE current_mt{};
    if (!CopyCurrentMediaType(&current_mt)) {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pReceivePin->ReceiveConnection(static_cast<IPin*>(this), &current_mt);
    if ( current_mt.cbFormat != 0 && current_mt.pbFormat ) {
        CoTaskMemFree(current_mt.pbFormat);
    }

    // If the downstream pin rejected m_CurrentMediaType and no explicit pmt was forced, try our other capabilities
    if (FAILED(hr) && (!pmt || !pmt->pbFormat)) {
        for (int i = 0; i < kNumCapabilities; ++i) {
            AM_MEDIA_TYPE candidate{};
            if (!BuildMediaType(i, &candidate)) continue;
            hr = pReceivePin->ReceiveConnection(static_cast<IPin*>(this), &candidate);
            if (SUCCEEDED(hr)) {
                SetFormat(&candidate);
                if (candidate.cbFormat != 0 && candidate.pbFormat) CoTaskMemFree(candidate.pbFormat);
                break;
            }
            if (candidate.cbFormat != 0 && candidate.pbFormat) CoTaskMemFree(candidate.pbFormat);
        }
    }

    if (FAILED(hr)) return hr;

    m_pConnectedPin = pReceivePin;
    m_pConnectedPin->AddRef();

    hr = m_pConnectedPin->QueryInterface(IID_IMemInputPin_Local, reinterpret_cast<void**>(&m_pMemInputPin));
    if (SUCCEEDED(hr) && m_pMemInputPin) {
        std::lock_guard<std::mutex> lock(m_FormatMutex);
        ConfigureAllocatorLocked();
    }

    return S_OK;
}

STDMETHODIMP CVCamOutputPin::ReceiveConnection(IPin*, const AM_MEDIA_TYPE*) {
    return E_NOTIMPL;
}

STDMETHODIMP CVCamOutputPin::Disconnect() {
    std::lock_guard<std::mutex> lock(m_FormatMutex);
    if (m_pAllocator) {
        m_pAllocator->Decommit();
        m_pAllocator->Release();
        m_pAllocator = nullptr;
    }
    if (m_pMemInputPin) {
        m_pMemInputPin->Release();
        m_pMemInputPin = nullptr;
    }
    if (m_pConnectedPin) {
        m_pConnectedPin->Release();
        m_pConnectedPin = nullptr;
    }
    return S_OK;
}

STDMETHODIMP CVCamOutputPin::ConnectedTo(IPin** pPin) {
    if (!pPin) return E_POINTER;
    if (!m_pConnectedPin) return VFW_E_NOT_CONNECTED;
    *pPin = m_pConnectedPin;
    m_pConnectedPin->AddRef();
    return S_OK;
}

STDMETHODIMP CVCamOutputPin::ConnectionMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (!m_pConnectedPin) return VFW_E_NOT_CONNECTED;
    return CopyCurrentMediaType(pmt) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CVCamOutputPin::QueryPinInfo(PIN_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = m_pFilter;
    if (m_pFilter) m_pFilter->AddRef();
    pInfo->dir = PINDIR_OUTPUT;
    wcscpy_s(pInfo->achName, L"Capture");
    return S_OK;
}

STDMETHODIMP CVCamOutputPin::QueryDirection(PIN_DIRECTION* pPinDir) {
    if (!pPinDir) return E_POINTER;
    *pPinDir = PINDIR_OUTPUT;
    return S_OK;
}

STDMETHODIMP CVCamOutputPin::QueryId(LPWSTR* Id) {
    if (!Id) return E_POINTER;
    *Id = static_cast<LPWSTR>(CoTaskMemAlloc(sizeof(L"Capture")));
    if (!*Id) return E_OUTOFMEMORY;
    wcscpy_s(*Id, 8, L"Capture");
    return S_OK;
}

STDMETHODIMP CVCamOutputPin::QueryAccept(const AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (pmt->majortype != MEDIATYPE_Video_Local) return S_FALSE;
    if (pmt->subtype != MEDIASUBTYPE_NV12_Local && pmt->subtype != MEDIASUBTYPE_YUY2_Local) return S_FALSE;
    if (pmt->formattype == FORMAT_VideoInfo_Local && pmt->pbFormat && pmt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
        auto* vih = reinterpret_cast<const VIDEOINFOHEADER*>(pmt->pbFormat);
        if (vih->bmiHeader.biWidth <= 0 || std::abs(vih->bmiHeader.biHeight) <= 0) return S_FALSE;
    }
    return S_OK;
}

STDMETHODIMP CVCamOutputPin::EnumMediaTypes(IEnumMediaTypes** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new CVCamEnumMediaTypes(this);
    return S_OK;
}

STDMETHODIMP CVCamOutputPin::QueryInternalConnections(IPin**, ULONG* nPin) {
    if (!nPin) return E_POINTER;
    *nPin = 0;
    return S_OK;
}

STDMETHODIMP CVCamOutputPin::EndOfStream() { return S_OK; }
STDMETHODIMP CVCamOutputPin::BeginFlush() { return S_OK; }
STDMETHODIMP CVCamOutputPin::EndFlush() { return S_OK; }
STDMETHODIMP CVCamOutputPin::NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) { return S_OK; }

// --- IAMStreamConfig ---

STDMETHODIMP CVCamOutputPin::SetFormat(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (QueryAccept(pmt) != S_OK) return VFW_E_INVALIDMEDIATYPE;

    std::lock_guard<std::mutex> lock(m_FormatMutex);

    auto* p = CreateMediaType(pmt);
    if (!p) return E_OUTOFMEMORY;

    if (p->formattype == FORMAT_VideoInfo_Local && p->pbFormat && p->cbFormat >= sizeof(VIDEOINFOHEADER)) {
        auto* vih = reinterpret_cast<VIDEOINFOHEADER*>(p->pbFormat);
        uint32_t w = vih->bmiHeader.biWidth > 0 ? static_cast<uint32_t>(vih->bmiHeader.biWidth) : 1920u;
        uint32_t h = std::abs(vih->bmiHeader.biHeight) > 0 ? static_cast<uint32_t>(std::abs(vih->bmiHeader.biHeight)) : 1080u;
        m_Width = w;
        m_Height = h;
        m_AvgTimePerFrame = vih->AvgTimePerFrame > 0 ? vih->AvgTimePerFrame : 333333;

        bool is_yuy2 = (p->subtype == MEDIASUBTYPE_YUY2_Local);
        vih->bmiHeader.biWidth = static_cast<LONG>(w);
        vih->bmiHeader.biHeight = static_cast<LONG>(h);
        vih->bmiHeader.biPlanes = 1;
        vih->bmiHeader.biBitCount = is_yuy2 ? 16 : 12;
        vih->bmiHeader.biCompression = is_yuy2 ? 0x32595559 : 0x3231564E;
        vih->bmiHeader.biSizeImage = (w * h * vih->bmiHeader.biBitCount) / 8;
        p->lSampleSize = vih->bmiHeader.biSizeImage;
        p->bFixedSizeSamples = TRUE;
    }

    if (m_CurrentMediaType.cbFormat != 0 && m_CurrentMediaType.pbFormat) {
        CoTaskMemFree(m_CurrentMediaType.pbFormat);
    }
    std::memcpy(&m_CurrentMediaType, p, sizeof(AM_MEDIA_TYPE));
    CoTaskMemFree(p);

    if (m_pMemInputPin) {
        ConfigureAllocatorLocked();
    }
    return S_OK;
}

STDMETHODIMP CVCamOutputPin::GetFormat(AM_MEDIA_TYPE** ppmt) {
    if (!ppmt) return E_POINTER;
    std::lock_guard<std::mutex> lock(m_FormatMutex);
    *ppmt = CreateMediaType(&m_CurrentMediaType);
    return *ppmt ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CVCamOutputPin::GetNumberOfCapabilities(int* piCount, int* piSize) {
    if (!piCount || !piSize) return E_POINTER;
    *piCount = kNumCapabilities;
    *piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

STDMETHODIMP CVCamOutputPin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) {
    if (!ppmt || !pSCC) return E_POINTER;
    if (iIndex < 0 || iIndex >= kNumCapabilities) return S_FALSE;

    *ppmt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (!*ppmt) return E_OUTOFMEMORY;
    if (!BuildMediaType(iIndex, *ppmt)) {
        CoTaskMemFree(*ppmt);
        *ppmt = nullptr;
        return S_FALSE;
    }

    auto* vih = reinterpret_cast<VIDEOINFOHEADER*>((*ppmt)->pbFormat);
    LONG w = vih ? vih->bmiHeader.biWidth : 1920;
    LONG h = vih ? std::abs(vih->bmiHeader.biHeight) : 1080;
    REFERENCE_TIME avg_time = (vih && vih->AvgTimePerFrame > 0) ? vih->AvgTimePerFrame : 333333;

    auto* scc = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(pSCC);
    std::memset(scc, 0, sizeof(VIDEO_STREAM_CONFIG_CAPS));
    scc->guid = FORMAT_VideoInfo_Local;
    scc->VideoStandard = 0;
    scc->InputSize.cx = w;
    scc->InputSize.cy = h;
    scc->MinCroppingSize.cx = w;
    scc->MinCroppingSize.cy = h;
    scc->MaxCroppingSize.cx = w;
    scc->MaxCroppingSize.cy = h;
    scc->CropGranularityX = 2;
    scc->CropGranularityY = 2;
    scc->MinOutputSize.cx = w;
    scc->MinOutputSize.cy = h;
    scc->MaxOutputSize.cx = w;
    scc->MaxOutputSize.cy = h;
    scc->OutputGranularityX = 2;
    scc->OutputGranularityY = 2;
    scc->MinFrameInterval = avg_time;
    scc->MaxFrameInterval = avg_time;
    scc->MinBitsPerSecond = 5000000;
    scc->MaxBitsPerSecond = 100000000;
    return S_OK;
}

// --- IKsPropertySet ---

STDMETHODIMP CVCamOutputPin::Set(REFGUID, DWORD, LPVOID, DWORD, LPVOID, DWORD) {
    return E_NOTIMPL;
}

STDMETHODIMP CVCamOutputPin::Get(REFGUID guidPropSet, DWORD dwPropID, LPVOID, DWORD, LPVOID pPropData, DWORD cbPropData, DWORD* pcbReturned) {
    if (guidPropSet == AMPROPSETID_Pin_Local && dwPropID == 0 /* AMPROPERTY_PIN_CATEGORY */) {
        if (!pPropData || cbPropData < sizeof(GUID)) {
            if (pcbReturned) *pcbReturned = sizeof(GUID);
            return cbPropData == 0 ? S_OK : E_UNEXPECTED;
        }
        *reinterpret_cast<GUID*>(pPropData) = PIN_CATEGORY_CAPTURE_Local;
        if (pcbReturned) *pcbReturned = sizeof(GUID);
        return S_OK;
    }
    return E_NOTIMPL;
}

STDMETHODIMP CVCamOutputPin::QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport) {
    if (guidPropSet == AMPROPSETID_Pin_Local && dwPropID == 0) {
        if (pTypeSupport) *pTypeSupport = 1 /* KSPROPERTY_SUPPORT_GET */;
        return S_OK;
    }
    return E_NOTIMPL;
}

// --- CVCamFilter Implementation ---

CVCamFilter::CVCamFilter() {
    m_pPin = new CVCamOutputPin(this);
}

CVCamFilter::~CVCamFilter() {
    Stop();
    if (m_pPin) {
        m_pPin->Release();
        m_pPin = nullptr;
    }
    if (m_pClock) {
        m_pClock->Release();
        m_pClock = nullptr;
    }
    if (m_pName) {
        CoTaskMemFree(m_pName);
        m_pName = nullptr;
    }
}

STDMETHODIMP CVCamFilter::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IMediaFilter_Local || riid == IID_IBaseFilter_Local) {
        *ppv = static_cast<IBaseFilter*>(this);
    } else if (riid == IID_IAMFilterMiscFlags_Local) {
        *ppv = static_cast<IAMFilterMiscFlags*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CVCamFilter::AddRef() { return ++m_cRef; }
STDMETHODIMP_(ULONG) CVCamFilter::Release() {
    ULONG r = --m_cRef;
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP CVCamFilter::GetClassID(CLSID* pClassID) {
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_CameraPhoneVCam;
    return S_OK;
}

STDMETHODIMP CVCamFilter::Stop() {
    StopWorker();
    if (m_pPin && m_pPin->GetAllocator()) {
        m_pPin->GetAllocator()->Decommit();
    }
    m_State = State_Stopped;
    return S_OK;
}

STDMETHODIMP CVCamFilter::Pause() {
    if (m_pPin && m_pPin->GetAllocator()) {
        m_pPin->GetAllocator()->Commit();
    }
    m_State = State_Paused;
    return S_OK;
}

STDMETHODIMP CVCamFilter::Run(REFERENCE_TIME) {
    if (m_pPin && m_pPin->GetAllocator()) {
        m_pPin->GetAllocator()->Commit();
    }
    m_State = State_Running;
    StartWorker();
    return S_OK;
}

STDMETHODIMP CVCamFilter::GetState(DWORD, FILTER_STATE* State) {
    if (!State) return E_POINTER;
    *State = m_State;
    return S_OK;
}

STDMETHODIMP CVCamFilter::SetSyncSource(IReferenceClock* pClock) {
    if (m_pClock) m_pClock->Release();
    m_pClock = pClock;
    if (m_pClock) m_pClock->AddRef();
    return S_OK;
}

STDMETHODIMP CVCamFilter::GetSyncSource(IReferenceClock** pClock) {
    if (!pClock) return E_POINTER;
    *pClock = m_pClock;
    if (m_pClock) m_pClock->AddRef();
    return S_OK;
}

STDMETHODIMP CVCamFilter::EnumPins(IEnumPins** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new CVCamEnumPins(m_pPin);
    return S_OK;
}

STDMETHODIMP CVCamFilter::FindPin(LPCWSTR Id, IPin** ppPin) {
    if (!ppPin) return E_POINTER;
    if (Id && wcscmp(Id, L"Capture") == 0) {
        *ppPin = m_pPin;
        m_pPin->AddRef();
        return S_OK;
    }
    *ppPin = nullptr;
    return VFW_E_NOT_FOUND;
}

STDMETHODIMP CVCamFilter::QueryFilterInfo(FILTER_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    wcscpy_s(pInfo->achName, kVCamFriendlyName);
    pInfo->pGraph = m_pGraph;
    if (m_pGraph) m_pGraph->AddRef();
    return S_OK;
}

STDMETHODIMP CVCamFilter::JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) {
    m_pGraph = pGraph;
    if (m_pName) {
        CoTaskMemFree(m_pName);
        m_pName = nullptr;
    }
    if (pName) {
        size_t len = wcslen(pName) + 1;
        m_pName = static_cast<LPWSTR>(CoTaskMemAlloc(len * sizeof(wchar_t)));
        if (m_pName) wcscpy_s(m_pName, len, pName);
    }
    return S_OK;
}

STDMETHODIMP CVCamFilter::QueryVendorInfo(LPWSTR* pVendorInfo) {
    if (!pVendorInfo) return E_POINTER;
    *pVendorInfo = static_cast<LPWSTR>(CoTaskMemAlloc(sizeof(L"CameraPhone Team")));
    if (!*pVendorInfo) return E_OUTOFMEMORY;
    wcscpy_s(*pVendorInfo, 17, L"CameraPhone Team");
    return S_OK;
}

static void EnsureCompanionRunning() {
    HANDLE hObs = OpenMutexW(SYNCHRONIZE, FALSE, camphone::kOBSStreamingMutexName);
    if (hObs) {
        CloseHandle(hObs);
        return;
    }
    HANDLE hComp = OpenMutexA(SYNCHRONIZE, FALSE, "Local\\CameraPhone_Companion_Mutex");
    if (hComp) {
        CloseHandle(hComp);
        return;
    }
    const char* exe_path = "C:\\Program Files\\CameraPhone\\CameraPhone.exe";
    if (GetFileAttributesA(exe_path) != INVALID_FILE_ATTRIBUTES) {
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "\"%s\" --background", exe_path);
        if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
}

static void BlitOrScaleNV12(const uint8_t* src, uint32_t src_w, uint32_t src_h,
                            uint8_t* dst, uint32_t dst_w, uint32_t dst_h, bool dst_yuy2) {
    const uint8_t* src_y  = src;
    const uint8_t* src_uv = src + (src_w * src_h);

    if (!dst_yuy2) {
        if (src_w == dst_w && src_h == dst_h) {
            std::memcpy(dst, src, (dst_w * dst_h * 3) / 2);
            return;
        }
        // Precompute X lookup tables once per frame and reuse duplicate rows via memcpy
        std::vector<uint32_t> x_map(dst_w);
        for (uint32_t x = 0; x < dst_w; ++x) {
            x_map[x] = (x * src_w) / dst_w;
        }
        uint32_t half_dst_w = dst_w / 2;
        uint32_t half_src_w = src_w / 2;
        std::vector<uint32_t> uv_x_map(half_dst_w);
        for (uint32_t x = 0; x < half_dst_w; ++x) {
            uv_x_map[x] = ((x * half_src_w) / half_dst_w) * 2;
        }

        uint8_t* dst_y  = dst;
        uint8_t* dst_uv = dst + (dst_w * dst_h);
        uint32_t prev_sy = UINT32_MAX;
        for (uint32_t y = 0; y < dst_h; ++y) {
            uint32_t sy = (y * src_h) / dst_h;
            uint8_t* drow = dst_y + y * dst_w;
            if (sy == prev_sy && y > 0) {
                std::memcpy(drow, drow - dst_w, dst_w);
                continue;
            }
            prev_sy = sy;
            const uint8_t* srow = src_y + sy * src_w;
            for (uint32_t x = 0; x < dst_w; ++x) {
                drow[x] = srow[x_map[x]];
            }
        }

        uint32_t half_dst_h = dst_h / 2;
        uint32_t half_src_h = src_h / 2;
        prev_sy = UINT32_MAX;
        for (uint32_t y = 0; y < half_dst_h; ++y) {
            uint32_t sy = (y * half_src_h) / half_dst_h;
            uint8_t* drow = dst_uv + y * dst_w;
            if (sy == prev_sy && y > 0) {
                std::memcpy(drow, drow - dst_w, dst_w);
                continue;
            }
            prev_sy = sy;
            const uint8_t* srow = src_uv + sy * src_w;
            for (uint32_t x = 0; x < half_dst_w; ++x) {
                uint32_t sx2 = uv_x_map[x];
                drow[x * 2]     = srow[sx2];
                drow[x * 2 + 1] = srow[sx2 + 1];
            }
        }
    } else {
        // Convert NV12 -> YUY2 (with scaling if needed)
        std::vector<uint32_t> x_map(dst_w);
        for (uint32_t x = 0; x < dst_w; ++x) {
            x_map[x] = (x * src_w) / dst_w;
        }
        uint32_t prev_sy = UINT32_MAX;
        for (uint32_t y = 0; y < dst_h; ++y) {
            uint32_t sy = (y * src_h) / dst_h;
            uint8_t* drow = dst + y * (dst_w * 2);
            if (sy == prev_sy && y > 0) {
                std::memcpy(drow, drow - (dst_w * 2), dst_w * 2);
                continue;
            }
            prev_sy = sy;
            const uint8_t* sy_row = src_y + sy * src_w;
            const uint8_t* suv_row = src_uv + (sy / 2) * src_w;
            for (uint32_t x = 0; x < dst_w; x += 2) {
                uint32_t sx0 = x_map[x];
                uint32_t sx1 = x_map[x + 1];
                uint32_t uv_idx = (sx0 / 2) * 2;
                drow[x * 2 + 0] = sy_row[sx0];
                drow[x * 2 + 1] = suv_row[uv_idx];
                drow[x * 2 + 2] = sy_row[sx1];
                drow[x * 2 + 3] = suv_row[uv_idx + 1];
            }
        }
    }
}

void CVCamFilter::StartWorker() {
    if (m_WorkerRunning.exchange(true)) return;
    EnsureCompanionRunning();
    m_Worker = std::thread([this]() { WorkerLoop(); });
}

void CVCamFilter::StopWorker() {
    if (!m_WorkerRunning.exchange(false)) return;
    if (m_Worker.joinable()) {
        m_Worker.join();
    }
}

void CVCamFilter::WorkerLoop() {
    camphone::VCamSharedMemoryReader shmem_reader;
    shmem_reader.open();

    uint64_t frame_index = 0;
    auto last_time = std::chrono::steady_clock::now();
    std::vector<uint8_t> payload;
    std::vector<uint8_t> standby_nv12;

    while (m_WorkerRunning.load()) {
        if (!m_pPin || !m_pPin->IsConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto* allocator = m_pPin->GetAllocator();
        auto* mem_input = m_pPin->GetMemInputPin();
        if (!allocator || !mem_input) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        IMediaSample* pSample = nullptr;
        HRESULT hr = allocator->GetBuffer(&pSample, nullptr, nullptr, 0);
        if (hr == static_cast<HRESULT>(0x80040211L) /* VFW_E_NOT_COMMITTED */) {
            allocator->Commit();
            hr = allocator->GetBuffer(&pSample, nullptr, nullptr, 0);
        }
        if (FAILED(hr) || !pSample) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        BYTE* pBuffer = nullptr;
        pSample->GetPointer(&pBuffer);
        long buf_len = pSample->GetSize();

        uint32_t w = 1920, h = 1080;
        bool is_yuy2 = false;
        REFERENCE_TIME avg_time = 333333;
        m_pPin->GetCurrentFormatSnapshot(w, h, is_yuy2, avg_time);
        uint32_t needed_bytes = is_yuy2 ? (w * h * 2) : ((w * h * 3) / 2);

        if (!pBuffer || buf_len < static_cast<long>(needed_bytes)) {
            pSample->Release();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        // Low-latency wakeup hint (up to 4ms), then read the latest active frame if fresh (< 2500ms)
        shmem_reader.wait_for_frame(4);

        bool has_frame = false;
        camphone::VCamSharedHeader hdr{};
        if (shmem_reader.read_frame(hdr, payload, 2500)) {
            uint32_t src_nv12_bytes = (hdr.width * hdr.height * 3) / 2;
            if (hdr.width > 0 && hdr.height > 0 && payload.size() >= src_nv12_bytes) {
                BlitOrScaleNV12(payload.data(), hdr.width, hdr.height, pBuffer, w, h, is_yuy2);
                has_frame = true;
            }
        }

        if (!has_frame) {
            if (!is_yuy2) {
                camphone::VCamStandbyRenderer::render_nv12(pBuffer, w, h, frame_index);
            } else {
                uint32_t nv12_sz = (w * h * 3) / 2;
                if (standby_nv12.size() < nv12_sz) standby_nv12.resize(nv12_sz);
                camphone::VCamStandbyRenderer::render_nv12(standby_nv12.data(), w, h, frame_index);
                BlitOrScaleNV12(standby_nv12.data(), w, h, pBuffer, w, h, true);
            }
        }

        pSample->SetActualDataLength(needed_bytes);
        pSample->SetSyncPoint(TRUE);

        REFERENCE_TIME start_rt = frame_index * avg_time;
        REFERENCE_TIME stop_rt = start_rt + avg_time;
        pSample->SetTime(&start_rt, &stop_rt);

        mem_input->Receive(pSample);
        pSample->Release();
        ++frame_index;

        auto target_delay = std::chrono::nanoseconds(avg_time * 100);
        auto elapsed = std::chrono::steady_clock::now() - last_time;
        if (elapsed < target_delay) {
            std::this_thread::sleep_for(target_delay - elapsed);
        }
        last_time = std::chrono::steady_clock::now();
    }
}

// --- Class Factory & Registration ---

class CVCamClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_cRef; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --m_cRef;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        auto* filter = new CVCamFilter();
        HRESULT hr = filter->QueryInterface(riid, ppvObject);
        filter->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) InterlockedIncrement(&g_server_locks);
        else InterlockedDecrement(&g_server_locks);
        return S_OK;
    }

private:
    std::atomic<LONG> m_cRef{1};
};

extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_module_handle = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    return g_server_locks == 0 ? S_OK : S_FALSE;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid == CLSID_CameraPhoneVCam) {
        auto* factory = new CVCamClassFactory();
        HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

static void FormatGuidString(const GUID& guid, wchar_t* out, size_t max_len) {
    StringFromGUID2(guid, out, static_cast<int>(max_len));
}

extern "C" HRESULT WINAPI DllRegisterServer() {
    wchar_t dll_path[MAX_PATH] = {0};
    if (GetModuleFileNameW(g_module_handle, dll_path, MAX_PATH) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    wchar_t clsid_str[64] = {0};
    FormatGuidString(CLSID_CameraPhoneVCam, clsid_str, 64);

    // 1. Register in HKCR\CLSID\{CLSID}
    std::wstring clsid_key = L"CLSID\\" + std::wstring(clsid_str);
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, clsid_key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(kVCamFriendlyName), static_cast<DWORD>((wcslen(kVCamFriendlyName) + 1) * sizeof(wchar_t)));
        
        HKEY hInproc = nullptr;
        if (RegCreateKeyExW(hKey, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &hInproc, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(hInproc, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(dll_path), static_cast<DWORD>((wcslen(dll_path) + 1) * sizeof(wchar_t)));
            const wchar_t* tm = L"Both";
            RegSetValueExW(hInproc, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(tm), static_cast<DWORD>((wcslen(tm) + 1) * sizeof(wchar_t)));
            RegCloseKey(hInproc);
        }
        RegCloseKey(hKey);
    }

    // 2. Register under DirectShow Video Input Device Category:
    // HKCR\CLSID\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\Instance\{CLSID}
    wchar_t cat_str[64] = {0};
    FormatGuidString(CLSID_VideoInputDeviceCategory_Local, cat_str, 64);
    std::wstring instance_key = L"CLSID\\" + std::wstring(cat_str) + L"\\Instance\\" + std::wstring(clsid_str);

    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, instance_key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"FriendlyName", 0, REG_SZ, reinterpret_cast<const BYTE*>(kVCamFriendlyName), static_cast<DWORD>((wcslen(kVCamFriendlyName) + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"CLSID", 0, REG_SZ, reinterpret_cast<const BYTE*>(clsid_str), static_cast<DWORD>((wcslen(clsid_str) + 1) * sizeof(wchar_t)));

        BYTE filter_data[44] = {
            0x02, 0x00, 0x00, 0x00,
            0x00, 0x06, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x30, 0x30, 0x70, 0x69,
            0x08, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x76, 0x69, 0x64, 0x73,
            0x00, 0x00, 0x00, 0x00
        };
        RegSetValueExW(hKey, L"FilterData", 0, REG_BINARY, filter_data, sizeof(filter_data));
        RegCloseKey(hKey);
    }

    return S_OK;
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
    wchar_t clsid_str[64] = {0};
    FormatGuidString(CLSID_CameraPhoneVCam, clsid_str, 64);
    wchar_t cat_str[64] = {0};
    FormatGuidString(CLSID_VideoInputDeviceCategory_Local, cat_str, 64);

    std::wstring instance_key = L"CLSID\\" + std::wstring(cat_str) + L"\\Instance\\" + std::wstring(clsid_str);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, instance_key.c_str());

    std::wstring clsid_key = L"CLSID\\" + std::wstring(clsid_str);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, clsid_key.c_str());

    return S_OK;
}
