#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "ElysiumInputAssets.generated.h"

class UInputAction;

// One generated row from Config/ElysiumInputActions.csv. Analog actions have an empty Command and
// bind to the user-command builder directly; command actions retain VtMB's console string identity.
USTRUCT(BlueprintType)
struct FElysiumInputActionDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> Action = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
	FString Command;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
	bool bButtonPair = false;
};

// Generated beside the Input Actions and mapping context. It keeps the runtime command routing a
// projection of the committed CSV instead of hard-coding +jump in the router.
UCLASS(BlueprintType)
class UElysiumInputActionSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
	TArray<FElysiumInputActionDefinition> Actions;

	const FElysiumInputActionDefinition* Find(FName Id) const;
};

namespace ElysiumInputAssets
{
	inline constexpr TCHAR ActionSetPath[] =
		TEXT("/Game/Input/DA_ElysiumInputActions.DA_ElysiumInputActions");
	inline constexpr TCHAR GamepadContextPath[] =
		TEXT("/Game/Input/IMC_Player_Gamepad.IMC_Player_Gamepad");

	inline constexpr TCHAR DualSenseCreateKey[] = TEXT("Elysium_DualSense_Create");
	inline constexpr TCHAR DualSensePSKey[] = TEXT("Elysium_DualSense_PS");
	inline constexpr TCHAR DualSenseMuteKey[] = TEXT("Elysium_DualSense_Mute");
}
