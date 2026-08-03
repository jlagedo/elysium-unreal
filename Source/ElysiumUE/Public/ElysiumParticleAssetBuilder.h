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

/** Editor-only implementation behind the reproducible Python particle-asset generator. */
UCLASS()
class ELYSIUMUE_API UElysiumParticleAssetBuilder final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
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
		UNiagaraSystem* System, const FString& LayerName, UMaterialInterface* Material);

	/** Empty on success; otherwise a verifier-ready description of every discovered error. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Particles")
	static FString ValidateParticleSystem(UNiagaraSystem* System, int32 ExpectedLayers);
};
