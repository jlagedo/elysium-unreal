#pragma once

#include "CoreMinimal.h"
#include "ElysiumNpcMindTypes.h"

// The bounded first NPC mind: deterministic admission plus K7 body arbitration. It deliberately
// contains no navigation, animation, actor or UObject access. Patrol, ambient and sequence remain
// executors on FElysiumNpc and may act only while holding the token this class issued.
class FElysiumNpcMind
{
public:
	enum class EAdmission : uint8 { Spawned, Armed, Admitted };

	void ArmAdmission();
	bool Admit();
	bool RequestState(EElysiumNpcState NewState, const TCHAR* Reason);

	bool Acquire(EElysiumBodyOwner Requested, bool bSuspendCurrent,
		FElysiumBodyOwnerToken& OutToken, const TCHAR* Reason);
	// Would `Requested` be granted right now? A caller that retries a refused claim asks first, so a
	// claim the arbitration cannot grant yet does not record a refusal on every think.
	bool CanAcquire(EElysiumBodyOwner Requested) const { return IsAcquisitionAllowed(Requested); }
	bool Release(const FElysiumBodyOwnerToken& Token, const TCHAR* Reason);
	void ForgetSuspended(EElysiumBodyOwner Owner, const TCHAR* Reason);
	void Invalidate(const TCHAR* Reason, bool bDead);

	// Restore only resumable autonomous ownership. Session tokens/generations never serialize.
	void Restore(EElysiumNpcState SavedState, EElysiumBodyOwner SavedOwner);

	EAdmission Admission() const { return AdmissionPhase; }
	bool IsAdmitted() const { return AdmissionPhase == EAdmission::Admitted; }
	EElysiumNpcState State() const { return CurrentState; }
	EElysiumNpcState IdealState() const { return DesiredState; }
	EElysiumBodyOwner Owner() const { return CurrentOwner; }
	EElysiumBodyOwner SuspendedOwner() const { return ParkedOwner; }
	uint32 Generation() const { return OwnerGeneration; }
	FElysiumBodyOwnerToken CurrentToken() const
	{
		return CurrentOwner == EElysiumBodyOwner::None
			? FElysiumBodyOwnerToken() : FElysiumBodyOwnerToken{ CurrentOwner, OwnerGeneration };
	}
	const FString& LastTransition() const { return Last; }
	const TArray<FString>& Trace() const { return Transitions; }

	// One row from a system outside the mind — the schedule runner's selections and refusals share
	// the mind's trace so a single read shows stimulus, state, owner and schedule in order.
	void RecordExternal(const FString& Row) { Record(Row); }

	static bool IsSupportedState(EElysiumNpcState State);
	static bool IsResumableOwner(EElysiumBodyOwner Owner);

private:
	bool IsAcquisitionAllowed(EElysiumBodyOwner Requested) const;
	void Record(const FString& Row);
	void RefreshStateFromOwner();

	EAdmission AdmissionPhase = EAdmission::Spawned;
	EElysiumNpcState CurrentState = EElysiumNpcState::Idle;
	EElysiumNpcState DesiredState = EElysiumNpcState::Idle;
	EElysiumBodyOwner CurrentOwner = EElysiumBodyOwner::None;
	EElysiumBodyOwner ParkedOwner = EElysiumBodyOwner::None;
	uint32 OwnerGeneration = 0;
	FString Last;
	TArray<FString> Transitions;
	static constexpr int32 MaxTraceRows = 16;
};
