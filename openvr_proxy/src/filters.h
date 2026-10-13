// Critically damped follower - Unity's "SmoothDamp" (Game Programming Gems 4).
//
// Why not a first-order low-pass: a low-pass reaches its maximum speed in the VERY FIRST frame.
// Velocity jumps from 0 to max instantly = infinite acceleration = a visible kick at the start
// and a hard corner on arrival. That is "lagging", not "smooth".
//
// This is a second-order critically damped spring (zeta = 1):
//     acc = w^2 * err  -  2*w * v            w = 2 / smoothTime
// The velocity starts at ZERO, ramps up, peaks, then decays as it lands: the S-curve.
// Critically damped => it cannot overshoot. The exp() form below is Unity's closed-form
// solution, which stays stable and frame-rate independent even at low frame rates.
//
// smoothTime is in seconds (roughly the time taken to reach the target) and maps 1:1 onto the
// old cutoff tuning:  smoothTime = 1 / (2*pi*cutoff).
#pragma once
#include <cmath>

static const double kPi = 3.14159265358979323846;

static inline double DampOmega(double smoothTimeSec) {
    return 2.0 / (smoothTimeSec > 1e-4 ? smoothTimeSec : 1e-4);
}
// Unity's approximation of e^(-w*dt) - keeps the spring stable at any frame rate
static inline double DampExp(double omega, double dt) {
    double x = omega * dt;
    return 1.0 / (1.0 + x + 0.48 * x * x + 0.235 * x * x * x);
}

struct OneEuroVec3 {
    bool   init = false;
    double y[3] = { 0, 0, 0 };    // the pose we output
    double v[3] = { 0, 0, 0 };    // the follower's own velocity (this is what makes the S-curve)

    void reset() { init = false; for (int i = 0; i < 3; i++) v[i] = 0.0; }

    void step(const float* x, double dt, double minCut, double beta, double dCut,
              double predictSec, const float* vel, float* out) {
        (void)dCut;
        double st = (minCut <= 0.0) ? 0.0 : 1.0 / (2.0 * kPi * minCut);   // cutoff Hz -> seconds
        if (!init) {
            for (int i = 0; i < 3; i++) { y[i] = (double)x[i]; v[i] = 0.0; }
            init = true;
        } else if (st > 0.0) {
            double sp = sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
            if (beta > 0.0) {                 // moving => stiffen. Smooth, no threshold, no toggle
                double s = st / (1.0 + beta * sp);
                if (s < 0.01) s = 0.01;
                st = s;
            }
            double w = DampOmega(st), ex = DampExp(w, dt);
            for (int i = 0; i < 3; i++) {
                double d  = (double)x[i] - y[i];      // how far behind we still are
                double tp = (v[i] - w * d) * dt;
                v[i] = (v[i] - w * tp) * ex;
                y[i] = (double)x[i] + (-d + tp) * ex;
            }
        }
        for (int i = 0; i < 3; i++) {
            double o = y[i];
            if (predictSec > 0.0 && vel) o += (double)vel[i] * predictSec;  // simple extrapolation
            out[i] = (float)o;
        }
    }
};

struct OneEuroQuat {
    bool   init = false;
    double q[4] = { 0, 0, 0, 1 };
    double av[3] = { 0, 0, 0 };   // body-frame angular velocity (rad/s) - the S-curve lives here

    void reset() { init = false; for (int i = 0; i < 3; i++) av[i] = 0.0; }

    void step(const float* in, double dt, double minCut, double beta, double dCut, float* out) {
        (void)dCut;
        double n[4] = { in[0], in[1], in[2], in[3] };
        double len = sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2] + n[3]*n[3]);
        if (len < 1e-9) return;
        for (int i = 0; i < 4; i++) n[i] /= len;

        double st = (minCut <= 0.0) ? 0.0 : 1.0 / (2.0 * kPi * minCut);   // cutoff Hz -> seconds
        if (!init) {
            for (int i = 0; i < 4; i++) q[i] = n[i];
            for (int i = 0; i < 3; i++) av[i] = 0.0;
            init = true;
        } else if (st > 0.0) {
            // r = rotation vector from q to n, in q's own (body) frame. Its length is the angle
            // we are still behind by - the rotation equivalent of "err".
            double ax = q[0], ay = q[1], az = q[2], aw = q[3];
            double dx = aw*n[0] - ax*n[3] - ay*n[2] + az*n[1];
            double dy = aw*n[1] + ax*n[2] - ay*n[3] - az*n[0];
            double dz = aw*n[2] - ax*n[1] + ay*n[0] - az*n[3];
            double dw = aw*n[3] + ax*n[0] + ay*n[1] + az*n[2];
            if (dw < 0) { dx = -dx; dy = -dy; dz = -dz; dw = -dw; }
            if (dw > 1.0) dw = 1.0;
            double ang = 2.0 * acos(dw), sl = sqrt(dx*dx + dy*dy + dz*dz);
            double r[3];
            if (sl > 1e-9) { double k = ang / sl; r[0] = dx*k; r[1] = dy*k; r[2] = dz*k; }
            else           { r[0] = r[1] = r[2] = 0.0; }

            double sp = sqrt(av[0]*av[0] + av[1]*av[1] + av[2]*av[2]);
            if (beta > 0.0) {                 // turning => stiffen. Smooth, no threshold, no toggle
                double t = st / (1.0 + beta * sp);
                if (t < 0.01) t = 0.01;
                st = t;
            }
            double w = DampOmega(st), ex = DampExp(w, dt);
            double stepR[3];
            for (int i = 0; i < 3; i++) {
                double tp = (av[i] - w * r[i]) * dt;
                av[i] = (av[i] - w * tp) * ex;
                stepR[i] = r[i] - (r[i] - tp) * ex;   // the part of the lag we close this frame
            }

            // q = q * exp(stepR)  - a body-frame rotation, from zero angular velocity
            double sh = 0.5 * sqrt(stepR[0]*stepR[0] + stepR[1]*stepR[1] + stepR[2]*stepR[2]);
            if (sh > 1e-12) {
                double kk = sin(sh) / (2.0 * sh);
                double d0 = stepR[0]*kk, d1 = stepR[1]*kk, d2 = stepR[2]*kk, d3 = cos(sh);
                double o0 = q[3]*d0 + q[0]*d3 + q[1]*d2 - q[2]*d1;
                double o1 = q[3]*d1 - q[0]*d2 + q[1]*d3 + q[2]*d0;
                double o2 = q[3]*d2 + q[0]*d1 - q[1]*d0 + q[2]*d3;
                double o3 = q[3]*d3 - q[0]*d0 - q[1]*d1 - q[2]*d2;
                q[0] = o0; q[1] = o1; q[2] = o2; q[3] = o3;
            }
            double l2 = sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
            if (l2 > 1e-9) for (int i = 0; i < 4; i++) q[i] /= l2;
        }
        for (int i = 0; i < 4; i++) out[i] = (float)q[i];
    }
};