#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumParticleAssetBuilder.generated.h"

class UNiagaraEmitter;
class UNiagaraSystem;
class UTexture;

/**
 * One Niagara emitter's worth of a compiled VtMB particle closure.
 *
 * VtMB's definitions are a spawn *graph* — a root names child particles by rate or burst, and those
 * children can name more. Niagara has no equivalent of a particle spawning a particle outside event
 * handlers, so the graph is flattened offline (`pipeline/unreal/make_particle_systems.py`) into one
 * layer per reachable leaf and handed over already resolved. Everything here is Unreal-native:
 * centimetres, seconds, linear colour.
 */
USTRUCT(BlueprintType)
struct FElysiumParticleLayer
{
	GENERATED_BODY()

	/** Emitter name inside the system — the leaf definition it came from. */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	FString Name;

	/** Package path of the sprite this layer draws, or empty to keep the template's material. */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	FString SpriteTexture;

	/** Continuous emission, particles per second. Zero for a burst-only layer. */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	float SpawnRate = 0.0f;

	/** One-shot count emitted at t=0. Zero for a rate-only layer. */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	int32 Burst = 0;

	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	float LifetimeSeconds = 1.0f;

	/** Sprite extent in centimetres (VtMB `size` x `height`). */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	FVector2D SpriteSize = FVector2D(1.0, 1.0);

	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	FLinearColor Color = FLinearColor::White;

	/** Initial velocity in cm/s, already resolved out of VtMB's cartesian and spherical speeds. */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	FVector Velocity = FVector::ZeroVector;

	/** Spawn offset from the emitter origin, in centimetres. */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	FVector Offset = FVector::ZeroVector;
};

/**
 * What one `NS_<root>` build reports back to the Python driver.
 *
 * `Errors` is fatal -- the system was not authored. `Skipped` is not: every module-input write is
 * attempted against whatever the base emitter actually exposes, and an input the base does not
 * carry is recorded here rather than failing the root. That is what lets the same generator run
 * against the stock Fountain template (the spike, where nearly everything skips) and against the
 * authored `E_VtMBLeaf` (where nearly nothing does).
 */
USTRUCT(BlueprintType)
struct FElysiumRootSystemResult
{
	GENERATED_BODY()

	/** `UNiagaraSystem::IsReadyToRun()` after the explicit compile. False is a hard refusal. */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	bool bReady = false;

	/**
	 * True when every emitter still carries the base emitter as its parent. `AddEmitter` always
	 * asks for inheritance (`UNiagaraSystem::AddEmitterHandle` -> `CreateWithParentAndOwner`), but
	 * an emitter asset marked `bIsInheritable = false` -- which every stock *template* is -- has
	 * its parent stripped on the way in, yielding a copy. The author needs to know which they got:
	 * only an inherited child follows later edits to the base.
	 */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	bool bInherited = false;

	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	int32 EmitterCount = 0;

	/** The names the engine actually gave the emitters, in tree order. */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	TArray<FString> EmitterNames;

	/** Per emitter, parallel to `EmitterNames`; -1 when the system was never probed. */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	TArray<int32> ParticleCounts;

	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	TArray<FString> Errors;

	/** `<emitter>/<script>/<module>/<input>: <engine text>` for every write the base did not take. */
	UPROPERTY(BlueprintReadWrite, Category="Elysium|Particles")
	TArray<FString> Skipped;
};

/** Editor-only implementation behind the reproducible Python particle-asset generator. */
UCLASS()
class ELYSIUMUE_API UElysiumParticleAssetBuilder final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Wait for any Niagara work started while existing systems were loaded for replacement. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Particles")
	static void FinishAssetCompilation();

	/** Author one Niagara system per VtMB emitter closure. Null on any authoring error. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Particles")
	static UNiagaraSystem* BuildParticleSystem(
		const FString& AssetName,
		const FString& PackagePath,
		UNiagaraEmitter* TemplateEmitter,
		const TArray<FElysiumParticleLayer>& Layers);

	/**
	 * Bind one layer's sprite material to its emitter's renderer. Unlike the rain system, whose
	 * three layers share one material, every VtMB layer carries its own sprite.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Particles")
	static bool BindLayerMaterial(
		UNiagaraSystem* System, int32 LayerIndex, UMaterialInterface* Material);

	/** Empty on success; otherwise a verifier-ready description of every discovered error. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Particles")
	static FString ValidateParticleSystem(UNiagaraSystem* System, int32 ExpectedLayers);

	// ------------------------------------------------------------------ the generated lane (R7.3)
	//
	// One `NS_<root>` per VtMB root, one inherited emitter per *drawing* node of the staged
	// `particleTrees{}` entry, composed headless and compiled explicitly
	// (`docs/project/niagara_authoring_strategy.md` 4.1). The four calls above stay for the legacy
	// per-map lane (`make_particle_systems.py`), which flattens a closure into `FElysiumParticleLayer`
	// and knows nothing about trees.

	/**
	 * Author `<PackagePath>/<AssetName>` from `BaseEmitter` and the staged tree.
	 *
	 * `TreeJson` is one `particleTrees{}` value, verbatim -- `{"root", "name", "nodes":[...],
	 * "stats"}`. It travels as JSON rather than as `TArray<FElysiumParticleNode>` because that
	 * struct is a plain `USTRUCT()` and cannot cross a `UFUNCTION` boundary without changing the
	 * runtime header the R7.3 actor contract is written against; and because the staged document
	 * is already the bake's own transport for exactly this shape, so there is one parser rather
	 * than a second Python-side field-mapping table.
	 *
	 * Does not save. Null on a fatal error, with `OutResult.Errors` filled either way.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Particles")
	static UNiagaraSystem* BuildRootSystem(
		const FString& AssetName,
		const FString& PackagePath,
		UNiagaraEmitter* BaseEmitter,
		const FString& TreeJson,
		FElysiumRootSystemResult& OutResult);

	/**
	 * Compile the system to completion and read its readiness back.
	 *
	 * `IsReadyToRun()` is live in an uncooked build, so this is the real gate and not a cache read
	 * -- but it returns false unconditionally when `FApp::CanEverRender()` is false, so the host
	 * commandlet must carry `-AllowCommandletRendering`.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Particles")
	static FElysiumRootSystemResult FinishAndVerify(UNiagaraSystem* System);

	/**
	 * Spawn the system into the editor world, tick it `TickCount` times and report per-emitter
	 * particle counts. Best effort: a headless editor has no tick loop of its own, so the world is
	 * ticked by hand and anything that refuses is reported in `Errors` rather than fataled.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Particles")
	static FElysiumRootSystemResult ProbeSystem(
		UNiagaraSystem* System, int32 TickCount = 30, float DeltaSeconds = 0.0333f);
};
