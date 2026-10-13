// openvr_api.dll proxy for Borderlands 2 VR  (Route A - "Evening 1", logging only)
//
// What it does:
//   * every export is forwarded to the real DLL, renamed to openvr_api_orig.dll
//     (10 of them are pure linker forwarders, see proxy.def)
//   * VR_InitInternal / VR_GetGenericInterface / VR_ShutdownInternal are
//     implemented here so we can log what the game asks SteamVR for
//
// Nothing is modified yet - this build only records. It answers "which
// interface + which call does the game use to read the controller poses?".
//
// Build: x64 MSVC, see build.bat.  Output: openvr_api.dll

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <tlhelp32.h>
#include "ovrhook.h"

// ---------------------------------------------------------------- logging ---
static FILE*            g_log = nullptr;
static CRITICAL_SECTION g_cs;
static HMODULE          g_self = nullptr;

static void LogOpen() {
    char path[MAX_PATH] = { 0 };

    // 1) try next to the DLL
    GetModuleFileNameA(g_self, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) slash[1] = 0;
    strncat_s(path, MAX_PATH, "openvr_proxy.log", _TRUNCATE);
    fopen_s(&g_log, path, "w");
    if (g_log) return;

    // 2) fall back to %LOCALAPPDATA%\BL2VR-grip\ (game folder may be read-only)
    char dir[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableA("LOCALAPPDATA", dir, MAX_PATH) > 0) {
        strncat_s(dir, MAX_PATH, "\\BL2VR-grip", _TRUNCATE);
        CreateDirectoryA(dir, nullptr);
        strncat_s(dir, MAX_PATH, "\\openvr_proxy.log", _TRUNCATE);
        fopen_s(&g_log, dir, "w");
    }
}

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_cs);
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_cs);
}

// used by the ovr hook payload (ovrhook.cpp)
void Bl2Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_cs);
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_cs);
}

// ------------------------------------------------------------- real DLL -----
static HMODULE Real() {
    static HMODULE real = nullptr;
    if (!real) {
        char path[MAX_PATH] = { 0 };
        GetModuleFileNameA(g_self, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        if (slash) slash[1] = 0;
        strncat_s(path, MAX_PATH, "openvr_api_orig.dll", _TRUNCATE);
        Log("Real(): loading \"%s\" ...", path);
        real = LoadLibraryA(path);
        Log("Real(): -> %p", (void*)real);
    }
    return real;
}

// ------------------------------------------------- exports -----------------
// EVRInitError is passed as int32_t (same ABI). The 8 functions the game does
// not reference are forwarded too, so our export table matches the original.
#define GETFN(T, N) Log("  call %s", N); static T fn = nullptr; if (!fn) fn = (T)GetProcAddress(Real(), N)
extern "C" {

__declspec(dllexport) intptr_t VR_InitInternal(int32_t* peError, int32_t appType) {
    typedef intptr_t (*FN)(int32_t*, int32_t);
    GETFN(FN, "VR_InitInternal");
    intptr_t r = fn ? fn(peError, appType) : 0;
    Log("VR_InitInternal(appType=%d) -> 0x%llX", appType, (unsigned long long)r);
    return r;
}

__declspec(dllexport) uint32_t VR_GetInitToken() {
    typedef uint32_t (*FN)();
    GETFN(FN, "VR_GetInitToken");
    uint32_t r = fn ? fn() : 0;
    Log("VR_GetInitToken -> %u", r);
    return r;
}

__declspec(dllexport) bool VR_IsInterfaceVersionValid(const char* v) {
    typedef bool (*FN)(const char*);
    GETFN(FN, "VR_IsInterfaceVersionValid");
    bool r = fn ? fn(v) : false;
    Log("VR_IsInterfaceVersionValid(\"%s\") -> %d", v ? v : "(null)", (int)r);
    return r;
}

__declspec(dllexport) void VR_ShutdownInternal() {
    typedef void (*FN)();
    GETFN(FN, "VR_ShutdownInternal");
    Log("VR_ShutdownInternal()");
    if (fn) fn();
}

__declspec(dllexport) intptr_t VR_GetGenericInterface(const char* v, int32_t* peError) {
    typedef intptr_t (*FN)(const char*, int32_t*);
    GETFN(FN, "VR_GetGenericInterface");
    intptr_t r = fn ? fn(v, peError) : 0;
    Log("VR_GetGenericInterface(\"%s\") -> 0x%llX", v ? v : "(null)", (unsigned long long)r);
    return r;
}

__declspec(dllexport) bool VR_IsHmdPresent() {
    typedef bool (*FN)();
    GETFN(FN, "VR_IsHmdPresent");
    bool r = fn ? fn() : false;
    Log("VR_IsHmdPresent -> %d", (int)r);
    return r;
}

// ---- not referenced by Borderlands2VR.exe; forwarded for export parity ----
__declspec(dllexport) void* VRControlPanel() {
    typedef void* (*FN)(); GETFN(FN, "VRControlPanel"); return fn ? fn() : nullptr;
}
__declspec(dllexport) void* VRDashboardManager() {
    typedef void* (*FN)(); GETFN(FN, "VRDashboardManager"); return fn ? fn() : nullptr;
}
__declspec(dllexport) void* VRTrackedCameraInternal() {
    typedef void* (*FN)(); GETFN(FN, "VRTrackedCameraInternal"); return fn ? fn() : nullptr;
}
__declspec(dllexport) const char* VR_GetStringForHmdError(int32_t e) {
    typedef const char* (*FN)(int32_t); GETFN(FN, "VR_GetStringForHmdError"); return fn ? fn(e) : "";
}
__declspec(dllexport) const char* VR_GetVRInitErrorAsEnglishDescription(int32_t e) {
    typedef const char* (*FN)(int32_t); GETFN(FN, "VR_GetVRInitErrorAsEnglishDescription"); return fn ? fn(e) : "";
}
__declspec(dllexport) const char* VR_GetVRInitErrorAsSymbol(int32_t e) {
    typedef const char* (*FN)(int32_t); GETFN(FN, "VR_GetVRInitErrorAsSymbol"); return fn ? fn(e) : "";
}
__declspec(dllexport) bool VR_IsRuntimeInstalled() {
    typedef bool (*FN)(); GETFN(FN, "VR_IsRuntimeInstalled"); return fn ? fn() : false;
}
__declspec(dllexport) const char* VR_RuntimePath() {
    typedef const char* (*FN)(); GETFN(FN, "VR_RuntimePath"); return fn ? fn() : "";
}

#undef GETFN
} // extern "C"

// ------------------------------------------------------------ diagnostics ---
static void SnapshotModules(const char* tag) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) { Log("module snapshot failed (%s)", tag); return; }
    MODULEENTRY32 me; me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) {
        do { Log("MODULE[%s] %p %s", tag, (void*)me.modBaseAddr, me.szExePath); } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
}

static DWORD WINAPI ModuleLogThread(LPVOID) {
    Sleep(8000);  SnapshotModules("8s");
    Sleep(42000); SnapshotModules("50s");
    return 0;
}

// ---------------------------------------------------------------- DllMain ---
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = hModule;
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&g_cs);
        LogOpen();
        char exe[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        Log("=== openvr_api proxy attached (self=%p) ===", (void*)hModule);
        Log("host process: %s", exe);
        HANDLE th = CreateThread(nullptr, 0, ModuleLogThread, nullptr, 0, nullptr);
        if (th) CloseHandle(th);
        // start the Oculus pose-smoothing payload
        HANDLE oh = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)InstallOvrHookThread, nullptr, 0, nullptr);
        if (oh) CloseHandle(oh);
    } else if (reason == DLL_PROCESS_DETACH) {
        Log("=== openvr_api proxy detached ===");
        if (g_log) { fclose(g_log); g_log = nullptr; }
        DeleteCriticalSection(&g_cs);
    }
    return TRUE;
}
