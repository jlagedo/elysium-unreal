// Elysium.Substrate.UiArt -- R6.6's path -> asset resolution (docs/architecture/ui-architecture.md
// -> "9. Art from assets"): the key a screen names folds to the texture lane's `T_` package by the
// same rule that named it at import, the use-icon enum folds to its `hud/context_icons/` art with
// the two on-disk aliases, and the clan sigil table is total over VtMB's 2..8 encoding. Content-
// free: nothing here loads an asset; `Elysium.Content.UiArt` walks the real mount.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumUseIcons.h"
#include "UI/ElysiumUiArt.h"

static constexpr EAutomationTestFlags GElysiumUiArtTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUiArtTest, "Elysium.Substrate.UiArt", GElysiumUiArtTestFlags)
bool FElysiumUiArtTest::RunTest(const FString&)
{
	// --- the fold: <dir>/<stem> -> /ElysiumBaked/Textures/<dir>/T_<safe stem> ---
	TestEqual(TEXT("HUD art path -> T_ object path"),
		FElysiumContentPaths::BakedTexture(TEXT("hud/area_icons/area_icon_combat")),
		FString(TEXT("/ElysiumBaked/Textures/hud/area_icons/T_area_icon_combat.T_area_icon_combat")));
	TestEqual(TEXT("nested inventory path keeps its folders"),
		FElysiumContentPaths::BakedTexture(TEXT("hud/inventory_images/weapons_melee/tire_iron")),
		FString(TEXT("/ElysiumBaked/Textures/hud/inventory_images/weapons_melee/T_tire_iron.T_tire_iron")));
	TestEqual(TEXT("a bare stem lands at the root"),
		FElysiumContentPaths::BakedTexture(TEXT("white")),
		FString(TEXT("/ElysiumBaked/Textures/T_white.T_white")));
	TestEqual(TEXT("safe_name folds a run of illegal characters"),
		FElysiumContentPaths::BakedTexture(TEXT("interface/pop_ups/pop-up 1")),
		FString(TEXT("/ElysiumBaked/Textures/interface/pop_ups/T_pop_up_1.T_pop_up_1")));
	TestTrue(TEXT("an empty key folds to no path"),
		FElysiumContentPaths::BakedTexture(TEXT("")).IsEmpty());

	// --- the key a screen names is normalised before the fold ---
	TestEqual(TEXT("authored spelling: case, backslashes, materials/ prefix"),
		ElysiumUI::ArtKey(TEXT("materials\\Interface\\Pop_Ups\\Pop_Up_1")),
		FString(TEXT("interface/pop_ups/pop_up_1")));
	TestEqual(TEXT("a legacy sheet name drops its .png"),
		ElysiumUI::ArtKey(TEXT("interface/charactermaintenance/cm_divider.png")),
		FString(TEXT("interface/charactermaintenance/cm_divider")));
	TestTrue(TEXT("nothing in, nothing out"), ElysiumUI::ArtKey(TEXT("")).IsEmpty());

	// --- the use-icon enum -> hud/context_icons/<name>, with the compositor's two aliases ---
	TestEqual(TEXT("use_icon 1 is CarryBody"),
		ElysiumUI::UseIconArt(1), FString(TEXT("hud/context_icons/carrybody")));
	TestEqual(TEXT("use_icon 9 is PhysicsHand"),
		ElysiumUI::UseIconArt(9), FString(TEXT("hud/context_icons/physicshand")));
	TestEqual(TEXT("use_icon 11 Stakeable is filed as stakable"),
		ElysiumUI::UseIconArt(11), FString(TEXT("hud/context_icons/stakable")));
	TestEqual(TEXT("use_icon 65 valve is filed as valvewheel"),
		ElysiumUI::UseIconArt(65), FString(TEXT("hud/context_icons/valvewheel")));
	TestEqual(TEXT("use_icon 72 is push"),
		ElysiumUI::UseIconArt(72), FString(TEXT("hud/context_icons/push")));
	TestTrue(TEXT("use_icon 0 is no icon"), ElysiumUI::UseIconArt(0).IsEmpty());
	TestTrue(TEXT("use_icon 73 is out of the table"), ElysiumUI::UseIconArt(73).IsEmpty());
	// Every slot of the 72 names an art path, and the duplicates (42/43, 49/56, 54/55) agree.
	int32 Named = 0;
	for (int32 N = 1; N <= 72; ++N)
	{
		if (ElysiumUI::UseIconArt(N).StartsWith(TEXT("hud/context_icons/")))
		{
			++Named;
		}
	}
	TestEqual(TEXT("all 72 slots name context-icon art"), Named, 72);
	TestEqual(TEXT("duplicate slots 42/43 share art"), ElysiumUI::UseIconArt(42), ElysiumUI::UseIconArt(43));
	TestEqual(TEXT("duplicate slots 49/56 share art"), ElysiumUI::UseIconArt(49), ElysiumUI::UseIconArt(56));
	TestEqual(TEXT("duplicate slots 54/55 share art"), ElysiumUI::UseIconArt(54), ElysiumUI::UseIconArt(55));

	// --- the clan sigil table ---
	TestEqual(TEXT("clan 2 is Brujah"),
		ElysiumUI::ClanSigilArt(2), FString(TEXT("interface/charactermaintenance/cm_clan_symbol_brujah")));
	TestEqual(TEXT("clan 8 is Ventrue"),
		ElysiumUI::ClanSigilArt(8), FString(TEXT("interface/charactermaintenance/cm_clan_symbol_ventrue")));
	TestTrue(TEXT("no clan yet flies nothing"), ElysiumUI::ClanSigilArt(0).IsEmpty());
	TestTrue(TEXT("clan 1 is unused"), ElysiumUI::ClanSigilArt(1).IsEmpty());
	TestTrue(TEXT("clan 9 is out of range"), ElysiumUI::ClanSigilArt(9).IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
