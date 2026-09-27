// `CAI_BaseNPC`'s bodies of the `Combat10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseCombat10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcCombat10Shared.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcWitness.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `_DAT_1047b868` and `_DAT_1049e890` — the prayer interval's floor and its per-pulse decrement.
	// Both are **doubles** in `.rdata` (the decompiler shows `(float)_DAT_…`), reading **0.3** and
	// **0.15**; `FElysiumFeedState` already documents "the 0.30 s floor".
	constexpr double GPrayerIntervalFloor = 0.3;
	constexpr double GPrayerIntervalStep = 0.15;
	// The stat ids this family reads, in retail's numbering. `ElysiumSlot` carries the same numbers.
	constexpr int32 GStatFaithPoints = 0x0e;   // ElysiumSlot::FaithPoints (14)
}

// --- Moved from `ElysiumNpcCombat10.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::HasTypedStatList(int32 ListType) const
{
	// The `+0x13bc` count / `+0x13c0` table walk, as an answer. Type 0 IS `FElysiumSheet`: story
	// 29b's shape bound `ElysiumSlot::Health` to stat `0xf` and `ElysiumSlot::MaxHealth` to stat
	// `0x11`, and `FElysiumCombatCharacter::SyncHealthFromSheet` already cites `0x1032fe60` for it.
	// Types 2 and 3 have no port container, so the walk falls through to `DAT_109f0b40`.
	return ListType == NpcKernelCombat10Shared::GStatListTypeSheet;
}

int32 FElysiumNpcBase::TypedStatValue(int32 ListType, int32 StatId) const
{
	// `CVStatList_t::GetValue` (`0x102012d0`): the stored value clamped to the stat's own Min/Max —
	// which is what `FElysiumSheet::GetCurrent` is, the base with the trait layer applied and the
	// authored bounds enforced.
	if (!HasTypedStatList(ListType))
	{
		return 0;   // DAT_109f0b40, the empty lazily-built global
	}
	return Sheet.GetCurrent(EElysiumTraitContainer::Attributes, StatId);
}

int32 FElysiumNpcBase::HealthToPercent()
{
	// `1032ff24`: the type-0 list, then `GetValue(0x11)` — the CAP.
	const int32 Cap = TypedStatValue(NpcKernelCombat10Shared::GStatListTypeSheet, NpcKernelCombat10Shared::GStatMaxHealth);
	// `1032ff8f`: the same walk again, then `GetValue(0x0f)` — the accumulated WOUND counter.
	const int32 Wounds = TypedStatValue(NpcKernelCombat10Shared::GStatListTypeSheet, NpcKernelCombat10Shared::GStatWounds);
	// `1032ffa8`: `((cap - wounds) * m_iMaxHealth) / cap`, an INTEGER divide.
	if (Cap == 0)
	{
		// CRASH GUARD, not a rule: retail divides by the cap unguarded and faults on a character
		// whose type-0 list answers zero (which is every character in a world with no `stats.txt`).
		// Named in the answer; no arm of retail can be reached through it.
		return 0;
	}
	return ((Cap - Wounds) * MaxHealth) / Cap;
}

bool FElysiumNpcBase::WeaponAmmoFromWeaponData(const FElysiumEntity* /*Weapon*/, int32 /*Index*/) const
{
	// SEAM for the weapon's slot `+0x454`. False takes the `+0x450` cap arm below.
	return false;
}

int32 FElysiumNpcBase::WeaponAmmoCap(const FElysiumEntity* /*Weapon*/, int32 /*Index*/) const
{
	// SEAM for the weapon's slot `+0x450`. `0` is below `1`, so the entry takes retail's flat zero.
	return 0;
}

void FElysiumNpcBase::RerollDroppedWeaponAmmo(FElysiumEntity* Weapon)
{
	// `1032cf0a`–`1032cf8c`: `i = 0` and `1` (`iVar9` steps by `0x1093c` while below `0x21278`, which
	// is exactly two iterations), writing `weapon+0x74c + 4*i`.
	FWeaponAmmoReroll Roll;
	Roll.Weapon = Weapon != nullptr ? Weapon->Handle : FElysiumEntityHandle::Invalid();
	FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (WeaponAmmoFromWeaponData(Weapon, Index))
		{
			// `1032cf1e`: `thunk_FUN_102517b0(weapon)` then
			// `RandomInt(1, wpndata+0x2674 + i*0x1093c)`.
			const int32 Cap = WeaponAmmoCap(Weapon, Index);
			Roll.bFromWeaponData[Index] = true;
			Roll.Cap[Index] = Cap;
			Roll.Entry[Index] = Rng.RandRange(1, Cap);
			continue;
		}
		// `1032cf3a`: the weapon's slot `+0x450` supplies the cap.
		const int32 Cap = WeaponAmmoCap(Weapon, Index);
		Roll.Cap[Index] = Cap;
		// `1032cf4a`: a cap below 1 writes a FLAT ZERO, it does not roll.
		Roll.Entry[Index] = Cap < 1 ? 0 : Rng.RandRange(1, Cap);
	}
	WeaponAmmoRerolls.Add(Roll);
}

void FElysiumNpcBase::Weapon_Drop()
{
	// Slot 386 — `CAI_BaseNPC::Weapon_Drop()` `0x1032ce40`, 511 bytes, no species override.
	// `1032cec2`: `m_bCantDropWeapons` (`+0x1589`) clear, a non-null `GetActiveWeapon`, and
	// `thunk_FUN_102577e0` on it. All three, in that order.
	if (bCantDropWeapons)
	{
		return;
	}
	FElysiumEntity* const Weapon = ActiveWeaponEntity();
	if (Weapon == nullptr || !WeaponIsDroppable(Weapon))
	{
		return;
	}
	// `1032cefa`: `GetFlags() & FL_NPC (0x2000)` — an NPC body REROLLS the weapon's ammo first.
	const bool bIsNpcBody = true;   // every body on this line is an NPC; `FL_NPC` is set at spawn
	if (bIsNpcBody)
	{
		RerollDroppedWeaponAmmo(Weapon);
	}
	// `1032cf94`: `thunk_FUN_100b5190(weapon)` gates the weapon's slot `+0x138`.
	if (WeaponNeedsTransmitUpdate(Weapon))
	{
		// the transmit-state update; no port surface, and the gate above never admits it today
	}
	// `1032cfa8` / `1032cfeb`: `m_hLastWeapon` (`+0xea4`) and `m_hLastMeleeWeapon` (`+0xea8`) are
	// each reset to `0xffffffff` ONLY when they resolve to THIS weapon.
	if (Inventory.PreviousWeapon == Weapon->Handle)
	{
		Inventory.PreviousWeapon = FElysiumEntityHandle::Invalid();
	}
	if (LastMeleeWeapon == Weapon->Handle)
	{
		LastMeleeWeapon = FElysiumEntityHandle::Invalid();
	}
	// `1032d02c`: the weapon's slot `+0x4b0`, then `Inventory_Remove`, then — only when `FL_NPC` is
	// STILL set (retail re-reads `GetFlags`) — `Weapon_Detach`.
	NotifyWeaponDropped(Weapon);
	if (FElysiumItem* Item = Weapon->AsItem())
	{
		Inventory.Detach(*this, *Item);
	}
	if (bIsNpcBody)
	{
		WeaponDetach(Weapon);
	}
}

void FElysiumNpcBase::Weapon_Drop(FElysiumEntity* Weapon, const FVector* /*TargetUnits*/, bool bForce)
{
	// Slot 385 — `CAI_BaseNPC::Weapon_Drop(CBaseCombatWeapon*, const Vector*, bool)` `0x1032d0c0`,
	// 431 bytes, no species override. The second argument is read by no arm of the body.
	// `1032d129`: a null weapon does nothing at all.
	if (Weapon == nullptr)
	{
		return;
	}
	// `1032d13d`: the weapon IS the active weapon → the holster path, `this->vtable +0x608`, which is
	// slot 386 — this class's own no-argument `Weapon_Drop`. Dispatched, not re-ported.
	if (ActiveWeaponEntity() == Weapon)
	{
		Weapon_Drop();
		return;
	}
	// `1032d158`: droppable, OR the caller forces it.
	if (!WeaponIsDroppable(Weapon) && !bForce)
	{
		return;
	}
	// `1032d16d`: the STACK SPLIT — the weapon's slot `+0x5cc` and `m_iItemCount` (`+0x8d0`) at two
	// or more. See the CORRECTION on the declaration: this is not an ammo test.
	if (WeaponIsStackSplittable(Weapon) && WeaponStackCount(Weapon) >= 2)
	{
		// `1032d27a`: decrement, `GetClassname` off the ORIGINAL (its vtable `+0x570`),
		// `Weapon_Create`, `thunk_FUN_10258620`, then the NEW weapon's slot `+0x4b0` — and RETURN.
		SetWeaponStackCount(Weapon, WeaponStackCount(Weapon) - 1);
		const FElysiumItem* const Item = Weapon->AsItem();
		FElysiumEntity* const Spawned =
			WeaponCreateForDrop(Item != nullptr ? *Item->ClassName() : nullptr);
		if (Spawned != nullptr)
		{
			NotifyWeaponDropped(Spawned);
			return;
		}
		// `1032d2b2`: a FAILED create falls straight out of the body. The original keeps its
		// decremented count and is NOT removed — retail's own arm, reproduced.
		return;
	}
	// `1032d18e` / `1032d1d1`: the same two handle resets as slot 386.
	if (Inventory.PreviousWeapon == Weapon->Handle)
	{
		Inventory.PreviousWeapon = FElysiumEntityHandle::Invalid();
	}
	if (LastMeleeWeapon == Weapon->Handle)
	{
		LastMeleeWeapon = FElysiumEntityHandle::Invalid();
	}
	// `1032d212`: the weapon's slot `+0x4b0`, `Inventory_Remove`, and `Weapon_Detach` under `FL_NPC`.
	NotifyWeaponDropped(Weapon);
	if (FElysiumItem* Item = Weapon->AsItem())
	{
		Inventory.Detach(*this, *Item);
	}
	WeaponDetach(Weapon);
}

void FElysiumNpcBase::Slot357()
{
	// `1033b5f0`: the two gates, both of which must pass — `m_Activity == 0x132` AND
	// `m_flNextFeedPulse < curtime`.
	//
	// **SEAM**: family Hints' `CurrentRetailActivityId()` stands for `m_Activity` (`+0xfec`) and
	// answers `-1`, so the gate refuses — which is retail's own answer for a body that is not
	// praying, and the reason nothing pulses today. `RunPrayerPulse` below the gate is the recovered
	// half and is what the suite drives.
	const double Now = NpcKernelCombat10Shared::Combat10Now(*this);
	if (CurrentRetailActivityId() != PrayerActivityId)
	{
		return;
	}
	if (static_cast<double>(FeedState.NextPulse) >= Now)
	{
		return;
	}
	RunPrayerPulse(Now);
}

// --- Moved from `ElysiumNpcCombat10.cpp` (story 5 step 5) ---

void FElysiumNpcBase::TypedStatIncBase(int32 ListType, int32 StatId)
{
	TypedStatWrites.Add(FTypedStatWrite{ ListType, StatId, 1, TEXT("IncBase") });
	if (!HasTypedStatList(ListType))
	{
		return;
	}
	Sheet.IncBase(EElysiumTraitContainer::Attributes, StatId, SheetRules(), SheetEffects());
}

bool FElysiumNpcBase::WeaponIsDroppable(const FElysiumEntity* Weapon)
{
	// `thunk_FUN_102577e0(weapon)` — the item record's `is_droppable`, which the port already holds.
	const FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr;
	return Item != nullptr && Item->IsDroppable();
}

bool FElysiumNpcBase::WeaponIsStackSplittable(const FElysiumEntity* Weapon) const
{
	// SEAM for the weapon's own vtable `+0x5cc`, slot 385's split gate. See the CORRECTION on the
	// declaration: the count beside it is `m_iItemCount` (`+0x8d0`), so the gate is stackability.
	const FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr;
	return Item != nullptr && Item->IsStackable();
}

int32 FElysiumNpcBase::WeaponStackCount(const FElysiumEntity* Weapon) const
{
	const FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr;
	return Item != nullptr ? Item->ItemCount : 0;   // +0x8d0 m_iItemCount
}

void FElysiumNpcBase::SetWeaponStackCount(FElysiumEntity* Weapon, int32 Count)
{
	if (FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr)
	{
		Item->ItemCount = Count;
	}
}

FElysiumEntity* FElysiumNpcBase::WeaponCreateForDrop(const TCHAR* Classname)
{
	// SEAM for `CBaseCombatCharacter::Weapon_Create(classname)` + `thunk_FUN_10258620`. The port's
	// creation route is the world's entity factory; a headless world with no item catalogue answers
	// null, which is retail's own null arm.
	if (World == nullptr || Classname == nullptr || *Classname == TEXT('\0'))
	{
		return nullptr;
	}
	FElysiumEntityDef SpawnDef;
	SpawnDef.Classname = FString(Classname);
	SpawnDef.Origin = Origin;
	return World->Resolve(World->SpawnRuntimeEntity(MoveTemp(SpawnDef)));
}

void FElysiumNpcBase::NotifyWeaponDropped(FElysiumEntity* /*Weapon*/)
{
	// SEAM for the weapon's own vtable `+0x4b0` — `Drop()`. Counted.
	++WeaponDropNotifies;
}

void FElysiumNpcBase::WeaponDetach(FElysiumEntity* /*Weapon*/)
{
	// SEAM for `CBaseCombatCharacter::Weapon_Detach(weapon)`, the `FL_NPC` tail. Counted.
	++WeaponDetaches;
}

bool FElysiumNpcBase::WeaponNeedsTransmitUpdate(const FElysiumEntity* /*Weapon*/) const
{
	// SEAM for `thunk_FUN_100b5190(weapon)`. Answers false: no transmit state stands here.
	return false;
}

int32 FElysiumNpcBase::FaithPointsMaximum() const
{
	// `0x10200370(&DAT_1074e658, 0xe)` then `0x10205840(def+0x60)` — the stat DEFINITION's own
	// maximum, not the list's. `FElysiumSheet::BoundsFor` is the port of that walk.
	int32 Min = 0;
	int32 Max = 0;
	Sheet.BoundsFor(EElysiumTraitContainer::Attributes, GStatFaithPoints, SheetRules(),
		SheetEffects(), Min, Max);
	// `Max < Min` is `BoundsFor`'s "unbounded" answer, which only a world with no `stats.txt` gives.
	// `0` then makes `GetValue >= max` true and the pulse takes `PrayerEnd` rather than incrementing
	// for ever, which is the safe half of retail's own split.
	return Max >= Min ? Max : 0;
}

void FElysiumNpcBase::RunPrayerPulse(double Now)
{
	// `1033b673`: the type-0 list, then `GetValue(0x0e FaithPoints)`.
	const int32 Faith = TypedStatValue(NpcKernelCombat10Shared::GStatListTypeSheet, GStatFaithPoints);
	const int32 Maximum = FaithPointsMaximum();
	if (Faith < Maximum)
	{
		// `1033b6c8`: `IncBase(0x0e)` on the same list.
		TypedStatIncBase(NpcKernelCombat10Shared::GStatListTypeSheet, GStatFaithPoints);
	}
	else
	{
		// `1033b70c`: slot `0x598` — slot 358, `PrayerEnd` `0x1033b890`. Dispatched, not re-ported.
		PrayerEnd();
	}
	// `1033b712`: `m_flNextFeedPulse = curtime + m_flNextFeedDuration`, then the interval
	// accelerates toward its floor. Both cells are DOUBLES in `.rdata`: 0.3 and 0.15. The pair IS
	// `+0x1490`/`+0x1494`, which `FElysiumFeedState` already names — retail reuses the feed cadence
	// words for the prayer pulse.
	FeedState.NextPulse = static_cast<float>(Now) + FeedState.Interval;
	// Retail narrows BOTH doubles to float before the compare and the subtract
	// (`(float)_DAT_1047b868 < (float)m_flNextFeedDuration`), so an interval sitting exactly on the
	// floor compares EQUAL and stops shrinking. Comparing in double would make `0.3f` (which is
	// 0.30000001…) strictly greater and take one step too many; the narrowing is reproduced.
	if (FeedState.Interval > static_cast<float>(GPrayerIntervalFloor))
	{
		FeedState.Interval -= static_cast<float>(GPrayerIntervalStep);
	}
}
