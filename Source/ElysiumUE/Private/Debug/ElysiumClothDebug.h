#pragma once

#include "CoreMinimal.h"

class IConsoleVariable;

/**
 * Chaos's own cloth visualisers, as one named set.
 *
 * A garment that is not where it should be has several causes that look identical in the
 * viewport — the pose it is skinned from is wrong, nothing is holding it on the body, nothing is
 * limiting how far it may travel, or it is passing through its collision. Each overlay draws
 * exactly one of those, so the diagnosis is which one disagrees with the body.
 *
 * The set exists here rather than in the panel that draws it because the console verb
 * `elysium.cloth` reads the same table, and a name that only one of the two knows is a name the
 * other silently ignores.
 *
 * The underlying cvars are registered by the `ChaosCloth` module, are `ECVF_Cheat`, and are
 * compiled out with `CHAOS_DEBUG_DRAW` in Shipping — so a null lookup means unavailable here, not
 * off.
 */
namespace ElysiumClothDebug
{
	/** One `p.ChaosCloth.DebugDraw*` toggle, named for what it answers rather than what it draws. */
	struct FDrawToggle
	{
		/** The cvar name after `p.ChaosCloth.DebugDraw`, and the verb's own keyword for it. */
		const TCHAR* Suffix;
		/** The panel's label. */
		const char* Label;
		/** What seeing it, or not seeing it, tells you. */
		const char* Help;
	};

	/** The curated set, in diagnosis order rather than alphabetically. */
	TConstArrayView<FDrawToggle> DrawToggles();

	/** The cvar behind one toggle, or null where the module or the build does not carry it. */
	IConsoleVariable* DrawCVar(const TCHAR* Suffix);

	/** How many of the set are currently on. */
	int32 NumActiveDraws();

	/** Turn the whole set off. */
	void ClearDraws();

	/**
	 * Switch one overlay on or off — and this, not the cvar, is what every caller should use.
	 *
	 * Chaos gates all of its debug drawing behind one master switch, `p.Chaos.DebugDraw.Enabled`,
	 * which is off at every launch. An overlay set while that is off is genuinely set and draws
	 * nothing, so the panel reads as broken to anyone who has not also typed the master into the
	 * console. Switching an overlay on switches that on too.
	 */
	void SetDraw(const TCHAR* Suffix, bool bOn);

	/** Whether Chaos is drawing at all. Off means no overlay can appear, whatever it is set to. */
	bool IsDrawEnabled();

	/** Arm or disarm Chaos's master draw switch. */
	void SetDrawEnabled(bool bEnabled);
}
