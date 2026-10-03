// `CAI_BaseNPC`'s helper declarations (story 5 step 5), moved from `ElysiumNpcKernelBaseHelpers.inl`
// and `ElysiumNpcTroikaHelpers.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseHelpers.cpp`.

/** SEAM for the `CAI_Motor` calls the bodies above make that have no mover here:
 *  `thunk_FUN_102e2690` (`SetAbsVelocity`), `thunk_FUN_102e1c10` (reissue the move at a yaw and a
 *  speed) and `thunk_FUN_102e12c0` (the base speed). The velocity set is counted; the reissue
 *  records its arguments; the base speed answers `0.0`. */
struct FTroikaMotorSeams
{
	int32 VelocitySets = 0;
	FVector LastVelocityUnits = FVector::ZeroVector;
	int32 MoveReissues = 0;
	float LastReissueYaw = 0.f;
	float LastReissueSpeed = 0.f;
	int32 ForcedActivities = 0;
	int32 LastForcedActivity = INDEX_NONE;
	/** `m_flMoveInterval`, `CAI_Motor+0x30` — the one motor word these bodies read AND write. */
	float MoveInterval = 0.f;
};


/** Family Hints owns `FHintWords`, the typed view of one `CAI_Hint`'s datamap. Forward-declared
 *  here because this family's `.inl` is included ahead of that one and the pure rules below take it
 *  by reference; reusing it is the point — a second copy of a hint's words is exactly what the two
 *  families must not stand. */
struct FHintWords;

// From `ElysiumNpcTroikaHelpers.inl`: the navigator and hint helpers base bodies use.

/** `CAI_Navigator+0x54`, `+0x58` and `+0x60` — the three words `0x102eeb70` resets, written as
 *  `-1`, `-1.0` and `-1.0`. Their retail names are **unrecovered**: the corpus holds the reset and
 *  no reader that pins any of the three. Declared by offset so `FUN_102eea70` / `FUN_102eeac0`
 *  write the words retail writes rather than recording a bare "reset happened".
 *
 *  `CAI_Navigator+0x18` (`NavType`) and `+0x1c` (`bNavFailed`) belong to family **Motor**'s
 *  `FNavigator Navigator` and are read and written through it rather than duplicated here. */
int32 NavigatorWord0x54 = -1;

float NavigatorWord0x58 = -1.f;

float NavigatorWord0x60 = -1.f;

/** The `thunk_FUN_102ddc40(nav+0x28)` route clear at the head of the same reset. **SEAM**: family
 *  Motor's standing fact is that this substrate keeps no route, so the clear is counted and clears
 *  nothing. */
int32 NavigatorRouteClears = 0;

/** `CAI_StandoffBehavior+0x19` — the byte `vfunc3` (`0x102c7410`) reads first and CLEARS on its
 *  two refusal arms. Carried on the leaf because this runtime stands no behaviour object; family
 *  **Lifecycle** made the same call for `FStandoffWords` and this is the one word that view does
 *  not carry. Its retail name is **unrecovered**; 29c read it as "the owner's weapon-drawn cache". */
bool bStandoffRangedCache = false;

/** `CAI_StandoffBehavior+0x50` — the float `vfunc5` (`0x102c7530`) copies into the owner's
 *  `m_flDistTooFar` (`+0x5de4`, `FElysiumNpcBase::DistTooFar`). Name **unrecovered**; it is the
 *  behaviour's own authored stand-off distance. */
float StandoffDistTooFar = 0.f;


mutable FTroikaMotorSeams TroikaMotor;

/** `CAI_Hint::GetPosition` (`0x102d1180(hint, npc, &out)`) -- the ONE port body of the hint's
 *  position, centimetres: an unbound hint (`m_nNodeID +0x5e4 == -1`) answers its own origin; a bound
 *  one answers its network node at this NPC's PATHING hull (`0x102f46d0`, `+0x156c`), or
 *  `vec3_origin` when the id is outside the network. The point `0x102961a0` rays to,
 *  `TASK_GET_PATH_TO_HINTNODE`'s destination, and (through `HintStandPosition`, in units) the
 *  Troika line's cover / cower stand. False only for an index that names no live hint. */
bool HintPositionCm(int32 HintNode, FVector& OutPointCm) const;

/** `+0x156c`, the PATHING hull every `0x102f46d0` / `CAI_Navigator` read hands `GetPosition`:
 *  `FElysiumNpc::PathingHullKind` on the Troika line, the standing `HullKind` on a base-only NPC. */
int32 RetailPathingHull() const;

/** `GetCurTask()->iTask` (`0x1028a150`), the retail task NUMBER of the running program's current
 *  step: `FElysiumScheduleStep::TaskId`, which is retail's global id. False with no running step
 *  (retail's NULL task). */
bool CurrentRetailTaskNumber(int32& OutTaskNumber) const;

/** `0x102d1540` — "is this hint free, or already mine?", on the live hint (0018 story 8). In order:
 *  the owner resolves to me → true; `curtime < m_flNextUseTime` (`+0x5ec`, strict) → false; a live
 *  owner → false; else true. `m_iDisabled` is NOT read here (that is `0x102d14c0`'s arm). This is the
 *  OWNER-only half `IsUnusableNode` (slot 527) negates; an index that is not a hint answers true, a
 *  named crash guard (retail would dereference NULL). */
bool IsHintAvailableToMe(int32 HintNode) const;

/** `CAI_BaseNPC::GetNavTargetEntity` (`0x102729d0`). NAMED: the SDK twin's two arms
 *  (`GOALTYPE_ENEMY` -> `GetEnemy()`, `GOALTYPE_TARGETENT` -> `GetTarget()`) are retail's modes 2
 *  and 1 exactly; retail adds a third, mode 7 `GOALTYPE_COVER`, through the navigator. */
FElysiumEntity* GetNavTargetEntity() const;

/** `CAI_BaseNPC::RememberUnreachable` (`0x10274080`). NAMED by the SDK twin, arm for arm: a
 *  BACKWARD scan for an existing record refreshes its expiry, a miss appends one, and the entity's
 *  current position is written either way. The duration is baked (`_DAT_10449258`). */
void RememberUnreachable(FElysiumEntity* Entity);

/** `CAI_BaseNPC::SetDefaultEyeOffset` (`0x10274ca0`). NAMED by the SDK twin and by its own string,
 *  `"WARNING: %s has no eye offset in .qc!"`. */
void SetDefaultEyeOffset();

/** `CAI_BaseNPC::GetScriptCustomMoveActivity` (`0x10289fe0`). NAMED by the SDK twin: the cine's
 *  `m_iszCustomMove` (`+0x5f50` on `m_hCine`) as an activity, else as a sequence, else `ACT_WALK`.
 *  Returns a retail `Activity` number — 9 `ACT_WALK` or 0x18 `ACT_SCRIPT_CUSTOM_MOVE`. */
int32 GetScriptCustomMoveActivity() const;

/** `thunk_FUN_102d12e0(hint)` — the ONE port body of the hint's yaw (RETAIL frame, degrees): a
 *  network-bound hint answers its node's yaw (`0x102f47b0`, 0.0 for an id outside the network), an
 *  unbound one its own `GetAbsAngles().y`. `ApplyHintLeanOffset`, `FindTacticalHintNode` and the
 *  StartTask facing arms (`StartTaskHintYaw`) all read it. False only for an index that names no
 *  live hint. */
bool HintYaw(int32 HintNode, float& OutYaw) const;

/** SEAM for slot 16 (vtable `+0x40`), the attack-extent margin `FindTacticalHintNode` caches into
 *  `m_vecSavedSleepExtents` (`+0x65d0`) before `SetAbsoluteAttackExtents`. Answers the zero vector,
 *  which is the margin `FElysiumEntity::SetAttackExtents` already stands for. SOURCE units. */
FVector HintAttackExtentsUnits() const;

/** SEAM for `thunk_FUN_102ee3f0(m_pNavigator)` — the navigator's current LINK activity
 *  `StopScheduledMove` (`0x102bf770`) compares `m_IdealActivity` (`+0x0ff0`) against. Family
 *  **Motor** records the same absent link object (`NavLinkActivity`); this answers `-1`. */
int32 NavCurrentLinkActivity() const;

/** `CBaseAnimating::SelectHeaviestSequence(activity, -1)` through the sequence bridge. */
int32 SelectHeaviestSequence(int32 Activity, int32 CurrentSequence) const;

/** `CAI_Motor#8` `0x102e1270` — the full stop: `SetAbsVelocity(0,0,0)` then force activity `0x30`.
 *  Also fills `CAI_HumanoidMotor#8`. */
void FUN_102e1270();

/** `CAI_Navigator#7` `0x102eea70` — the shared `0x102eeb70` reset (`+0x54 := -1`, `+0x58 := -1.0`,
 *  `+0x60 := -1.0`, clear the route at `+0x28`) and then `+0x1c := 1`, family Motor's
 *  `Navigator.bNavFailed`. (`CAI_Navigator#11` `0x102eeac0` is byte-identical; nothing in the port
 *  dispatches it.) */
void NavStopAndMarkDirty();

void FUN_102eea70();


