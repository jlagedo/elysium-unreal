// Story 0019/8 (29e under the strict verdict), family **Conditions19** -- the species classes'
// bodies.
//
// Every body below calls `CAI_BaseNPCTroika::GatherConditions` DIRECTLY in retail (`CALL 0x10013f2f`
// -> `0x102b27f0`): no intermediate class (`CNPC_VVampire`, `CNPC_VVampireBoss`, `CNPC_VAnimal`,
// `CNPC_VHuman`, `CNPC_VHumanCombatant`, `CNPC_VBaseBoss`) holds a slot-433 body of its own, so the
// parent call is spelled `FElysiumNpc::GatherConditions()`. The `(*DAT_10924a6c)->vfunc1()` read
// before every `SetCondition` is the `ent_trace_conditions` ConVar with its answer discarded
// (`ElysiumNpcConditions10.inl`), and is absent here, as are the scope-trace frames. Walked prose:
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Story 8, family Conditions19".
//
// Owns (Conditions19's `rule` rows): 0x1035d180 CNPC_VAndreiBlood::GatherConditions, 0x10365a70
// CNPC_VBach::GatherConditions, 0x1036b590 CNPC_VChangBros::GatherConditions, 0x10374b00
// CNPC_VDog::GatherConditions, 0x10375ed0 CNPC_VFrenzyShadow::GatherConditions (its body stands in
// `ElysiumNpcFrenzyShadow.cpp`), 0x10378df0 CNPC_VGargoyle::GatherConditions, 0x1037b570
// CNPC_VGhoulCroucher::GatherConditions, 0x103803d0 CNPC_VHengeyokai::GatherConditions, 0x10394e40
// CNPC_VMingXiao::GatherConditions, 0x1039ec10 CNPC_VMingXiaoTentacle::GatherConditions, 0x103a2c30
// CNPC_VPedestrian::GatherConditions, 0x103a77f0 CNPC_VSabbatLeader::GatherConditions, 0x103ac500
// CNPC_VScurrying::GatherConditions, 0x103bce40 CNPC_VTzimisce::GatherConditions, 0x103c17f0
// CNPC_VTzimisceHeadClaw::GatherConditions, 0x103c35a0 CNPC_VTzimisceRunner::GatherConditions,
// 0x103d0410 CNPC_VWerewolf::GatherConditions.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcWerewolf.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers, family-prefixed for the unity build -------------------------------------

// `AngleVectors(GetAbsAngles(), NULL, &right, NULL)` (`0x10139610`) in the port's frame. `Angles`
// hold SOURCE angles and port positions are Source's with Y mirrored (the convention
// `FElysiumNpcSenses::ViewForward` follows), so this is Source's right vector
// `(-sr*sp*cy + cr*sy, -sr*sp*sy - cr*cy, -sr*cp)` with its Y negated. (The lane's first cut fed the
// mirrored yaw into Source's formula unmirrored, which answered the LEFT vector.)
static FVector Conditions19SpeciesRightVector(const FElysiumEntity& Entity)
{
	const double Pitch = FMath::DegreesToRadians(static_cast<double>(Entity.Angles.X));
	const double Yaw = FMath::DegreesToRadians(static_cast<double>(Entity.Angles.Y));
	const double Roll = FMath::DegreesToRadians(static_cast<double>(Entity.Angles.Z));
	const double Sp = FMath::Sin(Pitch), Cp = FMath::Cos(Pitch);
	const double Sy = FMath::Sin(Yaw), Cy = FMath::Cos(Yaw);
	const double Sr = FMath::Sin(Roll), Cr = FMath::Cos(Roll);
	return FVector(-Sr * Sp * Cy + Cr * Sy, Sr * Sp * Sy + Cr * Cy, -Sr * Cp);
}

// The squared SOURCE-unit distance between two port (centimetre) positions.
static double Conditions19SpeciesDistSqUnits(const FVector& ACm, const FVector& BCm)
{
	const double USq = static_cast<double>(ElysiumMove::U) * static_cast<double>(ElysiumMove::U);
	return FVector::DistSquared(ACm, BCm) / USq;
}

// =================================================================================================
// 0x1035d180 CNPC_VAndreiBlood::GatherConditions, 19 bytes
// =================================================================================================

void FElysiumNpcAndreiBlood::GatherConditions()
{
	FElysiumNpc::GatherConditions();                                     // 1035d183
	// `COND_VANDREIBLOOD_TIME_TO_TELEPORT`. This body only ever clears it; no setter of 0x79 on
	// this class is in the image (unrecovered producer).
	Cognition.Conditions.Clear(Cond19AndreiTimeToTeleport);              // 1035d18c ClearCondition(0x79)
}

// =================================================================================================
// 0x10365a70 CNPC_VBach::GatherConditions, 16 bytes (its tail 0x10365a90 is Misc19's row, landed by
// lane L11 as `BachGatherCamperConditions` in `ElysiumNpcMisc2Species.cpp`)
// =================================================================================================

void FElysiumNpcBach::GatherConditions()
{
	FElysiumNpc::GatherConditions();                                     // 10365a73
	BachGatherCamperConditions();                                        // 10365a7b JMP 0x1000ce2d -> 0x10365a90
}

// =================================================================================================
// 0x1036b590 CNPC_VChangBros::GatherConditions, 215 bytes (also CNPC_VChangBrosBlade / Claw)
// (absent `ent_trace_conditions` reads before its three `SetCondition`s: calls 0x1036b614
// 0x1036b633 0x1036b652)
// =================================================================================================

void FElysiumNpcChangBros::GatherConditions()
{
	FElysiumNpc::GatherConditions();                                     // 1036b5e3
	UpdateFacingTimer();                                                 // 1036b5ea 0x1036d600
	Cognition.Conditions.Clear(Cond19ChangTimeToJumpAttack);             // 1036b5f3 ClearCondition(0x79)
	Cognition.Conditions.Clear(Cond19ChangTimeToUnitedAttack);           // 1036b5fc ClearCondition(0x7c)
	if (CheckForJumpAttack())                                            // 1036b603 / 1036b60a
	{
		Cognition.Conditions.Set(Cond19ChangTimeToJumpAttack);           // 1036b61b SetCondition(0x79)
	}
	if (CheckForTeleport())                                              // 1036b622 / 1036b629
	{
		Cognition.Conditions.Set(Cond19ChangTimeToTeleport);             // 1036b63a SetCondition(0x7a), never cleared here
	}
	if (CheckForUnited())                                                // 1036b641 / 1036b648
	{
		Cognition.Conditions.Set(Cond19ChangTimeToUnitedAttack);         // 1036b659 SetCondition(0x7c)
	}
}

// =================================================================================================
// 0x10374b00 CNPC_VDog::GatherConditions, 508 bytes
// (absent `ent_trace_conditions` reads before its `SetCondition`s: calls 0x10374b1d 0x10374b65
// 0x10374ba6 0x10374c86 0x10374ca6 0x10374cc8 0x10374ceb)
// =================================================================================================

void FElysiumNpcDog::GatherConditions()
{
	FElysiumNpc::GatherConditions();                                     // 10374b06
	FElysiumNpcConditions& C = Cognition.Conditions;
	if (bPlayerAttackedMe)                                               // 10374b0b / 10374b13 +0x6660
	{
		C.Set(Cond19DogPlayerAttacked);                                  // 10374b24 SetCondition(0x7e)
	}
	if (AnimalFriendshipLevel == 6)                                      // 10374b29 / 10374b30 +0x6664
	{
		return;
	}
	if (!C.Has(EElysiumNpcCond::HearPlayer)                              // 10374b3a / 10374b41
		&& !C.Has(EElysiumNpcCond::SeePlayer))                           // 10374b47 / 10374b4e
	{
		if (ActivityNumber != 0x6e)                                      // 10374b50 / 10374b57 ACT_SNARL
		{
			return;
		}
		C.Set(Cond19DogPlayerMovedAway);                                 // 10374b68 PUSH 0x7c / 10374b6c SetCondition
		return;
	}
	if (AnimalPlayerFriendshipState == 2)                                // 10374b7f +0x665c
	{
		C.Set(Cond19DogPlayerBefriended);                                // 10374cf2 SetCondition(0x7d)
		return;
	}
	if (AnimalPlayerFriendshipState == 0)                                // 10374b85 / 10374b87
	{
		if (AnimalFriendshipLevel == 5)                                  // 10374b92
		{
			AnimalPlayerFriendshipState = 2;                             // 10374b94
			C.Set(Cond19DogPlayerBefriended);                            // 10374bad, and the body goes on
		}
		else if (AnimalFriendshipLevel == 4)                             // 10374bb7
		{
			AnimalPlayerFriendshipState = 1;                             // 10374bb9
		}
	}
	// `UTIL_GetLocalPlayer()` (`0x101cda50`), dereferenced unguarded in retail.
	FElysiumPlayer* const Player = World != nullptr ? World->FindPlayer() : nullptr;   // 10374bc3
	if (Player == nullptr)
	{
		return;   // crash guard: retail faults on a null player
	}
	// Slot 220 on both, `fabs(sqrt(...))`, SOURCE units.
	const float Distance = static_cast<float>(FMath::Abs(FMath::Sqrt(
		Conditions19SpeciesDistSqUnits(Player->GetOrigin(), GetOrigin()))));   // 10374bcc..10374c47 (10374bea slot 220, 10374c36 sqrt)
	if (ActivityNumber == 0x6e && Distance < AnimalConflictRangeUnits)   // 10374c4e / 10374c54 / 10374c5f
	{
		if (AnimalPlayerFriendshipState == 0)                            // 10374c69
		{
			AnimalPlayerFriendshipState = 1;                             // 10374c6b
		}
		if (AnimalPlayerFriendshipState == 1)                            // 10374c7c
		{
			C.Set(Cond19DogPlayerTooClose);                              // 10374ccf SetCondition(0x78)
		}
	}
	else if (Distance < AnimalWarnRangeUnits)                            // 10374c91 / 10374c9c
	{
		C.Set(EElysiumNpcCond::PlayerSnarlRange);                        // 10374ccf SetCondition(0x2b)
	}
	else if (Distance >= AnimalWarnRangeUnits)                           // 10374cad..10374cbe `AND 0x100 / JNZ` (ordered >=)
	{
		C.Set(Cond19DogPlayerMovedAway);                                 // 10374ccf SetCondition(0x7c)
	}
	DogSnarlSourceWord = Distance;                                       // 10374cd4 / 10374cd8 +0x667c
}

// =================================================================================================
// 0x10378df0 CNPC_VGargoyle::GatherConditions, 147 bytes
// =================================================================================================

void FElysiumNpcGargoyle::GatherConditions()
{
	FElysiumNpc::GatherConditions();                                     // 10378df4
	FElysiumNpcConditions& C = Cognition.Conditions;
	C.Clear(EElysiumNpcCond::ShouldKick);                                // 10378dfd 0x0f
	C.Clear(EElysiumNpcCond::ShouldDodge);                               // 10378e06 0x0c
	C.Clear(EElysiumNpcCond::ShouldBlock);                               // 10378e0f 0x0d
	const FElysiumNpcBase* const ConstThis = this;
	const FElysiumEntity* const Enemy = ConstThis->GetEnemy();           // 10378e18 slot 167
	if (Enemy == nullptr)                                                // 10378e22
	{
		return;
	}
	const FVector Delta = (Enemy->GetAbsOrigin() - GetAbsOrigin()) / ElysiumMove::U;   // 10378e29..10378e50 slot 217 (10378e29 / 10378e35)
	const double Distance2D = FMath::Sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y);        // 10378e57
	if (Distance2D <= 50.0)                                              // double `0x104493c0`; 10378e6c JP (greater or NaN skips)
	{
		C.Clear(EElysiumNpcCond::TooCloseToAttack);                      // 10378e72 0x5f
		C.Clear(EElysiumNpcCond::ShouldStepback);                        // 10378e7b 0x0e
	}
}

// =================================================================================================
// 0x1037b570 CNPC_VGhoulCroucher::GatherConditions, 288 bytes
// (absent: the scope-trace name pick, branches 0x1037b575 0x1037b57f; the `ent_trace_conditions`
// read before `SetCondition(0x79)`, call 0x1037b67b)
// =================================================================================================

void FElysiumNpcGhoulCroucher::GatherConditions()
{
	FElysiumNpcConditions& C = Cognition.Conditions;
	if (!bWasDisturbed)                                                  // 1037b5da / 1037b5e2 +0x6666
	{
		// `NEW_ENEMY` as the PREVIOUS pass left it: the base gather has not run yet.
		if (C.Has(EElysiumNpcCond::NewEnemy))                            // 1037b5e8 / 1037b5ef
		{
			OnDisturbed(this);                                           // 1037b5f4 (the disturber is this)
			return;                                                      // 1037b601, the base NOT run
		}
		if (!bWasDisturbed)                                              // 1037b602 / 1037b60a
		{
			C.Set(Cond19CroucherUnaware);                                // 1037b682 SetCondition(0x79)
			return;
		}
	}
	FElysiumNpc::GatherConditions();                                     // 1037b60e
	const FElysiumNpcBase* const ConstThis = this;
	if (C.Has(EElysiumNpcCond::NewEnemy)                                 // 1037b617 / 1037b61e
		&& SquadDisconnected < 1 && SquadWord() != 0                     // 1037b628 / 1037b632
		&& ConstThis->GetEnemy() != nullptr)                             // 1037b638 slot 167 / 1037b640
	{
		// The squad argument is the inlined `GetSquad()`, `+0x5bb0 > 0` read again (1037b64b JG):
		// the same connected squad the gate just admitted.
		// `SquadNewEnemy(m_pSquad, GetEnemy())` (`0x103161a0`), the enemy fetched again. No squad
		// object stands on the kernel (0002/17); the landed counter for this call is State19's.
		++SelectIdealStateSquadNewEnemyCalls;                            // 1037b65b / 1037b664
	}
}

// =================================================================================================
// 0x103803d0 CNPC_VHengeyokai::GatherConditions, 114 bytes
// (absent `ent_trace_conditions` read before `SetCondition(0x1b)`: call 0x10380429)
// =================================================================================================

bool FElysiumNpcHengeyokai::HengeyokaiThrowLosTest(FElysiumEntity* Enemy)
{
	// `0x10382020`.
	if (Enemy == nullptr)                                                // 10382031
	{
		return false;
	}
	const FVector RightVec = Conditions19SpeciesRightVector(*this);          // 1038204f slot 221 / 10382056 0x10139610
	// `EyePosition() - right * _DAT_1049a198 (-80.0)`: 80 units to the NPC's right.
	const FVector StartCm = EyePosition() - RightVec * (-80.0 * ElysiumMove::U);   // 1038205b..103820b3
	const FVector EndCm = Enemy->EyePosition();                          // 103820c0 slot 193
	// `TraceRay` mask `0x600400b` (`10382182` / `103821a3`), `fraction == 1.0` against the double
	// `0x10449280` (`103821e9..103821fb`): the family's live world ray.
	return Conditions19RayReaches(StartCm, EndCm);
}

void FElysiumNpcHengeyokai::GatherConditions()
{
	FElysiumNpc::GatherConditions();                                     // 103803d3
	FElysiumNpcConditions& C = Cognition.Conditions;
	C.Clear(EElysiumNpcCond::ShouldKick);                                // 103803dc 0x0f
	C.Clear(EElysiumNpcCond::ShouldDodge);                               // 103803e5 0x0c
	C.Clear(EElysiumNpcCond::ShouldBlock);                               // 103803ee 0x0d
	const FElysiumNpcBase* const ConstThis = this;
	if (HengeyokaiCarryFormBit()                                         // 103803f5 / 103803fc 0x10381c80
		&& C.Has(EElysiumNpcCond::HaveEnemyLos)                          // 10380402 / 10380409
		&& HengeyokaiThrowLosTest(ConstThis->GetEnemy()))                // 1038040f slot 167 / 10380418 / 1038041f
	{
		C.Set(Cond19HaveEnemyThrowLos);                                  // 10380430 SetCondition(0x1b)
		return;
	}
	C.Clear(Cond19HaveEnemyThrowLos);                                    // 1038043b ClearCondition(0x1b)
}

// =================================================================================================
// 0x10394e40 CNPC_VMingXiao::GatherConditions, 719 bytes (absent `ent_trace_conditions` reads:
// calls 0x10394ec9 0x10394ee9 0x103950fc)
// =================================================================================================

bool FElysiumNpcMingXiao::MingXiaoWeaponLosCondition(FElysiumEntity* Weapon, const FVector& FromCm,
	const FVector& ToCm)
{
	(void)Weapon;
	(void)FromCm;
	(void)ToCm;
	return true;
}

void FElysiumNpcMingXiao::GatherConditions()
{
	FElysiumNpcConditions& C = Cognition.Conditions;
	// 10394e4a 0x77, 10394e53 0x78, 10394e5c 0x79, 10394e65 0x7a, 10394e6e 0x7b, 10394e77 0x7c, 10394e80 0x7d
	for (int32 Ordinal = Cond19MingXiaoCanAttackFirst; Ordinal <= 0x7d; ++Ordinal)   // 10394e4a..10394e80 (0x7e is NOT cleared)
	{
		C.ClearOrdinal(Ordinal);
	}
	FElysiumNpc::GatherConditions();                                     // 10394e87
	if (NpcStateRetail() == 1)                                           // 10394e90 slot 464 / 10394e99
	{
		return;
	}
	bool bAnySlot = false;
	for (int32 Slot = 0; Slot < 6; ++Slot)                               // 10394ea3..10394edb
	{
		int32 UnusedSchedule = 0;
		if (FUN_10398030(Slot, false, UnusedSchedule))                   // 10394ea8 / 10394eaf
		{
			bAnySlot = true;                                             // 10394eb6
			if (FUN_10398030(Slot, true, UnusedSchedule))                // 10394eb8 / 10394ebf
			{
				C.SetOrdinal(Cond19MingXiaoCanAttackFirst + Slot);       // 10394ed2 SetCondition(0x77 + i)
			}
		}
	}
	if (!bAnySlot)                                                       // 10394edd / 10394edf
	{
		C.Set(Cond19MingXiaoMeleeHelpless);                              // 10394ef0 SetCondition(0x7e)
	}
	// `m_flPlayerDist (+0x6264) > 200.0` (`_DAT_104492b8`); less, equal or NaN returns.
	if (!(Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U > 200.f))   // 10394ef5 / 10394f08
	{
		return;
	}
	if (BlockedByFriend())                                               // 10394f10 0x1039ab10 / 10394f17
	{
		return;
	}
	FElysiumEntity* const WeaponEntity = World != nullptr && MingXiaoRangedWeapon.IsSet()
		? World->Resolve(MingXiaoRangedWeapon) : nullptr;               // 10394f1d..10394f50 +0x6680 (10394f26 `-1`, 10394f46 serial)
	if (WeaponEntity == nullptr)
	{
		return;
	}
	// `+0xa0` on the weapon entity: its `CBaseCombatWeapon*` self-cast (as `+0x9c` is the combat
	// character's and `+0x98` the Troika's).
	FElysiumItem* const Item = WeaponEntity->AsItem();
	FElysiumWeapon* const Weapon = Item != nullptr ? Item->AsWeapon() : nullptr;   // 10394f56 / 10394f5e
	if (Weapon == nullptr)
	{
		return;
	}
	const FElysiumNpcBase* const ConstThis = this;
	FElysiumEntity* const Enemy = ConstThis->GetEnemy();                 // 10394f68 slot 167
	if (Enemy == nullptr)                                                // 10394f72
	{
		return;
	}
	const double Now = Conditions19Now();
	// `0x10252450(weapon, 0)`: `m_flNextPrimaryAttack` (`+0x730`); a later stamp or NaN returns.
	if (!(Weapon->NextPrimaryAttackTime <= Now))                         // 10394f7c / 10394f8f
	{
		return;
	}
	if (!(Now >= MingXiaoSpitAttackTimer))                               // 10394f95..10394fa5 +0x66c0 `AND 0x100`: below or NaN returns
	{
		return;
	}
	const FVector MyCm = GetAbsOrigin();                                 // 10394fb0 slot 217
	const FVector TargetCm = Enemy->BodyTarget(Enemy->GetAbsOrigin(), true, false);   // 10394fd2 slot 217 / 10394fe0 slot 197
	const FVector Side = FVector(TargetCm.Y - MyCm.Y, MyCm.X - TargetCm.X, 0.0).GetSafeNormal();   // 10394fe6..1039500c 0x10137220
	// The enemy's OBB width (`m_Collision +0x270`, vfunc 2 minus vfunc 1) times 0.3 (`_DAT_10451ab8`).
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	RetailCollisionExtents(*Enemy, MinsUnits, MaxsUnits);               // 10395022 / 1039502b
	const double HalfWidthCm = (MaxsUnits.X - MinsUnits.X) * 0.3 * ElysiumMove::U;   // 10395035
	const FVector LeftCm = TargetCm + Side * HalfWidthCm;                // 1039503b..10395095
	const FVector RightCm = TargetCm - Side * HalfWidthCm;               // 103950ac..103950e5
	if (!MingXiaoWeaponLosCondition(WeaponEntity, MyCm, LeftCm))         // 103950a1 weapon slot 364 / 103950aa
	{
		return;
	}
	if (!MingXiaoWeaponLosCondition(WeaponEntity, MyCm, RightCm))        // 103950ea / 103950f2
	{
		return;
	}
	C.Set(Cond19MingXiaoCanAttackSpit);                                  // 10395103 SetCondition(0x7d)
}

// =================================================================================================
// 0x1039ec10 CNPC_VMingXiaoTentacle::GatherConditions, 148 bytes
// (absent `ent_trace_conditions` reads: calls 0x1039ec6b 0x1039ec96)
// =================================================================================================

void FElysiumNpcMingXiaoTentacle::GatherConditions()
{
	FElysiumNpcConditions& C = Cognition.Conditions;
	C.Clear(Cond19TentacleFlee);                                         // 1039ec15 0x77
	C.Clear(Cond19TentaclePhaseExpired);                                 // 1039ec1e 0x79
	FElysiumNpc::GatherConditions();                                     // 1039ec25
	const double Now = Conditions19Now();
	const FElysiumNpcBase* const ConstThis = this;
	if (ConstThis->GetEnemy() != nullptr                                 // 1039ec2e / 1039ec36 slot 167
		&& Now >= MingXiaoTentacleFailedEvadeTimer                       // 1039ec41 / 1039ec4e +0x6678 (ordered; NaN skips)
		&& ScheduleHost.EnemyDistUnits <= 256.f)                         // 1039ec56 `_DAT_1044ddb0` / 1039ec61 JP
	{
		C.Set(Cond19TentacleFlee);                                       // 1039ec72 SetCondition(0x77)
	}
	if (Now >= MingXiaoTentaclePhaseExpireTimer)                         // 1039ec7f / 1039ec8c +0x6674 (ordered; NaN skips)
	{
		C.Set(Cond19TentaclePhaseExpired);                               // 1039ec9d SetCondition(0x79)
	}
}

// =================================================================================================
// 0x103a2c30 CNPC_VPedestrian::GatherConditions, 399 bytes
// (absent `ent_trace_conditions` read: call 0x103a2dac)
// =================================================================================================

void FElysiumNpcPedestrian::GatherConditions()
{
	FElysiumNpcConditions& C = Cognition.Conditions;
	C.Clear(EElysiumNpcCond::PassOut);                                   // 103a2c3a 0x24
	FElysiumNpc::GatherConditions();                                     // 103a2c41
	if (!C.Has(EElysiumNpcCond::SeeSoundSource))                         // 103a2c4a / 103a2c51
	{
		return;
	}
	if (World == nullptr)
	{
		return;
	}
	// Retail compares the RESOLVED pointers (null == null passes the identity test).
	auto ResolveOrNull = [this](const FElysiumEntityHandle& H) -> const FElysiumEntity*
	{
		return H.IsSet() ? ElysiumNpcCond::ResolveEnemyHandle(*World, H) : nullptr;
	};
	const FElysiumEntity* const Heard = ResolveOrNull(BaseMemory.BestSoundSource);   // +0x5b78
	// `+0x6160` resolved (103a2c66 `-1`, 103a2c80 serial), `+0x5b78` resolved (103a2c91, 103a2cab)
	if (Heard != ResolveOrNull(Senses.Memory.LastSoundCombat.Source))    // 103a2c57..103a2cb5 +0x6160
	{
		if (ResolveOrNull(BaseMemory.BestSoundSource)
			!= ResolveOrNull(Senses.Memory.LastSoundBulletImpact.Source)) // 103a2cb7..103a2d0f +0x618c (103a2cc0, 103a2cda; `+0x5b78` 103a2ceb, 103a2d05)
		{
			return;
		}
	}
	const FElysiumEntity* const Source = ResolveOrNull(BaseMemory.BestSoundSource);   // 103a2d15..103a2d3b (103a2d1e `-1`, 103a2d35 serial)
	if (Source == nullptr)
	{
		return;   // crash guard: retail reads `[0 + 0x9c]` when both handles failed to resolve
	}
	const FElysiumCombatCharacter* const Shooter = Source->AsCombatCharacter();   // 103a2d3d +0x9c
	if (Shooter == nullptr)                                              // 103a2d45
	{
		return;
	}
	if (!Shooter->Inventory.ActiveWeapon.IsSet()
		|| World->Resolve(Shooter->Inventory.ActiveWeapon) == nullptr)   // 103a2d49 / 103a2d50 GetActiveWeapon
	{
		return;
	}
	// `0x102517e0(GetActiveWeapon())->+0x3c4`, the weapon's crime level: the one SEAM the pedestrian
	// family already stands for it (`PedestrianWeaponCrimeLevel`, -1: no port weapon record carries
	// the column).
	const int32 Level = PedestrianWeaponCrimeLevel;                      // 103a2d54 / 103a2d5b / 103a2d68
	if (Level < PlCriminalFlee)                                          // 103a2d70 signed JL +0x634c
	{
		return;
	}
	RecordCriminalWitness(Level, const_cast<FElysiumCombatCharacter*>(Shooter)->GetOrigin(), Source);   // 103a2d76 slot 220 / 103a2d9f 0x1028ea60
	C.Set(EElysiumNpcCond::CriminalFleeLevel);                           // 103a2db3 SetCondition(0x1f)
}

// =================================================================================================
// 0x103a77f0 CNPC_VSabbatLeader::GatherConditions, 152 bytes
// (absent `ent_trace_conditions` read: call 0x103a7858)
// =================================================================================================

void FElysiumNpcSabbatLeader::GatherConditions()
{
	FElysiumNpc::GatherConditions();                                     // 103a7840
	FElysiumNpcConditions& C = Cognition.Conditions;
	if (CheckForJumpCondition())                                         // 103a7847 / 103a784e
	{
		C.Set(Cond19SabbatTimeToJump);                                   // 103a785f SetCondition(0x79)
	}
	C.Clear(EElysiumNpcCond::ShouldKick);                                // 103a7868 0x0f
	C.Clear(EElysiumNpcCond::ShouldDodge);                               // 103a7871 0x0c
	C.Clear(EElysiumNpcCond::ShouldStepback);                            // 103a787a 0x0e
}

// =================================================================================================
// 0x103ac500 CNPC_VScurrying::GatherConditions, 203 bytes (also CNPC_VRat)
// =================================================================================================

FElysiumEntity* FElysiumNpcScurrying::ScurryingFindDetectablePlayer()
{
	// `0x103aca80`: `UTIL_PlayerByIndex(1)`, answered only when `0x103acac0` admits it.
	FElysiumEntity* const Player = World != nullptr ? static_cast<FElysiumEntity*>(World->FindPlayer()) : nullptr;
	return ScurryingShouldDetect(Player) ? Player : nullptr;
}

void FElysiumNpcScurrying::GatherConditions()
{
	FElysiumNpcConditions& C = Cognition.Conditions;
	C.Clear(Cond19ScurryingPlayerTooClose);                              // 103ac505 0x78
	FElysiumNpc::GatherConditions();                                     // 103ac50c
	const double Now = Conditions19Now();
	auto ResolveDetected = [this]() -> FElysiumEntity*
	{
		return World != nullptr && ScurryingDetected.IsSet()
			? const_cast<FElysiumEntity*>(ElysiumNpcCond::ResolveEnemyHandle(*World, ScurryingDetected))
			: nullptr;
	};
	if (Now >= ScurryingDetectGateTime)                                  // 103ac519 / 103ac526 +0x6678 (ordered; NaN skips)
	{
		// `+0x667c` resolved (103ac531 `-1`, 103ac54e serial) into 103ac559's `0x103acac0`
		if (!ScurryingShouldDetect(ResolveDetected()))                   // 103ac528..103ac560 0x103acac0
		{
			FElysiumEntity* const Found = ScurryingFindDetectablePlayer();   // 103ac564 0x103aca80
			ScurryingDetected = Found != nullptr ? Found->Handle : FElysiumEntityHandle::Invalid();   // 103ac56b / 103ac571..103ac57e
		}
	}
	if (ResolveDetected() != nullptr)                                    // 103ac588..103ac5b3 +0x667c (103ac591 `-1`, 103ac5ae serial)
	// (the `ent_trace_conditions` read before the set, call 0x103ac5bd, is absent)
	{
		C.Set(Cond19ScurryingPlayerTooClose);                            // 103ac5c4 SetCondition(0x78)
	}
}

// =================================================================================================
// 0x103bce40 CNPC_VTzimisce::GatherConditions, 833 bytes
// =================================================================================================

float FElysiumNpcTzimisce::TzimisceThrowPosYConVar()
{
	return -80.f;   // `tzimisce_throw_pos_y` default "-80" (`0x10653a4c`)
}

int32 FElysiumNpcTzimisce::TzimiscePounceConVar()
{
	return 1;       // `tzimisce_pounce` default "1"
}

bool FElysiumNpcTzimisce::TzimisceThrowLosTest(FElysiumEntity* Enemy)
{
	// `0x103be630`.
	if (Enemy == nullptr)
	{
		return false;
	}
	const FVector RightVec = Conditions19SpeciesRightVector(*this);          // slot 221 + 0x10139610
	const FVector StartCm = EyePosition()
		- RightVec * (static_cast<double>(TzimisceThrowPosYConVar()) * ElysiumMove::U);   // slot 193 minus right * cvar
	const FVector EndCm = Enemy->EyePosition();                          // the enemy's slot 193
	// `TraceRay` mask `0x400b`, `fraction == 1.0` (double `0x10449280`): the family's live world ray.
	return Conditions19RayReaches(StartCm, EndCm);
}

bool FElysiumNpcTzimisce::TzimiscePounceTest()
{
	// `0x103bf660`.
	const FElysiumNpcBase* const ConstThis = this;
	FElysiumEntity* const Enemy = ConstThis->GetEnemy();                 // slot 167
	if (Enemy == nullptr)
	{
		return false;
	}
	FVector LeadCm = Conditions19LastKnownPosition(Enemy);               // slot 541 / 0x102dfed0
	float Tolerance = 0.f;
	ChaseLeadTolerance(Enemy, LeadCm, Tolerance);                        // 0x102c3b50 (SEAM: leaves both)
	LeadCm.Z += ElysiumNpcTunables::TenthDouble * ElysiumMove::U;        // 103bf6d6 `FADD double [0x104493d0]`
	FVector MeCm = GetOrigin();                                          // slot 220
	MeCm.Z += ElysiumNpcTunables::TenthDouble * ElysiumMove::U;          // 103bf702 `FADD double [0x104493d0]`
	const double DistSq = Conditions19SpeciesDistSqUnits(MeCm, LeadCm);
	// `103bf740..103bf75c`, `AND 0x4100 / JZ` twice: only an ordered `d < 40000` or `d > 360000`
	// refuses, so a NaN distance passes.
	if (40000.0 > DistSq || DistSq > 360000.0)                           // `_DAT_104b73e4` / `_DAT_104cc530`
	{
		return false;
	}
	FVector HullMins = FVector::ZeroVector;
	FVector HullMaxs = FVector::ZeroVector;
	RetailHullExtents(HullKind, EElysiumHullExtents::Full, HullMins, HullMaxs);   // `m_Collision` vfunc 1 / 2
	FKernelHullTrace Trace;
	KernelHullTrace(MeCm / ElysiumMove::U, LeadCm / ElysiumMove::U, HullMins, HullMaxs, 0x202400b, Trace);   // UTIL_TraceHull
	return Trace.HitEntity.IsSet() && Trace.HitEntity == Enemy->Handle;  // `tr.m_pEnt == GetEnemy()`
}

void FElysiumNpcTzimisce::GatherConditions()
{
	// Absent `ent_trace_conditions` reads before the sets: calls 0x103bcf2b 0x103bcf62 0x103bcf81
	// 0x103bcff9 0x103bd078 0x103bd08c 0x103bd133.
	FElysiumNpc::GatherConditions();                                     // 103bce47 (the base FIRST)
	FElysiumNpcConditions& C = Cognition.Conditions;
	C.Clear(Cond19TzimisceShouldDropBody);                               // 103bce50 0x77
	C.Clear(Cond19HaveEnemyThrowLos);                                    // 103bce59 0x1b
	C.Clear(Cond19TzimisceForceThrowBody);                               // 103bce62 0x78
	const FElysiumNpcBase* const ConstThis = this;
	if (TzimisceCarryFormBit())                                          // 103bce69 0x103be130 / 103bce70
	{
		const FElysiumEntity* const Pickup = World != nullptr && PickupTarget.IsSet()
			? ElysiumNpcCond::ResolveEnemyHandle(*World, PickupTarget) : nullptr;   // 103bce76..103bceac +0x6670 (103bce7f / 103bcea3 / 103bcebb / 103bced2)
		if (Pickup != nullptr
			&& Conditions19SpeciesDistSqUnits(GetAbsOrigin(), Pickup->GetAbsOrigin()) > 25600.0)   // 103bcedc / 103bcee8 / 103bcf21
		{
			C.Set(Cond19TzimisceShouldDropBody);                         // 103bcf32 SetCondition(0x77)
		}
		if (C.Has(EElysiumNpcCond::HaveEnemyLos)                         // 103bcf3b / 103bcf42
			&& TzimisceThrowLosTest(ConstThis->GetEnemy()))              // 103bcf48 slot 167 / 103bcf51 / 103bcf58
		{
			C.Set(Cond19HaveEnemyThrowLos);                              // 103bcf69 SetCondition(0x1b)
		}
		if (FUN_103be150())                                              // 103bcf70 / 103bcf77
		{
			C.Set(Cond19TzimisceForceThrowBody);                         // 103bcf88 SetCondition(0x78)
		}
	}
	FHintWords Hint;
	if (BaseScheduleHost.HintNode != INDEX_NONE && HintWords(BaseScheduleHost.HintNode, Hint)   // 103bcf8d / 103bcf95 +0x5ddc
		&& (Hint.HintType == 0x36b0 || Hint.HintType == 0x36b1))         // 103bcf9b..103bcfad (103bcfa6)
	{
		FElysiumEntity* const Enemy = GetEnemy();                        // 103bcfb7 slot 168
		if (Enemy == nullptr)                                            // 103bcfc1
		{
			C.Set(Cond19ClawHintInvalid);                                // 103bd07f SetCondition(0x1c)
			C.Set(Cond19ClawHintSpecialInvalid);                         // 103bd093 SetCondition(0x1d)
		}
		else if (IsTzimisceHintUsable(BaseScheduleHost.HintNode, Enemy)) // 103bcfd1 0x103bfc20 / 103bcfd8
		{
			C.Clear(Cond19ClawHintInvalid);                              // 103bcfde 0x1c
			C.Clear(Cond19ClawHintSpecialInvalid);                       // 103bcfe7 0x1d
		}
		else
		{
			C.Set(Cond19ClawHintInvalid);                                // 103bd000 SetCondition(0x1c)
			const double DistSq = Conditions19SpeciesDistSqUnits(Hint.OriginCm, Enemy->GetOrigin());   // 103bd009 / 103bd019 slot 220
			// SET below 10000, above 40000 or unordered; CLEARED only inside [10000, 40000].
			if (DistSq < 10000.0 || DistSq > 40000.0 || FMath::IsNaN(DistSq))   // 103bd052 / 103bd05f
			{
				C.Set(Cond19ClawHintSpecialInvalid);                     // 103bd093 SetCondition(0x1d)
			}
			else
			{
				C.Clear(Cond19ClawHintSpecialInvalid);                   // 103bd065 ClearCondition(0x1d)
			}
		}
	}
	const double Now = Conditions19Now();
	if (Now > TzimiscePounceCheckTimer)                                  // 103bd0ae (strict; NaN skips) +0x66ac
	{
		TzimiscePounceCheckTimer = Now + 2.5;                            // `_DAT_104629ec`, 103bd0c1
		bool bPounce = false;
		if (ElysiumSchedule::MaskHasCondition(Schedule, *this, Cond19CanPounce)   // 103bd0c7 0x10269c70 / 103bd0ce / 103bd0d0
			&& ConstThis->GetEnemy() != nullptr)                         // 103bd0d4 slot 167 / 103bd0dc / 103bd0de
		{
			// `GetEnemies()->GetLastKnownPosition(GetEnemy())` (103bd0e2..103bd0fa): computed and
			// handed to `0x103bf660`, which never reads it.
			(void)Conditions19LastKnownPosition(ConstThis->GetEnemy());  // 103bd0f2 slot 541
			// `tzimisce_pounce`: `!IsCommand()` (103bd10c) and `m_nValue` (103bd119), then 103bd122
			// `0x103bf660`.
			if (TzimiscePounceConVar() != 0 && TzimiscePounceTest())     // 103bd107..103bd129
			{
				C.Set(Cond19CanPounce);                                  // 103bd13a SetCondition(0x23)
				bPounce = true;
			}
		}
		if (!bPounce)
		{
			C.Clear(Cond19CanPounce);                                    // 103bd145 ClearCondition(0x23)
		}
	}
	if (Now > TzimisceShunnedBodyTimer)                                  // 103bd160 (strict) +0x66b0
	{
		TzimisceShunnedFindBody = 0;                                     // 103bd16b +0x66b8
		TzimisceShunnedBodyTimer = Now + 10.0;                           // `_DAT_1044e664`, 103bd175
	}
}

// =================================================================================================
// 0x103c17f0 CNPC_VTzimisceHeadClaw / 0x103c35a0 CNPC_VTzimisceRunner::GatherConditions, 28 bytes
// =================================================================================================

void FElysiumNpcTzimisceHeadClaw::GatherConditions()
{
	FElysiumNpc::GatherConditions();                                     // 103c17f3
	Cognition.Conditions.Clear(EElysiumNpcCond::ShouldKick);             // 103c17fc ClearCondition(0x0f)
	Cognition.Conditions.Clear(EElysiumNpcCond::ShouldStepback);         // 103c1805 ClearCondition(0x0e)
}

void FElysiumNpcTzimisceRunner::GatherConditions()
{
	FElysiumNpc::GatherConditions();                                     // 103c35a3
	Cognition.Conditions.Clear(EElysiumNpcCond::ShouldKick);             // 103c35ac ClearCondition(0x0f)
	Cognition.Conditions.Clear(EElysiumNpcCond::ShouldStepback);         // 103c35b5 ClearCondition(0x0e)
}

// =================================================================================================
// 0x103d0410 CNPC_VWerewolf::GatherConditions, 448 bytes
// =================================================================================================

int32 FElysiumNpcWerewolf::WerewolfForceTeleportConVar()
{
	return 0;       // `werewolf_force_teleport` default "0"
}

void FElysiumNpcWerewolf::GatherConditions()
{
	// Absent: the scope-trace name pick (branches 0x103d0416 0x103d0420) and the
	// `ent_trace_conditions` reads before the sets (calls 0x103d04ab 0x103d04ec 0x103d053c 0x103d055d
	// 0x103d057b).
	FElysiumNpcConditions& C = Cognition.Conditions;
	const FElysiumNpcBase* const ConstThis = this;
	FElysiumEntity* const Enemy = ConstThis->GetEnemy();                 // 103d0480 slot 167, BEFORE the base
	// `m_flTargetHullRadius (+0x66d0) + m_flHullRadius (+0x66cc) + 1.0` (double `0x10449280`).
	// The three terms sum at x87 precision (the last `FADD double [0x10449280]`) before the one
	// `FSTP float` at `103d049c`.
	const float Limit = static_cast<float>(static_cast<double>(WerewolfTeleportDistanceB)
		+ static_cast<double>(WerewolfTeleportDistanceA) + ElysiumNpcTunables::OneDouble);   // 103d0486..103d049c
	if (Enemy != nullptr)                                                // 103d04a0
	{
		C.Set(EElysiumNpcCond::SeeEnemy);                                // 103d04b2 SetCondition(0x46)
		UpdateEnemyMemory(Enemy, Enemy->GetOrigin(), nullptr);           // 103d04c2 slot 220 / 103d04cc slot 544 (vec3_origin)
		if (Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U < Limit)   // 103d04d8 / 103d04e2 (greater, equal or NaN skips)
		{
			C.Set(EElysiumNpcCond::TooCloseToAttack);                    // 103d04f3 SetCondition(0x5f)
		}
	}
	FElysiumNpc::GatherConditions();                                     // 103d04fa
	if (Enemy != nullptr)                                                // 103d04ff / 103d0502 (captured before the base)
	{
		UpdateConditionCanTeleport();                                    // 103d0506 0x103cc0d0
		UpdateConditionEnemyUnreachable();                               // 103d050d 0x103cc320
		UpdateConditionDeathTriggered();                                 // 103d0514 0x103cc890
		UpdateConditionCanSpecialMove();                                 // 103d051b 0x103cc5c0
		UpdateConditionShouldBreakHint();                                // 103d0522 0x103cc450
	}
	if (C.Has(EElysiumNpcCond::TooFarForMelee))                          // 103d052b / 103d0532
	{
		C.Set(EElysiumNpcCond::TooFarToAttack);                          // 103d0543 SetCondition(0x60)
	}
	if (C.Has(EElysiumNpcCond::TooFarToAttack))                          // 103d054c / 103d0553
	{
		C.Set(EElysiumNpcCond::TooFarForMelee);                          // 103d0564 SetCondition(0x09)
	}
	if (MoveHintNode != INDEX_NONE)                                      // 103d0569 / 103d0571 m_pMoveHint +0x66bc
	{
		C.Set(Cond19WerewolfCanSpecialMove);                             // 103d0582 SetCondition(0x78)
	}
	if (WerewolfForceTeleportConVar() != 0)                              // 103d058f..103d05a0 `!IsCommand() && m_nValue` (103d0594)
	{
		C.Clear(EElysiumNpcCond::CanMeleeAttack1);                       // 103d05a6 0x51
		C.Clear(EElysiumNpcCond::CanMeleeAttack2);                       // 103d05af 0x52
		C.Clear(Cond19WerewolfCanSpecialMove);                           // 103d05b8 0x78
		C.Clear(Cond19WerewolfEnemyReachable);                           // 103d05c1 0x79
	}
}
