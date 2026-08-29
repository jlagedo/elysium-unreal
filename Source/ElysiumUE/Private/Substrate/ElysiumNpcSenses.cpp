#include "Substrate/ElysiumNpcSenses.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

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
	// Version 19 appends the repeated-damage window and the eluded marker at the END of the memory
	// record, which is itself the end of the NPC leaf. Additive: an `NpcSenses` payload restores an
	// NPC with no open damage window and an un-eluded enemy, which is the default state anyway.
	uint8 Eluded = bEnemyEluded ? 1 : 0;
	if (Ar.Version() >= FElysiumSaveVersion::NpcCognition)
	{
		Ar << RepeatedDamageWindowStart;
		Ar << RepeatedDamageAccumulated;
		Ar << Eluded;
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
		bEnemyEluded = Ar.Version() >= FElysiumSaveVersion::NpcCognition && Eluded != 0;
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
	LastDamageAttacker = World.RebaseSavedHandle(LastDamageAttacker);
	ClosestPlayer = World.RebaseSavedHandle(ClosestPlayer);
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
		// The eluded marker names one committed enemy's record; with no enemy there is no record.
		bEnemyEluded = false;
	}
}

void FElysiumNpcSenses::ResolveTuning(FElysiumNpc& Npc)
{
	UElysiumGameStateSubsystem* GameState = Npc.World ? Npc.World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;

	FString Report;
	Perception.ResolveFromRulebook(Npc.AuthoredPerception, Npc.AuthoredVision, Npc.AuthoredHearing,
		Rules, Report);
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

bool FElysiumNpcSenses::IsInViewCone(const FElysiumNpc& Npc, const FVector& TargetCm,
	float TargetConeScalar)
{
	// The entity's `Angles.Y` is the negated Unreal yaw the motor is driven with, which is the one
	// place this frame conversion lives on a character.
	const float YawRadians = FMath::DegreesToRadians(-static_cast<float>(Npc.Angles.Y));
	const FVector Forward(FMath::Cos(YawRadians), FMath::Sin(YawRadians), 0.0);
	FVector ToTarget = TargetCm - Npc.Origin;
	ToTarget.Z = 0.0;
	if (ToTarget.IsNearlyZero())
	{
		return true;   // standing on the observer: the angle is undefined, not "behind"
	}
	ToTarget.Normalize();
	// The target's `m_flStealthVisionCone` multiplies the observer's own threshold, inside this
	// test, exactly where `FInViewCone` applies it. A scalar below 1 LOWERS the dot the target has
	// to clear, which widens the cone — the table's own direction, and the reason it is a multiply
	// on the threshold rather than on the angle.
	const float Threshold = ElysiumNpcSense::DefaultViewConeDot * TargetConeScalar;
	// KINDA_SMALL_NUMBER tolerance: a boundary target's dot and the threshold are each built from
	// FMath::Cos(FMath::DegreesToRadians(...)) in float, so an angle that is exactly on the cone
	// edge mathematically can land a few ULPs under the threshold rather than on it.
	return static_cast<float>(FVector::DotProduct(Forward, ToTarget)) >= Threshold - UE_KINDA_SMALL_NUMBER;
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

void FElysiumNpcSenses::TickSight(FElysiumNpc& Npc, double Now)
{
	FElysiumEntityWorld* World = Npc.World;
	if (World == nullptr)
	{
		return;
	}
	if (Memory.PlayerLosNextUpdateTime > 0.0 && Now < Memory.PlayerLosNextUpdateTime)
	{
		return;   // inside the 2 s cadence: the cache is what a consumer reads
	}
	Memory.PlayerLosNextUpdateTime = Now + ElysiumNpcSense::PlayerLosCadenceSeconds;

	// `SetClosestPlayer` (`0x10293a80`): the nearest present player by Euclidean distance. This
	// runtime has exactly one. The cache is not hostility admission and fires no output.
	FElysiumPlayer* Player = World->FindPlayer();
	if (Player == nullptr || Player->IsInert())
	{
		// The retail no-player branch initialises its cached bytes true with current timestamps.
		// That is a sentinel, explicitly not evidence of detection (`docs/vtmb/stealth.md`), so
		// the cache clears here rather than reading as "the player has been seen".
		Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
		Memory.ClosestPlayerDistanceCm = 0.f;
		Memory.bPlayerInRange = false;
		Memory.bPlayerInOuterBand = false;
		Memory.bPlayerInCone = false;
		Memory.bPlayerLos = false;
		return;
	}

	const bool bWasVisible = Memory.bPlayerLos;
	Memory.ClosestPlayer = Player->Handle;
	const float DistanceCm = static_cast<float>(FVector::Dist(Npc.Origin, Player->Origin));
	Memory.ClosestPlayerDistanceCm = DistanceCm;

	// Observer range admission (`0x102b4760`): the observer's own effective distance scaled by the
	// TARGET's vision scalar, rejected before any relationship work.
	const float RadiusCm = Perception.VisionDistanceCm * TargetVisionScalar(*Player);
	Memory.bPlayerInRange = DistanceCm <= RadiusCm;
	Memory.bPlayerInOuterBand = Memory.bPlayerInRange
		&& DistanceCm > ElysiumNpcSense::OuterBandFraction * RadiusCm;
	Memory.bPlayerInCone = IsInViewCone(Npc, Player->Origin, TargetConeScalar(*Player));

	if (!Memory.bPlayerInRange || !Memory.bPlayerInCone)
	{
		Memory.bPlayerLos = false;
	}
	else if (DistanceCm <= ElysiumNpcSense::NearBypassUnits * ElysiumMove::U)
	{
		// In cone and inside 512 units: LOS true with NO trace. The last-clear time is
		// deliberately not touched — the recovered body updates it from the far trace only, and
		// the eight-second grace below is written against that clock.
		Memory.bPlayerLos = true;
	}
	else if (SegmentClear(World, Npc.EyePosition(), Player->EyePosition()))
	{
		Memory.bPlayerLos = true;
		Memory.PlayerLosLastClearTime = Now;
	}
	else
	{
		// Blocked while the player is still in cone: sight is preserved for eight seconds past
		// the last clear far trace, and only then drops.
		Memory.bPlayerLos = Memory.PlayerLosLastClearTime >= 0.0
			&& (Now - Memory.PlayerLosLastClearTime) <= ElysiumNpcSense::BlockedInConeGraceSeconds;
	}

	// The closest-player special case.
	// "the closest-player special case opens the Nosferatu window for five seconds." The setter is
	// called from `SetClosestPlayer`'s own body, which is this function, so the CALLER stays where
	// retail's is and the rule itself lives with the rest of the witness transaction
	// (`Substrate/ElysiumNpcWitness.h`). It runs after the cache above is committed because the
	// proximity term it reads is `bPlayerInRange`.
	ElysiumNpcWitness::OnClosestPlayerUpdated(Npc, *Player, Now);

	// The HUD observability offer (`docs/vtmb/stealth.md` -> "HUD observability is not authority").
	// `SetClosestPlayer` feeds this surface, and the player-side update is what filters and ranks:
	// this call only OFFERS, and the player think commits. Nothing here reads it back, so no
	// gameplay decision can come to depend on presentation state.
	//
	// The relationship filter is the recovered "nearest ELIGIBLE HOSTILE observer": an NPC that
	// hates the player, or one that has already committed to it as its enemy. A neutral bystander
	// standing closer must not take the readout off a guard that is actually hunting.
	const bool bHostile =
		Npc.Relationships.Resolve(Player->Handle, TEXT("player")) == EElysiumRelationship::Hate
		|| Memory.Enemy == Player->Handle;
	if (bHostile)
	{
		Player->OfferStealthObserver(Npc.Handle, DistanceCm, RadiusCm,
			/*bDetected*/ Memory.bPlayerLos && Memory.bPlayerInRange, Now);
	}

	if (Memory.bPlayerLos != bWasVisible)
	{
		Npc.RecordScheduleEvent(FString::Printf(
			TEXT("sight: player %s at %.0fcm (cone %s, band %s)"),
			Memory.bPlayerLos ? TEXT("visible") : TEXT("not visible"), DistanceCm,
			Memory.bPlayerInCone ? TEXT("in") : TEXT("out"),
			Memory.bPlayerInOuterBand ? TEXT("outer") : TEXT("inner")));
	}
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
	const bool bClear = SegmentClear(World, Npc.EyePosition(), Enemy->EyePosition());

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
				// `m_iEnemySightings` (+0x60a8) — the alert-lookaround chance's own producer, and
				// the one place it has: retail counts acquisition EPISODES, and the latch above is
				// exactly what makes this branch one episode. `SelectIdleSchedule` reads the count
				// through `min(30, (sightings+2)*5)`.
				++Npc.EnemySightings;
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
	FElysiumEntityWorld* World = Npc.World;
	if (World == nullptr)
	{
		return;
	}
	const FElysiumGameSoundBus& Bus = World->GameSounds();
	TArrayView<const FElysiumGameSoundEvent> Pending = Bus.EventsSince(Cursor);
	if (Pending.IsEmpty())
	{
		return;
	}
	// The cursor advances over the whole window whether or not an event was admitted: a stimulus
	// this NPC could not hear has still been considered, and re-considering it next think would
	// be the double-consume the serial cursor exists to prevent.
	Cursor = Pending.Last().Serial;

	const FElysiumEntityHandle PlayerHandle = World->PlayerHandle();
	static const FName OnHearCombat(TEXT("OnHearCombat"));
	static const FName OnHearPlayer(TEXT("OnHearPlayer"));
	static const FName OnHearWorld(TEXT("OnHearWorld"));

	for (const FElysiumGameSoundEvent& Event : Pending)
	{
		if (Event.Source.IsSet() && Event.Source == Npc.Handle)
		{
			continue;   // an NPC does not hear itself
		}
		// The observer's authored hearing value scales the reach the bus already resolved (the
		// authored radius, with the emitter's stealth reduction already subtracted at insertion).
		const float RadiusCm = Event.RadiusCm * Perception.HearingScalar;
		if (RadiusCm <= 0.f
			|| FVector::DistSquared(Npc.Origin, Event.Position) > static_cast<double>(RadiusCm) * RadiusCm)
		{
			continue;
		}
		if (Event.bOccludable && !SegmentClear(World, Event.Position, Npc.EyePosition()))
		{
			// CHOSEN, NOT RECOVERED: an occluded occludable stimulus is DROPPED. The table's
			// `OccludedVolumeLevels` block says which levels a wall stops, but the attenuation it
			// applies to the ones it does not stop outright is unrecovered — so the binary answer
			// the table's own name gives is taken rather than an invented falloff curve. A
			// non-occludable level (the loud family, gunshots included) never reaches this test.
			continue;
		}

		Memory.LastHeardSource = Event.Source;
		Memory.LastHeardPosition = Event.Position;
		Memory.LastHeardCategory = Event.Category.ToString();
		Memory.LastHeardTime = Event.Time;

		const FString Folded = Memory.LastHeardCategory.ToLower();
		const bool bFromPlayer = Event.Source.IsSet() && PlayerHandle.IsSet()
			&& Event.Source == PlayerHandle;
		FName Output = OnHearWorld;
		if (ElysiumNpcCond::IsCombatSoundCategory(Folded))
		{
			Output = OnHearCombat;
		}
		else if (bFromPlayer)
		{
			Output = OnHearPlayer;
		}
		// One output per accepted stimulus, and one accepted stimulus per emission: the bus's
		// serial cursor is the rate limit, and its retention window bounds the backlog a late
		// listener can be handed. There is no recovered re-fire suppression to reproduce.
		Npc.FireOutput(Output, Event.Source);
		Npc.RecordScheduleEvent(FString::Printf(TEXT("heard %s at %.0fcm -> %s"),
			*Memory.LastHeardCategory,
			static_cast<float>(FVector::Dist(Npc.Origin, Event.Position)), *Output.ToString()));
	}
}

void FElysiumNpcSenses::Serialize(FElysiumSaveArchive& Ar, FElysiumNpc& Npc)
{
	Memory.Serialize(Ar);
	if (Ar.IsLoading())
	{
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
