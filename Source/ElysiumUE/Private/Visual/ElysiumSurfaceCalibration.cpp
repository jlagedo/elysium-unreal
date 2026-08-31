#include "ElysiumSurfaceCalibration.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSurfaceCalibration, Log, All);

namespace
{
	/** `/Game/ElysiumGenerated/Materials/V2/T_SurfaceClassLUT`, beside the calibration data asset. */
	const TCHAR* LutPackagePath()
	{
		return TEXT("/Game/ElysiumGenerated/Materials/V2/T_SurfaceClassLUT");
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

	if (!Lut)
	{
		// LoadObject first, not FindObject: a prior run (or another lane's placeholder importer)
		// may have left a real asset on disk at this path with different platform/bulk data than
		// a freshly NewObject'd texture would carry, and overwriting that mismatch in place at
		// save time is what corrupts the package. Loading it properly first, exactly as the
		// production texture-import lane does, gives Source.Init below a texture whose bulk data
		// plumbing is actually wired up, so replacing its source cleanly replaces the payload too.
		const FString ObjectPath = FString(LutPackagePath()) + TEXT(".") + FPackageName::GetShortName(LutPackagePath());
		Lut = LoadObject<UTexture2D>(nullptr, *ObjectPath);
		if (!Lut)
		{
			UPackage* Package = CreatePackage(LutPackagePath());
			if (!Package)
			{
				OutError = FString::Printf(TEXT("could not create package %s"), LutPackagePath());
				return;
			}
			const FString AssetName = FPackageName::GetShortName(LutPackagePath());
			Lut = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
			if (!Lut)
			{
				OutError = TEXT("could not create the LUT texture object");
				return;
			}
			FAssetRegistryModule::AssetCreated(Lut);
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

	Lut->Source.Init(MaxRows, 1, /*NumSlices*/ 1, /*NumMips*/ 1, TSF_BGRA8, Texels);
	Lut->SRGB = false;
	Lut->CompressionSettings = TC_VectorDisplacementmap;
	Lut->MipGenSettings = TMGS_NoMipmaps;
	Lut->Filter = TF_Nearest;
	Lut->LODGroup = TEXTUREGROUP_UI;
	Lut->AddressX = TA_Clamp;
	Lut->AddressY = TA_Clamp;
	Lut->LossyCompressionAmount = TLCA_None;
	Lut->UpdateResource();
	Lut->MarkPackageDirty();
	MarkPackageDirty();

	bOutOk = true;
#endif
}

#if WITH_EDITOR
void UElysiumSurfaceCalibration::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
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
	const TArray<FString>& ClassNames, bool& bOutOk, FString& OutError)
{
	bOutOk = false;
	OutError.Reset();
	if (!Calibration)
	{
		OutError = TEXT("no calibration asset");
		return;
	}
	if (ClassNames.Num() > UElysiumSurfaceCalibration::MaxRows)
	{
		OutError = FString::Printf(TEXT("%d class names exceeds the %d-row limit"),
			ClassNames.Num(), UElysiumSurfaceCalibration::MaxRows);
		return;
	}

	// Index is assigned 0..N-1 in the caller's own order -- the pipeline's SURFACE_CLASSES list
	// already puts `default` first, so this function trusts that order rather than re-deriving it.
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

	TArray<FElysiumSurfaceClassRow> NewRows;
	NewRows.Reserve(ClassNames.Num());
	for (int32 Index = 0; Index < ClassNames.Num(); ++Index)
	{
		FElysiumSurfaceClassRow Row;
		Row.Name = FName(*ClassNames[Index]);
		Row.Index = Index;
		NewRows.Add(Row);
	}

	Calibration->Rows = MoveTemp(NewRows);
	Calibration->MarkPackageDirty();
	bOutOk = true;
}
