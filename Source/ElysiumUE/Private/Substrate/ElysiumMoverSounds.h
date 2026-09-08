#pragma once

#include "CoreMinimal.h"

// The `soundgroup` resolver — a token to the WAVs a door, switch or computer emits.
//
// Retail has no soundgroup index. `SndSchemeTables` (vampire.dll `FUN_101f66c0` @0x101f66c0)
// registers five tables, each with its own vdata vocabulary file and its own directory token:
//
//     SndScheme_Char      -> Female, Male, Monster, Animal
//     SndScheme_Wpn       -> Melee, Ranged, Ejection
//     SndScheme_Openable  -> Openable
//     SndScheme_Switch    -> Switches
//     SndScheme_Computer  -> Computers
//
// `FUN_101f5210` @0x101f5210 loads `vdata/system/<table>.txt` ("%s%s.txt") and `FUN_101f5390`
// @0x101f5390 parses it: `SoundSchemeTables/SoundScheme` yields `Name` (kept at table+0xc) and
// `InternalName` (table+0x8), and every `SoundList/Sound/Name` becomes one ordinal slot — that list
// IS the subkey vocabulary. All three usable tables author `"Name" "Usable"`, which is the only
// place the `usable/` component of the path comes from.
//
// `FUN_101f41b0` @0x101f41b0 then builds the directory as `sprintf("%s\%s", scheme.Name,
// categoryToken)` -> "Usable\Openable" / "Usable\Switches" / "Usable\Computers", and `FUN_101f3810`
// @0x101f3810 WALKS IT: it enumerates `sound\<dir>\*.*`, turning every file into a sound slot
// (matched to the vocabulary by stem) and recursing into every subdirectory as a child table. So
// the group names are the shipped directory names, verbatim — including the two that carry a space
// ("squeaky_metal door", "squeaky_wood door"). Note the token is `Openable`, never `doors`: the
// `sound/usable/doors/` tree the install also ships is unreachable from any of the five tables.
//
// Lookup is `FUN_101f39d0` @0x101f39d0: `Q_FixSlashes(token, '\')` then `__strcmpi` per path
// component. Case-insensitive, otherwise VERBATIM — retail tries no space/underscore variance, so
// neither do we (we fold case only, because the deployed corpus is all lower-case).
//
// `FUN_101f42a0` @0x101f42a0 is the miss arm: a token that names no child table returns the
// CATEGORY ROOT's own base index, i.e. the default sounds sitting directly under
// `usable/<category>/` (`open.wav`/`close.wav`/`locked.wav`/`swing.wav`, `on.wav`/`off.wav`,
// `accept.wav`/`access.wav`/`error.wav`/`typing.wav`). An unknown soundgroup is not silent.
//
// A subkey the group ships no file for stays empty and the cue is silent — retail's slot is simply
// never filled by the directory walk. `switches/elevator_button` ships `on.wav` and no `off.wav`.
//
// NPC `soundgroup`s (`Young_Thug` and friends) belong to SndScheme_Char, a different table with a
// different vocabulary and no `usable/` component. Nothing here is reachable without naming one of
// the three usable categories explicitly, so an NPC token can never be routed into `usable/`.
namespace ElysiumSoundGroups
{
	// The three categories a usable entity can draw from — the directory token retail registers,
	// lower-cased to match the deployed corpus.
	inline const TCHAR* Openable  = TEXT("openable");
	inline const TCHAR* Switches  = TEXT("switches");
	inline const TCHAR* Computers = TEXT("computers");

	// All three, in the registration order of FUN_101f66c0. For the debug browser.
	const TArray<FString>& Categories();

	// The category's subkey vocabulary, parsed once from the deployed
	// `vdata/system/sndscheme_{openable,switch,computer}.txt`. Empty if the file is not deployed.
	const TArray<FName>& Subkeys(const FString& Category);

	// Resolve one token: subkey -> sound-relative WAV, for every subkey the group actually ships.
	// Probed against the deployed corpus, cached per (category, group), and answered by reference so
	// a consumer can hold onto it. An unknown group falls back to the category root (FUN_101f42a0).
	const TMap<FName, FString>& Resolve(const FString& Category, const FString& Group);

	// The group directories shipped under `usable/<category>/`, for the Cog audio window. Not a
	// gameplay path: retail never enumerates groups, it only ever looks one up by name.
	TArray<FString> EnumerateGroups(const FString& Category);
}
