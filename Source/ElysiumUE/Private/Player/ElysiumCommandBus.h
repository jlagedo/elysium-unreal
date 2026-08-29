#pragma once

#include "CoreMinimal.h"

class FElysiumConsole;

// The one door a VtMB console line comes through. Everything that fires a verb by
// name — a bound key, a level script's `ccmd` attribute-set, a `.dlg` action, `elysium.cmd` from the
// UE console, an MCP `elysium_console_exec`, `-ExecCmds` — arrives here and gets the same
// precedence: **registered command -> alias -> cvar -> Python**.
//
// The console store lives on `FElysiumPythonVM` because that is what the `ccmd`/`cvar` object
// surface drives, and the VM singleton exists whether or not CPython is present. This wrapper is
// what lets a caller reach it without knowing that, and it seeds the alias/cvar tables from
// `out/cfg` on first use so the patch's aliases resolve even when the interpreter never came up.
namespace ElysiumCommandBus
{
	// The shared console store, seeded on first call.
	FElysiumConsole& Console();

	// Run one console line (`;`-separated statements allowed).
	void Exec(const FString& Line);

	// Split a **tap** line into the press line and the release line that complete one button press.
	//
	// A typed `+attack` latches until a typed `-attack`, exactly as retail does (`docs/vtmb/controls.md`
	// § "The model in one paragraph": a `+cmd` runs on key-down and its `-cmd` on key-up, and a console
	// line has no key-up). That is the faithful behaviour and it stays. A tap is the **debug** surface's
	// way of asking for the release a key would have produced: it is not a verb of its own, and it
	// resolves through the same registry, so an undeclared word or a `Once` verb is refused rather than
	// turned into a press nothing releases.
	//
	// The sign the caller wrote is ignored — a tap always presses and then releases — and any argument
	// tail rides on both lines, because a `+cmd` and its `-cmd` take the same arguments.
	bool ParseTap(const FString& Line, FString& OutPressLine, FString& OutReleaseLine);
}
