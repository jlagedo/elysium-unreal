# 0002 npc-ai — NPC AI: what an NPC senses, remembers, decides, schedules and walks, witnessed by the tutorial's stealth lessons

## Witness
`sp_tutorial_1`'s stealth lessons played against retail. The sneak-past lesson on `thug_1`: he
idles at his interesting place, hears the player's footsteps and walks the alert ladder to look,
sees the player only inside `540 ×` the light scalar and the 157° cone, and only then commits an
enemy and fires `OnFoundPlayer` into the "spotted" sign. The stealth-kill lesson on
`stealth_victim`. The post-feed trance on any civilian.

## Scope
The whole of NPC AI: everything an NPC senses, remembers, decides, schedules and navigates, in
retail's order. Four themes: stealth, senses and memory, the schedule host, incapacitation.

Owned elsewhere and consumed here: weapons, damage, death — **0005**; firearms — **0008**;
disciplines' own effects and costs — **0006**; conversation UI and the `.dlg` runtime — **0004**
(this spec provides the `NO_DIALOG` refusal and the partner's sense freeze); the
`scripted_sequence` beat and `NPC_STATE_SCRIPT` — **0003** (this spec provides the hold, the
kernel and the route refusal it runs on).

## Sources
- Oracle: `docs/vtmb/npc-ai/README.md` (the NPC AI oracle), `docs/vtmb/stealth.md`,
  `docs/vtmb/navigation-jump-links.md`, `docs/vtmb/feeding.md`, `docs/vtmb/footsteps.md`,
  `docs/vtmb/entity_io.md`, `docs/vtmb/activity_enum.md`, `docs/vtmb/animation_and_movers.md`,
  `docs/vtmb/disciplines.md`, `docs/vtmb/lighting.md`, `docs/vtmb/vdata-catalog.md`,
  `docs/vtmb/sp_tutorial_1-event-surface.md`. Each story names its section.
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `maps/sp_tutorial_1.entities.glb`,
  `maps/sp_tutorial_1.lighting.glb`, `nav-graphs/sp_tutorial_1.glb`, `vdata/` (`stealth.txt`,
  `sound_volume_table.txt`, `interestingplacetypelist.txt`, `Rules.txt`, `npctemplate*.txt`,
  `StealthKillRules.txt`).

## Witness data
**`thug_1`**, child of `npc_maker` `thug_maker` (`NPCType npc_VVampire`, `NPCTargetname thug_1`,
`Flag_StartDisabled 1`, `Flag_InfChild 1`, `MaxLiveChildren 1`, `SpawnFrequency 5`), model
`Shovelhead/shovelhead.mdl`, `stattemplate TutorialShovelhead`, `soundgroup Young_Thug`.
- Relation: `player_reaction D_HT 5`; `default_disposition Neutral`; `additionalequipment
  item_w_tire_iron`; `combat_start_activity ACT_INVALID`.
- Senses: `npc_perception 3`, `vision 540`, `hearing 1.00`, `investigate_mode 4`,
  `investigate_mode_combat 4`, `full_investigate 0`, `allow_alert_lookaround 1`,
  `pl_investigate 1`, `pl_criminal_attack 2`, `pl_criminal_flee 6`, `pl_supernatural_attack 4`,
  `pl_supernatural_flee 6`.
- Reaction keys: `hint_groups 1..32`, `allow_kick_hint_use 1`, `use_interesting 1`,
  `interesting_place_groups 2`, `percent_occluded_wait 10 / cover 30 / walk 10 / flank 20 /
  chase 30`, `bright_route_penalty 0`, `follower_type Default`, `stay_entrenched 0`, no squad.
- Outputs (authored on the maker, forwarded to the child): `OnFoundPlayer →
  logic_failed_thug.Trigger` (0.5 s) → `popup_29.OpenWindow`, whose `OnUseBegin` kills him;
  `OnDeath → kill_check.Test`, `trig_popup_tireiron.Enable`.
- Spawn and reset: `trig_dialog_feed_on_rats.OnStartTouch → thug_maker.Spawn` (the player is in
  Jack's dialogue ~2380 units away); `logic_failed_thug_2 → thug_maker.Spawn` at 1.2 s and 2.5 s
  beside `teleport_thug.Teleport`; `logic_reset_sneak_2` / `logic_reset_unarmed_2 → thug_1.Kill,
  thug_maker.Spawn`.
- Places: `pt1` (`group_id 2`, `enabled 1`, `min_time 30`, `max_time 60`) at his spawn; `pt2`,
  `pt3` (`group_id 2`, `enabled 0`) down the alley. His pool is `pt1` alone until a script enables
  the others.
- The `trig_diversion → logic_gunfire` sounds are `ambient_generic`s with `sound_event 0`: audio
  only, no NPC hears them. His investigate inputs are the player's footsteps (180/240 units) and
  doors.

**Retail, spawn to detection.** Look prefilters at 3072 units every 0.15 s; admission needs
`d ≤ 540 × player light scalar` (75.6…540), the 157° cone, and an eye-to-eye trace; the outer 30 %
of that radius raises `SEE_UNKNOWN` instead. A pass writes `SEE_PLAYER` + `SEE_HATE` and an
enemy-memory record; `BestEnemy` walks that memory only, so at 2380 units nothing happens and he
takes `pt1`. Hearing yields conditions 0.2–0.9 s late, never a memory record: a noise sends him
through turn → step → walk to the sound (once per life), and he turns hostile only by seeing.
On commit, `NEW_ENEMY` breaks the interest program; the committed-enemy LOS edge fires
`OnFoundPlayer`. The player's dialogue is invisible to him.

**`stealth_victim`**, child of `stealth_victim_maker`, same model and template, `player_reaction
D_NU 0`, `item_w_fists`, `vision -1`, `hearing -1`, `allow_alert_lookaround 0`,
`interesting_place_groups 8`, all `pl_*` 6; `popup_39.OnUseEnd → SetRelationship player D_HT 5`;
`OnDeath → thug_maker_4.Spawn`.

The seven Society hunters and `sentry2` at the far end of the BSP are an Unofficial Patch
level-select hub, not tutorial content.

## Stories
In build order. A story is done when every behaviour it lists is in the
substrate and its recovery is written in the oracle section it names. Numbers are stable ids
cited by other documents; a split keeps the number and adds a letter. Each open story carries
the retail contract the code must match, the job, what it consumes or provides, and a size
(XS–XL) with the model / effort tier recommended for it. Sizes are read off the kernel ledger
(`## Build order` below), not guessed: a story is sized after its closure is known.

- [x] **29. The kernel ledger.** Retail: the whole `CAI_BaseNPC` family in `vampire.dll` — 77
  classes, 600 primary-vtable slots, the 835-offset `CAI_BaseNPCTroika` layout, and the 5,050
  functions reachable from the slot bodies, `NPCThink 0x10292de0` and `RunAI 0x1026f110` —
  read out of the whole-body corpus in one pass. Job (landed): `uv run elysium research
  kernel_ledger` writes `docs/vtmb/npc-kernel/` (classes, slots, fields with writers and readers,
  functions with citations, the call graph, the layered build order, the kernel's entry points
  from other subsystems, coverage, the naming backlog, and the address → oracle-section index);
  `--check` verifies the committed tables; `pipeline/tests/test_kernel_ledger.py` holds the
  derivations and the 25a facts as oracles against the real corpus. The prose oracle was split
  into `docs/vtmb/npc-ai/` by subsystem, headers unchanged, and
  `pipeline/tests/test_oracle_citations.py` refuses a dangling `§ "…"` or path citation.
  Provides: the closure, layer and producer facts every story below is sized and ordered by.
  Oracle: `docs/vtmb/npc-kernel/README.md`. Unrecovered: READ/WRITE direction is a regex over
  the decompiled C; 78 closure bodies are damaged decompilations; 2,970 are still `FUN_`.
  Size: M. Effort: Fable / high.
- [x] **29a. The naming pass.**
  Retail: of the closure's 5,050 functions, 2,970 are `FUN_`; of the 2,490 *core* functions (a
  family or helper method, or a body touching an offset past `CBaseCombatCharacter`'s layout
  `+0x19b0`) 1,073 are — 725 in layers 0–4, 141 in 5–9, 116 in 10–18, 90 in 19–26. The image
  names 129 through the doc overlay (`corpus names`, `research/tooling/ghidra/driver/names.tsv`,
  tier + evidence per row); `corpus harvest` proposes rows from addresses the docs pair with a
  name. Three evidence sources the harvest does not read yet: the Source SDK 2003 class
  declaration order (a vtable's slot order *is* the header's virtual order, so every slot whose
  neighbours are named names itself — verified against the 129), the `file:line` stamps the
  selector trace writes (`AI_BaseNPC.cpp:682`, `scripted.cpp:0x3ca`…) which pin a body to a
  source file, and DevMsg strings.
  Gap: `unnamed.md` ranks the backlog; nothing proposes names from slot order or stamps.
  Job: extend `corpus harvest` with the slot-order pass over the SDK class declarations
  (`$ELYSIUM_WORK_ROOT/research/reference-source/Bloodlines SDK/`) and the stamp pass; review the proposal file as
  text (a row is a question, not a name); `corpus names --apply`; regenerate the ledger. Names
  are identity only — no behaviour is read beyond what names it. Acceptance: core `FUN_` count
  under 200 in `coverage.md`; every slot of `CAI_BaseNPC` 0–583 named or marked "no SDK twin".
  Consumes: 29. Provides: readable checklists to 29b–29e; `functions.md` rows a story can cite
  by name. Oracle: `npc-kernel/unnamed.md` (the count is the record). Unrecovered: nothing —
  a name the evidence cannot settle stays `FUN_` and says why in the overlay's evidence column.
  Size: M. Effort: Sonnet / medium — mechanical, reviewed as text.
  Landed (2026-09-13); closed with the core-count half of the acceptance unmet, by decision.
  `corpus harvest` gained the slot-order,
  translation-unit-order and message-prefix passes (`name_passes.py`, `sdk_layout.py`) and a
  slot-identity pass (a named body at slot *N* names every override at *N*), all held to the
  image's `RET n` arity; docs-proximity rows lose to them. The SDK premise did not hold: the
  Bloodlines SDK tree has no `dlls/` headers, so the declarations are Source SDK 2013's
  (`$ELYSIUM_WORK_ROOT/research/sources/source-sdk-2013/src`). Leave-one-out over
  `CAI_BaseNPC`'s named slots: 112 agree, 0 disagree. The overlay went from 157 rows to 2,009
  (1,455 inferred, 239 doc, 315 unsettled) through three harvest → apply rounds to a fixpoint.
  `coverage.md` now counts `vfuncN` as unnamed; on that measure the closure went 4,064 → 3,235
  unnamed and the core 2,155 → 1,371 (`FUN_` alone: 835). **Met:** every `CAI_BaseNPC` slot
  0–582 is named or carries an `unsettled` reason (315 do). **Not met:** core unnamed under
  200. Of the 1,371, 1,084 are slot bodies at slots where no body carries a name, the named
  bodies disagree, the arity differs, or the body is folded; the other 287 fill no slot, and
  no SDK definition run or message reaches them (Troika's units — `AI_BaseNPCTroika.cpp`,
  `NPC_V*.cpp` — have no SDK source at all). What would move them: the 2003 headers, or a
  walked body per name — 29c–29e's verdicts.
  Recovered on the way: slot 437 is `PreSelectSchedule` (the image's string; the oracle called
  `0x102ae920` `GetSchedule`); slot 439 is `SelectFailSchedule` inside `GetFailSchedule`
  `0x10281730`.
- [ ] **29b. The shape: every field and every slot declared.**
  Retail: `fields.md` — the 835-offset `CAI_BaseNPCTroika` layout (`CAI_BaseNPC`'s 620 are its
  prefix), plus the species-only offsets and the undeclared `field_0x…` words the walk touches;
  `slots.md` — 600 primary-vtable slots, 347 with a single body across the family, the rest with
  a Troika body and up to 27 species overrides; `classes.md` — the 77-class tree and the entity
  classnames each claims.
  Gap: `FElysiumNpc` (`ElysiumNpc.h`) cites ~30 offsets and ~25 slots in comments only, nothing
  asserts them; `FElysiumNpcMind` has no retail counterpart at all (`m_NPCState +0x5cc0` /
  `m_IdealNPCState +0x5cc4` are not declared anywhere); nine sites call `Schedule.Clear()` where
  retail calls `ClearSchedule`; `ElysiumStub::Fired` keys on free text with no address or story
  field. `FElysiumNpc` is `final` and species are data (`ElysiumNpcClasses.cpp`), which is right
  and stays.
  Job: a generator beside `gen_action_tables.py` reads the ledger tables and emits
  `ElysiumNpcKernelShape.cpp` — the census (offset → member, slot → virtual, class → base) the
  runtime asserts in an `ElysiumActionTableTests`-shaped test — and the shape itself lands by
  hand from the census: every Troika offset a named member on the struct that owns its concern
  (`ScheduleHost`, `Cognition`, `Senses`, `NpcFlags`, the leaf), existing members kept and
  mapped, new ones default-initialised and unwritten; every slot a virtual on `FElysiumNpc` with
  its address in the declaration comment and its base body as the default, or a named stub that
  tallies `elysium.stubs` — which gains structured `Address` and `Story` columns so the tally
  joins `functions.md`. Species overrides are rows in the class registry (class → slot →
  address), not subclasses. The nine raw `Schedule.Clear()` sites become the one
  `ClearSchedule` (25a's first job, done here because it is shape).
  Consumes: 29, 29a. Provides: the object every later story fills instead of re-shaping — the
  end of "seam shaped by guess, rewired by the next story".
  Oracle: `npc-kernel/fields.md`, `slots.md`, `classes.md`; the census file is the record.
  Unrecovered: nothing new — the shape is a transcription. Size: L. Effort: Opus / high.
- [ ] **29c. The primitives: layers 0–9, in bulk.**
  Retail: 1,695 core functions in the first ten layers of `order.md` — 1,156 are ≤ 64 bytes
  (accessors, predicates, one-field setters), 924 touch no NPC field, 1,325 fill a family slot
  (mostly species overrides of tiny virtuals: `IsX()`, `GetY()`), 21 are damaged decompilations.
  Job: `kernel_ledger` gains `--checklist <band>`, which emits `npc-kernel/checklist-0-9.md`: one row per
  function (address, name, size, slots, fields written/read, callers, damaged) with an empty
  *verdict* column; the story fills every verdict with one of four words and does what the word
  says. **rule** — port the body verbatim onto 29b's virtual with one test derived from the
  decompiled C (the vision's feel layer: formulas, thresholds, call order); **mechanism** — Unreal
  already provides it (a trace, a physics query, an audio call, a string op); name the service
  seam it maps to and record it, port nothing; **present** — the port already has it; cite the
  function; **dead** — no caller in the closure and no slot: recorded. Work by band as
  checkpoints, 0–4 (1,453) then 5–9 (242); damaged rows go to `corpus asm` one at a time and are
  never bulk-verdicted. Acceptance: no empty verdict; every `rule` row has a test; the
  `coverage.md` "cited by neither" count for layers 0–9 is zero.
  Consumes: 29b. Provides: every leaf a later story's body calls, already present; the feature
  stories below lose their low-layer work (the per-story split is in `## Build order`).
  Oracle: the checklist is the record; a `rule` row whose body is > 64 bytes gets a walked
  paragraph in the subsystem file its fields belong to.
  Unrecovered: the 21 damaged bodies until read from the listing. Size: XL — bulk, each row
  trivial. Effort: Sonnet / medium in bulk; Opus for the damaged rows and any verdict argued.
- [ ] **29d. The middle: layers 10–18.**
  Retail: 347 core functions — the three `GatherConditions` sweeps (10a–10c, landed), the
  see-unknown sweep's neighbours, `CAI_Memory` and the sense helpers, hint and navigator
  helpers, `SetEnemy 0x10279a50`, the flag-word writers — 116 unnamed, 4 damaged, 17 port-cited.
  Job: the same checklist form (`--checklist 10-18`); bodies > 64 bytes are walked into their
  subsystem file (`senses.md`, `social.md`, `conditions-and-states.md`) with the section
  convention (address in the header, `**Unrecovered:**` at the end); bodies ≤ 64 bytes take a
  verdict as in 29c. Acceptance as 29c.
  Consumes: 29c. Provides: the producers every gather and selector reads (`fields.md` *Producers
  later* for layers 19–26 drops to zero). Oracle: the subsystem files. Unrecovered: the 4
  damaged bodies. Size: L. Effort: Opus / high for the walked bodies, Sonnet for the rest.
- [ ] **29e. The loop and the state machine: layers 19–26.**
  Retail: 354 core functions, 90 unnamed, 8 damaged, 106 already cited by the oracle — the
  interpreter itself: `SetState 0x1026e340`, `SelectIdealState 0x1026f660` / Troika `0x102ad660`
  / `CNPC_VHuman 0x103851e0`, `NPCInit 0x10273390` / `0x1029a0b0` and the species inits,
  `TranslateSchedule 0x102b12f0` and its twenty species overrides, `MaintainSchedule 0x102817c0`
  and every exit, `GatherConditions 0x1026ec30` / Troika `0x102b27f0`, `RunAI 0x1026f110` /
  `0x1028fcc0`, `StartTask 0x102827f0` / `0x102a1910` and `RunTask 0x10288780` / `0x102aacf0`
  arm by arm, `SelectSchedule` (Troika `0x102af660`, VHuman `0x10384ee0`, base `0x1028a260` —
  damaged), `GetSchedule 0x102ae920`, `TaskFail` and the `CNPC_VSabbatLeader 0x103a9400`
  override, `NPCThink 0x10292de0`.
  Gap: about half is walked (25, 15, 10a–c, the idle branch, `GetSchedule`); the rest is the
  open stories 25b, 25c, 26, 16b's ideal state, 10d's selector, and 21c's loop consumers.
  Job: walked, not bulk — one section per function in `schedule-kernel.md`,
  `conditions-and-states.md` or `lifecycle.md`, every arm, every field it reads, its priority
  order, what it writes, ported onto 29b's virtuals with the tests the walk implies. This story
  **absorbs** 25b (slot 440 and the frenzied pre-table), 25c (`MaintainSchedule`'s exits), 26
  (`GetSchedule`'s eight high-layer bodies), 16b's `SelectIdealState`, 10d's `SelectSchedule`
  case 3; those stories keep their numbers for their remaining program and wiring work and say
  so. Order within: `SetState` → `SelectIdealState` → `NPCInit` → `TranslateSchedule` →
  `MaintainSchedule` exits → `GatherConditions` → `RunAI` → the task arms → `SelectSchedule` /
  `GetSchedule` → `NPCThink`.
  Consumes: 29d. Provides: the interpreter every program family runs on, finished once.
  Oracle: `schedule-kernel.md`, `conditions-and-states.md`, `lifecycle.md`. Unrecovered: the 8
  damaged bodies (`0x1028a260` first). Size: XL. Effort: Opus or Fable / high.
- [x] **1. Target surface and the light query.** The light row and `Sneaking` publish the vision,
  cone and hearing scalars; `trigger_stealth_mod` is a balanced overlap modifier. The light query
  is a live per-worldlight evaluation: Source falloff by light type, the cone/angle term, the live
  lightstyle value, occlusion by a trace only world geometry and unflagged static props block,
  the first sun via a sky trace, luminance `0.30/0.59/0.11`, sum clamped to `[0, 1]`; eligible
  only under `FL_DUCKING` and unseen by any `D_HT` NPC for 1.0 s, `-1.0` when not, sampling runs
  even when ineligible, three sample points off the world AABB. Oracle: `stealth.md` § "Player
  target-surface update", § "`trigger_stealth_mod`", § "The light query, recovered". Settled
  as not in the image: `IVEngineServer` slot 118 (`GetLightForPoint`), the `0x100` shadow-brush
  contents name.
- [x] **2. The light gauge producer.** Gameplay publishes the 0..10 light row into
  `FElysiumStealthView`, valid once a real sample exists, shown only while crouched; the HUD is
  this project's own five-step asset and maps the eleven rows itself. Oracle: `stealth.md`
  § "HUD observability is not authority". Unrecovered: the recv-prop names of the client's
  `+0x16b4` / `+0x16f4` stealth icon fields.
- [x] **3a. Stealth kill: rules and arc.** `StealthKillRules.txt` loader, the deaf arc
  (`InDeafArc` `0x101be500`) and minimum approach depth (`ComputeMinDepth` `0x101bef50`),
  active-weapon Brawl/Melee arc selection, the rear deaf zone shared with hearing. Oracle:
  `stealth.md` § "Stealth-kill transaction (RE50)".
- [x] **3b. Stealth kill: victim selection.**
  Retail: `FindVictim` (`0x101be1f0`) runs per frame with a cache; admission is the trace to
  the victim, the weapon's stealth-kill capability, the arc and depth from 3a; an oblivious
  victim (7) admits a backstab from any angle.
  Job: the victim query on the player, the weapon capability read, the obliviousness override.
  Oracle: `stealth.md` § "Victim selection and per-frame cache".
- [x] **3c. Stealth kill: commitment and the paired action.**
  Retail: the input commits the kill; grapple type 3 pairs attacker and victim through the
  `m_GrappleType` role pair; the victim's death is synchronized to the action; the tutorial's
  lesson completes on the victim's `OnDeath`.
  Job: input commitment, the grapple type 3 pair, the synchronized death, the output.
  Consumes: the grapple state machine and death from 0005.
  Oracle: `stealth.md` § "HUD publication and input commitment", § "The grapple role pair and
  the `m_GrappleType` enum", § "Tutorial lesson completion".
- [x] **4. The committed observer snapshot.** Gameplay publishes searching/detected to the HUD
  after the NPC state commits; the HUD runs no trace and no detection. Oracle: `stealth.md`
  § "HUD observability is not authority".
- [x] **5. The enemy memory** (`CAI_Memory`). Records at `mem+0xc` (handle, last position,
  anchor, velocity, last-seen time, nav nodes, position-only byte, eluded byte) written by
  `UpdateEnemyMemory` (slot 544, `0x102709c0`) from `OnLooked`'s `D_HT`/`D_FR` arms;
  `RefreshMemories` (`0x102df320`) drops only dead or invalid handles; `BestEnemy`
  (`0x102743c0`) walks this list only; `ChooseEnemy` (`0x10279dd0`) treats a null active
  schedule as interested in `NEW_ENEMY` / `LOST_ENEMY` / `ENEMY_DEAD`, so no substitute spawn
  schedule. Oracle: § "The enemy memory — `CAI_Memory`".
- [x] **6a. Sight and hearing.** 3072-unit prefilter; cadences 0.15 s players / 0.25 s NPCs /
  0.45 s objects; `m_flFieldOfView 0.2`; `SEE_PLAYER 0x5a`; `IRelationPriority` `<0` DISLIKE /
  `0..10` HATE / `≥11` NEMESIS; `FinViewCone3dNew`'s strict front test then the apex cosine
  times the target's cone scalar; the combat-state range bypass; concealment `0x10146b20`; the
  outer-band path `0x102b3e00` (`SEE_UNKNOWN`, `OnUnknownVisionPlayer`); hearing radius ×
  `hearing`, conditions delayed `RandomFloat(0.2, 0.9)`, deaf-arc suppression, the
  cowering/sleeping quarter radius, the seventh (`Flinch`) record; `ambient_generic`
  `sound_event` / `sound_event_level` 1..3 → 180/240/1200, non-occludable, once per activation;
  `m_flStealthVisionOverrideTime` (+0x6604) written by damage with a 5 s deadline, consumed by
  `FVisible` (`0x102b4760`) as a range bypass. Oracle: § "The sense pass for a hated player,
  walked", § "Hearing, walked", § "`ambient_generic` as an AI sound source", § "Sense and
  investigate leftovers, closed". Named seams: the cone-apex ConVar (`0x10937a8c`, no writer in
  `.text`) and the 2-D cone mode (`0x10936f74`); 0006's cloak/detection-record producers.
  Settled as not in the image: `+0x6081`.
- [x] **6b. The Troika cone override.**
  Retail: `FInViewCone` is slot 363 and every caller dispatches it virtually, so on a VtMB NPC
  the body that runs is `CAI_BaseNPCTroika` `0x102b4540`, not the base
  `CBaseCombatCharacter::FInViewCone` `0x10326750` that 6a ported. Its arms, in order: null
  target → false; `DAT_10924fba` (`npc_ignore_senses`) → false; `DAT_10924fb9`
  (`npc_ignore_player`) and the target is a player (`target+0xa8`) → false; slot 293
  (`GetFollowerBoss` `0x102c5470`) returns `boss+0x9c` equal to `m_hClosestPlayer` (+0x628c)
  and `target->+0x98` (Troika self-pointer) non-null and `*(+0x6279)` (`m_bInPlayerLOS`) set →
  **true, skipping the cone entirely**; else the base. The first two globals also gate
  `QuerySeeEntity` (slot 468, `0x102b38b0`), `QueryHearSound` (slot 467, `0x102b35b0`) and
  Troika `FVisible` (slot 201, `0x102b4630`). Arm 4 is a follower looking at an NPC in player
  LOS, not a player-behind skip; a player does not write `+0x98`.
  Job: the override in `FElysiumNpcSenses::IsInViewCone`, the two globals as ConVars, and the
  arm-4 seam.
  Consumed by: every sight admission (6a) and 10a's `SEE_SOUND_SOURCE` stranger arm.
  Oracle: § "The sense pass for a hated player, walked" (Cone; "The Troika cone override
  `0x102b4540`, walked").
  Unrecovered as a live producer: `m_hFollowerBoss` (16a) and the target's `m_bInPlayerLOS`
  overlay (15). Arms 1-3, the sibling gates, and the base fall-through are ported; arm 4 is
  the named seam.
- [x] **7. Obliviousness.** `TASK_MAKE_OBLIVIOUS` 0x131: `flags2 |= 0x80001000`,
  `SetEnemy(NULL)`, squad disconnect (`0x1026d050`, a seam until 17), `++m_iIsOblivious`
  (+0x5bb4, saved), `OnIncapacitatedStart`. Consumers: `PerformSensing` skips the pass,
  `UpdatePoseParameters` (slot 314) drops aim, slot 587 rejects; `FindVictim`'s any-angle
  backstab is 3b. Oracle: § "`TASK_MAKE_OBLIVIOUS` and `m_iIsOblivious`". Settled as not in
  the image: slot 587's name (`CanWitnessSupernatural()` is the project's).
- [x] **8. The NPC flag word.** `m_bfAINPCFlags` (+0x14b8) / `m_bfAINPCFlags2` (+0x14bc), 62
  names (`0x1030cbd0`); `TASK_SET/CLEAR_NPC_FLAG` 0x100/0x101; `D_IS_BUSY` →
  `IsBusyWithDiscipline` only, `NO_DIALOG` → the dialogue gate, `DONT_INVESTIGATE` → the
  interest predicate only; `OnScheduleChange` (slot 435, `0x102a0940`, gated on
  `PRESERVE_PATH`) `flags1 &= 0xbbf4b97e`, `flags2 &= 0x77fff14f`, `MADE_OBLIVIOUS` cleared
  with decrement; every species override chains to it, so no classname branch. The second
  writer is a discipline HitGroup's `AI_NPCFlag`, set on apply (`0x101de660`), cleared on
  expiry (`0x101def10`, with `RemoveFromComfortList` and a schedule teardown); `MiscFlag`
  (`+0xa4`, resolver `0x1033cb00`) is the sibling. Oracle: § "The incapacitation tasks and the
  NPC flag word", § "Disciplines that possess or frenzy an NPC; the `AI_NPCFlag` payload",
  § "Species slot-435 overrides all chain".
- [x] **9. The interest predicate and the base condition table.** `ShouldInvestigate`
  (`0x102b3270`) with `investigate_mode(_combat)` 0–6 (default 4), rejecting
  `DONT_INVESTIGATE|IN_FLEE_SCHED`, `stay_entrenched`, null and my follower boss; the base
  table (`0x102c8ce0`, 119 entries). Oracle: § "The three `GatherConditions` sweeps and the
  interest predicate", § "The base condition table".
- [x] **13. `TaskFail`** (slot 448). Base `0x10273fc0`: reason table `0x106152b0` (0x00–0x29)
  to +0x5c50, clear `m_bShouldMove`, `COND_TASK_FAILED` 0x5c. Troika `0x1029adb0` first:
  interesting-place teardown, clear `PRESERVE_PATH` unless nav type CLIMB/JUMP, motor reset,
  four think stamps := curtime, goal tolerance and interrupt distances 0, move target and kick
  prop released, `m_afMemory &= 0x0fffffff`, `flags2 &= 0x7fffe24f`, `flags1 &= 0xa3f40178`
  (`MADE_OBLIVIOUS` cleared, +0x5bb4 not decremented: retail's bounded leak, reproduced with a
  trace row), `SLEEP_BOUNDING_BOX` restore through the attack extents, `ClearHintNode(5.0)`.
  `COND_SCHEDULE_DONE` 0x5d. `TASK_STOP_MOVING`'s `FAIL_STUCK_ONTOP` 0x1c: goal active at
  StartTask, then `NAV_JUMP`, not on ground, `|v| ≤ 0.01` at RunTask. Oracle: § "`TaskFail`
  and stopped special navigation, walked".
- [x] **14. `TASK_SET_ACTIVITY` completes on a miss** (arm `0x102a1c0f`, no fail path); the
  miss is a trace row. Oracle: § "Nothing in schedule data clears these bits — the schedule
  *change* does".
- [x] **19. Jump links.** `NavLinkProxy`s from the decoded `.ain` links with retail's
  disabled-bit and capability filter; the motor reports `Jump` while traversing one and
  `Ground` before arrival or failure. Oracle: `navigation-jump-links.md`; the Unreal flight
  service is a named modernization.
- [x] **20. The trance.** `FeedInterrupt` (`0x1033a9e0`) on a victim with `BloodPool ≥ 1` and
  `IRelationType(attacker) != D_HT`: think timers := curtime, `SetSchedule(0xfb
  SCHED_TROIKA_MESMERIZED)` (`MAKE_OBLIVIOUS TRUE; SET_NPC_FLAG D_IS_BUSY, DONT_INVESTIGATE,
  NO_DIALOG; SET_ACTIVITY ACT_DISPOSITION_MESMERIZED; WAIT 30; WAIT_RANDOM 120`, damage
  interrupts, `DELAY_INTERRUPTS`); after `LeaveGrappleState`; replaced by 0x6b when
  `IsBusyWithDiscipline()`. `DELAY_INTERRUPTS` is one think of immunity re-armed by every
  install; `SetSchedule` zeroes the condition set; `MaintainSchedule` bound 10; effective mask
  = authored ∪ `BuildScheduleTestBits` ∪ `NPC_FREEZE`. Oracle: § "Incapacitation, feeding,
  grapple, and death", § "`DELAY_INTERRUPTS`, decoded", § "`SetSchedule` clears the condition
  set".
- [x] **23. The NPC debugger** (visual-only modernization, out of Shipping, reads only).
  Job: an `FGameplayDebuggerCategory` "ElysiumNPC" whose `CollectData` packs mind state,
  schedule/task, conditions, enemy memory, sense radii and the trace, and whose `DrawData`
  draws the vision circle and cone, hearing radius, enemy LOS line and held place; an
  `IVisualLoggerDebugSnapshotInterface` on the body actor with `UE_VLOG` shapes at sense
  admission, enemy choice and schedule install.
- [x] **15. The think cadence.**
  Retail: four stamps with four interval laws (`CalcNextUpdateThink` `0x10290720`, `Normal`
  `0x10290b60`, `Move` `0x10290fc0`, `AI` `0x10291230`), distance/PVS/LOS-driven, none reading
  NPC state; due when `(stamp − curtime) ≤ frametime`; `NPCThink` (`0x10292de0`) runs its
  body only when the normal think is due and passes `bReduced = !IsThinkDue(NextAI)` to
  `RunAI` (`0x1026f110`: no `GatherConditions`, `MaintainSchedule` bound 1, no clear of
  `LIGHT/HEAVY_DAMAGE` / `WAS_BUMPED`); `m_flNextThink = min(NextUpdate, NextNormal)`;
  `SCHEDULE_CHANGED`, LOS and dialogue pin normal and AI to 0.1 s; `SetPlayerLOS` at most every
  2 s with the 512-unit bypass and 8 s hysteresis. The port runs one `NextThink`, bound 10
  always, no `WasBumped`; 13 already resets the four stamps.
  The clock is re-based by slot 614 `ResetThinkTimers` (`0x102c23f0`: the four `Next` and
  `m_flNextThink` := curtime) at 34 sites, and by its two wrappers slot 583 (`0x1028d860`, within
  2048 units of a point, a broadcast from every player teleport) and slot 584 (`0x1028d910`, plus
  every `Last` stamp; `TASK_WAIT_PVS`'s completion, the node-graph rebuild). `SetSchedule`,
  `ForceScheduleChange` and `aiscripted_schedule` write no stamp: an install from outside a think
  waits for the cadence. The AI console gate `0x1026c3d0` refuses every `NPCThink` while
  `g_AIDisabled` bit 0 is set (`SetAIEnabled` `0x10265680`: a player's `FeedBegin` in a map with
  `m_nAreaType ≠ 0` unobserved for 3 s, the level-change fade, `events_world` `AIEnable`), and
  enabling re-bases every NPC with the slot-584 form. `m_bfNPCStateFlags` (`0x1026e3e0`, a pure
  function of `m_NPCState`: idle `0x31`, combat `0x8f`, alert `0x39`, script `0x08`, flee
  `0x85`, hunt `0x7f`) carries bit 3, which forces PVS and LOS true in `SetPlayerLOS`.
  Job: the four stamps and laws on the NPC, `IsThinkDue` per stamp, the reduced mode gating the
  condition pass and the completion bound, the pins, `WAS_BUMPED`; every reset in the port at a
  retail site and at no other (the dialogue START, the spoken-line player and `m_bIsTalking`,
  `LeaveGrappleState`, `SetDisableAI` and the `DisableThink` input on NPC and maker, the
  discipline `AI_Schedule` arm, the scene cast's `DisableAI` save/restore, `WAIT_PVS`, the
  teleport broadcast, the map-wide AI gate with its producers); the state byte and its PVS/LOS
  force; `DISAPPEAR` and the enemy distance triple in `NPCThink`; the sighting rebuilt on every
  pass with no grace; the shared test fixture drives the clock rather than pinning it.
  Provides: the clock every later program family runs on (10d–10h, 16, 17, 21): `DELAY_INTERRUPTS`'
  "one think", the sweeps' reduced-mode gating, the follower distance checks and the squad's
  0.2 s sighting window are all stated in thinks; `FElysiumEntityWorld::SetAiEnabled` /
  `WakeNpcsNear` and `FElysiumNpc::ResetAllThinkStamps` to 0003, 0004, 0005, 0006, 11, 16c, 21a.
  Oracle: § "The think cadence, decoded" (Writers; Slot 614 dispatch sites; Port, the reset
  sites). Closed 2026-09-12: `m_flTeleportMoveTimer` is the `teleport_move_timer` keyfield alone
  (827 rows, three authored `2`); `m_bForceFrequentThink`'s setter has no caller; `UpdateCharacter`
  is the boss registry over `CBaseCombatCharacter::UpdateCharacter` `0x103246d0`;
  `m_bIsBCCTargetable` is 1 on every NPC but `CNPC_VCamera`; the toggle is `ai_disable`
  (`0x100851f0`); the werewolf gates are `werewolf_show_debug` and its hint-draw sibling; slot
  578 is an empty virtual; slot 168's Troika body returns `m_hLastEnemy` under state bit 6 (hunt
  or flee `0x7f`); `CAI_BaseNPC+0x98` is the Troika self-pointer. Deferred to their owners: the
  `NPCThink` hint upkeep (12b), shoot-target override (0008), death-scream roll (16c), the
  motor's contact/gravity/turn-pose arms (the motor, this spec's navigator half, no story yet), the boss registry (a boss story).
  Size: XL. Effort: Fable / high.
- [x] **10a. The sound sweep.**
  Retail: `0x102b1cd0` over the seven sound records (incl. `Flinch` +0x6210); gate
  `m_flNextInvestigateSoundTime` +0x623c re-armed 2.0 s, 20.0 s for a stranger's sound;
  `HasCondition(HEAR_X) && (mask has HEAR_X || ShouldInvestigate)` → `INVESTIGATE_SOUND` 0x25;
  `HEAR_DANGER` skips the predicate; `HEAR_FLANK_SOUND` 0x33; `SEE_SOUND_SOURCE` 0x2d
  rate-limited by +0x6418; `CommitBestSound` (`0x102b4090`) copies the winner by priority into
  the sticky `m_InvestigateSound` +0x60dc.
  Job: the sweep in `GatherConditions`, the gate, the three conditions, `CommitBestSound`.
  Oracle: § "The three `GatherConditions` sweeps and the interest predicate", § "The
  `INVESTIGATE` family, decoded" (selection).
  Size: M. Effort: Sonnet / high.
- [x] **10b. The see-unknown sweep.**
  Retail: `0x102b15c0`: `m_hBestSeeUnknown` +0x6088 with 1.5 s grace +0x6084,
  `m_vecLastSeeUnknownPos` +0x6090; the one-shot `MADE_INITIAL_RESPONSE` roll over
  `m_iSeeUnknownRepeatSightings` +0x60a4 and `full_investigate` +0x6340 setting
  `ATTACK_UNKNOWN` / `IGNORE_UNKNOWN`; 2-D closing speed vs `20.0f` → `UNKNOWN_ADVANCING /
  HOLDING / RETREATING` 0x05–0x07 (`HOLDING` dead, reproduced); `INVESTIGATE_SIGHT` 0x26.
  Job: the sweep, the five conditions, the roll.
  Oracle: § "The three `GatherConditions` sweeps and the interest predicate".
  Size: M. Effort: Sonnet / high.
- [x] **10c. The comfort sweep.**
  Retail: `0x102b1a20`: `m_flNextComfortCheckTime` +0xe90 tested and re-armed `RandomFloat(0.2,
  0.4)` ahead of the idle test, so a non-idle NPC re-arms and draws; idle: the nearest
  comfort-list member (`0x10323630` / `0x10323770`) at `<= 1024` units, self skipped, a tie to
  the later entry; `!IsBusyWithDiscipline` and an installed schedule; a running `0x12f` compares
  `m_hTargetEnt` with the comforter and `TaskComplete(false)` (`0x10273e80`) on a change; an NPC
  comforter must share the connected squad (`+0x94` → `+0x5bb0 < 1 ? +0x5da4 : 0`, both sides);
  the comforter's `m_iComfortingCount` +0xe94 `>= 3` skips with no fallback; `COMFORT` 0x27 and
  `SetTarget` (`0x10279cc0`, `m_hTargetEnt` +0x5ce4); the tail completes a running `0x12f` in
  every other case. `BuildScheduleTestBits` withholds it while `COWERING`. `m_hTargetEnt` feeds
  the gaze cascade's target arm. Both fields saved (`ComfortSweep` schema).
  Oracle: § "The comfort sweep `0x102b1a20`, walked". The squad test answers through 17's null
  squad; the `0x12f` arms fire once 10i registers the program.
- [x] **25. The kernel's failure route and the random wait.**
  Retail: `MaintainSchedule` (`0x102817c0`) takes the fail route on `COND_TASK_FAILED 0x5c` with
  the state unchanged and no door block: slot 439 `GetFailSchedule` (`0x1028abe0`, no override
  on any of 79 classes) answers `m_failSchedule` (+0x5c54, zeroed by every `SetSchedule
  0x10280e50`) or base **`0x43 FAIL`**: `STOP_MOVING; SET_ACTIVITY ACT_IDLE; WAIT 1; WAIT_PVS`,
  interrupts `CAN_RANGE_ATTACK1/2 CAN_MELEE_ATTACK1/2 GIVE_WAY`; the install goes through
  `SetSchedule(int)` (`0x102cc1f0`: translate, then `GetScheduleOfType`; a missing program
  DevMsgs and installs base **1 `IDLE_STAND`** — `STOP_MOVING; SET_ACTIVITY ACT_IDLE; WAIT 5;
  WAIT_PVS`) and the same `do…while` keeps running it, so the route costs no think beyond the
  one the failure ended: a task failing inside the loop leaves its program installed (`TaskFail
  0x10273fc0` never touches the status word) and the loop exits at `0x102821ae`; the route runs
  at the top of the NEXT pass. Every other invalidity (state change, door block) goes to
  `SetIdealState` + `GetNewSchedule` instead.
  `TASK_WAIT_RANDOM` is task `0x67`, base arm `0x10283dae`: `curtime + RandomFloat(0.1, arg)`
  (a `0.00` operand still waits up to 0.1 s); `TASK_WAIT` (2) has no floor. `ClearSchedule
  0x10280d30` zeroes the six schedule words, clears `PRESERVE_PATH` and dispatches slot 435
  with `NULL`; reached from inside a task (0003), never from the think.
  Chain: `MaintainSchedule 0x102817c0` → (in-loop failure: `TaskFail 0x10273fc0` writes the
  reason and `0x5c`, leaves status `+0x5c44`, loop exits at `0x102821ae`) → next pass
  `IsScheduleValid 0x10280ff0` → `0x10281730` → slot 439 `GetFailSchedule 0x1028abe0` →
  `SetSchedule(int) 0x102cc1f0` → **slot 440 `TranslateSchedule`** (Troika `0x102b12f0`, base
  `0x102cc080`) → slot 446 `GetScheduleOfType 0x102cc260` (miss: literal 1, untranslated) →
  `SetSchedule 0x10280e50` (slot 435, conditions zeroed, `m_failSchedule = 0`).
  Job (landed): `FAIL` (0x43) and `IDLE_STAND` (1) registered from their blobs; a task failing
  inside the loop ends the pass with its program installed and the next pass's top arm routes it
  (both `TASK_FAILED` producers share the arm); the route's id goes through the runner's slot-440
  seam (`IElysiumScheduleRunner::TranslateSchedule`), Troika's `1/0x6b → 0x6b` arm on
  `FElysiumNpc`; `Start`'s miss arm installs `IDLE_STAND` untranslated after the trace row and
  tallies the unported program (`elysium.stubs`); `RandomSeconds` drawing `[0.1, Max]`; a
  runner-requested `ClearSchedule` honoured after the task step or at the next tick's top,
  superseded by an install, keeping `m_failSchedule`.
  Provides: the failure route every program family runs on (10g, 11, 10e, 10f, 21, 0003/1).
  **What a consumer observes:** a Troika NPC whose `m_failSchedule` is `Idle_Stand` (comfort,
  calmed, follow, disoriented, interesting-place) lands on `0x6b IDLE_DISPOSITION`, never on base
  `IDLE_STAND`; only the miss arm runs `IDLE_STAND`.
  Seams left: `0x132 LAUGHING` under `D_MILDLY_CRAZY` (21a); the frenzied pre-table
  `0x102b11c0` (`0xc7 → 0xc9`, `0xca → 0xcc`, …) and the twelve species overrides of slot 440
  (25b); the non-task `ClearSchedule` callers (25a); the loop's other exits (25c).
  Oracle: § "The `INVESTIGATE` family, decoded" → "The kernel's failure route and the base
  programs, walked". Unrecovered: the base `RunTask` arm that completes the base
  `GET_PATH_TO_*` tasks after a bare `SetGoal` (not on this story's path: the port's path tasks
  complete in their start arm).
  Size: S. Effort: Sonnet / medium — the oracle is complete and the gap is mechanical.
- [ ] **25a. The `ClearSchedule` producers.**
  Retail: `0x10280d30` has twelve direct callers (`thunk 0x10006a8c`). Task bodies:
  `CAI_BaseNPC::RunTask 0x10288780` and the scripted family (0003). Not task bodies:
  `0x102ae8e0` (stores `m_vSavePosition +0x5dd0`, sets the byte `+0x63e0`, clears), base slot
  420 `0x10273390`, Troika slot 379 `0x102b5c00`, `CCineNPC`/`CCineAI` slot 586
  (`0x101a8840`/`0x101a95d0`), `0x10084260`, `0x101a81a0`, `0x10265820`, `0x1027be60`,
  `0x102c6ff0`, `CNPC_VCamera 0x103692c0`. A clear from `StartTask` falls through to
  `m_pSchedule == NULL` in the same loop (`GetNewSchedule` + install, same think); one from
  `RunTask` exits at `status != 4` and reselects next think.
  Gap: `FElysiumNpc::RequestClearSchedule` is a seam no body calls; the kernel honours a request
  after the task step or at the next tick's top (both "same pass" shapes).
  Job: each caller recovered (what it clears for, what it writes beside the clear), wired to the
  seam or named as a later story's; the `StartTask`-vs-`RunTask` timing difference where the
  port's task bodies need it.
  Consumes: 25. Oracle: § "The kernel's failure route and the base programs, walked".
  Recovery banked 2026-09-13 (all twelve callers walked, in that section): `NPCInit` clears
  unconditionally on every class; Troika's grapple entry clears after the queued-burn refusal
  (`+0x65a8` ← `CreateDamageEffects 0x10330d00`), the dialogue abort and the cine cancel, and
  base entry `0x1026cdc0` is not type-gated (the port's stealth-only gate is a divergence);
  `OnRestore`'s `DiscardScheduleState` keep-rules; `npc_reset` reloads nothing; the CopGenerator
  chain to `+0x63e0`; `0x10084260` and `0x102c6ff0` are dead. The port work is the wiring above.
  Size: S. Effort: Sonnet / medium.
- [ ] **25b. Species `TranslateSchedule` overrides and the frenzied pre-table.**
  Absorbed by 29e (slot 440 and its twenty overrides are layer-21/22 bodies); this story keeps
  the index below and the registration of `0xc9`/`0xcc`/`0xf0` with their melee family.
  Retail: slot 440 is filled by `CAI_BaseNPCTroika 0x102b12f0` on 62 classes and overridden by
  `CNPC_VAsianVampire 0x10362910`, `CNPC_VBach 0x10363a30`, `CNPC_VChangBros 0x1036b460`,
  `CNPC_VCop 0x10372150`, `CNPC_VDog 0x10374370`, `CNPC_VFrenzyShadow 0x10375f20`,
  `CNPC_VGargoyle 0x10378a30`, `CNPC_VGuard1 0x1037d240`, `CNPC_VHengeyokai 0x1037ffa0`,
  `CNPC_VHunter 0x10388a40`, `CNPC_VMingXiao 0x10394570` / `Tentacle 0x1039e2d0`,
  `CNPC_VSabbatLeader 0x103a7390`, `CNPC_VScurrying 0x103ac490`, `CNPC_VSheriffMan 0x103b0320`,
  `CNPC_VTzimisce 0x103bd390` / `HeadClaw 0x103c1720` / `Runner 0x103c3560`,
  `CNPC_VWerewolf 0x103d5e00` (its own `0x43` arm), `CNPC_VZombie 0x103df580`. Troika's frenzied
  pre-table `0x102b11c0` (`m_bfNPCFrenziedFlags & 0x100`): `0xc7 → 0xc9`, `0xca/0xcb/0xd1/0xd2 →
  0xcc`, `0xef → 0xf0`, `0x87/0x88 → 0x7d/0x7e` by slot 168 and flags1 `0x1000`.
  Gap: `FElysiumNpc::TranslateSchedule` carries Troika's `1/0x6b` arm; the frenzied arm is a
  named seam (identity, tallied); no species override exists.
  Job: an index of the overrides so none is silently identity; each lands with its species
  story; the frenzied targets `0xc9`/`0xcc`/`0xf0` registered with the melee family that owns
  them.
  Consumes: 25. Oracle: § "Species slot-435 overrides all chain" gains a slot-440 twin.
  Size: XS now (the index), grows per species. Effort: Sonnet / low.
- [ ] **25c. `MaintainSchedule`'s other exits.**
  Absorbed by 29e (`MaintainSchedule 0x102817c0` is walked whole there); this story keeps the
  door-block gate, which waits on 11's door selector.
  Retail: `m_pSchedule == NULL` → `GetNewSchedule 0x102814d0` + install in the same loop; an
  installed schedule with zero tasks → `"ERROR: Missing or invalid schedule"` and `SetState(1)`
  through slot `0x4d8`; the `ai_step` debug return; the door-block local from flags2 `0x200`
  against `m_hBlockedDoor +0x5d28` that withholds the fail route.
  Gap: the kernel's `Schedule == nullptr` arm answers `TaskFail(0x05)` (`ElysiumSchedule.cpp`,
  the "ran off the end" arm), which is not a retail exit; the door-block gate on the route is
  not reproduced (the port's door selector runs elsewhere).
  Job: the null-schedule arm as a reselect, the zero-task arm as the state push, the door-block
  gate on the top arm.
  Consumes: 26 (`GetNewSchedule`), 11 (the door selector). Oracle: § "The kernel's failure route
  and the base programs, walked".
  Size: XS. Effort: Sonnet / low.
- [ ] **22. `sp_tutorial_1` on the V2 lane.**
  Job: the map baked on the V2 lane so the light query reads its 396 worldlights (1), the nine
  hull-0 Jump links `22, 24, 30, 88, 110, 115, 147, 163, 218` exist as link actors (19) and 24's
  link check has a graph to walk. Until then 13's stuck-on-top failure is unreachable in the
  tutorial. No corpus item: a pipeline story (the V2 lane's bake), gap stated by 24 and 19.
  Size: S–M. Effort: Sonnet / medium.
- [ ] **24. Reachability: the graph's components on the runtime mesh.**
  Retail: a route exists when the `.ain` graph has a node path for the hull, and for nothing
  else — navigator `SetGoal` (`0x102ecd20`) refuses otherwise and every path task answers
  `TaskFail(0x0c)`. The tutorial graph is ten components (27/23/18/16/10/7/6/3/3/3 nodes);
  Jack's start area has no node within 6000 units and the Society hub none at all, so in
  retail Jack's walk to `ip_by_window` / `ip_lean_1` and every hunter's route fails at `SetGoal`
  and runs the program's failure route. Hull 0 stands `(-13,-13,0)..(13,13,72)` (66 × 183 cm),
  steps 18 units (45.7 cm); links carry per-hull ground/jump masks, `linkInfo & 0x1000` is
  never set at build; doors are not graph cuts: `CAI_Node::InitLinks` (`0x102fb4e0`) probes
  every link with mask `0x2000b` (`SOLID|WINDOW|GRATE|MONSTERCLIP`), which excludes
  `MOVEABLE`, and marks a hull-0 ground link that a `0x2000`-mask hull trace hits with
  `linkInfo |= 0x2000` (the door-on-link mark; the NPC opens the door: `m_hBlockedDoor`,
  `SelectDoorObstructionSchedule 0x102b7370`, `IGNORE_DOOR_FAILURE`). **`MONSTERCLIP` cuts
  links at graph build**: the fit, stand, walk and jump probes all carry `0x20000`.
  Port: a Recast projection of the `.hulls` sidecar (world brushes, player-blocking contents,
  **monsterclip excluded** — `ElysiumMapCollision.cpp:285`, `UE_bsp_to_scene.py:135`,
  `UE_map_sidecars.py:70`) at the engine's default agent — `DefaultEngine.ini:73-75` sets only
  `RuntimeGeneration` and `bForceRebuildOnLoad`, no agent radius/height/step — built at
  activation (`ElysiumMapActorLifecycle.cpp:840`); the nine hull-0 jump links are proxies (19,
  `ElysiumNavJumpLink.h:12`); `FindPathSync` is called once, for the jump-link check
  (`ElysiumNpcBody.cpp:617`), and partial paths are refused by every NPC request
  (`ElysiumNpcBody.cpp:545`, callers `ElysiumNpc.cpp:614/2535/2817/3337`). Its connectivity is
  tied to nothing retail authored: on `sp_tutorial_1` it refuses Jack's and the hunters' routes
  as retail does, by coincidence of geometry (partial paths 160–240 m short, 2026-09-12); a
  retail ground link over a 45.7 cm riser is a false refusal at step 35; a mesh that joins two
  retail components walks an NPC where retail stands him idle; and a monsterclip volume retail
  authored to keep NPCs out is open ground in the port.
  Job: the agent from hull 0 in `DefaultEngine.ini`'s `RecastNavMesh` block; monsterclip
  brushes added to the NPC-blocking geometry of the mesh projection (a second projection or a
  per-agent area, the player's collision untouched); a game-side check that every enabled
  hull-0 ground link of the map is walkable on the built mesh (`FindPathSync` over the link
  list, reported like 19's staging); the reachability gate — a request whose start and goal
  fall in different hull-0 components (nearest node per end, the components baked beside 19's
  links) is refused before Recast is asked, so `TaskFail 0x0c` fires where retail's does, the
  mesh supplying only the geometry inside a component; door brushes verified not to cut the
  mesh. Decision for the owner: the gate is the retail contract; naming wider reachability a
  modernization instead means NPCs retail stands idle (Jack at the tutorial start) walk off in
  the port.
  Provides: the refusal 10g, 11, 10e/10f and 0003's walks fail through. Consumes: 19's bake.
  Oracle: `navigation-jump-links.md` § "Tutorial connectivity: the graph's components" (incl.
  "Closed 2026-09-12: `MONSTERCLIP` cuts links at graph build"). Unrecovered: the navigator's
  reader of `linkInfo & 0x2000`, the name of contents bit `0x2000` in this engine's
  `bspflags`, whether door brushes cut the port's mesh (a witness, not a corpus item).
  Size: M. Effort: Opus / high.
- [ ] **10g. The patrol programs.**
  Retail: a patrol is a `CAI_PatrolPath` object in the `+0x658c` cell (`+0x6590` pointer;
  `+0 type`, `+4 schedule id`, `+8 repeat`, `+0xc count`, `+0x10 index`, `+0x14 nodes[]`),
  built only by three inputs: `SetupPatrolType "<repeat> <type> <schedule>"` (`0x1029eb30`;
  type 0 loops forward, 1 backward, 2/3 ping-pong, `repeat` wraps allowed before the path is
  spent; the schedule by name, `SCHED_%s`, `SCHED_TROIKA_%s`), `FollowPatrolPath "<node…>"`
  (`0x1029ed90`; nodes by `info_node_patrol_point`/`info_node_hint` name, type `10000 || 800`;
  an existing object keeps its type/repeat/schedule) and `WalkToNode "<schedule> <node>"`
  (`0x1029e840`). The builder `0x1029f460` **installs the object's schedule at once**
  (`0x102ae750` → `SetSchedule`, refused only while dead) when the id is non-zero; a path built
  with schedule 0 is discarded by the idle selector with `"WARNING: Patrol path for '%s' has no
  schedule."`. `SelectSchedule` case 1 step 3 returns `path->+4` verbatim after the interest
  roll: `0x46 IDLE_PATROL` (`TASK_PATROL_PATH; WAIT_FOR_MOVEMENT; WAIT_PVS`), one of
  `0x64/0x66/0x68 INVESTIGATE_NODE/_WALK/_HUNT` or `0x65/0x67/0x69 FOLLOW_PATROL_PATH/_WALK/
  _HUNT` (six identical lists: `SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_PATROL_POINT;
  [SET_NPC_FLAG FORCE_RELAXED_ANIMS; RUN_PATH | WALK_PATH | WALK_PATH_HUNT]; WAIT_FOR_MOVEMENT;
  FACE_PATROL_INTEREST; DO_PATROL_INTEREST_ACTIVITY; NEXT_PATROL_POINT`, none with a fail
  schedule, so a refused point runs `FAIL` — 25 — and the selector re-picks the same point one
  second later). An alert or combat program replaces the patrol through `SetSchedule` and case
  1 re-selects it when that program ends. `GET_PATH_TO_PATROL_POINT 0x7a` (`0x102aa640`): no
  object **or a current node id of −1** → `TaskFail(0x1d)`; else the node's hull position
  (`0x102fb0d0`) as `AI_NavGoal_t{type 4, tolerance −1, flags −1}` into `SetGoal(…, 2)`;
  success → `TaskComplete`, refusal → `"%s can't reach patrol point"` + `TaskFail(0x0c)`.
  `NEXT_PATROL_POINT 0x7d` (`0x102aa9e0`): advance; a spent path is freed; the interest roll;
  complete. `FACE_PATROL_INTEREST 0xb3` / `DO_PATROL_INTEREST_ACTIVITY 0xb5` complete on their
  first think when the node carries no interest record (27 owns the record arm). `sp_tutorial_1`
  sends `SetupPatrolType` then `FollowPatrolPath` to `sentry2` and `monk_upstairs_podium`.
  Gap: the port has no path object; `InputSetupPatrolType` / `InputFollowPatrolPath`
  (`ElysiumNpc.cpp:499`, `:506`) write `PatrolType` / `PatrolPath` strings (`ElysiumNpc.h:212-213`)
  and set `bPatrolActive` (`ElysiumNpc.h:1066`); `WalkToNode` has no input; the executor
  `ThinkPatrol` (`ElysiumNpc.cpp:1734`, called `:1348`) and `IssuePatrolMove` (`:604`) re-issue a
  refused point every think; the `Patrol` body owner (`ElysiumNpcMindTypes.h:24`) and its
  suspend/resume path in `ThinkSchedulePolicy` (`ElysiumNpc.cpp:1292`); `SelectIdleSchedule`
  returns `None` on `bPatrolActive` (`ElysiumNpc.cpp:1396`) and `SelectAlertSchedule` at `:1472`;
  no program in the registry (`ElysiumSchedule.cpp:224-273`, `ElysiumNpcCombatSchedules.cpp`)
  carries a patrol id; no `GET_PATH_TO_PATROL_POINT`, `NEXT_PATROL_POINT`, `FACE_PATROL_INTEREST`,
  `DO_PATROL_INTEREST_ACTIVITY`, `FACE_IDEAL`, `WALK_PATH`, `WALK_PATH_HUNT` or `PATROL_PATH` task
  (`EElysiumTask`, `ElysiumSchedule.h:26-111`); no immediate install from the input;
  `ScheduleHost.bPatrolPathUseHint` (`ElysiumNpcScheduleHost.h:40`) is written and read by nobody.
  Job: the path object with its type table and `NextPoint`; the three inputs building it and
  installing its schedule at once; the seven programs (`0x46`, `0x64–0x69`) and the tasks
  `0x7a`, `0x7d`, `0xb3`, `0xb5` (no-record arm), `FACE_IDEAL 0x2b`, `WALK_PATH 0x23`,
  `WALK_PATH_HUNT 0x104`; the id read in the idle selector (step 3) with the interest roll;
  running under the ordinary `Schedule` body owner. Retires `ThinkPatrol`, `IssuePatrolMove`,
  `bPatrolActive` / `bMoveIssued` / `bWalkingAnimation`, the `Patrol` body owner and its
  suspend/resume path in `ThinkSchedulePolicy`, and the idle and alert selectors' `None`
  return on `bPatrolActive`; the saved route state moves into the schedule block. Save files
  are disposable.
  Decided 2026-09-12: patrol and interesting places (11) become kernel programs here, before
  10d; no selector returns `None` to defer to an executor after this story, and no routing
  branch is added to `ThinkSchedulePolicy` to bridge one.
  Consumes: 25 (the `FAIL` route), 24 (the refusal). Provides: the path object and the
  interest roll to 27 and the hunt cell to 10h.
  Oracle: § "The `INVESTIGATE` family, decoded" → "Patrol paths, walked", "The kernel's
  failure route and the base programs, walked"; § "Followers, patrols, and loitering".
  Unrecovered: `TASK_PATROL_PATH`'s navigator call after its activity pick (`0x46` only),
  `TASK_GET_FULL_PATROL_PATH 0x7c`'s body (no shipped program uses it).
  Size: L. Effort: Opus / high.
- [ ] **11. Interesting places: the selector arms.**
  Retail: arms `0xff SETUP` / `0x100 WALK` / `0x102 CROSSWALK` / `0x105 LOITER` / `0x106
  INTERACT`; the last two unreachable (no setter for `SHOULD_LOITER`, no caller pairs NPCs for
  `SHOULD_INTERACT`; do not invent one); `group_id` and `interesting_place_groups` are 1-based
  index lists → masks, empty `interesting_place_groups` matches nothing; eligibility
  `0x102dad60`; `RandomFloat(min_time, max_time)` into `m_flWaitFinished`; `Enable`/`Disable`
  on the place. The visit is a schedule: an alert or combat program replaces it through
  `SetSchedule` and case 1 re-selects a place when that program ends. The place's disable/kill
  walk over its visitors (`0x102daac0`): a visitor carrying `DISAPPEAR` gets `flags2 |=
  0x80000008`, any other `TaskFail(0x23)` (slot 448); both arms then dispatch slot 614
  (`ResetThinkTimers`) on the visitor. The failed walk: `GET_PATH_TO_INTERESTING_PLACE`
  (Troika `StartTask` case 0x31) → no held place `TaskFail(0x22)`, `SetGoal 0x102ecd20`
  refused `TaskFail(0x0c)`; `TaskFail`'s step 1 (`0x102b53d0`) releases the visit; the
  program's route is `_FAILED` (`0x105e6520`: `SET_ACTIVITY ACT_IDLE; WAIT 5; WAIT_RANDOM 5;
  SET_SCHEDULE SETUP`) — one attempt every 5.1–10 s, idle between, the pick keeping no memory
  of a failed place, so the same top-rated node again, forever.
  Job: each of the three reachable arms and its program compared against retail and ported,
  `_FAILED` included; the masks; the wait; the arms read in the idle selector (step 4),
  running under the ordinary `Schedule` body owner. Retires the port's ambient executor
  (`ThinkAmbient`, `EAmbientPhase`, `ClaimAmbientSpot` / `BeginAmbientUse` /
  `BeginAmbientLeave` / `FinishAmbientUse`, the `Ambient` body owner and its finish-on-claim
  path in `ThinkSchedulePolicy`) — and with it the port-only `FailedSpotIndices` and its
  every-think retry (2026-09-12: `Jack`, groups 32, alternates `ip_by_window` / `ip_lean_1` at
  0.1 s; `mercenary_upstairs`, groups 8, cycles its three on 16 s) — and the idle and alert
  selectors' `None` return on `bUseInteresting`; the held place index and the wait deadline
  become the program's operands in the schedule block. Same decision as 10g. Consumes 24: the
  refusal those witnesses hit is retail's own (no node path), and stays so only through the
  gate.
  Gap: the executor is whole — `ThinkAmbient` (`ElysiumNpc.cpp:3311`, called `:1352`),
  `EAmbientPhase` (`ElysiumNpc.h:1084`), `ClaimAmbientSpot` (`:1787`), `BeginAmbientUse`
  (`:3221`), `BeginAmbientLeave` (`:3258`), `FinishAmbientUse` (`:3271`, 15 call sites),
  `FailedSpotIndices` (`ElysiumNpc.h:1091`, `ElysiumNpc.cpp:1810/3327-3376`), the `Ambient`
  body owner (`ElysiumNpcMindTypes.h:25`); `SelectIdleSchedule` returns `None` on
  `bUseInteresting` (`ElysiumNpc.cpp:1396`); the keyfields exist (`use_interesting`
  `ElysiumNpcClasses.cpp:125`, `interesting_place_groups` `:135`, mask at `ElysiumNpc.cpp:1849`);
  no `0xff/0x100/0x102/0x105/0x106` or `_FAILED` program and no `FIND_INTERESTING_PLACE`,
  `GET_PATH_TO_INTERESTING_PLACE`, `SET_PRESERVE_PATH`, `FACE_INTEREST`, `DO_INTEREST_ACTIVITY`,
  `PAUSE_MOVING`, `FACE_NEXT_NODE`, `WAIT_INDEFINITE` task in `EElysiumTask`. The shared entry
  `0x102a9f40` / loop `0x102aa210` / release `0x102da600` that 27's patrol arm also calls have
  no port function; the `Idle_Stand` fail schedule is 25's route, which lands a Troika NPC on
  `0x6b IDLE_DISPOSITION` (25's "what a consumer observes").
  Oracle: § "Interesting places: the selector, the programs, the wait" (incl. "The failed
  walk, walked"), § "Interesting-place eligibility". Provides: the entry/loop/release trio to 27.
  Size: L. Effort: Opus / high.
- [ ] **27. Patrol-point interest records.**
  Retail: an `info_node_patrol_point` may carry an interest record at `node->+0xa0`
  (`+0x468` the name of a `CAI_InterestingPlace`, `+0x46c` a 0–99 chance). `0x1029f650` rolls
  `m_bPatrolInterest (+0x65a0) = RandomInt(0,99) < chance` at each selection of the patrol
  program and at every `NEXT_PATROL_POINT`; `0x1029f730` resolves the record (cached `+0x659c`)
  only while the roll holds, `0x1029f780` its named place into `+0x6300`. `FACE_PATROL_INTEREST
  0xb3` (`0x102a63bd` / run `0x102ab974`): with a place whose `match_orientation +0x570` is set,
  `SetIdealYaw(hint yaw 0x102d12e0)`, turn activity unless `MEMORY:TURNING`, complete when
  `FacingIdeal` (`0x10278c80`), clearing both fields; otherwise clear both and complete.
  `DO_PATROL_INTEREST_ACTIVITY 0xb5` (`0x102a64a6` / run `0x102aba6c`): the interesting-place
  entry `0x102a9f40(place, record, 0)` (claim, INTO, `m_flWaitFinished = RandomFloat(min_time,
  max_time)`), the loop `0x102aa210` until it answers true, then holster per `place->+0x571`,
  `m_OnInterestingPlaceLeft` when arrived, release `0x102da600`, clear `+0x6300`/`+0x659c`/
  `m_bInterestingPlaceArrived`, complete.
  Gap: nothing — the port has no patrol node record, no `+0x65a0` roll and no place-facing.
  Job: the record on the decoded patrol node (the two keys), the roll at 10g's two sites, the
  record arm of the two tasks over 11's entry/loop/release.
  Consumes: 10g (the object and the roll sites), 11 (the entry, loop and release), 19's bake
  (the node record). Oracle: § "Patrol paths, walked" (The interest roll and the two interest
  tasks). Unrecovered: the `info_node_patrol_point` key names that fill `+0x468`/`+0x46c`; which
  shipped map authors one.
  Size: S–M. Effort: Fable / medium; corpus pass on the node keys first.
- [ ] **26. `GetSchedule`, the pre-selector.**
  Absorbed by 29e for its eight layer-19+ bodies (`0x102ae920`, base `0x1028a380`, the ideal-state
  arms); this story keeps the six leaf helpers (29c's verdicts) and the arms' consumer wiring.
  Retail: `GetNewSchedule` (`0x1028a260`) dispatches slot 437 (`CAI_BaseNPCTroika::GetSchedule`
  `0x102ae920`) and, only on 0, slot 438 (`SelectSchedule` `0x102af660`, the state cases 10d,
  10g, 11, 21a port; its `default:` is base `0x1028a380`, whose invalid-state and
  no-combat-schedule arms return `0x43 FAIL`). `0x102ae920` in order: `m_iForcedSchedule`
  consumed and returned; the connected-squad `flags2 & 0x2000` arm (17: `combat_start_activity`
  `+0x65e4 != -1` and not `frenziedFlags & 0x80` → 0xeb, else `SquadNewEnemy` `0x103161a0` with
  the enemy); `WAS_BUMPED` 0x38 → the discipline manager (`DAT_10739a4c`) removes every active
  effect whose HitGroup record byte `+0x33` is set (`0x101e3df0` → `RemoveEffect 0x101e3af0`),
  and a running `0x14a D_MESMERIZE` runs the `OnInterruptSchedule` HitInfo of every effect
  whose byte `+0x34` is set (`0x101e3ee0`) — neither returns a schedule; base `0x1028a2a0`:
  `NPC_FREEZE` 0x75 → 0x3a `NPC_FREEZE`, `ON_FIRE` 0x30 → 0x151 `ONFIRE`, `FLOATING_OFF_GROUND`
  0x73 → gravity 1.0, slot 0x340(0), 0x3e `FALL_TO_GROUND`; state 2:
  `SUPERNATURAL/CRIMINAL_ATTACK_LEVEL` 0x22/0x20 through `HasInterruptCondition` `0x10269d30` →
  `ReportSupernaturalAct` `0x1017f4a0` / `ReportCriminalAct` `0x1017f2a0` when the closest player
  is the offender, `SetEnemy` slot 0x950 and slot 0x954(offender, 5); `ON_FIRE` → 0x151; no
  enemy → ideal state 3 (1 under `no_alert_state`) and re-run; `ATTACK_UNKNOWN` `flags1 &
  0x800000` cleared → 0x5b unless `flags2` bit 7 or frenzied (10f); `NEW_ENEMY` 0x54 → 0xea
  `START_COMBAT` unless frenzied. Any state: `PLAYER_ON_HEAD` 0x3b and not busy → cleared and,
  with no live dialogue partner, the answer of 28. State 0xe: the criminal half, `ON_FIRE`, no
  enemy → state 3/1. Then the base again; then `flags2 & 2` → `flags1 &= ~8`, `flags2 &= ~2`,
  navigator goal type (`+0x5d34` → `+0x18`) 3 → 0xfc `FINISH_CLIMB` (`WALK_PATH;
  WAIT_FOR_MOVEMENT`), 1 → 0xfd `FINISH_JUMP` (`TASK_JUMP; TASK_LAND`); combat +
  `stay_entrenched` + slot 0x940 → `0x102b7690(1,0,0,0)` (12b); `flags1 & 2` cleared → 0xf1
  `STARTLED` (`WAIT_RANDOM 0.5; STOP_MOVING; SET_ACTIVITY ACT_WALK`; interrupts damage); idle:
  `KNOCKBACK` 0x28 → 0x14c `KNOCKBACK`, `COMFORT` 0x27 → 0x12f, `D_CALM` → 0x130 `CALMED`,
  `D_FOLLOW` → 0x131 `FOLLOW`, `D_POSSESSED` → 0x131, a live dialogue partner → 0x6a
  `RUN_DIALOG` (`TASK_RUN_DIALOG 0`, interrupt `PROVOKED`); combat: `KNOCKBACK` → 0x14c; alert:
  `0x102b8a10` (`ENEMY_DEAD` 0x58 with a `SelectWeightedSequence(0x61)` hit → 8);
  `m_fSavePositionWalk` → cleared, 0x89 `RUN_TO_SAVED`; else 0. `CALMED`/`FOLLOW` name
  themselves as their own second fail schedule, so an unreachable target re-tries at once.
  Gap: no pre-selector exists — `SelectSchedule` (`ElysiumNpc.cpp:1429`) runs the law branch
  (`ElysiumNpcWitness::SelectLawSchedule`, `ElysiumNpcWitness.cpp:597`, its one call at
  `ElysiumNpc.cpp:1452`) ahead of a three-way state switch (`:1459-1461`); no `ForcedSchedule`,
  no squad arm, no `WAS_BUMPED` consumer (`EElysiumNpcCond::WasBumped` exists,
  `ElysiumNpcConditions.h:78`), no base arms, no `0xfc/0xfd/0xf1/0x14c/0x130/0x131/0x6a/0x89`
  program in the registry; `Knockback` 0x28 is a condition identity only
  (`ElysiumNpcConditions.h:71`); `ScheduleHost.bSavePositionWalk` is written
  (`ElysiumNpc.cpp:2717`) and read by nobody; `D_POSSESSED` has one reader (`ElysiumNpc.cpp:3135`,
  the interest overlay); the dialogue partner arm has no `EElysiumScheduleId`.
  Job: the pre-selector as the kernel's first selection step, ahead of the state switch; the
  law branch moved into retail's state-2/0xe arms; the `WAS_BUMPED` arm as two calls into
  0006's effect store (a seam answering "no effects" until 0006 lands the two record bytes);
  the idle chain's six arms — 0x14c is 0005's knockback, 0x12f is 10i, 0x130/0x131 registered
  from their blobs (`0x105df780` / `0x105df4f8`) over 0006's `D_CALM`/`D_FOLLOW` flags and 16c's
  `D_POSSESSED`, 0x6a over the dialogue partner; the three base arms with their programs;
  0xfc/0xfd over the motor's goal type; 0xf1; 0x89 over `bSavePositionWalk` (10k's program);
  the `PLAYER_ON_HEAD` clear with 28's answer behind it; base `SelectSchedule`'s `FAIL`
  fall-throughs. Arms whose producer is another story's (the squad arm, 12b's chooser, 10f's
  0x5b) take that story's seam until it lands.
  Provides: selection to 10i, 10k, 16c, 28, 0006 (`CALMED`/`FOLLOW`), 0005 (the knockback
  install). Consumes: 17, 12b, 10f, 25 (`FAIL`), 0004 (the dialogue partner), 0006 (the flags
  and the effect record bytes).
  Oracle: § "`GetSchedule` `0x102ae920` runs ahead of `SelectSchedule`" (incl. "Story 26
  recovery"). Unrecovered: the HitGroup keys behind record bytes `+0x33`/`+0x34` (0006's
  table), `TASK_RUN_DIALOG`'s arm (0004), `TASK_MELEE_KNOCKBACK` (0005), the `ON_FIRE_*` trio
  and `TASK_JUMP`/`TASK_LAND` (the motor).
  Size: L. Effort: Opus / high.
- [ ] **28. The player-on-head answer.**
  Retail: `GetSchedule` (`0x102ae920`), any state: `HasCondition(PLAYER_ON_HEAD 0x3b)` and
  `!IsBusyWithDiscipline()` → clear it; with no live `m_hDialogPartner`, the ConVar
  `debug_player_on_head` (`0x10924508`, default `"3"`, bounded 0..3) picks `0 → 0x7b
  PLAYER_ON_HEAD_RUN` (`GET_PATH_TO_RANDOM_NODE 256; RUN_PATH; WAIT_FOR_MOVEMENT`), `1 → 0x79
  _DIVE` (`SET_FAIL_SCHEDULE _RUN; TASK_ATTEMPT_DIVE_SIDE 0; SET_SCHEDULE _RUN`), `2 → 0x7a
  _DIVE_FORWARD` (`SET_FAIL_SCHEDULE _DIVE; TASK_ATTEMPT_DIVE_FORWARD 0`), `3 → RandomInt(0,99)
  < 80 ? 0x79 : 0x7a`; none of the three interrupts on anything. Fourteen programs list
  `COND_PLAYER_ON_HEAD` as an interrupt (the interest, patrol, alert and follower families).
  Gap: no `PlayerOnHead` condition, producer, program or task in the port.
  Job: the condition and its producer, the ConVar, the three programs, `TASK_ATTEMPT_DIVE_SIDE
  0x108` / `_FORWARD 0x109` / `GET_PATH_TO_RANDOM_NODE 0x1f` (shared with 10k/21b), the
  interrupt on the fourteen programs that name it.
  Consumes: 26 (the arm), 0004 (the dialogue partner). Oracle: § "`GetSchedule` `0x102ae920`
  runs ahead of `SelectSchedule`" (Story 26 recovery). Unrecovered: the producer of
  `COND_PLAYER_ON_HEAD` (the contact test that sets it), the two dive arms.
  Size: M. Effort: Fable / medium; corpus pass on the producer first.
- [ ] **10i. The comfort program.**
  Retail: `SCHED_TROIKA_COMFORT` 0x12f (blob `0x105df9d0`), selected by 26 on `COMFORT` in
  idle: `SET_NPC_FLAG DONT_INVESTIGATE; SET_NPC_FLAG NO_DIALOG; SET_FAIL_SCHEDULE Idle_Stand;
  SET_TOLERANCE_DISTANCE 60; GET_PATH_TO_TARGET; RUN_TO_TARGET; WAIT_FOR_MOVEMENT; FACE_TARGET;
  PLAY_COMFORT_INTO; DO_COMFORT_LOOP; PLAY_COMFORT_OUTOF; WAIT 4; WAIT_PVS`; interrupts
  `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE GIVE_WAY HEAR_DANGER`.
  Tasks `GET_PATH_TO_TARGET` 0x15, `RUN_TO_TARGET` 9, `FACE_TARGET` 0x31; `PLAY_COMFORT_INTO`
  0xec (`0x102a51b3`): `act = ACT_COMFORT_INTO 0x106a + 3 × RandomInt(0, 1)`,
  `SelectWeightedSequence`, `SetIdealActivity` — **`ACT_COMFORT3_*` is never chosen**;
  `DO_COMFORT_LOOP` 0xed and `PLAY_COMFORT_OUTOF` 0xee share start arm `0x102a51e8`
  (`SetIdealActivity(m_Activity)`, the INTO→IDLE→OUTOF advance living in `SetIdealActivity`'s
  transition switch); the loop's run arm is a bare `RET` — **it ends only through the sweep's
  `TaskComplete(false)` arms** — and the INTO/OUTOF run arm `0x102ab83c` completes when the
  sequence finishes. While it runs, `CAI_BaseNPC::GatherConditions` (`0x1026ec30`) plays the
  idle sound through slot 0x7dc instead of 0x7a8, and `0x1027a420` rolls `RandomInt(0, 20)`
  instead of `(0, 999)`. `COND_GIVE_WAY` 0x68's reader is base idle `0x1028a380` → `0x38
  GIVE_WAY`, unreachable on a Troika NPC; no `SetCondition` site carries the literal.
  `SET_FAIL_SCHEDULE Idle_Stand` takes 25's route: through slot 440 a Troika NPC lands on `0x6b
  IDLE_DISPOSITION`, not base 1 (25's "what a consumer observes").
  Gap: `GatherComfort` and its two `0x12f` arms (`ElysiumNpcConditions.cpp:581-649`,
  `bTaskCompletedExternally` `:569`) and `SetTarget` (`ElysiumNpc.h:646`) exist; no `Comfort`
  program in the registry, no `GET_PATH_TO_TARGET`, `RUN_TO_TARGET`, `FACE_TARGET`,
  `PLAY_COMFORT_INTO/LOOP/OUTOF` task in `EElysiumTask`; `SpecialIdleActivity`'s idle-sound
  roll and slot are the port's own (the two readers unmodelled); no `GiveWay` condition
  identity.
  Job: the program registered; the six tasks with the arms above; the activity chain
  INTO→IDLE→OUTOF for the two comfort variants stated as the transition rows this task needs
  (a CHOSEN reproduction of the `SetIdealActivity` switch until it is read whole); the two
  0x12f readers; `GIVE_WAY` as an identity with no producer; the fail route through 25.
  Consumes: 10c (the target), 26 (selection), 0003/1 (`RUN_TO_TARGET`), 24 (the refusal), 25.
  Oracle: § "The comfort sweep `0x102b1a20`, walked" (What reads the result; "Stories 10i and
  10j recovery"). Unrecovered: `SetIdealActivity`'s (`0x10272650`) transition rows — the
  comfort chain is one of them.
  Size: M. Effort: Fable / medium; corpus pass on the transition switch first.
- [ ] **10j. `CheckTarget`.**
  Retail: `CAI_BaseNPC::GatherConditions` (`0x1026ec30`), after `ChooseEnemy`, on a live
  `m_hTargetEnt`: `CAI_Memory::CheckTarget` `0x10271d10` clears `HAVE_TARGET_LOS` 0x4b and
  `TARGET_OCCLUDED` 0x49, sets 0x4b when `FVisible(target, 0x2804091)` else 0x49; then
  `0x10271b10`: with a navigator goal (`+0x5d34`, goal type not 1 or 3, `0x102ee620` non-null)
  whose entity (`0x102ee160`) is the target and whose flags (`0x102ee640`) carry 4, a target
  farther than **80 units** (`_DAT_104454c8`) from the goal point (`0x102ee140`) re-paths
  (`0x10007b4e` → `0x102f1dc0`); a goal on another entity is re-pointed at the target with a
  zero offset (`0x102ed310`) and re-pathed. Slot 0x364 is `CBaseEntity::GetAbsOrigin`.
  Gap: no `CheckTarget`, no `HaveTargetLos`/`TargetOccluded` condition identity, no goal refresh
  (`ElysiumNpcConditions.h:36-146` lacks 0x4b/0x49); `TargetEnt` exists (`ElysiumNpc.h:1065`).
  Job: the two conditions and the goal refresh, over 10c's target and 0003/2's cine.
  Consumes: 10c, the motor. Provides: 0x4b/0x49 to 10i and 0003/2.
  Oracle: § "The comfort sweep `0x102b1a20`, walked" (Stories 10i and 10j recovery).
  Unrecovered: the goal flag 4's name; whether the comfort sweep's distance reads slot 217
  (`GetAbsOrigin`) or slot 220 — a re-read of `0x102b1a20`'s asm, not on this story's path.
  Size: S. Effort: Sonnet / medium.
- [ ] **10d. The alert selectors and the ladder.**
  Absorbed by 29e for `SelectSchedule` case 3; this story keeps the alert programs' blobs and
  the ladder's registration.
  Retail: `SelectSchedule` (`0x102af660`) case 3 runs regardless of what the NPC was doing:
  an investigate program replaces a patrol or interesting-place program through
  `SetSchedule`, and case 1 re-selects that program when the investigation ends (10g, 11).
  Case 3 in order: see-unknown selector
  `FUN_102b8a60`, damage `FUN_102b8c40` → 0x8a, `DETECTED_ATTACK` 0x0b → 0x56, door
  obstruction `FUN_102b7370`, sound selector `FUN_102b9060`, else 0x4b `ALERT_WAIT`. The ladder
  `FUN_102b8980` on `m_eAlertLevel` +0x63f4 (saved, zeroed only at Spawn: once per life;
  `full_investigate` jumps to 3): 0 → 0x4c `ALERT_TURN_TO_SOUND`, 1 → 0x4d
  `ALERT_STEP_TOWARDS_SOUND`, 2/3 → 0x51 + (`MEMORY:INVESTIGATING` 0x8000000 ? 1 : 0). The
  third-party tail `FUN_102b8d20` (D_HT → 0x89, D_FR → 0x73). `HasInterruptCondition` needs
  the bit in the running mask; `HasCondition` does not, and every selector mixes the two. The
  hunt-state case 0xb exists only under `debug_allow_npc_hunting` (default `"0"`).
  `CommitBestSound` writes `m_BestSound` +0x60b0 and mirrors it to `m_InvestigateSound` +0x60dc;
  `FUN_102b9060`'s `HEAR_WORLD` arm writes +0x60dc directly, bypassing the commit and leaving
  `m_hBestSoundSource` +0x5b78 stale. 10a collapsed both into one `Memory.BestSound`, which is
  unobservable only while +0x60dc has no reader. The ladder's programs: `0x4b ALERT_WAIT` =
  `TASK_RUN_DISPOSITION 5` (the selector sets `m_bGoToIdleState` and `m_bForceStateChange`
  beside it); `0x4c ALERT_TURN_TO_SOUND` = `PAUSE_MOVING; SET_NPC_FLAG NO_UNKNOWN_ATTACK;
  ALERT_LOOK_AT_BEST_SOUND; WAIT_RANDOM 0.5; PLAY_SOUND Target_Suspect; WAIT_RANDOM 1.5;
  UNLOOK_AT; CLEAR_NPC_FLAG NO_UNKNOWN_ATTACK`; `0x4d ALERT_STEP_TOWARDS_SOUND` = `STOP_MOVING;
  STORE_LASTPOSITION; REMEMBER INVESTIGATING; ALERT_LOOK_AT_BEST_SOUND; WAIT_RANDOM 0.5;
  PLAY_SOUND Target_Suspect; WAIT_RANDOM 1.5; SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_BESTSOUND;
  FACE_IDEAL; WALK_PATH_TIMED 2; SET_SCHEDULE ALERT_LOOK_AROUND` (two seconds of walking, no
  `WAIT_FOR_MOVEMENT`); `0x56 ALERT_TURN_TO_DETECTED_ATTACK` = `0x4c` with
  `ALERT_LOOK_AT_DETECTED_ATTACK`; `0x4e ALERT_LOOK_AROUND` (10e's exit) walks back to the
  stored position. `0x4c`, `0x4d` and `0x56` set no fail schedule (a refused
  `GET_PATH_TO_BESTSOUND` runs `FAIL`, 25). The damage answer `0x8a` is 10k.
  Gap: no alert selector, ladder or tail exists — `SelectAlertSchedule` (`ElysiumNpc.cpp:1465`)
  returns `None` at `:1472` or `AlertLookAroundNi` at `:1493`; no `AlertLevel`,
  `FullInvestigate` reader beyond the see-unknown sweep (`ElysiumNpcConditions.cpp:504`), no
  `Investigating` memory bit, no `0x4b/0x4c/0x4d/0x4e/0x56` program; `HasInterruptCondition` is
  on the kernel (`ElysiumSchedule.cpp:678`); `CommitBestSound` exists with no runtime caller
  (`ElysiumNpcSenses.cpp:936`); `BestSound` and `BestSoundSource` are one record
  (`ElysiumNpcSenses.h:138/143`); `NextInvestigateSoundTime` is gated (`ElysiumNpcConditions.cpp:682`).
  Job: the two selectors, the ladder, the tail, the case-3 order, the four programs
  `0x4b/0x4c/0x4d/0x56` with `RUN_DISPOSITION 0xba`, `PAUSE_MOVING 0xa6`,
  `ALERT_LOOK_AT_DETECTED_ATTACK 0xfd`, `WALK_PATH_TIMED 0x24`, `UNLOOK_AT`, `NO_UNKNOWN_ATTACK`
  set/clear; `m_eAlertLevel` saved and zeroed only at spawn; the `INVESTIGATING` memory bit; the
  +0x60b0 / +0x60dc split, before `FUN_102b9060` is wired; `CommitBestSound` wired at the
  selector's sites.
  Consumes: 25 (`FAIL`), 10e (`ALERT_LOOK_AROUND` and the look/sound tasks). Provides: the
  ladder's `m_eAlertLevel` to 10e; `0x8a`'s selector site to 10k.
  Oracle: § "The `INVESTIGATE` family, decoded" (Selection; "The alert programs, verbatim").
  Unrecovered: `TASK_RUN_DISPOSITION`'s arms (`0x102a49bc` / `0x102ab351`) beyond "the
  disposition stance machine for the operand's seconds".
  Size: L. Effort: Opus / high.
- [ ] **10e. The sound-investigation programs.**
  Retail: 0x50, 0x51, 0x52, 0x53, 0x54, 0x58 with their task lists and interrupt sets; 0x53
  and 0x54 are dead in code, reachable by name only. No `INVESTIGATE` program declares
  `DELAY_INTERRUPTS`.
  `0x4e ALERT_LOOK_AROUND`, their fail and exit program, is `REMEMBER INVESTIGATING;
  PLAY_SEQUENCE ACT_IDLE; SET_ACTIVITY ACT_ALERT_FIDGET_AGRO_LOOKAROUND; WAIT 5; PLAY_SOUND
  SUSPECT_GIVEUP; WAIT_RANDOM 1; SET_ACTIVITY ACT_IDLE; WAIT_RANDOM 0; SET_NPC_FLAG
  FORCE_RELAXED_ANIMS; WAIT_RANDOM 1; SET_TOLERANCE_DISTANCE 5; GET_PATH_TO_LASTPOSITION;
  WALK_PATH; WAIT_FOR_MOVEMENT; FACE_LASTANGLE; CLEAR_LASTPOSITION; FORGET INVESTIGATING`.
  `GET_PATH_TO_BESTSOUND 0x20` (base `0x10285df8`) issues `SetGoal` and returns without
  completing; the base `RunTask` decides the task.
  Gap: none of the six programs, none of the tasks (`EElysiumTask`, `ElysiumSchedule.h:26-111`
  has neither `StoreLastPosition` nor `PlaySound` nor `PlayCower`), no `LastPosition`/`LastAngle`
  fields on the NPC (`ElysiumNpc.h`; the enemy-memory `LastPosition` at
  `ElysiumNpcEnemyMemory.h:18` is 5's record), no `Investigating` memory bit, no `TASK_REMEMBER`
  operand table (`Remember` is inert, `ElysiumSchedule.h:82`).
  Job: the six programs and their tasks: `STORE_LASTPOSITION` 0x17, `GET_PATH_TO_BESTSOUND`
  0x20, `WALK_RUN_PATH_COMBAT_SOUND` 0xd7, `ALERT_LOOK_AT_BEST_SOUND` 0xf9, `PLAY_SOUND` 0x11e,
  `ADD_EVENT_EXPRESSION` 0xbe, `PLAY_COWER` 0xe6, `ALERT_LOOK_AT_UNKNOWN_ATTACKER` 0x148,
  `IGNORE_DOOR_FAILURE`, and `ALERT_LOOK_AROUND` 0x4e with `PLAY_SEQUENCE 0x52`,
  `GET_PATH_TO_LASTPOSITION 0x1c`, `FACE_LASTANGLE 0x11d`, `CLEAR_LASTPOSITION 0x18`, `FORGET
  0x6d`; `m_vecLastPosition`/`m_angLastAngle` on the NPC; the `REMEMBER`/`FORGET` bit table.
  Consumes: 10d (selection, `m_eAlertLevel`), 25 (`FAIL` for `0x4c/0x4d/0x56`). Provides: the
  look/sound/last-position tasks to 10d, 10f, 10h, 10k, 21b.
  Oracle: § "The `INVESTIGATE` family, decoded" (The programs, verbatim; Tasks the port lacks;
  "The alert programs, verbatim"). Unrecovered: the base `RunTask` arm that completes
  `GET_PATH_TO_BESTSOUND` / `GET_PATH_TO_LASTPOSITION` after `SetGoal` (goal active → complete,
  else fail — UNREAD), `PLAY_COWER`'s run arm beyond sequence-finished.
  Size: M–L. Effort: Fable / medium; corpus pass on the base `RunTask` path arm first.
- [ ] **10f. The unknown-investigation programs.**
  Retail: 0x59–0x63 with their task lists and interrupt sets; 0x61 and 0x63 dead in code;
  0x5b from `GetSchedule` (`0x102ae920`) in combat only under `ATTACK_UNKNOWN`; 0x62 and 0x58
  reached by `SET_SCHEDULE`/`SET_FAIL_SCHEDULE` only; 0x60/0x61 clear `NO_UNKNOWN_ATTACK` and
  0x62/0x63 clear `LOOKED_AT_UNKNOWN`.
  Gap: none of the eleven programs; the see-unknown sweep exists (`GatherSeeUnknown`,
  `ElysiumNpcConditions.cpp:408`, conditions 0x01–0x07/0x26 at `ElysiumNpcConditions.h:39-55`);
  no `LOOKED_AT_UNKNOWN`, `NO_UNKNOWN_ATTACK`, `FINISHED_IGNORE_UNKNOWN` flag reader or writer
  (the flag word carries the bits, 8); no `TASK_CLEAR_NPC_FLAG` (`ElysiumSchedule.h:101-103`
  states the false premise that no program clears a flag); `0x5b`'s `GetSchedule` site is 26's.
  Job: the eleven programs and their tasks: `LOOK_AT_BEST_UNKNOWN` 0xfc, `UNLOOK_AT` 0xfe,
  `GET_PATH_TO_BESTUNKNOWN` 0x79, `SET_PRESERVE_PATH` 0xc4, `WALK_PATH_HUNT` 0x104,
  `CLEAR_NPC_FLAG` 0x101, `GET_PATH_TO_LASTPOSITION` 0x1c, `WALK_PATH` 0x23, `FACE_LASTANGLE`
  0x11d, `CLEAR_LASTPOSITION` 0x18, `FORGET` 0x6d, `PLAY_SEQUENCE` 0x52; the selector
  `FUN_102b8a60` at 10d's step 1; `0x5b`'s arm in 26.
  Consumes: 10d (the selector's position), 10e (the shared tasks), 26 (`0x5b`), 25.
  Oracle: § "The `INVESTIGATE` family, decoded". Unrecovered: `LOOK_AT_BEST_UNKNOWN`'s look
  target (`0x102a55e3`) and the shared look run arm `0x102ab76a`'s head-turn virtual, the
  base `RunTask` path-completion arm (10e).
  Size: L. Effort: Fable / high; corpus pass on the two look arms first.
- [ ] **12a. The reaction keyfields.**
  Retail: `percent_occluded_*` normalized at Spawn to a cumulative ladder, `_chase` forced to
  100 and never compared, rolled only in the ranged occluded selector `0x102b8320`;
  `hint_groups` index list → mask, empty = all, `FValidateHintType` slot 566;
  `stay_entrenched` (+0x6435, input `StayEntrenched`, seven "keep my cover" readers);
  `combat_start_activity` (`TASK_PLAY_COMBAT_START_SEQUENCE` in 0xeb, squad-only);
  `bright_route_penalty` parsed and never read: no NavMesh light cost.
  Gap: none of the seven keys is parsed (`ElysiumNpcClasses.cpp` registers no
  `percent_occluded_*`, `hint_groups`, `stay_entrenched`, `combat_start_activity`,
  `bright_route_penalty`, `allow_kick_hint_use`); `stay_entrenched` is a stated NOT MODELLED
  arm of the interest predicate (`ElysiumNpcConditions.h:307`, `.cpp:258`); `npc_kickable` is
  parsed on the prop (`ElysiumPropClasses.cpp:117`).
  Job: the parse, the normalization, the readers named (the interest predicate's arm 2 closed).
  Oracle: § "The navigation and reaction keyfields", § "The cover and kick chooser, and the
  combat leftovers". Unrecovered: what authors hint type 800 (an authoring question; no arm of
  this story reads it).
  Size: S–M. Effort: Sonnet / medium.
- [ ] **12b. The cover and kick chooser.**
  Retail: `0x102b7690` gated on `CanSeekCover` slot 592 and `allow_kick_hint_use`; the
  physics-prop kick 0xa9 with its 10° predicate and the one-shot `npc_kickable` byte; the kick
  hints 0xa7/0xa8; `COND_KICK_PROP_INVALID` has no producer. `NPCThink`'s hint upkeep
  (`0x10292de0`, on every normal-due think): `m_flOccludedDelay` := the cover value with a hint
  held, else the normal one; a hint that fails `FValidateHintType` (slot 566), or, unless
  `stay_entrenched`, one whose cover object is my enemy while `COND 0x2e || 0x48` holds →
  `ClearHintNode(5.0)` + `SetCondition(COND_HINT_INVALID 0x29)`.
  The three programs verbatim: `0xa9` as above; `0xa7 HINT_KICK_OVER` = `SET_TOLERANCE_DISTANCE
  0; GET_PATH_TO_HINTNODE; SET_NPC_FLAG FORCE_RELAXED_ANIMS; RUN_PATH; WAIT_FOR_MOVEMENT;
  SNAP_TO_HINT; FACE_HINTNODE; CLEAR_NPC_FLAG FORCE_RELAXED_ANIMS; KICK_HINT`, `0xa8` the same
  with `FACE_ENEMY` and `KICK_HINT_AT`. Tasks: `GET_PATH_TO_KICK_PROP 0x111` (prop dead →
  `TaskFail(0x25)`, no enemy → `0x06`, goal = prop origin + 64 units away from the enemy,
  `SetGoal` flags `0xa`, completion left to the base `RunTask`); `SNAP_TO_KICK_PROP 0x112` (a
  liveness check only); `KICK_PROP 0x113` (`RestartIdealActivity(ACT_KICK 0xc84)`, the impulse
  `0x102b6890`: toward the enemy, yaw clamped ±20° about my facing, mass-scaled speed, z boost,
  then the handle cleared); `KICK_HINT 0x10f` (hint → the activity, else `TaskFail(4)`);
  `KICK_HINT_AT 0x110` (the same, then the hint's named physics object into the handle and the
  impulse). `COND_KICK_PROP_INVALID` has no producer.
  Gap: `ScheduleHost.KickProp` and `HintNode` / `HintReusableAt` exist as saved state
  (`ElysiumNpcScheduleHost.h:34`, `.cpp:13`) with a release at `ElysiumNpc.cpp:2739-2745` and
  `TaskFail`'s consumption of `bNpcKickable` (`ElysiumPhysProp.h:25`); no chooser, no
  `CanSeekCover`, no `allow_kick_hint_use`, no `m_flOccludedDelay`, no `ClearHintNode` function
  (`ElysiumNpc.cpp:870` names it in a comment), no `HintInvalid` / `KickPropInvalid` condition
  identity, no `0xa7/0xa8/0xa9` program or kick task.
  Job: the chooser, the three programs and their tasks with the arms above, the kickable byte,
  the hint upkeep arm and `COND_HINT_INVALID` (its other producers are `0x102d30b9` and the hint
  store's own), `KICK_PROP_INVALID` as an identity with no producer.
  Consumes: 12a (`allow_kick_hint_use`, `stay_entrenched`, `hint_groups`), 26 (the entrenched
  call site), 0005 (the prop impact's damage). Provides: the cover search to 10k.
  Oracle: § "The cover and kick chooser, and the combat leftovers" (incl. "Story 12b recovery").
  Unrecovered: `GET_PATH_TO_HINTNODE` / `SNAP_TO_HINT` arms (shared with the cover family), the
  cells `0x1049a1b0`, `_DAT_10451ad0`, `_DAT_10447ee0`, `_DAT_10457f60`.
  Size: M–L. Effort: Fable / medium; corpus pass on the two hint arms and the cells first.
- [ ] **10k. The saved-position programs: shot by unknown, run to saved.**
  Retail: `FUN_102b8c40` (alert step 2, hunt step 2): `HasInterruptCondition(LIGHT_DAMAGE 0x4c
  || HEAVY_DAMAGE 0x4d)` → `m_bCondTookDamage = 0`, `m_vSavePosition = m_vecLastDamagePosition
  +0x5b9c`, `0x8a SCHED_TROIKA_SHOT_BY_UNKNOWN`: `SET_NPC_FLAG DONT_INVESTIGATE;
  SET_TOLERANCE_DISTANCE 12; FIND_COVER_FROM_SAVEPOSITION; SET_NPC_FLAG FORCE_RELAXED_ANIMS;
  RUN_PATH; WAIT_FOR_MOVEMENT; GET_PATH_TO_SAVEPOSITION; FACE_PATH; CLEAR_NPC_FLAG
  DONT_INVESTIGATE;` two lookaround/idle wait pairs; `WAIT 20`; interrupts `NEW_ENEMY SEE_ENEMY
  SQUAD_SEE_ENEMY SEE_FEAR INVESTIGATE_SIGHT INVESTIGATE_SOUND DETECTED_ATTACK`. `0x89
  SCHED_TROIKA_RUN_TO_SAVED` (the `SEE_SOUND_SOURCE` third-party D_HT answer in alert and
  `GetSchedule`'s `m_fSavePositionWalk` answer): `SET_TOLERANCE_DISTANCE 24;
  GET_PATH_TO_SAVEPOSITION_LOS_NOATTACK; SET_NPC_FLAG FORCE_RELAXED_ANIMS; RUN_PATH;
  WAIT_FOR_MOVEMENT; SET_TOLERANCE_DISTANCE 24; GET_PATH_TO_SAVEPOSITION; WALK_PATH_HUNT;
  WAIT_FOR_MOVEMENT; SET_ACTIVITY ACT_ALERT_FIDGET_AGRO_LOOKAROUND; WAIT 3; WAIT_RANDOM 5;
  SET_ACTIVITY ACT_IDLE; WAIT_RANDOM 1; GET_PATH_TO_RANDOM_NODE 2048; WALK_PATH;
  WAIT_FOR_MOVEMENT`; interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR`. `0x84
  HUNT_RUN_TO_SAVED` is `0x89`'s first nine tasks.
  Gap: nothing — no `m_vSavePosition` consumer beyond the door programs, no cover-from-position
  task, no `0x8a/0x89/0x84` program.
  Job: the damage selector at 10d's and 10h's sites; the three programs; the tasks
  `FIND_COVER_FROM_SAVEPOSITION`, `GET_PATH_TO_SAVEPOSITION`, `GET_PATH_TO_SAVEPOSITION_LOS_NOATTACK`,
  `FACE_PATH`, `GET_PATH_TO_RANDOM_NODE 0x1f`; `m_vecLastDamagePosition` from 0005's damage.
  Consumes: 10d (the site), 12b (the cover search the find task shares), 0005 (the damage
  position), 26 (`m_fSavePositionWalk`). Oracle: § "The alert programs, verbatim".
  Unrecovered: the four task arms (`FIND_COVER_FROM_SAVEPOSITION`, `GET_PATH_TO_SAVEPOSITION`,
  `_LOS_NOATTACK`, `FACE_PATH`), `m_vecLastDamagePosition`'s writer.
  Size: M. Effort: Fable / medium; corpus pass on the four arms first.
- [ ] **10h. The hunt-investigation programs.**
  Retail: 0x7f, 0x80, 0x81, 0x82 and the hunt-state case 0xb order (raw `HEAR_*` accepted
  there, unlike alert); the expiry chain after the sound arms: `+0x6598 == 0` clears
  `MADE_HUNT_PATH`; clear → `m_flHuntExpireTimer <= curtime` → `0x85 HUNT_FINISH`, slot 168's
  enemy alive → `0x7c HUNT_SETUP`, else `0x7d HUNT_SETUP_NO_ENEMY`; set → `0x7e HUNT`
  (`GET_PATH_TO_PATROL_POINT_HUNT; WALK_PATH_HUNT; WAIT_FOR_MOVEMENT; NEXT_PATROL_POINT_HUNT`
  over the second `CAI_PatrolPath` in the `+0x6594` cell, built by `FIND_HUNT_PATROL_TARGET
  0xaf` / `CREATE_HUNT_PATROL_LIST 0xae`); `HUNT_LOOK_AROUND`, `HUNT_FAILED` and the turn pair
  verbatim in the oracle. Reached in retail only by script, by name, or by `DoFrenzy` (16c):
  `CNPC_VHuman::SelectIdealState` enters state 0xb only under `debug_allow_npc_hunting`
  (default `"0"`).
  Gap: no hunt state (`EElysiumNpcState`, `ElysiumNpcMindTypes.h:8-16`: Idle, Alert, Combat,
  Scripted, Prone, Dead), no `debug_allow_npc_hunting`, no hunt program or task; the `Hunt`
  identifiers in the substrate are the law's hunter pursuit (`ElysiumLaw.h:186-363`).
  Job: state 0xb with its `0x7f` state byte (15) behind the `"0"` default; the nine programs
  (`0x7c/0x7d/0x7e/0x7f/0x80/0x81/0x82/0x84/0x85`, `HUNT_LOOK_AROUND`, `HUNT_FAILED`); the hunt
  cell over 10g's object; the tasks `0x7b`, `0x7e`, `0xae`, `0xaf`, `GET_PATH_TO_LASTENEMY_LKP`,
  `SUGGEST_STATE 0x06`; case 0xb's order.
  Consumes: 10g (the path object), 10e/10f (the shared tasks), 10k (`0x84`), 16c (`DoFrenzy`'s
  entry), 15 (the state byte). Oracle: § "The `INVESTIGATE` family, decoded" (Case 0xb; "The
  hunt programs and the expiry chain, verbatim"). Unrecovered: the two list builders' node
  choice (`0x10306700` / `0x10306f60`), `GET_PATH_TO_LASTENEMY_LKP`'s arm,
  `m_flHuntExpireTimer`'s writer.
  Size: M–L. Effort: Fable / medium; corpus pass on the list builders first.
- [ ] **13b. The leak in the defect catalogue.**
  Job: the `TaskFail` obliviousness leak as an entry in `docs/vtmb/retail-defects.md`, from
  the oracle section above. Gap: the file carries no "oblivious", "MADE_OBLIVIOUS" or
  "m_iIsOblivious" today (2026-09-12).
  Size: XS. Effort: Haiku / low.
- [ ] **16a. Followers.**
  Retail: keyfields `follower_boss` (+0x6478 → `m_hFollowerBoss` +0x647c) and `follower_type`
  (+0x6480); `SetFollowerBoss` (`0x102c44e0`, refuses self and squad members) through the
  `!player`/`!self`/`!enemy` resolver and the entity input (`0x102c3350`);
  `CNPC_VPedestrian::Activate` clears it; `Npc_Follower_Info` from `Rules.txt` (`0x102c4680`:
  back-away / walk-to / run-to per type, `walkTo ≥ backAway + overlap`, `runTo ≥ walkTo +
  overlap`); slot 607 (`0x102b93c0`) as the idle selector's step between busy/choreo and patrol
  (distance² vs +0x6484 → 0x10c, +0x648c → 0x113, +0x6488 → 0x112, else 0x115); the four
  programs — ten blobs from `0x105e3100`: `0x115 WAIT`, `0x113 FOLLOW_RUN`, `0x112
  FOLLOW_WALK`, `FOLLOW_FAILED`, `0x10c BACKAWAY` → `BACKAWAY_FAILED` → `BACKAWAY_ASTAR` →
  `_ASTAR_FAILED` (`BACKAWAY_NODE`/`_NODE_FAILED` by name only) — every one interrupting on the
  common alert set plus `COND_INSIDE/OUTSIDE_INTERRUPT_DIST_F` 0x19/0x18, which the tasks
  `SET_INSIDE/OUTSIDE_INTERRUPT_DIST DIST:…` arm from the `DIST:` operand vocabulary
  (`FOLLOWER_DISTANCE_BACKAWAY/WALKTO/RUNTO`, `OVERLAP = 10.0`, `ACCUM` via
  `SET/ADD/SUB_SPECIAL_DISTANCE_ACCUM`); `TASKS_FACE_TARGET` set by the wait/failed programs;
  tasks `0x86 FIND_FOLLOWER_BACKAWAY_SIMPLE` (a point `backAway` units from the boss on a
  ±45° jittered away direction, walk-probed, fail 7), `0x87 _NODE` (`0x102edae0`), `0x88
  _ASTAR` (`0x102edbb0`), all `TaskFail(0x29)` when the boss is dead; `SetFollowerBoss`
  (`0x102c44e0`) also runs the `SetEnemy(NULL)`/`SetTarget(NULL)` bundle and sets
  `frenziedFlags |= 0x3008`; `Npc_Follower_Info` clamps `walkTo ≥ backAway + 10`, `runTo ≥
  walkTo + 10`.
  Gap: a comment stub in the idle selector (`ElysiumNpc.cpp:1388`) and the `Follower` body
  owner the mind refuses (`ElysiumNpcMindTypes.h:28`, `ElysiumNpcMind.cpp:136`,
  `ElysiumNpc.cpp:4174`); `InsideInterruptDistanceSqr`/`OutsideInterruptDistanceSqr` are saved
  (`ElysiumNpcScheduleHost.h:25-26`) with a reset at `ElysiumNpc.cpp:2697` and one reader at
  `:3098`; no `follower_boss`/`follower_type` keyfield, no `Npc_Follower_Info` reader, no
  `FollowerBoss` field, no condition 0x18/0x19 identity, no program, no `DIST:` operand.
  Job: the keyfields and setter, the rules table, the idle step, the two conditions and their
  `DIST:` producers, the ten programs and the three find tasks, the accumulator; `GetFollowerBoss`
  as 6b's arm-4 producer.
  Consumes: 25 (`FAIL`), 24 (the refusal), 0006 (the possession caller). Provides:
  `m_hFollowerBoss` to 6b, 9, 16b, 16c.
  Oracle: § "`m_hFollowerBoss` — the follower controller" (incl. "Story 16a recovery"),
  § "Followers, patrols, and loitering". Unrecovered: the arms of the five `DIST:` tasks and
  the accumulator's offset, `TASKS_FACE_TARGET`'s bit and reader.
  Size: L–XL. Effort: Fable / high; corpus pass on the `DIST:` tasks first.
- [ ] **16b. The composed relationship and the human ideal state.**
  Absorbed by 29e for `SelectIdealState` (`0x1026f660` / `0x102ad660` / VHuman `0x103851e0`);
  this story keeps the composed relationship and its consumers.
  Retail: `IRelationType` (`0x10299da0`): self → D_ER; a `D_INSANE` target with my closest
  player not hated and not my enemy → D_HT; target's boss hated or my enemy → D_HT; my boss ==
  target → D_LI; else inherit `boss->IRelationType(target)`, upgraded to D_HT if either hates
  or targets the other; else the base table. `CNPC_VHuman::SelectIdealState` (`0x103851e0`):
  enemy gone → follower to alert (idle under `no_alert_state`), non-follower to hunt only under
  `debug_allow_npc_hunting` `"0"`. The player's action state (`0x101755d0`) reads a follower
  as ally. The `D_INSANE` arm reads the target's cached `CBaseCombatCharacter*` (`+0x9c`) and
  applies only while my closest player is live and not hated / not my enemy.
  Gap: a flat table — `IRelationType` exists only in comments (`ElysiumFeedSchedules.h:14`,
  `ElysiumNpcConditions.h:156`); `D_INSANE` is a flag identity (`ElysiumNpcFlags.h:119`) with no
  reader; `SelectIdealState` (`ElysiumNpcConditions.cpp:1217`, base `:1164`) has no follower
  arm; no ally read on the player.
  Job: the composition routed through the feed guard and `GatherSight`; the ideal-state arm;
  the ally read.
  Consumes: 16a (the boss), 16c (`D_INSANE`'s producer). Provides: the composed relation to
  0005, 0006 and the target HUD.
  Oracle: § "`m_hFollowerBoss` — the follower controller", § "Relationship table, exactly
  decoded" (incl. "Story 16b recovery"). Unrecovered: nothing on the composition; the ally
  read `0x101755d0` keeps its summary.
  Size: M. Effort: Opus / medium.
- [ ] **17. Squads.**
  Retail: one shared `AI_Enemies` memory. Joining (`squadname` + `bits_CAP_SQUAD`, `InitSquad`
  `0x10273d30` / `SetSquad` `0x1029a930`) points `m_pEnemies` at `squad+8`; `GetEnemies()`
  (`0x10273e10`) diverts to the global `g_DisconnectedEnemies` while `m_iSquadDisconnected` (a
  refcount) is nonzero; `DisconnectFromSquad` (`0x1026d050`) wipes that global and increments;
  `ReconnectToSquad` (`0x1026d0c0`) decrements, re-adds at 0, clears `D_DISCONNECT_SQUAD`;
  `LeaveSquad` is empty. `COND_SQUAD_SEE_ENEMY` 0x31 producer `0x102b2730` (someone sharing
  my memory saw him in the last 0.2 s); `TASK_SQUAD_NEW_ENEMY` 0x138 → `SquadNewEnemy`
  `0x103161a0`; `TASK_DISCONNECT_FROM_SQUAD` 0xf5. 16 members, the 17th overwrites the 16th;
  `GetMember` returns NULL for all when member 0 is disconnected; `Event_Killed` compacts;
  membership rebuilt on restore from `squadname`. `SQUAD_NEW_ENEMY` and
  `IGNORE_SQUAD_SEE_ENEMY` have no readers (drop the port's clear under the latter); the
  strategy-slot namespace ships dead (do not build). The tutorial's `squad_warehouse` (`thug_2`,
  `thug_3`) is the witness.
  `m_iMySquadSlot` is `+0x5dac`, saved, read by nobody.
  Gap: no squad object — `ConnectedSquad()` answers null (`ElysiumNpc.h:642`); the refcount
  lives on `ScheduleHost.SquadDisconnected` (`ElysiumNpcScheduleHost.h:22`) with
  `DisconnectFromSquad`/`ReconnectToSquad` (`ElysiumNpc.cpp:2758-2777`); `SquadSeeEnemy` is an
  identity (`ElysiumNpcConditions.h:130`) cleared by the overlay at `ElysiumNpc.cpp:3165` (the
  `IGNORE_SQUAD_SEE_ENEMY` clear to drop); `SquadSeesPlayer` is a stub native
  (`ElysiumScriptNatives.cpp:35`); no `squadname` keyfield, no `g_DisconnectedEnemies`, no
  `TASK_SQUAD_NEW_ENEMY` / `TASK_DISCONNECT_FROM_SQUAD`.
  Job: the squad object sharing 5's record store, the disconnect refcount replacing the seams
  in 7 and 8, the condition and its producer, the two tasks, the `SquadSeesPlayer` stub
  replaced, the overlay's clear removed.
  Oracle: § "Squads, decoded" (unrecovered list closed 2026-09-12). Unrecovered: the six
  `CAI_Squad` memory-forwarding wrappers (names only; their bodies forward to `AI_Enemies`).
  Size: L–XL. Effort: Opus / high.
- [ ] **16c. Possession and frenzy.**
  Retail: `Dominate_Possession`'s `DoPossession` byte runs `0x102c51a0`: squad disconnect,
  `SetEnemy(NULL)`, `flags2 |= D_POSSESSED | D_DISCONNECT_SQUAD`, `"player D_LI 99"`,
  `SetFollowerBoss(caster)` + `SetFollowerType("Combat")`, ideal state 1, target/friend =
  caster, `frenziedFlags = 0x3b1c`, acquire the nearest hated entity (`0x102b4cc0`). `DoFrenzy`
  (`Dementation_Berserk` / `_Bedlam`) runs `0x102c5310`: same teardown, `D_INSANE`, hunt state,
  investigate modes 6, `frenziedFlags = 0x9fbd`, no follower. The HitGroup's `AI_Schedule`
  installs before either. `DoPossession` dispatches slot 614 (`ResetThinkTimers`) at its start;
  `DoFrenzy` never does, so a possessed NPC acts on this frame and a frenzied one on its next
  cadence think. `NPCThink`'s 1 % `"Scream_Death"` roll runs under `frenziedFlags & 0x8000`.
  Gap: both bytes are parsed and inherited (`ElysiumDisciplineTargetTables.cpp:199-200`,
  `:302-303`) and refused with a warning at apply (`ElysiumDisciplines.cpp:949-952`); no
  `m_bfNPCFrenziedFlags` field (comments only, `ElysiumNpcFlags.h:223`), no hunt state (10h),
  no follower (16a), no squad disconnect beyond the refcount (17); `D_POSSESSED` has one reader
  (`ElysiumNpc.cpp:3135`).
  Job: both arms executed on apply, over 16a and 17; the frenzied word with its readers.
  Consumes: the HitGroup apply path from 0006; 16a, 17, 10h (the hunt ideal state), 15 (slot
  614). Provides: `D_INSANE` to 16b, the frenzied word to 6a/10a/10d's gates.
  Oracle: § "Disciplines that possess or frenzy an NPC; the `AI_NPCFlag` payload".
  Settled as not in the image: the `m_bfNPCFrenziedFlags` bit names.
  Size: M. Effort: Opus / medium.
- [ ] **21a. The flee state.**
  Retail: `m_NPCState == 8`, entered only in `CAI_BaseNPCTroika::SelectIdealState`
  (`0x102ad660`) from idle and alert on `COND_SUPERNATURAL_FLEE_LEVEL` 0x21 or
  `COND_CRIMINAL_FLEE_LEVEL` 0x1f, setting `INITIAL_FLEE` 0x100; terminal (case 8 returns 8),
  `m_bfNPCStateFlags = 0x85` (state 8; `0x7f` for the hunt/flee pair 0xb/0xe, which carries the
  PVS/LOS-force bit 3 that 0x85 does not). `InputFleeAndDie` `0x1029f210` (→ 0x6f) and
  `InputFaint` `0x1029f250` (→ 0xfa) each dispatch slot 614 (`ResetThinkTimers`) before their
  install. `SelectSchedule` case 8 in order: `COVER_FAILURE` 0x39 → 0x73;
  no stimulus → (`DETECTED_ATTACK` → 0x56; `INVESTIGATE_SOUND` → re-arm, clear `INITIAL_FLEE`,
  `CommitBestSound`, 0x48; else 0x77); `INITIAL_FLEE` clear → 0x73; else the one-shot pass:
  clear it, `m_flNextFleeSoundTime = curtime + RandomFloat(10, 20)`, the flee vocalisation
  (slot +0x7c8), damage → 0x72, `SEE_FEAR` without a law level → 0x72, the supernatural then
  criminal branch (offender, `m_vSavePosition`, 0x76 / 0x73, `MakeAISound(type 8, player eye,
  radius and type from `sound_volume_table.txt`, 10.0)`, `ReportSupernaturalAct` `0x1017f4a0`
  / `ReportCriminalAct` `0x1017f2a0`, `m_flPlayerDist < 512.0 && RandomInt(0,99) < 80` → 0x71
  else 0x70). `TranslateSchedule` (`0x102b12f0`): 0x77 → 0x78 when the hint node is type
  0x2774 (= 10100 `info_hint`); 1/0x6b → 0x132 under `D_MILDLY_CRAZY`. The criminal level
  `+0x6364` is written obfuscated by `0x1028ea60(level, location, offender)` from the law
  sweep `0x1028efc0` (encode `0x1042fde0`, decode `0x1042fe90`); the flee sound is the
  `NPC_FLEE` row of `sound_volume_table.txt` (index 26: volume 5 → 240 units, occludable);
  `0x48 SCHED_VTROIKA_TURN_TO_SOUND` = `PAUSE_MOVING; SET_NPC_FLAG NO_UNKNOWN_ATTACK;
  LOOK_AT_BEST_SOUND; WAIT_RANDOM 1.5; UNLOOK_AT; CLEAR_NPC_FLAG NO_UNKNOWN_ATTACK`.
  Gap: no flee state (`EElysiumNpcState`, `ElysiumNpcMindTypes.h:8-16`), the flee level
  conditions exist (`ElysiumNpcConditions.h:63/65`) with the witness producing them
  (`ElysiumNpcWitness.cpp:597-698`) and the overlay setting them (`ElysiumNpc.cpp:3141-3142`);
  `IN_FLEE_SCHED` is read by the interest overlay (`ElysiumNpc.cpp:3133`,
  `ElysiumNpcConditions.cpp:254`); no `InitialFlee`, `NextFleeSoundTime`, `TranslateSchedule`,
  `FleeAndDie`/`Faint` input, `MildlyCrazy` reader, no `0x48/0x70–0x78` program; the state byte
  `0x85` is in 15's table (`ElysiumNpcFlags.cpp:160`).
  Job: the state, the ideal-state entry, the case-8 chain, the translation's two flee arms
  (`0x77 → 0x78`; `D_MILDLY_CRAZY → 0x132`, which needs `0x132 LAUGHING` registered) extending
  25's `FElysiumNpc::TranslateSchedule` rather than re-creating it, `0x48`, the two inputs with
  their slot-614 reset, the flee vocalisation slot and the `NPC_FLEE` sound.
  Consumes: 15 (the state byte, slot 614), 25 (the slot-440 seam), 10a (`CommitBestSound`), 10e
  (the look tasks), 26 (the state-2/0xe law arms share `ReportCriminalAct`).
  Oracle: § "The flee state and the cower, disoriented and lost programs" (incl. "Story 21a
  recovery"). Unrecovered: bytes `+0x6360/+0x6361` beside the level, `0x1042fde0`'s transform
  (the port keeps the level plain).
  Size: L. Effort: Opus / high.
- [ ] **21b. The cower, disoriented and lost programs.**
  Retail: 0x70/0x71 `FLEE_AND_COWER_TURN_TO_PLAYER(_NEAR)`, 0x72 `_SCREAM`, 0x73
  `FLEE_AND_COWER`, 0x74/0x75 `_STALL(_FAILED)`, 0x76 `_NO_ENEMY`, 0x77/0x78 `COWER(_HINT)`,
  0x109/0x10a/0x10b `COWER_SIMPLE(_HINT/_NOSEE)` (`SEE_FEAR` listed twice, verbatim),
  0x12d `DISORIENTED` / 0x12e `LOST` (`DELAY_INTERRUPTS`, mask `{NPC_FREEZE}`, `WAIT_PVS`).
  `DISORIENTED` is the terminal schedule of 16 discipline programs; `LOST` has no producer.
  `TASK_PLAY_COWER` rolls `m_iCowerAnimOffset` +0x6414 = `RandomInt(0,2) × 3`, `SET_COWER`
  reuses it. `IN_FLEE_SCHED` set by every flee leg and cleared first by `COWER_SIMPLE*`.
  Base `COWER` (`STOP_MOVING; PLAY_SEQUENCE ACT_COWER`) is base `0x1e`.
  Gap: no program of the family, no task of the list; `COWERING`/`COWER_PATH`/`ONE_HIT_KILL`
  are flag identities (`ElysiumNpcFlags.h:65-86`) with `COWERING` read at `ElysiumNpc.cpp:3157`
  and `ElysiumNpcSenses.cpp:857`; no `CowerAnimOffset`; `Disoriented`/`Lost` have no schedule
  identity; the 0xe1/0xe3 completion branch is at `ElysiumNpc.cpp:2779`.
  Job: the fourteen programs and their tasks: `SUGGEST_STATE` 0x06, `GET_PATH_TO_RANDOM_NODE`
  0x1f, `FACE_HINTNODE` 0x2f, `GET_PATH_TO_COWER_NODE` 0x84, `_SAVE_POS` 0x85, `PAUSE_MOVING`
  0xa6, `PLAY_COWER` 0xe6, `SET_COWER` 0xe7, `LOOK_AT_PLAYER` 0xfb, `RUN_PATH_FLEE` 0x103,
  `FLIP_NEXT_IDEAL_YAW` 0x106, `WAIT_PVS`; the 0xe1/0xe3 completion branch 8 left waiting.
  Consumes: 21a (the state and chain), 25 (the `Idle_Stand` route for `DISORIENTED`/`LOST`,
  which lands on `0x6b` through slot 440 — 25's "what a consumer observes"), 10e
  (`PLAY_COWER`), 12b (the hint search behind `GET_PATH_TO_COWER_NODE`). Provides: `COWERING`
  and `ONE_HIT_KILL`'s writers to 21c; `DISORIENTED` to 0006.
  Oracle: § "The flee state and the cower, disoriented and lost programs". Unrecovered: the
  cower-node query through slot `0x688` (the hint search's cower category), `FLIP_NEXT_IDEAL_YAW`'s
  motor read.
  Size: L. Effort: Opus / high.
- [ ] **21c. The incapacitated victim's consumers.**
  Absorbed by 29e for its seventeen loop-side bodies (`SetState`, `GatherConditions`, `RunAI`,
  the task arms, `NPCThink`); this story keeps the victim-side consumers and the `ONE_HIT_KILL`
  seam.
  Retail: `AttemptFeed` (`0x10168910`) reads the victim's ideal activity (+0xff0), auto-accepts
  on `ACT_DISPOSITION_MESMERIZED 0x104e / ACT_DISORIENTED 0x1068 / ACT_LOST 0x1069 / ACT_COWER
  0x1098` only, so a cowering NPC auto-accepts one time in three. `ONE_HIT_KILL` (bit 30) has
  one reader, `OnTakeDamage` `0x102beda0`: any non-light hit kills outright. `COWERING` (bit
  10) withholds `COMFORT` in `BuildScheduleTestBits` and quarters hearing.
  Gap: `IsFeedAutoAcceptState` (`ElysiumFeed.cpp:222`) answers through
  `IsAutoAcceptDispositionName` (`:187`, called `:243`) from the disposition name;
  `ONE_HIT_KILL` (`ElysiumNpcFlags.h:86`) has no reader; `COWERING` is read by the overlay
  (`ElysiumNpc.cpp:3157`) and the hearing quarter (`ElysiumNpcSenses.cpp:857`) — that arm is
  reproduced.
  Job: `IsFeedAutoAcceptState` answering from the ideal activity instead of the disposition
  name; the `ONE_HIT_KILL` read in the damage path.
  Consumes: 21b (the writers), 0005 (the damage path). Provides: the `ONE_HIT_KILL` seam to
  0005's damage.
  Oracle: § "The flee state and the cower, disoriented and lost programs" (Activities and the
  feed; Flags the family writes); `feeding.md` § "Step 4, decoded". Unrecovered: nothing.
  Size: S. Effort: Sonnet / medium.

## Build order
Derived from `docs/vtmb/npc-kernel/order.md` (2026-09-13): the closure's call graph layered so a
function sits after everything it calls. Layer 0 is the leaves; the think is the top. The
kernel's spine, by layer:

| Layer | Function |
|---|---|
| 0 | `ClearSchedule 0x10280d30`; base `SelectSchedule 0x1028a260` (damaged) |
| 2–3 | the sound sweep `0x102b1cd0`, the comfort sweep `0x102b1a20` |
| 13 | the see-unknown sweep `0x102b15c0` |
| 19–20 | `SetState 0x1026e340`; `NPCInit 0x10273390` |
| 21–22 | `SelectIdealState 0x1026f660` / Troika `0x102ad660`; `MaintainSchedule 0x102817c0`; `RunTask 0x10288780`; Troika `NPCInit 0x1029a0b0` |
| 23–24 | `GatherConditions 0x1026ec30`; Troika `RunTask 0x102aacf0`; `StartTask 0x102827f0`; `RunAI 0x1026f110` |
| 25–26 | `GetSchedule 0x102ae920`; Troika `SelectSchedule 0x102af660`; Troika `StartTask 0x102a1910`; `NPCThink 0x10292de0` |

Two rules follow. A story whose closure reaches a higher layer than a story it consumes is
ordered after it; the list above keeps that order. A story is *recovery-complete* when every
function its `Retail:` cites is in the closure with a name and an undamaged body; until then
its size is provisional. The open stories, as the ledger reads them (functions cited by the
story's `Retail:` text; `FUN_` = still unnamed):

| Story | Cited | In closure | Max layer | `FUN_` | Damaged |
|---|---|---|---|---|---|
| 25a `ClearSchedule` producers | 14 | 10 | 22 | 7 | 0 |
| 25b species `TranslateSchedule` | 22 | 22 | 22 | 3 | 0 |
| 25c `MaintainSchedule` exits | 1 | 1 | 3 | 1 | 0 |
| 24 reachability | 3 | 2 | 20 | 1 | 0 |
| 10g patrol programs | 8 | 5 | 21 | 5 | 0 |
| 11 interesting places | 8 | 7 | 20 | 7 | 0 |
| 27 patrol-point interest | 12 | 8 | 9 | 8 | 0 |
| 26 `GetSchedule` | 16 | 14 | 25 | 11 | 1 |
| 28 player-on-head | 2 | 1 | 25 | 1 | 0 |
| 10i comfort program | 9 | 5 | 23 | 3 | 1 |
| 10j `CheckTarget` | 11 | 11 | 23 | 10 | 0 |
| 10d alert selectors | 3 | 1 | 25 | 0 | 0 |
| 10f unknown-investigation | 3 | 1 | 25 | 1 | 0 |
| 12b cover and kick | 5 | 3 | 26 | 2 | 0 |
| 10h hunt-investigation | 2 | 2 | 21 | 2 | 0 |
| 16a followers | 7 | 4 | 20 | 4 | 0 |
| 16b composed relationship | 3 | 2 | 23 | 1 | 0 |
| 17 squads | 7 | 6 | 20 | 6 | 0 |
| 16c possession and frenzy | 3 | 3 | 21 | 3 | 0 |
| 21a flee | 10 | 8 | 24 | 5 | 0 |
| 21c incapacitated consumers | 2 | 1 | 24 | 1 | 0 |

Stories citing no function (22, 10e, 12a, 10k, 13b, 21b) are pipeline or program-blob work, or
cite their programs by schedule id; they are sized by hand. Cited functions outside the closure
are the other subsystems' producers (`entries.md` names them) — a story that consumes one names
the owning spec.

### The sequence, optimized on the ledger (2026-09-13)

The closure's *core* — a family or helper method, or a body touching an NPC-range offset — is
2,490 functions. By layer band: 0–4 has 1,453 (725 unnamed), 5–9 has 242, 10–14 has 175, 15–19
has 172, 20–24 has 230, 25–29 has 218. 1,382 of the 2,490 are ≤ 64 bytes. That distribution is
the argument for building the kernel bottom-up in bulk rather than feature by feature: the bottom
two bands are shape and accessors, the top two are the interpreter, and every feature story
today re-walks pieces of both. So:

1. **29a → 29b → 29c → 29d → 29e**, in that order. Each is a layer band; each enters
   implementation with its checklist generated from the ledger and leaves with its
   `coverage.md` count at zero for its band.
2. The open feature stories shrink to what sits **above** the bands already built. The split of
   each story's cited functions between layers ≤ 9 (29c's) and > 9, from the ledger today:
   25a 3/9 · 25b 1/21 · 25c 1/0 · 24 1/1 · 10g 2/3 · 11 6/1 · 27 8/0 · 26 6/8 · 28 0/1 ·
   10i 3/2 · 10j 6/5 · 10d 0/1 · 10f 0/1 · 12b 2/1 · 10h 0/2 · 16a 1/3 · 16b 0/2 · 17 5/1 ·
   16c 0/3 · 21a 4/4 · 21c 4/17. After 29c, 27 and 11 are wiring only; after 29e, 25b, 25c, 26,
   10d and 21c's loop half are done and those stories keep only their program blobs, species
   rows and consumer wiring.
3. What remains after 29e is **programs and wiring**, and it is ordered by consumer: 25a (the
   `ClearSchedule` wiring, now one story's worth), the program families in the order the
   selectors reach them (10d, 10e, 10f, 10h, 10g, 10i/10j, 10k, 11/27, 12a/12b), the social
   families (16a, 17, 16c, 21a–c), the graph (24, 22), then 28 and 13b.
4. The tutorial cut comes between 29e and step 3: a reachability query over the ledger (planned
   as `kernel_ledger --reach`) seeded from `sp_tutorial_1`'s population selects which programs
   and species rows 0004–0009 actually need, and step 3 is run on that subset first.

The rule the sequence encodes: **a story enters implementation only when its closure is
recovery-complete** — every cited function named, undamaged, and in a band already built or in
the story itself. A story that fails the rule is a recovery story first.

## Seams
- Provides: the awareness seam (`Cognition.Conditions`, the enemy memory, `Senses.Memory`,
  `IsOblivious()`, `ShouldInvestigate`) to 0005 and 0007; the stealth scalars; the failure path
  (13), the cadence (15) and the route refusal (24) every later program family runs on (0005,
  0006, 0003); the composed
  relation (16b) to 0005, 0006 and the target HUD; the trance and the flag word to the feed
  and dialogue gates (0004).
- Consumes: the HitGroup apply path and the cloak/detection-record producers from 0006 (6a,
  16c); the grapple state machine and damage from 0005 (3c, 21c).
