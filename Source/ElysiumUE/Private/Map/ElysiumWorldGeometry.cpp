#include "Map/ElysiumWorldGeometry.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumCollisionChannels.h"
#include "ElysiumWorldCollisionActor.h"
#include "Map/ElysiumRetailMaskRecipe.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "NavigationData.h"
#include "NavigationSystemTypes.h"

// 0018 story 6, wave 2: the Unreal queries behind the geometry seam. The map actor's `TraceRetail`
// and the NPC body's `QueryRoute` / `NavRaycast` resolve their own inputs and land here.

DEFINE_LOG_CATEGORY_STATIC(LogElysiumGeometry, Log, All);

static TAutoConsoleVariable<float> CVarElysiumGeomBudgetLogSeconds(
	TEXT("elysium.geom.BudgetLogSeconds"), 0.f,
	TEXT("0018 story 6: log the geometry seam's cost (peak per-frame path tests, totals, ms) to ")
	TEXT("LogElysiumGeometry once per this many seconds. 0 = off."),
	ECVF_Default);

namespace ElysiumWorldGeometryDetail
{
	// A rejected world hit is excluded and the query asked again: Unreal's single and multi traces
	// both stop at the FIRST blocking hit, so a hit the recipe does not admit (a prop without
	// MONSTER, an ignored entity's brush) cannot be skipped by reading further down one result list.
	// Bounded so a pathological pile of props cannot spin; past the bound the trace answers clear.
	constexpr int32 MaxWorldRetraces = 32;

	// The profile the bake applies to a solid static prop (`pipeline/unreal/bake_map_v2.py`: the
	// prop component and the placed model's collision proxy). It is WorldStatic like the
	// brush-signature bodies, so no channel tells a prop from a wall; the profile name does.
	const FName& PropSolidProfile()
	{
		static const FName Name(TEXT("ElysiumPropSolid"));
		return Name;
	}

	// The retail ray, as Unreal sweeps it. Source's `Ray_t(start, end, mins, maxs)` places the box
	// RELATIVE to the traced point; Unreal's shapes are centred on it. So the query runs from
	// `start + centre` to `end + centre` and the reported position has `centre` taken off again.
	struct FRetailShape
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		FVector Centre = FVector::ZeroVector;
		FCollisionShape Shape;
		// A zero box: a line.
		bool bLine = true;
		// `start == end`: an overlap (`IsAreaClear 0x102a0fb0` traces `pos -> pos`).
		bool bOverlap = false;
	};

	FRetailShape ShapeOf(const FElysiumRetailTrace& Request)
	{
		FRetailShape Out;
		Out.Centre = (Request.MinsCm + Request.MaxsCm) * 0.5;
		const FVector Extent = ((Request.MaxsCm - Request.MinsCm) * 0.5).GetAbs();
		Out.Start = Request.StartCm + Out.Centre;
		Out.End = Request.EndCm + Out.Centre;
		Out.bLine = Extent.IsNearlyZero();
		Out.bOverlap = Request.StartCm.Equals(Request.EndCm);
		// An overlap needs a volume to ask about: a zero box at one point is asked as a point-sized
		// box. No recovered retail caller traces a zero box from a point to itself.
		Out.Shape = FCollisionShape::MakeBox(
			Out.bOverlap ? Extent.ComponentMax(FVector(0.01)) : Extent);
		return Out;
	}

	// The entity behind one hit body. A brush entity's body is a component of the map actor, so
	// the actor cannot name it; the component carries its own handle.
	FElysiumEntityHandle HandleOf(const UPrimitiveComponent* Component, const AActor* Actor,
		TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle)
	{
		if (const UElysiumBrushComponent* Brush = Cast<UElysiumBrushComponent>(Component))
		{
			return Brush->GetOwningEntity();
		}
		return Actor != nullptr ? ToHandle(Actor) : FElysiumEntityHandle::Invalid();
	}

	enum class EAdmit : uint8
	{
		Admit,
		RejectComponent,
		RejectActor,
	};

	// Whether the world answer keeps one blocking body.
	EAdmit AdmitWorldHit(const UPrimitiveComponent* Component, const AActor* Actor,
		const FElysiumRetailMaskRecipe& Recipe, const FElysiumRetailTrace& Request,
		TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle)
	{
		// Characters are never folded into the world answer. `CharacterMaskBit` already drops them
		// (the NPC capsule, the player's hull); this guards a character body that does not wear it.
		if (Actor != nullptr && Actor->IsA<APawn>())
		{
			return EAdmit::RejectActor;
		}
		// Props by profile -- the named mapping. R2 § 2: `StandardFilterRules 0x101d3080` rejects
		// every entity that is not a solid brush model unless the mask carries MONSTER. A solid
		// prop is known here by the `ElysiumPropSolid` profile it wears, not by a mask bit.
		if (!Recipe.bProps && Component != nullptr
			&& Component->GetCollisionProfileName() == PropSolidProfile())
		{
			return EAdmit::RejectComponent;
		}
		// The filter's pass entity and its second (`CTraceFilterSimpleTwoEnt`), whichever body
		// they stand in.
		const FElysiumEntityHandle Entity = HandleOf(Component, Actor, ToHandle);
		if (Entity.IsSet() && Request.Ignore.Contains(Entity))
		{
			return EAdmit::RejectComponent;
		}
		return EAdmit::Admit;
	}

	void Exclude(EAdmit Verdict, const UPrimitiveComponent* Component, const AActor* Actor,
		FCollisionQueryParams& Params)
	{
		if (Verdict == EAdmit::RejectActor && Actor != nullptr)
		{
			Params.AddIgnoredActor(Actor);
		}
		else if (Component != nullptr)
		{
			Params.AddIgnoredComponent(Component);
		}
	}

	// The first admitted blocking body overlapping `Shape` centred at `Centre`, or null.
	const FOverlapResult* FirstAdmittedOverlap(UWorld& World, const FVector& Centre,
		const FCollisionShape& Shape, ECollisionChannel Channel, const FCollisionQueryParams& Params,
		const FElysiumRetailMaskRecipe& Recipe, const FElysiumRetailTrace& Request,
		TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle, TArray<FOverlapResult>& Scratch)
	{
		Scratch.Reset();
		World.OverlapMultiByChannel(Scratch, Centre, FQuat::Identity, Channel, Shape, Params);
		for (const FOverlapResult& Overlap : Scratch)
		{
			if (Overlap.bBlockingHit
				&& AdmitWorldHit(Overlap.GetComponent(), Overlap.GetActor(), Recipe, Request, ToHandle)
					== EAdmit::Admit)
			{
				return &Overlap;
			}
		}
		return nullptr;
	}

	// The contents signature of a world-collision body (`UElysiumWorldCollisionComponent`); -1 for
	// any other body. A mover's own signature is private to `UElysiumBrushComponent` and reads -1.
	int32 SignatureOf(const UPrimitiveComponent* Component)
	{
		const UElysiumWorldCollisionComponent* Body = Cast<UElysiumWorldCollisionComponent>(Component);
		return Body != nullptr ? static_cast<int32>(Body->Signature) : -1;
	}

	// The world answer: brushes, and the movers and props the recipe admits, on its channel.
	void TraceWorld(UWorld& World, const FRetailShape& Q, const FElysiumRetailMaskRecipe& Recipe,
		const FElysiumRetailTrace& Request, FCollisionQueryParams& Params,
		TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle, FElysiumRetailTraceResult& Out)
	{
		TArray<FOverlapResult> Scratch;
		if (Q.bOverlap)
		{
			// A start == end trace is an overlap: it answers only whether the box starts in solid.
			// The fraction stays 1 (retail `IsAreaClear` reads the flags beside it, R2 § 4); a body
			// that holds the whole zero-length trace holds its end too, so both flags go together.
			if (const FOverlapResult* Solid = FirstAdmittedOverlap(World, Q.Start, Q.Shape,
					Recipe.Channel, Params, Recipe, Request, ToHandle, Scratch))
			{
				Out.bStartSolid = true;
				Out.bAllSolid = true;
				Out.HitEntity = HandleOf(Solid->GetComponent(), Solid->GetActor(), ToHandle);
				Out.HitSignature = SignatureOf(Solid->GetComponent());
			}
			return;
		}

		for (int32 Attempt = 0; Attempt < MaxWorldRetraces; ++Attempt)
		{
			FHitResult Hit;
			const bool bBlocked = Q.bLine
				? World.LineTraceSingleByChannel(Hit, Q.Start, Q.End, Recipe.Channel, Params)
				: World.SweepSingleByChannel(Hit, Q.Start, Q.End, FQuat::Identity, Recipe.Channel,
					Q.Shape, Params);
			if (!bBlocked || !Hit.bBlockingHit)
			{
				return;
			}
			const UPrimitiveComponent* Component = Hit.GetComponent();
			const AActor* Actor = Hit.GetActor();
			const EAdmit Verdict = AdmitWorldHit(Component, Actor, Recipe, Request, ToHandle);
			if (Verdict != EAdmit::Admit)
			{
				Exclude(Verdict, Component, Actor, Params);
				continue;
			}

			Out.Fraction = Hit.Time;
			Out.EndPosCm = Hit.Location - Q.Centre;
			Out.Normal = Hit.ImpactNormal;
			Out.HitEntity = HandleOf(Component, Actor, ToHandle);
			Out.HitSignature = SignatureOf(Component);
			// The shape's index in its body (Unreal truncates it to a byte). Debug only.
			Out.ElementIndex = static_cast<int32>(Hit.ElementIndex);
			// The named mapping for Source's two flags. Unreal's `bStartPenetrating` (under
			// `bFindInitialOverlaps`) is their union: the trace starts in solid. It is reported as
			// `startsolid`, and as `allsolid` too when the trace's end also stands in admitted solid
			// -- the sweep found no exit. A solid the trace leaves and a second one it ends in read as
			// all-solid here where Source would say start-solid only.
			if (Hit.bStartPenetrating)
			{
				Out.bStartSolid = true;
				const FCollisionShape EndShape = Q.bLine
					? FCollisionShape::MakeBox(FVector(0.01)) : Q.Shape;
				Out.bAllSolid = FirstAdmittedOverlap(World, Q.End, EndShape, Recipe.Channel, Params,
					Recipe, Request, ToHandle, Scratch) != nullptr;
			}
			return;
		}
		UE_LOG(LogElysiumGeometry, Verbose,
			TEXT("TraceRetail mask 0x%x: %d rejected bodies in a row, answered clear"),
			static_cast<uint32>(Request.RetailMask), MaxWorldRetraces);
	}

	// One character met, merged into the list at its nearest contact.
	struct FCharacterContact
	{
		FElysiumEntityHandle Entity;
		float Fraction = 1.f;
		bool bStartSolid = false;
	};

	void NoteCharacter(const FCharacterContact& Contact, const FElysiumRetailTrace& Request,
		FElysiumRetailTraceResult& Out)
	{
		if (!Contact.Entity.IsSet() || Request.Ignore.Contains(Contact.Entity))
		{
			return;   // not a game entity, or the filter's pass entity
		}
		for (FElysiumRetailTraceCharacter& Known : Out.Characters)
		{
			if (Known.Entity == Contact.Entity)
			{
				if (Contact.Fraction < Known.Fraction)
				{
					Known.Fraction = Contact.Fraction;
					Known.bStartSolid = Contact.bStartSolid;
				}
				return;
			}
		}
		FElysiumRetailTraceCharacter& Added = Out.Characters.AddDefaulted_GetRef();
		Added.Entity = Contact.Entity;
		Added.Fraction = Contact.Fraction;
		Added.bStartSolid = Contact.bStartSolid;
	}

	// Every character body the same ray, box or overlap meets, nearest first. No channel and no
	// mask filter: the object types ARE the characters (NPC capsules on `Pawn`, the player's hull on
	// its own channel). One entry per entity, at its nearest contact.
	void ListCharacters(UWorld& World, const FRetailShape& Q, const FElysiumRetailTrace& Request,
		const AActor* IgnoreSelf, TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle,
		FElysiumRetailTraceResult& Out)
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		Objects.AddObjectTypesToQuery(ElysiumCollision::PlayerChannel);
		FCollisionQueryParams Params(FName(TEXT("ElysiumRetailTraceCharacters")), /*bTraceComplex*/ false);
		Params.bFindInitialOverlaps = true;
		Params.bReturnPhysicalMaterial = false;
		if (IgnoreSelf != nullptr)
		{
			Params.AddIgnoredActor(IgnoreSelf);
		}

		if (Q.bOverlap)
		{
			TArray<FOverlapResult> Overlaps;
			World.OverlapMultiByObjectType(Overlaps, Q.Start, FQuat::Identity, Objects, Q.Shape, Params);
			for (const FOverlapResult& Overlap : Overlaps)
			{
				// As the world answer's overlap: in solid, fraction 1.
				const AActor* Actor = Overlap.GetActor();
				FCharacterContact Contact;
				Contact.Entity = Actor != nullptr ? ToHandle(Actor) : FElysiumEntityHandle::Invalid();
				Contact.Fraction = 1.f;
				Contact.bStartSolid = true;
				NoteCharacter(Contact, Request, Out);
			}
		}
		else
		{
			TArray<FHitResult> Hits;
			if (Q.bLine)
			{
				World.LineTraceMultiByObjectType(Hits, Q.Start, Q.End, Objects, Params);
			}
			else
			{
				World.SweepMultiByObjectType(Hits, Q.Start, Q.End, FQuat::Identity, Objects, Q.Shape,
					Params);
			}
			for (const FHitResult& Hit : Hits)
			{
				const AActor* Actor = Hit.GetActor();
				FCharacterContact Contact;
				Contact.Entity = Actor != nullptr ? ToHandle(Actor) : FElysiumEntityHandle::Invalid();
				Contact.Fraction = Hit.Time;
				Contact.bStartSolid = Hit.bStartPenetrating;
				NoteCharacter(Contact, Request, Out);
			}
		}
		Out.Characters.Sort(
			[](const FElysiumRetailTraceCharacter& A, const FElysiumRetailTraceCharacter& B)
			{
				return A.Fraction < B.Fraction;
			});
	}

	// Times one seam query into the budget.
	struct FBudgetScope
	{
		FElysiumGeometryBudget::EKind Kind;
		uint64 StartCycles;

		explicit FBudgetScope(FElysiumGeometryBudget::EKind InKind)
			: Kind(InKind)
			, StartCycles(FPlatformTime::Cycles64())
		{
		}
		~FBudgetScope()
		{
			FElysiumGeometryBudget::Count(Kind,
				FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles));
		}
	};
}

namespace ElysiumWorldGeometry
{
	bool Trace(UWorld& World, const FElysiumRetailTrace& Request, FElysiumRetailTraceResult& Out,
		const AActor* IgnoreSelf, TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle)
	{
		Out = FElysiumRetailTraceResult();
		Out.EndPosCm = Request.EndCm;
		if (World.GetPhysicsScene() == nullptr)
		{
			return false;
		}
		const FElysiumRetailMaskRecipe Recipe = ElysiumRetailMask::Recipe(Request.RetailMask);
		if (Recipe.bNothing)
		{
			return true;   // mask 0 asks nothing: clear, untraced
		}
		const ElysiumWorldGeometryDetail::FBudgetScope Budget(FElysiumGeometryBudget::EKind::Trace);
		const ElysiumWorldGeometryDetail::FRetailShape Q = ElysiumWorldGeometryDetail::ShapeOf(Request);

		FCollisionQueryParams Params(FName(TEXT("ElysiumRetailTrace")), /*bTraceComplex*/ false);
		Params.bFindInitialOverlaps = true;
		Params.bReturnPhysicalMaterial = false;
		if (IgnoreSelf != nullptr)
		{
			Params.AddIgnoredActor(IgnoreSelf);
		}
		// Characters never; movers only under MOVEABLE (R2 § 2: `StandardFilterRules` rejects
		// movetype 8 without it). The two bits are worn by the bodies themselves: the NPC capsule
		// and the player's hull carry `CharacterMaskBit`, every brush-entity body `MoverMaskBit`.
		Params.IgnoreMask = static_cast<FMaskFilter>(ElysiumRetailMask::CharacterMaskBit
			| (Recipe.bMovers ? 0 : ElysiumRetailMask::MoverMaskBit));

		ElysiumWorldGeometryDetail::TraceWorld(World, Q, Recipe, Request, Params, ToHandle, Out);
		if (Recipe.bCharacters)
		{
			ElysiumWorldGeometryDetail::ListCharacters(World, Q, Request, IgnoreSelf, ToHandle, Out);
		}
		return true;
	}

	bool Route(const ANavigationData& NavData, const FNavAgentProperties& Agent,
		FSharedConstNavQueryFilter Filter, const FVector& FromCm, const FElysiumNpcRouteQuery& Query,
		FElysiumNpcRouteAnswer& Out)
	{
		const ElysiumWorldGeometryDetail::FBudgetScope Budget(FElysiumGeometryBudget::EKind::PathTest);
		// Both ends projected at the nav data's default query extent, unfiltered -- what
		// `ProjectPointToNavigation(..., INVALID_NAVEXTENT, &Agent)` resolves to, and what `MoveTo`
		// projects its goal with.
		const FVector Extent = NavData.GetDefaultQueryExtent();
		FNavLocation FromOnMesh;
		FNavLocation DestOnMesh;
		if (!NavData.ProjectPoint(FromCm, FromOnMesh, Extent)
			|| !NavData.ProjectPoint(Query.DestCm, DestOnMesh, Extent))
		{
			return false;
		}
		// No querier: the contract carries none, and the filter arrives already resolved.
		FPathFindingQuery PathQuery(static_cast<const UObject*>(nullptr), NavData,
			FromOnMesh.Location, DestOnMesh.Location, Filter);
		PathQuery.SetAllowPartialPaths(Query.bAcceptPartial);
		PathQuery.SetNavAgentProperties(Agent);
		const FPathFindingResult Found = NavData.FindPath(Agent, PathQuery);
		if (!Found.IsSuccessful() || !Found.Path.IsValid())
		{
			return false;
		}
		const bool bPartial = Found.Path->IsPartial();
		if (bPartial && !Query.bAcceptPartial)
		{
			return false;
		}
		Out.bReachable = true;
		Out.bPartial = bPartial;
		Out.LengthCm = static_cast<float>(Found.Path->GetLength());
		return true;
	}

	bool Raycast(const ANavigationData& NavData, FSharedConstNavQueryFilter Filter,
		const FElysiumNpcNavRaycast& Query, FElysiumNpcNavRaycastAnswer& Out)
	{
		const ElysiumWorldGeometryDetail::FBudgetScope Budget(FElysiumGeometryBudget::EKind::Raycast);
		// True from the engine = the corridor left the mesh; `HitLocation` is then the edge it left
		// by (the segment's end otherwise).
		FVector HitLocation = Query.ToCm;
		Out.bHit = NavData.Raycast(Query.FromCm, Query.ToCm, HitLocation, Filter);
		Out.HitCm = HitLocation;
		return true;
	}
}

namespace ElysiumWorldGeometryDetail
{
	FElysiumGeometryBudget GElysiumGeometryCurrent;
	FElysiumGeometryBudget GElysiumGeometryPeak;

	// One log period: the busiest frame's path tests and cost, and the period's totals.
	struct FBudgetPeriod
	{
		double StartSeconds = 0.0;
		int32 PeakPathTests = 0;
		double PeakFrameMs = 0.0;
		int64 Frames = 0;
		int64 PathTests = 0;
		int64 Raycasts = 0;
		int64 Traces = 0;
		double Ms = 0.0;
	};
	FBudgetPeriod GElysiumGeometryPeriod;
	uint64 GElysiumGeometryPeriodFrame = 0;

	// Running totals since the process started.
	int64 GElysiumGeometryTotalQueries = 0;
	double GElysiumGeometryTotalMs = 0.0;

	void MaybeLogPeriod()
	{
		const float PeriodSeconds = CVarElysiumGeomBudgetLogSeconds.GetValueOnGameThread();
		const double Now = FPlatformTime::Seconds();
		FBudgetPeriod& Period = GElysiumGeometryPeriod;
		if (PeriodSeconds <= 0.f || Period.StartSeconds == 0.0)
		{
			if (PeriodSeconds <= 0.f)
			{
				Period = FBudgetPeriod();
			}
			Period.StartSeconds = Now;
			return;
		}
		if (Now - Period.StartSeconds < PeriodSeconds)
		{
			return;
		}
		UE_LOG(LogElysiumGeometry, Log,
			TEXT("geometry budget %.1fs: peak frame %d path tests (peak frame %.3f ms); %lld frames: ")
			TEXT("%lld path tests, %lld nav raycasts, %lld traces, %.3f ms; lifetime %lld queries, %.1f ms"),
			Now - Period.StartSeconds, Period.PeakPathTests, Period.PeakFrameMs, Period.Frames,
			Period.PathTests, Period.Raycasts, Period.Traces, Period.Ms,
			GElysiumGeometryTotalQueries, GElysiumGeometryTotalMs);
		Period = FBudgetPeriod();
		Period.StartSeconds = Now;
	}
}

// The seam's cost, counted and never enforced: retail has no per-frame budget on these queries --
// nothing refuses a trace or a route for cost -- so this only measures what the story holds the
// port to.
void FElysiumGeometryBudget::Count(EKind Kind, double InMs)
{
	check(IsInGameThread());
	FElysiumGeometryBudget& Current = ElysiumWorldGeometryDetail::GElysiumGeometryCurrent;
	FElysiumGeometryBudget& PeakFrame = ElysiumWorldGeometryDetail::GElysiumGeometryPeak;
	ElysiumWorldGeometryDetail::FBudgetPeriod& Period = ElysiumWorldGeometryDetail::GElysiumGeometryPeriod;
	if (Current.Frame != GFrameCounter)
	{
		Current = FElysiumGeometryBudget();
		Current.Frame = GFrameCounter;
	}
	if (ElysiumWorldGeometryDetail::GElysiumGeometryPeriodFrame != GFrameCounter)
	{
		ElysiumWorldGeometryDetail::GElysiumGeometryPeriodFrame = GFrameCounter;
		++Period.Frames;
	}
	switch (Kind)
	{
	case EKind::PathTest:
		++Current.PathTests;
		++Period.PathTests;
		break;
	case EKind::Raycast:
		++Current.Raycasts;
		++Period.Raycasts;
		break;
	case EKind::Trace:
		++Current.Traces;
		++Period.Traces;
		break;
	}
	Current.Ms += InMs;
	Period.Ms += InMs;
	Period.PeakPathTests = FMath::Max(Period.PeakPathTests, Current.PathTests);
	Period.PeakFrameMs = FMath::Max(Period.PeakFrameMs, Current.Ms);
	++ElysiumWorldGeometryDetail::GElysiumGeometryTotalQueries;
	ElysiumWorldGeometryDetail::GElysiumGeometryTotalMs += InMs;
	if (Current.Frame == PeakFrame.Frame || Current.Ms > PeakFrame.Ms)
	{
		PeakFrame = Current;
	}
	ElysiumWorldGeometryDetail::MaybeLogPeriod();
}

const FElysiumGeometryBudget& FElysiumGeometryBudget::Peak()
{
	return ElysiumWorldGeometryDetail::GElysiumGeometryPeak;
}
