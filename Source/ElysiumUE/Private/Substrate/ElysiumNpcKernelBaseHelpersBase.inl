// `CAI_BaseNPC`'s half of family **KernelBaseHelpers** (0019 story 6): the declarations of the value
// helpers re-homed from the deleted Debug family. Included inside `class FElysiumNpcBase` by
// `Substrate/ElysiumNpcBase.h`; the bodies are in `Substrate/ElysiumNpcKernelBaseHelpers.cpp`. Each
// one answers a rule body a value -- the nearest-node search `StartTaskSetGoal`, `NavNearestNodeTo`
// and the Zombie's task `0x150` read, the sequence descriptor RunTask's `0x97` arm reads -- and none
// of them prints.

/** `CBaseAnimating::GetSeqDesc(m_nSequence)` (`0x1000b4f6`) and the two `studiohdr_t` string offsets
 *  the stat overlay reads off it — `seqdesc + *(int*)seqdesc` (the sequence label) and
 *  `seqdesc + *(int*)(seqdesc+4)` (its activity name). **SEAM**: family Anim already records that
 *  this runtime stands no studio header; answers false, which is retail's `"(INVALID)"` arm. */
bool SequenceDescriptor(int32 Sequence, FString& OutLabel, FString& OutActivityName) const;

/** The `0x2000` arm's node resolve (and the species task `0x150`'s): stamp the navigator's
 *  `+0x8`/`+0xc` scratch, then `CAI_Network::NearestNodeToNPC` (`0x102f3c10`, `NavNearestNodeToNpc`)
 *  at `GetOrigin()` and `CAI_Node::GetPosition(node, m_eHull)` (`0x102fb0d0`), in this family's
 *  `cm / U` units. False, `OutUnits` untouched, when no node answers. */
bool NavigatorNearestNodePositionUnits(FVector& OutUnits) const;

/** `CAI_Network::NearestNodeToNPC` (`0x102f3c10`, VPROF "CAI_Network_NearestNodeToNPCAtPoint"):
 *  `ListNodesInBox(10)` over a box of ±800 x ±800 x ±200 units (±2048 on every axis when slot 513's
 *  capabilities carry `4`, fly) under `CNodeNPCFilter` (a type-3 node needs capability `4`, a type-2
 *  node capability `1`, and slot 527 must pass; the distance is to `GetPosition(node, +0x156c)`),
 *  then, nearest first, the first node that fits (`0x102f1900`), is usable (`0x1027db30`) and
 *  whose line from `PositionCm` to its position raised by the view offset (`0x102f3900`) is clear.
 *  A clear trace that only an entity the filter flags (`+0x9c` set) was in the way of is kept as the
 *  fallback, the first such wins. -1 with no network or no node. The 20-entry cache in front of it
 *  (`0x102f4520` / `0x102f45f0`) is engine machinery and is not ported. */
int32 NavNearestNodeToNpc(const FVector& PositionCm) const;

/** `CAI_Navigator::CanFitAtNode` (`0x102f1900`, "CanFitAtNode() called with no network"): the node's
 *  position at the navigator's hull (`nav+8`, the pathing hull), then -- for a ground node, or a
 *  climb node with any of the info bits `0x1d` -- the move probe's `CheckStandPosition`
 *  (`0x102e7270`, mask as given), then `0x102f1a20`'s hull trace at the point, which fits unless it
 *  starts solid. **SEAM** on both geometry queries: the stand check is family Motor's
 *  `MoveProbeCheckStandPosition` (false: no hull probe), the hull trace `KernelHullTrace` (clear).
 *  0018 story 6's geometry services absorb them. */
bool NavCanFitAtNode(int32 Node, int32 Hull, int32 Mask) const;

/** `0x102f39a0(ignore, start, end, &flag)` -- the nearest-node LINE trace (`Ray_t` with no
 *  extents), mask `0x202400b` (`MASK_NPCSOLID`), filter `CTraceFilterNearestNode(ignore, 0)`
 *  (`0x102f37d0`): only an entity whose `GetMoveType()` (slot 94) is 0 blocks, so the world and its
 *  static brushes -- never a character. Clear exactly at `fraction == 1.0` (`_DAT_10449280`). The
 *  port traces the solid-world channel through `IElysiumEmbodiment::TraceCameraHull` with a zero
 *  half-extent, the brush-only ray `ElysiumLockable` already uses; a world with no collision
 *  answers clear. The filter's `+0xc` flag (an entity with `+0x9c` set was skipped) has no port
 *  source: `ClearPastFlagged` is never answered. */
enum class ENavNodeTrace : uint8
{
	Blocked,             // fraction < 1.0
	Clear,               // fraction == 1.0, the flag clear
	ClearPastFlagged,    // fraction == 1.0 with the filter's `+0xc` flag set (never, here)
};
ENavNodeTrace NavNearestNodeTrace(const FVector& StartCm, const FVector& EndCm,
	const FElysiumEntityHandle& Ignore) const;

