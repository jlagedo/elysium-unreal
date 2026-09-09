#pragma once

#if !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER

#include "Debug/ElysiumNpcDebugData.h"
#include "GameplayDebuggerCategory.h"

class FElysiumNpcGameplayDebuggerCategory final : public FGameplayDebuggerCategory
{
public:
	FElysiumNpcGameplayDebuggerCategory();
	static TSharedRef<FGameplayDebuggerCategory> MakeInstance();

	virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor) override;
	virtual void DrawData(APlayerController* OwnerPC,
		FGameplayDebuggerCanvasContext& CanvasContext) override;

private:
	FElysiumNpcDebugData DataPack;
};

#endif // !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER
