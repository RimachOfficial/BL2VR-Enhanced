// LibOVRRT64_1.dll proxy for Borderlands 2 VR  (Oculus / LibOVR path)
//
// The game loads LibOVRRT64_1.dll from its OWN folder (Binaries\Win64) and it is
// really running on Virtual Desktop's Oculus runtime. So this DLL takes that
// name, forwards all 128 ovr_* exports to VD's real runtime, and hooks
// ovr_GetTrackingState to smooth the controller poses (One Euro filter).
//
// Config: ovr_proxy.ini next to this DLL - re-read every 2s, so you can tune it
// while the game is running. Build: build.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include "stubs.h"
#include "filters.h"

#define VD_RUNTIME "C:\\Program Files\\Virtual Desktop Streamer\\VirtualDesktop.LibOVRRT64_1.dll"

// ---------- Oculus structures (SDK 1.x layout) ----------
struct ovrVector3f { float x, y, z; };
struct ovrQuatf    { float x, y, z, w; };
struct ovrPosef    { ovrQuatf Orientation; ovrVector3f Position; };
struct ovrPoseStatef {
    ovrPosef     ThePose;
    ovrVector3f  AngularVelocity, LinearVelocity, AngularAcceleration, LinearAcceleration;
    double       TimeInSeconds;
    unsigned int Pad;
};
struct ovrTrackingState {
    double        TimeInSeconds;       // 0
    ovrPoseStatef HeadPose;            // 8
    unsigned int  StatusFlags;         // 104
    unsigned int  _pad0;               // 108
    ovrPoseStatef HandPoses[2];        // 112
    unsigned int  HandStatusFlags[2];  // 304
    ovrPosef      CalibratedOrigin;    // 312
    unsigned int  _pad1;               // 340
};
static_assert(sizeof(ovrPosef) == 28,          "ovrPosef layout mismatch");
static_assert(sizeof(ovrPoseStatef) == 96,     "ovrPoseStatef layout mismatch");
static_assert(sizeof(ovrTrackingState) == 344, "ovrTrackingState layout mismatch");
static_assert(offsetof(ovrTrackingState, HeadPose) == 8,        "HeadPose offset");
static_assert(offsetof(ovrTrackingState, HandPoses) == 112,     "HandPoses offset");
static_assert(offsetof(ovrTrackingState, HandStatusFlags) == 304, "HandStatusFlags offset");

#define HAND_LEFT  0
#define HAND_RIGHT 1
static const unsigned int STATUS_POSITION_TRACKED = 0x0002;

// ---------- logging ----------
static FILE* g_log = nullptr; static CRITICAL_SECTION g_cs; static HMODULE g_self = nullptr;
static char g_iniPath[MAX_PATH] = { 0 };

// ---------- config (ovr_proxy.ini, hot-reloaded) ----------
struct Config {
    int    enabled, log, logFrames, smoothHands, smoothHead;
    double minCutoff, beta, derivCutoff, minCutoffRot, betaRot, predictMs;
};
static Config g_cfg = { 1, 1, 400, 0, 0, 3.0, 1.0, 1.0, 2.0, 0.6, 0.0 };

static double ReadDouble(const char* key, double def) {
    char buf[128] = { 0 };
    GetPrivateProfileStringA("ovr_proxy", key, "", buf, sizeof(buf), g_iniPath);
    double d = 0.0;
    if (buf[0] && sscanf_s(buf, "%lf", &d) == 1) return d;
    return def;
}

static void LoadConfig() {
    g_cfg.enabled     = GetPrivateProfileIntA("ovr_proxy", "enabled", 1, g_iniPath);
    g_cfg.log         = GetPrivateProfileIntA("ovr_proxy", "log", 1, g_iniPath);
    g_cfg.logFrames   = GetPrivateProfileIntA("ovr_proxy", "log_frames", 400, g_iniPath);
    g_cfg.smoothHands = GetPrivateProfileIntA("ovr_proxy", "smooth_hands", 0, g_iniPath);
    g_cfg.smoothHead  = GetPrivateProfileIntA("ovr_proxy", "smooth_head", 0, g_iniPath);
    g_cfg.minCutoff    = ReadDouble("min_cutoff", 3.0);
    g_cfg.beta         = ReadDouble("beta", 1.0);
    g_cfg.derivCutoff  = ReadDouble("derivative_cutoff", 1.0);
    g_cfg.minCutoffRot = ReadDouble("min_cutoff_rot", 2.0);
    g_cfg.betaRot      = ReadDouble("beta_rot", 0.6);
    g_cfg.predictMs    = ReadDouble("predict_ms", 0.0);
}

static void LogOpen() {
    char p[MAX_PATH] = { 0 };
    GetModuleFileNameA(g_self, p, MAX_PATH);
    char* s = strrchr(p, '\\'); if (s) s[1] = 0;
    strncat_s(p, MAX_PATH, "ovr_proxy.log", _TRUNCATE);
    fopen_s(&g_log, p, "w");
    if (!g_log) {
        char d[MAX_PATH] = { 0 };
        if (GetEnvironmentVariableA("LOCALAPPDATA", d, MAX_PATH) > 0) {
            strncat_s(d, MAX_PATH, "\\BL2VR-grip", _TRUNCATE);
            CreateDirectoryA(d, nullptr);
            strncat_s(d, MAX_PATH, "\\ovr_proxy.log", _TRUNCATE);
            fopen_s(&g_log, d, "w");
        }
    }
}
static void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_cs);
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
    LeaveCriticalSection(&g_cs);
}

// ---------- resolve every export from the real runtime ----------
#include "resolve.inl"
static HMODULE g_real = nullptr;
static void ResolveAll() {
    g_real = LoadLibraryA(VD_RUNTIME);
    if (!g_real) { Log("FATAL: LoadLibrary(\"%s\") failed, err=%lu", VD_RUNTIME, GetLastError()); return; }
    Log("real Oculus runtime loaded at %p", (void*)g_real);
    RESOLVE_ALL(g_real);
}

// ---------- hooks ----------
typedef ovrTrackingState (*PFN_GetTrackingState)(void* session, double absTime, char latencyMarker);
static PFN_GetTrackingState real_GetTrackingState = nullptr;
static LONG g_calls = 0;

// ---------- smoothing state ----------
static double        g_freq = 0.0;
static LARGE_INTEGER g_lastQpc = {};
static void*         g_lastSession = nullptr;
static OneEuroVec3   g_fpos[2];
static OneEuroQuat   g_frot[2];
static OneEuroVec3   g_fhead;

static double NowDt() {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    double dt = (double)(now.QuadPart - g_lastQpc.QuadPart) / g_freq;
    g_lastQpc = now;
    if (dt < 1e-4) dt = 1e-4;
    if (dt > 0.10) dt = 0.10;
    return dt;
}

static void ResetFilters() {
    for (int h = 0; h < 2; h++) { g_fpos[h].reset(); g_frot[h].reset(); }
    g_fhead.reset();
}

static DWORD WINAPI ReloadThread(LPVOID) {
    for (;;) { Sleep(2000); LoadConfig(); }
    return 0;
}

static ovrTrackingState Hook_GetTrackingState(void* session, double absTime, char latencyMarker) {
    ovrTrackingState st = real_GetTrackingState(session, absTime, latencyMarker);
    LONG c = InterlockedIncrement(&g_calls);

    if (session != g_lastSession) { g_lastSession = session; ResetFilters(); }

    double dt = NowDt();
    const int doHands = g_cfg.enabled && g_cfg.smoothHands;
    const int doHead  = g_cfg.enabled && g_cfg.smoothHead;

    if (doHands) {
        for (int h = 0; h < 2; h++) {
            ovrPoseStatef& ps = st.HandPoses[h];
            if (!(st.HandStatusFlags[h] & STATUS_POSITION_TRACKED)) {
                g_fpos[h].reset(); g_frot[h].reset();
                continue;
            }
            float p[3] = { ps.ThePose.Position.x,    ps.ThePose.Position.y,    ps.ThePose.Position.z };
            float v[3] = { ps.LinearVelocity.x,      ps.LinearVelocity.y,      ps.LinearVelocity.z };
            float q[4] = { ps.ThePose.Orientation.x, ps.ThePose.Orientation.y,
                           ps.ThePose.Orientation.z, ps.ThePose.Orientation.w };
            float op[3], oq[4];
            g_fpos[h].step(p, dt, g_cfg.minCutoff,    g_cfg.beta,    g_cfg.derivCutoff,
                           g_cfg.predictMs / 1000.0, v, op);
            g_frot[h].step(q, dt, g_cfg.minCutoffRot, g_cfg.betaRot, g_cfg.derivCutoff, oq);
            ps.ThePose.Position.x = op[0]; ps.ThePose.Position.y = op[1]; ps.ThePose.Position.z = op[2];
            ps.ThePose.Orientation.x = oq[0]; ps.ThePose.Orientation.y = oq[1];
            ps.ThePose.Orientation.z = oq[2]; ps.ThePose.Orientation.w = oq[3];
        }
    }

    if (doHead) {
        if (!(st.StatusFlags & STATUS_POSITION_TRACKED)) {
            g_fhead.reset();
        } else {
            float p[3] = { st.HeadPose.ThePose.Position.x, st.HeadPose.ThePose.Position.y, st.HeadPose.ThePose.Position.z };
            float op[3];
            g_fhead.step(p, dt, g_cfg.minCutoff, g_cfg.beta, g_cfg.derivCutoff, 0.0, nullptr, op);
            st.HeadPose.ThePose.Position.x = op[0];
            st.HeadPose.ThePose.Position.y = op[1];
            st.HeadPose.ThePose.Position.z = op[2];
        }
    }

    if (g_cfg.log && c <= g_cfg.logFrames) {
        Log("#%ld dt=%.4f flags=%u  head=(%.3f %.3f %.3f)  L=(%.3f %.3f %.3f)  R=(%.3f %.3f %.3f)  Rq=(%.3f %.3f %.3f %.3f)  hf=%u/%u",
            c, dt, st.StatusFlags,
            st.HeadPose.ThePose.Position.x, st.HeadPose.ThePose.Position.y, st.HeadPose.ThePose.Position.z,
            st.HandPoses[0].ThePose.Position.x, st.HandPoses[0].ThePose.Position.y, st.HandPoses[0].ThePose.Position.z,
            st.HandPoses[1].ThePose.Position.x, st.HandPoses[1].ThePose.Position.y, st.HandPoses[1].ThePose.Position.z,
            st.HandPoses[1].ThePose.Orientation.x, st.HandPoses[1].ThePose.Orientation.y,
            st.HandPoses[1].ThePose.Orientation.z, st.HandPoses[1].ThePose.Orientation.w,
            st.HandStatusFlags[0], st.HandStatusFlags[1]);
    }
    return st;
}

static void InstallHooks() {
    if (g_ovr_GetTrackingState) {
        real_GetTrackingState = (PFN_GetTrackingState)g_ovr_GetTrackingState;
        g_ovr_GetTrackingState = (void*)&Hook_GetTrackingState;
        Log("hooked ovr_GetTrackingState (real=%p)", (void*)real_GetTrackingState);
    } else {
        Log("WARNING: ovr_GetTrackingState not found in the real runtime");
    }
}

// ---------- entry ----------
BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h;
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&g_cs);
        char exe[MAX_PATH] = { 0 }; GetModuleFileNameA(NULL, exe, MAX_PATH);
        // the ini sits next to this DLL
        GetModuleFileNameA(h, g_iniPath, MAX_PATH);
        { char* s = strrchr(g_iniPath, '\\'); if (s) s[1] = 0; }
        strncat_s(g_iniPath, MAX_PATH, "ovr_proxy.ini", _TRUNCATE);
        LogOpen();
        Log("=== LibOVRRT64_1.dll proxy attached (host: %s) ===", exe);
        Log("ini: %s", g_iniPath);
        QueryPerformanceCounter(&g_lastQpc);
        { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = (double)f.QuadPart; }
        LoadConfig();
        Log("config: enabled=%d log=%d frames=%d hands=%d head=%d minCutoff=%.2f beta=%.2f rot=%.2f/%.2f predict=%.1fms",
            g_cfg.enabled, g_cfg.log, g_cfg.logFrames, g_cfg.smoothHands, g_cfg.smoothHead,
            g_cfg.minCutoff, g_cfg.beta, g_cfg.minCutoffRot, g_cfg.betaRot, g_cfg.predictMs);
        ResolveAll();
        InstallHooks();
        { HANDLE th = CreateThread(nullptr, 0, ReloadThread, nullptr, 0, nullptr); if (th) CloseHandle(th); }
    } else if (reason == DLL_PROCESS_DETACH) {
        Log("=== proxy detached (%ld tracking calls seen) ===", g_calls);
        if (g_log) { fclose(g_log); g_log = nullptr; }
        DeleteCriticalSection(&g_cs);
    }
    return TRUE;
}