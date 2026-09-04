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
#include "Materials/MaterialParameterCollectionInstance.h"
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

	// The ten named in the mechanics plan, plus the design doc's Overbright, MaskMetallicMax,
	// ChromaticTintStrength and ChromaThreshold (fourteen), plus ClassInfluence (the
	// Default*-vs-class-table lerp weight) -- fifteen settings scalars in all
	// (seam_map_material.md knob contract; owner review addendum 2026-08-31). R7.2 retired
	// DecalDepthOffset: a mesh decal is coplanar with its wall and has no bias to apply.
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
		TEXT("DefaultSpecular"), TEXT("DefaultRoughness"), TEXT("DefaultMetallic"), TEXT("ClassInfluence"),
		TEXT("LightSpecularScale"), TEXT("Overbright"), TEXT("MaskRoughnessMin"),
		TEXT("MaskRoughnessMax"), TEXT("MaskSpecularScale"), TEXT("MaskMetallicMax"),
		TEXT("EnvTintScale"), TEXT("FixedCubeStrength"), TEXT("ChromaticTintStrength"),
		TEXT("ChromaThreshold"), TEXT("CaptureRadius"),
		TEXT("DetailSwayAmplitude"),
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
	// A collection built in a throwaway /Temp/ package, never at the production CollectionPath():
	// PushToCollectionDefaults/PushToWorldInstances both take the collection as an explicit
	// parameter, so this test (a friend of UElysiumSurfaceSettings, see the header) calls them
	// directly rather than going through PushToCollection()'s hardcoded LoadCollection() lookup --
	// nothing here can shadow, or race with, a real load of `MPC_ElysiumSurfaces`.
	UPackage* Package = CreatePackage(
		*FString::Printf(TEXT("/Temp/ElysiumSurfaceSettingsCollectionPushTest_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	Package->SetFlags(RF_Transient);
	UMaterialParameterCollection* Collection = NewObject<UMaterialParameterCollection>(
		Package, TEXT("MPC_Test"), RF_Transient);
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

	// The cheap branch (PreEditChange/PostEditChange bracketing a same-size mutation) is what the
	// class comment above `PushToCollectionDefaults` in the header promises: this push only ever
	// changes a scalar's DefaultValue, never adds or removes a row, so the storage layout never
	// differs and ParameterCollection.cpp's PostEditChangeProperty must not regenerate `StateId`
	// (that only happens on an actual layout change). Pin it: capture StateId before the push and
	// assert it is unchanged after.
	const FGuid StateIdBeforePush = Collection->StateId;
	Settings->PushToCollectionDefaults(Collection);
	TestEqual(TEXT("PushToCollectionDefaults takes the cheap branch (StateId unchanged)"),
		Collection->StateId, StateIdBeforePush);

	bool bPassed = true;
	for (const TPair<FName, float UElysiumSurfaceSettings::*>& Binding : Bindings)
	{
		bool bFound = false;
		const float Value = Collection->GetScalarParameterDefaultValue(Binding.Key, bFound);
		if (!bFound)
		{
			AddError(FString::Printf(TEXT("collection has no row for %s after PushToCollectionDefaults"),
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

	// The world-instance half only has anything to assert with a live world to look an instance up
	// in -- gate that half of the check on one existing, rather than the whole test.
	if (!GEngine || GEngine->GetWorldContexts().Num() == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no world contexts to push a live collection instance into"));
	}
	else
	{
		Settings->PushToWorldInstances(Collection);
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World)
			{
				continue;
			}
			UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection);
			if (!Instance)
			{
				continue;
			}
			for (const TPair<FName, float UElysiumSurfaceSettings::*>& Binding : Bindings)
			{
				float Value = 0.0f;
				if (!Instance->GetScalarParameterValue(Binding.Key, Value))
				{
					AddError(FString::Printf(TEXT("world instance has no row for %s"), *Binding.Key.ToString()));
					bPassed = false;
					continue;
				}
				if (!FMath::IsNearlyEqual(Value, Settings->*Binding.Value))
				{
					AddError(FString::Printf(TEXT("%s: world instance value %f != settings value %f"),
						*Binding.Key.ToString(), Value, Settings->*Binding.Value));
					bPassed = false;
				}
			}
		}
	}

	// Out of the way of any later test: a /Temp/ package with a GUID-suffixed name never collides,
	// but garbage it promptly regardless.
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
	TestEqual(FString::Printf(TEXT("Lut is %d wide"), UElysiumSurfaceCalibration::MaxRows),
		Calibration->Lut->Source.GetSizeX(), int64(UElysiumSurfaceCalibration::MaxRows));
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

	bool bOkA = false;
	FString ErrorA;
	InOrder->RegenerateLut(bOkA, ErrorA);
	if (!bOkA || !InOrder->Lut)
	{
		AddError(FString::Printf(TEXT("RegenerateLut (in-order) failed: %s"), *ErrorA));
		return false;
	}

	// Copy the first LUT's texels out to a plain buffer right after this regenerate, before the
	// second calibration's RegenerateLut runs -- the comparison then never has two textures'
	// FTextureSource mips locked at once, and never depends on InOrder's Source memory staying
	// valid (or unchanged) across an unrelated object's own regenerate.
	TArray<uint8> InOrderTexels;
	{
		const uint8* Locked = InOrder->Lut->Source.LockMipReadOnly(0);
		if (!Locked)
		{
			AddError(TEXT("LockMipReadOnly(0) on the in-order LUT returned null"));
			return false;
		}
		InOrderTexels.Append(Locked, UElysiumSurfaceCalibration::MaxRows * 4);
		InOrder->Lut->Source.UnlockMip(0);
	}

	UElysiumSurfaceCalibration* Reordered = NewCalibration();
	Reordered->Rows = {
		MakeRow(TEXT("metal"), 2, 0.2f, 0.9f, 1.0f),
		MakeRow(TEXT("default"), 0, 0.6f, 0.5f, 0.0f),
		MakeRow(TEXT("brick"), 1, 0.7f, 0.4f, 0.0f),
	};
	bool bOkB = false;
	FString ErrorB;
	Reordered->RegenerateLut(bOkB, ErrorB);
	if (!bOkB || !Reordered->Lut)
	{
		AddError(FString::Printf(TEXT("RegenerateLut (reordered) failed: %s"), *ErrorB));
		return false;
	}

	const uint8* ReorderedTexels = Reordered->Lut->Source.LockMipReadOnly(0);
	if (!ReorderedTexels)
	{
		AddError(TEXT("LockMipReadOnly(0) on the reordered LUT returned null"));
		return false;
	}
	const bool bIdentical = FMemory::Memcmp(
		InOrderTexels.GetData(), ReorderedTexels, UElysiumSurfaceCalibration::MaxRows * 4) == 0;
	TestTrue(TEXT("reordering Rows does not change the baked LUT"), bIdentical);
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
	int32 Added = 0, Kept = 0;
	UElysiumSurfaceCalibration::SeedDefaultRows(Seeded, TooManyNames, bSeeded, SeedError, Added, Kept);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSurfaceCalibrationSeedRejectsDuplicateIndexTest,
	"Elysium.Substrate.SurfaceCalibration.SeedRejectsDuplicateIndex", GElysiumSurfaceKnobTestFlags)
bool FElysiumSurfaceCalibrationSeedRejectsDuplicateIndexTest::RunTest(const FString&)
{
	// SeedDefaultRows itself never produces a duplicate Index (ClaimNextFreeIndex always walks
	// past a claimed one) -- the duplicate-Index guard lives in RegenerateLut, reached when rows
	// are hand-edited in the details panel rather than seeded.
	UElysiumSurfaceCalibration* Calibration = NewCalibration();
	Calibration->Rows = {
		MakeRow(TEXT("brick"), 0, 0.6f, 0.5f, 0.0f),
		MakeRow(TEXT("metal"), 0, 0.2f, 0.9f, 1.0f), // same Index as "brick"
	};
	bool bOk = true;
	FString Error;
	Calibration->RegenerateLut(bOk, Error);
	TestFalse(TEXT("a duplicate Index fails RegenerateLut"), bOk);
	TestFalse(TEXT("failure carries a reason"), Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSurfaceCalibrationSeedRejectsDuplicateNameTest,
	"Elysium.Substrate.SurfaceCalibration.SeedRejectsDuplicateName", GElysiumSurfaceKnobTestFlags)
bool FElysiumSurfaceCalibrationSeedRejectsDuplicateNameTest::RunTest(const FString&)
{
	UElysiumSurfaceCalibration* Calibration = NewCalibration();
	Calibration->Rows = {
		MakeRow(TEXT("brick"), 0, 0.6f, 0.5f, 0.0f),
		MakeRow(TEXT("brick"), 1, 0.2f, 0.9f, 1.0f), // same Name as row 0
	};
	bool bOk = true;
	FString Error;
	Calibration->RegenerateLut(bOk, Error);
	TestFalse(TEXT("a duplicate Name fails RegenerateLut"), bOk);
	TestFalse(TEXT("failure carries a reason"), Error.IsEmpty());

	// SeedDefaultRows rejects a duplicate name in ClassNames up front, the same way.
	UElysiumSurfaceCalibration* Seeded = NewCalibration();
	bool bSeeded = true;
	FString SeedError;
	int32 Added = 0, Kept = 0;
	UElysiumSurfaceCalibration::SeedDefaultRows(Seeded, {TEXT("brick"), TEXT("brick")}, bSeeded, SeedError, Added, Kept);
	TestFalse(TEXT("a duplicate class name fails SeedDefaultRows"), bSeeded);
	TestTrue(TEXT("a rejected seed leaves Rows empty"), Seeded->Rows.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSurfaceCalibrationSeedRejectsOutOfRangeIndexTest,
	"Elysium.Substrate.SurfaceCalibration.SeedRejectsOutOfRangeIndex", GElysiumSurfaceKnobTestFlags)
bool FElysiumSurfaceCalibrationSeedRejectsOutOfRangeIndexTest::RunTest(const FString&)
{
	UElysiumSurfaceCalibration* Calibration = NewCalibration();
	Calibration->Rows = {
		MakeRow(TEXT("brick"), UElysiumSurfaceCalibration::MaxRows, 0.6f, 0.5f, 0.0f), // one past [0, MaxRows)
	};
	bool bOk = true;
	FString Error;
	Calibration->RegenerateLut(bOk, Error);
	TestFalse(TEXT("an out-of-range Index fails RegenerateLut"), bOk);
	TestFalse(TEXT("failure carries a reason"), Error.IsEmpty());

	UElysiumSurfaceCalibration* NegativeIndex = NewCalibration();
	NegativeIndex->Rows = {MakeRow(TEXT("brick"), -1, 0.6f, 0.5f, 0.0f)};
	bool bOkNegative = true;
	FString ErrorNegative;
	NegativeIndex->RegenerateLut(bOkNegative, ErrorNegative);
	TestFalse(TEXT("a negative Index fails RegenerateLut"), bOkNegative);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSurfaceCalibrationSeedIsNonDestructiveTest,
	"Elysium.Substrate.SurfaceCalibration.SeedIsNonDestructive", GElysiumSurfaceKnobTestFlags)
bool FElysiumSurfaceCalibrationSeedIsNonDestructiveTest::RunTest(const FString&)
{
	// C1: seed, tune a value by hand (as if from Project Settings / the editor grid), re-seed with
	// an extended class list -- the tuned value and its Index must survive verbatim, and only the
	// unseen name gets a new row.
	UElysiumSurfaceCalibration* Calibration = NewCalibration();
	bool bOk = false;
	FString Error;
	int32 Added = 0, Kept = 0;
	UElysiumSurfaceCalibration::SeedDefaultRows(
		Calibration, {TEXT("default"), TEXT("brick"), TEXT("metal")}, bOk, Error, Added, Kept);
	TestTrue(TEXT("first seed succeeds"), bOk);
	TestEqual(TEXT("first seed adds three rows"), Added, 3);
	TestEqual(TEXT("first seed keeps none (nothing existed yet)"), Kept, 0);
	TestEqual(TEXT("three rows after the first seed"), Calibration->Rows.Num(), 3);

	// Tune "brick" by hand, exactly as a human would in the details-panel grid.
	const int32 BrickRowIndex = Calibration->Rows.IndexOfByPredicate(
		[](const FElysiumSurfaceClassRow& Row) { return Row.Name == FName(TEXT("brick")); });
	if (!TestTrue(TEXT("brick row exists after the first seed"), BrickRowIndex != INDEX_NONE))
	{
		return false;
	}
	Calibration->Rows[BrickRowIndex].Roughness = 0.123f;
	Calibration->Rows[BrickRowIndex].Specular = 0.456f;
	Calibration->Rows[BrickRowIndex].Metallic = 0.789f;
	const int32 TunedBrickIndex = Calibration->Rows[BrickRowIndex].Index;

	// Re-seed with an extended list: "brick"/"default"/"metal" already exist, "wood" is new.
	bOk = false;
	Error.Reset();
	Added = 0;
	Kept = 0;
	UElysiumSurfaceCalibration::SeedDefaultRows(
		Calibration, {TEXT("default"), TEXT("brick"), TEXT("metal"), TEXT("wood")}, bOk, Error, Added, Kept);
	TestTrue(TEXT("re-seed succeeds"), bOk);
	TestEqual(TEXT("re-seed adds only the new name"), Added, 1);
	TestEqual(TEXT("re-seed keeps the three existing names"), Kept, 3);
	TestEqual(TEXT("four rows after the re-seed"), Calibration->Rows.Num(), 4);

	const int32 BrickRowIndexAfter = Calibration->Rows.IndexOfByPredicate(
		[](const FElysiumSurfaceClassRow& Row) { return Row.Name == FName(TEXT("brick")); });
	if (TestTrue(TEXT("brick row still exists after the re-seed"), BrickRowIndexAfter != INDEX_NONE))
	{
		const FElysiumSurfaceClassRow& BrickRow = Calibration->Rows[BrickRowIndexAfter];
		TestEqual(TEXT("brick's tuned Roughness survives"), BrickRow.Roughness, 0.123f);
		TestEqual(TEXT("brick's tuned Specular survives"), BrickRow.Specular, 0.456f);
		TestEqual(TEXT("brick's tuned Metallic survives"), BrickRow.Metallic, 0.789f);
		TestEqual(TEXT("brick's Index is unchanged"), BrickRow.Index, TunedBrickIndex);
	}

	TestTrue(TEXT("wood was appended"), Calibration->IndexOf(FName(TEXT("wood"))) != INDEX_NONE);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
