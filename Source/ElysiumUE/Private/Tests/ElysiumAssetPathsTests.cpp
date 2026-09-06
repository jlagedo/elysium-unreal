#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ElysiumContentPaths.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBakedUnitPathsTest,
	"Elysium.Substrate.ModelNames.BakedUnit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumBakedUnitPathsTest::RunTest(const FString&)
{
	FString Text;
	const FString File = FPaths::ProjectDir() / TEXT("pipeline/tests/fixtures/baked_paths.json");
	if (!TestTrue(TEXT("golden fixture exists"), FFileHelper::LoadFileToString(Text, *File))) return false;
	TArray<TSharedPtr<FJsonValue>> Rows;
	if (!TestTrue(TEXT("golden fixture parses"),
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Rows))) return false;
	for (const auto& Value : Rows)
	{
		const auto Row = Value->AsObject();
		const FString Id = Row->GetStringField(TEXT("id"));
		FString Role, Label, Expected;
		Row->TryGetStringField(TEXT("role"), Role);
		Row->TryGetStringField(TEXT("label"), Label);
		if (Row->TryGetStringField(TEXT("path"), Expected))
		{
			Expected += TEXT(".") + FPaths::GetCleanFilename(Expected);
		}
		TestEqual(*Id, FElysiumContentPaths::BakedUnit(
			Id, Row->GetStringField(TEXT("prefix")), Role, Label), Expected);
	}
	const FString Prop = FElysiumContentPaths::BakedPropMesh(
		FElysiumContentPaths::PropModelStem(TEXT("models/scenery/signs/cliff-danger.mdl")), TEXT("synthetic"));
	TestTrue(TEXT("legacy static address uses the same legal object-name fold as the bake"),
		Prop.EndsWith(TEXT("/SM_models_scenery_signs_cliff_danger.SM_models_scenery_signs_cliff_danger")));
	TestEqual(TEXT("existing map package root remains in place until R9"), FElysiumContentPaths::BakedMapDir(TEXT("sm_hub_1")),
		FString(TEXT("/ElysiumBaked/sm_hub_1")));
	TestEqual(TEXT("entity table lives beside the existing level"), FElysiumContentPaths::BakedMapEntities(TEXT("sm_hub_1")),
		FString(TEXT("/ElysiumBaked/sm_hub_1/DA_sm_hub_1_Entities.DA_sm_hub_1_Entities")));
	TestEqual(TEXT("level helper keeps package spelling"), FElysiumContentPaths::BakedLevel(TEXT("sm_hub_1")),
		FString(TEXT("/ElysiumBaked/sm_hub_1/sm_hub_1")));
	return true;
}
#endif
