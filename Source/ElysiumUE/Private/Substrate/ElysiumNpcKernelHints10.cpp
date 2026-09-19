#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"

// Story 29d, family **Hints10** — slot 566 and the Werewolf's hint endpoints. See
// `Substrate/ElysiumNpcKernelHints10.inl` for what this family is and which seams it reuses; the
// walked prose is `docs/vtmb/npc-ai/conditions-and-states.md`.

namespace
{
	// The census addresses of the bodies that fill slot 566, as `slots.md` records them. Only the
	// ManBat arm is this family's; the other eleven are story 29c-1's table
	// (`FElysiumNpc::HintTypeSpeciesRows`) and the twelfth is the Troika line itself.
	const TCHAR* const GHints10Body_ManBatValidate = TEXT("0x1038e480");
	const TCHAR* const GHints10Body_TroikaValidate = TEXT("0x10295c20");

	// `CNPC_VManBat`'s five name templates, at their `.rdata` addresses in the pinned image. Read out
	// of `vampire.dll` at `address - 0x10000000` because the corpus's string table does not hold the
	// two that no decompiled body names as a literal.
	// `ManBat Landpoint` (`0x10642c74`) is the only one without a `%d`; the four formats are spelled
	// inline at their `FString::Printf` call sites because the engine's checked-format-string
	// sanitiser only accepts a literal there. Each carries its `.rdata` address at the call.
	const TCHAR* const GHints10ManBatLandpoint = TEXT("ManBat Landpoint");            // 0x10642c74

	// `DAT_1070d1b0/b4/b8`, the triple `GetHintEndpoint` answers for a null hint.
	// `staticinit_101370b0` writes zero into all three, so it is `vec3_origin`.
	const FVector GHints10Vec3Origin = FVector::ZeroVector;
}

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

	// Retail dispatches slot 566 by class before the body runs at all. Twelve census rows carry a
	// species body; eleven of them are story 29c-1's constant/range table and one — `CNPC_VManBat` —
	// is a whole replacement body, this family's row. `CNPC_VBach` is the one row that FALLS THROUGH
	// to this body rather than refusing, which `HintTypeSpeciesFallsThroughToBase` states.
	const FElysiumNpcClass* Cls = RetailClass();
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(Cls, 566);
	if (SlotBody != nullptr && FCString::Strcmp(SlotBody, GHints10Body_ManBatValidate) == 0)
	{
		// `1038e5b3` — the ManBat arm dereferences its hint unconditionally; a null one faults in
		// retail. CRASH GUARD, named: a null hint answers false, which is the arm every other
		// refusal in this body reaches.
		return Words != nullptr && ManBatValidateHintType(*Words);
	}
	if (SlotBody != nullptr && FCString::Strcmp(SlotBody, GHints10Body_TroikaValidate) != 0)
	{
		const FHintTypeSpecies* Row = HintTypeSpeciesOf(Cls != nullptr ? Cls->Name : nullptr);
		// `CNPC_VTzimisce` (`0x103ba780`) is the ONE species body that null-checks the hint; the
		// other ten dereference it. The type of a hint the seam could not resolve is 0, which no
		// species row accepts, so the answer is the same either way.
		const int32 SpeciesType = Words != nullptr ? Words->HintType : 0;
		if (FValidateHintTypeSpecies(Row, SpeciesType))
		{
			return true;
		}
		if (!HintTypeSpeciesFallsThroughToBase(Row))
		{
			return false;
		}
		// `CNPC_VBach` (`0x10365800`) falls through into the body below rather than refusing.
	}

	// `10295c96 TEST EBP,EBP / JZ` — a null hint takes the `XOR AL,AL` tail.
	if (Words == nullptr)
	{
		return false;
	}

	// `10295ca0`: `hint->m_iGroupID (+0x470) & this->m_iHintGroups (+0x62e4)`. A 32-bit SET on both
	// sides, not an id compare — `ElysiumNpcKernelHints.inl` records that reading and
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

	switch (FValidateHintTypeArm(Words->HintType))
	{
	case EHintTypeArm::CoverValid:
		// `10295dd5` — `0x10297430`, family Hints' `IsHintCoverValid`. Called, not restated.
		return IsHintCoverValid(Words->NodeId);
	case EHintTypeArm::CoverValidLoose:
		// `10295dba` — `0x102974f0`, family Hints' `IsHintCoverValidLoose`.
		return IsHintCoverValidLoose(Words->NodeId);
	case EHintTypeArm::Accept:
		// `10295dac MOV AL,1`.
		return true;
	case EHintTypeArm::QuietCoverRule:
		// `10295e28` — `0x10295ed0`, family BaseHelpers' `FUN_10295ed0`.
		return FUN_10295ed0(Words->NodeId);
	case EHintTypeArm::VerboseCoverRule:
		// `10295e0d` — `0x102961a0`, family BaseHelpers' `FUN_102961a0`, whose `None` is its pass.
		return FUN_102961a0(Words->NodeId) == EHintRejectReason::None;
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

uint32 FElysiumNpc::ManBatHintMode(uint32 ScrambledWord)
{
	// `1038e49b`..`1038e4bf`, verbatim:
	//     MOV ECX,[ESI + 0x6670] ; MOV EAX,ECX
	//     AND EAX,0x710935 ; XOR EAX,0x148739 ; ADD EAX,0x4094ab
	//     AND EAX,0x18ef6ca ; XOR EAX,ECX ; XOR EAX,0x412a96ec
	//     CALL 0x1042fbf0
	uint32 Acc = (ScrambledWord & 0x00710935u) ^ 0x00148739u;
	Acc = (Acc + 0x004094abu) & 0x018ef6cau;
	Acc = Acc ^ ScrambledWord ^ 0x412a96ecu;
	return HintObfuscationFold(Acc);
}

FString FElysiumNpc::ManBatHintName(uint32 Mode, int32 Index)
{
	// `1038e4c7 DEC EAX / CMP EAX,0x7 / JA default / JMP [EAX*4 + 0x1038e5c0]`. The jump table holds
	// eight entries for modes 1..8; 5, 6 and 7 point at the default label `1038e51b`. Anything
	// outside 1..8 — including 0 — takes the default too, because the `DEC` makes it wrap above 7.
	switch (Mode)
	{
	case 1:
		// `1038e4f5` — a raw byte-copy loop, NOT a `sprintf`: mode 1 carries no `%d` and the index
		// is never read on this arm.
		return GHints10ManBatLandpoint;
	case 2:
	case 4:
		return FString::Printf(TEXT("ManBat Divepoint %d"), Index);          // 0x10642ca8
	case 3:
		return FString::Printf(TEXT("ManBat Divepoint %d Bottom"), Index);   // 0x10642c88
	case 8:
		return FString::Printf(TEXT("ManBat Script Node %d"), Index);        // 0x10642c58
	default:
		return FString::Printf(TEXT("ManBat %d"), Index);                    // 0x10642c4c
	}
}

bool FElysiumNpc::ManBatValidateHintType(const FHintWords& Hint) const
{
	// `1038e48b CMP [EBX + 0x5dc],0x4e20 / JNZ 0x1038e5b3` — only hint type 20000 is considered.
	if (Hint.HintType != 20000)
	{
		return false;
	}

	const FString Name = ManBatHintName(ManBatHintMode(ManBatHintModeWord), ManBatHintIndex);

	// `1038e534`: `in_EAX = hint->m_iName; if (in_EAX == 0 || in_EAX != local_50) { ... }`. The
	// second test compares the NAME POINTER against the stack buffer's address, which can never be
	// equal, so the compare below always runs. Reproduced as the unconditional compare it is.
	//
	// `1038e556 JNZ` — the `repne scasb` length. When the built name is EMPTY retail does NOT
	// compare strings at all: it loads the hint's name POINTER into `EAX` and tests it for zero at
	// `1038e5a0`. So an unnamed hint matches an empty template and a named one does not. Retail's
	// own arm, reproduced: none of the five templates can produce an empty string, so it is
	// unreachable from `ManBatHintName` and reachable only from a hand-built name in a test.
	if (Name.IsEmpty())
	{
		return Hint.Name.IsEmpty();
	}

	// `1038e560 MOV AL,[ESP + ECX*1 + 0x7] / CMP AL,0x2a` — the LAST character of the template. A
	// `*` takes `__strnicmp` over `strlen - 1` characters (`1038e590 DEC ECX`), which excludes the
	// `*` itself; anything else takes `__strcmpi` over the whole string. A null hint name is
	// replaced by the shared empty literal `DAT_106b8540` on both arms.
	if (Name[Name.Len() - 1] == TEXT('*'))
	{
		const FString Prefix = Name.Left(Name.Len() - 1);
		return Hint.Name.Left(Prefix.Len()).Equals(Prefix, ESearchCase::IgnoreCase);
	}
	return Hint.Name.Equals(Name, ESearchCase::IgnoreCase);
}

// =================================================================================================
// `CNPC_VWerewolf::GetHintEndEntity` (`0x103d6390`) and the endpoint accessor `0x103d6650`.
// =================================================================================================

int32 FElysiumNpc::GetHintEndEntity(const FHintWords& Hint) const
{
	// `103d63xx`: a linear scan of the `+0x6714` record array, count `+0x6720`, stride `0x48`
	// (0x12 ints). The match is on the record's word at `+0x04` — the HINT — and the answer is its
	// word at `+0x00`, a cached `EHANDLE` to the end entity.
	//
	// Retail validates the handle TWICE: the loop's own guard resolves it and requires a non-null
	// entity, then the hit arm re-reads `base[i * 0x12]` — the SAME word — and returns null if that
	// second resolve fails. The checklist walk called the second read "a second cached handle at the
	// same slot"; the decompiled C shows one word read twice. CORRECTED here and in the prose.
	for (const FWerewolfHintGroundpoint& Row : WerewolfHintGroundpoints)
	{
		if (Row.HintNode == INDEX_NONE || Row.HintNode != Hint.HintIndex)
		{
			continue;
		}
		FHintWords Cached;
		if (!HintWords(Row.CachedEndEntity, Cached))
		{
			// The loop's guard: an unresolvable cached handle does NOT match, so the scan keeps
			// walking rather than answering. `103d63d5` falls through to `iVar5 = iVar5 + 1`.
			continue;
		}
		// The second, redundant resolve. Same word, same answer.
		return HintWords(Row.CachedEndEntity, Cached) ? Row.CachedEndEntity : INDEX_NONE;
	}
	// `103d6410` — the miss and the empty-array case both fall through to `FindHintEndEntity`
	// (`0x103d6520`), which family Hints already ported. Called, not restated.
	return FindHintEndEntity(Hint);
}

FVector FElysiumNpc::GetHintEndpoint(const FHintWords* Hint) const
{
	// `103d66xx`. A null hint answers `DAT_1070d1b0/b4/b8`, which `staticinit_101370b0` zeroes —
	// `vec3_origin`, and NOT a "fixed global point" as the checklist walk reads. Recovered by
	// reading the initialiser; recorded in the prose.
	if (Hint == nullptr)
	{
		return GHints10Vec3Origin;
	}
	const int32 End = GetHintEndEntity(*Hint);
	FHintWords EndWords;
	if (!HintWords(End, EndWords))
	{
		// CRASH GUARD, named: retail dereferences the end entity's vtable `+0x364` with NO null
		// check, so a hint whose end entity does not resolve faults. This runtime cannot fault, and
		// there is no origin to answer, so it answers `vec3_origin` — the SAME value retail's own
		// null-hint arm answers, which is the one value this body is already known to produce.
		return GHints10Vec3Origin;
	}
	// `GetAbsOrigin` (vtable `+0x364`) copied into the caller's `Vector`.
	return EndWords.OriginCm;
}

// =================================================================================================
// `CNPC_VWerewolf::GetForwardHintForHint` (`0x103d7090`).
// =================================================================================================

bool FElysiumNpc::IsForwardHintExemptType(int32 HintType)
{
	// The switch at `103d70dd`, case for case. FOURTEEN literals, and `0x3aa2` is NOT among them —
	// the checklist walk's "0x3a9f-0x3aaa" would be fifteen. Corrected from the decompiled C.
	switch (HintType)
	{
	case 15000:   // 0x3a98
	case 0x3a99:
	case 0x3a9c:
	case 0x3a9f:
	case 0x3aa0:
	case 0x3aa1:
	case 0x3aa3:
	case 0x3aa4:
	case 0x3aa5:
	case 0x3aa6:
	case 0x3aa7:
	case 0x3aa8:
	case 0x3aa9:
	case 0x3aaa:
		return true;
	default:
		return false;
	}
}

TArray<int32> FElysiumNpc::GlobalHintList() const
{
	// `DAT_10925450` walked by its `+0x5d8` next link: the world's hint list, head first.
	return World != nullptr ? World->HintList() : TArray<int32>();
}

int32 FElysiumNpc::GetForwardHintForHint(const FHintWords& Hint) const
{
	// `103d70dd` — the exemption list hands the input hint straight back.
	if (IsForwardHintExemptType(Hint.HintType))
	{
		return Hint.HintIndex;   // the input hint itself
	}

	// `103d7150`: resolve the input's end entity ONCE, then walk the global hint list from its head
	// looking for a hint of type `0x3a9c` whose own end entity is the SAME object.
	const int32 TargetEnd = GetHintEndEntity(Hint);
	for (const int32 Candidate : GlobalHintList())
	{
		FHintWords CandidateWords;
		if (!HintWords(Candidate, CandidateWords))
		{
			continue;
		}
		if (CandidateWords.HintType != 0x3a9c)
		{
			continue;
		}
		if (GetHintEndEntity(CandidateWords) == TargetEnd)
		{
			return Candidate;
		}
	}

	// `103d7176` — the no-match arm warns with the INPUT hint's debug name and returns the loop
	// cursor, which is null because the loop ran to its end. The format at `0x10662f98` reads
	// `"Could Not find forward hint for hint: %s (%s) \n"` in the pinned image — TWO `%s`, which
	// the checklist walk records as one; `CBaseEntity::GetDebugName` supplies both halves of retail's
	// name-and-class pair.
	// Verbose, not Warning: retail's `DevWarning` is developer-gated, this arm is the ONLY one
	// reachable while the hint list is a seam, and family Senses10 logs `0x103d698c`'s twin the same
	// way.
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("Could Not find forward hint for hint: %s (%s) "),
		*Hint.Name, *Hint.Group);
	return INDEX_NONE;
}

// =================================================================================================
// `CNPC_VWerewolf::IsValidTeleportHint` (`0x103d8300`).
// =================================================================================================

bool FElysiumNpc::IsTeleportHintExcludedType(int32 HintType)
{
	// `103d83c8`..`103d8430` — six separate `CMP`s, each its own refusal.
	return HintType >= 0x3aa3 && HintType <= 0x3aa8;
}

bool FElysiumNpc::HintEndEntityScriptHidden(int32 EndEntityNode) const
{
	// SEAM for `thunk_FUN_100b5190(endEntity)` — `*(byte*)(entity + 0xf4)`, `m_bScriptHidden`. There
	// is no entity behind a hint node on this substrate, so this answers false: retail NEGATES it,
	// so false is the ADMITTING value and nothing is silently refused.
	(void)EndEntityNode;
	return false;
}

bool FElysiumNpc::IsValidTeleportHint(const FHintWords* Hint, double Now) const
{
	// `103d83xx`, seven ordered refusals then one negated test. Every gate below falls out to false
	// on the first that fails, in retail's order.

	// 1. `if (param_1 == 0)`.
	if (Hint == nullptr)
	{
		return false;
	}
	// 2. `(field_0x66e8 & 4) == 4` — family Hints' `WerewolfHintFlags`, the same bit
	//    `IsImperativeTeleportHint` reads as `bBit0x4`.
	if ((WerewolfHintFlags & 0x4u) == 0x4u)
	{
		return false;
	}
	// 3. `thunk_FUN_102d14c0(hint)` — family Hints' `IsHintUnusable`, the three-arm rule over the
	//    hint's own words. Called, not restated. `bOwnerAlive` is the `EHANDLE` validity test retail
	//    runs on `m_hHintOwner`; with no hint store the handle never resolves.
	if (IsHintUnusable(*Hint, Now, /*bOwnerAlive=*/false))
	{
		return false;
	}
	// 4. `(**(code **)(*this + 0x8d8))(hint)` — slot 566, VIRTUAL, so a Werewolf takes
	//    `CNPC_VWerewolf`'s own species row (15000..15018 except 15007) through the dispatcher above.
	if (!const_cast<FElysiumNpc*>(this)->FValidateHintType(const_cast<FHintWords*>(Hint)))
	{
		return false;
	}
	// 5. the excluded type set.
	if (IsTeleportHintExcludedType(Hint->HintType))
	{
		return false;
	}
	// 6. `(hint->+0x470 == 1) && (field_0x66e8 & 0x40) == 0x40`. Note that `+0x470` is compared for
	//    EQUALITY with 1 here, not masked — the same word slot 566 treats as a bit set. Retail's
	//    asymmetry, reproduced.
	if (Hint->GroupMask == 1 && (WerewolfHintFlags & 0x40u) == 0x40u)
	{
		return false;
	}
	// 7. the endpoint's own `m_bScriptHidden`, NEGATED: a hint is valid only when its far end is not
	//    script-hidden.
	return !HintEndEntityScriptHidden(GetHintEndEntity(*Hint));
}
