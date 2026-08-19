// func_elevator — VtMB CFuncElevator (vampire.dll 0x1020e740 / datamap 0x105aadf0)

#include "Substrate/ElysiumMover.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumClassFields.h"

class FElysiumElevator final : public FElysiumMoverBase
{
public:
	float Speed = 100.0f;
	int32 NumFloors = 0;
	bool bLocked = false;
	FString StartSound;
	FString StopSound;

	virtual void Spawn() override
	{
		CurrentFloor = 0;       // CFuncElevator::Spawn 0x1020f220
		TargetFloor = INDEX_NONE;
		bRestoreSeatPending = false;
		Passed.Init(false, 8);
	}

	void InputLock() { bLocked = true; }
	void InputUnlock() { bLocked = false; }

	void InputGotoFloor(int32 OneBased, const FElysiumEntityHandle& Activator)
	{
		const int32 Requested = OneBased - 1;
		if (CurrentFloor == INDEX_NONE || bLocked)
		{
			return;   // retail ignores retargets while moving and all requests while locked
		}
		if (Requested == CurrentFloor)
		{
			FireReachOutputs(CurrentFloor, Activator);   // synchronous in retail
			return;
		}
		// Retail's exact guard is index < 9 && index <= numfloors && index >= 0. The table itself
		// has eight rows, so cap to its real storage while preserving the inclusive numfloors test.
		if (Requested < 0 || Requested >= 8 || Requested > NumFloors
			|| !Def || !Def->ElevatorFloors.IsValidIndex(Requested))
		{
			UE_LOG(LogElysiumMover, Warning, TEXT("%s asked to go to invalid floor %d"),
				*DebugString(), Requested);
			return;
		}
		if (!Body)
		{
			UE_LOG(LogElysiumMover, Warning, TEXT("%s cannot move without a brush body"),
				*DebugString());
			return;
		}

		LastActivator = Activator;
		MoveStartZ = Body->GetRelativeLocation().Z;
		TargetFloor = Requested;
		Passed.Init(false, 8);
		const FVector Destination(Origin.X, Origin.Y, Def->ElevatorFloors[Requested]);
		LinearMove(Destination, Speed * MoverInchToCm);
		CurrentFloor = INDEX_NONE;
		PlayMoverSoundRel(StartSound);
		static const FName OnMoveStart(TEXT("OnMoveStart"));
		FireOutput(OnMoveStart, Activator);
	}

	void InputSnapToFloor(int32 OneBased)
	{
		const int32 Requested = OneBased - 1;
		if (!Def || !Def->ElevatorFloors.IsValidIndex(Requested) || Requested >= 8)
		{
			return;
		}
		SnapBody(FVector(Origin.X, Origin.Y, Def->ElevatorFloors[Requested]), FRotator::ZeroRotator);
		CurrentFloor = Requested;
		TargetFloor = INDEX_NONE;
		NextThink = ELYSIUM_NEVER_THINK;
	}

	void InputCallCurrentFloorOutputs(const FElysiumEntityHandle& Activator)
	{
		if (CurrentFloor != INDEX_NONE)
		{
			FireReachOutputs(CurrentFloor, Activator);
		}
	}

	virtual void Think() override
	{
		if (bRestoreSeatPending)
		{
			bRestoreSeatPending = false;
			if (Def && Def->ElevatorFloors.IsValidIndex(CurrentFloor))
			{
				SnapBody(FVector(Origin.X, Origin.Y, Def->ElevatorFloors[CurrentFloor]),
					FRotator::ZeroRotator);
			}
			NextThink = ELYSIUM_NEVER_THINK;
			return;
		}
		if (!IsMoving())
		{
			return;
		}
		TickMove(World ? World->NowSeconds() : 0.0);
		if (IsMoving())
		{
			FirePassedFloors();
		}
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		int32 SavedFloor = CurrentFloor == INDEX_NONE ? TargetFloor : CurrentFloor;
		Ar << SavedFloor;
		Ar << bLocked;
		if (Ar.IsLoading())
		{
			CurrentFloor = FMath::Clamp(SavedFloor, 0, 7);
			TargetFloor = INDEX_NONE;
			bRestoreSeatPending = true;
			NextThink = 0.0f;
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Current floor"), CurrentFloor == INDEX_NONE
			? TEXT("(moving)") : FString::FromInt(CurrentFloor + 1));
		Out.Emplace(TEXT("Target floor"), TargetFloor == INDEX_NONE
			? TEXT("(none)") : FString::FromInt(TargetFloor + 1));
		Out.Emplace(TEXT("Locked"), bLocked ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Speed"), FString::Printf(TEXT("%.2f in/s"), Speed));
	}

protected:
	virtual void MoveDone() override
	{
		FirePassedFloors();
		const int32 Arrived = TargetFloor;
		TargetFloor = INDEX_NONE;
		PlayMoverSoundRel(StopSound);
		FireReachOutputs(Arrived, LastActivator);
	}

private:
	int32 CurrentFloor = 0;
	int32 TargetFloor = INDEX_NONE;
	float MoveStartZ = 0.0f;
	FElysiumEntityHandle LastActivator;
	TArray<bool> Passed;
	bool bRestoreSeatPending = false;

	void FireReachOutputs(int32 Floor, const FElysiumEntityHandle& Activator)
	{
		if (Floor < 0 || Floor >= 8)
		{
			return;
		}
		static const FName OnReachFloorAny(TEXT("OnReachFloorAny"));
		FireOutput(OnReachFloorAny, Activator);
		FireOutput(FName(*FString::Printf(TEXT("OnReachFloor%d"), Floor + 1)), Activator);
		CurrentFloor = Floor;   // retail writes m_nCurrFloor after firing both outputs
	}

	void FirePassedFloors()
	{
		if (!Body || !Def || TargetFloor == INDEX_NONE)
		{
			return;
		}
		const float Z = Body->GetRelativeLocation().Z;
		const float TargetZ = Def->ElevatorFloors[TargetFloor];
		// Retail only owns OnPassFloor2..OnPassFloor7 (indices 1..6).
		for (int32 Floor = 1; Floor < 7 && Def->ElevatorFloors.IsValidIndex(Floor); ++Floor)
		{
			if (Floor == TargetFloor || Passed[Floor])
			{
				continue;
			}
			const float FloorZ = Def->ElevatorFloors[Floor];
			const bool bCrossed = MoveStartZ <= TargetZ
				? (MoveStartZ < FloorZ && Z >= FloorZ)
				: (MoveStartZ > FloorZ && Z <= FloorZ);
			if (bCrossed)
			{
				Passed[Floor] = true;
				static const FName OnPassFloorAny(TEXT("OnPassFloorAny"));
				FireOutput(OnPassFloorAny, LastActivator);
				FireOutput(FName(*FString::Printf(TEXT("OnPassFloor%d"), Floor + 1)),
					LastActivator);
			}
		}
	}
};

static TUniquePtr<FElysiumEntity> MakeElevator() { return MakeUnique<FElysiumElevator>(); }

static FElysiumClassRegistrar GRegFuncElevator(
	TEXT("func_elevator"), ElysiumBaseClassName(), &MakeElevator,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("GotoFloor"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumElevator&>(E).InputGotoFloor(A.Param.ToInt(), A.Activator); });
		D.Input(TEXT("SnapToFloor"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumElevator&>(E).InputSnapToFloor(A.Param.ToInt()); });
		D.Input(TEXT("Lock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ static_cast<FElysiumElevator&>(E).InputLock(); });
		D.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ static_cast<FElysiumElevator&>(E).InputUnlock(); });
		D.Input(TEXT("CallCurrentFloorOutputs"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumElevator&>(E).InputCallCurrentFloorOutputs(A.Activator); });
		ElysiumAddClassField(D, TEXT("speed"), &FElysiumElevator::Speed);
		ElysiumAddClassField(D, TEXT("numfloors"), &FElysiumElevator::NumFloors);
		ElysiumAddClassField(D, TEXT("locked"), &FElysiumElevator::bLocked);
		ElysiumAddClassField(D, TEXT("startsound"), &FElysiumElevator::StartSound);
		ElysiumAddClassField(D, TEXT("stopsound"), &FElysiumElevator::StopSound);
	});
