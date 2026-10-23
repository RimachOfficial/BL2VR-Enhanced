# Fixing Borderlands 2 VR — the story

*How a one-hand-only, jittery VR shooter got a real gunstock feel — reverse-engineered at the
runtime boundary, with an AI doing the pairing and a human doing the testing.*

**Timeline:** 2026-10-12 → 2026-10-13. **Stack:** Windows x64, C++, MinHook, Virtual Desktop,
one very patient tester in a headset. **Repo commits:** 19, conventional-commit style.

---

## 1. The problem (context & scope)

Borderlands 2 VR (Steam 991260) aims one-handed. In VR that means: every shot inherits your raw
wrist tremor, there is nothing to brace against, and long-range guns are lottery tickets. No
two-handed-grip mod existed that we could find. Scope decisions made up front:

- **In:** make guns aimable with two hands; smooth rotation; keep the game's own weapon feel
  (per-weapon grip offsets); install/uninstall must be trivial.
- **Out:** touching game files. An earlier attempt patched `Startup.upk` — reverted, and the
  decision hardened into a rule: **zero game files modified**.
- **Constraint:** the mod must fight nothing. Anything that patches the running exe or the
  renderer would break every patch day and every anti-cheat-adjacent edge.

## 2. The plan (action, part 1)

Four routes were written down before a line of code, ranked cheap→expensive, each with the
assumptions it needed proven first:

| Route | Idea | Assumption to test |
|---|---|---|
| **A** | Proxy `openvr_api.dll`: forward everything, rewrite the gun-hand pose | the game reads poses through OpenVR |
| **B** | Inject unrealsdk, edit Unreal objects | the SDK attaches to this game at all |
| **C** | Native hook inside the exe (Ghidra + x64dbg) | — (expensive fallback) |
| **D** | Patch the exe on disk | — (last resort, rejected) |

The discipline that made it work: **never build on an untested assumption**. Every "if" in the
plan got an evening that existed only to kill or confirm it.

## 3. Session 1 — the corpse of the plan (action, part 2)

Route A was built fast: a proxy DLL with the game's own `openvr_api.dll` name, 14 exports
forwarded through explicit C++ wrappers (MSVC `.def` forwarders fail with LNK2001 — first
gotcha), logging everything.

Then the game loaded the proxy… and **never called a single OpenVR function**. Not once.

What followed was runtime forensics, each step driven by evidence:

1. **Standalone test host** proved the proxy itself worked (`VR_IsHmdPresent`=1, every call logged).
   So the proxy was fine — the game just wasn't using it.
2. **Module snapshots at 8 s and 50 s** inside the real game: 124 modules, no SteamVR, and at
   50 s exactly one VR DLL — `VirtualDesktop.LibOVRRT64_1.dll`.
3. **Registry**: `HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime` → Virtual Desktop's VDXR.
4. **Strings in the exe**: 72 `ovr_*` symbols, `LibOVRRT`, and a `SwitchVRPlatform` selector —
   the game supports Oculus *and* SteamVR and picks one.

**Root cause:** on this PC the game runs in **Oculus mode, served by Virtual Desktop's fake
Oculus runtime**. It never touches OpenVR. Route A's *assumption* was wrong — the route's
*idea* (proxy the folder DLL) was still right.

The plan's biggest open question — "which pose API does the game read: IVRSystem, IVRCompositor
or IVRInput?" — was answered in one evening with a better answer than any of the three:
*none of them*.

## 4. The pivot (action, part 3)

Two candidates:

- **Standalone `LibOVRRT64_1.dll` proxy** — built completely: 128 exports regenerated as MASM
  x64 jump stubs (`.def` forwarders are broken; stubs pass every register), export set verified
  byte-for-byte identical to Virtual Desktop's DLL, struct layouts `static_assert`-checked.
  Installed… the game wrote **no log**. It would not load our runtime stand-in.
- **Hybrid (shipped):** the game *always* loads `openvr_api.dll` from its own folder — so keep
  that DLL as the **carrier** (pure forwarding), and put the **payload** inside: a worker thread
  waits for VD's Oculus runtime, then MinHooks `ovr_GetTrackingState` — the exact call the game
  uses to read controller poses.

That is the architecture that shipped: hook placed at the *app boundary*, inside the app's own
process, on the one call that matters. It does not fight the runtime because it *is* the app.

## 5. The tuning war (action, part 4)

The hook worked; the *feel* took several measured iterations. Each was built, tested in
headset, and kept or killed on evidence:

| Iteration | What it was | Verdict |
|---|---|---|
| One Euro, heavy profiles | first smoothing | too heavy; "boat" feel on movement onset |
| **"Bubble" release** (HandStabilizer-style) | thresholds: unlock at speed, re-lock at rest | **reverted** — tracking a moving enemy crosses the threshold several times/second; every toggle is a lurch, and the release ramp felt like a boat. Rule born: *there is no smooth way to switch; only not to switch* |
| Critically damped S-curve follower | second-order spring, zero initial velocity | kept as knowledge: a first-order low-pass hits max velocity on frame 1 — lag ≠ smooth |
| **The dt bug** | logs showed every filter step fed `dt=0.0001` (the clamp floor): the game polls in bursts from multiple threads, so the follower's clock ran **~55× slow** and every profile collapsed into the same molasses | fixed by stepping on averaged inter-call time (deltas telescope). The trigger gate had been applying all along — it just could not be felt |
| `OVR_SUCCESS` bug | gate tested `== 0`; Oculus returns `>= 0` on success → every successful poll was **discarded** | one-character fix; the dead trigger gate came alive |
| **Canonical One Euro** | cutoff driven by RAW velocity, no stored follower velocity | kept — the raw velocity is already large on frame 1 of a move, so the filter opens instantly; nothing to overshoot after a stop |
| **Virtual gunstock** | filter hands first, then smooth the hand-to-hand line with its *own* filter | shipped — "smoothing is the base of everything" |

Plus the invisible-quality work: ini reload went from ~30 disk reads per change to one 8 KB
read; a desktop panel (ImGui) made every value editable live while the game runs.

## 6. The result

| | Before | After |
|---|---|---|
| Grip | one-hand | two-handed virtual gunstock, free-sliding hands |
| Post-move settle | 1.28 s float | **0.08 s** (16×, bench) |
| Fast-move lag | 13.9° | **1.8°** (7.7×, bench) |
| In-game tremor | raw | **3.3× smoother** rotation (jitter meter, live log) |
| Zoom feel | nothing | trigger progressively weights the stock |
| Install | — | 2 files, zero game-file edits, fully reversible |

Validated in the real game on 2026-10-13, on real hardware, with the log to prove it.

## 7. The AI workflow (how this was actually built)

The division of labour that worked:

- **Human:** owned the *problem* — sat in the headset, felt every regression, said "the trigger
  does nothing" and "it floats after big moves", made the calls on feel. Never delegated the
  judgement of "is this better?".
- **AI:** owned the *mechanics* — wrote the proxy, the MASM stub generator, the hooks, the
  filters, the panel; ran the forensics (module snapshots, registry, string dumps, export lists);
  built the measurement instruments (jitter meter, bench harness) so decisions had numbers.
- **Both:** every assumption was written down before it was tested, and every session ended with
  a log line or a benchmark that settled the question. The AI was wrong several times — the EMA
  `dt` detour, the bubble's promise, the OpenVR assumption — and the *measurement* was what
  caught it every time, not opinions.

Rules that emerged and are now written in the repo:

1. Measure, don't assert (build the instrument first).
2. Never ship on an untested assumption; write the assumption down, then go kill it.
3. No thresholds in a control loop a human hand moves through.
4. Keep the receipts: the [DEVLOG](DEVLOG.md) is the raw session log, wrong turns included.

## 8. Timeline

| When | What |
|---|---|
| 2026-10-12 | Plan written (4 routes, assumptions listed). Route A built; proxy loads, never called. Forensics: module snapshots → registry → strings → **root cause: Oculus-via-Virtual-Desktop**. Standalone 128-export `LibOVRRT64_1.dll` proxy built; game refuses it. Hybrid carrier+payload design chosen. |
| 2026-10-13 | Two-handed grip implemented and **validated in game**. Axis calibration helper added. README written. |
| after | Bubble built → measured → reverted. S-curve explored. dt bug found & fixed. `OVR_SUCCESS` fix. Canonical One Euro. Single-read ini + ImGui launcher/patcher panel. **Virtual gunstock** — final architecture. |
| 2026-10-23 | Repository professionalized: docs suite, GPL-3.0, hygiene (no game files, ever). |

## 9. What we'd tell the next person

- **The layer matters more than the idea.** Rewriting the gun pose was always going to work;
  the fight was finding the one boundary the game actually crosses.
- **Runtime forensics beats disassembly.** 124 module names answered what hours in a
  disassembler could not.
- **A number in a log beats an hour of debate.** The jitter meter and the bench harness paid
  for themselves many times over.
- **Your filter feels bad because of a 0.0001 in a log file.** Check your clocks first.

