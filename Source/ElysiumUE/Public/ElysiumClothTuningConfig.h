#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "ElysiumClothTuningConfig.generated.h"

/**
 * One layer of garment tuning: the corpus defaults, one material, or one garment's own overrides.
 *
 * Every value carries its own `bOverride` flag, because a layer states only what it differs in.
 * Resolution is `Defaults` <- `Materials[<name>]` <- the garment entry, each layer writing its set
 * keys over the one below. `Defaults` sets every flag, so a material or a garment states only what
 * it changes, and a key nothing ever set is a hole in the table rather than an invitation to
 * substitute an average.
 *
 * Stiffnesses are Chaos's legacy PBD 0..1 relaxation factors, which is what
 * `UChaosClothConfig::EdgeStiffnessWeighted` / `BendingStiffnessWeighted` feed. They are NOT the
 * physical-unit XPBD values (stiffness 100, kg/s^2) quoted by the Dataflow editor and by most
 * current tutorials; those belong to a different property set and must not be pasted here.
 */
USTRUCT(BlueprintType)
struct FElysiumClothTuningLayer
{
	GENERATED_BODY()

	/**
	 * Why this layer reads the way it does — the material judgement, the authored measurements it
	 * was taken from. Prose for the next reader; the build never looks at it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (MultiLine = true))
	FString Note;

	/**
	 * Which material this garment is read as. Only a garment entry sets it; a material layer and
	 * the defaults leave it clear. A garment that sets nothing resolves to `FallbackMaterial` and
	 * is reported, so an untuned garment is visible rather than silently average.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideMaterial = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideMaterial"))
	FName Material;

	/**
	 * Chaos's own kg scale. The engine's own reference points: melton wool 0.7, heavy leather 0.6,
	 * polyurethane 0.5, denim 0.4, light leather 0.3, cotton 0.2, silk 0.1.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideDensity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideDensity"))
	float Density = 0.35f;

	/** Relaxation on the mesh's own edges — VtMB's distance constraint set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideEdgeStiffness = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideEdgeStiffness"))
	float EdgeStiffness = 1.0f;

	/**
	 * Relaxation on the bend diagonals. VtMB's compression-only set is those same diagonals with
	 * rest length `actual / k`, so `1/k` is how much resistance the artist asked for — the
	 * strongest single signal there is for this value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideBendStiffness = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideBendStiffness"))
	float BendStiffness = 0.7f;

	/**
	 * Area preservation, zero across the corpus and deliberately so: VtMB authors exactly two
	 * constraint sets, edges and bend diagonals, and no area set at all — so the engine's default
	 * of 1 buys a constraint the garment was never designed around and charges for it every
	 * substep.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideAreaStiffness = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideAreaStiffness"))
	float AreaStiffness = 0.0f;

	/**
	 * The fraction of velocity removed per 60 Hz frame. Retail retains 0.97 of the previous
	 * displacement per substep at a 300 Hz target, which is where the corpus default `1 - 0.97^5`
	 * came from; a material may still move it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideDamping = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideDamping"))
	float Damping = 0.1413f;

	/** Neighbourhood-relative damping, on top of the global coefficient above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideLocalDamping = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideLocalDamping"))
	float LocalDamping = 0.0f;

	/** How far off a collider the surface is held, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideCollisionThickness = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideCollisionThickness"))
	float CollisionThickness = 1.0f;

	/** Friction against the colliders. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideFriction = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideFriction"))
	float Friction = 0.8f;

	/**
	 * Continuous collision detection. A garment on a character who turns, sits or takes a hit moves
	 * far enough in one substep to pass straight through a hip capsule, and a discrete test only
	 * ever asks where the particle ENDED — so the miss is silent and the hem comes out the far
	 * side. Per material because it is a real cost and most of what it buys is on the long coats: a
	 * necktie on a sternum never meets a fast collider.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideUseCCD = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideUseCCD"))
	bool bUseCCD = true;

	/**
	 * Multiplies VtMB's own authored per-garment gravity scale rather than replacing it, so a
	 * material can be adjusted without discarding what the artist asked for.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideGravityMultiplier = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideGravityMultiplier"))
	float GravityMultiplier = 1.0f;

	/** How much of the wearer's linear motion reaches the solver. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideLinearVelocityScale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideLinearVelocityScale"))
	float LinearVelocityScale = 0.75f;

	/** How much of the wearer's rotation reaches the solver. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideAngularVelocityScale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideAngularVelocityScale"))
	float AngularVelocityScale = 0.75f;

	/** The centrifugal/Coriolis term a turning wearer applies to the garment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideFictitiousAngularScale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideFictitiousAngularScale"))
	float FictitiousAngularScale = 1.0f;

	/** Long range attachment stiffness — what holds the surface onto the pinned set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideTetherStiffness = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideTetherStiffness"))
	float TetherStiffness = 1.0f;

	/** Scales the generated tether lengths. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideTetherScale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideTetherScale"))
	float TetherScale = 1.0f;

	/**
	 * The leash: how far a free particle may leave its skinned position, as a fraction of that
	 * particle's own geodesic distance to the pinned set.
	 *
	 * Ours, not VtMB's — the retail solve has no per-particle displacement limit at all and holds
	 * the surface on the body with its constraint graph alone. Chaos needs one at real-time substep
	 * counts, and scaling it per particle is what makes one number mean the same thing on a 35 cm
	 * necktie and a 228 cm cloak: a waistband particle gets a tight leash, a hem particle a loose
	 * one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideLeashReachFraction = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideLeashReachFraction"))
	float LeashReachFraction = 0.6f;

	/** Floor on the scaled leash, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideLeashMinCm = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideLeashMinCm"))
	float LeashMinCm = 2.0f;

	/** Cap on the scaled leash, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (InlineEditConditionToggle))
	bool bOverrideLeashMaxCm = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (EditCondition = "bOverrideLeashMaxCm"))
	float LeashMaxCm = 25.0f;

	/** Write every value this layer sets over `Target`, so a later layer wins key by key. */
	void OverlayOnto(FElysiumClothTuningLayer& Target) const;

	/**
	 * One line per value no layer ever set, naming the key. `Defaults` declares them all, so an
	 * absence is an incomplete table rather than a value to invent.
	 */
	void ReportUnsetValues(TArray<FString>& OutErrors) const;
};

/**
 * The solver block, shared by every garment.
 *
 * `SubstepTargetMs` and `MaxSubsteps` are VtMB's own clock — `N = min(30, round(elapsed * 300))` —
 * and Chaos reproduces it as `Clamp(Round(DeltaTime * 1000 / DynamicSubstepDeltaTime), 1,
 * NumSubsteps)`, so 60 fps runs 5 substeps and the cap only binds below 10 fps. The iteration
 * counts are the real budget knob: Epic's guidance for cloth on many NPCs is 1 for both, and the
 * engine default for the max is 10.
 *
 * Unlike a tuning layer, every value here is required and always present — there is one solver
 * block and nothing overrides it.
 */
USTRUCT(BlueprintType)
struct FElysiumClothSolverTuning
{
	GENERATED_BODY()

	/** Why the counts read the way they do. Prose; the build never looks at it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (MultiLine = true))
	FString Note;

	/** `UChaosClothSharedSimConfig::IterationCount`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth")
	int32 IterationCount = 1;

	/** `UChaosClothSharedSimConfig::MaxIterationCount`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth")
	int32 MaxIterationCount = 2;

	/** The `DynamicSubstepDeltaTime` property, in milliseconds. VtMB's 300 Hz substep clock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth")
	float SubstepTargetMs = 3.3333333f;

	/** The substep cap — `UChaosClothSharedSimConfig::SubdivisionCount`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth")
	int32 MaxSubsteps = 30;

	/** Whether a garment collides with itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth")
	bool bUseSelfCollisions = false;
};

/**
 * One model's garment entries, in the order its payload declares them.
 *
 * A wrapper rather than a bare array because Unreal carries no nested container: a model may author
 * more than one garment — `tremere_female_armor_3` tunes its hanging bell sleeves apart from its
 * robe skirt — and each definition needs its own layer.
 */
USTRUCT(BlueprintType)
struct FElysiumClothGarmentDefinitions
{
	GENERATED_BODY()

	/** One entry per garment definition in the model's payload, indexed the same way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth")
	TArray<FElysiumClothTuningLayer> Definitions;
};

/**
 * `/Game/ElysiumAuthored/Cloth/DA_ClothTuning` — what every VtMB garment is MADE of, in Chaos's own
 * terms.
 *
 * Project-authored reconstruction content: nothing here is copied out of the user's game. What VtMB
 * itself authored — particle rest positions, the anchored prefix, the constraint graph, the
 * collider set and the per-garment gravity scale — is read at build time from the gitignored export
 * sidecar and is never restated here.
 *
 * This asset exists because the authored payload states a garment's SHAPE and its SOLVE, and states
 * nothing about what it is MADE OF. VtMB's solver had no notion of a material: no density, no
 * friction, no thickness. Chaos does, and a coat that is leather has to be told so. That judgement
 * is a reading of the garment — its name, the bones it hangs from, its drop and footprint — and it
 * is recorded per garment rather than derived, because no rule recovers "heavy leather" from a
 * vertex count.
 *
 * Two authored measurements inform the numbers, and each garment's note names them:
 *
 *  - **bend slack k** — VtMB's compression-only constraint set is the bend diagonals of the same
 *    mesh, one-sided, with rest length `actual / k`. The spring is inert until the diagonal folds
 *    to `1/k` of rest, so `1/k` is how much resistance the artist asked for. It runs 1.00
 *    (stiffest, resists at rest) to 31.62 (Jeanette alone, effectively free).
 *  - **stretch s** — the distance set's per-edge relaxation factor. It is NOT transplanted: VtMB
 *    solved that set twice per substep across 30 substeps plus 15 settling passes, so the same
 *    factor buys far less convergence in our budget. It is used as an ORDERING between garments,
 *    compressed into a range that holds.
 *
 * Tuning the whole corpus means editing `Defaults` or one material; tuning one garment means
 * setting an override on its own entry.
 */
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumClothTuningConfig final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** What the table as a whole is for, and the conventions its numbers are stated in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth",
		meta = (MultiLine = true))
	FString Note;

	/** The solver block, shared by every garment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth")
	FElysiumClothSolverTuning Solver;

	/** The material a garment with no entry of its own resolves to. Its use is always reported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth")
	FName FallbackMaterial;

	/** The bottom layer. It sets every value, so a material or a garment states only what differs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth")
	FElysiumClothTuningLayer Defaults;

	/** What a garment can be made of, by material name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth")
	TMap<FName, FElysiumClothTuningLayer> Materials;

	/** One entry per model id that authors cloth; material profiles remain keyed by material name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Cloth")
	TMap<FName, FElysiumClothGarmentDefinitions> Garments;

	/**
	 * The authored table, loaded on first use and rooted for the process. Null — with one error
	 * naming the package — when the asset is not on the mount. There is no second copy of these
	 * values and no compiled fallback, which is what keeps the whole corpus tunable from one
	 * reviewable asset.
	 */
	static const UElysiumClothTuningConfig* Load();

	/**
	 * One garment's fully resolved parameters: `Defaults` <- `Materials[<name>]` <- the garment's
	 * own entry, each layer overriding the last value by value.
	 *
	 * `Definition` selects among a model's entries for a model that authors more than one garment.
	 * A missing garment resolves to the authored `FallbackMaterial` and reports the substitution.
	 * Native importers can collect it as a pending-tuning warning; legacy callers receive it in
	 * OutErrors. Missing material definitions and unset resolved values remain errors.
	 */
	FElysiumClothTuningLayer ResolveGarment(const FString& Stem, int32 Definition,
		FName& OutMaterial, TArray<FString>& OutErrors, TArray<FString>* OutWarnings = nullptr) const;
};
