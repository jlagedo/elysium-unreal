#include "Substrate/ElysiumStealth.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumMoveSolve.h"          // ElysiumMove::U — the one units conversion
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumStealthTables.h"
#include "Substrate/ElysiumNpc.h"

// The pure rule's input triplet is spelled as a literal 3 so the header stays free of the storage
// type. This is what holds the two in step: a surface that grew a fourth sample would copy three
// and read a stale one, silently.
static_assert(FElysiumStealthSurface::NumSamples == 3,
	"ElysiumStealth::FRecomputeInputs::Samples must match FElysiumStealthSurface::NumSamples");

namespace ElysiumStealth
{

// The pure rule.

float NormalizeBodyLight(float Feet, float Centre, float Head, float Min, float Max)
{
	const float Raw = (Feet + Centre + Head) * RawLightScale;
	const float Range = Max - Min;
	if (Range <= UE_SMALL_NUMBER)
	{
		// A degenerate configured range cannot describe anything. Full light is the answer that
		// grants nothing, which is the same posture the light seam itself takes when it has no rig.
		return 1.f;
	}
	return FMath::Clamp((FMath::Clamp(Raw, Min, Max) - Min) / Range, 0.f, 1.f);
}

int32 SelectLightRow(const FElysiumStealthTables& Tables, float Normalized)
{
	if (Normalized >= 1.f)
	{
		return 0;   // at or above 1.0 is Light0, ahead of the walk
	}
	int32 Row = 0;
	while (Row < FElysiumStealthTables::NumLight - 1 && Normalized <= Tables.Threshold(Row))
	{
		++Row;
	}
	return Row;
}

FRecomputeResult Recompute(const FElysiumStealthTables& Tables, const FRecomputeInputs& In)
{
	FRecomputeResult Out;

	// 1. No world-light service: return without manufacturing replacement values. The caller keeps
	//    what it last committed and does not bump the generation.
	if (!In.bLightServiceAvailable)
	{
		Out.bCommitted = false;
		return Out;
	}

	// 2. The Sneaking rating, capped before the lookup. Negative is legal input (a `-10` volume on
	//    an untrained character) and reads as the `Stealth0` column.
	Out.StealthRow = FMath::Clamp(In.Sneaking, 0, MaxStealthRow);
	Out.bCommitted = true;
	Out.bEligible = In.bEligible;

	// 4./5. A lit torch forces full light; otherwise the retained triplet is aggregated and
	//       normalized over the configured world range.
	Out.LightOnMe = In.bTorchEquipped
		? 1.f
		: NormalizeBodyLight(In.Samples[0], In.Samples[1], In.Samples[2],
			In.WorldLightMin, In.WorldLightMax);

	// 6. The light row, equality entering the next darker one.
	Out.LightRow = SelectLightRow(Tables, Out.LightOnMe);

	// 7. The three table reads.
	Out.VisionScalar = Tables.Vision(Out.LightRow, Out.StealthRow);
	Out.ConeScalar = Tables.Cone(Out.LightRow, Out.StealthRow);
	Out.HearingReductionCm = Tables.HearingUnits(Out.StealthRow) * ElysiumMove::U;
	// 0x10351c95: overwrite only these four values AFTER computing/publishing the two rows.
	if (!In.bEligible)
	{
		Out.LightOnMe = InactiveLightSentinel;
		Out.VisionScalar = 1.f;
		Out.HearingReductionCm = Tables.HearingUnits(0) * ElysiumMove::U;
		Out.ConeScalar = Tables.Cone(0, 0);
	}
	return Out;
}

// The wiring half.

const FElysiumStealthTables& TablesFor(const FElysiumEntityWorld* World)
{
	UElysiumSessionSubsystem* GameState = World ? World->GetGameState() : nullptr;
	if (UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr)
	{
		return Rules->Stealth();
	}
	// The headless binding: a Substrate-tier world has no GameInstance subsystem at all, and the
	// bound table is a fallback rather than an override (the subsystem above always wins).
	if (const FElysiumStealthTables* Bound = ElysiumSheetRules::BoundTables().Stealth)
	{
		return *Bound;
	}
	return FElysiumStealthTables::Neutral();
}

int32 ResolveSneaking(const FElysiumCombatCharacter& Character)
{
	// Through the ordinary sheet path, which is where `GetStealthModifier()` enters the rating
	// (`ElysiumFeats::FeatValue`, feat id 1). Capped at 10 before the caller indexes a row.
	return FMath::Clamp(Character.CalcFeat(TEXT("Sneaking")), 0, MaxStealthRow);
}

FVector SamplePoint(const FBox& WorldBounds, const FVector& WorldCenter, int32 Index)
{
	FVector Point = WorldCenter;
	if (Index != 1)
		Point.Z = FMath::Lerp(WorldBounds.Min.Z, WorldBounds.Max.Z,
			Index == 2 ? HeadSampleWeight : FeetSampleWeight);
	return Point;
}

bool IsEligible(const FElysiumPlayer& Player, bool bDucking, double Now)
{
	return bDucking && !Player.WasRecentlyObservedByHostile(Now);
}

namespace
{

	// The active usable weapon's registered classname, folded, or empty.
	bool IsTorchEquipped(const FElysiumPlayer& Player)
	{
		const FElysiumEntity* Weapon = Player.World
			? Player.World->Resolve(Player.Inventory.ActiveWeapon)
			: nullptr;
		if (Weapon == nullptr || Weapon->Class == nullptr)
		{
			return false;
		}
		// One registered entity class per `vdata/items` definition, so the classname IS the item.
		return Weapon->Class->ClassName == FName(TorchWeaponClass);
	}
}

void TickPlayerSurface(FElysiumPlayer& Player, double Now)
{
	FElysiumStealthSurface& Surface = Player.Stealth;

	// The 0.1 s cadence. A negative deadline is "due now", which is what a freshly spawned or
	// freshly restored surface carries.
	if (Surface.NextUpdateTime > 0.0 && Now < Surface.NextUpdateTime)
	{
		return;
	}
	// Advanced before the body runs, and whether or not the body commits: retail's think advances
	// the deadline at the call site, so an unavailable light service costs one pass, not the cadence.
	Surface.NextUpdateTime = Now + UpdateIntervalSeconds;

	const IElysiumEmbodiment* Embodiment = Player.World ? Player.World->Embodiment() : nullptr;

	FRecomputeInputs In;
	In.bLightServiceAvailable = Embodiment && Embodiment->IsLightQueryAvailable();
	if (!In.bLightServiceAvailable) return;
	In.bEligible = IsEligible(Player, Embodiment->IsPlayerDucking(), Now);
	In.bTorchEquipped = IsTorchEquipped(Player);
	In.Sneaking = ResolveSneaking(Player);

	// ONE point per pass, index advancing. The other two retained samples participate unchanged, so
	// a step from light into shadow takes ~0.3 s to be fully believed.
	//
	// Ineligibility does not suppress sampling. Only the usable torch skips the body query.
	const int32 Index = FMath::Clamp(Surface.NextSampleIndex, 0,
		FElysiumStealthSurface::NumSamples - 1);
	if (!In.bTorchEquipped)
	{
		FBox Bounds(ForceInit);
		FVector Center;
		if (!Embodiment->SamplePlayerStealthBounds(Bounds, Center) || !Bounds.IsValid) return;
		Surface.Samples[Index] = Embodiment->QueryLightAtPoint(SamplePoint(Bounds, Center, Index));
		Surface.bHasLightSample = true;
		Surface.NextSampleIndex = (Index + 1) % FElysiumStealthSurface::NumSamples;
	}

	for (int32 i = 0; i < FElysiumStealthSurface::NumSamples; ++i)
	{
		In.Samples[i] = Surface.Samples[i];
	}

	const FRecomputeResult Result = Recompute(TablesFor(Player.World), In);
	if (!Result.bCommitted)
	{
		return;
	}
	Surface.LightOnMe = Result.LightOnMe;
	Surface.VisionScalar = Result.VisionScalar;
	Surface.ConeScalar = Result.ConeScalar;
	Surface.HearingReductionCm = Result.HearingReductionCm;
	Surface.LightRow = Result.LightRow;
	Surface.StealthRow = Result.StealthRow;
	Surface.bEligible = Result.bEligible;
	++Surface.Generation;
}

void CommitObserverSnapshot(FElysiumPlayer& Player, double Now)
{
	FElysiumStealthObserver& Pending = Player.PendingObserver;

	// An offer nothing has refreshed within the stale window, or whose observer no longer resolves
	// or has gone inert, clears presentation — WITHOUT mutating any gameplay state, which is the
	// whole point of the snapshot being downstream.
	const FElysiumEntity* Who = (Player.World && Pending.Observer.IsSet())
		? Player.World->Resolve(Pending.Observer)
		: nullptr;
	const bool bExpired = Pending.Time < 0.0 || (Now - Pending.Time) > ObserverStaleSeconds;
	if (!Pending.IsSet() || Who == nullptr || Who->IsInert() || bExpired)
	{
		Pending.Reset();
	}

	FElysiumStealthObserver& Published = Player.Observer;
	const bool bChanged = Published.Observer != Pending.Observer
		|| Published.bDetected != Pending.bDetected
		|| !FMath::IsNearlyEqual(Published.DistanceCm, Pending.DistanceCm, 1.f)
		|| !FMath::IsNearlyEqual(Published.Meter, Pending.Meter, 0.01f);
	if (!bChanged)
	{
		return;
	}
	const int32 Generation = Published.Generation;
	Published = Pending;
	Published.Generation = Generation + 1;
}

void PublishObservers(FElysiumPlayer& Player, double Now)
{
	Player.PendingObserver.Reset();
	// A mode-3 grapple is the committed stealth action. The victim's grapple entry has
	// already made its mind oblivious, and any observer snapshot from the pre-commit
	// frame is presentation history rather than a live sense result. Clear it before
	// the HUD projection can reuse that stale state.
	if (Player.Grapple.bOwnsStealthAction)
	{
		Player.Observer.Reset();
		return;
	}
	if (Player.World)
	{
		for (const auto& Entity : Player.World->Entities())
		{
			const FElysiumNpc* Npc = Entity ? Entity->AsNpc() : nullptr;
			if (!Npc || Npc->IsInert() || Npc->IsOblivious()) continue;
			const auto& Memory = Npc->Senses.Memory;
			const bool EnemyIsPlayer = Memory.Enemy == Player.Handle;
			if (!EnemyIsPlayer && Npc->Relationships.Resolve(Player.Handle, TEXT("player")) != EElysiumRelationship::Hate) continue;
			const bool Detected = EnemyIsPlayer && Memory.bEnemyLosLatched && !Memory.bEnemyOccluded;
			if (!Detected && !Npc->Senses.Sighted().Contains(Player.Handle)) continue;
			Player.OfferStealthObserver(Npc->Handle, FVector::Dist(Npc->Origin, Player.Origin),
				Npc->Senses.Perception.VisionDistanceCm * Player.Stealth.VisionScalar, Detected, Now);
		}
	}
	CommitObserverSnapshot(Player, Now);
}

float HearingReductionCmFor(const FElysiumEntity* Source)
{
	// Only the player carries a surface — retail's fields are CBasePlayer's — so every other
	// source removes nothing. Not warned: zero reduction is the correct answer, not a failure.
	if (Source == nullptr || Source->World == nullptr)
	{
		return 0.f;
	}
	const FElysiumPlayer* Player = Source->World->FindPlayer();
	if (Player == nullptr || Player->Handle != Source->Handle)
	{
		return 0.f;
	}
	return FMath::Max(0.f, Player->Stealth.HearingReductionCm);
}

float HearingReductionCmFor(const FElysiumEntityWorld* World, const FElysiumEntityHandle& Source)
{
	if (World == nullptr || !Source.IsSet())
	{
		return 0.f;
	}
	return HearingReductionCmFor(World->Resolve(Source));
}

}   // namespace ElysiumStealth

// The observer snapshot (`docs/vtmb/stealth.md` -> "HUD observability is not authority").

void FElysiumPlayer::OfferStealthObserver(const FElysiumEntityHandle& Who, float DistanceCm,
	float RadiusCm, bool bDetected, double Now)
{
	if (!Who.IsSet())
	{
		return;
	}
	// Nearest wins, and an offer for the incumbent refreshes it rather than losing to itself. A
	// pending candidate older than the offering pass's own cadence is stale — the observer that
	// made it has moved on or died — so it does not defend its distance.
	const bool bIncumbent = PendingObserver.Observer == Who;
	const bool bStale = PendingObserver.Time < 0.0
		|| (Now - PendingObserver.Time) > ElysiumStealth::ObserverStaleSeconds;
	if (PendingObserver.IsSet() && !bIncumbent && !bStale && DistanceCm >= PendingObserver.DistanceCm)
	{
		return;
	}
	PendingObserver.Observer = Who;
	PendingObserver.DistanceCm = DistanceCm;
	PendingObserver.bDetected = bDetected;
	PendingObserver.Time = Now;
	// The meter is derived from the observer's OWN effective radius, which already carries this
	// player's sight scalar — the same scalar, never a second calculation.
	PendingObserver.Meter = RadiusCm > UE_SMALL_NUMBER
		? FMath::Clamp(1.f - DistanceCm / RadiusCm, 0.f, 1.f)
		: 0.f;
}
