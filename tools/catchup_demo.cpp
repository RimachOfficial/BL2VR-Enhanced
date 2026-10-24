// catchup_demo.cpp - what does the `catchup` term really do to the effective cutoff?
//
// catchup adds stiffness proportional to LAG:  cutoff = min_cutoff + beta*|v| + catchup*|lag|
// That is meant to stop a heavy profile from drifting. But lag and cutoff are coupled: a higher
// cutoff reduces the lag, which reduces the catchup term, which lowers the cutoff again. The
// steady state for a constant angular velocity w is:
//     lag = w / (2*pi*(f0 + catchup*lag))        (rad)
// This tool solves that for realistic aiming speeds and reports the EFFECTIVE cutoff, so you can
// see whether catching up is quietly undoing the smoothing you asked for.
//
// Build (repo root): cl /O2 /EHsc /std:c++17 catchup_demo.cpp   (no filter header needed)
#include <cstdio>
#include <cmath>

static const double kPi = 3.14159265358979323846;

// solve lag = w / (2*pi*(f0 + catchup*lag)) for lag > 0
static double steadyLag(double w, double f0, double catchup) {
    if (catchup <= 0.0) return w / (2.0 * kPi * f0);
    // catchup*L^2 + f0*L - w/(2pi) = 0
    const double a = catchup, b = f0, c = -w / (2.0 * kPi);
    const double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return w / (2.0 * kPi * f0);
    return (-b + sqrt(disc)) / (2.0 * a);
}

// rough tremor pass-through at 8 Hz for a given cutoff, from tools/tremor_sweep.cpp measurements
static double tremorPct(double cutoffHz) {
    if (cutoffHz <= 0.8) return 10.0;
    if (cutoffHz <= 1.5) return 19.0;
    if (cutoffHz <= 2.25) return 27.0;
    if (cutoffHz <= 3.0) return 34.0;
    if (cutoffHz <= 4.5) return 46.0;
    if (cutoffHz <= 6.0) return 60.0;
    return 70.0;
}

static void row(double w_deg, double f0, double catchup) {
    const double w = w_deg * kPi / 180.0;
    const double L = steadyLag(w, f0, catchup);
    const double feff = w / (2.0 * kPi * L);
    printf("  %6.0f deg/s   f0=%.2f  catchup=%5.0f | lag %6.2f deg | effective cutoff %5.2f Hz | tremor ~%2.0f%%\n",
           w_deg, f0, catchup, L * 180.0 / kPi, feff, tremorPct(feff));
}

int main() {
    printf("Smoothing UNDER THE TRIGGER: cutoff drops (0.8 -> 0.4 Hz) but catch-up opens it back up.\n");
    printf("(rotation profile: min_cutoff_rot 0.8 Hz; with the trigger held the mod divides it by 2)\n\n");

    printf("slow aim, 15 deg/s (tracking a target sideways)\n");
    const double cs1[] = { 0.0, 20.0, 80.0, 150.0 }; for (int i = 0; i < 4; i++) row(15.0, 0.8, cs1[i]);
    printf("  ---- trigger held (f0 = 0.4 Hz) ----\n");
    for (int i = 0; i < 4; i++) row(15.0, 0.4, cs1[i]);

    printf("\nmedium sweep, 60 deg/s (swinging between targets)\n");
    for (int i = 0; i < 4; i++) row(60.0, 0.8, cs1[i]);
    printf("  ---- trigger held (f0 = 0.4 Hz) ----\n");
    for (int i = 0; i < 4; i++) row(60.0, 0.4, cs1[i]);

    printf("\nRead it as: the trigger lowered the cutoff 2x (more smoothing, as designed), but any\n");
    printf("catch-up above ~20 puts the effective cutoff ABOVE the un-triggered value - so holding\n");
    printf("the trigger makes the aim rougher, not smoother. That is the reported feel.\n");
    return 0;
}