# Borderlands 2 VR — Two-Handed Grip + Aim Smoothing

A DLL-proxy mod for **Borderlands 2 VR** (Steam AppID 991260, version 1.0.462.2101248).

Two features, both driven by your **right index trigger** (the zoom/aim bind):

1. **Two-handed grip** — while the trigger is held, the gun aims along the **line between your
   two hands**. Your hands stay completely free to slide back and forth along the grip; only the
   *aim direction* is locked to the line. Move the off-hand and the gun rotates with it.
2. **Rotation smoothing** — your hand poses are run through a One Euro filter, so the small
   tremble that ruins long-range shots is removed. It gets much heavier while zoomed (that is
   the setting that matters for snipers).

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
- Each call: optionally apply the two-hand solve, then run both hand poses through the filter.
- All struct layouts were taken from the generated Oculus SDK bindings (not guessed) and then
  **confirmed against a raw dump of the live tracking state** on real hardware.
- A built-in **jitter meter** prints how much the right-hand rotation actually moved per call,
  raw vs filtered, so improvements are measured rather than guessed.

Log file: `Binaries\Win64\openvr_proxy.log` (or `%LOCALAPPDATA%\BL2VR-grip\openvr_proxy.log`).

---

## How the two-hand solve works

```
forward = normalize(offhand_position - gunhand_position)
```

The gun hand's orientation gets a **delta rotation** that points its barrel axis along that line.
Because it is a delta on top of the raw pose, the game's own per-weapon "how the gun sits in the
hand" offset is preserved. **Positions are never modified.**

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
| `min_cutoff` | 5.0 | position: lower = steadier at rest, laggier |
| `beta` | 2.0 | position: higher = responds sooner when you move |
| `min_cutoff_rot` | 3.0 | rotation: same idea, for rotation |
| `beta_rot` | 1.2 | rotation equivalent of `beta` |
| `predict_ms` | 0 | extra motion prediction (0 = off) |

### Zoom (trigger) gate
| Key | Default | Meaning |
|---|---|---|
| `trigger_gate` | 1 | 0 = ignore trigger, 1 = stronger while held, 2 = smooth **only** while held |
| `trigger_threshold` | 0.5 | how far the trigger must be pulled |
| `zoom_mult` | 20.0 | position is this many times smoother while zoomed |
| `zoom_mult_rot` | 60.0 | rotation equivalent (3× the position value) |

> Both the cutoff **and** `beta` are divided by these, so zooming makes the filter uniformly
> heavier — a smooth "slide", not added responsiveness.

### Two-handed grip
| Key | Default | Meaning |
|---|---|---|
| `twohand` | 1 | enable the feature |
| `twohand_gate` | 1 | 1 = only while the trigger is held (recommended), 0 = always on |
| `gun_hand` | right | **which hand holds the gun**: `right` normal, `left` for lefty mode |
| `aim_axis` | -z | which local axis of that hand points down the barrel: `+x -x +y -y +z -z` |
| `twohand_min_dist` | 0.12 | don't engage if the hands are closer than this (metres) |
| `twohand_max_dist` | 0.75 | …or further apart than this |
| `twohand_blend` | 1.0 | 0..1, how hard to snap to the line (1 = fully locked) |

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

## Credits / prior art

Implemented at the Oculus runtime boundary using [MinHook](https://github.com/TsudaKageyu/minhook).
Same class of tool as OpenVR-InputEmulator / gun-stock "motion compensation", but placed at the app
boundary so it does not fight the runtime. I could not find an existing two-handed-grip mod for
this game — that is "I couldn't find one", not a claim to be the first.
