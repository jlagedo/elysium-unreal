// `CAI_BaseNPC`'s declarations of the `Schedule` family (story 5 step 5),
// moved from `ElysiumNpcSchedule*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSchedule.cpp`.

/** `CAI_ClassScheduleIdSpace::ScheduleLocalToGlobal` (`0x102ea2d0`, the same walk family Squad
 *  ports for squad slots over the space at `+0x48`): -1 stays -1, otherwise walk the chain and
 *  answer `(globalBase - localBase) + id` for the first space whose range holds it, else -1.
 *
 *  SEAM: every row's range is the empty one the static constructor left, because the port parses no
 *  schedule text, so this answers -1 for every id. Retail's ranges are filled by the class's own
 *  `InitCustomSchedules`. */
static int32 ScheduleLocalToGlobal(const FElysiumLocalIdSpace* Space, int32 LocalId);

/** `CAI_BaseNPC::SetSchedule(int)` (`0x10280de0`), first half: the value `m_IdealSchedule`
 *  (`+0x5c3c`) is stamped with.
 *
 *  Retail: an id at or above 1,000,000,000 is already global and is stamped unchanged; anything
 *  below it — and the -1 sentinel — goes through this class's schedule id space. Split out from
 *  `ChangeSchedule` because it is the whole of what that body adds over the port's existing
 *  install chain, and a test has to be able to state it without installing a program. */
int32 ResolveIdealScheduleStamp(int32 RawRetailId) const;

/** `CAI_BaseNPC::SetSchedule(int)` (`0x10280de0`) whole, and the body all five slot-619 species
 *  overrides forward to (`0x1035dba0`, `0x10361530`, `0x1036c760`, `0x103a9fd0`, `0x103af8d0` —
 *  each a scope-trace wrapper with no logic of its own; their trace names are
 *  `FElysiumNpcScheduleHost::SetScheduleTraceName`).
 *
 *  Stamp raw `BaseScheduleHost.IdealScheduleRetail`, then run `SetSchedule(int)` (`0x102cc1f0`) →
 *  `CAI_BaseNPC::SetSchedule(CAI_Schedule*)` (`0x10280e50`), which is `ElysiumSchedule::Start`. */
void ChangeSchedule(int32 Id);

/** `NextScheduledTask` (`0x10280f40`): clear the task status, advance the task index, and when the
 *  program is exhausted zero `m_failedSchedule`/`m_interuptSchedule` and raise
 *  `COND_SCHEDULE_DONE` (0x5d). */
void NextScheduledTask();

/** `CAI_BaseNPC::TaskComplete(bool)` (`0x10273e80`) — the body `CAI_Motor` slot 2 (`0x102623c0`)
 *  forwards to through its owner back-pointer. `bIgnoreTaskFailed` false refuses to overwrite an
 *  already raised `COND_TASK_FAILED`; true writes the status regardless. */
void TaskComplete(bool bIgnoreTaskFailed);

/** `CAI_Motor` slot 2 (`0x102623c0`): `m_pOuter->TaskComplete(b)`. The motor telling its owner a
 *  motor-driven task finished; no logic of its own. */
void MotorTaskComplete(bool bIgnoreTaskFailed);

/** `0x1027db30` — **NOT `StartTaskByIndex`.** 29c's target name was a guess over a `‼` row with no
 *  recovered callers; the disassembly reads the NAVIGATOR at `+0x5d34`, indexes its node list
 *  (`+0x2c`, count at `+0x00`, array at `+0x04`) and tail-jumps to slot 527 `IsUnusableNode`, which
 *  the ledger's signature table names. An index below zero or past the end bumps the global error
 *  counter `0x106c994c` and answers false, as does a null node.
 *
 *  SEAM: there is no node list on the port's motor, so the bounds test always fails and the counter
 *  is tallied instead of incremented. */
bool IsUnusableNodeIndex(int32 NodeIndex) const;
