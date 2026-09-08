// Detail props. The bake
// instances the lump; the runtime's whole share is the two settings pages the bake reads, the tag
// contract it buckets by, and the actor shape it adopts. Each is pinned here, content-free.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumBakedTags.h"
#include "ElysiumDetailPropActor.h"
#include "ElysiumModelSettings.h"
#include "ElysiumSurfaceSettings.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Tests/AutomationCommon.h"

namespace ElysiumDetailPropTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDetailPropsTest, "Elysium.Substrate.DetailProps", GElysiumTestFlags)
bool FElysiumDetailPropsTest::RunTest(const FString&)
{
	// The two draw distances are VtMB's own: `cl_detaildist` 600 and `cl_detailfade` 300 inches,
	// registered by `CDetailObjectSystem::vfunc10` (client.dll 100e0d90) -- shipped as the Models
	// page's defaults in centimetres, never a Python literal.
	const UElysiumModelSettings* Models = GetDefault<UElysiumModelSettings>();
	TestEqual(TEXT("DetailDrawDistanceCm is cl_detaildist 600 in"), Models->DetailDrawDistanceCm, 600.f * 2.54f);
	TestEqual(TEXT("DetailFadeRangeCm is cl_detailfade 300 in"), Models->DetailFadeRangeCm, 300.f * 2.54f);
	TestTrue(TEXT("the fade band fits inside the draw distance"),
		Models->DetailFadeRangeCm <= Models->DetailDrawDistanceCm);

	// The sway amplitude is the one material knob, a Surfaces-page scalar pushed into
	// `MPC_ElysiumSurfaces` like every other: Source's `cl_detail_max_sway` 5 units, in cm.
	const UElysiumSurfaceSettings* Surfaces = GetDefault<UElysiumSurfaceSettings>();
	TestEqual(TEXT("DetailSwayAmplitude default"), Surfaces->DetailSwayAmplitude, 5.f * 2.54f);
	bool bBound = false;
	for (const TPair<FName, float UElysiumSurfaceSettings::*>& Binding : UElysiumSurfaceSettings::ScalarBindings())
	{
		if (Binding.Key == FName(TEXT("DetailSwayAmplitude")))
		{
			bBound = true;
			TestEqual(TEXT("the binding reaches the field"), Surfaces->*Binding.Value, Surfaces->DetailSwayAmplitude);
		}
	}
	TestTrue(TEXT("DetailSwayAmplitude is a collection binding"), bBound);

	// The tag contract the bake writes and `AdoptBakedLevel` buckets by.
	TArray<FName> Tags;
	Tags.Add(ElysiumBakedTags::Detail);
	Tags.Add(ElysiumBakedTags::DetailModel(TEXT("models_scenery_plants_grass_grassa")));
	TestEqual(TEXT("elysium.detail"), ElysiumBakedTags::Detail, FName(TEXT("elysium.detail")));
	TestEqual(TEXT("elysium.model parses"), ElysiumBakedTags::ParseDetailModel(Tags),
		FString(TEXT("models_scenery_plants_grass_grassa")));
	const TArray<FName> Untagged = { ElysiumBakedTags::Prop };
	TestTrue(TEXT("a prop carries no detail model"), ElysiumBakedTags::ParseDetailModel(Untagged).IsEmpty());

	// The actor's shape: an instanced component as root, static, uncollidable, shadowless -- the
	// facts the bake relies on rather than restating per map.
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
	{
		TestWorld.ForwardErrorMessages(this);
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no test world for the actor shape"));
		return true;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AElysiumDetailPropActor* Actor = World->SpawnActor<AElysiumDetailPropActor>();
	if (Actor == nullptr)
	{
		AddError(TEXT("AElysiumDetailPropActor did not spawn"));
		return false;
	}
	UInstancedStaticMeshComponent* Instances = Actor->Instances;
	TestTrue(TEXT("the instanced component is the root"), Actor->GetRootComponent() == Instances);
	if (Instances != nullptr)
	{
		TestEqual(TEXT("static mobility"), Instances->Mobility, EComponentMobility::Static);
		TestEqual(TEXT("no collision"), Instances->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("casts no shadow"), Instances->CastShadow);
		TestEqual(TEXT("a fresh actor places nothing"), Instances->GetInstanceCount(), 0);
		// One float per instance is the sway slot the bake fills; the component accepts it.
		Instances->SetNumCustomDataFloats(1);
		TestEqual(TEXT("one custom data float"), Instances->NumCustomDataFloats, 1);
	}
	TestWorld.ForwardErrorMessages(this);
	return true;
}
} // namespace ElysiumDetailPropTests

#endif // WITH_DEV_AUTOMATION_TESTS
