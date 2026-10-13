// virtual-desktop based Oculus hook (see ovrhook.h).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <tlhelp32.h>
#include "MinHook.h"
#include "ovrhook.h"
#include "filters.h"

// ---------- Oculus structures ----------
// Layouts verified against the generated Oculus SDK bindings (docs.rs ovr-sys,
// which wraps LibOVR's OVR_CAPI.h) - NOT guessed:
//   ovrPoseStatef  = 88 bytes : ThePose 0, AngularVel 28, LinearVel 40,
//                               AngularAcc 52, LinearAcc 64, [u8;4] pad, TimeInSeconds 80
//   ovrTrackingState = 312 bytes: HeadPose 0, StatusFlags 88, (pad 4),
//                               HandPoses 96, HandStatusFlags 272, CalibratedOrigin 280
// The struct is returned by value, so a wrong size/offset corrupts memory.
struct ovrVector3f { float x, y, z; };
struct ovrQuatf    { float x, y, z, w; };
struct ovrPosef    { ovrQuatf Orientation; ovrVector3f Position; };
struct ovrPoseStatef {
    ovrPosef     ThePose;              // 0
    ovrVector3f  AngularVelocity;      // 28
    ovrVector3f  LinearVelocity;       // 40
    ovrVector3f  AngularAcceleration;  // 52
    ovrVector3f  LinearAcceleration;   // 64
    unsigned int _pad0;                // 76  (aligns the double to 8)
    double       TimeInSeconds;        // 80
};
struct ovrTrackingState {
    ovrPoseStatef HeadPose;            // 0
    unsigned int  StatusFlags;         // 88
    unsigned int  _pad1;               // 92
    ovrPoseStatef HandPoses[2];        // 96
    unsigned int  HandStatusFlags[2];  // 272
    ovrPosef      CalibratedOrigin;    // 280
    unsigned int  _pad2;               // 308
};
static_assert(sizeof(ovrPosef) == 28,          "ovrPosef");
static_assert(sizeof(ovrPoseStatef) == 88,     "ovrPoseStatef");
static_assert(sizeof(ovrTrackingState) == 312, "ovrTrackingState");
static_assert(offsetof(ovrTrackingState, StatusFlags) == 88,     "StatusFlags");
static_assert(offsetof(ovrTrackingState, HandPoses) == 96,       "HandPoses");
static_assert(offsetof(ovrTrackingState, HandStatusFlags) == 272, "HandStatusFlags");

#define HAND_LEFT  0
#define HAND_RIGHT 1
static const unsigned int STATUS_POSITION_TRACKED = 0x0002;

// ---------- config (bl2vr_grip.ini) ----------
struct Cfg {
    int    enabled, log, logFrames, smoothHands, smoothHead;
    double minCutoff, beta, derivCutoff, minCutoffRot, betaRot, predictMs;
    int    triggerGate;          // 0 = ignore trigger, 1 = stronger while held, 2 = only while held
    double triggerThreshold, zoomMult, zoomMultRot;
};
static Cfg  g_cfg = { 1, 1, 400, 1, 0, 3.0, 1.0, 1.0, 2.0, 0.6, 0.0, 1, 0.5, 20.0, 60.0 };
static char g_ini[MAX_PATH] = { 0 };

static double CfgD(const char* key, double def) {
    char b[128] = { 0 };
    GetPrivateProfileStringA("bl2vr_grip", key, "", b, sizeof(b), g_ini);
    double d = 0.0;
    if (b[0] && sscanf_s(b, "%lf", &d) == 1) return d;
    return def;
}
static void LoadCfg() {
    g_cfg.enabled     = GetPrivateProfileIntA("bl2vr_grip", "enabled", 1, g_ini);
    g_cfg.log         = GetPrivateProfileIntA("bl2vr_grip", "log", 1, g_ini);
    g_cfg.logFrames   = GetPrivateProfileIntA("bl2vr_grip", "log_frames", 400, g_ini);
    g_cfg.smoothHands = GetPrivateProfileIntA("bl2vr_grip", "smooth_hands", 1, g_ini);
    g_cfg.smoothHead  = GetPrivateProfileIntA("bl2vr_grip", "smooth_head", 0, g_ini);
    g_cfg.minCutoff    = CfgD("min_cutoff", 3.0);
    g_cfg.beta         = CfgD("beta", 1.0);
    g_cfg.derivCutoff  = CfgD("derivative_cutoff", 1.0);
    g_cfg.minCutoffRot = CfgD("min_cutoff_rot", 2.0);
    g_cfg.betaRot      = CfgD("beta_rot", 0.6);
    g_cfg.predictMs    = CfgD("predict_ms", 0.0);
    g_cfg.triggerGate      = GetPrivateProfileIntA("bl2vr_grip", "trigger_gate", 1, g_ini);
    g_cfg.triggerThreshold = CfgD("trigger_threshold", 0.5);
    g_cfg.zoomMult         = CfgD("zoom_mult", 20.0);
    g_cfg.zoomMultRot      = CfgD("zoom_mult_rot", 0.0);   // 0 = same as zoom_mult
    if (g_cfg.zoomMult < 1.0) g_cfg.zoomMult = 1.0;
    if (g_cfg.zoomMultRot < 0.0) g_cfg.zoomMultRot = 0.0;
}

// ---------- smoothing state ----------
static double        g_freq = 0.0;
static LARGE_INTEGER g_lastQpc = {};
static void*         g_lastSession = nullptr;
static OneEuroVec3   g_fpos[2];
static OneEuroQuat   g_frot[2];
static OneEuroVec3   g_fhead;
static volatile LONG g_calls = 0;
static bool          g_dumped = false;

// ---------- trigger gate ----------
// Input state layout verified against the SDK bindings (ovr-sys / OVR_CAPI.h):
//   f64 TimeInSeconds @0, uint Buttons @8, uint Touches @12, float IndexTrigger[2] @16
//   IndexTrigger is indexed by ovrHand_Left(0)/ovrHand_Right(1), so the RIGHT trigger
//   is IndexTrigger[1] at byte 20. (ovrControllerType_RTouch = 0x02 for the query.)
struct ovrInputStateLite {
    double       TimeInSeconds;
    unsigned int Buttons;
    unsigned int Touches;
    float        IndexTrigger[2];
};
static_assert(offsetof(ovrInputStateLite, IndexTrigger) == 16, "IndexTrigger offset");

// The runtime writes its FULL ovrInputState (120+ bytes, version dependent) through the
// pointer we hand it - so the buffer must be generously oversized. Passing a 24-byte
// struct made the runtime scribble past our stack (game died with no error).
struct ovrInputStateBuf {
    ovrInputStateLite f;              // the fields we read, at offset 0
    unsigned char     slack[512];     // absorb everything the runtime writes
};
static_assert(offsetof(ovrInputStateBuf, f) == 0, "head must be at offset 0");

typedef int (*PFN_GetInputState)(void* session, unsigned int controllerType, void* inputState);
static PFN_GetInputState g_getInputState = nullptr;
static void*             g_session = nullptr;
static volatile LONG     g_trigger1000 = 0;      // right index trigger x1000
static bool              g_dumpedInput = false;

// ---- jitter meter ----
// Measures how much the right-hand ORIENTATION moves per call, raw vs after filtering.
// That turns "feels smoother" into a number we can compare between settings.
static float  g_prevRawQ[4]  = { 0, 0, 0, 1 };
static float  g_prevFiltQ[4] = { 0, 0, 0, 1 };
static bool   g_prevQValid = false;
static double g_accRawDeg = 0.0, g_accFiltDeg = 0.0;
static int    g_accN = 0;

// The game calls ovr_GetTrackingState about twice per frame, and not necessarily from one
// thread - so all filter state must be guarded or two threads fight over the same quaternions.
static CRITICAL_SECTION g_fcs;

static double QuatAngleDeg(const float* a, const float* b) {
    // Stable for tiny angles: build the relative rotation a * conj(b) and use atan2 on its
    // vector vs scalar part. The naive 2*acos(dot) collapses to float noise near 0 degrees,
    // which made the jitter meter report phantom motion while the hand was still.
    const double aw = a[3], ax = a[0], ay = a[1], az = a[2];
    const double bw = b[3], bx = -b[0], by = -b[1], bz = -b[2];
    const double rw = aw * bw - ax * bx - ay * by - az * bz;
    const double rx = aw * bx + ax * bw + ay * bz - az * by;
    const double ry = aw * by - ax * bz + ay * bw + az * bx;
    const double rz = aw * bz + ax * by - ay * bx + az * bw;
    const double v = sqrt(rx * rx + ry * ry + rz * rz);
    return 2.0 * atan2(v, (rw < 0 ? -rw : rw)) * 180.0 / 3.14159265358979323846;
}

static void PollTrigger() {
    if (!g_getInputState || !g_session) return;
    static ovrInputStateBuf buf;      // static: oversized, off the stack
    memset(&buf, 0, sizeof(buf));
    if (g_getInputState(g_session, 0x02 /* ovrControllerType_RTouch */, &buf) == 0) {
        int v = (int)(buf.f.IndexTrigger[1] * 1000.0f);
        if (v < 0) v = 0;
        if (v > 1000) v = 1000;
        g_trigger1000 = v;
        if (!g_dumpedInput) {
            g_dumpedInput = true;
            Bl2Log("--- input state dump: Time=%.2f Buttons=0x%X Touches=0x%X trigL=%.3f trigR=%.3f ---",
                   buf.f.TimeInSeconds, buf.f.Buttons, buf.f.Touches,
                   buf.f.IndexTrigger[0], buf.f.IndexTrigger[1]);
        }
    }
}

static double NowDt() {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    double dt = (double)(now.QuadPart - g_lastQpc.QuadPart) / (g_freq > 0 ? g_freq : 1.0);
    g_lastQpc = now;
    if (dt < 1e-4) dt = 1e-4;
    if (dt > 0.10) dt = 0.10;
    return dt;
}
static void ResetFilters() {
    for (int h = 0; h < 2; h++) { g_fpos[h].reset(); g_frot[h].reset(); }
    g_fhead.reset();
}

// ---------- the hook ----------
// The original is:  ovrTrackingState ovr_GetTrackingState(ovrSession, double, ovrBool)
// A 312-byte struct is returned through a hidden pointer, so the real ABI is:
//   rcx = return buffer, rdx = session, xmm0 = absTime, r8 = latencyMarker
// and MSVC ALSO returns that buffer pointer in RAX. Our detour must do the same,
// otherwise the caller reads a garbage pointer out of RAX and dies with a fatal
// error full of tiny addresses (0x4, 0x20, 0xfffffff8...).
// Using the explicit-retbuf form means the ABI does NOT depend on our struct size;
// we only need the field offsets right (verified against the SDK bindings).
typedef void* (*PFN_GetTrackingState)(void* retbuf, void* session, double absTime, unsigned char latencyMarker);
static PFN_GetTrackingState g_tramp = nullptr;

static void* Hook_GetTrackingState(void* retbuf, void* session, double absTime, unsigned char latencyMarker) {
    if (!retbuf) return nullptr;
    g_tramp(retbuf, session, absTime, latencyMarker);   // run the real function first

    ovrTrackingState* st = (ovrTrackingState*)retbuf;
    LONG c = InterlockedIncrement(&g_calls);
    if (session != g_lastSession) { g_lastSession = session; ResetFilters(); }

    double dt = NowDt();

    // ---- trigger gate ----
    // The right index trigger is the game's zoom/aim bind. While it is held we divide the
    // filter cutoff by zoom_mult, so smoothing gets that many times stronger - exactly what
    // a sniper wants when scoped in, with normal 1:1 feel the rest of the time.
    if (!g_session && session) g_session = session;   // the polling thread does the rest

    const double trig     = (double)g_trigger1000 / 1000.0;
    const bool   gateHeld = (g_cfg.triggerGate != 0) && (trig >= g_cfg.triggerThreshold);
    const bool   doSmooth = g_cfg.enabled && g_cfg.smoothHands &&
                            (g_cfg.triggerGate != 2 || gateHeld);
    const double mult    = (g_cfg.triggerGate != 0 && gateHeld) ? g_cfg.zoomMult : 1.0;
    // rotation gets its own multiplier: for sniping it is the angular jitter that hurts,
    // because a tiny angle becomes a big miss at distance.
    const double multRot = (g_cfg.triggerGate != 0 && gateHeld)
                           ? (g_cfg.zoomMultRot > 0 ? g_cfg.zoomMultRot : g_cfg.zoomMult) : 1.0;
    // Both cutoff AND beta are divided by the multiplier, so zooming simply makes the filter
    // N times heavier in every respect - that is the additive "slide" feel that was liked at 20x.
    // (No beta boost: boosting it made the filter wake up early, i.e. MORE responsive - the
    // opposite of what was wanted.)
    const double mc  = g_cfg.minCutoff    / mult,    bt  = g_cfg.beta    / mult;
    const double mcr = g_cfg.minCutoffRot / multRot, btr = g_cfg.betaRot / multRot;

    EnterCriticalSection(&g_fcs);
    if (doSmooth) {
        for (int h = 0; h < 2; h++) {
            ovrPoseStatef* ps = &st->HandPoses[h];
            if (!(st->HandStatusFlags[h] & STATUS_POSITION_TRACKED)) {
                g_fpos[h].reset(); g_frot[h].reset();
                continue;
            }
            float p[3] = { ps->ThePose.Position.x,    ps->ThePose.Position.y,    ps->ThePose.Position.z };
            float v[3] = { ps->LinearVelocity.x,      ps->LinearVelocity.y,      ps->LinearVelocity.z };
            float q[4] = { ps->ThePose.Orientation.x, ps->ThePose.Orientation.y,
                           ps->ThePose.Orientation.z, ps->ThePose.Orientation.w };
            float op[3], oq[4];
            g_fpos[h].step(p, dt, mc,  bt,  g_cfg.derivCutoff,
                           g_cfg.predictMs / 1000.0, v, op);
            g_frot[h].step(q, dt, mcr, btr, g_cfg.derivCutoff, oq);

            if (h == HAND_RIGHT) {          // rotation noise: raw vs filtered
                if (g_prevQValid) {
                    g_accRawDeg  += QuatAngleDeg(q,  g_prevRawQ);
                    g_accFiltDeg += QuatAngleDeg(oq, g_prevFiltQ);
                    g_accN++;
                }
                memcpy(g_prevRawQ,  q,  sizeof(g_prevRawQ));
                memcpy(g_prevFiltQ, oq, sizeof(g_prevFiltQ));
                g_prevQValid = true;
            }

            ps->ThePose.Position.x = op[0]; ps->ThePose.Position.y = op[1]; ps->ThePose.Position.z = op[2];
            ps->ThePose.Orientation.x = oq[0]; ps->ThePose.Orientation.y = oq[1];
            ps->ThePose.Orientation.z = oq[2]; ps->ThePose.Orientation.w = oq[3];
        }
    } else {
        // not smoothing right now - drop filter history so re-engaging doesn't snap
        for (int h = 0; h < 2; h++) { g_fpos[h].reset(); g_frot[h].reset(); }
    }
    LeaveCriticalSection(&g_fcs);
    if (g_cfg.enabled && g_cfg.smoothHead) {
        if (!(st->StatusFlags & STATUS_POSITION_TRACKED)) {
            g_fhead.reset();
        } else {
            float p[3] = { st->HeadPose.ThePose.Position.x, st->HeadPose.ThePose.Position.y, st->HeadPose.ThePose.Position.z };
            float op[3];
            g_fhead.step(p, dt, g_cfg.minCutoff, g_cfg.beta, g_cfg.derivCutoff, 0.0, nullptr, op);
            st->HeadPose.ThePose.Position.x = op[0];
            st->HeadPose.ThePose.Position.y = op[1];
            st->HeadPose.ThePose.Position.z = op[2];
        }
    }
    if (g_cfg.log && (c <= g_cfg.logFrames || (c % 120) == 0)) {
        Bl2Log("#%ld dt=%.4f flags=%u hf=%u/%u trig=%.2f x%.0f/%.0f on=%d head=(%.3f %.3f %.3f) L=(%.3f %.3f %.3f) R=(%.3f %.3f %.3f) Rq=(%.3f %.3f %.3f %.3f)",
            c, dt, st->StatusFlags, st->HandStatusFlags[0], st->HandStatusFlags[1],
            trig, mult, multRot, (int)doSmooth,
            st->HeadPose.ThePose.Position.x, st->HeadPose.ThePose.Position.y, st->HeadPose.ThePose.Position.z,
            st->HandPoses[0].ThePose.Position.x, st->HandPoses[0].ThePose.Position.y, st->HandPoses[0].ThePose.Position.z,
            st->HandPoses[1].ThePose.Position.x, st->HandPoses[1].ThePose.Position.y, st->HandPoses[1].ThePose.Position.z,
            st->HandPoses[1].ThePose.Orientation.x, st->HandPoses[1].ThePose.Orientation.y,
            st->HandPoses[1].ThePose.Orientation.z, st->HandPoses[1].ThePose.Orientation.w);
    }

    // Periodic jitter report: how much the right-hand rotation actually moved per call,
    // before vs after our filter. This is the number that says whether it got smoother.
    if (g_accN > 0 && (c % 120) == 0) {
        Bl2Log("jitter R-rot: raw %.4f deg/call -> filtered %.4f deg/call  (%.1fx smoother, %d samples)",
               g_accRawDeg / g_accN, g_accFiltDeg / g_accN,
               (g_accFiltDeg > 1e-9 ? g_accRawDeg / g_accFiltDeg : 0.0), g_accN);
        g_accRawDeg = 0.0; g_accFiltDeg = 0.0; g_accN = 0;
    }

    // Once tracking is actually live, dump the raw buffer as floats exactly once.
    // This is how we CONFIRM (or repair) the field offsets from real data instead of
    // trusting the header: the head position should read about y ~ 1.6 metres, and
    // the hands should sit within an arm's reach of it.
    if (!g_dumped && st->StatusFlags != 0) {
        g_dumped = true;
        const float* raw = (const float*)retbuf;
        Bl2Log("--- RAW layout dump (80 floats / 320 bytes of the tracking state) ---");
        for (int i = 0; i < 10; i++) {
            int o = i * 8;
            Bl2Log("raw[%3d] %9.4f %9.4f %9.4f %9.4f %9.4f %9.4f %9.4f %9.4f", o,
                   raw[o + 0], raw[o + 1], raw[o + 2], raw[o + 3],
                   raw[o + 4], raw[o + 5], raw[o + 6], raw[o + 7]);
        }
        Bl2Log("--- end raw dump (offset 0 should be head orientation.x) ---");
    }

    return retbuf;
}
static HMODULE FindVdRuntime() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return nullptr;
    HMODULE found = nullptr;
    MODULEENTRY32 me; me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) {
        do {
            if (strstr(me.szModule, "LibOVRRT64") || strstr(me.szModule, "LibOVRRT")) {
                Bl2Log("found Oculus runtime module: %s (%p)", me.szModule, (void*)me.hModule);
                found = me.hModule;
                break;
            }
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return found;
}
// ---------- install the inline hook (MinHook) ----------
// MinHook (per its documented API: MH_Initialize -> MH_CreateHook -> MH_EnableHook)
// reads the prologue itself, builds the trampoline, and hands the original back
// through ppOriginal - so nothing about the target's prologue is hard-coded here.
static const char* MhName(MH_STATUS s) {
    switch (s) {
        case MH_OK: return "MH_OK";
        case MH_ERROR_ALREADY_INITIALIZED: return "ALREADY_INITIALIZED";
        case MH_ERROR_NOT_INITIALIZED: return "NOT_INITIALIZED";
        case MH_ERROR_ALREADY_CREATED: return "ALREADY_CREATED";
        case MH_ERROR_NOT_CREATED: return "NOT_CREATED";
        case MH_ERROR_ENABLED: return "ENABLED";
        case MH_ERROR_DISABLED: return "DISABLED";
        case MH_ERROR_NOT_EXECUTABLE: return "NOT_EXECUTABLE";
        case MH_ERROR_UNSUPPORTED_FUNCTION: return "UNSUPPORTED_FUNCTION";
        case MH_ERROR_MEMORY_ALLOC: return "MEMORY_ALLOC";
        case MH_ERROR_MEMORY_PROTECT: return "MEMORY_PROTECT";
        case MH_ERROR_MODULE_NOT_FOUND: return "MODULE_NOT_FOUND";
        case MH_ERROR_FUNCTION_NOT_FOUND: return "FUNCTION_NOT_FOUND";
        default: return "UNKNOWN";
    }
}

static bool InstallHook(void* target) {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        Bl2Log("MH_Initialize failed: %s", MhName(s));
        return false;
    }

    s = MH_CreateHook(target, (LPVOID)&Hook_GetTrackingState, (LPVOID*)&g_tramp);
    if (s != MH_OK) { Bl2Log("MH_CreateHook failed: %s", MhName(s)); return false; }

    s = MH_EnableHook(target);
    if (s != MH_OK) { Bl2Log("MH_EnableHook failed: %s", MhName(s)); return false; }

    Bl2Log("HOOKED ovr_GetTrackingState  target=%p  trampoline=%p  detour=%p",
           target, (void*)g_tramp, (void*)&Hook_GetTrackingState);
    return true;
}

void InstallOvrHookThread() {
    InitializeCriticalSection(&g_fcs);
    { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = (double)f.QuadPart; }
    QueryPerformanceCounter(&g_lastQpc);

    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&InstallOvrHookThread, &self);
    GetModuleFileNameA(self, g_ini, MAX_PATH);
    { char* s = strrchr(g_ini, '\\'); if (s) s[1] = 0; }
    strncat_s(g_ini, MAX_PATH, "bl2vr_grip.ini", _TRUNCATE);
    LoadCfg();

    Bl2Log("--- waiting for Virtual Desktop's Oculus runtime (ini: %s) ---", g_ini);
    for (int tries = 0; tries < 120 && !g_tramp; tries++) {          // up to 60 s
        HMODULE vd = FindVdRuntime();
        if (vd) {
            void* fn = (void*)GetProcAddress(vd, "ovr_GetTrackingState");
            g_getInputState = (PFN_GetInputState)GetProcAddress(vd, "ovr_GetInputState");
            Bl2Log("resolved ovr_GetInputState = %p", (void*)g_getInputState);
            if (fn) {
                LoadCfg();
                Bl2Log("target: enabled=%d hands=%d head=%d minCutoff=%.2f beta=%.2f rot=%.2f/%.2f",
                       g_cfg.enabled, g_cfg.smoothHands, g_cfg.smoothHead,
                       g_cfg.minCutoff, g_cfg.beta, g_cfg.minCutoffRot, g_cfg.betaRot);
                InstallHook(fn);
                break;
            }
            Bl2Log("runtime module found but ovr_GetTrackingState is missing");
        }
        Sleep(500);
    }
    if (!g_tramp) Bl2Log("gave up waiting for the Oculus runtime");

    // Keep the ini hot, and poll the trigger from OUR OWN thread - never from inside the game's
    // ovr_GetTrackingState call (calling back into the runtime re-entrantly is asking for trouble).
    int tick = 0;
    for (;;) {
        Sleep(15);
        PollTrigger();
        if (++tick >= 133) { tick = 0; LoadCfg(); }   // ~2 s
    }
}