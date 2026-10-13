// Standalone host: proves the proxy DLL is loadable and its exports are callable.
// Build: cl /nologo /O2 /EHsc test_host.cpp /link /OUT:test_host.exe
#include <windows.h>
#include <cstdio>

int main() {
    const char* p = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Borderlands 2 VR\\Binaries\\Win64\\openvr_api.dll";
    HMODULE h = LoadLibraryA(p);
    printf("LoadLibraryA(openvr_api.dll) -> %p (err %lu)\n", (void*)h, GetLastError());
    if (!h) return 1;

    auto tok = (unsigned int (*)(void))GetProcAddress(h, "VR_GetInitToken");
    auto hmd = (bool (*)(void))GetProcAddress(h, "VR_IsHmdPresent");
    auto ver = (bool (*)(const char*))GetProcAddress(h, "VR_IsInterfaceVersionValid");

    printf("VR_GetInitToken()              -> %u\n", tok ? tok() : 0xDEADBEEFu);
    printf("VR_IsHmdPresent()              -> %d\n", hmd ? (int)hmd() : -1);
    printf("VR_IsInterfaceVersionValid(IVRSystem_012) -> %d\n", ver ? (int)ver("IVRSystem_012") : -1);
    return 0;
}
