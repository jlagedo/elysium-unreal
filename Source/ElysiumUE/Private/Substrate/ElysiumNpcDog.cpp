#include "Substrate/ElysiumNpcDog.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelAnim10Shared.h"
#include "Substrate/ElysiumNpcKernelMotorShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcKernelState19Shared.h"
#include "Substrate/ElysiumNpcKernelState19_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Visual/ElysiumActionTables.h"

const FElysiumNpcClass* FElysiumNpcDog::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 375: `0x10374ad0`, which calls the Troika body `0x10295590` directly.
/** `CNPC_VDog::NPC_EarlyTranslateActivity` (`0x10374ad0`), 21 bytes. Retail preserves
 *  `3 ACT_FIDGET` by returning with `EAX` still holding the request — one early return the Troika
 *  base never sees — and forwards every other activity to `0x10295590`. */
int32 FElysiumNpcDog::NPC_EarlyTranslateActivity(int32 Activity)
{
	// `CNPC_VDog::NPC_EarlyTranslateActivity` `0x10374ad0`, 21 bytes. ONE early return the Troika
	// base never sees: retail preserves `ACT_FIDGET` by returning with `EAX` still holding the
	// request. Everything else forwards to `0x10295590`.
	if (Activity == NpcKernelAnim10Shared::GAnim10ActFidget)
	{
		return Activity;
	}
	return TroikaNpcEarlyTranslateActivity(Activity);   // the direct thunk `0x10295590`
}

// Slot 461: `0x103743c0`, chaining the animal line's `0x1035fe80` directly.
int32 FElysiumNpcDog::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0xd;
	const int32 State = NpcStateRetail();
	if (State == 1)
	{
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeFear))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x30f);
			if (NpcKernelState19_2Shared::State19_2Rand99() < 0x32)
			{
				Cognition.Conditions.Set(EElysiumNpcCond::DogBark);
			}
			return IdealStateRetail();
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0x31d);
			if (NpcKernelState19_2Shared::State19_2Rand99() < 0x32)
			{
				Cognition.Conditions.Set(EElysiumNpcCond::DogBark);
			}
			return IdealStateRetail();
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::LightDamage)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
		{
			Cognition.bCondTookDamage = false;
			return NpcKernelState19_2Shared::State19_2ChainAnimal(*this);
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeUnknown))
		{
			// `10374602`: a tail JMP to `CNPC_VAnimal` (`0x1035fe80`), not to Troika.
			Cognition.Conditions.Set(EElysiumNpcCond::DogBark);
			return NpcKernelState19_2Shared::State19_2ChainAnimal(*this);
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearWorld)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearThumper)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::Smell))
		{
			return NpcKernelState19_2Shared::State19_2ChainAnimal(*this);
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::PlayerSnarlRange))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 3, 0x347);
			Cognition.Conditions.Set(EElysiumNpcCond::DogBark);
			// `103746b5`: the snarl arm snapshots `+0x667c` into `+0x6680` and stamps `+0x6678`
			// with `gpGlobals->curtime` before the tail JMP to `CNPC_VAnimal` (`103746d3`).
			DogSnarlPrevValue = DogSnarlSourceWord;
			DogSnarlTime = World != nullptr ? World->NowSeconds() : 0.0;
			return NpcKernelState19_2Shared::State19_2ChainAnimal(*this);
		}
	}
	else if (State == 3)
	{
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeFear))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x357);
			return IdealStateRetail();
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0x35d);
			return IdealStateRetail();
		}
		// `10374436`..`1037447a`: five guards that all land on `FUN_103747c0(1)`, whose whole body
		// is `return 0`, so each is simply "go to `CNPC_VAnimal` now" — but they run BEFORE the
		// three arms below and so take priority over them.
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact)
			|| ShouldGoToIdleState())
		{
			return NpcKernelState19_2Shared::State19_2ChainAnimal(*this);
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::DogCombatLatch))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0x36d);
			return IdealStateRetail();
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::DogIdleFromAlert)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::DogIdleFromAlert2))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 1, 0x372);
			return IdealStateRetail();
		}
	}
	return NpcKernelState19_2Shared::State19_2ChainAnimal(*this);
}

// Slot 460: `0x10374d80`, whose non-latched arms call the Troika body `0x102ad340` directly.
int32 FElysiumNpcDog::PreSelectIdealStateRetail()
{
	if (NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::DogCombatLatch)
		|| NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::DogCombatLatch2))
	{
		DogCombatShortCircuit();
		return IdealStateRetail();
	}
	if (NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::DogAlertSound))
	{
		NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x517);
		return FElysiumNpc::PreSelectIdealStateRetail();   // 0x102ad340, direct
	}
	if (NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::DogIdleFromAlert)
		&& IdealStateRetail() == 3)
	{
		NpcKernelState19Shared::State19StampIdeal(*this, 1, 0x51d);
	}
	return FElysiumNpc::PreSelectIdealStateRetail();   // 0x102ad340, direct
}

// Slot 440: `0x10374370`.
	// `0x10374370`
// `0x10374370`, `CNPC_VDog::TranslateSchedule`, the body of `FElysiumNpcDog::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcDog::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber == 0x6b) { return 0x164; }
	if (ScheduleNumber == 0x156) { return 0x161; }
	if (ScheduleNumber == 0x157) { return 0x162; }
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 516: `0x10374130`, which replaces the Troika ladder.
/** `CNPC_VDog::MaxYawSpeed` `0x10374130` and `CNPC_VTzimisce::MaxYawSpeed` `0x103ba020` — the two
 *  species that replace the Troika ladder wholesale rather than adding an arm to it. Both take the
 *  same "turning" arm at `m_afMemory & 0x2000`; that arm is shared with the Troika body and lives in
 *  `MaxYawSpeedTurningArm` below. */
float FElysiumNpcDog::MaxYawSpeed()
{
	// `CNPC_VDog::MaxYawSpeed` `0x10374130`. The Troika ladder with three differences: the fall-out
	// for a recognised activity is 40.0 rather than 45.0, there is no `debug_slow_*` arm at all, and
	// the turning cvar is the Dog's own `0x1093ad24`.
	if ((ScheduleHost.MemoryBits & NpcKernelMotorShared::GMemoryTurning) != 0)
	{
		return MaxYawSpeedTurningArm(TEXT("0x1093ad24"));
	}
	if (NpcFlags.Has(EElysiumNpcFlag::PLAYING_FACE_ANIM))
	{
		return NpcKernelMotorShared::GYawDefault;
	}
	const int32 Activity = ActivityNumber;
	if (Activity < NpcKernelMotorShared::GActCrouchWalk + 1)
	{
		if (NpcKernelMotorShared::GActCrouchIdle - 1 < Activity)
		{
			return NpcKernelMotorShared::GYawCrouch;        // _DAT_104492a8 = 30.0
		}
		if (Activity == NpcKernelMotorShared::GActIdle || Activity == NpcKernelMotorShared::GActIdleAngry)
		{
			// `m_bAllowTurningAnims` ORed with the cvar the Facing family recorded (`0x109247ec`):
			// when neither is on, `debug_turning_speed` (`0x10923e84`, 90) decides; when either is, 30.0.
			if (!TurningAnimsEnabled())
			{
				return RetailYawConVarValue(TEXT("0x10923e84"));
			}
			return NpcKernelMotorShared::GYawCrouch;
		}
		if (Activity != NpcKernelMotorShared::GActRun)
		{
			return NpcKernelMotorShared::GYawDefault;
		}
	}
	else if (Activity != 0x1093 && (Activity < 0x1094 || 0x1096 < Activity))
	{
		return NpcKernelMotorShared::GYawDefault;
	}
	return NpcKernelMotorShared::GTestHullTunable;          // _DAT_10462950 = 40.0
}

// Slot 566: `0x10374aa0`, a replacement that does not chain.
bool FElysiumNpcDog::FValidateHintType(void* Hint)
{
	// `m_nHintType == 12000`.
	// Retail dereferences the hint unchecked; a hint the seam could not resolve reads type 0
	// here, which the rule refuses either way.
	const FHintWords* Words = static_cast<const FHintWords*>(Hint);
	const int32 HintType = Words != nullptr ? Words->HintType : 0;
	return HintType == 12000;
}

// Slot 546: `0x103736d0`, the class's own schedule id space.
const TCHAR* FElysiumNpcDog::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093ad64`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VDog"), TEXT("0x103736d0"), TEXT("0x1093ad64") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcKernelState19.cpp` (story 5 step 4) ---

void FElysiumNpcDog::DogCombatShortCircuit()
{
	Slot596(World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr);
	NpcKernelState19Shared::State19StampIdeal(*this, 2, 0x52a);
	Slot600(GetEnemy());
	if (SquadDisconnected < 1 && SquadWord() != 0 && GetEnemy() != nullptr)
	{
		++SelectIdealStateSquadNewEnemyCalls;
	}
}
