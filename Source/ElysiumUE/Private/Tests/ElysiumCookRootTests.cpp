#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumCookRoot.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString CookRootFixture()
	{
		auto Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("schemaVersion"), TEXT("1.0.0"));
		Object->SetStringField(TEXT("producer"), TEXT("r8-cook-roots"));
		Object->SetStringField(TEXT("primaryAssetType"), TEXT("ElysiumR8CookRoot"));
		Object->SetStringField(TEXT("assetPath"), UElysiumCookRoot::PackagePath());
		Object->SetStringField(TEXT("physicsScope"), TEXT("export-import-data-conservation"));
		Object->SetBoolField(TEXT("readyToPublish"), true);
		Object->SetStringField(TEXT("sourceEvidenceJson"), TEXT("{\"sourceOnly\":[\"w_null\"]}"));
		Object->SetStringField(TEXT("inputDigest"), FString::ChrN(64, TEXT('a')));
		Object->SetStringField(TEXT("inventoryDigest"), FString::ChrN(64, TEXT('b')));
		Object->SetArrayField(TEXT("issues"), {});
		TArray<TSharedPtr<FJsonValue>> Targets;
		for (const TCHAR* Package : {TEXT("/ElysiumBaked/Models/_Corpus/DA_Cast"),
			TEXT("/ElysiumBaked/ExpressionTables/_Corpus/DA_ExpressionTables"),
			TEXT("/ElysiumBaked/Models/_Corpus/DA_WieldModels"), TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels"),
			TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins"),
			TEXT("/ElysiumBaked/Models/_Corpus/DA_OrnamentModels"), TEXT("/ElysiumBaked/Models/unused/A_never_selected")})
		{
			FString Name(Package); int32 Slash = INDEX_NONE; Name.FindLastChar(TEXT('/'), Slash);
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("packagePath"), Package);
			Row->SetStringField(TEXT("objectPath"), FString(Package) + TEXT(".") + Name.Mid(Slash + 1));
			Targets.Add(MakeShared<FJsonValueObject>(Row));
		}
		Object->SetArrayField(TEXT("targets"), Targets);
		FString Json; FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Json)); return Json;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCookRootTest, "Elysium.Substrate.CookRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumCookRootTest::RunTest(const FString&)
{
	auto* Label = NewObject<UElysiumCookRoot>(); FString Error;
	const FString Json = CookRootFixture();
	TestEqual(TEXT("canonical administrative package"), UElysiumCookRoot::PackagePath(), FString(TEXT("/ElysiumBaked/Models/_Corpus/DA_R8CookRoot")));
	TestEqual(TEXT("dedicated packaging primary type"), Label->GetPrimaryAssetId().PrimaryAssetType.ToString(), FString(TEXT("ElysiumR8CookRoot")));
	if (!TestNotNull(TEXT("label accepts all explicit roots without loading them"), UElysiumCookRoot::ApplyJson(Label, Json, Error)))
	{ AddError(Error); return false; }
	TestEqual(TEXT("six globals plus unused published model"), Label->ExplicitAssets.Num(), 7);
	TestTrue(TEXT("administrative label stays editor only"), Label->IsEditorOnly());
	TestFalse(TEXT("no directory-wide cook policy"), bool(Label->bLabelAssetsInMyDirectory));
	TestFalse(TEXT("no redirector admission"), bool(Label->bIncludeRedirectors));
	TestTrue(TEXT("dependent native materials/textures can follow roots"), Label->Rules.bApplyRecursively);
	TestEqual(TEXT("explicit targets always cook"), Label->Rules.CookRule, EPrimaryAssetCookRule::AlwaysCook);
	TestEqual(TEXT("field verification"), UElysiumCookRoot::Verify(Label, Json), FString());
	Label->ExplicitAssets.Pop();
	TestFalse(TEXT("unused asset loss is diagnosed"), UElysiumCookRoot::Verify(Label, Json).IsEmpty());
	if (!UElysiumCookRoot::ApplyJson(Label, Json, Error)) { AddError(Error); return false; }
	const FString Invalid = Json.Replace(TEXT("export-import-data-conservation"), TEXT("simulation-ready"));
	TestNull(TEXT("simulation readiness is outside this R8 scope"), UElysiumCookRoot::ApplyJson(Label, Invalid, Error));
	TestEqual(TEXT("rejection leaves source inventory untouched"), UElysiumCookRoot::Verify(Label, Json), FString());
	return true;
}
#endif
