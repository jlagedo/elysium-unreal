#pragma once

#include "CoreMinimal.h"
#include "ElysiumNpcMindTypes.h"

// The NPC's two state words, `m_NPCState` (`+0x5cc0`) and `m_IdealNPCState` (`+0x5cc4`), with the
// port's admission barrier and the transition trace. It holds no body owner: retail has none. Who
// holds a body is retail's own words on the NPC — `m_hCine` (`+0x5d74`, `FElysiumEntity::
// ScriptOwner`), `m_scriptState` (`+0x5d70`) and `m_hDialogPartner` (`+0xfe8`) — and the program it
// runs. Retail's writers reach `m_NPCState` through `SetState 0x1026e340` (`WriteNpcStateRetail` /
// `WriteIdealStateRetail`); the only other writes are the port's lifecycle ones below (admission,
// restore, invalidation), each named at its line. No navigation, animation, actor or UObject
// access.
class FElysiumNpcMind
{
public:
	enum class EAdmission : uint8 { Spawned, Armed, Admitted };

	// The admission barrier: port-only, V6's (lifecycle `NPCInitThink 0x10273aa0`; story V3
	// README Q5). It stays until V6 retires it.
	void ArmAdmission();
	bool Admit();

	// Death (`bDead`, state Dead) or dormancy (state Idle). The dormancy write has no retail writer
	// named in `docs/vtmb/` yet; the caller names it at its line.
	void Invalidate(const TCHAR* Reason, bool bDead);

	// A saved state, admitted. `Scripted` restores as `Idle`: a cine refuses a save while it
	// possesses an NPC (K1, a named divergence) and the restart that would re-possess it is V6's, so
	// a saved `Scripted` has no scene to return to. Both go together in V6.
	void Restore(EElysiumNpcState SavedState);

	EAdmission Admission() const { return AdmissionPhase; }
	bool IsAdmitted() const { return AdmissionPhase == EAdmission::Admitted; }
	EElysiumNpcState State() const { return CurrentState; }
	EElysiumNpcState IdealState() const { return DesiredState; }
	const FString& LastTransition() const { return Last; }
	const TArray<FString>& Trace() const { return Transitions; }

	// One row from a system outside the mind — the schedule runner's selections and refusals share
	// the mind's trace so a single read shows stimulus, state and schedule in order.
	void RecordExternal(const FString& Row) { Record(Row); }

	// --- Story 29c-1, family Schedule: `m_bForceStateChange` (`+0x1b28`) --------------------------
	//
	// `0x102ae840`, the scripted-schedule order push, stamps this word directly beside the order id
	// and the `CHOOSE_NEW_SCHEDULE` flag. It is not a transition request — it is a plain store — so
	// it is a set/consume pair. NOT CONSUMED yet: the state machine that reads it is story 29e's.
	void ForceStateChange() { bForceStateChange = true; }
	void ClearForceStateChange() { bForceStateChange = false; }
	bool IsStateChangeForced() const { return bForceStateChange; }

	static bool IsSupportedState(EElysiumNpcState State);

	// --- Story 29c-1, family Conditions: `RequestDesiredState` -----------------------------------
	//
	// `FUN_102ad260` and `FUN_102ad2d0`, the two identical flee arms `PreSelectIdealState` (slot 460,
	// `0x102ad340`) calls, write `m_IdealNPCState` (`+0x5cc4`) DIRECTLY — not through `SetState` and
	// not through any admission or support test. This is the write side of that, a plain store with
	// no support test.
	//
	// `RetailIdealState` is retail's RAW `NPC_STATE` id (`0x1026e3e0`'s cases: 1 idle, 2 combat,
	// 3 alert, 4 script, 7 dead, **8 flee**, 0xb hunt). Both callers pass 8.
	//
	// NOT BUILT — this runtime's `EElysiumNpcState` has no member for retail state 8, so the raw id
	// is all that is stored and `DesiredState` is LEFT ALONE. `DesiredRetailState()` is the read side;
	// the day a flee state exists, the mapping lands here and nothing else moves.
	void RequestDesiredState(int32 RetailIdealState, int32 RetailSourceLine);

	// `m_IdealNPCState` (`+0x5cc4`) as the raw retail id the write above stored, or 0 for "never
	// written". `IdealState()` beside it is this runtime's typed ideal and is the one every other
	// system reads. The overlay's own "never written" is `INDEX_NONE` — 0 is `NPC_STATE_NONE`, a
	// state retail writes — and this accessor keeps answering 0 for it, which is what its callers
	// and `RequestDesiredState`'s suite mean by "nothing was written".
	int32 DesiredRetailState() const
	{
		return PendingRetailIdealState == INDEX_NONE ? 0 : PendingRetailIdealState;
	}

	// Story 29e, family State19: `SetState` (`0x1026e340`) writes BOTH `+0x5cc0` and `+0x5cc4` as
	// raw `NPC_STATE` ids, including values this runtime's typed enum has no member for (8 FLEE,
	// 0xb HUNT, 0xe). These are that write and the matching reads. Mapped ids (1/2/3/4/6/7) also
	// update `CurrentState` / `DesiredState`; unmapped ids leave the typed words and keep the raw.
	void WriteNpcStateRetail(int32 RetailId);
	void WriteIdealStateRetail(int32 RetailId);
	int32 NpcStateRetail() const;
	int32 IdealStateRetail() const;
	void StampLastStateChangeTime(double Now) { LastStateChangeTime = Now; }
	double GetLastStateChangeTime() const { return LastStateChangeTime; }

private:
	void Record(const FString& Row);
	// `m_NPCState` and `m_IdealNPCState` have ONE source of truth each. The typed word is it; the
	// raw overlay below exists only for the retail ids `EElysiumNpcState` cannot spell (8 FLEE,
	// 0xb HUNT, 0xe), and any typed write retires the overlay so the two can never disagree.
	void SetCurrentStateTyped(EElysiumNpcState NewState);
	void SetDesiredStateTyped(EElysiumNpcState NewState);

	EAdmission AdmissionPhase = EAdmission::Spawned;
	EElysiumNpcState CurrentState = EElysiumNpcState::Idle;
	EElysiumNpcState DesiredState = EElysiumNpcState::Idle;
	FString Last;
	TArray<FString> Transitions;
	static constexpr int32 MaxTraceRows = 16;

	// --- The retail words, declared and unwritten ------------------------------------------------
	//
	// Every word of `CAI_BaseNPCTroika` this struct owns that no port system writes yet
	// (`docs/vtmb/npc-kernel/layout.md`), default-initialised, each carrying its offset, its
	// retail name and the tier that typed it. They are the shape 29b landed so a later story
	// fills a member instead of inventing one; `ElysiumNpcKernelShapeMap.cpp` binds every one of
	// them to its offset and the shape test fails if one goes missing.
	// The SAME retail word as `DesiredState` above (`+0x5cc4 m_IdealNPCState`), held a second time as
	// the raw retail id — NOT a new offset. `EElysiumNpcState` has no member for retail 8 (FLEE),
	// 0xb (HUNT) or 0xe, so a body that writes one of those has nowhere to put it; this is where it
	// goes until the vocabulary grows, and `RequestDesiredState` is its only writer. Session state:
	// the two flee arms rewrite it every pass they fire, and no save carries an ideal state.
	int32 PendingRetailIdealState = INDEX_NONE;
	// The SAME retail word as `CurrentState` above (`+0x5cc0 m_NPCState`), held a second time as
	// the raw retail id — NOT a new offset. `INDEX_NONE` means "never written raw"; readers then
	// map `CurrentState`. It cannot be 0, because **0 is `NPC_STATE_NONE`**: `NPCInit` writes it
	// (`1029a0f5`) and the state machine reads it back until the first `SetState`.
	int32 PendingRetailNpcState = INDEX_NONE;
	bool bForceStateChange = false;  // +0x1b28 m_bForceStateChange (datamap)
	// +0x5cc8 m_flLastStateChangeTime (datamap) — an absolute curtime stamp, carried as double
	double LastStateChangeTime = 0.0;
};
