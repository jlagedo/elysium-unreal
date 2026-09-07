#pragma once

#include "CoreMinimal.h"

#include "Engine/DataAsset.h"

#include "ElysiumTerminalScreenTuning.generated.h"

class UPrimitiveComponent;

// How one monitor model's authored `screen` UVs are laid out, so the render target lands the right
// way up on the glass.
//
// This is a per-MODEL fact and nothing else can answer it. A `.mdl`'s screen face carries whatever
// UVs its 2004 author gave it — mirrored, rotated, or neither — and the decal it was drawn with hid
// that. A live render target does not, so the flip is a property of the asset and belongs in a
// tracked authored table beside it rather than in a material instance per placement or in code.
//
// The projection's fallback answer is "none": an unlisted model draws the target unflipped, which is
// correct for every model whose UVs run the ordinary way and is a visible, fixable wrong for the
// rest. Slice D's calibration pattern (`SElysiumTerminalCells::SetCalibration`) is what reads a
// model's orientation off the glass so an entry can be authored.
USTRUCT(BlueprintType)
struct FElysiumTerminalScreenTuningEntry
{
	GENERATED_BODY()

	/** `M_ElysiumTerminalScreen`'s `FlipU` scalar: the sampled U becomes `1 - U`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Terminal")
	bool bFlipU = false;

	/** `FlipV`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Terminal")
	bool bFlipV = false;

	/** `Rotate90`: the U and V channels are swapped before the flips are applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Terminal")
	bool bRotate90 = false;
};

/**
 * `/Game/ElysiumAuthored/UI/DA_ElysiumTerminalScreenTuning` — the tracked table of per-model screen
 * UV orientation. Absent by default and absent is legal: with no asset, every terminal draws
 * unflipped.
 *
 * Keys are the baked model id with the `vtmb:model:` scheme stripped, so an entry reads
 * `scenery/furniture/computer/monitor_useable`. `FName` compares case-insensitively, which is what
 * the bake's own path casing needs.
 */
UCLASS(BlueprintType)
class UElysiumTerminalScreenTuning final : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Terminal",
		meta = (DisplayName = "Models"))
	TMap<FName, FElysiumTerminalScreenTuningEntry> Models;

	/**
	 * The authored table, loaded on first use and rooted for the process. Null — with ONE Verbose
	 * line, not a warning — when the asset is absent, because absent is the default state of this
	 * table and not a defect.
	 */
	static const UElysiumTerminalScreenTuning* Load();

	/** The model id a bound body's baked mesh belongs to, or empty when it is not a baked asset. */
	static FString ModelIdForBody(const UPrimitiveComponent* Body);

	/** The authored entry for a bound body, or the default (no flip) when it has none. */
	static FElysiumTerminalScreenTuningEntry FindForBody(const UPrimitiveComponent* Body);
};
