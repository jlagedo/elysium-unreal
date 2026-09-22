#include "Substrate/ElysiumTaskOps.h"

#include "Substrate/ElysiumIdNamespace.h"
#include "Substrate/ElysiumScheduleManager.h"

namespace
{
	struct FTaskOpRow
	{
		EElysiumTaskOp Op;
		const TCHAR* Name;
	};

	// The bodies this runtime carries, under retail's spellings. This table is the whole binding:
	// a name here that the corpus never registers binds nothing, and a name the corpus registers
	// that is not here is an unported identity. Neither is an error.
	constexpr FTaskOpRow GTaskOps[] = {
		{ EElysiumTaskOp::SpecialIdleActivity,      TEXT("TASK_SPECIAL_IDLE_ACTIVITY") },
		{ EElysiumTaskOp::WaitPvs,                  TEXT("TASK_WAIT_PVS") },
		{ EElysiumTaskOp::SetActivity,              TEXT("TASK_SET_ACTIVITY") },
		{ EElysiumTaskOp::Wait,                     TEXT("TASK_WAIT") },
		{ EElysiumTaskOp::WaitRandom,               TEXT("TASK_WAIT_RANDOM") },
		{ EElysiumTaskOp::FaceSavePosition,         TEXT("TASK_FACE_SAVEPOSITION") },
		{ EElysiumTaskOp::MoveAwayFromSavePosition, TEXT("TASK_MOVE_AWAY_PATH") },
		{ EElysiumTaskOp::SetFailSchedule,          TEXT("TASK_SET_FAIL_SCHEDULE") },
		{ EElysiumTaskOp::StopMoving,               TEXT("TASK_STOP_MOVING") },
		{ EElysiumTaskOp::FindCoverFromEnemy,       TEXT("TASK_FIND_COVER_FROM_ENEMY") },
		{ EElysiumTaskOp::SetToleranceDistance,     TEXT("TASK_SET_TOLERANCE_DISTANCE") },
		{ EElysiumTaskOp::GetPathToEnemy,           TEXT("TASK_GET_PATH_TO_ENEMY") },
		{ EElysiumTaskOp::RunPath,                  TEXT("TASK_RUN_PATH") },
		{ EElysiumTaskOp::WaitForMovement,          TEXT("TASK_WAIT_FOR_MOVEMENT") },
		{ EElysiumTaskOp::FaceEnemy,                TEXT("TASK_FACE_ENEMY") },
		{ EElysiumTaskOp::AnnounceAttack,           TEXT("TASK_ANNOUNCE_ATTACK") },
		{ EElysiumTaskOp::MeleeAttack1,             TEXT("TASK_MELEE_ATTACK1") },
		{ EElysiumTaskOp::RangeAttack1,             TEXT("TASK_RANGE_ATTACK1") },
		{ EElysiumTaskOp::SetSchedule,              TEXT("TASK_SET_SCHEDULE") },
		{ EElysiumTaskOp::Remember,                 TEXT("TASK_REMEMBER") },
		{ EElysiumTaskOp::MakeOblivious,            TEXT("TASK_MAKE_OBLIVIOUS") },
		{ EElysiumTaskOp::SetNpcFlag,               TEXT("TASK_SET_NPC_FLAG") },
		{ EElysiumTaskOp::PlayDeathSequence,        TEXT("TASK_PLAY_DEATH_SEQUENCE") },
		{ EElysiumTaskOp::GetPathToGoal,            TEXT("TASK_GET_PATH_TO_GOAL") },
	};

	const FString GNoName;
}

const TCHAR* ElysiumTaskOps::Name(EElysiumTaskOp Op)
{
	for (const FTaskOpRow& Row : GTaskOps)
	{
		if (Row.Op == Op)
		{
			return Row.Name;
		}
	}
	return TEXT("TASK_?");
}

EElysiumTaskOp ElysiumTaskOps::FromName(const FString& TaskName)
{
	for (const FTaskOpRow& Row : GTaskOps)
	{
		if (TaskName.Equals(Row.Name, ESearchCase::IgnoreCase))
		{
			return Row.Op;
		}
	}
	return EElysiumTaskOp::Unknown;
}

TConstArrayView<EElysiumTaskOp> ElysiumTaskOps::All()
{
	static const TArray<EElysiumTaskOp> Ops = []
	{
		TArray<EElysiumTaskOp> Out;
		Out.Reserve(UE_ARRAY_COUNT(GTaskOps));
		for (const FTaskOpRow& Row : GTaskOps)
		{
			Out.Add(Row.Op);
		}
		return Out;
	}();
	return Ops;
}

void FElysiumTaskOpTable::Build(const FElysiumIdNamespace& Tasks)
{
	Reset();

	for (const TPair<FString, int32>& Row : Tasks.Rows())
	{
		const EElysiumTaskOp Op = ElysiumTaskOps::FromName(Row.Key);
		Names.Add(Row.Value, Row.Key);
		if (Op != EElysiumTaskOp::Unknown)
		{
			Ops.Add(Row.Value, Op);
		}
		else
		{
			Unported.Add(Row.Key, 0);
			UnportedById.Add(Row.Value, Row.Key);
		}
	}
}

EElysiumTaskOp FElysiumTaskOpTable::Find(int32 GlobalTaskId) const
{
	const EElysiumTaskOp* Op = Ops.Find(GlobalTaskId);
	return Op != nullptr ? *Op : EElysiumTaskOp::Unknown;
}

const FString& FElysiumTaskOpTable::NameOf(int32 GlobalTaskId) const
{
	const FString* Found = Names.Find(GlobalTaskId);
	return Found != nullptr ? *Found : GNoName;
}

int32 FElysiumTaskOpTable::Measure(const FElysiumScheduleManager& Manager)
{
	for (TPair<FString, int32>& Row : Unported)
	{
		Row.Value = 0;
	}

	int32 UnportedSteps = 0;
	for (const FElysiumScheduleProgram& Program : Manager.Programs())
	{
		for (const FElysiumScheduleStep& Step : Program.Tasks)
		{
			if (const FString* Key = UnportedById.Find(Step.TaskId))
			{
				++Unported.FindChecked(*Key);
				++UnportedSteps;
			}
		}
	}
	return UnportedSteps;
}

void FElysiumTaskOpTable::Reset()
{
	Ops.Reset();
	Names.Reset();
	Unported.Reset();
	UnportedById.Reset();
}
