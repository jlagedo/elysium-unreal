# Decisions for the owner

Collected while the plan was built (2026-10-06, overnight). Nothing here is applied; the plan
works around each one with the default stated.

## D1. Start a story when what it calls below is done (amend R1)

R1 as committed: a layer is tested before the next layer starts. The plan marks every story and
brief **ready** when every lower-layer function it calls is already done (`audit.tsv`), and
`schedule.md` compares both orders. Proposed wording: "a story starts when every lower-layer
function it calls is done; the layer order holds per function". **Default until decided:** strict R1;
the readiness marks are informational.

## D2. More than one lane needs a second checkout

One checkout builds one binary; two workers editing it at once break each other's builds. Lanes in
`schedule.md` beyond one need a second checkout (a full clone, never a `git worktree` — `AGENTS.md`'s
gotcha) with its own binaries, or worktrees that only write and are compiled in turn in the main
checkout (0002's method step 3). **Default:** one lane.

## D3. The "engine-replaced" verdicts

556 core functions (137 KB) were judged *engine-replaced*: Unreal does the job and the port does not
reproduce the Source mechanism. Most are safe by `AGENTS.md`'s modernization rule — debug output,
rendering, networking and edicts, client prediction, memory and string pools, precache, file I/O.
About 150 touch state or event order — physics and collision, traces, sound, bone and pose queries —
and the rule admits them only "with the retail contract and event sequencing kept". **Proposed:** accept
the safe categories as named modernizations; each layer gets a contract-check story over its
state-bearing engine-replaced rows (inputs, outputs and event order compared with retail).
**Default:** those rows count as done in the numbers, and the contract checks are listed in each layer.

## D4. The ledger tooling story (R3)

R3 takes `research kernel --check` off the per-change path. The tooling half — the ledger stops
writing port `file:line` citations into `functions.md` / `coverage.md` — is a small pipeline story.
**Default:** first story of L0's tooling lane, before any L0 code lands.

## D5. Retail-default stubs

The re-check found generated slot "stubs" whose retail body is empty or returns zero, so the port's
default already matches (28 in L0/L1 alone); only the stub's diagnostic tally remains. **Proposed:**
the slot generator marks them `default:` like the other retail-default bodies. **Default:** listed as done.

## D6. The planners' questions, by layer

The read-only planners that turned each subsystem into stories raised these. Each is about a
modernization boundary or a scope cut; the stories proceed on retail's behaviour until decided.

### L0

- `L0-audio` — Owner decision: classify/register the 0x1022b590 -> 0x10175360 write to player+0x1e04 as an L3 upward hook, or explicitly reassign its ownership; hooks.tsv has no entry, so the SoundScheme story cannot close as an unlisted hook.
- `L0-entity_core` — Owner decision: retain the existing single-origin and angle transform simplification as a named modernization, or restore retail local/absolute frame state and parent-inverse conversion at 0x100b2150, 0x100b1ac0, 0x100b2300, and 0x100b2510. The current port explicitly collapses these values.
- `L0-entity_core` — Owner decision: define the Unreal replacement boundary for Source networking and edict services used by these rows, including ForceTransmit, dirty-state notification, and edict-backed PostConstructor paths; retain their game-visible flags and lifecycle effects either way.
- `L0-entity_core` — Owner decision: choose whether to reproduce 0x101d1960's cyclic multi-client selection and cluster cache or keep the current one-player PVS seam as an explicit single-player scope cut.
- `L0-entity_core` — Owner decision: 0x1023fd00 is marked engine-replaced in the layer audit; decide how Unreal collision queries preserve its strict squared-distance '< radius squared' boundary before accepting that replacement. 
- `L0-movers_doors` — Scope decision: keep 0x1013dea0 CFuncBrush::Spawn in L0 brush/solidity work rather than widening this movers_doors packet to generic func_brush behavior.
- `L0-movers_doors` — Modernization decision: retain and name the existing renderer replacement for Source visibility-portal updates at 0x100ef990, 0x100ef630 and 0x100f21d0 as a visual-only modernization; implement each row's remaining handle or restore semantics.
- `L0-physics` — Owner decision: name whether UE/Chaos collision may replace Source movement simulation. If chosen, preserve retail trace results, callback order, blocked-time rollback, and contact gates as a named modernization boundary.
- `L0-physics` — Owner decision: confirm the existing contact-tolerance and all-solid mappings as named geometry modernizations, or require retail parity before accepting the trace story.
- `L0-physics` — Owner decision: accept UE asset/material loading as the rope Precache modernization while retaining retail base Spawn reset and endpoint initialization order.
- `L0-physics` — Owner scope decision: keep physcannon acquisition/drop rows in L3, ragdoll/physics-chain rows in L1, player spawn placement in L3, NPC forget cleanup in L4, and dynamic-prop skin input outside L0 physics.
- `L0-rules_cvars` — Scope: keep the stock single-player cut or explicitly add a Counter-Bite/multiplayer restoration. docs/vtmb/multiplayer.md says maxplayers is 1 and no multiplayer rules class ships; the rows below stay unplaced under the current scope despite the packet's static 108-map reach.
- `L0-rules_cvars` — Law divergences: decide whether the 2-second floor, low-level 0..5 clamp, and extra PublishLawEvent are intentional named modernizations. Without that decision, restore the retail cvar and field-write behavior.
- `L0-save_restore` — Choose whether exact Source token/size words from `0x101a05f0` are required, or whether the existing Unreal archive is the named wire-format replacement; save files are disposable, so this is a behavior-versus-format boundary.
- `L0-save_restore` — Choose the Unreal physics-state serializer that preserves the `ShouldSavePhysics == true` admission and the save handler's timing; the current base slot returns false.
- `L0-save_restore` — Choose the Unreal asset-loading replacement for `MODELNAME` and `SOUNDNAME` precaching in `0x101a2820`, preserving its restore ordering.
- `L0-sound_list` — The owner must choose whether the 128-event serial/cursor bus is a named modernization for the retail 64-slot and reserved-client-index behavior, or whether the port will expose that retail lookup contract. The current bus does not establish equivalent slot identity, bounds or null-pool lifetime.

### L1

- `L1-animation` — Owner decision: whether Unreal skeletal evaluation may supply gameplay SetupBones and cache matrices after matching Source's mask, cache keys, and event timing, or whether the Source evaluator must remain separate. The current wield path reads the drawn frame and differs from the retail flags-filtered server merge.
- `L1-animation` — Owner decision: whether the player's visual clip phase remains a named modernization in place of the retail StudioFrameAdvance clock; that choice affects sequence and event timing.
- `L1-animation` — Owner decision: name the eye-rig blink envelope as a visual-only modernization, or require the retail m_blinktoggle state to drive the blink path.

### L2

- `L2-inventory_ui_items` — Owner decision: choose whether CWeaponIGeneric::Kick preserves retail VPhysics force semantics or receives a named Chaos/Unreal physics modernization.
- `L2-rpg` — Choose the Unreal asset-residency and socket mapping that replaces Source PrecacheSound/PrecacheModel and the retail particle attachment names. Keep retail parser order, payload names, defaults and event timing; record any chosen visual-only modernization explicitly.
- `L2-weapons` — Decide whether `0x1024f840` may replace Source VPhysics and optional constraints with Unreal physics as a named modernization; the plan assumes retail branch selection, bounds, touch and fall-think timing are preserved.

### L3

- `L3-player` — Owner decision: retain or remove the already named transform-flattening modernization at 0x100b7e40; if removed, player eye angles must compose with the move parent's transform.
- `L3-player` — Owner decision: choose the Unreal-side replacement for VPhysicsShadowUpdate at 0x1017b8a0 and ShouldSavePhysics at 0x1017be30, with the retail transform, velocity, collision, and save ordering as its contract.
- `L3-player` — Owner decision: confirm the scope cut for the six CHL2 suit-device and suit-power rows; the recovered player document finds no VtMB datamap or authored-content producer for them. The rows stay unplaced unless that scope changes.

### L4

- `L4-navigation` — Owner choice: expose the current Detour corridor corner for ordinary crowd routes so `0x102f1cf0` can measure it, or approve endpoint-only distance as a named Unreal modernization. The current body facts document the missing corridor data; the 0018 engine-replacement decision does not settle this behavior measurement.
- `L4-npc_kernel` — Decide whether the existing ground-door NavMesh route at 0x10303d10/0x10303850 remains a named modernization or is replaced by the retail local-route gates, acceptance, triangulation, and fly path. The packet describes contract-changing gaps; absent an owner decision to retain that modernization, implement the recovered retail behavior.
- `L4-npc_kernel` — Choose how Unreal carries the retail studio-header eye vector from 0x10428310: preserve the imported vector exactly or use an authored Unreal socket. A socket replacement can change NPC gaze and visibility geometry.
- `L4-npc_species` — Navigation modernization: decide whether to restore retail node-link search and local-route behavior for `0x102f1860`, `0x102f3270`, `0x10300700`, `0x103005f0` and `0x103007e0`, or keep the existing NavMesh replacement. This plan assumes retail behavior; keeping NavMesh makes those differences a named modernization.
- `L4-npc_species` — Wander modernization: decide whether to restore the visited-set behavior behind `0x102fe9a0` or retain the current no-visited-set wander behavior. The random walk and neighbor cursor are outside this packet, so include them if restoring the retail contract.
- `L4-npc_species` — Search scope: confirm the parent A* search `0x102fd240` and node-link graph are already owned by L4 work or expand the packet; helper-only implementation cannot restore route choice end to end.
- `L4-npc_species` — Squad scope: this story includes the `CAI_Squad` roster storage and writer path that the packet omits, because the current substrate has no squad object and the notifier cannot be wired with a null-squad seam.
- `L4-perception` — Assign the missing scent producer before closing `l4-nearest-sound-scent`. The current selector's input source is absent from the L0 sound path; the owner must place that producer in the earlier sound-emission work rather than accept a null GetBestScent seam.
- `L4-social` — Decide whether `TArray` growth is an approved Unreal replacement for retail's five-entry expansion and allocation-failure behavior, or whether to model the explicit capacity, copy, zero and warning path.

### L5

- `L5-dialogue` — Owner: decide whether the retail report, ellipses, auto-end and script-problem diagnostics are retained as nonshipping developer tools or scope-cut; they must not make runtime content reads or writes escape the project's baked and corpus content boundary.
- `L5-dialogue` — Owner: approve the current decoded clan-enum lookup as a named modernization, or require the protected clan-cache transforms while preserving the same visible clan column and take selection.
- `L5-dialogue` — Owner: decide whether restoring the shared local-time helper also changes save-header formatting; its dialogue report caller and save caller currently have separate contracts. 
- `L5-python` — Confirm whether the existing CPython 2.7 host and corpus `ScriptsDir` are the approved modernization for retail CPython 2.1.2 and the engine-root `python_lib` path; retain the retail binding and startup contract either way.
- `L5-scripted` — `0x1007bef0`: decide whether the existing 600-second scene cap remains as a named modernization. Removing it changes the tutorial alley scene’s completion and output timing for the authored ~1.5-million-second event; the raw retail maximum should still be available either way.
- `L5-scripted` — `0x101c7b80`: decide whether the optional `vskip_intro` console path is in scope. The packet marks it core, but `docs/vtmb/choreographed_scenes.md` says its gate is set only by the `vskip_intro` console command and no map or script reaches it.
