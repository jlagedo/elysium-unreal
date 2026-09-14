// Story 29d, family **Combat10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. What lands here is
// the non-slot half — the base-class bodies beneath a Troika override, the species arms the slot
// dispatchers run, the retail helpers those bodies call, and the seams that stand for retail inputs
// this substrate has no source for.
//
// The definitions are in `Substrate/ElysiumNpcKernelCombat10.cpp` (the loadout, the health-percent
// readers, the weapon drops, the discipline write, the ideal-state pre-select, the knockback and the
// yaw sweep) and `Substrate/ElysiumNpcKernelCombat10_2.cpp` (slot 605 and everything it calls); the
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

/** Retail's list-type scan, as an answer rather than as a pointer: `true` when THIS body carries a
 *  `CVStatList_t` of `ListType` (the `+0x13bc`/`+0x13c0` walk found one), `false` when the walk fell
 *  through to the lazily built empty global `DAT_109f0b40`.
 *
 *  **Type 0 is the character sheet and this runtime has it.** Types 2 (the buff list) and 3 (the
 *  scripted list) have no port container, so this answers false for them and every caller takes
 *  retail's own empty-global arm — `GetBase`/`GetValue` read `0`, `Set`/`AddBase`/`SubBase` write
 *  into a list nothing else reads, which is what retail does for an entity with no such list. Named
 *  rather than guessed. */
bool HasTypedStatList(int32 ListType) const;

/** `CVStatList_t::GetValue(statId)` (`0x102012d0`) on the list `HasTypedStatList(ListType)` found —
 *  the clamped read every health body makes. `0` for an absent list, which is the empty global's
 *  answer. */
int32 TypedStatValue(int32 ListType, int32 StatId) const;
/** `CVStatList_t::GetBase(statId)` on the same list. `0` for an absent list. */
int32 TypedStatBase(int32 ListType, int32 StatId) const;
/** `CVStatList_t::Set` / `AddBase` / `SubBase` / `IncBase` on the same list. A write to an absent
 *  list is COUNTED rather than dropped, so retail's two-step orders stay observable. */
void TypedStatSet(int32 ListType, int32 StatId, int32 Value);
void TypedStatAddBase(int32 ListType, int32 StatId, int32 Delta);
void TypedStatSubBase(int32 ListType, int32 StatId, int32 Delta);
void TypedStatIncBase(int32 ListType, int32 StatId);

/** One recorded write to a list this runtime does not stand — the seam's ledger, so a test can read
 *  the ORDER retail's `Set`-then-`SubBase` and `AddBase`-then-`Set` pairs write in. */
struct FTypedStatWrite
{
	int32 ListType = 0;
	int32 StatId = 0;
	int32 Value = 0;
	const TCHAR* Op = nullptr;   // "Set", "AddBase", "SubBase", "IncBase"
};
TArray<FTypedStatWrite> TypedStatWrites;

// --- Slot 348 `HealthToPercent` and its species arm ----------------------------------------------

/** `CNPC_VMingXiao::vfunc348` (`0x103970d0`), slot 348's one species arm: the base formula with a
 *  loop `i = 0..5` over `0x10398000(this, i)` folding one extra contribution in per true result —
 *  the regrown-limb count. Declared here and dispatched from the slot body. */
int32 MingXiaoHealthToPercent();

/** SEAM for `CNPC_VMingXiao::0x10398000(this, i)` — "is limb `i` (0..5) still attached". No port
 *  system stands Ming Xiao's severable limbs, so this answers **false** for every index, which
 *  leaves the arm equal to the base formula — retail's own answer for an intact boss. */
bool MingXiaoLimbPresent(int32 LimbIndex) const;

/** `CNPC_VVampireBoss::GetCurrHealthPercent` (`0x103c6830`), 356 bytes, no slot. The COMPLEMENT of
 *  `HealthToPercent`: `stat0xf / stat0x11` as a float, guarded by `ABS(cap) > 1e-05`
 *  (`_DAT_104ce8c0`) on the DIVISOR — the decompiler's `(a < eps) == (a == eps)` idiom resolves to
 *  `a > eps` — and `_DAT_104454c4` = **0.0** otherwise. */
float GetCurrHealthPercent() const;

// --- Slots 304 / 305: the fighting-item loadout --------------------------------------------------

/** `CNPC_VWerewolf::GiveBaseFightingItems` (`0x103cc9b0`) and `::RemoveBaseFightingItems`
 *  (`0x103cca80`), slot 304's and 305's one species pair. The Werewolf REPLACES the base rather than
 *  extending it: where the base gates on slots 307 and 308 BOTH answering false before giving
 *  `item_w_fists`, the Werewolf runs `Inventory_Find("item_w_werewolf_attacks")` and, only when
 *  absent, gives that item and sets misc flag `0x10`. It never consults the two base gates and never
 *  grants fists.
 *
 *  **UNREACHABLE TODAY**: `CNPC_VWerewolf` carries no entity classname in the census, so no spawned
 *  NPC's `RetailClass()` can be it. The arm is ported and exercised through
 *  `SetRetailClassForTests`, which is the only way to reach it; see the suite. */
void WerewolfGiveBaseFightingItems();
void WerewolfRemoveBaseFightingItems();

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

/** `thunk_FUN_102577e0(weapon)` (`0x102577e0`) — "may this weapon be dropped at all", the gate both
 *  bodies put in front of everything else. The port's item data carries `is_droppable`, so this is
 *  the real answer and not a seam. A null weapon answers false. */
static bool WeaponIsDroppable(const FElysiumEntity* Weapon);

/** **CORRECTION.** The checklist's walk of `0x1032d0c0` calls slot 385's second arm an AMMO test —
 *  "when the weapon still has ammo (vfunc+0x5cc true and ammo count `param_1[0x234]` >= 2)". The
 *  decompiler types the weapon `int*`, so `param_1[0x234]` is BYTE offset **`0x8d0`**, which is
 *  `m_iItemCount` — the STACK quantity, not a magazine. Slot 385's second arm is therefore a STACK
 *  SPLIT: a stackable weapon carrying two or more decrements the stack and spawns one loose copy.
 *  `+0x74c` (the real magazine) is untouched by either drop body except through slot 386's reroll.
 *
 *  `WeaponIsStackSplittable` is the weapon's own vtable `+0x5cc`, which the port answers as the item
 *  record's `is_stackable`; `WeaponStackCount` is `+0x8d0` and is a real member. */
bool WeaponIsStackSplittable(const FElysiumEntity* Weapon) const;
int32 WeaponStackCount(const FElysiumEntity* Weapon) const;
void SetWeaponStackCount(FElysiumEntity* Weapon, int32 Count);

/** SEAM for `CBaseCombatCharacter::Weapon_Create(classname)` + `thunk_FUN_10258620` + the new
 *  weapon's slot `+0x4b0`, slot 385's split arm. The port creates the item through the world's own
 *  factory; a world that cannot create answers null, and retail's own null arm then falls straight
 *  out of the body WITHOUT removing the original, which is reproduced. The classname comes from the
 *  original weapon's own vtable `+0x570` (`GetClassname`). */
FElysiumEntity* WeaponCreateForDrop(const TCHAR* Classname);

/** SEAM for the dropped weapon's own vtable `+0x4b0` — the drop notify (`Drop()`), which detaches
 *  the model, re-enables its physics and starts its world-lifetime timer. Nothing in this runtime
 *  stands a loose world weapon, so the call is COUNTED with the weapon it was made for. */
int32 WeaponDropNotifies = 0;
void NotifyWeaponDropped(FElysiumEntity* Weapon);

/** SEAM for `CBaseCombatCharacter::Weapon_Detach(weapon)` — the `FL_NPC` tail of both bodies, run
 *  AFTER `Inventory_Remove`. `Inventory_Remove` is the port's `FElysiumInventory::Detach`; the
 *  detach here is retail's SECOND, model-side release and has no port surface, so it is counted. */
int32 WeaponDetaches = 0;
void WeaponDetach(FElysiumEntity* Weapon);

/** SEAM for `thunk_FUN_100b5190(weapon)` and the weapon slot `+0x138` it gates — the "is this
 *  weapon networked/visible" test and the transmit-state update slot 386 runs after the ammo
 *  reroll. Neither has a port surface; answers false, which is the arm that performs no update. */
bool WeaponNeedsTransmitUpdate(const FElysiumEntity* Weapon) const;

/** SEAM for the ammo reroll slot 386 runs for an `FL_NPC` body: two entries (`i = 0` and `1`, byte
 *  stride `0x1093c` up to `0x21278`) written into `weapon+0x74c+4*i`. Per entry, the weapon's slot
 *  `+0x454` decides the source — true takes `RandomInt(1, wpndata+0x2674+i*0x1093c)` through
 *  `0x102517b0`, false takes the weapon's slot `+0x450` as a cap and writes `RandomInt(1, cap)`, or
 *  a flat `0` when the cap is below `1`.
 *
 *  This runtime stands neither the two-entry ammo array nor the weapon-data table, so the rolled
 *  pair is RECORDED against the weapon with the cap it came from, and the recovered rule — the two
 *  entries, the `< 1` flat zero, the `RandomInt(1, cap)` — is what the test drives. */
struct FWeaponAmmoReroll
{
	FElysiumEntityHandle Weapon;
	int32 Entry[2] = { 0, 0 };
	int32 Cap[2] = { 0, 0 };
	bool bFromWeaponData[2] = { false, false };
};
TArray<FWeaponAmmoReroll> WeaponAmmoRerolls;
void RerollDroppedWeaponAmmo(FElysiumEntity* Weapon);
/** The weapon's slot `+0x454` ("does ammo type `i` come from the weapon-data table") and its slot
 *  `+0x450` ("the cap for ammo type `i`"). **SEAM**: no weapon vtable stands here; `+0x454` answers
 *  false and `+0x450` answers `0`, which is the `cap < 1` arm and writes a flat `0` — retail's own
 *  answer for a weapon that declares no ammo of that type. */
bool WeaponAmmoFromWeaponData(const FElysiumEntity* Weapon, int32 Index) const;
int32 WeaponAmmoCap(const FElysiumEntity* Weapon, int32 Index) const;

/** `m_hLastWeapon` (`+0x0ea4`) and `m_hLastMeleeWeapon` (`+0x0ea8`) — the two handles both bodies
 *  reset to `0xffffffff`, and ONLY when they resolve to the weapon being dropped. The port's
 *  `FElysiumInventory::PreviousWeapon` is `m_hLastWeapon`; there is no melee twin, so it is declared
 *  here because these two bodies are its retail writers. */
FElysiumEntityHandle LastMeleeWeapon;   // +0x0ea8 m_hLastMeleeWeapon

/** SEAM for the holster path slot 385 takes when the weapon it was handed IS the active weapon:
 *  `this->vtable +0x608`, which is `CBaseCombatCharacter::Weapon_Drop()` — slot 386's own no-argument
 *  body. Dispatched, not re-ported: `+0x608 / 4` is **386**. */

// --- Slot 460 `PreSelectIdealState` ---------------------------------------------------------------

/** The RAW recovered answer of `0x102ad340` — retail's `NPC_STATE` id, which this runtime's
 *  `EElysiumNpcState` cannot spell for `0` (no change), `8` (flee) or `0xe` (the criminal-suspicion
 *  window). The generated virtual returns the typed enum, so the raw id is the deliverable and is
 *  what the suite reads; `FElysiumNpcMind::DesiredRetailState()` carries the same word for the two
 *  flee arms and is written by every arm of this body too. */
int32 PreSelectIdealStateRetail();
int32 LastPreSelectIdealStateRetail = 0;

/** `m_eForcedState` (`+0x65cc`) — see standing fact three. Read and CLEARED by slot 460's first arm.
 *  Held on `ScriptedScheduleOrder.RetailOrderId`, which is the member the shape map binds to the
 *  offset; these two accessors exist so the correction is written down once. */
int32 ForcedNpcState() const;
void ClearForcedNpcState();

/** `CSecureType`'s decode pair, `0x1042fe90` over the `+0x6364` unscramble — the two steps slot 460
 *  runs on `m_iPLCriminalLevelWitnessed` and on the literal `0x3cf445af`, which decodes to **2**.
 *  `ElysiumNpcKernelSenses.cpp` carries the same constants for the pedestrian's reader; they are
 *  restated here (file statics, not shared) and the literal's decode is pinned by a test.
 *
 *  This runtime stores the witnessed level PLAIN (`FElysiumNpcWitnessChannel::Level`), so the decode
 *  is the identity on the port's word and the bound is the recovered `2`. */
static uint32 SecureUnhashLevel(uint32 Value);
static uint32 SecureUnscrambleLevel(uint32 Stored);

// --- Slot 589 `SetScriptedDiscipline` -------------------------------------------------------------
//
// The slot is generated; everything it needs is the typed stat join above. 830 bytes only because
// the list lookup is inlined six times.

// --- Slot 357: the prayer pulse ------------------------------------------------------------------

/** `0x1033b5f0`'s body BELOW its two gates — the half the suite drives while `m_Activity` is a seam.
 *
 *  Retail: compare `GetValue(stat 0xe FaithPoints)` against the stat definition's own maximum
 *  (`0x10200370` over `DAT_1074e658`, then `0x10205840` on its `+0x60`); strictly below it
 *  `IncBase(0xe)`, at or above it dispatch slot 358 `PrayerEnd`. Then
 *  `m_flNextFeedPulse = curtime + m_flNextFeedDuration`, and while `m_flNextFeedDuration` is above
 *  `_DAT_1047b868` (a DOUBLE reading **0.3**) it is reduced by `_DAT_1049e890` (also a double,
 *  **0.15**) so the interval accelerates to that floor. */
void RunPrayerPulse(double Now);

/** `m_Activity == 0x132`, the activity gate. Family Hints' `CurrentRetailActivityId()` is the seam
 *  and answers `-1`, so the gate refuses — retail's own answer for a body that is not praying. */
static constexpr int32 PrayerActivityId = 0x132;

/** The authored maximum of stat `0xe`. `FElysiumSheet::BoundsFor` resolves the same `stats.txt`
 *  `Max` retail reads off the stat definition, so this is the real answer where a rulebook is in
 *  reach and `0` where none is — and `0` makes `GetValue >= max` true, which takes retail's
 *  `PrayerEnd` arm rather than incrementing forever. Named because that fallback is a choice. */
int32 FaithPointsMaximum() const;

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
// The slot method is the dispatcher and lives in `ElysiumNpcKernelCombat10_2.cpp`; it selects on
// `OverrideOf(RetailClass(), 605)` exactly as slot 604's does and runs one of the six bodies below.
// They are declared rather than inlined for one recovered reason: `CNPC_VBach`'s arm ends by calling
// `CNPC_VHuman`'s body `0x10386560` through a DIRECT, non-virtual call, which one function cannot
// express — the same situation `FSpeciesDispatchScope` was ported for.
//
// A combat-schedule selector is an ORDERED body. Every arm below is in retail's order and the first
// that answers wins; the order is the behaviour.

/** `CAI_BaseNPCTroika::SelectScheduleRangedCombat` (`0x102b7fc0`), the Troika-line body, 677 bytes.
 *  Fills `CAI_BaseNPCTroika#605` and 20 more. */
int32 TroikaSelectScheduleRangedCombat(int32 Arg);

/** `CNPC_VHuman::SelectScheduleRangedCombat` (`0x10386560`), 802 bytes — the shared human arm that
 *  fills 36 species `#605` slots and no Troika slot. It REPLACES the Troika body wholesale and never
 *  chains it. */
int32 HumanSelectScheduleRangedCombat(int32 Arg);

/** `CNPC_VAsianVampire::SelectScheduleRangedCombat` (`0x103620d0`), 546 bytes. Answers `0xf0` where
 *  the Troika base answers `0xb8` for COND `0x3c`, and against the human arm it has no dodge helper,
 *  no slot-606 arm and no cover-hint arm. */
int32 AsianVampireSelectScheduleRangedCombat(int32 Arg);

/** `CNPC_VBach::SelectScheduleRangedCombat` (`0x103642f0`), 413 bytes — the one arm that CHAINS:
 *  its katana/rifle classname tests fall through to the human body, and its own tail then rewrites
 *  the answer. */
int32 BachSelectScheduleRangedCombat(int32 Arg);

/** `CNPC_VMingXiao::SelectScheduleRangedCombat` (`0x103967d0`), 794 bytes — the human skeleton with
 *  the COND `0x3c` arm dropped, the discipline gate dropped, and the dodge decision INLINED at the
 *  same `0x4b` threshold `0x102b7f40` uses. */
int32 MingXiaoSelectScheduleRangedCombat(int32 Arg);

/** `CNPC_VSheriffMan::SelectScheduleRangedCombat` (`0x103afdb0`), 984 bytes — three helpers offered
 *  separately rather than as one chained condition, an inlined dodge, and its own `0x15a` answer on
 *  an unreachable enemy. */
int32 SheriffManSelectScheduleRangedCombat(int32 Arg);

/** `CNPC_VBach`'s `+0x6690` — the stamp its COND `0x7b` arm writes `curtime + _DAT_10463584`
 *  (**15.0**) into before answering `0x15a`. A Bach-line word with no port producer and no other
 *  recovered reader; declared here because this body IS its retail writer. (`CNPC_VScurrying` owns
 *  the same OFFSET on its own line as `m_flDetectionDistance`, family Senses10 — two classes, one
 *  offset, two words.) */
double BachRepositionTimer = 0.0;

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

/** SEAM for the two ConVar objects the pre-pass and the prologue gate on: `DAT_10923d3c` (the weapon
 *  pre-pass's reload gate) and `DAT_109248f4` (the prologue's taunt gate). Retail reads each
 *  object's vtable `+0x04` bool and its `+0x2c` int and runs the block only when the bool is CLEAR
 *  and the int is non-zero. Neither convar's name nor its default is in the corpus, so both answer
 *  the SHIPPED-DEFAULT shape: bool clear, int non-zero — the ADMITTING arm, which is the one that
 *  lets the recovered block run rather than silently deleting it. */
static bool RangedGateConVarEnabled(const TCHAR* RetailGlobal);

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
 *  seam (`ElysiumNpcKernelMotor.inl`) — the SAME read with the SAME `0x6000` mask — and is reused
 *  rather than stood a second time. */
int32 ActiveWeaponFirstAmmoEntry() const;
bool ActiveWeaponWantsReload() const;
int32 ActiveWeaponReserveAmmo() const;

/** `thunk_FUN_102c54c0(this)` (`0x102c54c0`) — the call `0x102b8620` makes before answering a reload
 *  schedule. **SEAM**: unported; counted so the arm is observable. */
int32 RangedReloadPrepCalls = 0;

/** `m_iFakeReloadCount` (`+0x65f0`) is already a member (`FakeReloadCount`) and `+0x5ddc` is
 *  `ScheduleHost::HintNode`; both are read by `0x102b8620` and neither is declared here. */
