// R6.7 -- 3D-skybox composition (`docs/architecture/seam_map_map.md` -> "3D-skybox composition
// (R6.7)"). The bake places every miniature class through the one transform; the runtime's whole
// share is the scope marker it buckets by second, the fog set it stamps a detail component with,
// and the toggle that hides the miniature as one thing. Pinned here content-free, on a level of
// five tagged actors the test spawns itself.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumBakedTags.h"
#include "ElysiumDetailPropActor.h"
#include "ElysiumEnvironment.h"
#include "ElysiumFog.h"
#include "ElysiumSpriteActor.h"
#include "Visual/ElysiumMapVisuals.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Tests/AutomationCommon.h"

namespace ElysiumSkyScopeTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkyScopeTest, "Elysium.Substrate.SkyScope", GElysiumTestFlags)
bool FElysiumSkyScopeTest::RunTest(const FString&)
{
	// The marker: `elysium.sky` beside a class tag reads as "inside the miniature"; a class tag
	// alone does not.
	const TArray<FName> SkyDetailTags = {
		ElysiumBakedTags::Detail, ElysiumBakedTags::DetailModel(TEXT("weed")), ElysiumBakedTags::Sky };
	const TArray<FName> WorldDetailTags = {
		ElysiumBakedTags::Detail, ElysiumBakedTags::DetailModel(TEXT("grass")) };
	const TArray<FName> SkySpriteTags = {
		ElysiumBakedTags::Sprite, ElysiumBakedTags::EntityIndex(7), ElysiumBakedTags::Sky };
	TestTrue(TEXT("a detail actor tagged elysium.sky is in the miniature"), ElysiumBakedTags::InMiniature(SkyDetailTags));
	TestFalse(TEXT("a detail actor without it is not"), ElysiumBakedTags::InMiniature(WorldDetailTags));
	TestTrue(TEXT("a sprite actor tagged elysium.sky is in the miniature"), ElysiumBakedTags::InMiniature(SkySpriteTags));
	TestEqual(TEXT("the marker parses beside the model tag"),
		ElysiumBakedTags::ParseDetailModel(SkyDetailTags), FString(TEXT("weed")));
	TestEqual(TEXT("the marker parses beside the entity tag"), ElysiumBakedTags::ParseEntityIndex(SkySpriteTags), 7);

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
	{
		TestWorld.ForwardErrorMessages(this);
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no test world for the adopted level"));
		return true;
	}
	UWorld* World = TestWorld.GetTestWorld();

	// The visuals component on a bare owner, as AElysiumMapActor carries it. No BeginPlay, so no
	// light rig: AdoptBakedLevel hands its (empty) light list to nobody.
	AActor* Owner = World->SpawnActor<AActor>();
	UElysiumMapVisuals* Visuals = Owner ? NewObject<UElysiumMapVisuals>(Owner, TEXT("Visuals")) : nullptr;
	if (Visuals == nullptr)
	{
		AddError(TEXT("UElysiumMapVisuals did not construct"));
		return false;
	}
	Visuals->RegisterComponent();

	// The level: one sky chunk, one world prop, a world and a sky detail component, a sky sprite.
	AStaticMeshActor* Chunk = World->SpawnActor<AStaticMeshActor>();
	AStaticMeshActor* Prop = World->SpawnActor<AStaticMeshActor>();
	AElysiumDetailPropActor* SkyDetail = World->SpawnActor<AElysiumDetailPropActor>();
	AElysiumDetailPropActor* WorldDetail = World->SpawnActor<AElysiumDetailPropActor>();
	AElysiumSpriteActor* SkySprite = World->SpawnActor<AElysiumSpriteActor>();
	if (!Chunk || !Prop || !SkyDetail || !WorldDetail || !SkySprite)
	{
		AddError(TEXT("the five level actors did not spawn"));
		return false;
	}
	Chunk->Tags = { ElysiumBakedTags::Sky };
	Prop->Tags = { ElysiumBakedTags::Prop };
	SkyDetail->Tags = SkyDetailTags;
	WorldDetail->Tags = WorldDetailTags;
	SkySprite->Tags = SkySpriteTags;

	const FString MapName(TEXT("r6_7_sky_scope"));
	const int32 Adopted = Visuals->AdoptBakedLevel(MapName, FElysiumSkyDef());
	TestEqual(TEXT("five tagged actors adopted"), Adopted, 5);
	// Class first, scope second: the marked detail and sprite are in their class buckets, never
	// in the static-mesh sky bucket, and the counts read the marker.
	TestEqual(TEXT("one sky chunk"), Visuals->GetSkyActors().Num(), 1);
	TestEqual(TEXT("one prop"), Visuals->GetPropActors().Num(), 1);
	TestEqual(TEXT("two detail components"), Visuals->GetDetailActors().Num(), 2);
	TestEqual(TEXT("one of them in the miniature"), Visuals->DetailSkyComponentCount, 1);
	TestEqual(TEXT("one sprite"), Visuals->GetSpriteActors().Num(), 1);
	TestEqual(TEXT("in the miniature"), Visuals->SpriteSkyCount, 1);

	// The two fog sets: the world's off, the sky_camera's 100..200 cm. ApplyEnvironment stamps
	// every adopted primitive before it touches anything sky-shaped (no faces here, so it stops
	// right after).
	FElysiumEnvDef Env;
	Env.bFog = false;
	Env.bSkyFog = true;
	Env.SkyFogColor = FLinearColor::White;
	Env.SkyFogStartCm = 100.f;
	Env.SkyFogEndCm = 200.f;
	Visuals->ApplyEnvironment(Env, MapName);

	auto InvRange = [](const UPrimitiveComponent* Comp) -> float
	{
		const TArray<float>& Data = Comp->GetCustomPrimitiveData().Data;
		return Data.Num() > ElysiumFog::SlotInvRange ? Data[ElysiumFog::SlotInvRange] : -1.f;
	};
	auto Start = [](const UPrimitiveComponent* Comp) -> float
	{
		const TArray<float>& Data = Comp->GetCustomPrimitiveData().Data;
		return Data.Num() > ElysiumFog::SlotStart ? Data[ElysiumFog::SlotStart] : -1.f;
	};
	TestTrue(TEXT("the sky chunk carries the sky set"),
		FMath::IsNearlyEqual(InvRange(Chunk->GetStaticMeshComponent()), 0.01f, 1e-6f));
	TestTrue(TEXT("the sky detail component carries the sky set"),
		FMath::IsNearlyEqual(InvRange(SkyDetail->Instances), 0.01f, 1e-6f)
		&& FMath::IsNearlyEqual(Start(SkyDetail->Instances), 100.f, 1e-3f));
	TestTrue(TEXT("the world detail component carries the world set, which is off"),
		FMath::IsNearlyEqual(InvRange(WorldDetail->Instances), 0.f, 1e-6f));
	TestTrue(TEXT("the prop carries the world set, which is off"),
		FMath::IsNearlyEqual(InvRange(Prop->GetStaticMeshComponent()), 0.f, 1e-6f));

	// The miniature toggles as one thing: chunk, sky detail and sky sprite hide together; the
	// world detail and the prop stand.
	Visuals->ToggleSkybox();
	TestFalse(TEXT("miniature hidden"), Visuals->IsSkyboxVisible());
	TestTrue(TEXT("the sky chunk hides"), Chunk->IsHidden());
	TestTrue(TEXT("the sky detail hides"), SkyDetail->IsHidden());
	TestTrue(TEXT("the sky sprite hides"), SkySprite->IsHidden());
	TestFalse(TEXT("the world detail stands"), WorldDetail->IsHidden());
	TestFalse(TEXT("the prop stands"), Prop->IsHidden());
	Visuals->ToggleSkybox();
	TestFalse(TEXT("the sky detail shows again"), SkyDetail->IsHidden());
	TestFalse(TEXT("the sky sprite shows again"), SkySprite->IsHidden());

	TestWorld.ForwardErrorMessages(this);
	return true;
}
} // namespace ElysiumSkyScopeTests

#endif // WITH_DEV_AUTOMATION_TESTS
