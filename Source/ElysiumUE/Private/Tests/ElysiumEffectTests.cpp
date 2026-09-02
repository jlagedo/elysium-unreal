// R7.3 -- effects (`docs/architecture/effects-architecture.md` §5, the one runtime row the
// ambient set authorizes): `SetRateScale` on an env_particle ramps linearly, through the leaf's
// publish, into the placed actor's one rate float -- `User.RateScale = rampedScale x VolumeScale`
// -- and TurnOff / TurnOn reach the actor's on/off state on the same seam.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEffectActor.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumTestServices.h"
#include "ElysiumVariant.h"

#include "Engine/World.h"
#include "Tests/AutomationCommon.h"

namespace ElysiumEffectTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEffectRateRampTest, "Elysium.Policy.Effects.RateRamp", GElysiumTestFlags)
bool FElysiumEffectRateRampTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");
	FElysiumEntityDef Emitter;
	Emitter.Classname = TEXT("env_particle");
	Emitter.TargetName = TEXT("fire");
	Emitter.Keys.Add(TEXT("active"), TEXT("1"));
	Emitter.Keys.Add(TEXT("particle_definition"), TEXT("barrelfireemitter"));
	Emitter.Keys.Add(TEXT("ramp_scale"), TEXT("1"));
	Emitter.Keys.Add(TEXT("ramp_time"), TEXT("2"));
	Defs.Defs.Add(MoveTemp(Emitter));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	const FElysiumEntity* Fire = World.FindByName(TEXT("fire"));
	if (!TestNotNull(TEXT("env_particle resolved"), Fire))
	{
		return false;
	}
	const int32 Index = Fire->Handle.Index;

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
	{
		TestWorld.ForwardErrorMessages(this);
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no test world for the effect actor"));
		return true;
	}
	AElysiumEffectActor* Actor = TestWorld.GetTestWorld()->SpawnActor<AElysiumEffectActor>();
	if (!TestNotNull(TEXT("effect actor spawned"), Actor))
	{
		return false;
	}
	// A func_particle's size scalar folds into the same float; 2 makes the fold visible.
	Actor->VolumeScale = 2.f;

	auto Push = [&]() -> bool
	{
		const FElysiumWeatherEmitterState* State = Services.Emitters.Find(Index);
		if (!State)
		{
			return false;
		}
		Actor->Drive(*State, nullptr);
		return true;
	};
	auto Send = [&](const TCHAR* Input, const TCHAR* Param)
	{
		World.AcceptInput(TEXT("fire"), FName(Input),
			Param ? FElysiumVariant::String(Param) : FElysiumVariant::Void(),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	};

	TestTrue(TEXT("the spawn publish reaches the actor"), Push());
	TestTrue(TEXT("an active emitter turns the actor on"), Actor->IsOn());
	TestTrue(TEXT("rate 1 x volume 2"), FMath::IsNearlyEqual(Actor->GetRate(), 2.0f));

	// SetRateScale 0 at t = 0 with ramp_time 2: the substrate ramps 1 -> 0 linearly over two
	// seconds and publishes every think; each publish lands in the actor's float x VolumeScale.
	Send(TEXT("SetRateScale"), TEXT("0"));
	World.Tick(0.0);
	Push();
	TestTrue(TEXT("the ramp starts at the old value"), FMath::IsNearlyEqual(Actor->GetRate(), 2.0f, 0.01f));
	World.Tick(0.5);
	Push();
	TestTrue(TEXT("a quarter in: 0.75 x 2"), FMath::IsNearlyEqual(Actor->GetRate(), 1.5f, 0.01f));
	World.Tick(1.0);
	Push();
	TestTrue(TEXT("half way: 0.5 x 2"), FMath::IsNearlyEqual(Actor->GetRate(), 1.0f, 0.01f));
	World.Tick(1.5);
	Push();
	TestTrue(TEXT("three quarters: 0.25 x 2"), FMath::IsNearlyEqual(Actor->GetRate(), 0.5f, 0.01f));
	World.Tick(2.0);
	Push();
	TestTrue(TEXT("the ramp lands on the target"), FMath::IsNearlyZero(Actor->GetRate(), 0.01f));
	World.Tick(5.0);
	Push();
	TestTrue(TEXT("a landed ramp holds"), FMath::IsNearlyZero(Actor->GetRate(), 0.01f));

	// A ramp back up from a held target, with a shorter ramp time, is the same line.
	Send(TEXT("SetRampTime"), TEXT("1"));
	Send(TEXT("SetRateScale"), TEXT("1"));
	World.Tick(5.0);
	World.Tick(5.25);
	Push();
	TestTrue(TEXT("a quarter up: 0.25 x 2"), FMath::IsNearlyEqual(Actor->GetRate(), 0.5f, 0.01f));
	World.Tick(6.0);
	Push();
	TestTrue(TEXT("back at 1 x 2"), FMath::IsNearlyEqual(Actor->GetRate(), 2.0f, 0.01f));

	// TurnOff lets the actor finish; TurnOn restarts it.
	Send(TEXT("TurnOff"), nullptr);
	World.Tick(6.0);
	Push();
	TestFalse(TEXT("TurnOff reaches the actor"), Actor->IsOn());
	Send(TEXT("TurnOn"), nullptr);
	World.Tick(6.0);
	Push();
	TestTrue(TEXT("TurnOn restarts the actor"), Actor->IsOn());
	TestTrue(TEXT("the rate survives the restart"), FMath::IsNearlyEqual(Actor->GetRate(), 2.0f, 0.01f));
	return true;
}
}

#endif
