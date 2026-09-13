#include "Substrate/ElysiumNpcMind.h"

void FElysiumNpcMind::Record(const FString& Row)
{
	Last = Row;
	Transitions.Add(Row);
	if (Transitions.Num() > MaxTraceRows)
	{
		Transitions.RemoveAt(0, Transitions.Num() - MaxTraceRows, EAllowShrinking::No);
	}
}

void FElysiumNpcMind::ArmAdmission()
{
	if (AdmissionPhase == EAdmission::Spawned)
	{
		AdmissionPhase = EAdmission::Armed;
		Record(TEXT("admission: spawned -> armed"));
	}
}

bool FElysiumNpcMind::Admit()
{
	if (AdmissionPhase != EAdmission::Armed)
	{
		return false;
	}
	AdmissionPhase = EAdmission::Admitted;
	CurrentState = EElysiumNpcState::Idle;
	DesiredState = EElysiumNpcState::Idle;
	Record(TEXT("admission: armed -> admitted (Idle, no executor)"));
	return true;
}

bool FElysiumNpcMind::IsSupportedState(EElysiumNpcState State)
{
	// `Alert` and `Combat` are admitted: the two-layer
	// `SelectIdealState` promotes into them from gathered conditions and a committed enemy, and the
	// state selects its own schedule. `Prone` stays a named refusal — nothing recovered produces it,
	// and a state with no producer is a diagnostic, not a transition.
	return State == EElysiumNpcState::Idle
		|| State == EElysiumNpcState::Alert
		|| State == EElysiumNpcState::Combat
		|| State == EElysiumNpcState::Scripted
		|| State == EElysiumNpcState::Dead;
}

bool FElysiumNpcMind::IsResumableOwner(EElysiumBodyOwner Owner)
{
	return Owner == EElysiumBodyOwner::None
		|| Owner == EElysiumBodyOwner::Patrol
		|| Owner == EElysiumBodyOwner::Ambient;
}

bool FElysiumNpcMind::RequestState(EElysiumNpcState NewState, const TCHAR* Reason)
{
	DesiredState = NewState;
	if (!IsSupportedState(NewState))
	{
		Record(FString::Printf(TEXT("state rejected: %s (%s)"), LexToString(NewState),
			Reason ? Reason : TEXT("no reason")));
		return false;
	}
	const EElysiumNpcState Before = CurrentState;
	CurrentState = NewState;
	Record(FString::Printf(TEXT("state: %s -> %s (%s)"), LexToString(Before),
		LexToString(NewState), Reason ? Reason : TEXT("no reason")));
	return true;
}

bool FElysiumNpcMind::IsAcquisitionAllowed(EElysiumBodyOwner Requested) const
{
	if (Requested == EElysiumBodyOwner::None || AdmissionPhase != EAdmission::Admitted
		|| CurrentState == EElysiumNpcState::Dead)
	{
		return false;
	}
	if (Requested == CurrentOwner)
	{
		return true;
	}
	switch (Requested)
	{
	case EElysiumBodyOwner::Patrol:
	case EElysiumBodyOwner::Ambient:
		return CurrentOwner == EElysiumBodyOwner::None;
	// The combat schedule families' movement claim. It displaces this NPC's own autonomous
	// executors: `FElysiumNpc::Think` routes a committed enemy to schedule selection ahead of the
	// patrol and interesting-place executors, and the arbiter has to be able to grant what that
	// routing decided — otherwise a patrolling guard would select a chase and then refuse itself the
	// body. Patrol is suspended by the claim and resumes on release; an interesting-place visit is
	// finished at the claim site instead, because ambient owns a claimed place rather than a
	// resumable route.
	//
	// It does NOT displace `ScriptedSchedule`: an authored director outranks instinct.
	case EElysiumBodyOwner::Schedule:
		return CurrentOwner == EElysiumBodyOwner::None
			|| CurrentOwner == EElysiumBodyOwner::Patrol
			|| CurrentOwner == EElysiumBodyOwner::Ambient;
	// `aiscripted_schedule`'s movement claim. It outranks both autonomous executors and an ordinary
	// combat `Schedule`, because the map author asked for this movement by name and the combat
	// program was this NPC's own idea. Sequence and dialogue still displace it — a cutscene and a
	// conversation own the body outright, and a director pushing a goal does not.
	case EElysiumBodyOwner::ScriptedSchedule:
		return CurrentOwner == EElysiumBodyOwner::None
			|| CurrentOwner == EElysiumBodyOwner::Patrol
			|| CurrentOwner == EElysiumBodyOwner::Ambient
			|| CurrentOwner == EElysiumBodyOwner::Schedule;
	case EElysiumBodyOwner::Sequence:
		return CurrentOwner == EElysiumBodyOwner::None
			|| CurrentOwner == EElysiumBodyOwner::Patrol
			|| CurrentOwner == EElysiumBodyOwner::Ambient
			|| CurrentOwner == EElysiumBodyOwner::Schedule
			|| CurrentOwner == EElysiumBodyOwner::ScriptedSchedule;
	case EElysiumBodyOwner::Dialogue:
		return CurrentOwner == EElysiumBodyOwner::None
			|| CurrentOwner == EElysiumBodyOwner::Patrol
			|| CurrentOwner == EElysiumBodyOwner::Schedule
			|| CurrentOwner == EElysiumBodyOwner::ScriptedSchedule;
	default:
		return false;
	}
}

void FElysiumNpcMind::RefreshStateFromOwner()
{
	// `ScriptedSchedule` is deliberately NOT in this set, and it is the one owner in the arbiter that
	// is a body claim without being a cognitive state. `aiscripted_schedule` "pushes an AI policy and
	// goal rather than claiming the body for one exact animation"
	// (`docs/vtmb/npc-ai/README.md`), and the policy it pushes IS a state — its
	// `forcestate` resolves to idle, alert or combat. Forcing `Scripted` on the claim would overwrite
	// the push with the arbiter's opinion the instant the first movement task ran, which is exactly
	// backwards: eleven of the thirteen corpus rows author a forced state, eight of them combat.
	const bool bScripted = CurrentOwner == EElysiumBodyOwner::Sequence
		|| CurrentOwner == EElysiumBodyOwner::Dialogue
		|| CurrentOwner == EElysiumBodyOwner::Follower;
	if (bScripted)
	{
		CurrentState = EElysiumNpcState::Scripted;
	}
	else if (CurrentState == EElysiumNpcState::Scripted)
	{
		// Handing the body back leaves the ordinary states, and idle is where an unowned body
		// starts; the next decision pass re-derives alert or combat from its conditions.
		CurrentState = EElysiumNpcState::Idle;
	}
	// An ordinary owner change is NOT a cognitive transition (K7 separates the two): a patrol token
	// taken by an alert NPC must not reset it to idle, or the ideal-state pass and the body arbiter
	// would fight for the state every think.
	DesiredState = CurrentState;
}

bool FElysiumNpcMind::Acquire(EElysiumBodyOwner Requested, bool bSuspendCurrent,
	FElysiumBodyOwnerToken& OutToken, const TCHAR* Reason)
{
	OutToken.Reset();
	if (!IsAcquisitionAllowed(Requested))
	{
		Record(FString::Printf(TEXT("owner rejected: %s -> %s (%s)"),
			LexToString(CurrentOwner), LexToString(Requested), Reason ? Reason : TEXT("no reason")));
		return false;
	}
	if (Requested == CurrentOwner)
	{
		OutToken = { CurrentOwner, OwnerGeneration };
		return OutToken.IsSet();
	}
	const EElysiumBodyOwner Before = CurrentOwner;
	ParkedOwner = bSuspendCurrent ? CurrentOwner : EElysiumBodyOwner::None;
	CurrentOwner = Requested;
	++OwnerGeneration;
	if (OwnerGeneration == 0)
	{
		++OwnerGeneration;
	}
	OutToken = { CurrentOwner, OwnerGeneration };
	RefreshStateFromOwner();
	Record(FString::Printf(TEXT("owner: %s -> %s gen=%u%s (%s)"), LexToString(Before),
		LexToString(CurrentOwner), OwnerGeneration, ParkedOwner != EElysiumBodyOwner::None
			? *FString::Printf(TEXT(" parked=%s"), LexToString(ParkedOwner)) : TEXT(""),
		Reason ? Reason : TEXT("no reason")));
	return true;
}

bool FElysiumNpcMind::Release(const FElysiumBodyOwnerToken& Token, const TCHAR* Reason)
{
	if (!Token.IsSet() || Token.Owner != CurrentOwner || Token.Generation != OwnerGeneration)
	{
		Record(FString::Printf(TEXT("release rejected: token=%s/%u live=%s/%u (%s)"),
			LexToString(Token.Owner), Token.Generation, LexToString(CurrentOwner), OwnerGeneration,
			Reason ? Reason : TEXT("no reason")));
		return false;
	}
	const EElysiumBodyOwner Before = CurrentOwner;
	CurrentOwner = ParkedOwner;
	ParkedOwner = EElysiumBodyOwner::None;
	++OwnerGeneration;
	if (OwnerGeneration == 0)
	{
		++OwnerGeneration;
	}
	RefreshStateFromOwner();
	Record(FString::Printf(TEXT("owner: %s -> %s gen=%u (%s)"), LexToString(Before),
		LexToString(CurrentOwner), OwnerGeneration, Reason ? Reason : TEXT("no reason")));
	return true;
}

void FElysiumNpcMind::ForgetSuspended(EElysiumBodyOwner Owner, const TCHAR* Reason)
{
	if (ParkedOwner == Owner)
	{
		ParkedOwner = EElysiumBodyOwner::None;
		Record(FString::Printf(TEXT("parked owner cleared: %s (%s)"), LexToString(Owner),
			Reason ? Reason : TEXT("no reason")));
	}
}

void FElysiumNpcMind::Invalidate(const TCHAR* Reason, bool bDead)
{
	const EElysiumBodyOwner Before = CurrentOwner;
	CurrentOwner = EElysiumBodyOwner::None;
	ParkedOwner = EElysiumBodyOwner::None;
	++OwnerGeneration;
	if (OwnerGeneration == 0)
	{
		++OwnerGeneration;
	}
	CurrentState = bDead ? EElysiumNpcState::Dead : EElysiumNpcState::Idle;
	DesiredState = CurrentState;
	Record(FString::Printf(TEXT("invalidate: %s -> None gen=%u state=%s (%s)"),
		LexToString(Before), OwnerGeneration, LexToString(CurrentState),
		Reason ? Reason : TEXT("no reason")));
}

void FElysiumNpcMind::Restore(EElysiumNpcState SavedState, EElysiumBodyOwner SavedOwner)
{
	AdmissionPhase = EAdmission::Admitted;
	CurrentOwner = IsResumableOwner(SavedOwner) ? SavedOwner : EElysiumBodyOwner::None;
	ParkedOwner = EElysiumBodyOwner::None;
	++OwnerGeneration;
	CurrentState = IsSupportedState(SavedState) && SavedState != EElysiumNpcState::Scripted
		? SavedState : EElysiumNpcState::Idle;
	if (CurrentState == EElysiumNpcState::Dead)
	{
		CurrentOwner = EElysiumBodyOwner::None;
	}
	DesiredState = CurrentState;
	Record(FString::Printf(TEXT("restore: state=%s owner=%s gen=%u"), LexToString(CurrentState),
		LexToString(CurrentOwner), OwnerGeneration));
}
