// Content-free Substrate automation for UElysiumTextureProvenance: the record a baked texture
// carries from its `vtmb:texture:` unit.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumTextureProvenance.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#if WITH_EDITORONLY_DATA
#include "UObject/MetaData.h"
#endif

static constexpr EAutomationTestFlags GElysiumTextureProvenanceTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A sidecar in the shape the offline stage writes: every section present, so each parser
	// branch is exercised, with values that are not the struct defaults.
	const TCHAR* GSidecar = TEXT(R"json({
  "assetId": "vtmb:texture:hud/signs/notepad_yellow",
  "texturePath": "hud/signs/notepad_yellow",
  "assetPath": "/ElysiumBaked/Textures/hud/signs/T_notepad_yellow",
  "unitGlb": "textures/hud/signs/notepad_yellow.glb",
  "unitSchemaVersion": "1.1.0",
  "unitSha256": "unit-sha",
  "payloadSha256": "payload-sha",
  "settingsVersion": "elysium-texture-import-v1",
  "sourceFormat": "DXT5",
  "sourceFormatEnum": 15,
  "vkFormat": 137,
  "vkFormatName": "VK_FORMAT_BC3_UNORM_BLOCK",
  "vtfVersion": "7.1",
  "tthVersion": 1,
  "flags": 8448,
  "startFrame": 0,
  "bumpScale": 1.0,
  "reflectivity": [0.5187763571739197, 0.3833024799823761, 0.0762154683470726],
  "width": 512,
  "height": 1024,
  "frames": 1,
  "faces": 1,
  "mipCount": 11,
  "sourceMipCount": 11,
  "partialMipChain": false,
  "expandedFromRgb8": false,
  "sampling": {"pointSample": false, "trilinear": false, "clampS": false, "clampT": false,
               "anisotropic": false, "noMip": true, "noLod": false, "allMips": false},
  "members": [
    {"role": "tth", "path": "materials/hud/signs/notepad_yellow.tth",
     "originKind": "vpk", "container": "pack007.vpk", "offset": 123217637, "size": 316,
     "byteLength": 316, "sha256": "tth-sha"},
    {"role": "ttz", "path": "materials/hud/signs/notepad_yellow.ttz",
     "origin": {"kind": "loose", "root": "Unofficial_Patch"},
     "byteLength": 227689, "sha256": "ttz-sha"},
    {"role": "ttz", "path": "materials/hud/signs/notepad_yellow_hi.ttz",
     "originKind": "loose", "container": "Unofficial_Patch", "offset": 0, "size": 0,
     "byteLength": 4096, "sha256": "ttz-hi-sha"}
  ],
  "role": "colour",
  "roleEvidence": ["$basetexture", "$envmapmask"],
  "roleConflict": true,
  "twinOf": "",
  "faceMapping": [{"unrealFace": "+X", "ktxFace": 0, "sourceFace": 0, "transform": "rotate-ccw"}]
})json");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTextureProvenanceApplyJsonTest,
	"Elysium.Substrate.TextureProvenance.ApplyJson", GElysiumTextureProvenanceTestFlags)
bool FElysiumTextureProvenanceApplyJsonTest::RunTest(const FString&)
{
	UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);

	FString Error;
	UElysiumTextureProvenance* Record = UElysiumTextureProvenance::ApplyJson(Texture, GSidecar, Error);
	if (!Record)
	{
		AddError(FString::Printf(TEXT("ApplyJson rejected the sidecar: %s"), *Error));
		return false;
	}
	TestTrue(TEXT("no error on success"), Error.IsEmpty());
	TestTrue(TEXT("the record is outered to the texture"), Record->GetOuter() == Texture);

	// Identity and source format round-trip.
	TestEqual(TEXT("AssetId"), Record->AssetId, FString(TEXT("vtmb:texture:hud/signs/notepad_yellow")));
	TestEqual(TEXT("TexturePath"), Record->TexturePath, FString(TEXT("hud/signs/notepad_yellow")));
	TestEqual(TEXT("UnitSha256"), Record->UnitSha256, FString(TEXT("unit-sha")));
	TestEqual(TEXT("PayloadSha256"), Record->PayloadSha256, FString(TEXT("payload-sha")));
	TestEqual(TEXT("SettingsVersion"), Record->SettingsVersion, FString(TEXT("elysium-texture-import-v1")));
	TestEqual(TEXT("SourceFormat"), Record->SourceFormat, FString(TEXT("DXT5")));
	TestEqual(TEXT("SourceFormatEnum"), Record->SourceFormatEnum, 15);
	TestEqual(TEXT("VkFormat"), Record->VkFormat, 137);
	TestEqual(TEXT("VtfVersion"), Record->VtfVersion, FString(TEXT("7.1")));
	TestEqual(TEXT("TthVersion"), Record->TthVersion, 1);
	TestEqual(TEXT("Flags"), Record->Flags, 8448);
	TestTrue(TEXT("Reflectivity"), Record->Reflectivity.Equals(FVector3f(0.5187764f, 0.3833025f, 0.07621547f), 1e-5f));
	TestEqual(TEXT("BumpScale"), Record->BumpScale, 1.0f);

	// Extent and sampling.
	TestEqual(TEXT("Width"), Record->Width, 512);
	TestEqual(TEXT("Height"), Record->Height, 1024);
	TestEqual(TEXT("MipCount"), Record->MipCount, 11);
	TestTrue(TEXT("noMip decoded"), Record->Sampling.bNoMip);
	TestFalse(TEXT("clampS decoded"), Record->Sampling.bClampS);

	// Members carry both origin shapes: the stage's flat row, and the unit's nested `origin`. A
	// loose member's container span is empty in either shape, and its size is then the file's.
	if (TestEqual(TEXT("three members"), Record->Members.Num(), 3))
	{
		TestEqual(TEXT("tth kind (flat)"), Record->Members[0].OriginKind, FString(TEXT("vpk")));
		TestEqual(TEXT("tth container"), Record->Members[0].Container, FString(TEXT("pack007.vpk")));
		TestEqual(TEXT("tth offset"), Record->Members[0].Offset, int64(123217637));
		TestEqual(TEXT("tth size"), Record->Members[0].Size, int64(316));
		TestEqual(TEXT("loose kind (nested)"), Record->Members[1].OriginKind, FString(TEXT("loose")));
		TestEqual(TEXT("loose root as container"), Record->Members[1].Container, FString(TEXT("Unofficial_Patch")));
		TestEqual(TEXT("nested loose size falls back to byteLength"), Record->Members[1].Size, int64(227689));
		TestEqual(TEXT("loose kind (flat)"), Record->Members[2].OriginKind, FString(TEXT("loose")));
		TestEqual(TEXT("flat loose size 0 falls back to byteLength"), Record->Members[2].Size, int64(4096));
	}

	// Role.
	TestEqual(TEXT("Role"), Record->Role, FString(TEXT("colour")));
	TestTrue(TEXT("RoleConflict"), Record->RoleConflict);
	TestEqual(TEXT("RoleEvidence"), Record->RoleEvidence.Num(), 2);
	if (TestEqual(TEXT("one face row"), Record->FaceMapping.Num(), 1))
	{
		TestEqual(TEXT("face transform"), Record->FaceMapping[0].Transform, FString(TEXT("rotate-ccw")));
	}

	// Find returns the attached record.
	TestTrue(TEXT("Find"), UElysiumTextureProvenance::Find(Texture) == Record);

	// Applying again replaces rather than accumulates.
	UElysiumTextureProvenance* Second = UElysiumTextureProvenance::ApplyJson(Texture, TEXT("{\"assetId\": \"vtmb:texture:other\"}"), Error);
	if (TestNotNull(TEXT("second apply"), Second))
	{
		TestEqual(TEXT("second record read"), Second->AssetId, FString(TEXT("vtmb:texture:other")));
		TestEqual(TEXT("missing keys keep defaults"), Second->BumpScale, 1.0f);
		int32 Records = 0;
		if (const TArray<UAssetUserData*>* All = Texture->GetAssetUserDataArray())
		{
			for (UAssetUserData* Data : *All)
			{
				Records += Data && Data->IsA<UElysiumTextureProvenance>() ? 1 : 0;
			}
		}
		TestEqual(TEXT("exactly one provenance record after re-apply"), Records, 1);
		TestTrue(TEXT("Find follows the replacement"), UElysiumTextureProvenance::Find(Texture) == Second);
	}

	// A wrong-typed reflectivity element is tolerated: the record lands, the vector keeps its
	// default, and nothing is logged as an error.
	UElysiumTextureProvenance* Third = UElysiumTextureProvenance::ApplyJson(
		Texture, TEXT("{\"assetId\": \"vtmb:texture:third\", \"reflectivity\": [\"a\", 1, 2]}"), Error);
	if (TestNotNull(TEXT("third apply"), Third))
	{
		TestTrue(TEXT("wrong-typed reflectivity keeps the default"), Third->Reflectivity.Equals(FVector3f::ZeroVector));
	}

	// Malformed input, and well-formed JSON that is not an object, is refused with a reason and
	// leaves the texture as it was.
	const TCHAR* Rejected[] = { TEXT("not json"), TEXT("[]"), TEXT("5"), TEXT("\"x\""), TEXT("") };
	for (const TCHAR* Document : Rejected)
	{
		const FString Label = FString::Printf(TEXT("refused: %s"), Document);
		TestNull(*Label, UElysiumTextureProvenance::ApplyJson(Texture, Document, Error));
		TestFalse(*(Label + TEXT(" names a reason")), Error.IsEmpty());
		TestTrue(*(Label + TEXT(" left the record")), UElysiumTextureProvenance::Find(Texture) == Third);
	}
	TestNull(TEXT("null texture refused"), UElysiumTextureProvenance::ApplyJson(nullptr, GSidecar, Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTextureProvenanceRegistryTagsTest,
	"Elysium.Substrate.TextureProvenance.RegistryTags", GElysiumTextureProvenanceTestFlags)
bool FElysiumTextureProvenanceRegistryTagsTest::RunTest(const FString&)
{
	// A throwaway package: the stamp writes package metadata, and the transient package outlives
	// this test, so the keys must not land there.
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/ElysiumTextureProvenanceTest_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	Package->SetFlags(RF_Transient);
	UTexture2D* Texture = NewObject<UTexture2D>(Package, NAME_None, RF_Transient);
	FString Error;

	// Without a record there is nothing to publish.
	bool bStamped = true;
	UElysiumTextureProvenance::StampRegistryTags(Texture, bStamped, Error);
	TestFalse(TEXT("no record, no stamp"), bStamped);
	TestFalse(TEXT("no record names a reason"), Error.IsEmpty());

	UElysiumTextureProvenance::ApplyJson(Texture, GSidecar, Error);
#if WITH_EDITORONLY_DATA
	UElysiumTextureProvenance::StampRegistryTags(Texture, bStamped, Error);
	if (!bStamped)
	{
		AddError(FString::Printf(TEXT("StampRegistryTags failed: %s"), *Error));
		return false;
	}
	FMetaData& Meta = Texture->GetPackage()->GetMetaData();
	TestEqual(TEXT("ElysiumAssetId"), Meta.GetValue(Texture, UElysiumTextureProvenance::TagAssetId),
		FString(TEXT("vtmb:texture:hud/signs/notepad_yellow")));
	TestEqual(TEXT("ElysiumSourceFormat"), Meta.GetValue(Texture, UElysiumTextureProvenance::TagSourceFormat), FString(TEXT("DXT5")));
	TestEqual(TEXT("ElysiumRole"), Meta.GetValue(Texture, UElysiumTextureProvenance::TagRole), FString(TEXT("colour")));
	TestEqual(TEXT("ElysiumRoleConflict"), Meta.GetValue(Texture, UElysiumTextureProvenance::TagRoleConflict), FString(TEXT("1")));
	for (const FName& Tag : { UElysiumTextureProvenance::TagAssetId, UElysiumTextureProvenance::TagSourceFormat,
		UElysiumTextureProvenance::TagRole, UElysiumTextureProvenance::TagRoleConflict })
	{
		Meta.RemoveValue(Texture, Tag);
	}
#else
	UElysiumTextureProvenance::StampRegistryTags(Texture, bStamped, Error);
	TestFalse(TEXT("editor-only outside the editor"), bStamped);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTextureProvenanceBuiltSentinelsTest,
	"Elysium.Substrate.TextureProvenance.BuiltSentinels", GElysiumTextureProvenanceTestFlags)
bool FElysiumTextureProvenanceBuiltSentinelsTest::RunTest(const FString&)
{
	// A texture that was never built holds no platform data; every query answers with its
	// documented sentinel and the mip export refuses with a reason instead of guessing.
	UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);

	TestEqual(TEXT("no platform data: mip count is -1"), UElysiumTextureImportLibrary::BuiltMipCount(Texture), -1);
	TestTrue(TEXT("no platform data: format is empty"), UElysiumTextureImportLibrary::BuiltPixelFormat(Texture).IsEmpty());
	int32 Width = 7, Height = 7, Slices = 7, Mips = 7;
	UElysiumTextureImportLibrary::BuiltExtent(Texture, Width, Height, Slices, Mips);
	TestEqual(TEXT("no platform data: width 0"), Width, 0);
	TestEqual(TEXT("no platform data: height 0"), Height, 0);
	TestEqual(TEXT("no platform data: slices 0"), Slices, 0);
	TestEqual(TEXT("no platform data: mips 0"), Mips, 0);

	FString Error;
	const FString Path = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("ElysiumTextureProvenanceTest.built.dds"));
	bool bWritten = true;
	UElysiumTextureImportLibrary::WriteBuiltMipZeroAsDds(Texture, Path, bWritten, Error);
	TestFalse(TEXT("no platform data: export refused"), bWritten);
	TestFalse(TEXT("export refusal names a reason"), Error.IsEmpty());
	TestFalse(TEXT("export refusal wrote nothing"), IFileManager::Get().FileExists(*Path));

	// The null texture is the same answer.
	TestEqual(TEXT("null texture: mip count is -1"), UElysiumTextureImportLibrary::BuiltMipCount(nullptr), -1);
	bWritten = true;
	UElysiumTextureImportLibrary::WriteBuiltMipZeroAsDds(nullptr, Path, bWritten, Error);
	TestFalse(TEXT("null texture: export refused"), bWritten);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
