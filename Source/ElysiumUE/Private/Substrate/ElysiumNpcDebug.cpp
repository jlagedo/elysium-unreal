#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcDebugShared.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **Debug** — the 21 `DevMsg` / debug-ring / text-overlay / `NDebugOverlay`
// bodies of `order.md` layers 0–9. The walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// THE STANDING FACT OF THIS FAMILY: none of these bodies has an output device in this runtime.
// Retail has four — `DevMsg`, the ConVar-gated message ring `0x10119750`, the engine's
// `AddEntityTextOverlay`, and `NDebugOverlay` — and this substrate has none. So every arm is ported
// verbatim, the format strings are retail's own, and the line lands on `FDebugLine`: written to
// `LogElysiumNpcEnt` and, while a capture is open, recorded in emission order. A test reads the
// capture, which is why the ORDER of these arms and the GATES on them are the deliverable and the
// picture is not.
//
// This file: the seam, the two id spaces, the three name tables, and the twelve bodies that are
// text. `ElysiumNpcDebug2.cpp` carries the eight that are geometry.

namespace
{
	// Unit-prefixed because the module builds adaptive-unity and this anonymous namespace is
	// regularly merged with others.

	// Source units out of the port's centimetres. Retail's overlay arguments are all source units,
	// and reproducing a box half-extent of 5 as 12.7 would hide the recovered constant.
	FString GNpcKernelDebugVec(const FVector& Cm)
	{
		const FVector Units = Cm / ElysiumMove::U;
		return FString::Printf(TEXT("(%.1f %.1f %.1f)"), Units.X, Units.Y, Units.Z);
	}

	static_assert(UE_ARRAY_COUNT(NpcKernelDebugShared::GNpcKernelDebugConditionNames) == 0x77,
		"0x102c8ce0 registers exactly 119 conditions, ids 0x00..0x76");

	static_assert(UE_ARRAY_COUNT(NpcKernelDebugShared::GNpcKernelDebugShortConditionNames) == 0x77,
		"0x1027e7f0's switch covers ids 0x00..0x76 and nothing else");

}

// -------------------------------------------------------------------------------------------------
// The output seam.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::BeginDebugCapture()
{
	NpcKernelDebugShared::GNpcKernelDebugCapture.Reset();
	NpcKernelDebugShared::GNpcKernelDebugCapturing = true;
}

TArray<FElysiumNpc::FDebugLine> FElysiumNpc::EndDebugCapture()
{
	NpcKernelDebugShared::GNpcKernelDebugCapturing = false;
	return MoveTemp(NpcKernelDebugShared::GNpcKernelDebugCapture);
}

// -------------------------------------------------------------------------------------------------
// The two id spaces.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// The three global name tables.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// Slots 458 / 449 — `ConditionName` and `TaskName`.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// Slots 407 / 451 / 14 — the three name answers.
// -------------------------------------------------------------------------------------------------

TCHAR* FElysiumNpc::GetSchedulingErrorName()
{
	// slot 451, `0x101aa7b0` — the Troika line's override, six bytes,
	// `return PTR_s_CAI_BaseNPCTroika_105d1060;` which dereferences to `"CAI_BaseNPCTroika"`
	// (`0x105d748c`). Every classname this runtime stands is on the Troika line, so this is the
	// answer for all of them and `BaseSchedulingErrorName` is reachable only from the classes
	// between `CAI_BaseNPC` and `CAI_BaseNPCTroika`, none of which is a spawnable leaf.
	return const_cast<TCHAR*>(TEXT("CAI_BaseNPCTroika"));
}

// -------------------------------------------------------------------------------------------------
// Slot 76 `DrawDebugStatOverlays` — three bodies.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::DrawDebugStatOverlays()
{
	// slot 76, `CAI_BaseNPCTroika::DrawDebugStatOverlays` `0x1029c010`. `CNPC_VBaseBoss`
	// (`0x10366290`, inherited by its subclasses) and `CNPC_VWerewolf` (`0x103d5130`) override this
	// method on their C++ classes (story 5 step 3).
	//
	// `0x1029c010`'s own first arm: `if (m_iDialog == 0) { CAI_BaseNPC::DrawDebugStatOverlays();
	// return; }`. `m_iDialog` (+0x0128) is the dialogue file name, which `DialogName` answers.
	if (DialogName.IsEmpty())
	{
		FElysiumNpcBase::DrawDebugStatOverlays();
		return;
	}
	TroikaDrawDebugStatOverlays();
}

void FElysiumNpc::TroikaDrawDebugStatOverlays()
{
	// `0x1029c010` past its `m_iDialog == 0` arm, recovered from the LISTING: the decompiled C lost
	// every `Msg` argument list to the frame reconstruction and misordered two of them.
	//
	// 1. The expression name, resolved BEFORE anything is printed:
	//      `IsValidExpressionIndex(m_idxDefExpression)` ? `GetExpressionName(m_idxDefExpression)`
	//                                                   : `"None"` (`0x10547418`)
	//    This runtime stores the expression by NAME (`DefExpression`, +0x10b4, family Conditions —
	//    "there is no index"), so a non-empty name IS the valid arm.
	const FString ExpressionName = DefExpression.IsEmpty() ? FString(TEXT("None")) : DefExpression;

	// 2. `Msg("Expression / Gesture information for %s:", GetDebugName())`. Retail's `GetDebugName`
	//    is `m_iName` when set and the classname otherwise.
	const FString Named = TargetName.IsEmpty()
		? (Def != nullptr ? Def->Classname : FString()) : TargetName;
	EmitDebugMsg(TEXT("Expression / Gesture information for %s:"),
		FString::Printf(TEXT("Expression / Gesture information for %s:"), *Named));

	// 3. `Msg("Seq (%.2f): %s / %s ", m_flCycle, seqdesc->pszLabel, seqdesc->pszActivityName)`.
	//    Retail calls `GetSeqDesc(m_nSequence)` TWICE for the two strings and does NOT guard the
	//    result, so a missing descriptor is a null dereference in retail. The port asks the seam
	//    once and prints empty strings where retail would crash — a stated divergence, forced by the
	//    absence of the studio header rather than chosen.
	FString SeqLabel;
	FString SeqActivity;
	SequenceDescriptor(SequenceNumber, SeqLabel, SeqActivity);
	EmitDebugMsg(TEXT("Seq (%.2f): %s / %s "), FString::Printf(TEXT("Seq (%.2f): %s / %s "),
		SequenceCycle, *SeqLabel, *SeqActivity));

	// 4. `Msg("Disposition:  %s,  Expression: %s (%.2f)", DispositionName(m_nCurrDisposition),
	//        expressionName, m_flDefExpressionIntensity)`. The float is `+0x10b8`, family
	//    TroikaHelpers' `ExpressionBlendWeight`; the disposition is `+0x64d4`, the chain's own
	//    `Disposition` name.
	EmitDebugMsg(TEXT("Disposition:  %s,  Expression: %s (%.2f)"),
		FString::Printf(TEXT("Disposition:  %s,  Expression: %s (%.2f)"), *Disposition,
			*ExpressionName, ExpressionBlendWeight));

	// 5. The scene line, three arms in this order:
	//      `m_hDialogScene` (+0x6554) does not resolve      -> `"No Scene Entity"`
	//      it resolves but its `+0x4bc` scene is null       -> `"No Scene"`
	//      otherwise                                        -> `"Scene time: %.2f"`
	//    `+0x4bc` is the `CChoreoScene*` on the scene entity and `0x1007dfa0` is its `GetTime()`.
	const FElysiumEntity* SceneEntity =
		World != nullptr ? World->Resolve(Dialogue.DialogScene) : nullptr;
	if (SceneEntity == nullptr)
	{
		EmitDebugMsg(TEXT("No Scene Entity"), TEXT("No Scene Entity"));
	}
	else
	{
		// SEAM for the scene entity's `+0x4bc CChoreoScene*` and `CChoreoScene::GetTime()`
		// (`0x1007dfa0`). The kernel has no reader for the scene object, so this takes the
		// `"No Scene"` arm — retail's answer for a scene entity that is not playing one.
		EmitDebugMsg(TEXT("No Scene"), TEXT("No Scene"));
	}

	// 6. Every row of `m_Expressions` (+0x10bc, count at +0x10c8, stride 0x20):
	//      level = (row[0x1c] && row[0x18]) ? Evaluate(row[0x1c], SceneTime(row[0x18]))
	//                                       : CalcExpressionIntensity(curtime, row)
	//      data  = CExpressionTable::GetExpressionData(&DAT_10709200, row[0])
	//      if (data) Msg((data[0x18c] & 1) ? "Expression:(O) %s level: %0.2f"
	//                                      : "Expression:(N) %s level: %0.2f", data, level)
	//    The `%s` is the DATA STRUCT's address, so the expression's name is its first field.
	//    SEAM: family Anim already records that the scripted-expression vector has no port member;
	//    the list is empty here, so the loop runs zero times. Nothing is invented to fill it.

	// 7. The four anim layers (`m_AnimOverlay` +0x748, stride 0x30), each printed only when its
	//    weight is strictly greater than `_DAT_104454c4` = 0.0:
	//      `Msg("Anim Layer %d: %s (weight: %f)", i, seqdesc(layer->m_nSequence)->pszLabel, weight)`
	//    SEAM: this runtime stands no overlay-layer array on the kernel surface, so no layer has a
	//    weight and the loop prints nothing. The threshold is recorded because it is the recovered
	//    gate: a layer at EXACTLY zero weight is silent, one at 0.0001 is not.

	// 8. `Msg("Eye targets -  current: %d  default: %d  step: %d", m_RelativeEyeTarget,
	//        DefaultEyeTarget(m_nCurrDisposition), m_nFidgetStep)`. The listing's push order is what
	//    settles which word is which: `+0x5b94` first, the disposition lookup second, `+0x6578`
	//    third.
	EmitDebugMsg(TEXT("Eye targets -  current: %d  default: %d  step: %d"),
		FString::Printf(TEXT("Eye targets -  current: %d  default: %d  step: %d"),
			RelativeEyeTarget, DispositionDefaultEyeTarget(), FidgetStep));

	// 9. `if (m_hEyeLookTarget (+0xe64) resolves)`:
	//      `Msg("Looking at entity %d (%s)", IndexOfEdict(target->edict), target->GetDebugName())`
	//    The index goes through the engine interface `DAT_1070b22c`+0x8c; the port's entity index is
	//    the handle's own slot, which is the same identity.
	const FElysiumEntity* LookTarget =
		World != nullptr ? World->Resolve(EyeLookTargetHandle) : nullptr;
	if (LookTarget != nullptr)
	{
		const FString LookNamed = LookTarget->TargetName.IsEmpty()
			? (LookTarget->Def != nullptr ? LookTarget->Def->Classname : FString())
			: LookTarget->TargetName;
		EmitDebugMsg(TEXT("Looking at entity %d (%s)"),
			FString::Printf(TEXT("Looking at entity %d (%s)"), LookTarget->Handle.Index,
				*LookNamed));
	}

	// 10. `if (m_szDialogQue[0]) Msg("Qued Dialog: %s", m_szDialogQue)` — the FIRST BYTE, so an
	//     empty queued line is silent.
	if (!Dialogue.DialogQue.IsEmpty())
	{
		EmitDebugMsg(TEXT("Qued Dialog: %s"),
			FString::Printf(TEXT("Qued Dialog: %s"), *Dialogue.DialogQue));
	}

	// 11. `Msg("Talk Time Remaining: %.2f", MAX(m_flTalkTime - curtime, 0.0))`. The clamp is an
	//     `FCOMP` against `_DAT_104454c4` = 0.0 with the constant in ST0, so the constant wins only
	//     when it is strictly greater — a remainder of exactly 0.0 prints as 0.0 either way.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const double Remaining = FMath::Max(TalkingUntil - Now, 0.0);
	EmitDebugMsg(TEXT("Talk Time Remaining: %.2f"),
		FString::Printf(TEXT("Talk Time Remaining: %.2f"), Remaining));
}

// -------------------------------------------------------------------------------------------------
// The seams the text bodies read through.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::DispositionDefaultEyeTarget() const
{
	// SEAM for `0x100eccf0(&DAT_10924980, m_nCurrDisposition)` — the disposition table's default
	// eye-target index. `FElysiumDisposition` carries `EyeTurnRate` and the keypad cells, not an
	// index into a global target list, so this answers -1.
	return INDEX_NONE;
}

