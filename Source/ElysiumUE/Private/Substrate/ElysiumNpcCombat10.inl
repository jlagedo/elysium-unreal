// Story 29d, family **Combat10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. What lands here is
// the non-slot half — the base-class bodies beneath a Troika override, the species arms the slot
// dispatchers run, the retail helpers those bodies call, and the seams that stand for retail inputs
// this substrate has no source for.
//
// The definitions are in `Substrate/ElysiumNpcCombat10.cpp` (the loadout, the health-percent
// readers, the weapon drops, the discipline write, the ideal-state pre-select, the knockback and the
// yaw sweep) and `Substrate/ElysiumNpcCombat10_2.cpp` (slot 605 and everything it calls); the
// tests are `Tests/ElysiumNpcKernelCombat10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Story 29d, family Combat10 — …" and
// `docs/vtmb/combat-and-damage.md` § "Story 29d, family Combat10 — …".
//
// --- What this family is ------------------------------------------------------------------------
//
// **How a fighting body picks its ranged program, what it fights with, and how hurt it is.**
// Twenty-one rows: slot 605 `SelectScheduleRangedCombat` with its five species arms (Troika, human,
// Asian vampire, Bach, Ming Xiao, Sheriff man), the fighting-item pair (slots 304/305) with the
// Werewolf's, `HealthToPercent` (slot 348) with Ming Xiao's and the damage-fraction twin
// `GetCurrHealthPercent`, the two `Weapon_Drop` bodies (slots 385/386), slot 589
// `SetScriptedDiscipline`, slot 460 `PreSelectIdealState`, slot 357's prayer pulse, the knockback
// velocity builder and the yaw clearance sweep.
//
// FOUR STANDING FACTS OF THIS FAMILY, stated once here rather than at thirty call sites.
//
//   * **Stat `0xf` is the accumulated WOUND counter and stat `0x11` is the cap.**
//     `CBaseCombatCharacter::HealthToPercent` (`0x1032fe60`, which carries retail's own scope-trace
//     string) returns `((stat0x11 - stat0xf) * m_iMaxHealth) / stat0x11`. This runtime's sheet IS
//     that list: `ElysiumSlot::Health` is **15** (`0xf`, "damage TAKEN, not hit points remaining")
//     and `ElysiumSlot::MaxHealth` is **17** (`0x11`), which is why `FElysiumCombatCharacter::
//     SyncHealthFromSheet` already cites this function. So slot 348 needs no seam — it reads the
//     real sheet — and `CNPC_VVampireBoss::GetCurrHealthPercent` (`0x103c6830`) is its COMPLEMENT,
//     the damage fraction `stat0xf / stat0x11`.
//   * **Retail's `CVStatList_t` lookup is a LINEAR SCAN FOR A LIST TYPE, and this runtime stands
//     only the type-0 list.** Every body of this family that touches a stat opens with the same
//     eleven-instruction walk: `+0x13bc` count, `+0x13c0` table, first entry whose record `+0x10`
//     equals the wanted TYPE, else the lazily built global `DAT_109f0b40` (guard bit 0 of
//     `DAT_109f0b2a`, `atexit 0x10012a8a`) — an EMPTY list whose every read answers `0`. The port's
//     `FElysiumSheet` is retail's type-0 list; types 2 (buff) and 3 (scripted) have no port
//     container, so `TypedStatList` below answers "absent" for them and the callers take retail's
//     own empty-global arm. Family Motor10's `EnemyTypedStatValue` records the same absence for the
//     type-3 read on an ENEMY; this one is the join on THIS body and by type.
//   * **`+0x65cc` is `m_eForcedState`, a raw `NPC_STATE`, not an order id.** `0x102ae840` — which
//     `ElysiumAiScriptedSchedule.h` describes as stamping "the director's own order id" — stores its
//     first argument there beside `m_bForceStateChange` and `CHOOSE_NEW_SCHEDULE`, and slot 460
//     (`0x102ad340`) CONSUMES it as the ideal state and clears it. CORRECTION recorded at both
//     sites; the word is read through `ScheduleHost`'s owner, `ScriptedScheduleOrder.RetailOrderId`,
//     because that is the member the shape map binds to `+0x65cc`.
//   * **Every arm of every selector stamps retail's file/line trace (`+0x1b30`/`+0x1b34`, and
//     `+0x1b38`/`+0x1b3c`/`+0x1b40` for the ideal state).** The shape map records both as ABSENT —
//     "the mind's transition trace carries the same account" — so each arm calls
//     `RecordScheduleEvent` with the same file and line, exactly as family Schedule's slot-604
//     bodies do. The line numbers are retail's, in decimal where retail's own `.cpp` line is.

// --- The typed `CVStatList_t` join ---------------------------------------------------------------

/** `CVStatList_t::GetBase(statId)` on the same list. `0` for an absent list. */
int32 TypedStatBase(int32 ListType, int32 StatId) const;
/** `CVStatList_t::Set` / `AddBase` / `SubBase` / `IncBase` on the same list. A write to an absent
 *  list is COUNTED rather than dropped, so retail's two-step orders stay observable. */
void TypedStatSet(int32 ListType, int32 StatId, int32 Value);
void TypedStatAddBase(int32 ListType, int32 StatId, int32 Delta);
void TypedStatSubBase(int32 ListType, int32 StatId, int32 Delta);

// --- Slots 304 / 305: the fighting-item loadout --------------------------------------------------

/** `CBaseCombatCharacter::AddMiscFlag(0x10)` (`0x1033c6b0`) / `RemoveMiscFlag(0x10)` — the
 *  unarmed-combat marker both slot-304 bodies set and both slot-305 bodies clear. `ElysiumMiscFlags`
 *  names the word; this is the ONE bit this family writes, and it is named here because no
 *  `ElysiumMiscFlags` constant carried it before. */
static constexpr uint32 MiscFlagBaseFightingItems = 0x10;

/** `thunk_FUN_1021fe50(this, classname, 0)` (`0x1021fe50`) — `Weapon_Create`, init `0x10258620`,
 *  `Inventory_Can_Insert`, then slot `0x5fc` `Weapon_Equip` and, because the third argument is `0`,
 *  slot `0x610` `Weapon_Switch`. The port's `FElysiumInventory::GiveNamedItem` IS that route.
 *  Answers whether the item landed. */
bool GiveNamedFightingItem(const TCHAR* Classname);
/** `thunk_FUN_1021fee0(this, classname)` (`0x1021fee0`) — `Inventory_Find`, `Inventory_Remove`, then
 *  `UTIL_Remove` on the item. Answers whether anything was removed. */
bool RemoveNamedFightingItem(const TCHAR* Classname);
/** `CBaseCombatCharacter::Inventory_Find(classname)` — ordinary carried slots, case-insensitive. */
bool InventoryFindByClassname(const TCHAR* Classname) const;

// --- Slots 385 / 386: the two `Weapon_Drop` bodies ------------------------------------------------
//
// Both slots are generated virtuals; what they need and this runtime has no word for is below.

/** SEAM for the holster path slot 385 takes when the weapon it was handed IS the active weapon:
 *  `this->vtable +0x608`, which is `CBaseCombatCharacter::Weapon_Drop()` — slot 386's own no-argument
 *  body. Dispatched, not re-ported: `+0x608 / 4` is **386**. */

// --- Slot 460 `PreSelectIdealState` ---------------------------------------------------------------

/** The RAW recovered answer of `0x102ad340` — retail's `NPC_STATE` id, which this runtime's
 *  `EElysiumNpcState` cannot spell for `0` (no change), `8` (flee) or `0xe` (the criminal-suspicion
 *  window). The generated virtual returns the typed enum, so the raw id is the deliverable and is
 *  what the suite reads; `FElysiumNpcMind::DesiredRetailState()` carries the same word for the two
 *  flee arms and is written by every arm of this body too. */
/** Slot 460 in retail's own ordinals — the method species classes override (story 5 step 3). */
int32 PreSelectIdealStateRetail() override;
int32 LastPreSelectIdealStateRetail = 0;

/** `m_eForcedState` (`+0x65cc`) — see standing fact three. Read and CLEARED by slot 460's first arm.
 *  Held on `ScriptedScheduleOrder.RetailOrderId`, which is the member the shape map binds to the
 *  offset; these two accessors exist so the correction is written down once. */
int32 ForcedNpcState() const;
void ClearForcedNpcState();


// --- Slot 589 `SetScriptedDiscipline` -------------------------------------------------------------
//
// The slot is generated; everything it needs is the typed stat join above. 830 bytes only because
// the list lookup is inlined six times.

// --- Slot 357: the prayer pulse ------------------------------------------------------------------

// --- `0x102a0290` — the knockback velocity builder -----------------------------------------------

/** `FUN_102a0290(this, source, unused)`, 400 bytes, no slot — the builder of `m_KnockbackVelocity`
 *  (`+0x6004`). Three recovered facts the checklist's walk states and this reading confirms:
 *
 *    * the `5.0` initialiser is **DEAD** on the null-source path — it is only ever read through
 *      `t = raw * _DAT_104491b4`, which that path does not take;
 *    * with a source whose weapon id at `+0x3ec` lies in **139..147** (`0x10344da0`) the raw attack
 *      value is FORCED to `1.0` and the velocity is copied verbatim from the source's
 *      `+0x3bc..+0x3c4` (after `CalcAbsoluteVelocity` when `m_iEFlags` bit 12 is set);
 *    * `Z` is multiplied by the XY factor and then **REPLACED** outright by the Z factor, so the
 *      multiply is dead.
 *
 *  The four blend cells, read out of the pinned image: `_DAT_1049a1d8` **220**, `_DAT_1049a1dc`
 *  **400**, `_DAT_1049a1e0` **200**, `_DAT_1049a1e4` **310**; `_DAT_104491b4` is **0.1** and
 *  `_DAT_104454c0` **1.0**. SOURCE units per second, as `m_KnockbackVelocity` is. */
void ComputeKnockbackVelocity(FElysiumEntity* Source);

/** SEAM for `CBaseCombatCharacter::GetRawAttackValue(source)` — the blend parameter's source. No
 *  port accessor stands the attacker's raw attack value; answers `0.0`, which puts `t` at `0` and
 *  therefore lands on the LOW end of both blends (220 / 200) — the arm a zero-strength attacker
 *  takes, and an admitting answer rather than a refusal. */
float SourceRawAttackValue(const FElysiumEntity* Source) const;

/** SEAM for `0x10344da0(source->+0x3ec)` — "is the source's weapon id one of the 139..147 family",
 *  the test that forces the raw value to `1.0` and copies the source's own velocity. This runtime
 *  keys a weapon by classname and stands no retail weapon id, so this answers **false**; the arm is
 *  named and the recovered id window is the comment at the definition. */
bool SourceWeaponIdIsKnockbackFamily(const FElysiumEntity* Source) const;

/** SEAM for the source's `+0x3bc..+0x3c4` absolute velocity and the `m_iEFlags` bit-12 recompute
 *  (`CBaseEntity::CalcAbsoluteVelocity`). `FElysiumEntity` carries a velocity; the dirty-flag
 *  recompute has no counterpart and is counted. */
FVector SourceAbsVelocityUnits(FElysiumEntity* Source) const;

// --- `0x102a1650` — the yaw clearance sweep ------------------------------------------------------

/** `FUN_102a1650(this, unusedArg, yawDegrees, reach)`, 455 bytes, no slot — the hull sweep three
 *  `StartTask` call sites (`0x102a1910`, once with a literal `180.0`) and `CNPC_VFrenzyShadow::
 *  StartTask` (`0.0`) use to decide whether a task may run.
 *
 *  **CORRECTION to the walk on two points, both from the listing.** (1) The decompiler types the
 *  body `void`, but `102a1807` tail-calls the move probe and `RET 0xc` returns whatever it left in
 *  `AL` — so the sweep's answer IS this function's answer, which is why all four call sites read the
 *  byte. (2) The two scale cells are now RECOVERED out of the pinned image rather than unrecovered:
 *  `_DAT_10462950` is **40.0** (the forward leg) and `_DAT_10451acc` is **64.0** (the right leg).
 *
 *  End = origin + cos(yaw) * m_vecForward * 40 * reach + sin(yaw) * m_vecRight * 64 * reach, swept
 *  from `GetAbsOrigin` (slot 217) through `m_pMoveProbe` (`+0x5d40`, `0x102e6d70`) with mask
 *  `0x202400b` and the literal `100.0`. `param_1` is read by no arm and is carried for the record.
 *
 *  **SEAM**: no move probe stands here. Family Motor's `KernelHullTrace` is the standing seam and
 *  reports a CLEAR sweep, so this answers **true** — the admitting arm, which is what lets the task
 *  run, and a blocked sweep is what would refuse it. */
bool TraceMoveClearanceAtYaw(int32 UnusedArg, float YawDegrees, float Reach) const;

/** The last sweep this body ran, so a test can assert the recovered endpoint arithmetic without a
 *  probe behind it. SOURCE units. */
struct FYawClearanceSweep
{
	FVector StartUnits = FVector::ZeroVector;
	FVector EndUnits = FVector::ZeroVector;
	float YawDegrees = 0.f;
	float Reach = 0.f;
	int32 Mask = 0;
};
FYawClearanceSweep LastYawClearanceSweep;

// =================================================================================================
// Slot 605 `SelectScheduleRangedCombat` — the dispatcher's six arms and the three helpers
// =================================================================================================
//
// The slot method is the Troika body and lives in `ElysiumNpcCombat10_2.cpp`; each species body is
// its class's override (story 5 step 3). The six bodies are declared rather than inlined for one
// recovered reason: `CNPC_VBach`'s arm ends by calling
// `CNPC_VHuman`'s body `0x10386560` through a DIRECT, non-virtual call, which a named body expresses
// (story 5 step 3 made the six bodies their classes' overrides).
//
// A combat-schedule selector is an ORDERED body. Every arm below is in retail's order and the first
// that answers wins; the order is the behaviour.

/** `CAI_BaseNPCTroika::SelectScheduleRangedCombat` (`0x102b7fc0`), the Troika-line body, 677 bytes.
 *  Fills `CAI_BaseNPCTroika#605` and 20 more. */
int32 TroikaSelectScheduleRangedCombat(int32 Arg);

/** `FUN_102b8620` (`0x102b8620`), 688 bytes, no slot and no verdict row of its own — the weapon
 *  pre-pass every ranged selector offers first. It owns reload `0xc4`/`0xc6`, cover `0xc2`/`0xc3`,
 *  the draw `0xe9`, melee `0xe3`, the spacing trio `0xe4`/`0xe5`/`0xe7` and the no-weapon `0x98`.
 *  Ported here because a selector whose first helper is a stub is a selector with no order. */
int32 RangedWeaponPrePass();

/** `FUN_102b7f40` (`0x102b7f40`), 93 bytes, no slot and no verdict row — the dodge test.
 *
 *  **CORRECTION**: the checklist's one-line walk of the Ming Xiao selector describes this as
 *  "`SelectWeightedSequence(ACT 0x10)` non-zero AND not `COND 0x2c` AND `RandomInt(0,99) < 0x4b`".
 *  That is only the FIRST of three arms. The body is: a zero weighted sequence refuses outright;
 *  then `!COND_STOP_BACKUP(0x2c) && RandomInt(0,99) < 0x4b` → true; then the discipline test
 *  `0x101e3f50` → true; then `COND_WEAPON_THROUGH_WALL(0x3c)` → true; else false. The last two run
 *  even when `COND 0x2c` stands. Ming Xiao and the Sheriff man inline only the first arm, which is
 *  why they are NOT calls to this body and are written out at their own sites. */
bool ShouldDodgeRangedAttack();

/** `FUN_102b7cf0` (`0x102b7cf0`), 462 bytes — the taunt-and-cover prologue the human, Ming Xiao and
 *  Sheriff-man selectors offer third.
 *
 *  **CORRECTION**: the checklist's walk reads as though the cvar-gated half sits beside the
 *  `GetEnemy && CanSeekCover` gate ("then, gated on cvar …"). It does not: `102b7d2a` opens ONE
 *  block on `GetEnemy() != 0 && CanSeekCover()` (slot 592, `vt+0x940`) and the cover offer, the cvar
 *  gate, the `0x800` taunt arm and the `SEE_ENEMY` arm are ALL inside it. A body with no enemy
 *  answers `0x10` for `COND_LOST_ENEMY` and `0` for everything else.
 *
 *  `_DAT_10463584` is **15.0**, read out of the pinned image — the amount the `0x8f` arm advances
 *  `m_flNextDodgeTime` (`+0x65a4`) by. */
int32 SelectCombatReactionSchedule();

/** The two ConVar gates the pre-pass and the prologue read as `!vtable[+0x04]() && m_nValue`:
 *  `DAT_10923d3c` `debug_allow_fake_reload` (shipped "1", the reload gate) and `DAT_109248f4`
 *  `debug_allow_dodge` (shipped "0", the no-cover dodge gate). */
static bool RangedGateConVarEnabled(ElysiumNpcTunables::EConVar ConVar);

/** SEAM for `0x101e3f50(&DAT_10739a4c, entity)` — the discipline test both the Troika base and the
 *  human arm put in front of their slot-606 branch (the base passes THIS body, the human passes
 *  `enemy->+0x9c`, its combat-character self-downcast). `DAT_10739a4c` is an unnamed discipline
 *  record and no port discipline is bound to it, so this answers **false**, which is the arm that
 *  ADMITS the slot-606 branch rather than skipping it. */
bool RangedDisciplineGate(const FElysiumEntity* Subject) const;

/** SEAM for the weapon-side reads `0x102b8620` makes: the active weapon's slot `+0x5a0` capability
 *  word (tested against `0x6000`), its `+0x74c` first ammo entry, its slot `+0x460` "wants reload"
 *  and its `+0x744` ammo-type id fed to `thunk_FUN_103346c0` `GetAmmoCount`. The port's item record
 *  answers the last two through the inventory's own magazine and reserve; `+0x460` has no port
 *  surface and answers false, which is the arm that skips the reload and the cover pair and lets the
 *  body reach its spacing tail. The `+0x5a0` word is family **Motor**'s `ActiveWeaponCapabilityWord`
 *  seam (`ElysiumNpcMotor.inl`) — the SAME read with the SAME `0x6000` mask — and is reused
 *  rather than stood a second time. */
int32 ActiveWeaponFirstAmmoEntry() const;
bool ActiveWeaponWantsReload() const;
int32 ActiveWeaponReserveAmmo() const;

/** `thunk_FUN_102c54c0(this)` (`0x102c54c0`) — the call `0x102b8620` makes before answering a reload
 *  schedule. **SEAM**: unported; counted so the arm is observable. */
int32 RangedReloadPrepCalls = 0;

/** `m_iFakeReloadCount` (`+0x65f0`) is already a member (`FakeReloadCount`) and `+0x5ddc` is
 *  `ScheduleHost::HintNode`; both are read by `0x102b8620` and neither is declared here. */
