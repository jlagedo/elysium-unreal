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

**Final for the code as landed (2026-10-04, after V5a, V4a, V4b, V11 and — before you start —
V4o; `packets-S11.md` item 2, `packets-S12.md` items c and d.1).** Where an item below and this
block disagree, this block wins. Every `~:line` below predates V11-2's rewrite of
`ElysiumWeaponClasses.cpp` (now ~3,800 lines): re-locate by name.

**What is already landed in your files — do not redo it** (read-only check of the tree, S12):

- **V11-2 took the contact.** In `ElysiumWeaponClasses.cpp`: the walk of `AdvanceSwingContact`
  (:2609) is `MeleeSwingStep 0x10343020` per sub-step — the relation filter and
  `debug_allow_melee_ff`, the per-sample rays, the non-character overlap hit (D5), the window test
  (`ElysiumSwing::StepWindowOpen`, `ElysiumSwingContact.{h,cpp}`), `MeleeContact` (:3082),
  `SwingWallContact` (:3340), `KnockbackContact` (:3421), `StageSwingOpposedRoll` (:2519).
  **V11-3 took slot 331** (`ElysiumMeleeSequenceChoice.{h,cpp}`, the `NpcSchedule` stream).
  **V5a took** `RangeAttack1Conditions` (:900), the melee band and the wait's reader.
- **D9's function exists with your operand**: `ElysiumSwingEndpointsAt(const
  FElysiumSwingEndpointQuery&, FVector& OutA, FVector& OutB)` (:2463), the query carrying
  `PrevFrame`, `NowFrame`, `FromLocal`, `ToLocal`, `Alpha` and **`Cycle` (the sub-step's cycle,
  unread today)**. You replace **that body** and nothing of the walk.
- **O3 (V4o) took the event shot**: `FElysiumWeapon::ShotFromAnimEvent` is `Shot 0x102387b0` for
  an NPC with nothing staged — the gates, the count, the stamp `+0x730` (S11 item 2.3), no clip
  spend (J12) — and `CommitFromAnimEvent` (:1358) routes `!Swing.bActive` to it. O3's report
  says what you can now delete; item 2 below is that deletion.
- **What is still the old shape, and yours**: `AdvanceSwingContact(float DeltaSeconds)`'s entry
  still takes the caller's delta and its liveness from the pose layer's clip phase
  (`GetLiveClipPhase`), and is still driven by the world tick
  (`FElysiumEntityWorld::AdvanceMeleeSwings`, `ElysiumEntityWorldInteraction.cpp` :299-326, called
  from `Map/ElysiumMapActor.cpp` :2630) for **every** wielder, the player included; slot 315
  (`ElysiumCombatCharacterSlots.cpp` :490-495) and slot 247 (`ElysiumAnimatingSlots.cpp`
  :222-227) are counting stubs; `PostRun`'s weapon line is a counter (`ElysiumNpcBaseMotor.cpp`
  :597); the base `UpdateCharacter 0x103246d0` is the counting seam
  `FElysiumNpc::Think19CombatCharacterUpdateCharacter` (`ElysiumNpcThink.cpp` :122-127), reached
  from `UpdateCharacterRetail` (:132) at the think's tail (:396); `IsMeleeSwingTrigger` (:468)
  claims 3047 only and `OperatorHandleAnimEvent` (:1384) gives it no consumer (:1408-1424); the
  estimate is `ContactEventCycle` (`.h` :303; the commit time :2201; `CommitArrivesFromAnimEvent`
  :1238 with its NPC branch :1269-1292).

**The sweep's place, for the NPC and the player** (S12 d.1 item 2, the listing read whole):

- `CBaseCombatCharacter::UpdateCharacter 0x103246d0` is slot 312 on `CAI_BaseNPC` **and on
  `CBasePlayer`** (no player override); it calls **slot 315 `MeleeSwingUpdate` with no
  argument** (`+0x4ec`) after slots 313 / 314 and the expression and eye arms. NPC: from the
  Troika think's tail `0x1029365b`. **Player: from `PostThink 0x1016be10` at `0x1016c316`,
  after slot 258, behind the four gates.**
- You: inside `Think19CombatCharacterUpdateCharacter` call slot 315 `MeleeSwingUpdate()` at its
  place and **name what stays unported in that body at the line** (`UpdateDisciplineVisuals`,
  slot 313, `UpdateVampHeal_HOT`, `UpdateExpressions`, slot 333 / `MaintainScriptedEyeDirection`,
  slot 314, the render-fx expiries `0x1a` / `0x25` — owners 0006 / 0015; the counter stays for
  them). Slot 315's body is the sweep for **whoever the character is**: it reads no "is NPC"
  except the two NPC-only arms of the listing.
- **`AdvanceMeleeSwings` sweeps nobody**: delete its body's walk. The player's call is **one line
  in `FElysiumPlayer::PostThinkAnimation`'s tail — C2's file and C2's line** (C2 owns that
  function for the alive gate; it calls your slot 315). Write in your report, exact, for the
  integrator: delete the call at `Map/ElysiumMapActor.cpp` :2630 and the declaration at
  `Public/ElysiumEntityWorld.h` :432 (and the comment in `Visual/ElysiumMeleeTrail.h` :19); you
  delete the definition. Until C2's line is in, the player's swing is swept by nothing: the
  integrator checks both landed (`anim_player_weapon_event_melee`).
- **Three species sites already call slot 315** (`ElysiumNpcRunTaskSpecies.cpp` :2117, :2128,
  :2152, retail `0x103ce43c` / `0x103ce538`; not your file): with the stub gone they become real
  sweeps, as in retail. Nothing to edit; say so in your report.
- **The stamp**: the sweep's `dt` is `curtime − m_flLastMeleeSwingUpdate (+0xaa4)`, its own word
  (`ElysiumNpcKernelShape.cpp` :451 already names the offset), not `DeltaSeconds`; `N = ceil(dt ×
  100)` — **`0x10450564` is the float 100.0, bytes read** (S12 d.1 item 1). The stored last pose
  the walk needs is today `Swing.PrevOrigin` / `PrevAngles` / `PrevSegmentsLocal` on the weapon.

**The two slot files are generated — the bodies go beside them** (the convention V4a and V4b
landed). `ElysiumCombatCharacterSlots.cpp` and `ElysiumAnimatingSlots.cpp` carry "Generated by
`gen_kernel_shape`. Do not hand-edit": you do **not** edit them, whatever the Files list below
says. Write slot 315 `FElysiumCombatCharacter::MeleeSwingUpdate()` in
**`ElysiumCombatCharacterSlotBodies.cpp`** (where slot 389's hand body is) and slot 247
`FElysiumAnimating::SetAttackExtentsForSequence(int32)` in **`ElysiumAnimatingSlotBodies.cpp`**
(where slots 242 / 248 are); these two files replace the two generated ones in your list. In your
report, for the integrator: the two `kernel_verdicts.tsv` rows `10346cd0` and `10090c80` become
`rule … hand:FElysiumCombatCharacter::MeleeSwingUpdate` / `hand:FElysiumAnimating::
SetAttackExtentsForSequence` (the shape of row `103338c0`), then `gen_kernel_shape` before the
build — until then your bodies and the generated stubs are duplicate definitions.

**Settled, so not "owed"** (S12 item c): the species classes' own `HandleAnimEvent` bodies are
walked and ported (`ElysiumNpcMisc2Species.cpp` :192-496, `ElysiumNpcTzimisceRunner.cpp` :243);
the sentence in item 2 that calls them "not walked" is void — they are simply not your file.
`Weapon_FrameUpdate 0x1032aa40` and slot 369 `0x1024efa0` are re-read whole (S12 d.1 item 3) and
are exactly item 1.

## Files (only these)

Runtime (`Source/ElysiumUE/Private/Substrate/` unless a path says otherwise):

- `ElysiumNpcBaseMotor.cpp` (`PostRun`'s weapon line, `++PostRunWeaponUpdates` ~:597, only)
- `ElysiumWeaponClasses.{h,cpp}` (the `ContactEventCycle` estimate ~`.h:303`, `.cpp:1263-1317,
  2166`; `OperatorHandleAnimEvent` ~:1349; `IsMeleeSwingTrigger` ~`.cpp:468`; the weapon's frame
  update; of the swing contact only `AdvanceSwingContact`'s entry, its sub-step loop and the
  endpoint function (D9) — the walk's filters, the hit test, `MeleeContact` and
  `KnockbackContact` were made retail by V11-2 and are not reopened). **Not** `BuildActivityClipRequest`'s `Variant` line
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

Wave check, re-done against the tree after V11 (S12): C1 and C2 share no file. **You do not
touch `ElysiumPlayerEntity.cpp`** (C2's, for `PostThinkAnimation`), `ElysiumSwingContact.{h,cpp}`,
`ElysiumMeleeSequenceChoice.{h,cpp}`, `ElysiumNpcRunTaskSpecies.cpp`, `Map/ElysiumMapActor.cpp` or
`Public/ElysiumEntityWorld.h` (integrator's lines).
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
   as `packets-S2.md` item 1 settled it). **O3 owns the event shot and it is landed**
   (`ShotFromAnimEvent`): what is left for you is the removal — an NPC wielder's standing shot
   (`AttackIntent` → `BeginRangedShot`) stages nothing, plays nothing on the `UpperBody` channel
   and queues no estimate; `CommitArrivesFromAnimEvent`'s NPC branch and the estimate's commit
   time are the player's only; the Warning (b) replaces the Verbose `kernelseq:` line. In
   `ElysiumWeaponTests.cpp` (`Weapons.AnimEvent`, the cast-gunman block) the `PlayNpcClip … ch=upper
   body` assertions for an NPC wielder go with it (O3 already deleted the inverted "keeps the
   estimate" line). `ranged_open_fire`, `ranged_sustained_fire` and `cover` must not move: their
   shots are the kernel sequence's 3031 through O3's entry. Retail: the event 3030..3044 → `CWeaponRanged
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
     `0x10239f30` (the type-6 throw's "launch") — **settled (`packets-S5.md` item 5): 40 bytes,
     two virtual reads whose results are discarded; it launches nothing, and `0x10239e70` only
     plays weapon activity `0xb9`, spends one of the clip and drops the emptied weapon. You do
     not port the type-6 arm**.
   - (c) is the integrator's (the list of NPC classes whose ranged attack activity has no
     3030..3044 clip, from the baked event tables).
   - **The player's use of the estimate is not touched** ("its commit stays on the estimate",
     `ElysiumWeaponClasses.cpp`): it goes with the player's weapon story.
   - The move-and-shoot overlay `0x102e8560` is the second retail producer (a layer's 3031):
     **landed by V4o** (O1 the layers, O2 the overlay, O3 the shot) — write nothing for it, and
     break nothing of it: `cover_move_shoot` stays green.
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
     `AdvanceMeleeSwings` (the world interaction tick) **stops sweeping every wielder** — the
     player's sweep is the same slot 315, called from `PostThinkAnimation`'s tail by C2's line
     (the block at the top; an earlier text kept the player on the world tick).
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
       weapon's traced-impact virtual `+0x438` `0x102579f0` — **walked (`packets-S5.md` item 3)
       and already retail when you start: lane V11-2 (`../v11/brief-V11-2-melee-contact.md`,
       V11's wave, before V4c) fixed D1–D8, D10 and D11** — the relation filter, the gates, the
       per-sample rays, the non-character overlap hit, the wall contact, slot 270's dice,
       dispatch, side effects and reaction. Do not rewrite them; a line of theirs your change
       must move is reported. **What remains yours of the contact**: the sweep's place in the
       think (slot 312 → slot 315, above), the stamp (`+0xaa1`, `+0xaa4`, the stored last
       pose), the cycles each step is given, `Weapon_FrameUpdate` (item 1) — and:
     - **D9 — the record's endpoints are posed at each sub-step's own cycle**
       (`MeleeSwingStep 0x10343020` step 1: `CalcPose` at `cycle`, the bone's matrix built from
       the sub-step's lerped `pos` / `ang`, `0x100c3600`). Today the segment is the live
       bone's, lerped linearly between two batches in the attacker's frame
       (`ElysiumWeaponClasses.cpp` ~:2786-2795). V11-2 left the endpoints behind **one
       function taking the sub-step's cycle**: replace its body so each of your `N` steps asks
       the bone's transform at `c` under the lerped `pos` / `ang`. Grep the embodiment for a
       bone transform at a given sequence cycle (`Public/ElysiumWorldServices.h`, read not
       edited); if none exists, keep the lerp behind that function as a **named seam** ("stands
       for `CalcPose` at `cycle`, `0x10343020`") and write the accessor's exact signature in
       your report — a pose evaluator is then a judge item, not yours to build.
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
   stamp; **D9**: each sub-step's endpoints are asked at that step's cycle (`0x10343020`), or
   the seam's name is asserted if the accessor does not exist; `PostRun`'s order with
   `Weapon_FrameUpdate` last, if A1's `.PostRunOrder` does not already cover it. V11-2's
   `Elysium.Arm.MeleeSwingStep.*` / `Elysium.Arm.MeleeContact.*` (`ElysiumMeleeSwingStepTests.cpp`,
   not your file) must still hold: a line of theirs your change breaks is reported, exact.
   `ElysiumNpcAttackExtentsTests.cpp`: `Elysium.Arm.NpcKernelAnim.AttackExtents` (`0x10090c80`):
   `Flags2 & 4` clear → nothing; a fixture bbox (through a test double of `SequenceBounds`, since
   the real one is the J2b seam answering false) → `max(|min|, max)` per axis, the radial x / y, the
   excess over the collision's maxs, zero where it does not exceed; no descriptor → nothing
   written.

## Not yours

The dispatcher (V4a), the attack conditions and `TASK_WAIT_ATTACK_TIME1` (V5, N2), the coordinator
(V11, N3), the contact's D1–D8, D10, D11 (V11-2, landed) and slot 331 (V11-3, landed), the swing
query (`Public/ElysiumWorldServices.h`, `Map/ElysiumMapActor.cpp`, `ElysiumSwingContact.*`), the weighted pick, `SequenceBounds`'s body, death, `CreateCorpse` and the pedestrian's
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
