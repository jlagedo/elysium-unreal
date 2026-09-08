#pragma once

#include "CoreMinimal.h"

class UTexture2D;

// UI art from assets.
//
// Every image a screen draws is the `T_` asset the texture lane imported for the same install
// path the screen always named -- a `materials/` path without extension, exactly as the HUD
// tables, the sheet, the chargen data and the sign definitions spell it. Nothing here opens a
// file: the loose `ui/art`, `ui/menu`, `signs/tex` and `hud/use_icons.png` outputs are gone.
namespace ElysiumUI
{
	// `<dir>/<stem>` -> the imported `UTexture2D`, or null (logged Verbose) when neither the `T_`
	// of that path nor the `MI_` of that path exists. The path is normalised first: lower-cased,
	// backslashes folded, a leading `materials/` and a trailing `.png` dropped -- so authored data
	// (`Bkg_Image "Interface/Pop_Ups/Pop_Up_1"`) and the old sheet names both resolve.
	//
	// Two steps, both by install path. First the `T_` the path names directly
	// (`FElysiumContentPaths::BakedTexture`). Failing that, the `MI_` of the same path and its
	// `BaseTexture` -- the material lane's own VMT join, which is what tells `hud/disciplines/
	// bloodheal_hud` that its pixels are `bloodheal_base`'s. Callers cache the result, including
	// the miss; this function does not.
	UTexture2D* ArtTexture(const FString& MaterialPath);

	// The normalised key `ArtTexture` resolves (exposed for the tests and the caches).
	FString ArtKey(const FString& MaterialPath);

	// The `use_icon` enum's art: `hud/context_icons/<name>` for N in [1, 72] through
	// `ElysiumUseIconName`, with the two on-disk aliases the retired atlas compositor carried
	// (`Stakeable` -> `stakable`, `valve` -> `valvewheel`). Empty for 0 and out of range.
	FString UseIconArt(int32 N);

	// The idle reticle drawn around a use icon.
	inline const TCHAR* const UseRingArt = TEXT("hud/context_icons/context_icon_ring");

	// The clan sigil the character sheet flies and the menu rail watermarks:
	// `interface/charactermaintenance/cm_clan_symbol_<clan>` for VtMB's 2..8 clan encoding, empty
	// for 0/1 (no character yet) and anything out of range.
	FString ClanSigilArt(int32 Clan);
}
