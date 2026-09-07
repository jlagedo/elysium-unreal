#pragma once

// The transient-game-world preamble every native-actor case rebuilt by hand: create the world,
// start play, and (as each case needs it) spawn the faithful player pawn, its controller, and the
// production map actor wrapper. RAII: destroying the fixture tears the transient world down through
// its own wrapped FTestWorldWrapper, exactly as each hand-written local `TestWorld` did — one fixture
// instance per RunTest, so each case still gets its own independent world with no cross-case sharing.
// Every spawn helper is a thin mechanical wrapper that performs no assertions of its own, so each case
// keeps its own TestNotNull/TestTrue wording exactly as authored.
//
// Moved verbatim out of `ElysiumPlayerWorldTests.cpp` so the terminal gym
// (`Tests/ElysiumTerminalGym.h`) can stand the same host without including that file's test bodies.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumMapActor.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

struct FPlayerWorldFixture
{
	FTestWorldWrapper TestWorld;
	UWorld* World = nullptr;
	APlayerController* PlayerController = nullptr;
	AElysiumPawn* Pawn = nullptr;
	AElysiumMapActor* MapActor = nullptr;

	// Creates the transient game world and starts play. Forwards the wrapper's own error messages to
	// Test and returns false on failure, exactly as every hand-written preamble did.
	bool CreateWorld(FAutomationTestBase& Test)
	{
		if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
		{
			TestWorld.ForwardErrorMessages(&Test);
			return false;
		}
		World = TestWorld.GetTestWorld();
		return true;
	}

	// Spawns the faithful player pawn into Pawn, at the given feet-origin lifted by the hull's own
	// half-height exactly as every hand-written call site lifted it. A null World (a prior CreateWorld
	// failure) yields a null Pawn, matching the `World ? ... : nullptr` guard every call site used.
	AElysiumPawn* SpawnPawn(const FVector& FeetOrigin = FVector::ZeroVector,
		const FRotator& Facing = FRotator::ZeroRotator)
	{
		Pawn = World ? World->SpawnActor<AElysiumPawn>(
			FeetOrigin + FVector(0.f, 0.f, ElysiumMove::StandHeight * 0.5f), Facing) : nullptr;
		return Pawn;
	}

	// Spawns SpawnPawn's pawn plus its controller into PlayerController/Pawn, without possessing —
	// several call sites assert on the unpossessed pair, or run their own assertions between spawn and
	// Possess, before calling it themselves.
	void SpawnPlayerControllerAndPawn(const FVector& FeetOrigin = FVector::ZeroVector,
		const FRotator& Facing = FRotator::ZeroRotator)
	{
		PlayerController = World ? World->SpawnActor<APlayerController>() : nullptr;
		SpawnPawn(FeetOrigin, Facing);
	}

	// Deferred-spawns the production map actor wrapper into MapActor. FinishSpawning is left to the
	// caller: two of the three call sites this replaces never call it at all (TeleportPlayer and
	// GetPlayerViewPoint need only the deferred actor), and the third (stage teardown) must set
	// bStageOnly first.
	AElysiumMapActor* SpawnMapActorDeferred()
	{
		MapActor = World->SpawnActorDeferred<AElysiumMapActor>(
			AElysiumMapActor::StaticClass(), FTransform::Identity);
		return MapActor;
	}
};

#endif   // WITH_DEV_AUTOMATION_TESTS
