// R6.1 -- sprites (`docs/architecture/seam_map_map.md` -> "Sprites (R6.1)"). Two seams, both
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpriteGlowTest, "Elysium.Substrate.SpriteGlow", GElysiumTestFlags)
bool FElysiumSpriteGlowTest::RunTest(const FString&)
{
	// The page ships VtMB's own numbers (client.dll `GlowBlend` 100c24a0 and the two cvars).
	const UElysiumSpriteSettings* Page = GetDefault<UElysiumSpriteSettings>();
	TestEqual(TEXT("GlowFalloff is 19000"), Page->GlowFalloff, 19000.f);
	TestEqual(TEXT("GlowMinBrightness is 0.05"), Page->GlowMinBrightness, 0.05f);
	TestEqual(TEXT("GlowSizePerDistance is 1/200"), Page->GlowSizePerDistance, 0.005f);
	TestEqual(TEXT("GlowFadeInSeconds is r_glowfadein 0.2"), Page->GlowFadeInSeconds, 0.2f);
	TestEqual(TEXT("GlowFadeOutSeconds is r_glowfadeout 0.1"), Page->GlowFadeOutSeconds, 0.1f);
	TestEqual(TEXT("the query footprint is 3/128 of the distance"), Page->SpriteQueryFootprintPerDistance, 3.f / 128.f);
	TestEqual(TEXT("the fixed query half-size is VtMB's 3 units"), Page->SpriteQueryFixedHalfInches, 3.f);
	TestEqual(TEXT("the query grid is 4"), Page->SpriteQueryGrid, 4);
	const ElysiumSpriteGlow::FParams P = ElysiumSpriteGlow::FParams::FromSettings();
	TestEqual(TEXT("FromSettings reads the page"), P.Falloff, Page->GlowFalloff);
	TestEqual(TEXT("FromSettings reads the fixed half-size"), P.QueryFixedHalfInches, Page->SpriteQueryFixedHalfInches);
	TestEqual(TEXT("FromSettings reads the grid"), P.QueryGrid, Page->SpriteQueryGrid);

	// Size: a rendermode-3 corona is screen-constant -- `size x dist / 200` -- whatever the actor
	// scale; NoDissipation, WorldGlow and every plain mode keep the authored `size x 2.54` cm,
	// scaled with the actor.
	TestTrue(TEXT("mode 3 at 2000 cm: 64 x 2000 / 200 = 640 cm"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::WorldSizeCm(64.f, 3, 0, 2000.f, 16.f, P), 640.f, 1e-3f));
	TestTrue(TEXT("mode 3 doubles with the distance"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::WorldSizeCm(64.f, 3, 0, 4000.f, 1.f, P), 1280.f, 1e-3f));
	TestTrue(TEXT("mode 3 under NoDissipation keeps the world size"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::WorldSizeCm(64.f, 3, 14, 4000.f, 1.f, P), 64.f * 2.54f, 1e-3f));
	TestTrue(TEXT("mode 9 keeps the world size"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::WorldSizeCm(64.f, 9, 0, 4000.f, 1.f, P), 64.f * 2.54f, 1e-3f));
	TestTrue(TEXT("mode 5 keeps the world size and scales with the actor"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::WorldSizeCm(64.f, 5, 0, 4000.f, 16.f, P), 64.f * 2.54f * 16.f, 1e-3f));

	// Brightness: `clamp(19000 / dist_in^2, 0.05, 1)` for a glow mode, 1 under NoDissipation and
	// for every plain mode.
	TestTrue(TEXT("a glow 100 in away is 1.0 (19000/10000 clamped)"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::Brightness(100.f * 2.54f, 3, 0, P), 1.f, 1e-4f));
	TestTrue(TEXT("a glow 200 in away is 0.475"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::Brightness(200.f * 2.54f, 3, 0, P), 0.475f, 1e-4f));
	TestTrue(TEXT("a glow 200 in away in WorldGlow is the same 0.475"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::Brightness(200.f * 2.54f, 9, 0, P), 0.475f, 1e-4f));
	TestTrue(TEXT("a glow 2000 in away floors at 0.05"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::Brightness(2000.f * 2.54f, 3, 0, P), 0.05f, 1e-4f));
	TestTrue(TEXT("NoDissipation skips the distance term"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::Brightness(2000.f * 2.54f, 3, 14, P), 1.f, 1e-4f));
	TestTrue(TEXT("a plain additive sprite has no distance term"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::Brightness(2000.f * 2.54f, 5, 0, P), 1.f, 1e-4f));

	// Smoothing: 1/0.2 per second up, 1/0.1 per second down, never past the target.
	TestTrue(TEXT("rising: 0 -> 0.5 in 0.1 s"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::Smooth(0.f, 1.f, 0.1f, P), 0.5f, 1e-4f));
	TestTrue(TEXT("rising stops at the target"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::Smooth(0.f, 0.25f, 0.1f, P), 0.25f, 1e-4f));
	TestTrue(TEXT("falling: 1 -> 0.5 in 0.05 s"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::Smooth(1.f, 0.f, 0.05f, P), 0.5f, 1e-4f));
	TestTrue(TEXT("falling stops at the target"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::Smooth(1.f, 0.75f, 0.05f, P), 0.75f, 1e-4f));
	// The occlusion sample's footprint: VtMB scales the query quad with the distance for
	// rendermode 3 alone (100c25ed-100c25fa) and queries a fixed 3 Source units for every other
	// mode (10225158) -- the renderfx-14 test is separate and later (100c30e9), so a mode-3
	// NoDissipation corona still takes the screen-constant query, and it is the clamp to the drawn
	// card that keeps that sample off the wall behind a card that never grew with it.
	TestTrue(TEXT("mode 3's query square is 3/128 of the distance"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::QueryHalfSizeCm(1280.f, 3, 1000.f, P), 30.f, 1e-3f));
	TestTrue(TEXT("mode 9 queries VtMB's fixed 3 units"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::QueryHalfSizeCm(1280.f, 9, 1000.f, P), 3.f * 2.54f, 1e-3f));
	TestTrue(TEXT("a plain additive sprite queries the same fixed 3 units at any range"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::QueryHalfSizeCm(12800.f, 5, 1000.f, P), 3.f * 2.54f, 1e-3f));
	// A 64-unit NoDissipation card is 81.28 cm half-wide whatever the range; at 40 m the
	// screen-constant sample would be 93.75 cm and is clamped back into the card it gates.
	TestTrue(TEXT("the sample never outgrows the card it gates"),
		FMath::IsNearlyEqual(ElysiumSpriteGlow::QueryHalfSizeCm(4000.f, 3, 64.f * 2.54f * 0.5f, P), 81.28f, 1e-3f));

	// The tag contract the bake writes and `AdoptBakedLevel` buckets by.
	TArray<FName> Tags;
	Tags.Add(ElysiumBakedTags::Sprite);
	Tags.Add(ElysiumBakedTags::EntityIndex(309));
	TestEqual(TEXT("elysium.sprite"), ElysiumBakedTags::Sprite, FName(TEXT("elysium.sprite")));
	TestEqual(TEXT("elysium.ent parses"), ElysiumBakedTags::ParseEntityIndex(Tags), 309);
	const TArray<FName> Untagged = { ElysiumBakedTags::Prop };
	TestEqual(TEXT("a prop carries no entity index"), ElysiumBakedTags::ParseEntityIndex(Untagged), INDEX_NONE);

	// The actor's shape: the billboard is the root, static, uncollidable, shadowless.
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
	{
		TestWorld.ForwardErrorMessages(this);
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no test world for the actor shape"));
		return true;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AElysiumSpriteActor* Actor = World->SpawnActor<AElysiumSpriteActor>();
	if (Actor == nullptr)
	{
		AddError(TEXT("AElysiumSpriteActor did not spawn"));
		return false;
	}
	UElysiumSpriteComponent* Sprite = Actor->Sprite;
	TestTrue(TEXT("the sprite component is the root"), Actor->GetRootComponent() == Sprite);
	if (Sprite != nullptr)
	{
		TestEqual(TEXT("static mobility"), Sprite->Mobility, EComponentMobility::Static);
		TestEqual(TEXT("no collision"), Sprite->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("casts no shadow"), Sprite->CastShadow);
		TestFalse(TEXT("a fresh sprite is not a glow"), Sprite->IsGlow());
		Sprite->RenderMode = 3;
		TestTrue(TEXT("mode 3 is a glow"), Sprite->IsGlow());
		Sprite->RenderMode = 9;
		TestTrue(TEXT("mode 9 is a glow"), Sprite->IsGlow());
		Sprite->RenderMode = 5;
		Sprite->SizeInches = FVector2D(64.0, 32.0);
		const FBoxSphereBounds Fixed = Sprite->CalcBounds(FTransform::Identity);
		TestTrue(TEXT("a fixed card's bounds are its 2.54 cm size"),
			FMath::IsNearlyEqual(static_cast<float>(Fixed.SphereRadius), 64.f * 2.54f, 1e-2f));
		Sprite->RenderMode = 3;
		const FBoxSphereBounds Glow = Sprite->CalcBounds(FTransform::Identity);
		TestTrue(TEXT("a corona's bounds hold its quad at 200 m"),
			FMath::IsNearlyEqual(static_cast<float>(Glow.SphereRadius), 64.f * 20000.f * 0.005f, 1e-1f));
	}
	TestWorld.ForwardErrorMessages(this);
	return true;
}
} // namespace ElysiumSpriteTests

#endif // WITH_DEV_AUTOMATION_TESTS
