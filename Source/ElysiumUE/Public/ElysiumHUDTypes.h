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

// The weapon class determines the glyph prefix in the equipment readout and whether ammo is shown.
UENUM(BlueprintType)
enum class EElysiumWeaponClass : uint8
{
	None,
	Unarmed,
	Melee,
	Ranged,
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

	// Stem name of the inventory image (e.g. "weapons_ranged/38"). None = no icon.
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

	// Stem name of the inventory image. None = no icon.
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
