#pragma once

// The shipped tutorial's collision, authored into a live transient world, with the retail trace
// asked of it directly (0018 story 6, lane D).
//
// It reuses the setup `Elysium.Content.MapCollision.Tutorial` runs: the payload the bake ships,
// authored through `AElysiumWorldCollisionActor::AuthorFromPayload` (editor-only) into
// `FPlayerWorldFixture::CreateWorld`'s world. On top of that it exposes what the geometry seam's
// witnesses need:
//   - the world, and a per-signature list of the payload's convexes with centroid and bounds;
//   - `Trace`, which calls `ElysiumWorldGeometry::Trace` itself (no map actor, no entity world);
//   - `Services`, a recording double whose `TraceRetailQuery` forwards into `Trace`, so a kernel
//     body can be pointed at the real geometry;
//   - `FindRay`, which picks the axis on which a ray through a convex's centroid first meets THAT
//     convex (by `ElementIndex`), so a per-element assertion never reads a neighbour's answer.
//
// Centimetres throughout. Compiled only where the payload can be authored (`WITH_EDITOR`).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "ElysiumContentPaths.h"
#include "ElysiumContentsSignature.h"
#include "ElysiumMapCollisionPayload.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldCollisionActor.h"
#include "ElysiumWorldServices.h"
#include "Engine/World.h"
#include "Map/ElysiumWorldGeometry.h"
#include "PhysicsEngine/BodySetup.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Tests/ElysiumPlayerWorldFixture.h"
#include "Tests/ElysiumTestServices.h"
#include "UObject/StrongObjectPtr.h"

// The payload's signatures, spelled as the census spells them (`ElysiumContentsSignature.h`'s bits:
// P = player 1, N = npc 2, S = sight 4, p = pedestrian 8).
namespace ElysiumGeometrySignatures
{
	inline constexpr uint8 SolidToAll = 0x7;   // PNS-
	inline constexpr uint8 Window = 0x3;       // PN--: windows and grates among them
	inline constexpr uint8 SightOnly = 0x4;    // --S-
	inline constexpr uint8 NpcClip = 0x2;      // -N--
}

// The three retail masks the witnesses ask, by what they are.
namespace ElysiumGeometryMasks
{
	inline constexpr int32 Sight = 0x2804091;
	inline constexpr int32 Npc = 0x2400b;
	inline constexpr int32 Player = 0x1400b;
	inline constexpr int32 NpcSolid = 0x202400b;   // MASK_NPCSOLID, the hull-probe mask
}

// One convex of the payload, in world space.
struct FElysiumGeometryConvex
{
	uint8 Signature = 0;
	// The convex's index in its body's `AggGeom.ConvexElems`, which is what a hit's `ElementIndex`
	// names on a body that carries convexes alone.
	int32 Index = INDEX_NONE;
	// The mean of the convex's vertices: inside the hull, which its box centre need not be.
	FVector CentroidCm = FVector::ZeroVector;
	FBox BoundsCm = FBox(ForceInit);
	// The horizontal top face, when the convex has one: at least three vertices within 0.05 cm of its
	// highest point. `TopVertexCount` is 0 otherwise; the centre is their mean and the half extent
	// is their xy box's.
	int32 TopVertexCount = 0;
	FVector TopCentreCm = FVector::ZeroVector;
	FVector2D TopHalfCm = FVector2D::ZeroVector;
};

// A ray chosen to meet one convex first.
struct FElysiumGeometryRay
{
	int32 Index = INDEX_NONE;
	FVector StartCm = FVector::ZeroVector;
	FVector EndCm = FVector::ZeroVector;
};

class FElysiumGeometryFixture
{
public:
	FElysiumGeometryFixture() = default;
	FElysiumGeometryFixture(const FElysiumGeometryFixture&) = delete;
	FElysiumGeometryFixture& operator=(const FElysiumGeometryFixture&) = delete;

	FPlayerWorldFixture Scene;
	FElysiumRecordingServices Services;
	// Every box that stops an NPC (the Npc-signature convexes' bounds, and the displacement's), for a
	// witness that must pick open space from the payload rather than from a trace.
	TArray<FBox> NpcBlockerBoxes;
	// Whether the last `Trace` was answered by a collision world.
	bool bLastAnswered = false;

	// Authors `Map`'s shipped collision payload into a fresh transient world. False (after
	// reporting through `Test`) when any step fails.
	bool Init(FAutomationTestBase& Test, const TCHAR* Map = TEXT("sp_tutorial_1"))
	{
		if (!Scene.CreateWorld(Test))
		{
			return false;
		}
		UWorld* World = Scene.World;
		Payload.Reset(LoadObject<UElysiumMapCollisionPayload>(
			nullptr, *FElysiumContentPaths::BakedMapCollision(Map)));
		if (!Test.TestNotNull(TEXT("the map's collision payload exists"), Payload.Get())
			|| !Test.TestTrue(TEXT("its cooked bodies materialise"), Payload->CreatePhysicsMeshes()))
		{
			return false;
		}
		Actor = World->SpawnActor<AElysiumWorldCollisionActor>();
		if (!Test.TestNotNull(TEXT("the world-collision actor spawns"), Actor))
		{
			return false;
		}
		Actor->AuthorFromPayload(Payload.Get());

		for (UElysiumWorldCollisionComponent* Component : Actor->Bodies)
		{
			if (Component == nullptr || Component->Body == nullptr)
			{
				continue;
			}
			const FTransform& ToWorld = Component->GetComponentTransform();
			const TArray<FKConvexElem>& Elems = Component->Body->AggGeom.ConvexElems;
			TArray<FElysiumGeometryConvex>& Into = BySignature.FindOrAdd(Component->Signature);
			for (int32 Index = 0; Index < Elems.Num(); ++Index)
			{
				FElysiumGeometryConvex Convex;
				Convex.Signature = Component->Signature;
				Convex.Index = Index;
				FVector Sum = FVector::ZeroVector;
				for (const FVector& Vertex : Elems[Index].VertexData)
				{
					const FVector Point = ToWorld.TransformPosition(Vertex);
					Sum += Point;
					Convex.BoundsCm += Point;
				}
				if (Elems[Index].VertexData.Num() > 0)
				{
					Convex.CentroidCm = Sum / Elems[Index].VertexData.Num();
				}
				FBox2D TopBox(ForceInit);
				FVector TopSum = FVector::ZeroVector;
				for (const FVector& Vertex : Elems[Index].VertexData)
				{
					const FVector Point = ToWorld.TransformPosition(Vertex);
					if (Point.Z >= Convex.BoundsCm.Max.Z - 0.05)
					{
						++Convex.TopVertexCount;
						TopSum += Point;
						TopBox += FVector2D(Point.X, Point.Y);
					}
				}
				if (Convex.TopVertexCount >= 3)
				{
					Convex.TopCentreCm = TopSum / Convex.TopVertexCount;
					Convex.TopHalfCm = TopBox.GetExtent();
				}
				else
				{
					Convex.TopVertexCount = 0;
				}
				Into.Add(Convex);
				if (EnumHasAnyFlags(static_cast<EElysiumContentsSignature>(Component->Signature),
					EElysiumContentsSignature::Npc))
				{
					NpcBlockerBoxes.Add(Convex.BoundsCm);
				}
			}
		}
		if (Actor->Displacement != nullptr)
		{
			NpcBlockerBoxes.Add(Actor->Displacement->LocalCollisionBounds.TransformBy(
				Actor->Displacement->GetComponentTransform()));
		}

		// The double answers `TraceRetail` from this fixture's own `Trace`. The lambda captures `this`
		// and is stored in `Services`, a member of the same object: it can never outlive the fixture,
		// and the fixture is neither copied nor moved.
		Services.TraceRetailQuery = [this](const FElysiumRetailTrace& Request,
			FElysiumRetailTraceResult& Out)
		{
			return TraceInto(Request, Out);
		};
		return true;
	}

	UWorld* GetWorld() const { return Scene.World; }

	// The payload's convexes of one signature, in index order.
	const TArray<FElysiumGeometryConvex>& Convexes(uint8 Signature) const
	{
		static const TArray<FElysiumGeometryConvex> None;
		const TArray<FElysiumGeometryConvex>* Found = BySignature.Find(Signature);
		return Found != nullptr ? *Found : None;
	}

	// One retail trace, straight into `ElysiumWorldGeometry::Trace`. `bLastAnswered` says whether a
	// collision world answered; `Out` keeps its clear defaults otherwise.
	FElysiumRetailTraceResult Trace(const FElysiumRetailTrace& Request)
	{
		FElysiumRetailTraceResult Out;
		Out.EndPosCm = Request.EndCm;
		TraceInto(Request, Out);
		return Out;
	}

	// A ray through `Convex`'s centroid along one axis, from just outside its bounds to just outside
	// the far side: the shortest axis first (through a pane's thickness), the positive direction
	// before the negative, ties in axis order.
	static FElysiumGeometryRay AxisRay(const FElysiumGeometryConvex& Convex, int32 Axis,
		bool bPositive)
	{
		constexpr double Standoff = 2.0;
		FElysiumGeometryRay Ray;
		Ray.Index = Convex.Index;
		Ray.StartCm = Convex.CentroidCm;
		Ray.EndCm = Convex.CentroidCm;
		const double Low = Convex.BoundsCm.Min[Axis] - Standoff;
		const double High = Convex.BoundsCm.Max[Axis] + Standoff;
		Ray.StartCm[Axis] = bPositive ? Low : High;
		Ray.EndCm[Axis] = bPositive ? High : Low;
		return Ray;
	}

	// Picks the first convex of `Signature` (by index) and the first axis on which a ray through its
	// centroid meets THAT convex first under `OwnMask`: fraction below 1, the signature's body, the
	// convex's own element. Among those, the first ray `Accept` also takes (a caller's "and its other
	// masks read clean" test, since shipped brushes overlap a neighbour now and then). When none is
	// accepted, `Out` is the first ray that met the convex, so a caller's assertions still run on a
	// real ray and name what went wrong.
	//
	// False = no ray met any convex of this signature first: a stub `Trace`, a wrong mask recipe or
	// an empty signature. Loud on purpose; never a reason to skip.
	bool FindRay(uint8 Signature, int32 OwnMask,
		TFunctionRef<bool(const FVector& StartCm, const FVector& EndCm)> Accept,
		FElysiumGeometryRay& Out)
	{
		bool bHaveFirst = false;
		FElysiumGeometryRay First;
		for (const FElysiumGeometryConvex& Convex : Convexes(Signature))
		{
			const FVector Size = Convex.BoundsCm.GetSize();
			TArray<int32, TInlineAllocator<3>> Axes = { 0, 1, 2 };
			Axes.StableSort([&Size](int32 A, int32 B) { return Size[A] < Size[B]; });
			for (const int32 Axis : Axes)
			{
				for (int32 Direction = 0; Direction < 2; ++Direction)
				{
					const FElysiumGeometryRay Ray = AxisRay(Convex, Axis, Direction == 0);
					FElysiumRetailTrace Request;
					Request.StartCm = Ray.StartCm;
					Request.EndCm = Ray.EndCm;
					Request.RetailMask = OwnMask;
					const FElysiumRetailTraceResult Hit = Trace(Request);
					if (!bLastAnswered || Hit.Fraction >= 1.f || Hit.HitSignature != Signature
						|| Hit.ElementIndex != Convex.Index)
					{
						continue;
					}
					if (!bHaveFirst)
					{
						bHaveFirst = true;
						First = Ray;
					}
					if (Accept(Ray.StartCm, Ray.EndCm))
					{
						Out = Ray;
						return true;
					}
				}
			}
		}
		if (bHaveFirst)
		{
			Out = First;
		}
		return bHaveFirst;
	}

	// Retail's HUMAN_HULL (`ElysiumRetailHulls::Table` row 0 -- `RetailHullExtents(0, Full)`'s row),
	// converted from Source units to centimetres.
	static void HumanHullCm(FVector& OutMinsCm, FVector& OutMaxsCm)
	{
		const ElysiumRetailHulls::FRow* Row = ElysiumRetailHulls::Find(0);
		OutMinsCm = Row != nullptr ? Row->Mins * ElysiumMove::U : FVector::ZeroVector;
		OutMaxsCm = Row != nullptr ? Row->Maxs * ElysiumMove::U : FVector::ZeroVector;
	}

private:
	bool TraceInto(const FElysiumRetailTrace& Request, FElysiumRetailTraceResult& Out)
	{
		// The fixture holds no entities: a hit body is never an entity, so every actor maps to none.
		const auto NoEntity = [](const AActor*) { return FElysiumEntityHandle::Invalid(); };
		bLastAnswered = Scene.World != nullptr
			&& ElysiumWorldGeometry::Trace(*Scene.World, Request, Out, nullptr, NoEntity);
		return bLastAnswered;
	}

	TStrongObjectPtr<UElysiumMapCollisionPayload> Payload;
	AElysiumWorldCollisionActor* Actor = nullptr;
	TMap<uint8, TArray<FElysiumGeometryConvex>> BySignature;
};

#endif   // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
