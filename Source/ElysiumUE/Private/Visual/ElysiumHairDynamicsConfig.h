#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "ElysiumHairDynamicsConfig.generated.h"

/**
 * One authored AnimDynamics chain on a character body: which run of bones swings, and how.
 *
 * These are owner-authored presentation values, tuned in the editor against the body they run on --
 * not a decode of anything the user's install carries. The fields mirror
 * `FElysiumHairDynamicsChainConfig`, which is the anim node's own configuration vocabulary; this is
 * the asset schema the editor edits, and `ElysiumNpcVisual::InstallHairDynamics` is the one place
 * the two meet.
 */
USTRUCT(BlueprintType)
struct FElysiumHairDynamicsChain
{
	GENERATED_BODY()

	/** The first simulated bone. Its parent is the frame the chain swings in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Hair")
	FName BoundBone;

	/** The last simulated bone. Must descend from `BoundBone` and differ from it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Hair")
	FName ChainEnd;

	/** Share of gravity the chain feels. Must be finite and non-negative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Hair")
	float GravityScale = 1.0f;

	/** Linear and angular damping, in [0.7, 1.0]; below that the chain never settles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Hair")
	float Damping = 0.9f;

	/** Angular spring constant. Zero -- the default -- leaves the spring disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Hair")
	float AngularSpring = 0.0f;

	/** Per-body cone limit in degrees, in [0, 90]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Hair")
	float ConeAngleDegrees = 0.0f;
};

/** Every authored chain on one body stem. An empty chain list means the body simulates nothing. */
USTRUCT(BlueprintType)
struct FElysiumHairDynamicsStem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Hair")
	TArray<FElysiumHairDynamicsChain> Chains;
};

/**
 * `/Game/ElysiumAuthored/Hair/DA_HairDynamics` -- the tracked authored table of which character
 * bodies swing hair, and with what tuning.
 *
 * The keys ARE the opt-in: a stem with no entry simulates nothing, which is the ordinary answer for
 * almost the whole cast and never a warning. Being a stem's entry is the whole scope gate, so there
 * is no allow-list anywhere in the runtime.
 *
 * `FName` keys compare case-insensitively, which is what a body stem needs -- the callers pass the
 * stem as the map or the bake spelled it.
 */
UCLASS(BlueprintType)
class UElysiumHairDynamicsConfig final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** One entry per model id. The serialized field name is retained for authored asset migration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Hair", meta=(DisplayName="Models"))
	TMap<FName, FElysiumHairDynamicsStem> Stems;

	/**
	 * The authored table, loaded on first use and rooted for the process. Null -- with one warning
	 * naming the missing package -- when the tracked asset is not present. The attempt is made once
	 * per process either way, because this is reached per body stood on every map load.
	 */
	static const UElysiumHairDynamicsConfig* Load();

	/** The authored entry for a model id, or null when the owner has not tuned it. */
	static const FElysiumHairDynamicsStem* FindModel(const FString& Model);
};
