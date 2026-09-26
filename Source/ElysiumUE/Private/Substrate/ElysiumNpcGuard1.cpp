#include "Substrate/ElysiumNpcGuard1.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Misc/FileHelper.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpeciesLifecycle10Shared.h"
#include "Substrate/ElysiumNpcSpeciesMisc10Shared.h"
#include "Substrate/ElysiumNpcState19_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	const TCHAR* const GGuardRelationshipLiteral = TEXT("player D_HT 10");    // 0x1063bc28
	/** `CNPC_VGuard1::vfunc461`'s repeated law arm (`0x1037d2a4` and thirteen siblings): the
	 *  interrupt stands AND the closest player IS that channel's offender. The guard latches its
	 *  hate (`0x1037e2d0`), re-reads `m_hClosestPlayer` and hands it to slot 596, then answers. */
	bool State19_2Guard1LawArm(FElysiumNpcGuard1& Npc, EElysiumNpcCond Cond,
		ElysiumNpcWitness::EChannel Channel, int32 IdealRetail, int32 Line)
	{
		if (!NpcKernelState19_2Shared::State19_2HasInterrupt(Npc, Cond))
		{
			return false;
		}
		FElysiumEntity* const Closest = NpcKernelState19_2Shared::State19_2Resolve(Npc, Npc.Senses.Memory.ClosestPlayer);
		FElysiumEntity* const Offender =
			NpcKernelState19_2Shared::State19_2Resolve(Npc, Npc.Witness.Channel(Channel).Offender);
		if (Closest != Offender)
		{
			return false;
		}
		Npc.Guard1HatePlayer();
		Npc.Slot596(Closest);
		NpcKernelState19_2Shared::State19_2Stamp(Npc, IdealRetail, Line);
		return true;
	}
}

const FElysiumNpcClass* FElysiumNpcGuard1::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 420: `0x1037e240`.
// `0x1037e240` — not a TSV row; dispatcher completeness
void FElysiumNpcGuard1::NPCInit()
{
	// `0x1037e240`. Not a TSV row; dispatcher completeness.
	bGuard1HatesPlayer = false;                                          // 1037e24a +0x6660 FIRST
	FElysiumInputArgs Args;
	Args.Param = FElysiumVariant::String(TEXT("player D_NU 0"));
	InputSetRelationship(Args);
	TroikaNPCInit();
	HideActiveWeaponIfAny();
}

// Slot 463: `0x1037d020`, the enemy-is-the-player pre-step, its own copy of the holster/draw
// switch, then a direct call into the Troika body.
void FElysiumNpcGuard1::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	Guard1StateChangePreStep();
	ApplyStateWeaponVisibility(NewState);
	OnStateChangeTroika(OldState, NewState);
}

// Slot 461: `0x1037d290`, chaining the human line's `0x103851e0` directly.
// `CNPC_VGuard1::vfunc461` (`0x1037d290`) — slot 461. The law ladder: four channel arms per
// state, each "the interrupt stands AND the closest player IS that channel's offender", flee for
// the two FLEE levels and combat for the two ATTACK ones, with `INVESTIGATE_LEVEL` promoting to
// the guard's own investigate state `0xc`. Whatever it does not name chains `CNPC_VHuman`.
int32 FElysiumNpcGuard1::SelectIdealStateRetail()
{
	using EChannel = ElysiumNpcWitness::EChannel;
	SelectIdealStateSelector = 0x12;
	switch (NpcStateRetail())
	{
	case 1:
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalFleeLevel,
			EChannel::Supernatural, 8, 0x215))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalFleeLevel,
			EChannel::Criminal, 8, 0x222))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalAttackLevel,
			EChannel::Supernatural, 2, 0x22f))
		{
			return 2;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalAttackLevel,
			EChannel::Criminal, 2, 0x23c))
		{
			return 2;
		}
		// `1037d5d5`: the fifth law condition, with no offender compare at all.
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::InvestigateLevel))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 0xc, 0x243);
			return 0xc;
		}
		break;

	case 3:
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalFleeLevel,
			EChannel::Supernatural, 8, 0x251))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalFleeLevel,
			EChannel::Criminal, 8, 0x25e))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalAttackLevel,
			EChannel::Supernatural, 2, 0x26b))
		{
			return 2;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalAttackLevel,
			EChannel::Criminal, 2, 0x278))
		{
			return 2;
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::InvestigateLevel))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 0xc, 0x27f);
			return 0xc;
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0x286);
			return 2;
		}
		// `1037d8e7`: four hear conditions, and `m_fHatesPlayer` decides HUNT `0xb` or ALERT.
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
		{
			if (bGuard1HatesPlayer)
			{
				NpcKernelState19_2Shared::State19_2Stamp(*this, 0xb, 0x291);
			}
			else
			{
				NpcKernelState19_2Shared::State19_2Stamp(*this, 3, 0x296);
			}
			if (NpcKernelState19_2Shared::State19_2BestSound(*this) != nullptr)
			{
				++SelectIdealStateMotorResets;
			}
			return IdealStateRetail();
		}
		if (ShouldGoToIdleState())
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 1, 0x2a5);
			return 1;
		}
		break;

	case 0xb:
		// `1037da3c`: a guard that hates the player leaves the hunt the moment it sees one.
		if (bGuard1HatesPlayer && NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeePlayer))
		{
			Slot596(NpcKernelState19_2Shared::State19_2Resolve(*this, Senses.Memory.ClosestPlayer));
			NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0x2b1);
			return 2;
		}
		break;

	case 0xc:
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalFleeLevel,
			EChannel::Supernatural, 8, 0x2c1))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalFleeLevel,
			EChannel::Criminal, 8, 0x2ce))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalAttackLevel,
			EChannel::Supernatural, 2, 0x2db))
		{
			return 2;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalAttackLevel,
			EChannel::Criminal, 2, 0x2e8))
		{
			return 2;
		}
		// `1037dd4d`: the investigate state has no fall-through — it ends in IDLE and RETURNS,
		// so a Guard1 in state 0xc never reaches `CNPC_VHuman`.
		NpcKernelState19_2Shared::State19_2Stamp(*this, 1, 0x2ee);
		return 1;

	default:
		break;
	}
	return NpcKernelState19_2Shared::State19_2ChainHuman(*this);
}

// Slot 453: `0x1037cdf0`. It calls the EMPTY base `CAI_BaseNPC::BuildScheduleTestBits` (`0x10280fb0`,
// nothing to run) rather than the Troika body, then its state ladder; `CacheInterruptConditions`
// (`0x1026a0f0`) adds `NPC_FREEZE` after the virtual.
void FElysiumNpcGuard1::BuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
	Guard1BuildScheduleTestBits(InOutMask);
	InOutMask.Set(EElysiumNpcCond::NpcFreeze);
}

// Slot 440: `0x1037d240`.
// `0x1037d240`
// `0x1037d240`, `CNPC_VGuard1::TranslateSchedule`, the body of `FElysiumNpcGuard1::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcGuard1::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber == 0x6b)
	{
		return NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY) ? 0x132 : 0x15a;
	}
	if (ScheduleNumber == 0x103) { return 0x159; }
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 546: `0x1037c800`, the class's own schedule id space.
const TCHAR* FElysiumNpcGuard1::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093b1b4`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VGuard1"), TEXT("0x1037c800"), TEXT("0x1093b1b4") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 4) ---

// Slot 453: `0x1037cdf0`'s own bits, the body of its class's `BuildScheduleTestBits` override (story 5 step 3).
void FElysiumNpcGuard1::Guard1BuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
	const EElysiumNpcState State = Mind.State();
	if (State == EElysiumNpcState::Idle)
	{
		// Retail state 1: `SetScheduleTestBits(COMFORT 0x27)` and nothing else — and then it
		// falls into the shared state-3 tail below, which is what the listing's fallthrough
		// from `iVar5 == 1` does.
		InOutMask.Set(EElysiumNpcCond::Comfort);
	}
	else if (State != EElysiumNpcState::Alert)
	{
		// Retail state 0xb is the HUNT state, which this runtime's `EElysiumNpcState` does not
		// carry; every other state returns without touching the mask.
		//
		// SEAM: the hunt arm sets or clears `SEE_PLAYER` (0x5a) and `HEAR_PLAYER` (0x6f) on the
		// same five-threshold test as the alert tail below, plus `m_fHatesPlayer`. It is
		// unreachable here and is named rather than folded into another state.
		return;
	}

	// The state-3 (alert) tail, shared with the state-1 fallthrough: the five `pl_*` thresholds
	// against the CLOSEST PLAYER's current levels.
	bool bAnyThresholdPassed = false;
	if (World != nullptr && Senses.Memory.ClosestPlayer.IsSet())
	{
		if (const FElysiumPlayer* Player = World->FindPlayer())
		{
			if (Player->Handle == Senses.Memory.ClosestPlayer && !Player->IsInert())
			{
				bAnyThresholdPassed =
					PlInvestigate <= Player->Law.Investigate
					|| PlCriminalFlee <= Player->Law.Criminal
					|| PlCriminalAttack <= Player->Law.Criminal
					|| PlSupernaturalFlee <= Player->Law.Supernatural
					|| PlSupernaturalAttack <= Player->Law.Supernatural;
			}
		}
	}
	if (bAnyThresholdPassed)
	{
		InOutMask.Set(EElysiumNpcCond::InvestigateLevel);
		InOutMask.Set(EElysiumNpcCond::CriminalFleeLevel);
		InOutMask.Set(EElysiumNpcCond::CriminalAttackLevel);
		InOutMask.Set(EElysiumNpcCond::SupernaturalFleeLevel);
		InOutMask.Set(EElysiumNpcCond::SupernaturalAttackLevel);
		return;
	}
	// The miss arm clears ONE bit, `HEAR_PLAYER` (0x6f), and leaves the five alone.
	InOutMask.Clear(EElysiumNpcCond::HearPlayer);
}

// --- Moved from `ElysiumNpcSpeciesLifecycle10.cpp` (story 5 step 4) ---

void FElysiumNpcGuard1::Guard1StateChangePreStep()
{
	// `CNPC_VGuard1::OnStateChange` `0x1037d020`, arm (1). Unconditional on the states: the only
	// question is whether my enemy is the player. `GetEnemy()` is dispatched TWICE (`1037d026`,
	// `1037d034`) and retail caches neither call, so both are made.
	if (GetEnemyEntity() != nullptr)
	{
		const FElysiumEntity* Enemy = GetEnemyEntity();
		if (NpcKernelSpeciesLifecycle10Shared::SpeciesLifecycle10IsPlayer(*this, Enemy))
		{
			// `0x1037e2d0` is family SpeciesMisc10's `Guard1HatePlayer()` — six other
			// callers in `vfunc461` reach it too. Called, not restated.
			Guard1HatePlayer();
			++PlayerHateRelationshipSets;
		}
	}
}

// --- Moved from `ElysiumNpcSpeciesMisc10.cpp` (story 5 step 4) ---

void FElysiumNpcGuard1::Guard1HatePlayer()
{
	// `1037e2d0`: the latch byte first...
	bGuard1HatesPlayer = true;
	// `1037e2da`: ...then `InputSetRelationship("player D_HT 10", 0)`. Lower-case `player`, which is
	// the only difference from `CNPC_VCop`'s literal and is reproduced verbatim because the input's
	// own parse is case-insensitive only for the value word.
	FElysiumInputArgs Args;
	Args.Param = FElysiumVariant::String(GGuardRelationshipLiteral);
	Args.Activator = World != nullptr ? World->PlayerHandle() : FElysiumEntityHandle::Invalid();
	Args.Caller = Handle;
	Args.Input = FName(TEXT("SetRelationship"));
	InputSetRelationship(Args);
}

// --- Moved from `ElysiumNpcState19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 4) ---

