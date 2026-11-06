#include <windows.h>
#include <initguid.h>
#include <dshow.h>
#include <stdio.h>

int main() {
    auto p = [](const char* name, const GUID& g) {
        printf("%s: {0x%08lx, 0x%04x, 0x%04x, {0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x}}\n",
               name, (unsigned long)g.Data1, g.Data2, g.Data3,
               g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
               g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    };
    p("CLSID_MemoryAllocator", CLSID_MemoryAllocator);
    p("IID_IMemAllocator", IID_IMemAllocator);
    p("IID_IMemInputPin", IID_IMemInputPin);
    p("IID_IBaseFilter", IID_IBaseFilter);
    p("IID_IPin", IID_IPin);
    return 0;
}
