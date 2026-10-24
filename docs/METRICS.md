# Metrics — measured, not asserted

Every performance claim in this repo comes from one of three instruments. No vibes.

## 1. Offline bench — `tools/filter_bench.cpp`

Runs the **real shipping filter code** (`openvr_proxy/src/filters.h`) on synthetic motions at
120 Hz. Reproduce:

```
g++ -O2 -std=c++17 -I openvr_proxy tools/filter_bench.cpp -o filter_bench.exe
./filter_bench.exe
```

Synthetic signals: slow move 15°/s, fast move 60°/s, hand tremor 0.3° at 8 Hz, step correction 2°,
settle threshold 0.5°.

### Results (rotation profiles, degrees)

| profile | lag slow | lag fast | float tail | 2° fix t90 | tremor kept |
|---|---|---|---|---|---|
| trigger, old values 0.375 Hz | 5.26 | 13.86 | 1.28 s | 0.96 s | 4.8% |
| trigger, 0.30 Hz + catchup 120 | **0.93** | **1.79** | **0.08 s** | 0.24 s | 10.4% |
| two-hand, old 0.4 Hz | 4.29 | 9.29 | 0.92 s | 0.87 s | 5.2% |
| two-hand, 0.30 Hz + catchup 120 | **0.86** | **1.55** | **0.05 s** | 0.22 s | 10.8% |

**Headline:** post-move float tail 1.28 s → 0.08 s (**16× faster settle**); fast-move rotation lag
13.86° → 1.79° (**7.7× less lag**).

### Cutoff → forced lag (first-order equivalent)

| cutoff | lag it forces | verdict |
|---|---|---|
| 0.05 Hz | 3.18 s | drifts away |
| 0.30 Hz | 0.53 s | only usable *with* catch-up or high beta |
| 1.5 Hz | 0.11 s | light |
| 3.0 Hz | 0.05 s | default band |
| 4.5 Hz | 0.035 s | nearly raw |

Settle-time reference used in the log: `settle(95%) ≈ 2.4 · smoothTime = 0.38 / cutoff`.

## 1b. Tremor sweep — how much hand tremor actually survives

`tools/tremor_sweep.cpp` answers the one question the bench above does not: **of an 8 Hz hand
tremor (where VR hand shake actually lives), how much is left in the output?** Same real filter
code, 120 Hz, 5 s settle + 5 s measure, peak amplitude of the output vs the input.

```
g++ -O2 -std=c++17 -I openvr_proxy tools/tremor_sweep.cpp -o tremor_sweep
./tremor_sweep
```

| rotation cutoff | lag at rest | tremor 0.3° kept | tremor 1.0° kept |
|---|---|---|---|
| 6.0 Hz (Responsive) | 27 ms | 60% | 62% |
| 4.5 Hz | 35 ms | 46.5% | 49.0% |
| **3.0 Hz (shipped default)** | 53 ms | **34.4%** | **37.7%** |
| 2.25 Hz (Hybrid) | 71 ms | 27.1% | 29.9% |
| 2.0 Hz | 80 ms | 24.4% | 26.8% |
| 1.5 Hz | 106 ms | 18.8% | 20.9% |
| 1.0 Hz (Steady) | 159 ms | 12.7% | 14.1% |
| 0.8 Hz (Locked) | 199 ms | ~10% | ~11% |
| 0.5 Hz | 318 ms | 6.5% | 7.4% |

Read that honestly: **the shipped base profile removes about two thirds of the tremor, not "all of
it"** — a third still gets through, which is exactly why a lighter profile can feel like "the mod
is barely doing anything". The profiles in the panel spread across this table on purpose.

It is also roughly amplitude-independent (0.3° vs 1.0° tremor give similar percentages), i.e. the
adaptive cutoff does not collapse on bigger transients.

### The `derivative_cutoff` hypothesis, tested and rejected

Our `derivative_cutoff` is 6 Hz where the reference 1€ spec says ~1 Hz (the velocity estimator's
own filter). The theory was that a noisier velocity estimate would inflate the cutoff and let
tremor through. Measured:

| rotation cutoff | d_cutoff 1.0 | d_cutoff 3.0 | d_cutoff 6.0 |
|---|---|---|---|
| 4.5 Hz | 45.6% | 46.1% | 46.5% |
| 3.0 Hz | 33.4% | 33.9% | 34.4% |
| 1.5 Hz | 18.3% | 19.0% | 19.6% |
| 1.0 Hz | 12.6% | 13.3% | 13.9% |

Under one percentage point. The hypothesis is **wrong** — 6 Hz is fine, and lowering it to the
reference value would only add ~9 ms of movement-onset delay for no measurable rejection gain.

## 1c. Why the smoothing seemed weak: mixed prediction timelines (measured)

The game polls `ovr_GetTrackingState` from **two threads** whose `absTime` values are offset by
about one frame and interleaved. From a real session log (sampled lines only):

```
t=2593.7865  dt=0.0122  tid=28368   <- thread B, predicted to t
t=2593.7727  dt=0.0001  tid=25120   <- thread A, predicted to t-14ms (one frame BACK)
t=2593.8004  dt=0.0155  tid=28368   <- B
t=2593.7865  dt=0.0001  tid=25120   <- A, equal to B's PREVIOUS value
```

With **one shared filter state**, the pose it receives therefore steps *backwards* on roughly
half of all calls. Measured on the full log: **`t` went backwards on 1449 of 3325 samples (44%)**,
with jumps back of up to 0.58 s. The raw derivative `(x - x_prev)/dt` then reports velocity the
hand never had — and because `dt` for those calls sits at the 1e-4 clamp floor, the number is
enormous (tens of m/s). The adaptive cutoff is `min_cutoff + beta*|v|`, so:

- the filter opens completely → **no smoothing**, exactly the "smoothing seems off" report;
- `min_cutoff` becomes irrelevant next to the fake velocity term → **profile changes look like
  they do nothing** (0.4 Hz vs 4.5 Hz makes no difference when `beta*|v|` is ~60 Hz).

Fix: `per_thread_filters=1` (default since v0.1.6) gives each calling thread its own filters and
clock, so each filter only ever sees one monotonic timeline. The jitter line now reports
`t-backwards=N/M`; it was ~44% with a shared state and should be ~0 per thread.

## 1d. Why the trigger felt like it switched smoothing OFF

Profiles set a `catchup` term (extra stiffness per radian of lag). Catch-up works by *opening*
the filter, so it directly trades smoothing away — and it used to be applied **while the trigger
was held**, i.e. exactly when the player wants maximum steadiness.

`tools/catchup_demo.cpp` solves the steady state `lag = ω / (2π(f₀ + catchup·lag))`:

| aim speed | profile | catchup | effective cutoff | tremor passed |
|---|---|---|---|---|
| 60°/s | 0.8 Hz | 0 | 0.80 Hz | ~10% |
| 60°/s | 0.8 Hz | 150 | 5.42 Hz | ~60% |
| 60°/s | **0.4 Hz (trigger held)** | 0 | 0.40 Hz | ~10% |
| 60°/s | **0.4 Hz (trigger held)** | 150 | **5.20 Hz** | **~60%** |

So holding the trigger made the aim **~6× rougher**, not steadier. Fixed in v0.1.6: catch-up is
skipped entirely while the trigger is held, and the profile values dropped from 80–160 to 0–30.

## 2. In-game jitter meter (built into the mod)

Prints the right-hand rotation delta per `ovr_GetTrackingState` call, raw vs filtered, to
`openvr_proxy.log`. Evidence from the dt-bug investigation:

- Before the dt fix: meter read **3.3× vs 3.1× smoother** held-vs-not — i.e. the trigger
  profile was *applying* but invisible, because the follower's clock ran 55× slow.
- After: real smoothing at the configured cutoffs; `dt=` in the log reads ~0.005–0.006 s
  (game poll interval ~180 Hz: 120 calls / 0.667 s measured).

## 3. Functional checks (log lines)

| Check | Expected in `openvr_proxy.log` |
|---|---|
| Trigger gate engages | `trigger gate -> HELD ...` with `trig=1.000`, `poll_rc >= 0` |
| Trigger gate releases | `trigger gate -> released ...` |
| Two-hand aim axis | `twohand: ACTIVE ... best-matching axis=+z (0.998)` — value close to ±1 |
| Hot-reload | config change visible ≤ 2 s without restart |

If there is no gate line, or `trig=0.000`, or `poll_rc` is not ≥ 0, the trigger is not being read
(the old "no smoothing on trigger" bug). Procedure: [TESTING.md](TESTING.md).

## Engineering outcomes (not timing, but measurable)

| Outcome | Before | After |
|---|---|---|
| Config reload cost | ~30 `GetPrivateProfile*` calls, each re-opening the ini from disk | one 8 KB read + in-memory parse |
| Biggest design open question ("which pose API does the game read?") | unknown — plan listed 3 candidates | answered **in one evening**: none of them; no `IVRInput`, Oculus path |
| Game files modified | early attempt patched `Startup.upk` (reverted) | **zero** — 2 added files only |
| Distribution surface | — | source only; no game/Valve/Meta binaries in the repo |

## Honesty notes

- Bench numbers are synthetic-motion results of the shipping filter code — comparable across
  configs, not a promise of in-game feel.
- The 3.3× jitter figure is an in-game log measurement on one PC + one headset (OMEN
  Transcend 16, RTX 4070 laptop, Virtual Desktop + Quest-class HMD).
- The catch-up rows are provided for comparison; `catchup` ships **off** (0), the validated default.
