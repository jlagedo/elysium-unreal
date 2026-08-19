#pragma once

#include "CoreMinimal.h"

#include "ElysiumInventorySections.h"

// ================================================================================================
// 15. vdata/items/*.txt — the item definitions
// ================================================================================================
//
// The one authority on item POLICY (`docs/vtmb/inventory.md` §4). A filename prefix is a
// convention and not a type system, so nothing anywhere reads `item_w_`/`item_k_` to decide
// whether a thing stacks, drops or is a weapon — it is decided here or not at all.
//
// Unlike the twelve `system/` tables, this one is a whole DIRECTORY: one file per item, its
// basename being the entity classname the maps, the dialogue and the scripts all name.

// The eleven semantic item types `system/items.txt` declares, in that file's own order — the order
// is the engine's item-type enum (the file's own comment points at `vamp_data.h`). This is the
// compile-time mirror of the file, the way `EElysiumTraitOp` mirrors `traiteffect.txt`.
enum class EElysiumItemType : uint8
{
	WeaponMelee = 0,
	WeaponFirearm,
	WeaponThrown,
	Ammo,
	Armor,
	Money,
	Jewelry,
	Generic,
	Powerup,
	Bloodpack,
	Hidden,
	Count,
};

const TCHAR* ElysiumItemTypeName(EElysiumItemType Type);

// `item_type` is a SPACE-SEPARATED SET, not one word: `"weapon_firearm hidden"` and
// `"hidden hidden"` are both authored. The first token that names a type wins; a `hidden` token
// beyond that one sets OutHidden. Returns false when no token named a type.
bool ElysiumParseItemType(const FString& Raw, EElysiumItemType& OutType, bool& OutHidden);

// The authored `Type` of one weapon mode — the value `CWeaponRanged::ModeDispatch` branches on
// (`docs/vtmb/combat-and-damage.md` § "Input and firing modes"). This is a FIRE-MODE state machine,
// not a combo system: the record decides what a press does, and nothing derives it from a classname.
// An authored spelling outside this set stays readable as `TypeName` and resolves to `Other`.
enum class EElysiumWeaponModeType : uint8
{
	None = 0,
	Attack,              // `Attack` — the ordinary attack modes
	SecondaryAttack,     // `Secondary_Attack`
	TogglePrimaryMode,   // `Toggle_Primary_Mode` — swap primary modes 0/1
	ZoomLoop,            // `Zoom_Out_Loop` — cycle the scope range/state
	Other,               // a consumable/throw-style or otherwise unrecovered mode
};

const TCHAR* ElysiumWeaponModeTypeName(EElysiumWeaponModeType Type);

// One `Activation` block — a weapon MODE. A weapon record carries one per authored block, in file
// order, and the `Tag` (`Primary`, `PrimaryMode2`, `Secondary`) is how retail names them.
//
// `Dmg` is kept as the authored string here: the rulebook is the data layer, and turning the
// grammar into a descriptor is `ElysiumDamage::ParseDmg`'s job, which the weapon controller does
// once when the entity spawns.
struct FElysiumWeaponMode
{
	FString Tag;                        // `Tag`
	FString TypeName;                   // `Type`, verbatim
	EElysiumWeaponModeType Type = EElysiumWeaponModeType::None;

	FString Dmg;                        // `Dmg` — the authored damage grammar
	// `BaseLethality` and `SkillRequirement` are the two adjacent integers
	// (`combat-and-damage.md` § "Authored weapon inputs"). The first is read by the lethality stage;
	// the second is a DEAD FIELD — the mode loader parses it and no engine path ever reads it back
	// (§ RE40 -> SkillRequirement), so it is stored for audit and gates nothing.
	int32 BaseLethality = 0;
	int32 SkillRequirement = 0;

	// `Attack_Rate` — the ranged next-shot interval, in seconds. Melee recovery does NOT use it
	// (it is the selected clip's duration over its playback rate), but a melee record authors it and
	// the dry-fire path advances by it, so it is loaded for every mode.
	float AttackRate = 0.0f;

	FString AmmoType;                   // `Ammo_Type`
	// The two counts retail keeps DISTINCT: rounds spent per scheduled shot, and rays/pellets placed
	// in the fire packet for each of those shots. The M37 spends one shell and emits eight rays.
	int32 AmmoCost = 0;                 // `Ammo_Cost`
	int32 AmmoFired = 1;                // `Ammo_Fired`; unauthored means one ray

	// `allow_autofire` — clear means held attack intent is lost after the press edge.
	bool bAllowAutofire = false;

	// `BurstMin`/`BurstMax`, loaded with the loader's own `BurstMin <= BurstMax` clamp. Both are DEAD
	// FIELDS — parsed by the mode loader and unreferenced by runtime combat logic on either the
	// player or the NPC side (§ RE40 -> Burst Fields) — so they are stored for audit and no burst
	// queue exists to build from them.
	int32 BurstMin = 0;
	int32 BurstMax = 0;

	float Range = 0.0f;                 // `Range`
	FString BotchTable;                 // `Botch_Table`

	bool IsAttack() const
	{
		return Type == EElysiumWeaponModeType::Attack || Type == EElysiumWeaponModeType::SecondaryAttack;
	}
};

// The section an item type files under, and whether that type is held or worn. Every column is
// authored per `ItemType` in `system/items.txt`: `Ammo` files under `None` and is never browsable,
// and `Bloodpack`, `Money` and `Jewelry` all file under `Generic`.
EElysiumInvSection ElysiumSectionForItemType(EElysiumItemType Type);
bool ElysiumItemTypeIsWielded(EElysiumItemType Type);
bool ElysiumItemTypeIsWorn(EElysiumItemType Type);

// One `vdata/items/<classname>.txt` — the `WeaponData` block every one of them hangs off, reduced
// to what the inventory runtime and the economy read. The file carries far more (crosshair bloom,
// muzzle particles, botch tables, sprite atlases); those belong to the systems that own them and
// are deliberately not mirrored here.
struct FElysiumItemDef
{
	FString Classname;          // the file's basename — the entity classname
	FString PrintName;          // `printname` — the display name
	FString Description;

	EElysiumItemType Type = EElysiumItemType::Generic;
	bool bHidden = false;       // the second `hidden` token beside the type

	// --- Policy. Three DISTINCT keys; none is a synonym for another (`inventory.md` §4) --------
	bool  bStackable = false;           // `is_stackable` — selects quantity behaviour / m_iItemCount
	int32 StackLimit = 0;               // `stack_limit`; 0 = the file authored none
	// `is_droppable` — read by the ordinary player drop predicate. 78 of the 244 files author it;
	// the engine's own default for the other 166 was not recovered, so `true` is this runtime's
	// choice and is stated here rather than implied.
	bool  bDroppable = true;
	bool  bPermanentInventory = false;  // `permanent_inventory` — longer-lived storage policy

	bool  bWieldable = false;           // `is_wieldable`
	bool  bVisibleInHud = true;         // `is_visible_in_hud`

	// --- Selection order (`bucket` / `bucket_position`) ---------------------------------------
	// The weapon-selection column and the row inside it. Every `item_w_*` record authors both:
	// bucket 0 holds the melee families and bucket 1 the ranged and thrown ones, and
	// `bucket_position` orders the entries within a bucket. This is the ordering the weapon
	// selector cycles in; it is authored data, not a derived sort.
	int32 Bucket = 0;                   // `bucket`
	int32 BucketPosition = 0;           // `bucket_position`

	int32 Worth = 0;                    // `item_worth`
	int32 PlayerSell = 0;               // `player_sell` — the vendor half is 9.10's
	int32 Weight = 0;                   // `weight`
	int32 ItemFlags = 0;                // `item_flags`

	// `camera_class`, parsed to its bits. The equipped item's value is what decides whether drawing
	// this weapon changes the view, and it lives on the item record rather than on the player
	// (`docs/vtmb/camera-view-modes.md` §2). 0 for `noswitch`, an unrecognized literal and an absent
	// key alike — all three reach the same early-out.
	int32 CameraClass = 0;

	// --- The `Magazine` child block — a firearm's ammunition -----------------------------------
	FString AmmoType;                   // `Type`, e.g. `ThirtyeightRound`; empty = carries no magazine
	int32 MagazineSize = 0;             // `Size` — the loaded-magazine capacity
	int32 DefaultAmmo = 0;              // `Default_Size` — what a fresh item spawns loaded with
	int32 DroppedAmmo = 0;              // `Dropped_Ammo`
	float ReloadTime = 0.0f;            // `ReloadTime`

	// `reload_single` — a WeaponData-level key (the patch-first M37, .38 and flaming crossbow author
	// it). One round per reload cycle, re-entering until interrupted, full or out of reserve.
	bool bReloadSingle = false;

	// `Disallow_FirearmsToBashing` — the record read by the Kindred lethal->bashing conversion in
	// `ElysiumDamage::Apply` step 2. No shipped `vdata/items` record authors it; the one shipped
	// author is an NPC template, so this join is the one `combat-and-damage.md` states (the
	// attacker's active weapon record) and the observed authoring disagrees with it. Reading the
	// key here keeps the documented join and changes no shipped behaviour.
	bool bDisallowFirearmsToBashing = false;

	// --- The `Activation` blocks — the weapon modes ---------------------------------------------
	TArray<FElysiumWeaponMode> Modes;

	// --- Models --------------------------------------------------------------------------------
	FString PlayerModel;                // `playermodel` — the loose world (ground) model
	FString ViewModel;
	FString InfoModel;
	// `wieldmodel_m` / `wieldmodel_f` — the held geometry the equip transaction applies by the
	// wielder's sex (`docs/vtmb/wielded_weapons.md` §§1-2). `w_null.mdl` (loose or under
	// `weapons/`) is the authored no-geometry answer, not a missing value.
	FString WieldModelM;
	FString WieldModelF;
	// `anim_prefix` — the weapon-family lookup key `EventDispatch` resolves together with an
	// activity constant into a melee impact-profile variant (§5). Consumed opaquely; never
	// assembled into a sequence or activity name.
	FString AnimPrefix;

	bool IsValid() const { return !Classname.IsEmpty(); }
	// The three wielded weapon families — the ones `FElysiumWeapon` controls. Policy from the parsed
	// record, never from the classname prefix.
	bool IsControllableWeapon() const
	{
		return Type == EElysiumItemType::WeaponMelee
			|| Type == EElysiumItemType::WeaponFirearm
			|| Type == EElysiumItemType::WeaponThrown;
	}
	// The first mode carrying `Tag`, case-insensitively, or null.
	const FElysiumWeaponMode* FindMode(const TCHAR* Tag) const;
	// The `system/items.txt` `IsWeapon` column, mirrored: the wielded families plus Bloodpack.
	bool IsWeaponType() const;
	// Whether an ordinary stack may still take one more. A `StackLimit` of 0 is unauthored, which
	// is not a limit of zero.
	bool StackHasRoom(int32 Count) const { return StackLimit <= 0 || Count < StackLimit; }
};

struct FElysiumItemTable
{
	TArray<FElysiumItemDef> Items;   // filename order

	bool Load(FString& OutError);
	bool IsValid() const { return !Items.IsEmpty(); }

	// Rebuild the classname index from `Items`. `Load` calls it; a hand-built table (the tests')
	// needs it because the lookup IS the index, not a scan — the rule `FElysiumQuestTables` set.
	void Reindex();

	// Case-insensitive, as every inventory lookup in the game is.
	const FElysiumItemDef* Find(const FString& Classname) const;
	const FElysiumItemDef* At(int32 Index) const;
	int32 Num() const { return Items.Num(); }
	int32 CountOfType(EElysiumItemType Type) const;

	// Parse one file's text into a definition. Public because it is the piece the tests drive
	// directly, and because the directory walk is the only other thing `Load` does.
	static bool ParseText(const FString& Classname, const FString& Text, FElysiumItemDef& Out,
		FString& OutError);

private:
	TMap<FString, int32> ByName;
};
