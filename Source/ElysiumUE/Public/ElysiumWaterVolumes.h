#pragma once

#include "CoreMinimal.h"
#include "ElysiumLocomotionSample.h"   // EElysiumWaterLevel — the body state ClassifyBody answers in
#include "ElysiumMoveSolve.h"          // ElysiumMove::U — VtMB's numbers are authored in Source units
#include "Engine/HitResult.h"          // FHitResult — the overlap handlers' reflected signature
#include "Engine/Scene.h"              // FPostProcessSettings — a persistent by-value member
#include "GameFramework/Actor.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "ElysiumWaterVolumes.generated.h"

class FSceneView;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class USceneComponent;

// R7.1, rulings B/C/D: VtMB's water VOLUME, as the map lane stages it and the bake places it.
//
// The volume is the second of water's four stacked facts and the only one the runtime owns: the
// surface look is a material, the underside is a face set, and both are already drawn by the time
// this actor exists. What is here is `CONTENTS_WATER` — the convex brush hulls `CheckWater`'s
// `MASK_WATER` traces hit, the `LEAFWATERDATA` plane they belong to, and the volume's authored
// linear fog. One actor per map rather than one per volume: every query is "which volume is this
// point in", asked against all of them at once, three times a frame.
//
// R7.4 (water-complete) adds the fluid row, the compiler's convex pieces, the leaf boxes and the
// near-water leaf set beside them: the carve the runtime tests, and the PVS neighbourhood the
// underwater lane and the audio seam ask "am I near water" against.

// R7.4 (G7/G17, Phase 0 verdict B5/D2): the `fluid` block on the water model's physics
// key-values, staged verbatim. VtMB creates a fluid controller when `index > 0` alone — not on
// contents, not on solid flags (`vampire.dll FUN_10158600`, `101586ff JLE skip`) — and both owner
// maps author `index "5"`, so the pier gets one too. `Contents` is parsed by the game DLL and read
// by nothing in it; it rides along because the stage publishes what the file says, not what the
// 2004 build happened to consume.
USTRUCT()
struct FElysiumWaterFluid
{
	GENERATED_BODY()

	// False on a volume whose model authored no `fluid` block: every other field is then the
	// member initialiser, and the runtime neither damps nor floats anything in it.
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bHasFluid = false;
	// Which `physics.models[0].solids[n]` the controller wraps. The creation guard, > 0.
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 Index = 0;
	// kg/m³, VtMB's own unit (water is 1000, which is also `PM_water`'s `rawDensity`).
	UPROPERTY(EditAnywhere, Category = "Elysium") float Density = 0.f;
	// vphysics' linear damping while a body is in the fluid.
	UPROPERTY(EditAnywhere, Category = "Elysium") float Damping = 0.f;
	// The authored surface, in the Unreal frame (cm): `n·p − d = 0`, normal up out of the water.
	UPROPERTY(EditAnywhere, Category = "Elysium") FPlane SurfacePlane = FPlane(0.0, 0.0, 1.0, 0.0);
	// `currentvelocity`, cm/s. A QUERY ONLY: nothing in this module applies it, because player
	// water MOVEMENT is out of scope by owner call — the seam exists so the movement lane can read
	// it when it lands. No corpus water authors a non-zero one.
	UPROPERTY(EditAnywhere, Category = "Elysium") FVector CurrentVelocityCm = FVector::ZeroVector;
	// The authored `contents` word, parsed and unread by the 2004 game DLL.
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 Contents = 0;
};

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

	// R7.4 (G7): the `fluid` block, or an unset row when the model authored none.
	UPROPERTY(EditAnywhere, Category = "Elysium") FElysiumWaterFluid Fluid;
	// R7.4 (G18): the COMPILER's convex decomposition of the same water solid
	// (`physics.models[0].solids`), which is the carve `CheckWater` actually traces against. Where
	// it is present it outranks `Brushes` in every point query: the authored brush is the pre-CSG
	// shape, and a canal cut out of it answers differently at the cut. Empty on a volume vbsp
	// emitted no solid for, and then `Brushes` is the whole answer. A piece row carries planes and
	// a box, like a brush row — but the box is the stage's, the AABB of the ledge's own vertices
	// (`map_geometry._solid_pieces`), because vbsp publishes no bounds for a ledge.
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumWaterBrush> Pieces;
	// R7.4 (G10): the AABBs of the `LEAFWATERDATA` leaves this volume stands in, clipped to the
	// brush. Diagnostic and coarse-reject data — the leaves are the compiler's own answer to
	// "where is this water", published beside the brush rather than instead of it.
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FBox> LeafBoxesCm;
	// R7.4 (G9/G23): the AABBs of every leaf in ⋃PVS(water clusters) — "you can see the water from
	// here". Derived from the visibility sub-unit, never read off the leaf `0x200` bit, because the
	// Unofficial-Patch recompile of `sm_pier_1` destroyed it (owner decision 1). This is what
	// `IsNearWater` answers, and it gates the underwater post-process walk and the audio seam's
	// near-water state.
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FBox> NearBoxesCm;
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

	// R7.4 (G9/G23): "can this point see water" — the index of the first volume one of whose
	// `NearBoxesCm` contains `PointCm`, or `INDEX_NONE`. A volume that publishes no near set (a
	// level baked before R7.4) falls back to its own brush bounds, so the answer degrades to
	// "inside the water" rather than to "never".
	int32 FindNearVolumeAt(TConstArrayView<FElysiumWaterVolume> Volumes, const FVector& PointCm);
	inline bool IsNearWater(TConstArrayView<FElysiumWaterVolume> Volumes, const FVector& PointCm)
	{
		return FindNearVolumeAt(Volumes, PointCm) != INDEX_NONE;
	}

	// --- the water-entry splash (G7, Phase 0 verdict D1) ------------------------------------------
	//
	// It is NOT the fluid controller's: `FUN_10151150` is Source's `PhysicsSplash` with both
	// `DispatchEffect` calls stripped, so the shipped build's controller emits nothing. The live
	// splash is the client's, keyed on the water LEVEL transition (`client.dll FUN_10099630`,
	// reached from `DrawModel`), and the port binds it to the transition rather than to a draw call.
	enum class ESplash : uint8
	{
		None = 0,
		// `watersplash_emitter`: wading, while `0 < waterLevel < 3`, on its own long cooldown.
		Wade = 1,
		// `waterbigsplash_emitter`: the entry, `0 -> >= 1` with enough downward speed.
		Big = 2,
	};

	// The two generated systems the effect lane stands for these (`NS_<root>`, R7.3 naming).
	inline const TCHAR* WadeSplashRoot = TEXT("watersplash_emitter");
	inline const TCHAR* BigSplashRoot = TEXT("waterbigsplash_emitter");

	// VtMB's own numbers, authored in Source units and converted once here.
	// Entry: `velocity.z < -200 in/s`.
	inline constexpr float BigEntryVelocityZCmPerSec = -200.f * ElysiumMove::U;
	// Wade: horizontal speed `>= 50 in/s`.
	inline constexpr float WadeSpeedCmPerSec = 50.f * ElysiumMove::U;
	// The spawn point leads the body: `origin - vel.xy * 0.035`, a time, not a distance.
	inline constexpr float SplashLeadSeconds = 0.035f;
	// The wade cooldown, seconds: `5.0 - horizontalSpeedInPerSec * 7.8e-5`.
	inline constexpr float WadeCooldownBaseSeconds = 5.f;
	inline constexpr float WadeCooldownSpeedTerm = 7.8e-5f;
	// D2: one splash per entity per half second, whatever the two rules above decide. VtMB carries
	// it as the `m_iEFlags 0x80000` in-fluid bit plus its own re-entry guard.
	inline constexpr float SplashRetriggerSeconds = 0.5f;

	// "this entity has never splashed" — far enough back that every first frame passes both gates.
	inline constexpr double NeverSeconds = -1.0e30;

	inline float WadeCooldownSeconds(float HorizontalSpeedCmPerSec)
	{
		return WadeCooldownBaseSeconds
			- (HorizontalSpeedCmPerSec / ElysiumMove::U) * WadeCooldownSpeedTerm;
	}

	// Everything the decision reads, so it is a pure function of numbers the caller already has.
	// `NowSeconds` is the caller's own accumulated clock, not wall time.
	struct FSplashInput
	{
		int32 PreviousLevel = 0;
		int32 Level = 0;
		FVector OriginCm = FVector::ZeroVector;
		FVector VelocityCmPerSec = FVector::ZeroVector;
		float SurfaceZCm = 0.f;
		double NowSeconds = 0.0;
		// When this entity last splashed, and when its wade cooldown next expires. Both are
		// carried by the caller (one pair per entity) and both come back updated in the decision.
		double LastSplashSeconds = NeverSeconds;
		double NextWadeSeconds = NeverSeconds;
		// `+ RandomInt(0, 8)` Source units on the wade splash only, drawn by the caller from a
		// named stream so the decision itself stays deterministic.
		int32 WadeJitterUnits = 0;
	};

	struct FSplashDecision
	{
		ESplash Kind = ESplash::None;
		// `origin - vel.xy * 0.035`, snapped to the volume's surface plane (+ the jitter, wade only).
		FVector LocationCm = FVector::ZeroVector;
		// The caller writes these back onto the entity it asked for.
		double LastSplashSeconds = NeverSeconds;
		double NextWadeSeconds = NeverSeconds;

		const TCHAR* Root() const
		{
			return Kind == ESplash::Big ? BigSplashRoot
				: Kind == ESplash::Wade ? WadeSplashRoot : nullptr;
		}
	};

	FSplashDecision DecideSplash(const FSplashInput& In);

	// --- buoyancy (named modernization "vphysics buoyancy") ---------------------------------------
	//
	// VtMB hands the `CONTENTS_WATER` solid to vphysics' own fluid controller, which is not in the
	// corpus and cannot be reproduced instruction for instruction. What IS authored, and what the
	// stage now carries, is the fluid row's `density` and `damping` — so the port spends them on
	// Archimedes and on the body's linear damping, which is the native Unreal expression of the
	// same two numbers. Nothing here displaces the surface: VtMB never did either (U2).
	//
	// Water's own density when the row authors none. `PM_water`'s `rawDensity`, and VtMB's unit.
	inline constexpr float DefaultFluidDensityKgPerM3 = 1000.f;

	// How much of an AABB stands below `SurfaceZCm`, 0..1. The body's box rather than its hull:
	// a convex decomposition per frame would buy precision the 2004 game never had.
	float SubmergedFraction(const FBox& BodyBoundsCm, float SurfaceZCm);

	// The displaced volume in m³ — the body's box scaled by how much of it is under.
	float DisplacedVolumeM3(const FBox& BodyBoundsCm, float Fraction);

	// The upward force in Unreal's own force unit (kg·cm/s²), which is what `AddForce` takes with
	// `bAccelChange` false. `GravityZCmPerSec2` is the world's, signed (-980 by default).
	float BuoyantForceZ(float FluidDensityKgPerM3, float DisplacedVolumeM3, float GravityZCmPerSec2);
}

// The splash the water lane decided, raised to whoever owns the effect and audio lanes. Declared
// out here rather than in the class because UHT parses the class body.
DECLARE_DELEGATE_TwoParams(FElysiumWaterSplashSignature, ElysiumWater::ESplash,
	const FVector& /* LocationCm */);

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
	/** G9: is this point in ⋃PVS(water clusters) — close enough to hear or fog the water. */
	bool IsNearWater(const FVector& PointCm) const
	{
		return ElysiumWater::IsNearWater(Volumes, PointCm);
	}
	/**
	 * The near-water state the audio seam reads: whether the last view resolved by
	 * `UpdateViewPostProcess` stood in a water cluster's PVS. Read, never written, by the seam —
	 * it is the same answer the post-process gate already computed, not a second query.
	 */
	bool IsViewNearWater() const { return bViewNearWater; }

	/** The fluid row of a volume by index, or an unset row for an out-of-range index. */
	const FElysiumWaterFluid& FluidAt(int32 VolumeIndex) const;

	/**
	 * The splash the water lane decided, handed to whoever owns the effect and audio lanes
	 * (`AElysiumMapActor`, which binds this at map activation). The actor raises it for physics
	 * bodies it tracks; the player's own transition is decided in the map actor, which calls the
	 * same handler directly.
	 */
	FElysiumWaterSplashSignature OnSplash;

	/**
	 * G7 / D2, named modernization "vphysics buoyancy": settle every simulating body standing in a
	 * volume that authors a fluid row — the in-fluid flag, the buoyant force from `density`, the
	 * linear damping from `damping`, and the entry splash. Driven from the actor's own tick, which
	 * runs only while a body is being tracked (or the debug draw is on).
	 */
	void TickFluidBodies(float DeltaSeconds);

	/** How many bodies are in a fluid right now — the readout, and what the tests assert. */
	int32 FluidBodyCount() const;

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

	/**
	 * One query-only box per volume, sized to its merged brush bounds, generating overlap events
	 * against `ECC_PhysicsBody` alone. It is the broadphase the fluid pass rides on: the engine
	 * tells us which simulating components are anywhere near the water, and the exact carve test
	 * (`FindVolumeAt`) then runs over those few rather than over the world. Built at `BeginPlay`,
	 * because a bake cannot save a runtime component into a level.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> FluidQueryBoxes;

	/** One tracked simulating body: what it was doing before the water, so it can be undone. */
	struct FFluidBody
	{
		TWeakObjectPtr<UPrimitiveComponent> Component;
		int32 VolumeIndex = INDEX_NONE;
		bool bInFluid = false;
		float RestoreLinearDamping = 0.f;
		double LastSplashSeconds = ElysiumWater::NeverSeconds;
	};
	TArray<FFluidBody> FluidBodies;
	/** The fluid pass' own clock, accumulated from the tick — never wall time (`cpp.md`). */
	double FluidClockSeconds = 0.0;

	UFUNCTION()
	void OnFluidBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);
	UFUNCTION()
	void OnFluidBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** Leave the fluid: restore the damping the body had and clear the flag. Safe on a dead body. */
	void ReleaseFluidBody(FFluidBody& Body);
	/** The tick runs for the debug draw or for a tracked body, and is off otherwise. */
	void RefreshTickEnabled();

	/** Does any volume author a fluid row? Settled at `BeginPlay`; no fluid, no query boxes. */
	bool bAnyFluidAuthored = false;
	/** The last view's near-water answer, published for the audio seam (G9). */
	bool bViewNearWater = false;

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
