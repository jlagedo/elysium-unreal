// Story 0019/8 (29e under the strict verdict), family **Conditions19** -- `CAI_BaseNPCTroika`'s
// bodies.
//
// Declarations are in `ElysiumNpcConditions2.inl` (included inside `class FElysiumNpc`) or
// generated in `ElysiumNpcSlots.inl` for a slot body. Walked prose:
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Story 8, family Conditions19".
//
// Owns (Conditions19's `rule` rows): 0x102b27f0 CAI_BaseNPCTroika::GatherConditions; and the Troika
// half of `CAI_BaseNPC::GatherConditions` (`0x1026ec30`, `1026eec1..1026efa4`) with its two uncatalogued
// callees `0x1028e790` / `0x1028e700`.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSightTrace.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"

// =================================================================================================
// The Troika half of `CAI_BaseNPC::GatherConditions` (`0x1026ec30`)
// =================================================================================================

void FElysiumNpc::Conditions19UpdateApproachGoalPos(FElysiumEntity* GoalEntity)
{
	(void)GoalEntity;
	++Conditions19ApproachGoalCalls;
}

void FElysiumNpc::Conditions19RefreshGoalPosition(FElysiumEntity* GoalEntity)
{
	(void)GoalEntity;
	++Conditions19RefreshGoalCalls;
}

void FElysiumNpc::Conditions19OcclusionReportUpkeep()
{
	// `0x1028e790`.
	const FElysiumNpcBase* const ConstThis = this;
	const double Now = Conditions19Now();
	if (ConstThis->GetEnemy() != nullptr)                                // 1028e795 slot 167 / 1028e79d
	{
		RefreshOccludedCondition(EElysiumNpcCond::EnemyOccluded, OccludedReportTimeE, Now);   // 1028e7aa 0x1028e700 +0x62cc
		if (!Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded))  // 1028e7b3 / 1028e7ba
		{
			UpdateEnemyWentOccluded(ConstThis->GetEnemy(), false);       // 1028e7c2 / 1028e7cb 0x10270180
		}
	}
	// `m_hTargetEnt (+0x5ce4)`: `-1`, a serial mismatch or an empty entry skips (dead-inclusive).
	const FElysiumEntity* const TargetEntity = World != nullptr && GetTarget().IsSet()
		? ElysiumNpcCond::ResolveEnemyHandle(*World, GetTarget())
		: nullptr;
	if (TargetEntity != nullptr)                                         // 1028e7d6 / 1028e7f5 / 1028e7fa
	{
		RefreshOccludedCondition(FElysiumNpcBase::Cond19TargetOccluded, OccludedReportTimeT, Now);   // 1028e807 0x1028e700 +0x62d0
	}
}

void FElysiumNpc::Conditions19TroikaGoalUpkeep()
{
	// `1026eed8` / `1026ef10`: slot 586 twice (test, then fetch); the handle-table test is the
	// dead-inclusive one.
	if (World != nullptr)
	{
		const FElysiumEntityHandle SeeUnknown = GetBestSeeUnknown();     // 1026eed8 slot 586
		const FElysiumEntity* const SeeUnknownEntity = SeeUnknown.IsSet()
			? ElysiumNpcCond::ResolveEnemyHandle(*World, SeeUnknown)
			: nullptr;
		if (SeeUnknownEntity != nullptr)                                 // 1026eee3 / 1026ef00 / 1026ef05
		{
			const FElysiumEntityHandle Again = GetBestSeeUnknown();      // 1026ef10 slot 586
			const FElysiumEntity* const AgainEntity = Again.IsSet()      // 1026ef1b / 1026ef38
				? ElysiumNpcCond::ResolveEnemyHandle(*World, Again)
				: nullptr;
			Conditions19UpdateApproachGoalPos(const_cast<FElysiumEntity*>(AgainEntity));   // 1026ef43
		}
		const FElysiumEntityHandle MoveTargetHandle = ScheduleHost.MoveTarget;   // 1026ef48 +0x6240
		// The handle re-read for the argument (1026ef7b `-1`, 1026ef92 serial): the same entity.
		const FElysiumEntity* const MoveTargetEntity = MoveTargetHandle.IsSet()
			? ElysiumNpcCond::ResolveEnemyHandle(*World, MoveTargetHandle)
			: nullptr;
		if (MoveTargetEntity != nullptr)                                 // 1026ef51 / 1026ef71 / 1026ef76
		{
			Conditions19RefreshGoalPosition(const_cast<FElysiumEntity*>(MoveTargetEntity));   // 1026ef9d
		}
	}
	Conditions19OcclusionReportUpkeep();                                 // 1026efa4 0x1028e790
}

// =================================================================================================
// Slot 433 -- `CAI_BaseNPCTroika::GatherConditions` `0x102b27f0`, 2133 bytes
// =================================================================================================

float FElysiumNpc::Conditions19NavPathEndDistSqrUnits() const
{
	return 0.f;
}

bool FElysiumNpc::Conditions19FireParticleLive(const FElysiumEntity& Particle) const
{
	(void)Particle;
	return false;
}

int32 FElysiumNpc::Conditions19CorpseQuery(FElysiumEntity** OutCorpses, int32 MaxCorpses)
{
	(void)OutCorpses;
	(void)MaxCorpses;
	return 0;
}

void FElysiumNpc::Conditions19GatherCorpse(double Now)
{
	// `0x1028fa50`. `FCOMP [+0x6608]`, `AND 0x100 / JNZ`: only `curtime >= stamp` runs (NaN skips).
	if (!(CorpseConditionTime <= Now))                                   // 1028fa5f / 1028fa6c
	{
		return;
	}
	CorpseConditionTime = Now + static_cast<double>(ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
		.FRandRange(Cond19CorpseRetimeMin, Cond19CorpseRetimeMax));       // 1028fa84 / 1028fa93
	Cognition.Conditions.Clear(Cond19SeeCorpse);                        // 1028fa99 ClearCondition(0x3d)
	Cognition.Conditions.Clear(EElysiumNpcCond::SeeCorpseFriend);       // 1028faa2 ClearCondition(0x3e)
	FElysiumEntity* Corpses[4] = { nullptr, nullptr, nullptr, nullptr };
	int32 Count = Conditions19CorpseQuery(Corpses, 4);                   // 1028fab5 0x102cabd0
	while (Count > 0)                                                    // 1028fabe / 1028fb15
	{
		FElysiumEntity* const Corpse = Corpses[Count - 1];               // 1028fac0 `[ESP+EDI*4+4]`
		--Count;                                                         // 1028fac4
		FElysiumNpc* const CorpseTroika = Corpse != nullptr ? Corpse->AsNpc() : nullptr;   // 1028fac9 +0x98
		bool bFriendArm = true;                                          // 1028fac7 / 1028fad1 -> 1028faeb
		if (CorpseTroika != nullptr)
		{
			const int32 Relation = IRelationType(CorpseTroika);         // 1028fad8 slot 404
			if (Relation == 1 || Relation == 2)                          // table 1028fb20 -> 1028faff
			{
				bFriendArm = false;
			}
			else if (Relation != 3 && Relation != 4)                     // 1028fae2 JA 1028fb13
			{
				continue;
			}
		}
		if (bFriendArm)
		{
			Cognition.Conditions.Set(EElysiumNpcCond::SeeCorpseFriend); // 1028fafa SetCondition(0x3e)
		}
		Cognition.Conditions.Set(Cond19SeeCorpse);                      // 1028fb0e SetCondition(0x3d)
	}
}

void FElysiumNpc::Conditions19GatherSquad(double Now)
{
	// `0x102b2730`.
	if (SquadDisconnected >= 1 || SquadWord() == 0)                      // 102b273b / 102b2745
	{
		return;
	}
	FElysiumEntity* const Enemy = GetEnemy();                            // 102b2749 slot 168
	if (Enemy != nullptr)                                                // 102b2751
	{
		// `GetEnemies()->LastTimeSeen(enemy)` (`0x102e0150`). For an enemy with no record retail
		// DevWarns "Asking LastTimeSeen for enemy that's not in my memory!!" and answers the last
		// position-only record's time, or 0.0; the port answers the same from its store.
		const double LastSeen = Conditions19LastTimeSeen(Enemy);         // 102b2758 slot 541 / 102b2760
		if (!(LastSeen + Cond19SquadSeenWindow < Now))                   // 102b2765..102b277a
		{
			Cognition.Conditions.Set(EElysiumNpcCond::SquadSeeEnemy);   // 102b278b SetCondition(0x31)
			if (Cognition.Conditions.Has(EElysiumNpcCond::HaveEnemyLos)) // 102b2794 / 102b279b
			{
				Cognition.Conditions.Set(Cond19SquadLosEnemy);          // 102b27ac SetCondition(0x32)
			}
		}
	}
	(void)Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy);          // 102b27b5, the answer discarded
}

void FElysiumNpc::GatherConditions()
{
	// `0x102b27f0`. The scope-trace push/pop (`102b27f0..102b2861`, `102b3020`; the name pick,
	// branches 0x102b27fb 0x102b2805) is absent, as is the `ent_trace_conditions` read before each
	// `SetCondition` (calls 0x102b28ef 0x102b2997 0x102b29f9 0x102b2a65 0x102b2ab9 0x102b2b1d
	// 0x102b2b8b 0x102b2bcb 0x102b2c98 0x102b2d0b 0x102b2fc4 0x102b3014).
	FElysiumNpcConditions& C = Cognition.Conditions;
	C.Clear(EElysiumNpcCond::InvestigateSight);                          // 102b2863 0x26
	C.Clear(EElysiumNpcCond::Comfort);                                   // 102b286c 0x27
	C.Clear(EElysiumNpcCond::SeeUnknown);                                // 102b2875 0x01
	C.Clear(EElysiumNpcCond::LostUnknown);                               // 102b287e 0x02
	C.Clear(EElysiumNpcCond::IgnoreUnknown);                             // 102b2887 0x03
	C.Clear(EElysiumNpcCond::UnknownRunTimer);                           // 102b2890 0x04
	C.Clear(EElysiumNpcCond::UnknownAdvancing);                          // 102b2899 0x05
	C.Clear(EElysiumNpcCond::UnknownHolding);                            // 102b28a2 0x06
	C.Clear(EElysiumNpcCond::UnknownRetreating);                         // 102b28ab 0x07
	C.Clear(EElysiumNpcCond::OnFire);                                    // 102b28b4 0x30
	C.Clear(EElysiumNpcCond::DetectedAttack);                            // 102b28bd 0x0b

	FElysiumNpcBase::GatherConditions();                                 // 102b28c4 direct -> 0x1026ec30
	const double Now = Conditions19Now();

	// Slot 168, fetched ONCE into EDI and reused at `102b29a3`, `102b2ac5` and `102b2c06`.
	FElysiumEntity* const Enemy = GetEnemy();                            // 102b28cd
	if (Enemy != nullptr && !Enemy->IsAlive())                           // 102b28d7 / 102b28dd slot 158 / 102b28e5
	{
		C.Set(EElysiumNpcCond::EnemyDead);                               // 102b28f6 0x58
		C.Clear(EElysiumNpcCond::SeeEnemy);                              // 102b28ff 0x46
		C.Clear(EElysiumNpcCond::EnemyOccluded);                         // 102b2908 0x48
	}

	// `0x1028efc0`, the law sweep: the port's producer of 0x1e..0x22 (`ElysiumNpcWitness`).
	ElysiumNpcWitness::GatherLawConditions(*this, Now, C);               // 102b290f
	Conditions19GatherCorpse(Now);                                       // 102b2916 0x1028fa50

	C.Clear(Cond19InsideInterruptDist);                                  // 102b291f 0x15
	C.Clear(Cond19OutsideInterruptDist);                                 // 102b2928 0x14
	C.Clear(Cond19InsideInterruptDistE);                                 // 102b2931 0x17
	C.Clear(Cond19OutsideInterruptDistE);                                // 102b293a 0x16
	C.Clear(Cond19InsideInterruptDistF);                                 // 102b2943 0x19
	C.Clear(Cond19OutsideInterruptDistF);                                // 102b294c 0x18

	// Squared SOURCE-unit distances between slot-220 origins (the port's are centimetres).
	const double USq = static_cast<double>(ElysiumMove::U) * static_cast<double>(ElysiumMove::U);
	auto DistSqUnits = [this, USq](FElysiumEntity& Other) -> float
	{
		return static_cast<float>(FVector::DistSquared(GetOrigin(), Other.GetOrigin()) / USq);
	};

	const float Inside = ScheduleHost.InsideInterruptDistanceSqr;        // +0x6324
	if (Inside > ElysiumNpcTunables::Zero)                               // 102b2951 / 102b2964 (NaN skips)
	{
		if (NavigatorGoalIsActive()                                      // 102b2970 0x102ee6a0 / 102b2977
			&& Conditions19NavPathEndDistSqrUnits() < Inside)            // 102b297f nav+0x14 / 102b298d
		{
			C.Set(Cond19InsideInterruptDist);                            // 102b299e 0x15
		}
		if (Enemy != nullptr && DistSqUnits(*Enemy) < Inside)            // 102b29a5 / 102b29ab 102b29b7 slot 220 / 102b29de / 102b29ef
		{
			C.Set(Cond19InsideInterruptDistE);                           // 102b2a00 0x17
		}
		if (FElysiumEntity* const Boss = GetFollowerBoss())              // 102b2a09 slot 293 / 102b2a11
		{
			if (DistSqUnits(*Boss) < Inside)                             // 102b2a17 102b2a23 slot 220 / 102b2a4a / 102b2a5b
			{
				C.Set(Cond19InsideInterruptDistF);                       // 102b2a6c 0x19
			}
		}
	}
	const float Outside = ScheduleHost.OutsideInterruptDistanceSqr;      // +0x6328
	if (Outside > ElysiumNpcTunables::Zero)                              // 102b2a71..102b2a84
	{
		if (NavigatorGoalIsActive()                                      // 102b2a90 / 102b2a97
			&& Conditions19NavPathEndDistSqrUnits() > Outside)           // 102b2a9f / 102b2aaf
		{
			C.Set(Cond19OutsideInterruptDist);                           // 102b2ac0 0x14
		}
		if (Enemy != nullptr && DistSqUnits(*Enemy) > Outside)           // 102b2ac7 / 102b2acd 102b2ad9 slot 220 / 102b2b00 / 102b2b13
		{
			C.Set(Cond19OutsideInterruptDistE);                          // 102b2b24 0x16
		}
		if (FElysiumEntity* const Boss = GetFollowerBoss())              // 102b2b2d slot 293 (again) / 102b2b35
		{
			if (DistSqUnits(*Boss) > Outside)                            // 102b2b3b 102b2b47 slot 220 / 102b2b6e / 102b2b81
			{
				C.Set(Cond19OutsideInterruptDistF);                      // 102b2b92 0x18
			}
		}
	}
	if (ScheduleHost.InterruptTime > static_cast<double>(ElysiumNpcTunables::Zero)   // 102b2b97..102b2baa
		&& ScheduleHost.InterruptTime <= Now)                            // 102b2bb1..102b2bc1 `FCOMP / AND 0x100` (NaN skips)
	{
		C.Set(EElysiumNpcCond::InterruptTime);                           // 102b2bd2 0x1a
	}

	ElysiumNpcCond::GatherSeeUnknown(*this, Now, C);                     // 102b2bd9 0x102b15c0
	ElysiumNpcCond::GatherComfort(*this, Now, C);                        // 102b2be0 0x102b1a20
	ElysiumNpcCond::GatherSounds(*this, Now, C);                         // 102b2be7 0x102b1cd0

	if (ElysiumSchedule::MaskHasCondition(Schedule, *this, EElysiumNpcCond::StopBackup))   // 102b2bf0 0x10269c70 / 102b2bfb
	{
		C.Clear(EElysiumNpcCond::StopBackup);                            // 102b2c01 0x2c
		if (Enemy != nullptr)                                            // 102b2c08
		{
			// Slot 217 origins, Z dropped, `VectorNormalize` (`0x10137220`, which adds
			// `FLT_EPSILON` to the length), dotted in X/Y with `m_vecForward` (`+0x6290/+0x6294`).
			// That word is NPCThink's `AngleVectors(GetAngles())` write, which the port never makes;
			// the same vector is derived from the angles here.
			FVector Direction = (Enemy->GetAbsOrigin() - GetAbsOrigin()) / ElysiumMove::U;   // 102b2c12..102b2c3a (102b2c1e slot 217)
			Direction.Z = 0.0;                                           // 102b2c5d
			Direction = Direction / (static_cast<double>(ElysiumNpcTunables::FloatEpsilon) + Direction.Size());   // 102b2c65
			const FVector ForwardVector = FElysiumNpcSenses::ViewForward(*this);
			const double Dot = Direction.Y * ForwardVector.Y + Direction.X * ForwardVector.X;   // 102b2c6d..102b2c81
			if (Dot <= Cond19StopBackupDot)                              // 102b2c83 / 102b2c8e JP (greater or NaN skips)
			{
				C.Set(EElysiumNpcCond::StopBackup);                      // 102b2c9f
			}
		}
	}
	else
	{
		C.Clear(EElysiumNpcCond::StopBackup);                            // 102b2ca6
	}

	RefreshCombatConditions();                                           // 102b2cad 0x102b2570

	for (int32 Index = 0; Index < Conditions19BodyFireParticleCount; ++Index)   // 102b2cb8 / 102b2cfc / 102b2cff
	{
		const FElysiumEntity* const Particle = World != nullptr && BodyFireParticles[Index].IsSet()
			? ElysiumNpcCond::ResolveEnemyHandle(*World, BodyFireParticles[Index])   // 102b2cc5..102b2ce6 (102b2cdc serial, 102b2ce1 entry)
			: nullptr;
		if (Particle != nullptr && Conditions19FireParticleLive(*Particle))   // 102b2cee / 102b2cf6
		{
			C.Set(EElysiumNpcCond::OnFire);                              // 102b2d12 0x30
			break;
		}
	}

	Conditions19GatherSquad(Now);                                        // 102b2d19 0x102b2730

	const FElysiumEntity* const Door = World != nullptr && BlockedDoor.IsSet()
		? ElysiumNpcCond::ResolveEnemyHandle(*World, BlockedDoor)       // 102b2d1e..102b2d4b (102b2d29 `-1`, 102b2d46 serial)
		: nullptr;
	if (Door != nullptr
		&& (C.Has(EElysiumNpcCond::SeeEnemy) || C.Has(EElysiumNpcCond::NewEnemy)))   // 102b2d51 / 102b2d58 / 102b2d5e / 102b2d65
	{
		BlockedDoor = FElysiumEntityHandle::Invalid();                   // 102b2d67 +0x5d28 := -1
	}

	// `+0x65c0` / `+0x65c4`: the port's notice record stores the notice STAMP where retail stores
	// the EXPIRY (`curtime + 5.0`, `_DAT_10454110`), so retail's expiry is the stamp plus the
	// retention, and retail's restamp `+0x65c4 := curtime` is the stamp moved back by it.
	FElysiumNpcMemory& Memory = Senses.Memory;
	const double DetectedAttackExpiry = Memory.DetectedAttackTime + ElysiumNpcCond::DetectedAttackRetentionSeconds;
	if (ElysiumSchedule::MaskHasCondition(Schedule, *this, EElysiumNpcCond::DetectedAttack)   // 102b2d75 / 102b2d7c
		&& Now < DetectedAttackExpiry                                    // 102b2d83 / 102b2d91
		&& World != nullptr && Memory.DetectedAttackAttacker.IsSet()
		&& ElysiumNpcCond::ResolveEnemyHandle(*World, Memory.DetectedAttackAttacker) != nullptr)   // 102b2d9c..102b2dbe (102b2db9 serial)
	{
		const float Delay = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(Cond19DetectedAttackDelayMin, Cond19DetectedAttackDelayMax);   // 102b2dc0..102b2dd5 (102b2dd2 RandomFloat)
		Conditions19PushDelayedCondition(static_cast<int32>(EElysiumNpcCond::DetectedAttack), Delay, Now);   // 102b2de6 0x102cc6c0
		Memory.DetectedAttackTime = Now - ElysiumNpcCond::DetectedAttackRetentionSeconds;   // 102b2deb..102b2df4 +0x65c4 := curtime
	}

	// `FLD curtime / FCOMP [+0x6600] / AND 0x100 / JNZ`: C0 is set by "below" AND by unordered, so
	// only an ordered `curtime >= stamp` runs.
	if (WeaponThroughWallTime <= Now)                                    // 102b2dfa..102b2e10
	{
		WeaponThroughWallTime = Now + Cond19WallTraceInterval;           // 102b2e16..102b2e28
		// A RAY (zero extents, `m_IsRay = 1` at `102b2ef7`) from slot 193 `EyePosition` along
		// `m_vecForward * 32`, mask `0x2000b` (`Cond19WallTraceMask`), `CTraceFilterSimple(this, 0)`,
		// through the family's live world ray (`Ray_t::Init`'s `m_IsSwept` at 102b2eb9,
		// `CTraceFilterSimple` 102b2f2e). The debug line under `0x10738964` (`102b2f52..102b2f8a`:
		// call 0x102b2f5a, branches 0x102b2f5f 0x102b2f6b) is absent.
		const FVector EyeCm = EyePosition();                             // 102b2e2e
		const FVector EndCm = EyeCm + FElysiumNpcSenses::ViewForward(*this)
			* (static_cast<double>(Cond19WallTraceLengthUnits) * ElysiumMove::U);   // 102b2e34..102b2e74
		// `fraction < 1.0 || allsolid (tr+0x36) || startsolid (tr+0x37)` (102b2fa4 / 102b2faf): each
		// is "the ray did not reach".
		if (!ElysiumNpcSight::RayReaches(*this, EyeCm, EndCm, Cond19WallTraceMask, nullptr))   // 102b2f4f TraceRay / 102b2f92..102b2fba
		{
			C.Set(EElysiumNpcCond::WeaponThroughWall);                   // 102b2fcb 0x3c
		}
		else
		{
			C.Clear(EElysiumNpcCond::WeaponThroughWall);                 // 102b2fd6
		}
	}

	if (C.Has(EElysiumNpcCond::LightDamage)                              // 102b2fdf / 102b2fe6
		|| C.Has(EElysiumNpcCond::HeavyDamage)                           // 102b2fec / 102b2ff3
		|| C.Has(EElysiumNpcCond::RepeatedDamage))                       // 102b2ff9 / 102b3000
	{
		Cognition.bCondTookDamage = true;                                // 102b302f
		return;
	}
	if (Cognition.bCondTookDamage)                                       // 102b3002 / 102b300a
	{
		C.Set(EElysiumNpcCond::LightDamage);                             // 102b301b (the latch stays set)
	}
}
