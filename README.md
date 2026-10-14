# Borderlands 2 VR — Two-Handed Grip + Aim Smoothing

A DLL-proxy mod for **Borderlands 2 VR** (Steam AppID 991260, version 1.0.462.2101248).

Two features, driven by your controller buttons (both changeable in the ini):

1. **Virtual gunstock (two-handed grip)** — while you hold the **grip button** (default) the gun
   aims along the **line between your two hands**. Your hands stay completely free to slide back
   and forth along the grip; only the *aim direction* is locked to the line. The line passes
   through **its own low-pass filter**, so a quick movement of the main hand only *leans* the aim
   like a real stock braced against the shoulder — it never snaps. Move the off-hand and the gun
   rotates with it.
2. **Rotation smoothing** — your hand poses are run through a One Euro filter, so the small
   tremble that ruins long-range shots is removed. **Every system builds on this base**: the
   hands are filtered first, and the stock line is derived from the *filtered* hands. It gets
   much heavier while the **index trigger** is held — and the trigger now also makes the stock
   heavier (that is the zoom feel), in both one-handed and two-hand mode.

Works in **normal and lefty layouts** (see `gun_hand`).

---

## Install

1. In `...\Borderlands 2 VR\Binaries\Win64\`:
   - copy the game's original `openvr_api.dll` to **`openvr_api_orig.dll`** (keep it — our proxy
     forwards to that name),
   - put **our `openvr_api.dll`** and **`bl2vr_grip.ini`** in the same folder.
2. Launch the game **through Virtual Desktop** (VD → Games).

**Uninstall:** copy `openvr_api_orig.dll` back over `openvr_api.dll` and delete `bl2vr_grip.ini`.

> **Important:** launching this game through SteamVR crashes it (a pre-existing game/runtime
> incompatibility, reproducible with the untouched original DLL). Use Virtual Desktop.

---

## How it works (short version)

- On this setup BL2VR does **not** run on OpenVR — it runs on **Virtual Desktop's Oculus runtime**
  (`VirtualDesktop.LibOVRRT64_1.dll`). Verified from the loaded-module list in-game.
- The game **always** loads `openvr_api.dll` from its own folder, so that file is our way in: our
  DLL forwards every OpenVR export to the original and hosts the payload.
- A worker thread waits for VD's Oculus runtime to appear, then installs a **MinHook** detour on
  **`ovr_GetTrackingState`**, the call the game uses to read controller poses.
- Each call: both hand poses run through the filter **first**, then the virtual gunstock solve
  runs on the *filtered* hands — smoothing is the base of everything.
- All struct layouts were taken from the generated Oculus SDK bindings (not guessed) and then
  **confirmed against a raw dump of the live tracking state** on real hardware.
- A built-in **jitter meter** prints how much the right-hand rotation actually moved per call,
  raw vs filtered, so improvements are measured rather than guessed.

Log file: `Binaries\Win64\openvr_proxy.log` (or `%LOCALAPPDATA%\BL2VR-grip\openvr_proxy.log`).

---

## How the two-hand solve works (virtual gunstock)

```
aim_line = normalize(filtered_offhand_pos - filtered_gunhand_pos)
aim_line = OneEuro_lowpass(aim_line, stock_cutoff, stock_beta)   <- the stock's inertia
gun orientation = rotate(barrel_axis -> aim_line) * gun orientation
```

The hands are filtered first (base/trigger profile), the line between them is then smoothed once
more by its **own** One Euro filter, and only that smoothed line drives the gun. Because it is a
delta on top of the filtered pose, the game's own per-weapon "how the gun sits in the hand" offset
is preserved. **Positions are never modified** — the hands stay free to slide along the grip, and
a quick main-hand movement only leans the aim instead of throwing it.

---

## Config — `bl2vr_grip.ini`

**This file is re-read every 2 seconds while the game runs.** Edit it mid-session and the change
applies within a moment — no restart needed.

### Smoothing
| Key | Default | Meaning |
|---|---|---|
| `enabled` | 1 | master switch |
| `smooth_hands` | 1 | smooth both hand poses |
| `smooth_head` | 0 | smooth the head pose — usually bad (view lag) |
| `min_cutoff` | 4.5 | position cutoff (Hz): lower = steadier at rest, laggier |
| `beta` | 2.0 | position: how fast the cutoff rises with the RAW speed |
| `derivative_cutoff` | 6.0 | smooths the speed estimate — kept high so movement onset reacts instantly |
| `min_cutoff_rot` | 3.0 | rotation cutoff (Hz) |
| `beta_rot` | 1.5 | rotation equivalent of `beta` |
| `predict_ms` | 0 | extra motion prediction (0 = off — extrapolation is the one thing that *can* overshoot) |

### Zoom (trigger) gate
| Key | Default | Meaning |
|---|---|---|
| `trigger_gate` | 1 | 0 = ignore trigger, 1 = stronger while held, 2 = smooth **only** while held |
| `zoom_hand` | either | which controller's trigger drives the gate: `gun` / `offhand` / `either` (whichever pulls harder). `either` keeps the gate working even when the game's aim bind sits on the other controller |
| `trigger_threshold` | 0.25 | how far the trigger must be pulled. Keep it low |
| `zoom_mult` | 20 | the position cutoff is divided by this while the trigger is held |
| `zoom_mult_rot` | 2 | rotation equivalent |

> Both the cutoff **and** `beta` are divided, so holding the trigger makes the filter uniformly
> heavier — a smooth "slide", not added responsiveness. Keep the results inside the **1.5–4.5 Hz**
> band: anything below ~1 Hz drifts for about a second after you stop. In two-hand mode the
> trigger instead makes the **stock** heavier (`stock_zoom_mult`), see below.

### Two-handed grip
| Key | Default | Meaning |
|---|---|---|
| `twohand` | 1 | enable the feature |
| `twohand_gate` | 1 | 1 = only while the button below is held (recommended), 0 = always on |
| `twohand_button` | grip | which button engages two-hand mode: `grip` (recommended — the index trigger is also the game's pickup button) or `trigger` |
| `twohand_hand` | offhand | which **controller** owns that button: `offhand` (the hand *not* holding the gun — recommended, because on the gun hand the grip is the game's **reload** bind) or `gun`. Follows `gun_hand` automatically. |
| `gun_hand` | left | **which hand holds the gun**: `right` normal, `left` for lefty mode |
| `aim_axis` | -z | which local axis of that hand points down the barrel: `+x -x +y -y +z -z` |
| `twohand_min_dist` | 0.12 | don't engage if the hands are closer than this (metres) |
| `twohand_max_dist` | 0.75 | …or further apart than this |
| `twohand_blend` | 1.0 | 0..1, how hard to snap to the line (1 = fully locked) |

#### Virtual gunstock
The aim line between the hands is low-passed by its **own** filter — this is what makes it feel
like a braced stock instead of a twitchy "two hands glued to one gun":

| Key | Default | Meaning |
|---|---|---|
| `stock_cutoff` | 2.5 | how fast the aim follows the line between the hands (Hz). Lower = the stock leans the aim slowly when you move the main hand. Floored at 1 Hz |
| `stock_beta` | 2.0 | how fast the stock follows a deliberate swing |
| `stock_zoom_mult` | 2.5 | while the trigger is held, `stock_cutoff` is divided by this (1 Hz floor) — the zoom/aim-down-sights feel in two-hand mode |

#### Two-hand hand-filter override (optional)
Two-hand mode uses the **same base/trigger profile** as everything else — the stock line has its
own filter, so no separate hand profile is needed. These keys remain as an optional override;
**0 = fall back to the base profile (recommended)**:

| Key | Default | Meaning |
|---|---|---|
| `twohand_min_cutoff` | 0 | position cutoff override while two-handing (Hz), 0 = use base |
| `twohand_beta` | 0 | one-euro speed term override |
| `twohand_min_cutoff_rot` | 0 | rotation cutoff override, 0 = use base |
| `twohand_beta_rot` | 0 | rotation equivalent |
| `catchup_pos` | 0 | extra stiffness per metre of lag, heavy profiles only (0 = off) |
| `catchup_rot` | 0 | extra stiffness per radian of lag, heavy profiles only (0 = off) |

The trigger still applies while two-handing: it makes the **stock** heavier (`stock_zoom_mult`)
and, if a non-zero override is set, the hand profile as well.

---

## How the smoothing works

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

### Measured, not asserted — `filter_bench.cpp` (offline harness, in this repo)

```
g++ -O2 -std=c++17 -I openvr_proxy filter_bench.cpp -o filter_bench.exe
./filter_bench.exe
```

| profile (rotation, deg) | lag slow | lag fast | float tail | 2° fix t90 | tremor kept |
|---|---|---|---|---|---|
| trigger, old values 0.375 Hz | 5.26 | 13.86 | 1.28 s | 0.96 s | 4.8% |
| trigger, 0.30 Hz + catchup 120 | **0.93** | **1.79** | **0.08 s** | 0.24 s | 10.4% |
| two-hand, old 0.4 Hz | 4.29 | 9.29 | 0.92 s | 0.87 s | 5.2% |
| two-hand, 0.30 Hz + catchup 120 | **0.86** | **1.55** | **0.05 s** | 0.22 s | 10.8% |

Read the `float tail` column: the old values took **1.28 s** to settle inside 0.5° — that is the
drift you felt. The new ones take **0.08 s**.

### The rule that matters: no sub-1 Hz cutoffs

| cutoff | lag it forces | verdict |
|---|---|---|
| 0.05 Hz | 3.18 s | drifts away |
| 0.30 Hz | 0.53 s | only usable *with* catch-up or a high beta |
| 1.5 Hz | 0.11 s | light |
| 3.0 Hz | 0.05 s | the default band |
| 4.5 Hz | 0.035 s | nearly raw |

Keep the cutoffs in the **1.5–4.5 Hz** band and let `beta` do the work. A low cutoff buys
steadiness and pays for it in drift — that trade-off was the whole tuning saga in this project.

### `catchup` — optional, off by default

`catchup_pos` / `catchup_rot` add stiffness proportional to how far behind the pose is, for the
heavy profiles only:

```
cutoff = min_cutoff + beta*|raw velocity| + catchup*|lag|
```

It is a continuous term, so unlike a threshold it cannot toggle while you track a moving enemy.
It is **0 (off)** by default, which is the validated behaviour; the bench's catch-up rows above
show what it buys if you want it.

### The trigger gate (`OVR_SUCCESS`)

`ovr_GetInputState` returns **`>= 0`** on success — `OVR_SUCCESS(result)` is `result >= 0`. An
earlier build tested `== 0`, which **discarded every successful poll** and left the gate dead, so
the trigger profile never engaged. It also now queries `ovrControllerType_Touch` (LTouch|RTouch) in
one call, so both `[left,right]` array slots are populated regardless of how a runtime treats
single-hand queries. `openvr_proxy.log` prints a line on every gate flip with `poll_rc`.

### A note on `dt` (and a wrong turn we took)

`dt` comes from `QueryPerformanceCounter`. The game polls `ovr_GetTrackingState` in bursts from
more than one thread, so a single call's delta is often tiny — the log shows `dt=0.0001`, which is
the `1e-4` clamp floor. That is **not** a bug: consecutive deltas telescope, so their sum over a
second is a second, and the filter's clock stays right. Only sub-0.1 ms deltas are inflated by the
clamp, a small fraction at the ~5.6 ms average interval.

(An intermediate build replaced this with an EMA of the call interval. That was based on a
misreading — "dt=0.0001 means the clock runs 55× slow" — which the jitter meter already
contradicted, since it read 3.3× smoother rather than ~55×. The EMA was reverted; the two forms
are functionally equivalent.)

`beta` stiffens the spring smoothly as the gun picks up speed (no threshold, so nothing to
toggle). Higher `beta` = catches up sooner on a fast sweep, at the cost of some of the weight.

### Do not add a speed/error threshold

The one thing that must not come back is a **threshold that switches the filter on and off** (a
"bubble"). Tracking a moving enemy puts the hand speed right on top of any such threshold, so it
toggles several times a second — and every toggle is a lurch, plus a "boat" feeling on the way in
because the release has to ramp before it does anything. There is no smooth way to switch; only
not to switch.

A single always-on interpolator has no state to flip, so it has no jerk. It also cannot *cancel*
another profile's smoothing the way a release floor does (that is what made "trigger only" feel
unsmoothed).

> Reference values from elsewhere: Meta's Interaction SDK ships its wrist-rotation one-euro
> filter at `minCutoff 2.0 Hz, beta 3.0` — the 0.05 Hz we were using earlier is 40× heavier than
> a production rotation filter.

Set `twohand_min_cutoff_rot=0` to fall back to the normal zoom profile.
**Tuning rule of thumb:** smoother at rest → lower the `min_cutoff`s. Better tracking while
rotating → raise the `beta`s. They are independent, which is the whole point of this filter.

---

## Calibration: if the gun points the wrong way

`aim_axis` is the barrel direction in the controller's own frame, and it can differ per game
version / controller. There is a built-in helper:

1. Set `twohand=1` and `twohand_gate=0` (always on) for the test.
2. Hold the gun with both hands and **point it at a target**.
3. Read the log — roughly every second it prints:
   ```
   twohand: ACTIVE gun_hand=left using axis -z | best-matching axis=+z (0.998) <- if this is not the axis above, change aim_axis
   ```
4. Set `aim_axis` to whatever `best-matching` says.

If `best-matching` is negative (e.g. `-0.5`), flip the sign. Take the reading **while you are
actually aiming at something** — that is when the hand-to-hand line and the barrel agree.

---

## Known limitations

- **Positions are never modified** — by design, so you can slide your hands along the grip.
- **SteamVR launch is broken** for this game (see above); Virtual Desktop is required.
- The first sample after enabling has no filter history, so it starts from the raw pose.
- Per-weapon differences (how a gun sits in the hand) may want a different `aim_axis` or a
  lower `twohand_blend`.
- Built with AI assistance.

## Launcher + config panel (desktop)

`tools\imguipanel\build\bl2vr_panel.exe` is a standalone launcher/patcher (ImGui + Win32 + DX11),
shipped with `openvr_api.dll` beside it.

- **Detect** auto-finds the game from Steam (registry + `libraryfolders.vdf`).
- **Patch game** copies `openvr_api.dll` and writes `bl2vr_grip.ini` into `Binaries\Win64\`.
- **Create desktop shortcut** makes a `BL2VR (enhanced)` shortcut that launches the game.
- Every control has a friendly name + hover description; edits are saved live (the mod
  hot-reloads ~2 s).

Build with `tools\imguipanel\build.bat` (it also bundles `openvr_api.dll` next to the exe).
ImGui (MIT) is vendored under `tools\imguipanel\imgui\`.

## Credits / prior art

Implemented at the Oculus runtime boundary using [MinHook](https://github.com/TsudaKageyu/minhook).
Same class of tool as OpenVR-InputEmulator / gun-stock "motion compensation", but placed at the app
boundary so it does not fight the runtime. I could not find an existing two-handed-grip mod for
this game — that is "I couldn't find one", not a claim to be the first.
