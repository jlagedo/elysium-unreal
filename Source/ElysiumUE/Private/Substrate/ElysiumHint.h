#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"
#include "Substrate/ElysiumNpc.h"

struct FElysiumClassDesc;

// `ai_hint` — retail's `CAI_Hint` (datamap `0x106099f0`), the live entity every hint-carrying
// `info_node*` / `info_hint` row becomes (`ElysiumNodeEntity::ApplyHintReplacement`; retail's
// `CNodeEnt::Spawn` `0x102d78d0` → `FUN_102d2f30`). It owns the hint's own words — the eleven
// keyfields, the claim (`m_hHintOwner`, `m_flNextUseTime`), the disabled word — and its place on
// the world's hint list (`FElysiumEntityWorld::HintList`). Kernel queries read it through
// `FElysiumNpcBase::HintWords`, which fills an `FHintWords` view from this entity.
//
// The network node (`m_nNodeID` `+0x5e4`) is the node-row counter `CNodeEnt::Spawn` gave it (0018
// story 4, `ElysiumNodeEntity::SpawnNodeRow`), -1 for a standalone row; the node holds the hint back
// at `+0xa0` (`FElysiumPlaceSet::AttachedHint`). The searches over the list (`0x102d1af0`,
// `0x102d24b0`, `0x102d2980`) are story 8's.
class FElysiumHint final : public FElysiumEntity
{
public:
	// --- Keyfields (generated bindings, `ElysiumNpcKernelBindings::AddHintFields`) ----------------
	float TargetAngleRange = 0.0f;   // +0x454 m_flTargetAngleRange  target_angle_range
	float TargetDistMin = 0.0f;      // +0x45c m_flTargetDistMin     target_dist_min
	float TargetDistMax = 0.0f;      // +0x460 m_flTargetDistMax     target_dist_max
	float HintRating = 0.0f;         // +0x464 m_flHintRating        hint_rating
	FString InterestTargetName;      // +0x468 m_strTargetName       target_name (the interest
	                                 //        record's place name, not this entity's targetname)
	int32 IpPercent = 0;             // +0x46c m_iIPPercent          ip_percent
	// +0x470 m_iGroupID, key group_id. `CAI_Hint::Spawn` (`0x102d0b60`) folds it IN PLACE into the
	// 32-bit group set (1..32 → one bit, anything else → -1), so after Spawn this is the mask.
	int32 GroupId = 0;
	FString UserData;                // +0x5d4 m_iszUserData         UserData
	int32 HintType = 0;              // +0x5dc m_nHintType           HintType (the AUTHORED value)
	int32 Disabled = 0;              // +0x5e8 m_iDisabled           StartHintDisabled
	FString Group;                   // +0x5f0 m_strGroup            Group (exact case)

	// --- Save-only words, no external ------------------------------------------------------------
	FString Activity;                                // +0x450 m_strActivity
	float TargetAngleRangeDot = 0.0f;                // +0x458 m_flTargetAngleRangeDot
	FElysiumEntityHandle HintOwner;                  // +0x5e0 m_hHintOwner (-1 from the ctor)
	int32 NodeId = INDEX_NONE;                       // +0x5e4 m_nNodeID; 0018 story 4 writes it at load
	float NextUseTime = 0.0f;                        // +0x5ec m_flNextUseTime (FIELD_TIME, a float)

	// The registered classname (`ElysiumNodeEntity::HintClassname`).
	static FName ClassName();

	// The entity as a hint, or null when it is not one. The registry is the type check: a hint is
	// an entity whose resolved descriptor is `ai_hint`'s.
	static FElysiumHint* Cast(FElysiumEntity* Entity);
	static const FElysiumHint* Cast(const FElysiumEntity* Entity);

	// The hint's words as the kernel reads them (`FElysiumNpc::FHintWords`), and the write-back of
	// the words a kernel body is allowed to change.
	FElysiumNpcBase::FHintWords ToWords() const;
	void FromWords(const FElysiumNpcBase::FHintWords& Words);

	// `CAI_Hint::Spawn` (`0x102d0b60`): the per-type defaults and the group fold
	// (`FElysiumNpcBase::HintSpawn`).
	virtual void Spawn() override;

	// Slot 77 `CAI_Hint::ScriptHide` (`0x102d0860`): the base hide, then `m_iDisabled := 1`.
	// Slot 78 `CAI_Hint::ScriptUnhide` (`0x102d0890`): the base unhide, then `m_iDisabled := 0`.
	// Slot 119 `CAI_Hint::Kill` (`0x102d08c0`) jumps to slot 77: a hint's Kill hides it.
	void HintScriptHide();
	void HintScriptUnhide();

	// `CAI_Hint::InputEnableHint` (`0x102d09f0`): the BASE unhide, then `m_iDisabled := 0`.
	// `CAI_Hint::InputDisableHint` (`0x102d0a20`): the BASE hide, then `m_iDisabled := 1`.
	void InputEnableHint(const FElysiumInputArgs& Args);
	void InputDisableHint(const FElysiumInputArgs& Args);

	// `CAI_Hint::InputWalk` / `InputDontWalk` (`0x102d0a50` / `0x102d0a80`): resolve the hint's
	// network node (`FUN_102d3e60`, `FElysiumPlaceSet::ResolveHintNode`) and set the walk bits of its
	// links (`FUN_102f97c0(node, 1/0)`). The lookup is real (0018 story 4) and retail's null arm
	// returns; the link write is a SEAM -- the place set carries no links (stories 5 and 7).
	void InputWalk(const FElysiumInputArgs& Args);
	void InputDontWalk(const FElysiumInputArgs& Args);

	// `CAI_Hint::InputSetUserData` (`0x102d4060`): a string argument (variant type 2) replaces
	// `m_iszUserData`; any other type clears it.
	void InputSetUserData(const FElysiumInputArgs& Args);

	// Retail's one entry gate on `CBaseEntity::AcceptInput` (`FUN_100abc90`): a hidden entity
	// answers true, doing nothing, to every input whose name is not a case-insensitive prefix of
	// `ScriptUnhide` (`Q_strnicmp(input, "ScriptUnhide", strlen(input))`).
	// SEAM for the gate itself, which this substrate does not stand globally; the hint's own inputs
	// apply it, because a hidden hint swallowing `EnableHint` is what the shipped scripts observe.
	bool SwallowsInput(const FElysiumInputArgs& Args) const;

	// Slot 130 `CAI_Hint::OnRestore` (`0x102d3ec0`): relink onto the node its restored `m_nNodeID`
	// names and stand at the node's origin (`FElysiumNpcBase::HintOnRestore`).
	virtual void OnPostRestore(FElysiumEntityWorld& InWorld) override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

	// Register `ai_hint`: generated fields, the hint inputs, and the slot-77/78/119 overrides.
	static void BuildClass(FElysiumClassDesc& D);
};
