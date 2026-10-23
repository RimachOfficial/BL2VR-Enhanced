# Changelog

All notable changes to BL2VR Enhanced. Dates are local (Greece).
Format: [Keep a Changelog](https://keepachangelog.com/); versioning: [SemVer](https://semver.org/)
where "version" tracks the mod (not the game).

## [Unreleased]

## [0.1.2] — 2026-10-23

### Fixed
- **`twohand_min_dist` was never read by the mod** — the key existed in the ini, the README
  and the panel, but the grip always engaged at the hard-coded 0.12 m. It is now read, so
  the panel slider actually works.
- **Thread-safety of the runtime hook** (found via code-graph audit):
  - `NowDt()` ran outside the filter lock — two game threads could read the same clock and
    double-feed time to the filters. The hook now takes an atomic snapshot
    (config + clock + session reset) under `g_fcs` each call.
  - `LoadCfg()` rewrote ~30 `g_cfg` fields non-atomically while the hook read them at
    ~180 Hz — a reload could mix old/new profile values for one call. Config is now built
    locally and published under the same lock the hook snapshots with.
- **`LoadCfg` fallback defaults contradicted the shipped ini** (missing-key behavior):
  `trigger_threshold` 0.5 → 0.25 (0.5 re-created the old dead-gate bug),
  `derivative_cutoff` 1 → 6, `zoom_mult` 20 → 2, `min_cutoff`/`beta`/rot 3.0/1.0/2.0/0.6 →
  4.5/2.0/3.0/1.5, `twohand_*` 1.5/8.0/1.0/6.0 → 3.0/2.2/2.0/1.5, `log_frames` 400 → 150.
  All fallbacks now match the shipped `bl2vr_grip.ini`.
- **Shutdown safety**: the log is closed under its own lock and the lock is no longer
  deleted on DLL detach (the worker thread never exits and could still be logging).
- Ini parser now skips `;`/`#` comment lines properly (the shipped ini has comments that
  contain `=`, which parsed as bogus keys — harmless but wrong).
- Panel: the "Rotation beta" override on the Gunstock page had an empty hover tooltip.

## [0.1.1] — 2026-10-23

### Added
- Panel **Profiles** page: one-click feel presets (Default / Steady / Hybrid / Responsive /
  Sniper) with auto-detected **Custom** state; profiles write only the 19 feel keys and never
  touch hardware setup.
- Panel **Remove mod** (full uninstall: original DLL restored, mod files deleted) and
  **Launch game** buttons.
- GitHub Actions CI: builds the mod DLL + panel on every push, uploads the release files as
  artifacts; portable `build.bat` (locates MSVC via `vswhere`, no hardcoded VS path).
- Issue/PR templates, `.editorconfig`.

### Fixed
- **Panel patcher bug**: "Patch game" did not back up the original `openvr_api.dll` as
  `openvr_api_orig.dll` — on a clean game the proxy had nothing to forward to. The backup is
  now automatic (first patch only).
- Panel: widget ID conflicts (every slider/combo now scoped with `PushID(key)`).
- Panel: footer status text could overlap the hot-reload hint — status is now
  ellipsis-truncated to the available width, hint hides on very narrow windows.
- Panel: the ini is re-loaded after Detect/Patch, so the editor always edits the real file.
- README config table had stale defaults (`zoom_mult 20` vs shipped 2.0; twohand override
  0 vs shipped 3.0/2.2/2.0/1.5) — corrected to the shipped ini.

### Changed
- Panel UI redesigned: custom theme, Segoe UI fonts, sidebar navigation, toggle switches,
  sliders, per-monitor DPI awareness.
- Documentation suite: THE_STORY, ARCHITECTURE, METRICS, TESTING, CHANGELOG, CONTRIBUTING,
  CODE_OF_CONDUCT, SECURITY; GPL-3.0 license.
- Repository hygiene: game files, proprietary runtime DLLs, build artifacts and logs removed
  from tracking; strict `.gitignore`.

## [0.1.0] — 2026-10-13

First version validated in the real game: two-handed grip + smoothing, via an `openvr_api.dll`
proxy that carries a MinHook detour on `ovr_GetTrackingState` in Virtual Desktop's Oculus runtime.

### Added
- Virtual gunstock: aim along the filtered hand-to-hand line, own One Euro pass on the line,
  trigger-controlled stock weight (`stock_*` keys), configurable engage button/hand, lefty support.
- Canonical One Euro filtering (raw-velocity driven cutoff, no stored follower velocity).
- Trigger/zoom gate reading both controllers (`zoom_hand=either`, `OVR_SUCCESS >= 0`).
- Optional lag-proportional `catchup` term (off by default).
- Live config hot-reload every 2 s; single-read ini parser (was ~30 disk reads per reload).
- Desktop launcher/patcher + config panel (`tools/imguipanel`, ImGui): Steam auto-detect,
  one-click patch, desktop shortcut, friendly names + hover help.
- Built-in jitter meter (raw vs filtered deg/call) and per-second axis-calibration helper in the log.
- `install.ps1` install/restore with automatic backup.
- Offline filter bench (`tools/filter_bench.cpp`) exercising the real shipping filter code.

### Fixed
- Trigger gate was silently dead (`OVR_SUCCESS` tested `== 0` instead of `>= 0`).
- "Floaty / nothing changes" feel: per-call `dt` was the clamp floor (0.0001 s) because the game
  polls in bursts from multiple threads — follower clock ran ~55× slow.
- Post-move float: threshold "bubble" release replaced with always-on interpolation
  (a speed/error threshold toggles while tracking a moving target → lurch).
- Panel: black window (missing DX11 render target), duplicate widget IDs, streaky live resize.

### Known issues
- SteamVR launch crashes the game on the tested setup (pre-existing, reproducible with the
  pristine DLL — the crash precedes any mod code); launch through Virtual Desktop, which serves
  the game its Oculus (LibOVR) runtime — the path the mod hooks.
- First filter sample after enabling starts from the raw pose (no history yet).
