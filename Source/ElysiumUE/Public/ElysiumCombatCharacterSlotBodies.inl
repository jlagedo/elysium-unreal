// `CBaseCombatCharacter`'s hand-written slot bodies and the members they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Included inside `class FElysiumCombatCharacter`
// (`ElysiumPlayer.h`), after its generated slot surface; the definitions are in
// `Private/Substrate/ElysiumCombatCharacterSlotBodies.cpp`.

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

/** One recorded write to a list this runtime does not stand — the seam's ledger, so a test can read
 *  the ORDER retail's `Set`-then-`SubBase` and `AddBase`-then-`Set` pairs write in. */
struct FTypedStatWrite
{
	int32 ListType = 0;
	int32 StatId = 0;
	int32 Value = 0;
	const TCHAR* Op = nullptr;   // "Set", "AddBase", "SubBase", "IncBase"
};

// `CBaseAnimating::SetPoseParameter(name, value)` (retail vtable +0x564) — `SetAim`'s two writes.
// **SEAM**: this runtime's animating tier exposes no pose-parameter surface, so the writes are
// recorded and go no further. Read by the test and by nothing else.
struct FPoseParameterWrite
{
	FString Name;
	float Value = 0.f;
};

/** `CAI_BaseNPC::FUN_10344dd0` `0x10344dd0`, slot 323 — the four codes retail returns for a movement
 *  direction relative to the body's own facing, by the angle band the direction falls in. Every
 *  boundary below was read out of the pinned `vampire.dll`'s `.rdata`, so the bands are recovered
 *  numbers and the NAMES are this port's reading of them (`+yaw` is counter-clockwise in Source, so
 *  a relative yaw of 90 is to the body's left). The NUMBERS are retail's own return values.
 *
 *  Note `_DAT_1049e8a4` is **316**, not 315: the four bands are 89, 90, 90 and 91 degrees wide, not
 *  a clean quarter split. That is retail's number and it is kept. */
enum class EMoveDirectionCode : int32
{
	Behind = 0,   // (135, 225]                — `_DAT_1049e8a0` .. `_DAT_1049e89c`, and the too-slow arm
	Right = 1,    // (225, 316]                — above `_DAT_1049e89c`
	Ahead = 2,    // (316, 360) and [0, 45]    — above `_DAT_1049e8a4`, or at-or-below `_DAT_1049949c`
	Left = 3,     // (45, 135]                 — at-or-below `_DAT_1049e8a0`
};

// `m_bfAINPCFlags` / `m_bfAINPCFlags2` and the obliviousness refcount. Written by
// `TASK_SET_NPC_FLAG` / `TASK_MAKE_OBLIVIOUS` and released by every schedule install; saved,
// because retail's are datamap members and an NPC left mesmerized across a save must not wake up
// conversable.
FElysiumNpcFlags NpcFlags;

// `cantdropweapons` (78 authored rows; 71 write 0 and 7 write 1).
//
// SEAM (parsed, unread): the drop it suppresses is the death-time weapon drop, and this runtime
// has no such path — `Event_Killed`'s weapon cleanup does not spawn a loose item yet. The
// keyfield is carried so an authored NPC round-trips through a save with the policy it was
// authored with, and so the drop path has a value to read the day it lands.
bool bCantDropWeapons = false;

TArray<FWeaponAmmoReroll> WeaponAmmoRerolls;

TArray<FTypedStatWrite> TypedStatWrites;

/** SEAM for the dropped weapon's own vtable `+0x4b0` — the drop notify (`Drop()`), which detaches
 *  the model, re-enables its physics and starts its world-lifetime timer. Nothing in this runtime
 *  stands a loose world weapon, so the call is COUNTED with the weapon it was made for. */
int32 WeaponDropNotifies = 0;

/** SEAM for `CBaseCombatCharacter::Weapon_Detach(weapon)` — the `FL_NPC` tail of both bodies, run
 *  AFTER `Inventory_Remove`. `Inventory_Remove` is the port's `FElysiumInventory::Detach`; the
 *  detach here is retail's SECOND, model-side release and has no port surface, so it is counted. */
int32 WeaponDetaches = 0;

/** `m_hLastWeapon` (`+0x0ea4`) and `m_hLastMeleeWeapon` (`+0x0ea8`) — the two handles both bodies
 *  reset to `0xffffffff`, and ONLY when they resolve to the weapon being dropped. The port's
 *  `FElysiumInventory::PreviousWeapon` is `m_hLastWeapon`; there is no melee twin, so it is declared
 *  here because these two bodies are its retail writers. */
FElysiumEntityHandle LastMeleeWeapon;   // +0x0ea8 m_hLastMeleeWeapon

/** `m_Activity == 0x132`, the activity gate. Family Hints' `CurrentRetailActivityId()` is the seam
 *  and answers `-1`, so the gate refuses — retail's own answer for a body that is not praying. */
static constexpr int32 PrayerActivityId = 0x132;

TArray<FPoseParameterWrite> PoseParameterWrites;

/** `+0x0ec0 m_iCurFrenzyCount` (`CBaseCombatCharacter`) — the discipline gate slot 334 reads
 *  first: a body already mid-frenzy refuses every further cast outright. Below the shape map's
 *  band, so 29b did not bind it, and no landed family had a reader for it. Nothing in this runtime
 *  writes it yet. */
int32 CurFrenzyCount = 0;

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

void RerollDroppedWeaponAmmo(FElysiumEntity* Weapon);

/** The weapon's slot `+0x454` ("does ammo type `i` come from the weapon-data table") and its slot
 *  `+0x450` ("the cap for ammo type `i`"). **SEAM**: no weapon vtable stands here; `+0x454` answers
 *  false and `+0x450` answers `0`, which is the `cap < 1` arm and writes a flat `0` — retail's own
 *  answer for a weapon that declares no ammo of that type. */
bool WeaponAmmoFromWeaponData(const FElysiumEntity* Weapon, int32 Index) const;

int32 WeaponAmmoCap(const FElysiumEntity* Weapon, int32 Index) const;

void TypedStatIncBase(int32 ListType, int32 StatId);

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

void NotifyWeaponDropped(FElysiumEntity* Weapon);

void WeaponDetach(FElysiumEntity* Weapon);

/** SEAM for `thunk_FUN_100b5190(weapon)` and the weapon slot `+0x138` it gates — the "is this
 *  weapon networked/visible" test and the transmit-state update slot 386 runs after the ammo
 *  reroll. Neither has a port surface; answers false, which is the arm that performs no update. */
bool WeaponNeedsTransmitUpdate(const FElysiumEntity* Weapon) const;

/** `0x1033b5f0`'s body BELOW its two gates — the half the suite drives while `m_Activity` is a seam.
 *
 *  Retail: compare `GetValue(stat 0xe FaithPoints)` against the stat definition's own maximum
 *  (`0x10200370` over `DAT_1074e658`, then `0x10205840` on its `+0x60`); strictly below it
 *  `IncBase(0xe)`, at or above it dispatch slot 358 `PrayerEnd`. Then
 *  `m_flNextFeedPulse = curtime + m_flNextFeedDuration`, and while `m_flNextFeedDuration` is above
 *  `_DAT_1047b868` (a DOUBLE reading **0.3**) it is reduced by `_DAT_1049e890` (also a double,
 *  **0.15**) so the interval accelerates to that floor. */
void RunPrayerPulse(double Now);

/** The authored maximum of stat `0xe`. `FElysiumSheet::BoundsFor` resolves the same `stats.txt`
 *  `Max` retail reads off the stat definition, so this is the real answer where a rulebook is in
 *  reach and `0` where none is — and `0` makes `GetValue >= max` true, which takes retail's
 *  `PrayerEnd` arm rather than incrementing forever. Named because that fallback is a choice. */
int32 FaithPointsMaximum() const;

/** `CBaseCombatCharacter::CanBeFedUpon` (`0x10339a90`) — `GetCharTemplate(this)->+0x95 == 0`.
 *  **SEAM**: `+0x95` has no recovered column name and this runtime's `FElysiumClanTemplate` exposes
 *  none, so this answers TRUE, which is retail's own answer for a template whose byte is zero — the
 *  admitting arm, so nothing is silently refused. */
bool CanBeFedUponTemplate() const;

/** `CBaseCombatCharacter::IsUnconscious` (`0x10341aa0`) — `(m_iMiscFlags & 1) != 0`, bit 0 of the
 *  name table being `Unconscious` (`Substrate/ElysiumMiscFlags.h`). */
bool IsUnconsciousMiscFlag() const;

/** SEAM for `thunk_FUN_10427620` — `GetAmmoDef()->MaxCarry(index)`, the capacity `GiveAmmo` clamps
 *  against, and for the index-to-name join the clamp needs. This runtime's `FElysiumInventory`
 *  keys its reserve by the authored ammo TYPE NAME (`AmmoReserve`, a `TMap<FString,int32>`), not by
 *  retail's 0..31 `CAmmoDef` index, and no table joins the two. Both answer nothing —
 *  `MaxCarry` `0` and the name empty — which closes `GiveAmmo`'s clamp at zero and makes it return
 *  0 without sounding. **Unrecovered:** the `CAmmoDef` index order. */
int32 AmmoMaxCarry(int32 AmmoIndex) const;

FString AmmoTypeNameForIndex(int32 AmmoIndex) const;

/** SEAM for `(*g_pGameRules)->vtable+0xd4` — the "is ammo enabled" predicate `GiveAmmo` asks before
 *  anything else. No rules object here carries it; answers TRUE, the permissive arm, so the clamp
 *  below it is what actually decides. */
bool GameRulesAllowsAmmo(int32 AmmoIndex) const;

/** `CBaseCombatCharacter::GetActiveWeapon()` (`0x10007e19` → the inventory's active slot), which
 *  `DrawDebugGeometryOverlays`'s `0x10000` arm hands to `Weapon_Drop`. **SEAM**: no kernel accessor
 *  stands the active weapon entity yet (family BaseHelpers seams its range the same way). Answers
 *  null, and the drop still happens with a null weapon exactly as retail's would. */
FElysiumEntity* ActiveWeaponEntity() const;

/** `DAT_1072b360`, the single global word slot 240 returns. It is the CPython interop side of the
 *  entity — whatever the embedded interpreter last stored there — and this runtime embeds no
 *  interpreter. **SEAM**: answers null, which is the global's own pre-interpreter value. */
void* PythonInteropObject() const;

void SetPoseParameterByName(const TCHAR* Name, float Value);

/** SEAM for `thunk_FUN_101e1250(&DAT_10739a4c, disciplineId, arg)` and `thunk_FUN_101e11c0` — the
 *  global discipline table slot 334 looks a discipline up in, and the cooldown float at record
 *  `+0x2c`. There is no such table on this substrate; `Find` answers `INDEX_NONE` and the row
 *  lookup answers `0.0`. */
int32 DisciplineTableFind(int32 DisciplineId, int32 Level) const;

float DisciplineTableCooldown(int32 RowIndex) const;

/** SEAM for `m_fDisciplineTimers[row]` (`+0x146c`), the per-discipline last-cast stamps slot 334
 *  measures against. Below the shape map's band and with no producer here; answers `0.0`, which
 *  makes every elapsed time `curtime` and so every cooldown expired. */
double DisciplineTimer(int32 RowIndex) const;

/** SEAM for `m_Activity` (`+0xfec`), the currently playing retail `Activity` id that
 *  `RunInterestingPlaceLoop` compares its refreshed id against. Answers -1: this runtime's activity
 *  is an `FElysiumClipIdentity` name pair and carries no retail id. */
int32 CurrentRetailActivityId() const;

/** The arithmetic of slot 364 (`0x10326bd0`), pure over the three vectors the body fetches through
 *  a virtual. Split out because the virtual that supplies the aim — slot 370 `HeadDirection2D`,
 *  which slot 372 forwards to — is still a GENERATED STUB answering the zero vector, so the slot
 *  body below cannot exercise its own threshold yet. The rule is exact and testable here; the input
 *  is another story's seam and is named at the call.
 *
 *  `AimConeAdmits` zeroes the delta's Z BEFORE normalising and compares strictly against
 *  `0.994`. */
static bool AimConeAdmits(const FVector& OriginCm, const FVector& TargetCm, const FVector& Aim);
