// Story 29e, family Maintain19 — the schedule-change door and the one retail interpreter loop.
// Included inside `class FElysiumNpc`; definitions are in ElysiumNpcMaintain.cpp.

/** `ForceScheduleChange` (`0x102ae490`) and the translate/lookup entry `0x102ae750` feeding
 *  Troika `SetSchedule(CAI_Schedule*, bool)` (`0x102ae780`). */
void ForceScheduleChange(int32 NewSchedule, bool bForce);
void SetSchedule(int32 RawRetailId, bool bForce);

// Slot 435's Troika body and its four species tails. The public virtual remains the one dispatcher.
void TroikaOnScheduleChange(int32 NewSchedule);

virtual double ScheduleTime() const override;

// `MaintainSchedule`'s runner hooks. They are deliberately narrow: ElysiumSchedule::Tick owns the
// loop and asks the NPC only for words and virtuals that live on the entity.
virtual bool			   IsSpecialNavigation() const override;
virtual void			   MarkSpecialNavigationScheduleEnd() override;
virtual bool			   ConsumeChooseNewSchedule() override;
virtual bool			   ScheduleStateDiffersFromIdeal() const override;
virtual bool			   HasMaintenanceCondition(EElysiumNpcCond Cond) const override;
virtual void			   PrepareScheduleReselect() override;
virtual bool			   ConsumeBlockedDoorForSchedule(double Now) override;
virtual void			   CommitIdealStateForSchedule() override;
virtual int32 SelectScheduleForMaintenance(double Now,
	int32&													   OutIdealScheduleRetail) override;
virtual void			   SetIdealScheduleForMaintenance(int32 RetailId) override;
virtual void			   MissingSchedule() override;
virtual int32			   LocalScheduleIdForStart(int32 Id) override;
virtual void			   MaintenanceOnStartSchedule(int32 LocalScheduleId) override;
virtual void			   DebugTaskStart(const FElysiumScheduleStep& Step) override;
virtual void			   MaintenanceStartTaskOverlay() override;
virtual bool			   MaintenanceIsCurTaskContinuousMove() override;
virtual void			   RememberContinuousMove() override;
virtual bool			   IsAiStepMode() const override;
virtual void			   AdvanceAiStepDebugIndex() override;
virtual void			   FreezeForAiStep() override;
virtual void			   NextScheduledTaskForMaintenance(FElysiumScheduleState& State) override;
virtual bool			   TakeExternalExecutorReturn() override;

bool ShouldSelectIdealStateForMaintenance();
void CacheInterruptConditionsForMaintenance(double Now);

// `m_nDebugCurIndex` (`+0x5f40`), used only by `ai_step`.
int32 MaintainDebugTaskIndex = 0;
bool  bReturnToExternalExecutorAfterSchedule = false;
