#pragma once

#include "CoreMinimal.h"

class FElysiumNpc;

// The NPC combat loadout: turning an authored equipment keyfield into a carried, active weapon.
//
// It is a free function over `FElysiumNpc&` for the same reason the enemy transaction is — it owns
// no state. Everything it reads is already on the NPC (the authored keyfields) or in the installed
// item catalogue, and everything it writes is the character's own inventory. That is what makes a
// whole spawn loadout drivable from a content-free test with a fabricated catalogue.
//
// The authored surface (`docs/vtmb/npc-ai-reverse-engineering.md` -> "Common property surface"):
// `additionalequipment` (267 rows), `alternateequipment` (184) and `cantdropweapons` (78). The
// exported corpus states their shape exactly: `additionalequipment` is ONE classname per row, never
// a list, and `"0"` is the authored "none" sentinel that 78 of those rows carry.

namespace ElysiumNpcLoadout
{
	// The unarmed default. `item_w_fists` is a real `weapon_melee` record with an `Attack` mode —
	// the record that actually punches — and 18 authored rows name it explicitly. `item_w_unarmed`
	// is NOT it: that record's `item_type` is `hidden` and it authors no `Activation` block at all.
	inline const TCHAR* FistsClassname = TEXT("item_w_fists");

	// The authored "no additional equipment" sentinel. It is the literal `0`, not an empty string:
	// an absent key and a `0` mean the same thing and both reach the fists fallback.
	bool IsNoneSentinel(const FString& Authored);

	enum class EResult : uint8
	{
		Authored,       // the authored classname was granted and made active
		Fallback,       // nothing authored (or it did not resolve); the fists record armed the NPC
		Unarmed,        // no catalogue row for the fists either — the marked bare-hands path
		AlreadyArmed,   // this NPC already holds an active weapon; nothing was granted
		NoWorld,        // no entity world to create an item in
	};
	const TCHAR* ResultName(EResult Result);

	/**
	 * Resolve `Npc`'s loadout once.
	 *
	 * Grants the authored weapon and makes it the active one, falling back to the fists record. An
	 * NPC the catalogue cannot arm stays unarmed, which is a supported state rather than a failure:
	 * `ElysiumNpcCond::WeaponCapability` reports `Unarmed`, combat selection takes the melee branch
	 * with bare-hands defaults, and the attack tasks fail by name because there is no controller to
	 * press. The two causes are distinguished in the report — a world with no catalogue installed at
	 * all is the headless/fabricated case and is logged as such; a catalogue that is installed and
	 * has no fists row is a data problem and warns.
	 *
	 * NOT called from `Spawn`. Runtime item creation during the world's range-based spawn pass would
	 * invalidate the entity array mid-iteration, which is the same reason `FElysiumItemContainer`
	 * materialises its own equip seeds from a one-shot think.
	 */
	EResult Resolve(FElysiumNpc& Npc);
}
