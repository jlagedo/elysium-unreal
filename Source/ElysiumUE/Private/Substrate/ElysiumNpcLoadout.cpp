#include "Substrate/ElysiumNpcLoadout.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumRulebook.h"

bool ElysiumNpcLoadout::IsNoneSentinel(const FString& Authored)
{
	const FString Trimmed = Authored.TrimStartAndEnd();
	return Trimmed.IsEmpty() || Trimmed == TEXT("0");
}

const TCHAR* ElysiumNpcLoadout::ResultName(EResult Result)
{
	switch (Result)
	{
	case EResult::Authored:     return TEXT("authored");
	case EResult::Fallback:     return TEXT("fists fallback");
	case EResult::Unarmed:      return TEXT("unarmed");
	case EResult::AlreadyArmed: return TEXT("already armed");
	case EResult::NoWorld:      return TEXT("no world");
	}
	return TEXT("unknown");
}

namespace
{
	// Grant `Classname` and make it this character's active weapon.
	//
	// `GiveNamedItem` already runs the item through `Weapon_Equip`, but that route's active-weapon
	// switch is gated on the record's `is_wieldable` — a key none of the shipped `item_w_*` weapon
	// records authors — so the grant alone leaves an NPC carrying a gun it is not holding. The
	// explicit switch is what an authored `additionalequipment` means: the weapon this NPC is
	// spawned WIELDING. Nothing outside this path changes; the key's real retail meaning is
	// unrecovered and the player's own equip route is untouched.
	bool GrantAndWield(FElysiumNpc& Npc, const FString& Classname)
	{
		const FElysiumItemDef* Record = ElysiumItems::Find(Classname);
		if (Record == nullptr)
		{
			return false;
		}
		if (!Record->IsControllableWeapon())
		{
			// The classname resolves to a real item that is not one of the three wielded families.
			// Arming an NPC with it would give it a capability answer its record cannot support, so
			// it is refused by name rather than granted and quietly ignored.
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s authored equipment '%s' is a '%s' item, not a wielded weapon — not equipped"),
				*Npc.DebugString(), *Classname, ElysiumItemTypeName(Record->Type));
			return false;
		}
		const FElysiumEntityHandle Granted = Npc.Inventory.GiveNamedItem(Npc, Classname);
		FElysiumEntity* Created = Npc.World ? Npc.World->Resolve(Granted) : nullptr;
		FElysiumItem* Item = Created != nullptr ? Created->AsItem() : nullptr;
		if (Item == nullptr)
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s could not be granted its authored weapon '%s'"),
				*Npc.DebugString(), *Classname);
			return false;
		}
		if (!Npc.Inventory.SetActiveWeapon(Npc, *Item))
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s carries '%s' but could not make it the active weapon"),
				*Npc.DebugString(), *Classname);
			return false;
		}
		return true;
	}
}

ElysiumNpcLoadout::EResult ElysiumNpcLoadout::Resolve(FElysiumNpc& Npc)
{
	if (Npc.World == nullptr)
	{
		return EResult::NoWorld;
	}
	if (Npc.Inventory.Active(Npc) != nullptr)
	{
		// A restore rebuilt the inventory from the item entities' own owner fields, or a script
		// already handed this NPC something. Either way the loadout is not the authority any more.
		return EResult::AlreadyArmed;
	}

	// SEAM (parsed, unread): `alternateequipment` (184 authored rows). The recovered material names
	// the keyfield and nothing that CHOOSES between the two — no decoded body states whether the
	// alternate is a difficulty variant, a squad-role split or a random pick — so it is carried on
	// the leaf and never selected. Picking one would be inventing the rule, not reproducing it.
	//
	// SEAM (named): the stat template's own equipment package. `npctemplate*.txt` authors
	// `Starting_Equipment` as a name (`NPCGeneric`, `Civilian`, `Mercenary`, ...) resolving against
	// the `Starting_Equipment_Tables` block of `vdata/system/items.txt`, whose `StartingEquip`
	// records carry an `Items { "Item" ... }` list. `FElysiumItemTable` parses the per-item files
	// and not that block, so the package name is available on the resolved template
	// (`FElysiumClanTemplate::TraitStr("Starting_Equipment")`) with nothing to resolve it against.
	// The keyfield below is what the 267 authored NPC rows actually vary, and it is what a fight
	// needs; the package table joins here when the rulebook carries it.
	const FString& Authored = Npc.AdditionalEquipment;
	if (!IsNoneSentinel(Authored) && GrantAndWield(Npc, Authored.TrimStartAndEnd()))
	{
		return EResult::Authored;
	}

	if (GrantAndWield(Npc, FistsClassname))
	{
		return EResult::Fallback;
	}

	// Nothing armed this NPC. The two causes are different facts and are reported differently.
	if (ElysiumItems::Table() == nullptr)
	{
		// No catalogue installed at all: a headless substrate world or a fabricated rulebook. This
		// is a supported state, not a failure, and it is what an NPC in a Substrate-tier fixture
		// runs in.
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("%s stays unarmed: no `vdata/items` catalogue is installed. Combat selection takes "
				 "the melee branch with bare-hands defaults and its attack tasks fail by name"),
			*Npc.DebugString());
	}
	else
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s stays unarmed: the installed item catalogue carries no '%s' record and its "
				 "authored equipment '%s' did not resolve"),
			*Npc.DebugString(), FistsClassname,
			Authored.IsEmpty() ? TEXT("(none)") : *Authored);
	}
	return EResult::Unarmed;
}
