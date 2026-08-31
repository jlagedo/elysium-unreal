#include "ElysiumSurfaceCalibration.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSurfaceCalibration, Log, All);

namespace
{
	/** The LUT's object name, inside the calibration data asset's own package. */
	const TCHAR* LutObjectName()
	{
		return TEXT("T_SurfaceClassLUT");
	}
}

int32 UElysiumSurfaceCalibration::IndexOf(FName ClassKey) const
{
	for (const FElysiumSurfaceClassRow& Row : Rows)
	{
		if (Row.Name.IsEqual(ClassKey, ENameCase::IgnoreCase))
		{
			return Row.Index;
		}
	}
	return INDEX_NONE;
}

void UElysiumSurfaceCalibration::RegenerateLut(bool& bOutOk, FString& OutError)
{
	bOutOk = false;
	OutError.Reset();
#if !WITH_EDITOR
	OutError = TEXT("RegenerateLut needs FTextureSource, which is editor-only");
	return;
#else
	if (Rows.Num() > MaxRows)
	{
		OutError = FString::Printf(TEXT("%d rows exceeds the %d-row limit"), Rows.Num(), MaxRows);
		return;
	}

	// Every row claims a texel by its own Index, not by position, so a reorder in the details-panel
	// grid can never remap an already-imported instance's SurfaceClassIndex. Validate before
	// touching the texture: an out-of-range, duplicate or name-colliding row is a stage failure,
	// not a quietly overwritten texel.
	TSet<int32> SeenIndices;
	TSet<FName> SeenNames;
	for (const FElysiumSurfaceClassRow& Row : Rows)
	{
		if (Row.Index < 0 || Row.Index >= MaxRows)
		{
			OutError = FString::Printf(TEXT("row %s has Index %d, outside [0, %d)"),
				*Row.Name.ToString(), Row.Index, MaxRows);
			return;
		}
		bool bIndexAlreadySeen = false;
		SeenIndices.Add(Row.Index, &bIndexAlreadySeen);
		if (bIndexAlreadySeen)
		{
			OutError = FString::Printf(TEXT("Index %d is claimed by more than one row"), Row.Index);
			return;
		}
		bool bNameAlreadySeen = false;
		SeenNames.Add(Row.Name, &bNameAlreadySeen);
		if (bNameAlreadySeen)
		{
			OutError = FString::Printf(TEXT("Name %s is claimed by more than one row"), *Row.Name.ToString());
			return;
		}
	}

	if (!Lut || Lut->GetOuter() != this)
	{
		// Created inside this data asset's own package (outer `this`), never a sibling package at
		// a hand-guessed path: a transient calibration (a Substrate test's NewObject'd instance)
		// must never touch the production `/Game/.../DA_SurfaceCalibration` package's disk state
		// via a LoadObject/CreatePackage fallback, and a saved calibration's LUT living in its own
		// package is what makes Ctrl+S on the data asset save the LUT along with it -- no second
		// asset to remember to save, no sibling-package divergence.
		//
		// C-3: `Lut && Lut->GetOuter() != this` is the migration case -- an existing calibration
		// asset authored before the LUT moved in-package still points at the sibling
		// `T_SurfaceClassLUT` package this generator no longer saves, so that texture is stale the
		// moment this call runs. Nothing about the old object is copied forward (its payload is
		// regenerated from `Rows` below regardless); this just replaces the pointer with a fresh
		// in-package texture and logs the migration so it shows up in a generator run's log.
		const bool bMigrating = Lut != nullptr;
		Lut = NewObject<UTexture2D>(this, LutObjectName(), RF_Public);
		if (!Lut)
		{
			OutError = TEXT("could not create the LUT texture object");
			return;
		}
		if (bMigrating)
		{
			UE_LOG(LogElysiumSurfaceCalibration, Log,
				TEXT("migrated the surface-class LUT into %s's own package (was a sibling asset)"),
				*GetName());
		}
	}

	// One texel per row, written at that row's own Index. A texel no row claims holds the struct's
	// own default, a defined neutral rather than zeroed memory -- SurfaceClassIndex is a
	// master-supplied lookup and an out-of-range index must not read black.
	uint8 Texels[MaxRows * 4];
	static const FElysiumSurfaceClassRow Neutral;
	auto WriteTexel = [](uint8* Texel, const FElysiumSurfaceClassRow& Row)
	{
		Texel[0] = static_cast<uint8>(FMath::Clamp(Row.Metallic, 0.0f, 1.0f) * 255.0f + 0.5f);  // B
		Texel[1] = static_cast<uint8>(FMath::Clamp(Row.Specular, 0.0f, 1.0f) * 255.0f + 0.5f);  // G
		Texel[2] = static_cast<uint8>(FMath::Clamp(Row.Roughness, 0.0f, 1.0f) * 255.0f + 0.5f); // R
		Texel[3] = 255;                                                                          // A
	};
	for (int32 Index = 0; Index < MaxRows; ++Index)
	{
		WriteTexel(&Texels[Index * 4], Neutral);
	}
	for (const FElysiumSurfaceClassRow& Row : Rows)
	{
		WriteTexel(&Texels[Row.Index * 4], Row);
	}

	// PreEditChange/PostEditChange around a source replacement is the engine's own idiom
	// (Texture.h:36, "All changes to Texture properties must be wrapped in PreEditChange/
	// PostEditChange"): it invalidates the texture's render-thread resource and any cached
	// derived data before Source.Init rewrites the payload, rather than leaving UpdateResource
	// alone to reconcile a source that changed size or format out from under a live resource.
	Lut->PreEditChange(nullptr);
	Lut->Source.Init(MaxRows, 1, /*NumSlices*/ 1, /*NumMips*/ 1, TSF_BGRA8, Texels);
	Lut->SRGB = false;
	Lut->CompressionSettings = TC_VectorDisplacementmap;
	Lut->MipGenSettings = TMGS_NoMipmaps;
	Lut->Filter = TF_Nearest;
	Lut->LODGroup = TEXTUREGROUP_UI;
	Lut->AddressX = TA_Clamp;
	Lut->AddressY = TA_Clamp;
	Lut->LossyCompressionAmount = TLCA_None;
	Lut->PostEditChange();
	Lut->MarkPackageDirty();
	MarkPackageDirty();

	bOutOk = true;
#endif
}

#if WITH_EDITOR
void UElysiumSurfaceCalibration::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// An interactive change (a slider being dragged in the details panel) fires this on every
	// tick of the drag; regenerating and re-uploading a 128-texel source on every one of those
	// ticks is wasted work the drag never needs to see finished until it lets go. Regenerate on
	// the terminal ValueSet (mouse-up, or a typed value committed) only.
	// `EPropertyChangeType::Type` is a plain bitmask (UnrealType.h), not an exclusive enum: an
	// interactive drag can arrive combined with another flag, so testing equality against the
	// single `Interactive` value misses those and would bake mid-drag after all.
	if ((PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive) != 0)
	{
		return;
	}
	bool bOk = false;
	FString Error;
	RegenerateLut(bOk, Error);
	if (!bOk)
	{
		UE_LOG(LogElysiumSurfaceCalibration, Warning, TEXT("RegenerateLut on edit failed: %s"), *Error);
	}
}
#endif

void UElysiumSurfaceCalibration::SeedDefaultRows(UElysiumSurfaceCalibration* Calibration,
	const TArray<FString>& ClassNames, bool& bOutOk, FString& OutError, int32& OutAdded, int32& OutKept)
{
	bOutOk = false;
	OutError.Reset();
	OutAdded = 0;
	OutKept = 0;
	if (!Calibration)
	{
		OutError = TEXT("no calibration asset");
		return;
	}

	// Index is assigned 0..N-1 in the caller's own order only for a name with no existing row --
	// the pipeline's SURFACE_CLASSES list already puts `default` first, so a brand-new asset ends
	// up in that same order, but a rerun over an asset that already has rows never renumbers one.
	TSet<FString> SeenNames;
	for (const FString& Name : ClassNames)
	{
		bool bAlreadySeen = false;
		SeenNames.Add(Name.ToLower(), &bAlreadySeen);
		if (bAlreadySeen)
		{
			OutError = FString::Printf(TEXT("class name %s appears more than once"), *Name);
			return;
		}
	}

	TSet<FString> ExistingLowerNames;
	TSet<int32> ClaimedIndices;
	ExistingLowerNames.Reserve(Calibration->Rows.Num());
	for (const FElysiumSurfaceClassRow& Row : Calibration->Rows)
	{
		ExistingLowerNames.Add(Row.Name.ToString().ToLower());
		ClaimedIndices.Add(Row.Index);
	}

	// A name with no existing row will be appended; count how many that is and refuse before
	// mutating anything when the merged total would exceed MaxRows, so a rejected seed leaves
	// Rows exactly as it found it, just like the pre-merge behavior.
	int32 UnseenCount = 0;
	for (const FString& Name : ClassNames)
	{
		if (!ExistingLowerNames.Contains(Name.ToLower()))
		{
			++UnseenCount;
		}
	}
	const int32 MergedCount = Calibration->Rows.Num() + UnseenCount;
	if (MergedCount > UElysiumSurfaceCalibration::MaxRows)
	{
		OutError = FString::Printf(TEXT("merged row count %d exceeds the %d-row limit"),
			MergedCount, UElysiumSurfaceCalibration::MaxRows);
		return;
	}

	int32 NextFreeIndex = 0;
	auto ClaimNextFreeIndex = [&ClaimedIndices, &NextFreeIndex]() -> int32
	{
		while (ClaimedIndices.Contains(NextFreeIndex))
		{
			++NextFreeIndex;
		}
		ClaimedIndices.Add(NextFreeIndex);
		return NextFreeIndex;
	};

	int32 Added = 0;
	int32 Kept = 0;
	for (const FString& Name : ClassNames)
	{
		if (ExistingLowerNames.Contains(Name.ToLower()))
		{
			++Kept;
			continue;
		}
		FElysiumSurfaceClassRow Row;
		Row.Name = FName(*Name);
		Row.Index = ClaimNextFreeIndex();
		Calibration->Rows.Add(Row);
		++Added;
	}

	if (Added > 0)
	{
		Calibration->MarkPackageDirty();
	}
	OutAdded = Added;
	OutKept = Kept;
	bOutOk = true;
}
