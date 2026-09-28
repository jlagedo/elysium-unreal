// Story 0019/8 (29e under the strict verdict), family **Think19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Every retail call of `0x10002a7c` (the thunk of `CAI_BaseNPCTroika::NPCThink` `0x10292de0`) is a
// DIRECT call and is written `FElysiumNpc::NPCThink()`, whatever the port parent in between.
//
// Owns (Think19's `rule` rows): 0x10369120 CNPC_VCamera::NPCThink, 0x1037b3f0
// CNPC_VGhoulCroucher::NPCThink, 0x10394990 CNPC_VMingXiao::NPCThink, 0x103a05b0
// CNPC_VNewscaster::NPCThink, 0x103b9040 CNPC_VTzimisce::NPCThink, 0x103c6000
// CNPC_VVampireBoss::NPCThink, 0x103dfa20 CNPC_VZombie::NPCThink, 0x1035db20
// CNPC_VAndreiBlood::NPCThink, 0x10361490 CNPC_VAsianVampire::NPCThink, 0x1036c6c0
// CNPC_VChangBros::NPCThink, 0x10375e50 CNPC_VFrenzyShadow::NPCThink, 0x103af830
// CNPC_VSheriffMan::NPCThink. (`0x10375e50` landed earlier in `ElysiumNpcFrenzyShadow.cpp`; lane
// L13b re-read it against the listing and it stands.) Walked prose:
// `docs/vtmb/npc-ai/schedule-kernel.md` § "Story 8, family Think19".

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcCamera.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcNewscaster.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcZombie.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

namespace
{
	double Think19SpeciesNow(const FElysiumEntity& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	// The four boss think intervals, each a float cell read by `FLD` and added to curtime -- one cell
	// per class, 0.1f in the pinned image. None is a row of the kernel table.
	constexpr float GThink19VampireBossInterval = 0.1f;   // `0x104ce8b8` (CNPC_VVampireBoss / SabbatLeader)
	constexpr float GThink19AsianVampireInterval = 0.1f;  // `0x104a9304`
	constexpr float GThink19ChangBrosInterval = 0.1f;     // `0x104ad9f0`
	constexpr float GThink19SheriffManInterval = 0.1f;    // `0x104c6120`

	// `CNPC_VCamera::NPCThink`: the refused re-arm `0x104491b4` (0.1f) and the accepted cadence, the
	// DOUBLE `0x10449198` (0.2).
	constexpr float GThink19CameraRefusedInterval = 0.1f;
	constexpr double GThink19CameraInterval = 0.2;

	// `CNPC_VGhoulCroucher::NPCThink`: `PUSH 0x43480000` (200.0) / `PUSH 0x44160000` (600.0) and the
	// floor `0x3dcccccd` (0.1f) stored when the rate is below the DOUBLE 0.1 at `0x104493d0`.
	constexpr float GThink19GhoulFullRateUnits = 200.f;
	constexpr float GThink19GhoulFloorRateUnits = 600.f;
	constexpr float GThink19GhoulRateFloor = 0.1f;

	// `CNPC_VMingXiao::NPCThink`: slot 518's `1.0, 0.5, 0.0` (`0x10394ab8`..`0x10394abf`), the slime
	// roll `RandomInt(0, 99) < 10` (`0x10394b1b`..`0x10394b27`), the six regrow timers and their
	// re-arm `0x7f7fffff` (`0x10394c0b`).
	constexpr float GThink19MingPickupFaceDuration = 1.0f;
	constexpr float GThink19MingPickupFaceRamp = 0.5f;
	constexpr float GThink19MingPickupFaceTolerance = 0.f;
	constexpr int32 GThink19MingSlimeRollMax = 0x63;
	constexpr int32 GThink19MingSlimeRollBelow = 10;
	constexpr int32 GThink19MingRegrowTimers = 6;
	constexpr uint32 GThink19MingFaceEnemyBits = 0x80000400u;   // `0x7ffffbff` / `OR 0x80000400`

	// `m_iZombieAIType == 1` (`0x103dfa92`); the `<= 1.0` perception test (`0x104454c0`).
	constexpr int32 GThink19ZombieReacquireType = 1;

	// `0x101beed0`, the clamped fraction `(value - min) / (max - min)`: above `max` answers
	// `(max - min) / (max - min)`, below `min` answers `(min - min) / (max - min)`.
	float Think19GhoulRemapClamped(float Value, float Min, float Max)
	{
		if (Max < Value)
		{
			return (Max - Min) / (Max - Min);
		}
		if (Value < Min)
		{
			return (Min - Min) / (Max - Min);
		}
		return (Value - Min) / (Max - Min);
	}

	// `newscaster_debug` (`ConVar` object `0x1093be88`, pointer `0x1093be8c`, constructed by `0x103a0540`
	// with the shared default literal `DAT_105399a0`, "0"): not a row of the kernel ConVar table.
	int32 Think19NewscasterDebugConVar()
	{
		return 0;
	}

	// `__ftol` (`0x10431320`): a 64-bit `FISTP qword` (truncating) whose LOW dword is `EAX`. A value
	// in `[2^31, 2^63)` answers its low 32 bits; NaN or anything outside `int64` is the x87 integer
	// indefinite `0x8000000000000000`, whose low dword is 0.
	int32 Think19Ftol(float Value)
	{
		if (!(Value >= -9223372036854775808.f && Value < 9223372036854775808.f))
		{
			return 0;
		}
		return static_cast<int32>(static_cast<uint32>(static_cast<uint64>(static_cast<int64>(Value))));
	}
}

// Slot 431: `0x1035db20`, 90 bytes. The boss think and nothing else: Andrei keeps its 0.1 s.
void FElysiumNpcAndreiBlood::NPCThink()
{
	FElysiumNpcVampireBoss::NPCThink();                                     // 0x1035db6c 0x103c6000
}

// Slot 431: `0x10361490`, 114 bytes.
void FElysiumNpcAsianVampire::NPCThink()
{
	FElysiumNpcVampireBoss::NPCThink();                                     // 0x103614e0 0x103c6000
	NextThink = static_cast<float>(Think19SpeciesNow(*this)) + GThink19AsianVampireInterval;  // 0x103614e5..0x103614f3
}

// Slot 431: `0x10369120`, 210 bytes. Replaces the Troika body outright.
// Also carries the inherited body of CNPC_VCameraSecurity.
void FElysiumNpcCamera::NPCThink()
{
	const double Now = Think19SpeciesNow(*this);
	if (DebugRingDumpRequested())                                           // 0x10369123 / 0x1036912b
	{
		ClearDebugRingDumpRequest();                                        // 0x1036912d
		DumpDebugLogRing();                                                 // 0x10369134 0x1027efb0
	}
	if (IsAiDisabled())                                                     // 0x1036913b 0x1029f2e0 / 0x10369142
	{
		return;   // no stamp written
	}
	CacheInterruptConditionsForMaintenance(Now);                            // 0x1036914a 0x1026a0f0
	if (!Think19AiConsoleGate())                                            // 0x10369151 / 0x10369158
	{
		if (!Think19NodeGraphBuilt())                                       // 0x1036915a / 0x10369161
		{
			NextThink = static_cast<float>(Now) + GThink19CameraRefusedInterval;  // 0x10369167..0x10369175
		}
		return;
	}
	RunAI(false);                                                           // 0x1036917f / 0x10369183 slot 432
	ScheduleHost.LastUpdate = ScheduleHost.NextUpdate;                      // 0x10369189 / 0x1036919b
	ScheduleHost.LastNormal = ScheduleHost.NextNormal;                      // 0x1036918f / 0x103691a7
	ScheduleHost.LastMove = ScheduleHost.NextMove;                          // 0x10369195 / 0x103691ad
	ScheduleHost.LastAI = ScheduleHost.NextAI;                              // 0x103691a1 / 0x103691b3
	// `FLD curtime; FADD double 0.2; FST m_flNextThink` -- the float result is what the four copy.
	const float Stamp = static_cast<float>(Now + GThink19CameraInterval);  // 0x103691bf / 0x103691c2
	NextThink = Stamp;                                                      // 0x103691c8
	ScheduleHost.NextUpdate = Stamp;                                        // 0x103691d4
	ScheduleHost.NextNormal = Stamp;                                        // 0x103691de
	ScheduleHost.NextMove = Stamp;                                          // 0x103691e4
	ScheduleHost.NextAI = Stamp;                                            // 0x103691ea
}

// Slot 431: `0x1036c6c0`, 114 bytes.
// Also carries the inherited body of CNPC_VChangBrosBlade, CNPC_VChangBrosClaw.
void FElysiumNpcChangBros::NPCThink()
{
	FElysiumNpcVampireBoss::NPCThink();                                     // 0x1036c710 0x103c6000
	NextThink = static_cast<float>(Think19SpeciesNow(*this)) + GThink19ChangBrosInterval;  // 0x1036c715..0x1036c723
}

void FElysiumNpcGhoulCroucher::GhoulSetParticleRateScale(FElysiumEntity* Particle, float Rate)
{
	// SEAM: `0x100fb980` (see the declaration).
	(void)Particle;
	GhoulLastParticleRateScale = Rate;
	++GhoulParticleRateScaleCalls;
}

// Slot 431: `0x1037b3f0`, 202 bytes. The Troika body first; the species work is a tail.
void FElysiumNpcGhoulCroucher::NPCThink()
{
	FElysiumNpc::NPCThink();                                                // 0x1037b3f4 0x10292de0
	// The byte at `+0x6665` -- the port's `bGhoulSpawnBurning` (key `on_fire`); the checklist names
	// the offset `m_bSpawnDisturbed`, the packet `m_bSpawnBurning`, and the listing only the offset.
	if (!bGhoulSpawnBurning)                                                // 0x1037b3f9 / 0x1037b401
	{
		return;
	}
	// `m_hBurningParticle` (+0x6670): `-1`, stale, or a null entry returns (`0x1037b410`..`0x1037b43a`).
	FElysiumEntity* Particle = World != nullptr ? World->Resolve(BurningParticle) : nullptr;
	if (Particle == nullptr)
	{
		return;
	}
	// `rate = 1.0 - 0x101beed0(m_flPlayerDist, 200.0, 600.0)` (Source units).
	float Rate = ElysiumNpcTunables::One - Think19GhoulRemapClamped(
		Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U,
		GThink19GhoulFullRateUnits, GThink19GhoulFloorRateUnits);          // 0x1037b43c..0x1037b452
	// `FCOMP double 0.1` / `TEST AH,5` / `JP`: an ordered rate BELOW 0.1 becomes 0.1f; equal or NaN keeps.
	if (static_cast<double>(Rate) < ElysiumNpcTunables::TenthDouble)       // 0x1037b45f / 0x1037b46a
	{
		Rate = GThink19GhoulRateFloor;                                      // 0x1037b46c
	}
	// The handle is re-resolved (`0x1037b474`..`0x1037b49a`); its failure arm calls the setter with a
	// NULL receiver (`0x1037b4b2`), unreachable after the first resolve on one thread.
	GhoulSetParticleRateScale(Particle, Rate);                              // 0x1037b4a3 0x100fb980
}

// Slot 431: `0x10394990`, 926 bytes. ALL of Ming Xiao's work runs BEFORE the Troika body.
void FElysiumNpcMingXiao::NPCThink()
{
	// Scope trace "CNPC_VMingXiao::NPCThink" and VProf node "CNPC_VMingXiao_NPCThink": absent.
	// `switch (m_eThrowableObjectMode)` through the table `0x10394d30`; `> 4` unsigned is the default.
	switch (static_cast<uint32>(MingXiaoThrowableObjectMode))              // 0x10394a82 / 0x10394a8b / 0x10394a8d
	{
	case 1:
	case 2:
	{
		// `flags2 &= 0x7ffffbff` -- MOVE_FACE_ENEMY and bit 31 -- then slot 518 at my origin plus the
		// saved pickup forward (Source units, carried as the sibling `+0x6720` word is).
		NpcFlags.ClearRawWord2Bits(GThink19MingFaceEnemyBits);              // 0x10394a9c / 0x10394aa2
		const FVector FaceTarget = Origin + MingXiaoPickupSavedForward * ElysiumMove::U;   // 0x10394aaa slot 217 / 0x10394ab0..0x10394adb
		AddFacingTarget(FaceTarget, GThink19MingPickupFaceDuration, GThink19MingPickupFaceRamp,
			GThink19MingPickupFaceTolerance);                               // 0x10394b03 slot 518
		break;
	}
	default:
		NpcFlags.SetRawWord2Bits(GThink19MingFaceEnemyBits);                // 0x10394b0b OR 0x80000400
		break;
	}
	if (ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, GThink19MingSlimeRollMax)
		< GThink19MingSlimeRollBelow)                                       // 0x10394b21 / 0x10394b27
	{
		(void)GetAbsOrigin();                                               // 0x10394b31 slot 217, discarded
		(void)GetAbsAngles();                                               // 0x10394b3b slot 219, discarded
		MingXiaoEmitter(TEXT("Ming_xiao_slimetrail_emitter"), TEXT("Bip01 TailRoot"));   // 0x10394b50 0x102c42a0
		MingXiaoEmitter(TEXT("Ming_xiao_slimetrail_emitter2"), TEXT("Bip01 Tail1"));     // 0x10394b64
		MingXiaoEmitter(TEXT("Ming_xiao_slimetrail_emitter2"), TEXT("Bip01 Tail2"));     // 0x10394b78
		MingXiaoEmitter(TEXT("Ming_xiao_slimetrail_emitter2"), TEXT("Bip01 Tail3"));     // 0x10394b8c
		MingXiaoEmitter(TEXT("Ming_xiao_slimetrail_emitter2"), TEXT("Bip01 Tail4"));     // 0x10394ba0
		MingXiaoEmitter(TEXT("Ming_xiao_slimetrail_emitter2"), TEXT("Bip01 Tail5"));     // 0x10394bb4
		MingXiaoEmitter(TEXT("Ming_xiao_slimetrail_emitter2"), TEXT("Bip01 Tail6"));     // 0x10394bc8
	}
	if (!IsMingXiaoProxy())                                                 // 0x10394bcf 0x10398870 / 0x10394bd6
	{
		for (int32 Index = 0; Index < GThink19MingRegrowTimers; ++Index)   // 0x10394bd8..0x10394c3a
		{
			// `FLD m_flPrevAnimTime; FCOMP timer; TEST 0x4100; JNZ skip`: only an ORDERED
			// `m_flPrevAnimTime > timer` regrows.
			if (!(static_cast<double>(PrevAnimTime) > MingXiaoRegrowTimers[Index]))   // 0x10394be0..0x10394bf0
			{
				continue;
			}
			MingXiaoSeveredTentacleMask &= ~(1u << Index);                  // 0x10394bf2..0x10394c05
			MingXiaoRegrowTimers[Index] = static_cast<double>(FLT_MAX);     // 0x10394c0b 0x7f7fffff
			++MingXiaoConnectedTentacleCount;                               // 0x10394c12..0x10394c1b
			BodyGroup();                                                    // 0x10394c21 0x10398800
			MingXiaoIdealRange = MingXiaoIdealRangeFromLimbs();             // 0x10394c28 0x103986b0 / 0x10394c2d
		}
	}
	if (!IsMingXiaoProxy())                                                 // 0x10394c3e / 0x10394c45
	{
		CoordinateTroops();                                                 // 0x10394c49 0x10399610
	}
	// `PushPhysicsObjects` `0x10399c70` (0x10394c50): its whole body is a scope trace and an empty
	// VProf scope -- nothing to run.
	FUN_102c43f0();                                                         // 0x10394c57 0x102c43f0
	FElysiumNpc::NPCThink();                                                // 0x10394c5e 0x10292de0
}

// Slot 431: `0x103a05b0`, 136 bytes. The move and AI clocks are parked a second out BEFORE the base.
void FElysiumNpcNewscaster::NPCThink()
{
	const double Now = Think19SpeciesNow(*this);
	ScheduleHost.NextMove = Now + ElysiumNpcTunables::One;                  // 0x103a05b8..0x103a05c1
	ScheduleHost.NextAI = Now + ElysiumNpcTunables::One;                    // 0x103a05cd..0x103a05d6
	ScheduleHost.LastMove = Now;                                            // 0x103a05e2 / 0x103a05e5
	ScheduleHost.LastAI = Now;                                              // 0x103a05f1 / 0x103a05f6
	FElysiumNpc::NPCThink();                                                // 0x103a05fc 0x10292de0
	PlayNextNewscasterStory();                                              // 0x103a0603 0x103a0670
	if (Think19NewscasterDebugConVar() != 0                                 // 0x103a0610 / 0x103a0615 / 0x103a0622
		&& (DebugOverlays & 1) == 0)                                        // 0x103a0624 / 0x103a062b
	{
		TArray<FString> Lines;
		(void)FUN_103a0ff0(0, Lines);                                       // 0x103a0631 0x103a0ff0
	}
}

// Slot 431: `0x103af830`, 114 bytes.
void FElysiumNpcSheriffMan::NPCThink()
{
	FElysiumNpcVampireBoss::NPCThink();                                     // 0x103af880 0x103c6000
	NextThink = static_cast<float>(Think19SpeciesNow(*this)) + GThink19SheriffManInterval;  // 0x103af885..0x103af893
}

// Slot 431: `0x103b9040`, 16 bytes. Solid again (the ignore-collision expiry) BEFORE the pass.
void FElysiumNpcTzimisce::NPCThink()
{
	FUN_102c43f0();                                                         // 0x103b9043 0x102c43f0
	FElysiumNpc::NPCThink();                                                // 0x103b904b JMP 0x10292de0
}

// Slot 431: `0x103c6000`, 114 bytes. Also carries the inherited body of CNPC_VSabbatLeader.
void FElysiumNpcVampireBoss::NPCThink()
{
	FElysiumNpc::NPCThink();                                                // 0x103c6050 0x10292de0
	// The base pass's own `m_flNextThink` is overwritten: a fixed 10 Hz.
	NextThink = static_cast<float>(Think19SpeciesNow(*this)) + GThink19VampireBossInterval;  // 0x103c6055..0x103c6063
}

// `0x103e0a00`, 347 bytes (`__fastcall`, the zombie in ECX).
void FElysiumNpcZombie::ZombieReacquireEnemy()
{
	FElysiumEntity* Enemy = static_cast<const FElysiumNpcBase*>(this)->GetEnemy();   // 0x103e0a06 slot 167
	FElysiumCombatCharacter* EnemyCharacter =
		Enemy != nullptr ? Enemy->AsCombatCharacter() : nullptr;           // 0x103e0a0e / 0x103e0a10 +0x9c / 0x103e0a18
	if (EnemyCharacter == nullptr)
	{
		// `m_hClosestPlayer` (+0x628c): `-1`, stale or a null entry returns (`0x103e0a73`..`0x103e0aa0`).
		FElysiumEntity* Candidate =
			World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
		if (Candidate == nullptr)
		{
			return;                                                         // 0x103e0b58
		}
		// `0x10146a80(candidate)`: the candidate's own stat list and `+0x14dc`. NAMED CRASH GUARD: a
		// candidate with no combat-character half answers false (retail reads its words regardless;
		// the closest player always has one).
		const FElysiumCombatCharacter* CandidateCharacter = Candidate->AsCombatCharacter();
		if (CandidateCharacter != nullptr && CandidateCharacter->IsObfuscatedForSenses())  // 0x103e0ace / 0x103e0adb
		{
			ElysiumNpcEnemy::SetEnemy(*this, Candidate->Handle);            // 0x103e0b04 0x10279a50
			return;
		}
		Slot596(Candidate);                                                 // 0x103e0b42 slot 596
		return;
	}
	if (!EnemyCharacter->IsObfuscatedForSenses())                           // 0x103e0a1c 0x10146a80 / 0x103e0a23
	{
		// Slot 544 `UpdateEnemyMemory(enemy, enemy->GetAbsOrigin(), enemy + 0x3d4)`; the third word is
		// an address inside the enemy (unrecovered) that the base body does not read -- NULL here, as
		// the frenzy shadow's body passes it.
		UpdateEnemyMemory(Enemy, Enemy->Origin, nullptr);                  // 0x103e0a56 slot 217 / 0x103e0a60 slot 544
		return;
	}
	// Slot 541 `GetEnemies()` -> `0x102dfa20`: a record for the enemy exists.
	if (EnemyMemory.Find(Enemy->Handle) == nullptr)                         // 0x103e0a2a / 0x103e0a32 / 0x103e0a39
	{
		return;
	}
	// `0x10279c20`: slot 167 re-read, slot 541, `CAI_Enemies::MarkAsEluded` `0x102dfd90` -- the
	// record's `+0x35` eluded byte. The owner's slot-57 notification it sends first has no port target.
	FElysiumEntity* Current = static_cast<const FElysiumNpcBase*>(this)->GetEnemy();
	if (Current != nullptr)
	{
		EnemyMemory.MarkEluded(Current->Handle);                            // 0x103e0a43 JMP 0x10279c20
	}
}

// Slot 431: `0x103dfa20`, 197 bytes.
void FElysiumNpcZombie::NPCThink()
{
	// Scope trace "CNPC_VZombie::NPCThink": absent.
	if (IsAiDisabled())                                                     // 0x103dfa89 0x1029f2e0 / 0x103dfa90
	{
		return;   // neither the base pass nor slot 614
	}
	if (ZombieAiType == GThink19ZombieReacquireType)                        // 0x103dfa92 / 0x103dfa99
	{
		ZombieReacquireEnemy();                                             // 0x103dfa9d 0x103e0a00
	}
	// `m_flSeekDistInspection (+0x63b8) <= 1.0`, ordered (`JP` skips NaN), in Source units.
	if (Senses.Perception.VisionDistanceCm / ElysiumMove::U <= ElysiumNpcTunables::One)  // 0x103dfaa2..0x103dfab3
	{
		Senses.ResolveTuning(*this);                                        // 0x103dfab7 0x1028fb70
	}
	FElysiumNpc::NPCThink();                                                // 0x103dfabe 0x10292de0
	// `__ftol(m_flNextThink) <= 0` -> slot 614. RETAIL QUIRK reproduced: during the map's first
	// second (or for a never-think stamp, whose `__ftol` low dword is 0) every think resets the
	// four clocks.
	if (Think19Ftol(NextThink) <= 0)                                        // 0x103dfac3 / 0x103dfac9 / 0x103dfad0
	{
		ResetThinkTimers(Think19SpeciesNow(*this));                         // 0x103dfad6 slot 614
	}
}
