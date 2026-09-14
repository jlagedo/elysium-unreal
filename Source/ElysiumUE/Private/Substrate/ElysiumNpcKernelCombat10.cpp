#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcWitness.h"

// Story 29d, family **Combat10** — the loadout, the health-percent readers, the two weapon drops,
// the scripted-discipline write, the ideal-state pre-select, the prayer pulse, the knockback
// velocity and the yaw clearance sweep. Slot 605 and everything it calls is in
// `ElysiumNpcKernelCombat10_2.cpp`; the declarations and this family's standing facts are in
// `ElysiumNpcKernelCombat10.inl`.
//
// Every body below is retail's, arm by arm and in retail's order, with the `0x10……` address of the
// arm in the comment beside it. Where this reading CORRECTS the checklist's one-line walk the arm
// is marked **CORRECTION**.

namespace
{
	// --- The `.rdata` cells this family reads, every one of them recovered ------------------------
	//
	// `.rdata` is identity-mapped off image base `0x10000000`, so each was read out of the pinned
	// `vampire.dll` at file offset `address - 0x10000000` — story 29c-1's technique, applied to the
	// eight cells the corpus does not carry.

	// `_DAT_104ce8c0` — the shared epsilon `GetCurrHealthPercent` guards its DIVISOR with.
	constexpr float GCombatEpsilon = 1e-05f;
	// `_DAT_104454c4` — the shared float zero, and `GetCurrHealthPercent`'s refusal answer.
	constexpr float GCombatZero = 0.0f;
	// `_DAT_104454c0` — 1.0, `ComputeKnockbackVelocity`'s blend parameter on the NULL-source path.
	constexpr float GCombatOne = 1.0f;
	// `_DAT_104491b4` — **0.1**, the scale applied to the source's raw attack value.
	constexpr float GKnockbackRawAttackScale = 0.1f;
	// `_DAT_1049a1d8` / `_DAT_1049a1dc` — **220** and **400**, the XY factor's two ends.
	constexpr float GKnockbackXyLow = 220.0f;
	constexpr float GKnockbackXyHigh = 400.0f;
	// `_DAT_1049a1e0` / `_DAT_1049a1e4` — **200** and **310**, the Z factor's two ends.
	constexpr float GKnockbackZLow = 200.0f;
	constexpr float GKnockbackZHigh = 310.0f;
	// `_DAT_1044eb08` — pi/180, the degrees-to-radians scale the yaw sweep opens with.
	constexpr float GDegreesToRadians = 0.01745329238474369f;
	// `_DAT_10462950` — **40.0**, the yaw sweep's FORWARD leg. Recovered; the checklist left it
	// unnamed.
	constexpr float GYawSweepForwardUnits = 40.0f;
	// `_DAT_10451acc` — **64.0**, the yaw sweep's RIGHT leg. Recovered out of the pinned image.
	// The same cell is family Schedule's melee height-difference threshold; it read UNRECOVERED
	// (0.0) there when this was written and the story's close corrected it to 64.0, so the two
	// agree now.
	constexpr float GYawSweepRightUnits = 64.0f;
	// The move probe's trace mask and its literal float argument (`102a17fa`, `102a17e6`).
	constexpr int32 GYawSweepTraceMask = 0x202400b;
	constexpr float GYawSweepTraceArg = 100.0f;
	// `_DAT_1047b868` and `_DAT_1049e890` — the prayer interval's floor and its per-pulse decrement.
	// Both are **doubles** in `.rdata` (the decompiler shows `(float)_DAT_…`), reading **0.3** and
	// **0.15**; `FElysiumFeedState` already documents "the 0.30 s floor".
	constexpr double GPrayerIntervalFloor = 0.3;
	constexpr double GPrayerIntervalStep = 0.15;

	// The `CVStatList_t` list types the `+0x13bc`/`+0x13c0` scan looks for.
	constexpr int32 GStatListTypeSheet = 0;      // the character sheet — the one this runtime stands
	constexpr int32 GStatListTypeBuff = 2;       // the buff list
	constexpr int32 GStatListTypeScripted = 3;   // the scripted list

	// The stat ids this family reads, in retail's numbering. `ElysiumSlot` carries the same numbers.
	constexpr int32 GStatFaithPoints = 0x0e;   // ElysiumSlot::FaithPoints (14)
	constexpr int32 GStatWounds = 0x0f;        // ElysiumSlot::Health (15) — damage TAKEN
	constexpr int32 GStatMaxHealth = 0x11;     // ElysiumSlot::MaxHealth (17)

	// The two fighting-item classnames, `s_item_w_fists_10585f08` and
	// `s_item_w_werewolf_attacks_10661a20`.
	const TCHAR* const GItemFists = TEXT("item_w_fists");
	const TCHAR* const GItemWerewolfAttacks = TEXT("item_w_werewolf_attacks");

	// `CSecureType`'s scramble constants, verbatim from `0x1042fde0` / `0x1042fe90`. Restated here
	// rather than shared because `ElysiumNpcKernelSenses.cpp` holds them as file statics; the two
	// copies are checked against each other by this family's suite.
	constexpr uint32 GSecureHashXor = 0x7e92476fu;
	constexpr uint32 GSecureHashMaskA = 0xa0086435u;
	constexpr uint32 GSecureHashXorA = 0x4814ade7u;
	constexpr uint32 GSecureHashAddA = 0x8c4b7d1fu;
	constexpr uint32 GSecureHashXorB = 0x16066412u;
	constexpr uint32 GSecureHashMaskB = 0x5ff79bcau;
	constexpr uint32 GSecureStoreMask = 0x068d8635u;
	constexpr uint32 GSecureStoreXorRead = 0x0ce9f66au;
	constexpr uint32 GSecureStoreAdd = 0x0ffa91d8u;
	constexpr uint32 GSecureStoreMask2 = 0x197279cau;
	constexpr uint32 GSecureStoreXorTail = 0xa641cacdu;

	// `CAI_BaseNPC.cpp`'s own source file string, for the arms that stamp it.
	const TCHAR* const GTroikaSourceFile = TEXT("AI_BaseNPCTroika.cpp");

	// This runtime's state vocabulary onto retail's `m_NPCState` ids, the same table
	// `ElysiumNpcKernelConditions.cpp` keeps as a file static. Restated because slot 460's arms are
	// written over the RAW id.
	int32 Combat10RetailStateId(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		}
		return 0;
	}

	double Combat10Now(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	// `AngleVectors` (`0x10139550`) — forward and right for Source `[pitch yaw roll]`, in THIS
	// world's axes (`bsp.source_to_unreal` negates Y). Family Positions' `RetailAngleVectors`
	// (`ElysiumNpcKernelPositions2.cpp`) is the same routine; it is a file static there, so the two
	// rows this family needs are restated rather than reached across a family boundary.
	void Combat10AngleVectors(const FVector& SourceAngles, FVector& OutForward, FVector& OutRight)
	{
		const float Pitch = FMath::DegreesToRadians(static_cast<float>(SourceAngles.X));
		const float Yaw = FMath::DegreesToRadians(static_cast<float>(SourceAngles.Y));
		const float Roll = FMath::DegreesToRadians(static_cast<float>(SourceAngles.Z));
		const float Sp = FMath::Sin(Pitch);
		const float Cp = FMath::Cos(Pitch);
		const float Sy = FMath::Sin(Yaw);
		const float Cy = FMath::Cos(Yaw);
		const float Sr = FMath::Sin(Roll);
		const float Cr = FMath::Cos(Roll);
		OutForward = FVector(Cp * Cy, -(Cp * Sy), -Sp);
		OutRight = FVector(-Sr * Sp * Cy + Cr * Sy, -(-Sr * Sp * Sy - Cr * Cy), -Sr * Cp);
	}
}

// =================================================================================================
// The typed `CVStatList_t` join — standing fact two.
// =================================================================================================

bool FElysiumNpc::HasTypedStatList(int32 ListType) const
{
	// The `+0x13bc` count / `+0x13c0` table walk, as an answer. Type 0 IS `FElysiumSheet`: story
	// 29b's shape bound `ElysiumSlot::Health` to stat `0xf` and `ElysiumSlot::MaxHealth` to stat
	// `0x11`, and `FElysiumCombatCharacter::SyncHealthFromSheet` already cites `0x1032fe60` for it.
	// Types 2 and 3 have no port container, so the walk falls through to `DAT_109f0b40`.
	return ListType == GStatListTypeSheet;
}

int32 FElysiumNpc::TypedStatValue(int32 ListType, int32 StatId) const
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

int32 FElysiumNpc::TypedStatBase(int32 ListType, int32 StatId) const
{
	// `CVStatList_t::GetBase` — the raw stored base, unclamped and without the trait layer.
	if (!HasTypedStatList(ListType))
	{
		return 0;
	}
	return Sheet.GetBase(EElysiumTraitContainer::Attributes, StatId);
}

void FElysiumNpc::TypedStatSet(int32 ListType, int32 StatId, int32 Value)
{
	TypedStatWrites.Add(FTypedStatWrite{ ListType, StatId, Value, TEXT("Set") });
	if (!HasTypedStatList(ListType))
	{
		return;   // written into the empty global; nothing else reads it
	}
	Sheet.SetBase(EElysiumTraitContainer::Attributes, StatId, Value);
}

void FElysiumNpc::TypedStatAddBase(int32 ListType, int32 StatId, int32 Delta)
{
	TypedStatWrites.Add(FTypedStatWrite{ ListType, StatId, Delta, TEXT("AddBase") });
	if (!HasTypedStatList(ListType))
	{
		return;
	}
	Sheet.AddBase(EElysiumTraitContainer::Attributes, StatId, Delta, SheetRules(), SheetEffects());
}

void FElysiumNpc::TypedStatSubBase(int32 ListType, int32 StatId, int32 Delta)
{
	TypedStatWrites.Add(FTypedStatWrite{ ListType, StatId, Delta, TEXT("SubBase") });
	if (!HasTypedStatList(ListType))
	{
		return;
	}
	// `CVStatList_t::SubBase` is `AddBase` with a negative delta, which the port's own comment says
	// a negative delta bypasses the max on — retail's `SubBase` is unclamped too.
	Sheet.AddBase(EElysiumTraitContainer::Attributes, StatId, -Delta, SheetRules(), SheetEffects());
}

void FElysiumNpc::TypedStatIncBase(int32 ListType, int32 StatId)
{
	TypedStatWrites.Add(FTypedStatWrite{ ListType, StatId, 1, TEXT("IncBase") });
	if (!HasTypedStatList(ListType))
	{
		return;
	}
	Sheet.IncBase(EElysiumTraitContainer::Attributes, StatId, SheetRules(), SheetEffects());
}

// =================================================================================================
// Slot 348 `HealthToPercent` — `CBaseCombatCharacter::HealthToPercent` `0x1032fe60`, 344 bytes.
// =================================================================================================

int32 FElysiumNpc::HealthToPercent()
{
	// The one species arm. `CNPC_VMingXiao#348` (`0x103970d0`) is slot 348's only override.
	const FElysiumNpcClassSlot* const Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), 348);
	if (Override != nullptr && SpeciesDispatchingSlot != 348
		&& FCString::Strcmp(Override->Address, TEXT("0x103970d0")) == 0)
	{
		FSpeciesDispatchScope Scope(*this, 348);
		return MingXiaoHealthToPercent();
	}

	// `1032ff24`: the type-0 list, then `GetValue(0x11)` — the CAP.
	const int32 Cap = TypedStatValue(GStatListTypeSheet, GStatMaxHealth);
	// `1032ff8f`: the same walk again, then `GetValue(0x0f)` — the accumulated WOUND counter.
	const int32 Wounds = TypedStatValue(GStatListTypeSheet, GStatWounds);
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

int32 FElysiumNpc::MingXiaoHealthToPercent()
{
	// `0x103970d0`, `CNPC_VMingXiao#348`. The decompiler lost the FPU arithmetic (it shows three bare
	// `__ftol()` calls with no operands); what the body DOES is the base formula with one extra
	// contribution folded in per attached limb, `i = 0..5` over `0x10398000(this, i)`. The two stat
	// reads, their order and the scope-trace string are the base's — retail even reuses
	// `CBaseCombatCharacter::HealthToPercent` as this override's trace name.
	const int32 Cap = TypedStatValue(GStatListTypeSheet, GStatMaxHealth);
	const int32 Wounds = TypedStatValue(GStatListTypeSheet, GStatWounds);
	int32 Limbs = 0;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		if (MingXiaoLimbPresent(Index))
		{
			++Limbs;
		}
	}
	if (Cap == 0)
	{
		return 0;   // the same crash guard as the base
	}
	// The limb term LOWERS the reported percent — the regrown-limb count is added to the wounds.
	// With every limb absent (this runtime's seam) the arm equals the base, which is retail's own
	// answer for an intact boss.
	return ((Cap - (Wounds + Limbs)) * MaxHealth) / Cap;
}

bool FElysiumNpc::MingXiaoLimbPresent(int32 /*LimbIndex*/) const
{
	// SEAM for `0x10398000(this, i)`. No port system stands Ming Xiao's severable limbs.
	return false;
}

// =================================================================================================
// `CNPC_VVampireBoss::GetCurrHealthPercent` `0x103c6830`, 356 bytes, no slot.
// =================================================================================================

float FElysiumNpc::GetCurrHealthPercent() const
{
	// `103c68db`: the type-0 list, then `GetValue(0x0f)` FIRST — the numerator, the wound counter.
	const int32 Wounds = TypedStatValue(GStatListTypeSheet, GStatWounds);
	// `103c694d`: the same walk again, then `GetValue(0x11)` SECOND — the denominator, the cap.
	const int32 Cap = TypedStatValue(GStatListTypeSheet, GStatMaxHealth);
	// `103c6964`: `ABS((float)cap)` against `_DAT_104ce8c0`. The decompiler's
	// `(a < eps) == (a == eps)` idiom is true exactly when both are false, i.e. `a > eps` — so this
	// is a divide-by-zero guard on the DIVISOR and not a test on the numerator.
	if (FMath::Abs(static_cast<float>(Cap)) > GCombatEpsilon)
	{
		return static_cast<float>(Wounds) / static_cast<float>(Cap);
	}
	return GCombatZero;   // `_DAT_104454c4`
}

// =================================================================================================
// Slots 304 / 305 — the fighting-item loadout.
// =================================================================================================

bool FElysiumNpc::InventoryFindByClassname(const TCHAR* Classname) const
{
	// `CBaseCombatCharacter::Inventory_Find(classname)` — ordinary carried slots, case-insensitive.
	return Inventory.FindOrdinary(*this, FString(Classname)) != nullptr;
}

bool FElysiumNpc::GiveNamedFightingItem(const TCHAR* Classname)
{
	// `thunk_FUN_1021fe50(this, classname, 0)` — `0x1021fe50`: `Weapon_Create`, the init
	// `0x10258620`, `Inventory_Can_Insert`, then the owner's slot `+0x5fc` `Weapon_Equip` AND —
	// because the third argument is `0` — its slot `+0x610` `Weapon_Switch(item, 0)`.
	//
	// `FElysiumInventory::GiveNamedItem` is the first half; the switch is the second and is NOT
	// implied by it, because `Equip` only makes a WIELDABLE item active and no shipped `item_w_*`
	// record authors `is_wieldable`. Without it a second `GiveBaseFightingItems` on the same body
	// would find the capability word still Unarmed and grant a second pair of fists, which retail's
	// slot-307 gate refuses.
	const FElysiumEntityHandle Granted = Inventory.GiveNamedItem(*this, FString(Classname));
	if (!Granted.IsSet())
	{
		return false;
	}
	FElysiumEntity* const Created = World != nullptr ? World->Resolve(Granted) : nullptr;
	if (FElysiumItem* const Item = Created != nullptr ? Created->AsItem() : nullptr)
	{
		Inventory.SetActiveWeapon(*this, *Item);
	}
	return true;
}

bool FElysiumNpc::RemoveNamedFightingItem(const TCHAR* Classname)
{
	// `thunk_FUN_1021fee0(this, classname)` — `0x1021fee0`: `Inventory_Find`, then
	// `Inventory_Remove` and `UTIL_Remove` on the item. A miss does nothing at all.
	FElysiumItem* Item = Inventory.FindOrdinary(*this, FString(Classname));
	if (Item == nullptr)
	{
		return false;
	}
	Inventory.Detach(*this, *Item);   // CBaseCombatCharacter::Inventory_Remove
	Item->Kill();                     // thunk_FUN_1024f7b0, UTIL_Remove
	return true;
}

void FElysiumNpc::GiveBaseFightingItems()
{
	// Slot 304. The one species arm is `CNPC_VWerewolf#304` (`0x103cc9b0`); `CNPC_VBaseBoss`,
	// `CNPC_VMingXiao` and the three Tzimisce classes share `0x10390fd0`, which is family Bosses'
	// row and is dispatched rather than re-ported here.
	const FElysiumNpcClassSlot* const Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), 304);
	if (Override != nullptr && SpeciesDispatchingSlot != 304
		&& FCString::Strcmp(Override->Address, TEXT("0x103cc9b0")) == 0)
	{
		FSpeciesDispatchScope Scope(*this, 304);
		WerewolfGiveBaseFightingItems();
		return;
	}

	// `CAI_BaseNPCTroika::GiveBaseFightingItems` `0x102b5b20`, 56 bytes and the whole body:
	// `102b5b25`: slot 307 `HasUsableMeleeWeapon` (`vt+0x4cc`), then `102b5b34`: slot 308
	// `HasUsableRangedWeapon` (`vt+0x4d0`). Both generated slots are stubs answering false, and the
	// port already carries the fact through the item catalogue, so the capability word is the answer
	// here exactly as `ElysiumNpcKernelSchedule.cpp`'s `HasUsableRangedWeaponPort` reads it — the
	// two retail slots are named beside it rather than dispatched into a stub.
	if (ElysiumNpcCond::WeaponCapability(*this) != ElysiumNpcCond::ECapability::Unarmed)
	{
		return;
	}
	// `102b5b47`: `thunk_FUN_1021fe50(this, "item_w_fists", 0)` then `AddMiscFlag(0x10)`.
	GiveNamedFightingItem(GItemFists);
	ElysiumMiscFlags::Set(MiscFlags, MiscFlagBaseFightingItems);
}

void FElysiumNpc::RemoveBaseFightingItems()
{
	// Slot 305, the exact inverse of the grant above.
	const FElysiumNpcClassSlot* const Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), 305);
	if (Override != nullptr && SpeciesDispatchingSlot != 305
		&& FCString::Strcmp(Override->Address, TEXT("0x103cca80")) == 0)
	{
		FSpeciesDispatchScope Scope(*this, 305);
		WerewolfRemoveBaseFightingItems();
		return;
	}

	// `CAI_BaseNPCTroika::RemoveBaseFightingItems` `0x102b5b70`, 37 bytes.
	// `102b5b75`: `GetMiscFlags() & 0x10`.
	if (!ElysiumMiscFlags::Has(MiscFlags, MiscFlagBaseFightingItems))
	{
		return;
	}
	// `102b5b85`: `thunk_FUN_1021fee0(this, "item_w_fists")` then `RemoveMiscFlag(0x10)`.
	RemoveNamedFightingItem(GItemFists);
	ElysiumMiscFlags::Clear(MiscFlags, MiscFlagBaseFightingItems);
}

void FElysiumNpc::WerewolfGiveBaseFightingItems()
{
	// `CNPC_VWerewolf::GiveBaseFightingItems` `0x103cc9b0`, 155 bytes of which 99 are the scope-trace
	// frame. The arm REPLACES the base: no slot-307 gate, no slot-308 gate, and never fists.
	// `103cca32`: `Inventory_Find("item_w_werewolf_attacks")`, and only an ABSENT item grants.
	if (InventoryFindByClassname(GItemWerewolfAttacks))
	{
		return;
	}
	// `103cca44`: `thunk_FUN_1021fe50(this, "item_w_werewolf_attacks", 0)` then `AddMiscFlag(0x10)`.
	GiveNamedFightingItem(GItemWerewolfAttacks);
	ElysiumMiscFlags::Set(MiscFlags, MiscFlagBaseFightingItems);
}

void FElysiumNpc::WerewolfRemoveBaseFightingItems()
{
	// `CNPC_VWerewolf::RemoveBaseFightingItems` `0x103cca80`, 146 bytes. Mirrors the base's SHAPE
	// with its own item: the flag gate is the base's, the classname is the Werewolf's.
	if (!ElysiumMiscFlags::Has(MiscFlags, MiscFlagBaseFightingItems))
	{
		return;
	}
	RemoveNamedFightingItem(GItemWerewolfAttacks);
	ElysiumMiscFlags::Clear(MiscFlags, MiscFlagBaseFightingItems);
}

// =================================================================================================
// Slots 385 / 386 — the two `Weapon_Drop` bodies.
// =================================================================================================

bool FElysiumNpc::WeaponIsDroppable(const FElysiumEntity* Weapon)
{
	// `thunk_FUN_102577e0(weapon)` — the item record's `is_droppable`, which the port already holds.
	const FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr;
	return Item != nullptr && Item->IsDroppable();
}

bool FElysiumNpc::WeaponIsStackSplittable(const FElysiumEntity* Weapon) const
{
	// SEAM for the weapon's own vtable `+0x5cc`, slot 385's split gate. See the CORRECTION on the
	// declaration: the count beside it is `m_iItemCount` (`+0x8d0`), so the gate is stackability.
	const FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr;
	return Item != nullptr && Item->IsStackable();
}

int32 FElysiumNpc::WeaponStackCount(const FElysiumEntity* Weapon) const
{
	const FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr;
	return Item != nullptr ? Item->ItemCount : 0;   // +0x8d0 m_iItemCount
}

void FElysiumNpc::SetWeaponStackCount(FElysiumEntity* Weapon, int32 Count)
{
	if (FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr)
	{
		Item->ItemCount = Count;
	}
}

FElysiumEntity* FElysiumNpc::WeaponCreateForDrop(const TCHAR* Classname)
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

void FElysiumNpc::NotifyWeaponDropped(FElysiumEntity* /*Weapon*/)
{
	// SEAM for the weapon's own vtable `+0x4b0` — `Drop()`. Counted.
	++WeaponDropNotifies;
}

void FElysiumNpc::WeaponDetach(FElysiumEntity* /*Weapon*/)
{
	// SEAM for `CBaseCombatCharacter::Weapon_Detach(weapon)`, the `FL_NPC` tail. Counted.
	++WeaponDetaches;
}

bool FElysiumNpc::WeaponNeedsTransmitUpdate(const FElysiumEntity* /*Weapon*/) const
{
	// SEAM for `thunk_FUN_100b5190(weapon)`. Answers false: no transmit state stands here.
	return false;
}

bool FElysiumNpc::WeaponAmmoFromWeaponData(const FElysiumEntity* /*Weapon*/, int32 /*Index*/) const
{
	// SEAM for the weapon's slot `+0x454`. False takes the `+0x450` cap arm below.
	return false;
}

int32 FElysiumNpc::WeaponAmmoCap(const FElysiumEntity* /*Weapon*/, int32 /*Index*/) const
{
	// SEAM for the weapon's slot `+0x450`. `0` is below `1`, so the entry takes retail's flat zero.
	return 0;
}

void FElysiumNpc::RerollDroppedWeaponAmmo(FElysiumEntity* Weapon)
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

void FElysiumNpc::Weapon_Drop()
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

void FElysiumNpc::Weapon_Drop(FElysiumEntity* Weapon, const FVector* /*TargetUnits*/, bool bForce)
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

// =================================================================================================
// Slot 589 `SetScriptedDiscipline` — `CAI_BaseNPCTroika::SetScriptedDiscipline` `0x102c2ec0`.
// =================================================================================================

void FElysiumNpc::SetScriptedDiscipline(int32 StatId, int32 Value)
{
	// 830 bytes only because the list lookup is inlined six times; the rule is short and the ORDER of
	// the two-step writes is the behaviour — both intermediate states are observable.
	//
	// `102c2f1c`: `base = GetBase(list3, statId)`.
	const int32 Base = TypedStatBase(GStatListTypeScripted, StatId);
	if (Value > Base)
	{
		// `102c2f9c`: the type-2 BUFF list, and only when ITS base is below the new value…
		if (TypedStatBase(GStatListTypeBuff, StatId) < Value)
		{
			// `102c3002`: …`Set(list2, statId, value)`.
			TypedStatSet(GStatListTypeBuff, StatId, Value);
		}
		// `102c3081`: `AddBase(list3, statId, value - base)` …
		TypedStatAddBase(GStatListTypeScripted, StatId, Value - Base);
		// `102c30e6`: … and THEN `Set(list3, statId, value)`. Retail's order.
		TypedStatSet(GStatListTypeScripted, StatId, Value);
		return;
	}
	if (Value < Base)
	{
		// `102c3169`: `Set(list3, statId, value)` FIRST …
		TypedStatSet(GStatListTypeScripted, StatId, Value);
		// `102c31c3`: … and THEN `SubBase(list3, statId, base - value)`. Also retail's order.
		TypedStatSubBase(GStatListTypeScripted, StatId, Base - Value);
		return;
	}
	// Equal values write nothing at all — retail falls out of both branches.
}

// =================================================================================================
// Slot 460 `PreSelectIdealState` — `CAI_BaseNPCTroika::PreSelectIdealState` `0x102ad340`.
// =================================================================================================

uint32 FElysiumNpc::SecureUnhashLevel(uint32 Value)
{
	// `0x1042fe90`, verbatim. C precedence: `&` binds tighter than `^`.
	return Value
		^ ((((Value & GSecureHashMaskA) ^ GSecureHashXorA) + GSecureHashAddA) ^ GSecureHashXorB)
			& GSecureHashMaskB
		^ GSecureHashXor;
}

uint32 FElysiumNpc::SecureUnscrambleLevel(uint32 Stored)
{
	// `102ad4d9`'s inline unscramble of `+0x6364`, the argument `0x1042fe90` is handed.
	return ((((Stored & GSecureStoreMask) ^ GSecureStoreXorRead) + GSecureStoreAdd)
		& GSecureStoreMask2) ^ Stored ^ GSecureStoreXorTail;
}

int32 FElysiumNpc::ForcedNpcState() const
{
	// `m_eForcedState` (`+0x65cc`). See standing fact three: `0x102ae840` stores a raw `NPC_STATE`
	// here, not an order id, and this slot is its only consumer.
	return ScriptedScheduleOrder.RetailOrderId;
}

void FElysiumNpc::ClearForcedNpcState()
{
	ScriptedScheduleOrder.RetailOrderId = 0;
}

int32 FElysiumNpc::PreSelectIdealStateRetail()
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// `102ad349`: `+0x1b38 = 2` unconditionally at entry — the state-change record's reason word.
	// The shape map records `+0x1b38` ABSENT ("the mind's transition trace carries the same
	// account"), so the record is a trace row rather than three stores.
	RecordScheduleEvent(TEXT("PreSelectIdealState reason 2"));

	// --- Arm 1: `m_eForcedState` --------------------------------------------------------------
	// `102ad34f`: a non-zero forced state is copied into `m_IdealNPCState`, the forced word is
	// cleared, and it is RETURNED — nothing below runs.
	if (const int32 Forced = ForcedNpcState(); Forced != 0)
	{
		Mind.RequestDesiredState(Forced, 0x4495);
		ClearForcedNpcState();
		RecordScheduleEvent(FString::Printf(TEXT("PreSelectIdealState %s:%d -> forced %d"),
			GTroikaSourceFile, 0x4495, Forced));
		return Forced;
	}

	// --- The cover timer ----------------------------------------------------------------------
	// `102ad37a`: `HasInterruptCondition(0x46 SEE_ENEMY)` AND `m_flCanSeekCoverTimer == 0.0`
	// arms it; otherwise `HasCondition(0x48 ENEMY_OCCLUDED)` ZEROES it. Note the asymmetry — the
	// arming term is an INTERRUPT condition and the clearing term is a plain one.
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds, EElysiumNpcCond::SeeEnemy)
		&& CanSeekCoverTimer == 0.0)
	{
		// `102ad3a2`: `curtime + RandomFloat(1.0, 3.0)` (`0x3f800000`, `0x40400000`).
		CanSeekCoverTimer = Combat10Now(*this)
			+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(1.0f, 3.0f);
	}
	else if (Conds.Has(EElysiumNpcCond::EnemyOccluded))
	{
		CanSeekCoverTimer = 0.0;
	}

	// --- Arms 2 and 3: the two flee helpers ---------------------------------------------------
	// `102ad3c6` / `102ad3d8`: `0x102ad260` then `0x102ad2d0`, each of which WRITES
	// `m_IdealNPCState` itself and whose non-zero answer means "it already did". Family Conditions'
	// `RequestFleeDesiredState` is that pair; dispatched, not re-ported.
	if (const int32 Flee = RequestFleeDesiredState(EElysiumNpcCond::SupernaturalFleeLevel, 0x4477);
		Flee != 0)
	{
		return Flee;
	}
	if (const int32 Flee = RequestFleeDesiredState(EElysiumNpcCond::CriminalFleeLevel, 0x447d);
		Flee != 0)
	{
		return Flee;
	}

	// --- Arm 4: `COND_SEE_FEAR` ---------------------------------------------------------------
	// `102ad3ef`. **CORRECTION**: the checklist's walk reads the flag write as an alternative to the
	// ideal-state write ("when `m_NPCState` is not 8 OR `0x100` into `m_bfAINPCFlags`"). The body is
	// a nested `if`: the flag is armed ONLY while the state is not already 8, and the ideal state is
	// then written UNCONDITIONALLY.
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds, EElysiumNpcCond::SeeFear))
	{
		if (Combat10RetailStateId(Mind.State()) != 8)
		{
			NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);   // 0x100
		}
		Mind.RequestDesiredState(8, 0x44b8);
		RecordScheduleEvent(FString::Printf(TEXT("PreSelectIdealState %s:%d -> 8"),
			GTroikaSourceFile, 0x44b8));
		return 8;
	}

	// --- Arm 5: `COND_SUPERNATURAL_ATTACK_LEVEL` ----------------------------------------------
	// `102ad429`.
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
		EElysiumNpcCond::SupernaturalAttackLevel))
	{
		Mind.RequestDesiredState(2, 0x44be);
		RecordScheduleEvent(FString::Printf(TEXT("PreSelectIdealState %s:%d -> 2"),
			GTroikaSourceFile, 0x44be));
		return 2;
	}

	// --- Arm 6: `COND_CRIMINAL_ATTACK_LEVEL` --------------------------------------------------
	// `102ad459`. No condition at all → return 0 WITHOUT touching the ideal state.
	if (!ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
		EElysiumNpcCond::CriminalAttackLevel))
	{
		return 0;
	}

	// `102ad4d9`: decode `m_iPLCriminalLevelWitnessed` (`+0x6364`) and compare it against the same
	// decode of the literal `0x3cf445af`, which evaluates to **2**. This runtime stores the
	// witnessed level PLAIN, so the decode is the identity on the port's word and the literal's
	// recovered value is the bound.
	const int32 Bound = static_cast<int32>(SecureUnhashLevel(0x3cf445afu));
	const int32 Level =
		Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Level;
	const int32 RetailState = Combat10RetailStateId(Mind.State());
	if (Level <= Bound && RetailState != 2 && RetailState != 0xe)
	{
		// `102ad513`: resolve `m_hCriminalOffender` (`+0x638c`); an offender whose slot-404
		// `IRelationType` answers `1` (`D_HT`) falls STRAIGHT to the combat tail.
		const FElysiumEntityHandle Offender =
			Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Offender;
		const FElysiumEntity* const OffenderEntity =
			World != nullptr && Offender.IsSet() ? World->Resolve(Offender) : nullptr;
		if (OffenderEntity != nullptr && IRelationTypeOf(OffenderEntity) == 1)
		{
			Mind.RequestDesiredState(2, 0x44cb);
			RecordScheduleEvent(FString::Printf(TEXT("PreSelectIdealState %s:%d -> 2"),
				GTroikaSourceFile, 0x44cb));
			return 2;
		}
		// `102ad578`: otherwise `m_bAllowCriminalSuspicion` (`+0x65f8`) opens the suspicion window.
		if (Witness.bAllowCriminalSuspicion)
		{
			SubState = 0;   // +0x63f8 m_iSubState
			Mind.RequestDesiredState(0xe, 0x44d3);
			RecordScheduleEvent(FString::Printf(TEXT("PreSelectIdealState %s:%d -> 0xe"),
				GTroikaSourceFile, 0x44d3));
			return 0xe;
		}
	}
	// `102ad58a`: the shared tail — combat, line `0x44cb`. Reached when the level is above the
	// bound, when the state is already 2 or 0xe, when the offender does not resolve or is not hated,
	// and when suspicion is not allowed.
	Mind.RequestDesiredState(2, 0x44cb);
	RecordScheduleEvent(FString::Printf(TEXT("PreSelectIdealState %s:%d -> 2"),
		GTroikaSourceFile, 0x44cb));
	return 2;
}

EElysiumNpcState FElysiumNpc::PreSelectIdealState()
{
	// The generated virtual's return type is this runtime's typed state, which has no member for
	// retail `0` (no change), `8` (FLEE) or `0xe` (the criminal-suspicion window) — the same gap
	// `FElysiumNpcMind::RequestDesiredState` was written for. The RAW answer is the deliverable and
	// is kept beside the typed one; every arm above has already written `m_IdealNPCState` through
	// the mind, exactly as retail's arms write the word directly.
	LastPreSelectIdealStateRetail = PreSelectIdealStateRetail();
	return LastPreSelectIdealStateRetail == 2 ? EElysiumNpcState::Combat : Mind.IdealState();
}

// =================================================================================================
// Slot 357 — the prayer pulse, `0x1033b5f0`, 354 bytes.
// =================================================================================================

int32 FElysiumNpc::FaithPointsMaximum() const
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

void FElysiumNpc::RunPrayerPulse(double Now)
{
	// `1033b673`: the type-0 list, then `GetValue(0x0e FaithPoints)`.
	const int32 Faith = TypedStatValue(GStatListTypeSheet, GStatFaithPoints);
	const int32 Maximum = FaithPointsMaximum();
	if (Faith < Maximum)
	{
		// `1033b6c8`: `IncBase(0x0e)` on the same list.
		TypedStatIncBase(GStatListTypeSheet, GStatFaithPoints);
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

void FElysiumNpc::Slot357()
{
	// `1033b5f0`: the two gates, both of which must pass — `m_Activity == 0x132` AND
	// `m_flNextFeedPulse < curtime`.
	//
	// **SEAM**: family Hints' `CurrentRetailActivityId()` stands for `m_Activity` (`+0xfec`) and
	// answers `-1`, so the gate refuses — which is retail's own answer for a body that is not
	// praying, and the reason nothing pulses today. `RunPrayerPulse` below the gate is the recovered
	// half and is what the suite drives.
	const double Now = Combat10Now(*this);
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

// =================================================================================================
// `FUN_102a0290` — the knockback velocity builder, 400 bytes, no slot.
// =================================================================================================

float FElysiumNpc::SourceRawAttackValue(const FElysiumEntity* /*Source*/) const
{
	// SEAM for `CBaseCombatCharacter::GetRawAttackValue(source)`. `0.0` puts the blend parameter at
	// the LOW end of both curves (220 / 200), which is the arm a zero-strength attacker takes.
	return 0.0f;
}

bool FElysiumNpc::SourceWeaponIdIsKnockbackFamily(const FElysiumEntity* /*Source*/) const
{
	// SEAM for `thunk_FUN_10344da0(source->+0x3ec)` — "is the source's weapon id in 139..147". This
	// runtime keys a weapon by classname and stands no retail weapon id.
	return false;
}

FVector FElysiumNpc::SourceAbsVelocityUnits(FElysiumEntity* Source) const
{
	// `+0x3bc..+0x3c4`, with `CBaseEntity::CalcAbsoluteVelocity` first when `m_iEFlags` bit 12 is
	// set. The dirty-flag recompute has no counterpart here; the velocity does.
	return Source != nullptr ? Source->Velocity : FVector::ZeroVector;
}

void FElysiumNpc::ComputeKnockbackVelocity(FElysiumEntity* Source)
{
	// `102a0296`: `local_10 = 5.0`. DEAD on the null-source path — it is only read through
	// `t = raw * 0.1`, which that path does not take.
	float Raw = 5.0f;
	FVector Knockback = FVector::ZeroVector;
	bool bCopiedSourceVelocity = false;

	if (Source == nullptr)
	{
		// `102a02a7`: slot 219 `GetAbsAngles` (`vt+0x36c`), converted to a forward vector
		// (`0x10139550`) straight into `+0x6004`, then all three components NEGATED. Family Motor10's
		// `RetailGetAnglesDegrees` is the angles accessor (its own citation is slot 221 `GetAngles`,
		// which answers the same word on this substrate).
		FVector ForwardAxis = FVector::ZeroVector;
		FVector RightAxis = FVector::ZeroVector;
		Combat10AngleVectors(RetailGetAnglesDegrees(), ForwardAxis, RightAxis);
		Knockback = -ForwardAxis;
	}
	else
	{
		// `102a02ee`: `GetRawAttackValue(source)`.
		Raw = SourceRawAttackValue(Source);
		if (SourceWeaponIdIsKnockbackFamily(Source))
		{
			// `102a0307`: the raw value is FORCED to 1.0 and the velocity is copied verbatim from
			// the source, skipping the origin subtraction entirely.
			Raw = 1.0f;
			Knockback = SourceAbsVelocityUnits(Source);
			bCopiedSourceVelocity = true;
		}
		else
		{
			// `102a0348`: slot 217 `GetAbsOrigin` on the source and on this body; the vector is
			// THIS body's origin minus the source's. SOURCE units, as the field is.
			Knockback = (Origin - Source->Origin) / ElysiumMove::U;
		}
	}
	(void)bCopiedSourceVelocity;

	// `102a0395`: Z is ZEROED, then the whole vector is normalised (`0x10137220`).
	Knockback.Z = 0.0;
	Knockback.Normalize();

	// `102a03a2`: the blend parameter — the bare `1.0` on the null-source path, `raw * 0.1`
	// otherwise.
	const float T = Source == nullptr ? GCombatOne : Raw * GKnockbackRawAttackScale;
	const float XyFactor = (GKnockbackXyHigh - GKnockbackXyLow) * T + GKnockbackXyLow;
	const float ZFactor = (GKnockbackZHigh - GKnockbackZLow) * T + GKnockbackZLow;

	// `102a03d2`: X, then Z, then Y are each multiplied by the XY factor…
	Knockback.X *= XyFactor;
	Knockback.Z *= XyFactor;
	Knockback.Y *= XyFactor;
	// `102a03f3`: …and Z is then REPLACED outright by the Z factor, so its multiply is dead.
	Knockback.Z = ZFactor;

	KnockbackVelocity = Knockback;   // +0x6004 m_KnockbackVelocity, SOURCE units per second
}

// =================================================================================================
// `FUN_102a1650` — the yaw clearance sweep, 455 bytes, no slot.
// =================================================================================================

bool FElysiumNpc::TraceMoveClearanceAtYaw(int32 UnusedArg, float YawDegrees, float Reach) const
{
	(void)UnusedArg;   // read by no arm; `102a1650` never touches `[ESP+0x8c]`

	// `102a1656`: the yaw times `_DAT_1044eb08` (pi/180), then `FSINCOS`.
	const float Radians = YawDegrees * GDegreesToRadians;
	const float Cos = FMath::Cos(Radians);
	const float Sin = FMath::Sin(Radians);

	// `102a16a1`: slot 217 `GetAbsOrigin` (`vt+0x364`) is the sweep START. SOURCE units, as every
	// retail vector here is.
	const FVector StartUnits = Origin / ElysiumMove::U;
	// `102a16ab`–`102a17b5`: end = start + cos * m_vecForward * 40 * reach
	//                                   + sin * m_vecRight   * 64 * reach.
	//
	// `m_vecForward` (`+0x6290`) and `m_vecRight` (`+0x629c`) are retail's CACHED facing basis. The
	// port declares both words and writes neither — "this runtime recomputes it per query" — so they
	// are recomputed from this body's own angles here rather than read as two zero vectors, which
	// would collapse the sweep to a point and refuse nothing.
	FVector ForwardAxis = FVector::ZeroVector;
	FVector RightAxis = FVector::ZeroVector;
	Combat10AngleVectors(Angles, ForwardAxis, RightAxis);
	const FVector EndUnits = StartUnits
		+ ForwardAxis * (Cos * GYawSweepForwardUnits * Reach)
		+ RightAxis * (Sin * GYawSweepRightUnits * Reach);

	FElysiumNpc* const Mutable = const_cast<FElysiumNpc*>(this);
	Mutable->LastYawClearanceSweep = FYawClearanceSweep{ StartUnits, EndUnits, YawDegrees, Reach,
		GYawSweepTraceMask };

	// `102a17f0`: a 14-word `trace_t` is zeroed and `m_pMoveProbe` (`+0x5d40`) sweeps it through
	// `0x102e6d70` with mask `0x202400b` and the literal `100.0`; `RET 0xc` returns whatever the
	// probe left in `AL`, which is why all four call sites read the byte.
	//
	// **SEAM**: family Motor's `KernelHullTrace` is the standing hull seam and reports a CLEAR
	// sweep (`Fraction == 1.0`, no hit entity), so this answers TRUE — the admitting arm, the one
	// that lets the task run. `100.0` (`_DAT_…`, the literal at `102a17e6`) is the probe's own
	// argument and has no counterpart in the port's hull call; it is carried in the record above.
	FKernelHullTrace Trace;
	KernelHullTrace(StartUnits, EndUnits, HullMinsUnits(false), HullMaxsUnits(false),
		GYawSweepTraceMask, Trace);
	return Trace.Fraction >= 1.f && !Trace.HitEntity.IsSet();
}
