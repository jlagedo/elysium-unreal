#pragma once

#include "CoreMinimal.h"

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
// Elysium (no attacks or disciplines). Stubbed and mocked until area-rule subsystem is wired.
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

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	TArray<FElysiumHUDSelectorEntry> Entries;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 SelectedIndex = INDEX_NONE;

	// Brief mode shows a 3-item peek (prev/current/next) for D-pad weapon cycling, instead of
	// the full selector list used by KBM. Driven by the gamepad cycling path.
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bBriefMode = false;

	bool IsOpen() const { return Type != EElysiumHUDSelector::None; }
};

// Decoded HUD art paths under `$ELYSIUM_EXPORT_ROOT/ui/art`. The widget loads `<path>.png`; the
// publisher (preview today, the view-state owner later) is the only writer of these names.
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

	inline bool ShowsAmmo(EElysiumWeaponClass Class)
	{
		return Class == EElysiumWeaponClass::Ranged || Class == EElysiumWeaponClass::Thrown;
	}
}
