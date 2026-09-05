#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumSurfaceSettings.generated.h"

class UMaterialParameterCollection;

/**
 * The global surface-shading knobs a human tunes by eye and the project ships as
 * `Config/DefaultElysium.ini` (`docs/architecture/seam_map_material.md` → "Import" → "Knob
 * contract"; `docs/project/seam_migration.md` 2026-08-31, "Calibration happens on knobs inside the
 * editor, never in a loop").
 *
 * This is the single writer of `MPC_ElysiumSurfaces`'s scalars: every V2 master reads the
 * collection rather than a literal, so the Cog Environment window and the Project Settings page
 * both end up looking at the same values through the same collection, never a second writer of
 * their own. Project Settings → Elysium → Surfaces edits this object directly and
 * `TryUpdateDefaultConfigFile` (the stock `defaultconfig` behaviour) writes the tracked ini on
 * every edit, no Ctrl+S needed (`SSettingsEditor.cpp:441`). `PostEditChangeProperty` pushes the
 * new values into the collection's asset defaults and every live world instance, so PIE follows
 * the slider without a restart.
 *
 * Editor-only: the ini is what a human tunes and what this object pushes into the collection's
 * *asset* defaults, but a packaged game never runs `UDeveloperSettings`'s ini-loaded values
 * through `PushToCollection` again -- it loads `MPC_ElysiumSurfaces` as cooked data and reads
 * whatever scalar values were baked into it as of the last editor push. `OnPostEngineInit` below
 * re-pushes the ini's values on every editor boot precisely so the cooked collection never drifts
 * from the ini between an edit and the next cook; nothing pushes at runtime in a packaged build.
 */
UCLASS(Config = Elysium, DefaultConfig, meta = (DisplayName = "Surfaces"))
class ELYSIUMUE_API UElysiumSurfaceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UElysiumSurfaceSettings();

	// --- defaults: the non-`$envmap` surface's PBR triple, before the class-calibration lookup
	// and the reflection contract's mask term are applied -----------------------------------------
	UPROPERTY(EditAnywhere, Config, Category = "Defaults", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultSpecular = 0.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Defaults", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultRoughness = 0.6f;

	UPROPERTY(EditAnywhere, Config, Category = "Defaults", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultMetallic = 0.0f;

	/**
	 * The `Default*` triple's own weight against the per-class calibration row: every master
	 * computes `Roughness = lerp(DefaultRoughness, ClassRoughness, ClassInfluence)` (and the same
	 * `lerp` for Specular and Metallic), so at `0.0` the whole world follows the three global
	 * `Default*` knobs above and the 72-row class table (`UElysiumSurfaceCalibration`) has no
	 * effect at all, and at `1.0` (the default) every surface follows its class row exactly as it
	 * always has. This is the knob that actually makes `Default*` reachable: until this lerp
	 * existed, a master read the class table unconditionally and `Default*` was written but never
	 * sampled.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Defaults", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClassInfluence = 1.0f;

	// --- lighting -----------------------------------------------------------------------------
	/** The one global specular-response scale the light rig reads (SF-6.2); no per-map override. */
	UPROPERTY(EditAnywhere, Config, Category = "Lighting", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float LightSpecularScale = 1.0f;

	/** `mul_x2 c0` in every lit ps.1.x program: Source's `_x2 c0` overbright doubling. */
	UPROPERTY(EditAnywhere, Config, Category = "Lighting", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float Overbright = 2.0f;

	// --- envmap / reflection contract ------------------------------------------------------------
	UPROPERTY(EditAnywhere, Config, Category = "EnvMap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaskRoughnessMin = 0.08f;

	UPROPERTY(EditAnywhere, Config, Category = "EnvMap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaskRoughnessMax = 0.9f;

	UPROPERTY(EditAnywhere, Config, Category = "EnvMap", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float MaskSpecularScale = 1.0f;

	/** The mask term's metallic ceiling, the metallic counterpart of `MaskRoughnessMin/Max`. */
	UPROPERTY(EditAnywhere, Config, Category = "EnvMap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaskMetallicMax = 1.0f;

	/** The grey-tint specular scale a plain (non-masked) `$envmap` surface reflects at. */
	UPROPERTY(EditAnywhere, Config, Category = "EnvMap", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float EnvTintScale = 1.0f;

	/** The ~342 authored-fixed-cube instances' literal cube-sample strength. */
	UPROPERTY(EditAnywhere, Config, Category = "EnvMap", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float FixedCubeStrength = 1.0f;

	/** `$envmaptint`'s chromatic (non-grey) case: the tint's own strength as a reflection term. */
	UPROPERTY(EditAnywhere, Config, Category = "EnvMap", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float ChromaticTintStrength = 1.0f;

	/**
	 * The grey-vs-chromatic split for `$envmaptint`: a tint whose channels differ by more than
	 * this (normalized) is chromatic (`ChromaticTintStrength` applies) rather than grey
	 * (`EnvTintScale` applies). Read by the stage, not a Python literal.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "EnvMap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChromaThreshold = 0.02f;

	// --- decal --------------------------------------------------------------------------------
	// R7.2 retired `DecalDepthOffset`: an `isDecalSurface` face group now draws as a mesh decal,
	// coplanar with its wall by construction, so there is no bias left to apply.
	/**
	 * How many runtime stains `UElysiumDecalSubsystem` keeps in one world before it recycles the
	 * oldest — VtMB's `r_decals`, whose own default lives in `engine.dll` `staticinit_2007af40`
	 * and has not been read yet (R7.2's census says so); 2048 until it is. Not an
	 * `MPC_ElysiumSurfaces` row: no material samples it.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Decal", meta = (ClampMin = "1"))
	int32 MaxLaidDecals = 2048;

	// --- reflection capture placement (SF-6.2) -----------------------------------------------
	UPROPERTY(EditAnywhere, Config, Category = "Capture", meta = (ClampMin = "100.0"))
	float CaptureRadius = 1500.0f;

	// --- detail props (R6.3) -------------------------------------------------------------------
	/**
	 * The peak World Position Offset, in centimetres, of a fully swaying detail instance
	 * (`swayAmount` 255) -- the `UseDetailSway` term on `M_V2_Lit`/`M_V2_LitTranslucent`/
	 * `M_V2_Unlit` (`seam_map_material.md` -> "Detail sway on the model masters (R6.3)"). VtMB's
	 * own client never read the byte, so the default is the first Source build's that did:
	 * `cl_detail_max_sway` 5 world units (owner call filed to R7; wire first, tune later).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Detail Props", meta = (ClampMin = "0.0"))
	float DetailSwayAmplitude = 5.0f * 2.54f;

	// --- water (R7.1) ---------------------------------------------------------------------------
	/**
	 * The one translation knob on `M_V2_Water` (`water-architecture.md` section 4.2): the water
	 * volume's extinction is `WaterFogScale / (($fogend - $fogstart) x 2.54)` per centimetre, split
	 * into scattering and absorption by the decoded `$fogcolor`. VtMB's fog is linear (fully fogged
	 * at `$fogend`); SLW's is exponential; no one value makes the two curves coincide, so the
	 * faithful default is the one at which they agree at the half-fog distance: 2 ln 2. Wire first,
	 * tune later -- the tuning session moves it, not the graph.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "16.0"))
	float WaterFogScale = 1.3862944f;

	/**
	 * `Water_Old` warps the refracted image in screen space by the DUDV flipbook times the authored
	 * `$refractamount` (`texbem`, `water_dx80.cpp::DrawRefraction`; VS c44 on the SM2 twin). The
	 * amounts are authored 15-100 and the DUDV's signed rms is 0.027, so this is the screen-UV
	 * offset per authored unit on `M_V2_Water`'s Refraction pin: 0.01 puts a typical sewer (35)
	 * at ~1 % of the frame, which is what the 2004 frames show. Tune by eye against them.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float WaterWarpScale = 0.01f;

	/**
	 * The same field warped `_rt_WaterReflection` by `$reflectamount`. There is no reflection
	 * image to displace on a deferred renderer, so the port tilts the normal Lumen mirrors off by
	 * DUDV times the amount times this, per authored unit (named modernization).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float WaterReflectWarpScale = 0.01f;

	/** `/Game/ElysiumGenerated/Materials/V2/MPC_ElysiumSurfaces`, the collection every V2 master reads. */
	static const TCHAR* CollectionPath();

	/**
	 * Every scalar this settings object owns, name → member pointer, the single source of truth
	 * `PushToCollection` and the Substrate test both walk. Names match the `CollectionParameter`
	 * nodes `pipeline/unreal/make_surface_knobs.py` creates on `MPC_ElysiumSurfaces` verbatim.
	 */
	static const TArray<TPair<FName, float UElysiumSurfaceSettings::*>>& ScalarBindings();

	/**
	 * Write every scalar binding into `MPC_ElysiumSurfaces`'s asset defaults (one
	 * `Collection->PostEditChange()`, so the next PIE or packaged load reads the new value) and
	 * into every live world's parameter-collection instance (so a running PIE session follows the
	 * slider without a restart). A missing collection asset no-ops with a logged warning rather
	 * than failing: the generator creates it, and a settings edit before the first `uv run elysium
	 * export bundle policy` run must not crash the editor.
	 *
	 * `BlueprintCallable` so `make_surface_knobs.py` can call it once after creating/seeding
	 * `MPC_ElysiumSurfaces`: that generator only ever owns row *existence* (never overwrites a row
	 * a prior run or a tuned edit already created), so a knob whose class default changed needs
	 * this call to actually reach the collection's stored value.
	 *
	 * Python: `unreal.get_default_object(unreal.ElysiumSurfaceSettings).push_to_collection()`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Surfaces")
	void PushToCollection() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// Substrate-only: lets FElysiumSurfaceSettingsCollectionPushTest push into a `/Temp/`-package
	// collection directly, rather than through LoadCollection()'s hardcoded CollectionPath() --
	// exercising the collection-mutation logic without ever touching (or shadowing in memory) the
	// production `MPC_ElysiumSurfaces` asset path.
	friend class FElysiumSurfaceSettingsCollectionPushTest;

	/**
	 * `LoadObject<UMaterialParameterCollection>` at `CollectionPath()`, or null with a logged
	 * warning when the asset does not exist yet (the generator creates it). Shared by
	 * `PushToCollection` and the interactive-drag branch of `PostEditChangeProperty`, so both
	 * resolve the collection the same way.
	 */
	static UMaterialParameterCollection* LoadCollection();

	/** Asset-defaults half of `PushToCollection`; editor-only, `WITH_EDITOR`-guarded body. */
	void PushToCollectionDefaults(UMaterialParameterCollection* Collection) const;

	/** Live-world half: every world context's `UMaterialParameterCollectionInstance`. */
	void PushToWorldInstances(UMaterialParameterCollection* Collection) const;
};
