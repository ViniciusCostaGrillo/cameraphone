#include <windows.h>
#include <initguid.h>
#include <dshow.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdio.h>

int main() {
    auto p = [](const char* name, const GUID& g) {
        printf("static const GUID %s_Local = \n    { 0x%08lx, 0x%04x, 0x%04x, { 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x } };\n",
               name, (unsigned long)g.Data1, g.Data2, g.Data3,
               g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
               g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    };
    p("CLSID_VideoInputDeviceCategory", CLSID_VideoInputDeviceCategory);
    p("MEDIATYPE_Video", MEDIATYPE_Video);
    p("MEDIASUBTYPE_NV12", MEDIASUBTYPE_NV12);
    p("MEDIASUBTYPE_YUY2", MEDIASUBTYPE_YUY2);
    p("MEDIASUBTYPE_RGB24", MEDIASUBTYPE_RGB24);
    p("MEDIASUBTYPE_RGB32", MEDIASUBTYPE_RGB32);
    p("FORMAT_VideoInfo", FORMAT_VideoInfo);
    p("PIN_CATEGORY_CAPTURE", PIN_CATEGORY_CAPTURE);
    p("AMPROPSETID_Pin", AMPROPSETID_Pin);
    p("CLSID_MemoryAllocator", CLSID_MemoryAllocator);
    p("IID_IPin", IID_IPin);
    p("IID_IEnumPins", IID_IEnumPins);
    p("IID_IEnumMediaTypes", IID_IEnumMediaTypes);
    p("IID_IMediaFilter", IID_IMediaFilter);
    p("IID_IBaseFilter", IID_IBaseFilter);
    p("IID_IMemInputPin", IID_IMemInputPin);
    p("IID_IMemAllocator", IID_IMemAllocator);
    p("IID_IAMStreamConfig", IID_IAMStreamConfig);
    p("IID_IKsPropertySet", IID_IKsPropertySet);
    p("IID_IAMFilterMiscFlags", IID_IAMFilterMiscFlags);
    p("IID_IQualityControl", IID_IQualityControl);
    return 0;
}
