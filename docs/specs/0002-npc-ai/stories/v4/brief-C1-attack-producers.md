# Brief C1 — V4c: `Weapon_FrameUpdate`, the NPC attack producers, slot 247 with its bbox (coder; no build)

**Final (amended after V4r, 2026-10-04 — R2, J2, J6).** Read `README.md` here (§1 "The frame",
"The events" with the attack producers' amendment, "The attack extents"; §2 M3, M10, M11; §8 Q4,
Q6, Q7; § "Shared names"), `packets-R2.md` items 2, 3 and 5, the rulings J2 and J6
(`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it),
`docs/vtmb/combat-and-damage.md` § "The NPC attack producers, start to commit". After V4b's commit
(or V4a's, if V4c runs first). Re-locate by Grep.

## Files (only these)

Runtime (`Source/ElysiumUE/Private/Substrate/` unless a path says otherwise):

- `ElysiumNpcBaseMotor.cpp` (`PostRun`'s weapon line, `++PostRunWeaponUpdates` ~:597, only)
- `ElysiumWeaponClasses.{h,cpp}` (the `ContactEventCycle` estimate ~`.h:303`, `.cpp:1263-1317,
  2166`; `OperatorHandleAnimEvent` ~:1349; the weapon's frame update; the swing contact
  `AdvanceSwingContact` / `MeleeContact`). **Not** `BuildActivityClipRequest`'s `Variant` line
  (~:1077): that one line is K4's; C2 writes it in its report and the integrator applies it.
- `ElysiumEntityWorldInteraction.cpp` (`AdvanceMeleeSwings` ~:299-326)
- `ElysiumNpcStartTask.cpp` (the attack arms; the swing start ~:557, ~:852-862)
- `ElysiumNpcThink.cpp` and `ElysiumNpcThink.inl` (`UpdateCharacterRetail` and the slot-312 seam
  ~`.cpp:124`, `.inl:82`; the tail call ~`.cpp:391` — these only)
- `ElysiumCombatCharacterSlots.cpp` (slot 315 `MeleeSwingUpdate` ~:490-495 only)
- `ElysiumAnimatingSlots.cpp` (slot 247 ~:228-232 only)
- `Source/ElysiumUE/Public/ElysiumBodyData.h`, `Source/ElysiumUE/Private/ElysiumBodyData.cpp`,
  `Source/ElysiumUE/Public/Visual/ElysiumNpcClips.h` (the two bbox fields)
- `Source/ElysiumUE/Private/Tests/ElysiumWeaponTests.cpp`, new
  `Source/ElysiumUE/Private/Tests/ElysiumNpcAttackExtentsTests.cpp`

Pipeline:

- `pipeline/src/elysium_pipeline/importers/body_data.py`, `pipeline/tests/test_body_data.py` (you
  may run this one test file), and `docs/contracts/seam_map_model.md` if it lists the body-data
  row's fields (Grep `body_data` in it; if it does not, name the contract that does in your
  report and do not edit it)

## The job

1. **`Weapon_FrameUpdate 0x1032aa40`** in `PostRun`, after slot 258, with the interval, as R2 item
   2 read it: the active weapon's slot 369 (`+0x5c4`), which every weapon class holds as
   `CBaseCombatWeapon 0x1024efa0`, called with the wielder — the weapon model's
   `StudioFrameAdvance(0)`; if `finished && loops`, `SelectWeightedSequence(m_Activity +0x8a8)` →
   `m_nSequence`, `ResetSequenceInfo`; then the weapon's slot 258 `(interval, wielder)`: the
   weapon model's events delivered to the **NPC's** `HandleAnimEvent`. **No fire, no sweep** in
   it. No active weapon → nothing. Where the port's weapon has no sequence words for its world
   model, that part is a seam answering "nothing", named for `0x1024efa0` and the weapon model's
   clock — say what exists. `MotorSeams.PostRunWeaponUpdates` stays, tallied beside the real call:
   three test files read it as `PostRun`'s witness and are not yours.
2. **The NPC shot only from its event — `ContactEventCycle` removed for `CWeaponRanged`** (J6).
   Retail: the event 3030..3044 → `CWeaponRanged 0x10238160` → `0x10238320` → `ModeDispatch(1)
   0x102383b0` → slot 373 `Shot 0x102387b0`; no retail path fires a Troika human's shot without
   the event (R2 item 3a). The guard has three parts and you write the first two:
   - **(a) keyed on the operator body, not on "is an NPC".** The estimate goes for the operator
     body R2 read (`CWeaponRanged`, `0x10238160`). An operator body R2 did **not** read — the
     species weapon body `0x103ed200`, the flamethrower, thrown weapons — **keeps today's
     estimate behind a named seam**: one function, e.g. `UsesUnreadOperatorEstimate()`, whose
     comment gives that body's address (or "address unread" with the class name) and the word
     "unread; V5 reads it". Nothing changes for those classes. List each class you left on the
     seam in your report — that is **the silent-class list's first half**.
   - **(b) the Warning**, once per (model, sequence): an NPC attack clip with no fire event in
     3030..3044 was started for a `CWeaponRanged` wielder (README §8 Q6). `UE_LOG` at Warning,
     naming the model and the sequence.
   - (c) is the integrator's (the list of NPC classes whose ranged attack activity has no
     3030..3044 clip, from the baked event tables).
   - **The player's use of the estimate is not touched** ("its commit stays on the estimate",
     `ElysiumWeaponClasses.cpp`): it goes with the player's weapon story.
   - The move-and-shoot overlay `0x102e8560` is a second retail producer (a layer's 3031); it
     stays a counter in V4 (J5) — write nothing for it.
3. **The NPC melee, where R2 item 3b places it.**
   - The start arm `0x102a45c6`: weapon `+0x5a0 & 0x18000` → `m_flLastAttackTime`, the weapon's
     slot 326 `PrimaryAttack`, then `AutoMovement`. `CWeaponMelee::PrimaryAttack 0x103eaca0`:
     owner in a grapple → return; no player owner → slot 372 `RequestActivity(0x4b, 1, 1)`
     (`0x103e9e00`) → the swing sequence on the owner through slot 311 (`+0x4dc`), the playback
     rate, `m_flNextAttack`. Check the port's swing start (`ElysiumNpcStartTask.cpp`) against
     these arms and fix a difference at its line.
   - **The contact has no anim event.** It is the NPC's **own slot 312**: `UpdateCharacter
     0x103246d0` → `MeleeSwingUpdate 0x10346cd0`, reached from the Troika think's tail
     `0x1029365b` — after `PostRun` and `PerformMovement`, **on the update clock, not every
     think**. So: an NPC's swing is swept from `UpdateCharacterRetail` → slot 315
     `MeleeSwingUpdate` (replace its counting stub with the sweep, calling the existing
     `AdvanceSwingContact` / `MeleeContact`), and `AdvanceMeleeSwings` (the world interaction
     tick) **stops sweeping NPC wielders** — it keeps the player's swings (the player's slot 312
     site is the player story's; A4 named it). The interval the sweep integrates is the update
     clock's. *`MeleeSwingUpdate`'s body is cited from the doc (D), not re-read by R2: read
     `research section 0x10346cd0` first; what it does beyond the sweep the port already has is
     "unrecovered; the coder reads it first".*
4. **Slot 247 `SetAttackExtentsForSequence 0x10090c80`, whole** (J2), called by
   `ResetSequenceInfo`: only when `Flags2 & 4` — `CAI_BaseNPCTroika::Spawn 0x10298d30` calls
   `AddFlag2(4)`, so it is live on every Troika NPC; `RemoveFlag2(4)`: `CPayphone`,
   `CNPC_VHengeyokai`, `CNPC_VMingXiaoTentacle`, `CNPC_VTzimisce`, `…HeadClaw`, `…Runner` (Spawn
   and vfunc130), `CNPC_VWerewolf` (check the port's flag writes by Grep and report a missing
   one). It reads the sequence descriptor's bbox (`+0x1c..+0x30`), takes the radial excess over
   the collision's maxs, and hands it to **the entity's slot 15** (`+0x3c`),
   `CBaseEntity::SetAttackExtents 0x1009af40` (collision `0x100dc220` + `+0x50`,
   `m_vecAttackExtents`) — not the motor's slot 15. *The body is read only as far as "the radial
   excess over the collision's maxs" (README §1): read `0x10090c80` whole in the listing first
   and port it arm for arm.* The bbox comes from `FElysiumNpc::SequenceBounds(int32, FVector&,
   FVector&)` (README § "Shared names" — **its body in `ElysiumNpcAnim.cpp` is C2's**; you call
   it). It answers false for "no descriptor": the slot then writes nothing, as retail with no
   seqdesc. Remove the counting stub. Do not touch the sleep arms' save/restore of `+0x50`
   (slot 16, `0x102a29fa`, `0x102a710e` → `+0x65d0`): they exist; they now save a real value.
5. **The bbox on the body row — the pipeline half** (J2). `body_data.py`: each row gains
   `bboxMinCm` / `bboxMaxCm`, from `records.Seq`'s `bbmin` / `bbmax`
   (`skeletal_stage/unit.py:~234`, already in the `unit.sequences` that `project_body` walks),
   converted by the `UE_` coordinate contract: centimetres, the Y reflection (after the
   reflection min and max are re-ordered per axis — state it at the line). **Do not touch
   `clip_data.py`**: its `descriptor` drops the bbox, and adding it there would change
   `clipDataSha256` and re-cook every animation package. Runtime: `FElysiumBodySequence` and
   `FElysiumNpcClip` carry `BboxMinCm` / `BboxMaxCm`, loaded in `ElysiumBodyData.cpp` beside
   `RawIndex`; a row without them (an asset not re-imported yet) loads as "no bbox", which
   `SequenceBounds` must answer as false. Update `test_body_data.py` and the contract. You do
   not run the import: the C integrator does, once, with its stop rule.
6. **Tests.** `ElysiumWeaponTests.cpp`: an NPC `CWeaponRanged` clip with a 3031 row commits once
   at the event; one without commits nothing and logs once; **delete the estimate assertions on
   an NPC wielder** (they pin a port mechanism; list them); an NPC swing is swept from slot 315
   on the update clock and not by `AdvanceMeleeSwings`; `PostRun`'s order with
   `Weapon_FrameUpdate` last, if A1's `.PostRunOrder` does not already cover it.
   `ElysiumNpcAttackExtentsTests.cpp`: `Elysium.Arm.NpcKernelAnim.AttackExtents` (`0x10090c80`):
   `Flags2 & 4` clear → nothing; a fixture bbox → the extents retail computes; no descriptor →
   nothing written.

## Not yours

The dispatcher (V4a), the attack conditions and `TASK_WAIT_ATTACK_TIME1` (V5, N2), the coordinator
(V11, N3), the weighted pick, `SequenceBounds`'s body and death (C2), the import run (the
integrator), the species weapon bodies' reading (V5), the player's estimate, records (`melee_swing`
and `ranged_open_fire` were corrected by the seam).

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite (the one Python test file
excepted); only your files; cross-lane lines in the report. The query budget (10 s warns, 60 s
stops; never a file over ~200 KB whole). Text through Grep / Read / Glob. Do not commit. Report
≤300 words: what you ported (addresses), the classes left on the unread seam, the row's new
fields and their conversion, tests added and deleted, what stayed unrecovered.
