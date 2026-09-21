#pragma once

#include "CoreMinimal.h"

// VtMB's authored UI string table, read verbatim from the install's own
// `resource/gameui_english.txt` as `uv run elysium import ui-strings` deploys it into the corpus
// (`FElysiumContentPaths::UiStrings()`): a Valve KeyValues document, UTF-16 LE with a byte-order
// mark. Every player-facing string in the UI comes from here — the reconstruction re-skins the
// craft, never the content.
//
// Two namespaces live in the one table and the distinction is load-bearing: `#GameUI_*` skins
// `GameUI.dll`'s dialogs, while the main menu's own labels are **`VMainMenu_BTN_*`** tokens
// (`docs/vtmb/vtmb-ui.md` §2). Resolution mirrors `CVMainMenu`'s: look the token up, and on a miss
// fall back to a built-in English default rather than showing the raw token — which is exactly
// what `FUN_10065eb0` does with its hardcoded fallback table.
class FElysiumUIStrings
{
public:
	// Loads and parses the table once. Safe to call repeatedly; a failed load is not retried.
	static FElysiumUIStrings& Get();

	// The parse on its own: the `Tokens` block of a `lang` document, folded and last-wins. Exposed
	// because the singleton above loads once per process and cannot be re-pointed afterwards, so a
	// test drives the grammar here with text of its own. Empty when the document has no such block.
	static TMap<FString, FString> ParseTable(const FString& Text);

	// Token -> authored string, case-insensitively: Source KeyValues folds its keys, so the table is
	// keyed folded and this folds `Token` to match. `Fallback` is used when the table is absent or
	// lacks the token; pass the retail English text so a missing deploy degrades to the shipped
	// wording.
	FText Resolve(const TCHAR* Token, const TCHAR* Fallback) const;

	bool IsLoaded() const { return bLoaded; }
	int32 Num() const { return Table.Num(); }

private:
	void LoadOnce();

	TMap<FString, FString> Table;
	bool bLoaded = false;
	bool bTried = false;
};
