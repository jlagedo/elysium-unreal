#pragma once

#include "CoreMinimal.h"

#include "ElysiumInventorySections.h"

#include "ElysiumHUDTypes.generated.h"

// Presentation-only HUD vocabulary. These types deliberately carry no entity handles or gameplay
// objects: UElysiumPresentationSubsystem resolves the world into FElysiumViewState, and the HUD
// model resolves that snapshot into these Blueprint-readable values.

UENUM(BlueprintType)
enum class EElysiumHUDReticle : uint8
{
	None,
	Cross,
	UseIcon,
	ThirdPerson,
};

UENUM(BlueprintType)
enum class EElysiumHUDSelector : uint8
{
	None,
	Weapons,
	Disciplines,
	Inventory,
	Radial,
};

// The weapon class selects the category glyph and whether the equipment readout shows ammo.
UENUM(BlueprintType)
enum class EElysiumWeaponClass : uint8
{
	None,
	Unarmed,
	Melee,
	Ranged,
	Thrown,
};

// Zone-state indicator above the Life bar: combat (free attack), Masquerade (uphold the Masquerade),
// Elysium (no attacks or disciplines). Stubbed; area-rule authority is not this type.
UENUM(BlueprintType)
enum class EElysiumZoneState : uint8
{
	None,
	Combat,
	Masquerade,
	Elysium,
};

UENUM(BlueprintType)
enum class EElysiumHUDPreview : uint8
{
	Off,
	Passive,
	Combat,
	Weapon,
	Discipline,
	Inventory,
	Critical,
	Radial,
	Brief,
	Elysium,
	Sneak,
};

// The player's committed detection state, as presentation sees it.
UENUM(BlueprintType)
enum class EElysiumHUDDetection : uint8
{
	Unaware,
	Searching,
	Detected,
};

// The stealth readout. It is situational: nothing here is on screen unless the player is
// crouched. `bConcealmentValid` and `bObserverValid` are separate because the two halves have
// different owners and land at different times.
USTRUCT(BlueprintType)
struct FElysiumHUDStealthView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bSneaking = false;

	// False renders the gauge as unmeasured. An unfilled gauge and a gauge nobody has measured are
	// different statements and must not look the same.
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bConcealmentValid = false;

	// 0 (fully lit) to 4 (fully dark) — the five exported `lightgauge` steps.
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 ConcealmentStep = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bObserverValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	float ObserverDistanceMetres = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	EElysiumHUDDetection Detection = EElysiumHUDDetection::Unaware;
};

USTRUCT(BlueprintType)
struct FElysiumHUDEquipmentView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FText Name;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	EElysiumWeaponClass WeaponClass = EElysiumWeaponClass::None;

	// Path under `$ELYSIUM_EXPORT_ROOT/ui/art` without extension (e.g. hud/inventory_images/weapons_ranged/thirtyeight).
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FName Icon;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 AmmoCurrent = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 AmmoReserve = 0;
};

USTRUCT(BlueprintType)
struct FElysiumHUDDisciplineView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FText Name;

	// Path under `$ELYSIUM_EXPORT_ROOT/ui/art` without extension (e.g. hud/disciplines/bloodheal).
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FName Icon;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 BloodCost = 0;
};

USTRUCT(BlueprintType)
struct FElysiumHUDSelectorEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FText Label;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FText Detail;

	// Path under `$ELYSIUM_EXPORT_ROOT/ui/art` without extension.
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FName Icon;

	// Quantity for stackable items (blood packs, lockpicks). 0 = don't show quantity.
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 Quantity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct FElysiumHUDSelectorView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	EElysiumHUDSelector Type = EElysiumHUDSelector::None;

	// The browsed category's authored name, straight from `items.txt`'s own `Name` field. Carried as
	// text because the section vocabulary is the substrate's and the HUD only renders it.
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FText Heading;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	TArray<FElysiumHUDSelectorEntry> Entries;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 SelectedIndex = INDEX_NONE;

	// Brief mode shows a 3-item peek (prev/current/next) for D-pad weapon cycling, instead of
	// the full selector list used by KBM. Driven by the gamepad cycling path.
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bBriefMode = false;

	// The selector's own opacity, 0..1. The cycle peek fades itself out on a timer the publisher
	// owns, so the widget multiplies by this rather than holding a fade of its own. A selector the
	// player opened and holds open sits at 1.
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	float Alpha = 1.0f;

	bool IsOpen() const { return Type != EElysiumHUDSelector::None; }
};

// Decoded HUD art paths under `$ELYSIUM_EXPORT_ROOT/ui/art`. The widget loads `<path>.png`; the
// publisher is the only writer of these names.
namespace ElysiumHUDArt
{
	inline FName Inventory(const TCHAR* RelStem)
	{
		return FName(*FString::Printf(TEXT("hud/inventory_images/%s"), RelStem));
	}

	inline FName Discipline(const TCHAR* Stem)
	{
		return FName(*FString::Printf(TEXT("hud/disciplines/%s"), Stem));
	}

	inline FName Category(EElysiumWeaponClass Class)
	{
		switch (Class)
		{
		case EElysiumWeaponClass::Unarmed: return Inventory(TEXT("weapons_melee/fists"));
		case EElysiumWeaponClass::Melee:   return FName(TEXT("hud/catagory_icons/meleeweapons"));
		case EElysiumWeaponClass::Ranged:  return FName(TEXT("hud/catagory_icons/rangedweapons"));
		case EElysiumWeaponClass::Thrown:  return FName(TEXT("hud/catagory_icons/thrownweapons"));
		default:                           return NAME_None;
		}
	}

	inline FName Area(EElysiumZoneState Zone)
	{
		switch (Zone)
		{
		case EElysiumZoneState::Combat:     return FName(TEXT("hud/area_icons/area_icon_combat"));
		case EElysiumZoneState::Masquerade: return FName(TEXT("hud/area_icons/area_icon_safearea"));
		case EElysiumZoneState::Elysium:    return FName(TEXT("hud/area_icons/area_icon_elysium"));
		default:                            return NAME_None;
		}
	}

	// The category glyph for an inventory section, for a row whose own art cannot be resolved.
	inline FName SectionGlyph(EElysiumInvSection Section)
	{
		switch (Section)
		{
		case EElysiumInvSection::WeaponMelee:  return FName(TEXT("hud/catagory_icons/meleeweapons"));
		case EElysiumInvSection::WeaponRanged: return FName(TEXT("hud/catagory_icons/rangedweapons"));
		case EElysiumInvSection::WeaponThrown: return FName(TEXT("hud/catagory_icons/thrownweapons"));
		case EElysiumInvSection::Armor:        return FName(TEXT("hud/catagory_icons/armors"));
		default:                               return FName(TEXT("hud/catagory_icons/generalinven"));
		}
	}

	// The icon for an item classname.
	//
	// No item record names an icon: the `a_icons1`/`w_icons1` keys in `vdata/items` are commented-out
	// sprite-atlas leftovers, so the join between an item and its `inventory_images` art is ours to
	// make. It is the classname's stem (`item_w_tire_iron` -> `weapons_melee/tire_iron`) wherever the
	// exported art agrees, and a named alias for the records whose art was filed under a different
	// name. A classname with neither falls back to a category glyph, which is a readable icon rather
	// than a hole.
	//
	// Worn armour is deliberately not resolved to its own art. The tree files clothing per clan,
	// per sex and per tier (`armors/brujahf/brujah_f_a0`), and no decoded field on the item record
	// names that tier — so the portrait needs a join this runtime does not have, and the category
	// glyph is the honest answer.
	inline FName ItemIcon(const FString& Classname, EElysiumWeaponClass Class)
	{
		FString Stem = Classname;
		Stem.RemoveFromEnd(TEXT("-null"), ESearchCase::IgnoreCase);
		if (Stem.RemoveFromStart(TEXT("item_a_"), ESearchCase::IgnoreCase))
		{
			return SectionGlyph(EElysiumInvSection::Armor);
		}

		const bool bWeapon = Stem.RemoveFromStart(TEXT("item_w_"), ESearchCase::IgnoreCase);
		if (!bWeapon)
		{
			// Everything else files flat under `general_items`; `item_g_`/`item_p_` are conventions
			// on the classname, not a type system, so both reach the same tree.
			Stem.RemoveFromStart(TEXT("item_g_"), ESearchCase::IgnoreCase);
			Stem.RemoveFromStart(TEXT("item_p_"), ESearchCase::IgnoreCase);
			static const TMap<FString, FString> GeneralAliases = {
				{ TEXT("lockpick"), TEXT("general_items/lockpicks") },
				{ TEXT("keyring"),  TEXT("general_items/key") },
			};
			if (const FString* Alias = GeneralAliases.Find(Stem.ToLower()))
			{
				return Inventory(**Alias);
			}
			return Stem.IsEmpty()
				? SectionGlyph(EElysiumInvSection::Generic)
				: Inventory(*FString::Printf(TEXT("general_items/%s"), *Stem));
		}

		// The art tree's own names for weapon records the stem does not reach. `item_w_ithaca_m_37`
		// is the one judgement call in the table: the tree carries no `ithaca` art, and `shotgun` is
		// the pump-action icon its record describes.
		static const TMap<FString, FString> Aliases = {
			{ TEXT("unarmed"),         TEXT("weapons_melee/fists") },
			{ TEXT("avamp_blade"),     TEXT("weapons_melee/katana") },
			{ TEXT("chang_blade"),     TEXT("weapons_melee/katana") },
			{ TEXT("occultblade"),     TEXT("weapons_melee/sword") },
			{ TEXT("colt_anaconda"),   TEXT("weapons_ranged/anaconda") },
			{ TEXT("glock_17c"),       TEXT("weapons_ranged/glock") },
			{ TEXT("crossbow_flaming"), TEXT("weapons_ranged/crossbowflaming") },
			{ TEXT("remington_m_700"), TEXT("weapons_ranged/remington_m-700") },
			{ TEXT("rem_m_700_bach"),  TEXT("weapons_ranged/remington_m-700_bach") },
			{ TEXT("ithaca_m_37"),     TEXT("weapons_ranged/shotgun") },
		};
		if (const FString* Alias = Aliases.Find(Stem.ToLower()))
		{
			return Inventory(**Alias);
		}

		// The two trees the weapon art is filed under, chosen by the weapon's own family.
		const TCHAR* Tree = Class == EElysiumWeaponClass::Melee || Class == EElysiumWeaponClass::Unarmed
			? TEXT("weapons_melee")
			: TEXT("weapons_ranged");
		return Stem.IsEmpty() ? Category(Class) : Inventory(*FString::Printf(TEXT("%s/%s"), Tree, *Stem));
	}

	// The authored Masquerade ceiling. The sheet slot counts VIOLATIONS up from zero and the fifth
	// one ends the run, so the marks the player still holds are `Marks - Level`.
	constexpr int32 MasqueradeMarks = 5;

	// The concealment gauge's five steps, as exported.
	constexpr int32 ConcealmentSteps = 5;

	inline bool ShowsAmmo(EElysiumWeaponClass Class)
	{
		return Class == EElysiumWeaponClass::Ranged || Class == EElysiumWeaponClass::Thrown;
	}
}
