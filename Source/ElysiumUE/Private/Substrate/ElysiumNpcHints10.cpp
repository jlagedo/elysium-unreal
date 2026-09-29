#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"

// Story 29d, family **Hints10** — slot 566 and the Werewolf's hint endpoints. See
// `Substrate/ElysiumNpcHints10.inl` for what this family is and which seams it reuses; the
// walked prose is `docs/vtmb/npc-ai/conditions-and-states.md`.

// =================================================================================================
// Slot 566 `FValidateHintType` — the Troika-line dispatcher `0x10295c20`.
// =================================================================================================

FElysiumNpc::EHintTypeArm FElysiumNpc::FValidateHintTypeArm(int32 HintType)
{
	// `10295d80`..`10295e3d`, arm for arm in the listing's own order:
	//
	//     CMP EAX,0x27d8 ; JG  <upper>          10295d86
	//     JZ  0x10297430                        10295d8d   ==  0x27d8
	//     CMP EAX,0x64   ; JL  refuse           10295d8f   <   0x64     CONFIRMED
	//     CMP EAX,0x65   ; JLE 0x102974f0       10295d94   0x64..0x65   CONFIRMED
	//     CMP EAX,0x2774 ; JNZ refuse           10295d99   ==  0x2774 -> MOV AL,1
	//   <upper>:
	//     CMP EAX,0x283c ; JL  refuse           10295ded   <   0x283c   CONFIRMED
	//     CMP EAX,0x283d ; JLE 0x10295ed0       10295df8   0x283c..0x283d
	//     CMP EAX,0x28a0 ; JNZ refuse           10295dff   ==  0x28a0 -> 0x102961a0
	//
	// Note that the `0x2774` accept sits INSIDE the `99 < t` block, so a type below 0x64 can never
	// reach it — which is moot for a positive literal, and reproduced anyway because the structure
	// is what a reader checks against the listing.
	if (HintType > 0x27d8)
	{
		if (HintType < 0x283c)
		{
			return EHintTypeArm::Refuse;
		}
		if (HintType <= 0x283d)
		{
			return EHintTypeArm::QuietCoverRule;
		}
		return HintType == 0x28a0 ? EHintTypeArm::VerboseCoverRule : EHintTypeArm::Refuse;
	}
	if (HintType == 0x27d8)
	{
		return EHintTypeArm::CoverValid;
	}
	if (HintType < 0x64)
	{
		return EHintTypeArm::Refuse;
	}
	if (HintType <= 0x65)
	{
		return EHintTypeArm::CoverValidLoose;
	}
	return HintType == 0x2774 ? EHintTypeArm::Accept : EHintTypeArm::Refuse;
}

FString FElysiumNpc::HintGroupBitList(uint32 Mask)
{
	// `10295cdb`..`10295d0a` and its twin `10295d13`..`10295d42`. Retail's loop is
	// `for (i = 0; i < 0x20; ++i) if ((mask & (1 << i)) == (1 << i)) p += sprintf(p, " %d", i + 1);`
	// — `LEA EDX,[ESI + 0x1]` is the 1-BASED index, and the format at `0x105a1814` reads `" %d"` out
	// of the pinned image (the corpus holds the cell unnamed).
	FString Out;
	for (int32 Bit = 0; Bit < 0x20; ++Bit)
	{
		const uint32 One = 1u << static_cast<uint32>(Bit);
		if ((Mask & One) == One)
		{
			Out += FString::Printf(TEXT(" %d"), Bit + 1);
		}
	}
	return Out;
}

bool FElysiumNpc::FValidateHintType(void* Hint)
{
	// `CAI_BaseNPCTroika::FValidateHintType` (`0x10295c20`), slot 566, `vtable +0x8d8`.
	//
	// The generated signature spells the hint `void*` because retail's parameter is `CAI_Hint*` and
	// this substrate has no `CAI_Hint`. The CONVENTION this story settles, recorded here because it
	// has no default: the `void*` is a `const FHintWords*` — family Hints' typed view of a hint's own
	// datamap words is the only thing on this substrate that can stand for the parameter, and a
	// caller holding a node index goes through `FValidateHintTypeNode` below.
	const FHintWords* Words = static_cast<const FHintWords*>(Hint);

	// Eleven species classes override this method with their own bodies (story 5 step 4), ten a
	// constant or a type range and `CNPC_VManBat` a whole replacement. `CNPC_VBach` is the one
	// species body that FALLS THROUGH into this one rather than refusing.
	// `10295c96 TEST EBP,EBP / JZ` — a null hint takes the `XOR AL,AL` tail.
	if (Words == nullptr)
	{
		return false;
	}

	// `10295ca0`: `hint->m_iGroupID (+0x470) & this->m_iHintGroups (+0x62e4)`. A 32-bit SET on both
	// sides, not an id compare — `ElysiumNpcHints.inl` records that reading and
	// `FElysiumNpcScheduleHost::HintGroupMask` is the NPC's half, defaulting to `0xffffffff`.
	const uint32 GroupMask = static_cast<uint32>(Words->GroupMask);
	if ((GroupMask & ScheduleHost.HintGroupMask) == 0)
	{
		++HintGroupRefusals;
		// `10295cb8`: only when the globally tracked debug NPC (`DAT_10925444` through
		// `PTR_DAT_10566458`) IS this body does retail format the two bit lists and `DevMsg`
		// `"Hint group id %s usable %s"` (`0x105d8dd0`) onto the HINT (`0x102d0ab0`). Family
		// BaseHelpers' `IsHintDebugNpc()` is that comparison and answers false, so the message is
		// never built — which is retail's behaviour for every NPC but the debugged one. The bit
		// lists are still a named, tested rule (`HintGroupBitList`) because that is the recovery.
		if (IsHintDebugNpc())
		{
			UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("Hint group id %s usable %s"),
				*HintGroupBitList(GroupMask), *HintGroupBitList(ScheduleHost.HintGroupMask));
		}
		return false;
	}

	// Every arm hands the rule the HINT (`(int *)param_1`, `0x10295dd5` .. `0x10295e28`), which this
	// substrate names by its entity index -- not `m_nNodeID`, the network node it is bound to.
	switch (FValidateHintTypeArm(Words->HintType))
	{
	case EHintTypeArm::CoverValid:
		// `10295dd5` — `0x10297430`, family Hints' `IsHintCoverValid`. Called, not restated.
		return IsHintCoverValid(Words->HintIndex);
	case EHintTypeArm::CoverValidLoose:
		// `10295dba` — `0x102974f0`, family Hints' `IsHintCoverValidLoose`.
		return IsHintCoverValidLoose(Words->HintIndex);
	case EHintTypeArm::Accept:
		// `10295dac MOV AL,1`.
		return true;
	case EHintTypeArm::QuietCoverRule:
		// `10295e28` — `0x10295ed0`, family BaseHelpers' `FUN_10295ed0`.
		return FUN_10295ed0(Words->HintIndex);
	case EHintTypeArm::VerboseCoverRule:
		// `10295e0d` — `0x102961a0`, family BaseHelpers' `FUN_102961a0`, whose `None` is its pass.
		return FUN_102961a0(Words->HintIndex) == EHintRejectReason::None;
	case EHintTypeArm::Refuse:
	default:
		return false;
	}
}

bool FElysiumNpc::FValidateHintTypeNode(int32 HintNode) const
{
	FHintWords Words;
	if (!HintWords(HintNode, Words))
	{
		// The seam cannot resolve the node, which today is always. Retail's own null-hint arm.
		return false;
	}
	return const_cast<FElysiumNpc*>(this)->FValidateHintType(&Words);
}

// =================================================================================================
// `CNPC_VManBat::FValidateHintType` (`0x1038e480`) — slot 566's one replacement body.
// =================================================================================================

uint32 FElysiumNpc::HintObfuscationFold(uint32 Value)
{
	// `0x1042fbf0`, whose whole body is this one expression. `&` binds tighter than `^` in the
	// decompiled C and the listing agrees, so the mask applies to the inner fold alone.
	const uint32 Inner = ((((Value & 0x67c8c535u) ^ 0xdcb8cc14u) + 0x18e71cecu) ^ 0x82aa05e1u);
	return Value ^ (Inner & 0x98373acau) ^ 0xea3e269cu;
}

// =================================================================================================
// `CNPC_VWerewolf::IsValidTeleportHint` (`0x103d8300`).
// =================================================================================================

TArray<int32> FElysiumNpc::GlobalHintList() const
{
	// `DAT_10925450` walked by its `+0x5d8` next link: the world's hint list, head first.
	return World != nullptr ? World->HintList() : TArray<int32>();
}
