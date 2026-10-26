// Unit tests for the pure logic inside openvr_proxy/src/ovrhook.cpp.
//
// ovrhook.cpp is #included so its static functions are reachable; Bl2Log (proxy.cpp)
// and the three MinHook entry points are stubbed below - no game, no real hooking,
// no VR runtime. The fake trampoline lets us drive the FULL hook pipeline
// (Hook_GetTrackingState) with synthetic tracking states and inspect the output.
//
// Build + run:  tests\run_tests.bat
#include "ovrhook.cpp"      // brings windows.h, MinHook.h, filters.h with it
#include "test_util.h"
#include <cstdarg>
#include <cstring>
#include <thread>

// ---------------- stubs (replace proxy.cpp + the real MinHook) ----------------
static char g_log[64][300];
static int  g_logN = 0;
void Bl2Log(const char* fmt, ...) {
    if (g_logN >= 64) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_log[g_logN], sizeof(g_log[0]), fmt, ap);
    va_end(ap);
    g_log[g_logN][299] = 0;
    g_logN++;
}
static bool LogContains(const char* needle) {
    for (int i = 0; i < g_logN; i++) if (strstr(g_log[i], needle)) return true;
    return false;
}
static void LogClear() { g_logN = 0; }

static MH_STATUS g_mhInitRc = MH_OK, g_mhCreateRc = MH_OK, g_mhEnableRc = MH_OK;
static void*     g_mhTarget = nullptr;
static void*     g_mhDetour = nullptr;

static void* FakeTramp(void* retbuf, void* /*session*/, double /*absTime*/, unsigned char /*latency*/);

extern "C" MH_STATUS WINAPI MH_Initialize(VOID) { return g_mhInitRc; }
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal) {
    g_mhTarget = pTarget; g_mhDetour = pDetour;
    if (g_mhCreateRc == MH_OK && ppOriginal) *ppOriginal = (LPVOID)&FakeTramp;
    return g_mhCreateRc;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID pTarget) { (void)pTarget; return g_mhEnableRc; }

// ---------------- fake runtime plumbing ----------------
static ovrTrackingState g_feed;    // what the fake "runtime" hands to the hook
static ovrTrackingState g_outBuf;  // the buffer the hook writes through

static void* FakeTramp(void* retbuf, void*, double, unsigned char) {
    memcpy(retbuf, &g_feed, sizeof(g_feed));
    return retbuf;
}

static void* s_nextSession = (void*)0x1000;
// unique session per test -> the hook resets filter state on a session change
static void* FreshSession() { return s_nextSession = (char*)s_nextSession + 1; }

static void ResetCfg() {
    g_cfg = Cfg();
    g_cfg.enabled = 1; g_cfg.log = 1; g_cfg.logFrames = 1000000;
    g_cfg.smoothHands = 1; g_cfg.smoothHead = 0;
    g_cfg.minCutoff = 4.5; g_cfg.beta = 2.0; g_cfg.derivCutoff = 6.0;
    g_cfg.minCutoffRot = 3.0; g_cfg.betaRot = 1.5; g_cfg.predictMs = 0.0;
    g_cfg.triggerGate = 0; g_cfg.triggerThreshold = 0.25;
    g_cfg.zoomMult = 2.0; g_cfg.zoomMultRot = 0.0; g_cfg.zoomHand = 0;
    g_cfg.stockCutoff = 2.5; g_cfg.stockBeta = 2.0; g_cfg.stockZoomMult = 2.5;
    g_cfg.twohand = 0; g_cfg.twohandGate = 0; g_cfg.gunHand = 1; g_cfg.axisIndex = 5;
    g_cfg.twohandButton = 0; g_cfg.twohandHand = 0;
    g_cfg.twohandMinDist = 0.12; g_cfg.twohandMaxDist = 0.75; g_cfg.twohandBlend = 1.0;
    g_cfg.thMinCutoff = 0; g_cfg.thBeta = 0; g_cfg.thMinCutoffRot = 0; g_cfg.thBetaRot = 0;
    g_cfg.catchPos = 0.0; g_cfg.catchRot = 0.0;
    g_cfg.perThread = 0;   // shared filter state keeps the tests deterministic
}
static void ResetGlobals() {
    ResetCfg();
    g_trigger1000 = 0; g_trigOff1000 = 0; g_grip1000 = 0; g_btnTrig1000 = 0;
    g_pollRcGun = -999; g_twoHandActive = 0; g_session = nullptr;
    g_dumpedInput = false; g_dumped = false;
    LogClear();
}

static void FeedClear() {
    memset(&g_feed, 0, sizeof(g_feed));
    g_feed.HeadPose.ThePose.Orientation.w = 1.0f;
    for (int h = 0; h < 2; h++) g_feed.HandPoses[h].ThePose.Orientation.w = 1.0f;
}
static void SetHand(ovrTrackingState& st, int h, float x, float y, float z, bool tracked = true) {
    st.HandPoses[h].ThePose.Position.x = x;
    st.HandPoses[h].ThePose.Position.y = y;
    st.HandPoses[h].ThePose.Position.z = z;
    st.HandStatusFlags[h] = tracked ? STATUS_POSITION_TRACKED : 0;
}
static void SetHead(ovrTrackingState& st, float x, float y, float z, bool tracked = true) {
    st.HeadPose.ThePose.Position.x = x;
    st.HeadPose.ThePose.Position.y = y;
    st.HeadPose.ThePose.Position.z = z;
    st.StatusFlags = tracked ? STATUS_POSITION_TRACKED : 0;
}

// runs the real hook over g_feed and returns what the game would see
static ovrTrackingState* CallHook(void* session, double absTime = 1.0) {
    void* r = Hook_GetTrackingState(&g_outBuf, session, absTime, 0);
    CHECK(r == (void*)&g_outBuf);          // the ABI contract: RAX = the buffer pointer
    return &g_outBuf;
}
int main() {
    InitializeCriticalSection(&g_fcs);
    InitializeCriticalSection(&g_tsscs);
    g_freq = 1e7;                    // pretend QPC frequency; only dt clamping matters here
    g_tramp = (PFN_GetTrackingState)&FakeTramp;

    // ---------------- struct layout (the static_asserts re-run at compile time) ----------------
    Section("Oculus struct layout (must match the SDK bindings)");
    CHECK(sizeof(ovrPosef) == 28);
    CHECK(sizeof(ovrPoseStatef) == 88);
    CHECK(sizeof(ovrTrackingState) == 312);
    CHECK(offsetof(ovrTrackingState, StatusFlags) == 88);
    CHECK(offsetof(ovrTrackingState, HandPoses) == 96);
    CHECK(offsetof(ovrTrackingState, HandStatusFlags) == 272);
    CHECK(offsetof(ovrInputStateLite, IndexTrigger) == 16);
    CHECK(offsetof(ovrInputStateLite, HandTrigger) == 24);

    // ---------------- IniGet ----------------
    static auto SetIni = [](const char* text) {
        size_t n = strlen(text);
        memcpy(g_iniBuf, text, n + 1);
        g_iniLen = n;
    };
    Section("IniGet: plain key/value");
    {
        char out[64];
        SetIni("min_cutoff = 4.5\n");
        CHECK(IniGet("min_cutoff", out, sizeof(out)));
        CHECK(strcmp(out, "4.5") == 0);
    }
    Section("IniGet: whitespace around key, '=' and value is tolerated");
    {
        char out[64];
        SetIni("   beta\t =\t   2.0   \n");
        CHECK(IniGet("beta", out, sizeof(out)));
        CHECK(strcmp(out, "2.0") == 0);
    }
    Section("IniGet: keys are case-insensitive, match must be exact (no prefix hits)");
    {
        char out[64];
        SetIni("key=1\nkey2=2\nmykey=3\n");
        CHECK(IniGet("KEY", out, sizeof(out)) && strcmp(out, "1") == 0);
        CHECK(IniGet("key2", out, sizeof(out)) && strcmp(out, "2") == 0);
        CHECK(!IniGet("key3", out, sizeof(out)));
        CHECK(!IniGet("mykey_extra", out, sizeof(out)));
        CHECK(IniGet("mykey", out, sizeof(out)) && strcmp(out, "3") == 0);
    }
    Section("IniGet: ';' and '#' comment lines are skipped even when they contain '='");
    {
        char out[64];
        SetIni("; beta = 99\n# beta = 88\nbeta = 2.0\n");
        CHECK(IniGet("beta", out, sizeof(out)) && strcmp(out, "2.0") == 0);
        CHECK(!IniGet("", out, sizeof(out)) || true);   // empty key: just must not crash
    }
    Section("IniGet: CRLF endings, missing key, empty value, truncation");
    {
        char out[8];
        SetIni("a = 5\r\nb =\r\n");
        CHECK(IniGet("a", out, sizeof(out)) && strcmp(out, "5") == 0);
        CHECK(!IniGet("b", out, sizeof(out)));          // empty value -> false, out empty
        CHECK(out[0] == 0);
        CHECK(!IniGet("missing", out, sizeof(out)));
        CHECK(out[0] == 0);
        SetIni("k = abcdefghij\n");
        CHECK(IniGet("k", out, sizeof(out)));           // truncated to outsz-1, still terminated
        CHECK(strcmp(out, "abcdefg") == 0);            // 7 chars: outsz-1, still terminated
    }
    Section("IniGet: value may contain '=' and spaces");
    {
        char out[32];
        SetIni("k = a=b c\n");
        CHECK(IniGet("k", out, sizeof(out)) && strcmp(out, "a=b c") == 0);
    }

    // ---------------- CfgD / CfgI ----------------
    Section("CfgD / CfgI: parse, defaults, junk fallback");
    {
        SetIni("x = -1.25\ny = 42\nz = notanumber\nw =\n");
        CHECK_NEAR(CfgD("x", 9.0), -1.25, 1e-12);
        CHECK(CfgI("y", 7) == 42);
        CHECK_NEAR(CfgD("z", 3.5), 3.5, 1e-12);         // junk -> default
        CHECK(CfgI("z", 7) == 7);
        CHECK_NEAR(CfgD("missing", 0.75), 0.75, 1e-12); // missing -> default
        CHECK(CfgI("missing", 13) == 13);
    }
    // ---------------- IniLoad / LoadCfg (real files) ----------------
    static const char* kTmpIni = "bl2vr_test_tmp.ini";
    Section("IniLoad: reads the file once into the in-memory buffer");
    {
        FILE* fp = fopen(kTmpIni, "wb");
        CHECK(fp != nullptr);
        fputs("hello = world\n", fp);
        fclose(fp);
        strcpy(g_ini, kTmpIni);
        CHECK(IniLoad());
        CHECK(g_iniLen == strlen("hello = world\n"));
        CHECK(g_iniBuf[g_iniLen] == 0);
        char out[64];
        CHECK(IniGet("hello", out, sizeof(out)) && strcmp(out, "world") == 0);
    }
    Section("IniLoad: missing or empty file -> false (previous config kept by LoadCfg)");
    {
        memcpy(g_ini, "bl2vr_no_such_file.ini", sizeof("bl2vr_no_such_file.ini"));
        CHECK(!IniLoad());
        FILE* fp = fopen(kTmpIni, "wb");                 // existing but EMPTY
        CHECK(fp != nullptr);
        fclose(fp);
        strcpy(g_ini, kTmpIni);
        CHECK(!IniLoad());
    }
    Section("LoadCfg: full key mapping + clamps");
    {
        FILE* fp = fopen(kTmpIni, "wb");
        CHECK(fp != nullptr);
        fputs("enabled=0\nlog=1\nlog_frames=42\nsmooth_hands=1\nsmooth_head=1\n"
              "min_cutoff=1.5\nbeta=0.5\nderivative_cutoff=3.0\nmin_cutoff_rot=2.5\nbeta_rot=0.7\n"
              "predict_ms=12\ntrigger_gate=2\ntrigger_threshold=0.5\n"
              "zoom_mult=0.5\nzoom_mult_rot=-1\nzoom_hand=offhand\n"
              "stock_cutoff=3.5\nstock_beta=1.1\nstock_zoom_mult=0.5\n"
              "twohand=0\ntwohand_gate=0\ntwohand_button=trigger\ngun_hand=left\naim_axis=+y\n"
              "twohand_hand=gun\ntwohand_min_dist=0.2\ntwohand_max_dist=0.1\ntwohand_blend=2.5\n"
              "twohand_min_cutoff=4\ntwohand_beta=2.9\ntwohand_min_cutoff_rot=2.2\ntwohand_beta_rot=1.3\n"
              "catchup_pos=-5\ncatchup_rot=150\nper_thread_filters=0\n", fp);
        fclose(fp);
        strcpy(g_ini, kTmpIni);
        LoadCfg();
        CHECK(g_cfg.enabled == 0 && g_cfg.log == 1 && g_cfg.logFrames == 42);
        CHECK(g_cfg.smoothHands == 1 && g_cfg.smoothHead == 1);
        CHECK_NEAR(g_cfg.minCutoff, 1.5, 1e-9);
        CHECK_NEAR(g_cfg.beta, 0.5, 1e-9);
        CHECK_NEAR(g_cfg.derivCutoff, 3.0, 1e-9);
        CHECK_NEAR(g_cfg.minCutoffRot, 2.5, 1e-9);
        CHECK_NEAR(g_cfg.betaRot, 0.7, 1e-9);
        CHECK_NEAR(g_cfg.predictMs, 12.0, 1e-9);
        CHECK(g_cfg.triggerGate == 2);
        CHECK_NEAR(g_cfg.triggerThreshold, 0.5, 1e-9);
        CHECK_NEAR(g_cfg.zoomMult, 1.0, 1e-9);          // clamp: < 1 -> 1
        CHECK_NEAR(g_cfg.zoomMultRot, 0.0, 1e-9);       // clamp: < 0 -> 0
        CHECK(g_cfg.zoomHand == 1);                     // "offhand"
        CHECK_NEAR(g_cfg.stockCutoff, 3.5, 1e-9);
        CHECK_NEAR(g_cfg.stockBeta, 1.1, 1e-9);
        CHECK_NEAR(g_cfg.stockZoomMult, 1.0, 1e-9);     // clamp: < 1 -> 1
        CHECK(g_cfg.twohand == 0 && g_cfg.twohandGate == 0);
        CHECK(g_cfg.twohandButton == 1);                // "trigger"
        CHECK(g_cfg.gunHand == 0);                      // "left"
        CHECK(g_cfg.axisIndex == 2);                    // "+y"
        CHECK(g_cfg.twohandHand == 1);                  // "gun"
        CHECK_NEAR(g_cfg.twohandMinDist, 0.2, 1e-9);
        CHECK_NEAR(g_cfg.twohandMaxDist, 0.2, 1e-9);    // clamp: max < min -> min
        CHECK_NEAR(g_cfg.twohandBlend, 1.0, 1e-9);      // clamp: > 1 -> 1
        CHECK_NEAR(g_cfg.thMinCutoff, 4.0, 1e-9);
        CHECK_NEAR(g_cfg.thBeta, 2.9, 1e-9);
        CHECK_NEAR(g_cfg.thMinCutoffRot, 2.2, 1e-9);
        CHECK_NEAR(g_cfg.thBetaRot, 1.3, 1e-9);
        CHECK_NEAR(g_cfg.catchPos, 0.0, 1e-9);          // clamp: < 0 -> 0
        CHECK_NEAR(g_cfg.catchRot, 150.0, 1e-9);
        CHECK(g_cfg.perThread == 0);
    }
    Section("LoadCfg: zoom_hand=either, aim_axis default, missing file keeps old config");
    {
        FILE* fp = fopen(kTmpIni, "wb");
        CHECK(fp != nullptr);
        fputs("zoom_hand=either\n", fp);
        fclose(fp);
        strcpy(g_ini, kTmpIni);
        LoadCfg();
        CHECK(g_cfg.zoomHand == 2);                     // "either"
        CHECK(g_cfg.axisIndex == 5);                    // missing aim_axis -> default -z
        g_cfg.enabled = 7;                              // sentinel
        memcpy(g_ini, "bl2vr_no_such_file.ini", sizeof("bl2vr_no_such_file.ini"));
        LoadCfg();
        CHECK(g_cfg.enabled == 7);                      // untouched
    }
    // ---------------- quaternion maths ----------------
    static auto AxisQ = [](float* q, double ax, double ay, double az, double deg) {
        const double h = deg * kPi / 180.0 / 2.0;
        const double s = std::sin(h);
        q[0] = (float)(ax * s); q[1] = (float)(ay * s); q[2] = (float)(az * s); q[3] = (float)std::cos(h);
    };
    Section("QuatAngleDeg: known angles, tiny-angle stability, sign invariance");
    {
        float id[4] = { 0, 0, 0, 1 }, q90[4], qTiny[4];
        AxisQ(q90, 0, 0, 1, 90.0);
        AxisQ(qTiny, 0, 0, 1, 0.01);
        CHECK_NEAR(QuatAngleDeg(id, id), 0.0, 1e-6);
        CHECK_NEAR(QuatAngleDeg(q90, id), 90.0, 1e-3);
        CHECK_NEAR(QuatAngleDeg(id, q90), 90.0, 1e-3);     // symmetric
        CHECK_NEAR(QuatAngleDeg(qTiny, id), 0.01, 1e-3);   // naive 2*acos(dot) fails here
        float neg[4] = { -q90[0], -q90[1], -q90[2], -q90[3] };
        CHECK_NEAR(QuatAngleDeg(q90, neg), 0.0, 1e-6);     // q and -q are the same rotation
    }
    Section("QuatNormalizeF: unit length, zero quat left alone");
    {
        float q[4] = { 0, 0, 0, 5 };
        QuatNormalizeF(q);
        CHECK_NEAR(q[3], 1.0, 1e-6);
        float z[4] = { 0, 0, 0, 0 };
        QuatNormalizeF(z);                                 // must not divide by zero
        CHECK(z[0] == 0 && z[3] == 0);
    }
    Section("QuatMul: matches composed rotations (a*b)v == a(bv)");
    {
        float a[4], b[4], v[3] = { 1, 2, 3 };
        AxisQ(a, 0, 0, 1, 90.0);
        AxisQ(b, 1, 0, 0, 30.0);
        float ab[4], r1[3], r2[3], t[3];
        QuatMul(a, b, ab);
        QuatRotateVec(ab, v, r1);
        QuatRotateVec(b, v, t);
        QuatRotateVec(a, t, r2);
        CHECK_NEAR(r1[0], r2[0], 1e-4);
        CHECK_NEAR(r1[1], r2[1], 1e-4);
        CHECK_NEAR(r1[2], r2[2], 1e-4);
    }
    Section("QuatRotateVec: known axis rotations");
    {
        float qz[4], qx[4], v[3] = { 1, 0, 0 }, o[3];
        AxisQ(qz, 0, 0, 1, 90.0);
        QuatRotateVec(qz, v, o);                           // +x rotated 90 deg about z -> +y
        CHECK_NEAR(o[0], 0.0, 1e-5);
        CHECK_NEAR(o[1], 1.0, 1e-5);
        AxisQ(qx, 1, 0, 0, 180.0);
        float v2[3] = { 0, 1, 0 };
        QuatRotateVec(qx, v2, o);                          // +y flipped by 180 deg about x
        CHECK_NEAR(o[1], -1.0, 1e-5);
    }
    Section("QuatFromTo: takes unit vector a onto b (incl. the 180-deg case)");
    {
        float cases[3][2][3] = {
            { { 1, 0, 0 }, { 0, 1, 0 } },
            { { 0, 0,-1 }, { 1, 0, 0 } },
            { { 1, 0, 0 }, { -1, 0, 0 } },                 // exact opposite -> degenerate branch
        };
        for (int c = 0; c < 3; c++) {
            float q[4], o[3];
            QuatFromTo(cases[c][0], cases[c][1], q);
            const double n = std::sqrt((double)q[0]*q[0] + (double)q[1]*q[1]
                                     + (double)q[2]*q[2] + (double)q[3]*q[3]);
            CHECK_NEAR(n, 1.0, 1e-4);
            QuatRotateVec(q, cases[c][0], o);
            CHECK_NEAR(o[0], cases[c][1][0], 1e-3);
            CHECK_NEAR(o[1], cases[c][1][1], 1e-3);
            CHECK_NEAR(o[2], cases[c][1][2], 1e-3);
        }
        float id[4];
        float same[3] = { 0, 1, 0 };
        QuatFromTo(same, same, id);                        // a == b -> identity
        CHECK_NEAR(id[3], 1.0, 1e-6);
    }
    Section("SlerpQ: endpoints exact, midpoint is the half rotation, handles q vs -q");
    {
        float a[4], b[4], o[4];
        AxisQ(a, 0, 0, 1, 0.0);
        AxisQ(b, 0, 0, 1, 90.0);
        SlerpQ(a, b, 0.0f, o);
        CHECK_NEAR(QuatAngleDeg(o, a), 0.0, 1e-3);
        SlerpQ(a, b, 1.0f, o);
        CHECK_NEAR(QuatAngleDeg(o, b), 0.0, 1e-3);
        SlerpQ(a, b, 0.5f, o);
        CHECK_NEAR(QuatAngleDeg(o, a), 45.0, 1e-2);        // halfway along the arc
        float nb[4] = { -b[0], -b[1], -b[2], -b[3] };
        SlerpQ(a, nb, 0.5f, o);                            // same rotation, opposite sign
        CHECK_NEAR(QuatAngleDeg(o, a), 45.0, 1e-2);
    }
    // ---------------- virtual gunstock ----------------
    Section("StockActive: engages only when BOTH hands are tracked at the right distance");
    {
        ovrTrackingState st;
        memset(&st, 0, sizeof(st));
        Cfg cfg = g_cfg; cfg.gunHand = 1;                  // right = gun, left = offhand
        SetHand(st, 1, 0.0f, 0, 0);
        SetHand(st, 0, 0.3f, 0, 0);                        // 0.3 m apart: inside [0.12, 0.75]
        CHECK(StockActive(&st, cfg));
        cfg.gunHand = 0;                                   // mirrored (lefty) must agree
        CHECK(StockActive(&st, cfg));
        cfg.gunHand = 1;
        SetHand(st, 0, 0.05f, 0, 0);                       // too close
        CHECK(!StockActive(&st, cfg));
        SetHand(st, 0, 1.0f, 0, 0);                        // too far
        CHECK(!StockActive(&st, cfg));
        SetHand(st, 0, 0.3f, 0, 0, false);                 // offhand untracked
        CHECK(!StockActive(&st, cfg));
        SetHand(st, 0, 0.3f, 0, 0);
        SetHand(st, 1, 0.0f, 0, 0, false);                 // gun hand untracked
        CHECK(!StockActive(&st, cfg));
    }
    Section("ApplyStock: aims the barrel along the hand line, NEVER touches positions");
    {
        ResetCfg();
        ovrTrackingState st;
        memset(&st, 0, sizeof(st));
        SetHand(st, 1, 0, 0, 0);                           // gun (right) at origin, identity quat
        SetHand(st, 0, 0.3f, 0, 0);                        // offhand at +x -> stock line = +x
        st.HandPoses[1].ThePose.Orientation.w = 1.0f;
        st.HandPoses[0].ThePose.Orientation.w = 1.0f;
        FilterState F;
        ApplyStock(&st, 1.0 / 120, false, g_cfg, F);
        float q[4] = { st.HandPoses[1].ThePose.Orientation.x, st.HandPoses[1].ThePose.Orientation.y,
                       st.HandPoses[1].ThePose.Orientation.z, st.HandPoses[1].ThePose.Orientation.w };
        float barrel[3];
        QuatRotateVec(q, kAxes[5], barrel);                // aim_axis -z must now point +x
        CHECK_NEAR(barrel[0], 1.0, 1e-3);
        CHECK_NEAR(barrel[1], 0.0, 1e-3);
        CHECK_NEAR(barrel[2], 0.0, 1e-3);
        CHECK(st.HandPoses[1].ThePose.Position.x == 0.0f); // positions untouched
        CHECK(st.HandPoses[0].ThePose.Position.x == 0.3f);
        CHECK(g_bestAxis == 0);                            // +x matched against identity orientation
    }
    Section("ApplyStock: degenerate inputs are safe (coincident hands, partial blend)");
    {
        ResetCfg();
        ovrTrackingState st;
        memset(&st, 0, sizeof(st));
        SetHand(st, 1, 0.5f, 0, 0);
        SetHand(st, 0, 0.5f, 0, 0);                        // zero-length line -> early return
        st.HandPoses[1].ThePose.Orientation.w = 1.0f;
        FilterState F;
        ApplyStock(&st, 1.0 / 120, false, g_cfg, F);
        CHECK_NEAR(st.HandPoses[1].ThePose.Orientation.w, 1.0, 1e-6);  // unchanged

        Cfg cfg = g_cfg;
        cfg.twohandBlend = 0.5;                            // half-way = ~45 deg of the 90 deg turn
        ovrTrackingState st2;
        memset(&st2, 0, sizeof(st2));
        SetHand(st2, 1, 0, 0, 0);
        SetHand(st2, 0, 0.3f, 0, 0);
        st2.HandPoses[1].ThePose.Orientation.w = 1.0f;
        FilterState F2;
        ApplyStock(&st2, 1.0 / 120, false, cfg, F2);
        float before[4] = { 0, 0, 0, 1 };
        float after[4] = { st2.HandPoses[1].ThePose.Orientation.x, st2.HandPoses[1].ThePose.Orientation.y,
                           st2.HandPoses[1].ThePose.Orientation.z, st2.HandPoses[1].ThePose.Orientation.w };
        const double turned = QuatAngleDeg(after, before);
        CHECK(turned > 30.0 && turned < 60.0);             // blended, neither 0 nor full 90
    }
    // ---------------- trigger polling (PollTrigger) ----------------
    static int   g_fakeRc = 0;
    static float g_fakeIdx[2] = { 0, 0 }, g_fakeHt[2] = { 0, 0 };
    static auto FakeInput = [](void*, unsigned int, void* p) -> int {
        ovrInputStateBuf* b = (ovrInputStateBuf*)p;
        b->f.IndexTrigger[0] = g_fakeIdx[0];
        b->f.IndexTrigger[1] = g_fakeIdx[1];
        b->f.HandTrigger[0]  = g_fakeHt[0];
        b->f.HandTrigger[1]  = g_fakeHt[1];
        return g_fakeRc;
    };
    static auto RunPoll = []() {
        g_getInputState = (PFN_GetInputState)+FakeInput;
        g_session = (void*)0x1;
        PollTrigger();
    };
    Section("PollTrigger: reads both hands, x1000, into the right globals");
    {
        ResetGlobals();
        g_cfg.gunHand = 1; g_cfg.twohandHand = 0;          // right = gun; button hand = offhand(left)
        g_fakeIdx[0] = 0.3f; g_fakeIdx[1] = 0.8f;
        g_fakeHt[0]  = 0.9f; g_fakeHt[1]  = 0.4f;
        g_fakeRc = 0;
        RunPoll();
        CHECK(g_pollRcGun == 0);
        CHECK(g_trigger1000 == 800);                       // gun = right index
        CHECK(g_trigOff1000 == 300);                       // offhand index
        CHECK(g_grip1000 == 900);                          // button hand = offhand grip
        CHECK(g_btnTrig1000 == 300);                       // button hand index
    }
    Section("PollTrigger: REGRESSION - a positive ovr_GetInputState rc is still valid input");
    {
        // the shipped bug: OVR_SUCCESS tested as '== 0', which discarded every positive rc
        g_fakeRc = 5;
        RunPoll();
        CHECK(g_trigger1000 == 800);
        CHECK(g_grip1000 == 900);
    }
    Section("PollTrigger: negative rc unlatches everything and is recorded");
    {
        g_fakeRc = -3;
        RunPoll();
        CHECK(g_pollRcGun == -3);
        CHECK(g_trigger1000 == 0 && g_trigOff1000 == 0);
        CHECK(g_grip1000 == 0 && g_btnTrig1000 == 0);
    }
    Section("PollTrigger: values clamp to [0, 1000]");
    {
        g_fakeRc = 0;
        g_fakeIdx[0] = -0.5f; g_fakeIdx[1] = 1.4f;
        g_fakeHt[0] = -1.0f;  g_fakeHt[1] = 2.0f;
        RunPoll();
        CHECK(g_trigger1000 == 1000);                      // gun = right, 1.4 -> 1000
        CHECK(g_trigOff1000 == 0);                         // -0.5 -> 0
        CHECK(g_grip1000 == 0 && g_btnTrig1000 == 0);      // button hand = left
    }
    Section("PollTrigger: gun_hand and twohand_hand re-aim which slots are read");
    {
        g_fakeIdx[0] = 0.2f; g_fakeIdx[1] = 0.7f;
        g_fakeHt[0] = 0.1f;  g_fakeHt[1] = 0.6f;
        g_cfg.gunHand = 0;                                 // lefty: left = gun
        RunPoll();
        CHECK(g_trigger1000 == 200 && g_trigOff1000 == 700);
        g_cfg.twohandHand = 1;                             // button hand = gun (left now)
        RunPoll();
        CHECK(g_grip1000 == 100 && g_btnTrig1000 == 200);
    }
    Section("PollTrigger: no input function -> early return, nothing latched");
    {
        g_trigger1000 = 500;
        g_getInputState = nullptr;
        PollTrigger();
        CHECK(g_trigger1000 == 500);                       // untouched
    }
    // ---------------- Hook_GetTrackingState end-to-end (fake trampoline) ----------------
    Section("Hook: first call after a session change is a passthrough");
    {
        ResetGlobals();
        void* ses = FreshSession();
        FeedClear();
        SetHand(g_feed, 0, 0, 0, 0);
        SetHand(g_feed, 1, 0, 0, -0.2f);
        SetHead(g_feed, 0, 1.6f, 0);
        ovrTrackingState* out = CallHook(ses, 1.0);
        CHECK(out->HandPoses[1].ThePose.Position.z == -0.2f);  // exact: raw passed through
        CHECK(out->HeadPose.ThePose.Position.y == 1.6f);
    }
    Section("Hook: smoothing puts the output BETWEEN the previous and the raw pose");
    {
        ResetGlobals();
        void* ses = FreshSession();
        FeedClear();
        SetHand(g_feed, 0, 0, 0, 0);
        SetHand(g_feed, 1, 0, 0, 0);
        CallHook(ses, 1.0);
        SetHand(g_feed, 1, 0.01f, 0, 0);
        ovrTrackingState* out = CallHook(ses, 1.1);
        const float y = out->HandPoses[1].ThePose.Position.x;
        CHECK(y > 0.0f && y < 0.01f);                        // strictly between A and B
    }
    Section("Hook: trigger_gate=2 - no trigger, NO smoothing (poses stay raw)");
    {
        ResetGlobals();
        g_cfg.triggerGate = 2;
        void* ses = FreshSession();
        FeedClear();
        SetHand(g_feed, 0, 0, 0, 0);
        SetHand(g_feed, 1, 0, 0, 0);
        CallHook(ses, 1.0);
        SetHand(g_feed, 1, 0.01f, 0, 0);
        ovrTrackingState* out = CallHook(ses, 1.1);
        CHECK(out->HandPoses[1].ThePose.Position.x == 0.01f); // untouched
    }
    Section("Hook: trigger_gate=2 + trigger held -> smoothing ON + 'trigger gate -> HELD' log");
    {
        ResetGlobals();
        g_cfg.triggerGate = 2;
        g_trigger1000 = 1000;                                // gun trigger fully pulled
        void* ses = FreshSession();
        FeedClear();
        SetHand(g_feed, 0, 0, 0, 0);
        SetHand(g_feed, 1, 0, 0, 0);
        CallHook(ses, 1.0);
        SetHand(g_feed, 1, 0.01f, 0, 0);
        ovrTrackingState* out = CallHook(ses, 1.1);
        const float y = out->HandPoses[1].ThePose.Position.x;
        CHECK(y > 0.0f && y < 0.01f);
        CHECK(LogContains("trigger gate -> HELD"));
    }
    Section("Hook: zoom_hand picks WHICH hand's trigger drives the gate");
    {
        // only the OFFHAND trigger is pulled, trigger_gate=2 (smoothing only while held)
        static auto offhandRun = [](int zoomHand) -> bool {
            ResetGlobals();
            g_cfg.triggerGate = 2;
            g_cfg.zoomHand = zoomHand;
            g_trigOff1000 = 1000;
            void* ses = FreshSession();
            FeedClear();
            SetHand(g_feed, 0, 0, 0, 0);
            SetHand(g_feed, 1, 0, 0, 0);
            CallHook(ses, 1.0);
            SetHand(g_feed, 1, 0.01f, 0, 0);
            ovrTrackingState* out = CallHook(ses, 1.1);
            return out->HandPoses[1].ThePose.Position.x < 0.01f;   // true = smoothed
        };
        CHECK(!offhandRun(0));    // zoom_hand=gun:   gun trigger is 0 -> gate off -> raw
        CHECK(offhandRun(1));     // zoom_hand=offhand: gate on -> smoothed
        CHECK(offhandRun(2));     // zoom_hand=either: max(gun, off) -> gate on
    }
    Section("Hook: held trigger makes smoothing HEAVIER (zoom), not lighter");
    {
        static auto pairErr = []() -> double {
            void* ses = FreshSession();
            FeedClear();
            SetHand(g_feed, 0, 0, 0, 0);
            SetHand(g_feed, 1, 0, 0, 0);
            CallHook(ses, 1.0);
            SetHand(g_feed, 1, 0.01f, 0, 0);
            ovrTrackingState* out = CallHook(ses, 1.1);
            return 0.01 - (double)out->HandPoses[1].ThePose.Position.x;
        };
        ResetGlobals();
        g_cfg.triggerGate = 1;
        const double errFree = pairErr();                    // gate ready, trigger not pulled
        ResetGlobals();
        g_cfg.triggerGate = 1;
        g_trigger1000 = 1000;
        const double errHeld = pairErr();                    // zoom_mult=2 divides the cutoff
        CHECK(errFree > 0.0 && errHeld > 0.0);
        CHECK(errHeld > errFree);                            // held lags further behind = heavier
    }
    Section("Hook: untracked hands pass through raw; tracking resumes cleanly");
    {
        ResetGlobals();
        void* ses = FreshSession();
        FeedClear();
        SetHand(g_feed, 0, 0, 0, 0);
        SetHand(g_feed, 1, 0, 0, 0, false);                  // right hand NOT tracked
        CallHook(ses, 1.0);
        SetHand(g_feed, 1, 0.01f, 0, 0, false);
        ovrTrackingState* out = CallHook(ses, 1.1);
        CHECK(out->HandPoses[1].ThePose.Position.x == 0.01f); // never smoothed while untracked
    }
    Section("Hook: head smoothing runs only with smooth_head=1");
    {
        ResetGlobals();
        g_cfg.smoothHead = 1;
        void* ses = FreshSession();
        FeedClear();
        SetHead(g_feed, 0, 1.6f, 0);
        SetHand(g_feed, 0, 0, 0, 0);
        SetHand(g_feed, 1, 0, 0, 0);
        CallHook(ses, 1.0);
        SetHead(g_feed, 0, 1.61f, 0);
        ovrTrackingState* out = CallHook(ses, 1.1);
        const float hy = out->HeadPose.ThePose.Position.y;
        CHECK(hy > 1.6f && hy < 1.61f);                      // filtered
    }
    Section("Hook: two-hand stock engages, aims the barrel, positions stay raw");
    {
        ResetGlobals();
        g_cfg.twohand = 1;
        g_cfg.twohandGate = 0;                               // always on
        void* ses = FreshSession();
        FeedClear();
        SetHand(g_feed, 1, 0, 0, 0);                         // gun (right) at origin
        SetHand(g_feed, 0, 0.3f, 0, 0);                      // offhand at +x
        ovrTrackingState* out = CallHook(ses, 1.0);
        CHECK(g_twoHandActive == 1);
        float q[4] = { out->HandPoses[1].ThePose.Orientation.x, out->HandPoses[1].ThePose.Orientation.y,
                       out->HandPoses[1].ThePose.Orientation.z, out->HandPoses[1].ThePose.Orientation.w };
        float barrel[3];
        QuatRotateVec(q, kAxes[5], barrel);
        CHECK_NEAR(barrel[0], 1.0, 1e-3);                    // -z barrel now along +x hand line
        CHECK(out->HandPoses[1].ThePose.Position.x == 0.0f); // positions untouched
        CHECK(out->HandPoses[0].ThePose.Position.x == 0.3f);
    }
    Section("Hook: a session change resets filter history (no snap across sessions)");
    {
        ResetGlobals();
        void* ses1 = FreshSession();
        FeedClear();
        SetHand(g_feed, 0, 0, 0, 0);
        SetHand(g_feed, 1, 0, 0, 0);
        CallHook(ses1, 1.0);
        SetHand(g_feed, 1, 0.01f, 0, 0);
        CallHook(ses1, 1.1);                                 // now mid-move with history
        void* ses2 = FreshSession();
        FeedClear();
        SetHand(g_feed, 0, 0, 0, 0);
        SetHand(g_feed, 1, 0.5f, 0, 0);                      // totally different pose
        ovrTrackingState* out = CallHook(ses2, 2.0);
        CHECK(out->HandPoses[1].ThePose.Position.x == 0.5f); // fresh state -> exact passthrough
    }
    Section("Hook: null buffer is rejected safely");
    {
        CHECK(Hook_GetTrackingState(nullptr, (void*)0x2, 1.0, 0) == nullptr);
    }
    // ---------------- state, clock, hook install ----------------
    Section("NowDt: first call seeds with 1/90, then real deltas, clamped");
    {
        FilterState F;
        CHECK_NEAR(NowDt(F), 1.0 / 90.0, 1e-12);
        CHECK(F.clockValid);
        const double dt = NowDt(F);
        CHECK(dt >= 1e-4 && dt <= 0.10);
    }
    Section("GetState: shared mode = one global state; per-thread = stable slot per tid");
    {
        FilterState* a = GetState(false);
        FilterState* b = GetState(false);
        CHECK(a == &g_state && b == a);
        FilterState* t1 = GetState(true);
        CHECK(t1 != nullptr && t1 != &g_state);
        CHECK(GetState(true) == t1);                       // stable for this thread
        FilterState* other = nullptr;
        std::thread th([&other]() { other = GetState(true); });
        th.join();
        CHECK(other != nullptr && other != t1);            // a different tid gets its own slot
    }
    Section("ResetFilters: clears every stage and both history markers");
    {
        FilterState F;
        float o[3], x[3] = { 1, 2, 3 };
        F.fpos[0].step(x, 1.0 / 120, 4.5, 2.0, 6.0, 0.0, nullptr, o);
        F.stockWasActive = true;
        F.prevQValid = true;
        ResetFilters(F);
        CHECK(!F.fpos[0].init && !F.fpos[1].init && !F.fhead.init && !F.stockLine.init);
        CHECK(!F.stockWasActive && !F.prevQValid);
    }
    Section("MhName: every MinHook status has a name, unknown falls back");
    {
        CHECK(strcmp(MhName(MH_OK), "MH_OK") == 0);
        CHECK(strcmp(MhName(MH_ERROR_ALREADY_INITIALIZED), "ALREADY_INITIALIZED") == 0);
        CHECK(strcmp(MhName(MH_ERROR_NOT_CREATED), "NOT_CREATED") == 0);
        CHECK(strcmp(MhName(MH_ERROR_ENABLED), "ENABLED") == 0);
        CHECK(strcmp(MhName(MH_ERROR_NOT_EXECUTABLE), "NOT_EXECUTABLE") == 0);
        CHECK(strcmp(MhName(MH_ERROR_MEMORY_ALLOC), "MEMORY_ALLOC") == 0);
        CHECK(strcmp(MhName(MH_ERROR_MODULE_NOT_FOUND), "MODULE_NOT_FOUND") == 0);
        CHECK(strcmp(MhName((MH_STATUS)999), "UNKNOWN") == 0);
    }
    Section("InstallHook: failure modes report false, success writes the trampoline");
    {
        g_mhInitRc = MH_ERROR_MEMORY_ALLOC;
        CHECK(!InstallHook((void*)0x1234));
        g_mhInitRc = MH_ERROR_ALREADY_INITIALIZED;         // already up is fine
        g_mhCreateRc = MH_ERROR_NOT_EXECUTABLE;
        CHECK(!InstallHook((void*)0x1234));
        g_mhCreateRc = MH_OK;
        g_mhEnableRc = MH_ERROR_DISABLED;
        CHECK(!InstallHook((void*)0x1234));
        g_mhEnableRc = MH_OK;
        g_tramp = nullptr;
        CHECK(InstallHook((void*)0x1234));
        CHECK(g_mhTarget == (void*)0x1234);
        CHECK(g_mhDetour == (void*)&Hook_GetTrackingState);
        CHECK(g_tramp == (PFN_GetTrackingState)&FakeTramp); // stub wrote ppOriginal
    }
    Section("FindVdRuntime: no Oculus runtime in the test host -> nullptr");
    {
        CHECK(FindVdRuntime() == nullptr);
    }

    remove(kTmpIni);
    return Summary("test_ovrhook");
}