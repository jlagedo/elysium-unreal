#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcScheduleShared.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29c-1, family **Schedule** — the schedule host and the task surface of `order.md`
// layers 0–9.
//
// 64 rows: the species table (slot 580 `GetClassScheduleIdSpace`),
// the schedule-change door `0x10280de0`, the task surface (`NextScheduledTask`, `TaskComplete`, the
// two `CAI_Motor` forwards, `IsTaskIndexCurrent`, `WaitFinished`), the two stubbed slots 418
// `ResolveTaskDistance` and 547 `GetSlotSchedule`, and the selectors — slot 604
// `SelectScheduleMeleeCombat` with its five species overrides, the slot-437/438 species overrides,
// the melee failure gate `0x102b6fe0` and the entrenched cover/kick selector `0x102b7690`. The
// walked prose is `docs/vtmb/npc-ai/schedule-kernel.md`.
//
// TWO STANDING FACTS OF THIS FAMILY.
//
//   1. **There is no per-class schedule id space.** Retail's `InitCustomSchedules` parses each
//      class's schedule text into its `CAI_ClassScheduleIdSpace`; this runtime registers programs by
//      identity. Every species row below carries the retail globals so the tables are checkable, and
//      the translation says what it cannot do rather than inventing a range.
//
//   2. **Most of what these selectors answer is not a registered program.** The species selectors
//      answer raw retail schedule numbers (0x156, 0x15a–0x165, 0x9b–0xa9, 0xe4, 0xe7, 0xe9 …), and
//      this runtime registers about thirty programs. Every body here therefore answers the RETAIL
//      NUMBER — the recovered deliverable — and the one caller that installs anything
//      (`FElysiumNpc::SelectSchedule`) maps it through the registry and tallies the miss instead of
//      installing some other program under a recovered number.

namespace
{
	// ---------------------------------------------------------------------------------------------
	// Retail data constants. Everything whose value IS in the corpus is named; everything whose
	// value is a bare `.rdata` float the corpus does not carry says UNRECOVERED and is named so one
	// edit closes it.
	// ---------------------------------------------------------------------------------------------

	// `CAI_LocalIdSpace`'s "this space holds no ids" sentinel, tested by name in `0x102ea2d0`.
	constexpr int32 GScheduleEmptyIdSpace = 9999;

	// `_DAT_104454c4` — the shared float zero, 1,328 readers.
	constexpr float GScheduleZero = ElysiumNpcTunables::Zero;

	// `_DAT_1044e664` — 10.0f, recovered by story 16a as the follower-distance overlap
	// (`Substrate/ElysiumNpcSquad.cpp`). `ResolveTaskDistance`'s fourth table entry and the
	// tentacle's phase-2 duration both read it.
	constexpr float GScheduleFollowerOverlap = ElysiumNpcTunables::Ten;

	// `_DAT_104492b8` — 200.0f, the same constant `ElysiumFootsteps.h` names as the fall-punch
	// threshold. `CNPC_VChangBros` / `CNPC_VTzimisceRunner` add it to the melee-range convar.
	constexpr float GScheduleChangMeleeMargin = ElysiumNpcTunables::TwoHundred;

	// `_DAT_1044ddb0` — `CNPC_VMingXiaoTentacle`'s enemy-distance split: the pooled f32 256.0 (0019/6 Q3
	// read the cell out of the image; the earlier "UNRECOVERED" was the value, not the address).
	constexpr float GScheduleTentacleEnemyDistUnits = ElysiumNpcTunables::Melee1OuterBand;

	// `_DAT_10450aa0`, `_DAT_10449270`, `_DAT_104bea38` — the tentacle's phase-0/3, phase-1 and
	// phase-3 expire durations. Phase 2's is `_DAT_1044e664` (10.0) and phase 1's the pooled double
	// 0.5; phase 0 is the pooled f32 4.0 and phase 3 the f32 9999.0 (0019/6 Q3: both cells read out of
	// the image).
	constexpr float GScheduleTentaclePhase0Seconds = ElysiumNpcTunables::Four;
	constexpr float GScheduleTentaclePhase1Seconds = static_cast<float>(ElysiumNpcTunables::HalfDouble);
	constexpr float GScheduleTentaclePhase3Seconds = ElysiumNpcTunables::TentaclePhase3Seconds;

	// The three hint types the cover selector's table splits on, the same three family Hints found
	// on the five activity lookups.
	constexpr int32 GScheduleHintTypeCoverLow = 100;
	constexpr int32 GScheduleHintTypeCoverMed = 0x65;
	constexpr int32 GScheduleHintTypeLean = 0x27d8;
	constexpr int32 GScheduleHintTypeShootAtA = 0x283c;
	constexpr int32 GScheduleHintTypeShootAtB = 0x283d;

	// `flags2 &= 0x7ffffeff` — the mask `0x102b7690` clears on four of its arms. Its complement is
	// `0x80000100`: the unnamed bit 31 and `COVER_VS_MELEE_MODE`.
	constexpr uint32 GScheduleCoverVsMeleeClearMask =
		0x80000000u | static_cast<uint32>(EElysiumNpcFlag2::COVER_VS_MELEE_MODE);

	// `(wpn->slot360() & 0x6000)` — the weapon-capability bits the cover selector reads off the
	// enemy's active weapon to decide whether it faces a ranged threat.
	constexpr uint32 GScheduleRangedThreatWeaponBits = 0x6000;

}

// -------------------------------------------------------------------------------------------------
// Slot 580 `GetClassScheduleIdSpace` — one method and a species table.
// -------------------------------------------------------------------------------------------------
//
// All fourteen bodies are one line: `return &DAT_<class schedule id space>;`. The table below is
// nine distinct ones on classes with an instance (`CNPC_VCamera` is shared with
// `CNPC_VCameraSecurity`) plus the Troika line's own `0x101aa790` → `DAT_10924248`. The controller
// line's three (`CNPC_VPlayerController` sharing `CNPC_VVampire`'s `0x103750e0`,
// `CNPC_VFrenzyShadow` `0x10375240`, `CNPC_VWolfMorph` `0x103dc750`) have no row since fold A2: each
// class answers its own space through the corpus, keyed on its C++ class, like every species. `CNPC_VCombatman` (`0x1036fb10`) and `CNPC_VGangrel`
// (`0x10377070`) have no instance (`population.md` § "NPC classes with no instance") and no row.
//
// The ranges are `0x102ea090(isRoot = false)`'s leftovers, exactly as family Squad found for the
// squadslot spaces — but for a DIFFERENT reason, and the difference is the recovery. Retail's
// species schedule spaces really are filled: `CNPC_VBrujah::InitCustomSchedules` (`0x10367a40`)
// calls `CAI_LocalIdSpace::Init` (`0x102ea0e0`) on `DAT_1093a740` with the schedule namespace
// `0x109203cc` and the parent `DAT_1093d258` (`CNPC_VVampire`'s), then registers
// `SCHED_VBRUJAH_WALK` 0x158 and `SCHED_VBRUJAH_WATCH` 0x159 through `0x102ea130`. This runtime
// runs no such registration, so the rows keep the empty range and `ScheduleLocalToGlobal` says so.

namespace
{
	constexpr FElysiumNpc::FScheduleIdSpace GNpcKernelScheduleIdSpaces[] = {
		// The Troika line itself.
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x101aa790"), TEXT("0x10924248") },
		{ TEXT("CNPC_VBrujah"), TEXT("0x10367870"), TEXT("0x1093a740") },
		{ TEXT("CNPC_VCamera"), TEXT("0x103683b0"), TEXT("0x1093a7d8") },
		{ TEXT("CNPC_VCameraSecurity"), TEXT("0x103683b0"), TEXT("0x1093a7d8") },
		{ TEXT("CNPC_VChangBros"), TEXT("0x1036a1b0"), TEXT("0x1093a888") },
		{ TEXT("CNPC_VChangBrosBlade"), TEXT("0x1036eab0"), TEXT("0x1093a8f0") },
		{ TEXT("CNPC_VChangBrosClaw"), TEXT("0x1036f2b0"), TEXT("0x1093a938") },
		{ TEXT("CNPC_VCop"), TEXT("0x10370930"), TEXT("0x1093ac60") },
		{ TEXT("CNPC_VDog"), TEXT("0x10373530"), TEXT("0x1093acd8") },
		{ TEXT("CNPC_VGargoyle"), TEXT("0x10377b20"), TEXT("0x1093aff0") },
		{ TEXT("CNPC_VVampire"), TEXT("0x103750e0"), TEXT("0x1093d258") },
	};
}

const FElysiumNpc::FScheduleIdSpace* FElysiumNpc::ScheduleIdSpaceRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GNpcKernelScheduleIdSpaces);
	return GNpcKernelScheduleIdSpaces;
}

// slot 580, the species half of 0x101aa790
const FElysiumLocalIdSpace* FElysiumNpc::ClassScheduleIdSpace() const
{
	// 0x101aa790, slot 580 — `CAI_BaseNPCTroika`'s override, `return &DAT_10924248`. The space is
	// the CORPUS's, keyed on this NPC's retail class, so what this answers is the same
	// `CAI_ClassScheduleIdSpace` retail returns -- filled, with its parent link and its four
	// sub-spaces.
	//
	// The table above is documentation now -- the retail class, the slot-580 body and the id space
	// that body returns, all checkable against `docs/vtmb/npc-kernel/slots.md`. The LIVE space is
	// the corpus's, keyed on this NPC's own retail class rather than on the slot-580 override row,
	// because the sidecar already folds the classes that share one space.
	return IdSpace(EElysiumIdCategory::Schedule);
}

// -------------------------------------------------------------------------------------------------
// The schedule-change door.
// -------------------------------------------------------------------------------------------------

// `CCineAI::FixScriptNPCSchedule` (slot 586, `0x101a95d0`) is the director's own body since story 5
// fold A3 (`FElysiumAiScriptedSequence`).

// 0x102ae840 — the scripted-schedule order push
void FElysiumNpc::AcceptScriptedScheduleOrder(int32 OrderId, bool bForce)
{
	// `if (m_NPCState != 7 && m_IdealNPCState != 7)` — retail state 7 is dead.
	if (Mind.State() == EElysiumNpcState::Dead || Mind.IdealState() == EElysiumNpcState::Dead)
	{
		return;
	}
	// `if (IsAlive() || param_2)` — slot 158. Its generated stub answers false; the port carries the
	// fact on the entity, so `!IsDead()` is the answer and the slot is named beside it.
	if (IsDead() && !bForce)
	{
		return;
	}
	// `+0x65cc = param_1` — the port's `+0x65cc` is the whole pushed order (`the forced state
	// travels with the pushed order`), so the id is recorded on it rather than beside it.
	ScriptedScheduleOrder.RetailOrderId = OrderId;
	// `m_bForceStateChange (+0x1b28) = 1`.
	Mind.ForceStateChange();
	// `m_bfAINPCFlags2 |= 0x82000000` — `CHOOSE_NEW_SCHEDULE` and the unnamed bit 31.
	NpcFlags.Set(EElysiumNpcFlag2::CHOOSE_NEW_SCHEDULE);
	NpcFlags.SetRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
}

// -------------------------------------------------------------------------------------------------
// The task surface.
// -------------------------------------------------------------------------------------------------

// 0x102623a0 `CAI_Motor` slot 1
void FElysiumNpc::MotorTaskFail(int32 Reason)
{
	// `MOV ECX,[ECX+4]; JMP [[ECX]+0x700]` — the owner's slot 448, unchanged. `FElysiumNpc::TaskFail`
	// IS slot 448 (`0x1029adb0`), ported in story 13; family Motor's `ValidateNavGoal` already calls
	// it with `0x1b`. This adds the motor's door and nothing else.
	TaskFail(Reason);
}

// -------------------------------------------------------------------------------------------------
// slot 418 `ResolveTaskDistance` — the stubbed slot, plus two species overrides.
// -------------------------------------------------------------------------------------------------

// slot 418 0x102bf6e0 `float ResolveTaskDistance(float)`
float FElysiumNpc::ResolveTaskDistance(float Distance)
{
	// `CNPC_VMingXiao` (`0x10392a10`) and `CNPC_VTzimisce` (`0x103b9120`) override slot 418 on their
	// C++ classes (story 5 step 3): each tests its own sentinel and only then calls this body.
	const int32 Truncated = static_cast<int32>(Distance);   // retail's `__ftol`

	// 0x102bf6e0, the Troika line: a four-entry jump table on `(int)param + 1000008`.
	switch (Truncated + 1000008)
	{
	case 0:   // -1000008
		return FollowerDistanceBackAway;
	case 1:   // -1000007
		return FollowerDistanceWalkTo;
	case 2:   // -1000006
		return FollowerDistanceRunTo;
	case 3:   // -1000005
		return GScheduleFollowerOverlap;
	default:
		break;
	}
	// `CAI_BaseNPC::ResolveTaskDistance` (`0x102702d0`), layer 0 and NOT one of this story's rows:
	// it splits -1000003 (through a global's slot 1), -1000002 and -1000000 and otherwise answers
	// the truncated value. Only the pass-through arm is reproduced; the three sentinels are named.
	if (Truncated == -1000003 || Truncated == -1000002 || Truncated == -1000000)
	{
		ElysiumStub::Fired(TEXT("schedule"), TEXT("CAI_BaseNPC::ResolveTaskDistance 0x102702d0"),
			DebugString(), FString::FromInt(Truncated), TEXT("0002/29c: the base sentinel arms"));
	}
	return static_cast<float>(Truncated);
}

// -------------------------------------------------------------------------------------------------
// The species half of slot 438 `SelectSchedule`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::SpeciesSelectSchedule()
{
	// Slot 438 on the bare Troika line (`CAI_BaseNPCTroika`, `CNPCMaker*`, `CNPC_VBaseBoss`,
	// `CNPC_VNewscaster` and every class that inherits the slot unchanged) is
	// `CAI_BaseNPCTroika::SelectSchedule` `0x102af660`, ported as `TroikaSelectSchedule`
	// (`ElysiumNpcSelect.cpp`, story 8 Select19). Species bodies override this virtual.
	return TroikaSelectSchedule();
}

// -------------------------------------------------------------------------------------------------
// The melee failure gate and slot 604 `SelectScheduleMeleeCombat`.
// -------------------------------------------------------------------------------------------------

// 0x102b6fe0
int32 FElysiumNpc::MeleeScheduleFailureGate(FElysiumEntity* Enemy)
{
	// Two identical shapes, one per condition, in retail's order.
	const bool bOccluded = Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded);
	const bool bBlocked = !bOccluded && Cognition.Conditions.Has(EElysiumNpcCond::EnemyBlocked);
	if (!bOccluded && !bBlocked)
	{
		return 0;
	}
	if (NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this))   // slot 308
	{
		if (bInMelee)
		{
			Slot601(Enemy);   // vtable +0x964, `void vfunc601(CBaseEntity*)`; retail name unrecovered
		}
		RecordScheduleEvent(bOccluded
			? TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23135 -> 0xe9")
			: TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23159 -> 0xe9"));
		return 0xe9;
	}
	if (bOccluded)
	{
		RecordScheduleEvent(TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23145 -> 0xcd"));
		return 0xcd;
	}
	RecordScheduleEvent(TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23169 -> 0xce"));
	return 0xce;
}
// slot 604 0x102b6c30 `int SelectScheduleMeleeCombat(int)`, the Troika line
int32 FElysiumNpc::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;   // retail's argument is read by no arm of any of the six bodies
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumEntity* Enemy = const_cast<FElysiumEntity*>(World != nullptr
		? ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy) : nullptr);
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// Eight species classes override slot 604 on their C++ classes (story 5 step 3): the human line
	// `0x10385e40`, MingXiao `0x10396050`, Bach `0x10364080`, the Chang brothers `0x1036d800`, the
	// Tzimisce runner `0x103c4430`, AsianVampire `0x10361be0`, SheriffMan `0x103af960` and
	// SabbatLeader `0x103aa060`. Each REPLACES this body wholesale.

	// --- The Troika line 0x102b6c30 ----------------------------------------------------------------
	if (!bInMelee)
	{
		if (!Slot599(0))
		{
			if (const int32 Gate = MeleeScheduleFailureGate(Enemy); Gate != 0)
			{
				return Gate;
			}
			RecordScheduleEvent(TEXT("SelectScheduleMeleeCombat AI_BaseNPCTroika.cpp:22943 -> 0xe4"));
			return 0xe4;
		}
	}
	else if (Slot602())
	{
		Slot601(Enemy);
		if (NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this))
		{
			return 0xe9;
		}
		return ScheduleHost.EnemyDistUnits > MeleeRangeUnits() * 2.0f ? 0xe7 : 0xe4;
	}
	if (const int32 Gate = MeleeScheduleFailureGate(Enemy); Gate != 0)
	{
		return Gate;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		return NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this) ? 0xdc : 0xdd;
	}
	const bool bHeightArmed = NpcKernelScheduleShared::TickMeleeHeightDiffTimer(*this, Now);
	if (NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this)
		&& (Conds.Has(EElysiumNpcCond::TooFarForMelee) || Conds.Has(EElysiumNpcCond::InterruptTime)
			|| Conds.Has(EElysiumNpcCond::EnemyUnreachable) || bHeightArmed))
	{
		Slot601(Enemy);
		return 0xe9;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(Enemy);
		return 0x17;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee) && !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		return 199;
	}
	return NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this) ? 0xca : 0xcb;
}
// `CNPC_VChangBros::SelectScheduleMeleeCombat` `0x1036d800` and `CNPC_VTzimisceRunner`'s
// `0x103c4430`: one shape, two schedule id sets (`bChang`). The `0x59` and `0x15a`/`0xe1` arms are
// where they differ. Each class's slot-604 override calls it (story 5 step 3).
int32 FElysiumNpc::SelectScheduleMeleeCombatChangLine(bool bChang)
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumEntity* Enemy = const_cast<FElysiumEntity*>(World != nullptr
		? ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy) : nullptr);
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	if (!bInMelee && !Slot599(0))
	{
		// SEAM: slot 599 takes the enemy in retail (`GetEnemy()` then `vfunc599(enemy)`); the
		// ledger types its parameter `int`, so the generated signature cannot carry a pointer
		// and the port passes 0. Named rather than hidden.
		const float Threshold = MeleeRangeUnits() + GScheduleChangMeleeMargin;
		if (ScheduleHost.EnemyDistUnits <= Threshold)
		{
			return 0xe4;
		}
		return 0xe7;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyOccluded))
	{
		return 0xcd;
	}
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
		EElysiumNpcCond::ShouldDodge))
	{
		return 0xd5;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		return 0xdd;
	}
	const bool bHeightArmed = NpcKernelScheduleShared::TickMeleeHeightDiffTimer(*this, Now);
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		return bChang ? 0x15d : 0x17;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee)
		&& !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		return 199;
	}
	// `GetEnemy()` twice, then the enemy's `WorldSpaceCenter` (slot 192) into a discarded
	// stack vector, then `0x102a11d0`.
	if (Enemy != nullptr && ScheduleMeleeReachGate())
	{
		return bChang ? 0x15a : 0xe1;
	}
	if (ScheduleHost.EnemyDistUnits <= MeleeRangeUnits() && !bHeightArmed)
	{
		return 0xd2;
	}
	return 0xcb;
}

// -------------------------------------------------------------------------------------------------
// 0x102b7690 — the entrenched cover / kick-prop selector.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::SelectCoverOrKickSchedule(const FScheduleHintSearchRequest& Request)
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const bool bHasHintNode = BaseScheduleHost.HintNode != INDEX_NONE;
	const bool bDodging = NpcFlags.Has(EElysiumNpcFlag::DODGING);   // flags1 0x800
	FElysiumEntity* Prop = World != nullptr ? World->Resolve(ScheduleHost.KickProp) : nullptr;

	// A. The kick-physics-prop refresh, on its own two-second timer.
	if (Request.bRequest4 && ScheduleHost.bAllowKickHintUse && !bHasHintNode && !bDodging
		&& Prop == nullptr && ScheduleHost.KickPropSearchTimer <= Now)
	{
		// `RandomFloat(2.0, 2.0)` — a draw whose bounds are equal, so the interval is exactly two
		// seconds. Reproduced as the draw retail makes, because it advances the stream.
		ScheduleHost.KickPropSearchTimer =
			Now + ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(2.0f, 2.0f);
		Prop = FindKickPhysicsProp();
		ScheduleHost.KickProp = Prop != nullptr ? Prop->Handle : FElysiumEntityHandle::Invalid();
	}

	// B. A live kick prop short-circuits the whole hint search.
	if (Prop != nullptr)
	{
		return 0xa9;   // SCHED_TROIKA_KICK_PROP_AT_ENEMY
	}

	// C. The hint search, under the same three gates.
	if (!bHasHintNode && !bDodging && Prop == nullptr)
	{
		const FElysiumEntity* Enemy = World != nullptr
			? ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy) : nullptr;
		ScheduleHost.HintCoverObject =
			Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle::Invalid();
		uint32 SearchMask = Request.bRequest1 ? 1u : 0u;
		if (Request.bRequest2)
		{
			SearchMask |= 2u;
		}
		if (Request.bRequest3 && ScheduleHost.bAllowKickHintUse)
		{
			SearchMask |= 4u;
		}
		if (Request.bRequest4 && ScheduleHost.bAllowKickHintUse)
		{
			SearchMask |= 8u;
		}
		SearchForCoverHint(SearchMask);
	}

	// D. The hint-type table. Retail reads `m_pHintNode->m_nHintType` (`+0x5dc`).
	FHintWords Hint;
	if (BaseScheduleHost.HintNode == INDEX_NONE || !HintWords(BaseScheduleHost.HintNode, Hint))
	{
		return 0;
	}
	if (Hint.HintType == GScheduleHintTypeShootAtA)
	{
		RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23812 -> 0xa7"));
		return 0xa7;
	}
	if (Hint.HintType == GScheduleHintTypeShootAtB)
	{
		RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23818 -> 0xa8"));
		return 0xa8;
	}
	if (Hint.HintType != GScheduleHintTypeCoverLow && Hint.HintType != GScheduleHintTypeCoverMed
		&& Hint.HintType != GScheduleHintTypeLean)
	{
		return 0;
	}

	// The cover-hint tail.
	if (!NpcFlags.Has(EElysiumNpcFlag::AT_COVER_HINT))   // flags1 0x2000
	{
		RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23805 -> 0x9b"));
		NpcFlags.ClearRawWord2Bits(GScheduleCoverVsMeleeClearMask);
		return 0x9b;
	}

	// "Does my enemy carry a ranged threat" — the enemy's own enemy's active weapon reporting
	// `0x6000` in its capability word (slot 360). `bNoRangedThreat` is retail's `bVar1`.
	bool bNoRangedThreat = true;
	if (const FElysiumEntity* Enemy = World != nullptr
		? ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy) : nullptr)
	{
		// SEAM: retail reads `enemy->+0x9c` — `CBaseCombatCharacter`'s cached downcast of ITSELF
		// (`docs/vtmb/npc-ai/schedule-kernel.md` § "The three cached downcasts") — and asks that
		// character's active weapon. This substrate has no cross-entity weapon-capability reader, so
		// the threat is never seen and the `bVar1 == true` arm is the one taken.
		ElysiumStub::Fired(TEXT("schedule"),
			TEXT("SelectCoverOrKickSchedule enemy ranged-threat bits 0x6000"), DebugString(),
			*Enemy->DebugString(), TEXT("0002/29c-1: no cross-entity weapon capability reader"));
		(void)GScheduleRangedThreatWeaponBits;
	}

	if (ScheduleHost.ShootAtHintNode == 0)
	{
		// `m_pShootAtHint = vfunc609(false)` (vtable `+0x984`, `CAI_Hint*`). The port carries hints
		// as node indices, and slot 609 is a generated stub answering null, so the write is null and
		// the inner arm below is the one taken.
		void* const ShootAtHint = Slot609(false);
		if (ShootAtHint == nullptr)
		{
			if (!HintIdleActivityGate())   // 0x102b5de0
			{
				++PeekOutCount;
				if (PeekOutCount < 5 && !Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded))
				{
					RecordScheduleEvent(
						TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23797 -> 0x9d"));
					return 0x9d;
				}
				NpcFlags.ClearRawWord2Bits(GScheduleCoverVsMeleeClearMask);
				if (!bStayEntrenched)
				{
					// `CAI_BaseNPCTroika::ClearHintNode(60.0)` and no schedule.
					ClearScheduleHint(60.0f);
					return 0;
				}
				RecordScheduleEvent(
					TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23792 -> 0x9e"));
				return 0x9e;
			}
			// The gate passed: the peek-out counter decays by two with a floor at zero.
			PeekOutCount = FMath::Max(PeekOutCount - 2, 0);
			// `m_pSchedule == NULL || m_pSchedule != GetScheduleOfType(0x9e)` — "I am not already
			// running 0x9e" — and `COVER_VS_MELEE_MODE` (flags2 0x100) clear.
			const bool bNotAlready9e = !Schedule.IsRunning()
				|| GetLocalScheduleId(Schedule.Current) != 0x9e;
			const bool bCoverVsMelee = NpcFlags.Has(EElysiumNpcFlag2::COVER_VS_MELEE_MODE);
			if (!bNoRangedThreat)
			{
				const int32 Roll =
					ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99);
				if (bNotAlready9e && !bCoverVsMelee)
				{
					RecordScheduleEvent(
						TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23769 -> 0xa3"));
					return 0xa3;
				}
				NpcFlags.ClearRawWord2Bits(GScheduleCoverVsMeleeClearMask);
				return Roll > 0x1d ? 0xa0 : 0xa1;
			}
			if (bNotAlready9e && !bCoverVsMelee)
			{
				RecordScheduleEvent(
					TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23747 -> 0xa4"));
				return 0xa4;
			}
			RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23743 -> 0xa5"));
			NpcFlags.Set(EElysiumNpcFlag2::COVER_VS_MELEE_MODE);
			NpcFlags.SetRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
			return 0xa5;
		}
	}
	RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23725 -> 0xa2"));
	return 0xa2;
}

// -------------------------------------------------------------------------------------------------
// The seams.
// -------------------------------------------------------------------------------------------------

FElysiumEntity* FElysiumNpc::FindKickPhysicsProp() const
{
	// SEAM for `0x102b6650`, which reads `GetEnemy()` (`+0x29c`) and `GetAbsOrigin()` (`+0x364`) and
	// sweeps for a kickable `prop_physics`. `FElysiumNpc::TaskFail` already knows how to release one
	// (`ScheduleHost.KickProp`), so only the producer is missing.
	return nullptr;
}

void FElysiumNpc::SearchForCoverHint(uint32 SearchMask)
{
	// SEAM for `0x102b7110`, the hint search that writes `m_pHintNode` (`+0x5ddc`). It reads
	// `m_bLeaningLeft`, the cover-LOS counters (`+0x6404`/`+0x6408`), `m_iPeekOutCount` and
	// `m_bStayEntrenched` and is the Hints layer's producer, not this family's.
	ElysiumStub::Fired(TEXT("hints"), TEXT("CAI_BaseNPCTroika::SearchForCoverHint 0x102b7110"),
		DebugString(), FString::Printf(TEXT("mask=0x%x"), SearchMask),
		TEXT("0002/29c-1: no hint-node store"));
}

bool FElysiumNpc::ScheduleMeleeReachGate() const
{
	// SEAM for `0x102a11d0`. Retail name unrecovered; four direct callers, reads `+0x300` (the
	// enemy's `WorldSpaceCenter`) and `+0x650`.
	return false;
}

int32 FElysiumNpc::NavigatorNavType() const
{
	// `0x1027d990` -- `m_pNavigator->+0x18`, `m_navType` (`FUN_1027d990`, the NPC-level `GetNavType`),
	// the probe `CNPC_VManBat::SelectSchedule` (`0x1038e340`) opens with: 0 ground, 1 jump, 2 fly, 3
	// climb (a flyer's type is set to 2 through `0x1027d9b0`). Not the goal type -- the port's old name
	// for this word, `NavigatorGoalType`, misread it. Constructor default 0.
	return NavGetType();
}
