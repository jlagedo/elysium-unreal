// Elysium.Substrate.UiStrings -- 0018 story 21-6's reader flip, and the two grammar facts it turns
// on.
//
// Until 21-6 the runtime read a flat `strings.json` an offline extractor derived from the install
// with one regex. It reads the install's own `resource/gameui_english.txt` now, deployed verbatim
// by `uv run elysium import ui-strings` -- a Valve KeyValues document, UTF-16 LE with a byte-order
// mark -- and parses it with the runtime's own reader. Two things had to be got right for that to
// be the same answer: keys fold (Source KeyValues is case-insensitive, and `Resolve` used to
// compare them raw), and a repeated token takes the LAST value, which is how the shipped
// `GameUI_Advanced` resolves to "Advanced" rather than "Advanced...".
//
// Content-free: the singleton loads once per process and cannot be re-pointed after another case
// has touched it, so these drive `ParseTable` with text of their own. `Elysium.Content.UiStrings`
// below reads the deployed file.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "UI/ElysiumUIStrings.h"

#include "HAL/FileManager.h"

static constexpr EAutomationTestFlags GElysiumUiStringsTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The shape the install ships, in miniature: a `lang` root with `Language` beside the `Tokens`
	// block, one token from each of the two namespaces, and `GameUI_Advanced` authored twice.
	const TCHAR* const GTable =
		TEXT("\"lang\"\r\n{\r\n\"Language\" \"English\"\r\n\"Tokens\"\r\n{\r\n")
		TEXT("\"VMainMenu_BTN_NewGame\" \"New Game\"\r\n")
		TEXT("\"GameUI_Advanced\" \"Advanced...\"\r\n")
		TEXT("\"GameUI_Advanced\" \"Advanced\"\r\n")
		TEXT("}\r\n}\r\n");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUiStringsTest,
	"Elysium.Substrate.UiStrings", GElysiumUiStringsTestFlags)
bool FElysiumUiStringsTest::RunTest(const FString&)
{
	const TMap<FString, FString> Table = FElysiumUIStrings::ParseTable(FString(GTable));

	// --- what the table holds -----------------------------------------------------------------
	TestEqual(TEXT("both namespaces parse into one table"), Table.Num(), 2);
	TestEqual(TEXT("a main-menu label resolves"),
		Table.FindRef(TEXT("vmainmenu_btn_newgame")), FString(TEXT("New Game")));

	// A repeated token is last-wins, which is what the engine's own KeyValues does and what the
	// shipped file needs: `GameUI_Advanced` is authored "Advanced..." then "Advanced".
	TestEqual(TEXT("a repeated token takes the last value"),
		Table.FindRef(TEXT("gameui_advanced")), FString(TEXT("Advanced")));

	// `Language` is a leaf of the `lang` root, not of `Tokens`, so reading the block rather than
	// the root is what keeps it out -- no name-based exclusion list, and a token that happened to
	// be called `Language` would still resolve.
	TestFalse(TEXT("the document's own Language leaf is not a token"),
		Table.Contains(TEXT("language")));

	// --- the grammar's edges ------------------------------------------------------------------
	TestEqual(TEXT("a document with no lang/Tokens block yields nothing"),
		FElysiumUIStrings::ParseTable(TEXT("\"Scheme\"\r\n{\r\n\"Colors\"\r\n{\r\n}\r\n}\r\n")).Num(), 0);
	TestEqual(TEXT("empty text yields nothing"),
		FElysiumUIStrings::ParseTable(FString()).Num(), 0);

	// An authored empty string is a value, not an absence: retail draws it as empty rather than
	// falling back, so the table must carry it.
	const TMap<FString, FString> Empty = FElysiumUIStrings::ParseTable(
		TEXT("\"lang\"\r\n{\r\n\"Tokens\"\r\n{\r\n\"GameUI_Blank\" \"\"\r\n}\r\n}\r\n"));
	TestTrue(TEXT("an authored empty value is present"), Empty.Contains(TEXT("gameui_blank")));

	// --- resolution folds its token ------------------------------------------------------------
	// The table is keyed folded because Source KeyValues is case-insensitive; a caller naming the
	// authored spelling must still hit, and a miss must reach the built-in English rather than
	// showing the raw token (`FUN_10065eb0`'s hardcoded fallback table).
	const FElysiumUIStrings& Live = FElysiumUIStrings::Get();
	TestEqual(TEXT("an unknown token falls back to the built-in English"),
		Live.Resolve(TEXT("VMainMenu_BTN_NoSuchTokenExists"), TEXT("Continue")).ToString(),
		FString(TEXT("Continue")));

	return true;
}

// The content tier: the deployed file itself. Abstains when the lane has not been run, exactly as
// the other Content cases do, so a fresh checkout stays green without being reported as covered.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumContentUiStringsTest,
	"Elysium.Content.UiStrings", GElysiumUiStringsTestFlags)
bool FElysiumContentUiStringsTest::RunTest(const FString&)
{
	const FString Path = FElysiumContentPaths::UiStrings();
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: no deployed UI string table at %s. "
			     "Run: uv run elysium import ui-strings"), *Path));
		return true;
	}

	const FElysiumUIStrings& Strings = FElysiumUIStrings::Get();
	if (!TestTrue(TEXT("the deployed table loaded"), Strings.IsLoaded()))
	{
		return false;
	}
	// 194 distinct tokens across 195 authored rows -- `GameUI_Advanced` is the one repeat. Asserted
	// as a floor rather than an equality so a patch that adds a string does not fail the suite.
	TestTrue(TEXT("the table carries the shipped token set"), Strings.Num() >= 194);
	TestEqual(TEXT("the main menu's New Game label resolves off the deployed file"),
		Strings.Resolve(TEXT("VMainMenu_BTN_NewGame"), TEXT("<fallback>")).ToString(),
		FString(TEXT("New Game")));
	TestEqual(TEXT("the repeated token resolves to its last authored value"),
		Strings.Resolve(TEXT("GameUI_Advanced"), TEXT("<fallback>")).ToString(),
		FString(TEXT("Advanced")));
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
