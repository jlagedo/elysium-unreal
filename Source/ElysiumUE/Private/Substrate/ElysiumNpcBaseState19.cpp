// `CAI_BaseNPC`'s bodies of the `State19` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseState19.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcState19Shared.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	double State19Now(const FElysiumNpcBase& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}
	/** Slot 474 `GetBestSound`. On the Troika line that is `0x102b4520` = `&m_BestSound`, so it
	 *  never answers null; the type word retail reads at `CSound +0x4` is this runtime's
	 *  `FElysiumGameSoundEvent::TypeMask`, and the two numberings are the same SOUND_* bits
	 *  (`ElysiumGameSound.h`: Combat 1, World 2, Player 4, Danger 8, BulletImpact 0x10). */
	const FElysiumGameSoundEvent* State19BestSound(FElysiumNpcBase& Npc)
	{
		return static_cast<const FElysiumGameSoundEvent*>(Npc.GetBestSound());
	}
}

// --- Moved from `ElysiumNpcState19.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::NpcStateRetail() const
{
	return Mind.NpcStateRetail();
}

int32 FElysiumNpcBase::IdealStateRetail() const
{
	return Mind.IdealStateRetail();
}

void FElysiumNpcBase::WriteNpcStateRetail(int32 RetailId)
{
	Mind.WriteNpcStateRetail(RetailId);
}

void FElysiumNpcBase::WriteIdealStateRetail(int32 RetailId)
{
	Mind.WriteIdealStateRetail(RetailId);
}

void FElysiumNpcBase::SetState(int32 NewRetail)
{
	const int32 OldAtEntry = NpcStateRetail();
	if (NewRetail != OldAtEntry)
	{
		Mind.StampLastStateChangeTime(State19Now(*this));
	}
	if (NewRetail == 1 && GetEnemy() != nullptr)
	{
		ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());
		RecordScheduleEvent(TEXT("Stripped"));
	}
	const int32 OldAfterStrip = NpcStateRetail();
	Mind.WriteNpcStateRetail(NewRetail);
	Mind.WriteIdealStateRetail(NewRetail);
	if (OldAfterStrip != NewRetail)
	{
		LastOnStateChangeOldRetail = OldAtEntry;
		LastOnStateChangeNewRetail = NewRetail;
		OnStateChange(NpcKernelState19Shared::State19TypedFromRetail(OldAtEntry, Mind.State()),
			NpcKernelState19Shared::State19TypedFromRetail(NewRetail, Mind.State()));
	}
}

int32 FElysiumNpcBase::BasePreSelectIdealState()
{
	// `0x1026f590`, slot 460's base body. The one arm's target is `this`'s own `+0x98`
	// `m_pBaseNPCTroika` (`1026f5da`), not a move parent: a Troika NPC ORs `0x80002000` into its
	// own flags2 `+0x14bc` and returns; a base-only NPC (the word is null) takes `SquadNewEnemy`
	// `0x103161a0` on its squad (`1026f61c`). Story 5 step 5 correction: the port resolved
	// `MoveParent` here.
	SelectIdealStateSelector = 1;
	if (SquadDisconnected < 1 && SquadWord() != 0
		&& (NpcStateRetail() == 1 || NpcStateRetail() == 3)
		&& NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::NewEnemy)
		&& GetEnemy() != nullptr)
	{
		if (FElysiumNpc* const Troika = AsNpc())
		{
			Troika->NpcFlags.Set(EElysiumNpcFlag2::SQUAD_NEW_ENEMY);
			return 0;
		}
		++SelectIdealStateSquadNewEnemyCalls;
	}
	return 0;
}

int32 FElysiumNpcBase::BaseSelectIdealState()
{
	SelectIdealStateSelector = 1;
	switch (NpcStateRetail())
	{
	case 1:
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 2, 0x136a);
			break;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::LightDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateMotorResets;
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x1373);
			break;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateMotorResets;
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x137c);
			break;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearWorld)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearThumper)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
		{
			// `1026f77a`: slot 474 `GetBestSound()`. On the Troika line (`0x102b4520`) that is
			// `&m_BestSound` and is never null, so this miss arm is retail's own dead branch for
			// every class this port stands; the type word retail reads is `CSound +0x4`.
			const FElysiumGameSoundEvent* const Sound = State19BestSound(*this);
			if (Sound == nullptr)
			{
				break;
			}
			++SelectIdealStateMotorResets;
			const uint32 SoundType = Sound->TypeMask;
			if (SoundType != 1u && SoundType != 8u && SoundType != 0x10u)
			{
				break;
			}
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x1397);
			break;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::Smell))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x139e);
		}
		break;
	case 2:
		if (GetEnemy() == nullptr)
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x13cc);
			// `105cc04c`, the one string all five no-enemy-combat arms share.
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s ***Combat state with no enemy!"),
				*DebugString());
			return IdealStateRetail();
		}
		return IdealStateRetail();
	case 3:
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 2, 0x13ac);
			break;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearCombat))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x13b2);
			// `1026f8d1`: the ideal state is written BEFORE the sound is fetched, so this arm
			// promotes whether or not slot 474 answers; only the motor park is conditional.
			if (State19BestSound(*this) != nullptr)
			{
				++SelectIdealStateMotorResets;
			}
			// `1026f91b`: `thunk_FUN_101e3d70(&DAT_10739a4c, this)` runs UNCONDITIONALLY at the
			// end of this arm — the discipline manager's break-on-notice sweep, whose only other
			// caller is `SetEnemy` (`0x10279a50`). It walks the entity's discipline bitmask at
			// `+0xeb4` (`0x101e3d7f`) and `RemoveEffect`s (`0x101e3af0`) every discipline whose record carries a
			// non-zero byte at `+0x32`.
			++SelectIdealStateDisciplineStripCalls;
			return IdealStateRetail();
		}
		if (ShouldGoToIdleState())
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 1, 0x13c0);
		}
		break;
	case 4:
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::TaskFailed)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::LightDamage)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateScriptExitCalls;
			ExitScriptedSequence();                   // 0x1026f9dc thunk_FUN_1027d0a0, answer ignored
			return IdealStateRetail();
		}
		return IdealStateRetail();
	case 7:
		NpcKernelState19Shared::State19StampIdeal(*this, 7, 0x13dd);
		break;
	default:
		return IdealStateRetail();
	}
	return IdealStateRetail();
}

// --- Slots 460 / 461, the base's typed bodies -----------------------------------------------------
//
// The generated virtuals return this runtime's typed state, which has no member for retail `0` (no
// change); the retail-id bodies above are the deliverable, and these answer them typed exactly as
// the Troika's `PreSelectIdealState` / `SelectIdealState` answer `PreSelectIdealStateRetail` /
// `SelectIdealStateRetail`. A signature mismatch keeps the helper's name (story 5 step 5 rule).

EElysiumNpcState FElysiumNpcBase::PreSelectIdealState()
{
	// `0x1026f590` answers 0 on every path: no change, the ideal state the mind already holds.
	return BasePreSelectIdealState() == 2 ? EElysiumNpcState::Combat : Mind.IdealState();
}

EElysiumNpcState FElysiumNpcBase::SelectIdealState()
{
	// `0x1026f660`.
	return NpcKernelState19Shared::State19TypedFromRetail(BaseSelectIdealState(), Mind.IdealState());
}
