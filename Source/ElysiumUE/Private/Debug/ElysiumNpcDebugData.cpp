#include "Debug/ElysiumNpcDebugData.h"

#if !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

namespace
{
	FString HandleLabel(const FElysiumEntityWorld& World, const FElysiumEntityHandle& Handle)
	{
		if (!Handle.IsSet())
		{
			return TEXT("(none)");
		}
		const FElysiumEntity* Entity = ElysiumNpcCond::ResolveEnemyHandle(World, Handle);
		if (Entity == nullptr)
		{
			return FString::Printf(TEXT("%s (gone)"), *Handle.ToString());
		}
		const FString Name = Entity->TargetName.IsEmpty()
			? FString::Printf(TEXT("#%d"), Handle.Index) : Entity->TargetName;
		return Entity->IsInert() ? Name + TEXT(" (dead)") : Name;
	}

	FString AdmissionName(FElysiumNpcMind::EAdmission Admission)
	{
		switch (Admission)
		{
		case FElysiumNpcMind::EAdmission::Spawned: return TEXT("Spawned");
		case FElysiumNpcMind::EAdmission::Armed: return TEXT("Armed");
		case FElysiumNpcMind::EAdmission::Admitted: return TEXT("Admitted");
		default: return TEXT("Unknown");
		}
	}

	FString TaskOperand(const FElysiumTaskStep& Step)
	{
		if (!Step.Activity.IsEmpty())
		{
			return Step.Activity;
		}
		if (Step.Target != EElysiumScheduleId::None)
		{
			return ElysiumScheduleName(Step.Target);
		}
		if (Step.Flag != EElysiumNpcFlag::None)
		{
			return FString::Printf(TEXT("NPCFlag:%d"), static_cast<int32>(Step.Flag));
		}
		return FString::Printf(TEXT("%.3f"), Step.Param);
	}
}

void FElysiumNpcDebugMemoryRow::Serialize(FArchive& Ar)
{
	Ar << Handle << LastPosition << Anchor << Velocity << LastSeenTime;
	Ar << LastNavNode << AnchorNavNode;
	uint8 Flags = (bPositionOnly ? 1u : 0u) | (bEluded ? 2u : 0u);
	Ar << Flags;
	if (Ar.IsLoading())
	{
		bPositionOnly = (Flags & 1u) != 0;
		bEluded = (Flags & 2u) != 0;
	}
}

void FElysiumNpcDebugData::Reset()
{
	*this = FElysiumNpcDebugData();
}

void FElysiumNpcDebugData::Build(const FElysiumNpc& Npc, const FElysiumEntityWorld& World,
	EElysiumNpcDebugRows Rows)
{
	Reset();
	const bool bWithRows = Rows == EElysiumNpcDebugRows::Full;
	bValid = true;
	Epoch = World.GetEpoch();
	HandleIndex = Npc.Handle.Index;
	TargetName = Npc.TargetName;
	ClassName = Npc.Def ? Npc.Def->Classname : FString();
	Model = Npc.Model;

	const FElysiumNpcMind& Mind = Npc.GetMind();
	Admission = AdmissionName(Mind.Admission());
	State = LexToString(Mind.State());
	IdealState = LexToString(Mind.IdealState());
	BodyOwner = LexToString(Mind.Owner());
	SuspendedOwner = LexToString(Mind.SuspendedOwner());
	OwnerGeneration = Mind.Generation();
	LastTransition = Mind.LastTransition();
	Trace = Mind.Trace();
	if (Trace.Num() > MaxTraceRows)
	{
		Trace.RemoveAt(0, Trace.Num() - MaxTraceRows, EAllowShrinking::No);
	}

	const FElysiumScheduleState& ScheduleState = Npc.Schedule;
	if (ScheduleState.IsRunning())
	{
		ScheduleName = ElysiumScheduleName(ScheduleState.Current);
		ScheduleNumber = ElysiumScheduleNumber(ScheduleState.Current);
		TaskIndex = ScheduleState.TaskIndex;
		FailSchedule = ScheduleState.FailScheduleOverride == EElysiumScheduleId::None
			? TEXT("(program default)") : ElysiumScheduleName(ScheduleState.FailScheduleOverride);
		ToleranceUnits = ScheduleState.ToleranceUnits;
		bTaskStarted = ScheduleState.bTaskStarted;
		if (const FElysiumSchedule* Program = ElysiumScheduleFor(ScheduleState.Current))
		{
			TaskCount = Program->Tasks.Num();
			if (Program->Tasks.IsValidIndex(TaskIndex))
			{
				const FElysiumTaskStep& Step = Program->Tasks[TaskIndex];
				CurrentTask = ElysiumTaskName(Step.Task);
				CurrentTaskOperand = TaskOperand(Step);
			}
			for (int32 Index = 0; bWithRows && Index < Program->Tasks.Num() && Index < MaxTaskRows;
				++Index)
			{
				const FElysiumTaskStep& Step = Program->Tasks[Index];
				TaskRows.Add(FString::Printf(TEXT("%d%s %s (%s)"), Index,
					Index == TaskIndex ? TEXT(" *") : TEXT(""), ElysiumTaskName(Step.Task),
					*TaskOperand(Step)));
			}
		}
	}

	const FElysiumNpcConditions& Gathered = Npc.Cognition.Conditions;
	Conditions = Gathered.Describe();
	GatheredAt = Npc.Cognition.GatheredAt;
	if (const FElysiumSchedule* Program = ScheduleState.IsRunning()
		? ElysiumScheduleFor(ScheduleState.Current) : nullptr)
	{
		Interrupts = Program->Interrupts.Describe();
		InterruptHits = Gathered.Intersection(Program->Interrupts).Describe();
	}

	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	SenseOrigin = Npc.EyePosition();
	SenseForward = FElysiumNpcSenses::ViewForward(Npc);
	// The apex the cone test shifts back to (`ViewConeBodyOffsetCm`), so the drawn cone is the
	// tested cone. The radius and the LOS line still hang off the eye.
	ConeApex = SenseOrigin - SenseForward * Npc.Senses.ViewConeBodyOffsetCm;
	VisionRadiusCm = Npc.Senses.Perception.VisionDistanceCm;
	ViewConeHalfAngleRadians = FMath::Acos(ElysiumNpcSense::DefaultViewConeDot);
	HearingScalar = Npc.Senses.Perception.HearingScalar;
	if (Memory.BestSound.Serial != 0)
	{
		bHasHearingRadius = true;
		HearingOrigin = Npc.EyePosition();
		HearingRadiusCm = FMath::Max(0.0f,
			Memory.BestSound.UnadjustedRadiusCm * HearingScalar
			- Memory.BestSound.StealthHearingReductionCm);
		HearingCategory = Memory.BestSound.Category.ToString();
	}

	bHasEnemy = Memory.Enemy.IsSet();
	Enemy = HandleLabel(World, Memory.Enemy);
	bEnemyOccluded = Memory.bEnemyOccluded;
	EnemyLosFailures = Memory.EnemyLosFailures;
	if (const FElysiumEntity* EnemyEntity = ElysiumNpcCond::ResolveEnemyHandle(World, Memory.Enemy))
	{
		EnemyPosition = EnemyEntity->EyePosition();
	}
	EnemyMemoryCount = Npc.EnemyMemory.Records().Num();
	for (const FElysiumNpcEnemyMemoryRecord& Record : Npc.EnemyMemory.Records())
	{
		if (!bWithRows || EnemyMemory.Num() >= MaxMemoryRows)
		{
			break;
		}
		FElysiumNpcDebugMemoryRow& Row = EnemyMemory.AddDefaulted_GetRef();
		Row.Handle = Record.bPositionOnly ? TEXT("(position-only)") : HandleLabel(World, Record.Handle);
		Row.LastPosition = Record.LastPosition;
		Row.Anchor = Record.Anchor;
		Row.Velocity = Record.Velocity;
		Row.LastSeenTime = Record.LastSeenTime;
		Row.LastNavNode = Record.LastNavNode;
		Row.AnchorNavNode = Record.AnchorNavNode;
		Row.bPositionOnly = Record.bPositionOnly;
		Row.bEluded = Record.bEluded;
	}

	if (const FElysiumInterestingPlace* Spot = Npc.GetCurrentAmbientSpotForDebug())
	{
		bHasPlace = true;
		Place = Spot->TargetName.IsEmpty() ? Spot->Handle.ToString() : Spot->TargetName;
		PlacePosition = Spot->Origin;
		PlaceType = Spot->Type;
		PlaceGroup = Spot->GroupId;
		PlaceRating = Spot->Rating;
		AmbientPhase = Npc.GetAmbientPhaseForDebug();
	}
}

namespace
{
	// A bounded row array: the count travels first and a load rejects anything past the cap the
	// builder honours, so a torn pack cannot grow the receiver's arrays.
	template <typename RowType, typename RowSerializer>
	bool SerializeRows(FArchive& Ar, TArray<RowType>& Rows, int32 MaxRows,
		RowSerializer&& SerializeRow)
	{
		int32 Count = Rows.Num();
		Ar << Count;
		if (Ar.IsLoading())
		{
			if (Count < 0 || Count > MaxRows)
			{
				Ar.SetError();
				Rows.Reset();
				return false;
			}
			Rows.SetNum(Count);
		}
		for (RowType& Row : Rows)
		{
			SerializeRow(Ar, Row);
		}
		return true;
	}

	void SerializeString(FArchive& Ar, FString& Row)
	{
		Ar << Row;
	}
}

void FElysiumNpcDebugData::Serialize(FArchive& Ar)
{
	Ar << bValid << Error << Epoch << HandleIndex << TargetName << ClassName << Model;
	Ar << Admission << State << IdealState << BodyOwner << SuspendedOwner << OwnerGeneration;
	Ar << LastTransition;
	if (!SerializeRows(Ar, Trace, MaxTraceRows, &SerializeString))
	{
		return;
	}
	Ar << ScheduleName << ScheduleNumber << TaskIndex << TaskCount << CurrentTask;
	Ar << CurrentTaskOperand << FailSchedule << ToleranceUnits << bTaskStarted;
	if (!SerializeRows(Ar, TaskRows, MaxTaskRows, &SerializeString))
	{
		return;
	}
	Ar << Conditions << Interrupts << InterruptHits << GatheredAt;
	Ar << SenseOrigin << SenseForward << ConeApex << VisionRadiusCm << ViewConeHalfAngleRadians;
	Ar << HearingScalar << bHasHearingRadius << HearingOrigin << HearingRadiusCm << HearingCategory;
	Ar << bHasEnemy << Enemy << EnemyPosition << bEnemyOccluded << EnemyLosFailures;
	Ar << EnemyMemoryCount;
	if (!SerializeRows(Ar, EnemyMemory, MaxMemoryRows,
		[](FArchive& InAr, FElysiumNpcDebugMemoryRow& Row) { Row.Serialize(InAr); }))
	{
		return;
	}
	Ar << bHasPlace << Place << PlacePosition << PlaceType << PlaceGroup << PlaceRating << AmbientPhase;
}


#endif // !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER
