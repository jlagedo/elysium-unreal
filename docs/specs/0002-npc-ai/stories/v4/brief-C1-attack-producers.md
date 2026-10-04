# Brief C1 — V4c: `Weapon_FrameUpdate`, the NPC attack producers, slot 247's body (coder; no build)

**Final (amended after V4r and settling packet S2, 2026-10-04 — R2, J2, J6, S2 items 1, 7, 8, 9).**
Where this brief and `packets-R2.md` or the README disagree, this brief wins. **J2b is ruled
(the judge's second sitting, `stories/v1/triage.md` § "Judge's rulings, V4 — second sitting"):
J2's import is withdrawn and filed with the story that ports the player's acquire cone. Item 5
is closed — no pipeline, no runtime fields, no import. The slot-247 body (item 4) stays yours,
on the named `SequenceBounds` seam, proven by the arm test only.** Read `README.md` here (§1 "The frame",
"The events" with the attack producers' amendment, "The attack extents"; §2 M3, M10, M11; §8 Q4,
Q6, Q7; § "Shared names"), `packets-R2.md` items 2, 3 and 5, `packets-S2.md` items 1, 7 and 9, the
rulings J2 and J6
(`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it),
`docs/vtmb/combat-and-damage.md` § "The NPC attack producers, start to commit" and § "Weapon
operator bodies, the shot's gates and the attack data (S2)". After V4b's commit
(or V4a's, if V4c runs first). Re-locate by Grep.

## Files (only these)

Runtime (`Source/ElysiumUE/Private/Substrate/` unless a path says otherwise):

- `ElysiumNpcBaseMotor.cpp` (`PostRun`'s weapon line, `++PostRunWeaponUpdates` ~:597, only)
- `ElysiumWeaponClasses.{h,cpp}` (the `ContactEventCycle` estimate ~`.h:303`, `.cpp:1263-1317,
  2166`; `OperatorHandleAnimEvent` ~:1349; `IsMeleeSwingTrigger` ~`.cpp:468`; the weapon's frame
  update; the swing contact `AdvanceSwingContact` / `MeleeContact`). **Not** `BuildActivityClipRequest`'s `Variant` line
  (~:1077): that one line is K4's; C2 writes it in its report and the integrator applies it.
- `ElysiumEntityWorldInteraction.cpp` (`AdvanceMeleeSwings` ~:299-326)
- `ElysiumNpcStartTask.cpp` (the attack arms; the swing start ~:557, ~:852-862)
- `ElysiumNpcThink.cpp` and `ElysiumNpcThink.inl` (`UpdateCharacterRetail` and the slot-312 seam
  ~`.cpp:124`, `.inl:82`; the tail call ~`.cpp:391` — these only)
- `ElysiumCombatCharacterSlots.cpp` (slot 315 `MeleeSwingUpdate` ~:490-495 only)
- `ElysiumAnimatingSlots.cpp` (slot 247 ~:228-232 only)
- `Source/ElysiumUE/Private/Tests/ElysiumWeaponTests.cpp`, new
  `Source/ElysiumUE/Private/Tests/ElysiumNpcAttackExtentsTests.cpp`

**Not yours any more (J2b: withdrawn and filed)**: `Public/ElysiumBodyData.h`,
`Private/ElysiumBodyData.cpp`, `Public/Visual/ElysiumNpcClips.h`,
`pipeline/src/elysium_pipeline/importers/body_data.py`, `pipeline/tests/test_body_data.py`,
`docs/contracts/seam_map_model.md`. You edit no pipeline file and run no Python test.

Wave check (C1 against C2, by function): the two lanes share no file. `SequenceBounds`'s body is
in C2's `ElysiumNpcAnim.cpp`; `CreateCorpse` and the pedestrian's override are C2's
(`ElysiumCombatCharacter.cpp`, `ElysiumNpcPedestrian.{h,cpp}`); K4's line in your
`ElysiumWeaponClasses.cpp` is written by C2 in its report and applied by the integrator.

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
2. **The NPC shot only from its event — `ContactEventCycle` removed for every NPC wielder** (J6,
   as `packets-S2.md` item 1 settled it). Retail: the event 3030..3044 → `CWeaponRanged
   0x10238160` → `0x10238320` → `ModeDispatch(1) 0x102383b0` → slot 373 `Shot 0x102387b0`; no
   retail path fires a Troika human's shot without the event (R2 item 3a). Slot 370 (the
   operator) has **eight bodies in the whole image and all are read**:
   - **ranged** `0x10238160` — `CWeaponRanged` and all 16 subclasses, **the species ones
     included** (`…_FlameThrower`, `…_MingXiao_Spit`, `…_Tzimisce2Head`, both crossbows): fires
     from the event and only the event;
   - **base** `0x1024f030` — `CBaseCombatWeapon`, `CDisciplineWeapon`, `CWeaponUnarmed`, the
     thrown classes, …: takes no event (`DevWarning("Unhandled animation event…")`) and has no
     weapon-side timer; the species' own task bodies are the retail path
     (`CNPC_VChangBros::RunTask 0x1036bfc0` → `SpawnEnergyBall 0x1036dd20`,
     `CNPC_VFrenzyShadow::StartTask 0x10375f50`, `CNPC_VBach::StartTask 0x103645a0`,
     `CNPC_VManBat::RunTask 0x1038d130`, the Chang ghost `0x103f03e0`) — already in their class
     files, not estimates and not yours;
   - **item** `0x103f4470` (swallows 3014 and 3200, else the base);
   - **melee** `0x103ea5b0` (`CWeaponMelee` and 27 subclasses; item 3), with `0x103ec460` /
     `0x103eca20` (Ming Xiao's two, the same behaviour) and `0x103e8be0` (the Tzimisce melee).
   `0x103ed200` is **not** a species body: it is the player's grenade release (`CWeaponThrown`;
   it returns for an NPC). **So there is no "unread operator body" seam:
   `UsesUnreadOperatorEstimate()` is not written, no class keeps the estimate for an NPC
   wielder, and the report's "silent-class list, first half" is empty.** The guard has three
   parts and you write the first two:
   - **(a) keyed on the operator body, not on "is an NPC"** (J6): one ranged body, the event;
     the base body, nothing weapon-side; melee, the sweep (item 3). The estimate's code stays
     only for the player.
   - **(b) the Warning**, once per (model, sequence): an NPC attack clip with no fire event in
     3030..3044 was started for a `CWeaponRanged` wielder (README §8 Q6). `UE_LOG` at Warning,
     naming the model and the sequence.
   - Not walked, named at the line where you meet them: each species class's own
     `HandleAnimEvent` arm by arm (ported, `ElysiumNpcMisc2Species.cpp`; not your file), and
     `0x10239f30` (the type-6 throw's launch; the decompilation is truncated) — **unrecovered; a
     reader settles it before any lane ports the type-6 arm; you do not**.
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
   - **The melee operator's trigger set** (`0x103ea5b0`, `packets-S2.md` item 1): events **3001,
     3030..3037, 3039..3044, 3047** → **if the operator is not a player**, the weapon's slot 326
     `PrimaryAttack`; 3003 swallowed; 4001 / 4002 the bodygroup; 3038, 3045, 3046 → the base.
     Today the port claims only 3047, with no consumer: `IsMeleeSwingTrigger` takes the retail
     set and `OperatorHandleAnimEvent` calls the swing start for a non-player operator.
     `CWeaponMelee_TzimisceMelee`'s 3045 / 3046 (`0x103e8be0` → `0x103e8c50` / `0x103e8c90`, NPC
     operator only: weapon `+0x910 = 1` / `2`, slot 372 `RequestActivity(0x4b, 1, 0)`, `+0x910 =
     0`) is **a named seam answering nothing** unless the port's Tzimisce weapon class already
     exists in your files — say which.
   - **The contact has no anim event.** It is the NPC's **own slot 312**: `UpdateCharacter
     0x103246d0` → `MeleeSwingUpdate 0x10346cd0`, reached from the Troika think's tail
     `0x1029365b` — after `PostRun` and `PerformMovement`, on thinks where the update clock is
     due. `UpdateCharacter(interval)` calls **slot 315 with no argument**. So: an NPC's swing is
     swept from `UpdateCharacterRetail` → slot 315 `MeleeSwingUpdate` (replace its counting stub
     with the sweep, calling the existing `AdvanceSwingContact` / `MeleeContact`), and
     `AdvanceMeleeSwings` (the world interaction tick) **stops sweeping NPC wielders** — it keeps
     the player's swings (the player's slot 312 site is the player story's; A4 named it).
   - **`MeleeSwingUpdate 0x10346cd0`, as read** (`packets-S2.md` item 9) — port it arm for arm:
     - **Window**: no seqdesc or `seqdesc+0x2c4 < 1` (the baked `swings` count) →
       `m_bMeleeSwingIsLive (+0xaa1) = 0`, return, **nothing stamped**. Not yet live →
       `MeleeSwingStep(pos, angles, 0, 0)`, `ForceMeleeReset`, stamp.
     - Live: **`dt = curtime − m_flLastMeleeSwingUpdate (+0xaa4)` — the sweep's own stamp, not
       the update clock's interval and not the caller's**; `dt <= 0` → return unstamped. So the
       sweep covers the whole gap between due thinks. `rate = GetSequenceCycleRate ×
       m_flPlaybackRate`; `c1 = (curtime − m_flAnimTime + 0.1) × rate + m_flCycle`; `c0 = c1 −
       rate × dt`. NPC: `m_flCycle >= melee_swing_completion_percent` → NPC `+0x6064 = 0`. Once:
       `SendIncomingSwingNotice` (`0x10346ac0`); once: when `IsMeleeSwingActive(offset cvar)`,
       weapon slot 333 `(0x15, 1, …)`.
     - **`N = ceil(dt × 100)`** (`_ceil`, `[0x10450564]`; not `floor`), for `i = 1..N` while the
       previous cycle `< 1`: `c = lerp(c0, c1, i/N)` clamped to 1; position and angles lerped
       from the stored last pose; `MeleeSwingStep(pos, ang, prev, c)`. The epilogue stamps time,
       position, angles.
     - The trace shape, the hit decision and the damage are `MeleeSwingStep 0x10343020` → the
       weapon's traced-impact virtual `+0x438` `0x102579f0` — cited from the doc, **not re-read:
       unrecovered as a listing walk; a reader settles it before this lane starts** if the
       port's `AdvanceSwingContact` / `MeleeContact` is to be changed. You keep the port's
       contact as it is and change only who calls it, when, and over which cycles.
     Where the port has no word for `+0xaa1`, `+0xaa4` or the stored last pose, add them in
     `ElysiumWeaponClasses.h` or report the exact declaration if their home is a file not yours.
4. **Slot 247 `SetAttackExtentsForSequence 0x10090c80`, whole** (J2), called by
   `ResetSequenceInfo`: only when `Flags2 & 4` — `CAI_BaseNPCTroika::Spawn 0x10298d30` calls
   `AddFlag2(4)`, so it is live on every Troika NPC; `RemoveFlag2(4)`: `CPayphone`,
   `CNPC_VHengeyokai`, `CNPC_VMingXiaoTentacle`, `CNPC_VTzimisce`, `…HeadClaw`, `…Runner` (Spawn
   and vfunc130), `CNPC_VWerewolf` (check the port's flag writes by Grep and report a missing
   one). **The body, whole** (`packets-S2.md` item 7): `Flags2 & 4` and a seqdesc, else nothing;
   `e[i] = max(|bbmin[i]|, bbmax[i])` over the descriptor's bbox (`+0x1c..+0x30`); `e.x = e.y =
   sqrt(e.x² + e.y²)`; `e[i] = e[i] > collision maxs[i] ? e[i] − maxs[i] : 0`; then **the
   entity's slot 15** (`+0x3c`), `CBaseEntity::SetAttackExtents 0x1009af40` — `0x100dc220` (the
   engine's `SpatialPartition001` slot 0 `0x20040fc0`, which stores the vector in the partition
   element at `+0x28`) and `m_vecAttackExtents +0x50` — not the motor's slot 15. The bbox comes
   from `FElysiumNpc::SequenceBounds(int32, FVector&, FVector&)` (README § "Shared names" —
   **its body in `ElysiumNpcAnim.cpp` is C2's**; you call it). It answers false for "no
   descriptor": the slot then writes nothing, as retail with no seqdesc — **and it answers false
   for every sequence: `SequenceBounds` is a named seam (J2b), "stands for the seqdesc bbox
   `+0x1c..+0x30`; filled when the acquire cone is ported"**, so the slot is live and writes
   nothing; say so at the line. This is the seam for an input with no source, not a divergence.
   Remove the counting stub. Do not touch the sleep arms' save/restore of `+0x50`
   (slot 16 `0x1009b030`: `0x102a1910` twice, and the cover-hint acquire `0x102b7110` → `+0x65d0`
   then `SetAbsoluteAttackExtents`): they exist.
   **What the extents are for** (so nobody waits on them): they are only saved and restored in
   game code; the engine copy widens an element's box only in a query carrying Troika's flag,
   and the only flagged queries are the player's acquire cone (`0x1040f550`, `0x1040f080`). Not
   melee reach, not the sweep, not any NPC condition. The reader list is closed: `0x102fb4e0`
   (`CAI_Node::InitLinks`) passes 0 for the flag at all three of its `Ray.Init` sites
   (`packets-S4.md` item c). The acquire cone is not ported, so the port has no observer of the
   extents: **no arena record and no live read proves this item — the arm test does, and that is
   stated, not hidden.**
5. **CLOSED BY J2b — NOT YOURS, NOT IN V4.** J2 ("implement now with a body-data import") is
   withdrawn: with no observer in the port, the data half cannot close on a real test. Filed
   with the story that ports `0x1040f550` / `0x1040f080` (the player's acquire cone; `spec.md`
   carries the line): `bboxMinCm` / `bboxMaxCm` on the body row, the runtime fields, the one
   import, and the port's feed-target box growing by the extents (`ElysiumMapActor.cpp:~1513-1518`
   — not changed now). You write none of it. The text below is kept only as the filed item's
   description. **The bbox on the body row — the pipeline half** (J2, withdrawn). `body_data.py`: each row gains
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
   an NPC wielder of every class** (they pin a port mechanism; list them); an NPC wielder of a
   base-body weapon (unarmed, thrown) commits nothing weapon-side; the melee operator's trigger
   set (3001, 3030..3037, 3039..3044, 3047 start the swing for a non-player; 3003, 3038, 3045,
   3046 do not); an NPC swing is swept from slot 315 and not by `AdvanceMeleeSwings`, over **its
   own stamp's `dt`** with **`N = ceil(dt × 100)`** steps (a 0.25 s gap → 25 steps; 0.101 s →
   11), `dt <= 0` → no step and no stamp, a sequence with no swing window → not live and no
   stamp; `PostRun`'s order with `Weapon_FrameUpdate` last, if A1's `.PostRunOrder` does not
   already cover it.
   `ElysiumNpcAttackExtentsTests.cpp`: `Elysium.Arm.NpcKernelAnim.AttackExtents` (`0x10090c80`):
   `Flags2 & 4` clear → nothing; a fixture bbox (through a test double of `SequenceBounds`, since
   the real one is the J2b seam answering false) → `max(|min|, max)` per axis, the radial x / y, the
   excess over the collision's maxs, zero where it does not exceed; no descriptor → nothing
   written.

## Not yours

The dispatcher (V4a), the attack conditions and `TASK_WAIT_ATTACK_TIME1` (V5, N2), the coordinator
(V11, N3), the weighted pick, `SequenceBounds`'s body, death, `CreateCorpse` and the pedestrian's
override, the fade (C2), the bbox import (withdrawn by J2b, filed with the acquire cone's story),
the species classes' own task and event bodies (`ElysiumNpcMisc2Species.cpp`; the four species
with task-code fire paths — ChangBros, FrenzyShadow, Bach, ManBat — are absent from both witness
maps and filed on `spec.md`'s "on demand" line, J11: your Warning line is their tripwire),
`Shot`'s gates and the NPC's clip (`0x102387b0`: V4o lane O3, J12 — if O3 has landed, the commit
no longer spends or refuses on an NPC's magazine; do not put it back), the player's
estimate, records (`melee_swing`, `chase_melee` and `ranged_open_fire` were corrected by the seam).

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite (the one Python test file
excepted); only your files; cross-lane lines in the report. The query budget (10 s warns, 60 s
stops; never a file over ~200 KB whole). Text through Grep / Read / Glob. Do not commit. Report
≤300 words: what you ported (addresses), that no class keeps the estimate for an NPC wielder
(or the class that does and why — a stop, not a seam), the Tzimisce 3045 / 3046 seam, slot 247
on the `SequenceBounds` seam (J2b), tests added and deleted, what stayed unrecovered.
