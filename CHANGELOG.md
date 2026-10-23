# Changelog

All notable changes to BL2VR Enhanced. Dates are local (Greece).
Format: [Keep a Changelog](https://keepachangelog.com/); versioning: [SemVer](https://semver.org/)
where "version" tracks the mod (not the game).

## [Unreleased]

### Added
- Documentation suite: THE_STORY, ARCHITECTURE, METRICS, TESTING, CHANGELOG, CONTRIBUTING,
  CODE_OF_CONDUCT, SECURITY.
- GPL-3.0 license.

### Changed
- Repository hygiene: game files, proprietary runtime DLLs, build artifacts and logs removed
  from tracking (they were never meant to be public); strict `.gitignore` added.

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
