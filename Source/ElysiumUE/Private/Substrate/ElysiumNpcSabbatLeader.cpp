#include "Substrate/ElysiumNpcSabbatLeader.h"

#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpcMotor2Shared.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcDamage2Shared.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumNpcHintsShared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcPositionsShared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcScheduleShared.h"
#include "Substrate/ElysiumNpcSoundsShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpeciesMisc10_2Shared.h"
#include "Substrate/ElysiumNpcState19_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// The two wav tables `CNPC_VSabbatLeader::Precache` `0x103a6ab0` precaches, contiguous in
	// `.rdata`: `0x1064c480`, seven entries, and `0x1064c49c`, three.
	const TCHAR* const GSabbatLeaderSteps[] = {
		TEXT("character/monster/andrei_transformed/step1.wav"),
		TEXT("character/monster/andrei_transformed/step2.wav"),
		TEXT("character/monster/andrei_transformed/step3.wav"),
		TEXT("character/monster/andrei_transformed/step4.wav"),
		TEXT("character/monster/andrei_transformed/step5.wav"),
		TEXT("character/monster/andrei_transformed/step6.wav"),
		TEXT("character/monster/andrei_transformed/step7.wav"),
	};
	const TCHAR* const GSabbatLeaderExertHeavy[] = {
		TEXT("character/monster/andrei_transformed/exert_heavy_1.wav"),
		TEXT("character/monster/andrei_transformed/exert_heavy_2.wav"),
		TEXT("character/monster/andrei_transformed/exert_heavy_3.wav"),
	};

	constexpr float GStuckDegenerate = 9.999999974752427e-07f;  // _DAT_104c3d14
	constexpr float GStuckHalf = ElysiumNpcTunables::Half;
	constexpr float GStuckOne = ElysiumNpcTunables::One;
	constexpr float GStuckLift = ElysiumNpcTunables::Five;
	// `AngleVectors` `0x10139550`, forward only — pitch AND yaw, unlike
	// `ElysiumSkeletalBasis::FromSourceAngles`, which is the yaw-only standing-body form.
	FVector RetailForward(const FVector& SourceAngles)
	{
		const float Pitch = FMath::DegreesToRadians(static_cast<float>(SourceAngles.X));
		const float Yaw = FMath::DegreesToRadians(static_cast<float>(SourceAngles.Y));
		return FVector(FMath::Cos(Pitch) * FMath::Cos(Yaw),
			-(FMath::Cos(Pitch) * FMath::Sin(Yaw)), -FMath::Sin(Pitch));
	}
	// `AngleVectors` (`0x10139550`), forward only — the same body family Facing already recovered
	// (`ElysiumNpcFacing.cpp` § `RetailForward`), repeated here rather than exported because
	// it is a four-line Source identity and a cross-family header would be the larger dependency.
	FVector HintsRetailForward(const FVector& SourceAngles)
	{
		const float Pitch = FMath::DegreesToRadians(static_cast<float>(SourceAngles.X));
		const float Yaw = FMath::DegreesToRadians(static_cast<float>(SourceAngles.Y));
		return FVector(FMath::Cos(Pitch) * FMath::Cos(Yaw),
			-(FMath::Cos(Pitch) * FMath::Sin(Yaw)), -FMath::Sin(Pitch));
	}
	constexpr int32 GMaintainSabbatFailureFirst = 0x0c;
	constexpr int32 GMaintainSabbatFailureLast = 0x0f;
	constexpr float GMaintainSabbatRouteFailThreshold = 6.f; // _DAT_104c3cc0 = 00 00 c0 40
	// `CNPC_VSabbatLeader`'s melee-interrupt exception activity (`0x103ab400`).
	constexpr int32 GMiscSabbatLeaderMeleeActivity = 0x1141;
	constexpr float GSabbatLeadScale = 25.0f;       // DAT_104c3cb8
	constexpr float GNoJumpZoneDistance = 100.0f;   // DAT_104c3ccc
	constexpr int32 GNoJumpHintType = 0x3e84;
	constexpr float NormalizeEpsilon = ElysiumNpcTunables::FloatEpsilon;
	// `CNPC_VSabbatLeader`'s three node pickers.
	constexpr float ArchwayMinFlatDist = 24.0f;           // _DAT_104c3cbc
	constexpr float DiveMinFlatDist = 40.0f;              // _DAT_104c3cfc
	constexpr float DiveMinSelfDist = 45.0f;              // _DAT_104c3d00
	constexpr float DiveLengthEpsilon = 9.999999747378752e-05f;   // _DAT_104c3ce4
	constexpr int32 HintArchway = 0x3e82;
	constexpr int32 HintDive = 0x3e85;
	// `FUN_10137220` — Source's `VectorNormalize`, which divides by `length + _DAT_1046a51c` rather
	// than by the length, and returns the length in `ST0` (the decompiler prints it `void`; the
	// listing's trailing `FLD`/`FSTP` pattern leaves the length on the stack). The epsilon matters
	// only for a zero vector, which retail therefore leaves at zero rather than faulting.
	float RetailVectorNormalize(FVector& InOutVector)
	{
		const float Length = static_cast<float>(InOutVector.Size());
		const float Scale = NpcKernelPositionsShared::RetailOne / (NormalizeEpsilon + Length);
		InOutVector *= Scale;
		return Length;
	}
	const TCHAR* const GSabbatLeaderModels[] = {
		TEXT("models/character/monster/Andrei/andrei.mdl"),
		TEXT("models/character/npc/unique/hollywood/andrei/andrei_no_mouth.mdl"),
	};
	const TCHAR* const GAndreiTransformedSteps[] = {   // 0x1064c480, to 0x1c — seven
		TEXT("character/monster/andrei_transformed/step1.wav"),
		TEXT("character/monster/andrei_transformed/step2.wav"),
		TEXT("character/monster/andrei_transformed/step3.wav"),
		TEXT("character/monster/andrei_transformed/step4.wav"),
		TEXT("character/monster/andrei_transformed/step5.wav"),
		TEXT("character/monster/andrei_transformed/step6.wav"),
		TEXT("character/monster/andrei_transformed/step7.wav"),
	};
	const TCHAR* const GAndreiTransformedExerts[] = {  // 0x1064c49c, to 0xc — three
		TEXT("character/monster/andrei_transformed/exert_heavy_1.wav"),
		TEXT("character/monster/andrei_transformed/exert_heavy_2.wav"),
		TEXT("character/monster/andrei_transformed/exert_heavy_3.wav"),
	};
	// The seven singles, in the body's own push order (`0x1064ea40` down to `0x1064e8b0`).
	const TCHAR* const GAndreiTransformedSingles[] = {
		TEXT("Character/Monster/Andrei_Transformed/ambient_run.wav"),
		TEXT("Character/Monster/Andrei_Transformed/Leap_Down_Attack_1.wav"),
		TEXT("Character/Monster/Andrei_Transformed/dive_in_splash.wav"),
		TEXT("Character/Monster/Andrei_Transformed/dive_out_splash.wav"),
		TEXT("Character/Monster/Andrei_Transformed/splash_warning.wav"),
		TEXT("Character/Monster/Andrei_Transformed/jump_retreat.wav"),
		TEXT("Character/Monster/Andrei_Transformed/roar_1.wav"),
	};
	const TCHAR* const GAndreiPowerupEmitter = TEXT("Andrei_powerup_emitter");
	const TCHAR* const GAndreiBlastEmitter = TEXT("Andrei_blast_emitter");
	const TCHAR* const GSabbatLeaderWeapon = TEXT("item_w_sabbatleader_attack");
	// `_DAT_104c3cd4` — `CNPC_VSabbatLeader`'s `TOO_FAR_TO_ATTACK` distance bound, 120 units
	// (`103aa25f FCOMP float ptr`).
	constexpr float GScheduleSabbatTooFarUnits = ElysiumNpcTunables::SabbatLeaderTooFarToAttack;
	// `_DAT_104c3cc8` = **8.0** s — the idle window `0x103c67f0` is handed by
	// `CheckForJumpCondition`, measured from `m_flLastAttackTime` (`+0x5d9c`).
	constexpr float GSabbatJumpIdleSeconds = 8.f;
	// `_DAT_104c3cc4` = **0.0666667** — the health fraction lost since the mark that also allows it.
	constexpr float GSabbatJumpHealthLoss = 0.06666667f;
	// `_DAT_104c3cdc` = **0.25** s — the blood-splash repeat interval.
	constexpr double GSabbatSplashIntervalSeconds = 0.25;
	// `_DAT_104c3ce0` = **2.0** — the wound-counter rise `PlayerDamagedEnoughThisRound` requires.
	constexpr float GSabbatRoundDamageThreshold = 2.f;
}

// Slot 420: `0x103a6d40`.
// `0x103a6d40`
void FElysiumNpcSabbatLeader::NPCInit()
{
	VampireBossNPCInit();
	SabbatLeaderRouteFailCount = 0;
	FailureType = 0;
	bSabbatLeaderActivated = false;
	VampireBossMonsterModelName = TEXT("models/character/monster/Andrei/andrei.mdl");
	LastAttackTime = NpcKernelLifecycle19_2Shared::Lifecycle19_2Now(*this);
	VampireBossMonsterClassname = TEXT("npc_VSabbatLeader");
	SetBodyEmitterName(0, TEXT("Andrei_powerup_emitter"));
	SetBodyEmitterName(1, TEXT("Andrei_powerup_emitter"));
	// `103a6de9 MOV dword ptr [ESI + 0x66c4],EBX` is `CNPC_VSabbatLeader::m_nLastWaterLevel`, the
	// splash detector's edge latch — NOT the entity's own `m_nWaterLevel` (`+0x03e0`).
	SabbatLastWaterLevel = 0;                                            // 103a6de9 +0x66c4
	SabbatLastSplashTime = 0.0;
	RecordPlayerHealth();
	bSabbatLeaderTrackPlayer = false;
	bSabbatDiving = false;
	bSabbatLeaderLargeSplash = false;
	bSabbatLeaderParticleSpawned = false;
	SabbatLeaderJumpBloodBalance = 0;
	SabbatLeaderRoarAttackCount = 3;
	bSabbatLeaderLastAttackWasNova = false;
}

// Slot 104: `0x103a6ab0`.
// 0x103a6ab0
void FElysiumNpcSabbatLeader::Precache()
{
	// `CNPC_VSabbatLeader::Precache` `0x103a6ab0` — scope-trace frame, the Troika body, two models
	// with preload 1, the seven-entry step table, the three-entry exert table, seven singles, two
	// preload-1 emitters, and the attack weapon.
	TroikaPrecache();
	for (const TCHAR* AndreiModel : GSabbatLeaderModels)
	{
		NpcKernelPrecache10Shared::Precache10Model(*this, AndreiModel, /*Preload=*/1);
	}
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GAndreiTransformedSteps, UE_ARRAY_COUNT(GAndreiTransformedSteps));
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GAndreiTransformedExerts, UE_ARRAY_COUNT(GAndreiTransformedExerts));
	for (const TCHAR* Single : GAndreiTransformedSingles)
	{
		NpcKernelPrecache10Shared::Precache10Sound(*this, Single);
	}
	NpcKernelPrecache10Shared::Precache10Particle(*this, GAndreiPowerupEmitter, /*Preload=*/1);
	NpcKernelPrecache10Shared::Precache10Particle(*this, GAndreiBlastEmitter, /*Preload=*/1);
	NpcKernelPrecache10Shared::Precache10Other(*this, GSabbatLeaderWeapon);
}

// Slot 461: `0x103a7450`.
int32 FElysiumNpcSabbatLeader::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x1f;
	if (bSabbatLeaderActivated)
	{
		if (NpcStateRetail() == 2)
		{
			const FElysiumEntity* const Closest = World != nullptr
				? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
			if (Closest != nullptr)
			{
				return IdealStateRetail();
			}
		}
		else
		{
			if (GetEnemy() != nullptr)
			{
				Mind.WriteIdealStateRetail(2);
				return IdealStateRetail();
			}
			FElysiumEntity* const Closest = World != nullptr
				? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
			if (Closest != nullptr)
			{
				ElysiumNpcEnemy::SetEnemy(*this, Closest->Handle);
				Mind.WriteIdealStateRetail(2);
				return IdealStateRetail();
			}
		}
	}
	Mind.WriteIdealStateRetail(1);
	return IdealStateRetail();
}

// Slot 604: `0x103aa060`, which replaces the Troika body wholesale; its argument is read by no arm.
// `CNPC_VSabbatLeader::SelectScheduleMeleeCombat` `0x103aa060`, its slot-604 override's body.
int32 FElysiumNpcSabbatLeader::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumEntity* Enemy = const_cast<FElysiumEntity*>(World != nullptr
		? ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy) : nullptr);
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	if (!bInMelee && !Slot599(0))
	{
		return ScheduleHost.EnemyDistUnits <= MeleeRangeUnits() * 2.0f ? 0x15f : 0xe7;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyOccluded))
	{
		return 0xcd;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		return 0xdd;
	}
	// The leader runs the timer's first two arms only and never reads the answer.
	(void)NpcKernelScheduleShared::TickMeleeHeightDiffTimer(*this, Now);
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(Enemy);
		return 0x15b;
	}
	if (Conds.Has(EElysiumNpcCond::TooFarToAttack)
		&& ScheduleHost.EnemyDistUnits < GScheduleSabbatTooFarUnits)
	{
		return 0xdd;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee)
		&& !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		return 199;
	}
	return 0xcb;
}

// Slot 448: `0x103a9400`. Its route-flip arm returns at once (`0x103a94bc` / `0x103a94db`);
// otherwise a direct call into the Troika body `0x1029adb0`.
void FElysiumNpcSabbatLeader::TaskFail(int32 Reason)
{
	if (SabbatLeaderTaskFail(Reason))
	{
		return;
	}
	FElysiumNpc::TaskFail(Reason);
}

// Slot 440: `0x103a7390`.
// `0x103a7390`
// `0x103a7390`, `CNPC_VSabbatLeader::TranslateSchedule`, the body of `FElysiumNpcSabbatLeader::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcSabbatLeader::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber > 0xe4 && ScheduleNumber < 0xe7)
	{
		return 0x15f;
	}
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 24: `0x103ab4a0`, a replacement that never calls the Troika body.
// `0x103ab4a0`
void FElysiumNpcSabbatLeader::OnVictimHitByMe(FElysiumEntity* Victim)
{
	// `CNPC_VSabbatLeader::OnVictimHitByMe` `0x103ab4a0`, scope trace stripped:
	//     ent = resolve(m_hClosestPlayer);                 // +0x628c, index & 0x1fff,
	//                                                      // generation >> 0xd
	//     if (ent == param_1 && --m_RoarAttackCount < 0) m_RoarAttackCount = 0;
	//
	// The decrement is INSIDE the condition's second term, so it happens only when the victim is the
	// tracked player; the clamp is a separate test on the decremented value. It also does NOT call
	// the Troika line — the base's record clear does not run for a Sabbat leader.
	const FElysiumEntity* Closest = World != nullptr && Senses.Memory.ClosestPlayer.IsSet()
		? World->Resolve(Senses.Memory.ClosestPlayer)
		: nullptr;
	if (Closest != nullptr && Closest == Victim)
	{
		--SabbatLeaderRoarAttackCount;
		if (SabbatLeaderRoarAttackCount < 0)
		{
			SabbatLeaderRoarAttackCount = 0;
		}
	}
}

// Slot 590: `0x103ab400`, whose miss calls the Troika body `0x1029f940` directly.
/** `CNPC_VSabbatLeader::OkToInterruptForMelee` (`0x103ab400`) — the body of the class's override. */
bool FElysiumNpcSabbatLeader::OkToInterruptForMelee()
{
	// `CNPC_VSabbatLeader::OkToInterruptForMelee` (`0x103ab400`), the body of
	// `FElysiumNpcSabbatLeader::OkToInterruptForMelee`. Scope trace stripped:
	//     if (m_Activity (+0xfec) != 0x1141) return CAI_BaseNPCTroika::OkToInterruptForMelee();
	//     return true;
	// An exception, not a replacement: activity `0x1141` is always interruptible for the Sabbat
	// leader and everything else is a direct call into the Troika line.
	if (ActivityNumber != GMiscSabbatLeaderMeleeActivity)
	{
		return FElysiumNpc::OkToInterruptForMelee();
	}
	return true;
}

// Slot 566: `0x103a9340`, a replacement that does not chain.
bool FElysiumNpcSabbatLeader::FValidateHintType(void* Hint)
{
	// `15999 < t && t < 0x3e86`: 16000..16005 inclusive.
	// Retail dereferences the hint unchecked; a hint the seam could not resolve reads type 0
	// here, which the rule refuses either way.
	const FHintWords* Words = static_cast<const FHintWords*>(Hint);
	const int32 HintType = Words != nullptr ? Words->HintType : 0;
	return 15999 < HintType && HintType < 0x3e86;
}

// Slot 546: `0x103a5e50`, the class's own schedule id space.
const TCHAR* FElysiumNpcSabbatLeader::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093c3d4`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VSabbatLeader"), TEXT("0x103a5e50"), TEXT("0x1093c3d4") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 620: `0x103aa5e0`, a virtual `CNPC_VSabbatLeader` introduces, inside a VPROF scope:
// `RandomInt(0, 6)` (`PUSH 0x6` at `103aa6c9`) over the seven step wavs `0x1064c480`, one
// `EmitSound` at volume 1.0 on `CHAN_BODY` (`PUSH 0x4` at `103aa6dd`). NOT an animation event --
// `ElysiumFootsteps.cpp` records why this class is absent from the footstep species table: retail
// drives it from the schedule tasks `TASK_VSABBATLEADER_PLAY_FOOTSTEP_SOUND` /
// `..._STOP_FOOTSTEP_SOUND`, which are unbuilt. This is the SOUND that task will play.
void FElysiumNpcSabbatLeader::FootstepSound()
{
	NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, GSabbatLeaderSteps, UE_ARRAY_COUNT(GSabbatLeaderSteps),
		1.0f, NpcKernelSoundsShared::GSoundsChanBody);
}

// Slot 621: `0x103aa7a0`, a virtual `CNPC_VSabbatLeader` introduces: `RandomInt(0, 2)` (`PUSH 0x2` at
// `103aa889`) over the three exert wavs `0x1064c49c`, volume 1.0, `CHAN_BODY`.
void FElysiumNpcSabbatLeader::AttackSound()
{
	NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, GSabbatLeaderExertHeavy,
		UE_ARRAY_COUNT(GSabbatLeaderExertHeavy), 1.0f, NpcKernelSoundsShared::GSoundsChanBody);
}

// Slot 127: `0x103a6e80`, whose body is the `CNPC_VVampireBoss` restore (`0x103c5910`, family
// SaveRestore10's `VampireBossRestore`) — the census's mechanism row for this class.
int32 FElysiumNpcSabbatLeader::Restore(void* Archive)
{
	return VampireBossRestore(Archive);
}

// Slot 366: `0x103a76d0`, a scope-trace wrapper over a direct call into `CNPC_VAndreiBlood`'s
// `0x10385a70` (`return 0;`).
/** `CNPC_VSabbatLeader::HandleInteraction` (`0x103a76d0`, 111 bytes) — slot 366's `CNPC_VSabbatLeader`
 *  override. */
bool FElysiumNpcSabbatLeader::HandleInteraction(int32 Interaction, void* Data, FElysiumEntity* Other)
{
	// 0x103a76d0 — `CNPC_VSabbatLeader::HandleInteraction`, 111 bytes, slot 366. The whole body past
	// its scope-trace prologue and epilogue is one tail call:
	//
	//     CNPC_VAndreiBlood::HandleInteraction(this, interaction, data, other);   // 0x10385a70
	//
	// and `0x10385a70` is `return 0;` — nothing else. The override exists to put
	// `"CNPC_VSabbatLeader::HandleInteraction"` on the scope-trace stack, which is a debug artifact
	// this runtime has no counterpart for, so the observable answer is `false`.
	//
	// Slot 366's base body (`0x10326d40`, the SDK `CBaseCombatCharacter::HandleInteraction`) is
	// story 29c's generated stub; the human line's `0x10385a70` is `FElysiumNpcHuman`'s override,
	// called here directly as retail's tail call does.
	return FElysiumNpcHuman::HandleInteraction(Interaction, Data, Other);   // `0x10385a70`, direct
}

// --- From `FElysiumNpcWerewolf` (the move manifest's corrected owner) ---

void FElysiumNpcSabbatLeader::CheckStuck()
{
	// `CNPC_VSabbatLeader::CheckStuck` `0x103ab580`, read from the listing because the decompiler
	// aliased the two entities' collision extents onto one pair of registers.
	//
	//     CBaseEntity* p = m_hClosestPlayer;  if (!p) return;
	//     Vector pMins = p->m_Collision.OBBMins(), pMaxs = p->m_Collision.OBBMaxs();
	//     float pRadius = Length2D(pMaxs - pMins) * 0.5;                 // _DAT_104454d0
	//     float pTop    = p->GetAbsOrigin().z + pMaxs.z;
	//     float pBottom = p->GetAbsOrigin().z + pMins.z;
	//     Vector pCentre = p->GetAbsOrigin() + (pMins + pMaxs) * 0.5;
	//     Vector sMins = m_Collision.OBBMins(), sMaxs = m_Collision.OBBMaxs();
	//     float sRadius = Length2D(sMaxs - sMins) * 0.5;
	//     float sTop    = GetAbsOrigin().z + pMaxs.z;      // <- the PLAYER's maxs (see below)
	//     float sBottom = GetAbsOrigin().z + pMins.z;      // <- the PLAYER's mins
	//     Vector sCentre = GetAbsOrigin() + (sMins + sMaxs) * 0.5;
	//     if (!(sBottom <= pTop && pBottom <= sTop)) return;             // the Z-span overlap
	//     Vector d = sCentre - pCentre;  d.z = 0;
	//     float len = Length(d);
	//     float sum = sRadius + pRadius;
	//     if (!(len < sum)) return;
	//     if (len < 1e-6) d.x = 1.0;                                     // _DAT_104c3d14
	//     float scale = sum + 1.0;                                       // _DAT_104454c0
	//     Vector push = d * scale;
	//     Vector out = pCentre + push;
	//     out.z = pCentre.z + push.z + 5.0;                              // _DAT_10454110
	//     if (out.z < sBottom) out.z = sBottom;
	//     SetAbsOrigin(out);                                             // vtable +0x360
	//
	// **RETAIL BUG, reproduced.** `sTop` and `sBottom` are built from SELF's origin and the PLAYER's
	// box extents — the listing loads `[EBP+8]` and `[EBX+8]`, which still hold the player's maxs and
	// mins, after self's own pair has been read into `EDI` / `[ESP+0x14]`. A shipped program was
	// tuned against that, so it is kept; the two Z spans agree only when the two bodies use the same
	// hull. Note also that `push` is NOT normalised: its magnitude is `len * (sum + 1)`.
	//
	// **SEAM**: `RetailCollisionExtents` answers nothing, so the body refuses before it boxes
	// anything. The whole computation is written out so it stands the day an extent source exists.
	FElysiumPlayer* Player = Senses.Memory.ClosestPlayer.IsSet() && World != nullptr
		? World->FindPlayer()
		: nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return;
	}
	FVector PlayerMins = FVector::ZeroVector;
	FVector PlayerMaxs = FVector::ZeroVector;
	if (!RetailCollisionExtents(*Player, PlayerMins, PlayerMaxs))
	{
		return;
	}
	FVector SelfMins = FVector::ZeroVector;
	FVector SelfMaxs = FVector::ZeroVector;
	if (!RetailCollisionExtents(*this, SelfMins, SelfMaxs))
	{
		return;
	}
	const FVector PlayerUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Player->Origin);
	const FVector SelfUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Origin);

	const float PlayerRadius = NpcKernelMotor2Shared::Length2D(PlayerMaxs - PlayerMins) * GStuckHalf;
	const float PlayerTop = static_cast<float>(PlayerUnits.Z + PlayerMaxs.Z);
	const float PlayerBottom = static_cast<float>(PlayerUnits.Z + PlayerMins.Z);
	const FVector PlayerCentre = PlayerUnits + (PlayerMins + PlayerMaxs) * GStuckHalf;

	const float SelfRadius = NpcKernelMotor2Shared::Length2D(SelfMaxs - SelfMins) * GStuckHalf;
	// The two lines the bug lives on — the PLAYER's extents against SELF's origin.
	const float SelfTop = static_cast<float>(SelfUnits.Z + PlayerMaxs.Z);
	const float SelfBottom = static_cast<float>(SelfUnits.Z + PlayerMins.Z);
	const FVector SelfCentre = SelfUnits + (SelfMins + SelfMaxs) * GStuckHalf;

	if (!(SelfBottom <= PlayerTop && PlayerBottom <= SelfTop))
	{
		return;
	}
	FVector Delta = SelfCentre - PlayerCentre;
	Delta.Z = 0.0;
	const float Separation = NpcKernelMotor2Shared::Length3D(Delta);
	const float SumRadius = SelfRadius + PlayerRadius;
	if (!(Separation < SumRadius))
	{
		return;
	}
	if (Separation < GStuckDegenerate)
	{
		Delta.X = 1.0;
	}
	const float Scale = SumRadius + GStuckOne;
	const FVector Push = Delta * Scale;
	FVector Out = PlayerCentre + Push;
	Out.Z = PlayerCentre.Z + Push.Z + GStuckLift;
	if (static_cast<float>(Out.Z) < SelfBottom)
	{
		Out.Z = SelfBottom;
	}
	Origin = NpcKernelMotor2Shared::PortOf(Out);
}

// --- Moved from `ElysiumNpcDamage2.cpp` (story 5 step 4) ---

void FElysiumNpcSabbatLeader::SpawnBloodPoolEmitter(const FString& Name, const FElysiumEntity* OrientTo)
{
	// `GetAbsOrigin()` (slot 217) is the X and Y. The Z comes from the optional second parameter's
	// own origin when one is given, and from `thunk_FUN_101d08e0(origin, z)` — retail's floor drop —
	// when it is not. The port has no floor-drop service in the kernel, so the NPC's own Z stands in
	// and the substitution is named here rather than hidden.
	FVector Position = Origin / ElysiumMove::U;
	if (OrientTo != nullptr)
	{
		Position.Z = OrientTo->Origin.Z / ElysiumMove::U;
	}
	const int32 Index = CreateNamedEmitter(Name, Position, /*AttachMode*/ 0,
		FElysiumEntityHandle(), nullptr);
	// `thunk_FUN_100fbc90` failing leaves retail dereferencing a null pointer at `+0x3c4`; the port
	// refuses instead, which is the one stated divergence in this body.
	StartNamedEmitter(Index);
}

// --- Moved from `ElysiumNpcDialogueBodies.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcFacing.cpp` (story 5 step 4) ---

bool FElysiumNpcSabbatLeader::PlayerIsFacingMe() const
{
	// `CNPC_VSabbatLeader::PlayerIsFacingMe` `0x103aaf50`, 349 bytes, read from the listing because
	// the decompiler dropped the in-place normalize:
	//     if (!m_hClosestPlayer resolves) return false;
	//     AngleVectors( player->GetAbsAngles(), &fwd );      // full pitch+yaw forward
	//     Vector d = GetAbsOrigin() - player->GetAbsOrigin();
	//     float len = VectorNormalize( d );
	//     if (len > 0.0001f && DotProduct( fwd, d ) < 0.34202f) return false;
	//     return true;
	// `_DAT_104c3ce4 = 9.999999747378752e-05f` and `_DAT_104c3cf0 = 0.3420200049877167f`, which is
	// cos(70 degrees): a 140-degree cone, and a player standing ON the leader (inside the length
	// epsilon) counts as facing him.
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return false;
	}
	const FVector PlayerForward = RetailForward(Player->Angles);
	FVector Delta = Origin - Player->Origin;
	const float Length = static_cast<float>(Delta.Size());
	Delta = Delta.GetSafeNormal();
	if (Length > 0.0001f && static_cast<float>(FVector::DotProduct(PlayerForward, Delta)) < 0.34202f)
	{
		return false;
	}
	return true;
}

// --- Moved from `ElysiumNpcHints.cpp` (story 5 step 4) ---

float FElysiumNpcSabbatLeader::DistToHintCenterLine2D_3(const FVector& LineStart, const FVector& LineDir,
	const FVector& Point)
{
	// `CNPC_VVampireBoss::DistToHintCenterLine2D_3` (`0x103c6680`), transcribed from the listing
	// (`103c6716`–`103c6781`) because the decompiler drops the final `sqrt` call's result.
	//
	//   t  = (P.x - S.x) * D.x + (P.y - S.y) * D.y + D.z * K
	//   dx = (t * D.x + S.x) - P.x
	//   dy = (t * D.y + S.y) - P.y
	//   r  = sqrt(dx*dx + dy*dy + (t * D.z) * (t * D.z))
	//
	// `K` is `_DAT_104454c4`, the image's shared 0.0f, so the `D.z * K` term contributes nothing —
	// and `DistToHintCenterLine2D` zeroes `D.z` before calling in, which kills the third term too.
	// That is why this is a 2D distance. Both terms are kept because retail's arithmetic is the
	// deliverable, not the algebra it simplifies to.
	const float Sx = static_cast<float>(LineStart.X);
	const float Sy = static_cast<float>(LineStart.Y);
	const float Dx = static_cast<float>(LineDir.X);
	const float Dy = static_cast<float>(LineDir.Y);
	const float Dz = static_cast<float>(LineDir.Z);
	const float Px = static_cast<float>(Point.X);
	const float Py = static_cast<float>(Point.Y);

	const float T = (Px - Sx) * Dx + (Py - Sy) * Dy + Dz * NpcKernelHintsShared::GHintsZero;
	const float OffX = (T * Dx + Sx) - Px;
	const float OffY = (T * Dy + Sy) - Py;
	return FMath::Sqrt(OffX * OffX + OffY * OffY + (T * Dz) * (T * Dz));
}

float FElysiumNpcSabbatLeader::DistToHintCenterLine2D(const FHintWords& Hint, const FVector& PointCm)
{
	// `CNPC_VVampireBoss::DistToHintCenterLine2D_2` (`0x103c6570`): take the hint's origin with Z
	// forced to 0 as the line start, `AngleVectors(hint->GetAbsAngles())`'s forward with Z forced to
	// 0 and re-normalised as the direction, then forward to `_3`.
	FVector Start = Hint.OriginCm;
	Start.Z = 0.0;

	// `AngleVectors(hint->GetAbsAngles(), &forward)` — pitch AND yaw, Source's own formula.
	FVector Forward = HintsRetailForward(Hint.Angles);
	Forward.Z = 0.0;
	Forward.Normalize();

	return DistToHintCenterLine2D_3(Start, Forward, PointCm);
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMaintain19.cpp` (story 5 step 4) ---

bool FElysiumNpcSabbatLeader::SabbatLeaderTaskFail(int32 Reason)
{
	// `CNPC_VSabbatLeader::TaskFail` `0x103a9400`, the body of `FElysiumNpcSabbatLeader::TaskFail`.
	if (Reason < GMaintainSabbatFailureFirst || Reason > GMaintainSabbatFailureLast) // 0x103a9455
	{
		return false;
	}
	++SabbatLeaderRouteFailCount;															// 0x103a9463
	if (static_cast<float>(SabbatLeaderRouteFailCount) < GMaintainSabbatRouteFailThreshold) // 0x103a9474
	{
		return false;
	}
	FlipFailureType(); // 0x103a9489
	// Retail stamps `+0x1b30/+0x1b34` here; both are ABSENT shape words. The chosen schedule and
	// its instruction address are the port's observable provenance.
	if (FailureType != 0)
	{
		SetSchedule(0x161, false); // 0x103a94af
	}
	else
	{
		SetSchedule(0x160, false); // 0x103a94ce
	}
	return true; // 0x103a94bc / 0x103a94db
}

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 4) ---

bool FElysiumNpcSabbatLeader::SolveJumpArc(const FVector& FromUnits, const FVector& ToUnits,
	FVector& OutVelocityUnits) const
{
	// `thunk_FUN_102c4cc0(this, &outVelocity, from, to)`. **SEAM**: no arc solver here.
	(void)FromUnits;
	(void)ToUnits;
	(void)OutVelocityUnits;
	++MotorSeams.JumpArcSolves;
	return false;
}

bool FElysiumNpcSabbatLeader::DistToHintCenterLine2DSqr(int32 HintNode, const FVector& PositionUnits,
	float& OutSqr) const
{
	// `CNPC_VVampireBoss::DistToHintCenterLine2D_2(hint, pos)`. **SEAM**: no hint geometry.
	(void)HintNode;
	(void)PositionUnits;
	(void)OutSqr;
	return false;
}

// --- Moved from `ElysiumNpcKernelMotor2.cpp` (story 5 step 4) ---

void FElysiumNpcSabbatLeader::SetJumpVelocityTowardPlayer()
{
	// `CNPC_VSabbatLeader::SetJumpVelocityTowardPlayer` `0x103aad40`:
	//
	//     if (!m_hClosestPlayer resolves) return;
	//     Vector v = GetAbsVelocity();                              // vtable +0x318
	//     Vector lead;
	//     lead.x = 25.0 * (player.x - self.x);                      // DAT_104c3cb8
	//     lead.y = (player.y - self.y) * 25.0;
	//     lead.z = 25.0 * 0.0;                                      // the Z term is scaled from ZERO
	//     Vector to = player->GetAbsOrigin() - lead;
	//     thunk_FUN_102c4cc0(this, &v, GetAbsOrigin(), &to);        // the jump-arc solver
	//     SetAbsVelocity(v);
	//
	// Two facts the summary loses and that are kept here: the lead is built from the POSITION delta
	// between the two bodies (not from the player's velocity), and its Z term is a constant times
	// zero, so the lead is flat. The solver is a seam, so the velocity write is what it was.
	FElysiumPlayer* Player = Senses.Memory.ClosestPlayer.IsSet() && World != nullptr
		? World->FindPlayer()
		: nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return;
	}
	const FVector SelfUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Origin);
	const FVector PlayerUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Player->Origin);
	const FVector Lead(GSabbatLeadScale * (PlayerUnits.X - SelfUnits.X),
		(PlayerUnits.Y - SelfUnits.Y) * GSabbatLeadScale, GSabbatLeadScale * 0.0);
	const FVector ToUnits = PlayerUnits - Lead;
	FVector VelocityUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Velocity);
	if (!SolveJumpArc(SelfUnits, ToUnits, VelocityUnits))
	{
		// **SEAM**: with no solver retail's out-parameter is untouched, and it then assigns that
		// untouched value — the body's CURRENT velocity — straight back. Reproduced exactly: the
		// assignment happens and changes nothing.
		return;
	}
	Velocity = NpcKernelMotor2Shared::PortOf(VelocityUnits);
}

bool FElysiumNpcSabbatLeader::PlayerInNoJumpZone() const
{
	// `CNPC_VSabbatLeader::PlayerInNoJumpZone` `0x103a9e70`:
	//     if (!m_hClosestPlayer resolves) return false;
	//     for (hint = g_pHintList; hint; hint = hint->+0x5d8) {
	//         if (hint->m_nHintType != 0x3e84) continue;
	//         if (DistToHintCenterLine2D_2(hint, player->GetAbsOrigin()) < 100.0      // DAT_104c3ccc
	//          && DistToHintCenterLine2D_2(hint, GetAbsOrigin())         < 100.0)
	//             return true;
	//     }
	//     return false;
	// BOTH bodies have to be inside the SAME hint's line, which is why the second test is nested.
	// **SEAM**: the hint list answers empty, so nobody is ever in a no-jump zone.
	FElysiumPlayer* Player = Senses.Memory.ClosestPlayer.IsSet() && World != nullptr
		? World->FindPlayer()
		: nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return false;
	}
	TArray<int32> Hints;
	NavAllHintNodes(Hints);
	const FVector PlayerUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Player->Origin);
	const FVector SelfUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Origin);
	for (int32 Hint : Hints)
	{
		int32 Type = 0;
		if (!NavHintNodeType(Hint, Type) || Type != GNoJumpHintType)
		{
			continue;
		}
		float PlayerDistance = 0.f;
		if (!DistToHintCenterLine2DSqr(Hint, PlayerUnits, PlayerDistance)
			|| !(PlayerDistance < GNoJumpZoneDistance))
		{
			continue;
		}
		float SelfDistance = 0.f;
		if (DistToHintCenterLine2DSqr(Hint, SelfUnits, SelfDistance)
			&& SelfDistance < GNoJumpZoneDistance)
		{
			return true;
		}
	}
	return false;
}

// --- Moved from `ElysiumNpcPositions.cpp` (story 5 step 4) ---

int32 FElysiumNpcSabbatLeader::SelectTeleportArchwayRule(TArrayView<const FHintWords> Nodes,
	const FVector& PlayerCm, float PlayerYaw)
{
	// Type `0x3e82`; the FLAT distance from the node to the player must reach `_DAT_104c3cbc = 24.0`
	// units; the score is `|AngleDiff(playerYaw, VectorAngles(player - node).y)|` alone, so the
	// archway most nearly in front of the player wins regardless of how far away it is.
	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != HintArchway)
		{
			continue;
		}
		const float Flat = NpcKernelPositionsShared::FlatDistance(PlayerCm, Nodes[Index].OriginCm);
		if (Flat < ArchwayMinFlatDist * NpcKernelPositionsShared::U)
		{
			continue;
		}
		const float Score =
			FMath::Abs(NpcKernelPositionsShared::RetailAngleDiff(PlayerYaw, NpcKernelPositionsShared::FlatYaw(PlayerCm, Nodes[Index].OriginCm)));
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpcSabbatLeader::SelectTeleportArchway() const
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	// `FElysiumPlayer::Angles` is Source `[pitch yaw roll]`, exactly what `GetAbsAngles().y` is.
	const int32 Pick = SelectTeleportArchwayRule(Nodes, Player->Origin,
		static_cast<float>(Player->Angles.Y));
	return Pick == INDEX_NONE ? INDEX_NONE : NodeIds[Pick];
}

int32 FElysiumNpcSabbatLeader::SelectDiveOutPointRule(TArrayView<const FHintWords> Nodes,
	const FVector& PlayerCm, float PlayerYaw)
{
	// Type `0x3e85`; the flat distance must reach `_DAT_104c3cfc = 40.0` units; the score ADDS the
	// flat distance to the yaw delta, so a degree of misalignment and a unit of distance cost the
	// same — retail's own weighting, and the one line that separates this from the archway pick.
	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != HintDive)
		{
			continue;
		}
		const float Flat = NpcKernelPositionsShared::FlatDistance(PlayerCm, Nodes[Index].OriginCm);
		if (Flat < DiveMinFlatDist * NpcKernelPositionsShared::U)
		{
			continue;
		}
		const float Score =
			FMath::Abs(NpcKernelPositionsShared::RetailAngleDiff(PlayerYaw, NpcKernelPositionsShared::FlatYaw(PlayerCm, Nodes[Index].OriginCm))) + Flat;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpcSabbatLeader::SelectDiveOutPoint() const
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectDiveOutPointRule(Nodes, Player->Origin,
		static_cast<float>(Player->Angles.Y));
	return Pick == INDEX_NONE ? INDEX_NONE : NodeIds[Pick];
}

int32 FElysiumNpcSabbatLeader::SelectDiveInPointRule(TArrayView<const FHintWords> Nodes, const FVector& PlayerCm,
	float PlayerYaw, const FVector& SelfCm)
{
	// `SelectDiveOutPoint` plus two gates.
	//
	// The first is on the LEADER: the flat delta `player - self` is normalized in place
	// (`FUN_10137220`) and the length it returns must reach `_DAT_104c3d00 = 45.0` units, or the
	// body returns null without walking the list at all. The normalize matters because the SAME
	// three floats become the unit direction the second gate dots against.
	//
	// The second is per node: the delta `node - self` is normalized, its length must exceed
	// `_DAT_104c3ce4 = 1e-4`, and the dot of the two unit vectors must be BELOW `_DAT_104454c4 = 0`
	// — so the dive point has to lie in the hemisphere AWAY from the player. The leader dives out of
	// the player's reach, not toward him.
	FVector ToPlayerFlat(PlayerCm.X - SelfCm.X, PlayerCm.Y - SelfCm.Y, 0.0);
	const float ToPlayerLength = RetailVectorNormalize(ToPlayerFlat);
	if (ToPlayerLength < DiveMinSelfDist * NpcKernelPositionsShared::U)
	{
		return INDEX_NONE;
	}

	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != HintDive)
		{
			continue;
		}
		const float Flat = NpcKernelPositionsShared::FlatDistance(PlayerCm, Nodes[Index].OriginCm);
		if (Flat < DiveMinFlatDist * NpcKernelPositionsShared::U)
		{
			continue;
		}
		FVector ToNode = Nodes[Index].OriginCm - SelfCm;
		const float ToNodeLength = RetailVectorNormalize(ToNode);
		if (ToNodeLength <= DiveLengthEpsilon * NpcKernelPositionsShared::U)
		{
			continue;
		}
		if (static_cast<float>(FVector::DotProduct(ToPlayerFlat, ToNode)) >= NpcKernelPositionsShared::RetailZero)
		{
			continue;
		}
		const float Score =
			FMath::Abs(NpcKernelPositionsShared::RetailAngleDiff(PlayerYaw, NpcKernelPositionsShared::FlatYaw(PlayerCm, Nodes[Index].OriginCm))) + Flat;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpcSabbatLeader::SelectDiveInPoint() const
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectDiveInPointRule(Nodes, Player->Origin,
		static_cast<float>(Player->Angles.Y), Origin);
	return Pick == INDEX_NONE ? INDEX_NONE : NodeIds[Pick];
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 4) ---

// 0x103a9d00 `CNPC_VSabbatLeader::FlipFailureType`
void FElysiumNpcSabbatLeader::FlipFailureType()
{
	// The whole body inside the scope-trace push/pop. The trace stack is retail's debug aid and has
	// no port counterpart; the mind's transition trace carries the same account.
	FailureType = 1 - FailureType;
}

// --- Moved from `ElysiumNpcSpeciesMisc10_2.cpp` (story 5 step 4) ---

bool FElysiumNpcSabbatLeader::CheckForJumpCondition()
{
	// `103a9dda`: `0x103c67f0(this, DAT_104c3cc8)` — **8.0** s since the last attack.
	if (AttackIdleLongerThan(GSabbatJumpIdleSeconds))
	{
		return true;
	}
	// `103a9df6`: the health delta at or above `_DAT_104c3cc4` (**0.0666667**). The percent rises
	// with damage, so this is "lost a fifteenth of the bar since the mark".
	if (HealthPercentLostSinceRecord() >= GSabbatJumpHealthLoss)
	{
		return true;
	}
	// `103a9e15`: otherwise the answer IS `PlayerDamagedEnoughThisRound`'s.
	return PlayerDamagedEnoughThisRound();
}

void FElysiumNpcSabbatLeader::SabbatLeaderUpdateBloodSplash()
{
	// `103aa9ad`: `m_bDiving` set does NOTHING AT ALL — not even the level copy, which is what makes
	// leaving a dive re-fire the big splash.
	if (bSabbatDiving)
	{
		return;
	}
	const int32 CurrentWaterLevel = WaterLevel;
	// `103aa9b9`: the current level above 0 AND (the previous level was 0 OR the interval has
	// lapsed). The interval test is `m_fLastSplashTime + 0.25 < curtime`, strictly.
	const bool bIntervalPast =
		SabbatLastSplashTime + GSabbatSplashIntervalSeconds < NpcKernelSpeciesMisc10_2Shared::SpeciesMisc10_2Now(*this);
	if (CurrentWaterLevel > 0 && (SabbatLastWaterLevel == 0 || bIntervalPast))
	{
		// `103aa9e5`: the ordinary splash always...
		SpawnBloodPoolEmitter(TEXT("bloodsplash_emitter"), nullptr);
		// `103aaa01`: ...and the big one ONLY on the dry-to-wet edge.
		if (SabbatLastWaterLevel == 0)
		{
			SpawnBloodPoolEmitter(TEXT("bloodbigsplash_emitter"), nullptr);
		}
		// `103aaa1a`: the stamp, after both spawns.
		SabbatLastSplashTime = NpcKernelSpeciesMisc10_2Shared::SpeciesMisc10_2Now(*this);
	}
	// `103aaa27`: the previous level is copied on EVERY non-diving pass, inside the diving guard and
	// outside the splash one.
	SabbatLastWaterLevel = CurrentWaterLevel;
}

void FElysiumNpcSabbatLeader::RecordPlayerHealth()
{
	// `103aaad6`: nothing at all without a LIVE `m_hClosestPlayer` — the mark keeps its previous
	// value, which is retail's own behaviour and not a reset.
	const FElysiumEntity* Player = World != nullptr
		? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	if (Player == nullptr)
	{
		return;
	}
	// `103aab5f`: stat `0x0f` off the player's type-0 list. That is the accumulated WOUND counter —
	// `CBaseCombatCharacter::HealthToPercent` (`0x1032fe60`) computes
	// `((stat0x11 - stat0x0f) * m_iMaxHealth) / stat0x11` — so this snapshots the player's DAMAGE
	// TOTAL at the start of a round, not its health.
	SabbatLastPlayerHealth = TypedStatValueOf(Player, NpcKernelSpeciesMisc10_2Shared::GStatWounds);
}

bool FElysiumNpcSabbatLeader::PlayerDamagedEnoughThisRound() const
{
	// `103aabf9`: no live closest player answers false.
	const FElysiumEntity* Player = World != nullptr
		? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	if (Player == nullptr)
	{
		return false;
	}
	// `103aaca0`: the same stat re-derived, then `_DAT_104c3ce0 <= (float)(stat0x0f - mark)` — the
	// wound counter having risen by at least **2.0** since `RecordPlayerHealth`, which is what makes
	// the retail name literal.
	const float Risen = static_cast<float>(TypedStatValueOf(Player, NpcKernelSpeciesMisc10_2Shared::GStatWounds)
		- SabbatLastPlayerHealth);
	return GSabbatRoundDamageThreshold <= Risen;
}

// --- Moved from `ElysiumNpcState19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 4) ---

