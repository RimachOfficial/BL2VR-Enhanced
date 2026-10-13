// One Euro filters for VR hand poses.
//
// The cutoff adapts to RAW controller motion, not the filtered output or a spring's
// stored velocity. Small movements/jitter get a low cutoff; deliberate movement raises
// the cutoff immediately, so the pose does not need to accelerate out of a lag first.
// There is no stored follower velocity, so no extra spring momentum after movement stops.
//
// Replaces the critically damped follower, which drove the cutoff from the FOLLOWER's own
// velocity: that is 0 at the instant you start moving, so the filter stayed heavy until a
// lag had built up (the "boat" feeling), and its stored velocity could drift after a stop.
#pragma once
#include <cmath>

static const double kPi = 3.14159265358979323846;

// Low-pass coefficient for cutoff frequency in Hz.
static inline double OneEuroAlpha(double cutoffHz, double dt) {
    if (cutoffHz <= 0.0) return 1.0;
    const double tau = 1.0 / (2.0 * kPi * cutoffHz);
    return 1.0 / (1.0 + tau / dt);
}

struct OneEuroVec3 {
    bool   init = false;
    double y[3] = { 0, 0, 0 };       // filtered position
    double xp[3] = { 0, 0, 0 };      // previous RAW position
    double dx[3] = { 0, 0, 0 };      // low-passed raw velocity

    // catchup: extra stiffness per metre of lag (0 = off). Ported from the previous
    // implementation: the further behind we are, the faster the cutoff rises, so a big lag
    // cannot float while a small lag (tremor) stays heavily smoothed.
    double catchup = 0.0;

    void reset() {
        init = false;
        for (int i = 0; i < 3; ++i) dx[i] = 0.0;
    }

    void step(const float* x, double dt, double minCut, double beta, double dCut,
              double predictSec, const float* vel, float* out) {
        if (dt < 1e-5) dt = 1e-5;
        if (!init) {
            for (int i = 0; i < 3; ++i) {
                y[i] = xp[i] = (double)x[i];
                dx[i] = 0.0;
            }
            init = true;
        } else {
            const double ad = OneEuroAlpha(dCut, dt);
            double speed2 = 0.0;
            for (int i = 0; i < 3; ++i) {
                const double rawVelocity = ((double)x[i] - xp[i]) / dt;
                dx[i] = ad * rawVelocity + (1.0 - ad) * dx[i];
                xp[i] = (double)x[i];
                speed2 += dx[i] * dx[i];
            }
            double cutoff = minCut + beta * sqrt(speed2);
            if (catchup > 0.0) {
                const double lx = (double)x[0] - y[0];
                const double ly = (double)x[1] - y[1];
                const double lz = (double)x[2] - y[2];
                cutoff += catchup * sqrt(lx*lx + ly*ly + lz*lz);
            }
            const double a = OneEuroAlpha(cutoff, dt);
            for (int i = 0; i < 3; ++i)
                y[i] = a * (double)x[i] + (1.0 - a) * y[i];
        }

        for (int i = 0; i < 3; ++i) {
            double value = y[i];
            if (predictSec > 0.0 && vel)
                value += (double)vel[i] * predictSec;
            out[i] = (float)value;
        }
    }
};
struct OneEuroQuat {
    bool   init = false;
    double q[4] = { 0, 0, 0, 1 };   // filtered quaternion (x, y, z, w)
    double qp[4] = { 0, 0, 0, 1 };  // previous RAW quaternion
    double dw[3] = { 0, 0, 0 };     // low-passed raw angular velocity in body coordinates

    // catchup: extra stiffness per radian of lag (0 = off). See OneEuroVec3.
    double catchup = 0.0;

    void reset() {
        init = false;
        for (int i = 0; i < 3; ++i) dw[i] = 0.0;
    }

    void step(const float* in, double dt, double minCut, double beta, double dCut, float* out) {
        if (dt < 1e-5) dt = 1e-5;

        double n[4] = { in[0], in[1], in[2], in[3] };
        const double len = sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2] + n[3]*n[3]);
        if (len < 1e-9) {
            for (int i = 0; i < 4; ++i) out[i] = (float)q[i];
            return;
        }
        for (int i = 0; i < 4; ++i) n[i] /= len;

        if (!init) {
            for (int i = 0; i < 4; ++i) q[i] = qp[i] = n[i];
            for (int i = 0; i < 3; ++i) dw[i] = 0.0;
            init = true;
        } else {
            // Use the shortest quaternion arc for the RAW sample-to-sample rotation.
            double dotRaw = qp[0]*n[0] + qp[1]*n[1] + qp[2]*n[2] + qp[3]*n[3];
            if (dotRaw < 0.0) {
                for (int i = 0; i < 4; ++i) n[i] = -n[i];
                dotRaw = -dotRaw;
            }
            if (dotRaw > 1.0) dotRaw = 1.0;

            // Relative rotation conj(qp) * n, then log-map it to an angular-velocity vector.
            const double ax = qp[0], ay = qp[1], az = qp[2], aw = qp[3];
            double rx = aw*n[0] - ax*n[3] - ay*n[2] + az*n[1];
            double ry = aw*n[1] + ax*n[2] - ay*n[3] - az*n[0];
            double rz = aw*n[2] - ax*n[1] + ay*n[0] - az*n[3];
            double rw = aw*n[3] + ax*n[0] + ay*n[1] + az*n[2];
            if (rw < 0.0) { rx = -rx; ry = -ry; rz = -rz; rw = -rw; }
            if (rw > 1.0) rw = 1.0;

            const double sinHalf = sqrt(rx*rx + ry*ry + rz*rz);
            double rawW[3] = { 0.0, 0.0, 0.0 };
            if (sinHalf > 1e-9) {
                const double angle = 2.0 * atan2(sinHalf, rw);
                const double scale = angle / (sinHalf * dt);
                rawW[0] = rx * scale;
                rawW[1] = ry * scale;
                rawW[2] = rz * scale;
            }

            const double ad = OneEuroAlpha(dCut, dt);
            double speed2 = 0.0;
            for (int i = 0; i < 3; ++i) {
                dw[i] = ad * rawW[i] + (1.0 - ad) * dw[i];
                speed2 += dw[i] * dw[i];
            }
            double cutoff = minCut + beta * sqrt(speed2);

            // Interpolate the FILTERED pose toward the new RAW pose by this frame's alpha.
            double dot = q[0]*n[0] + q[1]*n[1] + q[2]*n[2] + q[3]*n[3];
            if (dot < 0.0) {
                for (int i = 0; i < 4; ++i) n[i] = -n[i];
                dot = -dot;
            }
            if (dot > 1.0) dot = 1.0;

            if (catchup > 0.0) {
                const double sres = sqrt((1.0 - dot) * (1.0 + dot));   // |sin| of half the residual
                cutoff += catchup * 2.0 * atan2(sres, dot);
            }

            const double a = OneEuroAlpha(cutoff, dt);
            if (dot > 0.9995) {
                for (int i = 0; i < 4; ++i) q[i] += a * (n[i] - q[i]);
            } else {
                const double theta = acos(dot);
                const double sinTheta = sin(theta);
                const double w1 = sin((1.0 - a) * theta) / sinTheta;
                const double w2 = sin(a * theta) / sinTheta;
                for (int i = 0; i < 4; ++i) q[i] = w1 * q[i] + w2 * n[i];
            }
            const double qLen = sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
            if (qLen > 1e-9) for (int i = 0; i < 4; ++i) q[i] /= qLen;

            for (int i = 0; i < 4; ++i) qp[i] = n[i];
        }

        for (int i = 0; i < 4; ++i) out[i] = (float)q[i];
    }
};
