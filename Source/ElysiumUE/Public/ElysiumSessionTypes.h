#pragma once

#include "CoreMinimal.h"

enum class EElysiumSaveKind : uint8 { Manual, Quick, Auto };
enum class EElysiumSaveOperationState : uint8 { Capturing, Writing, Written, Failed };

struct FElysiumSaveRequest
{
	EElysiumSaveKind Kind = EElysiumSaveKind::Manual;
	FString Slot;
};

struct FElysiumSaveResult
{
	uint64 OperationId = 0;
	EElysiumSaveOperationState State = EElysiumSaveOperationState::Failed;
	FString Slot;
	FString Error;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumSaveResult, const FElysiumSaveResult&);
