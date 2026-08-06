#include "Visual/ElysiumNpcAnimInstance.h"

#include "Visual/ElysiumClothRig.h"
#include "Visual/ElysiumCompositionRig.h"
#include "Visual/ElysiumFacialRig.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimCurveElementFlags.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// The reconstruction described in `Visual/ElysiumFacialRig.h`: the amplitude jaw's weight is also
	// raised into the `jaw_drop` controller, because the flexdesc `mstudiomouth_t` actually names
	// carries no flex record on any shipped model and so moves nothing on its own. 0 leaves only the
	// faithful write, which is the A/B baseline for the divergence.
	TAutoConsoleVariable<int32> CVarFacialJawBridge(
		TEXT("elysium.FacialJawBridge"),
		1,
		TEXT("Bridge the amplitude jaw into the jaw_drop flex controller (1, default) or write only "
		     "the mouth flexdesc, which no shipped model consumes (0)."),
		ECVF_Default);
}

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
	}
	bInitialized = false;
	// The facial track is not reset here: the rig is installed once per body, before or after this
	// runs depending on when the component registers, and re-initializing the pose graph does not
	// change which face the body wears.
}

void FElysiumNpcAnimProxy::CacheBones()
{
	FAnimationCacheBonesContext Context(this);
	for (FAnimNode_SequencePlayer_Standalone& Player : Players)
	{
		Player.CacheBones_AnyThread(Context);
	}
	for (FAnimNode_SequencePlayer_Standalone& Player : LayerPlayers)
	{
		Player.CacheBones_AnyThread(Context);
	}
	// The bone container is what a bone reference resolves against, and this is the callback its
	// change arrives on — so both composition stages resolve their indices here, once, and never
	// by name per evaluation.
	Split.ResolveBones(GetRequiredBones());
	AxisInterp.ResolveBones(GetRequiredBones());
	// The cloth chains take the context rather than the container: they are constructed here too,
	// because the reference skeleton their body definitions need is only reachable from it.
	Cloth.CacheBones(Context);
}

void FElysiumNpcAnimProxy::PreUpdateCloth(const UAnimInstance* Instance)
{
	Cloth.PreUpdate(Instance);
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

bool FElysiumNpcAnimProxy::RequestAdditive(UAnimSequence* Sequence, bool bLoop, float Weight)
{
	if (Sequence == nullptr)
	{
		return false;
	}
	// The gate that keeps this honest. `EvaluateAdditives` reads the layer player's pose as a
	// DELTA, and only a sequence Unreal considers additive evaluates to one: for those it starts
	// from the additive identity and lets the compressed delta overwrite the bones the clip carries
	// tracks for, so a bone the layer never touches comes back as identity. An ordinary sequence
	// starts from the REFERENCE pose instead, and accumulating that would post-multiply every
	// untouched bone by its own bind rotation — a folded skeleton, from data that looks fine.
	if (!Sequence->IsValidAdditive())
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
	bLayerNeedsReinit[Chosen] = true;   // reset that player's play time on the worker
	return true;
}

void FElysiumNpcAnimProxy::StopAdditive(const UAnimSequence* Sequence)
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

void FElysiumNpcAnimProxy::StopAllAdditives()
{
	for (float& Weight : LayerWeights)
	{
		Weight = 0.f;
	}
}

int32 FElysiumNpcAnimProxy::NumAdditiveLayers() const
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
	// Ahead of the initialised gate: the simulation's only source of a timestep is this call, and a
	// body with no clip still evaluates — against the ref pose — so its garment must still hang and
	// settle rather than freeze mid-air until something plays.
	Cloth.Update(InContext);

	// Also ahead of the gate, and for the same reason as the garment: a layer rides over whatever
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

// The A/B for the autolayer accumulator. 1 = VtMB's own post-multiplied combine, 0 = no layer at
// all, which is what every body composed before this existed. There is deliberately no "use
// Unreal's additive node" setting: every Unreal additive mode pre-multiplies, and the cost of that
// order is measured rather than offered — 89.1% of bone-frames within 0.5 degrees, 1.9% past 10,
// worst 162.9 on a thigh (`docs/vtmb/animation_and_movers.md`).
static TAutoConsoleVariable<int32> CVarAdditiveLayers(
	TEXT("elysium.AdditiveLayers"), 1,
	TEXT("1 = accumulate VtMB `_delta` autolayers onto the body pose, 0 = ignore every layer."),
	ECVF_Default);

// One-shot: dump the delta the NEXT additive evaluation reads, per bone, largest first.
//
// The question this exists to answer is the one no screenshot can: whether the pose the applier
// accumulates is the delta VtMB authored. The container's own numbers are readable offline, so a
// disagreement localises the fault immediately -- matching numbers mean the bake and the read are
// sound and the accumulate is wrong, differing numbers mean the opposite.
//
// A counter rather than a cvar read, because the consumer runs on the animation worker and a cvar
// write from there is not safe. The command arms it on the game thread; the worker takes it.
static FThreadSafeCounter GAdditiveDumpRequest;

static FAutoConsoleCommand GAdditiveDumpCommand(
	TEXT("elysium.AdditiveDump"),
	TEXT("Log the additive delta the next layer evaluation reads, per bone, largest first."),
	FConsoleCommandDelegate::CreateLambda([] { GAdditiveDumpRequest.Set(1); }));

void FElysiumNpcAnimProxy::DumpAdditivePose(int32 Layer, const FPoseContext& Delta)
{
	// Consumed here rather than in the caller, so exactly one layer of one body answers a request
	// even when several are composing.
	if (GAdditiveDumpRequest.Set(0) == 0)
	{
		return;
	}
	const FBoneContainer& Container = Delta.Pose.GetBoneContainer();
	const FReferenceSkeleton& Ref = Container.GetReferenceSkeleton();

	struct FRow { double Degrees; double Centimetres; FName Bone; };
	TArray<FRow> Rows;
	Rows.Reserve(Delta.Pose.GetNumBones());
	for (const FCompactPoseBoneIndex BoneIndex : Delta.Pose.ForEachBoneIndex())
	{
		const FTransform& Add = Delta.Pose[BoneIndex];
		const FMeshPoseBoneIndex MeshIndex = Container.MakeMeshPoseIndex(BoneIndex);
		Rows.Add({
			FMath::RadiansToDegrees(Add.GetRotation().GetAngle()),
			Add.GetTranslation().Size(),
			Ref.IsValidIndex(MeshIndex.GetInt()) ? Ref.GetBoneName(MeshIndex.GetInt()) : NAME_None });
	}
	Rows.Sort([](const FRow& A, const FRow& B) { return A.Degrees > B.Degrees; });

	const UAnimSequence* Sequence = Cast<UAnimSequence>(LayerPlayers[Layer].GetSequence());
	int32 Quiet = 0;
	for (const FRow& Row : Rows)
	{
		Quiet += Row.Degrees < 1.0 ? 1 : 0;
	}
	UE_LOG(LogTemp, Display, TEXT("additive layer %d: '%s' at t=%.3f, %d bone(s), additive=%d"),
		Layer, Sequence != nullptr ? *Sequence->GetName() : TEXT("none"),
		LayerPlayers[Layer].GetAccumulatedTime(), Rows.Num(),
		Sequence != nullptr ? static_cast<int32>(Sequence->AdditiveAnimType) : -1);
	for (int32 i = 0; i < FMath::Min(12, Rows.Num()); ++i)
	{
		UE_LOG(LogTemp, Display, TEXT("   %-24s %7.2f deg   %6.2f cm"),
			*Rows[i].Bone.ToString(), Rows[i].Degrees, Rows[i].Centimetres);
	}
	UE_LOG(LogTemp, Display, TEXT("   ... %d of %d bones under 1 deg"), Quiet, Rows.Num());
}

void FElysiumNpcAnimProxy::EvaluateAdditives(FPoseContext& Output)
{
	if (CVarAdditiveLayers.GetValueOnAnyThread() == 0)
	{
		return;
	}

	// RETAIL'S SLOT, and it is not the obvious one. `FUN_10089c40` walks a sequence's autolayers
	// and accumulates each through `FUN_10088e10` while the pose is still LOCAL, before
	// `BuildTransformations` (`FUN_1008fd00`) composes the hierarchy and applies split inheritance.
	// So a layer lands under the composition stages, not over them, and the stages see the
	// accumulated result. Running this after EvaluateComposition is wrong in a way that still looks
	// plausible on a screenshot.
	for (int32 Layer = 0; Layer < MaxLayers; ++Layer)
	{
		const float LayerWeight = LayerWeights[Layer];
		if (LayerPlayers[Layer].GetSequence() == nullptr || LayerWeight <= 0.f)
		{
			continue;
		}

		FPoseContext Delta(this);
		LayerPlayers[Layer].Evaluate_AnyThread(Delta);

		if (GAdditiveDumpRequest.GetValue() != 0)
		{
			DumpAdditivePose(Layer, Delta);
		}

		// `s = layer_weight * bone_weight` in retail, where bone_weight is the animation record's
		// `weight`@0. That field is a MASK rather than a factor — both channel decoders test it
		// against zero and return zeros — and no record in the retail capture corpus carries
		// anything but 1.0, so the product reduces to the layer weight. The masked partial-body
		// overlays (`<weapon>_aim_layer`) are the case that needs the mask, and they need it as a
		// skeleton blend profile rather than as a per-clip array.
		const float S = LayerWeight;
		const bool bFull = S >= 1.f - KINDA_SMALL_NUMBER;

		for (const FCompactPoseBoneIndex BoneIndex : Output.Pose.ForEachBoneIndex())
		{
			const FTransform& Add = Delta.Pose[BoneIndex];
			FTransform& Bone = Output.Pose[BoneIndex];

			// POST-multiplied: `out = out * scale(delta, s)`. All 118 shipped `_delta` sequences
			// carry `0x14` — STUDIO_DELTA together with STUDIO_POST — which selects `FUN_10088d60`,
			// the combine that puts the delta on the RIGHT. Every Unreal additive mode puts it on
			// the left (`FTransform::BlendFromIdentityAndAccumulate`), which is why this is written
			// out here instead of calling FAnimationRuntime::AccumulateAdditivePose.
			//
			// `scale(q, s)` is retail's `QuaternionScale`: a true power, not the nlerp Unreal's
			// accumulator uses. It keeps the sign of the input rather than aligning, which is the
			// same ROTATION as slerping from identity along the short arc.
			const FQuat Scaled = bFull
				? Add.GetRotation()
				: FQuat::Slerp(FQuat::Identity, Add.GetRotation(), S);
			Bone.SetRotation((Bone.GetRotation() * Scaled).GetNormalized());
			Bone.AddToTranslation(Add.GetTranslation() * S);
			// Scale is deliberately untouched. VtMB animates none — the format carries no scale
			// channel at all — so an additive scale term would only ever apply the identity, and
			// leaving it alone keeps a body whose mesh was built at a non-unit scale intact.
		}
	}
}

// The A/B for the two composition stages. 1 = VtMB's own composition, 0 = Unreal's ordinary
// hierarchy alone, which is what every body posed under before CAP7.2. Dropping it is visible
// exactly where the docs measure it: up to 44.9 degrees on a shoulder, 26.9 on a bicep, 6.4 on a
// wrist, and a `Flags & 0x2` spine rooted in its parent rather than the component.
static TAutoConsoleVariable<int32> CVarCompositionStages(
	TEXT("elysium.CompositionStages"), 1,
	TEXT("1 = apply VtMB split inheritance + axis interpolation over the blended pose (CAP7.2), ")
	TEXT("0 = ordinary Unreal hierarchy composition only."),
	ECVF_Default);

void FElysiumNpcAnimProxy::EvaluateComposition(FPoseContext& Output)
{
	const bool bStages = (Split.HasWork() || AxisInterp.HasWork())
		&& CVarCompositionStages.GetValueOnAnyThread() != 0;
	if (!bStages && !Cloth.HasWork())
	{
		return;
	}

	// Retail's slot: the locals are decoded and blended, the hierarchy composes, then these run,
	// then skinning.
	//
	// Axis interpolation genuinely belongs here. A driven bone reads its control bone's live
	// orientation, so it has no value at all until there is a finished pose to read one from, and
	// no offline pass can produce one.
	//
	// Split inheritance does NOT, and the distinction is worth stating because the opposite is easy
	// to assume. The correction is `world_rot(parent)^-1 * local`, which is fixed per clip per
	// frame, so `UE_mdl_skeletal.py` rewrites that one rotation curve at export and a clip off the
	// baked mount plays with this stage declined. The gate is PER CLIP, not per body
	// (`PlayClip` -> `IsBakedClip` -> `SetCompositionRig`): a baked body still reaches for
	// loader-sourced bank clips, which carry VtMB's rotations unchanged and do need it. Per clip that is
	// exact — 7.2e-06 degrees over 24 models and 14k frame-poses, which is float noise. It is
	// approximate only ACROSS A TRANSITION, where a crossfade blends two clips already normalised
	// against their own parent chains and then composes once against the blended chain: measured
	// over the male locomotion bank at five blend weights, 96.7% of bone samples land within 2
	// degrees and the worst is 5.2, for the length of a fade. That is the whole cost, and it buys
	// self-describing assets that pose correctly with no runtime rule — including in the Content
	// Browser and the animation editor, neither of which can run this node.
	//
	// This stage therefore runs for a body on the glTFRuntime path, whose `.glb` clips carry VtMB's
	// rotations unchanged, and both paths are live in one map.
	// Copied in, not moved: the conversion back writes *into* Output.Pose and addresses it by bone
	// index, so it has to still be a sized pose when we get there. Moving it out leaves it empty and
	// the first write indexes an array of size zero.
	FComponentSpacePoseContext Composed(this);
	Composed.Pose.InitPose(Output.Pose);

	// ORDER IS LOAD-BEARING. Split inheritance re-roots `Bip01 Spine1`'s orientation, and every
	// driven arm bone hangs off it, so running these the other way produces a different skeleton.
	if (bStages)
	{
		Split.Apply(Composed);
		AxisInterp.Apply(Composed);
	}
	// Last, over the finished skeleton. The garment is synthesised geometry hanging off the pelvis
	// and shares no bone with either stage above, but it should still swing from the pose that will
	// actually be drawn rather than one still missing its corrections.
	Cloth.Apply(Composed);

	// Safe, not the plain form: both stages leave a bone in local space whose parent may never have
	// been asked for in component space, and the plain conversion ensures against exactly that.
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(Composed.Pose, Output.Pose);
}

bool FElysiumNpcAnimProxy::Evaluate(FPoseContext& Output)
{
	EvaluateBody(Output);
	EvaluateAdditives(Output);
	EvaluateComposition(Output);

	// The face is written last, over whatever the body produced — including the ref pose a body
	// with no clip falls back to, so a facial-only preview still moves. VtMB's clips carry no curves
	// at all, so nothing is being overwritten here; these names exist only because 12.3 puts them
	// there. The curves reach the component's morph weights through the skeleton's morph-target
	// curve metadata (`ElysiumNpcVisual::RegisterMorphTargetCurves`), and every morph is written
	// every frame — including the zeros, which is what releases a controller that went back to rest.
	const int32 Num = FMath::Min(FacialCurves.Num(), FacialWeights.Num());
	for (int32 i = 0; i < Num; ++i)
	{
		Output.Curve.Set(FacialCurves[i], FacialWeights[i]);
		Output.Curve.SetFlags(FacialCurves[i], UE::Anim::ECurveElementFlags::MorphTarget);
	}
	return true;
}

void FElysiumNpcAnimProxy::SetFacialTrack(TArray<FName>&& InCurves)
{
	FacialCurves = MoveTemp(InCurves);
	FacialWeights.Reset(FacialCurves.Num());
	FacialWeights.AddZeroed(FacialCurves.Num());
}

void FElysiumNpcAnimProxy::SetFacialWeights(TArrayView<const float> InWeights)
{
	const int32 Num = FMath::Min(FacialWeights.Num(), InWeights.Num());
	for (int32 i = 0; i < Num; ++i)
	{
		FacialWeights[i] = InWeights[i];
	}
}

void FElysiumNpcAnimProxy::SetCompositionRig(TSharedPtr<const FElysiumCompositionRig> InRig,
	bool bSplitInheritance)
{
	// The split stage is declined by handing it no rig at all rather than by a flag it checks:
	// `HasWork()` then answers false, `EvaluateComposition` skips the component-space round trip
	// when the axis stage is also idle, and the debug surface reports the stage as absent, which
	// is the truth for a body whose clips already carry the correction.
	Split.SetRig(bSplitInheritance ? InRig : nullptr);
	AxisInterp.SetRig(MoveTemp(InRig));
	// A rig installed before the component has ever cached bones resolves on the first evaluate;
	// one installed after re-resolves here, because the bone container is already valid.
	if (const FBoneContainer& Container = GetRequiredBones(); Container.IsValid())
	{
		Split.ResolveBones(Container);
		AxisInterp.ResolveBones(Container);
	}
}

void FElysiumNpcAnimProxy::SetClothRig(TSharedPtr<const FElysiumClothRig> InRig)
{
	Cloth.SetRig(MoveTemp(InRig));
	// Same rule as the composition rig, and the same reason: installed before the first CacheBones
	// this resolves there, installed after it has to rebuild against the container already in force.
	// The chains are torn down by SetRig, so this is a construction rather than a re-resolve.
	if (const FBoneContainer& Container = GetRequiredBones(); Container.IsValid())
	{
		FAnimationCacheBonesContext Context(this);
		Cloth.CacheBones(Context);
	}
}

void UElysiumNpcAnimInstance::PlayClip(UAnimSequence* Sequence, bool bLoop, float BlendSeconds)
{
	if (Sequence == nullptr)
	{
		return;
	}
	// Split inheritance follows the CLIP, not the body. `ResolveClip` picks the baked sequence when
	// there is one and silently falls back to a glTFRuntime build otherwise, so a body on the baked
	// mount still plays un-normalised `.glb` clips for any bank or name the bake has not covered --
	// a cinematic set most of all. Those need the rule; the baked ones already carry it, and
	// applying it to them bends the body by exactly the amount it exists to remove.
	//
	// Re-installed only on a change: SetRig tears down and re-resolves the node's bone references,
	// which is not work to repeat on every clip request.
	const bool bWantSplit = !ElysiumNpcVisual::IsBakedClip(Sequence);
	if (bWantSplit != bSplitInheritance)
	{
		bSplitInheritance = bWantSplit;
		GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetCompositionRig(CompositionRig, bWantSplit);
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

bool UElysiumNpcAnimInstance::PlayLayer(UAnimSequence* Sequence, float Weight, bool bLoop)
{
	// No composition-rig decision here, unlike PlayClip. Split inheritance is a property of the
	// BASE pose's clip: an autolayer is a delta accumulated in local space underneath the
	// composition stages, so which stages run is still the standing clip's answer.
	return GetProxyOnGameThread<FElysiumNpcAnimProxy>().RequestAdditive(Sequence, bLoop, Weight);
}

void UElysiumNpcAnimInstance::StopLayer(UAnimSequence* Sequence)
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().StopAdditive(Sequence);
}

void UElysiumNpcAnimInstance::StopAllLayers()
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().StopAllAdditives();
}

int32 UElysiumNpcAnimInstance::GetActiveLayers() const
{
	return const_cast<UElysiumNpcAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumNpcAnimProxy>().NumAdditiveLayers();
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

void UElysiumNpcAnimInstance::SetFacialRig(TSharedPtr<const FElysiumFacialRig> InRig)
{
	FacialRig = MoveTemp(InRig);
	ControllerValues.Reset();
	FlexWeights.Reset();
	MorphWeights.Reset();
	MouthOpen = 0.f;

	TArray<FName> Curves;
	if (FacialRig.IsValid())
	{
		ControllerValues.AddZeroed(FacialRig->Controllers.Num());
		Curves.Reserve(FacialRig->Morphs.Num());
		for (const FElysiumFlexMorph& Morph : FacialRig->Morphs)
		{
			Curves.Add(Morph.Curve);
		}
	}
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetFacialTrack(MoveTemp(Curves));
	// Publish the rest pose immediately: with every controller at zero the rules resolve each lid to
	// its own hinge, so every morph target lands at exactly zero and the face is the authored mesh.
	EvaluateFacial();
}

void UElysiumNpcAnimInstance::SetCompositionRig(TSharedPtr<const FElysiumCompositionRig> InRig,
	bool bInSplitInheritance)
{
	CompositionRig = MoveTemp(InRig);
	// The state a body starts in, before any clip has been requested — a body posing its ref pose
	// with no clip must not have the rule applied either. `PlayClip` refines it per clip.
	bSplitInheritance = bInSplitInheritance;
	// GetProxyOnGameThread blocks on any in-flight parallel evaluation, so the worker cannot be
	// reading the rig this replaces.
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetCompositionRig(CompositionRig, bSplitInheritance);
}

int32 UElysiumNpcAnimInstance::GetResolvedAxisInterpRules() const
{
	return const_cast<UElysiumNpcAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumNpcAnimProxy>().NumAxisInterpRules();
}

void UElysiumNpcAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	// Skipped entirely for the overwhelming majority of bodies, which carry no garment rig at all.
	if (ClothRig.IsValid())
	{
		GetProxyOnGameThread<FElysiumNpcAnimProxy>().PreUpdateCloth(this);
	}
}

void UElysiumNpcAnimInstance::SetClothRig(TSharedPtr<const FElysiumClothRig> InRig)
{
	ClothRig = MoveTemp(InRig);
	// Same guarantee as the composition rig: GetProxyOnGameThread blocks on any in-flight parallel
	// evaluation, so no worker can be simulating against the chains this replaces.
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetClothRig(ClothRig);
}

void UElysiumNpcAnimInstance::SetClothTuning(const FElysiumClothTuning& InTuning)
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetClothTuning(InTuning);
}

FElysiumClothTuning UElysiumNpcAnimInstance::GetClothTuning() const
{
	return const_cast<UElysiumNpcAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumNpcAnimProxy>().GetClothTuning();
}

int32 UElysiumNpcAnimInstance::GetResolvedClothChains() const
{
	return const_cast<UElysiumNpcAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumNpcAnimProxy>().NumClothChains();
}

bool UElysiumNpcAnimInstance::SetFlexController(const FString& Name, float Value)
{
	const int32 Index = FacialRig.IsValid() ? FacialRig->FindController(Name) : INDEX_NONE;
	return Index != INDEX_NONE && SetFlexControllerByIndex(Index, Value);
}

bool UElysiumNpcAnimInstance::SetFlexControllerByIndex(int32 Index, float Value)
{
	if (!FacialRig.IsValid() || !ControllerValues.IsValidIndex(Index))
	{
		return false;
	}
	const float Normalized = FacialRig->Controllers[Index].Normalize(Value);
	if (ControllerValues[Index] != Normalized)
	{
		ControllerValues[Index] = Normalized;
		EvaluateFacial();
	}
	return true;
}

int32 UElysiumNpcAnimInstance::SetFlexControllers(TArrayView<const FElysiumFlexWrite> Writes,
	TArray<FString>* OutMissing)
{
	if (!FacialRig.IsValid())
	{
		return INDEX_NONE;
	}
	int32 Applied = 0;
	bool bChanged = false;
	for (const FElysiumFlexWrite& Write : Writes)
	{
		const int32 Index = FacialRig->FindController(Write.Name);
		if (!ControllerValues.IsValidIndex(Index))
		{
			// A key this model does not carry. Reported, never guessed at: the 249 shipped tables draw
			// on 48 distinct key names and no single rig carries all of them.
			if (OutMissing != nullptr)
			{
				OutMissing->AddUnique(Write.Name);
			}
			continue;
		}
		++Applied;
		const float Normalized = FacialRig->Controllers[Index].Normalize(Write.Value);
		if (ControllerValues[Index] != Normalized)
		{
			ControllerValues[Index] = Normalized;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		EvaluateFacial();
	}
	return Applied;
}

void UElysiumNpcAnimInstance::ResetFlexControllers()
{
	if (ControllerValues.IsEmpty() && MouthOpen == 0.f)
	{
		return;
	}
	FMemory::Memzero(ControllerValues.GetData(), ControllerValues.Num() * sizeof(float));
	MouthOpen = 0.f;
	EvaluateFacial();
}

bool UElysiumNpcAnimInstance::HasMouth() const
{
	return FacialRig.IsValid() && FacialRig->Mouth.IsValid();
}

bool UElysiumNpcAnimInstance::SetMouthOpen(float Open)
{
	if (!HasMouth())
	{
		return false;
	}
	const float Clamped = FMath::Clamp(Open, 0.f, 1.f);
	if (MouthOpen != Clamped)
	{
		MouthOpen = Clamped;
		EvaluateFacial();
	}
	return true;
}

bool UElysiumNpcAnimInstance::SetEyeInput(const FElysiumEyeInput& Eyes)
{
	if (!FacialRig.IsValid())
	{
		return false;
	}
	EyeInput = Eyes;
	EvaluateFacial();
	return true;
}

void UElysiumNpcAnimInstance::EvaluateFacial()
{
	if (!FacialRig.IsValid())
	{
		return;
	}
	FElysiumJawInput Jaw;
	Jaw.Open = MouthOpen;
	// Read here rather than latched at the write, so toggling the cvar takes on the next evaluation
	// of any kind instead of waiting for the next jaw write.
	Jaw.bBridge = CVarFacialJawBridge.GetValueOnGameThread() != 0;
	FacialRig->Evaluate(ControllerValues, Jaw, EyeInput, FlexWeights, MorphWeights);
	// GetProxyOnGameThread blocks on any in-flight parallel evaluation, so the worker cannot be
	// reading the weight array this overwrites.
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetFacialWeights(MorphWeights);
}
