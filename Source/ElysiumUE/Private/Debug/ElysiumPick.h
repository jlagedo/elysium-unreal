#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

#if !UE_BUILD_SHIPPING

class UMaterialInterface;
class UPrimitiveComponent;
class UWorld;

// The channel the baked level's render geometry blocks, and nothing else does. Declared in
// Config/DefaultEngine.ini as ECC_GameTraceChannel2 = "ElysiumPick", a trace type whose default
// response is Ignore. Keep the two in sync. It is deliberately separate from ECC_Visibility: the
// walkable surface is the .hulls brush collider, which carries no material and no face, so a pick
// on a shared channel would report an invisible clip volume instead of the wall that was clicked.
inline constexpr ECollisionChannel ELYSIUM_PICK_CHANNEL = ECC_GameTraceChannel2;

// What a click resolved to. Ordered by how the pick reports it, not by priority.
enum class EElysiumPickKind : uint8
{
	None,
	Entity,         // a brush entity's body (UElysiumBrushComponent) — includes invisible triggers
	WorldSurface,   // one BSP face of the world (procedural mesh or chunked static mesh) or sky
	PropInstance,   // one instance of one static-prop model
};

// Click-pick result: what is under a screen ray, plus the world-space geometry the Cog
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

	FString ModelName;      // PropInstance: the baked mesh asset name ("SM_<stem>")
	FString MaterialName;   // the hit slot's name — the OBJ group key ("<material>@<cubemap>")

	int32 Section = INDEX_NONE;    // material-slot index behind the hit face

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
	// Failing a gizmo, the nearest of entity brush bodies and the baked level's geometry (world
	// cells, sky, props) wins.
	//
	// The geometry cast runs on the dedicated ElysiumPick channel because the walkable surface is
	// the .hulls brush collider, which carries no material and no face: a pick on a shared channel
	// would report the invisible clip volume instead of the wall that was clicked. It traces
	// complex, so the face index resolves back to a material slot — the string the Inspector
	// reports.
	//
	// bBuildFaceOutline is inert: it gated a flood-fill from the hit triangle out to the whole BSP
	// face, which needed the CPU-side section geometry only the runtime-built world had. A baked
	// static mesh keeps none, so every caller gets the same impact-plane patch.
	//
	// CycleAfter steps through stacked gizmos: pass the previously picked entity and the ray
	// returns the *next* gizmo behind it (wrapping), so clicking a cluster repeatedly walks it.
	// Pass Invalid — as the hover preview does — to always take the nearest.
	bool Trace(UWorld* World, const FVector& Origin, const FVector& Dir,
		FElysiumPickResult& Out, bool bBuildFaceOutline,
		const FElysiumEntityHandle& CycleAfter = FElysiumEntityHandle::Invalid());
}

#endif // !UE_BUILD_SHIPPING
