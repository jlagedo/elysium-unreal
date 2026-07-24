#include "ElysiumPick.h"

#if !UE_BUILD_SHIPPING

#include "ElysiumBrushComponent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDebugSubsystem.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGizmoColor.h"
#include "ElysiumMapActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

namespace
{
	constexpr double PickReach = 100000.0;   // 1 km, the same reach as the ent_* crosshair picker
	constexpr int32  MaxFloodTris = 4096;    // bound the face flood (a displacement patch is one face)

	// --- ray primitives ----------------------------------------------------------------------

	// Moller-Trumbore, two-sided: a click through the back of a face should still resolve rather
	// than fall through to whatever is behind it. Returns the ray parameter, so `t` stays
	// comparable across the local spaces each caster works in.
	bool RayTri(const FVector& O, const FVector& D,
		const FVector& A, const FVector& B, const FVector& C, double& OutT)
	{
		const FVector E1 = B - A;
		const FVector E2 = C - A;
		const FVector P = D ^ E2;
		const double Det = E1 | P;
		if (FMath::Abs(Det) < UE_DOUBLE_SMALL_NUMBER)
		{
			return false;   // ray parallel to the triangle plane
		}
		const double Inv = 1.0 / Det;
		const FVector T = O - A;
		const double U = (T | P) * Inv;
		if (U < 0.0 || U > 1.0)
		{
			return false;
		}
		const FVector Q = T ^ E1;
		const double V = (D | Q) * Inv;
		if (V < 0.0 || U + V > 1.0)
		{
			return false;
		}
		const double Dist = (E2 | Q) * Inv;
		if (Dist <= 0.0)
		{
			return false;
		}
		OutT = Dist;
		return true;
	}

	// Slab test, reporting the entry parameter. D is never normalized here (it comes through
	// inverse transforms unnormalized so `t` stays in the world parameterization), so the
	// reciprocal is taken with a floor to keep an axis-aligned ray from producing 0 * inf = NaN.
	bool RayBoxEnter(const FVector& O, const FVector& D, const FBox& Box, double MaxT, double& OutT)
	{
		if (!Box.IsValid)
		{
			return false;
		}
		double T0 = 0.0;
		double T1 = MaxT;
		for (int32 A = 0; A < 3; ++A)
		{
			const double Axis = FMath::Abs(D[A]) < UE_DOUBLE_SMALL_NUMBER
				? (D[A] < 0.0 ? -UE_DOUBLE_SMALL_NUMBER : UE_DOUBLE_SMALL_NUMBER) : D[A];
			const double Inv = 1.0 / Axis;
			const double Lo = (Box.Min[A] - O[A]) * Inv;
			const double Hi = (Box.Max[A] - O[A]) * Inv;
			T0 = FMath::Max(T0, FMath::Min(Lo, Hi));
			T1 = FMath::Min(T1, FMath::Max(Lo, Hi));
		}
		OutT = T0;
		return T1 >= T0;
	}

	bool RayBox(const FVector& O, const FVector& D, const FBox& Box, double MaxT)
	{
		double Unused;
		return RayBoxEnter(O, D, Box, MaxT, Unused);
	}

	// --- highlight geometry ------------------------------------------------------------------

	// Append a box's 12 edges (as a line list) and 12 triangles (as a triangle list), both in
	// world space. Corners come from the local box through Xform, so a rotated body reads as an
	// oriented box rather than a world-axis one.
	void AppendBox(const FBox& Local, const FTransform& Xform,
		TArray<FVector>& OutSegs, TArray<FVector>* OutTris)
	{
		FVector C[8];
		for (int32 I = 0; I < 8; ++I)
		{
			const FVector P(
				(I & 1) ? Local.Max.X : Local.Min.X,
				(I & 2) ? Local.Max.Y : Local.Min.Y,
				(I & 4) ? Local.Max.Z : Local.Min.Z);
			C[I] = Xform.TransformPosition(P);
		}
		static const int32 Edges[12][2] = {
			{0,1},{2,3},{4,5},{6,7},   // along X
			{0,2},{1,3},{4,6},{5,7},   // along Y
			{0,4},{1,5},{2,6},{3,7} }; // along Z
		for (const int32(&E)[2] : Edges)
		{
			OutSegs.Add(C[E[0]]);
			OutSegs.Add(C[E[1]]);
		}
		if (OutTris != nullptr)
		{
			static const int32 Faces[12][3] = {
				{0,1,3},{0,3,2}, {4,7,5},{4,6,7},
				{0,5,1},{0,4,5}, {2,3,7},{2,7,6},
				{0,2,6},{0,6,4}, {1,5,7},{1,7,3} };
			for (const int32(&F)[3] : Faces)
			{
				OutTris->Add(C[F[0]]);
				OutTris->Add(C[F[1]]);
				OutTris->Add(C[F[2]]);
			}
		}
	}

	uint64 EdgeKey(int32 A, int32 B)
	{
		const uint32 Lo = static_cast<uint32>(FMath::Min(A, B));
		const uint32 Hi = static_cast<uint32>(FMath::Max(A, B));
		return (static_cast<uint64>(Hi) << 32) | Lo;
	}

	// Flood from the hit triangle to the whole BSP face it belongs to, and emit that face's
	// boundary as a line list.
	//
	// This works on shared vertex indices alone because of how the geometry is produced:
	// UE_bsp_to_scene.py's `emit()` appends a fresh vertex per face corner (no dedup across
	// faces), and BuildMeshFromObj remaps a section keyed on the *global* index — so the
	// triangles of one face share local indices and adjacent faces share none. The flood
	// therefore stops at the face boundary on its own; no position weld, no risk of running
	// around a corner into the next wall. The coplanarity test only bites on displacement grids,
	// where one "face" is a whole curved patch.
	void FloodFace(const FProcMeshSection& S, int32 HitTri,
		TArray<int32>& OutTris, TArray<int32>& OutBoundary)
	{
		const TArray<uint32>& Idx = S.ProcIndexBuffer;
		const int32 NumTris = Idx.Num() / 3;
		if (!Idx.IsValidIndex(HitTri * 3 + 2))
		{
			return;
		}

		auto TriNormal = [&S, &Idx](int32 Tri)
		{
			const FVector& A = S.ProcVertexBuffer[Idx[Tri * 3 + 0]].Position;
			const FVector& B = S.ProcVertexBuffer[Idx[Tri * 3 + 1]].Position;
			const FVector& C = S.ProcVertexBuffer[Idx[Tri * 3 + 2]].Position;
			return ((B - A) ^ (C - A)).GetSafeNormal();
		};

		TMultiMap<uint64, int32> EdgeToTri;
		EdgeToTri.Reserve(NumTris * 3);
		for (int32 T = 0; T < NumTris; ++T)
		{
			for (int32 K = 0; K < 3; ++K)
			{
				EdgeToTri.Add(EdgeKey(Idx[T * 3 + K], Idx[T * 3 + (K + 1) % 3]), T);
			}
		}

		const FVector FaceNormal = TriNormal(HitTri);
		TSet<int32> Visited;
		TArray<int32> Frontier;
		Visited.Add(HitTri);
		Frontier.Add(HitTri);

		TArray<int32> Neighbours;
		while (Frontier.Num() > 0 && Visited.Num() < MaxFloodTris)
		{
			const int32 T = Frontier.Pop(EAllowShrinking::No);
			for (int32 K = 0; K < 3; ++K)
			{
				Neighbours.Reset();
				EdgeToTri.MultiFind(EdgeKey(Idx[T * 3 + K], Idx[T * 3 + (K + 1) % 3]), Neighbours);
				for (int32 N : Neighbours)
				{
					if (Visited.Contains(N))
					{
						continue;
					}
					if (FMath::Abs(TriNormal(N) | FaceNormal) < 0.9995)
					{
						continue;   // displacement curvature: stop at the coplanar patch
					}
					Visited.Add(N);
					Frontier.Add(N);
				}
			}
		}

		OutTris = Visited.Array();

		// Boundary = every edge used by exactly one triangle of the flooded set.
		TMap<uint64, int32> Uses;
		for (int32 T : OutTris)
		{
			for (int32 K = 0; K < 3; ++K)
			{
				Uses.FindOrAdd(EdgeKey(Idx[T * 3 + K], Idx[T * 3 + (K + 1) % 3]))++;
			}
		}
		for (int32 T : OutTris)
		{
			for (int32 K = 0; K < 3; ++K)
			{
				const int32 A = Idx[T * 3 + K];
				const int32 B = Idx[T * 3 + (K + 1) % 3];
				if (Uses[EdgeKey(A, B)] == 1)
				{
					OutBoundary.Add(A);
					OutBoundary.Add(B);
				}
			}
		}
	}

	// --- the three casters -------------------------------------------------------------------

	struct FSurfaceHit
	{
		UProceduralMeshComponent* Mesh = nullptr;
		int32 Section = INDEX_NONE;
		int32 Triangle = INDEX_NONE;
	};

	// CPU cast against a procedural mesh's CPU-side sections. Needed because with the default
	// elysium.BrushCollision 1 the world render mesh is built with collision off — physics can
	// only report the .hulls collider, which carries no material and no face.
	void CastProcMesh(UProceduralMeshComponent* Mesh, const FVector& O, const FVector& D,
		double& BestT, FSurfaceHit& Out)
	{
		if (Mesh == nullptr || !Mesh->IsVisible())
		{
			return;
		}
		const FTransform Xform = Mesh->GetComponentTransform();
		const FVector LO = Xform.InverseTransformPosition(O);
		const FVector LD = Xform.InverseTransformVector(D);

		for (int32 SecIdx = 0; SecIdx < Mesh->GetNumSections(); ++SecIdx)
		{
			const FProcMeshSection* S = Mesh->GetProcMeshSection(SecIdx);
			if (S == nullptr || !S->bSectionVisible || S->ProcIndexBuffer.Num() < 3)
			{
				continue;
			}
			if (!RayBox(LO, LD, S->SectionLocalBox, BestT))
			{
				continue;
			}
			const TArray<uint32>& Idx = S->ProcIndexBuffer;
			const TArray<FProcMeshVertex>& Verts = S->ProcVertexBuffer;
			const int32 NumTris = Idx.Num() / 3;
			for (int32 T = 0; T < NumTris; ++T)
			{
				double Hit;
				if (RayTri(LO, LD,
					Verts[Idx[T * 3 + 0]].Position,
					Verts[Idx[T * 3 + 1]].Position,
					Verts[Idx[T * 3 + 2]].Position, Hit) && Hit < BestT)
				{
					BestT = Hit;
					Out.Mesh = Mesh;
					Out.Section = SecIdx;
					Out.Triangle = T;
				}
			}
		}
	}

	struct FPropHit
	{
		UInstancedStaticMeshComponent* Ism = nullptr;
		int32 Soup = INDEX_NONE;
		int32 Instance = INDEX_NONE;
		int32 Triangle = INDEX_NONE;
		FTransform Xform;
	};

	// CPU cast against every prop instance, triangle-exact. A solid prop's cooked collision is a
	// single convex hull of the whole model (ElysiumStaticMesh.cpp) and non-solid props have none
	// at all, so physics can neither pick a non-solid prop nor tell a railing from its bounding
	// blob. The ray goes into instance space so `t` stays comparable with the other casters.
	void CastProps(const AElysiumMapActor& Map, const FVector& O, const FVector& D,
		double& BestT, FPropHit& Out)
	{
		const TArray<AElysiumMapActor::FPropPickSoup>& Soups = Map.GetPropPickSoups();
		for (UInstancedStaticMeshComponent* Ism : Map.GetPropComponents())
		{
			if (Ism == nullptr || !Ism->IsVisible() || Ism->GetStaticMesh() == nullptr)
			{
				continue;
			}
			const int32 SoupIdx = Map.GetPropSoupIndex(Ism);
			if (!Soups.IsValidIndex(SoupIdx))
			{
				continue;
			}
			const AElysiumMapActor::FPropPickSoup& Soup = Soups[SoupIdx];
			const FBox LocalBounds = Ism->GetStaticMesh()->GetBounds().GetBox();

			const int32 NumInst = Ism->GetInstanceCount();
			for (int32 I = 0; I < NumInst; ++I)
			{
				FTransform Xform;
				if (!Ism->GetInstanceTransform(I, Xform, /*bWorldSpace*/ true))
				{
					continue;
				}
				const FVector LO = Xform.InverseTransformPosition(O);
				const FVector LD = Xform.InverseTransformVector(D);
				if (!RayBox(LO, LD, LocalBounds, BestT))
				{
					continue;
				}
				for (int32 T = 0; T + 2 < Soup.Tris.Num(); T += 3)
				{
					const int32 A = Soup.Tris[T], B = Soup.Tris[T + 1], C = Soup.Tris[T + 2];
					if (!Soup.Positions.IsValidIndex(A) || !Soup.Positions.IsValidIndex(B)
						|| !Soup.Positions.IsValidIndex(C))
					{
						continue;
					}
					double Hit;
					if (RayTri(LO, LD, Soup.Positions[A], Soup.Positions[B], Soup.Positions[C], Hit)
						&& Hit < BestT)
					{
						BestT = Hit;
						Out.Ism = Ism;
						Out.Soup = SoupIdx;
						Out.Instance = I;
						Out.Triangle = T / 3;
						Out.Xform = Xform;
					}
				}
			}
		}
	}

	AElysiumMapActor* FindMapActor(UWorld* World)
	{
		for (TActorIterator<AElysiumMapActor> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	struct FGizmoHit
	{
		const FElysiumEntity* Ent = nullptr;
		double T = 0.0;
	};

	// Every gizmo marker the ray passes through, near->far. The markers are a NoCollision ISM, so
	// there is nothing to physics-trace; they are axis-aligned 28 cm boxes on the shared anchor, so
	// a slab test per live entity is the whole job (~1,900 boxes on the tutorial).
	//
	// Dead entities are skipped: the layer keeps their instance but writes alpha 0, so they are not
	// on screen and must not be pickable. Hidden/dormant ones are only dimmed, so they stay pickable.
	void CastGizmos(const FElysiumEntityWorld& EW, const FVector& O, const FVector& D,
		TArray<FGizmoHit>& Out)
	{
		const FVector Extent(ElysiumGizmoHalfExtent);
		for (const TUniquePtr<FElysiumEntity>& EntPtr : EW.Entities())
		{
			const FElysiumEntity* E = EntPtr.Get();
			if (E == nullptr || E->IsDead() || E->Def == nullptr)
			{
				continue;
			}
			const FVector Center = ElysiumGizmoAnchor(*E);
			double T;
			if (RayBoxEnter(O, D, FBox(Center - Extent, Center + Extent), PickReach, T))
			{
				Out.Add({ E, T });
			}
		}
		Out.Sort([](const FGizmoHit& A, const FGizmoHit& B) { return A.T < B.T; });
	}
}

bool ElysiumPick::Trace(UWorld* World, const FVector& Origin, const FVector& Dir,
	FElysiumPickResult& Out, bool bBuildFaceOutline, const FElysiumEntityHandle& CycleAfter)
{
	Out.Reset();
	if (World == nullptr || Dir.IsNearlyZero())
	{
		return false;
	}
	const FVector D = Dir.GetSafeNormal();
	const FVector End = Origin + D * PickReach;
	AElysiumMapActor* Map = FindMapActor(World);
	FElysiumEntityWorld* EW = Map ? Map->GetEntityWorld() : nullptr;

	// --- 1) prop instances + world/sky surfaces (CPU) ----------------------------------------
	// These run first and share one cap, so RenderT ends up as the distance to the nearest thing
	// that actually renders. The gizmo depth test below needs exactly that: brush bodies draw
	// nothing, so an invisible trigger volume must not occlude a gizmo marker behind it.
	double RenderT = PickReach;
	FPropHit Prop;
	FSurfaceHit Surface;
	if (Map != nullptr)
	{
		CastProps(*Map, Origin, D, RenderT, Prop);
		CastProcMesh(Map->GetWorldMesh(), Origin, D, RenderT, Surface);
		CastProcMesh(Map->GetSkyMesh(), Origin, D, RenderT, Surface);
	}

	// --- 2) entity brush bodies (physics) ----------------------------------------------------
	// A multi-trace returns trigger overlaps as well as blocking hits in near->far order, so an
	// invisible trigger volume is pickable and a wall still occludes what is behind it.
	double BodyT = PickReach;
	UElysiumBrushComponent* HitBody = nullptr;
	FVector BodyPoint = FVector::ZeroVector;
	FVector BodyNormal = FVector::ZeroVector;
	{
		FCollisionQueryParams Params(FName(TEXT("ElysiumPick")), /*bTraceComplex*/ false);
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			Params.AddIgnoredActor(PC->GetPawn());
		}
		TArray<FHitResult> Hits;
		World->LineTraceMultiByChannel(Hits, Origin, End, ECC_Visibility, Params);
		for (const FHitResult& H : Hits)
		{
			UElysiumBrushComponent* B = Cast<UElysiumBrushComponent>(H.GetComponent());
			if (B == nullptr)
			{
				continue;
			}
			const double T = H.Distance > 0.0 ? H.Distance : FVector::Dist(Origin, H.ImpactPoint);
			if (T < BodyT)
			{
				BodyT = T;
				HitBody = B;
				BodyPoint = H.ImpactPoint;
				BodyNormal = H.ImpactNormal;
			}
		}
	}

	// --- 3) World Viz gizmo markers ----------------------------------------------------------
	// A gizmo outranks whatever it is drawn over: it is a deliberate handle, and for the ~1,000
	// bodiless entities on a map (lights, ambient_generic, logic) it is the only clickable
	// representation there is. "Drawn" is the whole test — Visible depth-tests the markers, so
	// one behind geometry is not on screen and must not pick; All draws them x-ray, so any does.
	const UElysiumEntityDebugSubsystem* Dbg = World->GetSubsystem<UElysiumEntityDebugSubsystem>();
	const UElysiumEntityDebugSubsystem::EGizmoMode GizmoMode = Dbg
		? Dbg->Viz().GizmoMode : UElysiumEntityDebugSubsystem::EGizmoMode::Off;
	const bool bGizmosOn = GizmoMode != UElysiumEntityDebugSubsystem::EGizmoMode::Off;

	if (EW != nullptr && bGizmosOn)
	{
		TArray<FGizmoHit> Gizmos;
		CastGizmos(*EW, Origin, D, Gizmos);
		if (GizmoMode == UElysiumEntityDebugSubsystem::EGizmoMode::Visible)
		{
			// Depth-tested: drop the markers geometry hides. The tolerance covers a marker
			// sunk into the surface it sits on.
			const double Cutoff = RenderT + ElysiumGizmoHalfExtent;
			Gizmos.RemoveAll([Cutoff](const FGizmoHit& G) { return G.T > Cutoff; });
		}

		if (Gizmos.Num() > 0)
		{
			// Stacked markers: step past the one already selected so clicking a cluster walks
			// through it. Not found (a different cluster, or the hover preview) -> nearest.
			int32 Index = 0;
			if (CycleAfter.IsSet())
			{
				const int32 Prev = Gizmos.IndexOfByPredicate(
					[&CycleAfter](const FGizmoHit& G) { return G.Ent->Handle == CycleAfter; });
				if (Prev != INDEX_NONE)
				{
					Index = (Prev + 1) % Gizmos.Num();
				}
			}
			const FElysiumEntity& Ent = *Gizmos[Index].Ent;

			Out.Kind = EElysiumPickKind::Entity;
			Out.bViaGizmo = true;
			Out.Entity = Ent.Handle;
			Out.Component = Ent.Body;               // null for a bodiless entity
			Out.bHasComponent = Out.Component.IsValid();
			Out.Distance = Gizmos[Index].T;
			Out.HitPoint = Origin + D * Gizmos[Index].T;
			Out.Center = ElysiumGizmoAnchor(Ent);
			Out.Label = Ent.DebugString();

			// The marker itself, so it is obvious which cube answered the click...
			AppendBox(FBox(FVector(-ElysiumGizmoHalfExtent), FVector(ElysiumGizmoHalfExtent)),
				FTransform(Out.Center), Out.OutlineSegs, &Out.FillTris);
			// ...plus the entity's real volume where it has one, which is the useful part when
			// the marker sits in the middle of a big trigger brush.
			if (Ent.Body != nullptr && Ent.Def != nullptr)
			{
				const FTransform BodyXform = Ent.Body->GetComponentTransform();
				for (const FElysiumConvexHull& Hull : Ent.Def->Hulls)
				{
					if (Hull.Vertices.Num() == 0)
					{
						continue;
					}
					FBox Local(ForceInit);
					for (const FVector& V : Hull.Vertices)
					{
						Local += V;
					}
					AppendBox(Local, BodyXform, Out.OutlineSegs, nullptr);
				}
			}
			return true;
		}
	}

	// Nearest of the three geometry sources wins. Surface and Prop shared one cap, so at most one
	// of them can be the nearest; a body only wins if it beat both.
	double BestT = RenderT;
	if (BodyT < RenderT)
	{
		BestT = BodyT;
		Prop.Ism = nullptr;
		Surface.Mesh = nullptr;
		Out.HitPoint = BodyPoint;
		Out.HitNormal = BodyNormal;
	}
	else
	{
		HitBody = nullptr;
	}
	if (Surface.Mesh != nullptr)
	{
		Prop.Ism = nullptr;
	}

	// --- resolve the winner into a result ----------------------------------------------------
	if (Surface.Mesh != nullptr)
	{
		const FProcMeshSection* S = Surface.Mesh->GetProcMeshSection(Surface.Section);
		if (S == nullptr)
		{
			return false;
		}
		const FTransform Xform = Surface.Mesh->GetComponentTransform();
		const TArray<uint32>& Idx = S->ProcIndexBuffer;

		Out.Kind = EElysiumPickKind::WorldSurface;
		Out.Component = Surface.Mesh;
		Out.bHasComponent = Out.Component.IsValid();
		Out.Material = Surface.Mesh->GetMaterial(Surface.Section);
		Out.MaterialName = Map->GetSectionMaterialName(Surface.Mesh, Surface.Section);
		Out.Section = Surface.Section;
		Out.Triangle = Surface.Triangle;
		Out.HitPoint = Origin + D * BestT;
		Out.Distance = BestT;

		TArray<int32> FaceTris;
		TArray<int32> Boundary;
		if (bBuildFaceOutline)
		{
			FloodFace(*S, Surface.Triangle, FaceTris, Boundary);
		}
		if (FaceTris.Num() == 0)
		{
			FaceTris.Add(Surface.Triangle);   // hover / flood failure: just the hit triangle
			for (int32 K = 0; K < 3; ++K)
			{
				Boundary.Add(Idx[Surface.Triangle * 3 + K]);
				Boundary.Add(Idx[Surface.Triangle * 3 + (K + 1) % 3]);
			}
		}

		FVector Centroid = FVector::ZeroVector;
		Out.FillTris.Reserve(FaceTris.Num() * 3);
		for (int32 T : FaceTris)
		{
			for (int32 K = 0; K < 3; ++K)
			{
				const FVector P = Xform.TransformPosition(S->ProcVertexBuffer[Idx[T * 3 + K]].Position);
				Out.FillTris.Add(P);
				Centroid += P;
			}
		}
		Out.Center = Out.FillTris.Num() > 0 ? Centroid / Out.FillTris.Num() : Out.HitPoint;
		Out.OutlineSegs.Reserve(Boundary.Num());
		for (int32 V : Boundary)
		{
			Out.OutlineSegs.Add(Xform.TransformPosition(S->ProcVertexBuffer[V].Position));
		}

		const FVector& A = S->ProcVertexBuffer[Idx[Surface.Triangle * 3 + 0]].Position;
		const FVector& B = S->ProcVertexBuffer[Idx[Surface.Triangle * 3 + 1]].Position;
		const FVector& C = S->ProcVertexBuffer[Idx[Surface.Triangle * 3 + 2]].Position;
		Out.HitNormal = Xform.TransformVectorNoScale(((B - A) ^ (C - A)).GetSafeNormal());

		Out.Label = FString::Printf(TEXT("%s  [section %d, %d tri]"),
			Out.MaterialName.IsEmpty() ? TEXT("(world surface)") : *Out.MaterialName,
			Surface.Section, FaceTris.Num());
		return true;
	}

	if (Prop.Ism != nullptr)
	{
		const AElysiumMapActor::FPropPickSoup& Soup = Map->GetPropPickSoups()[Prop.Soup];
		Out.Kind = EElysiumPickKind::PropInstance;
		Out.Component = Prop.Ism;
		Out.bHasComponent = Out.Component.IsValid();
		Out.Material = Prop.Ism->GetMaterial(0);
		Out.ModelName = Soup.Model;
		Out.Instance = Prop.Instance;
		Out.Triangle = Prop.Triangle;
		Out.HitPoint = Origin + D * BestT;
		Out.Distance = BestT;

		// The box says which prop; the lit triangle says exactly where the ray landed on it.
		const FBox LocalBounds = Prop.Ism->GetStaticMesh()->GetBounds().GetBox();
		AppendBox(LocalBounds, Prop.Xform, Out.OutlineSegs, nullptr);
		Out.Center = Prop.Xform.TransformPosition(LocalBounds.GetCenter());
		const int32 T = Prop.Triangle * 3;
		if (Soup.Tris.IsValidIndex(T + 2))
		{
			for (int32 K = 0; K < 3; ++K)
			{
				Out.FillTris.Add(Prop.Xform.TransformPosition(Soup.Positions[Soup.Tris[T + K]]));
			}
		}
		Out.Label = FString::Printf(TEXT("%s  #%d"), *Soup.Model, Prop.Instance);
		return true;
	}

	if (HitBody != nullptr && EW != nullptr)
	{
		const FElysiumEntity* Ent = EW->Resolve(HitBody->GetOwningEntity());
		Out.Kind = EElysiumPickKind::Entity;
		Out.Entity = HitBody->GetOwningEntity();
		Out.Component = HitBody;
		Out.bHasComponent = Out.Component.IsValid();
		Out.Distance = BestT;
		Out.Center = HitBody->GetComponentLocation();

		// One box per convex hull of the def. The hulls are vertex sets with no faces, so the
		// highlight is each hull's local AABB — exact for the axis-aligned box brushes that make
		// up nearly every trigger and door, and a tight over-approximation otherwise.
		if (Ent != nullptr && Ent->Def != nullptr)
		{
			const FTransform Xform = HitBody->GetComponentTransform();
			for (const FElysiumConvexHull& Hull : Ent->Def->Hulls)
			{
				if (Hull.Vertices.Num() == 0)
				{
					continue;
				}
				FBox Local(ForceInit);
				for (const FVector& V : Hull.Vertices)
				{
					Local += V;
				}
				AppendBox(Local, Xform, Out.OutlineSegs, &Out.FillTris);
			}
			Out.Label = Ent->DebugString();
		}
		else
		{
			Out.Label = TEXT("(stale brush body)");
		}
		return true;
	}

	// --- last resort: a bodiless logic entity near the ray -----------------------------------
	// Logic entities have no geometry to click, so (as the ent_* picker does) the one whose origin
	// lies nearest the ray wins, capped so an off-screen entity is never silently selected. This
	// only runs with gizmos off: a 2 m perpendicular tolerance is far looser than the 28 cm marker
	// box, so leaving it armed alongside the gizmos would undo their precision.
	if (EW != nullptr && !bGizmosOn)
	{
		constexpr double MaxPerp = 200.0;
		double BestPerp = MaxPerp;
		const FElysiumEntity* Best = nullptr;
		for (const TUniquePtr<FElysiumEntity>& EntPtr : EW->Entities())
		{
			const FElysiumEntity* E = EntPtr.Get();
			if (E == nullptr || E->IsDead() || E->Body != nullptr || E->Def == nullptr)
			{
				continue;
			}
			const double T = (E->Def->Origin - Origin) | D;
			if (T < 0.0 || T > BestT)
			{
				continue;   // behind the camera, or past whatever the ray already hit
			}
			const double Perp = FVector::Dist(E->Def->Origin, Origin + D * T);
			if (Perp < BestPerp)
			{
				BestPerp = Perp;
				Best = E;
			}
		}
		if (Best != nullptr)
		{
			Out.Kind = EElysiumPickKind::Entity;
			Out.Entity = Best->Handle;
			Out.HitPoint = Best->Def->Origin;
			Out.Center = Best->Def->Origin;
			Out.Distance = FVector::Dist(Origin, Best->Def->Origin);
			Out.Label = Best->DebugString();
			AppendBox(FBox(FVector(-16.0), FVector(16.0)),
				FTransform(Best->Def->Origin), Out.OutlineSegs, &Out.FillTris);
			return true;
		}
	}

	return false;
}

#endif // !UE_BUILD_SHIPPING
