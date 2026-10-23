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
