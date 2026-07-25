#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

#if !UE_BUILD_SHIPPING

class UMaterialInterface;
class UPrimitiveComponent;
class UWorld;

// What a click resolved to. Ordered by how the pick reports it, not by priority.
enum class EElysiumPickKind : uint8
{
	None,
	Entity,         // a brush entity's body (UElysiumBrushComponent) — includes invisible triggers
	WorldSurface,   // one BSP face of the world (procedural mesh or chunked static mesh) or sky
	PropInstance,   // one instance of one static-prop model
};

// P2.6 click-pick result: what is under a screen ray, plus the world-space geometry the Cog
// overlay draws to show it. Everything here is a snapshot — components are weak, so a map reload
// leaves a result that reports itself as stale rather than dangling.
struct FElysiumPickResult
{
	EElysiumPickKind Kind = EElysiumPickKind::None;

	// Set whenever the pick landed on something the Track-B substrate owns. A WorldSurface or
	// PropInstance pick leaves this unset — world brushes and props are not entities.
	FElysiumEntityHandle Entity;

	// The pick came from a World Viz gizmo marker rather than from geometry. Gizmos outrank
	// everything they are drawn over, so this is how a light, an ambient_generic, or any other
	// bodiless entity gets selected at all.
	bool bViaGizmo = false;

	TWeakObjectPtr<UPrimitiveComponent> Component;
	TWeakObjectPtr<UMaterialInterface> Material;   // the hit section's / model slot's MID

	// Whether Component was ever set. A weak pointer cannot tell "never had one" from "destroyed",
	// and a bodiless entity (a light, an ambient_generic — anything picked by its gizmo marker
	// alone) legitimately has none, so staleness needs this to avoid discarding it every frame.
	bool bHasComponent = false;

	FString ModelName;      // PropInstance: the .props model stem
	FString MaterialName;   // WorldSurface: the OBJ group key ("<material>@<cubemap>")

	int32 Section = INDEX_NONE;    // WorldSurface: section index within the picked component
	int32 Instance = INDEX_NONE;   // PropInstance: ISM instance index
	int32 Triangle = INDEX_NONE;   // the triangle actually hit, within the section / model

	FVector HitPoint = FVector::ZeroVector;
	FVector HitNormal = FVector::ZeroVector;
	double  Distance = 0.0;

	// Highlight geometry in world space. FillTris is a flat triangle list (3 verts per triangle);
	// OutlineSegs is a flat line list (2 verts per segment). Both may be empty.
	TArray<FVector> FillTris;
	TArray<FVector> OutlineSegs;
	FVector Center = FVector::ZeroVector;   // where the overlay pins the label
	FString Label;

	bool IsSet() const { return Kind != EElysiumPickKind::None; }
	// True once the picked component has been destroyed (map reload / unload). A pick that never
	// had a component is never stale by this test — the caller checks its entity handle instead,
	// which the epoch already invalidates across a reload.
	bool IsStale() const { return Kind != EElysiumPickKind::None && bHasComponent && !Component.IsValid(); }
	void Reset() { *this = FElysiumPickResult(); }
};

namespace ElysiumPick
{
	// Resolve what is under the ray and fill Out with it plus its highlight geometry. Returns
	// false (and resets Out) on a miss.
	//
	// Four sources. **World Viz gizmo markers win outright** whenever they are drawn, because a
	// gizmo is a deliberate "select me" handle — it is the only way to reach a light, an
	// ambient_generic, or any other bodiless entity. Whether a gizmo counts as drawn follows the
	// mode: in Visible the markers are depth-tested, so one behind geometry does not pick; in All
	// they are x-ray, so any of them does. With gizmos Off the source is skipped entirely.
	// Failing a gizmo, the nearest of entity brush bodies (physics), prop instances (CPU,
	// triangle-exact) and world/sky surfaces (CPU, triangle-exact) wins.
	//
	// The CPU casts exist because physics cannot answer these: under the default
	// elysium.BrushCollision 1 the world *render* mesh is built with collision off (the .hulls
	// convex set is the collider), and a solid prop's cooked collision is a single convex hull of
	// the whole model — neither can name the surface or the part of the prop that was clicked.
	// The world cast covers both shapes the world renders in — the single procedural mesh, and the
	// elysium.LumenCards path's chunked static meshes (whose CPU geometry the map actor retains,
	// since a runtime UStaticMesh keeps none).
	//
	// bBuildFaceOutline flows the WorldSurface highlight from the single hit triangle out to the
	// whole BSP face (see the flood in the .cpp). It costs an edge map over the section, so the
	// caller passes false for per-frame hover and true on commit.
	//
	// CycleAfter steps through stacked gizmos: pass the previously picked entity and the ray
	// returns the *next* gizmo behind it (wrapping), so clicking a cluster repeatedly walks it.
	// Pass Invalid — as the hover preview does — to always take the nearest.
	bool Trace(UWorld* World, const FVector& Origin, const FVector& Dir,
		FElysiumPickResult& Out, bool bBuildFaceOutline,
		const FElysiumEntityHandle& CycleAfter = FElysiumEntityHandle::Invalid());
}

#endif // !UE_BUILD_SHIPPING
