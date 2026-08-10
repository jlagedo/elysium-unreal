#include "Visual/ElysiumNpcAnimInstance.h"

#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendProfile.h"
#include "Animation/BlendSpace.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "HAL/IConsoleManager.h"

void FElysiumNpcAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);

	// Both players loop by default and are driven explicitly; no sync group, because two idle
	// clips of different lengths must not be forced onto a shared normalized time.
	FAnimationInitializeContext Context(this);
	for (FAnimNode_SequencePlayer_Standalone& Player : Players)
	{
		Player.SetLoopAnimation(true);
		Player.SetPlayRate(1.f);
		Player.SetGroupName(NAME_None);
		Player.SetGroupMethod(EAnimSyncMethod::DoNotSync);
		Player.Initialize_AnyThread(Context);
	}
	// Same treatment for the autolayers, and for the same reason: a layer must never be forced onto
	// a shared normalized time with the pose it rides over — an aim layer and a walk cycle have
	// nothing to say to each other's phase.
	for (FAnimNode_SequencePlayer_Standalone& Player : LayerPlayers)
	{
		Player.SetLoopAnimation(true);
		Player.SetPlayRate(1.f);
		Player.SetGroupName(NAME_None);
		Player.SetGroupMethod(EAnimSyncMethod::DoNotSync);
		Player.Initialize_AnyThread(Context);
	}
	Current = 0;
	Fading.Reset();
	Playing = nullptr;
	bPlayingLoop = true;
	for (bool& Reinit : bNeedsReinit)
	{
		Reinit = false;
	}
	for (int32 Layer = 0; Layer < MaxLayers; ++Layer)
	{
		LayerWeights[Layer] = 0.f;
		bLayerNeedsReinit[Layer] = false;
		bLayerAdditive[Layer] = false;
		LayerMasks[Layer].Reset();
	}
	bInitialized = false;
	// The facial track is not reset here: the rig is installed once per body, before or after this
	// runs depending on when the component registers, and re-initializing the pose graph does not
	// change which face the body wears.
}

void FElysiumNpcAnimProxy::CacheBones()
{
	// The composition stages and the garment first, through the shared base.
	FElysiumBodyAnimProxy::CacheBones();

	FAnimationCacheBonesContext Context(this);
	for (FAnimNode_SequencePlayer_Standalone& Player : Players)
	{
		Player.CacheBones_AnyThread(Context);
	}
	for (FAnimNode_SequencePlayer_Standalone& Player : LayerPlayers)
	{
		Player.CacheBones_AnyThread(Context);
	}
}

float FElysiumNpcAnimProxy::FadeWeight(const FFadingClip& Fade)
{
	if (Fade.Duration <= 0.f)
	{
		return 0.f;
	}
	const float F = 1.f - Fade.Elapsed / Fade.Duration;
	if (F <= 0.f)
	{
		return 0.f;
	}
	if (F >= 1.f)
	{
		return 1.f;
	}
	return F * F * (3.f - 2.f * F);   // SimpleSpline — retail's 3f^2 - 2f^3, not a linear ramp
}

int32 FElysiumNpcAnimProxy::TakeFreeSlot()
{
	for (int32 Slot = 0; Slot < MaxPlayers; ++Slot)
	{
		if (Slot == Current)
		{
			continue;
		}
		const bool bBusy = Fading.ContainsByPredicate(
			[Slot](const FFadingClip& F) { return F.Slot == Slot; });
		if (!bBusy)
		{
			return Slot;
		}
	}
	// Every slot is live. The oldest fade is the weakest contribution, so it is the one to lose.
	const int32 Reused = Fading.Last().Slot;
	Fading.Pop();
	return Reused;
}

void FElysiumNpcAnimProxy::Request(UAnimSequence* Sequence, bool bLoop, float FadeSeconds)
{
	if (Sequence == nullptr)
	{
		return;
	}

	// A clip takes the body back off a grid. The two are alternatives, not layers — leaving the grid
	// standing would have it keep producing the body pose while the crossfade below advanced a clip
	// nothing reads, which reads as a request that did nothing.
	StopGrid();

	if (Sequence == Playing)
	{
		// A repeated disposition/idle write must not visibly reset a looping stance. One-shots are
		// commands, though: repeating one means replay it, and changing loop policy must take effect.
		if (bLoop && bPlayingLoop)
		{
			// A body a prior Seek pinned at rate 0 must not stay frozen forever just because the
			// same looping clip was asked for again. SetPlayRate writes the node's own member, read
			// on every UpdateAssetPlayer, so this un-freezes without the visible restart the
			// early-out exists to prevent — no bNeedsReinit, and the play position is preserved.
			Players[Current].SetPlayRate(1.f);
			return;
		}
		Players[Current].SetLoopAnimation(bLoop);
		Players[Current].SetPlayRate(1.f);
		Players[Current].SetStartPosition(0.f);
		bNeedsReinit[Current] = true;
		bPlayingLoop = bLoop;
		Fading.Reset();
		return;
	}

	// The first clip has nothing to blend from, so it snaps in regardless — otherwise every NPC
	// would fade up out of the reference pose on map load. A zero duration is retail's `flags & 0x2`
	// hard cut, and it does more than skip this clip's own fade: it FLUSHES every clip still fading,
	// so an attack lands on a clean pose rather than over the tail of whatever it interrupted.
	// Nearly half the shipped vocabulary sets that bit, which is much of why VtMB's combat reads
	// sharp.
	// Retail times a transition by the LARGER of the two clips' own authored fades, so a clip that
	// asks for a long settle gets it whether it is the one arriving or the one leaving. A zero on
	// the incoming clip is the refusal and wins outright — it is not a `max` input.
	const float Duration = FadeSeconds <= 0.f
		? 0.f
		: FMath::Max(CurrentFade, FadeSeconds);
	CurrentFade = FMath::Max(0.f, FadeSeconds);

	const bool bSnap = !bInitialized || Duration <= 0.f;

	if (bSnap)
	{
		Fading.Reset();
	}
	else
	{
		// Stack it. Retail keeps every in-flight record untouched on its own clock — nothing is
		// shortened, dropped early, or refused because a transition is already running. The new clip
		// is simply blended over the partially-faded result.
		Fading.Insert(FFadingClip{ Current, 0.f, Duration }, 0);
	}

	const int32 Next = bInitialized ? TakeFreeSlot() : Current;

	Players[Next].SetSequence(Sequence);
	Players[Next].SetLoopAnimation(bLoop);
	Players[Next].SetPlayRate(1.f);
	Players[Next].SetStartPosition(0.f);
	bNeedsReinit[Next] = true;   // reset that player's play time on the worker

	Current = Next;
	Playing = Sequence;
	bPlayingLoop = bLoop;
	bInitialized = true;
}

void FElysiumNpcAnimProxy::Seek(float PositionSeconds)
{
	if (!bInitialized || Playing == nullptr)
	{
		return;
	}
	const float Length = Playing->GetPlayLength();
	const float Position = bPlayingLoop && Length > SMALL_NUMBER
		? FMath::Fmod(FMath::Max(0.f, PositionSeconds), Length)
		: FMath::Clamp(PositionSeconds, 0.f, Length);
	Players[Current].SetStartPosition(Position);
	Players[Current].SetPlayRate(0.f);
	bNeedsReinit[Current] = true;
	// The transition is deliberately left running. A cinematic scene seeks its clip on the frame it
	// starts it and on every frame after, so forcing the blend to settle here would mean no clip a
	// scene plays could ever blend in at all — the pose would snap at the head of every scene and at
	// every clip boundary inside one. Pinning the phase and fading up are independent: the outgoing
	// player holds whatever pose it was left at while this one rises under it.
}

float FElysiumNpcAnimProxy::GetClipPosition() const
{
	if (!bInitialized || Playing == nullptr)
	{
		return -1.f;
	}
	return Players[Current].GetAccumulatedTime();
}

void FElysiumNpcAnimProxy::ResyncPosition(float PositionSeconds)
{
	if (!bInitialized || Playing == nullptr)
	{
		return;
	}
	// A pending restart wins. The worker consumes bNeedsReinit in UpdateAnimationNode and
	// Initialize_AnyThread then sets the accumulator to the node's start position, so a resync
	// written in the same frame as a Request would be silently discarded. Bailing makes that
	// deterministic instead of dependent on which ran first; the next resync corrects it.
	if (bNeedsReinit[Current])
	{
		return;
	}
	const float Length = Playing->GetPlayLength();
	const float Position = bPlayingLoop && Length > SMALL_NUMBER
		? FMath::Fmod(FMath::Max(0.f, PositionSeconds), Length)
		: FMath::Clamp(PositionSeconds, 0.f, Length);
	// SetAccumulatedTime writes the node's play time and nothing else — no play rate, no start
	// position, no reinit, no blend state. That is the whole reason this is not Seek: a resync
	// landing inside a crossfade leaves the crossfade running, and only the incoming player is
	// re-phased because the outgoing pose is fading out and its phase no longer matters.
	//
	// This is a game-thread write to a field the worker advances. It is safe only because every
	// caller arrives through UElysiumNpcAnimInstance's GetProxyOnGameThread, which blocks on any
	// in-flight parallel evaluation. A future path reaching the proxy without that accessor turns
	// this into a silent data race on a float.
	Players[Current].SetAccumulatedTime(Position);
}

void FElysiumNpcAnimProxy::Stop()
{
	Playing = nullptr;
	bInitialized = false;
	Fading.Reset();
}

bool FElysiumNpcAnimProxy::RequestGrid(UBlendSpace* Space, bool bLoop)
{
	if (Space == nullptr)
	{
		return false;
	}

	// The mirror of RequestLayer's gate, and it fails the same way from the other side. A grid whose
	// cells are partial-body `*_layer` overlays owns only the bones its mask names; evaluated as a
	// base pose there is no mask in the path at all, so every bone it does not own arrives at the
	// shared skeleton's reference pose and the body loses its stance from the waist down. Retail
	// composes those through the layer accumulator and never as a base — a masked sequence reaching
	// the clip path is a defect, not a mode.
	for (const FBlendSample& Sample : Space->GetBlendSamples())
	{
		if (Sample.Animation != nullptr
			&& Sample.Animation->FindMetaDataByClass<UElysiumAnimLayerMask>() != nullptr)
		{
			return false;
		}
	}

	if (GridPlayer.GetBlendSpace() != Space)
	{
		GridPlayer.SetBlendSpace(Space);
		bGridNeedsReinit = true;   // reset the play time on the worker, like a sequence player
	}
	GridPlayer.SetLoop(bLoop);
	GridPlayer.SetPlayRate(1.f);

	// A grid REPLACES the crossfade rather than joining it, so the fades in flight are dropped along
	// with the clip they were blending toward. Leaving them running would have EvaluateBody blend a
	// pose nothing is producing any more.
	Fading.Reset();
	bInitialized = true;
	return true;
}

void FElysiumNpcAnimProxy::SetGridPosition(float Axis0, float Axis1)
{
	// No reinit: the sample point is read fresh on every update, so moving it steers the blend
	// without restarting the animations underneath it. That is the whole point of a grid over a
	// per-cell clip pick, which had to swap the sequence to change direction.
	GridPlayer.SetPosition(FVector(Axis0, Axis1, 0.f));
}

void FElysiumNpcAnimProxy::StopGrid()
{
	GridPlayer.SetBlendSpace(nullptr);
}

bool FElysiumNpcAnimProxy::RequestLayer(UAnimSequence* Sequence, bool bLoop, float Weight)
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

void FElysiumNpcAnimProxy::ResolveLayerMask(int32 Layer, const UAnimSequence* Sequence,
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

void FElysiumNpcAnimProxy::StopLayer(const UAnimSequence* Sequence)
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

void FElysiumNpcAnimProxy::StopAllLayers()
{
	for (float& Weight : LayerWeights)
	{
		Weight = 0.f;
	}
}

int32 FElysiumNpcAnimProxy::NumLayers() const
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

void FElysiumNpcAnimProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	// Ahead of the initialised gate: a layer rides over whatever
	// the body produced, INCLUDING the reference pose a body with no clip falls back to. A weapon
	// overlay on an NPC that has not been given a stance yet must still run rather than hold frame
	// zero until one arrives.
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

	if (!bInitialized)
	{
		return;
	}

	// The grid, when one is standing, IS the body — so it advances instead of the crossfade below
	// rather than beside it, and takes the whole weight.
	if (GridPlayer.GetBlendSpace() != nullptr)
	{
		if (bGridNeedsReinit)
		{
			bGridNeedsReinit = false;
			FAnimationInitializeContext InitContext(this);
			GridPlayer.Initialize_AnyThread(InitContext);
			FAnimationCacheBonesContext BoneContext(this);
			GridPlayer.CacheBones_AnyThread(BoneContext);
		}
		GridPlayer.Update_AnyThread(InContext);
		return;
	}

	// A player whose clip changed restarts from its start position. Done here rather than in
	// Request() because this is the thread and the context the node expects. Every slot, not the
	// first two: TakeFreeSlot hands out all four, and a slot whose flag is never consumed keeps the
	// previous clip's play time and starts the new one part-way through.
	for (int32 i = 0; i < MaxPlayers; ++i)
	{
		if (bNeedsReinit[i])
		{
			bNeedsReinit[i] = false;
			FAnimationInitializeContext InitContext(this);
			Players[i].Initialize_AnyThread(InitContext);
			FAnimationCacheBonesContext BoneContext(this);
			Players[i].CacheBones_AnyThread(BoneContext);
		}
	}

	// Every fade runs on its own absolute clock and leaves only when its weight reaches zero — retail
	// caps nothing and evicts nothing early, so a burst of clip changes simply stacks and decays.
	const float Dt = InContext.GetDeltaTime();
	for (int32 Index = Fading.Num() - 1; Index >= 0; --Index)
	{
		Fading[Index].Elapsed += Dt;
		if (FadeWeight(Fading[Index]) <= 0.f)
		{
			Fading.RemoveAt(Index);
		}
	}

	// Advancing play time is what this call is for, and a fading clip keeps advancing its own —
	// retail stores a playbackrate on the previous-sequence record and keeps running it. Weighting
	// each player by its share keeps notifies and root motion proportional, even though neither is
	// consumed yet.
	float Residual = 1.f;
	for (const FFadingClip& Fade : Fading)
	{
		Residual *= 1.f - FadeWeight(Fade);
	}
	Players[Current].Update_AnyThread(InContext.FractionalWeight(Residual));
	for (const FFadingClip& Fade : Fading)
	{
		if (Players[Fade.Slot].GetSequence() != nullptr)
		{
			Players[Fade.Slot].Update_AnyThread(InContext.FractionalWeight(FadeWeight(Fade)));
		}
	}
}

void FElysiumNpcAnimProxy::EvaluateBody(FPoseContext& Output)
{
	// A standing grid produces the whole body pose on its own. The blend across its cells is the
	// blend space's, evaluated at the sample point `SetGridPosition` last wrote — there is nothing
	// for the crossfade below to contribute, and `Request` cleared it when this was set.
	if (bInitialized && GridPlayer.GetBlendSpace() != nullptr)
	{
		GridPlayer.Evaluate_AnyThread(Output);
		return;
	}

	if (!bInitialized || Players[Current].GetSequence() == nullptr)
	{
		Output.ResetToRefPose();
		return;
	}

	// The live clip always contributes; a settled NPC has no fades and evaluates exactly one
	// sequence, which is the common case by a wide margin.
	FPoseContext Accumulated(this);
	Players[Current].Evaluate_AnyThread(Accumulated);

	// Retail blends newest-first, oldest-last — a CHAIN of pairwise slerps toward each previous
	// pose by that record's own weight, not a normalised N-way blend. Order matters: the oldest
	// record carries the smallest weight and lands last, so it exerts the weakest pull on the
	// result. `Fading` is already kept in that order.
	for (const FFadingClip& Fade : Fading)
	{
		if (Players[Fade.Slot].GetSequence() == nullptr)
		{
			continue;
		}
		FPoseContext Previous(this);
		Players[Fade.Slot].Evaluate_AnyThread(Previous);

		FPoseContext Blended(this);
		FAnimationPoseData BlendedData(Blended);
		const FAnimationPoseData AccumulatedData(Accumulated);
		const FAnimationPoseData PreviousData(Previous);
		// WeightOfPoseOne is the FIRST argument's share, so the pose built so far leads and the
		// fading clip takes its record's weight.
		FAnimationRuntime::BlendTwoPosesTogether(
			AccumulatedData, PreviousData, 1.f - FadeWeight(Fade), BlendedData);
		Accumulated = Blended;
	}

	Output = Accumulated;
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

void FElysiumNpcAnimProxy::DumpLayerPose(int32 Layer, const FPoseContext& Pose)
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
void FElysiumNpcAnimProxy::EvaluateLayers(FPoseContext& Output)
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
					// `FAnimNode_ApplyAdditive` does; the graph replaces this loop with that node.
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

bool FElysiumNpcAnimProxy::Evaluate(FPoseContext& Output)
{
	EvaluateBody(Output);
	EvaluateLayers(Output);
	// The composition stages and the face, shared with every other body.
	EvaluateTail(Output);
	return true;
}


void UElysiumNpcAnimInstance::PlayClip(UAnimSequence* Sequence, bool bLoop, float BlendSeconds)
{
	if (Sequence == nullptr)
	{
		return;
	}
	// GetProxyOnGameThread blocks on any in-flight parallel evaluation, so the write cannot race
	// the worker reading the same fields.
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().Request(Sequence, bLoop, BlendSeconds);
}

void UElysiumNpcAnimInstance::SeekClip(float PositionSeconds)
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().Seek(PositionSeconds);
}

void UElysiumNpcAnimInstance::StopClip()
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().Stop();
}

bool UElysiumNpcAnimInstance::PlayGrid(UBlendSpace* Space, bool bLoop)
{
	if (Space == nullptr)
	{
		return false;
	}
	return GetProxyOnGameThread<FElysiumNpcAnimProxy>().RequestGrid(Space, bLoop);
}

void UElysiumNpcAnimInstance::SetGridPosition(float Axis0, float Axis1)
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetGridPosition(Axis0, Axis1);
}

void UElysiumNpcAnimInstance::StopGrid()
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().StopGrid();
}

bool UElysiumNpcAnimInstance::PlayLayer(UAnimSequence* Sequence, float Weight, bool bLoop)
{
	// Layers accumulate in LOCAL space underneath the composition stage, which is where retail
	// puts them.
	return GetProxyOnGameThread<FElysiumNpcAnimProxy>().RequestLayer(Sequence, bLoop, Weight);
}

void UElysiumNpcAnimInstance::StopLayer(UAnimSequence* Sequence)
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().StopLayer(Sequence);
}

void UElysiumNpcAnimInstance::StopAllLayers()
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().StopAllLayers();
}

int32 UElysiumNpcAnimInstance::GetActiveLayers() const
{
	return const_cast<UElysiumNpcAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumNpcAnimProxy>().NumLayers();
}

float UElysiumNpcAnimInstance::GetClipPosition() const
{
	return const_cast<UElysiumNpcAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumNpcAnimProxy>().GetClipPosition();
}

void UElysiumNpcAnimInstance::ResyncClip(float PositionSeconds)
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().ResyncPosition(PositionSeconds);
}


bool UElysiumNpcAnimInstance::PlayOneShot(UAnimSequence* Sequence, bool bLoop, float BlendSeconds)
{
	// The shared seam, answered over the crossfade pool: this instance IS a clip player, so a
	// one-shot is an ordinary request rather than something layered over a base pose.
	if (Sequence == nullptr)
	{
		return false;
	}
	PlayClip(Sequence, bLoop, BlendSeconds);
	return true;
}

void UElysiumNpcAnimInstance::StopOneShot(float /*BlendSeconds*/)
{
	// No blend out: `Stop` clears the pool, which is what dropping the body's only clip means here.
	// A caller wanting a ramp plays the pose it wants to end on.
	StopClip();
}
