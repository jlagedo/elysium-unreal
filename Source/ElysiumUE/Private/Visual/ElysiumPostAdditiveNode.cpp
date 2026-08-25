#include "ElysiumPostAdditiveNode.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimStats.h"
#include "Animation/AnimTrace.h"

void FAnimNode_ElysiumPostAdditive::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	FAnimNode_Base::Initialize_AnyThread(Context);

	Base.Initialize(Context);
	Additive.Initialize(Context);
}

void FAnimNode_ElysiumPostAdditive::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
	Base.CacheBones(Context);
	Additive.CacheBones(Context);
}

void FAnimNode_ElysiumPostAdditive::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread)
	Base.Update(Context);

	ActualAlpha = 0.0f;
	if (IsLODEnabled(Context.AnimInstanceProxy))
	{
		// The pin is read here and nowhere else: an exposed input is only current after the
		// graph has executed it for this frame.
		GetEvaluateGraphExposedInputs().Execute(Context);
		// Retail clamps on entry (`0x100c13a4` / `0x100c13c8`) before it scales anything.
		ActualAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

		if (FAnimWeight::IsRelevant(ActualAlpha))
		{
			Additive.Update(Context.FractionalWeight(ActualAlpha));
		}
	}

	TRACE_ANIM_NODE_VALUE(Context, TEXT("Alpha"), ActualAlpha);
}

void FAnimNode_ElysiumPostAdditive::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)

	Base.Evaluate(Output);
	if (!FAnimWeight::IsRelevant(ActualAlpha))
	{
		// Retail skips a bone outright once its scaled weight reaches zero (`0x100c150e`); with
		// no per-bone mask on this node that is the whole layer, and the base passes through.
		return;
	}

	// **Not an additive pose context.** A `_delta` ships as an ordinary sequence carrying the raw
	// decoded delta, so `GetAnimationPose` hands back exactly those tracks; declaring the context
	// additive would only reset absent bones to the additive identity, which the bake already
	// wrote as real keys.
	FPoseContext AdditiveEval(Output, /*bExpectsAdditivePose=*/false);
	Additive.Evaluate(AdditiveEval);

	const float S = ActualAlpha;
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (const FCompactPoseBoneIndex BoneIndex : Output.Pose.ForEachBoneIndex())
	{
		const FTransform& Delta = AdditiveEval.Pose[BoneIndex];
		// **A bone the delta carries no track for is skipped, and the signal is the engine's
		// own.** An ordinary sequence seeds every bone with the compact reference pose and
		// overwrites only the ones it carries, so a bone still holding that pose bit-for-bit is
		// one the clip never addressed -- the same criterion the composition instruments use. The
		// bake writes an explicit identity for every bone of the skeleton it bakes against, but a
		// body plays a shared bank with bones the bank never declares (a face, a hair chain, a prop
		// helper), and post-multiplying a reference pose onto them turns each by its own bind every
		// frame. Retail reaches the same skip from its per-bone weight (`0x100c150e`). A tracked
		// bone whose delta happens to equal its reference pose can only be one whose reference
		// pose is the identity, where skipping and composing are the same answer.
		if (Delta.Equals(Bones.GetRefPoseTransform(BoneIndex), UE_SMALL_NUMBER))
		{
			continue;
		}
		// `QuaternionScale` (`0x1013add0`) is a shortest-arc slerp from identity, exact at both
		// ends; the full-weight case is lifted out because that is every autolayer.
		const FQuat Scaled = S >= 1.0f
			? Delta.GetRotation()
			: FQuat::Slerp(FQuat::Identity, Delta.GetRotation(), S);

		FTransform& Bone = Output.Pose[BoneIndex];
		// The rule: `q ⊗ scale(D, s)`, normalized -- `QuaternionMA` at `vampire.dll 0x100c12b0`.
		Bone.SetRotation((Bone.GetRotation() * Scaled).GetNormalized());
		// Position accumulates identically on both sides of retail's `0x10` selector
		// (`0x100c1564`), and scale is untouched on both.
		Bone.AddToTranslation(Delta.GetTranslation() * S);
	}

	Output.Curve.Accumulate(AdditiveEval.Curve, S);
}

void FAnimNode_ElysiumPostAdditive::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("(Alpha: %.1f%%)"), ActualAlpha * 100.0f);

	DebugData.AddDebugItem(DebugLine);
	Base.GatherDebugData(DebugData.BranchFlow(1.0f));
	Additive.GatherDebugData(DebugData.BranchFlow(ActualAlpha));
}
