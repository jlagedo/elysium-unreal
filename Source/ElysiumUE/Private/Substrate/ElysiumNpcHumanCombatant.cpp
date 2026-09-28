#include "Substrate/ElysiumNpcHumanCombatant.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcState19Shared.h"
#include "Substrate/ElysiumNpcState19_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Slot 420: `0x10387140`.
void FElysiumNpcHumanCombatant::NPCInit()
{
	HumanCombatantNPCInit();
}

// Slot 463: `0x103871c0`, hide on IDLE and unhide on ALERT / COMBAT / 11, then a direct call into
// the Troika body `0x102ae140`. Inherited by the ghoul croucher, the combat patrol, the Sabbat
// gunman, Yukie and `CNPC_ProneDialog`; `CNPC_VCop` and `CNPC_VHunter` call it directly.
void FElysiumNpcHumanCombatant::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	ApplyStateWeaponVisibility(NewState);
	OnStateChangeTroika(OldState, NewState);
}

// Slot 461: `0x10387380`, chaining the human line's `0x103851e0` directly (the port names the body after the combat patrol).
int32 FElysiumNpcHumanCombatant::SelectIdealStateRetail()
{
	return HumanCombatPatrolSelectIdealState();
}

// Slot 453: `0x10387520`, a direct call into the Troika body `0x102ad140` first, then its own bits.
// Slot 453: `0x10387520`'s own bits, the body of its class's `BuildScheduleTestBits` override (story 5 step 3).

void FElysiumNpcHumanCombatant::BuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
	FElysiumNpc::BuildScheduleTestBits(InOutMask);
	// Slot 158 `IsAlive` and slot 464 `GetState` (retail state 1 is idle). Both generated slots
	// are stubs; the port carries both facts, so they are read from the entity and the mind.
	if (!IsDead() && Mind.State() == EElysiumNpcState::Idle)
	{
		// SEAM: `m_edtDerivedType` (`+0x004c`) is a chain word of `CBaseEntity` with no port
		// member; the port asks it and reads bit 7 as clear, which is the arm that admits the
		// condition regardless of `m_bCameFromSpawner`.
		constexpr bool bDerivedTypeBit7 = false;
		if (!bDerivedTypeBit7 || !bCameFromSpawner)
		{
			InOutMask.Set(EElysiumNpcCond::SeeCorpseFriend);
		}
	}
}

// Slot 546: `0x10386c80`, the class's own schedule id space.
const TCHAR* FElysiumNpcHumanCombatant::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093b47c`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VHumanCombatant"), TEXT("0x10386c80"), TEXT("0x1093b47c") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

void FElysiumNpcHumanCombatant::HumanCombatantNPCInit()
{
	// `0x10387140`. Listing: Troika then Hide on the active weapon (slot 66 tail JMP).
	TroikaNPCInit();                                                     // 0x10387143 CALL 0x1000c531
	HideActiveWeaponIfAny();                                             // 0x1038714a GetActiveWeapon, 0x10387151 JZ null, 0x10387155, 0x1038715f JMP [weapon+0x108]
}

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcState19.cpp` (story 5 step 4) ---

// `CNPC_VHumanCombatant::OnStateChange` (`0x103871c0`), the shared tail BOTH of `0x10371c20`'s
// arms chain with `(old, new)` — read from the listing (`10371db3 CMP EBP,0x1` then `PUSH EBP`),
// because the C mis-renders the idle tail's argument as the literal 1. Its weapon half is
// UNCONDITIONAL, the same switch `FElysiumNpcHuman::ApplyStateWeaponVisibility` carries. The Troika body
// under it is run by `FElysiumNpcCop::OnStateChange`'s own tail, so only the weapon half lands here.
void FElysiumNpcHumanCombatant::CopHumanCombatantOnStateChange(int32 NewRetail)
{
	++CopHolsterDrawCalls;
	FElysiumItem* const Active = Inventory.Active(*this);
	FElysiumWeapon* const Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
	if (Weapon == nullptr)
	{
		// `103871d1` / `103871f6`: both arms are guarded by `GetActiveWeapon()`.
		return;
	}
	if (NewRetail == 1)
	{
		// `+0x108` — `CBaseEntity::Hide` (slot 66, `0x1009d2a0`).
		Weapon->Hide();
	}
	else if (NewRetail == 2 || NewRetail == 3 || NewRetail == 0xb)
	{
		// `+0x10c` — `Unhide`.
		Weapon->Unhide();
	}
}

// --- Moved from `ElysiumNpcState19_2.cpp` (story 5 step 4) ---

int32 FElysiumNpcHumanCombatant::HumanCombatPatrolSelectIdealState()
{
	SelectIdealStateSelector = 0x15;
	if (NpcStateRetail() == 1)
	{
		if (!bNoAlertState
			&& (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::LightDamage)
				|| NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::HeavyDamage)
				|| NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::RepeatedDamage)))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 3, 0x1c5);
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeCorpseFriend))
		{
			FElysiumEntity* const Closest = World != nullptr
				? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
			if (Closest != nullptr)
			{
				const FString Map = !SelectIdealStateMapNameOverride.IsEmpty()
					? SelectIdealStateMapNameOverride
					: (World != nullptr ? World->MapName() : FString());
				if (FCString::Strncmp(*Map, TEXT("la_empire_2"), 12) == 0)
				{
					Slot597(Closest, 5);
				}
			}
			NpcKernelState19_2Shared::State19_2Stamp(*this, 0xb, 0x1d1);
			return IdealStateRetail();
		}
	}
	return NpcKernelState19_2Shared::State19_2ChainHuman(*this);
}
