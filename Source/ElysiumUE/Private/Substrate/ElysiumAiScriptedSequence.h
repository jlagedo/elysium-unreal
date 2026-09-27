#pragma once

#include "Substrate/ElysiumScriptedSequence.h"

// `CCineAI` (primary vtable `0x10477d1c`), built by the `aiscripted_sequence` factory (`0x1000886e`
// overwrites the `CCineNPC` vtable after the shared constructor) — story 5 fold A3. No datamap of its
// own: its keys, inputs and outputs are `CCineNPC`'s, inherited through the descriptor chain.
//
// Five own vtable slots (the diff against `CCineNPC`, `0x104771e4`): slot 5 the deleting destructor
// (`0x101ab060`, the C++ destructor's) and the four branch virtuals below. It differs from
// `scripted_sequence` in exactly those: no queue (a standing cine is overwritten), no `DelayStart`,
// `FCanOverrideState` always true, and a finish schedule.
class FElysiumAiScriptedSequence : public FElysiumScriptedSequence
{
public:
	static constexpr const TCHAR* RetailClassName = TEXT("CCineAI");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;

	// Slot 583 `0x101a9080`.
	virtual void PossessEntity() override;
	// Slot 584 `0x101a9510` — `true` on the empty-name path too, no debug line.
	virtual bool StartSequence(FElysiumNpcBase& Npc, const FString& SequenceName, bool bCompleteOnEmpty) override;
	// Slot 585 `0x101a9060` — always true.
	virtual bool FCanOverrideState() const override;
	// Slot 586 `0x101a95d0` — `m_iFinishSchedule`.
	virtual void FixScriptNPCSchedule(FElysiumNpcBase& Npc) override;

	// `SCHED_AISCRIPT` and `SCHED_AMBUSH`, the two `cai_basenpc`-space LOCAL ids this class installs on
	// its NPC through `0x10280de0` (`FElysiumNpcBase::ChangeSchedule`, which translates them through the
	// NPC's own slot-580 space and slot 440).
	static constexpr int32 ScheduleAiScript = 0x2e;
	static constexpr int32 ScheduleAmbush = 0x2a;
};
