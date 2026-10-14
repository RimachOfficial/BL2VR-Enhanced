# Borderlands 2 VR — Two-Handed Grip: Implementation Plan

Created: 2026-10-12
Owner: Rimach (Dimitris Giamos)
Status: **It WORKS (a variant of Route A).** Validated in the real game on 2026-10-13: the two-handed grip runs, with smoothing. **Correction to this plan:** the shipped mod does not rewrite OpenVR poses. BL2VR talks to the Oculus SDK (LibOVR) through Virtual Desktop's runtime, so the mod hooks `ovr_GetTrackingState` there; `openvr_api.dll` is only the injection point (Appendix G). This file is now the plan plus a field record; what remains is tuning (Appendix E, config in Appendix F). Routes B–D below were never needed for the core result.

---

## Part 0 — TL;DR (read this first)

> [!note] Result, 2026-10-13
> A pose-override mod was built and **works in game**. The idea (rewrite the gun hand's pose before the game reads it) was right,
> but the layer was not: **not OpenVR** — see Appendix G. The game uses the Oculus SDK through Virtual Desktop, and the mod hooks
> `ovr_GetTrackingState` there. Everything below about OpenVR interfaces (`IVRSystem` / `IVRCompositor` / `IVRInput`) did not apply to this game.
> The shipped design has: a two-hand grip toggled by a button (`twohand_button=grip`), configurable gun hand / off-hand,
> a one-euro-style smoothing filter with an "intent bubble" (tremor is smoothed hard, deliberate movement opens the filter),
> a trigger-held zoom gate, a separate smoothing block for two-hand mode, and an INI that is re-read every 2 seconds.
> The sections below keep the original reasoning; where they say "assumption" or "to be tested", read "tested, passed".

**Goal:** make guns in Borderlands 2 VR aimable with two hands (off-hand on the foregrip), instead of the current one-hand-only aim.

**Key finding:** we do NOT have to decompile the game to get a working two-handed grip.
There is a route that touches zero game code:

1. **Route A (cheap, best first): proxy `openvr_api.dll`** — the game imports `openvr_api.dll` at startup [verified on this PC].
   We put our own DLL with that name in `Binaries\Win64`, it forwards everything to the real OpenVR DLL,
   and it *rewrites the right controller's pose* before the game reads it. **If** the gun's orientation really comes
   from that pose (assumption — tested in Evening 1, see Part 11), the gun then points along the line between both hands.
   No disassembly, no exe patching.
   The one detail to get right: **which OpenVR call the game reads** — `IVRSystem`, `IVRCompositor` **or `IVRInput`
   (SteamVR Input pose actions)**. Hooking all three is cheap; the logging proxy in Part 11 must cover all three.
2. **Route B (cheap test): inject `unrealsdk` (bl-sdk, WILLOW flavour)** — see if it attaches to BL2VR at all.
   If it does, we can read/change Unreal objects (weapon actor, motion controller components) from UnrealScript-level hooks.
3. **Route C (the real, expensive fix): native hook inside `Borderlands2VR.exe`** — Ghidra/IDA + x64dbg + AI agent,
   find the function that builds the weapon transform from the controller, feed it a two-hand solve.
   This is the "when I have money + a better model" job.
4. **Route D (last resort): patch the exe on disk** — most fragile, no flexibility.

**Assumptions that Evening 1 exists to test (do not build on them blind):**
- the gun's aim follows the right controller's pose — the exe also contains an `AimControllerComponent` string,
  so aim/reticle may be computed somewhere else,
- the game does not smooth or lag the pose itself before using it,
- the game reads poses through `IVRSystem`/`IVRCompositor` and not through SteamVR Input (`IVRInput` pose actions).

**Time estimate (honest):** the *visible core* of Route A — "proxy loads, logs poses, gun follows the two-hand line" —
is 1–3 evenings with an AI agent. A *shippable* mod is not: per-weapon offsets, scoped aiming, melee, vehicles and the
right-hand mesh rotating with the gun are where the days go. Route C = weeks, plus per-game-update maintenance.

**Next move, and it needs no money:** Evening 1 is a **logging-only** proxy (forward everything, write down every OpenVR
call the game makes). It answers the question everything else depends on — which pose API the game reads — in a few hours.

**The workflow that worked for all the other projects is in Part 1.** It is the same loop the September/October 2026
AI-modding wave used (Terraria, AoE2, SimCity 2000, GTA V × Minecraft, CoD Zombies, Halo CE in a browser).
Read Part 1 once, then reuse it forever.

---

## Part 1 — The workflow that worked (reusable for any game)

This is the community recipe (universal-modder, re-skill, and the decompilation blogs), plus the house rules
that already work for us. It is a loop, not a list — you always know which stage you are in and what the next artifact is.

> [!note] Evidence quality — read this before trusting any number below
> The figures that circulate with this workflow ("27 sub-agents", "100% of hallucinated claims caught", "a whole game
> decompiled overnight") come from **blog posts and single demos**, not from repeatable measurements. Treat them as
> **folklore, not guarantees**: the *shape* of the loop is what is reusable, the statistics are marketing. Wherever this
> plan repeats such a number it is labelled a *claim*.

### Stage 0 — Intake (5 minutes)
- Write the game, the platform/store, and the idea in one sentence. Here:
  *"BL2VR (Steam 991260): make guns aim with two hands, off-hand on the foregrip."*
- Agree what "done" means. Here: *gun orientation follows the two-hand line, in the real game, with a recording.*
- Check online/anti-cheat. BL2VR = single-player, no anti-cheat → free to mod the client.
- **Start `MODLOG.md`** in the project folder. It is the journal. Every path, offset, class name, failure and next step goes in.
  Anything not written in the journal is lost at the next context compaction.

### Stage 1 — Recon (`game-recon` skill → produces `MODDING_PLAN.md`)
- Search the shared knowledge base first: `um kb search "borderlands"`, `um kb search "unreal"`.
  Read `knowledge/INDEX.md` in the universal-modder repo (117+ game notes, many by agents, free answers).
- Fingerprint the install: `um scan --list`, `um scan "Borderlands 2 VR"` → engine, version, managed vs native,
  anti-cheat, loaders already present, save/config folders, ranked routes.
- Research the living community in this order: game wiki modding page → Nexus/Workshop/Thunderstore (what frameworks do
  popular mods depend on?) → GitHub (`<game> mod`, `<game> sdk`, `<game> decompile`, `<engine> mod loader`) →
  recent X/Reddit/Discord posts (a loader can be days old).
- Output: `MODDING_PLAN.md` (template in Appendix A).
- For BL2VR this stage is already done — see Part 2.

### Stage 2 — Choose the route (cheapest that reaches the idea)
Priority ladder — pick the FIRST one that works:
1. loader API (PythonSDK/UE4SS/BepInEx/ASI…)
2. data/asset mod (ini, cooked UPK, paks, text files)
3. managed patching
4. **native hooks (proxy DLL + detour)** ← Route A and C live here
5. reimplementation / mashup
Write the choice and the *reason* into `MODDING_PLAN.md`. Never jump to the expensive route because it sounds cooler.

### Stage 3 — Lab (make it safe before touching anything)
- Back up first: `um backup create`, or copy the files you will touch (`openvr_api.dll`, configs, saves).
  Write the restore path into `MODLOG.md`.
- Never edit the real install directly for experiments: copy the game folder, or keep every change as
  "one file + one restore command".
- Kill processes by exact PID only. Never wildcard-kill. Never block the game's main thread.
- Anti-cheat games: debuggers/scanners off, full stop. (Not an issue here.)

### Stage 4 — Source of truth (read the real thing, never guess)
Rule zero from the community skill: **decompiled code, the actual data, a live memory view, or a GPU capture IS the spec.**
Not the wiki, not the forum post, not the model's memory.

Tool per code type:

| Code type | Tool | Note |
|---|---|---|
| Managed .NET (C#) | `ilspycmd -p -o ~/<game>-decomp <Game>.exe`, dnSpyEx | gives a full C# project you can grep |
| Unity IL2CPP | Cpp2IL / Il2CppDumper on `GameAssembly.dll` + `global-metadata.dat` | bodies are native → then Ghidra |
| Java | Vineflower / CFR / Recaf | |
| Native C/C++ (UE3, custom engines) | Ghidra or IDA **driven through MCP** | GhidraMCP, pyghidra-mcp, IDA MCP, Binary Ninja MCP |
| Live memory | Cheat Engine (+ CheatEngine MCP bridge), x64dbg (+ x64dbg-mcp), Frida (+ frida-mcp), ReClass.NET | value scan → "find what writes this" → struct → owner |
| GPU / rendering | RenderDoc (+ renderdoc-mcp) | each draw, constant buffers (view/proj matrices), depth |
| Data formats | community tool first: FModel/UAssetGUI (Unreal), UABEA/AssetRipper (Unity), xEdit (Bethesda), GDRE (Godot) | XeNTaX + ZenHAX archives on archive.org for old formats |
| Claims checking | `reverify` (LLM proposes a fact about a binary, the tool verifies it deterministically, verified facts go in a ledger) | catches hallucinated offsets |

For an undocumented format the order is: collect stock files → hex-dump headers → guess the layout →
parse *all* stock files without errors → decode to something visible (PNG/JSON) and look at it →
write the encoder and **prove it with a round trip** (decode → encode → decode vs original) → only then write new files.

### Stage 5 — Vertical slice (one tiny thing working end to end)
Pick the smallest version of the idea that is visible in the real game. Not "the whole feature" — one weapon, one case.
Example for us: *one gun's orientation changes when the off-hand is near it, and nothing else breaks.*
This is where the AI-RE wave differs from old modding: the agent gets a **referee** (a machine that scores it) and can
loop alone. Pick the strictest referee available:

| Project type | Referee | Consequence |
|---|---|---|
| Matching decompilation (console, known compiler) | byte-compare of compiled output | agent can loop 100% alone |
| Reimplementation / port | screenshot / pixel compare, or state compare | agent can loop alone |
| Gameplay logic you port | recorded traces replayed against the port | agent can loop alone |
| **Native hook (ours)** | pose/transform traces + screenshots | agent loops, human checks "does it feel right" |

### Stage 6 — The agent loop (the actual mechanics)
- One task at a time from the task list. Run 2–3 tool calls → **write the finding down immediately** (MODLOG / REVERSE.md /
  labels.csv). Never batch investigation and write at the end.
- **Stuck rule:** after 10 tool calls with no progress → append to `dead_ends.md` (tried / why it failed / better idea),
  split the task into 2–3 smaller ones, move on. If the same task fails across 3 sessions, the decomposition is wrong —
  rethink the approach, not the effort.
- **Circuit breaker:** same failure 3 times → stop, write what is known, change approach.
- Commit every confirmed fact. Small verified steps beat big guesses.
- Parallel agents for independent work (the SimCity 2000 run used **27 sub-agents**, one per function/subsystem).
  Keep one human-in-the-loop check per few hours (community figure: ~5% expert intervention in decomp projects).
- Overnight runs are real: point the agent at the game before bed, read the journal in the morning
  (`re_loop.sh` from re-skill does exactly this: read task list → work → commit → repeat).

### Stage 7 — Verify in the real game
- Launch it, drive it (`um win ...`: screenshot, click/type, hold keys, record), check the real behaviour.
- Logs, per loader: `BepInEx/LogOutput.log`, `UE4SS.log`, `Player.log`, `client.log`, game-folder logs.
- Install an unhandled-exception filter that writes a minidump, and log with flushes. A crash with no log costs a day.
- Screenshot pairs / trace diffs for every claim: FIXED / MINOR / CRITICAL.

### Stage 8 — Package
- Ship code + your own assets + a patch. **Never game files, never decompiled code.**
- README: install steps, exact game version supported, honest "built with AI" line.
- `um publish check` lints for game files, decompiled code and secrets. Lint your own stuff the same way.

### Stage 9 — Field note (this is what makes the next project fast)
- Turn `MODLOG.md` into a knowledge-base note: exact versions, the route, what the engine really does,
  how you verified it, numbered gotchas. Publish even if the mod is unfinished — a documented dead end saves hours.
- Also save the durable facts to agent memory (versions, paths, decisions).

### Artifacts the workflow produces (create them, do not skip)
```
<project>/
  MODDING_PLAN.md   # route decision + install facts (Stage 1)
  MODLOG.md         # running journal: paths, IDs, offsets, failures, next step
  REVERSE.md        # task list + findings + verification checklist (native/decomp work)
  labels.csv        # addr,name,comment - grows forever, all tools read it
  dead_ends.md      # what failed and why
  re_loop.sh        # overnight autonomous session driver
  re_loop_sessions/ # session logs
  docs/             # architecture_exe.md, architecture_web.md
  knowledge-note.md # the field note you publish at the end
```

### Sources for Part 1
- universal-modder (skills + `um` CLI + shared knowledge base): https://github.com/rehan-remade/universal-modder
- re-skill (`/re` skill for Claude Code, phases + loop + templates): https://github.com/vgrichina/re-skill
- How to decompile games with AI (6-step guide, matching vs reimplementation): https://aisignal.day/en/ai-game-decompile-howto-2026/
- The per-function agent loop in practice (scoring, permuter, prohibitions): https://blog.chrislewis.au/using-coding-agents-to-decompile-nintendo-64-games/
- Opus 5.5 SimCity 2000 run (27 sub-agents + Unicorn harness, 10,000 simulated days as referee): https://agentlenshq.com/blogs/2db441c4-272f-4e80-9801-ca8cd9621c98
- The wave itself, in the author's words ("all games are open source now"): https://nitter.cf/rehan_shei/article/2106548006350942533

---

## Part 2 — BL2VR recon (Stage 1 result, already done)

### Install facts — verified on this PC, 2026-10-12
- Install: `C:\Program Files (x86)\Steam\steamapps\common\Borderlands 2 VR`
- Exe: `Binaries\Win64\Borderlands2VR.exe` — 31.2 MB, **PE x64** (machine 0x8664), FileVersion `1.0.462.2101248`
- Steam AppID: **991260** (`steam_appid.txt` is already in `Binaries\Win64`)
- Engine: Unreal Engine 3, 64-bit, newer UE3 build than BL2
- No anti-cheat. Single-player. Safe to mod.
- `Documents\My Games\Borderlands 2 VR\WillowGame\Config` does **not** exist yet → the game has probably never been
  launched on this PC. Config files appear after the first launch. (Back them up then.)
- Game subfolders: `Binaries`, `DLC`, `Engine`, `Gearbox`, `WillowGame` (`WillowGame` has `Config`, `CookedPCConsole`,
  `Localization`, `Movies`, `Splash`)

### What is in `Binaries\Win64` (this decides our route)
```
openvr_api.dll        <- the game imports it  ->  OUR HOOK POINT (Route A)
steam_api64.dll       <- Steam, imported
EasyHook64.dll        <- a ready-made hooking library, shipped with the game (not in the import table)
binkw64.dll, d3d9.dll, d3d11.dll, dxgi.dll (imported), PhysX*, APEX*, nvtt_64.dll, fbxsdk*, cudart*
```
Import strings confirmed inside the exe: `openvr_api.dll`, `steam_api64.dll`, `d3d9.dll`, `d3d11.dll`, `dxgi.dll`.
No `SteamStub` string and hex-editing the exe works [fdh.one] → the exe is not DRM-encrypted, so on-disk patching is possible if ever needed.

### VR code lives inside the exe (UTF-16 strings found in `Borderlands2VR.exe`)
`AimControllerComponent`, `MotionControllerComponent`, `Protea_3DReticle`, `Behavior_GetVRHandInfo`,
`StereoLayerComponent`, `MotionController`, `VRHand`, `SteamVR.Settings`, `WindowMirror*`.
(ASCII search misses them; UE3 names are UTF-16 — search with `[Text.Encoding]::Unicode.GetString($bytes)`.)

### The class list (from fdh.one, 30 Apr 2021) — where the VR behaviour lives
Gearbox codename for the VR layer is **Protea**. VR-specific classes:
- `WillowGame.Protea_3DReticle`, `ProteaSeqCond_IsDirectMovement`, `ProteaSeqCond_SwitchVRPlatform`, `ProteaVehicleDefinition`
- `WillowGame.Behavior_GetVRHandInfo`, `Behavior_GetVRReticleInfo`, `Behavior_IsUsingMotionController`,
  `Behavior_SetVRMeleeDefinition`, `Behavior_ToggleWandMelee`, `Behavior_ToggleWandMeleeVRAnim`
- `WillowGame.ReticleDefinition`, `ScopeDefinition`, `TeleportStandIn`, `TeleportDefinition`, `SlomoDefinition`,
  `DirectMovementDefinition`, `DashDefinition`, `WillowScrollingListDataProviderVROptions`
- `Engine.AimControllerComponent`, `Engine.MotionControllerComponent`, `Engine.StereoLayerComponent`,
  `Engine.MultitouchGestureProcessor` (engine upgrade, not Gearbox)

**The critical sentence from fdh.one:** *"almost all UnrealScript classes/functions that were available in BL2 have
since been switched to using UE3's `dllbind(...)`"* — i.e. the gameplay/VR logic is native C++ inside the exe,
not UnrealScript. That is why data mods (ini, cooked UPK, text mods) cannot touch the hands.

### What IS already fixed without decompiling (do these now, they are free)
- HUD position/size: Nexus BL2 mod **433** "VR HUD" and **459** "QUEST HUD fix" (459 explicitly does NOT fix crosshairs)
- Hands/HUD layout: Nexus **571** "Hand mod UI fix" — replaces `willowgame\cookedpcconsole\startup.upk`
- Graphics: Nexus **526** "VR Max Graphics ini", **332** texture upscale
- **Crosshair/HUD alignment (the real fix):** SteamVR → Video → Per-Application Video Settings → Borderlands 2 VR →
  FOV → Advanced → top outer + inner `100%` → `90%` (from Dragonold103, Steam app 991260). Reticle can also be turned off in-game.
- Console (useful for experiments): hex-edit `say` → 3 spaces (UTF-16: `20 00 20 00 20 00`) in `Borderlands2VR.exe`;
  then `ConsoleKey=Tilde` in `...\WillowGame\Config\WillowInput.ini`; txt mods go into `Binaries`.

### Prior art / community demand
- Steam thread "Guns should be grippable with both hands" (app 991260) asks for exactly this.
- Reddit r/OculusQuest: *"the thing that stopped me from playing was the lack of 2 handed weapon aiming."*
- I could **not find** any mod that adds two-handed aiming (checked Steam, Nexus, Reddit, GitHub on 2026-10-12).
  That is thin evidence, so publish it as **"I couldn't find an existing one"** — never as "first" or
  "nobody has done this". The 8-year gap is explained by the game being native code with no data-mod tooling,
  not by the idea being impossible.

### Tooling status for the Borderlands family
- `unrealsdk` / bl-sdk (PythonSDK) flavours: **WILLOW** = BL1, BL1E, BL2, TPS, AoDK (UE3, 32- and 64-bit), OAK = BL3/Wonderlands,
  OAK2 = BL4. **BL2VR is not listed.** The SDK says some runtime adaptation between similar engine versions is supported,
  so it is worth a cheap test (Route B) — but it hooks *UnrealScript-level* functions, and BL2VR moved that logic to native.
  So even if it attaches, it may not reach the VR aim math.
- **Answered by the build:** the game talks to the **Oculus SDK**, via Virtual Desktop's runtime on this setup — not OpenVR (Appendix G).

---

## Part 3 — What "two-handed grip" actually means (the target, in plain terms)

> [!warning] Assumption — test it, do not believe it
> "The gun is attached to the right controller" is an **assumption**, not a verified fact. The game may compute aim and
> the reticle from a separate component (the exe does contain an `AimControllerComponent` string), or smooth/lag the
> pose itself before using it. Evening 1's logging proxy (Part 11) is what converts this assumption into a fact.

**Working model (what the fix assumes):** the gun's position and rotation follow the right controller's pose, which the
game reads from OpenVR every frame. The off-hand does nothing to the gun.

A "two-handed grip" means: **the gun's forward direction is the line from the right hand to the left hand.**
The right hand holds the grip (back of the gun), the left hand holds the foregrip (front of the gun), so:

```
forward  = normalize(left_hand_pos - right_hand_pos)     # where the gun should point
barrel   = rotate(q_raw, barrel_local)                   # where the gun points now (barrel_local starts as controller forward)
q_delta  = shortest_arc(barrel, forward)                 # turn the barrel onto the line; the controller's roll is kept
q_new    = q_delta * q_raw                               # the game's baked-in hand offset is untouched
```

Three behaviours that make it feel like a real gun:
1. **Gating** — only apply the solve when the off-hand is near the gun (distance right↔left under ~0.4 m,
   and the left hand is in front of the right hand). Otherwise the gun follows the right hand exactly as today.
2. **Smoothing** — slerp between raw rotation and solved rotation over ~0.1 s, so the gun does not snap.
3. **Per-weapon offset** — each gun has its own fixed "how it sits in the hand" offset. That offset is baked in the game.
   We do not remove it; we apply our correction as a **delta rotation on top of the raw pose** (`q_new = q_delta * q_raw`),
   so the baked offset stays and only the aim direction changes. The one unknown is the gun's barrel axis in controller
   space (`barrel_local`): it starts as the controller's forward axis and is calibrated per weapon (pitch/yaw in the INI).

Where the fix can live (from cheapest to most expensive):

| Route | Where the code runs | Touches the game exe? | Robust to game updates? |
|---|---|---|---|
| A | our DLL replacing `openvr_api.dll` | no | yes (OpenVR API is stable) |
| B | injected `unrealsdk` plugin | no | mostly |
| C | injected DLL hooking game functions | reads exe, patches memory | no (offsets change) |
| D | patched `Borderlands2VR.exe` on disk | yes | no |

**Why Route A is the smart first move:** we only need the *pose the game receives*, and that comes from a documented,
stable OpenVR API. Three surfaces can deliver it, so we hook all three (each is cheap):

| Source | Call the game makes | What we rewrite |
|---|---|---|
| `IVRSystem` | `GetDeviceToAbsoluteTrackingPose`, `GetControllerStateWithPose` | the `TrackedDevicePose_t` array / the pose inside the state struct |
| `IVRCompositor` | `WaitGetPoses`, `GetLastPoses` | the pose array returned for rendering |
| `IVRInput` (SteamVR Input) | `GetPoseActionDataForNextFrame`, `GetPoseActionDataRelativeToNow` (older revisions: `GetPoseActionData`) | `InputPoseActionData_t::pose` |

SteamVR Input is the one people forget: many VR titles read controller poses as **pose actions** rather than device poses.
If BL2VR does that, hooking only `IVRSystem`/`IVRCompositor` would change what is rendered but never the gun. We still
never need to know a single function inside `Borderlands2VR.exe`.

---

## Part 4 — Route A: `openvr_api.dll` proxy with pose override (do this first)

### 4.1 Concept
`Binaries\Win64\openvr_api.dll` is a thin shim that the game imports. We rename the original to `openvr_api_orig.dll`,
put our DLL in its place, forward every export to the original, and hook the vtables of the interfaces the game gets back
from `VR_GetGenericInterface`. In the pose getters we rewrite the right controller's tracking matrix before returning it.
The game then renders — and, if the Part 3 assumption holds, aims — the gun along our line.

**Hook all three pose surfaces, not just two.** The game can read the pose through any of these:

| Interface | How the game obtains it | Pose call(s) to hook |
|---|---|---|
| `IVRSystem` | `VR_GetGenericInterface(IVRSystem_Version)` | `GetDeviceToAbsoluteTrackingPose`, `GetControllerStateWithPose` |
| `IVRCompositor` | `VR_GetGenericInterface(IVRCompositor_Version)` | `WaitGetPoses`, `GetLastPoses` |
| `IVRInput` (SteamVR Input) | `VR_GetGenericInterface(IVRInput_Version)` | the pose-action getters — see 4.2 step 4 |

We hook by **interface version string**, never by a hard-coded game address. Each version has its own vtable layout, so
take the slot indices from the `openvr.h` revision that matches the version string the game actually requests (record the
strings in `MODLOG.md`); if the game asks for a version we have no slot table for, log it and pass it through unhooked.
Install the hook by **patching the slot in the returned object's vtable in place** (`VirtualProtect` the slot, write our
function pointer, keep the original pointer to call through). Do **not** hand back a copied vtable on a new wrapper object:
the original methods would then receive our object as `this` and crash. Our hooks are free functions whose first parameter
is `void* self`; they call the saved original with the same `self`. That is what keeps Route A stable across SteamVR and
game updates — it relies on the versioned public API, not on game addresses.

### 4.2 Steps
1. **Lab first.** Copy the whole `Borderlands 2 VR` folder to a lab path (or at least back up `Binaries\Win64\openvr_api.dll`
   to `openvr_api.dll.bak`). Never experiment on the only copy. Write the restore command into `MODLOG.md`.
2. **Enumerate exports:** `dumpbin /exports openvr_api.dll` (or PE-bear / `llvm-readobj`). Note the exact export names —
   forwarding must be complete or the game will not start.
3. **Build the proxy:**
   - DLL named `openvr_api.dll`; every export forwarded to `openvr_api_orig.dll` (a `.def` file with
     `EXPORTS name=openvr_api_orig.name`) **except `VR_GetGenericInterface`**, which we implement ourselves — a forwarded
     export cannot be intercepted. Our version loads `openvr_api_orig.dll` lazily on its first call (`LoadLibrary` +
     `GetProcAddress`), calls the real function, then hooks the returned interface (step 4). If the export dump shows the
     game also calls `VR_InitInternal` / `VR_InitInternal2`, wrap those as pass-throughs too so we can log them.
   - Keep `DllMain` nearly empty: no thread, no `LoadLibrary` under the loader lock. (The `.def` forwarders already make the
     loader pull in `openvr_api_orig.dll` at startup.)
   - **Build environment:** MSVC from Visual Studio 2026 Community + CMake, **x64 only** (the game is 64-bit; a 32-bit DLL
     will not load). Do not build from a normal terminal: open **"x64 Native Tools Command Prompt for VS"** or
     **"Developer PowerShell for VS"** from the Start menu, which sets up the 64-bit compiler. Tell the agent this up front
     so it generates an x64 MSVC/CMake project and does not guess a toolchain.
4. **Find the interfaces and hook the pose calls.** In our `VR_GetGenericInterface`, call the real one, look at the version
   string, and patch the slots we need in the returned object's vtable in place (Part 4.1 — no copied vtable on a wrapper
   object). Relevant slots:
   - `IVRSystem::GetDeviceToAbsoluteTrackingPose` — post-process the returned `TrackedDevicePose_t` array
   - `IVRSystem::GetControllerStateWithPose`
   - `IVRCompositor::WaitGetPoses` — many engines track through this one. It fills two arrays (render poses and game
     poses): rewrite both consistently, and dedupe per frame.
   - `IVRInput::GetPoseActionDataForNextFrame` / `IVRInput::GetPoseActionDataRelativeToNow` — the SteamVR Input path.
     Older interface revisions expose a single `GetPoseActionData` instead; hook whichever slots exist in the layout for
     the version string the game requested. The member to rewrite is `InputPoseActionData_t::pose` (a `TrackedDevicePose_t`).
   - **`IVRInput` specifics.** Each call returns the pose of ONE action, so: (a) also hook `IVRInput::GetActionHandle` and
     record name → handle (the hand action names come from the game's action manifest, so log them rather than guess);
     (b) a right-hand call does not carry the left pose — keep the newest left-hand pose from the left action's own call
     (one frame old if the left call comes later), or call the saved original getter for the left handle yourself;
     (c) if the game draws the hands from skeletal actions (`GetSkeletalActionData` / `GetSkeletalBoneData`), log those
     calls too — that is a fourth surface, and it decides how the hand meshes behave.
5. **Evening 1 = logging only.** Before any rewrite: log every call from all three interfaces (call name + frame counter +
   the left/right device indices) for ~60 seconds of play, flushed to a file next to the DLL. This one log answers
   "which pose API does this game use" — the question the whole plan hangs on.
   Also log: the interface version strings the game requests, each pose's `bPoseIsValid` and position (so we can see the
   controllers move), and the `GetActionHandle` names. Cap the volume (e.g. every call for the first 2 seconds, then
   per-second counters) so the log stays readable and the game does not stutter. Add an INI switch `enable=0` that makes
   the proxy a pure pass-through, so a bad build can be disabled without restoring files.
6. **Identify the right controller** with `GetTrackedDeviceClass` +
   `GetStringTrackedDeviceProperty(Prop_ControllerRoleHint_String)` (values `left` / `right`). On the `IVRInput` path the
   device comes from the action origin, so map action handle → device index once at startup and cache it.
7. **Write the pose override** (math from Part 3). Keep the solve a **pure function with no OpenVR types**, so the offline
   replay test (Part 8) can run it without a headset. Sketch:
   ```cpp
   struct GripState { bool engaged = false; Quat delta = Quat::identity(); };

   Quat solve_two_hand(const Vec3& posR, const Quat& qRaw, const Vec3& posL,
                       const Vec3& headFwd, float dt, GripState& st) {
       Vec3 fwd = normalize(posL - posR);                       // where the gun should point
       float d  = length(posL - posR);

       // gating with hysteresis: engage under ~0.35 m, release over ~0.45 m (no flicker at the edge)
       bool in_range = st.engaged ? d < cfg.release_dist : d < cfg.engage_dist;
       bool aiming   = dot(fwd, headFwd) > 0.2f;                // left hand is out in front
       st.engaged = in_range && aiming;

       Vec3 barrel = rotate(qRaw, cfg.barrel_local);            // controller forward by default; per weapon in the INI
       Quat target = st.engaged ? shortest_arc(barrel, fwd)     // handle the antiparallel case explicitly
                                : Quat::identity();

       // time-based smoothing: the same ~0.1 s at any frame rate, on engage and on release
       float a = 1.0f - expf(-dt / cfg.blend_seconds);
       st.delta = slerp(st.delta, target, a);
       return st.delta * qRaw;                                  // position is untouched = right hand
   }
   ```
   **Adapters, one per pose path.** On the array paths (`WaitGetPoses`, `GetDeviceToAbsoluteTrackingPose`) both hands are in
   the same array: read R and L, call `solve_two_hand`, write the rotation back into R's matrix. On the `IVRInput` path
   use the cached or fetched left pose (step 4). Two rules: **advance `GripState` once per frame, not once per call**
   (several surfaces and two compositor arrays will all ask — compute once, cache the result, apply it everywhere), and take
   `dt` from a high-resolution timer or the frame counter, not from the call.
8. **Config file** next to the DLL (`bl2vr_grip.ini`): `enable` (0 = pure pass-through), `dominant_hand`, `engage_dist` /
   `release_dist`, `blend_seconds`, and `barrel_local` pitch/yaw per weapon type (the correct value differs per gun).
   Read once at load, allow a hot-reload key.
9. **Verify in game** (Stage 7): record a clip with the off-hand on the foregrip; the gun must point at the target.
   Test cases: hands apart (vanilla behaviour), hand near (two-hand line), weapon switch, scoped aim, melee,
   vehicles, menus. Screenshot or trace each case.
   **Expectation check:** these cases (plus per-weapon offsets and the hand mesh) are the days of work, not the core.
   Step 7 producing a visibly correct two-hand aim is the milestone; the rest is polish.

### 4.3 Known risks
- **The game may use none of the three pose paths we hook** (or only `IVRInput`) → hook all three and let the Evening 1
  log decide. This is the biggest single risk in Route A, and the cheapest to eliminate.
- **The gun may not follow the pose we rewrite** (aim computed elsewhere) → see the Part 3 assumption; the same log tells us.
- **Visuals vs. shots.** The gun may rotate on screen while shots still leave along the original aim (if aim is computed
  from another source). The Evening 1 canary test checks this explicitly (Part 11 step 7).
- **`openvr_api.dll` may not be Valve's original** (for example if OpenComposite or another replacement is installed): the
  backup and export dump in Part 11 Step 1 will show it. Do not proxy somebody else's proxy blindly.
- **The rendered right hand rotates too** (hand mesh sits on the same controller). Mostly hidden by the gun.
  Later fix: nudge the hand mesh offset, or accept it.
- **Steam verify / game update overwrites `openvr_api.dll`** → keep a one-click installer script that re-applies the proxy.
- **Latency**: poses must be modified on the frame they are produced, or the gun feels rubbery.
- **Other consumers** (mirror window, spectator) also see the modified pose — cosmetic only.

### 4.4 Why this is a real fix, not a hack
If the Part 3 assumption holds (the gun's aim is driven by the right controller's pose), then changing the pose the game
consumes changes the gun's aim — with no game code touched and nothing patched in memory.
Same class of tool as `OpenVR-InputEmulator` / "motion compensation" for gun stocks
(fork that tracks newer SteamVR: https://github.com/Erimelowo/OpenVR-InputEmulator-Fixed), but implemented at the app
boundary so it does not fight SteamVR versions.

---

## Part 5 — Route B: cheap test — does `unrealsdk` attach to BL2VR?

**Cost: one evening. Do it before Route C, because if it works it saves weeks.**

1. Clone `https://github.com/bl-sdk/unrealsdk` (recursive) and build the **WILLOW** flavour for **x64**
   (check `CMakePresets.json` in the repo for the UE3 x64 presets — the UE4/UE5 ones are named like `msvc-ue4-x64-debug`).
2. The SDK needs a game-specific **plugin loader** to get `unrealsdk.dll` into the process
   (bl-sdk ships one per supported game, e.g. through PythonSDK; BL2VR has none, so we would write a tiny proxy DLL:
   `dxgi.dll` or `d3d11.dll` are both imported by the exe [verified], so either is a valid proxy name).
3. Put `steam_appid.txt` (already present, content `991260`) next to the exe so Steam does not restart the app and kill the debugger.
4. Test 1: does it inject and initialise at all? Test 2: can it find a known class, e.g. log `WillowGame.WillowPlayerController`?
   Test 3: can it hook an UnrealScript function that runs every frame and read the player pawn?
5. **Decision point:**
   - Works → we can read/modify Unreal objects. Try to find the weapon actor / motion controller components and set their
     transform from a per-frame hook. Even if the game's native code overwrites it afterwards, we learn the object layout.
   - Fails → note it in `dead_ends.md` and move on. (Expected, because BL2VR replaced most UnrealScript with `dllbind` native code.)

---

## Part 6 — Route C: the real native fix (the "when I have money + better model" job)

This is the full Stage 1→9 workflow applied to the inside of `Borderlands2VR.exe`.

### 6.1 What we are looking for (targets, in order)
1. **The weapon transform builder.** The function that takes the right controller's pose (from
   `Engine.MotionControllerComponent` / `Engine.AimControllerComponent`) and produces the weapon's world transform.
   Candidates to locate first: `WillowGame.Behavior_GetVRHandInfo`, `Behavior_GetVRReticleInfo`,
   `Behavior_IsUsingMotionController`, and the `Protea_*` layer. The class names are in the exe as UTF-16 strings
   [verified] — that gives us xrefs to the code that registers them (UE3 registers native classes by name at startup),
   which is the cheapest entry point into the native code.
2. **The component tick** that copies controller pose → weapon component. A mid-function hook here is enough to replace
   the rotation input.
3. **The off-hand pose** the game already tracks (it renders the left hand, so it has the data).

### 6.2 Tooling to install for this route
- **Ghidra** (free) + **GhidraMCP** or **pyghidra-mcp** so an agent can decompile, rename, retype, follow xrefs.
  (IDA 9.4+ with the official Hex-Rays IDA MCP is better if money allows; IDA is the paid option.)
- **x64dbg** (+ x64dbg-mcp, bound to 127.0.0.1) for breakpoints and tracing.
- **Cheat Engine** (+ the CheatEngine MCP bridge) for "what writes this value" → struct → owner.
- **Frida** (+ frida-mcp) to hook and log function arguments in JavaScript without rebuilding.
- **ReClass.NET** to rebuild the struct layout from live memory.
- **RenderDoc** if we ever need to find the weapon's matrices in constant buffers.
- **MinHook** or **SafetyHook** (detours) and/or the **LINK/2012 `injector`** header for static-address patches
  (that is the pattern behind most NFS/ThirteenAG ASI mods: `WriteMemory`, `MakeNOP`, `MakeJMP`, naked code caves with
  a saved exit address, settings read from an INI).
- **reverify** to check every offset claim the model makes (*claim* from a blog post: "caught 100% of hallucinated claims"
  on real Windows binaries — treat as unverified until we reproduce it on our own target).
- Build chain: **MSVC from Visual Studio 2026 Community, workload "Desktop development with C++", driven by CMake**
  (see "Build environment" in Part 4.2). Everything we build must be **x64**; build from an x64 developer prompt.

### 6.3 Phases (each phase ends with something verifiable)
| Phase | Deliverable | Verification |
|---|---|---|
| C1. Recon done | `MODDING_PLAN.md` + `REVERSE.md` with the binary identification table | exe version, PE x64, imports, string hits logged |
| C2. Entry point | find the code that registers `Protea_*` / `MotionControllerComponent` class names | Ghidra xref from the UTF-16 string to the registering function; name it in `labels.csv` |
| C3. Trace the pose | find where the controller pose enters the weapon transform | breakpoint/log: change the controller pose in-game, watch which function's arguments change (Cheat Engine "find what accesses") |
| C4. First hook | our DLL rewrites the rotation input of that function | in-game: gun points along the two-hand line (this is the same visible result as Route A) |
| C5. Off-hand data | locate the off-hand pose source the game already uses | log both hand transforms while moving both controllers |
| C6. Two-hand solve | full gating + smoothing + per-weapon offsets, done in native code | feel test + recorded clips |
| C7. Robustness | signature scan instead of raw addresses; log clearly when a pattern is missing | restart 10×, game update simulation (restore a backup exe), no crashes |
| C8. Field note | knowledge-base note + installable package | someone else installs it from the README |

### 6.4 The hook design (what the DLL actually does)
- Inject with a **proxy DLL** (`dxgi.dll` or `d3d11.dll`, both imported [verified]) or the Ultimate ASI Loader.
  Allocate almost nothing in `DllMain`; start a thread, wait for the engine, then install hooks.
- Hook with **MinHook/SafetyHook**: either detour the whole transform function (call the original through the trampoline,
  then fix the result) or a **mid-hook** that only rewrites one register/one matrix at one instruction.
- **Never hard-code absolute addresses** — ASLR is on. Compute from the module base, and prefer **byte-pattern (AOB) scans**
  with wildcards, with a clear log line when a pattern is not found.
- Keep the game's own thread affinity: do the transform work on the game's thread inside the hook, not on our thread.
- Log to a file with flushes; install an unhandled-exception filter that writes a minidump.
- Config in an INI next to the DLL (enable, gating distance, blend time, per-weapon offsets), read relative to the DLL path.

### 6.5 Why Route C is still worth having even if Route A works
Route A changes the *input pose* — the gun follows the hands, but the game still thinks the gun is one-handed.
Route C changes the *weapon's own transform*, so we can also fix:
- the hand meshes (both hands visually on the weapon),
- per-weapon foregrip points (the correct place for the off-hand on each gun),
- the reticle/aim behaviour (`Protea_3DReticle`, `ReticleDefinition`) and the melee/scope states,
- and it becomes a base for other VR fixes (holsters, physical reloads, off-hand grenades).
That is the "real mod" version. Route A is the 20% that gets 80% of the feel.

### 6.6 Update fragility (be honest about it)
Native hooks break on every game patch. Mitigations: pin the supported build (`1.0.462.2101248`), keep AOB signatures,
ship a version check that refuses to load on an unknown build instead of crashing, and keep the Route A proxy alive as the
update-proof fallback.

---

## Part 7 — Route D: patching the exe on disk (last resort)

Only if injection is impossible. The exe is not DRM-encrypted (the console hex-edit proves it) [fdh.one],
so a byte patch is technically possible. Steps: find the instruction that sets the weapon rotation → replace it with a
jump to a code cave → write our own math in the cave → jump back to the saved exit address
(the LINK/2012 `injector` pattern: `MakeJMP` + naked function + recorded cave-exit).

Why it is last: no config, no logging, no toggles, breaks on every update, and one wrong byte = the game will not start.
Keep a `.bak` of the exe and a restore script.

---

## Part 8 — Verification harness (the "referee" for this project)

Without a referee the agent guesses. Build these, in this order:

1. **Pose trace recorder (inside Route A's proxy).** Log per frame: both controller poses, head pose, our computed
   two-hand rotation, and the final pose we returned. CSV out. This is the ground truth for every later claim.
2. **Replay test.** Re-run recorded traces through the math offline (no headset) and assert the output:
   hands apart → output == input (vanilla), hands together → output forward == hand-to-hand line.
   That gives a numeric score the agent can loop on, exactly like the community's trace-replay oracle.
3. **In-game check list** (must pass before calling it done):
   - hands apart, gun behaves exactly like vanilla (no drift, no snap)
   - off-hand near the foregrip → gun points along the hands line
   - aim down sights / scope, then release
   - switch weapon, reload, melee
   - vehicle, menu, inventory, pause
   - 30 minutes of play with no crash, no rubber-banding, no jitter
4. **Screenshot pairs / clips** for each case, saved next to the code. `um win record` or OBS for the clip.
5. **Regression rule:** every confirmed behaviour gets one line in `REVERSE.md` / `MODLOG.md` with the trace file that proves it.

---

## Part 9 — Effort, cost, and what to install when money arrives

### Effort estimates (honest)
| Work | Time | Notes |
|---|---|---|
| **Evening 1 — logging-only proxy** (forwards everything, logs every OpenVR call) | 3–4 hours | answers the only question that blocks everything else: which pose API does the game read? |
| Route A visible core (gun follows the two-hand line) | +1–3 evenings | biggest unknowns: which pose API the game uses, and the right per-weapon offset |
| Route A polish (gating, smoothing, installer) | 1–2 weekends | feel work, needs the headset on |
| Per-weapon offsets / calibration table | days, spread over sessions | every weapon type sits differently in the hand |
| Scoped aiming, melee, vehicles, hand mesh rotating | days each, open-ended | these are the "where the days go" items, not the core |
| Route B test (unrealsdk on BL2VR) | 1 evening | yes/no answer only |
| Route C (native hook, full mod) | 2–6 weeks part-time | mostly Ghidra time + per-weapon offsets |
| Field note + release | 1 evening | needed so the next person (or future you) does not redo it |

**Read the table like this:** "1–3 evenings" buys a demo that visibly works. Everything after it is a separate,
open-ended job. Plan the first one, expect the rest.

### What to install now (all free)
- Ghidra, x64dbg, Cheat Engine, ReClass.NET, Frida, RenderDoc, PE-bear, HxD (or hexed.it)
- **Visual Studio 2026 Community** with the **"Desktop development with C++"** workload (Community has no separate "Build Tools"
  option — this workload is the same toolchain). In the installer's details panel make sure these are ticked:
  *MSVC build tools for x64/x86 (latest)*, *Windows 11 SDK*, *C++ CMake tools for Windows* (that gives you CMake; no separate install).
  Git: tick "Git for Windows" under Individual components, or install it separately.
- MinHook or SafetyHook sources (only if we detour functions instead of patching vtable slots)
- `universal-modder` skills + `um` CLI (`uv tool install git+https://github.com/rehan-remade/universal-modder`)
- re-skill for the loop files, reverify for claim checking
- Obsidian note for the field note; keep `MODLOG.md` open while working

### What money buys later
- **A stronger model + more compute** for the agent (the difference between "explore the exe" and "actually finish Route C").
  The wave's numbers (*claim*, from blog posts): Opus 5.5 + sub-agents + a referee is what made overnight decompiles
  possible; plausible, unverified, but the same shape works here.
- **IDA Pro** (optional) — better decompiler than Ghidra for Windows PE, and the official IDA MCP.
- **A dedicated lab copy of the game on a second drive** so experiments never risk the playable install.

### Model/agent setup that matches the workflow
- One agent per subsystem (agent A: proxy DLL + pose override; agent B: Ghidra recon of the VR classes; agent C: harness/traces).
- Give each agent: the `MODLOG.md`, the `REVERSE.md` task list, the tool list, and the **referee command**
  (`run_trace_tests.ps1`) so it can score itself.
- Cap the loop: max N tool calls per task, then write down what it learned.

---

## Part 10 — Risks, limits, and the rules we follow

### Technical risks
1. **The game may not use the pose API we hook.** There are three candidate paths (`IVRSystem`, `IVRCompositor`, and
   `IVRInput` pose actions). Mitigation: hook all three and log which one the game actually calls on the first frames —
   the log answers it immediately, which is exactly why Evening 1 is logging-only.
2. **The "gun follows the right controller pose" assumption may be wrong.** Aim and reticle may be computed in a separate
   component (`AimControllerComponent`), or the game may smooth the pose itself. Mitigation: if the logged calls look right
   but the gun visibly does not react to the pose we rewrite — **or it reacts on screen while shots still leave along the
   original aim** — that is the signal to stop and move to Route C.
3. **The right hand mesh rotates with the gun.** Cosmetic; can be fixed later (Route C or a hand-mesh offset).
4. **Per-weapon offsets.** Each gun sits differently in the hand; needs a small calibration screen or a per-weapon table.
   Start with one weapon, add more later.
5. **Scope creep past "the gun aims right".** Scoped aiming, melee, vehicles and the hand mesh are each their own job —
   hence the split between "visible core" (evenings) and "shippable" (weeks) in Part 9.
6. **Which VR API the game actually uses.** This plan assumed OpenVR. In practice BL2VR uses the **Oculus SDK (LibOVR)**, reached through
   Virtual Desktop's runtime on this setup, so the working hook is on `ovr_GetTrackingState` (Appendix G). Lesson: log first and find
   out which API the game calls before designing the hook.
7. **Steam overwrites our DLL** on verify/update. Installer script + README note.
8. **"I could not find an existing mod" is not "impossible", and not "first".** Absence of evidence is a gap, not a wall —
   and not a claim about being the first.

### Legal / ethical lines (same rules as the community workflow)
- Mod your **own copy** of the game. Single-player only (BL2VR is single-player).
- **Never ship game files or decompiled code.** Ship our own DLL, our own math, our own config.
- Never bypass DRM, ownership checks or anti-cheat (not applicable here — there is none — but the rule stays).
- Disclose AI use in the README.
- Keep decompiled output and extracted assets **outside** the game folder and out of any public repo.
- Reverse engineering for interoperability is a grey area in some jurisdictions; publishing a patch that only runs on the
  user's own game files is the safest shape (same shape as every ASI/DLL-proxy mod).

---

## Part 11 — Next actions (do these in order)

> **Status, 2026-10-13:** Steps 0–2 are done in substance (Route A validated end to end); Step 3 (usable + tuned) is in progress.
> The text below is kept as the original plan.
>
> **Do this one next, without waiting for anything.** It costs no money, needs no better model and no Ghidra, and it
> answers the question the whole plan depends on: which pose API does the game read, and does the gun follow it?

**Step 0 — Preflight (15 minutes, before any code)**
0. Answer the runtime question: do you play BL2VR through **SteamVR** (Quest via Link / Virtual Desktop / Air Link normally
   still runs the game through SteamVR) or through something else? Route A needs the game to talk to SteamVR's OpenVR. If
   you use OpenComposite or anything else that replaces `openvr_api.dll`, say so now and tell the agent — the file in the
   game folder is then not Valve's. Also confirm SteamVR is installed and your headset works with it.

**Step 1 — Evening 1: the logging-only proxy (3–4 hours)**
1. Launch the game once, then back up `Documents\My Games\Borderlands 2 VR\WillowGame\Config` and
   `Binaries\Win64\openvr_api.dll` to `openvr_api.dll.bak`. Write the restore command into `MODLOG.md`.
2. `dumpbin /exports openvr_api.dll` → save the export list. That is the whole API surface we must forward.
3. Build a bare proxy DLL that forwards everything and **only logs** (no rewrite yet). Build **x64** with MSVC (VS 2026
   Community) + CMake from the "x64 Native Tools Command Prompt for VS" / "Developer PowerShell for VS" (Part 4.2 step 3).
   Confirm the game still starts and reaches the main menu with our DLL in place.
4. Log every pose call from **all three** interfaces — `IVRSystem`, `IVRCompositor`, `IVRInput` — with frame counters and
   the left/right device indices. Play ~60 seconds, then read the log and write the answer into `MODLOG.md`:
   *which interface does the game actually use, and how often?* (What to log: Part 4.2 step 5.)
5. Decide from the log:
   - uses `IVRSystem` / `IVRCompositor` → continue as planned;
   - uses `IVRInput` pose actions → hook those instead (Part 4.2 step 4);
   - uses something else entirely → that is Route C territory, and you learned it in one evening instead of one month.

**Step 2 — Evening 1, continued: first visible result (only if step 5 says "continue")**
6. Add the vtable hook on the pose getter that the log named; log both controller poses and their roles.
7. **Canary first, two-hand math second.** First rewrite the right controller's pose with something trivially visible
   (for example a fixed 30° yaw, always on). In game, check three things: (a) does the gun visibly rotate? (b) do shots and
   the reticle follow the rotated gun, or do they still go along the original aim? (c) is any lag or smoothing visible?
   Only if (a) and (b) pass, swap the canary for the two-hand rotation (Part 3 math, gating disabled, always on) and look
   at the gun.
   Gun follows the two-hand line and shots follow the gun → **Route A is validated end to end.**
   Log looks right but the gun does not move, or only looks like it moves → the Part 3 assumption is wrong → go to Route C.

**Step 3 — Evening 2–3: make it usable on one weapon**
8. Add gating + slerp + INI offsets; calibrate on one weapon.
9. Run the Part 8 check list and record a clip.
10. Write the field note (Part 1, Stage 9) — saying "I couldn't find an existing mod", never "first".

**Step 4 — Free polish while the proxy is in place**
11. Apply the free fixes from Part 2 (HUD mods + SteamVR FOV 90%) so the game is comfortable to test in.
12. Runtime question: already answered in Step 0. If the answer was "not SteamVR", Route A may not apply — re-read Part 4.3
    before going further.
13. Copy `MODDING_PLAN.md` (template below) into `C:\Users\Rimach\Desktop\BL2VR-grip\` and start `MODLOG.md` there.

**Step 5 — Later, when there is money for a better model**
14. Route B test (one evening, yes/no).
15. Route C: Ghidra + MCP, follow the `Protea_*` strings to the native weapon transform, then decide if the full mod is worth it.

---

## Appendix A — `MODDING_PLAN.md` template (Stage 1 output, for this project)
```markdown
# Borderlands 2 VR modding plan
- Install: C:\Program Files (x86)\Steam\steamapps\common\Borderlands 2 VR (Steam 991260), version 1.0.462.2101248
- Engine: Unreal Engine 3, 64-bit. Code: native (dllbind), no anti-cheat, single-player
- Saves/Config: %USERPROFILE%\Documents\My Games\Borderlands 2 VR\WillowGame\Config (created on first launch)
- Community route: no loader exists for BL2VR (unrealsdk supports BL1/BL2/TPS/AoDK, not VR)
- Chosen route for "two-handed grip": proxy openvr_api.dll + pose override (no game code touched)
- Fallbacks: (B) unrealsdk injection test, (C) native hook into the weapon transform, (D) exe patch
- Lab plan: back up openvr_api.dll + Config folder, experiment on a copied game folder
- Toolchain: MSVC from Visual Studio 2026 Community ("Desktop development with C++"), CMake, x64 only; build from an x64 developer prompt
- Unknowns to resolve first: which pose API the game reads; runtime (SteamVR vs Oculus); per-weapon offsets
```

## Appendix B — Suggested folder layout
```
C:\Users\Rimach\Desktop\BL2VR-grip\
  plan.md                (this file, or a copy)
  MODDING_PLAN.md
  MODLOG.md
  REVERSE.md
  labels.csv
  dead_ends.md
  re_loop.sh
  openvr_proxy\          (C++ project: proxy DLL + pose override + ini)
  harness\               (trace recorder + replay tests + run_trace_tests.ps1)
  traces\                (CSV pose traces)
  clips\                 (verification recordings)
  install.ps1            (backs up + installs the proxy, and restores)
```

## Appendix C — Sources used for this plan
- fdh.one BL2VR page (UE3, dllbind, class list, console hex edit): https://fdh.one/Borderlands-2-VR/
- unrealsdk flavours and injection: https://github.com/bl-sdk/unrealsdk
- universal-modder (workflow, skills, knowledge base, native playbook): https://github.com/rehan-remade/universal-modder
- re-skill (phases, loop, templates): https://github.com/vgrichina/re-skill
- AI decompilation workflow guide: https://aisignal.day/en/ai-game-decompile-howto-2026/
- Agent decompilation in practice: https://blog.chrislewis.au/using-coding-agents-to-decompile-nintendo-64-games/
- Opus 5.5 SimCity 2000 (sub-agents + emulator referee): https://agentlenshq.com/blogs/2db441c4-272f-4e80-9801-ca8cd9621c98
- OpenVR InputEmulator (pose manipulation prior art): https://github.com/matzman666/OpenVR-InputEmulator
  and the maintained fork: https://github.com/Erimelowo/OpenVR-InputEmulator-Fixed
- OpenVR `IVRInput` pose getters — names verified 2026-10-12 against `headers/openvr.h` (ValveSoftware/openvr) through the
  `openvr-sys2` binding reference (docs.rs) and Valve's own Proton VR client proxy (`cppIVRInput_IVRInput_011.cpp`):
  `GetPoseActionDataForNextFrame`, `GetPoseActionDataRelativeToNow`, and
  `struct InputPoseActionData_t { bool bActive; VRInputValueHandle_t activeOrigin; TrackedDevicePose_t pose; }`.
- Verified on this PC (2026-10-12): install path, exe version/architecture/imports, UTF-16 VR class strings,
  `openvr_api.dll` present, `EasyHook64.dll` present, no `SteamStub` string, config folder missing.

## Appendix D — Review corrections (2026-10-12)

A review pass over this plan found five things to fix, plus one environment update (row 6) and one design-review pass over Route A (row 7). All are applied above; kept here so the reasoning is not lost.

| # | Correction | Where it landed |
|---|---|---|
| 1 | Route A must also cover **SteamVR Input (`IVRInput` pose actions)**. Many VR titles read controller poses as *pose actions*, not device poses — hooking only `IVRSystem`/`IVRCompositor` would then change what is rendered but never the gun. | Part 0, Part 3, Part 4.1–4.3, Part 10, Part 11 (Evening 1 logs all three paths) |
| 2 | "The gun is parented to the right controller" is an **assumption**, not a fact. Aim/reticle may live in a separate component, or the game may smooth the pose itself. | Part 3 callout, Part 0 assumptions, Part 10 risk 2 |
| 3 | "1–3 evenings" is realistic only for the **visible core**; offsets, scopes, melee, vehicles and the hand mesh are where the days go. | Part 0, Part 9 |
| 4 | Part 1's workflow numbers are **blog folklore**, not measurements. | Part 1 evidence note, Part 6 (`reverify`), Part 9 (model claims) |
| 5 | Write **"I couldn't find one"**, never "first". | Part 2, Part 10 risk 8 |
| 6 | **Build environment:** the machine now has Visual Studio 2026 Community with the "Desktop development with C++" workload (replaces the old "no Visual Studio / MinGW" note). Build **x64 only**, from an x64 developer prompt. | Part 4.2 step 3, Part 6.2, Part 9, Part 11 step 3, Appendix A |
| 7 | **Route A design review.** (a) Handing back a copied vtable on a wrapper object gives the original methods the wrong `this` — now patch slots in place. (b) `VR_GetGenericInterface` must be implemented, not forwarded, and `DllMain` must not spawn a thread or load libraries. (c) `IVRInput` returns one action per call: the left pose has to be cached or fetched, `GetActionHandle` names logged, the skeletal path logged. (d) The code sketch contradicted Part 3 (absolute rotation vs "delta on top of raw"): now a pure delta-based solve with a calibrated `barrel_local`, gating hysteresis, time-based smoothing, state advanced once per frame. (e) Evening 1 now runs a **canary test** (does the gun rotate, and do shots follow it?) before the two-hand math. (f) The runtime question moved to a Step 0 preflight. (g) Small fixes: Appendix A / Stage 9 cross-references, "his PC" wording. | Part 2, Part 3, Part 4.1–4.3, Part 10 risk 2, Part 11 Steps 0–3 |

**What the review did not change:** the route ladder itself (A → B → C → D), the two-hand math, the gating/smoothing
design, or the decision to start at the OpenVR boundary. Those survived the pass.

**Agreed next move (from the review):** do **Evening 1 now** — a logging-only proxy. It needs no money, no better model
and no Ghidra, and it answers the biggest open question (which pose API the game reads) in a few hours.


---

## Appendix E — Tuning guide for the smoothing config (rewritten 2026-10-13 for the follower + catch-up filter)

The filter is a critically damped follower. Its stiffness is `1 + beta*speed + catchup*lag`, applied to the smooth time
`1/(2π·cutoff)`. Measured offline on the real `filters.h` (`grip_fix/filter_bench.cpp`, 90 Hz, two calls per frame).

### E.1 What the numbers mean
| Cutoff | smoothTime | Lag while moving at constant speed | Time to settle after a stop (to ~1%) |
|---|---|---|---|
| 0.3 Hz | 530 ms | up to 0.53 s of motion | ~1.7 s |
| 0.5 Hz | 320 ms | up to 0.32 s | ~1.0 s |
| 1.5 Hz | 106 ms | up to 0.11 s | ~0.35 s |
| 4 Hz | 40 ms | up to 0.04 s | ~0.13 s |

Those are the numbers with `beta` and `catchup` at 0. A low cutoff alone is exactly what made the gun "float": at the old
two-hand values the gun was **14° behind at 60°/s and took 0.6 s to settle**. `beta` (speed) only helps while moving fast;
`catchup` (lag) is what removes the long tail, because the lag itself raises the stiffness.

### E.2 Measured before and after (rotation, degrees)
| Profile | Lag at 15°/s | Lag at 60°/s | Settle to 0.5° after a stop | 2° correction, 90% | 8 Hz tremor left |
|---|---|---|---|---|---|
| base (unchanged) | 1.2 | 2.6 | 0.06 s | 0.16 s | 13% |
| trigger, old | 5.9 | 19.1 | 0.85 s | 0.81 s | 0.9% |
| trigger, new | 1.6 | 3.3 | 0.08 s | 0.25 s | 1.3% |
| two-hand, old | 5.1 | 14.4 | 0.62 s | 0.75 s | 1.0% |
| two-hand, new | 1.6 | 3.0 | 0.07 s | 0.23 s | 1.3% |

Position (centimetres) shows the same pattern: two-hand lag at 60 cm/s drops from 7.2 cm to 2.6 cm, the trigger profile from 11.7 cm to 2.9 cm.

### E.3 Symptom to knob
| Symptom | Try |
|---|---|
| Gun still floats / lags after you stop | raise `catchup_rot` / `catchup_pos` (e.g. 120 to 200) |
| Gun twitchy while holding still | lower `catchup_*`, or lower the profile's min cutoff |
| Late when tracking a moving target | raise `beta` for that profile, or raise `catchup_*` |
| Not steady enough at rest | lower the profile's min cutoff (`twohand_min_cutoff*`, or raise `zoom_mult*`) |
| Trigger-held aim does not feel smoother than normal | check the log (E.4) — the gate may not be seeing the trigger |

### E.4 Trigger-gate diagnostics
The hook now writes one log line each time the gun hand's trigger gate changes state, for example
`trigger gate -> HELD  trig=0.93 ... poll_rc=0 gun_hand=left ...`. If holding the trigger produces no such line, or `trig` stays 0.000,
or `poll_rc` is not 0, the runtime is not returning the trigger value and the smoothing profile is never selected. If the line appears
and `x8.0/5.0` is shown, the gate works and only the profile values need tuning.

### E.5 Still to check by feel
- `twohand_blend` units (the value 1.0 may be a 0-1 mix, not seconds).
- `twohand_min_dist=0.12`: raising it steadies the aim line but can stop the two-hand mode engaging on small guns.
- `smooth_head=0` and `predict_ms=0` stay as they are.

---

## Appendix F — Config snapshot (`bl2vr_grip.ini`, 2026-10-13, with catch-up)

The file is delivered separately as `grip_fix/bl2vr_grip.ini`. Key values: base unchanged (`min_cutoff=4.0`, `beta=2.0`, `min_cutoff_rot=1.5`,
`beta_rot=1.2`); trigger block `zoom_mult=8.0`, `zoom_mult_rot=5.0`; two-hand block `twohand_min_cutoff=0.5`, `twohand_beta=1.5`,
`twohand_min_cutoff_rot=0.3`, `twohand_beta_rot=1.5`; `catchup_pos=150`, `catchup_rot=120`; `gun_hand=left`, `twohand_button=grip`.

---

## Appendix G — What was actually built (from the project zip, 2026-10-13)

- Injection: a proxy `openvr_api.dll` is placed in `Binaries\Win64`; it only provides a place to run our code inside the game.
- The pose hook: our code waits for Virtual Desktop's Oculus runtime, then installs an inline hook (MinHook) on **`ovr_GetTrackingState`**
  and rewrites `HandPoses[]` before the game reads them. The trigger and grip are read by polling `ovr_GetInputState` from our own thread.
- Two-hand solve: `forward = normalize(offhand_pos - gunhand_pos)`; the gun hand's orientation gets a delta rotation so its barrel axis
  (`aim_axis=-z`) lands on that line; positions are never touched. Gated by the off-hand's grip button (`twohand_button=grip`).
- Lefty layout: `gun_hand=left`; the off-hand is the right hand.
- Smoothing: see Appendix E. The "bubble" release logic from earlier iterations was removed (the project journal explains why).
- Plan lesson: the hook layer was found while building, not by the Evening 1 log this plan proposed. Next time, log which API the game calls first.
