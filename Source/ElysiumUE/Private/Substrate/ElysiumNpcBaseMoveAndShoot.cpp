// `CAI_MoveAndShootOverlay` (embedded at `CAI_BaseNPC +0x5cf4`): the run-and-gun wire (spec 0002
// V4o, lane O2). The three bodies `RunTaskOverlay 0x10289c90` reaches -- `0x102e8560` (the run),
// `0x102e83e0` (can it aim) and `0x102e84a0` (the navigator's movement activity) -- and the weapon
// reads they take. The disable `0x102e8250` and the arm `0x102e8270` are in
// `ElysiumNpcBaseSenses10.cpp`, beside slot 445 which calls them. Declarations:
// `ElysiumNpcBaseSenses10.inl`. The walk: `docs/vtmb/animation_events.md` "The move-and-shoot
// overlay, arm by arm".

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEngineRandom.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Qualified at every use: a unity build concatenates translation units.
namespace NpcKernelMoveAndShoot
{
	// The conditions the three bodies read and write, by retail number.
	inline constexpr EElysiumNpcCond CondNoPrimaryAmmo = static_cast<EElysiumNpcCond>(0x40);
	inline constexpr EElysiumNpcCond CondEnemyOccluded = static_cast<EElysiumNpcCond>(0x48);
	inline constexpr EElysiumNpcCond CondCanRangeAttack1 = static_cast<EElysiumNpcCond>(0x4f);
	inline constexpr EElysiumNpcCond CondNewEnemy = static_cast<EElysiumNpcCond>(0x54);
	inline constexpr EElysiumNpcCond CondEnemyTooFar = static_cast<EElysiumNpcCond>(0x55);
	inline constexpr EElysiumNpcCond CondEnemyDead = static_cast<EElysiumNpcCond>(0x58);
	inline constexpr EElysiumNpcCond CondTooFarToAttack = static_cast<EElysiumNpcCond>(0x60);

	// The activities, by retail number.
	inline constexpr int32 ActWalk = 9;                  // ACT_WALK
	inline constexpr int32 ActWalkAim = 0x11;            // its aim twin
	inline constexpr int32 ActRun = 0x13;                // ACT_RUN
	inline constexpr int32 ActRunAim = 0x15;             // its aim twin
	inline constexpr int32 ActRangeAttack1 = 0x19;       // the weapon's own activity (`102e883b PUSH 0x19`)
	inline constexpr int32 ActRangeAttack1Layer = 0x1a;  // the layer pushed over the gait
	inline constexpr int32 ActLookbackLeft = 0x47;       // `lookback_left_layer`
	inline constexpr int32 ActLookbackRight = 0x48;      // `lookback_right_layer`

	inline constexpr float AimSwapShotDelay = 0.3f;      // double `0x1047b868`
	inline constexpr float FireRateLead = 0.1f;          // float `0x104493d0`
	inline constexpr float LookbackMinYaw = 90.f;        // double `0x1044e668`, strict
	inline constexpr double LookbackYawTop = 190.0;      // double `0x1049d910`
	inline constexpr double LookbackYawScale = 0.25;     // double `0x10449260`
	inline constexpr int32 LookbackDrawBelow = 5;        // `102e869e CMP EAX,5 / JGE`
	inline constexpr int32 StateAlert = 2;               // `0x1026e340(this, 2)`

	inline float NowOf(const FElysiumNpcBase& Npc)
	{
		// `gpGlobals->curtime` (`DAT_1070b228 + 0xc`), a float in retail.
		return Npc.World != nullptr ? static_cast<float>(Npc.World->NowSeconds()) : 0.f;
	}
}

bool FElysiumNpcBase::ActiveWeaponBurstWords(int32& OutMin, int32& OutMax) const
{
	// `0x102517e0(GetActiveWeapon())`: the weapon's current mode record; `+0x3a4` is `BurstMin`
	// (`0x10259664`), `+0x3a8` is `BurstMax` (`0x10259678`). The record is the primary mode in
	// force, as `ActiveWeaponBurstPauseWords` (`ElysiumNpcConditions10.cpp`) reads the same call.
	OutMin = 0;
	OutMax = 0;
	if (ActiveWeaponEntity() == nullptr)
	{
		return false;
	}
	FElysiumItem* const Item = Inventory.Active(*this);
	const FElysiumWeapon* const Weapon = Item != nullptr ? Item->AsWeapon() : nullptr;
	const FElysiumWeaponMode* const Mode =
		Weapon != nullptr ? Weapon->ModeFor(FElysiumWeapon::EIntent::Primary) : nullptr;
	if (Mode == nullptr)
	{
		// SEAM for `+0x3a4` / `+0x3a8` on an active item no mode record stands behind: 0 / 0.
		return true;
	}
	OutMin = Mode->BurstMin;                                             // data +0x3a4
	OutMax = Mode->BurstMax;                                             // data +0x3a8
	return true;
}

float FElysiumNpcBase::ActiveWeaponFireRate() const
{
	// Weapon slot 332 `0x10254410`: `0x1033d940(owner 0x10252240(weapon), 0x102517e0(weapon)[+0x260])`.
	// `+0x260` is the mode record's `Attack_Rate`. SEAM for `0x1033d940`: it doubles the rate when
	// `0x101e3f50(&0x10739a4c, owner)` finds a Presence level bit in the owner's
	// `m_iDisciplineFlags2`; nothing is read for it here, so the rate is unscaled.
	FElysiumItem* const Item = Inventory.Active(*this);
	const FElysiumWeapon* const Weapon = Item != nullptr ? Item->AsWeapon() : nullptr;
	const FElysiumWeaponMode* const Mode =
		Weapon != nullptr ? Weapon->ModeFor(FElysiumWeapon::EIntent::Primary) : nullptr;
	return Mode != nullptr ? Mode->AttackRate : 0.f;                     // data +0x260
}

float FElysiumNpcBase::MoveYawPoseParameter() const
{
	// `CBaseAnimating::GetPoseParameter("move_yaw")` (`0x102e865f CALL 0x1000108c`). The kernel's
	// pose-parameter record is name-keyed here (`PoseParameterWrites`): its live value is the last
	// write of the name, 0 when there is none (retail's rest value).
	for (int32 Index = PoseParameterWrites.Num() - 1; Index >= 0; --Index)
	{
		if (PoseParameterWrites[Index].Name.Equals(TEXT("move_yaw"), ESearchCase::IgnoreCase))
		{
			return PoseParameterWrites[Index].Value;
		}
	}
	return 0.f;
}

bool FElysiumNpcBase::CanAimAtEnemy()
{
	// `0x102e83e0`.
	const FElysiumNpcBase* const ConstThis = this;
	const FElysiumNpcConditions& Conds = Cognition.Conditions;
	// `m_bConditionsGathered (+0x5ca4)` clear -- `RunAI 0x1026f110` cleared it and slot 433 has not
	// run yet this think -- : slot 481 `GatherEnemyConditions(GetEnemy())` (`+0x784`, enemy from
	// slot 167 `+0x29c`), so the enemy conditions, the attack conditions among them, are gathered
	// BEFORE `0x4f` is read. The byte is the port's stamp `Cognition.GatheredAt` (negative = clear).
	if (Cognition.GatheredAt < 0.0)
	{
		GatherEnemyConditions(ConstThis->GetEnemy());                    // slot 167, slot 481 0x10270b20
	}
	if (Conds.Has(NpcKernelMoveAndShoot::CondCanRangeAttack1))      // HasCondition(0x4f)
	{
		return true;
	}
	if (!Conds.Has(NpcKernelMoveAndShoot::CondEnemyDead)            // 0x58
		&& !Conds.Has(NpcKernelMoveAndShoot::CondTooFarToAttack)    // 0x60
		&& !Conds.Has(NpcKernelMoveAndShoot::CondEnemyTooFar)       // 0x55
		&& !Conds.Has(NpcKernelMoveAndShoot::CondEnemyOccluded))    // 0x48
	{
		return !Conds.Has(NpcKernelMoveAndShoot::CondNoPrimaryAmmo);   // 0x40
	}
	return false;
}

void FElysiumNpcBase::UpdateMoveShootActivity(bool bCanAim)
{
	// `0x102e84a0`.
	const int32 CurrentActivity = Navigator.GetMovementActivity();              // 0x102ee3f0 (m_pNavigator +0x5d34)
	int32 Swapped = 0;
	if (!bCanAim)
	{
		if (CurrentActivity == NpcKernelMoveAndShoot::ActWalkAim)                // 0x11 -> 9
		{
			Swapped = NpcKernelMoveAndShoot::ActWalk;
		}
		else if (CurrentActivity == NpcKernelMoveAndShoot::ActRunAim)            // 0x15 -> 0x13
		{
			Swapped = NpcKernelMoveAndShoot::ActRun;
		}
		else
		{
			return;                                                      // nothing written
		}
	}
	else if (CurrentActivity == NpcKernelMoveAndShoot::ActWalk)                  // 9 -> 0x11
	{
		Swapped = NpcKernelMoveAndShoot::ActWalkAim;
	}
	else if (CurrentActivity == NpcKernelMoveAndShoot::ActRun)                   // 0x13 -> 0x15
	{
		Swapped = NpcKernelMoveAndShoot::ActRunAim;
	}
	else
	{
		return;                                                          // nothing written
	}
	// `fVar1 = curtime + 0.3` (`0x1047b868`); `fVar1 <= +0x18` keeps the clock, else `+0x18 = fVar1`:
	// the next shot is never sooner than 0.3 s after a swap.
	const float ShotFloor = NpcKernelMoveAndShoot::NowOf(*this) + NpcKernelMoveAndShoot::AimSwapShotDelay;
	if (!(ShotFloor <= MoveAndShootOverlay.NextShotTime))
	{
		MoveAndShootOverlay.NextShotTime = ShotFloor;
	}
	// The navigator's word only; the next move step commits it (`NavMoveNormalPass`' slot 310).
	NavSetMovementActivity(Swapped);                                     // 0x102ee250
}

void FElysiumNpcBase::RunMoveAndShootOverlay()
{
	// `0x102e8560`. The `(*DAT_10924ab4)->vfunc1()` calls on its exits are the profiler's and the
	// `(*DAT_10924a6c)->vfunc1()` before `SetCondition` is the condition trace's: no port line.
	FMoveAndShootOverlay& Words = MoveAndShootOverlay;
	const FElysiumNpcBase* const ConstThis = this;

	// 1. `+0x18 == FLT_MAX` (`0x7f7fffff`, compared as bits): disabled.
	if (Words.NextShotTime == MAX_flt)
	{
		return;
	}

	// 2. No enemy (slot 167), or the enemy's slot 158 `IsAlive` false: slot 478 `BestEnemy`; none
	//    -> slot 560 `ClearAttackConditions`; else `SetCondition(0x54)`, `SetEnemy`, `SetState(2)`.
	{
		FElysiumEntity* const Enemy = ConstThis->GetEnemy();             // slot 167 +0x29c
		if (Enemy == nullptr || !Enemy->IsAlive())                       // slot 158 +0x278
		{
			FElysiumEntity* const Best = BestEnemy();                    // slot 478 +0x778
			if (Best == nullptr)
			{
				ClearAttackConditions();                                 // slot 560 +0x8c0
			}
			else
			{
				Cognition.Conditions.Set(NpcKernelMoveAndShoot::CondNewEnemy);   // SetCondition(0x54)
				ElysiumNpcEnemy::SetEnemy(*this, Best->Handle);          // SetEnemy
				SetState(NpcKernelMoveAndShoot::StateAlert);             // 0x1026e340(this, 2)
			}
		}
	}

	// 3. No enemy, or `IsGoalActive 0x102ee6a0` false: return.
	FElysiumEntity* const Enemy = ConstThis->GetEnemy();                 // slot 167
	if (Enemy == nullptr || !Navigator.IsGoalActive())                   // 0x102ee6a0
	{
		return;
	}

	// 4, 5. Can it aim, and the navigator's movement activity for the answer.
	const bool bCanAim = CanAimAtEnemy();                                // 0x102e83e0
	UpdateMoveShootActivity(bCanAim);                                    // 0x102e84a0

	// 6. Cannot aim: leave the moving-and-shooting state (slot 558, empty on every class). No tail.
	if (!bCanAim)
	{
		if (Words.bMovingAndShooting)                                  // +0x10
		{
			Words.bMovingAndShooting = false;
			OnEndMoveAndShoot();                                         // slot 558 +0x8b8
		}
		return;
	}

	// 7. The look-back gesture (`0x102e8626..0x102e877a`), behind `debug_allow_mf_turn` (object
	//    `0x10923cf0`, shipped "0": no gesture and no draw). The gate, in order: `IsCommand()` false
	//    (the ConVar's own answer); the int value (`+0x2c`) non-zero; the Troika self-cast `+0x98`;
	//    `|GetPoseParameter("move_yaw")| > 90.0`, strict; `RandomInt(0, ftol((190.0 - |yaw|) *
	//    0.25)) < 5`.
	if (ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::DebugAllowMfTurn) != 0)   // 0x102e863f
	{
		const FElysiumNpc* const Troika = AsNpc();                       // 0x102e864a [ESI+0x98]
		if (Troika != nullptr)
		{
			const float AbsYaw = FMath::Abs(MoveYawPoseParameter());     // 0x102e865f / 0x102e8664 FABS
			if (AbsYaw > NpcKernelMoveAndShoot::LookbackMinYaw)          // 0x102e8666 FCOM 0x1044e668
			{
				const int32 DrawTop = static_cast<int32>(                // 0x102e8679..0x102e868d __ftol
					(NpcKernelMoveAndShoot::LookbackYawTop - static_cast<double>(AbsYaw))
					* NpcKernelMoveAndShoot::LookbackYawScale);
				if (ElysiumNpcEngineRandom::RandomInt(0, DrawTop)        // 0x102e869b
					< NpcKernelMoveAndShoot::LookbackDrawBelow)          // 0x102e869e
				{
					int32 WeaponActivity = 0;
					const int32 LookLeft = TranslateActivityNumber(      // 0x102e86ad TranslateActivity(0x47)
						NpcKernelMoveAndShoot::ActLookbackLeft, WeaponActivity);
					const int32 LookRight = TranslateActivityNumber(     // 0x102e86be TranslateActivity(0x48)
						NpcKernelMoveAndShoot::ActLookbackRight, WeaponActivity);
					if (LookLeft != INDEX_NONE && LookRight != INDEX_NONE    // 0x102e86c3 / 0x102e86d0
						&& !HasLayer(LookLeft)                           // slot 270 +0x438, 0x102e86de
						&& !HasLayer(LookRight))                         // 0x102e86f5
					{
						// `0x102e86ff..0x102e8716`: the waypoint after the head one (`path+0x24`, its
						// `+0x30`) when the route has one, else `ActualGoalPosition 0x102ee140`. SEAM for
						// the next waypoint: the waypoint list is the body's, so the goal position
						// answers for both arms.
						const FVector GoalCm = Navigator.GetGoalPos();   // 0x102e8716 0x102ee140
						const FVector DeltaCm = GoalCm - Origin;         // slot 217 +0x364 GetAbsOrigin
						// The 2-D dot with `m_vecRight (+0x629c / +0x62a0)`, retail frame: this world's Y
						// is negated (`bsp.source_to_unreal`); the scale does not move the sign.
						const double Dot = DeltaCm.X * Troika->Right.X + (-DeltaCm.Y) * Troika->Right.Y;   // 0x102e8732..0x102e874f
						// `FCOMP 0.0` (`0x104454c4`): `<= 0` -> `0x48`, else `0x47`.
						AddGesture(Dot <= 0.0 ? LookRight : LookLeft, true);   // 0x102e8775 / 0x102e8767 AddGesture(act, 1)
					}
				}
			}
		}
	}

	// 8. The shot: `COND 0x4f` (plain `HasCondition`, `0x10269b30`) and the clock due.
	//    Every arm of it ends on the tail below.
	const float Now = NpcKernelMoveAndShoot::NowOf(*this);
	if (Cognition.Conditions.Has(NpcKernelMoveAndShoot::CondCanRangeAttack1)   // 0x102e8784
		&& Words.NextShotTime <= Now)
	{
		// Entering the state asks slot 557 (`return 1` on every class); false -> the tail.
		if (Words.bMovingAndShooting || OnBeginMoveAndShoot())         // slot 557 +0x8b4
		{
			Words.bMovingAndShooting = true;                           // +0x10
			int32 WeaponActivity = 0;
			const int32 LayerActivity = TranslateActivityNumber(         // TranslateActivity(0x1a)
				NpcKernelMoveAndShoot::ActRangeAttack1Layer, WeaponActivity);
			--Words.MoveShots;                                         // +0x14
			if (Words.MoveShots < 0)
			{
				// The burst is spent: re-draw the count, pause, leave the state (slot 558).
				Words.MoveShots = ElysiumNpcEngineRandom::RandomInt(   // RandomInt(+0x1c, +0x20)
					Words.MinBurst, Words.MaxBurst);
				const float PauseSeconds = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
					.FRandRange(Words.PauseMin, Words.PauseMax);     // RandomFloat(+0x24, +0x28)
				Words.bMovingAndShooting = false;
				Words.NextShotTime = PauseSeconds + Now;                      // +0x18
				OnEndMoveAndShoot();                                     // slot 558 +0x8b8
			}
			else
			{
				if (HasLayer(LayerActivity))                             // slot 270 +0x438
				{
					// Retail only prints (two `DevMsg(1, …)`); the push below still runs.
					const FElysiumEntity* const HeldWeapon = ActiveWeaponEntity();
					UE_LOG(LogElysiumNpcEnt, Verbose,
						TEXT("WARNING: The attack layer for the %s lasts longer than the fire rate. ")
						TEXT("The animation needs to be shortened."),
						HeldWeapon != nullptr ? *HeldWeapon->DebugString() : TEXT("(no weapon)"));
				}
				LastAttackTime = static_cast<double>(Now);               // m_flLastAttackTime +0x5d9c
				AddGesture(LayerActivity, true);                         // 0x100991b0 AddGesture(act, 1)
				// `Weapon_SetActivity(Weapon_TranslateActivity(0x19), 0)`: slot 381 (`+0x5f4`) then
				// `0x1032a910`.
				WeaponSetActivity(Weapon_TranslateActivity(NpcKernelMoveAndShoot::ActRangeAttack1), 0.f);
				// `+0x18 = (weapon slot 332 + curtime) - 0.1` (`0x104493d0`).
				Words.NextShotTime = (ActiveWeaponFireRate() + Now)    // weapon +0x530 0x10254410
					- NpcKernelMoveAndShoot::FireRateLead;
			}
		}
	}

	// 9. The tail (`LAB_102e8876`): `0x10279bb0` -- the enemy memory's last known position
	//    (`0x102dfed0`) -- into slot 517 `AddFacingTarget(GetEnemy(), lkp, 1.0, 0.8, 0)`.
	FVector LastKnownCm = FVector::ZeroVector;
	EnemyLastKnownPosition(LastKnownCm);                                 // 0x10279bb0 -> 0x102dfed0
	AddFacingTarget(ConstThis->GetEnemy(), LastKnownCm, 1.0f, 0.8f, 0.f);   // slot 167, slot 517 +0x814 (0x3f800000, 0x3f4ccccd, 0)
}
