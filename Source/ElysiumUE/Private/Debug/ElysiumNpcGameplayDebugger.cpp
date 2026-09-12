#include "Debug/ElysiumNpcGameplayDebugger.h"

#if !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Visual/ElysiumNpcBody.h"

#include "DrawDebugHelpers.h"

class FElysiumEntityWorld;
class FElysiumNpc;

namespace
{
	const FElysiumNpc* ResolveSelectedNpc(AActor* DebugActor, const FElysiumEntityWorld*& OutWorld)
	{
		OutWorld = nullptr;
		const AElysiumNpcBody* Body = Cast<AElysiumNpcBody>(DebugActor);
		return Body ? Body->ResolveOwningNpc(OutWorld) : nullptr;
	}

	// The last rows of the mind trace, oldest first, so the transitions that led to the current
	// state read without opening the Visual Logger.
	constexpr int32 TraceRowsShown = 5;
	FColor StateColor(const FString& State)
	{
		if (State == TEXT("Combat")) return FColor(230, 90, 90);
		if (State == TEXT("Alert")) return FColor(230, 200, 90);
		if (State == TEXT("Dead")) return FColor(160, 80, 80);
		return FColor(170, 190, 205);
	}
}

FElysiumNpcGameplayDebuggerCategory::FElysiumNpcGameplayDebuggerCategory()
{
	bShowOnlyWithDebugActor = true;
	CollectDataInterval = 0.05f;
	SetDataPackReplication<FElysiumNpcDebugData>(&DataPack,
		EGameplayDebuggerDataPack::ResetOnActorChange);
}

TSharedRef<FGameplayDebuggerCategory> FElysiumNpcGameplayDebuggerCategory::MakeInstance()
{
	return MakeShareable(new FElysiumNpcGameplayDebuggerCategory());
}

void FElysiumNpcGameplayDebuggerCategory::CollectData(APlayerController*, AActor* DebugActor)
{
	DataPack.Reset();
	const FElysiumEntityWorld* World = nullptr;
	const FElysiumNpc* Npc = ResolveSelectedNpc(DebugActor, World);
	if (Npc == nullptr)
	{
		DataPack.Error = DebugActor == nullptr
			? TEXT("Select an AElysiumNpcBody to inspect an NPC.")
			: TEXT("Selected actor is not a live Elysium NPC body.");
		return;
	}
	DataPack.Build(*Npc, *World);
}

void FElysiumNpcGameplayDebuggerCategory::DrawData(APlayerController*,
	FGameplayDebuggerCanvasContext& CanvasContext)
{
	UWorld* World = CanvasContext.GetWorld();
	if (!DataPack.bValid)
	{
		CanvasContext.Print(FColor::Yellow, DataPack.Error.IsEmpty()
			? TEXT("ElysiumNPC: no selected NPC body.") : *DataPack.Error);
		return;
	}

	const FColor Colour = StateColor(DataPack.State);
	if (World != nullptr)
	{
		if (DataPack.VisionRadiusCm > 0.0f)
		{
			DrawDebugCircle(World, DataPack.SenseOrigin, DataPack.VisionRadiusCm, 64, Colour,
				false, -1.0f, 0, 2.0f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			DrawDebugCone(World, DataPack.ConeApex, DataPack.SenseForward,
				DataPack.VisionRadiusCm, DataPack.ViewConeHalfAngleRadians,
				DataPack.ViewConeHalfAngleRadians, 32, Colour, false, -1.0f, 0, 1.5f);
		}
		if (DataPack.bHasHearingRadius && DataPack.HearingRadiusCm > 0.0f)
		{
			DrawDebugCircle(World, DataPack.HearingOrigin, DataPack.HearingRadiusCm, 48,
				FColor(100, 170, 255), false, -1.0f, 0, 1.5f,
				FVector(1, 0, 0), FVector(0, 1, 0), false);
		}
		if (DataPack.bHasEnemy)
		{
			DrawDebugLine(World, DataPack.SenseOrigin, DataPack.EnemyPosition,
				DataPack.bEnemyOccluded ? FColor(220, 150, 60) : FColor(240, 80, 80),
				false, -1.0f, 0, DataPack.bEnemyOccluded ? 1.0f : 2.5f);
		}
		if (DataPack.bHasPlace)
		{
			DrawDebugSphere(World, DataPack.PlacePosition + FVector(0, 0, 8), 14.0f, 12,
				FColor(120, 230, 130), false, -1.0f, 0, 2.0f);
			DrawDebugString(World, DataPack.PlacePosition + FVector(0, 0, 30),
				FString::Printf(TEXT("%s [%s]"), *DataPack.Place, *DataPack.PlaceType), nullptr,
				FColor(120, 230, 130), 0.0f, true, 1.0f);
		}
	}

	CanvasContext.Print(FColor::White, FString::Printf(TEXT("{yellow}%s{white}  %s  %s"),
		*DataPack.TargetName, *DataPack.ClassName, *DataPack.Model));
	CanvasContext.Print(FColor::White, FString::Printf(TEXT("Mind: {yellow}%s{white} state=%s ideal=%s owner=%s"),
		*DataPack.Admission, *DataPack.State, *DataPack.IdealState, *DataPack.BodyOwner));
	CanvasContext.Print(FColor::White, FString::Printf(TEXT("Schedule: {yellow}%s{white} task %d/%d %s (%s)"),
		*DataPack.ScheduleName, DataPack.TaskIndex + 1, DataPack.TaskCount,
		*DataPack.CurrentTask, *DataPack.CurrentTaskOperand));
	CanvasContext.Print(FColor::White, FString::Printf(TEXT("Conditions: %s  interrupts: %s  hit: %s"),
		*DataPack.Conditions, *DataPack.Interrupts, *DataPack.InterruptHits));
	CanvasContext.Print(FColor::White, FString::Printf(
		TEXT("Cadence: upd %.2fs norm %.2fs ai %.2fs mv %.3fs  %s%s%s%s"),
		DataPack.NextUpdateIn, DataPack.NextNormalIn, DataPack.NextAiIn, DataPack.NextMoveIn,
		DataPack.bReducedThink ? TEXT("{yellow}REDUCED{white} ") : TEXT(""),
		DataPack.bInPlayerPvs ? TEXT("pvs ") : TEXT("{grey}no-pvs{white} "),
		DataPack.bInPlayerLos ? TEXT("los ") : TEXT("{grey}no-los{white} "),
		DataPack.bThinkFrequently ? TEXT("frequent") : TEXT("")));
	CanvasContext.Print(FColor::White, FString::Printf(TEXT("Sense: vision %.0fcm cone %.1fdeg hearing %.2fx"),
		DataPack.VisionRadiusCm, FMath::RadiansToDegrees(DataPack.ViewConeHalfAngleRadians * 2.0f),
		DataPack.HearingScalar));
	CanvasContext.Print(FColor::White, FString::Printf(TEXT("Enemy: %s LOS failures=%d%s memory=%d"),
		*DataPack.Enemy, DataPack.EnemyLosFailures,
		DataPack.bEnemyOccluded ? TEXT(" OCCLUDED") : TEXT(""), DataPack.EnemyMemoryCount));
	if (DataPack.bHasHearingRadius)
	{
		CanvasContext.Print(FColor::White, FString::Printf(TEXT("Sound: %s radius %.0fcm"),
			*DataPack.HearingCategory, DataPack.HearingRadiusCm));
	}
	else
	{
		CanvasContext.Print(FColor::White, TEXT("Sound: no committed sound radius"));
	}
	CanvasContext.Print(FColor::White, FString::Printf(TEXT("Trace (%d):"), DataPack.Trace.Num()));
	if (DataPack.Trace.Num() == 0)
	{
		CanvasContext.Print(FColor::White, TEXT("  (none)"));
	}
	for (int32 Index = FMath::Max(0, DataPack.Trace.Num() - TraceRowsShown);
		Index < DataPack.Trace.Num(); ++Index)
	{
		CanvasContext.Print(Index == DataPack.Trace.Num() - 1 ? FColor::White : FColor(190, 190, 190),
			FString::Printf(TEXT("  %s"), *DataPack.Trace[Index]));
	}
}

#endif // !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER
