#pragma once

#include "CommonInputBaseTypes.h"
#include "CommonUITypes.h"

#include "ElysiumCommonUIInputData.generated.h"

// Native row type so the CommonUI Back/Accept contract is a tracked C++ input fact rather than a
// binary asset. Mouse buttons keep their normal Slate path; these bindings provide keyboard and
// generic-gamepad navigation actions.
USTRUCT()
struct FElysiumCommonInputActionData final : public FCommonInputActionDataBase
{
	GENERATED_BODY()

	void SetKeys(FKey Keyboard, FKey Gamepad)
	{
		KeyboardInputTypeInfo.SetKey(Keyboard);
		DefaultGamepadInputTypeInfo.SetKey(Gamepad);
	}
};

UCLASS()
class UElysiumCommonUIInputData final : public UCommonUIInputData
{
	GENERATED_BODY()

public:
	UElysiumCommonUIInputData();
	const FDataTableRowHandle& GetUseAction() const { return UseAction; }

private:
	UPROPERTY()
	TObjectPtr<class UDataTable> ActionTable;

	UPROPERTY()
	FDataTableRowHandle UseAction;
};
