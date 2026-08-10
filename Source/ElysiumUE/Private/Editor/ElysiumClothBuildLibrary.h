#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumClothBuildLibrary.generated.h"

/** What one garment build produced. Empty Errors means success. */
USTRUCT(BlueprintType)
struct FElysiumClothBuildResult
{
	GENERATED_BODY()

	/** Package path of the generated cloth asset, empty when nothing was written. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	FString AssetPath;

	/**
	 * The material this garment was tuned as, out of `pipeline/unreal/cloth_tuning.json`.
	 *
	 * Reported because it is the one input to the build that is a judgement rather than a decode —
	 * a reading of what the garment is — so it belongs in the build log beside the counts that
	 * came off the authored payload.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	FString Material;

	/** Simulation vertices written — VtMB's particle count for this garment. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 SimVertices = 0;

	/** Simulation triangles written. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 SimFaces = 0;

	/** Vertices pinned by a zero MaxDistance — VtMB's anchored prefix. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 KinematicVertices = 0;

	/** Config properties written into the cloth collection — zero means nothing is enabled. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 ConfigProperties = 0;

	/** Collision bodies added to the generated physics asset. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 CollisionBodies = 0;

	/**
	 * What the BUILT simulation model carries, read back off the asset rather than counted from
	 * the intent above.
	 *
	 * Every step between the collection and the model is a silent filter — a compaction that
	 * renumbers, a facade that copies only what its schema knows, a property whose absence
	 * disables its constraint. These are the numbers the solver will actually see, so a garment
	 * that reports the right particles here and none of the rest is a build defect rather than a
	 * tuning one.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 BuiltSimVertices = 0;

	/** Vertices the built model will treat as kinematic — a sub-threshold max distance. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 BuiltKinematicVertices = 0;

	/** Long range attachments in the built model. Zero means a garment nothing holds onto a body. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 BuiltTethers = 0;

	/** Render vertices the simulation drives — VtMB's substituted set. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 DrivenVertices = 0;

	/** Render vertices that stay skinned — the waistband, the collar, the body around them. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 SkinnedVertices = 0;

	/**
	 * Driven render vertices whose particle no longer exists.
	 *
	 * Mesh compaction removes simulation vertices no triangle references, and the collection
	 * remaps every index that pointed at one — to `INDEX_NONE`. A render vertex still asking to
	 * be driven by a particle that was removed is bound to nothing, and nothing on the path from
	 * here to the frame says so.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 OrphanedBindings = 0;

	/**
	 * Simulation particles whose incident faces cancel to a zero normal.
	 *
	 * The deformer carries no normal of its own — it hands a driven render vertex the simulation
	 * mesh's normal at the particle it follows. A particle with no usable normal therefore shades
	 * every vertex it drives black, and nothing between here and the frame says so. Nonzero is a
	 * topology defect: the same three particles wound both ways, or a face with no area.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 DegenerateSimNormals = 0;

	/**
	 * Simulation particles that arrived with no skin weights and fell back to the root bone.
	 *
	 * Their animation position is then the rest shape carried rigidly by the root, which is what
	 * max distance and every tether measure against — so the garment chases a pose that has
	 * nothing to do with the body. Nonzero here is a defect, not a tolerance.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	int32 RootBoundParticles = 0;

	/** One line per failure. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Cloth")
	TArray<FString> Errors;
};

/**
 * Editor-only implementation behind `pipeline/unreal/make_cloth_assets.py`.
 *
 * VtMB authors renderer-side particle cloth in the model image: particles, distance and
 * compression constraints, collision capsules and spheres, and per-render-vertex substitution
 * maps (`docs/vtmb/secondary_motion.md`). The offline decode is
 * `elysium_pipeline.formats.mdl_cloth`, which writes `npc/garment/<stem>.json` beside the
 * character's `.glb` and in that glb's own basis.
 *
 * This turns that sidecar into a `UChaosClothAsset`, so the running game carries **no VtMB
 * cloth rule at all** — a stock Chaos solver consumes a generated asset, the same way blend
 * grids became `UBlendSpace` assets rather than a runtime evaluator. That is the repository's
 * standing rule for a representation the frame path should not have to know about.
 *
 * It lives in C++ rather than in the Python generator because none of the construction path has
 * a scripting surface: a cloth collection is an `FManagedArrayCollection` written through
 * `FCollectionClothFacade`, and neither the collection, the facade, nor `FClothGeometryTools`
 * carries a `UFUNCTION`. Creating the package, saving it and driving the loop are ordinary
 * Python, exactly as they are for the player animation graph.
 *
 * What the sidecar supplies and what is derived here:
 *
 *  - **Simulation positions** are authored particle rest positions, already reconstructed
 *    offline from the payload's anchor indices and its position map. Nothing is resampled.
 *  - **Simulation topology** is the surface induced from the render mesh. The payload's own
 *    collision-triangle array is a per-particle proxy, not a manifold, and cannot serve.
 *  - **Pinning** comes from the anchored prefix, written as a zero `MaxDistance` weight. Chaos
 *    pins by weight map; VtMB pins by skinning those particles from the bone palette, and the
 *    two agree because a zero max distance leaves a vertex on its skinned position.
 *  - **Skin weights** for the simulation mesh are the anchors' own render-vertex weights.
 *  - **2D pattern positions are derived, not authored.** VtMB has no flat pattern, so a
 *    cylindrical unwrap of the rest pose stands in. It drives pattern-space anisotropy only;
 *    the 3D rest positions the constraints were authored against are what the solver uses.
 *  - **Colliders** become a generated `UPhysicsAsset` of capsule and sphere bodies on the
 *    authored bones, because Chaos gathers cloth collision from a physics asset rather than
 *    from the cloth collection.
 *  - **Material is not decoded at all.** VtMB's solver had no density, friction or thickness to
 *    state, so what a garment is MADE of is a reading of it rather than a fact in the file. It
 *    lives in `pipeline/unreal/cloth_tuning.json` beside every other tuned value, and no number
 *    in the implementation is a tuning value.
 */
UCLASS()
class ELYSIUMUE_API UElysiumClothBuildLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Build one cloth asset per garment in `SidecarPath` under `PackageDirectory`.
	 *
	 * `SkeletalMeshPath` names the character mesh whose reference skeleton the cloth binds to;
	 * the sidecar's bone indices are model bone indices, so they are resolved by NAME against
	 * that skeleton rather than used directly — a baked family skeleton renumbers, and an index
	 * carried across that boundary silently attaches a hem to the wrong limb.
	 *
	 * `TuningPath` is `pipeline/unreal/cloth_tuning.json`, which owns every material and solver
	 * value the build applies. It is a required input rather than an optional override: the
	 * implementation carries no fallback to substitute, which is what keeps the whole corpus
	 * tunable from one reviewable file.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Cloth")
	static TArray<FElysiumClothBuildResult> BuildClothAssetsFromSidecar(
		const FString& SidecarPath,
		const FString& PackageDirectory,
		const FString& SkeletalMeshPath,
		const FString& TuningPath);
};
