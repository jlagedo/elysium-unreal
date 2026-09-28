// `CAI_BaseNPC`'s declarations of the `Maintain19` family (story 5 step 5),
// moved from `ElysiumNpcMaintain19*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseMaintain19.cpp`.

/** `CAI_BaseNPC::MaintainSchedule` (`0x102817c0`) through the engine-neutral schedule kernel. */
bool MaintainSchedule(double Now, bool bReduced);
/** The schedule interpreter itself (`ElysiumSchedule::Tick`, `0x102817c0`), with no owner routing:
 *  what `MaintainSchedule` reaches for a body the schedule owns. */
bool MaintainScheduleRetail(double Now, bool bReduced);

/** `TaskMovementComplete` (`0x10273ec0`), including all four task-status arms. */
void TaskMovementComplete();

virtual void			   RunTaskOverlay() override;

void RefreshIdealStateForMaintenance();
