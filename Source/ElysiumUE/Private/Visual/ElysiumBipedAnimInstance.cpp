#include "Visual/ElysiumBipedAnimInstance.h"

#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumAnimSubsystem.h"   // FElysiumResolvedAnimation

#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimStateMachineTypes.h"
#include "Animation/BlendProfile.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// The one machine the authored graph carries. The asset test asserts this name, so a graph whose
	// machine was renamed fails in a tier rather than silently reporting no completion.
	const FName GLocomotionMachine(TEXT("Locomotion"));
}

// ================================================================================================
// FElysiumBipedAnimProxy
// ================================================================================================

void FElysiumBipedAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);

	// Every player this proxy owns itself loops by default and is driven explicitly; no sync group,
	// because two clips of different lengths must not be forced onto a shared normalized time — and a
	// layer must never be phase-matched to the pose it rides over, which is what an aim layer and a
	// walk cycle have nothing to say to each other about.
	FAnimationInitializeContext Context(this);
	ClipPlayer.SetLoopAnimation(true);
	ClipPlayer.SetPlayRate(1.f);
	ClipPlayer.SetGroupName(NAME_None);
	ClipPlayer.SetGroupMethod(EAnimSyncMethod::DoNotSync);
	ClipPlayer.Initialize_AnyThread(Context);
	for (FAnimNode_SequencePlayer_Standalone& Player : LayerPlayers)
	{
		Player.SetLoopAnimation(true);
		Player.SetPlayRate(1.f);
		Player.SetGroupName(NAME_None);
		Player.SetGroupMethod(EAnimSyncMethod::DoNotSync);
		Player.Initialize_AnyThread(Context);
	}
	Playing = nullptr;
	bPlayingLoop = true;
	bClipNeedsReinit = false;
	for (int32 Layer = 0; Layer < MaxLayers; ++Layer)
	{
		LayerWeights[Layer] = 0.f;
		bLayerNeedsReinit[Layer] = false;
		bLayerAdditive[Layer] = false;
		LayerMasks[Layer].Reset();
	}
	// The facial track is not reset here: the rig is installed once per body, before or after this
	// runs depending on when the component registers, and re-initializing the pose graph does not
	// change which face the body wears.
}

void FElysiumBipedAnimProxy::CacheBones()
{
	// The compiled graph, the composition stages and the garment first, through the shared base.
	FElysiumBodyAnimProxy::CacheBones();

	FAnimationCacheBonesContext Context(this);
	ClipPlayer.CacheBones_AnyThread(Context);
	for (FAnimNode_SequencePlayer_Standalone& Player : LayerPlayers)
	{
		Player.CacheBones_AnyThread(Context);
	}
}

bool FElysiumBipedAnimProxy::Evaluate(FPoseContext& Output)
{
	// A standing cinematic clip IS the body pose, replacing the graph rather than blending with it —
	// a scene owns the body outright for its duration. The graph still advanced this frame, so
	// whatever it holds resumes the moment the clip is stopped.
	if (Playing != nullptr && ClipPlayer.GetSequence() != nullptr)
	{
		ClipPlayer.Evaluate_AnyThread(Output);
	}
	else
	{
		// The compiled graph. `FAnimInstanceProxy::Evaluate` returns false to mean "not handled, run
		// the graph", so calling it here would evaluate nothing and pose the reference pose.
		EvaluateAnimationNode(Output);
	}
	// VtMB's autolayers, under the composition stages, which is retail's own order.
	EvaluateLayers(Output);
	// Then the composition stages and the face, over whatever produced the pose — the same tail every
	// body wears, which is what stops a body shipping frozen eyes and untwisted forearms.
	EvaluateTail(Output);
	return true;
}

void FElysiumBipedAnimProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	// Ahead of everything else: a layer rides over whatever the body produced, INCLUDING the
	// reference pose a body with no clip falls back to. A weapon overlay on a body that has not been
	// given a stance yet must still run rather than hold frame zero until one arrives.
	for (int32 Layer = 0; Layer < MaxLayers; ++Layer)
	{
		if (bLayerNeedsReinit[Layer])
		{
			bLayerNeedsReinit[Layer] = false;
			FAnimationInitializeContext InitContext(this);
			LayerPlayers[Layer].Initialize_AnyThread(InitContext);
			FAnimationCacheBonesContext BoneContext(this);
			LayerPlayers[Layer].CacheBones_AnyThread(BoneContext);
		}
		if (LayerPlayers[Layer].GetSequence() != nullptr && LayerWeights[Layer] > 0.f)
		{
			LayerPlayers[Layer].Update_AnyThread(InContext.FractionalWeight(LayerWeights[Layer]));
		}
	}

	if (bClipNeedsReinit)
	{
		bClipNeedsReinit = false;
		FAnimationInitializeContext InitContext(this);
		ClipPlayer.Initialize_AnyThread(InitContext);
		FAnimationCacheBonesContext BoneContext(this);
		ClipPlayer.CacheBones_AnyThread(BoneContext);
	}
	if (Playing != nullptr && ClipPlayer.GetSequence() != nullptr)
	{
		ClipPlayer.Update_AnyThread(InContext);
	}

	// The graph advances whether or not its pose is consumed. A scene that ends hands the body back
	// to a machine that has kept up with the world rather than to one frozen where the scene began.
	FAnimInstanceProxy::UpdateAnimationNode(InContext);
}

// ================================================================================================
// The cinematic clip path
// ================================================================================================

void FElysiumBipedAnimProxy::PlayDirect(UAnimSequence* Sequence, bool bLoop)
{
	if (Sequence == nullptr)
	{
		return;
	}
	if (Sequence == Playing && bLoop && bPlayingLoop)
	{
		// A repeated looping request must not visibly reset the clip. A body a prior Seek pinned at
		// rate 0 must not stay frozen forever because of it, though: SetPlayRate writes the node's
		// own member, read on every UpdateAssetPlayer, so this un-freezes without the restart the
		// early-out exists to prevent — no reinit, and the play position is preserved.
		ClipPlayer.SetPlayRate(1.f);
		return;
	}
	ClipPlayer.SetSequence(Sequence);
	ClipPlayer.SetLoopAnimation(bLoop);
	ClipPlayer.SetPlayRate(1.f);
	ClipPlayer.SetStartPosition(0.f);
	bClipNeedsReinit = true;   // reset the play time on the worker
	Playing = Sequence;
	bPlayingLoop = bLoop;
}

void FElysiumBipedAnimProxy::Seek(float PositionSeconds)
{
	if (Playing == nullptr)
	{
		return;
	}
	const float Length = Playing->GetPlayLength();
	const float Position = bPlayingLoop && Length > SMALL_NUMBER
		? FMath::Fmod(FMath::Max(0.f, PositionSeconds), Length)
		: FMath::Clamp(PositionSeconds, 0.f, Length);
	ClipPlayer.SetStartPosition(Position);
	ClipPlayer.SetPlayRate(0.f);
	bClipNeedsReinit = true;
}

float FElysiumBipedAnimProxy::GetClipPosition() const
{
	if (Playing == nullptr)
	{
		return -1.f;
	}
	return ClipPlayer.GetAccumulatedTime();
}

void FElysiumBipedAnimProxy::ResyncPosition(float PositionSeconds)
{
	if (Playing == nullptr)
	{
		return;
	}
	// A pending restart wins. The worker consumes bClipNeedsReinit in UpdateAnimationNode and
	// Initialize_AnyThread then sets the accumulator to the node's start position, so a resync
	// written in the same frame as a PlayDirect would be silently discarded. Bailing makes that
	// deterministic instead of dependent on which ran first; the next resync corrects it.
	if (bClipNeedsReinit)
	{
		return;
	}
	const float Length = Playing->GetPlayLength();
	const float Position = bPlayingLoop && Length > SMALL_NUMBER
		? FMath::Fmod(FMath::Max(0.f, PositionSeconds), Length)
		: FMath::Clamp(PositionSeconds, 0.f, Length);
	// SetAccumulatedTime writes the node's play time and nothing else — no play rate, no start
	// position, no reinit. That is the whole reason this is not Seek: the clip keeps running from
	// its corrected phase rather than freezing at it.
	//
	// This is a game-thread write to a field the worker advances. It is safe only because every
	// caller arrives through UElysiumBipedAnimInstance's GetProxyOnGameThread, which blocks on any
	// in-flight parallel evaluation. A future path reaching the proxy without that accessor turns
	// this into a silent data race on a float.
	ClipPlayer.SetAccumulatedTime(Position);
}

void FElysiumBipedAnimProxy::StopDirect()
{
	Playing = nullptr;
	ClipPlayer.SetSequence(nullptr);
}

// ================================================================================================
// The autolayer accumulator
// ================================================================================================

bool FElysiumBipedAnimProxy::RequestLayer(UAnimSequence* Sequence, bool bLoop, float Weight)
{
	if (Sequence == nullptr)
	{
		return false;
	}

	// The gate that keeps this honest, and it is two gates because the two combines fail
	// differently.
	//
	// An ADDITIVE is read as a delta, and only a sequence Unreal considers additive evaluates to
	// one: it starts from the additive identity and lets the compressed delta overwrite the bones
	// the clip carries tracks for, so a bone the layer never touches comes back as identity. An
	// ordinary sequence starts from the REFERENCE pose instead, and accumulating that would
	// post-multiply every untouched bone by its own bind rotation — a folded skeleton, from data
	// that looks fine.
	//
	// An ORDINARY layer is read as a pose and needs the opposite thing: the mask that says which
	// bones it owns. Without it the blend would pull every bone it does not own toward the shared
	// skeleton's reference pose at full weight, which erases the body's stance from the waist down
	// and looks like a broken clip rather than a missing gate. All 209 shipped `*_layer` sequences
	// are masked, so refusing an unmasked one costs no content.
	const bool bAdditive = Sequence->IsValidAdditive();
	const UElysiumAnimLayerMask* Mask = bAdditive
		? nullptr : Sequence->FindMetaDataByClass<UElysiumAnimLayerMask>();
	if (!bAdditive && Mask == nullptr)
	{
		return false;
	}

	const float Clamped = FMath::Clamp(Weight, 0.f, 1.f);
	// Already running: re-weight and leave the phase alone. A caller ramping a layer in writes the
	// weight every frame, and restarting the clip under it would freeze it on frame zero.
	for (int32 Layer = 0; Layer < MaxLayers; ++Layer)
	{
		if (LayerPlayers[Layer].GetSequence() == Sequence)
		{
			LayerWeights[Layer] = Clamped;
			LayerPlayers[Layer].SetLoopAnimation(bLoop);
			return true;
		}
	}

	// A free slot, else the weakest — the least of what is playing is the least to lose.
	int32 Chosen = INDEX_NONE;
	for (int32 Layer = 0; Layer < MaxLayers; ++Layer)
	{
		if (LayerPlayers[Layer].GetSequence() == nullptr || LayerWeights[Layer] <= 0.f)
		{
			Chosen = Layer;
			break;
		}
		if (Chosen == INDEX_NONE || LayerWeights[Layer] < LayerWeights[Chosen])
		{
			Chosen = Layer;
		}
	}

	LayerPlayers[Chosen].SetSequence(Sequence);
	LayerPlayers[Chosen].SetLoopAnimation(bLoop);
	LayerPlayers[Chosen].SetPlayRate(1.f);
	LayerPlayers[Chosen].SetStartPosition(0.f);
	LayerWeights[Chosen] = Clamped;
	bLayerAdditive[Chosen] = bAdditive;
	ResolveLayerMask(Chosen, Sequence, Mask);
	bLayerNeedsReinit[Chosen] = true;   // reset that player's play time on the worker
	return true;
}

void FElysiumBipedAnimProxy::ResolveLayerMask(int32 Layer, const UAnimSequence* Sequence,
	const UElysiumAnimLayerMask* Mask)
{
	TArray<float>& Weights = LayerMasks[Layer];
	Weights.Reset();
	USkeleton* LayerSkeleton = Sequence != nullptr ? Sequence->GetSkeleton() : nullptr;
	UBlendProfile* Profile = Mask != nullptr && LayerSkeleton != nullptr
		? LayerSkeleton->GetBlendProfile(Mask->Profile) : nullptr;
	if (Profile == nullptr)
	{
		return;
	}
	// The profile LIVES on the layer's skeleton but this array is READ in the body's index space:
	// EvaluateLayers looks it up at FBoneContainer::GetSkeletonPoseIndexFromCompactPoseIndex, which
	// indexes the skeleton the container was made for -- the mesh's. Those were the same skeleton
	// until banks moved onto their own, and a bank owns every masked overlay, so building the array
	// against the sequence's skeleton now gates a shifted set of bones: an aim layer loses the
	// upper-body bones that fell outside and gains whatever leg bones fell inside, at full weight.
	// Nothing logs it, because a lookup that misses reads as 0.f, which is also how "not owned"
	// reads. Resolving the profile's own bone NAMES against the target skeleton is the whole fix --
	// it is the same name-keyed rule FSkeletonRemapping applies, without needing the table.
	const USkeleton* TargetSkeleton = GetSkeleton();
	const FReferenceSkeleton& Ref = TargetSkeleton != nullptr
		? TargetSkeleton->GetReferenceSkeleton() : LayerSkeleton->GetReferenceSkeleton();
	// Zero is the default a blend mask reads outside its own entries, and it is the answer that
	// keeps the base pose — so the array is built from the profile's entries alone and every bone
	// the mask does not name stays where the body put it. A bone the target skeleton does not carry
	// resolves to INDEX_NONE and is simply absent, which is the same answer the remapping gives.
	Weights.AddZeroed(Ref.GetNum());
	for (int32 Entry = 0; Entry < Profile->GetNumBlendEntries(); ++Entry)
	{
		const FBlendProfileBoneEntry& Bone = Profile->GetEntry(Entry);
		const int32 Index = Ref.FindBoneIndex(Bone.BoneReference.BoneName);
		if (Weights.IsValidIndex(Index))
		{
			Weights[Index] = Bone.BlendScale;
		}
	}
}

void FElysiumBipedAnimProxy::StopLayer(const UAnimSequence* Sequence)
{
	for (int32 Layer = 0; Layer < MaxLayers; ++Layer)
	{
		if (LayerPlayers[Layer].GetSequence() == Sequence)
		{
			// The weight is the whole gate — Update and Evaluate both skip a zero-weight layer, so
			// the sequence is left in place and re-asking for it costs no reinitialization.
			LayerWeights[Layer] = 0.f;
		}
	}
}

void FElysiumBipedAnimProxy::StopAllLayers()
{
	for (float& Weight : LayerWeights)
	{
		Weight = 0.f;
	}
}

int32 FElysiumBipedAnimProxy::NumLayers() const
{
	int32 Count = 0;
	for (int32 Layer = 0; Layer < MaxLayers; ++Layer)
	{
		if (LayerPlayers[Layer].GetSequence() != nullptr && LayerWeights[Layer] > 0.f)
		{
			++Count;
		}
	}
	return Count;
}

// One-shot: dump the pose the NEXT layer evaluation reads, per bone, largest first.
//
// The question this exists to answer is the one no screenshot can: whether what the applier
// composes is what VtMB authored. The container's own numbers are readable offline, so a
// disagreement localises the fault immediately -- matching numbers mean the bake and the read are
// sound and the combine is wrong, differing numbers mean the opposite.
//
// A counter rather than a cvar read, because the consumer runs on the animation worker and a cvar
// write from there is not safe. The command arms it on the game thread; the worker takes it.
static FThreadSafeCounter GLayerDumpRequest;

static FAutoConsoleCommand GLayerDumpCommand(
	TEXT("elysium.LayerDump"),
	TEXT("Log the pose the next autolayer evaluation reads, per bone, largest first."),
	FConsoleCommandDelegate::CreateLambda([] { GLayerDumpRequest.Set(1); }));

void FElysiumBipedAnimProxy::DumpLayerPose(int32 Layer, const FPoseContext& Pose)
{
	// Consumed here rather than in the caller, so exactly one layer of one body answers a request
	// even when several are composing.
	if (GLayerDumpRequest.Set(0) == 0)
	{
		return;
	}
	const FBoneContainer& Container = Pose.Pose.GetBoneContainer();
	const FReferenceSkeleton& Ref = Container.GetReferenceSkeleton();

	struct FRow { double Degrees; double Centimetres; float Mask; FName Bone; };
	TArray<FRow> Rows;
	Rows.Reserve(Pose.Pose.GetNumBones());
	for (const FCompactPoseBoneIndex BoneIndex : Pose.Pose.ForEachBoneIndex())
	{
		const FTransform& Add = Pose.Pose[BoneIndex];
		const FMeshPoseBoneIndex MeshIndex = Container.MakeMeshPoseIndex(BoneIndex);
		const FSkeletonPoseBoneIndex Skeletal =
			Container.GetSkeletonPoseIndexFromCompactPoseIndex(BoneIndex);
		Rows.Add({
			FMath::RadiansToDegrees(Add.GetRotation().GetAngle()),
			Add.GetTranslation().Size(),
			LayerMasks[Layer].IsValidIndex(Skeletal.GetInt()) ? LayerMasks[Layer][Skeletal.GetInt()] : 1.f,
			Ref.IsValidIndex(MeshIndex.GetInt()) ? Ref.GetBoneName(MeshIndex.GetInt()) : NAME_None });
	}
	Rows.Sort([](const FRow& A, const FRow& B) { return A.Degrees > B.Degrees; });

	const UAnimSequence* Sequence = Cast<UAnimSequence>(LayerPlayers[Layer].GetSequence());
	int32 Quiet = 0;
	for (const FRow& Row : Rows)
	{
		Quiet += Row.Degrees < 1.0 ? 1 : 0;
	}
	// An empty mask owns the whole rig, which is every additive and any unmasked layer.
	int32 Owned = Rows.Num();
	if (!LayerMasks[Layer].IsEmpty())
	{
		Owned = 0;
		for (const float BoneWeight : LayerMasks[Layer])
		{
			Owned += BoneWeight > 0.f ? 1 : 0;
		}
	}
	UE_LOG(LogTemp, Display,
		TEXT("autolayer %d: '%s' at t=%.3f, %d bone(s), additive=%d, mask owns %d"),
		Layer, Sequence != nullptr ? *Sequence->GetName() : TEXT("none"),
		LayerPlayers[Layer].GetAccumulatedTime(), Rows.Num(),
		Sequence != nullptr ? static_cast<int32>(Sequence->AdditiveAnimType) : -1, Owned);
	for (int32 i = 0; i < FMath::Min(12, Rows.Num()); ++i)
	{
		UE_LOG(LogTemp, Display, TEXT("   %-24s %7.2f deg   %6.2f cm   mask %.2f"),
			*Rows[i].Bone.ToString(), Rows[i].Degrees, Rows[i].Centimetres, Rows[i].Mask);
	}
	UE_LOG(LogTemp, Display, TEXT("   ... %d of %d bones under 1 deg"), Quiet, Rows.Num());
}

// The autolayer accumulator composes VtMB's own combines. There is deliberately no "use Unreal's
// additive node" setting: every Unreal additive mode pre-multiplies, and the cost of that order is
// measured rather than offered — 89.1% of bone-frames within 0.5 degrees, 1.9% past 10, worst 162.9
// on a thigh (`docs/vtmb/animation_and_movers.md`).
void FElysiumBipedAnimProxy::EvaluateLayers(FPoseContext& Output)
{
	// RETAIL'S SLOT, and it is not the obvious one. `FUN_10089c40` walks a sequence's autolayers
	// and accumulates each through `FUN_10088e10` while the pose is still LOCAL, before
	// `BuildTransformations` (`FUN_1008fd00`) composes the hierarchy and applies split inheritance.
	// So a layer lands under the composition stages, not over them, and the stages see the
	// accumulated result. Running this after EvaluateComposition is wrong in a way that still looks
	// plausible on a screenshot.
	//
	// ORDER WITHIN THE WALK is an assumption, and a stated one. Retail walks the host sequence's
	// own autolayer table (`numautolayers`@660) in its declared order; the export does not carry
	// that table, so the slots are composed overlay-first, additive-second. That is the order in
	// which both contributions survive: an `<weapon>_aim_layer` REPLACES the upper body under its
	// mask, so an attack delta accumulated before it would be blended straight back out, while the
	// same delta after it rides on top. The shipped pattern is exactly one of each per host
	// sequence (`docs/vtmb/animation_and_movers.md` A.3), so there is no third case being decided.
	const FBoneContainer& Container = Output.Pose.GetBoneContainer();
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bAdditivePass = Pass != 0;
		for (int32 Layer = 0; Layer < MaxLayers; ++Layer)
		{
			const float LayerWeight = LayerWeights[Layer];
			if (LayerPlayers[Layer].GetSequence() == nullptr || LayerWeight <= 0.f
				|| bLayerAdditive[Layer] != bAdditivePass)
			{
				continue;
			}

			FPoseContext Layered(this);
			LayerPlayers[Layer].Evaluate_AnyThread(Layered);

			if (GLayerDumpRequest.GetValue() != 0)
			{
				DumpLayerPose(Layer, Layered);
			}

			// `s = layer_weight * bone_weight` in retail, where bone_weight is the animation
			// record's `weight`@0 — a binary MASK rather than a factor, since both channel decoders
			// test it against zero and return zeros (`docs/vtmb/animation_and_movers.md` A.4).
			//
			// For an ADDITIVE the product reduces to the layer weight, and the reason is not the
			// mask: a zero-weight record never carries a channel offset, the exporter drops a
			// channel-less track, and a missing track on an additive evaluates to the additive
			// identity, so a masked bone already contributes no change. That equivalence belongs to
			// the additive identity, which is why the bake gives an additive no mask asset at all
			// and this array comes back empty for one.
			//
			// For an ORDINARY layer it does not reduce and cannot be inferred: a bone outside the
			// mask must keep the BASE pose, while one inside it with no track holds its BIND, and
			// the file states both as "no track". The mask is the only thing that separates them.
			const TArray<float>& Mask = LayerMasks[Layer];

			for (const FCompactPoseBoneIndex BoneIndex : Output.Pose.ForEachBoneIndex())
			{
				float S = LayerWeight;
				if (!Mask.IsEmpty())
				{
					const FSkeletonPoseBoneIndex Skeletal =
						Container.GetSkeletonPoseIndexFromCompactPoseIndex(BoneIndex);
					// A bone the mask cannot even name is outside it. The alternative — defaulting
					// an unknown bone to owned — would let one mismatched skeleton pull the whole
					// body onto the overlay.
					S *= Mask.IsValidIndex(Skeletal.GetInt()) ? Mask[Skeletal.GetInt()] : 0.f;
				}
				if (S <= 0.f)
				{
					continue;
				}
				const bool bFull = S >= 1.f - KINDA_SMALL_NUMBER;
				const FTransform& Add = Layered.Pose[BoneIndex];
				FTransform& Bone = Output.Pose[BoneIndex];

				if (bAdditivePass)
				{
					// PRE-multiplied, which is Unreal's own order and not a VtMB rule. VtMB puts
					// its delta on the RIGHT (`FUN_10088d60`, selected by `0x14`), so converting
					// between the two is a conjugation by the base's rotation — and the bake
					// performs it, by writing each additive against the base its host declares and
					// letting the compressor's subtraction do the conjugating. What arrives here is
					// already a delta in Unreal's order, so this accumulates it the way
					// `FAnimNode_ApplyAdditive` does; `CCC10`'s node replaces this loop with it.
					//
					// Slerp-from-identity rather than the nlerp `BlendFromIdentityAndAccumulate`
					// uses: it is the same rotation at full weight and a constant-rate path below
					// it, which is what the weight slider reads as linear.
					const FQuat Scaled = bFull
						? Add.GetRotation()
						: FQuat::Slerp(FQuat::Identity, Add.GetRotation(), S);
					Bone.SetRotation((Scaled * Bone.GetRotation()).GetNormalized());
					Bone.AddToTranslation(Add.GetTranslation() * S);
				}
				else
				{
					// COMPLEMENTARY weights, which is the other half of `FUN_10088e10`:
					// `out.quat = nlerp(out.quat, layer.quat, s)` and
					// `out.pos = (1 - s) * out.pos + s * layer.pos`. The overlay REPLACES the bones
					// it owns rather than adding to them, so at full weight the base pose is gone
					// from the upper body and untouched everywhere else.
					//
					// FastLerp is the nlerp, and it aligns the pair first — the same shortest-arc
					// choice Source's own QuaternionBlend makes, and what keeps a 180-degree
					// disagreement between base and overlay from taking the long way round.
					Bone.SetRotation(bFull
						? Add.GetRotation()
						: FQuat::FastLerp(Bone.GetRotation(), Add.GetRotation(), S).GetNormalized());
					Bone.SetTranslation(bFull
						? Add.GetTranslation()
						: FMath::Lerp(Bone.GetTranslation(), Add.GetTranslation(), static_cast<double>(S)));
				}
				// Scale is deliberately untouched on both sides. VtMB animates none — the format
				// carries no scale channel at all — so a layered scale term would only ever apply
				// the identity, and leaving it alone keeps a body whose mesh was built at a
				// non-unit scale intact.
			}
		}
	}
}

// ================================================================================================
// UElysiumBipedAnimInstance
// ================================================================================================

void UElysiumBipedAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	CacheStateMachine();
}

void UElysiumBipedAnimInstance::CacheStateMachine()
{
	MachineIndex = INDEX_NONE;
	for (int32& Index : StateIndex)
	{
		Index = INDEX_NONE;
	}

	const FBakedAnimationStateMachine* Machine = nullptr;
	GetStateMachineIndexAndDescription(GLocomotionMachine, MachineIndex, &Machine);
	if (Machine == nullptr)
	{
		return;
	}
	// By name rather than by declaration order: a state's index is whatever the compiler assigned,
	// and the names are the contract `ElysiumAnimGraph::StateName` and the authored asset share.
	for (int32 State = 0; State < ElysiumAnimGraph::NumGraphStates; ++State)
	{
		const FName Name(ElysiumAnimGraph::StateName(static_cast<EElysiumGraphState>(State)));
		for (int32 i = 0; i < Machine->States.Num(); ++i)
		{
			if (Machine->States[i].StateName == Name)
			{
				StateIndex[State] = i;
				break;
			}
		}
	}
}

void UElysiumBipedAnimInstance::PublishSelection(const FElysiumAnimationSelection& Selection,
	const FElysiumResolvedAnimation& Assets)
{
	Pending = Selection;
	PendingBlendSpace = Assets.Space;
	PendingSequence = Assets.Sequence;
}

void UElysiumBipedAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	// The garment's game-thread pass.
	Super::NativeUpdateAnimation(DeltaSeconds);

	// A component that was built before any graph existed, or whose class was swapped, resolves its
	// machine on the first update rather than staying inert for the body's whole life.
	if (MachineIndex == INDEX_NONE)
	{
		CacheStateMachine();
	}

	// --- a request that resolved no asset holds the pose it had -----------------------------------
	//
	// **This is retail's behaviour, not a guard bolted on.** A failed selection never reaches
	// `ResetSequenceInfo`: `m_nSequence` keeps whatever it held and the body goes on playing it. The
	// controlled corpus records exactly one such request on a validated player body — a ducked
	// phase-8 landing asking for `ACT_LAND_CROUCH`, whose selection returns `-1` and for which no
	// clip was ever observed (`docs/vtmb/animation_and_movers.md`).
	//
	// Projecting it anyway is what produced a visible **reference pose**: the state it routes to
	// takes its clip from the pin below, an unresolved request leaves that pin null, and a sequence
	// player with no asset evaluates to the skeleton's bind pose — a T-pose flash for as long as the
	// landing lasts. Declaring a state was never the problem; entering it with nothing to play was.
	//
	// The **record is untouched** and still names the miss, which is the whole reason a player miss
	// is a named one. Holding also leaves `OneShot` describing the pose that is actually on screen,
	// so the generation gate in the driver's owner reads stale and the latch falls back to its
	// timer — which is what ends the landing.
	bHoldingPose = ElysiumAnimGraph::ShouldHoldPose(bHasApplied, PendingSequence != nullptr,
		PendingBlendSpace != nullptr);
	if (bHoldingPose)
	{
		return;
	}

	// --- project the record onto what the graph reads --------------------------------------------
	RequestedState = ElysiumAnimGraph::StateFor(Pending);
	RequestedBlendSpace = PendingBlendSpace;
	RequestedSequence = PendingSequence;
	// A held stance repeats its into-pose, which is retail's reselect-and-restart expressed as a
	// loop (see `bRequestedLooping`). Every other state takes the model's own bit unchanged, and the
	// record keeps the authored value either way.
	bRequestedLooping = ElysiumAnimGraph::ShouldRepeatClip(RequestedState, Pending.bLooping);
	GridAxis0 = Pending.AxisValue[0];
	GridAxis1 = Pending.AxisValue[1];
	Speed = Pending.Speed;
	MoveYaw = Pending.MoveYaw;
	bHasBlendSpace = RequestedBlendSpace != nullptr;

	// The rules, decided here rather than in a rule graph. `bStateChanged` is measured against what
	// the machine is actually playing rather than against the last request, so a body whose graph
	// was rebuilt or whose state was entered from somewhere else still converges.
	bWantsIdle = RequestedState == EElysiumGraphState::Idle;
	bWantsWalk = RequestedState == EElysiumGraphState::Walk;
	bWantsRun = RequestedState == EElysiumGraphState::Run;
	bWantsSneak = RequestedState == EElysiumGraphState::Sneak;
	bWantsCrouch = RequestedState == EElysiumGraphState::Crouch;
	bWantsLeap = RequestedState == EElysiumGraphState::Leap;
	bWantsFalling = RequestedState == EElysiumGraphState::Falling;
	bWantsLand = RequestedState == EElysiumGraphState::Land;
	bStateChanged = MachineIndex != INDEX_NONE
		&& GetCurrentStateName(MachineIndex) != ElysiumAnimGraph::StateName(RequestedState);

	// --- one blend per discrete request change ---------------------------------------------------
	if (!bHasApplied || Pending.Generation != Applied.Generation)
	{
		// The authored fade, combined as retail combines it. It reaches the graph's inertialization
		// node through the slot node, which forwards whatever this writes into the proxy's slot-group
		// map — the supported native route, and the reason the graph asset carries only a ceiling.
		//
		// A duration of 0 is a legal request rather than a refusal, so `flags & 0x2`'s hard cut falls
		// out of the same call instead of needing a branch of its own.
		const float Blend = ElysiumAnimGraph::TransitionSeconds(
			bHasApplied ? &Applied : nullptr, Pending);
		RequestSlotGroupInertialization(FAnimSlotGroup::DefaultGroupName, Blend);

		Applied = Pending;
		bHasApplied = true;
	}

	// --- read the graph back ---------------------------------------------------------------------
	OneShot = FElysiumOneShotReport();
	OneShot.Generation = Pending.Generation;
	const int32 State = StateIndex[static_cast<uint8>(RequestedState)];
	// **`GetRelevantAnimTimeRemaining` answers `MAX_flt` when it finds no relevant asset player**,
	// not 0 — `FAnimNode_StateMachine::GetRelevantAnimTimeRemaining` returns it from the bottom of
	// the function, and `FAnimInstanceProxy` returns it again for an unknown machine. So the failure
	// direction is "infinitely long", and reporting that as a clip still playing is what hangs a
	// consumer: `Playing` is an answer, and an answer suppresses the latch's own timer. A body whose
	// landing resolved nothing would then stay in ACT_LAND forever rather than standing up.
	//
	// Anything that is not a sane finite duration therefore reports **nothing at all**, which leaves
	// `RemainingSeconds` at its "cannot say" −1 and routes the latch back to the fallback. The state
	// weight and the resolved clip are checked for the same reason and in the same direction.
	if (MachineIndex != INDEX_NONE && State != INDEX_NONE
		&& ElysiumAnimGraph::IsOneShotState(RequestedState)
		&& RequestedSequence != nullptr
		&& GetInstanceStateWeight(MachineIndex, State) > 0.99f)
	{
		const float Remaining = GetRelevantAnimTimeRemaining(MachineIndex, State);
		if (ElysiumAnimGraph::IsPlayableRemaining(Remaining, RequestedSequence->GetPlayLength()))
		{
			OneShot.bInOneShotState = true;
			OneShot.RemainingSeconds = Remaining;
			OneShot.bComplete = Remaining <= KINDA_SMALL_NUMBER;
		}
	}
}

bool UElysiumBipedAnimInstance::HasCompiledGraph() const
{
	// The class itself is the answer, and it is available before the first update: a generated
	// Animation Blueprint class carries the compiled graph, the plain native class carries none.
	// Asking the proxy for a root node would be the same question a frame later and only on the
	// worker's terms.
	return GetClass()->IsChildOf(UAnimBlueprintGeneratedClass::StaticClass());
}

bool UElysiumBipedAnimInstance::PlayOneShot(UAnimSequence* Sequence, bool bLoop, float BlendSeconds)
{
	if (Sequence == nullptr)
	{
		return false;
	}
	// No compiled graph means no slot node to play a montage into, so the clip player answers
	// instead. That is the body whose generated graph package is not on the mount — a named failure
	// rather than a silent one, and it still animates.
	if (!HasCompiledGraph())
	{
		PlayClip(Sequence, bLoop);
		return true;
	}
	// The FIRST clip has nothing to blend from and snaps in regardless, which is retail's own rule:
	// otherwise every body would fade up out of the reference pose on map load, because the slot's
	// source pose is a state machine that has been handed no asset yet.
	const float BlendIn = Montage_IsPlaying(ActiveSlotMontage) ? BlendSeconds : 0.0f;
	// A loop count of 0 is infinite. The blend in and out are the clip's own authored fade — the same
	// number the locomotion transition uses, so one authority serves both consumers.
	ActiveSlotMontage = PlaySlotAnimationAsDynamicMontage(Sequence, FAnimSlotGroup::DefaultSlotName,
		BlendIn, BlendSeconds, /*InPlayRate=*/1.0f, /*LoopCount=*/ bLoop ? 0 : 1);
	return ActiveSlotMontage != nullptr;
}

void UElysiumBipedAnimInstance::StopOneShot(float BlendSeconds)
{
	if (!HasCompiledGraph())
	{
		StopClip();
		return;
	}
	if (ActiveSlotMontage != nullptr)
	{
		Montage_Stop(BlendSeconds, ActiveSlotMontage);
		ActiveSlotMontage = nullptr;
	}
}

// ================================================================================================
// The cinematic clip path and the autolayers, over the proxy
//
// Every one of these goes through GetProxyOnGameThread, which blocks on any in-flight parallel
// evaluation — that block is the whole reason the writes below cannot race the worker.
// ================================================================================================

void UElysiumBipedAnimInstance::PlayClip(UAnimSequence* Sequence, bool bLoop)
{
	if (Sequence == nullptr)
	{
		return;
	}
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().PlayDirect(Sequence, bLoop);
}

void UElysiumBipedAnimInstance::SeekClip(float PositionSeconds)
{
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().Seek(PositionSeconds);
}

void UElysiumBipedAnimInstance::StopClip()
{
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().StopDirect();
}

UAnimSequence* UElysiumBipedAnimInstance::GetPlayingClip() const
{
	return const_cast<UElysiumBipedAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBipedAnimProxy>().GetPlaying();
}

float UElysiumBipedAnimInstance::GetClipPosition() const
{
	return const_cast<UElysiumBipedAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBipedAnimProxy>().GetClipPosition();
}

void UElysiumBipedAnimInstance::ResyncClip(float PositionSeconds)
{
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().ResyncPosition(PositionSeconds);
}

bool UElysiumBipedAnimInstance::PlayLayer(UAnimSequence* Sequence, float Weight, bool bLoop)
{
	// Layers accumulate in LOCAL space underneath the composition stage, which is where retail
	// puts them.
	return GetProxyOnGameThread<FElysiumBipedAnimProxy>().RequestLayer(Sequence, bLoop, Weight);
}

void UElysiumBipedAnimInstance::StopLayer(UAnimSequence* Sequence)
{
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().StopLayer(Sequence);
}

void UElysiumBipedAnimInstance::StopAllLayers()
{
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().StopAllLayers();
}

int32 UElysiumBipedAnimInstance::GetActiveLayers() const
{
	return const_cast<UElysiumBipedAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBipedAnimProxy>().NumLayers();
}
