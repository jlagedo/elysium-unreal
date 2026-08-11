#pragma once

#include "CoreMinimal.h"

class FElysiumEntityWorld;

// The dump half of the per-wire accounting instrument (`docs/architecture/gameplay-systems-architecture.md`
// §7). The tally itself lives on the entity world and is readable without any of this; what is here
// is the artifact an acceptance run leaves behind — one JSON file per map, holding every authored
// wire whether or not it ever did anything, which is the input the offline joiner reads against the
// research inventory.
namespace ElysiumWireDump
{
	// The report as JSON text. Pure: no file, no log, no world mutation, so a test can assert on the
	// string.
	FString BuildJson(const FElysiumEntityWorld& World);

	// Write the report under the export root's `_wires/` (the same place `_profile`, `_lights` and
	// `_greenroom` put their artifacts, which is below ELYSIUM_WORK_ROOT unless the export root is
	// pointed elsewhere). Returns the path written, or empty on failure — including the case where
	// no content root is configured at all.
	FString Write(const FElysiumEntityWorld& World);

	// The console line: total authored wires, fired, fully delivered, never fired, unknown target,
	// unknown input. Shared with the MCP tool so both surfaces say the same thing.
	FString Summarize(const FElysiumEntityWorld& World);
}
