// Native queue coverage; these tests author no files and require no installed corpus.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumSkyProvenance.h"
#include "Engine/TextureCube.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkySourceConservationTest,
	"Elysium.Substrate.SkyComposite.SourceConservation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumSkySourceConservationTest::RunTest(const FString&)
{
	UPackage* Package = CreatePackage(TEXT("/ElysiumBaked/Textures/skybox/TC_elysiumsynthetic_Sky"));
	UTextureCube* Cube = NewObject<UTextureCube>(Package, TEXT("TC_elysiumsynthetic_Sky"), RF_Transient);
	// Mip-major, six slices at each mip. Values above one catch accidental LDR narrowing.
	TArray<FLinearColor> Pixels;
	Pixels.Init(FLinearColor(12.5f, 2.25f, 0.125f, 0.75f), (4 * 4 + 2 * 2 + 1) * 6);
	const uint8* Bytes = reinterpret_cast<const uint8*>(Pixels.GetData());
	Cube->Source.Init(4, 4, 6, 3, TSF_RGBA32F, Bytes);
	Cube->SRGB = false;
	Cube->CompressionSettings = TC_HDR_F32;
	TArray<FString> Hashes;
	int32 Offset = 0;
	for (int32 Size : {4, 2, 1})
	{
		const int32 Count = Size * Size * 6 * sizeof(FLinearColor);
		Hashes.Add(TEXT("\"") + FMD5::HashBytes(Bytes + Offset, Count) + TEXT("\""));
		Offset += Count;
	}
	const FString Json = FString::Printf(TEXT(R"({
		"schemaVersion":"1.0.0","product":"sky-composite","skyName":"elysiumsynthetic",
		"assetPath":"/ElysiumBaked/Textures/skybox/TC_elysiumsynthetic_Sky",
		"recipeSha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		"meanMethod":"mip0-upper-z-solid-angle-rec709-pow2.2-v1",
		"upperHemisphereMean":2.25,"width":4,"height":4,"faces":6,"mipCount":3,
		"sourceUnits":[{},{},{},{},{},{}],"faceMapping":[{},{},{},{},{},{}],
		"sourceMipMd5":[%s],"stagedFormat":"rgba32f","srgb":false,"compression":"hdr-f32"
	})"), *FString::Join(Hashes, TEXT(",")));
	FString Error;
	UElysiumSkyProvenance* Record = UElysiumSkyProvenance::ApplyJson(Cube, Json, Error);
	if (!TestNotNull(TEXT("all six float faces and three mips conserve"), Record))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("full provenance is retained"), Record->ProvenanceJson, Json);
	TestEqual(TEXT("stored mean is read without image decoding"), Record->UpperHemisphereMean, 2.25);
	TestEqual(TEXT("authored mip count is retained"), Record->MipCount, 3);
	TestTrue(TEXT("Find returns the attached record"), UElysiumSkyProvenance::Find(Cube) == Record);
	// Alter only the last (1x1) mip: checking only mip 0 or only five faces misses this.
	Pixels.Last().R += 1.0f;
	Cube->Source.Init(4, 4, 6, 3, TSF_RGBA32F, reinterpret_cast<const uint8*>(Pixels.GetData()));
	TestNull(TEXT("last face of last mip tampering is refused"), UElysiumSkyProvenance::ApplyJson(Cube, Json, Error));
	TestTrue(TEXT("failure names mip 2"), Error.Contains(TEXT("mip 2")));
	TestTrue(TEXT("failed validation preserves the previous record"), UElysiumSkyProvenance::Find(Cube) == Record);
	return true;
}
#endif
