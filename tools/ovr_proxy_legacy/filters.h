// One Euro filter (Casiez, Roussel & Vogel 2012) for pose smoothing.
// Why this and not a plain low-pass: a fixed low-pass needs a big trade-off
// between "no jitter when still" and "no lag when moving". This filter adapts -
// low cutoff (= heavy smoothing) while you are slow, high cutoff (= responsive)
// while you move fast.
#pragma once
#include <cmath>

static const double kPi = 3.14159265358979323846;

// alpha for a first-order low-pass with the given cutoff frequency (Hz)
static inline double OneEuroAlpha(double cutoffHz, double dt) {
    if (cutoffHz <= 0.0) return 1.0;
    double tau = 1.0 / (2.0 * kPi * cutoffHz);
    return 1.0 / (1.0 + tau / dt);
}

struct OneEuroVec3 {
    bool   init = false;
    double y[3] = { 0, 0, 0 };    // filtered output
    double dy[3] = { 0, 0, 0 };   // filtered derivative

    void reset() { init = false; }

    void step(const float* x, double dt, double minCut, double beta, double dCut,
              double predictSec, const float* vel, float* out) {
        if (!init) {
            for (int i = 0; i < 3; i++) { y[i] = x[i]; dy[i] = 0.0; }
            init = true;
        } else {
            double ad = OneEuroAlpha(dCut, dt);
            double speed2 = 0.0;
            for (int i = 0; i < 3; i++) {
                double raw = (x[i] - y[i]) / dt;
                dy[i] = ad * raw + (1.0 - ad) * dy[i];
                speed2 += dy[i] * dy[i];
            }
            double cut = minCut + beta * sqrt(speed2);
            double a = OneEuroAlpha(cut, dt);
            for (int i = 0; i < 3; i++) y[i] = a * (double)x[i] + (1.0 - a) * y[i];
        }
        for (int i = 0; i < 3; i++) {
            double v = y[i];
            if (predictSec > 0.0 && vel) v += (double)vel[i] * predictSec;  // simple extrapolation
            out[i] = (float)v;
        }
    }
};

struct OneEuroQuat {
    bool   init = false;
    double q[4] = { 0, 0, 0, 1 };

    void reset() { init = false; }

    void step(const float* in, double dt, double minCut, double beta, double dCut, float* out) {
        (void)dCut;   // the rotation filter only needs the speed term
        double n[4] = { in[0], in[1], in[2], in[3] };
        double len = sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2] + n[3]*n[3]);
        if (len < 1e-9) return;
        for (int i = 0; i < 4; i++) n[i] /= len;

        if (!init) {
            for (int i = 0; i < 4; i++) q[i] = n[i];
            init = true;
        } else {
            double d = q[0]*n[0] + q[1]*n[1] + q[2]*n[2] + q[3]*n[3];
            if (d < 0) { for (int i = 0; i < 4; i++) n[i] = -n[i]; d = -d; }
            if (d > 1.0) d = 1.0;
            double angle = 2.0 * acos(d);           // rad between samples
            double speed = angle / dt;              // rad/s
            double cut = minCut + beta * speed;     // beta here is per rad/s
            double a = OneEuroAlpha(cut, dt);

            if (d > 0.9995) {
                for (int i = 0; i < 4; i++) q[i] = q[i] + a * (n[i] - q[i]);
            } else {
                double th = acos(d), s = sin(th);
                double w1 = sin((1.0 - a) * th) / s, w2 = sin(a * th) / s;
                for (int i = 0; i < 4; i++) q[i] = w1 * q[i] + w2 * n[i];
            }
            double l2 = sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
            if (l2 > 1e-9) for (int i = 0; i < 4; i++) q[i] /= l2;
        }
        for (int i = 0; i < 4; i++) out[i] = (float)q[i];
    }
};