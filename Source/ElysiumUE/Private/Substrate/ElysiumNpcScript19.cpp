// Story 0019/8 (29e under the strict verdict), family **Script19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcScript19.inl` (included inside `class FElysiumNpc`) or generated
// in `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved
// here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the
// marker.
//
// Owns (Script19's `rule` rows) here: 0x1029f460 (`BuildPatrolPath`), 0x10278220
// (`TryMoveToHiddenPosition`), 0x102800c0 ScheduledMoveToGoalEntity, 0x102801e0 ScheduledFollowPath,
// 0x102aa640 / 0x102aa860 (`IssuePatrolMoveStart` / `IssuePatrolMoveRun`). The director rows the
// shape commit listed here live in `ElysiumScriptedSequence.cpp`, 0x1027d0a0 in
// `ElysiumNpcBaseScript19.cpp`, 0x1038b1a0 in `ElysiumNpcScript19Species.cpp`. Walked prose:
// `docs/vtmb/npc-ai/story8/Script19.md`.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumNpcGait.h"
#include "Substrate/ElysiumNpcLog.h"

namespace
{
	// `s_E__Vampire_main_dlls_AI_BaseNPCT_105da024`, the `__FILE__` every Troika stamp here carries.
	const TCHAR* const GScript19TroikaFile = TEXT("E:\\Vampire\\main\\dlls\\AI_BaseNPCTroika.cpp");

	// `NPC_STATE_DEAD`, the state `0x1029f460` refuses to build a path in.
	constexpr int32 GScript19NpcStateDead = 7;

	// The patrol-type table `DAT_1049df20`, stride 0x14: `{type, name, startIndex, step, next}`. Only
	// the start index (`+0x08`, `DAT_1049df28`) is read here, by `0x10307c20`; read out of the pinned
	// image at `0x1049df28 + type * 0x14`.
	constexpr int32 GScript19PatrolTypeStart[] = { 0, 0x7fff, 0, 0x7fff };

	// Task fail reasons (the text table at `0x106152b0`): `0x1d` no patrol path, `0xc` no route.
	constexpr int32 GScript19FailNoPatrolPath = 0x1d;
	constexpr int32 GScript19FailNoRoute = 0x0c;

	// The `+0x1b48` source lines the two patrol arms stamp beside their `TaskFail`.
	constexpr int32 GScript19PatrolStartNoPathLine = 0x3d39;
	constexpr int32 GScript19PatrolStartNoNodeLine = 0x3d60;
	constexpr int32 GScript19PatrolStartMinusOneLine = 0x3d65;
	constexpr int32 GScript19PatrolStartNoRouteLine = 0x3d55;
	constexpr int32 GScript19PatrolRunNoPathLine = 0x3d73;
	// The `+0x1b34` line `0x1029f460` stamps before its `SetSchedule`.
	constexpr int32 GScript19BuildPatrolScheduleLine = 0x27a1;

	// `AI_NavGoal_t [0]`: 4 (`GOALTYPE_LOCATION`) for every Script19 goal but `0x102801e0`'s, which
	// is 3 (`GOALTYPE_PATHCORNER`, `0x10280205 MOV [ESP+0x8],0x3`). `SetGoal` routes type 3 through
	// its location arm (it is not 1 / 2 / 7), so the two differ only in the word.
	constexpr int32 GScript19GoalTypeLocation = 4;
	constexpr int32 GScript19GoalTypePathCorner = 3;
	// `ACT_RUN` / `ACT_WALK`, the movement activities the Script19 goals name.
	constexpr int32 GScript19ActRun = 0x13;
	constexpr int32 GScript19ActWalk = 9;
	// `0x102800c0`'s tolerance word, `0x43000000` = 128.0 SOURCE units.
	constexpr float GScript19GoalEntityToleranceUnits = 128.0f;
	// `0x102800c0` / `0x102801e0`'s goal flags word `[9]`.
	constexpr int32 GScript19ScheduledGoalFlags = 1;
	// `TASK_FIND_COVER`'s trace mask `0x2804091` and the move-probe mask `0x202400b` (MASK_NPCSOLID),
	// carried by the port's sight and lateral-cover services.

	// The pool (`0x10934158`, 32 slots of 0x114 bytes), its in-use bytes (`DAT_109363d8`) and its
	// cursor (`DAT_109363f8`). A DLL global in retail; a process static here.
	struct FScript19PatrolPool
	{
		FElysiumNpc::FPatrolPathRecord Slots[FElysiumNpc::PatrolPathPoolSize];
		bool bInUse[FElysiumNpc::PatrolPathPoolSize] = {};
		int32 Cursor = 0;
		int32 NodeMisses = 0;   // DAT_106c994c
	};

	FScript19PatrolPool& Script19PatrolPool()
	{
		static FScript19PatrolPool Pool;
		return Pool;
	}

	// `0x10307aa0` -- `+0x00 := -1`, `+0x04 := 0`, `+0x0c := 0`. The repeat word, the current index
	// and the node array are NOT touched.
	void Script19ResetPatrolPath(FElysiumNpc::FPatrolPathRecord& Path)
	{
		Path.Type = -1;
		Path.Schedule = 0;
		Path.Count = 0;
	}
}

// --- The pool --------------------------------------------------------------------------------------

// `0x10307d30`.
FElysiumNpc::FPatrolPathRecord* FElysiumNpc::AllocPatrolPath()
{
	FScript19PatrolPool& Pool = Script19PatrolPool();
	int32 Index = Pool.Cursor;
	while (true)
	{
		if (Index > PatrolPathPoolSize - 1)   // 0x1f < cursor
		{
			// `Error(...)` is fatal in retail (`Sys_Error`); the port logs and answers null, which every
			// caller's null arm then takes (crash guard).
			UE_LOG(LogElysiumNpcEnt, Error, TEXT("Patrol path pool is dry.  It will store up to %d paths.  "
				"Change PATROL_PATH_POOL_MAX_PATHS to increase this amount."), PatrolPathPoolSize);
			return nullptr;
		}
		if (!Pool.bInUse[Index])
		{
			break;
		}
		++Index;
	}
	Pool.bInUse[Index] = true;
	++Pool.Cursor;   // the CURSOR is bumped by one, not moved past the slot found (retail's)
	return &Pool.Slots[Index];
}

// `0x10307db0`.
void FElysiumNpc::FreePatrolPath(FPatrolPathRecord* Path)
{
	FScript19PatrolPool& Pool = Script19PatrolPool();
	if (Path == nullptr)
	{
		return;
	}
	const int32 Index = static_cast<int32>(Path - Pool.Slots);
	if (Index < 0 || Index >= PatrolPathPoolSize)
	{
		return;
	}
	Pool.bInUse[Index] = false;
	if (Index < Pool.Cursor)
	{
		Pool.Cursor = Index;
	}
}

// `0x10307d00`.
void FElysiumNpc::ResetPatrolPathPool()
{
	FScript19PatrolPool& Pool = Script19PatrolPool();
	for (int32 Index = 0; Index < PatrolPathPoolSize; ++Index)
	{
		Pool.bInUse[Index] = false;   // 0x10307e00
		Pool.Slots[Index] = FPatrolPathRecord();
	}
	Pool.Cursor = 0;
	Pool.NodeMisses = 0;
}

// `0x10307c20`.
int32 FElysiumNpc::PatrolPathStartIndex(const FPatrolPathRecord& Path)
{
	const int32 Last = Path.Count - 1;
	// A type outside 0..3 indexes past the four-row table in retail (garbage); no builder writes one
	// (the non-replace path forces 0, `0x103079c0` answers 0..3), so it answers `Last` here.
	if (Path.Type < 0 || Path.Type >= static_cast<int32>(UE_ARRAY_COUNT(GScript19PatrolTypeStart)))
	{
		return Last;
	}
	const int32 Start = GScript19PatrolTypeStart[Path.Type];
	return Start <= Last ? Start : Last;
}

int32& FElysiumNpc::PatrolNodeMissCounter()
{
	return Script19PatrolPool().NodeMisses;
}

// --- `0x1029f460` ----------------------------------------------------------------------------------

void FElysiumNpc::BuildPatrolPath(FPatrolPathCell* Cell, int32 Repeat, int32 Type, int32 ScheduleId,
	const int32* NodeIds, EPatrolPathBuild Build)
{
	if (NpcStateRetail() == GScript19NpcStateDead)            // 0x1029f464 / 0x1029f46b
	{
		return;                                               // 0x1029f570 -> 0x1029f572
	}
	if (Cell == nullptr)                                      // 0x1029f471 / 0x1029f477
	{
		return;
	}
	bool bSkipSeed = false;
	if (Build == EPatrolPathBuild::Extend)                    // 0x1029f47d / 0x1029f486 JZ 0x1029f4cf
	{
		if (Cell->Path != nullptr)                            // 0x1029f4cf / 0x1029f4d1 JNZ 0x1029f4f9
		{
			bSkipSeed = true;                                 // straight to the append
		}
		else
		{
			Cell->Path = AllocPatrolPath();                   // 0x1029f4d3 / 0x1029f4da
			Cell->bOwned = true;                              // 0x1029f4dd
			if (Cell->Path == nullptr)
			{
				// Retail resets the null slot here (`0x1029f4e0`) and faults; crash guard.
				return;
			}
			Script19ResetPatrolPath(*Cell->Path);             // 0x1029f4e0 0x10307aa0
			Type = 0;                                         // param_3 := 0
			Cell->Path->Repeat = 0;                           // 0x1029f4ea
		}
	}
	else
	{
		if (Cell->Path == nullptr)                            // 0x1029f48a
		{
			Cell->Path = AllocPatrolPath();                   // 0x1029f48c / 0x1029f491
			Cell->bOwned = true;                              // 0x1029f494
		}
		if (Cell->Path == nullptr)                            // 0x1029f497 / 0x1029f49c
		{
			UE_LOG(LogElysiumNpcEnt, Log, TEXT("Failed to create patrol path for %s"), *DebugString()); // 0x1029f4a0..0x1029f4ab
			return;                                           // 0x1029f4b6
		}
		Script19ResetPatrolPath(*Cell->Path);             // 0x1029f4b9 0x10307aa0
		Cell->Path->Repeat = Repeat;                          // 0x1029f4c9
	}
	if (!bSkipSeed)
	{
		Cell->Path->Type = Type;                              // 0x1029f4f4 0x10307b40
	}
	// 0x1029f4f9..0x1029f51b: append every id until the -1 terminator (0x10307bf0).
	if (NodeIds != nullptr)                                   // 0x1029f500
	{
		for (const int32* Id = NodeIds; *Id != -1; ++Id)      // 0x1029f507 / 0x1029f51b
		{
			if (Cell->Path->Count >= PatrolPathNodeCapacity)
			{
				break;   // retail writes into the next slot; crash guard
			}
			Cell->Path->Nodes[Cell->Path->Count++] = *Id;     // 0x1029f50d
		}
	}
	Cell->Path->Current = PatrolPathStartIndex(*Cell->Path);  // 0x1029f520 0x10307b60
	if (ScheduleId != 0)                                        // 0x1029f52c
	{
		Cell->Path->Schedule = ScheduleId;                      // 0x1029f531
	}
	if (Cell->Path->Schedule != 0)                            // 0x1029f534..0x1029f53c
	{
		// `0x1029f650(this, this+0x658c)` -- ALWAYS the patrol cell, whichever cell was built. The
		// port's draw takes the node that cell's path points at.
		const FPatrolPathRecord* Patrol = PatrolPathCell.Path;
		const int32 Node = Patrol != nullptr && Patrol->Current >= 0 && Patrol->Current < PatrolPathNodeCapacity
			? Patrol->Nodes[Patrol->Current] : -1;
		FUN_1029f650(Node);                                   // 0x1029f547
		RecordScheduleEvent(FString::Printf(TEXT("SetSchedule trace %s:%d"), GScript19TroikaFile,
			GScript19BuildPatrolScheduleLine));               // 0x1029f54c / 0x1029f556 +0x1b30/+0x1b34
		SetSchedule(Cell->Path->Schedule, false);             // 0x1029f56b 0x102ae750(path+4, 0)
	}
}                                                             // 0x1029f572

// --- `0x1029f5d0` and the patrol inputs' helpers ------------------------------------------------------

void FElysiumNpc::ReleasePatrolPath(FPatrolPathCell* Cell)
{
	if (Cell == nullptr || Cell->Path == nullptr)             // 0x1029f5d4 / 0x1029f5d9
	{
		return;
	}
	Cell->bOwned = false;                                     // 0x1029f5dd
	FreePatrolPath(Cell->Path);                               // 0x1029f5e0 0x10307db0
	Cell->Path = nullptr;                                     // 0x1029f5e8
}

int32 FElysiumNpc::PatrolCurrentNode(const FPatrolPathCell& Cell)
{
	const FPatrolPathRecord* Path = Cell.Path;
	return Path != nullptr && Path->Current >= 0 && Path->Current < PatrolPathNodeCapacity
		? Path->Nodes[Path->Current] : INDEX_NONE;
}

int32 FElysiumNpc::PatrolTypeForName(const FString& Name)
{
	// `DAT_1049df20`, five dwords a row: `{id, name, start, step, next}`; the names are the ids.
	static const TCHAR* const Names[] = { TEXT("0"), TEXT("1"), TEXT("2"), TEXT("3") };
	for (int32 Row = 0; Row < UE_ARRAY_COUNT(Names); ++Row)
	{
		if (Name.Equals(Names[Row], ESearchCase::IgnoreCase))    // __strcmpi
		{
			return Row;
		}
	}
	UE_LOG(LogElysiumNpcEnt, Error, TEXT("Invalid Path Type String.  Valid types: 0123"));
	return 0;
}

int32 FElysiumNpc::PatrolScheduleForName(const FString& Token) const
{
	const FElysiumIdNamespace& Schedules =
		FElysiumScheduleCorpus::Get().Namespace(EElysiumIdCategory::Schedule);
	int32 Global = Schedules.Find(Token);                                          // 0x1029f37a
	if (Global == INDEX_NONE)
	{
		Global = Schedules.Find(FString::Printf(TEXT("SCHED_%s"), *Token));         // 0x1029f39a
	}
	if (Global == INDEX_NONE)
	{
		Global = Schedules.Find(FString::Printf(TEXT("SCHED_TROIKA_%s"), *Token));  // 0x1029f3bb
	}
	if (Global == INDEX_NONE)
	{
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("ERROR: %s: Could not find schedule '%s'"),
			*DebugString(), *Token);
		return 0;
	}
	const int32 Local = const_cast<FElysiumNpc*>(this)->GetLocalScheduleId(Global); // 0x1029f3d9 slot 580 / 0x102ea280
	if (Local == INDEX_NONE)
	{
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("ERROR: %s: Could not convert schedule '%s' to a local id"),
			*DebugString(), *Token);
		return 0;
	}
	return Local;
}

int32 FElysiumNpc::PatrolNodeIdFor(const FString& Token) const
{
	const FElysiumEntity* Hint = FindPatrolPoint(Token);
	return Hint != nullptr ? Hint->Handle.Index : INDEX_NONE;
}

// --- The node graph seam ---------------------------------------------------------------------------

FElysiumNpc::EPatrolNode FElysiumNpc::PatrolNodePosition(int32 NodeId, FVector& OutPositionCm) const
{
	const int32 Count = World != nullptr ? World->Entities().Num() : 0;   // network +0x0
	if (NodeId < 0 || NodeId >= Count)
	{
		return EPatrolNode::OutOfRange;
	}
	const FElysiumHint* Hint = FElysiumHint::Cast(World->Entities()[NodeId].Get());   // network +0x4 [id]
	if (Hint == nullptr || Hint->IsDead())
	{
		return EPatrolNode::Null;
	}
	OutPositionCm = Hint->Origin;   // CAI_Node::GetPosition(m_eHull) 0x102fb0d0
	return EPatrolNode::Found;
}

// --- `0x102aa640` ----------------------------------------------------------------------------------

void FElysiumNpc::IssuePatrolMoveStart(FPatrolPathCell* Cell)
{
	auto Fail = [this](int32 Line, int32 Reason)
	{
		// `+0x1b44 = file, +0x1b48 = line`, then `TaskFail(reason)` (slot 448, `+0x700`).
		RecordScheduleEvent(FString::Printf(TEXT("TaskFail trace %s:%d"), GScript19TroikaFile, Line));
		TaskFail(Reason);
	};
	if (Cell == nullptr || Cell->Path == nullptr)             // 0x102aa64d / 0x102aa658 -> 0x102aa7c8
	{
		Fail(GScript19PatrolStartNoPathLine, GScript19FailNoPatrolPath);   // 0x102aa7c8..0x102aa7e2
		return;                                               // 0x102aa7ed
	}
	const FPatrolPathRecord& Path = *Cell->Path;
	const int32 NodeId = Path.Current >= 0 && Path.Current < PatrolPathNodeCapacity
		? Path.Nodes[Path.Current] : -1;                      // 0x102aa65e / 0x102aa664
	if (NodeId == -1)                                         // 0x102aa66a JZ 0x102aa7a0
	{
		Fail(GScript19PatrolStartMinusOneLine, GScript19FailNoPatrolPath); // 0x102aa7a6..0x102aa7ba
		return;                                               // 0x102aa7c5
	}
	FVector PositionCm = FVector::ZeroVector;
	const EPatrolNode Node = PatrolNodePosition(NodeId, PositionCm); // 0x102aa670..0x102aa68c
	if (Node == EPatrolNode::OutOfRange)                      // 0x102aa67b JL / 0x102aa683 JGE
	{
		++PatrolNodeMissCounter();                            // 0x102aa750 INC DAT_106c994c
	}
	if (Node != EPatrolNode::Found)                           // 0x102aa691 JZ 0x102aa756
	{
		Fail(GScript19PatrolStartNoNodeLine, GScript19FailNoPatrolPath);   // 0x102aa756 +0x1b48 = 0x3d60, 0x102aa7e2
		return;
	}
	FScript19NavGoal Goal;
	Goal.Type = GScript19GoalTypeLocation;                    // local_40 = 4
	Goal.PositionCm = PositionCm;                             // 0x102aa6b6 GetPosition(node, m_eHull +0x1568)
	Goal.Activity = -1;                                       // local_2c
	Goal.Tolerance = NavGoalToleranceKeep;                    // local_20 = DAT_1049a1ac (-1.0)
	Goal.Flags = 0;                                           // local_1c
	if (Script19SetGoal(Goal, 2, TEXT("patrol point (0x102aa640)")))  // 0x102aa735 SetGoal(goal, 2) / 0x102aa73f
	{
		TaskComplete(false);                                  // 0x102aa743 0x10273e80(0)
		return;                                               // 0x102aa74d
	}
	UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s can't reach patrol point"), *DebugString()); // 0x102aa762..0x102aa76f DevWarning(2)
	Fail(GScript19PatrolStartNoRouteLine, GScript19FailNoRoute); // 0x102aa77c..0x102aa792
}                                                             // 0x102aa79d

// --- `0x102aa860` ----------------------------------------------------------------------------------

void FElysiumNpc::IssuePatrolMoveRun(FPatrolPathCell* Cell)
{
	if (Cell == nullptr || Cell->Path == nullptr)             // 0x102aa86d / 0x102aa878 -> 0x102aa963
	{
		RecordScheduleEvent(FString::Printf(TEXT("TaskFail trace %s:%d"), GScript19TroikaFile,
			GScript19PatrolRunNoPathLine));                   // 0x102aa969 / 0x102aa973
		TaskFail(GScript19FailNoPatrolPath);                  // 0x102aa97d slot 448
		return;                                               // 0x102aa988
	}
	const FPatrolPathRecord& Path = *Cell->Path;
	const int32 NodeId = Path.Current >= 0 && Path.Current < PatrolPathNodeCapacity
		? Path.Nodes[Path.Current] : -1;                      // 0x102aa87e / 0x102aa884
	if (NodeId == -1)                                         // 0x102aa88a JZ 0x102aa983
	{
		return;                                               // silently: no goal, no fail
	}
	FVector PositionCm = FVector::ZeroVector;
	const EPatrolNode Node = PatrolNodePosition(NodeId, PositionCm); // 0x102aa890..0x102aa8a6
	if (Node == EPatrolNode::OutOfRange)                      // 0x102aa89d JL / 0x102aa8a1 JGE
	{
		++PatrolNodeMissCounter();                            // 0x102aa8ab..0x102aa8b3
	}
	if (Node != EPatrolNode::Found)
	{
		// Retail carries on with a NULL node into `CAI_Node::GetPosition` (`0x102aa8df`) and faults;
		// the port stops here (crash guard).
		return;
	}
	// `0x102d61b0(m_eHull)` -- `NAI_Hull::Width`, the Y span (family Positions' reading).
	FVector HullMins = FVector::ZeroVector;
	FVector HullMaxs = FVector::ZeroVector;
	RetailHullExtents(HullKind, EElysiumHullExtents::Full, HullMins, HullMaxs); // 0x102aa8b8..0x102aa8d3 (0x102aa8c5 -> 0x102d61b0)
	FScript19NavGoal Goal;
	Goal.Type = GScript19GoalTypeLocation;                    // local_40 = 4
	Goal.PositionCm = PositionCm;                             // 0x102aa8df GetPosition
	Goal.Activity = -1;                                       // local_2c
	Goal.Tolerance = static_cast<float>(HullMaxs.Y - HullMins.Y) * ElysiumMove::U; // local_20
	Goal.Flags = 0;                                           // local_1c
	Script19SetGoal(Goal, 0, TEXT("patrol point (0x102aa860)")); // 0x102aa954 SetGoal(goal, 0), answer ignored
}                                                             // 0x102aa960

// --- `0x102ecd20` onto the motor --------------------------------------------------------------------

bool FElysiumNpc::Script19SetGoal(const FScript19NavGoal& Goal, int32 SetGoalFlags, const TCHAR* Reason)
{
	// `0x102ecd20` is `FElysiumNpcBase::StartTaskSetGoal`, the one body (every arm of it and of its
	// route build `0x102f1dc0`: flag 1's `0x102f28a0`, the `[8]` resolution onto `NavPathToleranceCm`,
	// the navigator's slot-2 complete, `OnNavFailed(0xc)` gated on `NavRouteSearchTime`). This converts
	// the record: centimetres to SOURCE units for `[8]` (the two sentinels pass through), `[4]` / `[6]`
	// / `[7]` -1 as every Script19 builder writes them.
	(void)Reason;   // the caller's name; the body claim names `SetGoal 0x102ecd20` itself
	FStartTaskNavGoal Record;
	Record.Type = Goal.Type;
	Record.DestCm = Goal.PositionCm;
	Record.bDestSet = Goal.Type != 1 && Goal.Type != 2 && Goal.Type != 7;
	Record.MovementActivity = Goal.Activity;
	Record.ToleranceUnits = (Goal.Tolerance == NavGoalToleranceHull || Goal.Tolerance == NavGoalToleranceKeep)
		? Goal.Tolerance : Goal.Tolerance / ElysiumMove::U;
	Record.GoalFlags = Goal.Flags;
	Record.Target = Goal.Target;
	return StartTaskSetGoal(Record, SetGoalFlags);
}

// --- `0x10278220` ----------------------------------------------------------------------------------
// `TestLateralCover` is `CAI_BaseNPC`'s, ported once as `FElysiumNpcBase::StartTaskTestLateralCover`
// (`ElysiumNpcBaseStartTask19.cpp`), with this file's listing notes folded into it.

// --- `0x102800c0` / `0x102801e0` -------------------------------------------------------------------

bool FElysiumNpc::ScheduledMoveToGoalEntity(int32 ScheduleId, FElysiumEntity* Goal, int32 Activity)
{
	ChangeSchedule(ScheduleId);                                 // 0x102800cd 0x10280de0(param_1)
	if (Goal == nullptr)
	{
		return false;   // retail dispatches slot 217 on the null goal and faults (crash guard)
	}
	BaseScheduleHost.GoalEnt = Goal->Handle;                  // 0x102800d6 m_pGoalEnt +0x5de8
	FScript19NavGoal NavGoal;
	NavGoal.Type = GScript19GoalTypeLocation;                 // 0x102800ec [0] = 4
	NavGoal.PositionCm = Goal->Origin;                        // 0x102800e6 slot 217 GetAbsOrigin
	NavGoal.Activity = Activity;                              // 0x10280126 [5] = param_3
	NavGoal.Tolerance = GScript19GoalEntityToleranceUnits * ElysiumMove::U; // 0x10280150 [8] = 128.0
	NavGoal.Flags = GScript19ScheduledGoalFlags;              // 0x10280158 [9] = 1
	// Slot 563 with `(goal, &dest, &tolerance, &param_1)`: the fourth pointer lands on the dead
	// schedule argument's stack slot (`0x10280134 LEA EAX,[ESP+0x50]`).
	float Scratch = 0.f;
	TranslateEnemyChasePosition(Goal, NavGoal.PositionCm, &NavGoal.Tolerance, &Scratch); // 0x10280174
	return Script19SetGoal(NavGoal, 0, TEXT("ScheduledMoveToGoalEntity (0x102800c0)")); // 0x10280187, AL out
}

bool FElysiumNpc::ScheduledFollowPath(int32 ScheduleId, FElysiumEntity* Goal, int32 Activity)
{
	ChangeSchedule(ScheduleId);                                 // 0x102801ec 0x10280de0(param_1)
	if (Goal == nullptr)
	{
		return false;   // slot 220 on the null goal faults in retail (crash guard)
	}
	BaseScheduleHost.GoalEnt = Goal->Handle;                  // 0x102801f5 m_pGoalEnt +0x5de8
	FScript19NavGoal NavGoal;
	NavGoal.Type = GScript19GoalTypePathCorner;               // 0x10280205 [0] = 3
	NavGoal.PositionCm = Goal->Origin;                        // 0x102801ff slot 220
	NavGoal.Activity = Activity;                              // 0x10280227 [5] = param_3
	NavGoal.Tolerance = NavGoalToleranceKeep;                 // 0x10280231 / 0x10280276 [8] = _DAT_1049a154 (-1.0)
	NavGoal.Flags = GScript19ScheduledGoalFlags;              // [9] = 1
	float Scratch = 0.f;
	TranslateEnemyChasePosition(Goal, NavGoal.PositionCm, &NavGoal.Tolerance, &Scratch); // 0x10280295
	return Script19SetGoal(NavGoal, 0, TEXT("ScheduledFollowPath (0x102801e0)")); // 0x102802a8, AL out
}
