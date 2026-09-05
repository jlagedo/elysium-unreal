// R6.2 -- switched lights and lightstyles (`docs/architecture/seam_map_map_lighting.md` ->
// "Switched lights and lightstyles"). The seam is the pattern a light entity writes for its style,
// observed on the recording double, and the multiplier the rig's table turns it into.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumTestServices.h"
#include "ElysiumVariant.h"
#include "Components/StaticMeshComponent.h"
#include "ElysiumFog.h"                // ElysiumLightStyle::SlotBrightness
#include "Visual/ElysiumLightRig.h"

#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"

namespace ElysiumLightClassTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

static FElysiumEntityDef LightDef(const TCHAR* Class, const TCHAR* Name, int32 Style,
	const TCHAR* Pattern = nullptr, int32 SpawnFlags = 0)
{
	FElysiumEntityDef Def;
	Def.Classname = Class;
	Def.TargetName = Name;
	Def.Keys.Add(TEXT("style"), FString::FromInt(Style));
	Def.Keys.Add(TEXT("_light"), TEXT("238 211 185 150"));
	Def.Keys.Add(TEXT("fade_time"), TEXT("0.05"));
	if (Pattern)
	{
		Def.Keys.Add(TEXT("pattern"), Pattern);
	}
	if (SpawnFlags)
	{
		Def.Keys.Add(TEXT("spawnflags"), FString::FromInt(SpawnFlags));
	}
	return Def;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLightSwitchTest, "Elysium.Substrate.LightSwitch", GElysiumTestFlags)
bool FElysiumLightSwitchTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");
	Defs.Defs.Add(LightDef(TEXT("light"), TEXT("chop_light"), 32));                          // on at spawn
	Defs.Defs.Add(LightDef(TEXT("light"), TEXT("chop_light"), 32));                          // the second row, same style
	Defs.Defs.Add(LightDef(TEXT("light_spot"), TEXT("houselights"), 33, nullptr, 1));         // START_OFF
	Defs.Defs.Add(LightDef(TEXT("light"), TEXT("tunnel_lights"), 34, TEXT("mmnmmommommnonmmonqnmmo")));
	Defs.Defs.Add(LightDef(TEXT("light"), TEXT("static_light"), 0));                         // unnamed in retail: style 0

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* Chop = World.FindByName(TEXT("chop_light"));
	FElysiumEntity* House = World.FindByName(TEXT("houselights"));
	FElysiumEntity* Tunnel = World.FindByName(TEXT("tunnel_lights"));
	if (!TestNotNull(TEXT("light resolved"), Chop) || !TestNotNull(TEXT("light_spot resolved"), House)
		|| !TestNotNull(TEXT("patterned light resolved"), Tunnel))
	{
		return false;
	}
	TestFalse(TEXT("light is a real class"), Chop->IsRecordOnly());
	TestFalse(TEXT("light_spot is a real class"), House->IsRecordOnly());

	// Spawn: on -> "m", START_OFF -> "a", authored pattern -> the pattern; style 0 writes nothing.
	TestEqual(TEXT("a light spawns full"), Services.LightStylePattern(32), FString(TEXT("m")));
	TestEqual(TEXT("a START_OFF light spawns dark"), Services.LightStylePattern(33), FString(TEXT("a")));
	TestEqual(TEXT("an authored pattern is written at spawn"), Services.LightStylePattern(34),
		FString(TEXT("mmnmmommommnonmmonqnmmo")));
	TestFalse(TEXT("a style below 32 writes nothing"), Services.LightStylePatterns.Contains(0));

	auto Fire = [&World](const TCHAR* Target, const TCHAR* Input, const TCHAR* Param = TEXT(""))
	{
		World.AcceptInput(Target, FName(Input), FElysiumVariant::String(Param),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	};

	// The tutorial's own wire: prop_switch -> chop_light.Toggle, off then on again.
	Fire(TEXT("chop_light"), TEXT("Toggle"));
	TestEqual(TEXT("Toggle on an on light turns it off"), Services.LightStylePattern(32), FString(TEXT("a")));
	Fire(TEXT("chop_light"), TEXT("Toggle"));
	TestEqual(TEXT("Toggle again turns it on"), Services.LightStylePattern(32), FString(TEXT("m")));
	Fire(TEXT("houselights"), TEXT("TurnOn"));
	TestEqual(TEXT("TurnOn on a START_OFF light writes full (its pattern is 'a')"),
		Services.LightStylePattern(33), FString(TEXT("m")));
	Fire(TEXT("tunnel_lights"), TEXT("TurnOff"));
	TestEqual(TEXT("TurnOff writes dark"), Services.LightStylePattern(34), FString(TEXT("a")));
	Fire(TEXT("tunnel_lights"), TEXT("TurnOn"));
	TestEqual(TEXT("TurnOn restores an authored multi-letter pattern"), Services.LightStylePattern(34),
		FString(TEXT("mmnmmommommnonmmonqnmmo")));
	Fire(TEXT("chop_light"), TEXT("SetPattern"), TEXT("z"));
	TestEqual(TEXT("SetPattern writes the parameter as is"), Services.LightStylePattern(32), FString(TEXT("z")));
	Fire(TEXT("chop_light"), TEXT("TurnOn"));
	TestEqual(TEXT("TurnOn ignores a one-letter pattern and writes full"),
		Services.LightStylePattern(32), FString(TEXT("m")));

	// FadeToPattern: from 'm' to 'p', one letter per fade_time on the world clock, then the whole
	// target pattern. (TurnOn wrote "m" but left the pattern at "z", as retail does, so the fade's
	// start is set explicitly.)
	Fire(TEXT("chop_light"), TEXT("SetPattern"), TEXT("m"));
	Fire(TEXT("chop_light"), TEXT("FadeToPattern"), TEXT("pq"));
	TestEqual(TEXT("the fade schedules its first step now"), Chop->NextThink, 0.0f);
	World.Tick(0.0);
	TestEqual(TEXT("first step: n"), Services.LightStylePattern(32), FString(TEXT("n")));
	TestTrue(TEXT("the next step is fade_time later"), FMath::IsNearlyEqual(Chop->NextThink, 0.05f, 1e-4f));
	// The clock is ticked just past each step: a think runs once its time has passed.
	World.Tick(0.06);
	TestEqual(TEXT("second step: o"), Services.LightStylePattern(32), FString(TEXT("o")));
	World.Tick(0.12);
	TestEqual(TEXT("arriving writes the whole pattern"), Services.LightStylePattern(32), FString(TEXT("pq")));
	TestEqual(TEXT("the fade stops thinking"), Chop->NextThink, ELYSIUM_NEVER_THINK);
	World.Tick(0.20);
	TestEqual(TEXT("nothing more is written"), Services.LightStylePattern(32), FString(TEXT("pq")));

	// Dormancy: ScriptHide turns the light off first, ScriptUnhide back on.
	Fire(TEXT("houselights"), TEXT("ScriptHide"));
	TestEqual(TEXT("ScriptHide darkens the style"), Services.LightStylePattern(33), FString(TEXT("a")));
	Fire(TEXT("houselights"), TEXT("ScriptUnhide"));
	TestEqual(TEXT("ScriptUnhide restores it"), Services.LightStylePattern(33), FString(TEXT("m")));

	// The rig half: a pattern write reaches an adopted source of that style through the tick's
	// multiplier, and the entity-switched styles are no longer clamped away.
	UElysiumLightRig* Rig = NewObject<UElysiumLightRig>();
	UPointLightComponent* Point = NewObject<UPointLightComponent>();
	Point->SetMobility(EComponentMobility::Movable);
	Point->SetIntensity(2.f);
	TArray<UElysiumLightRig::FAdoptedLight> Adopted;
	Adopted.Add({ Point, 252, 1, 32 });
	Rig->AdoptBaked(Adopted, TEXT("sm_test_1"));
	TestEqual(TEXT("a style-32 source keeps its style"), Rig->Sources()[0].Style, 32);
	TestEqual(TEXT("one switched source counted"), Rig->SwitchedSourceCount(), 1);
	TestEqual(TEXT("an unwritten style is full"), Rig->StylePattern(32), FString(TEXT("m")));
	TestTrue(TEXT("style 1 keeps the engine flicker pattern"), Rig->StylePattern(1).StartsWith(TEXT("mmnmmo")));
	TestTrue(TEXT("SetStylePattern accepts a switched style"), Rig->SetStylePattern(32, TEXT("a")));
	TestTrue(TEXT("'a' is a multiplier of 0"), FMath::IsNearlyEqual(Rig->StyleMultiplier(32), 0.f));
	Rig->SetStylePattern(32, TEXT("m"));
	TestTrue(TEXT("'m' is a multiplier of 1"), FMath::IsNearlyEqual(Rig->StyleMultiplier(32), 1.f));
	TestFalse(TEXT("an empty pattern is refused"), Rig->SetStylePattern(32, TEXT("")));
	TestFalse(TEXT("a style past the table is refused"), Rig->SetStylePattern(64, TEXT("m")));

	// R7.4 (G6): a styled BRUSH ENTITY joins after the level walk that adopts the chunks, because
	// the runtime builds its visual when the entity world embodies it. `AddStyledPrimitive` is that
	// late join -- it stamps CPD slot 6 immediately, so a body added on a paused frame renders at
	// the style's own phase rather than at the bake's flat 1.0.
	UStaticMeshComponent* Foam = NewObject<UStaticMeshComponent>();
	const int32 Before = Rig->StyledPrimitiveCount();
	TestTrue(TEXT("a styled brush body joins the clock"), Rig->AddStyledPrimitive(Foam, 1));
	TestEqual(TEXT("one more styled primitive"), Rig->StyledPrimitiveCount(), Before + 1);
	TestEqual(TEXT("and its slot is stamped at once"),
		Foam->GetCustomPrimitiveData().Data[ElysiumLightStyle::SlotBrightness],
		Rig->StyledPrimitiveBrightness(1));
	TestTrue(TEXT("registering it again replaces the row"), Rig->AddStyledPrimitive(Foam, 1));
	TestEqual(TEXT("rather than animating it twice"), Rig->StyledPrimitiveCount(), Before + 1);
	TestFalse(TEXT("a null component is refused"), Rig->AddStyledPrimitive(nullptr, 1));
	TestFalse(TEXT("so is style 0, the always-on base"), Rig->AddStyledPrimitive(Foam, 0));
	TestFalse(TEXT("and a style past the table"), Rig->AddStyledPrimitive(Foam, 64));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLightDynamicTest, "Elysium.Substrate.LightDynamic", GElysiumTestFlags)
bool FElysiumLightDynamicTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");
	FElysiumEntityDef Spot;
	Spot.Classname = TEXT("light_dynamic");
	Spot.TargetName = TEXT("headlamp");
	Spot.Origin = FVector(100.f, 200.f, 300.f);
	Spot.Keys.Add(TEXT("_light"), TEXT("255 128 0 200"));
	Spot.Keys.Add(TEXT("brightness"), TEXT("2"));
	Spot.Keys.Add(TEXT("distance"), TEXT("100"));
	Spot.Keys.Add(TEXT("_cone"), TEXT("60"));
	Spot.Keys.Add(TEXT("_inner_cone"), TEXT("30"));
	Spot.Keys.Add(TEXT("pitch"), TEXT("-90"));
	Spot.Keys.Add(TEXT("style"), TEXT("0"));
	Defs.Defs.Add(MoveTemp(Spot));
	FElysiumEntityDef Point;
	Point.Classname = TEXT("light_dynamic");
	Point.TargetName = TEXT("bulb");
	Point.Keys.Add(TEXT("_light"), TEXT("255 255 255"));
	Point.Keys.Add(TEXT("brightness"), TEXT("0"));
	Point.Keys.Add(TEXT("distance"), TEXT("50"));
	Point.Keys.Add(TEXT("_cone"), TEXT("0"));
	Defs.Defs.Add(MoveTemp(Point));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* Headlamp = World.FindByName(TEXT("headlamp"));
	FElysiumEntity* Bulb = World.FindByName(TEXT("bulb"));
	if (!TestNotNull(TEXT("spot resolved"), Headlamp) || !TestNotNull(TEXT("point resolved"), Bulb))
	{
		return false;
	}
	TestFalse(TEXT("light_dynamic is a real class"), Headlamp->IsRecordOnly());
	TestEqual(TEXT("both lights stood one runtime light each"), Services.Count(TEXT("BuildDynamicLight")), 2);
	TestTrue(TEXT("the spot was built as a spot"), Services.Saw(TEXT("BuildDynamicLight spot")));
	TestTrue(TEXT("the point was built as a point"), Services.Saw(TEXT("BuildDynamicLight point")));

	// The last spec is the point's: white, brightness 0 -> S = 100 -> Mag = 100 x 100 / 2.55.
	const FElysiumDynamicLightSpec& Last = Services.LastDynamicLight;
	TestTrue(TEXT("white normalises to white"), Last.Color.Equals(FLinearColor::White));
	TestTrue(TEXT("brightness 0 is the 100-scale VRAD magnitude"), FMath::IsNearlyEqual(Last.Mag, 100.f * 100.f / 2.55f, 0.01f));
	TestTrue(TEXT("reach is distance x 2.54"), FMath::IsNearlyEqual(Last.RadiusCm, 127.f));
	TestFalse(TEXT("_cone 0 is a point"), Last.bSpot);

	USceneComponent* Light = Headlamp->GetAttachChild();
	if (TestNotNull(TEXT("the spot offers its light as its attach child"), Light))
	{
		TestTrue(TEXT("it is a spot light component"), Light->IsA<USpotLightComponent>());
		TestTrue(TEXT("a light_dynamic spawns on"), Light->GetVisibleFlag());
		World.AcceptInput(TEXT("headlamp"), TEXT("TurnOff"), FElysiumVariant::Void(),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		TestFalse(TEXT("TurnOff hides it"), Light->GetVisibleFlag());
		World.AcceptInput(TEXT("headlamp"), TEXT("Toggle"), FElysiumVariant::Void(),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		TestTrue(TEXT("Toggle shows it again"), Light->GetVisibleFlag());
	}
	return true;
}

}   // namespace ElysiumLightClassTests

#endif // WITH_DEV_AUTOMATION_TESTS
