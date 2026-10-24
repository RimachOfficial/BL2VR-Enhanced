// tremor_sweep.cpp - how much of an 8 Hz hand tremor survives each cutoff?
//
// The main bench (filter_bench.cpp) reports lag/settle; this one answers the question people
// actually argue about: "did the smoothing get stronger?" Hand tremor in VR sits around 5-20 Hz,
// so the % of an 8 Hz sinusoid left in the output is the honest measure of jitter rejection.
// It runs the REAL shipping filter (openvr_proxy/src/filters.h), no reimplementation.
//
// Build (from the repo root):
//   g++ -O2 -std=c++17 -I openvr_proxy tools/tremor_sweep.cpp -o tremor_sweep
//   (MSVC: cl /O2 /EHsc /std:c++17 /I openvr_proxy tools\tremor_sweep.cpp)
#include "src/filters.h"
#include <cstdio>
#include <cmath>

static const double D2R = kPi / 180.0;

// percent of the input tremor amplitude still present in the output (5 s settle, 5 s measure)
static double tremorKept(double minCut, double beta, double dCut, double ampDeg) {
    const double HZ = 120.0, DT = 1.0 / HZ, f = 8.0;
    OneEuroQuat flt;
    double outAmp = 0.0;
    const int n = (int)(10 * HZ);
    for (int i = 0; i < n; i++) {
        const double a = ampDeg * sin(2.0 * kPi * f * i * DT);
        float q[4] = { 0.0f, (float)sin(a * D2R / 2.0), 0.0f, (float)cos(a * D2R / 2.0) };
        float o[4];
        flt.step(q, DT, minCut, beta, dCut, o);
        if (i >= n / 2) {
            const double got = 2.0 * atan2((double)o[1], (double)o[3]) / D2R;
            if (fabs(got) > outAmp) outAmp = fabs(got);
        }
    }
    return 100.0 * outAmp / ampDeg;
}

int main() {
    struct P { const char* name; double mc, beta; };
    const P profs[] = {
        { "Default/hip  ", 4.5, 1.5 }, { "Hybrid       ", 2.25, 1.2 },
        { "Steady       ", 1.5, 0.8 }, { "Responsive   ", 4.5, 2.5 },
        { "rot 3.0 (old)", 3.0, 1.5 }, { "rot 2.0      ", 2.0, 1.0 },
        { "rot 1.0      ", 1.0, 0.5 }, { "rot 0.5      ", 0.5, 0.3 },
    };
    printf("rotation cutoff (Hz) vs an 8 Hz tremor - %% of tremor still present\n");
    printf("%-14s %6s %6s | %10s %10s\n", "profile", "minCut", "beta", "tremor 0.3d", "tremor 1.0d");
    for (const P& p : profs)
        printf("%-14s %6.2f %6.2f | %9.1f%% %9.1f%%\n", p.name, p.mc, p.beta,
               tremorKept(p.mc, p.beta, 6.0, 0.3), tremorKept(p.mc, p.beta, 6.0, 1.0));

    printf("\n--- derivative_cutoff (the velocity estimator's own cutoff) ---\n");
    printf("the reference spec says ~1.0 Hz; this mod ships 6.0 Hz\n");
    printf("%-14s %8s %8s %8s\n", "minCut(rot)", "d=1.0", "d=3.0", "d=6.0");
    const double mcs[] = { 4.5, 3.0, 1.5, 1.0 };
    for (double mc : mcs)
        printf("%-14.2f %7.1f%% %7.1f%% %7.1f%%\n", mc,
               tremorKept(mc, 1.5, 1.0, 0.3), tremorKept(mc, 1.5, 3.0, 0.3),
               tremorKept(mc, 1.5, 6.0, 0.3));

    printf("\nlag at rest = 1/(2*pi*minCut):  4.5Hz=35ms  3.0Hz=53ms  1.5Hz=106ms  1.0Hz=159ms  0.5Hz=318ms\n");
    return 0;
}