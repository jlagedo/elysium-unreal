#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "ElysiumEyeTuningConfig.generated.h"

/**
 * `/Game/ElysiumAuthored/Eyes/DA_EyeTuning` — the corpus-wide iris size / eye shift baseline every
 * rendered eye (player and NPC alike) is composed against, beside `Cloth/DA_ClothTuning`.
 *
 * Before R4.5 these two numbers existed only as `FElysiumEyeTuning`'s implicit struct default
 * (`ElysiumEyeRig.h`) — reachable from nowhere but the Green Room's "Reset tuning" button, so a
 * permanent corpus-wide nudge had no home short of a hand-edited C++ literal and a rebuild. This
 * asset is that home: `FElysiumEyePass::TickEyes` adds it under the Green Room's own live debug
 * nudge (`FElysiumEyeDebug::Tuning`), so the two compose rather than one replacing the other, and
 * an untouched asset (today's shipped 0 / zero-vector) changes nothing about what is drawn.
 *
 * `bEyeMove` is deliberately not carried here: it is the Green Room's own gaze-mode switch (parks
 * the eye on its authored resting aim), not a size/shift tuning value.
 */
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumEyeTuningConfig final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Widens or narrows the iris: `1 / (1 / IrisScale + EyeSize)`. 0 is the shipped config. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Eyes")
	float EyeSize = 0.0f;

	/**
	 * Per-component nudge applied to the eye centre, by the sign of each component (centimetres).
	 * Shifts the iris planes but not the shading origin, matching `FElysiumEyeTuning::EyeShift`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Eyes")
	FVector EyeShift = FVector::ZeroVector;

	/**
	 * The authored table, loaded on first use and rooted for the process. Null — with one error
	 * naming the package — when the asset is not on the mount, in which case every caller composes
	 * against a neutral baseline (no size/shift adjustment), exactly as before this asset existed.
	 */
	static const UElysiumEyeTuningConfig* Load();
};
