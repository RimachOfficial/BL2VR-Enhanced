# Changelog

All notable changes to BL2VR Enhanced. Dates are local (Greece).
Format: [Keep a Changelog](https://keepachangelog.com/); versioning: [SemVer](https://semver.org/)
where "version" tracks the mod (not the game).

## [Unreleased]

## [0.1.6] — 2026-10-23

### Fixed
- **The trigger no longer undoes the smoothing.** Catch-up (`catchup_rot/pos`) works by *opening*
  the filter to cut lag, so it trades smoothing away — and it was being applied while the trigger
  was held, i.e. exactly when you want maximum steadiness. Measured with `tools/catchup_demo.cpp`:
  sweeping at 60°/s with the trigger held, `catchup_rot 150` pushed the effective cutoff from
  0.4 Hz to 5.2 Hz (tremor pass-through ~10% → ~60%). Catch-up is now skipped while the trigger is
  held, and the profile values dropped from 80–160 to 0–30.
- **`per_thread_filters` now defaults to 1**, because the shared state was measurably broken on
  the tested setup: the game polls tracking from **two threads whose `absTime` values are offset by
  about one frame**, interleaved. A single shared filter therefore received the pose *stepping
  backwards* on **44% of calls (1449/3325 samples, jumps back up to 0.58 s)**. The raw derivative —
  with `dt` at the 1e-4 clamp floor — then reported fake velocity of tens of m/s, which opened the
  adaptive cutoff completely. Two consequences, both reported by the player: smoothing appeared to
  switch off, and profile changes appeared to do nothing (`min_cutoff` is irrelevant next to a
  ~60 Hz fake velocity term). Per-thread state gives every filter one monotonic timeline.

### Added
- `tools/catchup_demo.cpp` — solves the catch-up steady state and shows the effective cutoff and
  tremor pass-through for a given aim speed, cutoff and catch-up value.
- The jitter log line now reports `t-backwards=N/M` (samples whose `absTime` went backwards on that
  state) and the periodic line carries `tid=`, so the timeline question is answered by the log.
- "polling thread seen: tid=…" is logged for every distinct calling thread, in both modes (the
  counter is no longer silently zero when per-thread state is off).

## [0.1.5] — 2026-10-23

### Added
- `tools/tremor_sweep.cpp` — how much of an **8 Hz hand tremor** survives each cutoff, using the
  real filter code. It produced the honest number this project was missing: the shipped base
  profile (3.0 Hz rotation) leaves **34%** of the tremor, not the ~10% the docs implied (that
  figure came from the heavy trigger/two-hand profiles). Full table in `docs/METRICS.md`.
- Panel profile **Locked** — precision preset using cutoffs *below* the project's 1 Hz floor,
  guarded by a strong catch-up term. Steady/Responsive were also spread wider (1.0 Hz / 6.0 Hz)
  so switching profiles is unmistakable instead of a subtle nudge.
- Logging: periodic lines now carry `prof=` (the live filter values actually in use), and a failed
  or empty `IniLoad` now logs instead of silently keeping the old config — a silent reload failure
  looked exactly like "changing a setting did nothing".

### Fixed
- Panel ini writes are now atomic (temp file + `MoveFileExA`). The mod re-reads the file every 2 s,
  so a truncate-and-rewrite could be caught mid-write and parsed as a partial file — missing keys
  fall back to defaults, which looks like "the profile did nothing".
- Docs claimed "positions are never modified" — the **stock solve** never moves positions, but the
  hand filter does smooth them (that is what removes positional tremor). Corrected in README,
  ARCHITECTURE and the limitations list.
- `derivative_cutoff` (6 Hz vs the reference spec's ~1 Hz) was investigated as a possible cause of
  weak filtering and **measured to be a non-issue** (<1% tremor difference); recorded in METRICS.

## [0.1.4] — 2026-10-23

### Added
- **`per_thread_filters` (default 0)** — one filter state (`FilterState`: filters, clock, session
  marker, jitter counters) per calling thread, so threads that ask for poses at different
  `absTime` values can never mix prediction timelines. Opt-in and measured rather than switched
  on blindly: each thread then sees about half the samples, so the velocity estimate is a little
  noisier. The panel exposes it under **Smoothing → Advanced**, with the decision table in
  [docs/TUNING.md](docs/TUNING.md).
- Thread diagnostics: the periodic log line now carries `tid=`, and the jitter line reports how
  many distinct polling threads were seen — together with `t=` that answers whether the game
  really asks for different prediction horizons.

### Changed
- README results table states the tremor/lag tradeoff openly (current tuning keeps ~10% of tremor
  vs the old ~5%, in exchange for 7.7× less lag); new **Compatibility** section declares what is
  tested (one setup) vs untested (Link/Rift, SteamVR) plus the unsigned-binary/antivirus notes;
  deep filter content split out to `docs/TUNING.md`; `ovr_proxy/` moved to `tools/ovr_proxy_legacy/`.

## [0.1.3] — 2026-10-23

### Added
- The periodic log line now prints `t=` (the `absTime` each `ovr_GetTrackingState` call asked
  for) — first step in investigating whether different game threads request different prediction
  horizons (one shared filter state would then be mixing timelines). Hypothesis documented in
  `docs/ARCHITECTURE.md`.

### Changed
- README restructured after external review: the results table now states the tremor/lag tradeoff
  openly (current tuning keeps ~10% of tremor vs the old ~5%, in exchange for 7.7× less lag),
  a **Compatibility** section declares exactly what is tested (one setup) and what is not
  (Link/Rift, SteamVR) plus the expected antivirus/SmartScreen warnings for the unsigned DLL/exe,
  and the deep filter/tuning content moved to `docs/TUNING.md`.
- `ovr_proxy/` (abandoned first attempt) moved to `tools/ovr_proxy_legacy/` to declutter the root.
- New `docs/TUNING.md` (filter maths, bench numbers with the tradeoff column, tuning rules,
  the dt post-mortem); release assets now carry SHA-256 hashes in the release notes.

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
