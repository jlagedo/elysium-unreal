#include "Substrate/ElysiumNpcSenses.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSessionSubsystem.h"
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
#include "Substrate/ElysiumNpcSightTrace.h"
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

	// `SetPlayerLOS 0x10291610`'s mask (no MONSTER: no character is listed), and the `FVisible` mask
	// (`0x2804091`) every sight caller in the kernel closure passes, which the sense pass's own
	// `COND_SEE_PLAYER` reconstruction stands in for.
	constexpr int32 PlayerLosMask = 0x4091;
	constexpr int32 SightSeeMask = 0x2804091;

	// The looker's eye to the target's eye under a retail mask, through the one sight trace
	// (`ElysiumNpcSight::Visible`, retail's `CBaseEntity::FVisible` filter: NPCs are transparent, the
	// player and solid props block, a hit on the target is clear). No embodiment is a headless run
	// and reads clear, for the reason above.
	bool SightClear(const FElysiumEntityWorld* World, const FElysiumEntity& Looker,
		const FElysiumEntity& Target, int32 RetailMask)
	{
		const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (Embodiment == nullptr)
		{
			return true;
		}
		ElysiumNpcSight::FVisibleQuery Query;
		Query.EyeCm = Looker.EyePosition();
		Query.TargetCm = Target.EyePosition();
		Query.Mask = RetailMask;
		Query.Looker = Looker.Handle;
		Query.Target = Target.Handle;
		Query.World = World;
		return ElysiumNpcSight::Visible(*Embodiment, Query, nullptr);
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

// Twenty-seven of the words this block used to write are retail `SAVE` rows the generated datamap
// walk now carries under their own names -- `m_hEnemy`, `m_hLastSeen*Ent`, `m_hClosestPlayer`,
// the see-unknown timers, the player LOS/PVS stamps, the repeated-damage window. What is left is
// either a word retail has no row for at all, or a `FIELD_EMBEDDED` row: retail's own datamap
// points at a second `datamap_t` and recurses into it, and the eight sound records below are
// exactly that shape (`m_LastSoundCombat +0x6160` and its kin, which `gen_kernel_bindings.py`
// records as `SAVE_UNBOUND` for the same reason). So a nested serializer here is not a duplicate
// of the walk -- it is the port's spelling of the recursion.
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
		Ar.Time(Sound.Time); // 0x10265ed0 embedded sound stamp, map-clock domain
		Ar.Time(Sound.ExpireTime, EElysiumTimePolicy::MinusOne); // CSound +0x10 TIME mode2, 0x101b9840/0x101b9860
		Ar << Sound.Serial;
		uint8 Occludable = Sound.bOccludable ? 1 : 0;
		Ar << Occludable;
		if (Ar.IsLoading()) { Sound.bOccludable = Occludable != 0; }
	};
	Ar << LastHeardSource;
	Ar << LastHeardPosition;
	Ar << LastHeardCategory;
	Ar.Time(LastHeardTime, EElysiumTimePolicy::MinusOne); // 0x10310710 port memory, map-clock domain
	uint8 InRange = bPlayerInRange ? 1 : 0;
	uint8 OuterBand = bPlayerInOuterBand ? 1 : 0;
	uint8 InCone = bPlayerInCone ? 1 : 0;
	uint8 PlayerVisible = bPlayerVisible ? 1 : 0;
	Ar << InRange;
	Ar << OuterBand;
	Ar << InCone;
	Ar << PlayerVisible;
	SerializeSound(LastSoundCombat);
	SerializeSound(LastSoundBulletImpact);
	SerializeSound(LastSoundFlinch);
	SerializeSound(LastSoundPlayer);
	SerializeSound(LastSoundDanger);
	SerializeSound(LastSoundPhysicsDanger);
	SerializeSound(LastSoundWorld);
	SerializeSound(BestSound);
	Ar << DetectedAttackAttacker;
	Ar.Time(DetectedAttackTime, EElysiumTimePolicy::MinusOne); // 0x10265ed0 port attack memory
	if (Ar.IsLoading())
	{
		bPlayerInRange = InRange != 0;
		bPlayerInOuterBand = OuterBand != 0;
		bPlayerInCone = InCone != 0;
		bPlayerVisible = PlayerVisible != 0;
	}
}

// The load-side half (slot 130) of the sensory memory, and the whole of what the datamap walk
// cannot do. Re-stamping is idempotent -- it writes the live epoch onto a still-valid index -- so
// a word the walk already re-stamped costs nothing by passing through again, and keeping every
// handle on one path is what lets each dependent clear sit beside the handle it depends on. The
// handles that genuinely have no registered row, and would otherwise carry a dead epoch, are
// `LastHeardSource`, the eight embedded sound `Source`s and `DetectedAttackAttacker`.
void FElysiumNpcBaseMemory::Reset()
{
	*this = FElysiumNpcBaseMemory();
}

// The base layer's memory words are datamap rows the registry's save walk carries; the sighting
// times and the damage time beside them are this port's own, carried here.
void FElysiumNpcBaseMemory::Serialize(FElysiumSaveArchive& Ar)
{
	for (int32 i = 0; i < static_cast<int32>(ESeen::Count); ++i)
	{
		Ar.Time(LastSeenTime[i], EElysiumTimePolicy::MinusOne); // 0x102df090 port observed-actor stamp, map-clock domain
	}
}

void FElysiumNpcBaseMemory::Rebase(const FElysiumEntityWorld& World)
{
	// Every remembered handle goes through the one rebase path. A handle that no longer resolves
	// becomes invalid rather than pointing at whichever entity now occupies its slot.
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
	BestSoundSource = World.RebaseSavedHandle(BestSoundSource);
	LastDamageAttacker = World.RebaseSavedHandle(LastDamageAttacker);
}

void FElysiumNpcMemory::Rebase(const FElysiumEntityWorld& World, const FElysiumNpcBaseMemory& Base)
{
	// A value another build wrote is refused rather than trusted (see the base half).
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
}

void FElysiumNpcSenses::ResolveTuning(FElysiumNpc& Npc)
{
	UElysiumSessionSubsystem* GameState = Npc.World ? Npc.World->GetGameState() : nullptr;
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

FVector FElysiumNpcSenses::ViewForward(const FElysiumEntity& Npc)
{
	// Source angles carry the inverse Unreal yaw in this substrate; pitch remains Source pitch,
	// so construct the full forward vector rather than flattening a target above/below the
	// observer into its horizontal ray.
	const float PitchRadians = FMath::DegreesToRadians(static_cast<float>(Npc.Angles.X));
	const float YawRadians = FMath::DegreesToRadians(-static_cast<float>(Npc.Angles.Y));
	return FVector(FMath::Cos(PitchRadians) * FMath::Cos(YawRadians),
		FMath::Cos(PitchRadians) * FMath::Sin(YawRadians), -FMath::Sin(PitchRadians));
}

float FElysiumNpcSenses::ViewConeBodyOffsetCm()
{
	return ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::DebugViewconeBackDist)
		* ElysiumMove::U;
}

bool FElysiumNpcSenses::IsInViewCone(const FElysiumEntity& Npc, const FVector& TargetCm,
	float TargetConeScalar)
{
	// `FInViewCone` at 0x103264d0 is a strict 3-D apex test. `0x103268e0` would take the 2-D body
	// when `debug_view_cone_2d3d` reads 2; it ships "3", so the 3-D body is the shipped one.
	//
	// The threshold is the OBSERVER's `m_flFieldOfView` (`+0x1574`, a `CBaseCombatCharacter` word),
	// the last argument both callers push (`0x10326750`, `0x10326a20`): 0.2 on the Troika line
	// (`0x10298de8`), 0.5 on the player (`CBasePlayer::Spawn 0x1016d260`), the species' own on
	// theirs. The body is a `CBaseCombatCharacter` method, so retail has no non-character observer;
	// one reaching this port's static form reads the Troika 0.2.
	const FElysiumCombatCharacter* const Observer = Npc.AsCombatCharacter();
	const float FieldOfView = Observer != nullptr
		? Observer->FieldOfView : ElysiumNpcSense::DefaultViewConeDot;
	const FVector Forward = ViewForward(Npc);
	const FVector ToTarget = TargetCm - Npc.EyePosition();
	// 0x103265af rejects behind the original eye before shifting the apex: `FCOMP 0.0`, `TEST
	// AH,0x41`, `JP` continues only when the dot is strictly greater, so a dot of exactly zero is
	// out too.
	if (FVector::DotProduct(Forward, ToTarget) <= 0.0) return false;
	const FVector FromApex = ToTarget + Forward * ViewConeBodyOffsetCm();
	// 0x1032669c multiplies the COSINE by the target scalar, then 0x103266a0 compares it to the
	// FOV with the same `TEST AH,0x41` / `JP`: strictly greater admits, equality refuses.
	return FVector::DotProduct(Forward, FromApex.GetSafeNormal()) * TargetConeScalar
		> FieldOfView;
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

bool FElysiumNpcSenses::PerformSensing(FElysiumNpc& Npc, double Now)
{
	// `CAI_Senses::PerformSensing` (`0x10310710`): the same prelude and gate as `Tick`, then `Look`
	// and `Listen` in that order, and nothing else -- the LOS debounce is slot 481's.
	if (Npc.IsInert() || Npc.World == nullptr)
	{
		return false;
	}
	if (!Perception.bResolved)
	{
		ResolveTuning(Npc);
	}
	if (!bCanPerformSenses)
	{
		return false;
	}
	TickSight(Npc, Now);
	TickHearing(Npc, Now);
	++PassCount; // observation only: the admitted 0x10310710 Look/Listen transaction
	return true;
}

bool FElysiumNpcSenses::IsVisible(const FElysiumNpc& Npc, const FElysiumEntity& Candidate, double Now)
{
	// Troika `FVisible` `0x102b4630` and the slot 594 range/concealment test under it are story
	// 29d's family Senses10 (`ElysiumNpcSenses10.cpp`); this dispatches the slot rather than
	// keeping a second copy of the chain. Retail's mask here is `0x2804091`, which is what every
	// caller of `FVisible` in the kernel closure passes.
	//
	// The dispatch matters beyond tidiness: slot 594 is the ONE writer of `m_bSeenInOuterBand`
	// (`+0x6081`), which slot 472 `OnSeeEntity` reads, so the see-unknown path only works when the
	// sighting goes through the slot.
	(void)Now;
	return const_cast<FElysiumNpc&>(Npc).FVisible(const_cast<FElysiumEntity*>(&Candidate),
		0x2804091, nullptr, 0);
}

// `SetClosestPlayer` `0x10293a80`. Retail walks every client slot and keeps the nearest by 3-D
// distance, seeded at `20000.0` — a search bound, not a clamp, so a player farther than that
// leaves the handle invalid while the distance sits at the seed. `NPCThink` stores the return into
// `m_flPlayerDist` and the tail publishes the pair to the player's observer surface, which is the
// call this port answers with the witness channel's own closest-player entry point.
//
// Ungated: retail's 2-second cache is on `SetPlayerLOS` alone. This runs on the NORMAL think.
void FElysiumNpcSenses::SetClosestPlayer(FElysiumNpc& Npc, double Now)
{
	FElysiumEntityWorld* World = Npc.World;
	if (World == nullptr)
	{
		return;
	}
	FElysiumPlayer* Player = World->FindPlayer();
	const float SearchCm = ElysiumNpcSense::ClosestPlayerSearchUnits * ElysiumMove::U;
	const float DistanceCm = Player != nullptr && !Player->IsInert()
		? static_cast<float>(FVector::Dist(Npc.Origin, Player->Origin)) : SearchCm;
	if (Player == nullptr || Player->IsInert() || DistanceCm >= SearchCm)
	{
		Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
		Memory.ClosestPlayerDistanceCm = SearchCm;
		return;
	}
	Memory.ClosestPlayer = Player->Handle;
	Memory.ClosestPlayerDistanceCm = DistanceCm;
	ElysiumNpcWitness::OnClosestPlayerUpdated(Npc, *Player, Now);
}

// `SetPlayerLOS` `0x10291610`, walked. Note what is NOT here: a cone test. Retail's cached LOS is
// true for a player standing behind this NPC with a clear line to its eye, which is why the port's
// sighting answer is the separate `bPlayerVisible` and this pair feeds only the think cadence and
// `NPCThink`'s `DISAPPEAR` arm.
void FElysiumNpcSenses::SetPlayerLos(FElysiumNpc& Npc, double Now)
{
	FElysiumEntityWorld* World = Npc.World;
	if (World == nullptr)
	{
		return;
	}
	auto ForceVisible = [&]()
	{
		Memory.bPlayerInPvs = true;
		Memory.bPlayerLos = true;
		Memory.PlayerPvsLastClearTime = Now;
		Memory.PlayerLosLastClearTime = Now;
	};
	// `m_bfNPCStateFlags & 0x8`: the per-state byte `0x1026e3e0` carries it for combat (`0x8f`),
	// alert (`0x39`), script (`0x8`) and the hunt/flee pair (`0x7f`), never for idle (`0x31`), so
	// a body that is alert, fighting or scripted is treated as in view wherever the player is.
	// `m_bfNPCFrenziedFlags & 0x8` is set by both discipline arms (`0x3b1c` and `0x9fbd`); 16c is
	// its producer and the read is live the day it lands.
	if ((Npc.NpcStateFlags() & FElysiumNpcFlags::StateAlwaysInPlayerView) != 0
		|| Npc.HasFrenzied(FElysiumNpcBase::FrenziedAlwaysInPlayerView))
	{
		ForceVisible();
	}
	else if (Memory.PlayerLosNextUpdateTime <= 0.0 || Now >= Memory.PlayerLosNextUpdateTime)
	{
		Memory.PlayerLosNextUpdateTime = Now + ElysiumNpcSense::PlayerLosCadenceSeconds;
		FElysiumPlayer* Player = Memory.ClosestPlayer.IsSet() ? World->FindPlayer() : nullptr;
		if (Player == nullptr || Player->IsInert() || Player->Handle != Memory.ClosestPlayer)
		{
			// No closest player is the sentinel arm, not a detection: retail seeds both bytes true
			// here exactly as `NPCInit` does.
			ForceVisible();
		}
		else
		{
			const IElysiumEmbodiment* Embodiment = World->Embodiment();
			Memory.bPlayerInPvs = Embodiment == nullptr
				|| Embodiment->ArePointsInSamePvs(Player->Origin, Npc.Origin);
			if (!Memory.bPlayerInPvs)
			{
				Memory.bPlayerLos = false;
			}
			else
			{
				Memory.PlayerPvsLastClearTime = Now;
				// At or inside 512 units LOS is true with no trace at all (`dist < 512` and
				// `dist == 512` take the same arm in `0x10291610`).
				if (Memory.ClosestPlayerDistanceCm
					<= ElysiumNpcSense::NearBypassUnits * ElysiumMove::U)
				{
					Memory.bPlayerLos = true;
					Memory.PlayerLosLastClearTime = Now;
				}
				else
				{
					// The eye-to-eye trace, retail's mask `0x4091`. `SetPlayerLOS 0x10291610`'s
					// line has no MONSTER bit, so no character is listed and only the world (and the props
					// the mask admits) can stop it.
					Memory.bPlayerLos =
						SightClear(World, Npc, *Player, PlayerLosMask);
					if (Memory.bPlayerLos)
					{
						Memory.PlayerLosLastClearTime = Now;
					}
				}
			}
		}
	}
	// The hysteresis, and it runs on EVERY call — including one the 2 s gate declined. A body that
	// has just lost its line but is still in the same PVS keeps LOS for eight seconds past the last
	// clear one, which is what stops the cadence oscillating as a player walks behind a pillar.
	if (!Memory.bPlayerLos && Memory.bPlayerInPvs && Memory.PlayerLosLastClearTime >= 0.0
		&& Now - Memory.PlayerLosLastClearTime < ElysiumNpcSense::PlayerLosHysteresisSeconds)
	{
		Memory.bPlayerLos = true;
	}
}

void FElysiumNpcSenses::TickSight(FElysiumNpc& Npc, double Now)
{
	FElysiumEntityWorld* World = Npc.World;
	if (World == nullptr)
	{
		return;
	}
	// The port's `COND_SEE_PLAYER` reconstruction. It belongs to the sense pass, not to
	// `SetPlayerLOS`'s 2 s cache: retail rebuilds its sighting on every `Look`, and unlike the
	// cache this keeps the cone term. It resolves the player itself for the same reason `Look`
	// does — a sighting is an observation this pass makes, not a cached pair it reads back.
	FElysiumPlayer* Player = World->FindPlayer();
	if (Player == nullptr || Player->IsInert())
	{
		Memory.bPlayerInRange = Memory.bPlayerInOuterBand = Memory.bPlayerInCone = false;
		Memory.bPlayerVisible = false;
	}
	else
	{
		const float DistanceCm = static_cast<float>(FVector::Dist(Npc.Origin, Player->Origin));
		const float RadiusCm = Perception.VisionDistanceCm * TargetVisionScalar(*Player);
		const float NearBypassCm = ElysiumNpcSense::NearBypassUnits * ElysiumMove::U;
		Memory.bPlayerInRange = DistanceCm <= RadiusCm;
		Memory.bPlayerInOuterBand = Memory.bPlayerInRange
			&& DistanceCm > ElysiumNpcSense::OuterBandFraction * RadiusCm;
		Memory.bPlayerInCone = IsInViewCone(Npc, Player->EyePosition(), TargetConeScalar(*Player));
		// No grace of any kind: retail's only eight-second hold is `SetPlayerLOS`'s hysteresis on
		// the cadence byte, and `Look` has no memory between passes. A player who steps behind a
		// pillar in front of this NPC is unseen on the very next pass; what survives the loss is
		// the enemy memory record, not the sighting.
		Memory.bPlayerVisible = Memory.bPlayerInCone && Memory.bPlayerInRange
			&& (DistanceCm <= NearBypassCm
				|| SightClear(World, Npc, *Player, SightSeeMask));
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
		const bool bNpc = Candidate->AsNpcBase() != nullptr;   // a `g_AI_Manager` CAI_BaseNPC
		const FString Classname = Candidate->Def ? Candidate->Def->Classname : FString();
		// QuerySeeEntity slot 468 (`0x102b38b0`) — dispatched, not inlined. Its five arms (the two
		// sense-off bytes, the frenzy-friend veto, the unconditional player pass-through and the
		// D_HT/D_FR gate) are story 29d's family Senses10 body. The frenzy-friend arm the port's own
		// comment called "16c work" is live there.
		(void)Classname;
		if (!Npc.QuerySeeEntity(const_cast<FElysiumEntity*>(Candidate)))
		{
			continue;
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
		// `m_NPCState == 2` and `m_bEnemyWentOccluded` (`+0x5bc5`) CLEAR -- the occlusion edge, as the
		// recovered `GetVisionDistance` arm reads it (the port's old ten-failure debounce went with
		// its twin at story 8 wave 2).
		const bool bRangeBypass = Npc.GetMind().State() == EElysiumNpcState::Combat
			&& !Npc.BaseMemory.bEnemyWentOccluded;
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
		// `m_bSeenInOuterBand` (`+0x6081`) is written by SLOT 594, inside the `IsVisible` dispatch
		// just above — that is its ONE retail writer (`102b4857`). Read back here rather than
		// recomputed, so the byte slot 472 reads is the byte slot 594 wrote.
		const bool bOuterBand = Memory.bPlayerInOuterBand;
		{
			FElysiumNpcSightingDebug Sighting;
			Sighting.EffectiveRadiusCm = Perception.VisionDistanceCm * Scalar;
			Sighting.TargetConeScalar = ConeScalar;
			Sighting.bOuterBand = bOuterBand;
			Sighting.bRangeBypass = bRangeBypass;
			Sighting.bDamageOverride = bDamageOverride;
			ElysiumNpcDebugLogging::Sighting(Npc, *Candidate, Sighting);
		}
		// Slot 472 `OnSeeEntity` (`0x102b3e00`) — dispatched, not inlined. Story 29d's family
		// Senses10 owns the body and its two species arms (`CNPC_VCop`, `CNPC_VHunter`); the port
		// carried every arm here but fired `OnUnknownVisionPlayer` LAST where retail fires it
		// FIRST, which is the observable order difference that made the row `rule`.
		Npc.OnSeeEntity(const_cast<FElysiumEntity*>(Candidate));
		// `SEE_UNKNOWN` itself is the see-unknown sweep's, read off the memory slot 472 just wrote:
		// this candidate holds `m_hBestSeeUnknown` exactly when the body took its admitting arm.
		if (Memory.BestSeeUnknown == Candidate->Handle)
		{
			bSeeUnknownThisPass = true;
		}
	}
	for (const auto& Channel : SeenByChannel) SeenThisPass.Append(Channel);
	// Troika OnLooked runs before base GatherConditions clears/rebuilds the SEE family.
	if (Npc.Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy)) ++Npc.EnemySightings;
}


void FElysiumNpcSenses::TickHearing(FElysiumNpc& Npc, double Now)
{
	Npc.HeardConditions.Reset();
	// `CAI_Senses::Listen` rebuilds the sound list every pass, and `GetClosestSound`
	// (`0x103105d0`) walks exactly that list. Story 29c-1, family Senses.
	HeardThisPass.Reset();
	FElysiumEntityWorld* World = Npc.World;
	if (!World) return;
	const auto& Bus = World->GameSounds();
	const auto Pending = Bus.EventsSince(Cursor);
	if (!Pending.IsEmpty()) Cursor = Pending.Last().Serial;
	UElysiumSessionSubsystem* State = World->GetGameState();
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
		// `CanHearSound` (`0x1030f7b0`), the LISTEN-level cull retail runs before it dispatches slot
		// 467: the sound's expiry, the owner's own liveness, and the radius test against
		// `HearingSensitivity() * volume` with `AdjustSoundDistForStealth` applied. That radius test
		// is HERE, in `Listen`, and NOT in slot 467 — slot 467's own distance arm is the extra
		// quarter-scale one a cowering or sleeping body takes, and it lives in the slot body.
		const float BaseRadius = Event.UnadjustedRadiusCm;
		const float Radius = FMath::Max(0.f,
			BaseRadius * Perception.HearingScalar - Event.StealthHearingReductionCm);
		const float Distance = FVector::Dist(Npc.EyePosition(), Event.Position);
		if (Distance > Radius) continue;
		if (Event.bOccludable && !SegmentClear(World, Event.Position, Npc.EyePosition())) continue;
		// Slot 467 `QueryHearSound` (`0x102b35b0`) — dispatched, not inlined. Its eight arms (the
		// two sense-off bytes, the frenzy-friend veto the port's comment called "16c work", self,
		// the owner's concealment, the deaf zone and the cowering/sleeping quarter-radius test) are
		// story 29d's family Senses10 body.
		(void)Rules;
		if (!Npc.QueryHearSound(const_cast<FElysiumGameSoundEvent*>(&Event))) continue;
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
		// The record joins `CAI_Senses`'s list here, which is where retail's `Listen` inserts it:
		// after the audibility gates and before the delayed-condition queue. Story 29c-1.
		HeardThisPass.Add(Event);
		if (Record)
		{
			// CAI_Senses::GetClosestSound 0x103105d0: prefer my enemy, else nearest in this Listen.
			if (!Recorded.Contains(Condition) || (Record->Source != Npc.BaseMemory.Enemy
				&& (Event.Source == Npc.BaseMemory.Enemy || FVector::DistSquared(Npc.EyePosition(), Event.Position)
					< FVector::DistSquared(Npc.EyePosition(), Record->Position)))) *Record = Event;
			Recorded.Add(Condition);
		}
		Memory.LastHeardSource = Event.Source;
		Memory.LastHeardPosition = Event.Position;
		Memory.LastHeardCategory = Event.Category.ToString();
		Memory.LastHeardTime = Event.Time;
		const bool Flinch = Condition == EElysiumNpcCond::HearFlinch;
		// Slot 471 `GetReactionDelay` (`0x1026a8a0`) IS the non-flinch draw, `RandomFloat(0.2, 0.9)`
		// — story 29c-1, family Lifecycle landed the slot, and this site dispatches it rather than
		// keeping a second copy. `HEAR_FLINCH`'s `RandomFloat(0, 0.5)` is NOT that slot and stays
		// here. Exactly one draw either way, so the stream's position is unchanged.
		const double At = Now + (Flinch
			? ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(0.f, 0.5f)
			: static_cast<double>(Npc.GetReactionDelay()));
		// The eight-entry retail list (0x102cc6c0) min-updates a duplicate deadline (0x102cc590).
		// The capacity check precedes even the duplicate lookup.
		if (Npc.PendingSounds.Num() < 8)
		{
			if (FElysiumNpcPendingSound* Existing = Npc.PendingSounds.FindByPredicate([Condition](const FElysiumNpcPendingSound& Item)
				{ return Item.Condition == Condition; })) Existing->PromoteAt = FMath::Min(Existing->PromoteAt, At);
			else Npc.PendingSounds.Add({ Condition, At });
		}
	}
	LastListenTime = Now;
	for (int32 Index = Npc.PendingSounds.Num() - 1; Index >= 0; --Index)
	{
		if (Now >= Npc.PendingSounds[Index].PromoteAt)
		{
			Npc.HeardConditions.Set(Npc.PendingSounds[Index].Condition);
			Npc.PendingSounds.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}
	// Outputs follow delayed promotion, once per condition family per Listen, activator = self.
	if (Npc.HeardConditions.Has(EElysiumNpcCond::HearWorld)) Npc.FireOutput(FName(TEXT("OnHearWorld")), Npc.Handle);
	if (Npc.HeardConditions.Has(EElysiumNpcCond::HearPlayer)) Npc.FireOutput(FName(TEXT("OnHearPlayer")), Npc.Handle);
	if (Npc.HeardConditions.Has(EElysiumNpcCond::HearCombat) || Npc.HeardConditions.Has(EElysiumNpcCond::HearBulletImpact)
		|| Npc.HeardConditions.Has(EElysiumNpcCond::HearDanger)) Npc.FireOutput(FName(TEXT("OnHearCombat")), Npc.Handle);
	for (const auto Pair : { TPair<EElysiumNpcCond, const FElysiumGameSoundEvent*>(EElysiumNpcCond::HearCombat, &Memory.LastSoundCombat),
		TPair<EElysiumNpcCond, const FElysiumGameSoundEvent*>(EElysiumNpcCond::HearBulletImpact, &Memory.LastSoundBulletImpact) })
	{
		if (Npc.HeardConditions.Has(Pair.Key)) ExtendVisionOverride(Npc, Pair.Value->Source, Now, 1.0);
	}
}

void FElysiumNpcSenses::ExtendVisionOverride(FElysiumNpc& Npc, FElysiumEntityHandle Source,
	double Now, double Duration)
{
	const FElysiumEntity* Owner = Npc.World ? Npc.World->Resolve(Source) : nullptr;
	if (!Owner) return;
	const FElysiumNpc* OtherNpc = Owner->AsNpc();
	// 0x1028e8b0: any live owner when no enemy, my enemy, or an NPC sharing my enemy.
	if (!Npc.BaseMemory.Enemy.IsSet() || Source == Npc.BaseMemory.Enemy
		|| (OtherNpc && OtherNpc->BaseMemory.Enemy == Npc.BaseMemory.Enemy))
		Memory.StealthVisionOverrideUntil = FMath::Max(Memory.StealthVisionOverrideUntil, Now + Duration);
}

void FElysiumNpcSenses::CommitBestSound(FElysiumNpc& Npc, const FElysiumNpcConditions& Conditions)
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
			Npc.BaseMemory.BestSoundSource = Memory.BestSound.Source;
			return;
		}
	}
}

// --- Story 29c-1, family Senses -----------------------------------------------------------------

const FElysiumGameSoundEvent* FElysiumNpcSenses::ClosestSound(const FElysiumNpc& Npc,
	uint32 TypeMask) const
{
	// `CAI_Senses::GetClosestSound` (`0x103105d0`), arm for arm.
	//
	// Retail asks its owner for `GetEnemy()` (slot 167, vtable `+0x29c`) ONCE, before the walk, and
	// for `EarPosition()` (slot 196, vtable `+0x310`) once beside it. Then, for every record whose
	// type word (`sound+0x04`) equals the requested one: if there IS an enemy and this record's
	// owner handle resolves to it, RETURN IMMEDIATELY — the enemy's sound outranks a nearer one and
	// ends the walk. Otherwise keep the smallest squared distance from the ear to the record's
	// stored origin (`sound+0x20..0x28`), seeded at `0x4d800000` (2.68e8), and answer that.
	const FElysiumEntity* Enemy = Npc.World != nullptr ? Npc.World->Resolve(Npc.BaseMemory.Enemy) : nullptr;
	const FVector EarCm = const_cast<FElysiumNpc&>(Npc).EarPosition();
	const FElysiumGameSoundEvent* Best = nullptr;
	double BestDistanceSq = TNumericLimits<double>::Max();
	for (const FElysiumGameSoundEvent& Record : HeardThisPass)
	{
		if (Record.TypeMask != TypeMask)
		{
			continue;
		}
		if (Enemy != nullptr && Record.Source == Enemy->Handle)
		{
			return &Record;
		}
		const double DistanceSq = FVector::DistSquared(EarCm, Record.Position);
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = &Record;
		}
	}
	return Best;
}

float FElysiumNpcSenses::EffectiveVisionDistanceCm(const FElysiumNpc& Npc, double Now) const
{
	// `0x1029c970`, 83 bytes and four reads. The default is `m_flVisionDistance` (`+0x63b8`), the
	// resolved authored channel; TWO independent overrides replace it with `m_pSenses->m_LookDist`
	// (`+0x5cdc` then `+0x10`), and the answer is then clamped UP to `_DAT_104454c4` = 0.0.
	//
	//   1. `curtime < m_flStealthVisionOverrideTime` (`+0x6604`) — the damage/sound override window.
	//   2. `m_NPCState == 2` (COMBAT) **and** `m_bEnemyWentOccluded` (`+0x5bc5`) is CLEAR.
	//
	// The second arm reads the occlusion EDGE (`+0x5bc5`); `TickSight`'s `bRangeBypass` reads the
	// same word since story 8 wave 2.
	float Distance = Perception.VisionDistanceCm;
	const bool bStealthOverride = Now < Memory.StealthVisionOverrideUntil;
	const bool bCombatUnoccluded = Npc.GetMind().State() == EElysiumNpcState::Combat
		&& !Npc.BaseMemory.bEnemyWentOccluded;
	if (bStealthOverride || bCombatUnoccluded)
	{
		Distance = LookDistCm;
	}
	return FMath::Max(Distance, 0.f);
}

void FElysiumNpcSenses::Serialize(FElysiumSaveArchive& Ar)
{
	Memory.Serialize(Ar);
	Ar.Time(LastListenTime, EElysiumTimePolicy::MinusOne); // 0x10310710 port cadence, map-clock domain
	for (double& LookDeadline : NextLookTime) Ar.Time(LookDeadline, EElysiumTimePolicy::MinusOne); // 0x1030ff10 port scan deadline, map-clock domain
}

void FElysiumNpcPendingSound::SerializeQueue(FElysiumSaveArchive& Ar, TArray<FElysiumNpcPendingSound>& Queue)
{
	int32 Count = Queue.Num();
	Ar << Count;
	if (Ar.IsLoading()) Queue.SetNum(FMath::Clamp(Count, 0, 8));
	for (FElysiumNpcPendingSound& Sound : Queue)
	{
		uint8 Condition = static_cast<uint8>(Sound.Condition);
		Ar << Condition;
		Ar.Time(Sound.PromoteAt); // 0x1026ec30 delayed sound deadline, map-clock domain
		if (Ar.IsLoading()) Sound.Condition = static_cast<EElysiumNpcCond>(Condition);
	}
}

// The load-side half (slot 130). Everything here is re-derivation, which is why it needs the NPC
// and the archive does not: a saved pass cache would describe a look this NPC never took, and the
// tuning is authored data the field walk has already restored in its keyfield form.
void FElysiumNpcSenses::OnPostRestore(FElysiumNpc& Npc)
{
	Npc.HeardConditions.Reset();
	SeenThisPass.Reset();
	for (int32 Channel = 0; Channel < 3; ++Channel)
	{
		// 0x1030ff10: keep restored map-domain scan deadline; rebuild only the sight list cache.
		SeenByChannel[Channel].Reset();
	}
	if (Npc.World)
	{
		Memory.Rebase(*Npc.World, Npc.BaseMemory);
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
