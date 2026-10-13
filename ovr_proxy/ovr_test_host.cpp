// 1) try loading the DLL from the GAME folder (does DllMain even run there?)
// 2) dump the first bytes of VD's ovr_GetTrackingState (to plan the hook)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

int main() {
    printf("--- test A: load from the GAME folder ---\n");
    HMODULE a = LoadLibraryA("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Borderlands 2 VR\\Binaries\\Win64\\LibOVRRT64_1.dll");
    printf("LoadLibraryA(game-folder LibOVRRT64_1.dll) -> %p (err %lu)\n", (void*)a, GetLastError());

    printf("\n--- test B: load VD's real runtime and inspect the target ---\n");
    HMODULE v = LoadLibraryA("C:\\Program Files\\Virtual Desktop Streamer\\VirtualDesktop.LibOVRRT64_1.dll");
    printf("LoadLibraryA(VD runtime) -> %p (err %lu)\n", (void*)v, GetLastError());
    if (v) {
        void* f = (void*)GetProcAddress(v, "ovr_GetTrackingState");
        printf("ovr_GetTrackingState = %p\n", f);
        if (f) {
            unsigned char* p = (unsigned char*)f;
            printf("first 24 bytes:");
            for (int i = 0; i < 24; i++) printf(" %02X", p[i]);
            printf("\n");
        }
    }
    printf("\ndone\n");
    return 0;
}