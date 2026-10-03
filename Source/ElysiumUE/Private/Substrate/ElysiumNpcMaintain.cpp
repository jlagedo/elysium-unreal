#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29e, family Maintain19. The instruction addresses on every arm are from the retail
// vampire.dll listing; the scope/VProf bookkeeping and the ABSENT file/line stamps stay absent.

namespace
{
	constexpr int32 GMaintainActIdle = 1;

	enum class EForceScheduleWarning : uint8
	{
		NotInterruptable,
		DeadOrDying,
		Script,
	};

	void ForceScheduleWarning(FElysiumNpc& Npc, EForceScheduleWarning Kind,
		const FElysiumEntity* Script = nullptr)
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("********************* WARNING **********************"));
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("********************* WARNING **********************"));
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Calling ForceScheduleChange on NPC '%s'"),
			*Npc.DebugString());
		if (Kind == EForceScheduleWarning::Script)
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("who is in an uninterruptable script '%s'"),
				Script != nullptr ? *Script->DebugString() : TEXT("NULL ENTITY"));
		}
		else
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s"),
				Kind == EForceScheduleWarning::NotInterruptable
					? TEXT("who is not supposed to be interruptable")
					: TEXT("who is dead or dying."));
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Be sure you really want to do this!"));
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("This message will self destruct."));
		}
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("********************* WARNING **********************"));
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("********************* WARNING **********************"));
	}
} // namespace

void FElysiumNpc::SetSchedule(int32 RawRetailId, bool bForce)
{
	++SetScheduleRetailCalls;
	LastSetScheduleRetail = RawRetailId;
	bLastSetScheduleForce = bForce;

	const int32 Translated = TranslateScheduleRetail(RawRetailId); // 0x102ae758
	LastTranslateScheduleRetail = Translated;
	// `GetScheduleOfType` (`0x102cc260`): the translated number is class-LOCAL, so it goes through
	// slot 580's schedule space before the lookup.
	int32 Resolved = ResolveScheduleId(Translated); // 0x102ae75d
	if (ElysiumScheduleFor(Resolved) == nullptr)
	{
		// `GetScheduleOfType`'s miss installs literal 1 without translating it again.
		RecordScheduleEvent(FString::Printf(
			TEXT("GetScheduleOfType(): No CASE for 0x%x; installing SCHED_IDLE_STAND"), Translated));
		ElysiumStub::Fired(TEXT("schedule"), TEXT("SetSchedule retail registry miss"), DebugString(),
			FString::Printf(TEXT("0x%x"), Translated),
			TEXT("the corpus text that carries the program; IDLE_STAND stands in"));
		Resolved = ResolveScheduleId(ElysiumSched::IDLE_STAND); // 0x102cc229
	}

	// Troika `SetSchedule(CAI_Schedule*, bool)` refuses both DEAD words first.
	if (NpcStateRetail() == 7 || IdealStateRetail() == 7) // 0x102ae788, 0x102ae790
	{
		return;
	}
	if (!IsAlive() && !bForce) // 0x102ae79c, 0x102ae7aa
	{
		return;
	}

	ForceScheduleChange(Resolved, bForce);				 // 0x102ae7b7
	ElysiumSchedule::Install(Schedule, Resolved, *this); // 0x102ae7bf
}

void FElysiumNpc::ForceScheduleChange(int32 NewSchedule, bool bForce)
{
	if (!OkToDisturb()) // 0x102ae495
	{
		ForceScheduleWarning(*this, EForceScheduleWarning::NotInterruptable); // 0x102ae4c4
	}
	if (!IsAlive() && !bForce) // 0x102ae4ee, 0x102ae4f8
	{
		ForceScheduleWarning(*this, EForceScheduleWarning::DeadOrDying); // 0x102ae520
	}

	FElysiumEntity* Script = World != nullptr ? World->Resolve(ScriptOwner) : nullptr;
	if (Script != nullptr) // 0x102ae546..0x102ae57c
	{
		if (!Script->IsScriptedSequenceInterruptable()) // 0x102ae5ad
		{
			ForceScheduleWarning(*this, EForceScheduleWarning::Script, Script); // 0x102ae60e
		}
		Script->CancelScriptedSequenceForDialogue(Handle); // 0x102ae654
		if (NpcStateRetail() != IdealStateRetail())		   // 0x102ae659
		{
			if (IdealStateRetail() != 7) // 0x102ae669
			{
				RefreshIdealStateForMaintenance(); // 0x102ae670
			}
			SetState(IdealStateRetail()); // 0x102ae67e
		}
	}

	const FElysiumNpcNavigationSample First =
		Motor != nullptr ? Motor->SampleNavigation() : FElysiumNpcNavigationSample();
	if (First.Type != EElysiumNpcNavType::Climb) // 0x102ae68a
	{
		const FElysiumNpcNavigationSample Second =
			Motor != nullptr ? Motor->SampleNavigation() : FElysiumNpcNavigationSample();
		if (Second.Type != EElysiumNpcNavType::Jump) // 0x102ae696
		{
			NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH); // 0x102ae69b
		}
	}
	OnScheduleChange(NewSchedule); // 0x102ae6b2
}

void FElysiumNpc::OnScheduleChange(int32 NewSchedule)
{
	// The director runs an ordinary corpus program, so its authored interrupts can replace it.
	// Drop only the port's body claim here; recursively clearing the schedule would reorder slot 435.
	if (ScriptedScheduleOrder.Program != 0 && NewSchedule != ScriptedScheduleOrder.Program)
	{
		ScriptedScheduleOrder.Reset();
		ReleaseScriptedScheduleBody(TEXT("directed schedule replaced"));
	}
	// `CNPC_VGargoyle`, `CNPC_VHengeyokai`, `CNPC_VTzimisce` and `CNPC_VWerewolf` override this
	// method on their C++ classes (story 5 step 3); each calls this body (the Troika `0x102a0940`)
	// directly, first, then runs its own tail.
	TroikaOnScheduleChange(NewSchedule);
}

void FElysiumNpc::TroikaOnScheduleChange(int32 NewSchedule)
{
	(void)NewSchedule;
	// Base slot 435 (`0x1027a700`) first: navigator notification, move-wait zero, strategy reset.
	// Navigator slot 4 is a literal retail `RET 4`, so it has no state to carry.
	BaseScheduleHost.MoveWaitFinished = 0.0; // 0x1027a716
	VacateSquadSlot();                       // 0x1027a71f -> 0x1028ae60
	NpcFlags.BeginScheduleChange();					   // 0x102a095d
	if (!NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH)) // 0x102a0965
	{
		const FElysiumNpcNavigationSample First =
			Motor != nullptr ? Motor->SampleNavigation() : FElysiumNpcNavigationSample();
		if (First.Type != EElysiumNpcNavType::Climb) // 0x102a097b
		{
			const FElysiumNpcNavigationSample Second =
				Motor != nullptr ? Motor->SampleNavigation() : FElysiumNpcNavigationSample();
			if (Motor != nullptr && Second.Type != EElysiumNpcNavType::Jump) // 0x102a0987
			{
				Motor->ClearNavigationGoal(); // 0x102a0992
			}
		}
		if (CurrentAmbientSpot())
		{
			FinishAmbientUse(bAmbientArrived, false); // 0x102a09a0
		}
		ScheduleHost.Unknown6300 = 0; // 0x102a09b0
		ScheduleHost.Unknown659c = 0; // 0x102a09b6
		if (Motor != nullptr)
		{
			Motor->ResetSteering(); // 0x102a09bc
		}
		BaseScheduleHost.bShouldMove = false; // 0x102a09c8
		bMoveIssued = false;
		bWalkingAnimation = false;
		ScheduleHost.GoalToleranceCm = 0.f;								// 0x102a09ce
		ScheduleHost.InsideInterruptDistanceSqr = 0.f;					// 0x102a09d4
		ScheduleHost.OutsideInterruptDistanceSqr = 0.f;					// 0x102a09da
		ScheduleHost.InterruptTime = 0.0;								// 0x102a09e0
		ScheduleHost.MoveTarget = FElysiumEntityHandle::Invalid();		// 0x102a09e6
		if (World != nullptr && World->Resolve(OpeningDoor) != nullptr) // 0x102a09f8
		{
			Slot532(8); // 0x102a0a07
		}
		if (NpcFlags.ApplyScheduleChangeMasks()) // 0x102a0a19..0x102a0a45
		{
			RemoveOblivious();
			ReconnectToSquad();
			RecordScheduleEvent(TEXT("OnScheduleChange: obliviousness released"));
		}
		if (NpcFlags.Has(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX)) // 0x102a0a4a
		{
			SetAttackExtents(ScheduleHost.SavedSleepExtents);	  // 0x102a0a63
			ScheduleHost.SavedSleepExtents = FVector(-1.0);		  // 0x102a0a6b
			NpcFlags.Clear(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX); // 0x102a0a79
		}
		BaseScheduleHost.bMotorAnimationMovement = false; // 0x102a0a8a
		ScheduleHost.DesiredMoveYaw = 0.f;			  // 0x102a0a8d
		ScheduleHost.bWaitFinishedSet = false;		  // 0x102a0a93
	}
	if (NpcFlags.Has(EElysiumNpcFlag2::ACTIVITY_COPY_PROP_CLEAN)) // 0x102a0a99
	{
		ElysiumDisciplines::NotifyScheduleChanged(*this); // 0x102a0ab5
		ClearOwnedActivityCopyProps();					  // 0x102a0abb
		bInvincible = false;							  // 0x102a0ac3
	}
	NpcFlags.FinishScheduleChange();	 // 0x102a0adb
	BaseScheduleHost.MemoryBits &= ~0x2000u; // 0x102a0aed
}

double FElysiumNpc::ScheduleTime() const
{
	return World != nullptr ? World->NowSeconds() : 0.0;
}

bool FElysiumNpc::IsSpecialNavigation() const
{
	const FElysiumNpcNavigationSample Nav =
		Motor != nullptr ? Motor->SampleNavigation() : FElysiumNpcNavigationSample();
	return Nav.Type == EElysiumNpcNavType::Climb || Nav.Type == EElysiumNpcNavType::Jump;
}

void FElysiumNpc::MarkSpecialNavigationScheduleEnd()
{
	NpcFlags.Set(EElysiumNpcFlag::PRESERVE_PATH); // 0x10281045
	NpcFlags.Set(EElysiumNpcFlag2::FINISH_SPECIAL_NAV);
	NpcFlags.SetRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
}

bool FElysiumNpc::ConsumeChooseNewSchedule()
{
	if (!NpcFlags.Has(EElysiumNpcFlag2::CHOOSE_NEW_SCHEDULE))
	{
		return false;
	}
	NpcFlags.Clear(EElysiumNpcFlag2::CHOOSE_NEW_SCHEDULE); // 0x10281075
	NpcFlags.ClearRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
	return true;
}

bool FElysiumNpc::ScheduleStateDiffersFromIdeal() const
{
	return NpcStateRetail() != IdealStateRetail(); // 0x102819de
}

bool FElysiumNpc::HasMaintenanceCondition(EElysiumNpcCond Cond) const
{
	return Cognition.Conditions.Has(Cond);
}

bool FElysiumNpc::ShouldSelectIdealStateForMaintenance()
{
	if (IdealStateRetail() == 7) // 0x10281439
	{
		return false;
	}
	if (IdealStateRetail() == 4 && NpcStateRetail() != 4) // 0x1028143e
	{
		return false;
	}
	if (Mind.IsStateChangeForced()) // 0x1028144c
	{
		Mind.ClearForceStateChange(); // 0x10281456
		return true;
	}
	if (!Cognition.Conditions.Has(EElysiumNpcCond::ScheduleDone)) // 0x10281461
	{
		return true;
	}
	if (Schedule.IsRunning()
		&& Cognition.CustomInterruptConditions.Has(EElysiumNpcCond::ScheduleDone)) // 0x1028147c
	{
		return true;
	}
	return NpcStateRetail() == 2 && GetEnemy() == nullptr; // 0x1028148d..0x102814a4
}

void FElysiumNpc::PrepareScheduleReselect()
{
	if (ShouldSelectIdealStateForMaintenance()) // 0x102819f4
	{
		RefreshIdealStateForMaintenance(); // 0x102819ff
	}
}

bool FElysiumNpc::ConsumeBlockedDoorForSchedule(double Now)
{
	if (!NpcFlags.Has(EElysiumNpcFlag2::IGNORE_DOOR_FAILURE)) // 0x10281a19
	{
		return false;
	}
	NpcFlags.Clear(EElysiumNpcFlag2::IGNORE_DOOR_FAILURE); // 0x10281a29
	NpcFlags.ClearRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
	FElysiumEntity* Door = World != nullptr ? World->Resolve(BlockedDoor) : nullptr; // 0x10281a41
	if (Door == nullptr)
	{
		return false;
	}
	(void)Door;
	// FCOMP/TEST/JP treats GREATER and unordered as the latch arm. C++ `<=` is false for NaN, so
	// this preserves retail's unordered result as well as the ordinary future deadline.
	if (BlockedDoorExpiresAt <= Now) // 0x10281a4a..0x10281a5d
	{
		BlockedDoor = FElysiumEntityHandle::Invalid(); // 0x10281a5f
		return false;
	}
	return true; // 0x10281a67
}

void FElysiumNpc::CommitIdealStateForSchedule()
{
	SetState(IdealStateRetail()); // 0x10281b63
}

void FElysiumNpc::CacheInterruptConditionsForMaintenance(double Now)
{
	BaseScheduleHost.CacheInterruptTime = Now; // 0x1026a16d, +0x1b24
	if (!Schedule.IsRunning())
	{
		Cognition.CustomInterruptConditions.Reset();  // 0x1026a18d
		Cognition.InverseInterruptConditions.Reset(); // 0x1026a1a3
		return;
	}
	// The port's program record has no authored inverse-mask column; its recovered positive mask
	// plus slot 453 is the whole cached mask the interpreter and debug view consume.
	Cognition.CustomInterruptConditions =
		ElysiumSchedule::EffectiveInterrupts(Schedule, *this); // 0x1026a1d9..0x1026a25f
	Cognition.InverseInterruptConditions.Reset();
	RemoveIgnoredConditions();								// 0x1026a267, slot 459
	Cognition.Conditions.Clear(EElysiumNpcCond::NpcFreeze); // 0x1026a274
}

int32 FElysiumNpc::SelectScheduleForMaintenance(double Now,
	int32&															OutIdealScheduleRetail)
{
	if (BaseScheduleHost.CacheInterruptTime < Now) // 0x102814d0
	{
		CacheInterruptConditionsForMaintenance(Now); // 0x102814de
	}
	// `if (!m_bConditionsGathered) GatherConditions()` (slot 433): the byte `RunAI` clears at
	// `0x1026f1ad` and slot 433 sets (`0x1026eca9`), in the stamp form (negative = not gathered).
	if (Cognition.GatheredAt < 0.0)
	{
		GatherConditions(); // 0x102814d0 slot 433
	}
	const int32 Selected = SelectSchedule(); // 0x102814d0 slot 438
	if (Selected == ElysiumScheduleId::None && bUseInteresting)
	{
		// Retail's interesting-place answer is a schedule. This runtime still represents that
		// program as an external executor (the named survivor), so its slot-438 null adapter returns
		// control at the selection edge rather than flowing into the missing-ID fallback below.
		OutIdealScheduleRetail = 0;
		bReturnToExternalExecutorAfterSchedule = true;
		return ElysiumScheduleId::None;
	}
	// Today's selector surface is typed and therefore exposes only registered ids. Preserve its raw
	// retail number in the int32 ideal word before slot 440 transforms the installed pointer. The
	// later Select19 body owns returning local/-1/>=1e9 values; this adapter is already wide enough
	// to preserve them when that selector surface lands.
	OutIdealScheduleRetail = Selected; // 0x102814d0, +0x5c3c
	const int32 SelectedNumber = Selected;
	const int32 TranslatedNumber = TranslateScheduleRetail(SelectedNumber); // 0x102cc1f0 slot 440
	LastTranslateScheduleRetail = TranslatedNumber;
	const int32 Translated = TranslatedNumber;
	if (Translated != ElysiumScheduleId::None)
	{
		return Translated; // 0x102cc260 lookup hit
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("GetScheduleOfType(): No CASE for 0x%x; installing SCHED_IDLE_STAND"),
		TranslatedNumber));
	ElysiumStub::Fired(TEXT("schedule"), TEXT("GetNewSchedule registry miss"), DebugString(),
		FString::Printf(TEXT("0x%x"), TranslatedNumber),
		TEXT("0002/25: IDLE_STAND stands in for the unregistered selected program"));
	return ElysiumSched::IDLE_STAND; // 0x102cc229
}

void FElysiumNpc::SetIdealScheduleForMaintenance(int32 RetailId)
{
	BaseScheduleHost.IdealScheduleRetail = RetailId; // 0x10281ac1 / 0x102814d0
}

void FElysiumNpc::MissingSchedule()
{
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("ERROR: Missing or invalid schedule!")); // 0x1028226c
	SetActivity(GMaintainActIdle);													// 0x10282280, slot 310
}

int32 FElysiumNpc::LocalScheduleIdForStart(int32 Id)
{
	return GetLocalScheduleId(Id); // 0x10281d17
}

void FElysiumNpc::MaintenanceOnStartSchedule(int32 LocalScheduleId)
{
	OnStartSchedule(LocalScheduleId); // 0x10281d29, slot 436
}

void FElysiumNpc::DebugTaskStart(const FElysiumScheduleStep& Step)
{
	const bool bPrint = (DebugOverlays & OverlayTaskTextBit) != 0; // 0x10281d68, `npc_task_text`
	if (!bPrint && !IsAiTraced())
	{
		return;
	}
	// "Task: %s\n" (`0x105cdf20`), through the NPC trace, as `name (id)`; the AI trace's `task` event
	// adds the step's operand (debug output only).
	const FString Task = FString::Printf(TEXT("%s (%d)"),
		*FElysiumScheduleCorpus::Get().TaskOps().NameOf(Step.TaskId), Step.TaskId);
	if (bPrint)
	{
		NpcTraceMessage(FString::Printf(TEXT("Task: %s"), *Task)); // 0x10281d83
	}
	EmitAiTrace(TEXT("task"), FString::Printf(TEXT("%s %g"), *Task, Step.Data));
}

void FElysiumNpc::MaintenanceStartTaskOverlay()
{
	StartTaskOverlay(); // 0x10281e89, slot 445
}

bool FElysiumNpc::MaintenanceIsCurTaskContinuousMove()
{
	return IsCurTaskContinuousMove(); // 0x102820dc, slot 529
}

void FElysiumNpc::RememberContinuousMove()
{
	BaseScheduleHost.MemoryBits |= 0x00040000u; // 0x102820e6
}

bool FElysiumNpc::IsAiStepMode() const
{
	return World != nullptr && World->IsAiStepMode();
}

void FElysiumNpc::AdvanceAiStepDebugIndex()
{
	++MaintainDebugTaskIndex; // 0x102821fb
}

void FElysiumNpc::FreezeForAiStep()
{
	// `DAT_105c9798` is -1 in the pinned image, so every non-negative debug index passes.
	if (!NavIsGoalActive() && MaintainDebugTaskIndex >= -1) // 0x102821c2..0x102821e2
	{
		SequencePlaybackRate = 0.f; // 0x102821ea
	}
}

void FElysiumNpc::NextScheduledTaskForMaintenance(FElysiumScheduleState& State)
{
	check(&State == &Schedule);
	NextScheduledTask(); // 0x10281982
	if (FElysiumNpcScheduleHost::IsTaskIndexCurrent(Schedule))
	{
		// NextScheduledTask writes COND_SCHEDULE_DONE directly; this port callback additionally
		// latches the external-executor handoff on that exact edge.
		ScheduleDone();
	}
}

bool FElysiumNpc::TakeExternalExecutorReturn()
{
	const bool bReturn = bReturnToExternalExecutorAfterSchedule;
	bReturnToExternalExecutorAfterSchedule = false;
	return bReturn;
}
