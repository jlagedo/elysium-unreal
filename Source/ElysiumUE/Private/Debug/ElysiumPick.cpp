#include "Debug/ElysiumPick.h"

#if !UE_BUILD_SHIPPING

#include "ElysiumBakedTags.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDebugSubsystem.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Debug/ElysiumGizmoColor.h"
#include "ElysiumMapActor.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumPick, Log, All);

namespace
{
	constexpr double PickReach = 100000.0;   // 1 km, the same reach as the ent_* crosshair picker

	// Ray primitives.

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

	// Highlight geometry.

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

	// The geometry caster.

	// What the ray hit in the baked level. The map's geometry is real static-mesh assets now, so
	// this is one physics trace on the dedicated ElysiumPick channel rather than the CPU triangle
	// casts the runtime-built world needed. Complex tracing gives a face index, which is what turns
	// the hit back into a material slot — the string the Inspector actually reports.
	struct FBakedHit
	{
		UStaticMeshComponent* Comp = nullptr;
		bool bProp = false;                  // elysium.prop, vs a world/sky cell
		int32 Section = INDEX_NONE;          // material-slot index behind the hit face
		UMaterialInterface* Material = nullptr;
		FVector Point = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;

		bool IsSet() const { return Comp != nullptr; }
	};

	void CastBaked(UWorld* World, const FVector& O, const FVector& D, double& BestT, FBakedHit& Out)
	{
		FCollisionQueryParams Params(FName(TEXT("ElysiumPickGeo")), /*bTraceComplex*/ true);
		Params.bReturnFaceIndex = true;
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			Params.AddIgnoredActor(PC->GetPawn());
		}

		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, O, O + D * PickReach, ELYSIUM_PICK_CHANNEL, Params))
		{
			return;
		}
		UStaticMeshComponent* Comp = Cast<UStaticMeshComponent>(Hit.GetComponent());
		const AActor* Actor = Hit.GetActor();
		if (Comp == nullptr || Actor == nullptr)
		{
			return;
		}
		const double T = Hit.Distance > 0.0 ? Hit.Distance : FVector::Dist(O, Hit.ImpactPoint);
		if (T >= BestT)
		{
			return;
		}

		BestT = T;
		Out.Comp = Comp;
		Out.bProp = Actor->ActorHasTag(ElysiumBakedTags::Prop);
		Out.Point = Hit.ImpactPoint;
		Out.Normal = Hit.ImpactNormal;
		// Face index -> material slot. A mesh with no complex collision reports INDEX_NONE, which
		// still leaves a usable component-level hit.
		int32 Section = INDEX_NONE;
		Out.Material = (Hit.FaceIndex != INDEX_NONE)
			? Comp->GetMaterialFromCollisionFaceIndex(Hit.FaceIndex, Section)
			: Comp->GetMaterial(0);
		Out.Section = Section;
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

	// 1) The baked level's geometry.
	// This runs first so RenderT ends up as the distance to the nearest thing that actually
	// renders. The gizmo depth test below needs exactly that: brush bodies draw nothing, so an
	// invisible trigger volume must not occlude a gizmo marker behind it.
	double RenderT = PickReach;
	FBakedHit Baked;
	CastBaked(World, Origin, D, RenderT, Baked);

	// 2) Entity brush bodies (physics).
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

	// 3) World Viz gizmo markers.
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

	// Nearest of the two geometry sources wins. A brush body only wins if it beat the baked
	// geometry the ray passed through.
	double BestT = RenderT;
	if (BodyT < RenderT)
	{
		BestT = BodyT;
		Baked = FBakedHit();
		Out.HitPoint = BodyPoint;
		Out.HitNormal = BodyNormal;
	}
	else
	{
		HitBody = nullptr;
	}

	// Resolve the winner into a result.
	if (Baked.IsSet())
	{
		Out.Kind = Baked.bProp ? EElysiumPickKind::PropInstance : EElysiumPickKind::WorldSurface;
		Out.Component = Baked.Comp;
		Out.bHasComponent = Out.Component.IsValid();
		Out.Material = Baked.Material;
		Out.Section = Baked.Section;
		Out.HitPoint = Baked.Point;
		Out.HitNormal = Baked.Normal;
		Out.Distance = BestT;

		// The bake names each material slot after the OBJ group key ("<material>@<cubemap>"), so
		// the slot name is the same string the runtime-built world reports.
		UStaticMesh* Mesh = Baked.Comp->GetStaticMesh();
		if (Mesh != nullptr)
		{
			if (Mesh->GetStaticMaterials().IsValidIndex(Baked.Section))
			{
				Out.MaterialName = Mesh->GetStaticMaterials()[Baked.Section].MaterialSlotName.ToString();
			}
			Out.ModelName = Mesh->GetName();
		}

		// Highlight: the hit patch plus the actor's bounds. A baked static mesh keeps no CPU-side
		// geometry, so the exact BSP face the runtime path flood-filled is not recoverable here —
		// an oriented patch on the impact normal says precisely where the ray landed, and the box
		// says which cell or prop owns it.
		{
			const FVector N = Baked.Normal.GetSafeNormal();
			FVector U, V;
			N.FindBestAxisVectors(U, V);
			constexpr double Patch = 16.0;
			// Lifted off the surface so the fill does not z-fight the wall it describes.
			const FVector C = Baked.Point + N * 0.5;
			const FVector P00 = C - U * Patch - V * Patch;
			const FVector P10 = C + U * Patch - V * Patch;
			const FVector P11 = C + U * Patch + V * Patch;
			const FVector P01 = C - U * Patch + V * Patch;
			Out.FillTris = { P00, P10, P11, P00, P11, P01 };
			Out.OutlineSegs = { P00, P10, P10, P11, P11, P01, P01, P00 };

			const FBoxSphereBounds Bounds = Baked.Comp->Bounds;
			AppendBox(FBox(-Bounds.BoxExtent, Bounds.BoxExtent),
				FTransform(Bounds.Origin), Out.OutlineSegs, nullptr);
			Out.Center = Baked.Point;
		}

		Out.Label = Baked.bProp
			? FString::Printf(TEXT("%s"), *Out.ModelName)
			: FString::Printf(TEXT("%s  [slot %d]"),
				Out.MaterialName.IsEmpty() ? TEXT("(world surface)") : *Out.MaterialName, Baked.Section);
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

	// Last resort: a bodiless logic entity near the ray.
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

// The scriptable echo of the Inspector's click-pick. The Inspector is the interactive surface, but
// its ray comes from the mouse, so this is the only way a test — or an agent over
// elysium_console_exec — can exercise Trace at all. Aims down the player camera, like a crosshair
// pick, and logs what resolved.
static void ElysiumPickCommand(UWorld* World)
{
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC == nullptr || PC->PlayerCameraManager == nullptr)
	{
		UE_LOG(LogElysiumPick, Warning, TEXT("pick: no player camera"));
		return;
	}
	const FVector Origin = PC->PlayerCameraManager->GetCameraLocation();
	const FVector Dir = PC->PlayerCameraManager->GetCameraRotation().Vector();

	FElysiumPickResult Hit;
	if (!ElysiumPick::Trace(World, Origin, Dir, Hit, /*bBuildFaceOutline*/ true))
	{
		UE_LOG(LogElysiumPick, Log, TEXT("pick: nothing under the aim ray"));
		return;
	}

	const TCHAR* Kind = TEXT("none");
	switch (Hit.Kind)
	{
	case EElysiumPickKind::Entity:       Kind = Hit.bViaGizmo ? TEXT("entity (gizmo)") : TEXT("entity"); break;
	case EElysiumPickKind::WorldSurface: Kind = TEXT("world surface"); break;
	case EElysiumPickKind::PropInstance: Kind = TEXT("prop instance"); break;
	default: break;
	}
	const UPrimitiveComponent* Comp = Hit.Component.Get();
	UE_LOG(LogElysiumPick, Log,
		TEXT("pick: %s '%s' | component %s (%s) | section %d | at %s, %.1f cm | %d fill tris"),
		Kind, *Hit.Label,
		Comp ? *Comp->GetName() : TEXT("(none)"),
		Comp ? *Comp->GetClass()->GetName() : TEXT("-"),
		Hit.Section, *Hit.HitPoint.ToCompactString(), Hit.Distance,
		Hit.FillTris.Num() / 3);
}

static FAutoConsoleCommandWithWorld GElysiumPickCmd(
	TEXT("elysium.pick"),
	TEXT("elysium.pick — run the debug click-pick down the aim ray and log what it resolved to."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&ElysiumPickCommand));

#endif // !UE_BUILD_SHIPPING
