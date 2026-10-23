# Contributing

Thanks for wanting to make BL2VR Enhanced better. This is a small, focused mod — contributions
that respect the design rules below are welcome (issues, discussions, PRs).

## Ground rules

1. **No game files.** Never commit anything from Borderlands 2 VR (`*.upk`, configs, binaries),
   Valve/Meta/Virtual Desktop DLLs, or decompiled code. The `.gitignore` enforces most of this.
2. **No thresholds in the filter path.** A speed/error "bubble" was built, measured and reverted:
   it toggles while you track a moving enemy and every toggle is a lurch. See
   [README](README.md#do-not-add-a-speederror-threshold) before proposing one.
3. **No sub-1 Hz cutoffs** as defaults. Below ~1 Hz the pose drifts for ~a second after you stop.
4. **Measure, don't assert.** Any change to `filters.h` must report before/after numbers from
   `tools/filter_bench.cpp` (see [docs/METRICS.md](docs/METRICS.md)).
5. **`predict_ms` stays 0** unless you can prove it cannot overshoot.

## Workflow

- Branch from `main`; open a PR between branches (no forks needed for regulars).
- Commit style: [Conventional Commits](https://www.conventionalcommits.org/) —
  `feat(vr): ...`, `fix(panel): ...`, `docs: ...`, `chore: ...`.
- One logical change per PR; include the *why* in the description.
- Test: offline bench for filter work, plus the smoke tests in [docs/TESTING.md](docs/TESTING.md)
  for anything that touches the hook. Say what you could and could not test (no headset on your
  machine is fine — say so).

## Good first places to look

- Tuning presets for specific controllers/headsets (config-only PRs are welcome).
- `aim_axis` calibration data for other controllers.
- Panel UX improvements (`tools/imguipanel`).
- Documentation fixes — always welcome, never "too small".

## Code style

- C++17, MSVC x64. Existing style in `openvr_proxy/src` — keep it (tabs/spacing as found).
- No new dependencies without discussion; MinHook and ImGui are vendored on purpose.
