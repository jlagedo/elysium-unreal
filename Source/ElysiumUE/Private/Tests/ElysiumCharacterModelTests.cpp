#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Visual/ElysiumCharacterModel.h"
#include "ElysiumCharacterProvenance.h"
#include "ElysiumPlayer.h"
#include "Engine/SkeletalMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCharacterModelIdentityTest, "Elysium.Substrate.CharacterModel.Identity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumCharacterModelIdentityTest::RunTest(const FString&)
{
	const FString Source = TEXT("Models\\Character\\NPC\\First\\SharedName.MDL");
	const FString Id = TEXT("vtmb:model:character/npc/first/sharedname");
	TestEqual(TEXT("source path becomes the full ID"), ElysiumCharacterModel::IdFromSource(Source), Id);
	TestEqual(TEXT("canonical IDs remain IDs"), ElysiumCharacterModel::IdFromSource(Id), Id);
	TestNotEqual(TEXT("same basename does not collapse model identity"), ElysiumCharacterModel::IdFromSource(Source),
		ElysiumCharacterModel::IdFromSource(TEXT("models/character/npc/second/sharedname.mdl")));
	for (const TCHAR* Bad : {TEXT("sharedname"), TEXT("sharedname.mdl"), TEXT("C:/models/a.mdl"),
		TEXT("models/../a.mdl"), TEXT("models//a.mdl"), TEXT("vtmb:model:"), TEXT("models/a|b.mdl"), TEXT("*1")})
		TestTrue(FString::Printf(TEXT("no inferred model for '%s'"), Bad), ElysiumCharacterModel::IdFromSource(Bad).IsEmpty());
	FElysiumAnimating Entity;
	Entity.Model = Source; Entity.TargetName = TEXT("keep_entity_identity");
	Entity.Handle = FElysiumEntityHandle(7, 93);
	TestEqual(TEXT("historical metadata accessor now routes native ID"), Entity.ModelStem(), Id);
	TestEqual(TEXT("source Model remains byte-for-byte unchanged"), Entity.Model, Source);
	TestTrue(TEXT("handle remains unchanged"), Entity.Handle == FElysiumEntityHandle(7, 93));
	TestEqual(TEXT("source target name remains unchanged"), Entity.TargetName, FString(TEXT("keep_entity_identity")));
	Entity.Model = TEXT("models/character/npc/second/sharedname.mdl");
	TestEqual(TEXT("metadata follows logical model replacement without stale basename cache"), Entity.ModelStem(),
		FString(TEXT("vtmb:model:character/npc/second/sharedname")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCharacterModelProvenanceTest, "Elysium.Substrate.CharacterModel.Provenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumCharacterModelProvenanceTest::RunTest(const FString&)
{
	const FString Id = TEXT("vtmb:model:fixture/a/shared");
	auto* Mesh = NewObject<USkeletalMesh>();
	auto* Record = NewObject<UElysiumCharacterProvenance>(Mesh);
	Record->AssetId = Id; Record->ModelPath = TEXT("models/fixture/a/shared.mdl"); Record->Stem = TEXT("shared");
	Record->bHasMeshData = true; Record->bHasExpressionData = true;
	Mesh->AddAssetUserData(Record);
	FString Error;
	TestTrue(TEXT("matching native mesh accepted"), ElysiumCharacterModel::Validate(Id, Mesh, Error) == Record);
	TestNull(TEXT("another full identity with same basename rejected"),
		ElysiumCharacterModel::Validate(TEXT("vtmb:model:fixture/b/shared"), Mesh, Error));
	TestTrue(TEXT("mismatch is diagnosed"), Error.Contains(TEXT("disagrees")));
	TestNull(TEXT("basename is not a runtime ID"), ElysiumCharacterModel::Validate(TEXT("shared"), Mesh, Error));
	TestNull(TEXT("missing prepared mesh rejected"), ElysiumCharacterModel::Validate(Id, nullptr, Error));
	Record->ModelPath = TEXT("models/fixture/b/shared.mdl");
	TestNull(TEXT("source path mutation rejected even if AssetId unchanged"), ElysiumCharacterModel::Validate(Id, Mesh, Error));
	Record->ModelPath = TEXT("models/fixture/a/shared.mdl"); Record->bHasExpressionData = false;
	TestNull(TEXT("old mesh without prepared-expression metadata rejected"), ElysiumCharacterModel::Validate(Id, Mesh, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCharacterModelCacheTest, "Elysium.Substrate.CharacterModel.CacheIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumCharacterModelCacheTest::RunTest(const FString&)
{
	const FString Id = TEXT("vtmb:model:fixture/cache");
	auto MakeMesh = [&Id]()
	{
		auto* Mesh = NewObject<USkeletalMesh>();
		auto* Record = NewObject<UElysiumCharacterProvenance>(Mesh);
		Record->AssetId = Id; Record->ModelPath = TEXT("models/fixture/cache.mdl");
		Record->bHasMeshData = true; Record->bHasExpressionData = true; Record->RecipeFingerprint = TEXT("recipe_a");
		Mesh->AddAssetUserData(Record); return Mesh;
	};
	auto* First = MakeMesh(); auto* Second = MakeMesh();
	const FString Key = ElysiumCharacterModel::AnimationCacheIdentity(Id, First);
	TestFalse(TEXT("native identity can be cached"), Key.IsEmpty());
	TestNotEqual(TEXT("different mesh object paths cannot alias animation cache"), Key,
		ElysiumCharacterModel::AnimationCacheIdentity(Id, Second));
	auto* Record = const_cast<UElysiumCharacterProvenance*>(UElysiumCharacterProvenance::Find(First));
	Record->RecipeFingerprint = TEXT("recipe_b");
	TestNotEqual(TEXT("new recipe invalidates cache identity"), Key, ElysiumCharacterModel::AnimationCacheIdentity(Id, First));
	Record->AssetId = TEXT("vtmb:model:fixture/wrong");
	TestTrue(TEXT("invalid mesh metadata never mints a cache key"), ElysiumCharacterModel::AnimationCacheIdentity(Id, First).IsEmpty());
	return true;
}
#endif
