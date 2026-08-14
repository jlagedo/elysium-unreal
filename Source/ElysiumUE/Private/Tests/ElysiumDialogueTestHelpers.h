#pragma once

#include "CoreMinimal.h"

namespace ElysiumDialogueTestHelpers
{
FString ElysiumDlgRow(int32 Id, const FString& Text, const FString& Link,
	const FString& Condition, const FString& Action, const FString& Malkavian = FString());
TArray<uint8> ElysiumDlgBytes(const TArray<FString>& Rows);
}
