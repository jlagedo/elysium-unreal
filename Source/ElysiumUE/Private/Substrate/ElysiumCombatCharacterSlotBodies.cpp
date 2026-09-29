// `CBaseCombatCharacter`'s hand-written slot bodies and the helpers they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Declarations are generated in
// `ElysiumCombatCharacterSlots.inl` (a slot body) or in `ElysiumCombatCharacterSlotBodies.inl`.

#include "ElysiumPlayer.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumStanceTypes.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumCameraOverride.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAnimShared.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcBaseEntityChainShared.h"
#include "Substrate/ElysiumNpcCombat10Shared.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "Substrate/ElysiumNpcDebugShared.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcEntityChainShared.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcHintsShared.h"
#include "Substrate/ElysiumNpcKernelBaseHelpersShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSensesBodiesShared.h"
#include "Substrate/ElysiumNpcSpeciesMisc10_2Shared.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Substrate/ElysiumNpcTroikaHelpersShared.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSchedule.h"
#include "Visual/ElysiumActionTables.h"
#include "Visual/ElysiumEyeRig.h"

// --- File-scope helpers moved with the bodies (story 5 step 6) ---

namespace
{
	// `_DAT_1047b868` and `_DAT_1049e890` — the prayer interval's floor and its per-pulse decrement.
	// Both are **doubles** in `.rdata` (the decompiler shows `(float)_DAT_…`), reading **0.3** and
	// **0.15**; `FElysiumFeedState` already documents "the 0.30 s floor".
	constexpr double GPrayerIntervalFloor = 0.3;
	constexpr double GPrayerIntervalStep = 0.15;
	// The stat ids this family reads, in retail's numbering. `ElysiumSlot` carries the same numbers.
	constexpr int32 GStatFaithPoints = 0x0e;   // ElysiumSlot::FaithPoints (14)
	// `CAI_BaseNPC::GiveAmmo`'s cue and its parameters.
	constexpr const TCHAR* AmmoPickupSound = TEXT("weapons/misc/ammo_pickup.wav");
	constexpr float AmmoPickupVolume = 1.0f;      // 0x3f800000
	constexpr float AmmoPickupAttenuation = 0.8f; // 0x3f4ccccd
	constexpr int32 AmmoPickupPitch = 100;        // 0x64
	constexpr int32 AmmoSlotCount = 0x20;         // the `param_2 < 0x20` bound
	// `_DAT_1049e0c8`, a DOUBLE (`10326cbf  FCOMP double ptr [0x1049e0c8]`) with exactly ONE reader
	// in the image — slot 364. 0.994 is a 6.28-degree half-angle: the aim cone is far narrower than
	// the view cone (`0.2`, `ElysiumNpcSense::DefaultViewConeDot`), which is what makes
	// `FInAimCone` a firing test and not a seeing test.
	constexpr double GAimConeDotFloor = 0.994;
	// The pooled zeroes (`_DAT_104454c4`, and the double `_DAT_1044fab0`) and one.
	constexpr float SpeciesZero = ElysiumNpcTunables::Zero;
	// Slot 323 (`0x10344dd0`). The direction's 2-D length must clear this before an angle is taken
	// at all — a hair over zero, so the too-slow arm is only a genuinely stationary direction.
	constexpr float MoveDirectionMinLength2D = 1.0e-07f;    // _DAT_1049e028
	// Slot 323's `UTIL_AngleMod`: `(360/65536) * (ftol(a * 65536/360) & 0xffff)`.
	constexpr float AngleModScale = ElysiumNpcTunables::AngleQuantum;   // 360 / 65536
	constexpr float GSpeciesDegreesPerTurn = 360.0f;                // _DAT_10450568
	// Slot 323's four band boundaries. **316, not 315** — read out of the image; the bands are 89,
	// 90, 90 and 91 degrees wide and that asymmetry is retail's.
	constexpr float MoveDirectionAheadBand = ElysiumNpcTunables::FortyFive;
	constexpr float MoveDirectionLeftBand = 135.0f;         // _DAT_1049e8a0
	constexpr float MoveDirectionBehindBand = 225.0f;       // _DAT_1049e89c
	constexpr float MoveDirectionRightBand = 316.0f;        // _DAT_1049e8a4
	// `UTIL_VecToYaw` `0x101d2c70` over a delta in THIS world's axes, whose Y is the negated Source
	// one (`bsp.source_to_unreal`). Families Bosses, Facing and Positions each keep an identical
	// private copy; this is the fourth, for the same file-ownership reason.
	float SpeciesVecToYaw(const FVector& PortDelta)
	{
		if (PortDelta.X == 0.0 && PortDelta.Y == 0.0)
		{
			return SpeciesZero;
		}
		float Yaw = FMath::RadiansToDegrees(
			static_cast<float>(FMath::Atan2(-PortDelta.Y, PortDelta.X)));
		if (Yaw < SpeciesZero)
		{
			Yaw += GSpeciesDegreesPerTurn;
		}
		return Yaw;
	}
	// `UTIL_AngleMod` — the `ftol`/mask/scale triple slot 323 folds its relative yaw through.
	// Reproduced as the INTEGER pipeline retail runs, not as an `fmod`: the truncation toward zero
	// and the 16-bit mask are what make 360.0 come back as 0.0 and a negative angle come back
	// positive, and an `fmod` would answer differently at the boundaries this body compares on.
	float SpeciesAngleMod(float Degrees)
	{
		const int32 Fixed = static_cast<int32>(Degrees * (1.0f / AngleModScale));
		float Modded = static_cast<float>(static_cast<uint32>(Fixed) & 0xffffu) * AngleModScale;
		// `if (fVar1 < _DAT_104454c4) fVar1 = fVar1 + _DAT_10450568;` — dead after the mask, which
		// cannot produce a negative, and reproduced because retail carries it.
		if (Modded < SpeciesZero)
		{
			Modded += GSpeciesDegreesPerTurn;
		}
		return Modded;
	}
}

// --- Moved from `ElysiumNpcBaseAnim.cpp` (story 5 step 6) ---

// --- The pose parameter (slot 345) --------------------------------------------------------------

float FElysiumCombatCharacter::SetPoseParameter(const TCHAR* Name, float Value, bool bUpdatePoseControls)
{
	// `0x1032fb80`, slot 345. Retail's body is a VPROF scope push, `LookupPoseParameter(name)`, a
	// dispatch through vtable +0x568 — slot 346, the INDEX overload — and the scope pop. The scope
	// is a diagnostic this runtime replaces with its own channels; the lookup and the dispatch are
	// the behaviour.
	//
	// The write is ALSO recorded on family Facing's `PoseParameterWrites`, which is this runtime's
	// one pose-parameter surface: slot 346 is still 29c's stub, so without this the write would
	// vanish and a reader asking "what did this body aim" would see nothing.
	SetPoseParameterByName(Name, Value);
	return SetPoseParameter(LookupPoseParameter(Name), Value, bUpdatePoseControls);
}

// --- Moved from `ElysiumNpcBaseClosure.cpp` (story 5 step 6) ---

void* FElysiumCombatCharacter::GetPredDescMap()
{
	// `0x10321670` -> `&datamap_CBaseCombatCharacter_10619d10`. REFUSAL: no datamap; the port's
	// save mechanism is `FElysiumSaveArchive`, a per-type `Serialize`, not a descriptor table.
	++ClosureRefusals.PredDescMap;
	return nullptr;
}

float FElysiumCombatCharacter::SetPoseParameter(int32 Index, float Value, bool bWrap)
{
	// `CBaseCombatCharacter::SetPoseParameter`, 413 bytes, 85 classes — the stock SDK LOOPING
	// pose-parameter setter. It walks the two-entry registry at `m_flSet_PoseParameters`, and if the
	// asked-for index is one of them it stores the value and, when `bWrap` is set and the model
	// resolves, wraps it using that pose parameter's own bounds (`+0x8`, `+0xc`, `+0x10` off the
	// `mstudioposeparamdesc_t`) and the fixed SDK wrap fraction `_DAT_10449270`. An index that is
	// NOT in the registry falls through the loop to `CBaseAnimating::SetPoseParameter02` — slot 260,
	// `0x10091fe0` — which is the ordinary non-looping setter.
	//
	// **The fall-through is the arm this runtime can take, and it is retail's own.** The registry is
	// filled from the model's studio header, this substrate's animating tier stands no
	// `studiohdr_t` and therefore no pose-parameter descriptors, so no index is ever a registered
	// looping parameter and every call takes the miss. That is a refusal of the WRAP, not of the
	// write: the value still goes where retail sends it on a miss.
	(void)bWrap;
	++ClosureRefusals.LoopingPoseParameter;
	return SetPoseParameter02(Index, Value);
}

bool FElysiumCombatCharacter::FInViewCone(const FVector& PointCm)
{
	// `0x10326a20` -> `FElysiumNpcSenses::IsInViewCone(Npc, point)`, the base 3-D apex test. The
	// scope-trace push and pop around it are the crash-report breadcrumb stack and have no
	// observable effect. The target cone scalar defaults to 1.0 because a POINT carries no stealth
	// surface — retail's point overload does not read one either.
	return FElysiumNpcSenses::IsInViewCone(*this, PointCm);
}

// --- Moved from `ElysiumNpcBaseCombat10.cpp` (story 5 step 6) ---

bool FElysiumCombatCharacter::HasTypedStatList(int32 ListType) const
{
	// The `+0x13bc` count / `+0x13c0` table walk, as an answer. Type 0 IS `FElysiumSheet`: story
	// 29b's shape bound `ElysiumSlot::Health` to stat `0xf` and `ElysiumSlot::MaxHealth` to stat
	// `0x11`, and `FElysiumCombatCharacter::SyncHealthFromSheet` already cites `0x1032fe60` for it.
	// Types 2 and 3 have no port container, so the walk falls through to `DAT_109f0b40`.
	return ListType == NpcKernelCombat10Shared::GStatListTypeSheet;
}

int32 FElysiumCombatCharacter::TypedStatValue(int32 ListType, int32 StatId) const
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

int32 FElysiumCombatCharacter::HealthToPercent()
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

bool FElysiumCombatCharacter::WeaponAmmoFromWeaponData(const FElysiumEntity* /*Weapon*/, int32 /*Index*/) const
{
	// SEAM for the weapon's slot `+0x454`. False takes the `+0x450` cap arm below.
	return false;
}

int32 FElysiumCombatCharacter::WeaponAmmoCap(const FElysiumEntity* /*Weapon*/, int32 /*Index*/) const
{
	// SEAM for the weapon's slot `+0x450`. `0` is below `1`, so the entry takes retail's flat zero.
	return 0;
}

void FElysiumCombatCharacter::RerollDroppedWeaponAmmo(FElysiumEntity* Weapon)
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

void FElysiumCombatCharacter::Weapon_Drop()
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

void FElysiumCombatCharacter::Weapon_Drop(FElysiumEntity* Weapon, const FVector* /*TargetUnits*/, bool bForce)
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

void FElysiumCombatCharacter::Slot357()
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

void FElysiumCombatCharacter::TypedStatIncBase(int32 ListType, int32 StatId)
{
	TypedStatWrites.Add(FTypedStatWrite{ ListType, StatId, 1, TEXT("IncBase") });
	if (!HasTypedStatList(ListType))
	{
		return;
	}
	Sheet.IncBase(EElysiumTraitContainer::Attributes, StatId, SheetRules(), SheetEffects());
}

bool FElysiumCombatCharacter::WeaponIsDroppable(const FElysiumEntity* Weapon)
{
	// `thunk_FUN_102577e0(weapon)` — the item record's `is_droppable`, which the port already holds.
	const FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr;
	return Item != nullptr && Item->IsDroppable();
}

bool FElysiumCombatCharacter::WeaponIsStackSplittable(const FElysiumEntity* Weapon) const
{
	// SEAM for the weapon's own vtable `+0x5cc`, slot 385's split gate. See the CORRECTION on the
	// declaration: the count beside it is `m_iItemCount` (`+0x8d0`), so the gate is stackability.
	const FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr;
	return Item != nullptr && Item->IsStackable();
}

int32 FElysiumCombatCharacter::WeaponStackCount(const FElysiumEntity* Weapon) const
{
	const FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr;
	return Item != nullptr ? Item->ItemCount : 0;   // +0x8d0 m_iItemCount
}

void FElysiumCombatCharacter::SetWeaponStackCount(FElysiumEntity* Weapon, int32 Count)
{
	if (FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr)
	{
		Item->ItemCount = Count;
	}
}

FElysiumEntity* FElysiumCombatCharacter::WeaponCreateForDrop(const TCHAR* Classname)
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

void FElysiumCombatCharacter::NotifyWeaponDropped(FElysiumEntity* /*Weapon*/)
{
	// SEAM for the weapon's own vtable `+0x4b0` — `Drop()`. Counted.
	++WeaponDropNotifies;
}

void FElysiumCombatCharacter::WeaponDetach(FElysiumEntity* /*Weapon*/)
{
	// SEAM for `CBaseCombatCharacter::Weapon_Detach(weapon)`, the `FL_NPC` tail. Counted.
	++WeaponDetaches;
}

bool FElysiumCombatCharacter::WeaponNeedsTransmitUpdate(const FElysiumEntity* /*Weapon*/) const
{
	// SEAM for `thunk_FUN_100b5190(weapon)`. Answers false: no transmit state stands here.
	return false;
}

int32 FElysiumCombatCharacter::FaithPointsMaximum() const
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

void FElysiumCombatCharacter::RunPrayerPulse(double Now)
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

// --- Moved from `ElysiumNpcBaseConditions10.cpp` (story 5 step 6) ---

bool FElysiumCombatCharacter::CanBeFedUponTemplate() const
{
	// `CBaseCombatCharacter::CanBeFedUpon` (`0x10339a90`): `GetCharTemplate(this)->+0x95 == 0`.
	// **SEAM**: `+0x95` has no recovered column name and `FElysiumClanTemplate` exposes none, so
	// this answers TRUE — retail's own answer for a template byte of zero, and the ADMITTING arm.
	return true;
}

bool FElysiumCombatCharacter::IsUnconsciousMiscFlag() const
{
	// `CBaseCombatCharacter::IsUnconscious` (`0x10341aa0`) past its scope-trace push:
	// `return (m_iMiscFlags & 1) != 0`. Bit 0 of the name table at `0x10619ec8` is `Unconscious`.
	return ElysiumMiscFlags::Has(MiscFlags, ElysiumMiscFlags::Unconscious);
}

bool FElysiumCombatCharacter::CanBeFedUponBy(FElysiumEntity* Feeder)
{
	// `CBaseCombatCharacter::CanBeFedUponBy` (`0x10339800`), 237 bytes. **The feeder argument is
	// never read**: every one of the five terms is about the victim, which is exactly why the Troika
	// override above it has to make the follower test itself.
	(void)Feeder;

	// `1033988a`: `CanBeFedUpon()`.
	if (!CanBeFedUponTemplate())
	{
		return false;
	}
	// `1033989a`: `m_bfAINPCFlags2 & 0x8000000` — `NOT_FEEDABLE`.
	if (NpcFlags.Has(EElysiumNpcFlag2::NOT_FEEDABLE))
	{
		return false;
	}
	// `103398a6`: a live grapple refuses. The handle `m_GrapplePartner` (`+0x1538`) must fail to
	// resolve, OR `m_GrappleRole` (`+0x153c`) must be -1; anything else is a body already in a pair.
	{
		const FElysiumEntity* const Partner =
			World != nullptr ? World->Resolve(Grapple.Partner) : nullptr;
		if (Partner != nullptr && Grapple.Role != EElysiumGrappleRole::None)
		{
			return false;
		}
	}
	// `103398f1`: slot 158 `IsAlive()` (`vtable +0x278`).
	if (!IsAlive())
	{
		return false;
	}
	// `103398fa`: `IsUnconscious()`.
	if (IsUnconsciousMiscFlag())
	{
		return false;
	}
	return true;
}

// --- Moved from `ElysiumNpcBaseDamage.cpp` (story 5 step 6) ---

int32 FElysiumCombatCharacter::GiveAmmo(int32 Count, int32 AmmoIndex, bool bSuppressSound)
{
	// Five gates, in retail's order, each answering 0:
	//   count <= 0;  !g_pGameRules->+0xd4(this, index);  index < 0;  index >= 0x20;
	//   and finally the clamp itself.
	if (Count <= 0)
	{
		return 0;
	}
	if (!GameRulesAllowsAmmo(AmmoIndex))
	{
		return 0;
	}
	if (AmmoIndex < 0 || AmmoIndex >= AmmoSlotCount)
	{
		return 0;
	}

	// `room = GetAmmoDef()->MaxCarry(index) - m_iAmmo[index]`, then `add = min(count, room)`, and
	// `add < 1` answers 0 BEFORE the sound. So a full pool is silent as well as fruitless.
	const FString AmmoName = AmmoTypeNameForIndex(AmmoIndex);
	const int32 Held = AmmoName.IsEmpty() ? 0 : Inventory.Reserve(AmmoName);
	const int32 Room = AmmoMaxCarry(AmmoIndex) - Held;
	const int32 Add = FMath::Min(Count, Room);
	if (Add < 1)
	{
		return 0;
	}

	// The cue, only when the third argument is clear. `CPASAttenuationFilter` at this body's ear
	// position, channel 3 (`CHAN_ITEM`), volume 1.0, attenuation 0.8, flags 0, pitch 100.
	if (!bSuppressSound)
	{
		if (IElysiumAudio* Audio = World != nullptr ? World->Audio() : nullptr)
		{
			FElysiumBodySound Sound;
			Sound.Rel = AmmoPickupSound;
			Sound.Volume = AmmoPickupVolume;
			Sound.Pitch = 1.0f;   // retail's 100 is the engine's "unmodified" pitch
			Sound.Channel = EElysiumSoundChannel::Item;
			Audio->PlayBodySound(Handle, Sound);
		}
		(void)AmmoPickupAttenuation;
		(void)AmmoPickupPitch;
	}

	// `m_iAmmo[index] += add; return add;`
	if (!AmmoName.IsEmpty())
	{
		Inventory.AddReserve(AmmoName, Add);
	}
	return Add;
}

int32 FElysiumCombatCharacter::AmmoMaxCarry(int32 AmmoIndex) const
{
	// SEAM for `thunk_FUN_10427620` — `GetAmmoDef()->MaxCarry(index)`. UNRECOVERED: this runtime's
	// reserve is keyed by the authored ammo TYPE NAME and no table joins retail's 0..31 index to it.
	(void)AmmoIndex;
	return 0;
}

FString FElysiumCombatCharacter::AmmoTypeNameForIndex(int32 AmmoIndex) const
{
	// SEAM for the same `CAmmoDef` index order. UNRECOVERED; answers the empty name, which is what
	// closes `GiveAmmo`'s clamp.
	(void)AmmoIndex;
	return FString();
}

bool FElysiumCombatCharacter::GameRulesAllowsAmmo(int32 AmmoIndex) const
{
	// SEAM for `(*g_pGameRules)->vtable+0xd4`, `GiveAmmo`'s first gate. No rules object here carries
	// it; TRUE is the permissive arm, so the capacity clamp below is what decides.
	(void)AmmoIndex;
	return true;
}

// --- Moved from `ElysiumNpcBaseDebug.cpp` (story 5 step 6) ---

FElysiumEntity* FElysiumCombatCharacter::ActiveWeaponEntity() const
{
	// `CBaseCombatCharacter::GetActiveWeapon()` — the inventory's active slot (`+0x19a4`).
	//
	// Story 29d, family **Combat10**: this was a seam answering null because "there is no kernel
	// accessor for the entity", and there is — `FElysiumInventory::Active` is retail's own
	// `m_hActiveWeapon` resolve and is what `ElysiumNpcCond::WeaponCapability` already reads. The
	// two `Weapon_Drop` bodies (slots 385/386, `0x1032d0c0` / `0x1032ce40`), the ranged weapon
	// pre-pass (`0x102b8620`) and `CNPC_VBach::SelectScheduleRangedCombat` (`0x103642f0`) all begin
	// with this call, so a null answer would have made four recovered bodies no-ops.
	return Inventory.Active(*this);
}

// --- Moved from `ElysiumNpcBaseEntityChain.cpp` (story 5 step 6) ---

void* FElysiumCombatCharacter::Slot240()
{
	// 0x1014f8b0, slot 240 — `return DAT_1072b360;`. One global word, not a literal, which is why
	// the generator could not emit it as a `default:`.
	return PythonInteropObject();
}

void* FElysiumCombatCharacter::PythonInteropObject() const
{
	// SEAM for `DAT_1072b360`, slot 240's whole body. The global is the CPython interop side of the
	// entity — whatever the embedded interpreter last stored — and this runtime embeds none, so the
	// answer is the global's own pre-interpreter value.
	return nullptr;
}

// --- Moved from `ElysiumNpcBaseFacing.cpp` (story 5 step 6) ---

void FElysiumCombatCharacter::SetPoseParameterByName(const TCHAR* Name, float Value)
{
	// `CBaseAnimating::SetPoseParameter(const char*, float)`, slot 345 (retail vtable +0x564).
	// **SEAM**: the animating tier exposes no pose-parameter surface to the kernel, so the write is
	// recorded and goes no further.
	PoseParameterWrites.Add(FPoseParameterWrite{ FString(Name), Value });
}

// --- Moved from `ElysiumNpcBaseHelpers.cpp` (story 5 step 6) ---

// -------------------------------------------------------------------------------------------------
// Slot 334 — `CAI_BaseNPC::FUN_10330020` `0x10330020`.
// -------------------------------------------------------------------------------------------------

bool FElysiumCombatCharacter::Slot334(int32 DisciplineId, int32 Level)
{
	// `0x10330020`, arm by arm:
	//     if (m_iCurFrenzyCount > 0) return false;          // +0x146c (0x10330024), XOR AL,AL
	//     DAT_10937cf2 = 0;
	//     row = DisciplineTableFind(&DAT_10739a4c, id, level);      // 0x101e1250
	//     if (row != -1) {
	//         elapsed  = curtime - m_fDisciplineTimers[row];        // +0xec0 (0x1033006b)
	//         cooldown = record[+0x2c];
	//         if (cooldown > elapsed) { DAT_10937cf2 = 1; return false; }
	//     }
	//     return true;
	//
	// **29c's walk has the flag and the answer inverted.** The listing sets the global on the arm
	// that is STILL COOLING and answers false there; the elapsed-cooldown arm answers true and
	// leaves the global clear. The first arm's answer is `m_iCurFrenzyCount & 0xffffff00`, whose low
	// byte is zero — false — and not the count.
	if (CurFrenzyCount > 0)
	{
		return false;
	}
	NpcKernelTroikaHelpersShared::GTroikaDisciplineReadyFlag = false;
	const int32 Row = DisciplineTableFind(DisciplineId, Level);
	if (Row != INDEX_NONE)
	{
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		const double Elapsed = Now - DisciplineTimer(Row);
		if (static_cast<double>(DisciplineTableCooldown(Row)) > Elapsed)
		{
			NpcKernelTroikaHelpersShared::GTroikaDisciplineReadyFlag = true;
			return false;
		}
	}
	return true;
}

int32 FElysiumCombatCharacter::DisciplineTableFind(int32 DisciplineId, int32 Level) const
{
	// `thunk_FUN_101e1250(&DAT_10739a4c, id, level)`. **SEAM**: no global discipline table on this
	// substrate. `INDEX_NONE` is retail's own `0xffffffff` miss, which slot 334 answers true on.
	(void)DisciplineId;
	(void)Level;
	return INDEX_NONE;
}

float FElysiumCombatCharacter::DisciplineTableCooldown(int32 RowIndex) const
{
	// `thunk_FUN_101e11c0(&DAT_10739a4c, row)` then the float at record `+0x2c`. **SEAM**, `0.0`.
	(void)RowIndex;
	return 0.0f;
}

double FElysiumCombatCharacter::DisciplineTimer(int32 RowIndex) const
{
	// `m_fDisciplineTimers[row]` (+0xec0, float[60]). Below the shape map's band and with no producer in this
	// runtime. **SEAM**, `0.0` — every discipline reads as never cast.
	(void)RowIndex;
	return 0.0;
}

// --- Moved from `ElysiumNpcBaseHints.cpp` (story 5 step 6) ---

// --- Moved from `ElysiumNpcHints.cpp` (story 5 step 5) ---

int32 FElysiumCombatCharacter::CurrentRetailActivityId() const
{
	// SEAM for `m_Activity` (`+0xfec`).
	return INDEX_NONE;
}

// --- Moved from `ElysiumNpcBaseMisc.cpp` (story 5 step 6) ---

// -------------------------------------------------------------------------------------------------
// Slot 296 — `0x10348ba0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumCombatCharacter::Slot296(int32 Argument)
{
	// `0x10348ba0`, arm by arm:
	//     if (m_GrappleRole (+0x153c) == -1) return false;
	//     if (m_GrapplePartner (+0x1538) == INVALID_EHANDLE) return false;
	//     if (!resolve(m_GrapplePartner)) return false;           // PTR_DAT_10566458, & 0x1fff,
	//                                                             // generation >> 0xd
	//     return param_1 == 0xb;
	//
	// The three grapple terms are exactly `FElysiumGrappleState::IsPaired()` plus the world resolve
	// its comment says every consumer that can reach a world must also do.
	//
	// **Unrecovered: what `0xb` is.** Slot 296 has no dispatch site anywhere in the image (0d/0v/0c),
	// so nothing states the argument's domain; it is not a `m_GrappleType` value (those run 0..8).
	// The literal is reproduced as the literal.
	if (!Grapple.IsPaired())
	{
		return false;
	}
	if (World == nullptr || World->Resolve(Grapple.Partner) == nullptr)
	{
		return false;
	}
	return Argument == 0xb;
}

// --- Moved from `ElysiumNpcBaseSenses.cpp` (story 5 step 6) ---

bool FElysiumCombatCharacter::AimConeAdmits(const FVector& OriginCm, const FVector& TargetCm,
	const FVector& Aim)
{
	// Slot 364's arithmetic, with the three virtual reads lifted out. See `FInAimCone` below for
	// the listing this transcribes.
	FVector Delta = TargetCm - OriginCm;
	Delta.Z = 0.0;
	if (!Delta.Normalize())
	{
		// `VectorNormalize` of a zero vector leaves the components alone and answers length 0;
		// retail then dots zeros against 0.994, which refuses. Same answer.
		return false;
	}
	return FVector::DotProduct(Delta, Aim) > GAimConeDotFloor;
}

bool FElysiumCombatCharacter::FInAimCone(const FVector& TargetCm)
{
	// `0x10326bd0`, 283 bytes of which the scope-trace push/pop is 200. The computation, read off
	// the listing because the decompiler lost two of the three operands:
	//
	//     d = target - GetAbsOrigin()          (slot 217, vtable +0x364)
	//     d.z = 0                              (10326c83  MOV [ESP+0xc],0x0)   <-- BEFORE normalise
	//     VectorNormalize(&d)                  (PTR_thunk_FUN_10137220)
	//     return dot(d, EyeDirection2D()) > 0.994   (slot 372, vtable +0x5d0; FCOMP double)
	//
	// **The Z is zeroed before the normalise, not after**, so the test is planar in both operands
	// and a target directly overhead is at dot 0. 29c's walk reads it as a 3-D normalise; the
	// immediate at `10326c83` says otherwise, and the difference is the whole answer for a target
	// above or below the shooter.
	//
	// The dot's third term survives in the listing (`FLD [ESP+0x20]  FMUL [ESP+0x8]`) and is
	// `eyeDir.z * 0`, so it contributes nothing — retail computes it anyway and so does this.
	//
	// `GetAbsOrigin()` is slot 217; the generated virtual is a declared stub that answers null, and
	// `FElysiumEntity::Origin` is the word it would hand back — family **BaseHelpers** spells it
	// the same way at its own slot-217 read.
	//
	// **SEAM, named**: `EyeDirection2D()` (slot 372) forwards to `HeadDirection2D()` (slot 370),
	// which is still a GENERATED STUB answering the zero vector, so every call through this body
	// refuses today. The refusal is the stub's, not the cone's — `AimConeAdmits` above carries the
	// recovered rule and is what the suite drives.
	return AimConeAdmits(Origin, TargetCm, EyeDirection2D());
}

bool FElysiumCombatCharacter::FInAimCone(FElysiumEntity* AimTarget)
{
	// `0x10326ae0`, 181 bytes. Past the scope trace it is three dispatches and nothing else:
	//
	//     eye  = EyePosition()                         (this, slot 193, vtable +0x304)
	//     aim  = target->BodyTarget(eye, true, false)  (slot 197, vtable +0x314)
	//     return FInAimCone(aim)                       (this, slot 364, vtable +0x5b0)
	//
	// The two literal booleans are pushed at `10326b0c`/`10326b0e` before the eye call, which is
	// why the decompiler attached them to the wrong callee. `BodyTarget`'s answer, not the target's
	// origin, is what the cone is measured to — a prone or crouched body aims at a different point.
	if (AimTarget == nullptr)
	{
		// Retail dereferences `*param_1` for the vtable and would fault; every call site in the
		// closure has already null-checked. Refusing is this port's own guard and changes no
		// reachable arm.
		return false;
	}
	const FVector EyeCm = EyePosition();
	// Slot 197 is declared on `FElysiumNpc` only — this runtime stands the Troika line's
	// `BodyTarget` (`0x102789c0`) and no `CBaseEntity` tier below it. For a non-NPC target the base
	// `CBaseEntity::BodyTarget` is `WorldSpaceCenter()`, i.e. `GetAbsOrigin() + (mins+maxs)/2`, and
	// family Motor's `RetailCollisionExtents` — the only source for those extents — answers the
	// player's `CGameMovement` hull and nothing for any other non-NPC entity. This arm does not ask it
	// yet: the aim point is the origin, which is the centre only for a box symmetric about it -- a
	// named gap, not a value invented here.
	FElysiumNpc* TargetNpc = AimTarget->AsNpc();
	const FVector AimPointCm = TargetNpc != nullptr
		? TargetNpc->BodyTarget(EyeCm, true, false) : AimTarget->Origin;
	return FInAimCone(AimPointCm);
}

// --- Moved from `ElysiumNpcBaseSpecies.cpp` (story 5 step 6) ---

// -------------------------------------------------------------------------------------------------
// Slot 323 — `CAI_BaseNPC::FUN_10344dd0` `0x10344dd0`. 78 classes share the one body.
// -------------------------------------------------------------------------------------------------

int32 FElysiumCombatCharacter::Slot323(const FVector& DirectionCm)
{
	// `0x10344dd0`, arm by arm:
	//
	//     len2d = sqrt(dir.x*dir.x + dir.y*dir.y);          // PTR_thunk_FUN_101371d0
	//     if (_DAT_1049e028 (1e-07) <= len2d) {
	//         dir.z = 0;
	//         a = UTIL_AngleMod( VecToYaw(dir) - GetAbsAngles().y );   // 0x101d2c70, slot 219
	//         if (316.0 < a || a <= 45.0)   return 2;
	//         if (a <= 135.0)               return 3;
	//         if (135.0 < a && a <= 225.0)  return 0;
	//         if (225.0 < a)                return 1;
	//     }
	//     return 0;
	//
	// The decompiler lost the subtraction between `VecToYaw` and `GetAbsAngles` — both calls are
	// there with their results dropped on the FPU stack and a bare `__ftol()` after them, which is
	// the `UTIL_AngleMod` prologue. The reading is settled by the arithmetic that DID survive: the
	// `& 0xffff` and the `* 0.0054931640625` are `UTIL_AngleMod`'s, and a body that bucketed an
	// ABSOLUTE yaw into four fixed bands would answer the same code for the same world direction no
	// matter which way the NPC faced, which is not a direction code. **Unrecovered:** nothing else;
	// the operand order (`yaw - facing`, not `facing - yaw`) is the one that makes band 2 the body's
	// own heading, and band 2 is the band the recovered `45.0` centres on zero.
	//
	// `DirectionCm` is this runtime's axes; `SpeciesVecToYaw` negates Y for Source's, exactly as the
	// three other copies of `UTIL_VecToYaw` in the kernel do. The Z flatten is retail's and is done
	// before the yaw rather than after, which changes nothing and is kept in retail's order.
	const double Length2D = FMath::Sqrt(
		DirectionCm.X * DirectionCm.X + DirectionCm.Y * DirectionCm.Y);
	if (static_cast<float>(Length2D) < MoveDirectionMinLength2D)
	{
		return static_cast<int32>(EMoveDirectionCode::Behind);   // retail's `0`
	}
	const FVector Flat(DirectionCm.X, DirectionCm.Y, 0.0);
	const float Relative = SpeciesAngleMod(
		SpeciesVecToYaw(Flat) - static_cast<float>(Angles.Y));

	if (MoveDirectionRightBand < Relative || Relative <= MoveDirectionAheadBand)
	{
		return static_cast<int32>(EMoveDirectionCode::Ahead);
	}
	if (Relative <= MoveDirectionLeftBand)
	{
		return static_cast<int32>(EMoveDirectionCode::Left);
	}
	if (MoveDirectionLeftBand < Relative && Relative <= MoveDirectionBehindBand)
	{
		return static_cast<int32>(EMoveDirectionCode::Behind);
	}
	if (MoveDirectionBehindBand < Relative)
	{
		return static_cast<int32>(EMoveDirectionCode::Right);
	}
	// Unreachable — the four bands cover [0, 360). Retail carries the fall-through and so does this.
	return static_cast<int32>(EMoveDirectionCode::Behind);
}

// --- Moved from `ElysiumNpcBaseSpeciesMisc10_2.cpp` (story 5 step 6) ---

// --- Moved from `ElysiumNpcSpeciesMisc10_2.cpp` (story 5 step 5) ---

void FElysiumCombatCharacter::Slot332(FElysiumEntity* SlowTarget)
{
	// `CAI_BaseNPC#332` / `CAI_BaseNPCTroika#332` (`0x1014f890`) — the whole Troika-line body is
	// `return;`. The ONE species override is `CNPC_VTzimisceHeadClaw`'s `0x103c1d80`, on its C++
	// class (story 5 step 3).
	(void)SlowTarget;
}

void FElysiumCombatCharacter::Slot354()
{
	// `CBaseCombatCharacter` slot 354 (`0x1014f8d0`) is a lone `RET`: a victim that is not a
	// `CAI_BaseNPC` does nothing on feed begin (`FeedBegin` `0x10339d90` dispatches it,
	// `CALL [EDX+0x588]`). Hand body since story 8 lane L11; overlay row `hand:` (lane L12 integration).
}
