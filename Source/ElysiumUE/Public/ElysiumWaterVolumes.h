#pragma once

#include "CoreMinimal.h"
#include "ElysiumLocomotionSample.h"   // EElysiumWaterLevel — the body state ClassifyBody answers in
#include "Engine/Scene.h"              // FPostProcessSettings — a persistent by-value member
#include "GameFramework/Actor.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "ElysiumWaterVolumes.generated.h"

class FSceneView;
class UMaterialInstanceDynamic;
class USceneComponent;

// R7.1 (`docs/architecture/water-architecture.md` rulings B/C/D, `seam_map_map.md` -> "Import —
// water volumes"): VtMB's water VOLUME, as the map lane stages it and the bake places it.
//
// The volume is the second of water's four stacked facts and the only one the runtime owns: the
// surface look is a material, the underside is a face set, and both are already drawn by the time
// this actor exists. What is here is `CONTENTS_WATER` — the convex brush hulls `CheckWater`'s
// `MASK_WATER` traces hit, the `LEAFWATERDATA` plane they belong to, and the volume's authored
// linear fog. One actor per map rather than one per volume: every query is "which volume is this
// point in", asked against all of them at once, three times a frame.

// One `%compilewater` brush as a convex plane set, in Unreal centimetres. A point is inside when
// every plane answers `n·p − d <= 0` (`FPlane::PlaneDot`); the AABB is the plane-intersection hull
// the stage already solved, kept so the common "nowhere near the water" answer costs one box test.
USTRUCT()
struct FElysiumWaterBrush
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FPlane> Planes;
	UPROPERTY(EditAnywhere, Category = "Elysium") FBox BoundsCm = FBox(ForceInit);
};

// One `LEAFWATERDATA` row: the surface plane, the floor, the material the surface draws with, and
// the fog `SetFogVolumeState` would have pushed while the eye is under it. `FogColor` is the
// authored value (/255), undecoded — the `.env` convention, decoded once by `ElysiumFog::Pack`.
USTRUCT()
struct FElysiumWaterVolume
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Elysium") int32 Index = INDEX_NONE;
	UPROPERTY(EditAnywhere, Category = "Elysium") float SurfaceZCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float MinZCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") FString Material;   // vtmb:material:<key>
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bFogEnabled = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") FLinearColor FogColor = FLinearColor::Black;
	UPROPERTY(EditAnywhere, Category = "Elysium") float FogStartCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float FogEndCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumWaterBrush> Brushes;
};

// The two queries, as plain functions over the rows: no actor, no world, no trace, so the whole
// classification is asserted with no map (`Elysium.Substrate.Water`) — the split
// `ElysiumCameraSolve.h` and `ElysiumMoveSolve.h` use. The actor below only forwards.
namespace ElysiumWater
{
	// `MASK_WATER` at a point: the index into `Volumes` of the first volume one of whose brushes
	// contains `PointCm`, or `INDEX_NONE`. Bounds first, planes second.
	int32 FindVolumeAt(TConstArrayView<FElysiumWaterVolume> Volumes, const FVector& PointCm);

	// `CGameMovement::CheckWater`, in its own order: the feet decide whether the body is in water at
	// all, then the waist, then the eyes. `OutVolume` (optional) receives the volume the level was
	// decided by — the feet's at `Feet`, the waist's at `Waist`, the eyes' at `Eyes` — which is the
	// plane the camera's water offset measures against, and `INDEX_NONE` at `None`.
	EElysiumWaterLevel ClassifyBody(TConstArrayView<FElysiumWaterVolume> Volumes,
		const FVector& FeetCm, const FVector& WaistCm, const FVector& EyesCm,
		int32* OutVolume = nullptr);
}

// The bake-placed actor (tag `elysium.water`), adopted by `UElysiumMapVisuals::AdoptBakedLevel`.
//
// It is also the map's underwater post-process volume (ruling D). The engine walks every
// registered `IInterface_PostProcessVolume` per view and reads `bIsEnabled` before it ever calls
// `EncompassesPoint` (`World.cpp` DoPostProcessVolume), and a bounded volume's returned bool is
// discarded in favour of the distance it writes — so the gate is `bIsEnabled`, set per view by the
// `OnBeginPostProcessSettings` handler, and `EncompassesPoint` always answers distance 0. That is
// the Water plugin's own arrangement (`UUnderwaterPostProcessVolume`), which is the only shipped
// precedent for a volume whose shape the engine cannot evaluate itself.
UCLASS()
class ELYSIUMUE_API AElysiumWaterVolumes : public AActor, public IInterface_PostProcessVolume
{
	GENERATED_BODY()

public:
	AElysiumWaterVolumes();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** The staged rows, written by the bake through reflection (`bake_map_v2.py::_place_water`). */
	UPROPERTY(EditAnywhere, Category = "Elysium")
	TArray<FElysiumWaterVolume> Volumes;

	int32 FindVolumeAt(const FVector& PointCm) const
	{
		return ElysiumWater::FindVolumeAt(Volumes, PointCm);
	}
	EElysiumWaterLevel ClassifyBody(const FVector& FeetCm, const FVector& WaistCm,
		const FVector& EyesCm, int32* OutVolume = nullptr) const
	{
		return ElysiumWater::ClassifyBody(Volumes, FeetCm, WaistCm, EyesCm, OutVolume);
	}

	/**
	 * Resolve the view point against the rows and push the answer onto the post-process state: the
	 * `bIsEnabled` gate, and — when the point is in a volume — that volume's fog triple on the
	 * underwater MID. Returns the volume index, or `INDEX_NONE` outside.
	 *
	 * Its one production caller is the `OnBeginPostProcessSettings` handler, which adds the two
	 * `FSceneView` writes; it is separate so the state machine is assertable without building a
	 * scene view.
	 */
	int32 UpdateViewPostProcess(const FVector& ViewLocationCm);

	/** `/Game/ElysiumGenerated/Materials/V2/M_ElysiumUnderwater`, the one underwater master. */
	static const TCHAR* UnderwaterMasterPath();
	/** The MID the blendable holds, or null when the master has not been generated. */
	UMaterialInstanceDynamic* GetUnderwaterMID() const { return UnderwaterMID; }

	// IInterface_PostProcessVolume.
	virtual bool EncompassesPoint(FVector Point, float SphereRadius, float* OutDistanceToPoint) override;
	virtual FPostProcessVolumeProperties GetProperties() const override { return Properties; }
#if DEBUG_POST_PROCESS_VOLUME_ENABLE
	virtual FString GetDebugName() const override;
#endif

private:
	/** Where the actor stands. The rows are world-space, so nothing is drawn or offset through it. */
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	/** `UWorld::OnBeginPostProcessSettings`: the gate, the MID, and the renderer's two view flags. */
	void ComputeUnderwaterPostProcess(FVector ViewLocation, FSceneView* SceneView);

	/**
	 * The settings the blendable lives on, built once in `BeginPlay` and handed out by pointer
	 * every view — `FPostProcessVolumeProperties::Settings` is a raw pointer the engine reads
	 * after `GetProperties` returns, so it cannot be a temporary.
	 */
	FPostProcessSettings UnderwaterSettings;
	FPostProcessVolumeProperties Properties;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> UnderwaterMID;

	FDelegateHandle PostProcessHandle;
};
