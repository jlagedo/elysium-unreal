#pragma once

#include "CoreMinimal.h"

// VtMB's authored UI string table, read verbatim from the user's install (`resource/gameui_english.txt`
// mirrored to `out/ui/strings.json`). Every player-facing string in the UI comes from here — the
// reconstruction re-skins the craft, never the content (`docs/project/reconstruction-direction.md` axis 1).
//
// Two namespaces live in the one table and the distinction is load-bearing: `#GameUI_*` skins
// `GameUI.dll`'s dialogs, while the main menu's own labels are **`VMainMenu_BTN_*`** tokens
// (`docs/vtmb/vtmb-ui.md` §2). Resolution mirrors `CVMainMenu`'s: look the token up, and on a miss
// fall back to a built-in English default rather than showing the raw token — which is exactly
// what `FUN_10065eb0` does with its hardcoded fallback table.
class FElysiumUIStrings
{
public:
	// Loads `out/ui/strings.json` once. Safe to call repeatedly; a failed load is not retried.
	static FElysiumUIStrings& Get();

	// Token -> authored string. `Fallback` is used when the table is absent or lacks the token;
	// pass the retail English text so a missing mirror degrades to the shipped wording.
	FText Resolve(const TCHAR* Token, const TCHAR* Fallback) const;

	bool IsLoaded() const { return bLoaded; }
	int32 Num() const { return Table.Num(); }

private:
	void LoadOnce();

	TMap<FString, FString> Table;
	bool bLoaded = false;
	bool bTried = false;
};
