#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumExpressionData.h"
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumExpressionTable.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* SourceDocument = TEXT(R"JSON({"asset":{"version":"2.0"},"extensions":{"ELYSIUM_vtmb_expression_table":{
	 "schemaVersion":"1.0.0","identity":{"asset":"vtmb:expression-table:test","stem":"test","sourceKind":"vfe+txt","runtimeLoadable":true},
	 "coverage":{"unsupported":[],"typedUnidentified":[{"hex":"00abff","unused":7}],"unresolved":[]},
	 "vfe":{"numFlexSettings":2,"keys":["Zed","alpha"],"settings":[{"index":0,"type":"normal"},{"index":1,"type":"normal"}]},
	 "table":{"keys":["Zed","alpha"],"hasWeighting":true,"rows":[
	  {"index":0,"name":" Anger ","class":"0x0279","phonemeCode":633,"description":"compiled","values":[0.25,-2.0],"weights":[0.5,0.0]},
	  {"index":1,"name":"other","class":"0x0279","phonemeCode":633,"description":"duplicate code","values":[1.0,0.5],"weights":[2.0,1.0]}]},
	 "authoring":{"keys":["alpha"],"hasWeighting":false,"rows":[
	  {"index":0,"name":"Author","class":"_","phonemeCode":null,"description":"author only","values":[0.123456789],"weights":[]}]},
	 "futureUnused":{"sentinel":19},"omissions":[]}}})JSON");

	FString Stage(const FString& Document, const FString& Stem = TEXT("test"))
	{
		auto Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("schemaVersion"), TEXT("1.0.0"));
		Object->SetStringField(TEXT("assetId"), TEXT("vtmb:expression-table:") + Stem);
		Object->SetStringField(TEXT("assetPath"), TEXT("/ElysiumBaked/ExpressionTables/DA_") + Stem);
		Object->SetStringField(TEXT("sourceDocumentJson"), Document);
		Object->SetStringField(TEXT("sourceGlbSha256"), FString::ChrN(64, TEXT('a')));
		Object->SetStringField(TEXT("runtimeStatus"), TEXT("ready"));
		FString Result;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Result));
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumExpressionAdamStageTest, "Elysium.Substrate.ExpressionData.AdamStage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumExpressionAdamStageTest::RunTest(const FString&)
{
	// Reduced fixture: actual adam_expressions Joy row (source row 1), retaining all
	// 32 controller columns and exact published decimal tokens. Source GLB SHA-256:
	// 22a1d8fd664fce02bd1f0b8f90d494263983df198340513b54c52bbdfb995f9b.
	const FString Document = TEXT(R"JSON({"asset":{"version":"2.0"},"extensions":{"ELYSIUM_vtmb_expression_table":{
	 "schemaVersion":"1.0.0","identity":{"asset":"vtmb:expression-table:adam_expressions","stem":"adam_expressions","sourceKind":"vfe+txt","runtimeLoadable":true},
	 "coverage":{"unsupported":[]},
	 "vfe":{"numFlexSettings":1,"keys":["right_lid_raiser","left_lid_raiser","right_lid_tightener","left_lid_tightener","right_lid_droop","left_lid_droop","blink","right_inner_raiser","left_inner_raiser","right_outer_raiser","left_outer_raiser","right_lowerer","left_lowerer","right_cheek_raiser","left_cheek_raiser","wrinkler","right_upper_raiser","left_upper_raiser","right_corner_puller","left_corner_puller","corner_depressor","chin_raiser","right_funneler","left_funneler","right_stretcher","left_stretcher","tightener","smile","right_sneer","left_sneer","half_closed","lower_lip"],"settings":[{"index":1,"type":"normal"}]},
	 "table":{"keys":["right_lid_raiser","left_lid_raiser","right_lid_tightener","left_lid_tightener","right_lid_droop","left_lid_droop","blink","right_inner_raiser","left_inner_raiser","right_outer_raiser","left_outer_raiser","right_lowerer","left_lowerer","right_cheek_raiser","left_cheek_raiser","wrinkler","right_upper_raiser","left_upper_raiser","right_corner_puller","left_corner_puller","corner_depressor","chin_raiser","right_funneler","left_funneler","right_stretcher","left_stretcher","tightener","smile","right_sneer","left_sneer","half_closed","lower_lip"],"hasWeighting":true,"rows":[
	 {"class":"_","description":"","index":1,"name":"Joy","phonemeCode":null,"values":[0.0,0.0,0.38999998569488525,0.38999998569488525,0.15000000596046448,0.15000000596046448,0.09000000357627869,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.23999999463558197,0.23999999463558197,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.1899999976158142,0.0,0.0,0.0,0.0],"weights":[0.0,0.0,1.0,1.0,1.0,1.0,1.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,1.0,1.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0]}]},
	 "authoring":{"keys":["right_lid_tightener"],"hasWeighting":true,"rows":[{"index":1,"name":"Joy","class":"_","phonemeCode":null,"description":"Debauched Smile","values":[0.39],"weights":[1.0]}]}}}})JSON");
	const FString Json = Stage(Document, TEXT("adam_expressions"));
	auto* Data = NewObject<UElysiumExpressionData>(); FString Error;
	if (!TestNotNull(TEXT("real staged package path imports"), UElysiumExpressionData::ApplyJson(Data, Json, Error)))
	{ AddError(Error); return false; }
	TestEqual(TEXT("32 ordered controllers retained"), Data->Table.Keys.Num(), 32);
	TestEqual(TEXT("source row index preserved"), Data->Table.Rows[0].Index, 1);
	TestEqual(TEXT("compiled 0.39 remains exact float32"), Data->Table.Rows[0].Values[2], double(.39f));
	TestEqual(TEXT("authoring 0.39 remains a distinct double"), Data->Authoring.Rows[0].Values[0], .39);
	TestEqual(TEXT("source document retained verbatim"), Data->SourceDocumentJson, Document);
	const FString WrongPath = Json.Replace(TEXT("/ElysiumBaked/ExpressionTables/DA_adam_expressions"),
		TEXT("/ElysiumBaked/ExpressionTables/DA_adam_expressions.WrongObject"));
	TestNull(TEXT("object/package confusion is rejected"), UElysiumExpressionData::ApplyJson(Data, WrongPath, Error));
	TestTrue(TEXT("path error names field and expected package"), Error.Contains(TEXT("stage.assetPath")) && Error.Contains(TEXT("expected package")));
	const FString Inexact = Document.Replace(TEXT("0.38999998569488525"), TEXT("0.39"));
	TestNull(TEXT("authoring rounding never masks compiled data loss"), UElysiumExpressionData::ApplyJson(Data, Stage(Inexact, TEXT("adam_expressions")), Error));
	TestTrue(TEXT("numeric failure locates exact source column"), Error.Contains(TEXT("table.rows[0].values[2]"))
		&& Error.Contains(TEXT("right_lid_tightener")) && Error.Contains(TEXT("not exact float32")));
	TestEqual(TEXT("failed mutation leaves prior native data intact"), UElysiumExpressionData::Verify(Data, Json), FString());
	const FString MissingName = Document.Replace(TEXT("\"name\":\"Joy\""), TEXT("\"missingName\":\"Joy\""));
	TestNull(TEXT("missing row field rejected"), UElysiumExpressionData::ApplyJson(Data, Stage(MissingName, TEXT("adam_expressions")), Error));
	TestTrue(TEXT("missing-field diagnostic is specific"), Error.Contains(TEXT("table.rows[0].name")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumExpressionCorpusPackageTest, "Elysium.Substrate.ExpressionData.CorpusPackagePaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumExpressionCorpusPackageTest::RunTest(const FString&)
{
	// In-memory package only; no save/import or generated files. LoadObject must find
	// this object after validation of the staged package spelling.
	const FString PackagePath = TEXT("/ElysiumBaked/ExpressionTables/DA_expression_corpus_fixture");
	auto* Package = CreatePackage(*PackagePath);
	auto* Data = NewObject<UElysiumExpressionData>(Package, TEXT("DA_expression_corpus_fixture"));
	Data->AssetId = TEXT("vtmb:expression-table:expression_corpus_fixture");
	auto* Corpus = NewObject<UElysiumExpressionTables>(); FString Error;
	const FString Json = TEXT(R"JSON({"schemaVersion":"1.0.0","assetPath":"/ElysiumBaked/ExpressionTables/_Corpus/DA_ExpressionTables","tables":{"vtmb:expression-table:expression_corpus_fixture":"/ElysiumBaked/ExpressionTables/DA_expression_corpus_fixture"}})JSON");
	if (!TestNotNull(TEXT("corpus accepts staged package and loads canonical object"), UElysiumExpressionTables::ApplyJson(Corpus, Json, Error)))
	{ AddError(Error); return false; }
	TestTrue(TEXT("hard reference retained"), Corpus->Tables.FindRef(Data->AssetId) == Data);
	const FString Wrong = Json.Replace(TEXT("DA_expression_corpus_fixture\""), TEXT("DA_expression_corpus_fixture.Wrong\""));
	TestNull(TEXT("wrong corpus product rejected"), UElysiumExpressionTables::ApplyJson(Corpus, Wrong, Error));
	TestTrue(TEXT("corpus path error identifies unit"), Error.Contains(TEXT("corpus.tables[vtmb:expression-table:expression_corpus_fixture]"))
		&& Error.Contains(TEXT("expected package")));
	TestEqual(TEXT("corpus rejection preserves prior references"), UElysiumExpressionTables::Verify(Corpus, Json), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumExpressionDataRetentionTest, "Elysium.Substrate.ExpressionData.Retention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumExpressionDataRetentionTest::RunTest(const FString&)
{
	auto* Data = NewObject<UElysiumExpressionData>(); FString Error;
	const FString Json = Stage(SourceDocument);
	if (!TestNotNull(TEXT("imports typed table"), UElysiumExpressionData::ApplyJson(Data, Json, Error))) { AddError(Error); return false; }
	TestEqual(TEXT("controller order/case"), Data->Table.Keys[0], FString(TEXT("Zed")));
	TestEqual(TEXT("row spelling"), Data->Table.Rows[0].Name, FString(TEXT(" Anger ")));
	TestEqual(TEXT("trimmed lookup"), Data->Table.FindRow(TEXT("anger")), 0);
	TestEqual(TEXT("first duplicate phoneme wins"), Data->Table.FindRowByPhonemeCode(633), 0);
	TestEqual(TEXT("null author code never resolves"), Data->Authoring.FindRowByPhonemeCode(INDEX_NONE), INDEX_NONE);
	TestEqual(TEXT("authoring decimal not rounded to float"), Data->Authoring.Rows[0].Values[0], .123456789);
	TestTrue(TEXT("unweighted authored vector remains empty"), Data->Authoring.Rows[0].Weights.IsEmpty());
	const auto View = Data->PrepareLegacyView(Error);
	if (!TestTrue(TEXT("resident compatibility view"), View.IsValid())) return false;
	TestEqual(TEXT("no clamp or weight premultiplication"), View->Rows[0].Values[1], -2.f);
	TestEqual(TEXT("zero influence retained"), View->Rows[0].Weights[1], 0.f);
	TestEqual(TEXT("weight above one retained"), View->Rows[1].Weights[0], 2.f);
	TestEqual(TEXT("compatibility lookup by code"), View->FindRowByPhonemeCode(633), 0);

	TArray<uint8> Bytes;
	FObjectWriter Writer(Data, Bytes);
	auto* Restored = NewObject<UElysiumExpressionData>();
	FObjectReader Reader(Restored, Bytes);
	TestEqual(TEXT("all reflected fields survive serialization"), UElysiumExpressionData::Verify(Restored, Json), FString());
	const FProperty* Evidence = FindFProperty<FProperty>(UElysiumExpressionData::StaticClass(), TEXT("SourceDocumentJson"));
	TestTrue(TEXT("opaque/source evidence survives cook stripping"), Evidence && !Evidence->HasAnyPropertyFlags(CPF_EditorOnly));
	Restored->Table.Rows[0].Values[0] = .5;
	TestFalse(TEXT("detect value mutation"), UElysiumExpressionData::Verify(Restored, Json).IsEmpty());
	Restored->Table = Data->Table; Restored->Table.Rows.Swap(0, 1);
	TestFalse(TEXT("detect row-order mutation"), UElysiumExpressionData::Verify(Restored, Json).IsEmpty());
	Restored->Table = Data->Table; Restored->Table.Keys.Swap(0, 1);
	TestFalse(TEXT("detect controller-order mutation"), UElysiumExpressionData::Verify(Restored, Json).IsEmpty());
	Restored->Table = Data->Table; Restored->Table.Rows[0].Weights[1] = 1.;
	TestFalse(TEXT("detect unused weight mutation"), UElysiumExpressionData::Verify(Restored, Json).IsEmpty());
	Restored->Table = Data->Table; Restored->SourceDocumentJson += TEXT(" ");
	TestFalse(TEXT("detect evidence mutation"), UElysiumExpressionData::Verify(Restored, Json).IsEmpty());
	const FString Malformed = FString(SourceDocument).Replace(TEXT("[0.25,-2.0]"), TEXT("[0.25]"));
	TestNull(TEXT("misaligned import rejected"), UElysiumExpressionData::ApplyJson(Data, Stage(Malformed), Error));
	TestEqual(TEXT("failed import preserves prior data"), UElysiumExpressionData::Verify(Data, Json), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumExpressionResolverTest, "Elysium.Substrate.ExpressionData.Resolver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumExpressionResolverTest::RunTest(const FString&)
{
	auto* Corpus = NewObject<UElysiumExpressionTables>();
	auto Add = [Corpus](const TCHAR* Stem, const TCHAR* Status)
	{
		auto* Data = NewObject<UElysiumExpressionData>();
		Data->AssetId = FString(TEXT("vtmb:expression-table:")) + Stem;
		Data->Stem = Stem; Data->RuntimeStatus = Status;
		Data->Table.bPresent = Data->RuntimeStatus == TEXT("ready");
		Corpus->Tables.Add(Data->AssetId, Data); return Data;
	};
	auto* Generic = Add(TEXT("phonemes"), TEXT("ready"));
	auto* Male = Add(TEXT("phonemes_male"), TEXT("ready"));
	auto* Specific = Add(TEXT("demal_expressions"), TEXT("ready"));
	auto* Txt = Add(TEXT("scrubs_female_phonemes"), TEXT("authoring-only"));
	auto* Opaque = Add(TEXT("phonemes_strong"), TEXT("undecoded-vfe"));
	FElysiumExpressionSelection Selection;
	Selection.TableClass = TEXT("phonemes");
	Selection.FallbackAssetIds = {Generic->AssetId, Male->AssetId}; FString Error;
	TestTrue(TEXT("male chosen despite generic being first and resident"), Corpus->ResolveSelection(Selection, true, Error) == Male);
	TestTrue(TEXT("generic actor gets generic fallback"), Corpus->ResolveSelection(Selection, false, Error) == Generic);
	Selection.PrimaryAssetId = Specific->AssetId;
	TestTrue(TEXT("specific beats gender fallback"), Corpus->ResolveSelection(Selection, true, Error) == Specific);
	Selection.PrimaryAssetId = Txt->AssetId;
	TestTrue(TEXT("TXT-only cannot mask compiled fallback"), Corpus->ResolveSelection(Selection, false, Error) == Generic);
	TestFalse(TEXT("TXT fallback carries diagnostic"), Error.IsEmpty());
	Selection.PrimaryAssetId = Opaque->AssetId;
	TestNull(TEXT("undecoded VFE must not masquerade as generic"), Corpus->ResolveSelection(Selection, true, Error));
	Selection.PrimaryAssetId.Reset(); Corpus->Tables.Remove(Male->AssetId);
	TestNull(TEXT("broken preparation does not fall back to generic"), Corpus->ResolveSelection(Selection, true, Error));
	TestFalse(TEXT("broken preparation diagnosed"), Error.IsEmpty());
	TestTrue(TEXT("VFE-only event with path/extension"), Corpus->ResolveEvent(TEXT("Expressions\\DEMAL_EXPRESSIONS.VFE"), TEXT("expressions"), Error) == Specific);
	TestTrue(TEXT("bare model event"), Corpus->ResolveEvent(TEXT("demal"), TEXT("expressions"), Error) == Specific);
	TestNull(TEXT("authoring-only event is explicitly unavailable"), Corpus->ResolveEvent(TEXT("scrubs_female_phonemes"), TEXT("phonemes"), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumExpressionEmptyTableTest, "Elysium.Substrate.ExpressionData.EmptyControllers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumExpressionEmptyTableTest::RunTest(const FString&)
{
	auto* Data = NewObject<UElysiumExpressionData>();
	Data->AssetId = TEXT("vtmb:expression-table:crooked_cop_expressions");
	Data->Stem = TEXT("crooked_cop_expressions"); Data->RuntimeStatus = TEXT("ready");
	Data->Table.bPresent = true; Data->Table.bHasWeighting = true;
	Data->Table.Rows.AddDefaulted(32); FString Error;
	TestTrue(TEXT("zero-controller compiled table is present"), Data->IsRuntimeReady());
	const auto View = Data->PrepareLegacyView(Error);
	if (!TestTrue(TEXT("zero-controller view exists"), View.IsValid())) return false;
	TestEqual(TEXT("all empty rows retained"), View->Rows.Num(), 32);
	TestTrue(TEXT("no fake controller added"), View->Keys.IsEmpty());
	return true;
}
#endif
