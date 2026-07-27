#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityDebugSubsystem.h"   // EGizmoMode
#include "UObject/StrongObjectPtr.h"

class AActor;
class FElysiumEntity;
class FElysiumEntityWorld;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// P2.4 retained entity-gizmo layer — the answer to "avoid the per-frame CPU round trip". Instead of
// re-issuing ~1,200 DrawDebug boxes every frame (immediate mode: re-marshalled to the render thread
// each frame), this builds ONE UInstancedStaticMeshComponent of unit cubes at map load — one instance
// per entity, colour packed into per-instance custom data (RGBA floats 0..3, read by M_Gizmo). The
// GPU keeps the instance buffer; idle frames cost nothing but the instanced draw. When an entity's
// dormancy/liveness flips it fires FElysiumEntityWorld's visual-change hook, and only THAT one
// instance's custom data is re-uploaded — the event-driven "GPU dirty on change" the design calls for.
//
// Owned by UElysiumEntityDebugSubsystem (rebuilt per world epoch); the ISM is a component of the map
// actor, so it dies with the map. Debug-only: the whole class compiles only in non-Shipping (its one
// TU is guarded), and it draws nothing until the World Viz mode leaves Off.
class FElysiumGizmoLayer
{
public:
	~FElysiumGizmoLayer();

	// Build the ISM + one instance per live entity for this epoch, and install the world's
	// visual-change hook so later state flips dirty just the affected instance. Idempotent-safe:
	// tears down any prior build first. `Owner` is the map actor (the ISM attaches to it).
	void Rebuild(FElysiumEntityWorld& World, AActor* Owner);
	// Drop the ISM + hook (on epoch change / shutdown). The ISM also dies with the map actor.
	void Teardown();
	bool IsBuilt() const;

	// Off hides the component; Visible uses the depth-tested material (walls occlude); All uses the
	// x-ray material (drawn on top). Cheap: no-ops when the mode has not changed.
	void SetMode(UElysiumEntityDebugSubsystem::EGizmoMode Mode);

	// Apply the World Viz class filter (one bit per EElysiumGizmoClass). A filtered-out class goes
	// to zero alpha, which is the same lever a dead entity already uses — so this is a custom-data
	// re-upload over the existing instances, never a rebuild. No-ops when the mask has not changed.
	void SetClassMask(uint8 Mask);

private:
	// The world's visual-change hook target: recompute + re-upload this one entity's instance.
	void OnEntityVisualChanged(const FElysiumEntity& Ent);
	// Pack an entity's current colour+opacity (class colour; dimmed when hidden; transparent when
	// dead or filtered out by Mask) into the 4 per-instance custom-data floats.
	static void ComputeRGBA(const FElysiumEntity& Ent, uint8 Mask, float Out[4]);

	TWeakObjectPtr<UInstancedStaticMeshComponent> Ism;
	TStrongObjectPtr<UMaterialInstanceDynamic> DepthMID;   // Visible mode (depth-tested)
	TStrongObjectPtr<UMaterialInstanceDynamic> XRayMID;    // All mode (drawn on top)
	TArray<int32> EntityToInstance;                        // entity index -> ISM instance (INDEX_NONE = none)
	UElysiumEntityDebugSubsystem::EGizmoMode AppliedMode = UElysiumEntityDebugSubsystem::EGizmoMode::Off;
	bool bModeApplied = false;                             // force the first SetMode to apply
	uint8 AppliedClassMask = 0x3F;                         // ElysiumGizmoClassMaskAll
	// The world the instances were built from, so a mask change can re-read every entity's class.
	FElysiumEntityWorld* BuiltWorld = nullptr;
};
