// Probe: is a game-folder LibOVRRT64_1.dll loaded by the game?
// Logs to 3 places so a failed write can't hide the answer.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

static void Pwrite(const char* path, const char* s) {
    FILE* f = nullptr;
    fopen_s(&f, path, "a");
    if (f) { fprintf(f, "%s\n", s); fclose(f); }
}

static void Plog(const char* s) {
    OutputDebugStringA(s);
    OutputDebugStringA("\n");
    Pwrite("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Borderlands 2 VR\\Binaries\\Win64\\ovr_probe.log", s);
    char dir[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableA("LOCALAPPDATA", dir, MAX_PATH) > 0) {
        strncat_s(dir, MAX_PATH, "\\BL2VR-grip", _TRUNCATE);
        CreateDirectoryA(dir, nullptr);
        strncat_s(dir, MAX_PATH, "\\ovr_probe.log", _TRUNCATE);
        Pwrite(dir, s);
    }
    Pwrite("C:\\Users\\Rimach\\Desktop\\BL2VR-grip\\ovr_probe.log", s);
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        char self[MAX_PATH] = { 0 };
        char exe[MAX_PATH] = { 0 };
        GetModuleFileNameA(h, self, MAX_PATH);
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        Plog("=== LibOVRRT64_1.dll PROBE attached ===");
        Plog(self);
        Plog(exe);
    } else if (reason == DLL_PROCESS_DETACH) {
        Plog("=== probe detached ===");
    }
    return TRUE;
}

