#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "Substrate/ElysiumRelationships.h"

#if !UE_BUILD_SHIPPING

class FElysiumEntityWorld;
class FElysiumNpc;
class UElysiumGameStateSubsystem;

// Standing characters up in a room and outfitting the player, for the combat arena.
//
// **Every operation here goes through a door the game already uses.** A character is created with
// `FElysiumEntityWorld::SpawnRuntimeEntity` — the fused create/spawn/activate `npc_maker` calls — and
// carries nothing but ordinary authored keyfields, so the leaf's own `Spawn`, `Activate`, sheet
// seeding, loadout resolution and mind admission all run exactly as they do for a map-authored NPC.
// Hostility is written into `FElysiumRelationships`, the store `SeedPlayerRelationship` writes.
// Weapons are `FElysiumInventory::GiveNamedItem`, which is `GiveNamedItem(classname, 0)`.
//
// Nothing in this file reaches around a system to make the arena look better than the game. A
// spawned character that will not fight is a finding, not a bug in this file — which is the only
// posture that makes a playtest room worth anything.

namespace ElysiumArenaCast
{
	// --- The catalogs a picker draws from ---------------------------------------------------------

	// Every character body the `/ElysiumBaked` mount actually carries. A stem the export has not
	// covered is excluded rather than offered and refused: `ElysiumNpcVisual::LoadMesh` fails by
	// name for one, so a body that cannot stand should not be in the list.
	TArray<FString> BodyStems();

	// The `npctemplate*.txt` stat blocks, by authored template name. This is what a spawned
	// character's `stattemplate` key selects, and it is the difference between a body with a health
	// ceiling, soak, damage filters and a Kindred classification and one with a zeroed sheet whose
	// damage path stays fail-closed.
	TArray<FString> StatTemplates(UElysiumGameStateSubsystem* GameState);

	struct FItemOption
	{
		FString Classname;      // the `vdata/items` file basename — the entity classname
		FString PrintName;
		FString TypeName;       // `Weapon_Melee`, `Weapon_Firearm`, `Weapon_Thrown`, `Ammo`
		FString AmmoType;       // the magazine's `Type`, for a firearm; empty otherwise
		bool bWieldable = false;
		// Carried as a flag rather than left to a caller comparing `TypeName`: the type names are
		// `system/items.txt`'s own capitalization, and a picker filtering on a lower-cased guess
		// would silently offer ammunition as a weapon.
		bool bAmmo = false;
	};

	// Every controllable weapon and every ammunition record, from the parsed item catalog. Policy
	// comes from `FElysiumItemDef::IsControllableWeapon`, never from an `item_w_` prefix — the
	// prefix is a filename convention and nothing in the game reads it as a type.
	TArray<FItemOption> WeaponCatalog(UElysiumGameStateSubsystem* GameState);

	// --- Standing a character up ------------------------------------------------------------------

	struct FSpawnRequest
	{
		// Which registered `npc_*` leaf. `npc_VHumanCombatant` is the arena's default because it is
		// the ordinary armed humanoid: it takes the weapon-capability split in
		// `CNPC_VHuman::SelectSchedule` and therefore reaches the melee and ranged combat families.
		FString Classname = TEXT("npc_VHumanCombatant");
		FString Model;                    // a body stem from `BodyStems`
		FString StatTemplate;             // a name from `StatTemplates`
		FString TargetName;               // empty asks for a generated `arena_npc_<n>`
		FString Disposition = TEXT("Neutral");
		// `additionalequipment` — the loadout join resolves it into a carried, active weapon
		// (`Substrate/ElysiumNpcLoadout.h`). Empty leaves the character unarmed, which takes the
		// melee selector with bare-hands defaults and whose attack tasks then fail by name.
		FString Weapon;

		FVector Origin = FVector::ZeroVector;
		float Yaw = 0.0f;

		// Written as `player_reaction`, the key `SeedPlayerRelationship` parses at Activate. Hate at
		// a high priority is what makes a character actually select a combat schedule against the
		// player; Neutral stands one up to watch it idle.
		EElysiumRelationship PlayerReaction = EElysiumRelationship::Hate;
		int32 PlayerReactionPriority = 99;

		// `npc_perception`, the authored channel `InitPerceptionDistances` derives vision and hearing
		// from. 3 is `AVERAGE_HUMAN_INSPECTION`, the row the sound table's own comment names.
		int32 Perception = 3;
		// `pl_criminal_attack` and friends default to the authored-disable 6. The arena leaves them
		// alone: law witnessing is a separate lane from combat hostility, and turning it on here
		// would make every spawn a police test.
		bool bAllowAlertLookaround = true;
	};

	// Create, spawn and activate one character. Returns its handle, or an invalid handle with
	// `OutError` set. The world must be ACTIVE — a dormant world defers `Activate`, and a character
	// whose mind never armed never thinks.
	FElysiumEntityHandle Spawn(FElysiumEntityWorld& World, const FSpawnRequest& Request,
		FString& OutError);

	// Retarget one live character at the player, after the fact. It writes the same relationship
	// store the authored `player_reaction` seeds, at a priority that outranks it.
	bool SetPlayerReaction(FElysiumEntityWorld& World, FElysiumNpc& Npc,
		EElysiumRelationship Value, int32 Priority);

	// Kill every character this arena spawned. `Handles` is emptied. Killing is the ordinary
	// terminal path (`FElysiumEntity::Kill`), so dormancy tears down the mind, the schedule, the
	// body owner and the motor exactly as a death does.
	int32 Clear(FElysiumEntityWorld& World, TArray<FElysiumEntityHandle>& Handles);

	// The same, found by NAME rather than by a caller's list: every live entity whose targetname
	// carries the generated `arena_npc_` prefix. It exists because the room and the cast have
	// different owners — the green room owns the floor, a panel owns who is standing on it — and a
	// torn-down room must not leave characters thinking in a level that no longer has one. A
	// character spawned under an explicit targetname is deliberately NOT matched: naming one is how
	// a caller says it is theirs to remove.
	int32 ClearSpawned(FElysiumEntityWorld& World);

	// The prefix above, so a panel can label a row as arena-owned rather than re-deriving it.
	const TCHAR* GeneratedNamePrefix();

	// --- Seeding the player character ---------------------------------------------------------------

	// The 2..8 clan encoding, as (id, name) pairs, for a picker. Only the seven playable clans:
	// every one of them resolves a `Player_<name>` template, and a clan that does not is not a
	// character a preset can seed.
	struct FClanOption
	{
		int32 Id = 0;
		FString Name;
		// The body stem this clan's male/female slot-0 armour resolves to, or empty when the
		// character export has not covered it. Reported so a preset can say up front that it will
		// seed a sheet with no body to stand it on.
		FString MaleStem;
		FString FemaleStem;
	};
	TArray<FClanOption> PlayableClans(UElysiumGameStateSubsystem* GameState);

	/**
	 * Seed the player record with a real, baselined character of `Clan`.
	 *
	 * It goes through chargen's own two doors — `ElysiumChargen::ApplyBaseline` and
	 * `UElysiumGameStateSubsystem::CommitChargen` — so the clan template, its `ClanEffect` gifts and
	 * banes, the auto-levelled baseline and the derived health block all arrive exactly as they do
	 * from New Game. Nothing here writes a sheet slot directly.
	 *
	 * **The chargen POINT POOLS are left unspent**, which is deliberate and is the one difference
	 * from a played character. Spending them is a set of choices, and inventing seven pools' worth
	 * of choices would be inventing a character rather than seeding a baseline; the character screen
	 * is still there to spend them. A baseline character is what VtMB's own `vautolvl` produces, so
	 * it is a real fighting character, not an empty one.
	 *
	 * `OutBodyStem` receives the clan's player body stem, so a caller can stand the matching body.
	 */
	bool SeedPlayerCharacter(UElysiumGameStateSubsystem* GameState, int32 Clan, bool bMale,
		FString& OutBodyStem, FString& OutError);

	// --- Outfitting the player --------------------------------------------------------------------

	struct FArmResult
	{
		int32 Melee = 0;
		int32 Firearms = 0;
		int32 Thrown = 0;
		int32 AmmoTypes = 0;
		int32 Refused = 0;      // records the inventory would not take (full, or an unresolved def)
		// Whether the `item_w_unarmed` fallback landed. It is not a controllable weapon
		// (`IsControllableWeapon()` correctly excludes `CWeaponUnarmed`) so it is not counted in
		// Melee, but `Holster` refuses outright without one carried, so the grant is load-bearing.
		bool bUnarmedGranted = false;
		FString FirstError;
	};

	// Give the player every controllable weapon in the catalog plus a full reserve of every
	// ammunition type any of them names.
	//
	// The reserve is stocked by AMMO TYPE rather than by handing over ammunition item entities: a
	// magazine's reserve and its loaded rounds are two different homes (`FElysiumInventory` —
	// `AmmoCount` reports the loaded magazine, `GiveAmmo` adds to the reserve), and a test that
	// wants to press reload wants the reserve.
	FArmResult ArmPlayerWithArsenal(FElysiumEntityWorld& World,
		UElysiumGameStateSubsystem* GameState, int32 ReservePerType = 100);

	// One named item onto the player, through the same route. Returns false with `OutError` for a
	// classname the item catalog does not carry.
	bool GivePlayerItem(FElysiumEntityWorld& World, const FString& Classname, FString& OutError);
}

#endif // !UE_BUILD_SHIPPING
