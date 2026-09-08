#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

// VtMB's default bind set, as `FKey` -> console command string. The Enhanced Input mapping
// contexts project this table. It is `cfg/default.cfg` from the Unofficial Patch,
// which is the set that ships (`docs/vtmb/controls.md` § "Default bindings"):
// the patch's arrow/comma-period swap, its ten `vhotkey` slots and its numpad camera verbs, with
// `vphysicshand` dropped and the `kb_def.lst` disagreements resolved to `default.cfg`.
//
// Commands are strings, not enum values, on purpose: several patch defaults bind an **alias**
// (`vm_feed`, `skip`, `cam_restore`) rather than a compiled verb, and a key bound to either has to
// behave identically. Everything here goes through `FElysiumConsole::Execute`, which is what makes
// that true (registered command -> alias -> cvar -> Python).

struct FElysiumDefaultBind
{
	FKey Key;
	// The console line the key fires. A leading `+` marks a press/release pair: the router binds the
	// release to the matching `-cmd` and nothing else needs to know.
	const TCHAR* Command;
	// The VtMB keyname this row reproduces, for the `config.cfg` projection and the dump.
	const TCHAR* VtmbKey;
};

namespace ElysiumBinds
{
	// Built on first call: `EKeys::*` are runtime statics, so the table cannot be a constexpr array.
	const TArray<FElysiumDefaultBind>& Defaults();

	// The bare keys the **development layer** occupies, which no default bind and no player rebind
	// may take. Everything else the dev layer uses
	// is a chord — Cog's layout save/load menu items are `Ctrl+F1`–`Ctrl+F4`, and Elysium's own dev
	// toggles are `Ctrl+V` (noclip) and `Ctrl+T` (the 3D-skybox A/B), precisely so `v` and `t` stay
	// the player's.
	//
	// The console holds two of them — `` ` `` and `F7` — because `` ` `` is not on every physical
	// layout, and F7 is the one function key neither this table nor `default.cfg` claims. `F1` is
	// the third: Cog's own shell toggle is bare `F1` (no modifier), read inside Cog's input handling
	// ahead of this table, so it cannot share the key with a game verb the way the layout chords
	// share the Ctrl modifier.
	//
	// Distinct from *non-rebindable*: `ESCAPE` and `` ` `` are bound by `default.cfg` but appear in
	// no `kb_act.lst`, so the player keeps `cancelselect` and `toggleconsole` and cannot move them.
	const TArray<FKey>& ReservedKeys();
	bool IsReserved(const FKey& Key);
}
