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
};

UENUM(BlueprintType)
enum class EElysiumHUDSelector : uint8
{
	None,
	Weapons,
	Disciplines,
	Inventory,
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

	bool IsOpen() const { return Type != EElysiumHUDSelector::None; }
};
