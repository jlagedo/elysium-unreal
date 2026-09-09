#include "Substrate/ElysiumNpcSenses.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumWorldServices.h"
#include "HAL/IConsoleManager.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Debug/ElysiumNpcDebugLogging.h"

// Retail ConCommands `npc_ignore_player` / `npc_ignore_senses` (`0x10088c70` / `0x10088d70`)
// toggle `DAT_10924fb9` / `DAT_10924fba`. The spec's job is those bytes as ConVars; they are
// not saved. Help strings are the recovered ones.
static TAutoConsoleVariable<int32> CVarNpcIgnorePlayer(
	TEXT("npc_ignore_player"),
	0,
	TEXT("NPCs will not hear or see the player"),
	ECVF_Default);
static TAutoConsoleVariable<int32> CVarNpcIgnoreSenses(
	TEXT("npc_ignore_senses"),
	0,
	TEXT("NPCs will not hear or see anything"),
	ECVF_Default);

bool ElysiumNpcSense::IgnorePlayer()
{
	return CVarNpcIgnorePlayer.GetValueOnGameThread() != 0;
}

bool ElysiumNpcSense::IgnoreSenses()
{
	return CVarNpcIgnoreSenses.GetValueOnGameThread() != 0;
}

namespace
{
	// The one world term the engine answers (K13). No embodiment is a headless run, and the
	// service's own default is CLEAR for the reason stated on the seam: a `-nullrhi` Substrate run
	// has no collision world, and reporting "blocked" there would blind every NPC in exactly the
	// runs meant to prove they can see.
	bool SegmentClear(const FElysiumEntityWorld* World, const FVector& FromCm, const FVector& ToCm)
	{
		const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		return Embodiment == nullptr || Embodiment->QueryLineOfSight(FromCm, ToCm);
	}

	// The TARGET's own committed stealth surface (§5.9 -> `docs/vtmb/stealth.md` "Visual observer
	// transaction"). Only the player carries one — retail's `m_flStealthVisionScalar` and
	// `m_flStealthVisionCone` are CBasePlayer fields — which is why these take the player and
	// nothing else: a target that is not the player never reaches them, and the neutral 1.0 it
	// reads instead is `IsInViewCone`'s own parameter default.
	//
	// The senses never recompute the surface and never read the tables: they consume whatever the
	// player think last committed, which is what keeps one producer for the whole transaction.
	float TargetVisionScalar(const FElysiumPlayer& Target) { return Target.Stealth.VisionScalar; }
	float TargetConeScalar(const FElysiumPlayer& Target) { return Target.Stealth.ConeScalar; }

	// The hear-category to output mapping is `ElysiumNpcCond::IsCombatSoundCategory`, which the
	// `HEAR_*` condition producer reads too — one rule, one owner, and its CHOSEN, NOT RECOVERED
	// mark travels with it. What stays this file's own choice is the OUTPUT arbitration below:
	// exactly one output fires per accepted stimulus (combat outranks player, which outranks world)
	// rather than the found/lost pair's both-fire shape.

	const FElysiumRuleTable* FindInspectionTable(UElysiumRulebookSubsystem* Rules,
		const TCHAR* InternalName)
	{
		if (Rules == nullptr)
		{
			return nullptr;
		}
		const FElysiumFeat* Inspection = Rules->Feats().Find(TEXT("Inspection"));
		if (Inspection == nullptr)
		{
			return nullptr;
		}
		const FElysiumRuleTable* Table = Inspection->Tables.Find(ElysiumFold(InternalName));
		return (Table && Table->IsValid()) ? Table : nullptr;
	}
}

void FElysiumNpcPerception::ResolveFromRulebook(int32 AuthoredPerception, float AuthoredVision,
	float AuthoredHearing, UElysiumRulebookSubsystem* Rules, FString& OutWarning)
{
	// Each table is fetched only for a channel that actually derives: an NPC whose author wrote
	// both values must not pull `feats.txt` off disk to answer a question it does not ask.
	const bool bDeriveVision = AuthoredVision == ElysiumNpcSense::DerivedSentinel;
	const bool bDeriveHearing = AuthoredHearing == ElysiumNpcSense::DerivedSentinel;
	Resolve(AuthoredPerception, AuthoredVision, AuthoredHearing,
		bDeriveVision ? FindInspectionTable(Rules, ElysiumNpcSense::VisionTableName) : nullptr,
		bDeriveHearing ? FindInspectionTable(Rules, ElysiumNpcSense::HearingTableName) : nullptr,
		OutWarning);
	if (Rules == nullptr)
	{
		// No rulebook at all is the supported headless case, not a missing table: a `-nullrhi`
		// Substrate world and an editor commandlet both run without one, and the rulebook already
		// reports its own failed load once. The same posture the sound bus takes with no volume
		// table — the fallback applies and nothing is warned. A rulebook that IS present but does
		// not carry the table keeps its warning, because that one is an authoring fact.
		OutWarning.Reset();
	}
}

void FElysiumNpcPerception::Resolve(int32 AuthoredPerception, float AuthoredVision,
	float AuthoredHearing, const FElysiumRuleTable* VisionTable,
	const FElysiumRuleTable* HearingTable, FString& OutWarning)
{
	OutWarning.Reset();
	bResolved = true;
	bUsedFallback = false;

	const bool bDeriveVision = AuthoredVision == ElysiumNpcSense::DerivedSentinel;
	const bool bDeriveHearing = AuthoredHearing == ElysiumNpcSense::DerivedSentinel;

	// An authored value that is not the sentinel is copied through and `npc_perception` is inert
	// for that channel. The keyvalue is in Source game units; the effective distance is in cm.
	float VisionUnits = AuthoredVision;
	float Hearing = AuthoredHearing;

	if (bDeriveVision || bDeriveHearing)
	{
		if (bDeriveVision)
		{
			if (VisionTable)
			{
				// Both tables author `Clamping 1`, so a perception outside the authored 0..10
				// range takes the nearest end rather than the miss default.
				VisionUnits = VisionTable->Lookup(AuthoredPerception,
					ElysiumNpcSense::FallbackVisionUnits);
			}
			else
			{
				VisionUnits = ElysiumNpcSense::FallbackVisionUnits;
				bUsedFallback = true;
			}
		}
		if (bDeriveHearing)
		{
			if (HearingTable)
			{
				Hearing = HearingTable->Lookup(AuthoredPerception,
					ElysiumNpcSense::FallbackHearingScalar);
			}
			else
			{
				Hearing = ElysiumNpcSense::FallbackHearingScalar;
				bUsedFallback = true;
			}
		}

		if (bUsedFallback)
		{
			OutWarning = FString::Printf(
				TEXT("perception tables unavailable (feats.txt -> Inspection -> %s / %s); "
					 "npc_perception %d falls back to the average-human row (%.0f units, %.2fx)"),
				ElysiumNpcSense::VisionTableName, ElysiumNpcSense::HearingTableName,
				AuthoredPerception, ElysiumNpcSense::FallbackVisionUnits,
				ElysiumNpcSense::FallbackHearingScalar);
		}
	}

	VisionDistanceCm = FMath::Max(0.f, VisionUnits) * ElysiumMove::U;
	HearingScalar = FMath::Max(0.f, Hearing);
}

void FElysiumNpcMemory::Reset()
{
	*this = FElysiumNpcMemory();
}

void FElysiumNpcMemory::Serialize(FElysiumSaveArchive& Ar)
{
	auto SerializeSound = [&Ar](FElysiumGameSoundEvent& Sound)
	{
		Ar << Sound.Position;
		Ar << Sound.Category;
		Ar << Sound.TypeMask;
		Ar << Sound.RadiusCm;
		Ar << Sound.StealthHearingReductionCm;
		Ar << Sound.UnadjustedRadiusCm;
		Ar << Sound.Source;
		Ar << Sound.Time;
		Ar << Sound.ExpireTime;
		Ar << Sound.Serial;
		uint8 Occludable = Sound.bOccludable ? 1 : 0;
		Ar << Occludable;
		if (Ar.IsLoading()) { Sound.bOccludable = Occludable != 0; }
	};
	Ar << Enemy;
	Ar << LastEnemy;
	for (int32 i = 0; i < static_cast<int32>(ESeen::Count); ++i)
	{
		Ar << LastSeen[i];
		Ar << LastSeenTime[i];
	}
	Ar << LastHeardSource;
	Ar << LastHeardPosition;
	Ar << LastHeardCategory;
	Ar << LastHeardTime;
	Ar << LastDamageAttacker;
	Ar << LastDamageTime;
	Ar << LastDamageAmount;
	Ar << EnemyLosFailures;
	Ar << EnemyLastLosTime;
	uint8 Occluded = bEnemyOccluded ? 1 : 0;
	uint8 Latched = bEnemyLosLatched ? 1 : 0;
	Ar << Occluded;
	Ar << Latched;
	Ar << ClosestPlayer;
	Ar << ClosestPlayerDistanceCm;
	uint8 InRange = bPlayerInRange ? 1 : 0;
	uint8 OuterBand = bPlayerInOuterBand ? 1 : 0;
	uint8 InCone = bPlayerInCone ? 1 : 0;
	uint8 PlayerLos = bPlayerLos ? 1 : 0;
	Ar << InRange;
	Ar << OuterBand;
	Ar << InCone;
	Ar << PlayerLos;
	Ar << PlayerLosLastClearTime;
	Ar << PlayerLosNextUpdateTime;
	Ar << StealthVisionOverrideUntil;
	Ar << BestSeeUnknown;
	Ar << LastSeeUnknown;
	Ar << LastSeeUnknownPosition;
	Ar << SeeUnknownRepeatSightings;
	Ar << SeeUnknownRunTimer;
	Ar << SeeUnknownStartTimer;
	uint8 IgnoreUnknown = bIgnoreUnknown ? 1 : 0;
	uint8 MadeInitialUnknownResponse = bMadeInitialUnknownResponse ? 1 : 0;
	Ar << IgnoreUnknown;
	Ar << MadeInitialUnknownResponse;
	SerializeSound(LastSoundCombat);
	SerializeSound(LastSoundBulletImpact);
	SerializeSound(LastSoundFlinch);
	SerializeSound(LastSoundPlayer);
	SerializeSound(LastSoundDanger);
	SerializeSound(LastSoundPhysicsDanger);
	SerializeSound(LastSoundWorld);
	SerializeSound(BestSound);
	// The sound sweep's committed source and its two clocks. Version `SoundSweep` is the save floor,
	// so this is unconditional -- an older payload is refused whole rather than half-read.
	Ar << BestSoundSource;
	Ar << NextInvestigateSoundTime;
	Ar << NextSeeSoundSourceTime;
	// Version 19 appends the repeated-damage window at the END of the memory record. Elusion belongs
	// to CAI_Memory's per-observed-actor record, not the committed-enemy tracking cache here.
	if (Ar.Version() >= FElysiumSaveVersion::NpcCognition)
	{
		Ar << RepeatedDamageWindowStart;
		Ar << RepeatedDamageAccumulated;
	}
	// Version 20 appends the detected-attack record after those, in the same additive shape: a
	// payload that predates it restores an NPC that has not been swung at, which is the default.
	if (Ar.Version() >= FElysiumSaveVersion::NpcCombat)
	{
		Ar << DetectedAttackAttacker;
		Ar << DetectedAttackTime;
	}
	if (Ar.IsLoading())
	{
		if (Ar.Version() < FElysiumSaveVersion::NpcCombat)
		{
			DetectedAttackAttacker = FElysiumEntityHandle::Invalid();
			DetectedAttackTime = -1.0;
		}
		if (Ar.Version() < FElysiumSaveVersion::NpcCognition)
		{
			// A payload that predates the block restores the default rather than whatever this live
			// object happened to be carrying before the load.
			RepeatedDamageWindowStart = -1.0;
			RepeatedDamageAccumulated = 0;
		}
		bEnemyOccluded = Occluded != 0;
		bEnemyLosLatched = Latched != 0;
		bPlayerInRange = InRange != 0;
		bPlayerInOuterBand = OuterBand != 0;
		bPlayerInCone = InCone != 0;
		bPlayerLos = PlayerLos != 0;
		bIgnoreUnknown = IgnoreUnknown != 0;
		bMadeInitialUnknownResponse = MadeInitialUnknownResponse != 0;
		EnemyLosFailures = FMath::Clamp(EnemyLosFailures, 0,
			ElysiumNpcSense::EnemyLosFailureLimit);
		// A negative accumulator would make the 15% test unfalsifiable rather than merely wrong, so
		// a payload from another build is floored rather than trusted.
		RepeatedDamageAccumulated = FMath::Max(0, RepeatedDamageAccumulated);
	}
}

void FElysiumNpcMemory::Rebase(const FElysiumEntityWorld& World)
{
	// Every remembered handle goes through the one rebase path. A handle that no longer resolves
	// becomes invalid rather than pointing at whichever entity now occupies its slot; the LOS
	// latch travels with the enemy it was taken for, so it drops with it.
	Enemy = World.RebaseSavedHandle(Enemy);
	LastEnemy = World.RebaseSavedHandle(LastEnemy);
	for (int32 i = 0; i < static_cast<int32>(ESeen::Count); ++i)
	{
		LastSeen[i] = World.RebaseSavedHandle(LastSeen[i]);
		if (!LastSeen[i].IsSet())
		{
			LastSeenTime[i] = -1.0;
		}
	}
	LastHeardSource = World.RebaseSavedHandle(LastHeardSource);
	auto RebaseSoundOwner = [&World](FElysiumGameSoundEvent& Sound)
	{
		if (!Sound.Source.IsSet()) return;
		Sound.Source = World.RebaseSavedHandle(Sound.Source);
		if (!Sound.Source.IsSet()) { Sound.Serial = 0; }
	};
	RebaseSoundOwner(LastSoundCombat);
	RebaseSoundOwner(LastSoundBulletImpact);
	RebaseSoundOwner(LastSoundFlinch);
	RebaseSoundOwner(LastSoundPlayer);
	RebaseSoundOwner(LastSoundDanger);
	RebaseSoundOwner(LastSoundPhysicsDanger);
	RebaseSoundOwner(LastSoundWorld);
	RebaseSoundOwner(BestSound);
	BestSoundSource = World.RebaseSavedHandle(BestSoundSource);
	LastDamageAttacker = World.RebaseSavedHandle(LastDamageAttacker);
	ClosestPlayer = World.RebaseSavedHandle(ClosestPlayer);
	BestSeeUnknown = World.RebaseSavedHandle(BestSeeUnknown);
	LastSeeUnknown = World.RebaseSavedHandle(LastSeeUnknown);
	DetectedAttackAttacker = World.RebaseSavedHandle(DetectedAttackAttacker);
	if (!DetectedAttackAttacker.IsSet())
	{
		// The record names one attacker; with no attacker there is nothing the five-second window
		// could still be counting down for.
		DetectedAttackTime = -1.0;
	}
	if (!Enemy.IsSet())
	{
		EnemyLosFailures = 0;
		bEnemyOccluded = false;
		bEnemyLosLatched = false;
	}
}

void FElysiumNpcSenses::ResolveTuning(FElysiumNpc& Npc)
{
	UElysiumGameStateSubsystem* GameState = Npc.World ? Npc.World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;

	FString Report;
	Perception.ResolveFromRulebook(Npc.AuthoredPerception, Npc.AuthoredVision, Npc.AuthoredHearing,
		Rules, Report);
	// Rules.txt RuleData/Npc_Combat_Info.FreeKnowledgeDuration, copied to CAI_Memory on spawn.
	Npc.EnemyMemory.FreeKnowledgeDuration = Rules
		? Rules->Rules().Flt(TEXT("Npc_Combat_Info"), TEXT("FreeKnowledgeDuration"), 0.25f)
		: 0.25;
	if (!Report.IsEmpty() && !bWarnedPerception)
	{
		bWarnedPerception = true;
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s %s"), *Npc.DebugString(), *Report);
	}
}

void FElysiumNpcSenses::StartSoundCursorAtHead(const FElysiumNpc& Npc)
{
	Cursor = Npc.World ? Npc.World->GameSounds().LastSerial() : 0;
}

FVector FElysiumNpcSenses::ViewForward(const FElysiumNpc& Npc)
{
	// Source angles carry the inverse Unreal yaw in this substrate; pitch remains Source pitch,
	// so construct the full forward vector rather than flattening a target above/below the
	// observer into its horizontal ray.
	const float PitchRadians = FMath::DegreesToRadians(static_cast<float>(Npc.Angles.X));
	const float YawRadians = FMath::DegreesToRadians(-static_cast<float>(Npc.Angles.Y));
	return FVector(FMath::Cos(PitchRadians) * FMath::Cos(YawRadians),
		FMath::Cos(PitchRadians) * FMath::Sin(YawRadians), -FMath::Sin(PitchRadians));
}

bool FElysiumNpcSenses::IsInViewCone(const FElysiumNpc& Npc, const FVector& TargetCm,
	float TargetConeScalar)
{
	// `FInViewCone` at 0x103264d0 is a strict 3-D apex test.
	const FVector Forward = ViewForward(Npc);
	const FVector ToTarget = TargetCm - Npc.EyePosition();
	// 0x103265af rejects strictly behind the original eye before shifting the apex.
	if (FVector::DotProduct(Forward, ToTarget) < 0.0) return false;
	const FVector FromApex = ToTarget + Forward * Npc.Senses.ViewConeBodyOffsetCm;
	// 0x1032669c multiplies the COSINE by the target scalar, then compares to the FOV.
	return FVector::DotProduct(Forward, FromApex.GetSafeNormal()) * TargetConeScalar
		>= ElysiumNpcSense::DefaultViewConeDot;
}

bool FElysiumNpcSenses::IsInViewCone(const FElysiumNpc& Npc, const FElysiumEntity& Target,
	float TargetConeScalar)
{
	// Troika `FInViewCone` slot 363 (`0x102b4540`). Null-target is retail arm 1; this overload
	// takes a reference, and Look / the stranger arm never pass a missing entity.
	if (ElysiumNpcSense::IgnoreSenses())
	{
		return false;
	}
	if (ElysiumNpcSense::IgnorePlayer() && Npc.World && Target.Handle == Npc.World->PlayerHandle())
	{
		return false;
	}
	// Arm 4, named seam that currently answers nothing. Slot 293 (`FUN_102c5470`) is
	// `GetFollowerBoss`: resolve `m_hFollowerBoss` (`+0x647c`) and return `boss+0x9c` (the
	// cached `CBaseCombatCharacter*`). If that equals `m_hClosestPlayer` (`+0x628c`),
	// `target+0x98` (Troika self-pointer) is non-null, and `*(+0x6279)` (`m_bInPlayerLOS`) is
	// set, skip the cone. A player does not write `+0x98`, so a Look at the player cannot take
	// this arm. 16a owns the follower handle; `SetPlayerLOS` owns the target's LOS byte.
	return IsInViewCone(Npc, Target.EyePosition(), TargetConeScalar);
}

void FElysiumNpcSenses::Tick(FElysiumNpc& Npc, double Now)
{
	if (Npc.IsInert() || Npc.World == nullptr)
	{
		return;
	}
	if (!Perception.bResolved)
	{
		// A leaf constructed at runtime (an `npc_maker` child) reaches its first think without an
		// Activate of its own having run yet. Resolving here rather than reading zeroes is the
		// same authored data, one think later.
		ResolveTuning(Npc);
	}
	TickSight(Npc, Now);
	GatherEnemyLos(Npc, Now);
	TickHearing(Npc, Now);
}

bool FElysiumNpcSenses::IsVisible(const FElysiumNpc& Npc, const FElysiumEntity& Candidate, double Now)
{
	// Troika `FVisible` `0x102b4630` reads the same two bytes before slot 594. Retail writes
	// `*blocker = 0` when the caller passed one; this port has no blocker out-param.
	if (ElysiumNpcSense::IgnoreSenses())
	{
		return false;
	}
	if (ElysiumNpcSense::IgnorePlayer() && Npc.World && Candidate.Handle == Npc.World->PlayerHandle())
	{
		return false;
	}
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	const bool RangeBypass = (Npc.GetMind().State() == EElysiumNpcState::Combat && !Memory.bEnemyOccluded)
		|| Now < Memory.StealthVisionOverrideUntil;
	const FElysiumPlayer* Player = Npc.World ? Npc.World->FindPlayer() : nullptr;
	const float Scalar = Player && Player->Handle == Candidate.Handle ? Player->Stealth.VisionScalar : 1.f;
	if (!RangeBypass && FVector::Dist(Npc.EyePosition(), Candidate.EyePosition())
		> Npc.Senses.Perception.VisionDistanceCm * Scalar) return false;
	const FElysiumCombatCharacter* Character = Candidate.AsCombatCharacter();
	return (!Character || Npc.CanPerceiveConcealment(*Character))
		&& !Npc.HasDisciplineStatus(TEXT("Dominate_BrainWipe"))
		&& SegmentClear(Npc.World, Npc.EyePosition(), Candidate.EyePosition());
}

void FElysiumNpcSenses::TickSight(FElysiumNpc& Npc, double Now)
{
	FElysiumEntityWorld* World = Npc.World;
	if (World == nullptr)
	{
		return;
	}
	// `SetClosestPlayer`/PVS/LOS is a 2-second performance cache, never the Look gate.  Its
	// cached answer remains for HUD/witness consumers only; actual observations below are fresh.
	FElysiumPlayer* Player = World->FindPlayer();
	if (Memory.PlayerLosNextUpdateTime <= 0.0 || Now >= Memory.PlayerLosNextUpdateTime)
	{
		Memory.PlayerLosNextUpdateTime = Now + ElysiumNpcSense::PlayerLosCadenceSeconds;
		if (Player == nullptr || Player->IsInert())
		{
			Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
			Memory.bPlayerInRange = Memory.bPlayerInOuterBand = Memory.bPlayerInCone = Memory.bPlayerLos = false;
		}
		else
		{
			Memory.ClosestPlayer = Player->Handle;
			Memory.ClosestPlayerDistanceCm = FVector::Dist(Npc.Origin, Player->Origin);
			const float RadiusCm = Perception.VisionDistanceCm * TargetVisionScalar(*Player);
			Memory.bPlayerInRange = Memory.ClosestPlayerDistanceCm <= RadiusCm;
			Memory.bPlayerInOuterBand = Memory.bPlayerInRange
				&& Memory.ClosestPlayerDistanceCm > ElysiumNpcSense::OuterBandFraction * RadiusCm;
			Memory.bPlayerInCone = IsInViewCone(Npc, Player->EyePosition(), TargetConeScalar(*Player));
			const bool bClear = Memory.bPlayerInCone && Memory.bPlayerInRange
				&& (Memory.ClosestPlayerDistanceCm <= ElysiumNpcSense::NearBypassUnits * ElysiumMove::U
					|| SegmentClear(World, Npc.EyePosition(), Player->EyePosition()));
			if (bClear && Memory.ClosestPlayerDistanceCm > ElysiumNpcSense::NearBypassUnits * ElysiumMove::U)
			{
				Memory.PlayerLosLastClearTime = Now;
			}
			Memory.bPlayerLos = bClear || (Memory.bPlayerInCone && Memory.PlayerLosLastClearTime >= 0.0
				&& Now - Memory.PlayerLosLastClearTime <= ElysiumNpcSense::BlockedInConeGraceSeconds);
			ElysiumNpcWitness::OnClosestPlayerUpdated(Npc, *Player, Now);
		}
	}

	SeenThisPass.Reset();
	bSeeUnknownThisPass = false;
	const bool Due[3] = { Now >= NextLookTime[0], Now >= NextLookTime[1], Now >= NextLookTime[2] };
	const double Cadences[3] = { 0.15, 0.25, 0.45 };
	for (int32 Channel = 0; Channel < 3; ++Channel)
	{
		if (Due[Channel])
		{
			NextLookTime[Channel] = Now + Cadences[Channel];
			SeenByChannel[Channel].Reset();
		}
	}
	const float PrefilterCm = 3072.f * ElysiumMove::U;
	for (const TUniquePtr<FElysiumEntity>& CandidatePtr : World->Entities())
	{
		const FElysiumEntity* Candidate = CandidatePtr.Get();
		if (Candidate == nullptr || Candidate->IsInert() || Candidate->Handle == Npc.Handle)
		{
			continue;
		}
		const bool bPlayer = Candidate->Handle == World->PlayerHandle();
		const bool bNpc = Candidate->AsNpc() != nullptr;
		const FString Classname = Candidate->Def ? Candidate->Def->Classname : FString();
		// QuerySeeEntity slot 468 (`0x102b38b0`): the two sense-off bytes, then any player /
		// non-players only at D_HT/D_FR. Frenzy-friend (`m_bfNPCFrenziedFlags & 0x800` vs
		// `m_hFriendPlayer`) is 16c; the seam answers "not a friend".
		if (ElysiumNpcSense::IgnoreSenses())
		{
			continue;
		}
		if (ElysiumNpcSense::IgnorePlayer() && bPlayer)
		{
			continue;
		}
		const EElysiumRelationship Relation = Npc.Relationships.Resolve(Candidate->Handle, Classname);
		if (!bPlayer && Relation != EElysiumRelationship::Hate && Relation != EElysiumRelationship::Fear)
		{
			continue; // objects need an explicit D_HT/D_FR relation; neutral scenery is not a look target
		}
		const float DistanceCm = FVector::Dist(Npc.Origin, Candidate->Origin);
		if (DistanceCm > PrefilterCm)
		{
			continue;
		}
		const int32 Channel = bPlayer ? 0 : (bNpc ? 1 : 2);
		if (!Due[Channel]) continue;
		const float Scalar = bPlayer && Player ? TargetVisionScalar(*Player) : 1.f;
		const float ConeScalar = bPlayer && Player ? TargetConeScalar(*Player) : 1.f;
		const bool bRangeBypass = Npc.GetMind().State() == EElysiumNpcState::Combat && !Memory.bEnemyOccluded;
		const bool bDamageOverride = Now < Memory.StealthVisionOverrideUntil;
		const float EyeDistance = FVector::Dist(Npc.EyePosition(), Candidate->EyePosition());
		if (!bRangeBypass && !bDamageOverride && EyeDistance > Perception.VisionDistanceCm * Scalar)
		{
			continue;
		}
		if (!IsInViewCone(Npc, *Candidate, ConeScalar)
			|| !IsVisible(Npc, *Candidate, Now))
		{
			continue;
		}
		SeenByChannel[Channel].Add(Candidate->Handle);
		const bool bOuterBand = !bRangeBypass && !bDamageOverride
			&& !Npc.NpcFlags.Has(EElysiumNpcFlag2::NO_UNKNOWN_VISION)
			&& EyeDistance > ElysiumNpcSense::OuterBandFraction * Perception.VisionDistanceCm * Scalar;
		{
			FElysiumNpcSightingDebug Sighting;
			Sighting.EffectiveRadiusCm = Perception.VisionDistanceCm * Scalar;
			Sighting.TargetConeScalar = ConeScalar;
			Sighting.bOuterBand = bOuterBand;
			Sighting.bRangeBypass = bRangeBypass;
			Sighting.bDamageOverride = bDamageOverride;
			ElysiumNpcDebugLogging::Sighting(Npc, *Candidate, Sighting);
		}
		if (bOuterBand && ElysiumNpcCond::ShouldInvestigate(Npc, *Candidate, false))
		{
			const bool Eligible = bPlayer && Player && Player->IsInStealthPosture();
			if (!Eligible)
			{
				Npc.NpcFlags.Set(EElysiumNpcFlag::ATTACK_UNKNOWN);
			}
			else
			{
			bSeeUnknownThisPass = true;
			if (Memory.BestSeeUnknown == Candidate->Handle) continue;
			Memory.BestSeeUnknown = Candidate->Handle;
			++Npc.EnemySightings;
			if (Memory.LastSeeUnknown == Candidate->Handle)
			{
				++Memory.SeeUnknownRepeatSightings;
				Memory.bIgnoreUnknown = Memory.bMadeInitialUnknownResponse = false;
				Npc.NpcFlags.Clear(EElysiumNpcFlag::IGNORE_UNKNOWN);
				Npc.NpcFlags.Clear(EElysiumNpcFlag::MADE_INITIAL_RESPONSE);
			}
			else
			{
				Memory.LastSeeUnknown = Candidate->Handle;
				Memory.LastSeeUnknownPosition = Candidate->Origin;
				Memory.SeeUnknownRepeatSightings = 0;
				Memory.SeeUnknownRunTimer = Now + ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(10.f, 20.f);
				Memory.SeeUnknownStartTimer = Now + ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(5.f, 10.f);
				Memory.bIgnoreUnknown = Memory.bMadeInitialUnknownResponse = false;
				Npc.NpcFlags.Clear(EElysiumNpcFlag::IGNORE_UNKNOWN);
				Npc.NpcFlags.Clear(EElysiumNpcFlag::MADE_INITIAL_RESPONSE);
				Npc.NpcFlags.Clear(EElysiumNpcFlag::LOOKED_AT_UNKNOWN);
				Npc.FireOutput(FName(TEXT("OnUnknownVisionPlayer")), Candidate->Handle);
			}
			continue;
			}
		}
		if (Memory.BestSeeUnknown == Candidate->Handle)
		{
			Memory.BestSeeUnknown = FElysiumEntityHandle::Invalid();
			Memory.bIgnoreUnknown = Memory.bMadeInitialUnknownResponse = false;
			Npc.NpcFlags.Clear(EElysiumNpcFlag::IGNORE_UNKNOWN);
			Npc.NpcFlags.Clear(EElysiumNpcFlag::MADE_INITIAL_RESPONSE);
			Npc.NpcFlags.Clear(EElysiumNpcFlag::LOOKED_AT_UNKNOWN);
		}
	}
	for (const auto& Channel : SeenByChannel) SeenThisPass.Append(Channel);
	// Troika OnLooked runs before base GatherConditions clears/rebuilds the SEE family.
	if (Npc.Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy)) ++Npc.EnemySightings;
}

void FElysiumNpcSenses::GatherEnemyLos(FElysiumNpc& Npc, double Now)
{
	FElysiumEntityWorld* World = Npc.World;
	if (World == nullptr)
	{
		return;
	}
	if (!Memory.Enemy.IsSet())
	{
		// No committed enemy is the state every NPC is in until enemy selection lands. The
		// debounce and its edge latch belong to one acquisition episode, so they clear with it.
		Memory.EnemyLosFailures = 0;
		Memory.bEnemyOccluded = false;
		Memory.bEnemyLosLatched = false;
		return;
	}
	const FElysiumEntity* Enemy = World->Resolve(Memory.Enemy);
	if (Enemy == nullptr || Enemy->IsInert())
	{
		// A dead or hidden enemy is enemy SELECTION's transaction (`ENEMY_DEAD` / `LOST_ENEMY`),
		// not this debounce's. Nothing is inferred here.
		return;
	}

	const bool bIsPlayer = Memory.Enemy == World->PlayerHandle();
	// The committed enemy is tracked, not discovered, so this is the raw LOS query the recovered
	// body runs — no cone and no range gate, which are the ADMISSION stage's rules.
	const bool bClear = IsVisible(Npc, *Enemy, Now);

	if (bClear)
	{
		Memory.EnemyLosFailures = 0;
		Memory.bEnemyOccluded = false;
		Memory.EnemyLastLosTime = Now;
		if (!Memory.bEnemyLosLatched)
		{
			Memory.bEnemyLosLatched = true;
			static const FName OnFoundEnemy(TEXT("OnFoundEnemy"));
			static const FName OnFoundPlayer(TEXT("OnFoundPlayer"));
			Npc.FireOutput(OnFoundEnemy, Memory.Enemy);
			if (bIsPlayer)
			{
				// This edge publishes detection. OnLooked/unknown attention own EnemySightings.
				Npc.FireOutput(OnFoundPlayer, Memory.Enemy);
			}
			Npc.RecordScheduleEvent(FString::Printf(TEXT("OnFoundEnemy%s: %s"),
				bIsPlayer ? TEXT(" + OnFoundPlayer") : TEXT(""), *Memory.Enemy.ToString()));
		}
		return;
	}

	if (Memory.EnemyLosFailures < ElysiumNpcSense::EnemyLosFailureLimit)
	{
		++Memory.EnemyLosFailures;
	}
	if (Memory.EnemyLosFailures < ElysiumNpcSense::EnemyLosFailureLimit)
	{
		return;   // below ten: HAVE_ENEMY_LOS is retained, which is why nothing fires here
	}
	if (Memory.bEnemyOccluded)
	{
		return;   // already flipped; the eleventh failure is not a second edge
	}
	Memory.bEnemyOccluded = true;
	if (Memory.bEnemyLosLatched)
	{
		Memory.bEnemyLosLatched = false;
		static const FName OnLostEnemyLos(TEXT("OnLostEnemyLOS"));
		static const FName OnLostPlayerLos(TEXT("OnLostPlayerLOS"));
		Npc.FireOutput(OnLostEnemyLos, Memory.Enemy);
		if (bIsPlayer)
		{
			Npc.FireOutput(OnLostPlayerLos, Memory.Enemy);
		}
		// `OnLostPlayer`/`OnLostEnemy` are deliberately NOT fired here: they belong to enemy
		// selection's eluded/went-null transaction, and losing sight neither clears the enemy
		// nor forgets the player.
		Npc.RecordScheduleEvent(FString::Printf(TEXT("OnLostEnemyLOS%s after %d failed checks"),
			bIsPlayer ? TEXT(" + OnLostPlayerLOS") : TEXT(""), Memory.EnemyLosFailures));
	}
}

void FElysiumNpcSenses::TickHearing(FElysiumNpc& Npc, double Now)
{
	HeardConditions.Reset();
	FElysiumEntityWorld* World = Npc.World;
	if (!World) return;
	const auto& Bus = World->GameSounds();
	const auto Pending = Bus.EventsSince(Cursor);
	if (!Pending.IsEmpty()) Cursor = Pending.Last().Serial;
	UElysiumGameStateSubsystem* State = World->GetGameState();
	UElysiumRulebookSubsystem* Rules = State ? State->Rulebook() : nullptr;
	TSet<EElysiumNpcCond> Recorded;
	const FString Class = Npc.Def ? Npc.Def->Classname.ToLower() : FString();
	// Slot 473: Troika, human and cop 0x81f; animal 0x1035f540, camera
	// 0x103692a0, pedestrian 0x103a28f0, zombie 0x103df260.
	uint32 Interests = 0x81f;
	if (Class == TEXT("npc_vanimal") || Class == TEXT("npc_vdog") || Class == TEXT("npc_vrat")
		|| Class == TEXT("npc_vscurrying")) Interests = 0x1f;
	else if (Class.StartsWith(TEXT("npc_vcamera"))) Interests = 0;
	else if (Class == TEXT("npc_vpedestrian")) Interests = 0x81d;
	else if (Class == TEXT("npc_vzombie")) Interests = 0x17;
	for (const FElysiumGameSoundEvent& Event : Pending)
	{
		if ((Event.TypeMask & Interests) == 0) continue;
		if (Event.Time <= LastListenTime || Event.ExpireTime < Now || Event.Source == Npc.Handle) continue;
		const FElysiumEntity* Owner = Event.Source.IsSet() ? World->Resolve(Event.Source) : nullptr;
		if (Event.Source.IsSet() && (!Owner || Owner->IsInert())) continue;
		// QueryHearSound slot 467 (`0x102b35b0`): the same two bytes as QuerySeeEntity. Frenzy
		// friend is 16c.
		if (ElysiumNpcSense::IgnoreSenses()) continue;
		if (ElysiumNpcSense::IgnorePlayer() && Owner && Owner->Handle == World->PlayerHandle()) continue;
		if (Owner)
		{
			const FElysiumCombatCharacter* Character = Owner->AsCombatCharacter();
			if (!Character || !Npc.CanPerceiveConcealment(*Character)) continue;
		}
		if (Event.TypeMask == ElysiumGameSounds::Player && Event.Source == World->PlayerHandle()
			&& Rules && Rules->StealthKillRules().InDeafZone(*World->FindPlayer(), Npc)) continue;
		const float BaseRadius = Event.UnadjustedRadiusCm;
		float Radius = FMath::Max(0.f, BaseRadius * Perception.HearingScalar - Event.StealthHearingReductionCm);
		const float Distance = FVector::Dist(Npc.EyePosition(), Event.Position);
		if (Distance > Radius) continue;
		if (Event.bOccludable && !SegmentClear(World, Event.Position, Npc.EyePosition())) continue;
		if (Npc.NpcFlags.Has(EElysiumNpcFlag::COWERING) || Npc.NpcFlags.Has(EElysiumNpcFlag::SLEEPING))
		{
			Radius = FMath::Max(0.f, BaseRadius * Perception.HearingScalar * 0.25f - Event.StealthHearingReductionCm);
			if (Distance > Radius) continue;
		}
		// OnListened 0x1026a5e0 switches on the exact raw type; a combination is not two sounds.
		EElysiumNpcCond Condition = EElysiumNpcCond::None;
		FElysiumGameSoundEvent* Record = nullptr;
		switch (Event.TypeMask)
		{
		case ElysiumGameSounds::Combat: Condition = EElysiumNpcCond::HearCombat; Record = &Memory.LastSoundCombat; break;
		case ElysiumGameSounds::World: Condition = EElysiumNpcCond::HearWorld; Record = &Memory.LastSoundWorld; break;
		case ElysiumGameSounds::Player: Condition = EElysiumNpcCond::HearPlayer; Record = &Memory.LastSoundPlayer; break;
		case ElysiumGameSounds::Danger: Condition = EElysiumNpcCond::HearDanger; Record = &Memory.LastSoundDanger; break;
		case ElysiumGameSounds::BulletImpact: Condition = EElysiumNpcCond::HearBulletImpact; Record = &Memory.LastSoundBulletImpact; break;
		case ElysiumGameSounds::Flinch: Condition = EElysiumNpcCond::HearFlinch; Record = &Memory.LastSoundFlinch; break;
		case ElysiumGameSounds::PhysicsDanger: Condition = EElysiumNpcCond::HearPhysicsDanger; Record = &Memory.LastSoundPhysicsDanger; break;
		case ElysiumGameSounds::Thumper: Condition = EElysiumNpcCond::HearThumper; break;
		case ElysiumGameSounds::Bugbait: Condition = EElysiumNpcCond::HearBugbait; break;
		case ElysiumGameSounds::Carcass: Condition = EElysiumNpcCond::Smell; break;
		default: continue;
		}
		if (Record)
		{
			// CAI_Senses::GetClosestSound 0x103105d0: prefer my enemy, else nearest in this Listen.
			if (!Recorded.Contains(Condition) || (Record->Source != Memory.Enemy
				&& (Event.Source == Memory.Enemy || FVector::DistSquared(Npc.EyePosition(), Event.Position)
					< FVector::DistSquared(Npc.EyePosition(), Record->Position)))) *Record = Event;
			Recorded.Add(Condition);
		}
		Memory.LastHeardSource = Event.Source;
		Memory.LastHeardPosition = Event.Position;
		Memory.LastHeardCategory = Event.Category.ToString();
		Memory.LastHeardTime = Event.Time;
		const bool Flinch = Condition == EElysiumNpcCond::HearFlinch;
		const double At = Now + ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(
			Flinch ? 0.f : 0.2f, Flinch ? 0.5f : 0.9f);
		// The eight-entry retail list (0x102cc6c0) min-updates a duplicate deadline (0x102cc590).
		// The capacity check precedes even the duplicate lookup.
		if (PendingSounds.Num() < 8)
		{
			if (FPendingSound* Existing = PendingSounds.FindByPredicate([Condition](const FPendingSound& Item)
				{ return Item.Condition == Condition; })) Existing->PromoteAt = FMath::Min(Existing->PromoteAt, At);
			else PendingSounds.Add({ Condition, At });
		}
	}
	LastListenTime = Now;
	for (int32 Index = PendingSounds.Num() - 1; Index >= 0; --Index)
	{
		if (Now >= PendingSounds[Index].PromoteAt)
		{
			HeardConditions.Set(PendingSounds[Index].Condition);
			PendingSounds.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}
	// Outputs follow delayed promotion, once per condition family per Listen, activator = self.
	if (HeardConditions.Has(EElysiumNpcCond::HearWorld)) Npc.FireOutput(FName(TEXT("OnHearWorld")), Npc.Handle);
	if (HeardConditions.Has(EElysiumNpcCond::HearPlayer)) Npc.FireOutput(FName(TEXT("OnHearPlayer")), Npc.Handle);
	if (HeardConditions.Has(EElysiumNpcCond::HearCombat) || HeardConditions.Has(EElysiumNpcCond::HearBulletImpact)
		|| HeardConditions.Has(EElysiumNpcCond::HearDanger)) Npc.FireOutput(FName(TEXT("OnHearCombat")), Npc.Handle);
	for (const auto Pair : { TPair<EElysiumNpcCond, const FElysiumGameSoundEvent*>(EElysiumNpcCond::HearCombat, &Memory.LastSoundCombat),
		TPair<EElysiumNpcCond, const FElysiumGameSoundEvent*>(EElysiumNpcCond::HearBulletImpact, &Memory.LastSoundBulletImpact) })
	{
		if (HeardConditions.Has(Pair.Key)) ExtendVisionOverride(Npc, Pair.Value->Source, Now, 1.0);
	}
}

void FElysiumNpcSenses::ExtendVisionOverride(FElysiumNpc& Npc, FElysiumEntityHandle Source,
	double Now, double Duration)
{
	const FElysiumEntity* Owner = Npc.World ? Npc.World->Resolve(Source) : nullptr;
	if (!Owner) return;
	const FElysiumNpc* OtherNpc = Owner->AsNpc();
	// 0x1028e8b0: any live owner when no enemy, my enemy, or an NPC sharing my enemy.
	if (!Memory.Enemy.IsSet() || Source == Memory.Enemy
		|| (OtherNpc && OtherNpc->Senses.Memory.Enemy == Memory.Enemy))
		Memory.StealthVisionOverrideUntil = FMath::Max(Memory.StealthVisionOverrideUntil, Now + Duration);
}

void FElysiumNpcSenses::CommitBestSound(const FElysiumNpcConditions& Conditions)
{
	const EElysiumNpcCond Priority[] = { EElysiumNpcCond::HearCombat, EElysiumNpcCond::HearBulletImpact,
		EElysiumNpcCond::HearFlinch, EElysiumNpcCond::HearPlayer, EElysiumNpcCond::HearDanger,
		EElysiumNpcCond::HearPhysicsDanger, EElysiumNpcCond::HearWorld };
	const FElysiumGameSoundEvent* Records[] = { &Memory.LastSoundCombat, &Memory.LastSoundBulletImpact,
		&Memory.LastSoundFlinch, &Memory.LastSoundPlayer, &Memory.LastSoundDanger,
		&Memory.LastSoundPhysicsDanger, &Memory.LastSoundWorld };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Priority); ++Index)
	{
		if (Conditions.Has(Priority[Index]))
		{
			Memory.BestSound = *Records[Index];
			// `*(param_1 + 0x5b78) = *(param_1 + 0x60b0)`: the winner's owner handle, copied out of
			// the record before the sticky mirror. The sweep's `SEE_SOUND_SOURCE` tail is its only
			// reader.
			Memory.BestSoundSource = Memory.BestSound.Source;
			return;
		}
	}
}
void FElysiumNpcSenses::Serialize(FElysiumSaveArchive& Ar, FElysiumNpc& Npc)
{
	Memory.Serialize(Ar);
	Ar << LastListenTime;
	int32 Count = PendingSounds.Num();
	Ar << Count;
	if (Ar.IsLoading()) PendingSounds.SetNum(FMath::Clamp(Count, 0, 8));
	for (FPendingSound& Sound : PendingSounds)
	{
		uint8 Condition = static_cast<uint8>(Sound.Condition);
		Ar << Condition;
		Ar << Sound.PromoteAt;
		if (Ar.IsLoading()) Sound.Condition = static_cast<EElysiumNpcCond>(Condition);
	}
	if (Ar.IsLoading())
	{
		HeardConditions.Reset();
		SeenThisPass.Reset();
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			NextLookTime[Channel] = -1.0;
			SeenByChannel[Channel].Reset();
		}
		if (Npc.World)
		{
			Memory.Rebase(*Npc.World);
		}
		else
		{
			Memory.Reset();
		}
		// The bus is session state: a restored NPC starts at the live head rather than replaying
		// a retention window it was not present for.
		StartSoundCursorAtHead(Npc);
		// The tuning is authored data, re-derived from the restored keyfields rather than saved.
		Perception = FElysiumNpcPerception();
		ResolveTuning(Npc);
	}
}
