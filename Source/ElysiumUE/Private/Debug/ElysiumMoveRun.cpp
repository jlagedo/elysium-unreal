#include "Debug/ElysiumMoveRun.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumMoveCourses.h"
#include "ElysiumContentPaths.h"
#include "ElysiumInputRouter.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumMovementComponent.h"
#include "ElysiumPawn.h"
#include "ElysiumPlayerController.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMove, Log, All);

namespace
{
	// The same settle the other harnesses take: the spawn pass is done by then, but collision cooks
	// over the first frames and a body dropped into a half-built scene falls through the floor.
	constexpr int32 MoveSettleFrames = 30;

	FString OutputDir()
	{
		return FPaths::Combine(FElysiumContentPaths::Root(), TEXT("_move"));
	}
}

bool FElysiumMoveRun::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("ElysiumMove"));
}

FElysiumMoveRun::FElysiumMoveRun(UElysiumMapSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
	FParse::Value(FCommandLine::Get(), TEXT("MoveHz="), Hz);
	Hz = FMath::Clamp(Hz, 10, 1000);
	StepSeconds = 1.0f / static_cast<float>(Hz);

	FParse::Value(FCommandLine::Get(), TEXT("MoveCourse="), CourseFilter);

	UE_LOG(LogElysiumMove, Log, TEXT("headless movement run armed: %d Hz%s."), Hz,
		CourseFilter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", course '%s'"), *CourseFilter));

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FElysiumMoveRun::Tick));
}

FElysiumMoveRun::~FElysiumMoveRun()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
}

bool FElysiumMoveRun::BeginCourse(int32 Index)
{
	TArrayView<const ElysiumMoveCourses::FCourse> Courses = ElysiumMoveCourses::All();
	if (!Courses.IsValidIndex(Index))
	{
		return false;
	}
	const ElysiumMoveCourses::FCourse& Course = Courses[Index];

	UElysiumMapSubsystem* Sub = Subsystem.Get();
	const UGameInstance* GI = Sub ? Sub->GetGameInstance() : nullptr;
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	AElysiumPlayerController* PC = World
		? Cast<AElysiumPlayerController>(World->GetFirstPlayerController()) : nullptr;
	AElysiumPawn* Pawn = PC ? Cast<AElysiumPawn>(PC->GetPawn()) : nullptr;
	UElysiumInputRouter* Router = PC ? PC->GetInputRouter() : nullptr;
	if (!Pawn || !Router)
	{
		return false;
	}

	// Seat the body and aim it. The start is absolute and the mover's state is cleared, so a course
	// inherits neither the position nor the *motion* of the one before it — without the reset the
	// duck course opened at 203 u/s carried over from the strafe course's last airborne frame.
	Pawn->SetActorLocation(Course.Start, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
	PC->SetControlRotation(FRotator(0.0f, Course.StartYaw, 0.0f));
	if (UElysiumMovementComponent* Move = Cast<UElysiumMovementComponent>(Pawn->GetMovementComponent()))
	{
		Move->ResetState();
	}

	Router->StartReplay(ElysiumMoveCourses::Expand(Course, StepSeconds));

	Rows.Reset();
	Rows.Add(TEXT("frame,seq,dt,px,py,pz,vx,vy,vz,speed2d,onground,ducked,water,surffric"));
	PeakSpeed2D = 0.0;
	PeakApexUnits = 0.0;
	// Apex is measured per airborne span, from the takeoff height (see Sample).
	TakeoffZ = 0.0;
	LastGroundedZ = 0.0;
	bHaveDatum = false;
	GroundTransitions = 0;
	bWasOnGround = true;

	UE_LOG(LogElysiumMove, Log, TEXT("course '%s' begins at %s yaw %.1f"),
		Course.Name, *Course.Start.ToCompactString(), Course.StartYaw);
	return true;
}

void FElysiumMoveRun::Sample()
{
	UElysiumMapSubsystem* Sub = Subsystem.Get();
	const UGameInstance* GI = Sub ? Sub->GetGameInstance() : nullptr;
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	const AElysiumPlayerController* PC = World
		? Cast<AElysiumPlayerController>(World->GetFirstPlayerController()) : nullptr;
	const AElysiumPawn* Pawn = PC ? Cast<AElysiumPawn>(PC->GetPawn()) : nullptr;
	const UElysiumInputRouter* Router = PC ? PC->GetInputRouter() : nullptr;
	if (!Pawn || !Router)
	{
		return;
	}
	const UElysiumMovementComponent* Move =
		Cast<UElysiumMovementComponent>(Pawn->GetMovementComponent());
	if (!Move)
	{
		return;
	}

	const FVector P = Pawn->GetActorLocation();
	const FVector V = Move->Velocity;
	const double Speed2D = FVector2D(V.X, V.Y).Size();
	const bool bGround = Move->IsOnGround();

	// Positions and velocities are emitted in **Source units**, not cm, so a row reads directly
	// against `source_movement.md`'s numbers (and against a retail demo dump, if one is ever taken).
	const double Inv = 1.0 / ElysiumMove::U;

	Rows.Add(FString::Printf(
		TEXT("%d,%d,%.6f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%.3f"),
		Rows.Num() - 1, Router->CurrentCmd().Seq, StepSeconds,
		P.X * Inv, P.Y * Inv, P.Z * Inv,
		V.X * Inv, V.Y * Inv, V.Z * Inv, Speed2D * Inv,
		bGround ? 1 : 0,
		Move->IsDucked() ? 1 : 0,
		static_cast<int32>(Move->GetWaterLevel()),
		Move->GetSurfaceFriction()));

	PeakSpeed2D = FMath::Max(PeakSpeed2D, Speed2D * Inv);

	// The apex is measured from the height the body LEFT THE GROUND at, per airborne span — not
	// from one datum for the whole course. Against a single datum a running jump taken from higher
	// ground reads as a higher jump, which makes the number a property of the level rather than of
	// the movement, and it stops being comparable across frame rates (the body is somewhere
	// slightly different when it jumps).
	// The datum is the LAST GROUNDED sample, not the first airborne one. Sampling happens after the
	// move, so by the first frame that reports airborne the body has already risen a full `v0*dt` —
	// taking that as the takeoff height understates the apex by an amount that shrinks with the
	// frame time, which reads exactly like frame-rate dependence and is really just the ruler.
	if (bGround != bWasOnGround)
	{
		++GroundTransitions;
		if (!bGround)
		{
			TakeoffZ = LastGroundedZ;   // ground -> air
			bHaveDatum = true;
		}
		bWasOnGround = bGround;
	}
	if (bGround)
	{
		LastGroundedZ = P.Z;
	}
	else if (bHaveDatum)
	{
		PeakApexUnits = FMath::Max(PeakApexUnits, (P.Z - TakeoffZ) * Inv);
	}
}

void FElysiumMoveRun::FinishCourse()
{
	TArrayView<const ElysiumMoveCourses::FCourse> Courses = ElysiumMoveCourses::All();
	if (!Courses.IsValidIndex(CourseIndex) || Rows.Num() <= 1)
	{
		return;
	}
	const ElysiumMoveCourses::FCourse& Course = Courses[CourseIndex];

	const FString Stem = FString::Printf(TEXT("%s.%s.%dhz"), Course.Map, Course.Name, Hz);
	const FString CsvPath = FPaths::Combine(OutputDir(), Stem + TEXT(".csv"));
	FFileHelper::SaveStringToFile(FString::Join(Rows, TEXT("\n")) + TEXT("\n"), *CsvPath);

	// The summary is what a regression check compares first; the CSV is what a human reads when it
	// disagrees.
	const FString Json = FString::Printf(
		TEXT("{\n")
		TEXT("  \"map\": \"%s\",\n")
		TEXT("  \"course\": \"%s\",\n")
		TEXT("  \"hz\": %d,\n")
		TEXT("  \"fixedStep\": %.6f,\n")
		TEXT("  \"frames\": %d,\n")
		TEXT("  \"peakSpeed2dUnits\": %.4f,\n")
		TEXT("  \"peakRiseUnits\": %.4f,\n")
		TEXT("  \"groundTransitions\": %d\n")
		TEXT("}\n"),
		Course.Map, Course.Name, Hz,
		IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.move.FixedStep"))
			? IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.move.FixedStep"))->GetFloat()
			: 0.0f,
		Rows.Num() - 1, PeakSpeed2D, PeakApexUnits, GroundTransitions);
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(OutputDir(), Stem + TEXT(".json")));

	UE_LOG(LogElysiumMove, Log,
		TEXT("course '%s': %d frames, peak 2D %.1f u/s, peak rise %.2f u, %d ground transitions -> %s"),
		Course.Name, Rows.Num() - 1, PeakSpeed2D, PeakApexUnits, GroundTransitions, *CsvPath);
}

bool FElysiumMoveRun::Tick(float /*DeltaSeconds*/)
{
	if (bDone)
	{
		return false;
	}

	UElysiumMapSubsystem* Sub = Subsystem.Get();
	const UGameInstance* GI = Sub ? Sub->GetGameInstance() : nullptr;
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	AElysiumMapActor* MapActor = Sub ? Sub->GetCurrentMap() : nullptr;
	AElysiumPlayerController* PC = World
		? Cast<AElysiumPlayerController>(World->GetFirstPlayerController()) : nullptr;

	if (!(PC && MapActor && MapActor->IsSpawnDone()))
	{
		return true;
	}
	if (++FrameInPhase < MoveSettleFrames)
	{
		return true;
	}

	UElysiumInputRouter* Router = PC->GetInputRouter();
	if (!Router)
	{
		bDone = true;
		FPlatformMisc::RequestExit(false);
		return false;
	}

	// A course is in flight for as long as the router is still feeding its stream.
	if (CourseIndex >= 0 && Router->IsReplaying())
	{
		Sample();
		return true;
	}

	// The stream ran out (or nothing has started): close the last course and open the next one that
	// passes the filter.
	if (CourseIndex >= 0)
	{
		FinishCourse();
	}

	TArrayView<const ElysiumMoveCourses::FCourse> Courses = ElysiumMoveCourses::All();
	int32 Next = CourseIndex + 1;
	while (Courses.IsValidIndex(Next) &&
		!CourseFilter.IsEmpty() && CourseFilter != Courses[Next].Name)
	{
		++Next;
	}

	if (!Courses.IsValidIndex(Next))
	{
		UE_LOG(LogElysiumMove, Log, TEXT("movement run complete; exiting."));
		bDone = true;
		FPlatformMisc::RequestExit(false);
		return false;
	}

	CourseIndex = Next;
	if (!BeginCourse(CourseIndex))
	{
		UE_LOG(LogElysiumMove, Warning, TEXT("could not seat the body for course '%s'; exiting."),
			Courses[CourseIndex].Name);
		bDone = true;
		FPlatformMisc::RequestExit(false);
		return false;
	}
	return true;
}

#endif // !UE_BUILD_SHIPPING
