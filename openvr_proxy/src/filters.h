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

// ---------------------------------------------------------------------------
// "Bubble" release. Technique taken from HandStabilizer (H3VR): below the intent
// threshold the hand is only trembling, so smooth hard; above it the motion is
// deliberate, so let the raw pose straight through.
//
// The intent signal is the magnitude of the LOW-PASSED velocity VECTOR, not |dx|/dt:
// a tremor is an oscillation, so its mean velocity is ~0, while deliberate motion has
// a non-zero mean. |dx|/dt cannot tell them apart because the magnitude of an
// oscillation has a non-zero mean.
//
// Unlock fast (inserting between targets) / re-lock slower (soft landing on target).
// Because the released state converges fully onto the raw pose, stopping does NOT
// leave a long glide: there is nothing left to creep when the bubble closes again.
// ---------------------------------------------------------------------------
static double g_relPos    = 0.13;   // m/s   - position release threshold
static double g_relRot    = 0.54;   // rad/s - rotation release threshold
static double g_relSpan   = 1.5;    // fully released at threshold * span
static double g_attackMs  = 200.0;  // re-lock time after you stop
static double g_releaseMs = 40.0;   // unlock time when you move
static double g_intentHz  = 6.0;    // low-pass on the velocity vector (tremor is 4-12 Hz)
// Second release trigger: a lag that PERSISTS. Low-passed on purpose - a tremor alternates
// sign so it averages out, while "the gun is still behind my hand" does not. This is what
// removes the float: the bubble stays open until the residual is caught, then closes.
static double g_cutMove   = 8.0;    // Hz  - lowest cutoff while released. Never raw: 8 Hz is
                                    //       20 ms, fast enough to track, slow enough to kill jitter
static double g_errTolPos = 0.015;  // m   - persistent lag that counts as falling behind
static double g_errTolRot = 0.044;  // rad - same, rotation (~2.5 deg)
static double g_errHz     = 1.5;    // Hz  - slow, so only a real lag passes this filter

static inline double ErrMag(const double* v) { return sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); }

// how far outside its bubble the hand is moving: 0 = inside (tremor), 1 = deliberate
static inline double BubbleTarget(double intent, double thresh) {
    if (thresh <= 0.0) return 1.0;                 // threshold 0 = bubble off, always raw
    double span = (g_relSpan > 1.0 ? g_relSpan : 1.0) * thresh;
    double t = (intent - thresh) / (span - thresh);
    return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
}

// ease the release factor toward its target - asymmetric attack/release rates
static inline double BubbleChase(double rel, double t, double dt) {
    double ms = (t > rel) ? g_releaseMs : g_attackMs;
    if (ms <= 0.0) return t;
    rel += (t - rel) * (dt / (ms / 1000.0));
    return rel < 0.0 ? 0.0 : (rel > 1.0 ? 1.0 : rel);
}

struct OneEuroVec3 {
    bool   init = false;
    bool   xset = false;
    double y[3] = { 0, 0, 0 };    // filtered output
    double dy[3] = { 0, 0, 0 };   // filtered derivative
    double xp[3] = { 0, 0, 0 };   // previous raw input (intent velocity needs it)
    double iv[3] = { 0, 0, 0 };   // low-passed intent velocity
    double er[3] = { 0, 0, 0 };   // low-passed signed residual (the persistent-lag detector)
    double rel = 0.0;             // 0 = locked (smooth), 1 = released (follows the hand)

    void reset() {
        init = xset = false; rel = 0.0;
        for (int i = 0; i < 3; i++) { dy[i] = 0.0; iv[i] = 0.0; er[i] = 0.0; }
    }
    double release() const { return rel; }

    void step(const float* x, double dt, double minCut, double beta, double dCut,
              double predictSec, const float* vel, float* out) {
        if (!init) {
            for (int i = 0; i < 3; i++) { y[i] = x[i]; xp[i] = x[i]; dy[i] = 0.0; iv[i] = 0.0; }
            init = true; xset = true; rel = 0.0;
        } else {
            if (!xset) { for (int i = 0; i < 3; i++) xp[i] = x[i]; xset = true; }

            // ---- intent: raw derivative, low-passed as a VECTOR then measured ----
            double ai = OneEuroAlpha(g_intentHz, dt);
            double intent = 0.0;
            for (int i = 0; i < 3; i++) {
                double rv = ((double)x[i] - xp[i]) / dt;
                iv[i] = ai * rv + (1.0 - ai) * iv[i];
                xp[i] = (double)x[i];
                intent += iv[i] * iv[i];
            }
            intent = sqrt(intent);

            // ---- release: fast movement OR a persistent lag opens the bubble ----
            double t1 = BubbleTarget(intent, g_relPos);           // deliberate movement
            double t2 = BubbleTarget(ErrMag(er), g_errTolPos);    // falling behind
            rel = BubbleChase(rel, (t1 > t2 ? t1 : t2), dt);

            // ---- the one-euro low-pass itself ----
            double ad = OneEuroAlpha(dCut, dt);
            double speed2 = 0.0;
            for (int i = 0; i < 3; i++) {
                double raw = (x[i] - y[i]) / dt;
                dy[i] = ad * raw + (1.0 - ad) * dy[i];
                speed2 += dy[i] * dy[i];
            }
            double cut = minCut + beta * sqrt(speed2);
            // Released => the cutoff may only RISE, up to g_cutMove. There is no raw
            // passthrough, so a fast move is still filtered (no jitter) and, because this is
            // still a first-order approach, the output can never pass the hand position.
            double floorCut = minCut + (g_cutMove - minCut) * rel;
            if (floorCut > cut) cut = floorCut;
            double a = OneEuroAlpha(cut, dt);
            for (int i = 0; i < 3; i++) y[i] = a * (double)x[i] + (1.0 - a) * y[i];

            // how far behind we still are, low-passed: an oscillation cancels out here
            double ae = OneEuroAlpha(g_errHz, dt);
            for (int i = 0; i < 3; i++)
                er[i] = ae * ((double)x[i] - y[i]) + (1.0 - ae) * er[i];
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
    bool   qset = false;
    double q[4] = { 0, 0, 0, 1 };
    double qp[4] = { 0, 0, 0, 1 };   // previous raw input
    double iv[3] = { 0, 0, 0 };      // low-passed intent angular velocity (rad/s)
    double erAng = 0.0;              // low-passed residual angle (the persistent-lag detector)
    double rel = 0.0;                // 0 = locked (smooth), 1 = released (follows the hand)

    void reset() {
        init = qset = false; rel = 0.0; erAng = 0.0;
        for (int i = 0; i < 3; i++) iv[i] = 0.0;
    }
    double release() const { return rel; }

    void step(const float* in, double dt, double minCut, double beta, double dCut, float* out) {
        (void)dCut;   // the rotation filter only needs the speed term
        double n[4] = { in[0], in[1], in[2], in[3] };
        double len = sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2] + n[3]*n[3]);
        if (len < 1e-9) return;
        for (int i = 0; i < 4; i++) n[i] /= len;

        if (!init) {
            for (int i = 0; i < 4; i++) { q[i] = n[i]; qp[i] = n[i]; }
            for (int i = 0; i < 3; i++) iv[i] = 0.0;
            init = true; qset = true; rel = 0.0;
        } else {
            if (!qset) { for (int i = 0; i < 4; i++) qp[i] = n[i]; qset = true; }

            // ---- intent: signed angular velocity of the RAW input, low-passed as a vector ----
            // dq = conj(prev) * now  ->  the rotation from the last sample to this one.
            // Keeping it signed (axis included) is what lets a tremor cancel out: the tremor
            // oscillates, so on average its velocity vectors point nowhere. 2*acos(dot)/dt
            // would not work - |oscillation| has a non-zero mean and looks like intent.
            {
                double ax = qp[0], ay = qp[1], az = qp[2], aw = qp[3];
                double dx = aw*n[0] - ax*n[3] - ay*n[2] + az*n[1];
                double dy = aw*n[1] + ax*n[2] - ay*n[3] - az*n[0];
                double dz = aw*n[2] - ax*n[1] + ay*n[0] - az*n[3];
                double dw = aw*n[3] + ax*n[0] + ay*n[1] + az*n[2];
                if (dw < 0) { dx = -dx; dy = -dy; dz = -dz; dw = -dw; }
                if (dw > 1.0) dw = 1.0;
                double hth = acos(dw);                 // half the angle between the samples
                double s = sin(hth);
                double wx = 0.0, wy = 0.0, wz = 0.0;
                if (s > 1e-7) {                        // axis * rate, in rad/s
                    double k = (2.0 * hth / dt) / s;
                    wx = dx * k; wy = dy * k; wz = dz * k;
                }
                double ai = OneEuroAlpha(g_intentHz, dt);
                iv[0] = ai * wx + (1.0 - ai) * iv[0];
                iv[1] = ai * wy + (1.0 - ai) * iv[1];
                iv[2] = ai * wz + (1.0 - ai) * iv[2];
                for (int i = 0; i < 4; i++) qp[i] = n[i];
            }
            double intent = sqrt(iv[0]*iv[0] + iv[1]*iv[1] + iv[2]*iv[2]);

            // ---- release: fast rotation OR a persistent lag opens the bubble ----
            double t1 = BubbleTarget(intent, g_relRot);            // deliberate rotation
            double t2 = BubbleTarget(erAng, g_errTolRot);          // falling behind
            rel = BubbleChase(rel, (t1 > t2 ? t1 : t2), dt);

            // ---- the one-euro low-pass itself ----
            double d = q[0]*n[0] + q[1]*n[1] + q[2]*n[2] + q[3]*n[3];
            if (d < 0) { for (int i = 0; i < 4; i++) n[i] = -n[i]; d = -d; }
            if (d > 1.0) d = 1.0;
            double angle = 2.0 * acos(d);           // rad between samples
            double speed = angle / dt;              // rad/s
            double cut = minCut + beta * speed;     // beta here is per rad/s
            // Released => the cutoff may only RISE, up to g_cutMove. No raw passthrough: a fast
            // swing is still filtered (no jitter), and slerp only ever moves TOWARDS the hand,
            // so the aim cannot pass it.
            double floorCut = minCut + (g_cutMove - minCut) * rel;
            if (floorCut > cut) cut = floorCut;
            double a = OneEuroAlpha(cut, dt);

            if (d > 0.9995) {
                for (int i = 0; i < 4; i++) q[i] = q[i] + a * (n[i] - q[i]);
            } else {
                double th = acos(d), s = sin(th);
                double w1 = sin((1.0 - a) * th) / s, w2 = sin(a * th) / s;
                for (int i = 0; i < 4; i++) q[i] = w1 * q[i] + w2 * n[i];
            }

            // how far the aim still is from the hand (rad), low-passed: an alternating tremor
            // averages out here and only a real, sustained lag survives
            double dr = q[0]*n[0] + q[1]*n[1] + q[2]*n[2] + q[3]*n[3];
            if (dr < 0) dr = -dr;
            if (dr > 1.0) dr = 1.0;
            double ae = OneEuroAlpha(g_errHz, dt);
            erAng = ae * (2.0 * acos(dr)) + (1.0 - ae) * erAng;

            double l2 = sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
            if (l2 > 1e-9) for (int i = 0; i < 4; i++) q[i] /= l2;
        }
        for (int i = 0; i < 4; i++) out[i] = (float)q[i];
    }
};