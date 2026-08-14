#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumBrushComponent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumMover.h"
#include "Tests/ElysiumTestServices.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"

static constexpr EAutomationTestFlags GElysiumMoverTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRotatingDoorHandednessTest,
	"Elysium.Substrate.RotatingDoorHandedness", GElysiumMoverTestFlags)

bool FElysiumRotatingDoorHandednessTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}

	UWorld* EngineWorld = TestWorld.GetTestWorld();
	AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("mover owner spawned"), Owner))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("MoverRoot"));
	Owner->SetRootComponent(Root);
	Root->RegisterComponent();
	Owner->AddInstanceComponent(Root);

	auto DoorHull = []()
	{
		// An asymmetric leaf extending from its hinge along local +X makes the swing direction
		// observable from the embodied tip, rather than merely inspecting a cached target angle.
		FElysiumConvexHull Hull;
		for (float X : { 0.0f, 100.0f })
		{
			for (float Y : { -5.0f, 5.0f })
			{
				for (float Z : { -100.0f, 100.0f })
				{
					Hull.Vertices.Emplace(X, Y, Z);
				}
			}
		}
		return Hull;
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__rotating_door_handedness__");
	auto AddDoor = [&Defs, &DoorHull](const TCHAR* Name, int32 Model, const FVector& Origin,
		int32 SpawnFlags)
	{
		FElysiumEntityDef Door;
		Door.Classname = TEXT("func_door_rotating");
		Door.TargetName = Name;
		Door.Origin = Origin;
		Door.Model = Model;
		Door.Hulls.Add(DoorHull());
		Door.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
		Door.Keys.Add(TEXT("distance"), TEXT("90"));
		Door.Keys.Add(TEXT("speed"), TEXT("90"));
		Door.Keys.Add(TEXT("wait"), TEXT("-1"));
		Door.Keys.Add(TEXT("spawnflags"), FString::FromInt(SpawnFlags));
		Defs.Defs.Add(MoveTemp(Door));
	};
	AddDoor(TEXT("normal"), 1, FVector::ZeroVector, 0);
	AddDoor(TEXT("reversed"), 2, FVector(500.0f, 0.0f, 0.0f), 0x2);

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumDoorBase* Normal = World.FindByName(TEXT("normal"))
		? World.FindByName(TEXT("normal"))->AsDoorBase() : nullptr;
	FElysiumDoorBase* Reversed = World.FindByName(TEXT("reversed"))
		? World.FindByName(TEXT("reversed"))->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("normal rotating door resolved"), Normal)
		|| !TestNotNull(TEXT("reversed rotating door resolved"), Reversed)
		|| !TestNotNull(TEXT("normal rotating door has a body"), Normal ? Normal->Body : nullptr)
		|| !TestNotNull(TEXT("reversed rotating door has a body"), Reversed ? Reversed->Body : nullptr))
	{
		return false;
	}

	Normal->InputOpen(FElysiumEntityHandle());
	Reversed->InputOpen(FElysiumEntityHandle());
	World.Tick(1.0);

	const FVector NormalTip = Normal->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f));
	const FVector ReversedTip = Reversed->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f));
	TestTrue(TEXT("a normal Source +90 swing reflects to Unreal -Y"),
		NormalTip.Equals(FVector(0.0f, -100.0f, 0.0f), 0.1f));
	TestTrue(TEXT("REVERSE negates the Source swing before reflection"),
		ReversedTip.Equals(FVector(0.0f, 100.0f, 0.0f), 0.1f));
	TestEqual(TEXT("normal door reaches its open state"), Normal->State(),
		FElysiumDoorBase::EToggleState::AtTop);
	TestEqual(TEXT("reversed door reaches its open state"), Reversed->State(),
		FElysiumDoorBase::EToggleState::AtTop);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
