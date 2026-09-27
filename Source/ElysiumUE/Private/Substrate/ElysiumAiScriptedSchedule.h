#pragma once

#include "Substrate/ElysiumScriptedScheduleOrder.h"
#include "Substrate/ElysiumScriptedSequence.h"

// `aiscripted_schedule` — `CCineAISchedule` (primary vtable `0x10478854`, factory `0x101a96b0`), the
// authored AI director, a `CCineNPC` subclass (story 5 fold A3).
//
// It pushes a state and a goal instead of claiming the body for an animation: its `PossessEntity`
// (`0x101a9790`) takes no `m_hCine` and writes no script state or ideal state, and goes straight to
// its own `FixScriptNPCSchedule` (`0x101a98c0`), which forces the authored state and installs a
// move/follow program or an enemy on the NPC. `CCineNPC`'s slot 584 is inherited and unreachable
// (nothing it calls sets `m_hCine`).
//
// Target acquisition is `CCineNPC`'s: `StartSchedule` (`0x101a9b30`) is `ThinkSet(CineThink)` at
// curtime, and `CineThink` (`0x101a8070`) runs `FindEntity` over `m_iszEntity` WITHIN `m_flRadius`
// (23 of the 30 shipped rows author one); an NPC not found is `CancelScript` and a retry every second.
//
// Six own vtable slots (the diff against `CCineNPC`): slot 5 the deleting destructor (`0x101ab0e0`,
// the C++ destructor's), 82, 103, 583, 585, 586.
class FElysiumAiScriptedSchedule : public FElysiumScriptedSequence
{
public:
	ELYSIUM_NPC_CLASS("CCineAISchedule", FElysiumScriptedSequence)

	// Slot 82 `0x101a9620` — `&datamap_CCineAISchedule` (`0x10593c9c`), chained to `CCineNPC`'s.
	virtual void* GetDataDescMap() override;
	// Slot 103 `0x101a9730` — `CCineNPC::Spawn` (direct), then the "no schedule" line.
	virtual void Spawn() override;
	// Slot 583 `0x101a9790`.
	virtual void PossessEntity() override;
	// Slot 585 `0x101a9770` — always true.
	virtual bool FCanOverrideState() const override;
	// Slot 586 `0x101a98c0`.
	virtual void FixScriptNPCSchedule(FElysiumNpcBase& Npc) override;

	// `InputStartSchedule` `0x101a9b30`.
	void InputStartSchedule(const FElysiumInputArgs& Args);
	static void AddInputs(FElysiumClassDesc& D, const TCHAR* RetailClass);

	// The datamap words (`CCineAISchedule`, `+0x608c..+0x6094`), bound by `gen_kernel_bindings`.
	FString GoalEntity;     // +0x608c m_sGoalEnt — KEY `goalent`
	int32 Mode = 0;         // +0x6090 m_nSchedule — KEY `schedule`
	int32 ForceState = 0;   // +0x6094 m_nForceState — KEY `forcestate`, NOT the native state enum

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	// `0x100f7f20(NULL, m_sGoalEnt, this, 0)`: the procedural names, the first named match, else the
	// first classname match.
	FElysiumEntity* ResolveGoal();
};
