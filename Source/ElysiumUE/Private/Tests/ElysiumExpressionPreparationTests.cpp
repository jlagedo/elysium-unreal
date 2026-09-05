#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Visual/ElysiumExpressionPreparation.h"
#include "Visual/ElysiumExpressionTable.h"
#include "ElysiumCastData.h"
#include "ElysiumCharacterProvenance.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/GarbageCollection.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	UElysiumCastData* ExpressionCast()
	{
		auto* Cast = NewObject<UElysiumCastData>();
		Cast->ExpressionTables = NewObject<UElysiumExpressionTables>();
		for (const TCHAR* Stem : {TEXT("phonemes"), TEXT("phonemes_male"), TEXT("demal_expressions")})
		{
			auto* Data = NewObject<UElysiumExpressionData>();
			Data->AssetId = FString(TEXT("vtmb:expression-table:")) + Stem;
			Data->Stem = Stem; Data->RuntimeStatus = TEXT("ready");
			Data->Table.bPresent = true; Data->Table.bHasWeighting = true;
			Data->Table.Rows.AddDefaulted(); Data->Table.Rows[0].Name = TEXT("Neutral");
			Cast->ExpressionTables->Tables.Add(Data->AssetId, Data);
		}
		return Cast;
	}
	FString AsJson(const TSharedPtr<FJsonObject>& Object)
	{
		FString Result;
		FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Result));
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumExpressionPreparationLifetime, "Elysium.Substrate.ExpressionPreparation.Lifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumExpressionPreparationLifetime::RunTest(const FString&)
{
	ElysiumExpressions::ClearCache();
	FString Error;
	auto* Cast = ExpressionCast();
	TWeakObjectPtr<UElysiumCastData> WeakCast(Cast);
	auto Scope = ElysiumExpressions::PrepareResident(0xfed001u, Cast, Error);
	if (!TestTrue(TEXT("resident preparation succeeds"), Scope.IsValid())) { AddError(Error); return false; }
	TestEqual(TEXT("all ready tables cached including zero-key table"), Scope->NumPreparedTables(), 3);
	Cast = nullptr;
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("preparation keeps cast and hard corpus references alive"), WeakCast.IsValid());
	const auto View = ElysiumExpressions::LoadPreparedEvent(0xfed001u, TEXT("demal"), TEXT("expressions"), Error);
	TestTrue(TEXT("event uses resident view"), View.IsValid());
	TestFalse(TEXT("different map cannot borrow this scope"),
		ElysiumExpressions::LoadPreparedEvent(0xfed002u, TEXT("demal"), TEXT("expressions"), Error).IsValid());
	auto Replacement = ElysiumExpressions::PrepareResident(0xfed001u, WeakCast.Get(), Error);
	Scope.Reset();
	TestTrue(TEXT("older handle cannot unregister its replacement"),
		ElysiumExpressions::LoadPreparedEvent(0xfed001u, TEXT("demal"), TEXT("expressions"), Error).IsValid());
	Replacement.Reset();
	TestFalse(TEXT("last owner releases epoch"),
		ElysiumExpressions::LoadPreparedEvent(0xfed001u, TEXT("demal"), TEXT("expressions"), Error).IsValid());
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("weak registry does not pin cooked assets after teardown"), WeakCast.IsValid());
	TestTrue(TEXT("an active event's copied view survives scope release"), View.IsValid() && View->Rows.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumExpressionActorSelection, "Elysium.Substrate.ExpressionPreparation.ActorSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumExpressionActorSelection::RunTest(const FString&)
{
	FElysiumEntityWorld World(nullptr, nullptr);
	FElysiumEntityDefs Defs; Defs.MapName = TEXT("expression_actor_fixture");
	World.Load(MoveTemp(Defs)); World.SpawnPlayer();
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("player fixture"), Player)) return false;
	auto* Cast = ExpressionCast();
	FElysiumCastModel Model; Model.AssetId = TEXT("vtmb:model:fixture/female_named_model");
	Cast->Models.Add(Model.AssetId, Model);
	Cast->Aliases.Add(TEXT("models/fixture/female_named_model.mdl"), Model.AssetId);
	auto* Mesh = NewObject<USkeletalMesh>();
	auto* Record = NewObject<UElysiumCharacterProvenance>(Mesh);
	Record->AssetId = Model.AssetId; Record->bHasMeshData = true; Record->bHasExpressionData = true;
	Record->ExpressionModelFlags = 0; Record->bExpressionModelIsMale = true;
	auto& Selection = Record->ExpressionSelections.AddDefaulted_GetRef();
	Selection.TableClass = TEXT("phonemes");
	Selection.FallbackAssetIds = {TEXT("vtmb:expression-table:phonemes"), TEXT("vtmb:expression-table:phonemes_male")};
	Mesh->AddAssetUserData(Record);
	auto* Body = NewObject<USkeletalMeshComponent>(); Body->SetSkeletalMeshAsset(Mesh);
	FElysiumEntity Actor;
	Actor.World = &World; Actor.Handle = FElysiumEntityHandle(123, World.GetEpoch());
	Actor.Model = TEXT("models/fixture/female_named_model.mdl"); Actor.GenericModelBody = Body;
	FString Error;
	auto Scope = ElysiumExpressions::PrepareResident(World.GetEpoch(), Cast, Error);
	if (!TestTrue(TEXT("scope prepared"), Scope.IsValid())) { AddError(Error); return false; }
	auto View = ElysiumExpressions::LoadPreparedPhonemes(Actor, Error);
	TestTrue(TEXT("NPC gender is the source flag, not filename or player sheet"), View.IsValid() && View->Stem == TEXT("phonemes_male"));
	Player->Model = Actor.Model; Player->GenericModelBody = Body;
	Player->Sheet.SetMale(false); Player->Sheet.RecomputeCurrent(nullptr);
	View = ElysiumExpressions::LoadPreparedPhonemes(*Player, Error);
	TestTrue(TEXT("player uses its own sheet over model gender"), View.IsValid() && View->Stem == TEXT("phonemes"));
	Player->Sheet.SetMale(true); Player->Sheet.RecomputeCurrent(nullptr);
	Record->ExpressionModelFlags = 0x100; Record->bExpressionModelIsMale = false;
	View = ElysiumExpressions::LoadPreparedPhonemes(Actor, Error);
	TestTrue(TEXT("NPC does not borrow male player's gender"), View.IsValid() && View->Stem == TEXT("phonemes"));
	Selection.PrimaryAssetId = TEXT("vtmb:expression-table:demal_expressions");
	View = ElysiumExpressions::LoadPreparedPhonemes(Actor, Error);
	TestTrue(TEXT("specific compiled primary precedes fallback"), View.IsValid() && View->Stem == TEXT("demal_expressions"));
	Selection.PrimaryAssetId = TEXT("vtmb:expression-table:phonemes");
	FElysiumExpressionSelection Expression;
	Expression.TableClass = TEXT("expressions");
	Expression.PrimaryAssetId = TEXT("vtmb:expression-table:demal_expressions");
	Record->ExpressionSelections.Add(Expression);
	View = ElysiumExpressions::LoadPreparedModelSelection(Actor, TEXT("expressions"), Error);
	TestTrue(TEXT("disposition uses the expression class rather than phoneme class"), View.IsValid() && View->Stem == TEXT("demal_expressions"));
	Record->AssetId = TEXT("vtmb:model:fixture/different_body");
	TestFalse(TEXT("stale body/model pair rejected"), ElysiumExpressions::LoadPreparedPhonemes(Actor, Error).IsValid());
	TestTrue(TEXT("stale identity diagnosed"), Error.Contains(TEXT("disagree")));
	Record->AssetId = Model.AssetId; Record->bHasExpressionData = false;
	TestFalse(TEXT("old mesh metadata cannot silently use generic"), ElysiumExpressions::LoadPreparedPhonemes(Actor, Error).IsValid());
	Actor.GenericModelBody = nullptr; Player->GenericModelBody = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumExpressionProvenanceProjection, "Elysium.Substrate.ExpressionPreparation.Provenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumExpressionProvenanceProjection::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> MeshData;
	const FString MeshJson = TEXT(R"JSON({"schemaVersion":"1.0.0","modelPath":"models/fixture/test.mdl","stem":"test",
	 "facial":null,"eyes":null,"composition":{"rules":[]},"splitBones":[],"materialSlots":[],"skinTable":[],"skinFamilies":[],
	 "expressionData":{"schemaVersion":"1.0.0","modelFlags":256,"modelIsMale":false,"selections":[]}})JSON");
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MeshJson), MeshData)) return false;
	auto Source = MakeShared<FJsonObject>();
	Source->SetStringField(TEXT("class"), TEXT("phonemes"));
	Source->SetStringField(TEXT("stem"), TEXT("test_phonemes"));
	Source->SetStringField(TEXT("asset"), TEXT("vtmb:missing-expression-table:test_phonemes"));
	Source->SetBoolField(TEXT("resolved"), false);
	TArray<TSharedPtr<FJsonValue>> SourceFallbacks, FallbackIds;
	for (const TCHAR* Stem : {TEXT("phonemes"), TEXT("phonemes_male")})
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("stem"), Stem);
		const FString Id = FString(TEXT("vtmb:expression-table:")) + Stem;
		Row->SetStringField(TEXT("asset"), Id); Row->SetBoolField(TEXT("resolved"), true);
		SourceFallbacks.Add(MakeShared<FJsonValueObject>(Row)); FallbackIds.Add(MakeShared<FJsonValueString>(Id));
	}
	Source->SetArrayField(TEXT("fallbacks"), SourceFallbacks);
	auto Selection = MakeShared<FJsonObject>();
	Selection->SetStringField(TEXT("tableClass"), TEXT("phonemes")); Selection->SetStringField(TEXT("primaryAssetId"), TEXT(""));
	Selection->SetArrayField(TEXT("fallbackAssetIds"), FallbackIds);
	Selection->SetStringField(TEXT("sourceSelectionJson"), AsJson(Source));
	const auto ExpressionData = MeshData->GetObjectField(TEXT("expressionData"));
	ExpressionData->SetArrayField(TEXT("selections"), {MakeShared<FJsonValueObject>(Selection)});
	auto Envelope = MakeShared<FJsonObject>();
	Envelope->SetStringField(TEXT("assetId"), TEXT("vtmb:model:fixture/test")); Envelope->SetObjectField(TEXT("meshData"), MeshData);
	auto* Mesh = NewObject<USkeletalMesh>(); FString Error;
	const auto* Record = UElysiumCharacterProvenance::ApplyJson(Mesh, AsJson(Envelope), Error);
	if (!TestNotNull(TEXT("typed selections import"), Record)) { AddError(Error); return false; }
	TestTrue(TEXT("explicit cooked expression presence"), Record->bHasExpressionData);
	TestEqual(TEXT("raw model flags preserved"), Record->ExpressionModelFlags, 256u);
	TestEqual(TEXT("all fallback references retained"), Record->ExpressionSelections[0].FallbackAssetIds.Num(), 2);
	TestEqual(TEXT("typed expressions verified"), UElysiumCharacterProvenance::VerifyMeshData(Mesh, AsJson(MeshData)), FString());
	ExpressionData->SetBoolField(TEXT("modelIsMale"), true);
	TestNull(TEXT("gender/source mismatch rejected transactionally"), UElysiumCharacterProvenance::ApplyJson(Mesh, AsJson(Envelope), Error));
	TestTrue(TEXT("prior attached provenance survives rejection"), UElysiumCharacterProvenance::Find(Mesh) == Record);
	ExpressionData->SetBoolField(TEXT("modelIsMale"), false);
	FallbackIds.Swap(0, 1); Selection->SetArrayField(TEXT("fallbackAssetIds"), FallbackIds);
	TestNull(TEXT("fallback order mutation rejected"), UElysiumCharacterProvenance::ApplyJson(Mesh, AsJson(Envelope), Error));
	TestTrue(TEXT("mutation diagnostic identifies fallback"), Error.Contains(TEXT("fallbackAssetIds[0]")));
	return true;
}
#endif
