# Tuning guide — how the smoothing works, and what every knob trades away

Moved out of the README so it can go deep. Read [METRICS.md](METRICS.md) for how these numbers
are produced; read [THE_STORY.md](THE_STORY.md) for why the rules below exist.

## The canonical One Euro filter

It is the **canonical One Euro filter** (Casiez, Roussel & Vogel 2012), in its correct form:

```
cutoff = min_cutoff + beta * |RAW controller velocity|
a      = 1 / (1 + tau/dt),   tau = 1 / (2*pi*cutoff)
pose  += a * (hand - pose)
```

**The cutoff is driven by the RAW controller motion** — not by the filtered output, and not by a
follower's own velocity. That distinction is the whole thing:

- The instant you start moving, the raw velocity is already large, so the cutoff **rises
  immediately**. The pose never has to build up a lag before the filter lets go — that delay was
  the "boat" feeling.
- There is **no stored follower velocity**, so no spring momentum is left over after you stop:
  nothing carries the aim past the point you stopped at.
- The residual at the moment you stop is already tiny, because the filter stayed open through the
  whole movement *including the deceleration*. That is why even a low `min_cutoff` does not float.

`derivative_cutoff` smooths the speed estimate itself, and is deliberately high (6 Hz) so the
filter reacts to movement **onset** immediately.

`beta` stiffens the spring smoothly as the gun picks up speed (no threshold, so nothing to
toggle). Higher `beta` = catches up sooner on a fast sweep, at the cost of some of the weight.

## The measured tradeoff — no free lunch

The offline bench (runs the real shipping filter code, 120 Hz, synthetic motions):

```
g++ -O2 -std=c++17 -I openvr_proxy tools/filter_bench.cpp -o filter_bench.exe
./filter_bench.exe
```

| profile (rotation, deg) | lag slow | lag fast | float tail | 2° fix t90 | tremor kept |
|---|---|---|---|---|---|
| trigger, old values 0.375 Hz | 5.26 | 13.86 | 1.28 s | 0.96 s | **4.8%** |
| trigger, 0.30 Hz + catchup 120 | **0.93** | **1.79** | **0.08 s** | 0.24 s | **10.4%** |
| two-hand, old 0.4 Hz | 4.29 | 9.29 | 0.92 s | 0.87 s | 5.2% |
| two-hand, 0.30 Hz + catchup 120 | **0.86** | **1.55** | **0.05 s** | 0.22 s | 10.8% |

Read it honestly: the current tuning keeps **about twice as much tremor** as the old heavy
tuning (10.4% of an 8 Hz hand tremor passes through, vs 4.8%). In exchange, fast-move rotation
lag drops from 13.9° to 1.8° and the post-move float from 1.28 s to 0.08 s. That is the right
trade for a shooter — a lagging gun misses, a slightly livelier gun still hits — but it *is* a
trade. If you want the old heavier feel, lower the cutoffs and accept the drift.

### The rule that matters: no sub-1 Hz cutoffs

| cutoff | lag it forces | verdict |
|---|---|---|
| 0.05 Hz | 3.18 s | drifts away |
| 0.30 Hz | 0.53 s | only usable *with* catch-up or a high beta |
| 1.5 Hz | 0.11 s | light |
| 3.0 Hz | 0.05 s | the default band |
| 4.5 Hz | 0.035 s | nearly raw |

Keep the cutoffs in the **1.5–4.5 Hz** band and let `beta` do the work.

> Reference from elsewhere: Meta's Interaction SDK ships its wrist-rotation one-euro filter at
> `minCutoff 2.0 Hz, beta 3.0` — a production rotation filter sits in exactly this band.

**Tuning rule of thumb:** smoother at rest → lower the `min_cutoff`s. Better tracking while
rotating → raise the `beta`s. They are independent, which is the whole point of this filter.

## `catchup` — optional, off by default

`catchup_pos` / `catchup_rot` add stiffness proportional to how far behind the pose is, for the
heavy profiles only:

```
cutoff = min_cutoff + beta*|raw velocity| + catchup*|lag|
```

It is a continuous term, so unlike a threshold it cannot toggle while you track a moving enemy.
It is **0 (off)** by default, which is the validated behaviour; the bench's catch-up rows above
show what it buys if you want it. The **Steady** and **Sniper** panel profiles turn it on.

## Do not add a speed/error threshold

The one thing that must not come back is a **threshold that switches the filter on and off** (a
"bubble"). Tracking a moving enemy puts the hand speed right on top of any such threshold, so it
toggles several times a second — and every toggle is a lurch, plus a "boat" feeling on the way in
because the release has to ramp before it does anything. There is no smooth way to switch; only
not to switch. A single always-on interpolator has no state to flip, so it has no jerk, and it
cannot *cancel* another profile's smoothing the way a release floor does.

## The trigger gate (`OVR_SUCCESS`)

`ovr_GetInputState` returns **`>= 0`** on success — `OVR_SUCCESS(result)` is `result >= 0`. An
earlier build tested `== 0`, which **discarded every successful poll** and left the gate dead, so
the trigger profile never engaged. It also queries `ovrControllerType_Touch` (LTouch|RTouch) in
one call, so both `[left,right]` array slots are populated regardless of how a runtime treats
single-hand queries. The log prints a line on every gate flip with `poll_rc`.

## A note on `dt` (and a wrong turn we took)

`dt` comes from `QueryPerformanceCounter`. The game polls `ovr_GetTrackingState` in bursts from
more than one thread, so a single call's delta is often tiny — the log shows `dt=0.0001`, which
is the `1e-4` clamp floor. That is **not** a bug: consecutive deltas telescope, so their sum over
a second is a second, and the filter's clock stays right. Only sub-0.1 ms deltas are inflated by
the clamp, a small fraction at the ~5.6 ms average interval.

(An intermediate build replaced this with an EMA of the call interval, based on a misreading of
"dt=0.0001 means the clock runs 55× slow" — which the jitter meter already contradicted. It was
reverted; the two forms are functionally equivalent.)

Related, and still open: `ovr_GetTrackingState` takes an `absTime` and returns poses *predicted
to that time*. If different threads ask for different prediction horizons, one shared filter
state is mixing timelines — the clock telescopes, the prediction horizon does not.

### `per_thread_filters` — one filter state per calling thread

Default **0** (shared state, the validated behaviour). Set to **1** when the log says the
timelines are mixed: you are looking for the periodic `#N dt=... t=... tid=...` lines and the
`jitter R-rot: ... | polling threads=N` line.

| In `openvr_proxy.log` | Meaning | Action |
|---|---|---|
| `t=` moves smoothly, one `tid=` (or several `tid=` sharing one `t=` progression) | one prediction timeline | leave `per_thread_filters=0` |
| `t=` jumps between distinct values on consecutive calls with different `tid=` | threads ask for different horizons; a shared state mixes them | try `1`, compare the jitter line |

Why it can help: with a shared state, each call that asks for a different horizon makes the pose
jump forward and back in time. The filter reads that as velocity the hand never had, the cutoff
opens, and the aim gets livelier than you asked for. Per-thread state keeps each filter on one
timeline.

Why it is not the default: each thread then sees roughly half the samples (the calls interleave),
so each filter's velocity estimate is a little noisier. Both settings are measurable — the
`jitter R-rot` line prints raw vs filtered deg/call — so keep whichever is calmer. The panel has
it under **Smoothing → Advanced**.

## Two-hand filter override keys (optional)

Two-hand mode uses the **same base/trigger profile** as everything else — the stock line has its
own filter, so no separate hand profile is needed. These keys remain as an optional override;
**0 = fall back to the base profile (recommended)**:

| Key | Default | Meaning |
|---|---|---|
| `twohand_min_cutoff` | 3.0 | position cutoff override while two-handing (Hz), 0 = use base |
| `twohand_beta` | 2.2 | one-euro speed term override |
| `twohand_min_cutoff_rot` | 2.0 | rotation cutoff override, 0 = use base |
| `twohand_beta_rot` | 1.5 | rotation equivalent |
| `catchup_pos` | 0 | extra stiffness per metre of lag, heavy profiles only (0 = off) |
| `catchup_rot` | 0 | extra stiffness per radian of lag, heavy profiles only (0 = off) |

