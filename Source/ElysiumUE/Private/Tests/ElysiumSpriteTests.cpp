// R6.1 -- sprites. Two seams, both
// content-free: the `env_sprite` leaf's on/off rule observed on the recording double, and the
// glow rule the proxy applies -- the settings defaults, the pure formulas, the tag contract and
// the actor shape the bake relies on.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumBakedTags.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSpriteActor.h"
#include "ElysiumSpriteComponent.h"
#include "ElysiumSpriteGlow.h"
#include "ElysiumSpriteSettings.h"
#include "ElysiumTestServices.h"
#include "ElysiumVariant.h"

#include "Engine/World.h"
#include "Tests/AutomationCommon.h"

namespace ElysiumSpriteTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

static FElysiumEntityDef SpriteDef(const TCHAR* Name, int32 SpawnFlags, bool bStartHidden = false)
{
	FElysiumEntityDef Def;
	Def.Classname = TEXT("env_sprite");
	Def.TargetName = Name;
	Def.Keys.Add(TEXT("model"), TEXT("materials/sprites/glowa.vmt"));
	Def.Keys.Add(TEXT("rendermode"), TEXT("3"));
	Def.Keys.Add(TEXT("scale"), TEXT("0.5"));
	if (SpawnFlags)
	{
		Def.Keys.Add(TEXT("spawnflags"), FString::FromInt(SpawnFlags));
	}
	Def.bStartHidden = bStartHidden;
	return Def;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEnvSpriteTest, "Elysium.Substrate.EnvSprite", GElysiumTestFlags)
bool FElysiumEnvSpriteTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");
	Defs.Defs.Add(SpriteDef(TEXT(""), 0));                       // 0: unnamed -> on
	Defs.Defs.Add(SpriteDef(TEXT("lamp_glow"), 0));              // 1: named, no Start On -> off
	Defs.Defs.Add(SpriteDef(TEXT("cop_flash"), 1));              // 2: named + Start On -> on
	Defs.Defs.Add(SpriteDef(TEXT("hidden_glow"), 1, true));      // 3: start_hidden -> off whatever the flag
	Defs.Defs.Add(SpriteDef(TEXT("doomed_glow"), 1));            // 4: on, then killed

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* Lamp = World.FindByName(TEXT("lamp_glow"));
	if (!TestNotNull(TEXT("env_sprite resolved"), Lamp))
	{
		return false;
	}
	TestFalse(TEXT("env_sprite is a real class"), Lamp->IsRecordOnly());

	auto Visible = [&Services](int32 Index) -> int32
	{
		const bool* Found = Services.BakedSpriteVisible.Find(Index);
		return Found ? (*Found ? 1 : 0) : -1;
	};
	// CSprite::Spawn: unnamed or Start On draws, a named sprite without the flag does not, and
	// start_hidden wins over the flag. Every one publishes at spawn, by its lump index.
	TestEqual(TEXT("an unnamed sprite spawns on"), Visible(0), 1);
	TestEqual(TEXT("a named sprite without Start On spawns off"), Visible(1), 0);
	TestEqual(TEXT("a named Start On sprite spawns on"), Visible(2), 1);
	TestEqual(TEXT("a start_hidden sprite spawns off"), Visible(3), 0);
	TestEqual(TEXT("the fifth spawns on"), Visible(4), 1);

	auto Fire = [&World](const TCHAR* Target, const TCHAR* Input)
	{
		World.AcceptInput(Target, FName(Input), FElysiumVariant::String(TEXT("")),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	};
	// The four wires the corpus fires, plus the toggle.
	Fire(TEXT("lamp_glow"), TEXT("ShowSprite"));
	TestEqual(TEXT("ShowSprite draws"), Visible(1), 1);
	Fire(TEXT("lamp_glow"), TEXT("HideSprite"));
	TestEqual(TEXT("HideSprite hides"), Visible(1), 0);
	Fire(TEXT("lamp_glow"), TEXT("TurnOn"));
	TestEqual(TEXT("TurnOn draws"), Visible(1), 1);
	Fire(TEXT("lamp_glow"), TEXT("TurnOff"));
	TestEqual(TEXT("TurnOff hides"), Visible(1), 0);
	Fire(TEXT("lamp_glow"), TEXT("ToggleSprite"));
	TestEqual(TEXT("ToggleSprite flips on"), Visible(1), 1);
	Fire(TEXT("lamp_glow"), TEXT("ToggleSprite"));
	TestEqual(TEXT("ToggleSprite flips off"), Visible(1), 0);

	// Dormancy through the base chain: ScriptHide hides an on sprite, ScriptUnhide restores
	// its own state, and a sprite that was off stays off across the pair.
	Fire(TEXT("cop_flash"), TEXT("ScriptHide"));
	TestEqual(TEXT("ScriptHide hides the drawn sprite"), Visible(2), 0);
	Fire(TEXT("cop_flash"), TEXT("ScriptUnhide"));
	TestEqual(TEXT("ScriptUnhide restores it"), Visible(2), 1);
	Fire(TEXT("lamp_glow"), TEXT("ScriptHide"));
	Fire(TEXT("lamp_glow"), TEXT("ScriptUnhide"));
	TestEqual(TEXT("an off sprite stays off through ScriptHide/Unhide"), Visible(1), 0);
	// start_hidden is the base's ScriptHide at birth: ScriptUnhide reveals a Start On sprite.
	Fire(TEXT("hidden_glow"), TEXT("ScriptUnhide"));
	TestEqual(TEXT("ScriptUnhide reveals a start_hidden Start On sprite"), Visible(3), 1);
	// Kill is terminal and hides.
	Fire(TEXT("doomed_glow"), TEXT("Kill"));
	TestEqual(TEXT("Kill hides"), Visible(4), 0);
	return true;
}

} // namespace ElysiumSpriteTests

#endif // WITH_DEV_AUTOMATION_TESTS
