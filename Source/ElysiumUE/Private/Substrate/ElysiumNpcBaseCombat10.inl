// `CAI_BaseNPC`'s declarations of the `Combat10` family (story 5 step 5),
// moved from `ElysiumNpcCombat10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseCombat10.cpp`.

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

/** One recorded write to a list this runtime does not stand — the seam's ledger, so a test can read
 *  the ORDER retail's `Set`-then-`SubBase` and `AddBase`-then-`Set` pairs write in. */
struct FTypedStatWrite
{
	int32 ListType = 0;
	int32 StatId = 0;
	int32 Value = 0;
	const TCHAR* Op = nullptr;   // "Set", "AddBase", "SubBase", "IncBase"
};

void TypedStatIncBase(int32 ListType, int32 StatId);

TArray<FTypedStatWrite> TypedStatWrites;

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

/** `m_hLastWeapon` (`+0x0ea4`) and `m_hLastMeleeWeapon` (`+0x0ea8`) — the two handles both bodies
 *  reset to `0xffffffff`, and ONLY when they resolve to the weapon being dropped. The port's
 *  `FElysiumInventory::PreviousWeapon` is `m_hLastWeapon`; there is no melee twin, so it is declared
 *  here because these two bodies are its retail writers. */
FElysiumEntityHandle LastMeleeWeapon;   // +0x0ea8 m_hLastMeleeWeapon

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
