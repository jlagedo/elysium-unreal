#include "Substrate/ElysiumHint.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumNodeEntity.h"
#include "Substrate/ElysiumNpcKernelBindings.h"

FName FElysiumHint::ClassName()
{
	static const FName Name(ElysiumNodeEntity::HintClassname);
	return Name;
}

FElysiumHint* FElysiumHint::Cast(FElysiumEntity* Entity)
{
	return Entity != nullptr && !Entity->bRecordOnly && Entity->Class != nullptr
			&& Entity->Class->ClassName == ClassName()
		? static_cast<FElysiumHint*>(Entity) : nullptr;
}

const FElysiumHint* FElysiumHint::Cast(const FElysiumEntity* Entity)
{
	return Cast(const_cast<FElysiumEntity*>(Entity));
}

FElysiumNpc::FHintWords FElysiumHint::ToWords() const
{
	FElysiumNpc::FHintWords Words;
	Words.bValid = true;
	Words.HintIndex = Handle.Index;
	Words.Name = TargetName;
	Words.Activity = Activity;
	Words.TargetAngleRange = TargetAngleRange;
	Words.TargetAngleRangeDot = TargetAngleRangeDot;
	Words.TargetDistMin = TargetDistMin;
	Words.TargetDistMax = TargetDistMax;
	Words.HintRating = HintRating;
	Words.TargetName = InterestTargetName;
	Words.IpPercent = IpPercent;
	Words.GroupMask = GroupId;
	Words.HintType = HintType;
	Words.HintOwner = HintOwner;
	Words.NodeId = NodeId;
	Words.Disabled = Disabled;
	Words.NextUseTime = NextUseTime;
	Words.Group = Group;
	Words.OriginCm = Origin;
	Words.Angles = Angles;
	return Words;
}

void FElysiumHint::FromWords(const FElysiumNpc::FHintWords& Words)
{
	// The words a kernel body writes back: the Spawn fill and fold, the claim, the disabled word.
	// Identity (name, type, group, node, position) is the entity's and is never taken from a view.
	TargetAngleRange = Words.TargetAngleRange;
	TargetAngleRangeDot = Words.TargetAngleRangeDot;
	TargetDistMin = Words.TargetDistMin;
	TargetDistMax = Words.TargetDistMax;
	HintRating = Words.HintRating;
	GroupId = Words.GroupMask;
	HintOwner = Words.HintOwner;
	Disabled = Words.Disabled;
	NextUseTime = static_cast<float>(Words.NextUseTime);
}

void FElysiumHint::Spawn()
{
	// `CAI_Hint::Spawn` (`0x102d0b60`). Its `SetSolid(0)`/`Relink` half has no body here to act on;
	// the per-type default block and the in-place group fold are `FElysiumNpc::HintSpawn`.
	FElysiumNpc::FHintWords Words = ToWords();
	FElysiumNpc::HintSpawn(Words);
	FromWords(Words);
}

void FElysiumHint::HintScriptHide()
{
	ScriptHide();
	FElysiumNpc::FHintWords Words = ToWords();
	FElysiumNpc::HintScriptHide(Words);
	FromWords(Words);
}

void FElysiumHint::HintScriptUnhide()
{
	ScriptUnhide();
	FElysiumNpc::FHintWords Words = ToWords();
	FElysiumNpc::HintScriptUnhide(Words);
	FromWords(Words);
}

void FElysiumHint::InputEnableHint(const FElysiumInputArgs&)
{
	// `0x102d09f0` calls `CBaseEntity::ScriptUnhide` by address, not the hint's slot 78.
	ScriptUnhide();
	Disabled = 0;
}

void FElysiumHint::InputDisableHint(const FElysiumInputArgs&)
{
	// `0x102d0a20` calls `CBaseEntity::ScriptHide` by address, not the hint's slot 77.
	ScriptHide();
	Disabled = 1;
}

void FElysiumHint::InputWalk(const FElysiumInputArgs&)
{
	// SEAM: `FUN_102d3e60` resolves the network node this hint is bound to; no node is bound until
	// 0018 story 4, and retail's own null arm returns without touching anything.
}

void FElysiumHint::InputDontWalk(const FElysiumInputArgs&)
{
	// SEAM: as `InputWalk`, with `FUN_102f97c0(node, 0)`.
}

void FElysiumHint::InputSetUserData(const FElysiumInputArgs& Args)
{
	// `*(int *)(param_1 + 0x18) == 2` — the variant is a string.
	UserData = Args.Param.IsString() ? Args.Param.ToString() : FString();
}

bool FElysiumHint::SwallowsInput(const FElysiumInputArgs& Args) const
{
	// `Q_strnicmp(input, "ScriptUnhide", strlen(input)) != 0`: the input passes when its name is a
	// case-insensitive PREFIX of `ScriptUnhide`, and every other name is swallowed.
	const FString Name = Args.Input.ToString();
	return IsHidden() && FCString::Strnicmp(*Name, TEXT("ScriptUnhide"), Name.Len()) != 0;
}

void FElysiumHint::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Authored class"), Def != nullptr && !Def->SourceClassname.IsEmpty()
		? Def->SourceClassname : TEXT("(none)"));
	Out.Emplace(TEXT("HintType"), FString::FromInt(HintType));
	Out.Emplace(TEXT("Group"), Group.IsEmpty() ? TEXT("(none)") : Group);
	Out.Emplace(TEXT("Group mask"), FString::Printf(TEXT("0x%08x"), static_cast<uint32>(GroupId)));
	Out.Emplace(TEXT("Disabled"), Disabled != 0 ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Owner"), HintOwner.IsSet() ? FString::FromInt(HintOwner.Index) : TEXT("(none)"));
	Out.Emplace(TEXT("Next use"), FString::Printf(TEXT("%.3f"), NextUseTime));
	Out.Emplace(TEXT("Node"), NodeId == INDEX_NONE ? TEXT("unbound (story 3)") : FString::FromInt(NodeId));
}

void FElysiumHint::BuildClass(FElysiumClassDesc& D)
{
	ElysiumNpcKernelBindings::AddHintFields(D);

	// Every hint input runs behind retail's hidden-entity gate; see `SwallowsInput`.
	D.Input(TEXT("EnableHint"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			FElysiumHint& Hint = static_cast<FElysiumHint&>(E);
			if (!Hint.SwallowsInput(Args)) { Hint.InputEnableHint(Args); }
		});
	D.Input(TEXT("DisableHint"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			FElysiumHint& Hint = static_cast<FElysiumHint&>(E);
			if (!Hint.SwallowsInput(Args)) { Hint.InputDisableHint(Args); }
		});
	D.Input(TEXT("Walk"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			FElysiumHint& Hint = static_cast<FElysiumHint&>(E);
			if (!Hint.SwallowsInput(Args)) { Hint.InputWalk(Args); }
		});
	D.Input(TEXT("DontWalk"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			FElysiumHint& Hint = static_cast<FElysiumHint&>(E);
			if (!Hint.SwallowsInput(Args)) { Hint.InputDontWalk(Args); }
		});
	D.Input(TEXT("SetUserData"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			FElysiumHint& Hint = static_cast<FElysiumHint&>(E);
			if (!Hint.SwallowsInput(Args)) { Hint.InputSetUserData(Args); }
		});
	// The base inputs whose slot the hint overrides. `Kill` reaches slot 119, which on a hint is a
	// jump to slot 77: it hides the hint and disables it, and never removes it.
	D.Input(TEXT("Kill"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			FElysiumHint& Hint = static_cast<FElysiumHint&>(E);
			if (!Hint.SwallowsInput(Args)) { Hint.HintScriptHide(); }
		});
	D.Input(TEXT("ScriptHide"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			FElysiumHint& Hint = static_cast<FElysiumHint&>(E);
			if (!Hint.SwallowsInput(Args)) { Hint.HintScriptHide(); }
		});
	D.Input(TEXT("ScriptUnhide"), [](FElysiumEntity& E, const FElysiumInputArgs&)
		{ static_cast<FElysiumHint&>(E).HintScriptUnhide(); });

	// The save-only words the claim protocol writes (`0x102d1420`, `0x102d14c0`).
	ElysiumAddClassField(D, TEXT("m_hHintOwner"), &FElysiumHint::HintOwner, EElysiumField::Save);
	ElysiumAddClassField(D, TEXT("m_nNodeID"), &FElysiumHint::NodeId, EElysiumField::Save);
	ElysiumAddClassField(D, TEXT("m_strActivity"), &FElysiumHint::Activity, EElysiumField::Save);
	ElysiumAddClassField(D, TEXT("m_flNextUseTime"), &FElysiumHint::NextUseTime, EElysiumField::Save);
	ElysiumAddClassField(D, TEXT("m_flTargetAngleRangeDot"), &FElysiumHint::TargetAngleRangeDot,
		EElysiumField::Save);
}
