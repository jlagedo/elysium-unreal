#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "ElysiumLightCalibration.generated.h"

// One map's hand-tuned light overrides: the per-map half of the lighting seam, `UElysiumLightingSettings` being the global
// half. A row is a **merge**, not a replacement -- every field below is its own on/off switch plus
// a value, so a row can move one light's reach without restating its colour, intensity and every
// other attribute the calibrated baseline already got right. A source absent from `Rows` keeps
// exactly the value `UElysiumLightRig::ApplyToSource` derives for it; the asset states only what a
// human decided should differ from that.
//
// Keyed by the `.lights` line index (`worldLights[]`'s `sourceOffset` order),
// which survives a re-export -- not the rig's live
// array position, which drops the skyambient row and any row with no matching baked actor.
//
// No producer writes this asset. It ships with zero rows for every map (today's faithful defaults,
// unchanged) and is authored directly in the Content Browser's property panel -- the same "the
// Unreal editor is the tuning surface" rule `UElysiumSurfaceSettings` and the deleted Cog Lights
// per-light editor followed, just with a per-map asset in place of a per-map JSON survey file.
USTRUCT()
struct FElysiumLightCalibrationRow
{
	GENERATED_BODY()

	/** The `.lights` line this row overrides. `INDEX_NONE` is never matched. */
	UPROPERTY(EditAnywhere, Category = "Elysium")
	int32 SourceIndex = INDEX_NONE;

	/** Switches the source off entirely; independent of every value override below. */
	UPROPERTY(EditAnywhere, Category = "Elysium")
	bool bDisabled = false;

	UPROPERTY(EditAnywhere, Category = "Elysium", meta = (InlineEditConditionToggle))
	bool bOverrideIntensity = false;

	UPROPERTY(EditAnywhere, Category = "Elysium",
		meta = (EditCondition = "bOverrideIntensity", ClampMin = "0.0"))
	float Intensity = 0.f;

	UPROPERTY(EditAnywhere, Category = "Elysium", meta = (InlineEditConditionToggle))
	bool bOverrideReach = false;

	/** Local/spot attenuation radius, centimetres. No effect on the sun. */
	UPROPERTY(EditAnywhere, Category = "Elysium",
		meta = (EditCondition = "bOverrideReach", ClampMin = "1.0"))
	float ReachCm = 0.f;

	UPROPERTY(EditAnywhere, Category = "Elysium", meta = (InlineEditConditionToggle))
	bool bOverrideColor = false;

	UPROPERTY(EditAnywhere, Category = "Elysium", meta = (EditCondition = "bOverrideColor"))
	FLinearColor Color = FLinearColor::White;
};

// `/ElysiumBaked/<map>/DA_<map>_LightCalibration`, one asset per map, beside `DA_<map>_Entities` and
// `DA_<map>_Collision`. `FElysiumContentPaths::BakedMapLightCalibration` is the one path accessor.
// The asset's presence is the cutover flag, as in R4.1/R4.2: a map with one has its rows applied on
// top of the calibrated baseline; a map without one (every map today -- "no remapper, nothing to
// migrate") runs exactly as before this task landed.
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumLightCalibration : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Elysium")
	TArray<FElysiumLightCalibrationRow> Rows;

	/** The row for one `.lights` line, or null if the asset has none for it. */
	const FElysiumLightCalibrationRow* FindRow(int32 SourceIndex) const;
};
