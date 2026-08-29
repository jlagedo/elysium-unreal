#pragma once

#include "CoreMinimal.h"

// What a harness run may record, and how each value is compared.
//
// **A channel's comparison rule is part of its declaration**: the run publishes the table it used
// as a manifest beside its own output, and the differ reads the manifest rather than carrying a
// second copy. Registering a channel *is* registering its comparison, and a channel nothing
// declares cannot be written at all.
//
// This is the pure half — a static table and two lookups, no UObject and no filesystem, so it is
// asserted with no world (`Elysium.Substrate.ChannelRegistry`). `FElysiumChannelRecorder` is the
// engine half that fills it in. A new producer is a table edit and some writes, never a second
// format, and the differ does not change at all.

namespace ElysiumChannels
{
	// How a value is compared. The set is closed — a channel the differ cannot place in one of these
	// is a channel it refuses to accept — and it grows only when a value genuinely has a comparison
	// no existing kind performs, which is what `Angle` is.
	enum class EKind : uint8
	{
		Numeric,   // compared against an absolute tolerance in the channel's own unit
		Exact,     // compared for equality — a state flip is a behaviour change, never a rounding one
		// Degrees, compared as a **wrapped** difference. A yaw subtracted plainly reports ~360° at
		// the ±180 boundary, and a backpedalling body sits exactly on it — so a numeric yaw is the
		// silent-failure case this registry exists to close, running in reverse: a channel that is
		// checked and reddens for no reason.
		Angle,
	};

	// Where the value lives.
	enum class EScope : uint8
	{
		Frame,     // one value per frame, a column in the CSV
		Run,       // one value for the whole course, a field in the manifest
	};

	struct FChannelDef
	{
		const TCHAR* Name;      // unique across every producer; the CSV column or manifest field
		const TCHAR* Producer;  // "move", "camera", "anim" — who writes it
		EScope Scope;
		EKind Kind;
		// Numeric and Angle: the absolute tolerance, in `Unit`. Exact: zero, and unused.
		float Tolerance;
		// Decimals printed. Fixed per channel, which is what makes the text a deterministic function
		// of the value — a committed baseline that reformats between runs is a baseline that fails
		// for no reason.
		int8 Precision;
		const TCHAR* Unit;      // "u", "u/s", "deg", "s", "" — what the tolerance is measured in
		// True when the value moves if the speed authority moves. Every per-frame channel is one:
		// *when* a body reaches a feature depends on its gait even when *whether* it does not. A
		// committed baseline compares only the channels this is false for, so speed-invariant
		// thresholds are independent of the gait tables.
		bool bSpeedDependent;
		const TCHAR* Help;
	};

	TArrayView<const FChannelDef> Defs();
	const FChannelDef* Find(const TCHAR* Name);

	const TCHAR* KindName(EKind Kind);
	const TCHAR* ScopeName(EScope Scope);
}
