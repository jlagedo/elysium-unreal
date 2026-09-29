// Story 0019/8 (29e under the strict verdict), family **RunAi19** -- the species classes' bodies.
//
// Each body is the species' slot-432 `RunAI(bool)` override, declared on its class. Where retail chains
// through a thirteen-byte forwarder (`CNPC_VChangBros` `0x10385a10`, `CNPC_VAnimal` `0x10360160`), the
// forwarder is a bare `JMP` to `CAI_BaseNPCTroika::RunAI 0x1028fcc0`, and the port's parent class
// inherits `FElysiumNpc::RunAI`, so `Parent::RunAI` is that forward. Walked prose:
// `docs/vtmb/npc-ai/schedule-kernel.md` § "Story 8, family RunAi19".
//
// Owns (RunAi19's `rule` rows): 0x1039e3d0 CNPC_VMingXiaoTentacle::RunAI, 0x103bdef0
// CNPC_VTzimisce::vfunc432, 0x103c1d20 CNPC_VTzimisceHeadClaw::vfunc432, 0x1035e980
// CNPC_VAndreiBlood::vfunc432, 0x10361110 CNPC_VAsianVampire::RunAI, 0x10363b60
// CNPC_VBach::vfunc432, 0x103747e0 CNPC_VDog::vfunc432, 0x10378b80 CNPC_VGargoyle::vfunc432,
// 0x10380120 CNPC_VHengeyokai::vfunc432, 0x1038e990 CNPC_VManBat::vfunc432, 0x103a3670
// CNPC_VPedestrian::vfunc432, 0x103a75c0 CNPC_VSabbatLeader::RunAI, 0x103aebd0
// CNPC_VSheriffMan::RunAI, 0x103df850 CNPC_VZombie::vfunc432.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcZombie.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSoundsShared.h"
#include "Substrate/ElysiumProp.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumStealth.h"

namespace
{
	// Retail NPC states (`m_NPCState` / `m_IdealNPCState`).
	constexpr int32 RunAi19StateIdle = 1;
	constexpr int32 RunAi19StateCombat = 2;
	/** `D_NU`, the neutral disposition slot 404 answers and `AddClassRelationship` is given. */
	constexpr int32 RunAi19DispositionNeutral = 4;
	/** `PUSH 0xa` at `0x1035e9df`: the relationship's priority. */
	constexpr int32 RunAi19AndreiPlayerPriority = 10;
	/** `CVStatList_t` list tag 3 and stat `0xd` (`0x10363bb1` / `0x10363bf0`): the shield armour. */
	constexpr int32 RunAi19BachShieldListType = 3;
	constexpr int32 RunAi19BachShieldStat = 0xd;
	// `docs/vtmb/activity_enum.md`.
	constexpr int32 RunAi19ActIdle = 1;      // ACT_IDLE
	constexpr int32 RunAi19ActFidget = 3;    // ACT_FIDGET
	constexpr int32 RunAi19ActSit = 0x40;    // ACT_SIT
	constexpr int32 RunAi19ActSnarl = 0x6e;  // ACT_SNARL
	constexpr int32 RunAi19ActZombieBusy = 0x4b;   // `CMP [+0xfec],0x4b` at `0x103df8c4`
	/** `CMP EAX,0x80` at `0x10374806`: the one roll out of 256 that fidgets. */
	constexpr int32 RunAi19DogFidgetRoll = 0x80;
	/** `_DAT_1044e664` = 10.0 and `_DAT_1044ddb0` = 256.0: the flee sound's period and set-back. */
	constexpr double RunAi19FleeSoundPeriodSeconds = ElysiumNpcTunables::Ten;
	constexpr float RunAi19FleeSoundBackUnits = ElysiumNpcTunables::Melee1OuterBand;
	/** `PUSH 0x40000000` / `PUSH 0x3f800000` at `0x1039e4b2`: `RandomFloat(1.0, 2.0)`. */
	constexpr float RunAi19EvadeRearmMin = 1.0f;
	constexpr float RunAi19EvadeRearmMax = 2.0f;
	/** `_DAT_1044ddb0` = 256.0: the evade re-plan's enemy distance ceiling (`0x1039e518`). */
	constexpr float RunAi19EvadeEnemyDistUnits = ElysiumNpcTunables::Melee1OuterBand;
	/** `PUSH 0x44000000` / `PUSH 0x46ea6000` at `0x1039e541` / `0x1039e53c`: 512.0 and 30000.0. */
	constexpr float RunAi19EvadeNodeFleeUnits = 512.0f;
	constexpr float RunAi19EvadeNodeSearchUnits = 30000.0f;
	/** `GOALTYPE_LOCATION`, the goal type `0x102ee620` must answer (`CMP EAX,0x4` at `0x1039e4ed`). */
	constexpr int32 RunAi19GoalTypeLocation = 4;
	/** `PUSH 0x202400b` (`MASK_NPCSOLID`) at `0x1039e5c4`. */
	constexpr int32 RunAi19TentacleClearMask = 0x202400b;
	/** The evade re-plan's three TaskFail sites: line and reason (`0x1039e504` / `0x1039e50e`, ...). */
	constexpr int32 RunAi19TentacleNoEnemyLine = 0x4ba;
	constexpr int32 RunAi19TentacleNoEnemyReason = 6;
	constexpr int32 RunAi19TentacleNoNodeLine = 0x4c6;
	constexpr int32 RunAi19TentacleNoNodeReason = 7;
	constexpr int32 RunAi19TentacleNoRouteLine = 0x4d0;
	constexpr int32 RunAi19TentacleNoRouteReason = 0xc;
	/** `+0x1b44`'s literal at `0x1039e58c`. */
	const TCHAR* const RunAi19TentacleFile = TEXT("E:\\Vampire\\main\\dlls\\hl2_dll\\NPC_VMingXiaoTentacle.cpp");
	/** `0x106477c8` and `0x106477cc`, the two one-entry wav tables of `0x1039f030` / `0x1039f1a0`. */
	const TCHAR* const RunAi19TentacleHitGroundWav[] = {
		TEXT("character/monster/ming xiao/tentacle_hit_ground.wav"),
	};
	const TCHAR* const RunAi19TentacleFloppingWav[] = {
		TEXT("character/monster/ming xiao/tentacle_flopping_loop.wav"),
	};
	/** `PUSH 0x16a` at `0x103df98c`: `CNPC_VZombie`'s class-local `SCHED_VZOMBIE_FEED_LUNGE`
	 *  (registrar `0x103de500`), not in `ElysiumScheduleNumbers.h`. */
	constexpr int32 RunAi19ZombieFeedLungeSchedule = 0x16a;
	/** `PUSH 0x63` at `0x103df970`: `RandomInt(0, 99)`. */
	constexpr int32 RunAi19ZombieRollMax = 0x63;

	/** `CVFeatList_t` (`0x10739d08`, loaded by `0x101e6310` from `Rules.txt`) fields the zombie's
	 *  grapple roll reads, with the loader's defaults: `+0x250` `LungeDistanceMin` 98.0
	 *  (`0x101e8a90`), `+0x254` `LungeDistanceMax` 160.0 (`0x101e8ab0`), `+0x264` `Percent` 2
	 *  (`0x101e8b30`), `+0x280` `DelayBetween` 20.0 (`0x101e8c10`). */
	UElysiumRulebookSubsystem* RunAi19Rules(const FElysiumNpc& Npc)
	{
		UElysiumSessionSubsystem* GameState = Npc.World != nullptr ? Npc.World->GetGameState() : nullptr;
		return GameState != nullptr ? GameState->Rulebook() : nullptr;
	}
	float RunAi19ZombieFloat(const FElysiumNpc& Npc, const TCHAR* Key, float ImageDefault)
	{
		UElysiumRulebookSubsystem* Rules = RunAi19Rules(Npc);
		return Rules != nullptr ? Rules->Rules().Flt(TEXT("Zombie_Grapple_Info"), Key, ImageDefault)
								: ImageDefault;
	}
	int32 RunAi19ZombieInt(const FElysiumNpc& Npc, const TCHAR* Key, int32 ImageDefault)
	{
		UElysiumRulebookSubsystem* Rules = RunAi19Rules(Npc);
		return Rules != nullptr ? Rules->Rules().Int(TEXT("Zombie_Grapple_Info"), Key, ImageDefault)
								: ImageDefault;
	}
}

// =================================================================================================
// CNPC_VAndreiBlood -- 0x1035e980, 171 bytes
// =================================================================================================

void FElysiumNpcAndreiBlood::RunAI(bool bReduced)
{
	// Both arms write the state words DIRECTLY (`+0x5cc0` / `+0x5cc4`), not through `SetState
	// 0x1026e340`: no `OnStateChange`, no enemy strip.
	if (!bAndreiActivated)                                                    // 0x1035e98b +0x66cc
	{
		// The pre-fight arm, every pass: forced IDLE.
		WriteNpcStateRetail(RunAi19StateIdle);                                // 0x1035e98d +0x5cc0 = 1
		WriteIdealStateRetail(RunAi19StateIdle);                              // 0x1035e997 +0x5cc4 = 1
		FElysiumEntity* const Closest = World != nullptr
			? World->Resolve(Senses.Memory.ClosestPlayer)                      // 0x1035e9aa / 0x1035e9c7 / 0x1035e9cd +0x628c
			: nullptr;
		if (Closest != nullptr
			&& IRelationType(Closest) != RunAi19DispositionNeutral)          // 0x1035e9d4 slot 404 / 0x1035e9dd
		{
			// `AddClassRelationship(CLASS_PLAYER 1, D_NU 4, 10)`.
			Relationships.AddClassRelationship(TEXT("player"),
				EElysiumRelationship::Neutral, RunAi19AndreiPlayerPriority);  // 0x1035e9e7 0x10332aa0
		}
		// `m_bfNPCStateFlags (+0x5b64) &= ~4` (`0x1035e9ec`..`0x1035e9fb`). The port derives the byte
		// from the state (`NpcStateFlags()`, the shape map's reading of `+0x5b64`), and IDLE's byte
		// (`0x31`) already has bit 2 clear, so the clear is carried by the IDLE write above; retail's
		// OTHER stale bits of the previous `SetState` byte are not (named divergence).
		FElysiumNpcVampireBoss::RunAI(bReduced);                              // 0x1035ea01 0x10385a10 -> 0x1028fcc0
		return;                                                               // 0x1035ea07
	}
	// Activated: state and ideal both COMBAT, no relationship work, no flag clear.
	WriteNpcStateRetail(RunAi19StateCombat);                                  // 0x1035ea16 +0x5cc0 = 2
	WriteIdealStateRetail(RunAi19StateCombat);                                // 0x1035ea1c +0x5cc4 = 2
	FElysiumNpcVampireBoss::RunAI(bReduced);                                  // 0x1035ea22 0x10385a10 -> 0x1028fcc0
}

// =================================================================================================
// CNPC_VAsianVampire -- 0x10361110, 111 bytes
// =================================================================================================

void FElysiumNpcAsianVampire::RunAI(bool bReduced)
{
	// The moved-timestamp refresh FIRST -- what this species' own `StationaryForTooLong`
	// (`0x10362670`, read by `SelectSchedule 0x10360eb0`) measures -- then the base pass. Scope trace
	// absent. `0x10362540` ends in a bare `RET`: the decompiler's second argument is an artefact.
	UpdateMovedTimeStamp();                                                   // 0x10361163 0x10362540
	FElysiumNpcVampireBoss::RunAI(bReduced);                                  // 0x1036116f 0x10385a10 -> 0x1028fcc0
}

// =================================================================================================
// CNPC_VBach -- 0x10363b60, 186 bytes
// =================================================================================================

void FElysiumNpcBach::RunAI(bool bReduced)
{
	// Two independent arms before the base, in order.
	if (!bCanFightYet)                                                        // 0x10363b6b +0x66a8
	{
		ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());   // 0x10363b6f 0x10279a50(NULL)
	}
	// The shield's expiry half (`GatherAttackConditions 0x10363db0` raises it): `FCOMP [+0x6684]`
	// against curtime, `TEST AH,0x41 / JNZ` -- only a shield time strictly before curtime expires.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (bBachShieldActive                                                     // 0x10363b7c +0x66a5
		&& BachShieldTime < Now)                                              // 0x10363b97 +0x6684
	{
		// `CVStatList_t::SetBase(stat 0xd, 0)` on the list whose `+0x10` tag is 3, found by the
		// `+0x13bc`/`+0x13c0` walk (`0x10363ba5`, `0x10363bb5` tag == 3, `0x10363bbd` next) and falling
		// back to the lazily built global `0x109f0b40` (`0x10363bc9` init bit, `0x10363bda` constructor,
		// `0x10363be4` `_atexit`): family Combat10's typed-stat seam is that walk.
		TypedStatSet(RunAi19BachShieldListType, RunAi19BachShieldStat, 0);   // 0x10363bf7 0x102008e0
		bBachShieldActive = false;                                            // 0x10363bfd
	}
	FElysiumNpcVampire::RunAI(bReduced);                                      // 0x10363c0c 0x10385a10 -> 0x1028fcc0
}

// =================================================================================================
// CNPC_VDog -- 0x103747e0, 164 bytes
// =================================================================================================

void FElysiumNpcDog::RunAI(bool bReduced)
{
	// The idle fidget cadence and the return to idle, before `CNPC_VAnimal::RunAI 0x10360160` (a bare
	// forward to `0x1028fcc0`).
	const int32 Activity = ActivityNumber;                                    // 0x103747e3 +0x0fec m_Activity
	if (Activity == RunAi19ActIdle                                            // 0x103747ec
		&& NpcStateRetail() == RunAi19StateIdle)                              // 0x103747f4 +0x5cc0
	{
		// `RandomInt(0, 0xff)` through the import `[0x109f3868]`; ONLY the exact value 0x80 fidgets,
		// a 1-in-256 per pass -- an equality, never a threshold.
		if (ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 0xff)
			== RunAi19DogFidgetRoll)                                          // 0x103747fd / 0x1037480b
		{
			SetActivity(RunAi19ActFidget);                                    // 0x10374813 slot 310
		}
		FElysiumNpcAnimal::RunAI(bReduced);                                   // 0x10374820 0x10360160
		return;                                                               // 0x10374826
	}
	if (Activity == RunAi19ActSnarl)                                          // 0x1037482c / 0x10374831
	{
		if (bSequenceFinished)                                                // 0x1037483b +0x065c m_bSequenceFinished
		{
			SetActivity(RunAi19ActIdle);                                      // 0x10374843 slot 310
			FElysiumNpcAnimal::RunAI(bReduced);                               // 0x10374850 0x10360160
			return;                                                           // 0x10374856
		}
	}
	else if (Activity == RunAi19ActFidget                                     // 0x1037482c -> 0x1037485e
		|| Activity == RunAi19ActSit)                                         // 0x1037485c
	{
		if (bSequenceFinished)                                                // 0x10374866 +0x065c
		{
			SetActivity(RunAi19ActIdle);                                      // 0x1037486e slot 310
		}
	}
	FElysiumNpcAnimal::RunAI(bReduced);                                       // 0x1037487b 0x10360160
}

// =================================================================================================
// CNPC_VGargoyle -- 0x10378b80, 80 bytes
// =================================================================================================

float& FElysiumNpcGargoyle::GargoyleObstructionLookaheadConVar()
{
	static float Value = 24.0f;   // "24" `0x1063986c`
	return Value;
}

float& FElysiumNpcGargoyle::GargoyleObstructionScalarConVar()
{
	static float Value = 5.0f;    // "5" `0x1056d814`
	return Value;
}

float& FElysiumNpcGargoyle::GargoyleObstructionZConVar()
{
	static float Value = 75.0f;   // "75" `0x106398e4`
	return Value;
}

void FElysiumNpcGargoyle::RunAi19GargoyleBreakProp(FElysiumEntity* Prop)
{
	// SEAM (declaration). A `prop_dynamic` is built as `FElysiumProp` (its leaf class), which is what
	// the classname match the caller made guarantees.
	++RunAi19GargoylePropBreaks;
	FElysiumInputArgs Args;
	Args.Activator = Handle;
	Args.Caller = Handle;
	static_cast<FElysiumProp*>(Prop)->InputBreak(Args);
}

void FElysiumNpcGargoyle::RunAi19GargoyleObstacle(FElysiumEntity* Blocker)
{
	// `0x10379b40`.
	if (Blocker == nullptr)                                                   // 0x10379b54
	{
		return;
	}
	// The inline `FClassnameIs(blocker, "prop_dynamic")` (`0x10379b5a`..`0x10379bcd`): the pointer
	// compare against the literal, else `__strcmpi` -- case-insensitive, no wildcard in the literal.
	const FString Classname = Blocker->Def != nullptr ? Blocker->Def->Classname : FString();
	if (Classname.Equals(TEXT("prop_dynamic"), ESearchCase::IgnoreCase))
	{
		RunAi19GargoyleBreakProp(Blocker);                                    // 0x10379bd4 slot 266 (Break(this))
		return;
	}
	RunAi19ObstructionPush(Blocker, GargoyleObstructionScalarConVar(),
		GargoyleObstructionZConVar());                                        // 0x10379be6..0x10379db5
}

void FElysiumNpcGargoyle::RunAi19GargoyleReact(FElysiumEntity* Blocker, int32 Kind)
{
	if (Blocker == nullptr)                                                   // 0x10378b90
	{
		return;
	}
	if (Kind == 1)                                                            // 0x10378b99 / 0x10378b9c
	{
		RunAi19ObstructionStepUp();                                           // 0x10378ba1 0x10379e80
		return;
	}
	RunAi19GargoyleObstacle(Blocker);                                         // 0x10378bba 0x10379b40
}

void FElysiumNpcGargoyle::RunAI(bool bReduced)
{
	// The decompiled `this == 1` is the stack OUT-word `0x103796a0` writes (`PUSH ECX` reserves it).
	int32 Kind = 0;
	FElysiumEntity* const Blocker = RunAi19ObstructionSweep(GargoyleObstructionLookaheadConVar(),
		/*bRetraceAtStepHeight=*/true, Kind);                                 // 0x10378b89 0x103796a0
	RunAi19GargoyleReact(Blocker, Kind);                                      // 0x10378b90..0x10378bba
	FElysiumNpcVampire::RunAI(bReduced);                                      // 0x10378bad / 0x10378bc6 0x10385a10
}

// =================================================================================================
// CNPC_VHengeyokai -- 0x10380120, 111 bytes
// =================================================================================================

float& FElysiumNpcHengeyokai::HengeyokaiObstructionLookaheadConVar()
{
	static float Value = 24.0f;
	return Value;
}

float& FElysiumNpcHengeyokai::HengeyokaiObstructionScalarConVar()
{
	static float Value = 5.0f;
	return Value;
}

float& FElysiumNpcHengeyokai::HengeyokaiObstructionZConVar()
{
	static float Value = 75.0f;
	return Value;
}

int32& FElysiumNpcHengeyokai::HengeyokaiStunConVar()
{
	static int32 Value = 0;       // "0" `0x105399a0`
	return Value;
}

void FElysiumNpcHengeyokai::RunAi19HengeyokaiReact(FElysiumEntity* Blocker, int32 Kind)
{
	if (Blocker == nullptr)                                                   // 0x10380130
	{
		return;
	}
	if (Kind == 1)                                                            // 0x10380139 / 0x1038013c
	{
		RunAi19ObstructionStepUp();                                           // 0x10380141 0x103816e0
		return;
	}
	RunAi19ObstructionPush(Blocker, HengeyokaiObstructionScalarConVar(),
		HengeyokaiObstructionZConVar());                                      // 0x1038014b 0x10381460
}

void FElysiumNpcHengeyokai::RunAI(bool bReduced)
{
	// The Gargoyle `0x10378b80` twin, then an independent third arm.
	int32 Kind = 0;
	FElysiumEntity* const Blocker = RunAi19ObstructionSweep(HengeyokaiObstructionLookaheadConVar(),
		/*bRetraceAtStepHeight=*/true, Kind);                                 // 0x10380129 0x10380fc0
	RunAi19HengeyokaiReact(Blocker, Kind);                                    // 0x10380130..0x1038014b
	FElysiumNpcVampire::RunAI(bReduced);                                      // 0x10380157 0x10385a10
	// `hengeyokai_stun`: not a command (slot 1, `0x10380164`) and `m_nValue (+0x2c) != 0` -> the morph, then the
	// ConVar back to 0 -- a one-shot console trigger.
	if (HengeyokaiStunConVar() != 0)                                          // 0x10380169 / 0x10380175
	{
		HengeyokaiEnterMorph();                                               // 0x10380179 0x103830e0
		HengeyokaiStunConVar() = 0;                                           // 0x10380185 ConVar::SetValue(0) 0x10246160
	}
}

// =================================================================================================
// CNPC_VManBat -- 0x1038e990, 26 bytes
// =================================================================================================

void FElysiumNpcManBat::RunAI(bool bReduced)
{
	// The slowed-victim release polled with force FALSE on every pass (`Event_Killed 0x1038e8c0` is
	// the force-true caller), then the base.
	ManBatReleaseSlowedEntity(false);                                         // 0x1038e995 0x1038f020(0)
	FElysiumNpcVampire::RunAI(bReduced);                                      // 0x1038e9a1 0x10385a10 -> 0x1028fcc0
}

// =================================================================================================
// CNPC_VMingXiaoTentacle -- 0x1039e3d0, 820 bytes
// =================================================================================================

void FElysiumNpcMingXiaoTentacle::TentacleHitGroundSound()
{
	// `0x1039f030`: `CPASAttenuationFilter` at slot 222 (the recipient cull single-player never
	// runs), `EmitSound(..., CHAN_BODY 4, table[RandomInt(0, 0)], 1.0, 0.8, 0, 100)`.
	NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, RunAi19TentacleHitGroundWav, 1, 1.0f,
		NpcKernelSoundsShared::GSoundsChanBody);
}

void FElysiumNpcMingXiaoTentacle::TentacleFloppingSound()
{
	// `0x1039f1a0`: the same emit on `CHAN_VOICE` 2 over `0x106477cc`, flags 0 -- the loop
	// `0x1039f310` (`MingXiaoTentacleDeathSound`) stops.
	NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, RunAi19TentacleFloppingWav, 1, 1.0f,
		NpcKernelSoundsShared::GSoundsChanVoice);
}

void FElysiumNpcMingXiaoTentacle::RunAI(bool bReduced)
{
	// Three blocks BEFORE the base, in order. The scope trace (name pick `0x1039e3d9` / `0x1039e3e3`)
	// and the VProf node `CNPC_VMingXiaoTentacle_RunAI` are absent: the enter (`0x1039e444`..
	// `0x1039e48a`: branches `0x1039e44c` `0x1039e456` `0x1039e464`, calls `0x1039e472` `0x1039e484`)
	// and the exit (`0x1039e63e`..`0x1039e6ef`: branches `0x1039e646` `0x1039e650` `0x1039e663`
	// `0x1039e66a` `0x1039e6cd`, call `0x1039e6c2`).
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	auto Fail = [this](int32 Line, int32 Reason)
	{
		// `+0x1b48 = line`, `+0x1b44 = file` (`0x1039e58c`), then slot 448 (`+0x700`) with the reason.
		RecordScheduleEvent(FString::Printf(TEXT("RunAI fail trace %s:%d"), RunAi19TentacleFile, Line));
		TaskFail(Reason);                                                     // 0x1039e596 slot 448
	};

	// (1) The evade re-plan. `FCOMP [+0x667c]; AND 0x4100; JNZ`: only a curtime strictly past the
	//     timer runs it.
	if (Now > TentacleUpdateEvadeTimer)                                       // 0x1039e4a6 +0x667c
	{
		TentacleUpdateEvadeTimer = Now + ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(RunAi19EvadeRearmMin, RunAi19EvadeRearmMax);         // 0x1039e4be / 0x1039e4cf
		if (NavigatorGoalIsActive()                                           // 0x1039e4d5 0x102ee6a0 / 0x1039e4dc
			&& NavGoalState() == RunAi19GoalTypeLocation)                     // 0x1039e4e8 0x102ee620 / 0x1039e4f0
		{
			const FElysiumNpc& Self = *this;
			if (Self.GetEnemy() == nullptr)                                   // 0x1039e4fa slot 167 / 0x1039e502
			{
				Fail(RunAi19TentacleNoEnemyLine, RunAi19TentacleNoEnemyReason);   // 0x1039e504 / 0x1039e50e
			}
			// `FLD [+0x6268]; FCOMP 256.0; TEST AH,0x41; JP`: at or below 256 continues, above (or
			// unordered) stops.
			else if (ScheduleHost.EnemyDistUnits <= RunAi19EvadeEnemyDistUnits)   // 0x1039e512 / 0x1039e523
			{
				FElysiumEntity* const Enemy = Self.GetEnemy();                // 0x1039e529 slot 167, asked again
				FVector NodeCm = FVector::ZeroVector;
				if (!NearestNavigatorNode(Enemy->GetAbsOrigin(), RunAi19EvadeNodeFleeUnits,
						RunAi19EvadeNodeSearchUnits, NodeCm))                 // 0x1039e548 slot 217 / 0x1039e551 0x102edae0 / 0x1039e558
				{
					Fail(RunAi19TentacleNoNodeLine, RunAi19TentacleNoNodeReason);     // 0x1039e55a / 0x1039e564
				}
				else if (!RunAi19NavUpdateGoalPos(NodeCm))                    // 0x1039e573 0x102ee220 / 0x1039e57a
				{
					Fail(RunAi19TentacleNoRouteLine, RunAi19TentacleNoRouteReason);   // 0x1039e57c / 0x1039e586
				}
			}
		}
	}

	// (2) The collision release: CLEAR the flag first, then test the area; a blocked area re-arms it.
	//     `AND 0x100` on `curtime < timer`: an equal stamp has expired, an unordered one has not.
	if (bIgnoreCollisionSpecies                                               // 0x1039e5a4 +0x6688
		&& Now >= TentacleIgnoreCollisionTimer)                               // 0x1039e5bc +0x6684
	{
		bIgnoreCollisionSpecies = false;                                      // 0x1039e5cb
		if (!IsAreaClear(GetAbsOrigin(), RunAi19TentacleClearMask))           // 0x1039e5d2 slot 217 / 0x1039e5db 0x102a0fb0 / 0x1039e5e2
		{
			bIgnoreCollisionSpecies = true;                                   // 0x1039e5f2
			TentacleIgnoreCollisionTimer = Now + ElysiumNpcTunables::One;    // 0x1039e5ec _DAT_104454c0 / 0x1039e5f9
		}
	}

	// (3) The one-shot landing sounds.
	if (!bTentacleHitGroundSound                                              // 0x1039e607 +0x6698
		&& GetGroundEntity() != nullptr)                                      // 0x1039e60d slot 209 / 0x1039e615
	{
		TentacleHitGroundSound();                                             // 0x1039e619 0x1039f030
		TentacleFloppingSound();                                              // 0x1039e620 0x1039f1a0
		bTentacleHitGroundSound = true;                                       // 0x1039e625
	}

	FElysiumNpc::RunAI(bReduced);                                             // 0x1039e633 0x1028fcc0
}

// =================================================================================================
// CNPC_VPedestrian -- 0x103a3670, 257 bytes
// =================================================================================================

void FElysiumNpcPedestrian::RunAI(bool bReduced)
{
	// The base pass FIRST, then the fleeing pedestrian's danger sound behind itself.
	FElysiumNpcHuman::RunAI(bReduced);                                        // 0x103a367b 0x10385a10 -> 0x1028fcc0
	if (!NpcFlags.Has(EElysiumNpcFlag::IN_FLEE_SCHED))                        // 0x103a3680 +0x14b8 & 0x80 / 0x103a368f
	{
		return;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (!(Now >= Senses.Memory.NextFleeSoundTime))                            // 0x103a369e +0x641c / 0x103a36ab (AND 0x100: unordered returns)
	{
		return;
	}
	Senses.Memory.NextFleeSoundTime = Now + RunAi19FleeSoundPeriodSeconds;    // 0x103a36b4 / 0x103a36c7
	FVector FleeForward = FVector::ZeroVector;
	StartTaskAngleVectors(GetAngles(), &FleeForward, nullptr);                    // 0x103a36cd slot 221 / 0x103a36d4 0x10139610
	const FVector BehindCm = GetOrigin()
		- FleeForward * (RunAi19FleeSoundBackUnits * ElysiumMove::U);             // 0x103a36dd..0x103a3729, 0x103a370a slot 220
	// `CSoundEnt::InsertSound(SOUND_DANGER 8, &point, DAT_1072bc8c, 10.0, DAT_1072bcc3, this)`: the
	// two cells are `sound_volume_table.txt` row 27, `NPC_DISCIPLINE_ALERT` (radius and occlusion),
	// which the bus resolves from the category.
	if (World != nullptr)
	{
		World->EmitGameSound(BehindCm, ElysiumGameSounds::DisciplineAlert(), -1.f, Handle,
			ElysiumStealth::HearingReductionCmFor(this), ElysiumGameSounds::Danger,
			RunAi19FleeSoundPeriodSeconds);                                   // 0x103a3762 0x101bac90
	}
}

// =================================================================================================
// CNPC_VSabbatLeader -- 0x103a75c0, 111 bytes
// =================================================================================================

void FElysiumNpcSabbatLeader::RunAI(bool bReduced)
{
	// The base pass FIRST, then the per-think blood-splash emitter update, which sees the state the
	// base pass produced. Scope trace absent.
	FElysiumNpcVampireBoss::RunAI(bReduced);                                  // 0x103a7618 0x10385a10 -> 0x1028fcc0
	SabbatLeaderUpdateBloodSplash();                                          // 0x103a761f 0x103aa960
}

// =================================================================================================
// CNPC_VSheriffMan -- 0x103aebd0, 121 bytes
// =================================================================================================

void FElysiumNpcSheriffMan::RunAI(bool bReduced)
{
	// The once-per-life floor-height cache in FRONT of the base pass, so `SelectSchedule`'s
	// `CategorizeHeights 0x103b1680` reads it on the same think. Scope trace absent.
	if (!bSheriffLedgeHeightStored)                                           // 0x103aec29 +0x66e7
	{
		CacheFloorHeights();                                                  // 0x103aec2d 0x103b1510
	}
	FElysiumNpcVampireBoss::RunAI(bReduced);                                  // 0x103aec39 0x10385a10 -> 0x1028fcc0
}

// =================================================================================================
// CNPC_VTzimisce -- 0x103bdef0, 80 bytes
// =================================================================================================

float& FElysiumNpcTzimisce::TzimisceObstructionLookaheadConVar()
{
	static float Value = 24.0f;
	return Value;
}

float& FElysiumNpcTzimisce::TzimisceObstructionScalarConVar()
{
	static float Value = 5.0f;
	return Value;
}

float& FElysiumNpcTzimisce::TzimisceObstructionZConVar()
{
	static float Value = 75.0f;
	return Value;
}

void FElysiumNpcTzimisce::RunAi19TzimisceReact(FElysiumEntity* Blocker, int32 Kind)
{
	if (Blocker == nullptr)                                                   // 0x103bdf00
	{
		return;
	}
	if (Kind == 1)                                                            // 0x103bdf09 / 0x103bdf0c
	{
		// Unreachable in retail: `0x103c0160` writes only 0. Carried as the listing has it.
		RunAi19ObstructionStepUp();                                           // 0x103bdf11 0x103c0860
		return;
	}
	RunAi19ObstructionPush(Blocker, TzimisceObstructionScalarConVar(),
		TzimisceObstructionZConVar());                                        // 0x103bdf2a 0x103c05e0
}

void FElysiumNpcTzimisce::RunAI(bool bReduced)
{
	// The decompiled C is misfolded (`this == 1`): the listing reserves one stack word (`PUSH ECX`) as
	// the sweep's out-word.
	int32 Kind = 0;
	FElysiumEntity* const Blocker = RunAi19ObstructionSweep(TzimisceObstructionLookaheadConVar(),
		/*bRetraceAtStepHeight=*/false, Kind);                                // 0x103bdef9 0x103c0160
	RunAi19TzimisceReact(Blocker, Kind);                                      // 0x103bdf00..0x103bdf2a
	FElysiumNpcBaseBoss::RunAI(bReduced);                                     // 0x103bdf1d / 0x103bdf36 0x1028fcc0
}

// =================================================================================================
// CNPC_VTzimisceHeadClaw -- 0x103c1d20, 26 bytes
// =================================================================================================

void FElysiumNpcTzimisceHeadClaw::RunAI(bool bReduced)
{
	// The ManBat `0x1038e990` twin: the slowed-grab teardown with force FALSE, then
	// `CAI_BaseNPCTroika::RunAI` directly (`0x1000704f`, no ChangBros forwarder).
	TzimisceHeadClawEndSlow(false);                                           // 0x103c1d25 0x103c2230(0)
	FElysiumNpcBaseBoss::RunAI(bReduced);                                     // 0x103c1d31 0x1028fcc0
}

// =================================================================================================
// CNPC_VZombie -- 0x103df850, 364 bytes
// =================================================================================================

void FElysiumNpcZombie::RunAI(bool bReduced)
{
	// (1) The despawn gate. `FLD m_flPlayerDist; FCOMP m_flRemoveDist; AND 0x4100; JNZ`: only a
	//     player distance strictly beyond the remove distance. `Kill` does not return early: the
	//     base pass and the grapple roll still run on this pass.
	const float PlayerDistUnits = Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U;   // +0x6264
	if (ZombieAiType == 1                                                     // 0x103df85a +0x6678
		&& PlayerDistUnits > ZombieRemoveDistUnits)                           // 0x103df86f +0x66dc
	{
		Kill();                                                               // 0x103df89f slot 119 0x1033cb90
	}
	// (2) The base pass, unconditionally.
	FElysiumNpcAnimal::RunAI(bReduced);                                       // 0x103df8ac 0x10360160 -> 0x1028fcc0

	// (3) The grapple roll, read after the base pass.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const float PlayerDistAfter = Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U;   // +0x6264 re-read
	if (World != nullptr
		&& (ZombieAiType == 1 || ZombieAiType == 0)                           // 0x103df8ba / 0x103df8be
		&& ActivityNumber != RunAi19ActZombieBusy                             // 0x103df8cb +0x0fec
		&& Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy)                // 0x103df8d5 0x10269aa0(0x46) / 0x103df8dc
		// `FLD min; FCOMP dist; TEST AH,0x41; JP`: min at or below the distance continues.
		&& RunAi19ZombieFloat(*this, TEXT("LungeDistanceMin"), 98.0f) <= PlayerDistAfter      // 0x103df8e7 0x101e8a90 / 0x103df8f7
		// `FLD max; FCOMP dist; AND 0x100`: max below the distance, or unordered, stops.
		&& RunAi19ZombieFloat(*this, TEXT("LungeDistanceMax"), 160.0f) >= PlayerDistAfter      // 0x103df902 0x101e8ab0 / 0x103df914
		&& Now >= ZombieGrappleReadyTimer)                                    // 0x103df923 +0x66d8 / 0x103df930
	{
		FElysiumEntity* const Closest = World->Resolve(Senses.Memory.ClosestPlayer);   // 0x103df932 +0x628c / 0x103df93b / 0x103df957 / 0x103df95d
		// `+0xa8` (the entity's player record) set: the closest is the player.
		if (Closest != nullptr && Closest->Handle == World->PlayerHandle())   // 0x103df95f / 0x103df967
		{
			const int32 Roll = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
				.RandRange(0, RunAi19ZombieRollMax);                          // 0x103df976 (*DAT_1070b244)+8
			const int32 Percent = RunAi19ZombieInt(*this, TEXT("Percent"), 2);    // 0x103df980 0x101e8b30
			if (Percent > Roll)                                               // 0x103df985 CMP EAX,EDI / 0x103df988 JLE
			{
				SetSchedule(RunAi19ZombieFeedLungeSchedule, false);           // 0x103df993 0x102ae750(0x16a, 0)
				ZombieGrappleReadyTimer = Now
					+ RunAi19ZombieFloat(*this, TEXT("DelayBetween"), 20.0f); // 0x103df99d 0x101e8c10 / 0x103df9ab
			}
		}
	}
	bGroundSpeedFromIntervalMovement = true;                                  // 0x103df9b1 +0x05ac, every exit
}
