// Unit tests for openvr_proxy/src/filters.h - the One Euro filters that the whole
// mod is built on (OneEuroAlpha, OneEuroVec3, OneEuroQuat).
//
// Build + run everything:  tests\run_tests.bat
// Just this file (either compiler):
//   cl /std:c++17 /EHsc /I openvr_proxy\src tests\test_filters.cpp
//   g++ -std=c++17 -I openvr_proxy/src tests/test_filters.cpp -o test_filters && ./test_filters
#include "filters.h"
#include "test_util.h"

static const double kDt = 1.0 / 120;

// angle in degrees between two quaternions (x,y,z,w) - independent implementation
static double QAngleDeg(const double* a, const double* b) {
    double d = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
    if (d < 0.0) d = -d;
    if (d > 1.0) d = 1.0;
    return 2.0 * std::acos(d) * 180.0 / kPi;
}

int main() {
    // ---------------- OneEuroAlpha ----------------
    Section("OneEuroAlpha");
    CHECK(OneEuroAlpha(0.0, kDt) == 1.0);           // cutoff <= 0 -> no smoothing (alpha 1)
    CHECK(OneEuroAlpha(-3.0, kDt) == 1.0);
    {
        // exact formula: alpha = dt / (dt + tau), tau = 1 / (2*pi*f)
        const double f = 3.0;
        const double tau = 1.0 / (2.0 * kPi * f);
        CHECK_NEAR(OneEuroAlpha(f, kDt), kDt / (kDt + tau), 1e-12);
    }
    {
        // alpha in (0,1) for real cutoffs, and rising with cutoff (less smoothing)
        double prev = 0.0;
        for (double f = 0.1; f < 20.0; f *= 2.0) {
            const double a = OneEuroAlpha(f, kDt);
            CHECK(a > 0.0 && a < 1.0);
            CHECK(a > prev);
            prev = a;
        }
    }

    // ---------------- OneEuroVec3 ----------------
    Section("OneEuroVec3: first step is a passthrough (documented limitation)");
    {
        OneEuroVec3 f;
        CHECK(!f.init);
        float x[3] = { 1.5f, -2.0f, 3.25f }, o[3] = { 0, 0, 0 };
        f.step(x, kDt, 4.5, 2.0, 6.0, 0.0, nullptr, o);
        CHECK(f.init);
        CHECK_NEAR(o[0], 1.5f, 1e-6);
        CHECK_NEAR(o[1], -2.0f, 1e-6);
        CHECK_NEAR(o[2], 3.25f, 1e-6);
    }

    Section("OneEuroVec3: dt is floored at 1e-5 (zero/negative dt must not explode)");
    {
        OneEuroVec3 f;
        float x[3] = { 1, 1, 1 }, o[3];
        f.step(x, 0.0, 4.5, 2.0, 6.0, 0.0, nullptr, o);
        f.step(x, -5.0, 4.5, 2.0, 6.0, 0.0, nullptr, o);
        CHECK(std::isfinite(o[0]) && std::isfinite(o[1]) && std::isfinite(o[2]));
        CHECK_NEAR(o[0], 1.0, 1e-4);
    }

    Section("OneEuroVec3: constant input stays put");
    {
        OneEuroVec3 f;
        float o[3];
        for (int i = 0; i < 50; i++) {
            float x[3] = { 2.0f, -0.5f, 0.125f };
            f.step(x, kDt, 4.5, 2.0, 6.0, 0.0, nullptr, o);
        }
        CHECK_NEAR(o[0], 2.0, 1e-5);
        CHECK_NEAR(o[1], -0.5, 1e-5);
        CHECK_NEAR(o[2], 0.125, 1e-5);
    }

    // ramp lag helper: 1 m/s ramp for 2 s (beta 0 -> cutoff fixed = deterministic)
    struct RampRun { double lag; };
    static auto rampLag = [](double minCut, double beta, double catchup) -> double {
        OneEuroVec3 f;
        f.catchup = catchup;
        float o[3];
        double err = 0.0;
        for (int i = 1; i <= 240; i++) {
            float x[3] = { (float)(i * kDt), 0, 0 };   // 1 m/s
            f.step(x, kDt, minCut, beta, 6.0, 0.0, nullptr, o);
            err = (double)x[0] - (double)o[0];
        }
        return err;
    };

    Section("OneEuroVec3: lower cutoff = more lag on a ramp");
    {
        const double slow = rampLag(0.5, 0.0, 0.0);   // pure first-order at 0.5 Hz
        const double fast = rampLag(4.5, 0.0, 0.0);
        CHECK(slow > 0.0 && fast > 0.0);
        CHECK(slow > fast * 2.0);                     // 0.5 Hz lags FAR more than 4.5 Hz
        // first-order sanity: steady ramp lag ~= v / (2*pi*f)
        CHECK_NEAR(slow, 1.0 / (2.0 * kPi * 0.5), 0.15);
    }

    Section("OneEuroVec3: catchup reduces lag (the whole point of the term)");
    {
        CHECK(rampLag(1.5, 0.0, 150.0) < rampLag(1.5, 0.0, 0.0));
    }

    Section("OneEuroVec3: adaptive cutoff - beta opens the filter on fast motion");
    {
        CHECK(rampLag(0.5, 20.0, 0.0) < rampLag(0.5, 0.0, 0.0));
    }

    Section("OneEuroVec3: 8 Hz tremor is attenuated (kept < 50% at cutoff 3 Hz)");
    {
        OneEuroVec3 f;
        float o[3];
        double in2 = 0.0, out2 = 0.0;
        for (int i = 0; i < 720; i++) {               // 6 s at 120 Hz
            float x[3] = { (float)(0.003 * std::sin(2.0 * kPi * 8.0 * i * kDt)), 0, 0 };
            f.step(x, kDt, 3.0, 0.0, 6.0, 0.0, nullptr, o);
            if (i >= 240) {                            // skip the first 2 s of settling
                in2  += (double)x[0] * x[0];
                out2 += (double)o[0] * o[0];
            }
        }
        const double kept = 100.0 * std::sqrt(out2 / in2);
        CHECK(kept > 0.0 && kept < 50.0);             // measured ~34% at 3 Hz
    }

    Section("OneEuroVec3: predictSec adds vel*predictSec on top of the filtered value");
    {
        OneEuroVec3 f;
        float x[3] = { 1, 2, 3 }, vel[3] = { 0.5f, -1.0f, 2.0f }, o[3];
        f.step(x, kDt, 4.5, 2.0, 6.0, 0.1, vel, o);
        CHECK_NEAR(o[0], 1.0 + 0.5 * 0.1, 1e-5);       // first step: y == x exactly
        CHECK_NEAR(o[1], 2.0 - 1.0 * 0.1, 1e-5);
        CHECK_NEAR(o[2], 3.0 + 2.0 * 0.1, 1e-5);
        // predictSec == 0 ignores the velocity pointer entirely
        OneEuroVec3 g;
        g.step(x, kDt, 4.5, 2.0, 6.0, 0.0, vel, o);
        CHECK_NEAR(o[0], 1.0, 1e-5);
    }

    Section("OneEuroVec3: reset() clears history - next step is a fresh passthrough");
    {
        OneEuroVec3 f;
        float o[3];
        for (int i = 0; i < 100; i++) {
            float x[3] = { (float)i, 0, 0 };
            f.step(x, kDt, 4.5, 2.0, 6.0, 0.0, nullptr, o);
        }
        f.reset();
        CHECK(!f.init);
        CHECK(f.dx[0] == 0.0 && f.dx[1] == 0.0 && f.dx[2] == 0.0);
        float x2[3] = { -7.0f, 8.0f, -9.0f };
        f.step(x2, kDt, 4.5, 2.0, 6.0, 0.0, nullptr, o);
        CHECK_NEAR(o[0], -7.0, 1e-6);                  // passthrough, no snap from old state
    }
    // ---------------- OneEuroQuat ----------------
    Section("OneEuroQuat: first step passes through, normalized");
    {
        OneEuroQuat q;
        CHECK(!q.init);
        float in[4] = { 0, 0, 0, 2 }, o[4];              // un-normalized identity
        q.step(in, kDt, 3.0, 1.5, 6.0, o);
        CHECK(q.init);
        CHECK_NEAR(o[0], 0.0, 1e-6);
        CHECK_NEAR(o[3], 1.0, 1e-6);
    }

    Section("OneEuroQuat: zero-length input returns the current state, does not init");
    {
        OneEuroQuat q;
        float zero[4] = { 0, 0, 0, 0 }, o[4];
        q.step(zero, kDt, 3.0, 1.5, 6.0, o);
        CHECK(!q.init);                                  // untouched
        CHECK_NEAR(o[0], 0.0, 1e-6);
        CHECK_NEAR(o[3], 1.0, 1e-6);                     // identity default
    }

    Section("OneEuroQuat: output stays unit length through a long mixed run");
    {
        OneEuroQuat q;
        float o[4];
        for (int i = 1; i <= 400; i++) {
            const double ang = 0.5 * std::sin(i * 0.07);
            float in[4] = { 0, (float)std::sin(ang / 2), 0, (float)std::cos(ang / 2) };
            q.step(in, kDt, 3.0, 1.5, 6.0, o);
            const double n = std::sqrt((double)o[0]*o[0] + (double)o[1]*o[1]
                                     + (double)o[2]*o[2] + (double)o[3]*o[3]);
            CHECK_NEAR(n, 1.0, 1e-4);
        }
    }

    Section("OneEuroQuat: quaternion sign flip (q vs -q) does not make the output jump");
    {
        OneEuroQuat q;
        float o[4], prev[4] = { 0, 0, 0, 1 };
        const double ang = 0.8;
        float pos[4] = { 0, (float)std::sin(ang / 2), 0, (float)std::cos(ang / 2) };
        float neg[4] = { -pos[0], -pos[1], -pos[2], -pos[3] };
        for (int i = 0; i < 6; i++) {
            q.step(i % 2 ? neg : pos, kDt, 3.0, 1.5, 6.0, o);
            if (i > 0) {
                const double od[4] = { o[0], o[1], o[2], o[3] };
                const double pd[4] = { prev[0], prev[1], prev[2], prev[3] };
                CHECK_NEAR(QAngleDeg(od, pd), 0.0, 0.5);  // same rotation, no flip
            }
            for (int k = 0; k < 4; k++) prev[k] = o[k];
        }
    }

    Section("OneEuroQuat: tracks a constant-rate rotation, with real lag; catchup shrinks it");
    {
        static auto rotLag = [](double catchup) -> double {
            OneEuroQuat q;
            q.catchup = catchup;
            float o[4];
            const double rate = 60.0 * kPi / 180.0;    // 60 deg/s about y
            double lagDeg = 0.0;
            for (int i = 1; i <= 240; i++) {
                const double ang = rate * i * kDt;
                float in[4] = { 0, (float)std::sin(ang / 2), 0, (float)std::cos(ang / 2) };
                q.step(in, kDt, 3.0, 0.0, 6.0, o);     // beta 0 -> deterministic cutoff
                const double rawd[4] = { in[0], in[1], in[2], in[3] };
                const double outd[4] = { o[0], o[1], o[2], o[3] };
                lagDeg = QAngleDeg(outd, rawd);
            }
            return lagDeg;
        };
        const double plain = rotLag(0.0);
        const double cured = rotLag(120.0);
        CHECK(plain > 0.05);                           // it really lags (not passthrough)
        CHECK(cured < plain);                          // catchup brings it closer to raw
    }

    Section("OneEuroQuat: reset() clears history - next step is a fresh passthrough");
    {
        OneEuroQuat q;
        float o[4];
        for (int i = 1; i <= 50; i++) {
            const double ang = 1.0 * i * kDt;
            float in[4] = { 0, (float)std::sin(ang / 2), 0, (float)std::cos(ang / 2) };
            q.step(in, kDt, 3.0, 1.5, 6.0, o);
        }
        q.reset();
        CHECK(!q.init);
        float in2[4] = { 0.6f, 0.0f, 0.0f, 0.8f };     // 73.7 deg about x, unit length
        q.step(in2, kDt, 3.0, 1.5, 6.0, o);
        CHECK_NEAR(o[0], 0.6, 1e-5);                   // passthrough
        CHECK_NEAR(o[3], 0.8, 1e-5);
    }

    return Summary("test_filters");
}
