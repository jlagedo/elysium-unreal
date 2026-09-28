// `CAI_BaseNPC`'s declarations of the `Maintain19` family (story 5 step 5),
// moved from `ElysiumNpcMaintain19*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseMaintain.cpp`.

/** `CAI_BaseNPC::MaintainSchedule` (`0x102817c0`) through the engine-neutral schedule kernel. */
bool MaintainSchedule(double Now, bool bReduced);
/** The schedule interpreter itself (`ElysiumSchedule::Tick`, `0x102817c0`), with no owner routing:
 *  what `MaintainSchedule` reaches for a body the schedule owns. */
bool MaintainScheduleRetail(double Now, bool bReduced);
/** The interpreter's two task dispatches (`0x10281e10` / `0x1028202c`): slot 442 `StartTaskSlot442`
 *  and slot 444 `RunTaskSlot444` on this body, through the vtable, with the running step. */
virtual void StartTaskForMaintenance(FElysiumScheduleState& State, const FElysiumScheduleStep& Step, double Now) override;
virtual void RunTaskForMaintenance(FElysiumScheduleState& State, const FElysiumScheduleStep& Step, double Now) override;

/** `TaskMovementComplete` (`0x10273ec0`), including all four task-status arms. */
void TaskMovementComplete();

virtual void			   RunTaskOverlay() override;

void RefreshIdealStateForMaintenance();
