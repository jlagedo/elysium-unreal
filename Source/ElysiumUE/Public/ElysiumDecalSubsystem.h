#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Subsystems/WorldSubsystem.h"

#include "ElysiumDecalSubsystem.generated.h"

class AActor;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UTexture2D;
class UWorld;

// One decal to lay. Either `MaterialId` (a `vtmb:material:` id whose projector instance
// `MI_<unit>_Decal` the materials stage staged) or `Texture` (a collide sprite: an MID off
// `M_V2_Decal` with `BaseTexture` bound) names what it draws; a request that carries both takes
// the id, and one that carries neither is refused.
//
// Not a USTRUCT: this is a call parameter, built on the stack by its caller and consumed inside
// `Lay` before it returns, so it never needs reflection or a GC edge of its own.
struct FElysiumDecalRequest
{
	// `vtmb:material:<dir>/<stem>`. Resolved through `FElysiumContentPaths::BakedDecalMaterial`.
	FString MaterialId;
	// A collide sprite's own texture (R7.3 A2). Only read when `MaterialId` is empty.
	UTexture2D* Texture = nullptr;
	// The impact point, world space.
	FVector Location = FVector::ZeroVector;
	// The ROOM-facing normal (out of the surface, towards the shooter), as `FHitResult::ImpactNormal`
	// gives it. The component projects along its local -X, so this is its local +X.
	FVector Normal = FVector::UpVector;
	// The surface's horizontal axis (the exporter's `SDir`, a decal's texture U). Zero lets `Lay`
	// derive one -- a runtime stain has no authored s/t frame and no orientation anyone can see.
	FVector Tangent = FVector::ZeroVector;
	// Half-height (local Y) and half-width (local Z) in cm, the two axes `DecalSize` carries beside
	// the fixed projection reach.
	FVector2D HalfSizeCm = FVector2D(8.f, 8.f);
	// Seconds until the component fades out and is destroyed. 0 is persistent -- what every VtMB
	// decal is, until `r_decals` recycles it.
	float LifetimeSeconds = 0.f;
	// Attach to a moving thing (a door leaf, a body) instead of the world. The stain then rides it.
	USceneComponent* Attach = nullptr;
	// `DECALLIST`'s `entityIndex`: the `saveentityindex` this stain is stuck to, or INDEX_NONE for
	// a world decal (`docs/vtmb/savegame_format.md` -> "DECALLIST").
	int32 EntityIndex = INDEX_NONE;
};

// One `DECALLIST` record: what the save writes and what a load re-lays through `Lay`
// (`docs/vtmb/savegame_format.md` -> "DECALLIST": `position`, `name`, `entityIndex`, `flags`).
//
// The normal is ours and not the format's: Source re-derived it by tracing the world at restore,
// and re-tracing would put a stain on whatever geometry moved since. Recording it is the
// modernization, and it costs 12 bytes a record. The half-size is NOT recorded: it is a property
// of the material unit (`$decalscale` x the texture's dimensions), so `Restore` derives it the
// same way the shot that laid the stain did.
struct FElysiumDecalRecord
{
	FString MaterialId;
	FVector Position = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	int32 EntityIndex = INDEX_NONE;
};

// One pooled decal component and the MID it draws through.
USTRUCT()
struct FElysiumLaidDecal
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<UDecalComponent> Component;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mid;
	// The record this component stands for, so `Records()` is a read of live state rather than a
	// second list that can drift from it.
	FString MaterialId;
	FVector Position = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	int32 EntityIndex = INDEX_NONE;
	// A laid decal with a lifetime is not a save record: it is gone before the save is read.
	bool bPersistent = true;
};

/**
 * The one owner of every decal in a world.
 *
 * Two halves, one owner:
 *   (a) **Adoption** (ruling 4). A bake cannot save a `UMaterialInstanceDynamic` into a level, so
 *       the fog term `M_V2_Decal` declares (`ElysiumSurfaceParamsDecal`, R5.3) has nowhere to live
 *       on a baked `ADecalActor`. At map load `UElysiumMapVisuals::AdoptBakedLevel` hands this
 *       subsystem every `elysium.decal` component it walked past; each gets one MID parented to
 *       the instance the bake bound, and `ElysiumFog::ApplyToDecalMID` stamps the map's fog on it.
 *       `ApplySceneFog` re-stamps by calling `ApplyFog` here -- one walk, one owner, and the
 *       `elysium.Fog` A/B reaches a decal live.
 *   (b) **Laying** (owner call B). `Lay` puts a runtime stain in the world -- the ranged shot's
 *       bullet hole, the A2 collide archetype's sprite, the gib blood -- on a pooled
 *       `UDecalComponent` under one hidden actor this subsystem owns, oriented the way
 *       `bake_map.py::_place_decals` orients a placed one, with a running `SortOrder` so later
 *       stains layer over earlier ones as `R_DecalCreate`'s list does, and a cap
 *       (`UElysiumSurfaceSettings::MaxLaidDecals`) that recycles oldest-first.
 *
 * A `UWorldSubsystem` rather than a component on the map actor because a laid decal outlives the
 * thing that laid it and belongs to the world, not to a map load: travel destroys the world and
 * the whole pool with it, which is exactly `DECALLIST`'s per-map scope.
 */
UCLASS()
class ELYSIUMUE_API UElysiumDecalSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// --- (a) the baked decals -------------------------------------------------------------------

	/**
	 * Take ownership of the baked level's `elysium.decal` components: one MID per component off
	 * whatever instance the bake bound, set back on the component, then the current fog stamped on
	 * it. Idempotent per component -- re-adopting one already held re-uses its MID -- so a second
	 * `AdoptBakedLevel` (a travel back into the same world) does not double the list.
	 */
	void AdoptBaked(const TArray<UDecalComponent*>& Components);

	/**
	 * Stamp one fog set on every decal this subsystem owns, adopted and laid alike. The arguments
	 * are `ElysiumFog::ApplyToDecalMID`'s: the authored colour, the two distances in cm, and
	 * whether the set is on at all. Returns how many MIDs took it. The set is remembered, so a
	 * stain laid after this call is fogged like the wall it lands on.
	 */
	int32 ApplyFog(bool bEnabled, const FLinearColor& Color, float StartCm, float EndCm);

	int32 NumAdopted() const { return AdoptedDecals.Num(); }

	// --- (b) the runtime stain ------------------------------------------------------------------

	/**
	 * Lay one decal. Returns the component, or null when the request names nothing that resolves
	 * (a `vtmb:material:` id with no staged projector instance is a named failure, reported once
	 * per id -- never the error material, which is a surface-domain asset a `UDecalComponent`
	 * would silently replace with the engine default).
	 */
	UDecalComponent* Lay(const FElysiumDecalRequest& Request);

	/** The `DECALLIST` shape for the save's reserved `Maps` slot: every persistent laid decal. */
	TArray<FElysiumDecalRecord> Records() const;

	/** Re-lay a save's records through `Lay`. Returns how many landed. */
	int32 Restore(const TArray<FElysiumDecalRecord>& Records);

	int32 NumLaid() const { return LaidDecals.Num(); }

	/** Return every laid decal to the pool. The adopted baked set is untouched. */
	void ClearLaid();

	// --- resolution -----------------------------------------------------------------------------

	/** `/Game/ElysiumGenerated/Materials/V2/M_V2_Decal`, the one decal master (ruling 1). */
	static const TCHAR* DecalMasterPath();

	/**
	 * The shared projector instance a `vtmb:material:` id names (`MI_<unit>_Decal`, ruling 2), or
	 * null when the id is not a material id or the stage staged no such instance.
	 */
	static UMaterialInterface* LoadProjectorInstance(const FString& MaterialId);

private:
	/** The hidden actor every pooled `UDecalComponent` hangs off, spawned on first `Lay`. */
	AActor* PoolHost();

	/** `M_V2_Decal`, or the engine's own deferred-decal default when the package is absent. */
	static UMaterialInterface* LoadDecalMaster();

	/** Drop the entries whose component is gone -- a lifetime stain the engine's fade destroyed. */
	void PruneDead();

	UPROPERTY() TObjectPtr<AActor> PoolActor;

	// The adopted baked components, and one MID each.
	UPROPERTY() TArray<TObjectPtr<UDecalComponent>> AdoptedDecals;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> AdoptedMids;

	// The laid stains, oldest first -- which is what makes the cap's recycling a pop from the
	// front rather than a search.
	UPROPERTY() TArray<FElysiumLaidDecal> LaidDecals;

	// `SortOrder`, monotonically increasing for the world's lifetime: a stain laid later draws
	// over one laid earlier, which is what `R_DecalCreate`'s append-ordered list did. `AdoptBaked`
	// pushes it past the highest baked decal's own order (the bake numbers them 0..N-1 by `.decals`
	// line), so a laid stain layers over the map's own decals too -- one list, as retail had.
	int32 NextSortOrder = 0;

	// The fog set last stamped, re-applied to each newly laid decal so a stain born after
	// `ApplySceneFog` is fogged like the wall it lands on.
	bool bFogEnabled = false;
	FLinearColor FogColor = FLinearColor::Black;
	float FogStartCm = 0.f;
	float FogEndCm = 0.f;

	// One warning per unresolved material id, not one per shot.
	TSet<FString> ReportedMissing;
};

/**
 * `C_TEGunshotDecal`'s decal half of the impact matrix (`docs/vtmb/effects.md` §3.5).
 *
 * The impact PARTICLE is a surface x weapon-column lookup; the decal is not. `effects.md` §3.5
 * records the decal as a **parallel** lookup on the **unremapped** surface character, and the
 * corpus carries exactly six pools of five (`decals/hits/{concrete,metal,wood,glass}/shot1-5`,
 * `decals/hits/flesh/{blood,soak}1-5`) -- so the pool is chosen by the surface alone and the
 * variation is a roll inside it. `decals/hits/concrete/impact1-5` also exist and are NOT one of
 * the six: they are hand-placed `infodecal` art.
 *
 * The `scorch` column has no material in the corpus and therefore no row here.
 */
namespace ElysiumImpactDecals
{
	/** The five variations every pool carries. */
	inline constexpr int32 PoolSize = 5;

	/**
	 * The channel a shot's mark is traced on: `ECC_GameTraceChannel2`, declared in
	 * `Config/DefaultEngine.ini` as `ElysiumPick` and named `ELYSIUM_PICK_CHANNEL` by
	 * `Private/Debug/ElysiumPick.h` (which is `!UE_BUILD_SHIPPING`, so the constant is restated
	 * here rather than shared -- a shot marks a wall in a shipping build too).
	 *
	 * **Not `ECC_Visibility`.** A converted map's solid body is the `.hulls` convex collider, which
	 * is `BlockAll`, invisible, material-less and includes PLAYERCLIP; the *render* geometry wears
	 * the `ElysiumPickOnly` profile and is the only thing in the level that blocks this channel --
	 * and it is the half that carries the face's `MI_<unit>` and therefore its
	 * `/ElysiumBaked/SurfaceProperties/PM_<class>` physical material. A shot traced on
	 * `ECC_Visibility` would stop on a clip volume with no surface character and stain every
	 * surface in the game concrete.
	 */
	inline constexpr ECollisionChannel SurfaceTraceChannel = ECC_GameTraceChannel2;

	/**
	 * The pool a surface character selects, as a `vtmb:material:` id stem
	 * (`decals/hits/concrete/shot`). Never empty: an unnamed surface is concrete.
	 *
	 * `GameMaterial` is `UElysiumPhysicalMaterial::GameMaterial`, VtMB's own single-letter
	 * `gamematerial` code. The letters' meanings are recorded nowhere in the install, so this
	 * groups them by the units that DECLARE each one, read off the
	 * 63 staged `surface-properties/*.provenance.json` (2026-09-03):
	 *
	 *   `F` fish_fresh / fish_frozen / flesh / watermelon      -> `flesh/blood`
	 *   `W` wood / woodpanel                                   -> `wood/shot`
	 *   `Y` bottle / glass / glass_shard / glassbottle         -> `glass/shot`
	 *   `M` metal / metalpanel / canister / armorflesh / tin / ring / roller / grenade / gunship /
	 *       strider,
	 *   `V` metalvent / metal_barrel / kitchen_pan / kitchen_pot / kitchen_utensils / can_pop /
	 *       can_pop_crushed,
	 *   `G` metalgrate, `P` computer                           -> `metal/shot`
	 *
	 * -- the same four letters `particleimpacttable.txt` groups into one `Impact_Metal_Emitter`
	 * row (effects.md §3.5: "metal / vent / grate / computer"). Everything else falls to concrete:
	 * `C` (brick / concrete / default), `D` (dirt, carpet, sand, snow, paper, plaster, grass, mud),
	 * `O` (rock / stone / boulder), `T` (tile / plastic / cardboard / ice), `S` (water), `A`
	 * (gargoyle), `N`/`U` (ming_xiao), `I`, `X`, and the 7 units carrying no letter at all.
	 */
	FString PoolFor(const FString& GameMaterial, bool bSoak);

	/** `PoolFor` plus a 1-based variation: `vtmb:material:decals/hits/concrete/shot3`. */
	FString MaterialIdFor(const FString& GameMaterial, bool bSoak, int32 Variation);

	/**
	 * The half-size of one impact hole, cm, off the unit's own `$decalscale` and its texture's
	 * dimensions (the corpus provenance, measured 2026-09-03): every one of the thirty units is a
	 * 64x64 texture, `$decalscale 0.10` on twenty-five of them and **0.25 on the five
	 * `decals/hits/glass/shot`** -- so a bullet hole is 16.3 cm across and a glass one 40.6.
	 *
	 * `PoolOrMaterialId` takes either form (`decals/hits/glass/shot`,
	 * `vtmb:material:decals/hits/glass/shot2`); anything else takes the 0.10 case, which is what
	 * every pool but glass authors.
	 */
	FVector2D HalfSizeCmFor(const FString& PoolOrMaterialId);

	/**
	 * The ranged shot's forward world trace, and the request it produces. Traces
	 * `SurfaceTraceChannel` from `FromCm` along `Direction` for `RangeCm`, reads the hit's
	 * `UElysiumPhysicalMaterial` for its surface character, and fills `OutRequest` with that
	 * character's pool, the hit point and the hit normal. False when there is no world, no hit, or
	 * the direction is degenerate.
	 *
	 * **A character is not on this channel, and that is the shot's own answer.** Only the baked
	 * render geometry (`ElysiumPickOnly`) and a solid static prop (`ElysiumPropSolid`) block
	 * `SurfaceTraceChannel`; an NPC capsule wears the `Pawn` profile, its mesh is `NoCollision`,
	 * and a brush entity sets `ECR_Ignore` explicitly (`Config/DefaultEngine.ini`,
	 * `ElysiumBrushComponent.cpp`). So a shot at a body marks the wall behind it — which is what
	 * `CommitQueuedAttack` already says the mark is for ("a miss marks the wall behind the
	 * target"), and the damage half resolves its victim by handle, never by this ray. The
	 * `flesh/blood` and `flesh/soak` pools are therefore reached only by a caller that knows the
	 * surface without tracing for it: `env_shooter`'s gib blood and the soak column (R7.3).
	 *
	 * `Variation` is the caller's roll, so the substrate's own RNG owns the choice and a test can
	 * pin it.
	 */
	bool BuildImpactRequest(UWorld* World, const FVector& FromCm, const FVector& Direction,
		float RangeCm, int32 Variation, FElysiumDecalRequest& OutRequest, FHitResult& OutHit);
}
