// Story 0019/8 (29e under the strict verdict), family **Select19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBaseSelect.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated
// stub moved here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops
// the marker.
//
// Owns (Select19's `rule` rows): 0x1028a380 CAI_BaseNPC::SelectSchedule.
//
// Every arm below carries the address of the instruction it came from, read off the listing
// (`vtmb_asm 0x1028a380`) and the packet's merged walk (`Select19-READING.md`). Condition and
// schedule ids are retail's registered numbers: the condition word is class-local
// (`FElysiumNpcConditions`), so a condition with no port enumerator is spelled as its number.

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumScheduleNumbers.h"
#include "Substrate/ElysiumScriptedSequence.h"

namespace
{
	// `0x1049a1a0` f64 = 60.0 — the case-3 facing test's scale (`0x1028a5d1 FMUL double ptr`). Not a
	// row of `ElysiumNpcKernelTunables.h` (its 60.0 is the f32 cell `0x104492a4`, a different cell);
	// read out of the pinned image by the packet's Globals section.
	constexpr double GBaseSelectFacingScale = 60.0;

	// The retail activities the ladder asks `SelectWeightedSequence` for.
	constexpr int32 GBaseSelectActSmallFlinch = 0x49;   // `0x1028a482` / `0x1028a603` / `0x1028a6c9`
	constexpr int32 GBaseSelectActEnemyDead = 0x61;     // `0x1028a4f7`

	// `IRelationType`'s `D_FR` (`0x1028a70a CMP EAX,0x2`).
	constexpr int32 GBaseSelectDispositionFear = 2;

	// The base's registered schedules this body answers (`CAI_BaseNPC` local ids, 0x00..0x43, corpus
	// unit `cai_basenpc`, `space.json` registrations). The ones `ElysiumScheduleNumbers.h` names are
	// read from it; the rest carry the registered name beside the number.
	constexpr int32 GBaseSchedWakeAngry = 0x05;           // WAKE_ANGRY
	constexpr int32 GBaseSchedAlertFace = 0x06;           // ALERT_FACE
	constexpr int32 GBaseSchedAlertScan = 0x08;           // ALERT_SCAN
	constexpr int32 GBaseSchedAlertStand = 0x09;          // ALERT_STAND
	constexpr int32 GBaseSchedCombatFace = 0x0b;          // COMBAT_FACE
	constexpr int32 GBaseSchedFearFace = 0x0d;            // FEAR_FACE
	constexpr int32 GBaseSchedChaseEnemy = 0x0f;          // CHASE_ENEMY
	constexpr int32 GBaseSchedBackAwayFromEnemy = 0x15;   // BACK_AWAY_FROM_ENEMY
	constexpr int32 GBaseSchedRunFromEnemy = 0x1b;        // RUN_FROM_ENEMY
	constexpr int32 GBaseSchedMeleeAttack1 = 0x1f;        // MELEE_ATTACK1
	constexpr int32 GBaseSchedMeleeAttack2 = 0x20;        // MELEE_ATTACK2
	constexpr int32 GBaseSchedRangeAttack1 = 0x21;        // RANGE_ATTACK1
	constexpr int32 GBaseSchedRangeAttack2 = 0x22;        // RANGE_ATTACK2
	constexpr int32 GBaseSchedGiveWay = 0x38;             // GIVE_WAY
	constexpr int32 GBaseSchedAiScript = ElysiumSched::AISCRIPT;             // 0x2e
	constexpr int32 GBaseSchedDie = ElysiumSched::DIE;                       // 0x2b
	constexpr int32 GBaseSchedDieRagdoll = ElysiumSched::SCHED_DIE_RAGDOLL;  // 0x2c
	constexpr int32 GBaseSchedFail = ElysiumSched::FAIL;                     // 0x43

	// `m_flFieldOfView` (`+0x1574`), the cone half-angle cosine the case-3 facing test reads. Story 29c-1's
	// view-cone arm answers this word from the sense layer's default cone, which is the same question.
	float RetailFieldOfViewDot(const FElysiumNpc& Npc)
	{
		return Npc.FieldOfView;
	}

}

bool FElysiumNpcBase::SelectBecomeClientRagdoll()
{
 ++SelectRagdollRequests;
 FElysiumNpc* const RagdollNpc = AsNpc();
 return RagdollNpc != nullptr && RagdollNpc->BecomeClientRagdoll(FVector::ZeroVector, INDEX_NONE, false); // 0x1028a8f7
}

// =================================================================================================
// `0x1028a260` — the selector pair.
// =================================================================================================

int32 FElysiumNpcBase::SelectNewScheduleRetail()
{
	const int32 Pre = PreSelectSchedule();          // 0x1028a26d CALL [EAX+0x6d4], slot 437
	if (Pre != 0)                                   // 0x1028a275 TEST EAX,EAX
	{
		return Pre;
	}
	// 0x1028a27e JMP [EAX+0x6d8] — slot 438. On the Troika line the slot is
	// `FElysiumNpc::SpeciesSelectSchedule`; a base-only NPC's slot 438 is this class's body.
	if (FElysiumNpc* Troika = AsNpc())
	{
		return Troika->SpeciesSelectSchedule();
	}
	return BaseSelectSchedule();
}

// =================================================================================================
// Slot 438 base — `CAI_BaseNPC::SelectSchedule` `0x1028a380`, 1650 bytes.
// =================================================================================================

int32 FElysiumNpcBase::BaseSelectSchedule()
{
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	const TCHAR* Warning = nullptr;
	switch (NpcStateRetail())                       // 0x1028a383 / 0x1028a396 JA / 0x1028a39c table
	{
	case 0:                                         // 0x1028a3a3
		Warning = TEXT("NPC_STATE IS NONE!\n");         // 0x105ce114, verbatim
		break;
	case 1:                                         // 0x1028a3c8 — IDLE
		if (Cond.Has(EElysiumNpcCond::HearDanger)           // 0x1028a3cc CALL / 0x1028a3d3
			|| Cond.Has(EElysiumNpcCond::HearCombat)        // 0x1028a3dd CALL / 0x1028a3e4
			|| Cond.Has(EElysiumNpcCond::HearWorld)         // 0x1028a3ee CALL / 0x1028a3f5
			|| Cond.Has(EElysiumNpcCond::HearBulletImpact)  // 0x1028a3ff CALL / 0x1028a406
			|| Cond.Has(EElysiumNpcCond::HearPlayer))       // 0x1028a410 CALL / 0x1028a417
		{
			return GBaseSchedAlertFace;   // 0x1028a4c9
		}
		if (Cond.Has(EElysiumNpcCond::GiveWay))             // 0x1028a421 CALL / 0x1028a428
		{
			return GBaseSchedGiveWay;     // 0x1028a42a
		}
		if (NavigatorPathType() == 0)                       // 0x1028a44b CALL 0x102ee620 / 0x1028a452
		{
			return ElysiumSched::IDLE_STAND;  // 0x1028a454
		}
		if (Cond.Has(EElysiumNpcCond::LightDamage)          // 0x1028a473 CALL / 0x1028a47a
			&& SelectWeightedSequenceForActivity(GBaseSelectActSmallFlinch) != INDEX_NONE)  // 0x1028a48a
		{
			Cognition.bCondTookDamage = false;              // 0x1028a48c
			return ElysiumSched::SMALL_FLINCH;  // 0x1028a493
		}
		return ElysiumSched::IDLE_WALK;   // 0x1028a4ae
	case 2:                                         // 0x1028a639 — COMBAT
	{
		if (Cond.Has(EElysiumNpcCond::NewEnemy))                    // 0x1028a63d CALL / 0x1028a644
		{
			return GBaseSchedWakeAngry;   // 0x1028a646
		}
		if (Cond.Has(EElysiumNpcCond::EnemyDead))           // 0x1028a665 CALL / 0x1028a66e
		{
			ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());   // 0x1028a672
			// 0x1028a679 `ChooseEnemy` (`0x10279dd0`, ported by L11 on the base).
			if (ElysiumNpcEnemy::ChooseEnemy(*this))        // 0x1028a682
			{
				Cognition.Conditions.Clear(EElysiumNpcCond::EnemyDead);   // 0x1028a686
				return SelectNewScheduleRetail();           // 0x1028a68e tail JMP 0x1028a260
			}
			SetState(3);                                    // 0x1028a695 CALL 0x1026e340(3)
			return SelectNewScheduleRetail();               // 0x1028a69d tail JMP 0x1028a260
		}
		if ((Cond.Has(EElysiumNpcCond::LightDamage)         // 0x1028a6a4 CALL / 0x1028a6ab
				|| Cond.Has(EElysiumNpcCond::HeavyDamage))  // 0x1028a6b1 CALL / 0x1028a6b8
			&& (BaseScheduleHost.MemoryBits & 0x40u) == 0   // 0x1028a6c1 TEST byte [+0x5d8c],0x40
			&& SelectWeightedSequenceForActivity(GBaseSelectActSmallFlinch) != INDEX_NONE)  // 0x1028a6d1
		{
			Cognition.bCondTookDamage = false;              // 0x1028a6d3
			return ElysiumSched::SMALL_FLINCH;  // 0x1028a6da
		}
		FElysiumEntity* const Enemy = GetEnemy();           // 0x1028a6fa slot 167
		if (IRelationType(Enemy) == GBaseSelectDispositionFear)   // 0x1028a703 slot 404 / 0x1028a711
		{
			if (!Cond.Has(EElysiumNpcCond::SeeEnemy)            // 0x1028a713 CALL / 0x1028a71a
				&& !Cond.Has(EElysiumNpcCond::LightDamage)      // 0x1028a720 CALL / 0x1028a727
				&& !Cond.Has(EElysiumNpcCond::HeavyDamage))     // 0x1028a72d CALL / 0x1028a734
			{
				return GBaseSchedFearFace;   // 0x1028a736
			}
			Cognition.bCondTookDamage = false;              // 0x1028a755
			FearSound();                                    // 0x1028a75c slot 492
			return GBaseSchedRunFromEnemy;   // 0x1028a762
		}
		if (!Cond.Has(EElysiumNpcCond::SeeEnemy))           // 0x1028a77d CALL / 0x1028a786
		{
			// ENEMY_OCCLUDED set -> `0x1028a7ae` (0xed4 CHASE_ENEMY); clear -> `0x1028a79d` (0xecf
			// COMBAT_FACE).
			if (!Cond.Has(EElysiumNpcCond::EnemyOccluded))  // 0x1028a78a CALL / 0x1028a79b
			{
				return GBaseSchedCombatFace;   // 0x1028a79d
			}
			return GBaseSchedChaseEnemy;      // 0x1028a7ae
		}
		if (Cond.Has(EElysiumNpcCond::TooCloseToAttack))    // 0x1028a7c1 CALL / 0x1028a7c8
		{
			return GBaseSchedBackAwayFromEnemy;         // 0x1028a7ca
		}
		if (Cond.Has(EElysiumNpcCond::CanRangeAttack1))     // 0x1028a7e9 CALL / 0x1028a7f0
		{
			return GBaseSchedRangeAttack1;     // 0x1028a7f2
		}
		if (Cond.Has(EElysiumNpcCond::CanRangeAttack2))     // 0x1028a811 CALL / 0x1028a818
		{
			return GBaseSchedRangeAttack2;     // 0x1028a81a
		}
		if (Cond.Has(EElysiumNpcCond::CanMeleeAttack1))     // 0x1028a839 CALL / 0x1028a840
		{
			return GBaseSchedMeleeAttack1;     // 0x1028a842
		}
		if (Cond.Has(EElysiumNpcCond::CanMeleeAttack2))     // 0x1028a861 CALL / 0x1028a868
		{
			return GBaseSchedMeleeAttack2;     // 0x1028a86a
		}
		if (Cond.Has(EElysiumNpcCond::NotFacingAttack))     // 0x1028a889 CALL / 0x1028a890
		{
			return GBaseSchedCombatFace;       // 0x1028a892
		}
		// 0x1028a8b8 / 0x1028a8c5 — re-tests 0x4f and 0x51, which the ladder above has already
		// answered, so this pair is always false here: retail's own dead half, reproduced.
		if (!Cond.Has(EElysiumNpcCond::CanRangeAttack1)     // 0x1028a8b1 CALL / 0x1028a8b8
			&& !Cond.Has(EElysiumNpcCond::CanMeleeAttack1)) // 0x1028a8be CALL / 0x1028a8c5
		{
			return GBaseSchedChaseEnemy;      // 0x1028a8c7
		}
		Warning = TEXT("No suitable combat schedule!\n");  // 0x1028a8e2
		break;
	}
	case 3:                                         // 0x1028a4e4 — ALERT
		if (Cond.Has(EElysiumNpcCond::EnemyDead)            // 0x1028a4e8 CALL / 0x1028a4ef
			&& SelectWeightedSequenceForActivity(GBaseSelectActEnemyDead) != INDEX_NONE)  // 0x1028a4ff
		{
			return GBaseSchedAlertScan;  // 0x1028a501
		}
		if (!Cond.Has(EElysiumNpcCond::LightDamage)         // 0x1028a520 CALL / 0x1028a527
			&& !Cond.Has(EElysiumNpcCond::HeavyDamage))     // 0x1028a531 CALL / 0x1028a538
		{
			if (!Cond.Has(EElysiumNpcCond::HearDanger)          // 0x1028a53e CALL / 0x1028a545
				&& !Cond.Has(EElysiumNpcCond::HearPlayer)       // 0x1028a54b CALL / 0x1028a552
				&& !Cond.Has(EElysiumNpcCond::HearWorld)        // 0x1028a558 CALL / 0x1028a55f
				&& !Cond.Has(EElysiumNpcCond::HearBulletImpact) // 0x1028a565 CALL / 0x1028a56c
				&& !Cond.Has(EElysiumNpcCond::HearCombat))      // 0x1028a572 CALL / 0x1028a579
			{
				return GBaseSchedAlertStand;   // 0x1028a57b
			}
			return GBaseSchedAlertFace;        // 0x1028a596
		}
		{
			Cognition.bCondTookDamage = false;              // 0x1028a5b7
			// 0x1028a5be `0x102e1f90(m_pMotor)` DeltaIdealYaw, made absolute, against
			// `(1.0 - m_flFieldOfView) * 60.0` (`0x1028a5c5`..`0x1028a5d1`); strict less-than.
			// `m_flFieldOfView` (`+0x1574`) is carried on the combat character (`FieldOfView`); a
			// base-only NPC stands no such word and never enters ALERT (CAI_TestHull and the cine
			// directors), so it reads 0 — a crash guard, not a rule.
			const FElysiumNpc* const Troika = AsNpc();
			const double Fov = Troika != nullptr ? static_cast<double>(RetailFieldOfViewDot(*Troika)) : 0.0;
			const double Threshold = (ElysiumNpcTunables::OneDouble - Fov) * GBaseSelectFacingScale;
			if (FMath::Abs(static_cast<double>(MotorDeltaIdealYaw())) < Threshold)   // 0x1028a5e0
			{
				return ElysiumSched::TAKE_COVER_FROM_ORIGIN;  // 0x1028a5e2
			}
			if (SelectWeightedSequenceForActivity(GBaseSelectActSmallFlinch) != INDEX_NONE)  // 0x1028a615
			{
				return ElysiumSched::ALERT_SMALL_FLINCH;  // 0x1028a617
			}
			return GBaseSchedAlertFace;            // 0x1028a628
		}
	case 4:                                         // 0x1028a92c — SCRIPT
		// 0x1028a935 / 0x1028a950 / 0x1028a955 — `m_hCine` (`+0x5d74`) resolves to a live entity.
		if (ResolveCine() != nullptr)
		{
			return GBaseSchedAiScript;         // 0x1028a991
		}
		// 0x1028a959 GetClassname / 0x1028a966 DevWarning(2, ...)
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Script failed for %s"),   // 0x105ce0d4
			Def != nullptr ? *Def->Classname : TEXT(""));
		FElysiumScriptedSequence::CineCleanup(*this);   // 0x1028a971 CALL 0x1027d170
		return ElysiumSched::IDLE_STAND;       // 0x1028a976
	case 6:                                         // 0x1028a3ad
		return ElysiumSched::IDLE_STAND;
	case 7:                                         // 0x1028a8ec — DEAD
		if (SelectBecomeClientRagdoll())                    // 0x1028a8f7 / 0x1028a908
		{
			return GBaseSchedDieRagdoll;       // 0x1028a90a
		}
		return GBaseSchedDie;                  // 0x1028a91b
	case 0xc:                                       // 0x1028a9ac
		return ElysiumSched::IDLE_STAND;
	default:                                        // 0x1028a9c7 (5, 8..0xb and > 0xc)
		Warning = TEXT("Invalid State for SelectSchedule!\n");
		break;
	}
	// 0x1028a9ce DevWarning(2, message), then line 0xf21 and FAIL.
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s"), Warning);
	return GBaseSchedFail;                     // 0x1028a9d7
}
