# BL2VR Enhanced

**Two-handed virtual gunstock + motion smoothing for Borderlands 2 VR** — a dependency-free DLL-proxy mod that hooks the game at the runtime boundary. No game files are modified; uninstall is one click.

[![Build](https://github.com/RimachOfficial/BL2VR-Enhanced/actions/workflows/build.yml/badge.svg)](https://github.com/RimachOfficial/BL2VR-Enhanced/actions/workflows/build.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)](#install)
[![Game](https://img.shields.io/badge/game-Borderlands%202%20VR%20(Steam%20991260)-orange)](#install)
[![Built with](https://img.shields.io/badge/built%20with-AI%20pair%20programming-9cf)](docs/THE_STORY.md)

> **The story:** Borderlands 2 VR — a PSVR title (December 2018) ported to PC in 2019 — still
> aims one-handed, with raw controller jitter on every shot. Four implementation routes were
> planned, one runtime assumption turned out wrong, one 55× clock bug hid the fix — and the mod
> shipped anyway. Full narrative with metrics and wrong turns: **[docs/THE_STORY.md](docs/THE_STORY.md)**.
>
> 🎬 **Before/after video: coming with the YouTube release.**

---

## What it changes — honestly

**Compared to the unmodded game:**

| | Unmodded | With the mod |
|---|---|---|
| **Grip** | One-hand aim only | Two-handed virtual gunstock, hold `grip` to engage, hands free to slide |
| **Aim stability** | Raw controller tremor on every shot | One Euro filtering — right-hand rotation per call drops from 0.383° to 0.279° at the Default profile, and by 7.6× with the trigger held on the Locked profile (measured in a real session log, reproducibly via `tools/analyze_log.ps1`) |
| **Zoom/ADS feel** | Nothing changes when aiming | Trigger makes the stock progressively heavier (1 Hz floor) |
| **Install** | — | 2 files + a panel; no game-file edits, reversible |

**One internal trade, stated openly:** the **shipped base profile** (rotation cutoff 3.0 Hz)
removes about **two thirds of an 8 Hz hand tremor** — measured, not estimated:
`tools/tremor_sweep.cpp` reports 34–38% of the tremor still present. The **Steady** profile
(1.0 Hz) leaves ~13%, **Locked** (0.8 Hz) ~10%, while **Responsive** (6.0 Hz) leaves ~60%. The
price is lag: 53 ms at the default, 159 ms at 1.0 Hz. An earlier heavy tuning suppressed tremor
harder still but lagged 13.9° on fast moves and *floated* for 1.28 s after you stopped, which is
why the defaults sit where they do. Pick a profile by feel — the spread is wide on purpose.
Full bench + sweep numbers: [docs/METRICS.md](docs/METRICS.md).

## Features

1. **Virtual gunstock (two-handed grip)** — while you hold the **grip button** (default) the gun
   aims along the **line between your two hands**. Your hands stay completely free to slide back
   and forth along the grip; only the *aim direction* is locked to the line. The line passes
   through **its own low-pass filter**, so a quick movement of the main hand only *leans* the aim
   like a real stock braced against the shoulder — it never snaps. Move the off-hand and the gun
   rotates with it.
2. **Rotation smoothing** — hand poses run through a One Euro filter, so the small tremble that
   ruins long-range shots is removed. **Every system builds on this base**: hands are filtered
   first, and the stock line is derived from the *filtered* hands. It gets much heavier while the
   **index trigger** is held — the trigger also makes the stock heavier (that is the zoom feel),
   in both one-handed and two-hand modes.

Works in **normal and lefty layouts** (see `gun_hand`). All buttons and filter values are configurable.

---

## Install

**Easiest:** download the latest **[Release](https://github.com/RimachOfficial/BL2VR-Enhanced/releases)**
(`BL2VR-Enhanced-vX.Y.Z-win64.zip`, or the three files), and run `bl2vr_panel.exe` —
it finds the game in Steam, patches it, and offers Launch / Remove buttons.

**Manual:**

1. In `...\Borderlands 2 VR\Binaries\Win64\`:
   - copy the game's original `openvr_api.dll` to **`openvr_api_orig.dll`** (keep it — the proxy
     forwards to that name),
   - put the mod's **`openvr_api.dll`** and **`bl2vr_grip.ini`** in the same folder.
2. Launch the game **through Virtual Desktop** (VD → Games).

**Uninstall:** `bl2vr_panel.exe` → **Remove mod** (restores the original DLL and deletes the
mod files). Manually: copy `openvr_api_orig.dll` back over `openvr_api.dll` and delete
`bl2vr_grip.ini`.

## Compatibility — read this first

- **Tested on exactly one setup:** Windows 11 + a standalone headset (Quest class) over
  **Virtual Desktop**, game build 1.0.462.2101248. That is the configuration the mod was built
  and measured on.
- **Untested:** Oculus Link / Rift via the Oculus PC app, any SteamVR headset, other game
  versions. It may work — the hook sits on the Oculus runtime path, so any setup that serves
  the game a LibOVR runtime is plausible — but nobody has verified it. Reports welcome
  ([issues](https://github.com/RimachOfficial/BL2VR-Enhanced/issues)).
- **SteamVR launch crashes on the tested setup** (pre-existing, reproducible with the untouched
  original DLL — the crash happens before any mod code runs). Virtual Desktop serves the game
  its Oculus (LibOVR) runtime, which is the path the mod hooks.
- **Antivirus / SmartScreen:** `openvr_api.dll` and `bl2vr_panel.exe` are **unsigned** —
  SmartScreen will say "unknown publisher" and some AVs flag a DLL that sits next to a game exe
  and hooks a VR runtime. That is expected for a mod of this kind; nothing phones home, and you
  can audit or rebuild everything from this repo (see [Building from source](#building-from-source)).
  Hashes of release files are in the release notes if you want to verify what you downloaded.

## How it works (short version)

- On this setup BL2VR does **not** run on OpenVR — it runs on **Virtual Desktop's Oculus runtime**
  (`VirtualDesktop.LibOVRRT64_1.dll`). Verified from the loaded-module list in-game.
- The game **always** loads `openvr_api.dll` from its own folder, so that file is our way in: the
  proxy DLL forwards every OpenVR export to the original and hosts the payload.
- A worker thread waits for VD's Oculus runtime to appear, then installs a **MinHook** detour on
  **`ovr_GetTrackingState`**, the call the game uses to read controller poses.
- Each call: both hand poses run through the filter **first**, then the virtual gunstock solve
  runs on the *filtered* hands — smoothing is the base of everything.
- All struct layouts were taken from the generated Oculus SDK bindings (not guessed) and then
  **confirmed against a raw dump of the live tracking state** on real hardware.

Log file: `Binaries\Win64\openvr_proxy.log` (or `%LOCALAPPDATA%\BL2VR-grip\openvr_proxy.log`).
Full pipeline with diagrams: **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)**.

## How the two-hand solve works (virtual gunstock)

```
aim_line = normalize(filtered_offhand_pos - filtered_gunhand_pos)
aim_line = OneEuro_lowpass(aim_line, stock_cutoff, stock_beta)   <- the stock's inertia
gun orientation = rotate(barrel_axis -> aim_line) * gun orientation
```

The hands are filtered first (base/trigger profile), the line between them is then smoothed once
more by its **own** One Euro filter, and only that smoothed line drives the gun. Because it is a
delta on top of the filtered pose, the game's own per-weapon "how the gun sits in the hand" offset
is preserved. **The stock solve never moves positions** — only the aim orientation — so your hands
stay free to slide along the grip, and
a quick main-hand movement only leans the aim instead of throwing it.

---

## Launcher + config panel (desktop)

`bl2vr_panel.exe` (from the Release) is a standalone launcher/patcher (ImGui + Win32 + DX11).

- **Profiles** page: one-click feel presets — **Default** (shipped values), **Steady** (heavy
  resistance, sniping), **Hybrid** (balanced middle), **Responsive** (light, near-raw), **Sniper**
  (normal hip, extreme weight while aiming/braced). Hand-tuned values are auto-detected and shown
  as **Custom**. Profiles only touch feel keys — hardware setup (gun hand, buttons, axis,
  distances) is never changed.
- **Detect** auto-finds the game from Steam; **Patch game** backs up the original
  `openvr_api.dll` (first patch only) and installs the mod; **Remove mod** restores the stock
  game; **Launch game** starts the exe.
- Every control has a friendly name + hover description; edits are saved live (the mod
  hot-reloads ~2 s).

---

## Config — `bl2vr_grip.ini`

**Re-read every 2 seconds while the game runs** — edit mid-session, no restart. The panel edits
all of this with tooltips; the raw keys if you want them:

### Smoothing
| Key | Default | Meaning |
|---|---|---|
| `enabled` | 1 | master switch |
| `smooth_hands` | 1 | smooth both hand poses |
| `smooth_head` | 0 | smooth the head pose — usually bad (view lag) |
| `min_cutoff` | 4.5 | position cutoff (Hz): lower = steadier at rest, laggier |
| `beta` | 2.0 | how fast the cutoff rises with the RAW speed |
| `derivative_cutoff` | 6.0 | smooths the speed estimate — high so movement onset reacts instantly |
| `min_cutoff_rot` | 3.0 | rotation cutoff (Hz) — the knob that matters for aiming |
| `beta_rot` | 1.5 | rotation equivalent of `beta` |
| `predict_ms` | 0 | extra lookahead — keep 0 (prediction is the one thing that can overshoot) |
| `per_thread_filters` | 1 | one filter state per calling thread — **leave on**; the game interleaves two threads with offset prediction times ([why](docs/METRICS.md#1c-why-the-smoothing-seemed-weak-mixed-prediction-timelines-measured)) |

### Zoom (trigger) gate
| Key | Default | Meaning |
|---|---|---|
| `trigger_gate` | 1 | 0 = ignore trigger, 1 = stronger while held, 2 = smooth **only** while held |
| `zoom_hand` | either | which trigger drives the gate: `gun` / `offhand` / `either` |
| `trigger_threshold` | 0.25 | how far the trigger must be pulled — keep it low |
| `zoom_mult` | 2.0 | position cutoff divided by this while held |
| `zoom_mult_rot` | 1.5 | rotation equivalent |

### Two-handed grip
| Key | Default | Meaning |
|---|---|---|
| `twohand` | 1 | enable |
| `twohand_gate` | 1 | 1 = only while the button is held (recommended) |
| `twohand_button` | grip | `grip` (recommended — the trigger is also the pickup bind) or `trigger` |
| `twohand_hand` | offhand | which controller owns that button (on the gun hand, grip = **reload**) |
| `gun_hand` | left | which hand holds the gun; `left` = lefty mode |
| `aim_axis` | -z | which local axis of that hand points down the barrel |
| `twohand_min_dist` | 0.12 | don't engage if the hands are closer (metres) |
| `twohand_max_dist` | 0.75 | …or further apart |
| `twohand_blend` | 1.0 | 0..1, how hard to snap to the line |

### Virtual gunstock
| Key | Default | Meaning |
|---|---|---|
| `stock_cutoff` | 2.5 | how fast the aim follows the hand-to-hand line (Hz); floored at 1 Hz |
| `stock_beta` | 2.0 | how fast the stock follows a deliberate swing |
| `stock_zoom_mult` | 2.5 | while the trigger is held, `stock_cutoff` is divided by this |

Two-hand filter overrides and `catchup_*` keys: see [docs/TUNING.md](docs/TUNING.md).
What the filter maths is and what each knob trades: **[docs/TUNING.md](docs/TUNING.md)**.

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
4. Set `aim_axis` to whatever `best-matching` says (flip the sign if it's negative).

---

## Building from source

| Component | Toolchain | Command |
|---|---|---|
| Mod DLL (`openvr_api.dll`) | MSVC 2022+ (VS 18 tested), x64 | `openvr_proxy\build.bat` |
| Config panel (`bl2vr_panel.exe`) | MSVC, x64 | `tools\imguipanel\build.bat` |
| Offline filter bench | any C++17 compiler (g++/MSVC) | see [docs/METRICS.md](docs/METRICS.md) |
| Tremor sweep (`tools/tremor_sweep.cpp`) | any C++17 compiler | `g++ -O2 -std=c++17 -I openvr_proxy tools/tremor_sweep.cpp -o tremor_sweep` |
| Catch-up demo (`tools/catchup_demo.cpp`) | any C++17 compiler | `cl /O2 /EHsc /std:c++17 tools\catchup_demo.cpp` |
| Log analysis (`tools/analyze_log.ps1`) | PowerShell (no build) | `powershell -File tools\analyze_log.ps1` |
| Unit tests (`tests\`) | MSVC, x64 | `tests\run_tests.bat` |

MinHook is vendored under `third_party\minhook-master\` (BSD-3-Clause); ImGui (MIT) under
`tools\imguipanel\imgui\`. No package manager needed. `tools\ovr_proxy_legacy\` is the abandoned
first attempt at an Oculus-runtime proxy, kept as reference only.
Verification steps after a build: [docs/TESTING.md](docs/TESTING.md).

## Known limitations

- **The stock solve never moves your hands** — only the aim rotation — so hands slide freely along
  the grip. (The hand filter *does* smooth positions; that is a separate stage and is what removes
  positional tremor.)
- **SteamVR launch crashes on the tested setup** (see [Compatibility](#compatibility--read-this-first));
  Virtual Desktop is the supported path.
- **Untested on Link/Rift and SteamVR setups** — see [Compatibility](#compatibility--read-this-first).
- The first sample after enabling has no filter history, so it starts from the raw pose.
- Per-weapon differences (how a gun sits in the hand) may want a different `aim_axis` or a
  lower `twohand_blend`.
- Built with AI assistance (see [THE_STORY](docs/THE_STORY.md)).

---

## Documentation

| Document | What it is |
|---|---|
| [docs/THE_STORY.md](docs/THE_STORY.md) | The full problem → action → result narrative (the YouTube story) |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the proxy + hook pipeline works, with diagrams |
| [docs/TUNING.md](docs/TUNING.md) | Filter maths, every knob's tradeoff, the tuning rules |
| [docs/METRICS.md](docs/METRICS.md) | Every measurement, methodology and raw log evidence |
| [docs/DEVLOG.md](docs/DEVLOG.md) | The raw session-by-session development log |
| [docs/PLAN.md](docs/PLAN.md) | Original implementation plan + field record |
| [docs/TESTING.md](docs/TESTING.md) | How to verify a build works |
| [CHANGELOG.md](CHANGELOG.md) | Release history |

---

## Credits / prior art

- Filtering: **One Euro filter** — Casiez, Roussel & Vogel, CHI 2012.
- Hooking: [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-3-Clause), vendored.
- UI: [Dear ImGui](https://github.com/ocornut/imgui) (MIT), vendored.
- Same class of tool as OpenVR-InputEmulator / gun-stock "motion compensation", but placed at the
  app boundary so it does not fight the runtime. I could not find an existing two-handed-grip mod
  for this game — that is "I couldn't find one", not a claim to be the first.

## Legal

- GPL-3.0 — free to use, share and improve; derivative mods must stay open source ([LICENSE](LICENSE)).
- This is a fan project, not affiliated with or endorsed by Gearbox Software, 2K, or Meta.
- **No game files, Valve/Meta/Facebook binaries or decompiled code are distributed in this
  repository.** You must own Borderlands 2 VR; the mod only adds two files next to the game exe.



