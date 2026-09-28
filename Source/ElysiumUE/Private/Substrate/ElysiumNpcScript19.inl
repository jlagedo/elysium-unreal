// Story 0019/8 (29e under the strict verdict), family **Script19** -- `CAI_BaseNPCTroika`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcScript19.cpp`, or generated in the slot files for a slot body.
//
// Owns (Script19's `rule` rows) on this class: 0x1029f460 FUN_1029f460 (`BuildPatrolPath`),
// 0x10278220 FUN_10278220 (`TryMoveToHiddenPosition`), 0x102800c0 ScheduledMoveToGoalEntity,
// 0x102801e0 ScheduledFollowPath, 0x102aa640 / 0x102aa860 (`IssuePatrolMove`, the StartTask and
// RunTask patrol-point arms). The `CCineNPC` rows the shape header lists here (0x101a8c30,
// 0x101a8640, 0x101a8460, 0x101a8890) are the director's own bodies and live in
// `ElysiumScriptedSequence.cpp`; 0x1027d0a0 is a `CAI_BaseNPC` body (`ElysiumNpcBaseScript19.inl`);
// 0x1038b1a0 is `CNPC_VManBat`'s (`ElysiumNpcManBat.h`).

// --- `CAI_PatrolPath`, the pooled patrol record ---------------------------------------------------

/** `CAI_PatrolPath`, one 0x114-byte slot of the static pool at `0x10934158` that `0x10307d30` hands
 *  out. Layout read off its five accessors: `0x10307aa0` resets `+0x00 := -1`, `+0x04 := 0`,
 *  `+0x0c := 0`; `0x10307b40` writes `+0x00`; `0x10307bf0` appends at `+0x14 + count*4` and bumps
 *  `+0x0c`; `0x10307b60` writes `+0x10` from `0x10307c20`. The node array is the rest of the slot:
 *  `(0x114 - 0x14) / 4` = 64 ids. `0x10307bf0` does not bound it; neither does this (an append past
 *  64 is refused here instead of overrunning the neighbour slot -- a crash guard). */
struct FPatrolPathRecord
{
	int32 Type = -1;        // +0x00 the patrol type row of `DAT_1049df20` (0..3)
	int32 Schedule = 0;     // +0x04 the schedule `BuildPatrolPath` installs, 0 = none
	int32 Repeat = 0;       // +0x08
	int32 Count = 0;        // +0x0c node ids appended
	int32 Current = 0;      // +0x10 the node the next patrol arm reads
	int32 Nodes[64] = {};   // +0x14 network node ids, -1 = none
};

/** `m_sppPatrolPath` (`+0x658c`) / `m_sppPatrolPathHunt` (`+0x6594`): the two-word cell -- the owned
 *  byte at `+0x0` and the `CAI_PatrolPath*` at `+0x4` (`layout.md` `+0x6590` / `+0x6598`). */
struct FPatrolPathCell
{
	bool bOwned = false;                   // +0x0
	FPatrolPathRecord* Path = nullptr;     // +0x4
};

/** `+0x658c m_sppPatrolPath` — the patrol route (story 8 wave 2: the one route; the port's point
 *  list and its executor are deleted). */
FPatrolPathCell PatrolPathCell;
/** `+0x6594 m_sppPatrolPathHunt` -- the hunt sibling. */
FPatrolPathCell PatrolPathHuntCell;

/** `0x1029f5d0` -- release a cell: a cell holding a path clears its owned byte, frees the pooled
 *  record (`0x10307db0`) and nulls the pointer. A null cell or an empty one is left alone. */
static void ReleasePatrolPath(FPatrolPathCell* Cell);

/** The node id a cell's path currently points at (`path+0x14[path+0x10]`), -1 for none. */
static int32 PatrolCurrentNode(const FPatrolPathCell& Cell);

/** `0x103079c0` -- a patrol type name against the table `DAT_1049df20` (`"0".."3"`, `__strcmpi`).
 *  Retail's miss is the engine's fatal `Error("Invalid Path Type String...")`; this runtime logs it
 *  at Error and answers the `0` the call returns after it. */
static int32 PatrolTypeForName(const FString& Name);

/** `0x1029f370` -- a schedule token as given, then `SCHED_<token>`, then `SCHED_TROIKA_<token>`
 *  (`0x102cadb0`, the case-insensitive registry), translated to this class's local id through
 *  slot 580's space (`0x102ea280`). 0 on a miss, with retail's DevMsg. */
int32 PatrolScheduleForName(const FString& Token) const;

/** `0x102d2900` -- a patrol token to its network node id: `FindPatrolPoint` (`0x102d2840`)'s hint,
 *  whose node id in this runtime is the hint's entity index (`PatrolNodePosition`). -1 on a miss. */
int32 PatrolNodeIdFor(const FString& Token) const;

/** The pool's size, `0x10307d30`'s `0x1f < cursor` bound and its `Error(..., 0x20)`. */
static constexpr int32 PatrolPathPoolSize = 0x20;
static constexpr int32 PatrolPathNodeCapacity = 64;

/** `0x10307d30` -- the pool allocator: from the cursor `DAT_109363f8` walk the in-use bytes
 *  (`DAT_109363d8`) to the first free slot, mark it, bump the CURSOR by one (not to the slot found)
 *  and answer the slot. A cursor above 0x1f is `Error("Patrol path pool is dry! ...")` and null. */
static FPatrolPathRecord* AllocPatrolPath();
/** `0x10307db0` -- give a slot back: clear its byte and lower the cursor to it. */
static void FreePatrolPath(FPatrolPathRecord* Path);
/** `0x10307d00` -- the pool's static reset (`0x10307e00`, cursor := 0). Also the tests' fresh start. */
static void ResetPatrolPathPool();
/** `0x10307c20` -- the index a (re)built path starts at: `min(count - 1, DAT_1049df28[type])`, whose
 *  table row is `{0, 0x7fff, 0, 0x7fff}` for types 0..3 -- types 1 and 3 start at the LAST node. */
static int32 PatrolPathStartIndex(const FPatrolPathRecord& Path);
/** `DAT_106c994c` -- the global counter every patrol reader bumps on a node id outside the graph. */
static int32& PatrolNodeMissCounter();

/** `0x1029f460`'s `param_6`: false keeps an existing record and only appends; true re-seeds it. */
enum class EPatrolPathBuild : uint8
{
	Extend,
	Replace,
};

/** `0x1029f460` (`BuildPatrolPath`, name coined; no retail symbol) -- the builder the three patrol
 *  inputs funnel through: `InputSetupPatrolType 0x1029eb30` (repeat, type, schedule, NULL, Replace),
 *  `InputFollowPatrolPath 0x1029ed90` (0, -1, 0, ids, Extend), `InputWalkToNode 0x1029e840`
 *  (0, 0, schedule, [node, -1], Replace). `NodeIds` is `-1`-terminated or null. */
void BuildPatrolPath(FPatrolPathCell* Cell, int32 Repeat, int32 Type, int32 Schedule,
	const int32* NodeIds, EPatrolPathBuild Build);

/** `0x102aa640` (`IssuePatrolMove`, the StartTask arm of TASK `0x7a`..`0x7e`): the current node's
 *  position as a type-4 goal through `SetGoal(goal, 2)`; `TaskComplete` or the fail codes `0x1d` /
 *  `0xc`. */
void IssuePatrolMoveStart(FPatrolPathCell* Cell);
/** `0x102aa860` (`IssuePatrolMove`, the RunTask arm of TASK `0x7a` / `0x7b`): the same goal at the
 *  hull tolerance through `SetGoal(goal, 0)`, its answer ignored; never fails on an unreachable node. */
void IssuePatrolMoveRun(FPatrolPathCell* Cell);

/** What `navigator->m_pNetwork (+0x2c)` answers for a node id: `{count +0, nodes +4}` and a node's
 *  `CAI_Node::GetPosition(m_eHull)` (`0x102fb0d0`). */
enum class EPatrolNode : uint8
{
	OutOfRange,   // `id < 0 || id >= count` -- the `DAT_106c994c` arm
	Null,         // an in-range slot holding no node
	Found,
};
/** SEAM for the AI network (`CAI_Network`, `m_pNavigator +0x2c`) and `CAI_Node::GetPosition`
 *  (`0x102fb0d0`), which this substrate does not build (0018 story 4). A node id stands for the
 *  entity index of the patrol hint `0x102d2840` / `0x102d2900` resolved (`FindPatrolPoint`, retail's
 *  hint `+0x5e4` node), the network's count is the entity list's, a slot holding no `FElysiumHint` is
 *  a null node, and a node's position is its hint's origin (retail offsets it by the hull's floor
 *  height, which this runtime's hint origins already stand on). */
EPatrolNode PatrolNodePosition(int32 NodeId, FVector& OutPositionCm) const;

// --- The navigator goal (`AI_NavGoal_t`, `CAI_Navigator::SetGoal 0x102ecd20`) ---------------------

/** The words of retail's 16-word `AI_NavGoal_t` the Script19 builders fill, by index: `[0]` type,
 *  `[1..3]` destination, `[5]` movement activity, `[8]` tolerance (centimetres here, as slot 563's
 *  port reads and writes it, or one of the unit-free `-2.0` "hull width" / `-1.0` "keep the path's"
 *  sentinels `0x102ecd20` tests), `[9]` flags, `[10]` target.
 *  `[4]` dest node, `[6]` arrival activity and `[7]` arrival sequence are -1 at every Script19 site,
 *  `[11..13]` `DAT_10934060..68` (the invalid-vector sentinel), `[14]`/`[15]` zero. */
struct FScript19NavGoal
{
	int32 Type = 0;
	FVector PositionCm = FVector::ZeroVector;
	int32 Activity = -1;
	float Tolerance = 0.f;
	int32 Flags = 0;
	FElysiumEntityHandle Target;
};
/** `CAI_Path +0x28` is `FElysiumNpcBase::NavPathToleranceCm` (`ElysiumNpcBaseStartTask19.inl`): the
 *  navigator lives on `CAI_BaseNPC`, and `SetGoal 0x102ecd20` has one body there. */
/** `0x102ecd20`'s tolerance sentinels, `_DAT_1049d980` and `_DAT_1049d97c`. */
static constexpr float NavGoalToleranceHull = -2.0f;
static constexpr float NavGoalToleranceKeep = -1.0f;
/** `CAI_Navigator::SetGoal` (`0x102ecd20`): the Script19 builders' goal record handed to the ONE
 *  body, `FElysiumNpcBase::StartTaskSetGoal` (`ElysiumNpcBaseStartTask19.cpp`). This only converts the
 *  record (centimetres, the tolerance sentinels passed through) and names the caller for the body
 *  claim; every retail arm of `0x102ecd20` / `0x102f1dc0` is in that body. */
bool Script19SetGoal(const FScript19NavGoal& Goal, int32 SetGoalFlags, const TCHAR* Reason);

/** `0x10278220` (`TryMoveToHiddenPosition` in the port's earlier comments) is `CAI_BaseNPC::
 *  TestLateralCover`, `FElysiumNpcBase::StartTaskTestLateralCover` (`ElysiumNpcBaseStartTask19.inl`):
 *  one body, on the class retail defines it on. */

/** `0x102800c0` -- `SetSchedule(Schedule)`, `m_pGoalEnt := Goal`, and a type-4 goal at the goal's
 *  `GetAbsOrigin` (slot 217) with tolerance 128 and `Activity`, through slot 563 and `SetGoal(goal, 0)`.
 *  Answers `SetGoal`'s answer (the `AL` it leaves). */
bool ScheduledMoveToGoalEntity(int32 Schedule, FElysiumEntity* Goal, int32 Activity);
/** `0x102801e0` -- the path-corner twin: slot 220's origin and the `-1.0` "keep" tolerance. */
bool ScheduledFollowPath(int32 Schedule, FElysiumEntity* Goal, int32 Activity);
