#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER

class FElysiumEntityWorld;
class FElysiumNpc;

// The Gameplay Debugger and Visual Logger read this value type. It is deliberately not a live
// view: handles are converted to stable labels/positions during Build, and the category serializes
// this copy for the local drawing side.
struct FElysiumNpcDebugMemoryRow
{
	FString Handle;
	FVector LastPosition = FVector::ZeroVector;
	FVector Anchor = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	double LastSeenTime = -1.0;
	int32 LastNavNode = INDEX_NONE;
	int32 AnchorNavNode = INDEX_NONE;
	bool bPositionOnly = false;
	bool bEluded = false;

	void Serialize(FArchive& Ar);
};

// Which of the row arrays Build fills. The Gameplay Debugger wants everything; a Visual Logger
// snapshot is taken per entry and only prints scalars, so it skips the rows.
enum class EElysiumNpcDebugRows : uint8
{
	Full,
	SummaryOnly,
};

struct FElysiumNpcDebugData
{
	static constexpr int32 MaxTraceRows = 16;
	static constexpr int32 MaxMemoryRows = 64;
	static constexpr int32 MaxTaskRows = 64;

	bool bValid = false;
	FString Error;
	uint32 Epoch = 0;
	int32 HandleIndex = INDEX_NONE;
	FString TargetName;
	FString ClassName;
	FString Model;

	FString Admission;
	FString State;
	FString IdealState;
	FString BodyOwner;
	FString SuspendedOwner;
	uint32 OwnerGeneration = 0;
	FString LastTransition;
	TArray<FString> Trace;

	FString ScheduleName;
	int32 ScheduleNumber = 0;
	int32 TaskIndex = 0;
	int32 TaskCount = 0;
	FString CurrentTask;
	FString CurrentTaskOperand;
	FString FailSchedule;
	float ToleranceUnits = -1.0f;
	bool bTaskStarted = false;
	TArray<FString> TaskRows;

	FString Conditions;
	FString Interrupts;
	FString InterruptHits;
	double GatheredAt = -1.0;

	FVector SenseOrigin = FVector::ZeroVector;
	FVector SenseForward = FVector::ForwardVector;
	FVector ConeApex = FVector::ZeroVector;
	float VisionRadiusCm = 0.0f;
	float ViewConeHalfAngleRadians = 0.0f;
	float HearingScalar = 1.0f;
	bool bHasHearingRadius = false;
	FVector HearingOrigin = FVector::ZeroVector;
	float HearingRadiusCm = 0.0f;
	FString HearingCategory;

	bool bHasEnemy = false;
	FString Enemy;
	FVector EnemyPosition = FVector::ZeroVector;
	bool bEnemyOccluded = false;
	int32 EnemyLosFailures = 0;
	// The live record count; EnemyMemory carries at most MaxMemoryRows of them, or none for a
	// summary build.
	int32 EnemyMemoryCount = 0;
	TArray<FElysiumNpcDebugMemoryRow> EnemyMemory;

	bool bHasPlace = false;
	FString Place;
	FVector PlacePosition = FVector::ZeroVector;
	FString PlaceType;
	int32 PlaceGroup = 0;
	int32 PlaceRating = 0;
	int32 AmbientPhase = 0;

	void Reset();
	void Build(const FElysiumNpc& Npc, const FElysiumEntityWorld& World,
		EElysiumNpcDebugRows Rows = EElysiumNpcDebugRows::Full);
	void Serialize(FArchive& Ar);
};

#endif // !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER
