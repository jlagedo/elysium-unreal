// R6.4 -- brush fade distances (`docs/architecture/seam_map_map.md` -> "Brush fade distances").
// The producer wrote `cull_max_cm`; the seam under test is the world applying it to the brush
// visual it attaches, and nothing else deriving it.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumBrushComponent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumTestServices.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Tests/AutomationCommon.h"

namespace ElysiumBrushFadeTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBrushCullTest, "Elysium.Substrate.BrushCull", GElysiumTestFlags)
bool FElysiumBrushCullTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* EngineWorld = TestWorld.GetTestWorld();
	AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("brush owner spawned"), Owner))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("BrushRoot"));
	Owner->SetRootComponent(Root);
	Root->RegisterComponent();
	Owner->AddInstanceComponent(Root);

	auto BoxHull = []()
	{
		FElysiumConvexHull Hull;
		for (float X : { -20.f, 20.f })
		{
			for (float Y : { -20.f, 20.f })
			{
				for (float Z : { -20.f, 20.f })
				{
					Hull.Vertices.Emplace(X, Y, Z);
				}
			}
		}
		return Hull;
	};
	auto Brush = [&BoxHull](const TCHAR* Class, const TCHAR* Name, int32 Model, float CullMaxCm, bool bSky)
	{
		FElysiumEntityDef Def;
		Def.Classname = Class;
		Def.TargetName = Name;
		Def.Model = Model;
		Def.Hulls.Add(BoxHull());
		Def.BrushMesh = FString::Printf(TEXT("brush_%d"), Model);
		Def.CullMaxCm = CullMaxCm;
		Def.bSky = bSky;
		Def.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
		return Def;
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");
	// A func_lod as the producer emits it: DisappearDist 2500 -> cull_max_cm 6350.
	FElysiumEntityDef Lod = Brush(TEXT("func_lod"), TEXT("lod1"), 31, 6350.f, false);
	Lod.Keys.Add(TEXT("DisappearDist"), TEXT("2500"));
	Defs.Defs.Add(MoveTemp(Lod));
	// The same brush class in the 3D-skybox miniature: the distance rides the body scale (16).
	Defs.Defs.Add(Brush(TEXT("func_lod"), TEXT("lod_sky"), 32, 6350.f, true));
	// A plain brush with no cull range keeps Unreal's default (0 = never culled by distance).
	Defs.Defs.Add(Brush(TEXT("func_brush"), TEXT("wall"), 33, 0.f, false));
	// The point class: no model, no body, its numbers carried for the debug view.
	FElysiumEntityDef Window;
	Window.Classname = TEXT("func_areaportalwindow");
	Window.TargetName = TEXT("apw");
	Window.Keys.Add(TEXT("FadeStartDist"), TEXT("1000"));
	Window.Keys.Add(TEXT("FadeDist"), TEXT("1280"));
	Window.Keys.Add(TEXT("target"), TEXT("wndwblack1"));
	Window.Keys.Add(TEXT("BackgroundBModel"), TEXT("wndw1"));
	Defs.Defs.Add(MoveTemp(Window));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* LiveLod = World.FindByName(TEXT("lod1"));
	FElysiumEntity* LiveSky = World.FindByName(TEXT("lod_sky"));
	FElysiumEntity* LiveWall = World.FindByName(TEXT("wall"));
	FElysiumEntity* LiveWindow = World.FindByName(TEXT("apw"));
	if (!TestNotNull(TEXT("func_lod resolved"), LiveLod) || !TestNotNull(TEXT("sky func_lod resolved"), LiveSky)
		|| !TestNotNull(TEXT("func_brush resolved"), LiveWall) || !TestNotNull(TEXT("areaportalwindow resolved"), LiveWindow))
	{
		return false;
	}
	TestFalse(TEXT("func_lod is a real class now, not a record"), LiveLod->IsRecordOnly());
	TestFalse(TEXT("func_areaportalwindow is a real class now, not a record"), LiveWindow->IsRecordOnly());
	TestEqual(TEXT("every meshed brush requested one visual"), Services.Count(TEXT("BuildBrushVisual")), 3);

	UStaticMeshComponent* LodVisual = LiveLod->Body ? LiveLod->Body->GetVisual() : nullptr;
	UStaticMeshComponent* SkyVisual = LiveSky->Body ? LiveSky->Body->GetVisual() : nullptr;
	UStaticMeshComponent* WallVisual = LiveWall->Body ? LiveWall->Body->GetVisual() : nullptr;
	if (!TestNotNull(TEXT("func_lod visual attached"), LodVisual) || !TestNotNull(TEXT("sky visual attached"), SkyVisual)
		|| !TestNotNull(TEXT("wall visual attached"), WallVisual))
	{
		return false;
	}
	TestTrue(TEXT("the producer's cull_max_cm is the visual's cull distance, unchanged"),
		FMath::IsNearlyEqual(LodVisual->LDMaxDrawDistance, 6350.f));
	TestTrue(TEXT("a miniature brush's cull distance rides the body scale"),
		FMath::IsNearlyEqual(SkyVisual->LDMaxDrawDistance, 6350.f * 16.f));
	TestTrue(TEXT("a row with no cull range leaves the visual at never-culled"),
		FMath::IsNearlyEqual(WallVisual->LDMaxDrawDistance, 0.f));

	// The leaf carries the authored value beside the baked one, and derives neither.
	TArray<TPair<FString, FString>> State;
	LiveLod->GetDebugState(State);
	TestTrue(TEXT("func_lod reports its authored DisappearDist"),
		State.ContainsByPredicate([](const TPair<FString, FString>& Row)
		{
			return Row.Key == TEXT("DisappearDist") && Row.Value == TEXT("2500 in");
		}));
	State.Reset();
	LiveWindow->GetDebugState(State);
	TestTrue(TEXT("func_areaportalwindow reports its backing and window"),
		State.ContainsByPredicate([](const TPair<FString, FString>& Row)
		{
			return Row.Key == TEXT("Backing / window") && Row.Value == TEXT("wndwblack1 / wndw1");
		}));
	TestNull(TEXT("a point func_areaportalwindow has no body to cull"), LiveWindow->Body);
	return true;
}

}   // namespace ElysiumBrushFadeTests

#endif // WITH_DEV_AUTOMATION_TESTS
