# Testing — verify a build works

No unit-test suite in the game-loop sense (it hooks a shipping game); verification is layered.

## 1. Offline (no game, no headset)

```
g++ -O2 -std=c++17 -I openvr_proxy tools/filter_bench.cpp -o filter_bench.exe
./filter_bench.exe
```

Runs the real `filters.h` and prints the lag / tail / tremor table. Numbers must be in the same
range as [METRICS.md](METRICS.md); a regression here means the filter maths broke.

## 2. Trigger-gate smoke test (in game, 30 seconds)

1. Install the DLL (`openvr_proxy\install.ps1` or the panel).
2. Launch through **Virtual Desktop**.
3. Hold **only the gun-hand trigger** for a few seconds, release it.
4. Open `Binaries\Win64\openvr_proxy.log` (or `%LOCALAPPDATA%\BL2VR-grip\openvr_proxy.log`) and
   find:
   ```
   trigger gate -> HELD ...    /    trigger gate -> released ...
   ```
5. **Fail conditions:** no such line, `trig=0.000`, or `poll_rc` not ≥ 0 — the trigger is not
   being read (this was a real shipped bug: `OVR_SUCCESS` tested as `== 0` instead of `>= 0`).

## 3. Two-hand aim check

1. Set `twohand=1`, `twohand_gate=0` (always on).
2. Hold the gun with both hands, point at a target.
3. Log once per second:
   ```
   twohand: ACTIVE gun_hand=left using axis -z | best-matching axis=+z (0.998)
   ```
4. `best-matching` close to ±1 = axis is right. Negative = flip the sign in `aim_axis`.

## 4. Feel test (the only test that ultimately counts)

- One-handed: tremor on long shots should drop; big snap moves must not leave a long float tail
  (old bug: 1.28 s; fixed target: < 0.1 s).
- Two-handed: gun leans with the main hand like a braced stock, rotates with the off-hand,
  trigger makes the stock heavier.
- No lurches while tracking a moving enemy (any threshold-toggle regression shows up here first).

## Applying source changes quickly

1. Edit `openvr_proxy/src/filters.h` / `ovrhook.cpp`.
2. Rebuild: `openvr_proxy\build.bat` → `build\openvr_api.dll`.
3. Install: `openvr_proxy\install.ps1` (backs up the original once).
4. Copy the updated `bl2vr_grip.ini` next to the DLL if defaults changed (hot-reloads every 2 s).
5. Repeat steps 2–4 of this file.

`catchup_pos=0` and `catchup_rot=0` give exactly the pre-catchup behaviour — useful as a baseline
when A/B testing any filter change.
