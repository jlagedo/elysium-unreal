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
	SetCurrentStateTyped(EElysiumNpcState::Idle);
	SetDesiredStateTyped(EElysiumNpcState::Idle);
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

namespace
{
	int32 State19MindMapTyped(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		}
		return 0;
	}

	void State19MindApplyTyped(EElysiumNpcState& Out, int32 RetailId)
	{
		switch (RetailId)
		{
		case 1: Out = EElysiumNpcState::Idle;     return;
		case 2: Out = EElysiumNpcState::Combat;   return;
		case 3: Out = EElysiumNpcState::Alert;    return;
		case 4: Out = EElysiumNpcState::Scripted; return;
		case 6: Out = EElysiumNpcState::Prone;    return;
		case 7: Out = EElysiumNpcState::Dead;     return;
		default:
			return;
		}
	}
}

void FElysiumNpcMind::SetCurrentStateTyped(EElysiumNpcState NewState)
{
	CurrentState = NewState;
	// Any typed write retires the raw overlay, so `NpcStateRetail()` can never answer a stale id
	// the typed word has since moved away from. The retired value is `INDEX_NONE`, not 0, because
	// **0 is a retail state** — `NPC_STATE_NONE`, which `CAI_BaseNPCTroika::NPCInit` writes to
	// `m_NPCState` at `1029a0f5` and every state reader can see.
	PendingRetailNpcState = INDEX_NONE;
}

void FElysiumNpcMind::SetDesiredStateTyped(EElysiumNpcState NewState)
{
	DesiredState = NewState;
	PendingRetailIdealState = INDEX_NONE;
}

void FElysiumNpcMind::WriteNpcStateRetail(int32 RetailId)
{
	EElysiumNpcState Typed = CurrentState;
	State19MindApplyTyped(Typed, RetailId);
	SetCurrentStateTyped(Typed);
	PendingRetailNpcState = RetailId;
}

void FElysiumNpcMind::WriteIdealStateRetail(int32 RetailId)
{
	EElysiumNpcState Typed = DesiredState;
	State19MindApplyTyped(Typed, RetailId);
	SetDesiredStateTyped(Typed);
	PendingRetailIdealState = RetailId;
}

int32 FElysiumNpcMind::NpcStateRetail() const
{
	if (PendingRetailNpcState != INDEX_NONE)
	{
		return PendingRetailNpcState;
	}
	return State19MindMapTyped(CurrentState);
}

int32 FElysiumNpcMind::IdealStateRetail() const
{
	if (PendingRetailIdealState != INDEX_NONE)
	{
		return PendingRetailIdealState;
	}
	return State19MindMapTyped(DesiredState);
}

void FElysiumNpcMind::RequestDesiredState(int32 RetailIdealState, int32 RetailSourceLine)
{
	// `FUN_102ad260` / `FUN_102ad2d0`, the write half. Retail's body is three stores and a return:
	// the file/line ideal-state trace (`+0x1b3c` / `+0x1b40`, which the shape map records ABSENT and
	// the transition trace below stands in for), then `m_IdealNPCState = 8`.
	//
	// The typed `DesiredState` is deliberately NOT touched: no member of `EElysiumNpcState` is retail
	// state 8, and mapping it onto Alert or Combat would be a different behaviour wearing this one's
	// name. The raw id is stored so a reader — and the test — can see the request that was made.
	WriteIdealStateRetail(RetailIdealState);
	Record(FString::Printf(
		TEXT("ideal state requested: retail %d (AI_BaseNPCTroika.cpp:%d); no port state carries it"),
		RetailIdealState, RetailSourceLine));
}

void FElysiumNpcMind::Invalidate(const TCHAR* Reason, bool bDead)
{
	// Death writes Dead; dormancy writes Idle, a write with no retail writer named in `docs/vtmb/`
	// yet (the caller names it at its line). No owner: retail has none.
	SetCurrentStateTyped(bDead ? EElysiumNpcState::Dead : EElysiumNpcState::Idle);
	SetDesiredStateTyped(CurrentState);
	Record(FString::Printf(TEXT("invalidate: state=%s (%s)"), LexToString(CurrentState),
		Reason ? Reason : TEXT("no reason")));
}

void FElysiumNpcMind::Restore(EElysiumNpcState SavedState)
{
	AdmissionPhase = EAdmission::Admitted;
	// The `Scripted -> Idle` clamp, named: it pairs with K1 (a cine refuses a save while it possesses
	// an NPC) and the restart that would re-possess one, both V6's. A saved `Scripted` has no scene
	// to come back to until then.
	SetCurrentStateTyped(IsSupportedState(SavedState) && SavedState != EElysiumNpcState::Scripted
		? SavedState : EElysiumNpcState::Idle);
	SetDesiredStateTyped(CurrentState);
	Record(FString::Printf(TEXT("restore: state=%s"), LexToString(CurrentState)));
}
