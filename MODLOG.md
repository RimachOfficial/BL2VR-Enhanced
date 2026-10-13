# MODLOG — BL2VR two-handed grip (Route A: openvr_api.dll proxy)

## 2026-10-12 — session 1

### Install facts (verified on this PC)
- Game: `C:\Program Files (x86)\Steam\steamapps\common\Borderlands 2 VR` (Steam 991260)
- Binaries: `Binaries\Win64`
- `openvr_api.dll` original: 313,664 bytes,
  SHA256 `D793E2A76A61296DC5BCE5E6B8DC32F4F3096743ABA10C5BAC2EB465E635850C`
- Original DLL build date: 2016-07-05 (openvr SDK mid-2016)
- Toolchain: VS 2026 Community (MSVC 14.51.36231), CMake 4.4.4, x64

### Backups — restore command
```
Copy-Item "<game>\openvr_api.dll.bak" "<game>\openvr_api.dll" -Force
```
- `Binaries\Win64\openvr_api.dll.bak`
- `backup\openvr_api.dll.ORIGINAL`
- `backup\Startup.upk.ORIGINAL`   (mod 571 attempt, reverted)
- `backup\Config_20261012_2230\`

### openvr_api.dll exports (14, ordinals 1..14)
VRControlPanel, VRDashboardManager, VRTrackedCameraInternal, VR_GetGenericInterface,
VR_GetInitToken, VR_GetStringForHmdError, VR_GetVRInitErrorAsEnglishDescription,
VR_GetVRInitErrorAsSymbol, VR_InitInternal, VR_IsHmdPresent, VR_IsInterfaceVersionValid,
VR_IsRuntimeInstalled, VR_RuntimePath, VR_ShutdownInternal

### What Borderlands2VR.exe actually uses
- static imports, by NAME: `VR_InitInternal`, `VR_GetInitToken`, `VR_IsInterfaceVersionValid`,
  `VR_ShutdownInternal`
- dllbind (UTF-16 name string in the exe): `VR_GetGenericInterface`, `VR_IsHmdPresent`
- the other 8 exports: NOT referenced by the exe

### Interfaces the game requests (ASCII strings inside the exe)
`IVRSystem_012`, `IVRCompositor_016`, `IVROverlay_013`, `IVRChaperone_003`,
`IVRChaperoneSetup_005`, `IVRApplications_006`, `IVRSettings_001`, `IVRRenderModels_005`,
`IVRTrackedCamera_003`, `IVRExtendedDisplay_001`, `IVRResources_001`, `IVRScreenshots_001`

=> **NO `IVRInput`** — the game does NOT read poses through SteamVR Input.
   Route A only needs to hook the **IVRSystem_012** and **IVRCompositor_016** vtables.
   (This was the plan's biggest open question; answered in one evening.)

### Gotchas
- MSVC `.def` forwarders (`EXPORTS x=other.x`) do NOT work — the linker reads it as a
  local alias and fails with LNK2001. Used explicit C++ wrappers + `__declspec(dllexport)`.
- x64 MSVC has no inline `__asm`, so naked jump-trampolines are out; explicit wrappers instead.

### Installed now
- `Binaries\Win64\openvr_api.dll`      = our proxy (151,552 bytes)
- `Binaries\Win64\openvr_api_orig.dll` = original loader (313,664 bytes)
- Log: `Binaries\Win64\openvr_proxy.log` (fallback `%LOCALAPPDATA%\BL2VR-grip\openvr_proxy.log`)

### Next
- **Step B** — hook the `IVRSystem_012` vtable, log `GetDeviceToAbsoluteTrackingPose`
  and `GetControllerStateWithPose` (confirm the gun's pose source).
- **Step C** — rewrite the right controller pose to the two-hand line.

### Session 1b — first in-game test (23:16–23:18)
- Proxy loaded and unloaded (log has only the attach + detach lines).
- **No openvr export was called at all.** Game ran fine (config written 23:17–23:18),
  so VR init *should* have called `VR_InitInternal`.
- Exe does import `openvr_api.dll` as a normal (load-time) import — confirmed with dumpbin.
- Added diagnostics: log the host exe path + enumerate every loaded module after 8 s,
  so we can see whether SteamVR's own `openvr_api.dll` is loaded alongside ours.
- Next: re-run the game with the diagnostic build, read the module list.

### Session 1c — diagnostic test (23:24–23:25)
- Host = `Borderlands2VR.exe`; our `openvr_api.dll` loaded (base `7DB00000`). **Still no openvr calls.**
- Module list (124 modules) contains **no SteamVR DLL** (no `vrclient*`, no SteamVR `openvr_api.dll`)
  and **no Oculus DLL**. Only game DLLs, `steam_api64`/`steamclient64`, Steam overlay, and
  `C:\Program Files\Virtual Desktop Streamer\VirtualDesktop.Injector64.dll`
  (a DXGI hook — checked: no openvr/openxr/hook strings).
- => in that session the game never talked to a VR runtime. It almost certainly ran **flat (desktop)**.
- **Standalone test `test_host.exe` PROVES the proxy works**: `LoadLibraryA` ok,
  `VR_GetInitToken`->0, `VR_IsHmdPresent`->1, `VR_IsInterfaceVersionValid("IVRSystem_012")`->0,
  every call logged, `openvr_api_orig.dll` loaded. (`VR_IsHmdPresent`=1 => SteamVR + HMD were up.)
- Next: confirm with Rimach whether the game actually ran in the headset.

### Session 1d — the real cause (23:24 test + VD recon)
Rimach: he WAS in VR, it never ran flat. He suspected it ran through VD, not SteamVR. He is right.
- `HKLM\SOFTWARE\Khronos\OpenXR\1` → `ActiveRuntime = C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json`
  => **VDXR is the active OpenXR runtime** (not SteamVR).
- `%APPDATA%\Virtual Desktop\GameSettings.json` → app 991260: `OpenVRSupport: true`, `LaunchType: "vr"`.
  => VD launches BL2VR as an OpenVR app **through Virtual Desktop**.
- BL2VR exe strings: `LibOVR`(1 ascii/2 utf16), `LibOVRRT`(utf16), **72 `ovr_*`**, `Oculus`(23 ascii) — the game
  supports **both** Oculus (LibOVR) and OpenVR (SteamVR), and switches platform (`ProteaSeqCond_SwitchVRPlatform`).
- VD ships: `VirtualDesktop.Injector64.dll`, `VirtualDesktop.LibOVRRT64_1.dll`, `OpenXR\virtualdesktop-openxr.dll`,
  `openxr-oculus-compatibility.dll`, `OpenVRDriver\`.
- **Conclusion:** the game's VR is provided by VD (its injector / VDXR translation), not SteamVR, so our
  `openvr_api.dll` proxy is loaded but bypassed. Route A (OpenVR proxy) only works if the game uses real SteamVR.
- Diagnostic change: module snapshot now runs at 8 s **and** 50 s (to catch VR DLLs loaded after the menu).
- Next: re-test; decide between (a) forcing the game onto SteamVR, or (b) pivoting to the Oculus/VD path.

### Session 1e — SteamVR launch + VD internals
- Launching from **Steam/SteamVR** => game dies instantly: `Fatal error! Address = 0x0`.
  Our proxy attached but was NOT called before the crash, so the crash is not ours — it is the
  Steam/SteamVR launch path (VD, not SteamVR, is the headset provider on this PC).
- `VirtualDesktop.Injector64.dll` strings: only `CreateDXGIFactory`, `CreateDXGIFactory1`, `dxgi.dll`,
  `vrdesktop.net`, `IsDebuggerPresent` => **it is purely a DXGI hook** (rendering). It does NOT touch openvr.
- `VirtualDesktop\OpenVRDriver\resources\settings\default.vrsettings` => `driver_VirtualDesktop`,
  `loadPriority 1000`, controller/finger emulation settings => **VD's OpenVR driver feeds VD's headset +
  Quest controllers INTO SteamVR**. So SteamVR is meant to be the runtime; the 8 s snapshot was likely too early.
- `OpenXR\virtualdesktop-openxr.json` => VDXR ("VirtualDesktopXR (Bundled)").
  `openxr-oculus-compatibility.json` => an OpenXR API layer for the Unity "OculusXR Plugin" only.
- Next: re-run the **working VD launch** with the 8 s + 50 s module snapshots to see if SteamVR loads late.

### Session 1f — ROOT CAUSE CONFIRMED (50 s module snapshot)
The 50 s snapshot (new modules since 8 s) contains exactly one VR DLL:

    C:\Program Files\Virtual Desktop Streamer\VirtualDesktop.LibOVRRT64_1.dll

=> **The game runs in Oculus (LibOVR) mode, served by Virtual Desktop's fake Oculus runtime.**
   It never uses OpenVR, which is why our `openvr_api.dll` proxy is loaded but never called.
   The game was still in VR the whole time (Rimach confirmed).
- VD ships `VirtualDesktop.LibOVRRT64_1.dll` (409,624 B) and `VirtualDesktop.LibOVRRT32_1.dll`.
  There is **no** plain `LibOVRRT64_1.dll` anywhere and **no** Oculus registry key, so VD must redirect the
  game's `LoadLibrary("LibOVRRT64_1.dll")` to its own file (VD's injector imports `LoadLibraryW`,
  `LoadLibraryExW`, `GetProcAddress`, `FreeLibrary`; its pdb path is
  `D:\VirtualDesktop\...\VirtualDesktop.Injector\bin\x64\Release\VirtualDesktop.Injector64.pdb`).
- BL2VR supports Oculus / Vive(SteamVR) / PSVR (many `MenuInputMapArray=...,Set="Oculus"/"Vive"/"PSVR"`).
  `WillowEngine.ini` has an `[Oculus.Settings]` section.
- **Consequence for the plan:** Route A must be re-pointed from `openvr_api.dll` (OpenVR) to the Oculus path —
  i.e. hook `ovr_GetTrackingState` (right-hand pose lives in `ovrTrackingState::HandPoses[ovrHand_Right]`),
  either by proxying `LibOVRRT64_1.dll` or by hooking VD's DLL from inside the game.
- Options: (A) hook the Oculus path; (B) force the game back to OpenVR so the existing proxy works.

### Session 1g — Oculus path confirmed; probe + forwarder results
- VD's `VirtualDesktop.LibOVRRT64_1.dll` = **128 exports**, includes `ovr_GetTrackingState` (#85),
  `ovr_GetTrackingStateWithSensorData` (#86), `ovr_GetInputState` (#57), `ovr_GetDevicePoses` (#43),
  `ovr_Initialize` (#92), `ovr_Create` (#10). Full Oculus runtime.
- Probe test: dropped a dummy `LibOVRRT64_1.dll` (no exports) into `Binaries\Win64`. The game then opened
  **FLATSCREEN** instead of VR => the folder file IS being used (Oculus init failed -> game fell back to desktop).
  The probe's log file was empty though, so the write failed; probe now logs to 3 paths + OutputDebugString.
  NOTE: if Oculus fails the game goes **flat**, it does NOT fall back to SteamVR — so "force OpenVR" (Path B) is out.
- MSVC `.def` forwarders re-tested and **confirmed broken** (`LNK2001: unresolved external symbol`).
  => forward the 128 `ovr_*` exports with MASM x64 jump-stubs (`.asm` + ml64), which pass every register through.
- Plan: `LibOVRRT64_1.dll` proxy in `Binaries\Win64` -> forwards all 128 to VD's DLL -> hooks
  `ovr_GetTrackingState` -> rewrite `HandPoses[ovrHand_Right]` (index 1) to the two-hand line.

### Session 1h — note from Rimach
- While the game was open in **flat mode**, Rimach noticed that **correct gun animations exist** in the game.
  (Follow up later — could mean the two-handed/aim animations are present in the game's data.)

### Session 1l — Oculus proxy built (logging build)
- New project: `BL2VR-grip\ovr_proxy\`
  - `gen_stubs.ps1` — generates everything below from `..\ovr_exports.txt` (128 names)
  - `stubs.asm` — 128 MASM x64 jump stubs (`jmp qword ptr [g_<name>]`), passes all registers through
  - `stubs.h` / `resolve.inl` / `proxy.def` — generated
  - `proxy.cpp` — loads VD's runtime, resolves all 128, hooks `ovr_GetTrackingState`, logs poses
  - `build.bat` — ml64 + cl, x64
- **Build OK**: `build\LibOVRRT64_1.dll`, export set verified **identical** to
  `VirtualDesktop.LibOVRRT64_1.dll` (128/128 names, x64).
- Key insight: MSVC `.def` *exports* of real symbols work fine — only `.def` *forwarders*
  (`name=other.name`) fail. So MASM stubs + a plain export list is the working pattern.
- Oculus structs used (static_assert-checked): `ovrPosef`=28, `ovrPoseStatef`=96, `ovrTrackingState`=328.
- Installed to `Binaries\Win64\LibOVRRT64_1.dll`; log goes to `ovr_proxy.log` next to it.
- Purpose of this build: confirm the hook is reached and dump real pose values (no rewriting yet).
Rimach asked: can the SteamVR version work at all, and did I break something?
- **Game folder restored to pristine**: `openvr_api.dll` = original (SHA256 `D793E2A7...`, 313,664 B),
  `openvr_api_orig.dll` removed, `LibOVRRT64_1.dll` removed, logs removed. Nothing of ours remains.
- Result of the clean SteamVR test (SteamVR fully up, game launched FROM SteamVR):
  **still the `Fatal error! Address = 0x0` crash at VR init.**
  - `Launch.log` stops right after `161294 objects as part of root set at end of initial load`
    (same point as every other crash) — never reaches the menus.
  - SteamVR's own log: `11:34:40.705 ClearAppTransition: Reason: Abort. Was LaunchingApp_ExternalLaunch for 'steam.app.991260'`
    => SteamVR tried to launch the game and the transition was aborted (= game died during VR init).
  - SteamVR 2.17.10; HMD driver `oculus_virtualdesktop`; `steamvr.vrsettings` has no BL2VR overrides.
- **Conclusion:** BL2VR + SteamVR is broken on this PC, independent of our work. The game folder is original,
  and the crash reproduces with the untouched original `openvr_api.dll`.
- **Therefore: do NOT use the SteamVR path.** Launch via Virtual Desktop (as always) — that works.
  That is also the path we PROVED we can intercept (folder-local `LibOVRRT64_1.dll`).
- Consequence: the OpenVR/`openvr_api.dll` proxy (kept, working, tested) is now a dead end for this setup;
  the Oculus `LibOVRRT64_1.dll` proxy is the route.
- Removed our dummy `LibOVRRT64_1.dll` from `Binaries\Win64` and relaunched (VD connected):
  **VR came back immediately.**
- => With the dummy present the game launched **flat**; with it absent the game launches **in VR**.
  Therefore **the game loads `LibOVRRT64_1.dll` from its own folder** (its Oculus loader prefers the app dir),
  and a missing/incomplete one makes Oculus init fail so the game falls back to desktop.
  (Our dummy's `DllMain` never logged, so the loader probably validates it before use — the real proxy
  must export the full 128-function set.)
- **This unlocks the whole route:** we can proxy the Oculus runtime straight from the game folder, no injection.

### Session 1l — the smoothing build (Oculus proxy)
Rimach's new goal: not two-hand grip first, but **stop the hands feeling jittery/janky**
→ smooth the controller poses. Same hook point, so ~all of the work is shared.

- Kept the working stub machinery in `ovr_proxy\` (`gen_stubs.ps1` → `stubs.asm`,
  `stubs.h`, `resolve.inl`, `proxy.def`; `build.bat`).
- **Fixed a real bug in the existing logging draft:** `ovrTrackingState` was declared as the
  old SDK layout (328 B, no `TimeInSeconds`/`StatusFlags`). The SDK 1.x struct is **344 B**:
  `double TimeInSeconds; ovrPoseStatef HeadPose; uint StatusFlags; ovrPoseStatef HandPoses[2];
  uint HandStatusFlags[2]; ovrPosef CalibratedOrigin;` — offsets 0 / 8 / 104 / 112 / 304 / 312.
  A too-small struct here means the real runtime **writes past our stack buffer**.
  Now enforced with `static_assert`s (28 / 96 / 344 + offsets).
- Added `filters.h`: **One Euro filter** (Casiez 2012) for vec3 position and for quaternion
  rotation (slerp with an adaptive alpha). Adaptive = heavy smoothing when still, responsive
  when moving — the whole point vs a fixed low-pass.
- `proxy.cpp` now: config from `ovr_proxy.ini` (hot-reloaded every 2 s, so it can be tuned
  while the game runs), CSV-ish logging of the first N frames, then the filter.
- Build: `ovr_proxy\build\LibOVRRT64_1.dll` (198,144 B, x64, 128 exports) — verified with dumpbin.
- Installed to `Binaries\Win64\` together with `ovr_proxy.ini`.
  **Restore = delete `LibOVRRT64_1.dll` + `ovr_proxy.ini` from `Binaries\Win64`.**

### Session 1m — OpenXR default runtime check (Rimach asked if it's SteamVR)
**It is not.** Active OpenXR runtime:
- HKLM 64-bit `HKCU/HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime` =
  `C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json`  (**VDXR**)
- HKLM 32-bit (WOW6432Node) = `...\virtualdesktop-openxr-32.json` (VDXR)
- HKCU: not set. SteamVR's own `C:\Program Files (x86)\Steam\config\openxr.json` exists but is **not** active.
- Nothing VR running at check time (no vrserver/vrmonitor/vrcompositor).
- This does not affect BL2VR: the exe has **0** OpenXR references — it uses the **Oculus SDK (LibOVR)**.
  VD's Oculus path (`VirtualDesktop.LibOVRRT64_1.dll`) is what serves the game.
- (For the record, SteamVR can be made the OpenXR runtime from SteamVR → Settings → Developer →
  "Set SteamVR as OpenXR runtime", but that is irrelevant here: BL2VR doesn't use OpenXR, and
  BL2VR + SteamVR crashes on this PC anyway.)

### Session 1s — TWO-HANDED GRIP implemented + README
Rimach's spec: hold the zoom trigger -> the gun aims along the **1:1 line between the hands**;
hands stay free to slide along the grip; the off-hand drives the gun's rotation; must work in
**normal and lefty** layouts (his is lefty -> gun in LEFT hand, off-hand = right).

Implementation (`ovrhook.cpp`):
- maths added: `QuatMul`, `QuatRotateVec`, `QuatFromTo`, `SlerpQ`, `QuatNormalizeF`.
- `ApplyTwoHand()`: `forward = normalize(offhand_pos - gunhand_pos)`; the gun hand's orientation
  gets a **delta** rotation (`qNew = delta * qGun`) so its barrel axis lands on that line.
  **Positions are never touched** — that is what keeps the hands free to slide (as requested).
- Gated by the same trigger as zoom; requires both hands tracked and the hand-to-hand distance
  inside `twohand_min_dist`..`twohand_max_dist`.
- `twohand_blend` (0..1) slerps between untouched and fully-locked for a softer snap.
- Runs **before** the One Euro filter, so two-hand aim is also smoothed.
- New config: `twohand`, `twohand_gate`, `gun_hand` (left|right), `aim_axis` (+x..-z),
  `twohand_min_dist`, `twohand_max_dist`, `twohand_blend`.
- **Axis calibration helper**: every ~1 s while active the log prints which hand-local axis best
  matches the hand-to-hand line, so `aim_axis` can be set from data instead of guessing.
  (Hold the gun on a target while reading it.)
- Built + installed (220,160 B); his ini set to `gun_hand=left`.
- **README.md written** — what the mod does, how it works, install/uninstall, full config
  reference, the axis-calibration procedure, and the known limitations.
Rimach: "3x the rotation only, in an additive way" (my previous build had boosted `beta`, which
made it MORE responsive — wrong direction; reverted so cutoff AND beta are both divided).
Final: `zoom_mult=20` (position), `zoom_mult_rot=60` (rotation, 3x the 20 he liked).

**Measured (jitter meter, right-hand rotation per call):**
| raw deg/call | filtered | improvement |
|---|---|---|
| 0.5948 | 0.2298 | 2.6x |
| 0.6000+ | 0.2074-0.2327 | 2.7-2.9x |
| 1.0243 | 0.4901 | 2.1x |
=> ~2.5-3x attenuation, matching the request. Gate confirmed live in the log:
`trig=1.00 x20/60 on=1` while the trigger was held. `ovr_GetInputState` resolved ok and the
input dump reads sane zeros.

**Two bugs of mine found and fixed:**
1. **Jitter meter precision.** `2*acos(dot)` is numerically dead for tiny angles, so at rest it
   reported `raw 0.0000 -> filtered 0.0115` (phantom motion). Replaced with the stable
   `2*atan2(|vec|,|w|)` of the relative quaternion.
2. **Filter state was not thread-safe.** The game calls the hook ~1.8x/frame; concurrent calls
   would fight over the same quaternion state. Now wrapped in a `CRITICAL_SECTION` (`g_fcs`),
   initialised in `InstallOvrHookThread`.
- Rebuilt + installed (213,504 B).
The trigger-gate build made the game **open in VR then close silently, twice, no error**.
Two real bugs of mine, both now fixed in `ovrhook.cpp`:

1. **Stack smash.** `ovr_GetInputState` writes the *whole* `ovrInputState` (120+ bytes, version
   dependent) through the pointer you pass it, but the code passed a **24-byte** local struct.
   The runtime scribbled past it → silent death. Now uses
   `struct ovrInputStateBuf { ovrInputStateLite f; unsigned char slack[512]; }` — deliberately
   oversized, `static` (off the stack), zeroed before each call.
   **Lesson (again): any struct handed to the runtime must be sized for what the runtime writes,
   not for the fields I read.**

2. **Re-entrancy.** The trigger was polled *from inside* the `ovr_GetTrackingState` detour —
   i.e. calling back into the same runtime from within its own call. Now the polling happens on
   **our own thread** (every 15 ms, in the same loop that hot-reloads the ini), and the detour
   only reads the cached `g_trigger1000` value.

Rimach asked for two things: **smoothing ~4x stronger** (snipers) and **enable it on the right
controller trigger** (the game's zoom/aim bind).

Implemented (all in `ovrhook.cpp`, verifiable from `bl2vr_grip.ini`):
- **Trigger gate**, using `ovr_GetInputState(session, 0x02 /*RTouch*/, &state)` which we call
  ourselves ~20x/second from the detour (session handle captured from `ovr_GetTrackingState`).
  `ovrInputState` layout taken from the SDK bindings, not guessed:
  `f64 TimeInSeconds @0, uint Buttons @8, uint Touches @12, float IndexTrigger[2] @16`
  → **right trigger = IndexTrigger[1] @ byte 20** (`IndexTrigger` is indexed by hand, not by
  controller type; `ovrControllerType_RTouch = 0x02`).
- `trigger_gate`: **0** = ignore trigger, **1** = stronger while held (default), **2** = smooth
  ONLY while held.
- `zoom_mult` (default **4.0**): while the trigger is held the cutoffs are **divided** by this,
  i.e. 4x stronger smoothing — tuned for scoped/sniper aiming.
- `trigger_threshold` (default 0.5).
- When smoothing is off (gate mode 2, trigger released) the filter history is reset so
  re-engaging doesn't snap.
- Log line now carries `trig=`, the multiplier `x4`, and `on=1/0` so we can verify gating.
- One-time `ovrInputState` dump logged on the first successful poll (layout verification).
- Built + installed; `bl2vr_grip.ini` updated with the new keys.
The run at 12:27 succeeded end to end: **8,160 hook calls, game ran fine, no crash.**
- `found Oculus runtime module: VirtualDesktop.LibOVRRT64_1.dll (00007FFDD1E70000)`
- `HOOKED ovr_GetTrackingState target=00007FFDD1E81E70 trampoline=00007FFDD1E60FC0`
- 366 logged pose lines, `flags=15 hf=15/15` once tracking came up.

**Crash fix that mattered:** the real `ovr_GetTrackingState` returns its 312-byte buffer pointer
in **RAX**; our detour was declared `void`, so the game called through a garbage pointer
(hence the fatal error listing `0x4`, `0x20`, `0xfffffff8`). Detour now returns `retbuf`.

**Struct layout PROVEN from Rimach's own data** (the one-time raw float dump), not just trusted:
| value found in dump | byte offset | field | expected |
|---|---|---|---|
| `0.028, 0.006, 0.052, -0.998` | 0 | HeadPose.Orientation (quat) | 0 ✓ |
| `-0.0563, -0.4930, -0.3744` | 112 | HandPoses[0].Position | 96+16 ✓ |
| `0.1352, -0.4939, -0.4134` | 200 | HandPoses[1].Position | 184+16 ✓ |
| doubles (TimeInSeconds) | 80, 176, 264 | end of each ovrPoseStatef | stride 88 ✓ |
=> `ovrPoseStatef` = 88, `HandPoses` at 96, exact match to the SDK bindings.

**Note on call rate:** ~163 tracking calls/second (≈2 per frame at 90 fps). The One Euro filter
runs per call, so the effective smoothing is stronger than the Hz numbers suggest; `min_cutoff`
may need to go up, not down. First smoothing values set to min_cutoff=5, beta=2, rot 3.0/1.2 —
tunable live because the ini is re-read every 2 s.
Rimach: "why don't you use context7 to see how you should write it" → he was right, it caught a real bug.

**1. The struct was wrong (would have corrupted memory).** Checked the generated Oculus SDK bindings
(docs.rs `ovr-sys`, which wraps LibOVR's `OVR_CAPI.h`) instead of guessing:
- `ovrPosef` = 28 : Orientation 0, Position 16
- `ovrPoseStatef` = **88** : ThePose 0, AngularVel 28, LinearVel 40, AngularAcc 52, LinearAcc 64,
  `[u8;4]` pad, TimeInSeconds 80
- `ovrTrackingState` = **312** : HeadPose 0, **StatusFlags 88**, (pad 4), **HandPoses 96**,
  HandStatusFlags 272, CalibratedOrigin 280
Old code had **no StatusFlags** and HandPoses at **112**; size was declared 344 / originally 328.
Because the function returns this struct **by value**, a wrong size/offset corrupts the caller's buffer.
Now enforced with `static_assert`s in `ovrhook.cpp`.

**2. Hooking done with MinHook** (context7: `/tsudakageyu/minhook`) instead of a hand-rolled patch:
`MH_Initialize()` → `MH_CreateHook(target, detour, &orig)` → `MH_EnableHook(target)`.
MinHook decodes the prologue and builds the trampoline itself — nothing hard-coded.
Downloaded to `third_party\minhook-master`; `build.bat` now compiles
`buffer.c hook.c trampoline.c hde/hde64.c` with `include/ src/ src/hde/` on the include path.

**3. Injection vector changed.** The game **never** loads a folder-local `LibOVRRT64_1.dll`
(proved: our file there → flat launch + no log written, even though loading that exact path
in a test host runs `DllMain` fine). But the game **always** loads `openvr_api.dll`.
So the payload now lives in the openvr proxy:
- `proxy.cpp` (existing openvr forwarder) + `ovrhook.cpp` (new payload)
- `ovrhook.cpp` waits for VD's runtime module, finds `ovr_GetTrackingState`, MinHooks it,
  and runs both hands through the One Euro filter.
- Config: `bl2vr_grip.ini` next to the DLL, hot-reloaded every 2 s.

Installed in `Binaries\Win64`: `openvr_api.dll` (ours, 200,192 B), `openvr_api_orig.dll`
(true original, hash `D793E2A7...`), `bl2vr_grip.ini`. Our `LibOVRRT64_1.dll` removed.
**Restore = copy `openvr_api_orig.dll` over `openvr_api.dll`** (and delete `bl2vr_grip.ini`).
(`ovr_test_host.exe`: "real Oculus runtime loaded", "hooked ovr_GetTrackingState").
In-game it produced **no log**, i.e. the game has not loaded our file yet — the last launch was flat
(probably VD not connected, since the game falls back to desktop when there is no VR runtime).
- Full export list saved: `BL2VR-grip\ovr_exports.txt` (128 names, incl. `ovr_GetTrackingState`,
  `ovr_GetTrackingStateWithSensorData`, `ovr_GetInputState`, `ovr_Initialize`, `ovr_Create`).
- Design:
  1. `LibOVRRT64_1.dll` in `Binaries\Win64`, at load `LoadLibraryA` VD's
     `C:\Program Files\Virtual Desktop Streamer\VirtualDesktop.LibOVRRT64_1.dll`.
  2. Export all 128 `ovr_*` names as MASM x64 jump-stubs (`jmp qword ptr [g_<name>]`) — MSVC `.def`
     forwarders are broken (LNK2001), so stubs are used; they pass every register/stack slot through.
  3. At load, fill each `g_<name>` with the real `GetProcAddress`; then repoint
     `g_ovr_GetTrackingState` at our hook.
  4. Hook logs the raw `ovrTrackingState` first (to nail the struct layout empirically), then rewrites the
     right-hand pose (`HandPoses[ovrHand_Right]`) to the two-hand line.
- Open item: exact `ovrTrackingState` / `ovrPoseStatef` layout (HeadPose, HandPoses[2], HandStatusFlags[2],
  CalibratedOrigin) — to be confirmed by logging real data, not guessed.

## Bubble release smoothing (HandStabilizer technique)

- Replaced "cutoff as strength" with a velocity-discriminated release ("bubble").
- Intent signal = magnitude of the LOW-PASSED velocity VECTOR, position and rotation
  separately. A tremor oscillates so its mean velocity is ~0; |dAngle|/dt cannot
  discriminate because |oscillation| has a non-zero mean.
- Asymmetric attack/release: unlock ~40 ms, re-lock ~200 ms (release_ms / attack_ms).
- While released the state is pushed fully onto the raw pose, so stopping no longer
  leaves a multi-second glide to creep away.
- Defaults from HandStabilizer (H3VR): rot 0.54 rad/s, pos 0.13, attack 200 ms, release 40 ms.
- Two-hand block restored to the heavy slide (0.25/0.10, rot 0.05/0.02) and kept fully
  separate from the trigger/zoom block - they never combine.
- Diagnosis: the 20x/60x zoom profile only applies while the trigger is held >= 0.5, so a
  pistol tap-to-shoot never triggered it (that is what the bubble now covers).

## Anti-float release + no-jitter at speed

- The release no longer bypasses the filter. `y += rel*(x-y)` handed the RAW pose through
  while moving, which is what made fast moves jittery. The release now only raises the
  cutoff to a FLOOR (`cut_move`, default 8 Hz = 20 ms) - always filtered, never raw.
- Added a second bubble trigger: the LOW-PASSED residual (`err_tol_rot`/`err_tol_pos`,
  `err_hz`). A tremor alternates sign and averages out of that filter, a real lag does not.
  The bubble therefore stays open until the gun has caught up - this is what removes the
  float/glide tail left behind after a big move.
- Overshoot is impossible by construction: every update is a convex combination or a slerp
  that only moves toward the hand quaternion, and predict_ms stays 0 (extrapolation is the
  only thing that could pass the target).
- New keys: cut_move, err_tol_rot, err_tol_pos, err_hz.

## Reverted the bubble - back to a plain always-on interpolator

- Removed the swarm of threshold logic (release_*, attack_ms, release_ms, intent_hz,
  cut_move, err_tol_*, err_hz). Restored filters.h/ovrhook.cpp to the pre-bubble state.
- Why it had to go: any speed/error threshold is crossed several times a second while
  tracking a moving enemy, so the filter toggles on/off -> lurch, plus the release ramp
  gives a slow boat-like start. There is no smooth way to switch, only not to switch.
- The cut_move floor was also CANCELLING the trigger profile smoothing, which is why
  "trigger only" felt unsmoothed.
- Retuned so nothing drifts: base rot 1.5 Hz (0.11 s), trigger 0.375 Hz (0.42 s) via
  zoom_mult_rot=4, two-hand rot 0.4 Hz (0.40 s). No more 0.05 Hz (3.2 s) drift.
- predict_ms stays 0: extrapolation is the only thing that could overshoot.

## Critically damped follower (S-curve) replacing the first-order low-pass

- A first-order low-pass is not smooth: it hits MAXIMUM velocity in frame 1, i.e. an
  instant 0 -> max velocity jump = infinite acceleration. That is the kick at the start
  and the hard corner on arrival. Lag != smooth.
- filters.h now implements a second-order critically damped spring (zeta = 1), which is
  Unity SmoothDamp / Game Programming Gems 4:  acc = w^2*err - 2*w*v,  w = 2/smoothTime.
- The follower carries its own velocity, which starts at ZERO, ramps up, peaks, then
  decays into the target. That is the S-curve.
- Rotation version does the same in the log map: r = log(conj(q)*n), apply q*exp(stepR),
  with the body-frame angular velocity damped the same way.
- Unity's closed-form exp() solution is used (not raw integration) so it is stable and
  frame-rate independent. step() signatures, config keys and the ini are unchanged -
  smoothTime = 1/(2*pi*cutoff), so every existing value keeps its meaning.
- beta now stiffens the spring smoothly with speed (no threshold, nothing to toggle).
- Verified numerically: full step response at 90 Hz and 163 Hz -> peak 1.000000,
  overshoot 0; peak velocity dt-independent (6.9456 vs 6.9447).

## THE dt BUG (root cause of "floaty / no smoothing / nothing changes")

- Log evidence: the game polls ovr_GetTrackingState ~180 Hz (120 calls / 0.667 s) but every
  filter step was fed dt=0.0001 - the old clamp floor, not a real interval. The follower's
  clock ran ~55x slower than real time, so EVERY profile collapsed into the same molasses.
  The trigger gate and multipliers were applying all along (trig=1.00 x6/4 on=1 in the log),
  they just could not be felt: jitter meter showed 3.3x vs 3.1x smoother, held vs not.
- Why: the game calls in bursts from more than one thread, so the per-call QPC delta was
  near zero for most calls; the shared g_lastQpc was also racy.
- Fix: NowDt() now steps by the average inter-call time (deltas telescope, so the running
  mean IS 1/call-rate), guarded by g_fcs against the thread race, clamped to 0.2-50 ms.
  Verify in openvr_proxy.log: dt= must read ~0.005-0.006, never 0.0001.
- Also corrected the settle-time claim: settle(95%) ~= 2.4 * smoothTime = 0.38 / cutoff
  (a second-order follower arrives slower than a first-order at the same smoothTime; the
  earlier table omitted that factor). Retuned: base 3.0 Hz (0.13 s), trigger/two-hand
  1.0 Hz rot (0.38 s) via zoom_mult_rot=3.
