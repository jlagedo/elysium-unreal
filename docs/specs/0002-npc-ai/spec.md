# 0002 npc-ai — NPC AI: what an NPC senses, remembers, decides, schedules and walks, witnessed by the tutorial's stealth lessons

**Reworked 2026-09-15.** This spec is now the mind alone. The world's AI objects — graph,
hints, places, patrol paths, the sound list, squads, the coordinator, makers, the law bus — are
**0018**; the data seams, the strict verdict pass, the class tree and the reach cut are **0019**.
The rule both apply governs every open story here: a retail function is ported only if something
can observe it, and what Troika typed as text or a table is loaded from the install, never
retyped. Programs come from the schedule seam (0019 story 3), so where an open story's Job says
"the programs", read: the task bodies and the selector arms; the program itself loads from
retail's text. The landed stories stand as written — their text is the record of what was
recovered, not the method that continues.

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
kernel and the route refusal it runs on). The world's AI infrastructure and its query
surface — **0018** (this spec consumes hint searches, place selection, patrol records, the
sound list, squads, the melee slot and the route refusal through it); the data seams, the
verdict pass, the class tree and the reach cut — **0019** (this spec lands on its loaded
programs, generated bindings, tunables and class tree, and is scoped by its reach cut).

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

### The kernel ledger and the bands (29 series; 29e open, finishing under 0019 story 8)

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
- [x] **29a-1. The last two evidence sources: CRT bytes and accessors.**
  Retail: after 29a the backlog had two sources no pass read. The C runtime: `crt_fid` named 207
  bodies through Function ID, and missed the ones whose archive object and linked image analyse
  to different extents (`memcpy` at `0x10430fa0`, 285 callers; `rand`; `strchr`; the `_fpclass`
  / `_control87` family). The recovered layout: a leaf body that reads or writes one word of
  `layout.tsv` and nothing else is named by that word. Everything else in the backlog has no
  external source (314 no-SDK-twin slots, Troika's `NPC_V*.cpp` units, ~1,800 bodies under 64
  bytes): those names are coined from a walk, which is 29c–29e's job.
  Job (landed 2026-09-13): `corpus harvest` gained `crt` (`crt_match.py`: every symbol extent of
  the staged VC6 SP5 `LIBC.LIB`/`LIBCMT.LIB` objects, relocation sites masked, searched at the
  image's function entries; tier `binary`, the archive's own statement) and `accessor`
  (`name_passes.accessor_pass`: shape from the decompiled C, word from `layout.tsv`, receiver
  from the typed access, the slot's holder or the typed callers; tier `accessor` when coined,
  `inferred` when an SDK header or `.cpp` defines that one-line body, the virtual preferred for a
  slot body). Slot identity propagates a coined name as `accessor`. `NAME_TIERS` gained
  `accessor`. Tests: `test_crt_match.py`, `test_accessor_pass.py`.
  Result: 145 names applied, 83 inside the closure (41 CRT, 24 inferred, 18 accessor); closure
  unnamed 3,350 → 3,267, core unnamed 1,428 → 1,392, `CAI_BaseNPC` unnamed slots 315 → 299. The
  CRT pass reproduces 185 of FID's names and disagrees with none; 8 bodies VC6 links under two
  symbols (`memcpy`/`memmove`, `__CIsqrt` and its siblings, `_atodbl`/`_atoflt`) are `unsettled`
  with the reason, `0x10430fa0` among them. Recovered on the way: `CAI_BaseNPC#2` is
  `GetCollideable` (`IServerUnknown`'s virtual, `&m_Collision`), `#145` is `BloodColor`, `#251`
  is `IsActivityFinished`, `#464` is `GetState`, `#198` is `GetLocalVelocity`; slots 604/605/615
  (`SelectScheduleMeleeCombat`, `SelectScheduleRangedCombat`, `CanBeSetOnFire`) named across the
  species now that the vtable dump walks past 600. Measured and rejected: client.dll byte twins
  (2 names), constructor shapes (12), SDK 2013 BSim. Two accessor refusals worth knowing:
  `#158` `0x100b4dc0` is `m_lifeState == 0` (`IsAlive` by shape, no SDK body states it), and
  `0x10027490` is `m_iEFlags & 1` (`IsMarkedForDeletion` by shape, a flag test). The second
  harvest round proposes nothing but two rows the overlay already refuses (`CBaseDoor::Spawn`
  and `CBaseEntity::Remove`, both names another body carries).
  Unrecovered: the 1,392 core bodies with no source; the tracked number is now "core unnamed
  with an unread evidence source", which is zero. Size: S. Effort: Fable / high.
- [x] **29b-0. The shape's recovery: every word typed, every slot's signature.**
  Retail: a declaration needs what the ledger did not state. `fields.md` typed all 312 NPC-range
  words `undefined4`/`undefined1`; 929 offsets touched through `this` had no record; 315 base slots
  were `unsettled` and none had a parameter list; and the vtable dump stopped at 600.
  Job (landed 2026-09-13): `uv run elysium research kernel_shape` (`kernel_shape.py`, over
  `datamap_layout.py`) writes `npc-kernel/layout.md`/`.tsv` and `signatures.md`/`.tsv`, `--check`
  verifies them, `--residue` lists what no reading has settled. Types come from the datamap
  builder replay (`datamap_records-vampire.dll.json`, VtMB's own `fieldtype_t`, no
  `FIELD_QUATERNION`) and the image's static records; interiors of `COutputEvent`, `CUtlVector`,
  `Vector` and arrays from SDK 2013; untyped accesses are read receiver-aware (a slot body of the
  family, a method typed on it, or a constructor/destructor installing its vtable — a Global body
  alone never makes a word); signatures from SDK 2013 where the image pops the declared words.
  The rest was read, body by body, into the two overlays beside the tool
  (`kernel_fields.tsv` 417 rows, `kernel_signatures.tsv` 531 rows, each with the addresses read).
  `DumpVtables.java` (and `ApplyDatamapTypes.java`'s same walk) stopped at 600 slots; both now
  walk to the next table, the vtable pass was re-run and `vampire.dll` rebuilt, and the ledger
  types `fields.md` from the records.
  Result: `layout.md` 1,769 rows — 1,284 datamap, 39 interior, 67 sdk-order, 28 doc, 329 walked,
  22 unsettled, none open; `signatures.md` 666 rows (628 slots, one row per branch where a branch
  declares its own virtual) — 135 sdk, 522 walked, 9 unsettled, none open.
  Recovered on the way (`npc-ai/shape.md`): Troika's table is 617 slots, the deepest species' 628,
  and past a base's table (583 on `CAI_BaseActor`/`CAI_ExpressiveNPC`/`CCineNPC`, 617 on the boss,
  vampire-boss and maker branches) every branch declares unrelated virtuals at the same index;
  slot 0 is `SetRefEHandle`, the deleting destructor is slot 5; retail condition sets are 192 bits
  and three, plus a heard set; a 16 KB circular debug log and three selector/ideal-state/TaskFail
  stamps sit at `+0x1b2c..+0x5b55`; Troika's words end at `+0x665c`, not `+0x660c`; retail
  `CUtlVector` is 0x14; `RunAI(bool)`, `OnScheduleChange(CAI_Schedule*)`,
  `QuerySeeEntity(CBaseEntity*)`, `SelectIdealState` returns `NPC_STATE`.
  Consumes: 29, 29a. Provides: the typed layout and the signatures 29b transcribes.
  Oracle: `npc-kernel/layout.md`, `signatures.md`, `npc-ai/shape.md`. Unrecovered: 22 layout
  rows and 9 slots, each `unsettled` with its reason (the un-named members of retail
  `CTakeDamageInfo`; constructor defaults nothing reads; empty bodies with no NPC call site).
  Size: L. Effort: Opus / high, readers in parallel.
- [x] **29b. The shape: every field and every slot declared.**
  Retail: `layout.md` — the flattened `CAI_BaseNPCTroika` layout typed word by word (the 835
  datamap offsets, their interiors, and every word no datamap saves, to `+0x665c`), then each
  species class's own words; `signatures.md` — 617 Troika-line slots and the per-branch virtuals
  past them, each with a declaration; `slots.md` — the bodies per class; `classes.md` — the
  77-class tree and the entity classnames each claims.
  Gap: `FElysiumNpc` (`ElysiumNpc.h`) cites ~30 offsets and ~25 slots in comments only, nothing
  asserts them; `FElysiumNpcMind` has no retail counterpart at all (`m_NPCState +0x5cc0` /
  `m_IdealNPCState +0x5cc4` are not declared anywhere); nine sites call `Schedule.Clear()` where
  retail calls `ClearSchedule`; `ElysiumStub::Fired` keys on free text with no address or story
  field. `FElysiumNpc` is `final` and species are data (`ElysiumNpcClasses.cpp`), which is right
  and stays.
  Job: a generator beside `gen_action_tables.py` reads `layout.tsv`, `signatures.tsv` and the
  ledger tables and emits `ElysiumNpcKernelShape.cpp` — the census (offset → member and type,
  slot → virtual and declaration, class → base) the runtime asserts in an
  `ElysiumActionTableTests`-shaped test — and the shape itself lands by hand from the census:
  every word a named member of its recorded type on the struct that owns its concern
  (`ScheduleHost`, `Cognition`, `Senses`, `NpcFlags`, the leaf), existing members kept and
  mapped, new ones default-initialised and unwritten; every Troika-line slot a virtual on
  `FElysiumNpc` with its address and tier in the declaration comment and its body as the default
  only where the port already has it (29c decides the rest), or a named stub that tallies
  `elysium.stubs` — which gains structured `Address` and `Story` columns so the tally joins
  `functions.md`. Species overrides, and the per-branch virtuals past 583/617, are rows in the
  class registry (class → slot → address → declaration), not subclasses. An `unsettled` row lands
  as its recorded arity and type with the reason in the comment. The nine raw `Schedule.Clear()`
  sites become the one `ClearSchedule` (25a's first job, done here because it is shape).
  Consumes: 29, 29a, 29b-0. Provides: the object every later story fills instead of re-shaping —
  the end of "seam shaped by guess, rewired by the next story".
  Oracle: `npc-kernel/layout.md`, `signatures.md`, `slots.md`, `classes.md`; the census file is
  the record. Unrecovered: nothing new — 29b-0's `unsettled` rows carry over as recorded. Size: L.
  Effort: Opus / high.
  Landed (2026-09-13). `uv run elysium research gen_kernel_shape`
  (`research/tooling/gen_kernel_shape.py`, over `kernel_shape.build`) writes three committed files
  and `--check` verifies them byte for byte. **The census** (`Substrate/ElysiumNpcKernelShape.cpp`,
  6.6k lines): 1,141 top-level words — 756 of the flattened `CAI_BaseNPCTroika` table and 385 a
  species' own, with 391 interior rows collapsed onto the aggregate that owns them, because the port
  declares one member per retail aggregate; 666 slot rows (617 Troika-line plus the 49 per-branch
  virtuals past 583/617); the 77-class tree with its direct bases, vtables and the 77 entity
  classnames it claims; and 2,344 species slot overrides as rows keyed on the retail class, not
  subclasses. Every count is stored beside an FNV-1a 64 digest of the row stream the generator
  hashed. **The slot surface**: `ElysiumNpcKernelSlots.inl` declares 585 `virtual`s on
  `FElysiumNpc`, one per Troika-line slot the port does not already implement, each carrying `//
  slot N 0x…… (tier)` and its `order.md` layer; `ElysiumNpcKernelSlots.cpp` defines them as named
  stubs that tally `elysium.stubs` with the retail address and the owning story (466 `29c`, 91
  `29d`, 28 `29e`). The 32 slots the port already runs are a reviewed table in the generator
  (`SLOT_PORT_MAP`), and a generated name that would shadow anything in the port's entity chain —
  methods *and data members*, because a member function hides a base's field silently and MSVC does
  not warn — fails generation until a row decides it. That rule caught slot 534 `EyeLookTarget`,
  which would have hidden `FElysiumCombatCharacter::EyeLookTarget` and broken the gaze tests. **The
  shape**: the NPC's own 388 words (the `CAI_BaseNPC` and `CAI_BaseNPCTroika` layers) all have a
  home — 144 mapped to members the port already had, 180 newly declared, default-initialised and
  unwritten, on the struct that owns the concern (`FElysiumNpcScheduleHost` 26, `FElysiumNpcMemory`
  9 and `FElysiumNpcSenses` 4, `FElysiumNpcCognition` 4, `FElysiumNpcMind` 2, `FElysiumNpcWitness`
  3, `FElysiumNpcDialogue` 4, the leaf 128); 23 carried by the entity chain below the NPC; 28
  implicit (the vtable pointer, the per-state capability byte this runtime derives, the schedule
  loader and the alignment tail, and the 24 `COutputEvent`s the port fires by name through
  `FElysiumEntity::FireOutput`); and 13 `absent` with a stated reason — retail's 16 KB debug ring
  and its three cursors, the file/line selector/ideal-state/`TaskFail` stamps,
  `CAI_MoveAndShootOverlay`, the squad pointer, `MeleeMoveRecord_t`, the nav link, the cached
  `surfacedata_t`. `m_NPCState +0x5cc0` and `m_IdealNPCState +0x5cc4` are declared, on
  `FElysiumNpcMind`, where they belong. `Substrate/ElysiumNpcKernelShapeMap.cpp` binds all 388 by
  offset **in a form the compiler checks**: `ELYSIUM_NPC_WORD(0x60a8, FElysiumNpc, EnemySightings)`
  compiles the member's type and identifier and stores its `sizeof`, so a rename or a deletion is a
  build error instead of a stale comment; a private member takes the string form and says why.
  `Tests/ElysiumNpcKernelShapeTests.cpp` (`Elysium.Substrate.NpcKernelShape.Census` and `.Map`)
  walks both tables and requires the counts, the digest, the gapless 0–616 slot line and a binding
  per NPC word back; `pipeline/tests/test_gen_kernel_shape.py` holds the generator's derivations.
  `ClearSchedule`: eight of the nine substrate `Schedule.Clear()` sites became
  `FElysiumNpc::ClearSchedule` (`0x10280d30`), so `PRESERVE_PATH` and the slot-435 dispatch now run
  wherever a program is dropped. The ninth is the save-load reset and is deliberately still raw,
  with the reason on it: nothing is running there, and the dispatch would release the NPC flag word
  the payload has just restored. The test-side clears were reviewed and left: they set up a
  fixture's record rather than ending a program. `ElysiumStub` gained an `FSurface` argument struct
  with `Address` and `Story`, carried into `FTally` and the `elysium.stubs` readout, so the tally
  joins `functions.md` by address; the five-argument `Fired` stays for the surfaces with no
  recovered address. Decisions taken, none with a precedent to follow: (0) the census *header* is
  hand-written and only the `.cpp`/`.inl` are generated, which is the `ElysiumActionTables`
  convention: a header declares the shape and is reviewed, a generated file carries the rows; (1)
  the 585 slot declarations are **generated** into an `.inl` included inside the class rather than
  hand-typed — a surface that can drift from `signatures.tsv` is exactly what this story exists to
  end, and a virtual can only be declared inside its class; (2) the 368 entity-chain words below
  `CAI_BaseNPC` are census rows carrying their owning layer but not binding rows — they are
  `CBaseEntity`'s, `CBaseAnimating`'s and `CBaseCombatCharacter`'s concerns and re-homing them is
  those classes' story, not the kernel's; (3) a retail `float` holding an absolute curtime lands as
  `double`, which every other stamp in this runtime already is; (4) every entity pointer lowers to
  `FElysiumEntity*` because this port stands one leaf per classname, and a type with no port
  counterpart lowers to `void*` or `int32` with the retail spelling in the comment and the arity
  unchanged; (5) slots 442 and 444, whose declarations the ledger cannot tell from 441's and 443's,
  land as `StartTaskSlot442` / `RunTaskSlot444` with "which body is which is unrecovered" on them;
  (6) `FElysiumNpc` stays `final` — a virtual here declares the surface retail dispatches through,
  not an extension point. Unrecovered, as 29b-0 left it: seven top-level words and seven Troika-line
  slots are `unsettled`, and each landed with its recorded type and arity rather than being dropped.
- [x] **29c. The primitives: layers 0–9, in bulk.**
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
  Landed (2026-09-13), **split**: the checklist, every verdict, and the port of every `rule` row
  whose retail body is a *value* land here; the 915 `rule` rows whose body is code go to **29c-1**
  below, with their size measured rather than guessed. The premise "each row trivial" did not
  survive the reading — 582 of the 1,732 bodies are over 64 bytes and 347 are more than 25
  statements, so the bulk half and the body half are two different jobs and are now two stories.
  **The instrument.** `kernel_ledger` gained `--checklist <band>`, `--bodies <band>` and a verdict
  overlay: `research/tooling/ghidra/driver/kernel_verdicts.tsv` (`address / verdict / band /
  target / evidence`) is the record, `docs/vtmb/npc-kernel/checklist-0-9.md` is the ledger joined
  with it and is regenerated from the corpus on every run, so no reading can be lost by a
  regeneration; `--check` verifies it like the other ten tables. `--bodies` writes the decompiled
  bodies as 23 reading packs under `$ELYSIUM_WORK_ROOT` (never committed, `research/CLAUDE.md`),
  closing on whichever of 120 rows or 70 KB it reaches first, because 120 three-byte accessors and
  120 bodies off the top of the band are not the same reading. `merge_verdicts` folds batches in
  with later sources winning, refuses an address outside the band, is idempotent, and `--audit`
  prints every band's standing. `coverage.md` gained `## Verdicts by layer band`: **a verdict is a
  citation** — it says the body was read and what was done with it — so the table measures each
  band's core against port citations, oracle citations *and* verdicts, and its **Neither** column
  is the acceptance measure of 29c, 29d and 29e.
  **The reading.** 1,732 core functions, every one verdicted, in 23 Sonnet batches over the packs
  with the rubric and the exact row format, reviewed here: **1,471 `rule`** (915 whose target is a
  port method, 417 `registry:<slot>`, 139 `default:<literal>`), **150 `present`**, **108
  `mechanism`**, **3 `unsettled`**, and — the one count the story's premise got wrong — **0
  `dead`**: every core function of the first ten layers either fills a family vtable slot or has a
  caller inside the closure. `coverage.md`'s *Neither* for layers 0–4 and 5–9 is **0** and **0**.
  Review found four systematic batch errors and corrected all of them: slots 580, 452 and 451 (the
  class's own schedule id space, its load-once gate and its scheduling-error name) had been called
  `mechanism` by some batches and `rule` by others — they are species data and are now 42 uniform
  `registry:` rows; `AutoMovement 0x10280a50` was called `mechanism` for Unreal's root motion, but
  it applies the interval movement only under `GetMoveType() == 4` and a clear `0x400`, and a gate
  is the retail contract a modernization keeps; and one `present` (`CPayphone::CanTalk
  0x101aaee0`) was downgraded because the port carries one of its seven arms.
  **The port.** Three decisions, each recorded because none had a precedent. (1) *A constant body
  is data.* A Troika-line slot whose whole retail body is `return;` or `return <literal>;` is a
  recovered fact, not a behaviour someone wrote, so the generator emits that body rather than a
  stub — the same argument the story already makes for a species override — and
  `gen_kernel_shape` now reads the overlay: `default:<literal>` emits the body **and** verifies the
  recorded literal against the decompiled C, so a misread constant fails generation instead of
  compiling. **86 slots stopped being stubs** (499 remain), each with a generated probe that calls
  the virtual with value-initialised arguments; `Elysium.Substrate.NpcKernelSlots.Defaults` walks
  all 86, requires retail's own literal back, and requires that the call tallied **no** stub —
  that second half is what proves the body replaced the stub rather than sitting beside it. (2)
  *A species override of a constant is a registry row.* The census's 2,344 override rows gained
  `Verdict` and `Default`; 1,463 carry a verdict and **451 carry the literal that species
  answers**, read mechanically off the body under a regex strict enough that only a one-statement
  body matches. The base virtual that *reads* that table is not built: nothing in this runtime
  dispatches through these virtuals yet, and standing a species dispatcher before a caller exists
  is the seam-by-guess 29b existed to end. (3) *`hand:<PortMethod>`* is the third target spelling:
  the generator declares the virtual and emits no definition, so an unwritten body is a link error
  rather than a silent stub. It has no user yet and is covered by the generator's own tests.
  **The two bodies ported by hand** are the authored group lists, which `authored-control.md`
  § "The navigation and reaction keyfields (2026-09-08)" had already walked and nothing had
  claimed: `FElysiumNpc::ParseGroupMask` is `0x102989e0` and `0x10298910` — space-separated
  decimal ids, `id - 1` is the bit, anything outside 1..32 dropped in silence, `atoi`'s zero for a
  non-number — with the one difference between them as its only parameter, an unset `hint_groups`
  answering `0xffffffff` and an unset `interesting_place_groups` answering `0`.
  `FElysiumNpcScheduleHost::HintGroupMask +0x62e4` replaced 29b's unwritten `TSet<int32>` with
  retail's own 32-bit set (which is what `FValidateHintType 0x10295c20` ANDs a hint node against),
  both keyfields now parse on the write as retail's KeyValue does, and
  `Elysium.Substrate.NpcGroupMask` is the test. It also surfaced a **named divergence**:
  `AcceptsAmbientGroup` treated an empty interesting-place list as "every group" where retail's zero
  mask matches nothing, and 1,005 shipped NPCs author `"0"`. **Closed in 29c-1**: the admission rule
  is now `0x102dad60`'s own, `place->m_iGroupID +0x574 & npc->m_iInterestingPlaceGroups +0x62dc`
  against the mask `CAI_InterestingPlace::Spawn 0x102d9c20` folded, so an unset or `"0"` list
  matches no place — retail's answer, including its consequence for those 1,005 NPCs.
  **The damaged rows.** All 21 read from `corpus asm` one at a time and walked into `npc-ai/shape.md`
  § "The damaged bodies of layers 0–9, read from the listing (2026-09-13)". Thirteen are one
  `JMP [vtable + 0xNNN]` — a forward to slot `0xNNN / 4` of the same object, which is a fact about
  the table rather than a missing body — and the table names each pair (`Kill` on a hint node is
  `ScriptHide`; `OnRestore(bool)` replaces its own argument with `0` before calling
  `SetCheckUntouch`; `CAI_Motor`'s slot 1 is its owner's `TaskFail`). Six carry a rule and are
  walked in full. `CDialog::message_send 0x100e58e0` turns out to reach the closure through a
  numeric offset collision and belongs to 0004's dialogue session, not to this kernel.
  Unrecovered, and counted as such: **3 `unsettled`** — `0x102702d0` (an `__ftol` compared against
  x87 status-word artifacts, the same folded sequence in the listing), `0x1028e870` (the float at
  `+0x62cc` bound to the return-storage pointer, the comparison lost) and `0x102623e0` (returns an
  unassigned register, no caller of any kind). Also unrecovered: slot 37's float constant at
  `0x104454c8` and slot 240's global `DAT_1072b360`, which are `return <one word>;` bodies whose
  word the corpus does not hold, so neither is a `default:` the generator will emit.
  Verification: `uv run elysium build` clean; `uv run elysium test Elysium.` 658/658 (656 after
  29b, plus the two suites above); `uv run pytest pipeline/tests -q` green with 11 new cases;
  `kernel_ledger --check`, `kernel_shape --check` and `gen_kernel_shape --check` all pass.
- [x] **29c-1. The primitives: the bodies.**
  Retail: the 915 `rule` rows of `checklist-0-9.md` whose `target` is a port method — the layer
  0–9 bodies that are code rather than a value. 582 of the band's 1,732 are over 64 bytes; the
  heavy families are the per-species `SquadSlotName` translations (slot 546), the sound hooks
  (slots 488–510), the `Create*` component factories (424–430), the turn-activity ladders (572),
  the facing-target queue, the tactical-position validators, and the `CAI_Motor`/`CAI_Navigator`
  helpers the port has no seam for.
  Gap: each row already names the port method that will carry it and carries a one-line walk in
  the checklist's *evidence*; what is missing is the body, its test, and — for the 582 over 64
  bytes — its walked paragraph in the subsystem file its fields belong to.
  Job: port them by owning struct, not by address, so a family lands together with one fixture:
  the sound hooks, the component factories, the squad-slot id spaces, the turn ladder, the facing
  queue, the hint validators. A row whose port seam does not exist yet (squad, motor, navigator)
  builds the seam and leaves it answering nothing with the retail field named, per `CLAUDE.md`.
  Acceptance: every `rule` row of layers 0–9 has a body and a test; every body over 64 bytes has a
  walked paragraph; `gen_kernel_shape`'s stub count for the band reaches zero, with each slot's
  overlay row spelled `hand:<PortMethod>` so the linker is what checks the claim.
  Consumes: 29c. Provides: the leaves 29d and 29e's bodies call. Oracle: the subsystem files;
  `checklist-0-9.md` stays the index. Unrecovered: 29c's three `unsettled` rows and the two
  unrecovered constants. Size: XL. Effort: Opus / high, by family, in parallel.
  Landed (2026-09-13). All 915 rows are ported, in **22 families** dispatched as parallel agents in
  eight waves of three, each family owning one `ElysiumNpcKernel<Family>.inl` (declarations, included
  inside `class FElysiumNpc`), one `.cpp` (split at ~1,500 lines) and one `Elysium.Substrate.
  NpcKernel<Family>.` suite: squad 74, motor 73, schedule 64, species 60, lifecycle 59, entitychain
  57, sounds 57, damage 47, troikahelpers 45, facing 44, positions 42, basehelpers 40, hints 38,
  misc 37, anim 35, conditions 32, bosses 28, senses 26, geometry 22, debug 21, dialogue 14, plus
  **closure 41** — the Troika-line slots 29c had verdicted `present` or `mechanism`, which were
  still stubs answering a tally instead of the port's own answer.
  **The instrument changed twice, and both changes are the acceptance.** `hand:<PortMethod>` was
  29c's unused third target spelling; it is now what 235 slot rows carry, so a slot whose body was
  claimed and not written is an unresolved external rather than a silent stub — five families hit
  exactly that and found out at the link, which is the point. `gen_kernel_shape` gained a
  per-story-band stub breakdown in `--report`, because "the stub count" is only an acceptance
  measure if it can be read per band; and `hand:` was widened from `rule`/`present` to `mechanism`,
  since a mechanism that goes through a named service seam is a written body like any other.
  **The dispatcher 29c declined to build.** 29c left the species-override table unread on the
  argument that standing a dispatcher before a caller exists is seam-by-guess. 915 callers arrived,
  so `Substrate/ElysiumNpcKernelClassLookup.h` is the reader: `RetailClass()` (latched),
  `IsRetailClass()` (the chain walk, never a name compare), `OverrideOf`, `BodyOf`, `SlotRow`.
  `FElysiumNpc` stays `final` — every species difference is a data table keyed on the retail class,
  each row carrying the retail address of the body it came from and exercised by name in a test. The
  recovered fact that makes it work: a classname appears on *every* class in its chain, so the
  lookup takes the **most derived** claimant. Two tables disagree and both are right —
  `CNPC_VCop`'s census classname list is null (a spawned cop's `RetailClass()` is null and every
  species lookup correctly falls through to the Troika line) and `npc_VCamera` is claimed by the
  census but is not a registered spawn leaf. Four families lost time to that before it was written
  down.
  **What the reading changed.** More than thirty-five of 29c's one-line walks were wrong and are
  corrected in the code and the prose, each from the listing or the decompiled C — 29c's own
  premise, that a one-line walk is a summary and not the body, held. Slot 601 clears `m_bInMelee`
  (so `SelectSchedule`'s unreachable-enemy arm leaves melee and the next selection re-enters through
  599); slots 599/600 draw `RandomFloat(7.5, 15.0)`, not 4–15; the three Tzimisce slot-337 bodies
  *do* add a species bit (`OR AH,imm`, which the decompiler dropped); `_DAT_10449260` is a **double**
  and reads 0.25, so the tentacle scatter cone is 75.5° and not the whole forward half-plane;
  `0x1027de00` is not `SetEnemy` but the door-blocked notice; `0x1034b430` is not an NPC body at all
  but `CNPCMaker::IsDepleted`, and is re-verdicted `present` against the maker. **Forty-two `.rdata`
  cells were read out of the pinned image rather than left unrecovered**, including the two 29c
  recorded as not in the corpus: slot 37's `_DAT_104454c8` is **80.0** and slot 550's `_DAT_1045d650`
  is **1024.0**.
  **The seams.** Every retail input this substrate has no source for is a seam that answers nothing
  and names the retail call — the attack coordinator's five entry points, the node graph, the squad
  object, `CAI_Motor`/`CAI_Navigator`/`CAI_StandoffBehavior`, `datamap_t`/`ServerClass`/`trace_t`,
  the studio header, the physics object. Each answers the *admitting* value where retail's own
  refusal arm is the admitting one, so nothing is silently refused, and each is asserted as the
  recovered refusal rather than worked around.
  **Divergences, all named.** Slot 602 applies the null-coordinator guard on both species lines
  (retail would fault, and the arm is unreachable in retail); `PositionAtHint` declines the move on
  `vec3_invalid` rather than teleporting a Werewolf out of the world; slot 215's temp-vector ring is
  one-deep per entity rather than 128-deep global; `goto_line_for_response` refuses an out-of-range
  index retail reads past. And one divergence was **closed** rather than added:
  `AcceptsAmbientGroup` now runs `0x102dad60`'s own gate (see 29c above). *(The entity-chain walk
  order was listed here too and turned out not to be a divergence at all — see the cleanup below.)*
  **The measure.** `gen_kernel_shape --report` gives the 29c band **145** stubs, every one of them
  `no verdict`: the band's verdicted slots are all `hand:` and all defined, and the 145 that remain
  are non-core `CBaseEntity`/`CBaseAnimating` slots that 29c's closure never read and never
  verdicted. They are `CBaseEntity`'s story, not this one, and this is the one place the acceptance
  is met by argument rather than by the number reaching zero.
  Verification: `uv run elysium build` clean; `uv run elysium test Elysium.` **933/933** (658 after
  29c, plus 275 new cases in 22 suites); `uv run pytest pipeline/tests -q` 4,047 passed;
  `kernel_ledger --check`, `kernel_shape --check` and `gen_kernel_shape --check` all pass;
  `merge_verdicts --audit` reports layers 0–9 with **0** core functions still unverdicted.
  **Cleanup (2026-09-13), three results.** (1) *The overlay's targets now name real methods.* A
  mechanical sweep — every `rule` row whose `target` spells `FStruct::Method`, checked against an
  index of every member declared, defined or inherited anywhere in `Source/` — found **66** rows
  still carrying a placeholder 29c had guessed (`FUN_10…`, `Field_0x…`, `clan_offset`,
  `Slot0x630c`, `vfunc5`) although 29c-1 had ported the body under a recovered name; all 66 were
  retargeted to the definition that cites the address, **66 → 0**, with three further rows whose
  name *was* a real member but the wrong one (`0x102d08c0` → `HintKill`, `0x1037c2f0` →
  `GhoulCroucherScriptUnhideTail`, `0x1017f8b0` → `DialogHunterThreatCount`). Two rows named no
  port body at all and are now ported, cited and tested: `0x10026e70` is **not** a species
  override but `CBaseEntity`'s own slot-153 body — every class on the NPC line carries
  `0x10280300` — and lands as `FElysiumNpc::BaseEntityIsMoving` beside its twin in family Motor,
  exactly as `CanStandOn` carries `CAISound::FUN_10026f80`; `0x102d2f00` is `CAI_Hint`'s deleting
  destructor, whose kernel-observable half is done to the **owning NPC** (`SetCondition(0x29)`
  first, then `ClearHintNode(0.0)` — a ZERO reuse delay, unlike every other caller's 5.0 s) and
  lands as `FElysiumNpc::HintDeletingDestructor` in family Lifecycle, walked into
  `npc-ai/lifecycle.md`. (2) *The Species arms are wired.* All **18** dispatchers the family left
  unreachable now run from the top of the base body — slots 21/22/23/25/26/497/588 (BaseHelpers),
  482 (Anim), 488/506/510 (Sounds), 593 (Closure) and 599/600/601/602/606/609 (TroikaHelpers) —
  each with a named case in the owning suite proving the species body for its retail class and the
  Troika body for a plain `npc_VCop`. Four species bodies call the body they replace through a
  **direct, non-virtual** thunk in retail, which one function per slot cannot express, so the thunk
  itself is ported (`FSpeciesDispatchScope`: while slot N's species body runs, slot N's dispatcher
  answers "no species body") — per slot, as retail's thunks are. Slots 488 and 506 are layer-14
  bodies story **29d** still owns; they moved out of the generated file only so the override has a
  prologue, keep the identical `elysium.stubs` tally as their Troika arm, and their two new
  `hand:` rows are the reason band 10–18's *Neither* reads 265 rather than 266. One species arm at
  these slots stays unwired and is named: `CNPC_VYukie#602` (`0x103dda10`) lives in family Senses
  as `YukieShouldLeaveMelee` with no Species table row. (3) *The entity-chain walk order was never
  a divergence.* `AutoaimDeflection` (`0x10176930`) walks the **edict array by ascending index**
  (`edict += 0x78`, `i = 1 .. gpGlobals->maxEntities`, skipping the free byte at `edict+0x4c`), and
  the order is observable because the score test is `score <= best`, not `<`: a tie **replaces**
  the incumbent, so the highest edict index wins. 29c-1's note claimed the opposite and called the
  port's walk a modernization. `FElysiumEntityWorld::EntityList` *is* this runtime's edict array —
  its index is the handle index, "stable, never recycled", the map's lump order then the runtime
  spawns — so `ChainEntityList` now walks it by ascending index and
  `Elysium.Substrate.NpcKernelEntityChain.WalkOrder` pins it; the note is gone from the code and
  from the divergence list above. Verification: `uv run elysium build` clean; `uv run elysium test
  Elysium.` **954/954**; `uv run pytest pipeline/tests -q` 4,047 passed; `kernel_ledger --check`,
  `kernel_shape --check` and `gen_kernel_shape --check` all pass; `merge_verdicts --audit` still
  reports layers 0–9 with **0** unverdicted and *Neither* **0** and **0**.
- [x] **29d. The middle: layers 10–18.**
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
  Landed (2026-09-14). **The reading.** 344 core functions, every one verdicted: **269 `rule`, 67
  `mechanism`, 8 `present`, 0 `dead`, 0 `unsettled`**, and `coverage.md`'s *Neither* for layers
  10–18 is **265 → 0**. The count the story's premise got wrong this time is the opposite of 29c's:
  where 29c expected trivia and found bodies, this band expected 347 and holds 344, of which **241
  are over 64 bytes** — it really is the middle, and almost none of it is an accessor. The 67
  `mechanism` rows are not a spread: **63 of them are one family**, the per-species `vfunc5`
  deleting destructors, and the reviewer read the largest (`CNPC_VTzimisce` `0x103b6e90`, 225 bytes)
  to confirm it is pure `CUtlVector` teardown before accepting the other 62. The remaining four were
  each audited individually and **three were re-verdicted `rule`** — the `CNPC_VMingXiao`,
  `CNPC_VMingXiaoTentacle` and `CNPC_VTzimisceHeadClaw` slot-126 `Save` bodies, which bracket the
  archive call with in-place NPC writes under retail-chosen sentinel modes and are the rubric's
  explicit "a retail-specific rule around the mechanism" case. So the `mechanism` verdict — the one
  word that makes work disappear — has **100 % audit coverage** in this band.
  **Two systematic errors the review found, both band-wide.** (1) *A base body and its Troika
  override cannot both be `hand:`.* `gen_kernel_shape` binds a slot to `bodies["CAI_BaseNPCTroika"]
  or bodies["CAI_BaseNPC"]`, so only the Troika body is the slot's body; **seven** rows had given
  the base body the same `hand:` target as its twin (slots 104, 124, 126, 310, 326, 380, 469) and
  each now takes its own name on 29c-1's `BaseEntityIsMoving` precedent — `BasePrecache`,
  `BaseSave`, `BaseSetActivity`, `BaseOnLooked`, `BaseLeaveGrappleState`,
  `BaseDrawDebugTextOverlays`. The seventh, the unnamed `0x103482e0`, turned out to be the
  **knockback eligibility predicate** already walked in `combat-and-damage.md` and is `present`.
  (2) *A species override targets the slot's own method.* Band 0–9 records that convention — 57
  addresses all target `FElysiumNpc::SquadSlotName` — and **61 rows** of this band had coined a
  per-species name instead (`GargoylePrecache`, `CopIRelationType`, `WerewolfTaskFail` …). All 61
  were retargeted; a mechanical sweep (`species-sweep-10-18.py`) now reports 116 species slot rows
  on the slot's method and **7 off it, every one a confirmed false positive** — `CAI_Motor` has a
  21-slot table, `CAI_Navigator` 18, `CAI_StandoffBehavior` 29 and `CAI_StandoffGoal` is a 246-slot
  goal entity, so those slots are on their own vtables and their coined names are right. Left
  uncorrected, the 61 would have become 61 port methods nothing dispatches to — the "one species arm
  stays unwired" defect 29c-1 had to name.
  **The port.** 16 families in four waves of three, each owning one
  `ElysiumNpcKernel<Family>10.{inl,cpp}`, one `Elysium.Substrate.NpcKernel<Family>10.` suite and its
  own walked sections: SpeciesMisc10 41, Senses10+SpeciesSenses10 34, Sounds10 28, Precache10 28,
  Anim10+SpeciesAnim10 26, Combat10 21, SaveRestore10+Lifecycle10 22, Conditions10 20,
  Social10+Hints10 17, Debug10 15, Motor10 12, SpeciesLifecycle10 12 — plus a 17th group of **67
  rows that need no body at all** (the destructors). **236 automation cases in 13 new suites** and
  **222 of 222 `rule` bodies over 64 bytes** carry a walked paragraph, across `senses.md`,
  `social.md`, `conditions-and-states.md`, `lifecycle.md`, `schedule-kernel.md`, `shape.md` and
  `combat-and-damage.md`. The other 19 bodies over 64 bytes are `mechanism`/`present`, which the
  rubric exempts.
  **The build-order problem, and the protocol that already existed for it.** The batches had spelled
  75 slot rows `hand:` at reading time, which tells the generator to emit no stub — regenerating
  would have deleted 75 definitions before a single body existed and red-linked the tree for the
  whole porting phase. 29c→29c-1 had exactly this shape and solved it by flipping a row to `hand:`
  **as its body landed**, so that is what happened here: the prefix was held off 76 rows
  (`hand-held-10-18.tsv` is the record, the method names untouched) and each family flipped its own
  back. All three `--check`s pass at every point between waves, which is what made the waves safe to
  run in parallel.
  **What the reading changed.** More than a hundred of the checklist's one-line walks were wrong and
  are corrected in the code, the prose and the overlay's *evidence*. The ones that would have
  shipped wrong behaviour rather than wrong prose: `0x101cf5c0` is **`UTIL_SetOrigin`, not
  `NDebugOverlay::Line`**, which inverts the damaged row `0x102e1760`'s last arm — it *moves the
  body to the trace endpoint*, so it **is** the step and not a debug draw; `DAT_1070b22c + 0x8c` is
  **`IVEngineServer::IndexOfEdict`**, not `AddEntityTextOverlay`, and its answer is the first
  argument of an eight-dword `NDebugOverlay::EntityText` (a misidentification that ran through
  29c-1's code too); `CanTalk`'s three field names were **rotated** (`m_iDialog` `+0x128`,
  `m_bWillTalk` `+0x1088`, `m_bfNPCStateFlags` `+0x5b64`); both `OnStateChange` rows were walked as
  door hooks and **neither has a door in it** — `0x1037e2d0` calls `InputSetRelationship(this,
  "player D_HT 10", 0)`; `0x1032d0c0`'s second arm is a **stack split, not an ammo test**;
  `0x102b7f40` has **three** arms, not one; `0x10278650` is `GetShootTarget`, not a standoff anchor;
  `0x103a0670` is `CNPC_VNewscaster`'s, not MingXiao's; `m_altEquipment`/`m_spawnEquipment` and
  `+0x66b0`/`+0x66b8` were **swapped**; the dialogue-directory chop is **4**, not 5; and two
  boolean-polarity inversions (`0x103854f0`'s aggressive-anims gate, `0x10268900`'s spread gate).
  Two claimed retail quirks were **decompiler artifacts** and correctly not reproduced, and several
  real retail defects **were** reproduced: a format string missing its conversion character
  (`"aim_yaw: %.3"`), a flush gated on the wrong loop's counter that drops two of seven pending
  abbreviations, a dead relationship arm behind a word nothing writes (`+0x6658`), the Werewolf's
  footstep table being a copy-paste of the Tzimisce's, and the maker running both perception
  derivations on itself instead of the child.
  **Constants.** Story 29c-1 read 42 `.rdata` cells out of the pinned image; this band read **more
  than sixty more** the corpus does not hold, including two concept names (`Pain`, `Flee`) and
  `_DAT_10451acc` = **64.0**, which three families reached independently from three different bodies
  and the reviewer read back a fourth time from the file — because `ElysiumNpcKernelSchedule.cpp`
  was carrying that same cell as `GScheduleMeleeHeightDiffUnits = 0.0 /* UNRECOVERED */` in a
  **live threshold**. At 0.0 only an enemy exactly level or below disarmed the melee height-diff
  timer; at retail's 64.0 anything within 64 units counts as level. That is a defect in landed work
  that this band's reading closed, and it is the reason the story looked for more of them.
  **Three more defects in already-landed work, found and closed.** 29c-1's `RestoreExtendedHeader`
  (`0x1027c160`, band 5–9) had misread `thunk_FUN_101cf250(float*, mode)`'s second argument as a
  count and ported a **save/restore clock re-base retail does not perform**; the body is corrected
  in place and its test rewritten. Family Debug's `ActiveWeaponEntity` answered `nullptr` claiming
  no kernel accessor exists, when `FElysiumInventory::Active` is exactly that accessor and four
  recovered bodies open with the call. Debug10's own `TzimisceIsCarryingBody` seam answered false as
  "no port counterpart", when the whole of `0x103be130` is `(m_bfAINPCFlags >> 5) & 1` =
  `CARRYING_BODY`, a word this runtime carries. A fourth was closed at the story's close:
  `TraceMessageFormat` was a passthrough seam and now runs Conditions10's ported `0x1028d990` with
  retail's own `0x200` buffer size, handed in as a parameter so the "size ≤ 0 writes nothing at all"
  arm stays reachable.
  **The four damaged rows** were read from the listing, confirmed against `vtmb_asm` by a second
  reader, and all four stand: `0x102e1760` (`CAI_Motor` slot 20 — with two corrections: the trace
  record is copied into the caller's **second argument**, the `AIMoveTrace_t` out param, not back
  into the move goal, and `return 4` additionally requires the goal's expected blocker `+0x34` to be
  **non-zero**), `0x10292500` (Troika's pre-think debug pass — and **not dead** despite `0d/0v/0c`:
  `NPCThink 0x10292de0` reaches it through thunk `0x10008b57`), `0x102b5d90` (slot 380, `present`)
  and `0x102d72f0` (`CAI_TestHull::Spawn`, verified constant for constant). None is `unsettled`.
  ***Producers later*, reported rather than claimed.** The story promised this drops to zero for
  layers 19–26. It reads **18**, not zero — and **zero of the 18 name a producer in layer ≤ 18**, so
  nothing in 19–26 is waiting on this band. Every remaining one is 29e's own forward reference
  inside 19+: `NPCThink 0x10292de0` writing `+0x6264`/`+0x6268`, `NPCInit 0x1029a0b0`, Troika
  `Spawn 0x10298d30`, `StartTask 0x102a1910`, `0x100521e0`. They stay because 29e ports them.
  **The measure.** `gen_kernel_shape --report` gives the 29d band **89 → 17** stubs. Fourteen of the
  seventeen have no verdict at all and are **not core** — `CBaseCombatCharacter`/`CBaseAnimating`
  slots this band's closure never read, checked one by one against the checklist — and the other
  three are the destructor rows. This is the same place 29c-1's 145 stand, and the same argument.
  **Decisions taken**, none with a precedent: (1) a family is named `<Concern>10` rather than
  extending 29c-1's same-named family, so a file says which band it is and the two never contend for
  one `.cpp`; (2) the species-arm convention is the *bare slot method*, with a large arm free to be
  a private helper, because the overlay's target then answers "which slot does this body serve";
  (3) the port's deferred `NPCInit` (admission, loadout, a parked director's order) is **not** part
  of `NPCThink`, so a species body that replaces the think must still run it — without that a
  payphone is never admitted and **cannot be talked to at all**; (4) `Precache` and `Save`/`Restore`
  are ported and deliberately **not wired** into `Spawn`/`Serialize`, because this substrate acquires
  assets for a whole map epoch first and a call there would add an event retail's order does not
  have.
  **Divergences, all named, all crash guards or unreachable:** null-argument guards where retail
  faults (`FireBullets`, `KeyValue`, slot 594, `QuerySeeEntity`, `OnObstructingDoor`,
  `GhoulCroucherStartTouch`, `EquipZombieFists`, slot 348's zero cap, `GetHintEndpoint`,
  `LoadNewscasterStories`), `ProcessTweakParam` logging where retail's `Error()` exits the process,
  and the overlay durations, which `FDebugLine` has no column for and which decide only how long a
  picture stays on a screen this runtime does not have. One divergence was **closed** rather than
  added: `EnterNpcLine`'s `CandidateRows > 0` gate on the "I do not have a valid reply." fallback was
  a port invention — `CDialog::fill_packet 0x100e7da0` tests only `(m_bSawDisabledRow == 0) &&
  (survivors == 0)`, and `get_pc_responses 0x100e82d0` answers 0 for a band with no PC rows, so an
  under-authored terminal line offers that substitute row in retail too.
  Verification: `uv run elysium build` clean; `uv run elysium test Elysium.` **1,190/1,190** (954
  after 29c-1's cleanup, plus 236 new cases in 13 suites), 0 failed; `uv run pytest pipeline/tests
  -q` **4,047 passed**, 22 skipped; `kernel_ledger --check`, `kernel_shape --check` and
  `gen_kernel_shape --check` all pass; `merge_verdicts --audit` reports layers 10–18 with **0**
  unverdicted and *Neither* **0**. Band 19–99's *Neither* fell 298 → 289 as a side effect: nine of
  29e's functions are now cited by this band's port.
  Unrecovered, and counted as such: the `CVDmg_t` near-miss bands; the VSound concept list (nothing
  in `vdata/` holds the eighteen names, so no concept can actually be played); `DAT_109340d8`'s
  selector; `DAT_1093acac`/`DAT_1093acb0`; `m_rflRegrowTimers`' per-limb index; a dozen ConVar names
  and defaults; and whether the shipped CRT emits the literal prefix for retail's incomplete
  `"aim_yaw: %.3"`. Two reachability gaps are named rather than patched: **six retail classes carry
  no entity classname in the census** (`CNPC_VCop`, `CNPC_VGhoulCroucher`, `CNPC_VWerewolf`,
  `CNPC_VZombie`, `CScriptedTarget`, `CNPCMaker`) so their arms cannot be selected at runtime, and
  `npc_maker_zombie` is not a registered spawn leaf — every such arm is ported whole and driven by
  retail class in a test. One behavioural gap belongs to another story and is stated here: the
  ghoul's touch burn deals no health damage, because `BurnPlayer` faithfully builds a family-less
  `FElysiumDmg` (`1037c140 PUSH 0x0`) and this port's damage resolver rejects a descriptor with no
  family — that join is `CBaseEntity::TakeDamage`'s, not this band's.
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
  **Reading banked (2026-09-14); the story stays open for the port.** The band is **19–29**, not
  the 19–26 the story text names: `order.md` has been regenerated since that sizing and now runs to
  layer 29, so the whole top band is 29e's and nothing may be left unverdicted above 29d.
  `"19-29"` joins `CHECKLIST_BANDS` and `docs/vtmb/npc-kernel/checklist-19-29.md` is committed.
  **The reading.** 468 core functions, **every one verdicted**: **466 `rule`, 2 `present`, 0
  `mechanism`, 0 `dead`, 0 `unsettled`**, and `coverage.md`'s *Neither* for layers 19–29 is **289 →
  0**. No pack is missing — all 18 reading packs returned, plus both damaged batches. The band's
  shape is the opposite of 29c's and sharper than 29d's: **zero `mechanism`**, because a schedule
  interpreter's body is a rule even when it calls `memcpy` on the way, and the only two `present`
  rows are a three-step oblivious helper the port already carries at both call sites and a species
  `GatherConditions` whose whole content is an unconditional chain to the base.
  **Two spine bodies were read from the listing rather than the decompiler**, with their two-level
  MSVC jump tables byte-read out of the pinned image, because Ghidra's `case N` labels are arm
  indices and not task ids: `CAI_BaseNPC::StartTask 0x102827f0` (byte table `0x10287138`, arm table
  `0x10286f8c`, **107 arms**) and `CAI_BaseNPCTroika::StartTask 0x102a1910` (`0x102a7ab8` /
  `0x102a77f8`, **176 arms**). Both walks are recorded, together with the full **330-entry task id →
  name table** from the runtime registrar `FUN_10316ff0` and the **42-string `TaskFail` reason
  vocabulary** from the text pointer table at `0x106152b0` — which makes every failure exit in the
  band self-describing.
  **The 11 damaged rows all stand; none is `unsettled`.** The structural finding is that **not one
  of them has a jump table**: in every case Ghidra's "unrecovered indirect jump" is a *virtual tail
  call* (`JMP dword ptr [reg+0xNNN]`) in the epilogue, and `0xNNN / 4` is the slot. The recurring
  "unrecovered jump through the active weapon's vtable `+0x108`" that blocked four `NPCInit` rows is
  simply slot 66 `CBaseEntity::Hide 0x1009d2a0`. It also settled that the three 39-byte `NPCInit`s
  are **not** one body: `CNPC_VHunter 0x10388b30` and `CNPC_VYukie 0x103dd800` each chain
  `CNPC_VHumanCombatant::NPCInit 0x10387140` rather than Troika's, so both hide the active weapon
  twice. `0x1028a260` needs no row of its own — it is `GetNewSchedule`, layer 0, already verdicted
  in band 0–4, and this band decoded it: clear `+0x1b2c`, slot 437 `GetSchedule`, else tail-jump
  slot 438.
  **Three systematic batch errors were found and corrected before the merge**, each against a
  landed story's precedent, by a mechanical sweep over 281 rows: (1) **18 rows** gave a
  `CAI_BaseNPC` body the same `hand:` target as its Troika twin — 29d's error (1), since
  `gen_kernel_shape` binds a slot to the Troika body — and each now takes `Base<Method>`
  (`BaseNPCThink`, `BaseRunAI`, `BaseGatherConditions`, `BaseSelectIdealState`, `BaseStartTask`,
  `BaseRunTask`, `BaseSelectSchedule`, `BaseNPCInit`, `BaseTranslateSchedule`, …); (2) **193 rows**
  coined a per-species name instead of the slot's own method — 29d's error (2) — and were
  retargeted, with the sweep's Troika-line guard correctly *sparing* `CAI_Motor`, `CAI_Navigator`,
  `CAI_StandoffBehavior`, `CAI_StandoffGoal` and the `CCineNPC` line, whose slot indices are their
  own; (3) **70 `registry:` rows** named a datum the census cannot carry — `gen_kernel_shape`'s
  `CONSTANT_BODY_RE` matches only `{ return; }` and `{ return <literal>; }`, so each would have
  produced an empty `Default` and the behaviour would have vanished silently — and all 70 are now
  method targets. The band-wide ruling behind (3), recorded because it had no precedent: **a
  species override is a registry row only when the census can hold its whole body**; a translation
  table is a switch, not one literal.
  **Three defects in already-landed work were found and closed on the way.** (1) A latent
  **unity-build ODR collision** in 29d's family SpeciesMisc10: both halves defined
  `GSlowExpireSentinel` / `GSlowEntityMagnitude` in an anonymous namespace, which is *not*
  file-local under a unity build. It compiled only because the chunk boundary fell between them,
  and this story's census growth moved it — the hazard is that such a collision is invisible until
  an unrelated file changes size. (2) The **Tzimisce footstep pair sense was backwards**:
  `footsteps.md` recorded "which pair the `+0x9ac` flag selects" as unrecovered and the port guessed
  flag 1 = `foot_steps_1/2`; `0x103c4160`'s own listing (`103c41d8` / `103c420e` / `103c424b`),
  reached while working the damaged `CNPC_VTzimisceRunner::HandleAnimEvent 0x103c32c0`, gives **flag
  0 → `1/2`, flag 1 → `3/4`**, so 2050 is the `3/4` pair. The code, the oracle row, the
  unrecovered-list entry and (3) **the landed test that had pinned the guess**
  (`ElysiumNpcFootstepTests.cpp`) are all corrected to retail with the listing cited inline — the
  test was corrected, never the finding.
  **Wave 1 of the port was first started and cancelled** (State19, TranslateSchedule19,
  Lifecycle19) when a session's budget ran out; nothing of that attempt reached the tree, but its
  *reading* was banked at `$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/cancelled-wave1/` and
  the three families were then ported from it (see "Port progress" below). The banked reading is
  worth having: it confirms the `HasInterruptCondition` gate on `0x102ad660` from the listing,
  corrects thirteen `SelectIdealState` walks and nine slot-440 ones, transcribes all twenty species
  `TranslateSchedule` tables row by row, reads a dozen more `.rdata` cells, and names **three more
  landed tests that encode port-invented behaviour** (`Elysium.Substrate.NpcEnemy.IdealState` and
  two cases of `Elysium.Substrate.Schedule.TroikaTranslate`) for the port to correct to retail. Two
  findings there change the shape of a function rather than a constant: `CAI_BaseNPC::
  TranslateSchedule 0x102cc080` answers through a **virtual `CALL` back into slot 440 on the
  object's own vtable** (so a species class re-enters its *own* body, and the fall-out path is the
  **identity**, not "nothing"), and the frenzied pre-table's `0x87`/`0x88` tail tests the
  **complement** of `MADE_HUNT_PATH`. One brief this story wrote was itself wrong and is corrected
  in the record: `+0x1b38` is **not** absent in the port — 29d stood it as
  `SelectIdealStateSelector` — so the twelve fifteen-byte slot-461 species bodies are real data rows.
  At the banking, no body was ported and no slot row was flipped to `hand:`: all 54 rows that claim
  `hand:` were held with the prefix stripped (`hand-held-19-29.tsv` is the record, method names
  untouched), so every one of the band's 28 slots was a generated stub and the tree linked. The
  divergences the reading names for the port: the port's task runner **fails** an unknown task
  where retail only `DevMsg`s and keeps the program running; `SelectIdealState 0x102ad660` gates on
  `HasInterruptCondition` (the running schedule's mask), not `HasCondition`, with the two flee arms
  as the exceptions; and `CNPC_VSabbatLeader::TaskFail 0x103a9400` does **not** chain `0x1029adb0`
  on its flip path, which breaks the port's "species arms are a prologue" invariant.
  **Port progress (2026-09-14): 4 of 16 families landed, 106 of 457 rows; the story stays open.**
  The port runs as one headless coding agent per family (Grok 4.6 at `xhigh`, from a brief per
  family under `$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/`, launched by `grok-launch.ps1`;
  Codex `gpt-5.6-sol` is set up as the alternative, `codex-launch.ps1`), each followed by a full
  Opus review that reads every body against the listing and fixes what it finds; `HANDOFF.md`
  beside the briefs carries the run order and the review rules. Landed: **State19** (32 rows —
  `SetState 0x1026e340` writing both state words and dispatching slot 463 with the entry value;
  `SelectIdealState` base `0x1026f660` / Troika `0x102ad660` / VHuman / SabbatLeader / Tzimisce /
  Guard1's fourteen law arms / Pedestrian's 512-unit ladders / Cop / Dog / Animal / Zombie /
  Werewolf; the twelve fifteen-byte species rows as census data; the port-only ideal-state paths
  deleted; `m_NPCState` with one source of truth that can spell NONE), **Translate19** (22 rows —
  slot 440 base `0x102cc080` calling back into the object's own slot, Troika `0x102b12f0` whole
  with the `1/0x6b → 0x132/0x6b` arm replacing the port's partial body, the frenzied pre-table
  `0x102b11c0`, twenty species tables through the class lookup), **Lifecycle19** (42 rows —
  `NPCInit` base `0x10273390` / Troika `0x1029a0b0` in listing order, `StartNPC`, `OnRestore` with
  25a's CRC keep-rule and both patrol-pair validations, thirty-one species inits and the maker's
  `0x1034c260`; `FElysiumNpc::Activate` now calls `NPCInit` and the port-only think-stamp fragment
  is gone, so retail's `curtime + 0.1` first think stands and the shared test fixture activates at
  `-0.1`; the four tuning values come from `Rules.txt` through the rulebook; `IsBccTargetable`
  joined to `+0x1480`; `m_bCineScriptHidden +0x5d78` bound as its own byte). On the way, 25a's named
  divergence closed: Troika `EnterGrappleState 0x102b5c00` is ported whole and the stealth-only
  gate is gone. Tests 1190 → 1241; `hand-held-19-29.tsv` 54 → 37. **What the reviews found the
  porting agent gets wrong, for whoever continues**: a seam invented beside an accessor an earlier
  family already landed (twelve cases across the two runs, each silently disabling a live arm); arms
  stubbed inside a body whose row reads "ported" (WerewolfRearm shipped 4 of 23); values invented
  where the image holds them (the Camera's immediates shipped as the Troika tuning); the port bent
  to the fixture (`ArmThinkNow`). Remaining, in order, one brief each: Conditions19 (23),
  Spawn19 (60), RunAi19 (34), StartTask19 (28), RunTask19 (24), Select19 (39,
  plus the agreed visual-only modernization: the selector, ideal-state and `TaskFail` stamps carried
  as retail-address members and rendered in the story-23 debugger, the `__FILE__`/`__LINE__` words
  staying absent), Damage19 (26), Script19 (32), Think19 (15), Boss19 (19), Werewolf19 (17), Misc19
  (34, which closes the story: the absorbed stories' sentences, the `[x]`, the build-order line, and
  the file rename dropping the layer suffix from `ElysiumNpcKernel<Family>19`). Open hazards
  recorded by the reviews: `FElysiumEntityWorld::Activate` iterates `EntityList` by ranged-for while
  `NPCInit` may create entities (Werewolf, ghoul) — index the loop; the live sense cone reads the
  0.2 constant, not `RetailFieldOfViewDot`, so the Werewolf's −0.5 does not reach vision yet;
  `GetMoveType` / `GetSolidFlags` / `SetMoveType` are still 29c stubs; the cine-hide setter
  (`ScriptHide`) is unported.
  **Maintain19 landed (2026-09-14): 10 rows.** `MaintainSchedule 0x102817c0` is now the one
  interpreter loop: validity and same-call reselection, the state/door/fail routes, null retry,
  the five-value task status, start/run overlays, continuous-move memory, 10/1 completion and 8 ms
  budgets, both `ai_step` exits and the common `m_bDidMaintainSchedule` write all run in listing
  order. `0x102ae750` / `SetSchedule 0x102ae780` and `ForceScheduleChange 0x102ae490` preserve the
  dead/alive gates, live-cine cancellation and two ordered slot-435 calls; base/Troika and all four
  species `OnScheduleChange` bodies stand, including `VacateSquadSlot`, opening-door slot 532 and
  the Werewolf's 51-entry history. `TaskMovementComplete 0x10273ec0` carries all five statuses,
  and SabbatLeader `TaskFail 0x103a9400` returns without the base chain on its two flip arms.
  Review corrected two more incomplete claims before landing: `timeStarted +0x5c48` and
  `timeCurTaskStarted +0x5c4c` are now distinct schedule-state words written at install/task start,
  and base `OnScheduleChange 0x1027a700` reaches the already-ported `VacateSquadSlot 0x1028ae60`.
  It also retained the worker's corrections of the aliased `+0x1b24`/`+0x632c` timers and narrowed
  ideal-schedule enum: `m_IdealSchedule +0x5c3c` is raw int32. Tests **1,241 → 1,247**; pipeline
  tests remain **4,047 passed**. The scratch held list is reconciled as **49 → 48 raw rows** and
  **42 → 41 non-`hand:` rows**; the earlier printed 37 was stale, because seven scratch rows were
  already `hand:` and raw length was never an unfinished-body count. The full review was performed
  locally at the owner's request, with no review agent; it found the two omitted arms above and
  records that missing-arm habit for the next family. No new gameplay divergence. Remaining seams:
  the unexposed `ai_step` console producer, the existing absent move-and-shoot controller
  `0x102e8560`, the missing inverse-interrupt program column, and raw selector results that
  Select19 owns. Remaining families, in order: Conditions19 (23), Spawn19 (60), RunAi19 (34),
  StartTask19 (28), RunTask19 (24), Select19 (39), Damage19 (26), Script19 (32), Think19 (15),
  Boss19 (19), Werewolf19 (17), Misc19 (34).
  **RE pass 2026-09-21 — read before porting the remaining families (`docs/specs/RE-BACKLOG.md`).**
  Conditions19, Select19, StartTask19 and RunTask19 each touch a body the port currently GUESSES,
  and seven of eight guesses were wrong (`conditions-and-states.md` § "The port's NPC-core guesses,
  settled"): the alert selector (case 3 ends in `0x4b ALERT_WAIT`, never the lookaround); the
  attack bands (melee 64 / 256 / 180, ranged 100 / 200 / 1024 or the weapon's min / max, dot 0.5
  and 0.7); four separate sight / weapon-line traces; heavy damage `> 20`, repeated `> 30 %`;
  `0x1026fb40` is the better-weapon search, not a `NEW_ENEMY` repair. For the task families:
  a bare-`SetGoal` path task completes INSIDE `SetGoal` and sits running through the retry window
  (`schedule-kernel.md` § "`SetGoal` DOES complete the task"); `IsScheduleValid` makes a schedule
  uninterruptible while the navigator type is jump or climb; the comfort chain advances by
  `SetIdealActivity(m_Activity + 1)`. The witness code's guesses are settled in `population.md`
  § "The law transaction's guesses, settled".
  **Passes (2026-09-23).** The remaining twelve families run as 0019 story 8's three passes — R the
  retrieval packets, I the two-lane implementation, C the close — with the scope counted after the
  strict verdict (286 `rule` rows of 351) and the retrieval method as piloted, all recorded there.

### The mind

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
  Unrecovered as a live producer: `m_hFollowerBoss` (16a). (`m_bInPlayerLOS`'s producer was
  closed 2026-09-21, `senses.md`: `SetPlayerLOS 0x10291610` — a 2 s cadence, PVS, a 512-unit
  free pass, one eye line at mask `0x4091`, and an 8 s hold; no cone.) Arms 1-3, the sibling gates, and the base fall-through are ported; arm 4 is
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
  programs, walked". Unrecovered: nothing. (Closed 2026-09-21, `schedule-kernel.md` § "`SetGoal`
  DOES complete the task": there is no base `RunTask` path arm. `SetGoal`'s find wrapper
  `0x102f1dc0` calls `TaskComplete` itself on a successful find unless the current task is `0x6e`,
  `0x0b` or `0x72`, raises `OnNavFailed(0x0c)` on a refusal, and leaves the task RUNNING through
  the retry window. The port's "complete in the start arm" is retail's shape; what it lacks is
  the retry-window wait.)
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
  base entry `0x1026cdc0` is not type-gated. **That divergence is CLOSED (story 29e's review,
  2026-09-14):** `FElysiumNpc::EnterGrappleState` now ports `0x102b5c00` whole — the burn-list
  discharge and refusal, the ungated base half, the dialogue stop, the cine cancel with its
  immediate `SetState(m_IdealNPCState)`, and the trailing `ClearSchedule` — with the
  stealth-only gate deleted, exactly as 29d deleted `LeaveGrappleState`'s twin. Case
  `Elysium.Substrate.NpcKernelSpeciesMisc10.TroikaEnterGrappleState`;
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
- [ ] **22. `sp_tutorial_1` on the V2 lane.** Moved to 0018 story 20 (2026-09-15).
- [ ] **24. Unreal navigation and retail route outcomes.** Moved to 0018 stories 4 to 9
  (2026-09-15), boundary revised 2026-09-20. Unreal owns pathfinding, reachability and
  locomotion; AIN is translated at bake time into places, nav links and areas.
  Hunt/cover/retreat/flank goal selection applies retail's tests over those places; those
  rules remain observable even when Unreal supplies the walking route.
  A local route can precede node routing in retail, so no universal nearest-component veto.
  These tasks retain their goal kind, failure code and ordering when calling that service;
  ordinary graph nodes/edges require no actors or second pathfinder.
- [ ] **10g. The patrol programs.**
  Retail: a patrol is a `CAI_PatrolPath` object in the `+0x658c` cell (`+0x6590` pointer;
  `+0 type`, `+4 schedule id`, `+8 repeat`, `+0xc count`, `+0x10 index`, `+0x14 nodes[]`),
  built only by three inputs: `SetupPatrolType "<repeat> <type> <schedule>"` (`0x1029eb30`;
  type 0 loops forward, 1 backward, 2/3 ping-pong, `repeat` wraps allowed before the path is
  spent; the schedule by name, `SCHED_%s`, `SCHED_TROIKA_%s`), `FollowPatrolPath "<node…>"`
  (`0x1029ed90`; tokens match hint `Group` exactly, type `10000 || 800`, first in hint-list
  order; no disabled/owner/cooldown filter. A missing token aborts without installing a new
  path; a successful update keeps an existing object's type/repeat/schedule) and `WalkToNode "<schedule> <node>"`
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
  Correction, 2026-09-17: `TASK_PATROL_PATH` sets movement activity, clears memory mask 2
  and completes; it does not read or submit a patrol list. `GET_FULL_PATROL_PATH 0x7c` reads
  only the current node, with its distinct `-1` early return (no shipped program use found).
  Unrecovered: the upstream route expected by the `0x46` program's `WAIT_FOR_MOVEMENT`.
  Use 0018/4's place set and 0018/11's logical path state; I/O/Python and the task loop
  must share the same live state. Evidence: `navigation-jump-links.md` § "Task readers of network data".
  Size: L. Effort: Opus / high.
- [ ] **11. Interesting places: the selector arms.**
  Rework (2026-09-15): the registry, the visitor walk and the entry / loop / release trio
  are 0018 story 10; the programs load from the schedule seam (0019 story 3). This story
  keeps the three selector arms and retires the ambient executor.
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
  Rework (2026-09-15): the node record is 0018 story 11; this story keeps the roll and the
  two task arms.
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
  tasks). Unrecovered: nothing — the absent-key default of `+0x46c` is 0 (entities are
  `calloc`ed and no ctor writes it), so an absent `ip_percent` never takes the interest. (Closed 2026-09-21, `programs.md` § "The patrol-point interest record and the path
  object": the keys are `target_name` and `ip_percent`, authored on all 582 rows, 60 of them
  naming a place; the roll is `RandomInt(0, 99)` strictly `<` `+0x46c`.)
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
  recovery"). Unrecovered: nothing. Closed 2026-09-21 (`conditions-and-states.md` § "The bump
  and interrupt keys, `TASK_RUN_DIALOG`, `TASK_MELEE_KNOCKBACK`"): `+0x33` is the key
  `ShouldRemove_OnWasBumped` (six shipped disciplines) and `+0x34` is DERIVED — set when any
  HitGroup carries an `OnInterruptSchedule` block (Presence's mesmerize hits only);
  `TASK_RUN_DIALOG` holds the disposition activity until `IsInDialog` clears, fires `OnDialogEnd`,
  and a `COND_PROVOKED` interrupt ends the program but NOT the dialogue; `TASK_MELEE_KNOCKBACK`
  is a root-motion clip of an activity chosen before it; and **`COND_KNOCKBACK 0x28` has no
  producer anywhere**, so step 12's knockback arms are unreachable. Also closed 2026-09-21 (`conditions-and-states.md` § "The burning trio, `TASK_JUMP` / `TASK_LAND`,
  and who arms `FINISH_JUMP`"): `0x151` has no interrupts and no movement — three clips, the
  loop ending when its clip does, then slot 616 clears `ON_FIRE` and re-arms the burn 15 s out;
  `ON_FIRE`'s producer is the body-fire particle scan in `GatherConditions`; `TASK_JUMP`
  relaunches the stored arc with no resume branch; and step 9's `FINISH_SPECIAL_NAV` is written
  only by `IsScheduleValid`, which also makes a schedule UNINTERRUPTIBLE while the navigator type
  is jump or climb.
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
  runs ahead of `SelectSchedule`" (Story 26 recovery). Unrecovered: nothing on this story's path.
  (Closed 2026-09-21, `conditions-and-states.md` § "`COND_PLAYER_ON_HEAD 0x3b`: the producer and
  the two dive tasks": the PLAYER's `PostThink` `0x1016be10` raises it on whatever Troika NPC is
  its ground entity, at most every 2.0 s, with no contact test of its own and no gate; `DIVE_SIDE`
  tries right then left over 60 units, `DIVE_FORWARD` 96 units, each a ground `MoveLimit` then a
  root-motion clip under `ANIM_MOVEMENT`, `TaskFail(0x0e)` when blocked; `0x7b` has no interrupts.
  The port needs a player-side producer seam: the ground entity is the player's, not the NPC's.)
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
  10j recovery"). Unrecovered: nothing. (Closed 2026-09-21, `conditions-and-states.md`, the comfort
  tasks: there ARE no transition rows. `DO_COMFORT_LOOP` / `PLAY_COMFORT_OUTOF`'s shared start arm
  `0x102a51e8` is `SetIdealActivity(m_Activity + 1)` over consecutive ids — `INTO 0x106a`, `IDLE
  0x106b`, `OUTOF 0x106c`, the second trio `0x106d`–`0x106f` — and `PLAY_COMFORT_INTO` draws
  `0x106a + 3 × RandomInt(0, 1)`. The oracle had dropped the `INC` and invented a switch.)
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
  Unrecovered: the goal flag 4's retail name and any writer of it (its one reader is
  `UpdateTargetPos 0x10271b10`; none of the 15 `SetGoal` callers sets it). Closed 2026-09-21: the
  comfort sweep's distance reads **slot 220** (`+0x370`) on both endpoints, 3-D, against a running
  nearest that starts at 1024.0; `0x100113d8` returns the goal TYPE.
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
  Unrecovered: nothing. (Closed 2026-09-21, `programs.md` § "`TASK_RUN_DISPOSITION 0xba` and
  `_RANDOM 0xbb`, walked": the start arm only writes `m_flWaitFinished = curtime + operand`; the
  run arm calls slot 588 — `0x10293e50`, which restarts `ACT_DISPOSITION 0xf1` whenever the clip
  ends, so the stance selector `0x102c12a0` is re-asked — and completes at the deadline. No
  condition, yaw or look write; no fail exit. `0xbb` is the same with a `RandomFloat(0, operand)`
  deadline. Operands shipped: 4, 5, 30.)
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
  "The alert programs, verbatim"). Unrecovered: nothing. (Closed 2026-09-21: the path tasks
  complete inside `SetGoal` — `schedule-kernel.md` § "`SetGoal` DOES complete the task"; no best
  sound is `TaskFail(0x12)` before `SetGoal` is reached. `PLAY_COWER`'s run arm `0x102ab83c` is
  `AutoMovement` plus sequence-finished and nothing else, and its start draws the cower variant
  `RandomInt(0,2) × 3` into `+0x6414` — `programs.md` § "The look arms, walked".)
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
  Oracle: § "The `INVESTIGATE` family, decoded". Unrecovered: nothing. (Closed 2026-09-21,
  `programs.md` § "The look arms, walked": `LOOK_AT_BEST_UNKNOWN` looks at `m_hBestSeeUnknown`'s
  abs ORIGIN, else `m_vecLastSeeUnknownPos`, else `TaskFail(0x21)`; the shared run arm's virtual
  is slot 251 `IsActivityFinished`, and the task ends on (timer OR clip finished) AND
  `FacingIdeal`; the eight tasks `0xf8`–`0xff` share it. The path-completion arm does not exist —
  see 10e.)
  Size: L. Effort: Fable / high; corpus pass on the two look arms first.
- [ ] **12a. The reaction keyfields.**
  Rework (2026-09-15): the parse is 0019 story 2, generated from the datamap; this story
  keeps the normalization and the readers.
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
  combat leftovers". Unrecovered: nothing. (Closed 2026-09-21, `programs.md`: NOTHING authors
  hint type 800 — zero of 71,096 shipped entity rows, no classname maps to it, no FGD lists it;
  its one use is the patrol lookup's `|| 0x320` compare, dead by content.)
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
  Unrecovered: nothing. (Closed 2026-09-19 / 20: the two hint arms are walked in
  `navigation-jump-links.md` § "The hint-path, snap and cower arms, walked"; the four cells are
  plain `.rdata` floats — `0x1049a1b0` −2.0 the goal tolerance, `_DAT_10451ad0` 16.0 the kick's z
  lift, `_DAT_10447ee0` 1000.0 the mass-term cap, `_DAT_10457f60` 150.0 added to the impulse's z
  — `programs.md` § "The cover and kick chooser, and the combat leftovers".)
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
  Unrecovered: nothing. (Closed 2026-09-21, `programs.md` § "The saved-position arms and the
  damage position, walked". `FIND_COVER_FROM_SAVEPOSITION` is the shared cover search with the
  saved point as both threat and threat eye, 32.0 to `CoverRadius()`, a TYPE-4 goal, `TaskFail(8)`;
  `GET_PATH_TO_SAVEPOSITION` never reads its operand; `_LOS_NOATTACK` is the shoot-node search
  with flag 1 — plain sight, `0 < d < 4096`, no weapon range or weapon LOS — `TaskFail(0x0b)`;
  `FACE_PATH` faces the CURRENT WAYPOINT, done within 15°. `+0x5b9c` is `m_vecLastDamageAttackPos`,
  written only by `OnTakeDamage_Alive 0x10265ed0`: the INFLICTOR's origin, or with none the
  victim's origin + the attack direction × 64 — never the attacker's origin, never a hit point.)
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
  The hunt target is 0018 story 9's: a reachable place within 256 units of travel best aligned
  with the heading, retail's jitter draws kept; the list builder `0xae` has no issuer and is not
  built. Unreal moves to the resulting goal.
  Consumes: 0018/9 and 11 (the hunt target and the path object), 10g (the path object), 10e/10f (the shared
  tasks), 10k (`0x84`), 16c (`DoFrenzy`'s entry), 15 (the state byte).
  Oracle: § "The `INVESTIGATE` family, decoded" (Case 0xb; "The
  hunt programs and the expiry chain, verbatim") and `navigation-jump-links.md` § "Task readers
  of network data". Unrecovered: nothing. (Closed 2026-09-21, `programs.md` "The hunt leftovers,
  closed": `GET_PATH_TO_LASTENEMY_LKP 0x114` paths to the MEMORY record's last-known position of
  slot 168's enemy — the current one, else `m_hLastEnemy` while state flag `0x40` stands — as a
  type-4 goal whose `-1.0` tolerance keeps the program's `SET_TOLERANCE_DISTANCE 20`;
  `m_flHuntExpireTimer` has one writer, the HUNT arm of `OnStateChange`, and its expiry selects
  `0x85 HUNT_FINISH`, raising no condition; `WALK_PATH_HUNT` only prefers `ACT_HUNT_WALK`;
  `+0x6598` is the hunt path POINTER; the builders' endpoint filter reads no node type, hint,
  cooldown or zone — so 0018/9's hunt target over the place set loses nothing by having none.)
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
  § "Followers, patrols, and loitering". Unrecovered: nothing. (Closed 2026-09-21, `social.md`
  "The distance tasks and `TASKS_FACE_TARGET`, walked": operands resolve at run time through slot
  418; the interrupt-distance tasks truncate then square into `+0x6324` / `+0x6328`; the
  accumulator is the saved float `+0x5bac`, which `OnScheduleChange` does NOT clear though it
  zeroes the thresholds; seven accumulator / distance tasks exist, two of them `_RND`; `_F` is
  tested against `GetFollowerBoss()`, 3-D, strict; the radii come from `rules.txt`
  `Npc_Follower_Info` by follower type, clamped 10 apart; `TASKS_FACE_TARGET` is flags2 `0x20`,
  read only by the wait tasks' `0x102aab70`, motor yaw at `-2.0`.)
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
  Rework (2026-09-15): the squad object, membership, the cap and the disconnect refcount are
  0018 story 14; this story keeps the condition producer, the two tasks and the overlay's
  clear.
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
  Oracle: § "Squads, decoded" (unrecovered list closed 2026-09-12). Unrecovered: nothing — the
  six `CAI_Squad` wrappers were closed 2026-09-19 (`social.md`, "The six wrappers"): member
  fan-outs to NPC slots 54–58, of which only 54 and 56 do anything.
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
  recovery"). Unrecovered: nothing. (Closed: bytes `+0x6360/+0x6361` are uninitialised stack the
  writer `0x1028ea60` copies out of its own frame — a retail defect, `senses.md`; `0x1042fde0` is
  SafeDisc's `CSecureType` scrambler over a plain integer, `dead` under 0019 § Witness data, so
  the port keeps the level plain.)
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
  Oracle: § "The flee state and the cower, disoriented and lost programs". Unrecovered:
  nothing. (`FLIP_NEXT_IDEAL_YAW` closed 2026-09-21, `programs.md`: a byte at `CAI_Motor +0x28`
  that turns every committed ideal yaw by 180° until the program clears it; `SET_COWER` walked
  beside it.) (Closed 2026-09-19: the cower-node query —
  slot 418 `+0x688` resolves the task operand's sentinel to a distance and the search is walked in
  `navigation-jump-links.md` § "The hint-path, snap and cower arms, walked".)
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
Reworked 2026-09-15. The 29-series' order — bottom-up by call layer — stays; its scope — the
whole closure — does not. Programs are data (0019 story 3), the world's objects are 0018's, and
a function is ported only if something can observe it (0019 § Scope).

1. **0019 stories 1–4 and 0018 stories 1–3, in parallel.** Until 0019/3 lands no program is
   typed by hand; until 0018/5 lands no path task is wired.
2. **29e finishes under the strict verdict** (0019 story 8).
3. **0019/5 the class tree**, alone on a branch; then **0019/6** the deletions and the
   mechanism seams; then **0019/7** the reach cut.
4. **The mind, cut to `sp_tutorial_1`'s reach**: task bodies and selector arms only, in the
   order the selectors reach them — 25a; 10d, 10e, 10f, 10h, 10g, 10i / 10j, 10k, 11 / 27,
   12b; then the overlays and the social families — 16a, 16b, 16c, 17, 21a, 21b, 21c; then 28
   and 13b.
5. **The hub's reach** (0018 story 20): the idle families first.

The open stories, re-scoped. A story keeps its number and its retail text; what moved is named.

| Story | Keeps here | Moved out |
|---|---|---|
| 25a `ClearSchedule` producers | the wiring | — |
| 25b species `TranslateSchedule` | the twenty species bodies, on their classes after 0019/5 | the pre-table's ids → the seam |
| 25c `MaintainSchedule` exits | absorbed by 29e | — |
| 22 | — | 0018/20 |
| 24 | — | 0018/4–9 |
| 10g patrol programs | the roll sites, the task arms | programs → seam; paths → 0018/11 |
| 11 interesting places | the three selector arms; retires the ambient executor | programs → seam; registry, walk, trio → 0018/10 |
| 27 patrol-point interest | the roll, the two task arms | the record → 0018/11 |
| 26 `GetSchedule` | absorbed by 29e | — |
| 28 player-on-head | as written | — |
| 10i comfort program | the task arms | program → seam |
| 10j `CheckTarget` | as written | — |
| 10d alert selectors | the two selectors, the ladder, the tail | the four programs → seam |
| 10e sound-investigation | the task arms | programs → seam; the list → 0018/13 |
| 10f unknown-investigation | the task arms | programs → seam |
| 12a reaction keyfields | the normalization, the readers | the parse → 0019/2 |
| 12b cover and kick | the chooser | programs → seam; the searches → 0018/8 |
| 10k saved-position programs | the task arms | programs → seam |
| 10h hunt-investigation | the task arms | programs → seam; the hunt path → 0018/11 |
| 13b the leak | as written | — |
| 16a followers | as written | — |
| 16b composed relationship | as written; the ideal state in 29e | — |
| 17 squads | the condition producer, the two tasks | the object → 0018/14 |
| 16c possession and frenzy | as written | — |
| 21a flee | the selector, the task arms | programs → seam |
| 21b cower, disoriented, lost | the task arms | programs → seam |
| 21c incapacitated consumers | the loop half in 29e, the rest here | — |

Sizes are re-read after 0019/7: a story's size is the task identities and selector arms the
reach cut leaves it, not the programs it once listed.

## Seams
- Provides: the awareness seam (`Cognition.Conditions`, the enemy memory, `Senses.Memory`,
  `IsOblivious()`, `ShouldInvestigate`) to 0005 and 0007; the stealth scalars; the failure path
  (13), the cadence (15) and the route refusal (24) every later program family runs on (0005,
  0006, 0003); the composed
  relation (16b) to 0005, 0006 and the target HUD; the trance and the flag word to the feed
  and dialogue gates (0004).
- Consumes: the HitGroup apply path and the cloak/detection-record producers from 0006 (6a,
  16c); the grapple state machine and damage from 0005 (3c, 21c).
- Consumes (2026-09-15): 0018's query surface — hint searches, place selection and the trio,
  patrol records, the sound list, squads and the shared memory, the melee slot, the route
  refusal; 0019's loaded programs and id spaces, generated bindings, tunables, class tree and
  reach cut.
