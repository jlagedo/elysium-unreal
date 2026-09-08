// The C++ half of the model-name twin: `FElysiumContentPaths::PropModelStem` walked over the same golden table
// `pipeline/tests/test_model_import_editor.py` walks with `shared_corpus.static_stem`. Neither
// implementation may change without the other failing, which is the point -- the stem is what the
// baked `SM_` asset is named after, and the four substrate call sites that recompute it live
// (`ElysiumItemContainer`, `ElysiumItemClasses`, `ElysiumLockable`, `ElysiumTerminal`) would each
// resolve a different asset if the two folds drifted.
//
// The table is a tracked repository fixture, not generated content, so it is read from
// `FPaths::ProjectDir()`; a checkout without it abstains rather than failing.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumModelNamesTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelNamesPropModelStemTest,
	"Elysium.Substrate.ModelNames.PropModelStem", GElysiumModelNamesTestFlags)
bool FElysiumModelNamesPropModelStemTest::RunTest(const FString&)
{
	const FString FixturePath =
		FPaths::ProjectDir() / TEXT("pipeline/tests/fixtures/model_names.json");

	FString Body;
	if (!FFileHelper::LoadFileToString(Body, *FixturePath))
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: the model-name golden table is not in this checkout (%s)"),
			*FixturePath));
		return true;
	}

	TSharedPtr<FJsonValue> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
	{
		AddError(FString::Printf(TEXT("%s does not parse as JSON: %s"),
			*FixturePath, *Reader->GetErrorMessage()));
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Parsed->TryGetArray(Rows) || !Rows)
	{
		AddError(FString::Printf(TEXT("%s is not a JSON array"), *FixturePath));
		return false;
	}
	if (Rows->Num() == 0)
	{
		AddError(TEXT("the model-name golden table is empty"));
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Rows)
	{
		const TSharedPtr<FJsonObject>* RowPtr = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(RowPtr) || !RowPtr)
		{
			AddError(TEXT("a row of the model-name golden table is not an object"));
			continue;
		}
		const TSharedRef<FJsonObject> Row = (*RowPtr).ToSharedRef();
		FString ModelPath;
		FString ExpectedStem;
		if (!Row->TryGetStringField(TEXT("modelPath"), ModelPath)
			|| !Row->TryGetStringField(TEXT("stem"), ExpectedStem))
		{
			AddError(TEXT("a row of the model-name golden table lacks modelPath or stem"));
			continue;
		}
		// The whole path folded, `models/` included -- not the base filename.
		TestEqual(*FString::Printf(TEXT("PropModelStem(%s)"), *ModelPath),
			FElysiumContentPaths::PropModelStem(ModelPath), ExpectedStem);
	}

	// The fold keeps `.` and `-` and does not collapse runs, which is exactly where it differs
	// from `BakedAssetName`; a row in the table proves it, and this states it directly so the
	// difference cannot be "fixed" by swapping the two functions.
	TestEqual(TEXT("PropModelStem keeps `-` and `.` and does not collapse runs"),
		FElysiumContentPaths::PropModelStem(TEXT("models/scenery/signs/cliff-danger.mdl")),
		FString(TEXT("models_scenery_signs_cliff-danger")));
	TestEqual(TEXT("PropModelStem folds one underscore per illegal character"),
		FElysiumContentPaths::PropModelStem(TEXT("models/A B/C  D.mdl")),
		FString(TEXT("models_a_b_c__d")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
