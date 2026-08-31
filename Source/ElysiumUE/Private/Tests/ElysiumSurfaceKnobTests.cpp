// Content-free Substrate automation for SF-4.1: the surface-shading knobs a human tunes by eye
// (docs/architecture/seam_map_material.md -> "Import" -> "Knob contract";
// docs/project/seam_migration.md 2026-08-31, "Calibration happens on knobs inside the editor").
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumSurfaceCalibration.h"
#include "ElysiumSurfaceSettings.h"

#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

static constexpr EAutomationTestFlags GElysiumSurfaceKnobTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// ---------------------------------------------------------------------------------------------
// UElysiumSurfaceSettings
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSurfaceSettingsDefaultsTest,
	"Elysium.Substrate.SurfaceSettings.Defaults", GElysiumSurfaceKnobTestFlags)
bool FElysiumSurfaceSettingsDefaultsTest::RunTest(const FString&)
{
	const TArray<TPair<FName, float UElysiumSurfaceSettings::*>>& Bindings =
		UElysiumSurfaceSettings::ScalarBindings();

	// The ten named in the mechanics plan, plus the design doc's Overbright, DecalDepthOffset,
	// MaskMetallicMax, ChromaticTintStrength and ChromaThreshold -- fifteen settings scalars in
	// all (seam_map_material.md knob contract; owner review addendum 2026-08-31).
	TestTrue(TEXT("at least fifteen scalar bindings"), Bindings.Num() >= 15);

	TSet<FName> Names;
	for (const TPair<FName, float UElysiumSurfaceSettings::*>& Binding : Bindings)
	{
		bool bAlreadyInSet = false;
		Names.Add(Binding.Key, &bAlreadyInSet);
		if (bAlreadyInSet)
		{
			AddError(FString::Printf(TEXT("duplicate scalar binding name %s"), *Binding.Key.ToString()));
		}
	}
	TestEqual(TEXT("every binding name is unique"), Names.Num(), Bindings.Num());

	static const TCHAR* Expected[] = {
		TEXT("DefaultSpecular"), TEXT("DefaultRoughness"), TEXT("DefaultMetallic"),
		TEXT("LightSpecularScale"), TEXT("Overbright"), TEXT("MaskRoughnessMin"),
		TEXT("MaskRoughnessMax"), TEXT("MaskSpecularScale"), TEXT("MaskMetallicMax"),
		TEXT("EnvTintScale"), TEXT("FixedCubeStrength"), TEXT("ChromaticTintStrength"),
		TEXT("ChromaThreshold"), TEXT("DecalDepthOffset"), TEXT("CaptureRadius"),
	};
	for (const TCHAR* Name : Expected)
	{
		if (!Names.Contains(FName(Name)))
		{
			AddError(FString::Printf(TEXT("ScalarBindings is missing %s"), Name));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSurfaceSettingsIniRoundTripTest,
	"Elysium.Substrate.SurfaceSettings.IniRoundTrip", GElysiumSurfaceKnobTestFlags)
bool FElysiumSurfaceSettingsIniRoundTripTest::RunTest(const FString&)
{
	UElysiumSurfaceSettings* Settings = GetMutableDefault<UElysiumSurfaceSettings>();
	if (!Settings)
	{
		AddError(TEXT("GetMutableDefault<UElysiumSurfaceSettings> returned null"));
		return false;
	}

	const float Saved = Settings->DefaultSpecular;
	const float Sentinel = 0.777f;
	Settings->DefaultSpecular = Sentinel;

	// A scratch ini under ProjectSavedDir/ElysiumTests, never the tracked Config/DefaultElysium.ini.
	const FString ScratchIni = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("ElysiumTests") / (FGuid::NewGuid().ToString() + TEXT(".ini")));

	const bool bWrote = Settings->TryUpdateDefaultConfigFile(ScratchIni);
	bool bPassed = true;
	if (!bWrote)
	{
		AddError(TEXT("TryUpdateDefaultConfigFile returned false"));
		bPassed = false;
	}
	else
	{
		float ReadBack = 0.0f;
		const bool bFound = GConfig->GetFloat(TEXT("/Script/ElysiumUE.ElysiumSurfaceSettings"),
			TEXT("DefaultSpecular"), ReadBack, ScratchIni);
		if (!bFound)
		{
			AddError(FString::Printf(TEXT("DefaultSpecular not found in scratch ini %s"), *ScratchIni));
			bPassed = false;
		}
		else
		{
			TestEqual(TEXT("scratch ini round-trips the sentinel"), ReadBack, Sentinel);
		}
	}

	// Restore, and drop the scratch file plus GConfig's cached copy of it.
	Settings->DefaultSpecular = Saved;
	GConfig->UnloadFile(ScratchIni);
	IFileManager::Get().Delete(*ScratchIni, /*RequireExists*/ false);

	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSurfaceSettingsCollectionPushTest,
	"Elysium.Substrate.SurfaceSettings.CollectionPush", GElysiumSurfaceKnobTestFlags)
bool FElysiumSurfaceSettingsCollectionPushTest::RunTest(const FString&)
{
	if (!GEngine || GEngine->GetWorldContexts().Num() == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no world contexts to push a live collection instance into"));
		return true;
	}

	// A transient collection registered exactly at CollectionPath(): LoadObject inside
	// PushToCollection finds it in memory without touching disk, real or absent.
	UPackage* Package = CreatePackage(UElysiumSurfaceSettings::CollectionPath());
	const FString ShortName = FPackageName::GetShortName(FString(UElysiumSurfaceSettings::CollectionPath()));
	UMaterialParameterCollection* Collection = NewObject<UMaterialParameterCollection>(
		Package, *ShortName, RF_Transient);
	if (!Collection)
	{
		AddError(TEXT("could not create a transient MaterialParameterCollection"));
		return false;
	}

	const TArray<TPair<FName, float UElysiumSurfaceSettings::*>>& Bindings =
		UElysiumSurfaceSettings::ScalarBindings();
	for (const TPair<FName, float UElysiumSurfaceSettings::*>& Binding : Bindings)
	{
		FCollectionScalarParameter& Parameter = Collection->ScalarParameters.AddDefaulted_GetRef();
		Parameter.ParameterName = Binding.Key;
		Parameter.DefaultValue = -1.0f;
	}

	UElysiumSurfaceSettings* Settings = GetMutableDefault<UElysiumSurfaceSettings>();
	Settings->PushToCollection();

	bool bPassed = true;
	for (const TPair<FName, float UElysiumSurfaceSettings::*>& Binding : Bindings)
	{
		bool bFound = false;
		const float Value = Collection->GetScalarParameterDefaultValue(Binding.Key, bFound);
		if (!bFound)
		{
			AddError(FString::Printf(TEXT("collection has no row for %s after PushToCollection"),
				*Binding.Key.ToString()));
			bPassed = false;
			continue;
		}
		if (!FMath::IsNearlyEqual(Value, Settings->*Binding.Value))
		{
			AddError(FString::Printf(TEXT("%s: collection default %f != settings value %f"),
				*Binding.Key.ToString(), Value, Settings->*Binding.Value));
			bPassed = false;
		}
	}

	// Out of the way of any later test or real load at the same path.
	Collection->ClearFlags(RF_Standalone);
	Collection->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
	Collection->MarkAsGarbage();

	return bPassed;
}

// ---------------------------------------------------------------------------------------------
// UElysiumSurfaceCalibration
// ---------------------------------------------------------------------------------------------

namespace
{
	UElysiumSurfaceCalibration* NewCalibration()
	{
		return NewObject<UElysiumSurfaceCalibration>(GetTransientPackage(), NAME_None, RF_Transient);
	}

	FElysiumSurfaceClassRow MakeRow(const TCHAR* Name, int32 Index, float Roughness, float Specular, float Metallic)
	{
		FElysiumSurfaceClassRow Row;
		Row.Name = FName(Name);
		Row.Index = Index;
		Row.Roughness = Roughness;
		Row.Specular = Specular;
		Row.Metallic = Metallic;
		return Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSurfaceCalibrationLutTexelsTest,
	"Elysium.Substrate.SurfaceCalibration.LutTexels", GElysiumSurfaceKnobTestFlags)
bool FElysiumSurfaceCalibrationLutTexelsTest::RunTest(const FString&)
{
	UElysiumSurfaceCalibration* Calibration = NewCalibration();
	Calibration->Rows = {
		MakeRow(TEXT("brick"), 0, 0.7f, 0.4f, 0.0f),
		MakeRow(TEXT("metal"), 1, 0.2f, 0.9f, 1.0f),
		MakeRow(TEXT("default"), 2, 0.6f, 0.5f, 0.0f),
	};

	bool bOk = false;
	FString Error;
	Calibration->RegenerateLut(bOk, Error);
	if (!bOk)
	{
		AddError(FString::Printf(TEXT("RegenerateLut failed: %s"), *Error));
		return false;
	}
	if (!Calibration->Lut)
	{
		AddError(TEXT("RegenerateLut produced no Lut texture"));
		return false;
	}
	TestEqual(TEXT("Lut is 64 wide"), Calibration->Lut->Source.GetSizeX(), int64(UElysiumSurfaceCalibration::MaxRows));
	TestEqual(TEXT("Lut is 1 tall"), Calibration->Lut->Source.GetSizeY(), int64(1));
	TestEqual(TEXT("Lut format is BGRA8"), int32(Calibration->Lut->Source.GetFormat()), int32(TSF_BGRA8));

	const uint8* Texels = Calibration->Lut->Source.LockMipReadOnly(0);
	if (!Texels)
	{
		AddError(TEXT("Source.LockMipReadOnly(0) returned null"));
		return false;
	}

	auto CheckTexel = [this, Texels](int32 Index, float Roughness, float Specular, float Metallic)
	{
		const uint8* Texel = Texels + Index * 4;
		const uint8 ExpectB = static_cast<uint8>(FMath::Clamp(Metallic, 0.0f, 1.0f) * 255.0f + 0.5f);
		const uint8 ExpectG = static_cast<uint8>(FMath::Clamp(Specular, 0.0f, 1.0f) * 255.0f + 0.5f);
		const uint8 ExpectR = static_cast<uint8>(FMath::Clamp(Roughness, 0.0f, 1.0f) * 255.0f + 0.5f);
		TestEqual(*FString::Printf(TEXT("row %d B (metallic)"), Index), Texel[0], ExpectB);
		TestEqual(*FString::Printf(TEXT("row %d G (specular)"), Index), Texel[1], ExpectG);
		TestEqual(*FString::Printf(TEXT("row %d R (roughness)"), Index), Texel[2], ExpectR);
		TestEqual(*FString::Printf(TEXT("row %d A"), Index), Texel[3], uint8(255));
	};
	CheckTexel(0, 0.7f, 0.4f, 0.0f);
	CheckTexel(1, 0.2f, 0.9f, 1.0f);
	CheckTexel(2, 0.6f, 0.5f, 0.0f);
	// Past Num(): the struct's own default (roughness 0.6, specular 0.5, metallic 0), a defined
	// neutral rather than zeroed memory.
	CheckTexel(3, 0.6f, 0.5f, 0.0f);
	CheckTexel(UElysiumSurfaceCalibration::MaxRows - 1, 0.6f, 0.5f, 0.0f);

	Calibration->Lut->Source.UnlockMip(0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSurfaceCalibrationReorderIsStableTest,
	"Elysium.Substrate.SurfaceCalibration.ReorderIsStable", GElysiumSurfaceKnobTestFlags)
bool FElysiumSurfaceCalibrationReorderIsStableTest::RunTest(const FString&)
{
	// Two calibrations, same rows, one reordered in Rows -- RegenerateLut writes each row at its
	// own Index, so reordering (or inserting) rows in the details-panel grid must not remap an
	// already-imported instance's SurfaceClassIndex.
	UElysiumSurfaceCalibration* InOrder = NewCalibration();
	InOrder->Rows = {
		MakeRow(TEXT("default"), 0, 0.6f, 0.5f, 0.0f),
		MakeRow(TEXT("brick"), 1, 0.7f, 0.4f, 0.0f),
		MakeRow(TEXT("metal"), 2, 0.2f, 0.9f, 1.0f),
	};
	UElysiumSurfaceCalibration* Reordered = NewCalibration();
	Reordered->Rows = {
		MakeRow(TEXT("metal"), 2, 0.2f, 0.9f, 1.0f),
		MakeRow(TEXT("default"), 0, 0.6f, 0.5f, 0.0f),
		MakeRow(TEXT("brick"), 1, 0.7f, 0.4f, 0.0f),
	};

	bool bOkA = false, bOkB = false;
	FString ErrorA, ErrorB;
	InOrder->RegenerateLut(bOkA, ErrorA);
	Reordered->RegenerateLut(bOkB, ErrorB);
	if (!bOkA || !bOkB)
	{
		AddError(FString::Printf(TEXT("RegenerateLut failed: '%s' / '%s'"), *ErrorA, *ErrorB));
		return false;
	}

	const uint8* TexelsA = InOrder->Lut->Source.LockMipReadOnly(0);
	const uint8* TexelsB = Reordered->Lut->Source.LockMipReadOnly(0);
	if (!TexelsA || !TexelsB)
	{
		AddError(TEXT("LockMipReadOnly(0) returned null"));
		return false;
	}
	const bool bIdentical = FMemory::Memcmp(TexelsA, TexelsB, UElysiumSurfaceCalibration::MaxRows * 4) == 0;
	TestTrue(TEXT("reordering Rows does not change the baked LUT"), bIdentical);

	InOrder->Lut->Source.UnlockMip(0);
	Reordered->Lut->Source.UnlockMip(0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSurfaceCalibrationRowLimitTest,
	"Elysium.Substrate.SurfaceCalibration.RowLimit", GElysiumSurfaceKnobTestFlags)
bool FElysiumSurfaceCalibrationRowLimitTest::RunTest(const FString&)
{
	UElysiumSurfaceCalibration* Calibration = NewCalibration();
	Calibration->Rows.SetNum(UElysiumSurfaceCalibration::MaxRows + 1);
	for (int32 Index = 0; Index < Calibration->Rows.Num(); ++Index)
	{
		Calibration->Rows[Index].Name = FName(*FString::Printf(TEXT("class%d"), Index));
	}

	bool bOk = true;
	FString Error;
	Calibration->RegenerateLut(bOk, Error);
	TestFalse(TEXT("MaxRows+1 rows fails RegenerateLut"), bOk);
	TestFalse(TEXT("failure carries a reason"), Error.IsEmpty());

	// SeedDefaultRows applies the same limit on the class-name list.
	UElysiumSurfaceCalibration* Seeded = NewCalibration();
	TArray<FString> TooManyNames;
	TooManyNames.SetNum(UElysiumSurfaceCalibration::MaxRows + 1);
	for (int32 Index = 0; Index < TooManyNames.Num(); ++Index)
	{
		TooManyNames[Index] = FString::Printf(TEXT("class%d"), Index);
	}
	bool bSeeded = true;
	FString SeedError;
	UElysiumSurfaceCalibration::SeedDefaultRows(Seeded, TooManyNames, bSeeded, SeedError);
	TestFalse(TEXT("MaxRows+1 class names fails SeedDefaultRows"), bSeeded);
	TestTrue(TEXT("a rejected seed leaves Rows empty"), Seeded->Rows.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSurfaceCalibrationIndexOfTest,
	"Elysium.Substrate.SurfaceCalibration.IndexOf", GElysiumSurfaceKnobTestFlags)
bool FElysiumSurfaceCalibrationIndexOfTest::RunTest(const FString&)
{
	UElysiumSurfaceCalibration* Calibration = NewCalibration();
	Calibration->Rows = {
		MakeRow(TEXT("default"), 0, 0.6f, 0.5f, 0.0f),
		MakeRow(TEXT("Brick"), 1, 0.7f, 0.4f, 0.0f),
		MakeRow(TEXT("metal"), 2, 0.2f, 0.9f, 1.0f),
	};

	TestEqual(TEXT("exact match"), Calibration->IndexOf(FName(TEXT("metal"))), 2);
	TestEqual(TEXT("case-insensitive match"), Calibration->IndexOf(FName(TEXT("brick"))), 1);
	TestEqual(TEXT("case-insensitive match, other case"), Calibration->IndexOf(FName(TEXT("BRICK"))), 1);
	TestEqual(TEXT("no such class"), Calibration->IndexOf(FName(TEXT("nonexistent"))), INDEX_NONE);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
