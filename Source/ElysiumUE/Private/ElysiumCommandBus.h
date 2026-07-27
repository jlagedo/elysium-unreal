#pragma once

#include "CoreMinimal.h"

class FElysiumConsole;

// The one door a VtMB console line comes through (roadmap 11.6). Everything that fires a verb by
// name — a bound key, a level script's `ccmd` attribute-set, a `.dlg` action, `elysium.cmd` from the
// UE console, an MCP `elysium_console_exec`, `-ExecCmds` — arrives here and gets the same
// precedence: **registered command -> alias -> cvar -> Python**.
//
// The console store lives on `FElysiumPythonVM` (9.3b) because that is what the `ccmd`/`cvar` object
// surface drives, and the VM singleton exists whether or not CPython is present. This wrapper is
// what lets a caller reach it without knowing that, and it seeds the alias/cvar tables from
// `out/cfg` on first use so the patch's aliases resolve even when the interpreter never came up.
namespace ElysiumCommandBus
{
	// The shared console store, seeded on first call.
	FElysiumConsole& Console();

	// Run one console line (`;`-separated statements allowed).
	void Exec(const FString& Line);
}
