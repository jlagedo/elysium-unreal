// 8.3 — Dynamic props: the `prop_dynamic` (+ `prop_dynamic_ornament`) static-mesh leaf.
//
// Closes the "bodiless prop" gap (roadmap 9.3): a prop_dynamic record that parsed as an inert entity
// now stands its decoded static `.mdl` (out/<map>/props/<stem>.obj via IElysiumEmbodiment::BuildPropVisual,
// the shared prop decode 8.1 exports) at its placement, and gains per-instance addressability — the base
// ScriptHide/ScriptUnhide dormancy, the 9.3 SetOrigin/SetAngles/SetModel writers (body-follow), and the
// prop_dynamic inputs. `Break` hides the body and fires OnBreak; `Skin` repaints from the baked skin
// set; `SetAnimation` plays a named clip on the skeletal representation and arms the animate think
// that returns the prop to its `LoopSequence`.
//
// Deliberately out of scope: prop_physics Chaos bodies + constraints (8.4), the interactive
// prop_button/prop_sign/prop_switch/… `+use` family (4.10/8.8), and collision — a prop stands non-solid
// here (visual parity first; entity_visuals R2 "collision optional for visuals").

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumProp, Log, All);

namespace
{
	const UBodySetup* PropBodySetup(const UStaticMeshComponent* Comp)
	{
		const UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
		return Mesh ? Mesh->GetBodySetup() : nullptr;
	}

	// Does this body's mesh carry simple collision a Chaos rigid body can simulate against? The
	// bake writes one convex shape per `.phy` ledge, so an empty AggGeom means the model shipped
	// no VPhysics collision model at all.
	bool HasSimpleCollision(const UStaticMeshComponent* Comp)
	{
		const UBodySetup* Body = PropBodySetup(Comp);
		return Body && Body->AggGeom.GetElementCount() > 0;
	}

	// The model's authored `.phy` mass in kg, which the bake stored on the mesh's body setup, or
	// 0 when the model carries none. It has to be re-applied to the *component*: UBodySetup's
	// mass override is read off the owning primitive's own FBodyInstance whenever there is one
	// (UBodySetup::CalculateMass), and a component built at runtime never seeds that from the
	// asset — leaving Chaos to compute mass from hull volume instead (a 3 kg bin came out 48 kg).
	float AuthoredMassKg(const UStaticMeshComponent* Comp)
	{
		const UBodySetup* Body = PropBodySetup(Comp);
		return Body && Body->DefaultInstance.bOverrideMass
			? Body->DefaultInstance.GetMassOverride() : 0.0f;
	}
}

// A/B toggle for the dynamic-prop bodies (mirrors elysium.NpcBodies). Read in the leaf's Spawn, so it
// takes effect on the next map load: 1 stands the meshes, 0 leaves the props bodiless records (their
// I/O still resolves — this only gates the visual).
static TAutoConsoleVariable<int32> CVarPropBodies(
	TEXT("elysium.PropBodies"),
	1,
	TEXT("Stand dynamic-prop static-mesh bodies at their placements at map load (1, default) or skip them (0)."),
	ECVF_Default);

// A/B toggle for prop_physics simulation (8.4). 1 (default) simulates the body as a Chaos rigid
// body; 0 leaves it standing as a static, non-solid mesh (visual parity, like a prop_dynamic), so
// a map can be compared with and without physics. Read in the leaf's Spawn — takes effect next load.
static TAutoConsoleVariable<int32> CVarPhysicsProps(
	TEXT("elysium.PhysicsProps"),
	1,
	TEXT("Simulate prop_physics bodies as Chaos rigid bodies (1, default) or stand them static/non-solid (0)."),
	ECVF_Default);

namespace
{
	// Register a field backed by a subclass member (the base FElysiumClassDesc::Field only reaches
	// FElysiumEntity members). Mirrors AddNpcField — file-unique name so all of them can land in one
	// unity blob.
	template <typename TClass, typename TMember>
	void AddPropField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, EElysiumField Flags = ElysiumFieldDefault)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(Flags);
		if constexpr (std::is_same_v<TMember, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		}
		else if constexpr (std::is_same_v<TMember, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<TMember, float>)
		{
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToFloat(); };
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddPropField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

	// The `skin` field, whose setter repaints the body rather than only storing the number. VtMB
	// makes no distinction: `skin` is one datamap record flagged both KEY and INPUT with a null
	// inputFunc, so the keyvalue, the `Skin` wire and a script's `.skin =` all land in the same
	// direct write. Templated over the leaf because both prop leaves own their own Skin member.
	template <typename TClass>
	void AddPropSkinField(FElysiumClassDesc& D)
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(ElysiumFieldDefault);
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).Skin); };
		Acc.Set = [](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).SetSkin(V.ToInt()); };
		D.Fields.Add(FName(TEXT("skin")), MoveTemp(Acc));
	}
}

// ============================================================================================
// FElysiumProp — the AI-free static-mesh leaf shared by prop_dynamic / prop_dynamic_ornament. It
// stands a decoded prop mesh at its placement and exposes the dynamic-prop inputs over it.
// ============================================================================================

class FElysiumProp : public FElysiumEntity
{
public:
	// Exactly one representation is live. Ordinary props retain the baked static mesh; a model in
	// npc_index v4's animated_props section stands a skeletal glTF component instead.
	UStaticMeshComponent* Visual = nullptr;
	USkeletalMeshComponent* AnimatedVisual = nullptr;
	FString AnimatedStem;
	FString VisualStem;          // baked/static skin-table stem for either representation
	FString CurrentAnimation;
	bool bAnimationLoop = false;
	bool bBroken = false;
	int32 Skin = 0;

	// CDynamicProp's animation keyfields (datamap `0x1058d4f8`, 12 records at `0x1058d53c`). There is
	// no `demo_sequence` in the engine — the name appears nowhere in `vampire.dll`, so it is a
	// Hammer/FGD-only field and `LoopSequence` is the only authored default.
	FString LoopSequence;               // LoopSequence -> m_iszSequenceName @0x7d4
	bool    bRandomAnimator = false;    // RandomAnimation -> m_bRandomAnimator @0x7c4
	float   MinAnimTime = 0.0f;         // MinAnimTime -> m_flMinRandAnimTime @0x7cc
	float   MaxAnimTime = 0.0f;         // MaxAnimTime -> m_flMaxRandAnimTime @0x7d0

	// Derived. Retail carries a resolved sequence *index* (-1 when LoopSequence names nothing); the
	// animated-prop index exposes clip *names*, so the equivalent test is "it resolved to a real clip".
	bool   bLoopSequenceResolved = false;
	// The body is parked on a held pose — a clip seeked to frame 0 with the play rate at zero —
	// rather than playing. Retail's resting state (CBaseProp::Spawn); carried in the save so a
	// restore re-holds instead of starting the clip running.
	bool   bRestPoseHeld = false;
	// CDynamicProp::Activate armed the loop start and the think has not consumed it yet. Not saved:
	// a restored map re-runs Load -> PostSpawn and re-arms, which is what retail's Activate does.
	bool   bLoopStartPending = false;
	double NextRandAnim = 0.0;          // m_flNextRandAnim @0x7c8
	double AnimationEndTime = 0.0;      // absolute seconds the current one-shot ends; 0 = none pending

	virtual void Spawn() override
	{
		BuildBody(/*bFromSetModel=*/false);
		StandRestPose();
		// CDynamicProp::Spawn (FUN_101905e0) arms the animate think only for a random animator.
		// Every other prop spawns thinking never; SetAnimation is what arms it later.
		if (bRandomAnimator && World)
		{
			NextRandAnim = World->NowSeconds() + DrawRandomAnimInterval();
			NextThink = static_cast<float>(NextRandAnim + AnimThinkInterval);
		}
	}

	// CDynamicProp::Activate (FUN_101906c0). `LoopSequence` resolves HERE, not in Spawn: when it
	// names a real sequence — including sequence index 0, the compare is against -1 — the prop arms
	// a one-shot think at `curtime + RandomFloat(0.1, 0.99)`. That stagger is why three palm trees
	// do not sway in lockstep. The think it arms (FUN_10190750) assigns the sequence, calls
	// ResetSequenceInfo (which lifts the play rate off zero), fires OnAnimationBegun and hands over
	// to the 10 Hz animate think.
	virtual void PostSpawn() override
	{
		FElysiumEntity::PostSpawn();   // the base resolves `parentname` and attaches the body
		bLoopSequenceResolved = false;
		bLoopStartPending = false;
		if (!AnimatedVisual || !World || !MeaningfulSequence(LoopSequence))
		{
			return;
		}
		IElysiumEmbodiment* Embodiment = World->Embodiment();
		if (!Embodiment || AnimatedStem.IsEmpty())
		{
			return;
		}
		// Resolution is by name here rather than by index: the runtime addresses clips by label.
		bool bLoops = false;
		if (!Embodiment->FindAnimatedPropClip(AnimatedStem, LoopSequence, bLoops))
		{
			return;
		}
		bLoopSequenceResolved = true;
		bLoopStartPending = true;
		// NowSeconds() at PostSpawn equals the Now the world is activated with: the game clock only
		// advances from AElysiumMapActor::PreMoveTick, which is gated on RuntimePhase == Active and
		// so has not ticked yet. The stagger therefore survives the load gap intact. If that gate
		// ever moves, every prop's start collapses onto the first frame.
		NextThink = static_cast<float>(World->NowSeconds() + DrawLoopStartDelay());
	}

	// CDynamicPropAnimThink (FUN_10190850): drive the random animator and report a finished clip.
	// Retail leaves the think disarmed once a non-looping sequence has finished and there is no
	// random animator left to schedule — and because that disarm is permanent, the revert-to-
	// `LoopSequence` branch at the top of the retail think is **unreachable in shipped data**
	// (`m_bRandomAnimator` is zero on all 749 entities carrying the key). A finished one-shot holds
	// its final frame; it does not return to the authored loop, and there is no rest-pose fallback.
	virtual void Think() override
	{
		const double Now = World ? World->NowSeconds() : 0.0;

		// The Activate-phase one-shot (FUN_10190750), folded in as a flag rather than a second
		// think function: this think already owns the random animator's schedule, and a second
		// state would have to interleave with it.
		if (bLoopStartPending)
		{
			bLoopStartPending = false;
			if (StartLoopSequence())
			{
				static const FName OnAnimationBegun(TEXT("OnAnimationBegun"));
				FireOutput(OnAnimationBegun, Handle);
			}
			NextThink = static_cast<float>(Now + AnimThinkInterval);
			return;
		}

		if (bRandomAnimator && Now > NextRandAnim && PlayRandomAnimation())
		{
			static const FName OnAnimationBegun(TEXT("OnAnimationBegun"));
			FireOutput(OnAnimationBegun, Handle);
			NextRandAnim = Now + DrawRandomAnimInterval();
		}

		if (SequenceFinished(Now))
		{
			static const FName OnAnimationDone(TEXT("OnAnimationDone"));
			FireOutput(OnAnimationDone, Handle);
			if (!bRandomAnimator)
			{
				return;   // nothing left to schedule; retail returns without re-arming
			}
			NextThink = static_cast<float>(NextRandAnim + AnimThinkInterval);
			return;
		}
		NextThink = static_cast<float>(Now + AnimThinkInterval);
	}

	virtual UPrimitiveComponent* GetAttachBody() const override
	{
		return Visual ? static_cast<UPrimitiveComponent*>(Visual)
			: static_cast<UPrimitiveComponent*>(AnimatedVisual);
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		// m_flNextRandAnim is a retail SAVE field. AnimationEndTime is not carried: the replay below
		// restarts the clip, which is what re-derives it. `bRestPoseHeld` has to be, though — the
		// replay would otherwise start a resting prop's clip *running* instead of holding frame 0.
		Ar << CurrentAnimation << bAnimationLoop << bRestPoseHeld << NextRandAnim;
		if (Ar.IsLoading() && AnimatedVisual && !CurrentAnimation.IsEmpty())
		{
			const bool bWasHeld = bRestPoseHeld;
			PlayAnimation(CurrentAnimation, bAnimationLoop);   // clears bRestPoseHeld
			if (bWasHeld && World)
			{
				if (IElysiumEmbodiment* Embodiment = World->Embodiment())
				{
					Embodiment->SeekCinematicClip(AnimatedVisual, 0.0f);
					AnimationEndTime = 0.0;
					bRestPoseHeld = true;
				}
			}
		}
	}

	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		GateVisual();
	}

	virtual void OnRuntimeTransformChanged() override
	{
		FElysiumEntity::OnRuntimeTransformChanged();
		// Each representation keeps its own basis (see BuildBody).
		if (Visual)
		{
			Visual->SetWorldLocationAndRotation(Origin, FQuat(FRotator(0.0f, -Angles.Y, 0.0f)));
		}
		if (AnimatedVisual)
		{
			AnimatedVisual->SetWorldLocationAndRotation(Origin,
				FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles)));
		}
	}

	virtual void OnRuntimeModelChanged() override
	{
		DestroyBody();
		BuildBody(/*bFromSetModel=*/true);
		StandRestPose();
		// A SetModel arrives long after the Activate phase, so there is no stagger to wait out —
		// if the authored loop resolves on the new model it starts now.
		bLoopSequenceResolved = MeaningfulSequence(LoopSequence) && StartLoopSequence();
	}

	void InputBreak(const FElysiumInputArgs& Args)
	{
		if (bBroken)
		{
			return;
		}
		bBroken = true;
		GateVisual();
		static const FName OnBreak(TEXT("OnBreak"));
		FireOutput(OnBreak, Args.Activator);
		UE_LOG(LogElysiumProp, Verbose, TEXT("%s Break"), *DebugString());
	}

	void InputSkin(const FElysiumInputArgs& Args)
	{
		SetSkin(Args.Param.ToInt());
	}

	void SetSkin(int32 Family)
	{
		Skin = Family;
		ApplySkin();
	}

	void ApplySkin()
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (!Embodiment || VisualStem.IsEmpty())
		{
			return;
		}
		if (AnimatedVisual)
		{
			Embodiment->ApplyAnimatedPropSkin(AnimatedVisual, VisualStem, Skin);
		}
		else if (Visual)
		{
			Embodiment->ApplyPropSkin(Visual, VisualStem, Skin);
		}
	}

	void InputSetAnimation(const FElysiumInputArgs& Args)
	{
		const FString Clip = Args.Param.ToString();
		// InputSetAnimation does not force one shot: ResetSequenceInfo derives m_bSequenceLoops
		// from the model's own STUDIO_LOOPING bit, so the clip decides. Every clip the exported
		// maps name here is non-looping; a script naming a looping one gets a loop, as in retail.
		bool bLoops = false;
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Embodiment->FindAnimatedPropClip(AnimatedStem, Clip, bLoops);
		}
		if (!PlayAnimation(Clip, bLoops))
		{
			UE_LOG(LogElysiumProp, Warning, TEXT("%s SetAnimation '%s' did not resolve on %s"),
				*DebugString(), *Clip, AnimatedStem.IsEmpty() ? TEXT("static representation") : *AnimatedStem);
			return;
		}
		// InputSetAnimation (FUN_10190a00) re-arms the animate think, which is what carries the clip
		// to its end and fires OnAnimationDone before disarming again.
		if (World)
		{
			NextThink = static_cast<float>(World->NowSeconds() + AnimThinkInterval);
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
		Out.Emplace(TEXT("Body"), AnimatedVisual ? TEXT("skeletal animated prop")
			: (Visual ? TEXT("static mesh") : TEXT("(none)")));
		Out.Emplace(TEXT("Animation"), CurrentAnimation.IsEmpty() ? TEXT("(none)")
			: (bRestPoseHeld ? CurrentAnimation + TEXT(" (rest pose, held at frame 0)")
				: CurrentAnimation));
		Out.Emplace(TEXT("Animation loop"), bAnimationLoop ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Loop sequence"), LoopSequence.IsEmpty() ? TEXT("(none)")
			: (bLoopSequenceResolved ? LoopSequence : LoopSequence + TEXT(" (unresolved)")));
		if (bRandomAnimator)
		{
			Out.Emplace(TEXT("Random animator"),
				FString::Printf(TEXT("%.2f-%.2fs (not implemented)"), MinAnimTime, MaxAnimTime));
		}
		// Authored but ignored — `demo_sequence` is a Hammer/FGD field with no engine keyfield behind
		// it, so surface it rather than let the divergence disappear silently.
		const FString Demo = Def ? Def->Keys.FindRef(TEXT("demo_sequence")) : FString();
		if (MeaningfulSequence(Demo))
		{
			Out.Emplace(TEXT("demo_sequence"), Demo + TEXT(" (editor-only, ignored)"));
		}
		Out.Emplace(TEXT("Broken"), bBroken ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Skin"), FString::FromInt(Skin));
	}

private:
	static bool MeaningfulSequence(const FString& Sequence)
	{
		return !Sequence.IsEmpty()
			&& !Sequence.Equals(TEXT("none"), ESearchCase::IgnoreCase)
			&& !Sequence.Equals(TEXT("null"), ESearchCase::IgnoreCase)
			&& Sequence != TEXT("0");
	}

	// CDynamicPropAnimThink's re-arm interval (`_DAT_104493d0`).
	static constexpr double AnimThinkInterval = 0.1;

	// Retail's m_bSequenceFinished (+0x65c) for the cases the think cares about: a looping clip never
	// reports finished, and a one-shot is finished once its authored length has elapsed.
	bool SequenceFinished(double Now) const
	{
		return !bAnimationLoop && AnimationEndTime > 0.0 && Now >= AnimationEndTime;
	}

	// The gap until the next random draw. Cosmetic, so it deliberately does not come from a named
	// save-carried ElysiumRng stream.
	double DrawRandomAnimInterval() const
	{
		return FMath::FRandRange(FMath::Min(MinAnimTime, MaxAnimTime),
			FMath::Max(MinAnimTime, MaxAnimTime));
	}

	// CDynamicProp::Activate's `RandomFloat(0.1, 0.99)` start stagger. `FMath` for the same reason
	// the interval above uses it: the draw is sub-second, fires once per map load, and no save can
	// observe it — a restore re-runs PostSpawn and re-draws.
	static double DrawLoopStartDelay()
	{
		return FMath::FRandRange(0.1f, 0.99f);
	}

	// Retail re-picks here with the *same* call the spawn path uses — SelectWeightedSequence
	// (ACT_IDLE, -1), FUN_1008dc40 — not an arbitrary sequence. `RandomAnimation` is 0 on all 749
	// entities carrying it across the exported corpus, so this is unreachable in shipped data; it
	// is written out rather than stubbed because the seam it needs now exists, and no exported prop
	// model carries more than one ACT_IDLE clip, so the pick cannot actually vary.
	bool PlayRandomAnimation()
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (!Embodiment || AnimatedStem.IsEmpty())
		{
			return false;
		}
		const FString Clip = Embodiment->AnimatedPropRestClip(AnimatedStem);
		bool bLoops = false;
		Embodiment->FindAnimatedPropClip(AnimatedStem, Clip, bLoops);
		return PlayAnimation(Clip, bLoops);
	}

	void DestroyBody()
	{
		if (Visual) { Visual->DestroyComponent(); Visual = nullptr; }
		if (AnimatedVisual) { AnimatedVisual->DestroyComponent(); AnimatedVisual = nullptr; }
		AnimatedStem.Reset();
		VisualStem.Reset();
		CurrentAnimation.Reset();
		bAnimationLoop = false;
		bLoopSequenceResolved = false;
		bRestPoseHeld = false;
		bLoopStartPending = false;
		AnimationEndTime = 0.0;
	}

	bool PlayAnimation(const FString& Clip, bool bLoop)
	{
		if (!MeaningfulSequence(Clip) || !AnimatedVisual || !World)
		{
			return false;
		}
		IElysiumEmbodiment* Embodiment = World->Embodiment();
		float Seconds = 0.0f;
		if (!Embodiment || !Embodiment->PlayAnimatedPropClip(
			AnimatedVisual, AnimatedStem, Clip, bLoop, &Seconds))
		{
			return false;
		}
		CurrentAnimation = Clip;
		bAnimationLoop = bLoop;
		bRestPoseHeld = false;   // playing anything releases the held pose
		AnimationEndTime = (!bLoop && Seconds > 0.0f) ? World->NowSeconds() + Seconds : 0.0;
		return true;
	}

	// CBaseProp::Spawn (FUN_1018df70). A prop at rest is a **held pose, not a playing clip**: the
	// cycle and the playback rate are both left at zero, and only ResetSequenceInfo — which nothing
	// calls for a prop with no `LoopSequence` and no scripted SetAnimation — ever lifts the rate.
	// The sequence is SelectWeightedSequence(ACT_IDLE, -1) falling back to sequence index 0, which
	// is the branch that fires for the theatre's cinematic props: they tag `ACT_VM_IDLE`, not
	// `ACT_IDLE`. `demo_sequence` names the same clip but is not an engine keyfield, so it stays
	// unread — the activity/index rule reaches the same pose by the faithful route.
	bool StandRestPose()
	{
		bRestPoseHeld = false;
		if (!AnimatedVisual || !World)
		{
			return false;
		}
		IElysiumEmbodiment* Embodiment = World->Embodiment();
		if (!Embodiment)
		{
			return false;
		}
		// bLoop MUST stay false. The anim proxy's Request early-outs on
		// (Sequence == Playing && bLoop && bPlayingLoop) *without* restoring the play rate, so a
		// hold created as a loop would latch a later request out of ever un-freezing — and
		// `palmtree`'s rest clip and its `LoopSequence` are the same clip, so that path is live.
		if (!PlayAnimation(Embodiment->AnimatedPropRestClip(AnimatedStem), /*bLoop*/ false))
		{
			return false;
		}
		Embodiment->SeekCinematicClip(AnimatedVisual, 0.0f);   // start position 0, play rate 0
		AnimationEndTime = 0.0;   // a held pose never finishes, so it must not fire OnAnimationDone
		bRestPoseHeld = true;
		return true;
	}

	// The Activate-phase loop start: assign the authored sequence and let it run.
	bool StartLoopSequence()
	{
		bool bLoops = false;
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (Embodiment)
		{
			Embodiment->FindAnimatedPropClip(AnimatedStem, LoopSequence, bLoops);
		}
		return PlayAnimation(LoopSequence, bLoops);
	}

	void BuildBody(bool bFromSetModel)
	{
		if (CVarPropBodies.GetValueOnGameThread() == 0 || !World || !Def || Model.IsEmpty())
		{
			return;
		}
		IElysiumEmbodiment* Embodiment = World->Embodiment();
		if (!Embodiment)
		{
			return;
		}

		// The two representations do NOT share a rotation. `model_quat` is the placement of the
		// exporter's Unreal-native OBJ; a glTF body needs the fixed model-local correction on top
		// of it, because glTFRuntime imports mdl_gltf.py's standard Y-up glTF into its own basis
		// (ElysiumSkeletalBasis). Passing the static quat to the skeletal factory yaws the body 90
		// degrees — which on a cinematic prop, whose clip carries the whole scene motion relative
		// to its anchor, sweeps it through the wrong part of the room.
		FVector Loc;
		FQuat StaticRot, SkeletalRot;
		if (bFromSetModel)
		{
			// `Def->ModelQuat` belongs to the model this one replaced, so it cannot be reused. Both
			// representations fall back to the yaw-only runtime derivation, as the static path
			// already did.
			VisualStem = FPaths::GetBaseFilename(Model).ToLower();
			Loc = Origin;
			StaticRot = FQuat(FRotator(0.0f, -Angles.Y, 0.0f));
			SkeletalRot = FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles));
		}
		else
		{
			VisualStem = Def->ModelMesh;
			Loc = Def->Origin;
			StaticRot = Def->ModelQuat;
			// A model that decoded no static geometry carries no `model_quat`, so composing against
			// the identity default would silently drop the placement.
			SkeletalRot = Def->ModelMesh.IsEmpty()
				? FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles))
				: ElysiumSkeletalBasis::FromPlacementQuat(Def->ModelQuat);
		}

		AnimatedStem = Embodiment->AnimatedPropStemForModel(Model);
		// An indexed model that bakes no playable clip is not an animated representation — it is a
		// bind-pose skeleton standing where the baked static mesh should be. RestClip is empty
		// exactly when the entry carries no clips, so it is the same test.
		if (!AnimatedStem.IsEmpty() && Embodiment->AnimatedPropRestClip(AnimatedStem).IsEmpty())
		{
			UE_LOG(LogElysiumProp, Verbose,
				TEXT("%s: '%s' is indexed as animated but bakes no clip; standing the static mesh"),
				*DebugString(), *AnimatedStem);
			AnimatedStem.Reset();
		}
		if (!AnimatedStem.IsEmpty())
		{
			AnimatedVisual = Embodiment->BuildAnimatedPropVisual(AnimatedStem, Loc, SkeletalRot,
				Embodiment->BodyScaleFor(*Def));
			if (AnimatedVisual)
			{
				World->RegisterNpcBody(AnimatedVisual);
			}
		}
		else if (!VisualStem.IsEmpty())
		{
			Visual = Embodiment->BuildPropVisual(VisualStem, Loc, StaticRot, Embodiment->BodyScaleFor(*Def));
			if (Visual)
			{
				World->RegisterPropBody(Visual);
			}
		}

		if (Visual || AnimatedVisual)
		{
			if (Skin != 0) { ApplySkin(); }
			if (IsInert() || bBroken) { GateVisual(); }
		}
	}

	void GateVisual()
	{
		const bool bShown = !IsInert() && !bBroken;
		if (Visual)
		{
			Visual->SetVisibility(bShown);
		}
		if (AnimatedVisual)
		{
			AnimatedVisual->SetVisibility(bShown);
			AnimatedVisual->SetComponentTickEnabled(bShown);
		}
	}
};

// VtMB CPropButton (vampire.dll 0x10214d60; datamap builder 0x10214e30).
// State values are skin indices 0..max_states. Use fires OnPressed before cycling the state;
// SetState's external value is one-based and maps back to a zero-based skin/output index.
class FElysiumPropButton final : public FElysiumProp
{
public:
	bool bLocked = false;
	int32 CurrentState = 0;
	int32 MaxStates = 0;

	virtual void Spawn() override
	{
		FElysiumProp::Spawn();
		MaxStates = FMath::Clamp(MaxStates, 0, 7);
		CurrentState = FMath::Clamp(CurrentState, 0, MaxStates);
		SetSkin(CurrentState);
	}

	virtual bool IsUsable() const override { return true; }
	virtual bool IsUseLocked() const override { return bLocked; }
	virtual void Use(const FElysiumEntityHandle& Activator) override { ButtonUse(Activator); }

	void InputLock() { bLocked = true; }
	void InputUnlock() { bLocked = false; }
	void InputToggleLock() { bLocked = !bLocked; }
	void InputSetState(int32 ExternalState, const FElysiumEntityHandle& Activator)
	{
		const int32 LastSettable = FMath::Max(0, MaxStates - 1);
		SetButtonState(FMath::Clamp(ExternalState - 1, 0, LastSettable), Activator);
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		FElysiumProp::GetDebugState(Out);
		Out.Emplace(TEXT("Locked"), bLocked ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Button state"), FString::Printf(TEXT("%d / %d"), CurrentState, MaxStates));
	}

private:
	void ButtonUse(const FElysiumEntityHandle& Activator)
	{
		if (IsInert())
		{
			return;
		}
		if (bLocked)
		{
			static const FName OnPressedLocked(TEXT("OnPressedLocked"));
			FireOutput(OnPressedLocked, Activator);
			return;
		}

		static const FName OnPressed(TEXT("OnPressed"));
		FireOutput(OnPressed, Activator);
		SetButtonState(CurrentState >= MaxStates ? 0 : CurrentState + 1, Activator);
	}

	void SetButtonState(int32 NewState, const FElysiumEntityHandle& Activator)
	{
		CurrentState = FMath::Clamp(NewState, 0, MaxStates);
		const FName Output(*FString::Printf(TEXT("OnSetState%d"), CurrentState + 1));
		FireOutput(Output, Activator);
		SetSkin(CurrentState);
	}
};
// ============================================================================================
// FElysiumPhysProp — the `prop_physics` leaf: a Chaos rigid body. Stands the same decoded mesh as
// a dynamic prop but cooked with convex collision (the 8.4 `.hulls` decomposition) and simulating.
// Faithful I/O surface (RE'd from CPhysicsProp/CBreakableProp datamaps):
// `Wake` wakes the body; `Break` hides it + fires OnBreak (no gib system — the OnBreakLevel1..8 chain
// stays undriven); `Skin`/`SetSkin`/`FadeToSkin`/`SetSkinFadeTime` are skin stubs (skin 0 only
// exported, as 8.3). VtMB's prop has **no** EnableMotion/DisableMotion/Sleep — those don't exist here.
// ============================================================================================

class FElysiumPhysProp final : public FElysiumEntity
{
public:
	UStaticMeshComponent* Visual = nullptr;   // the simulating body, or null (gated off / decode failed)
	bool  bBroken = false;
	int32 Skin = 0;
	float SkinFadeTime = 0.0f;                // m_flSkinCrossfadeTime — stored, unread (skins snap)
	bool  bSimulating = false;                // elysium.PhysicsProps decided sim on at spawn

	virtual void Spawn() override
	{
		BuildBody();
	}

	// A constraint (phys_hinge) attaches to the simulating body, not the brush body.
	virtual UPrimitiveComponent* GetAttachBody() const override { return Visual; }

	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		GateBody();
	}

	// SetOrigin/SetAngles from a script teleport the body (no sweep — a simulating body is moved
	// directly, then physics resumes from the new pose).
	virtual void OnRuntimeTransformChanged() override
	{
		FElysiumEntity::OnRuntimeTransformChanged();
		if (Visual)
		{
			Visual->SetWorldLocationAndRotation(Origin, FQuat(FRotator(0.0f, -Angles.Y, 0.0f)), /*bSweep=*/false);
		}
	}

	virtual void OnRuntimeModelChanged() override
	{
		if (Visual)
		{
			Visual->DestroyComponent();
			Visual = nullptr;
		}
		BuildBody();
	}

	void InputWake(const FElysiumInputArgs&)
	{
		if (Visual && bSimulating)
		{
			Visual->WakeAllRigidBodies();
		}
	}

	// Break: drop the prop (hide + no collision + stop simulating) and fire OnBreak. Idempotent.
	// Source spawns gibs and fires the OnBreakLevel* chain here; no gib system exists, so the body
	// simply goes down (matches the 8.3 prop_dynamic Break).
	void InputBreak(const FElysiumInputArgs& Args)
	{
		if (bBroken)
		{
			return;
		}
		bBroken = true;
		GateBody();
		static const FName OnBreak(TEXT("OnBreak"));
		FireOutput(OnBreak, Args.Activator);
		UE_LOG(LogElysiumProp, Verbose, TEXT("%s Break"), *DebugString());
	}

	// Skin / SetSkin: snap to an alternate skin family (see FElysiumProp::InputSkin for the datamap
	// evidence that VtMB's `skin` input is a direct field write).
	void InputSkin(const FElysiumInputArgs& Args)
	{
		SetSkin(Args.Param.ToInt());
	}

	void SetSkin(int32 Family)
	{
		Skin = Family;
		ApplySkin();
	}

	void ApplySkin()
	{
		if (!Visual || !World || !Def)
		{
			return;
		}
		if (IElysiumEmbodiment* Embodiment = World->Embodiment())
		{
			Embodiment->ApplyPropSkin(Visual, Def->ModelMesh, Skin);
		}
	}

	// FadeToSkin: CBaseAnimating::FadeToSkin (vampire.dll @1008d6d0) sets m_nSkinCrossfade to the
	// old skin, m_nSkin to the new one, and leaves the blend to the client -- all three fields are
	// networked SendProps and the server writes no start time. We snap instead: no exported map
	// fires this input (18 skin wires across the 16 exported maps are all `Skin`, which snaps in
	// VtMB too), so the crossfade is engine code no map data reaches.
	void InputFadeToSkin(const FElysiumInputArgs& Args)
	{
		SetSkin(Args.Param.ToInt());
	}

	// SetSkinFadeTime: CBaseAnimating::SetSkinFadeTime (@1008d5f0) clamps to a floor and stores
	// m_flSkinCrossfadeTime, which only the crossfade reads. Stored for fidelity of the field, but
	// nothing consumes it while skin changes snap.
	void InputSetSkinFadeTime(const FElysiumInputArgs& Args)
	{
		SkinFadeTime = Args.Param.ToFloat();
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
		Out.Emplace(TEXT("Body"), Visual ? TEXT("physics mesh") : TEXT("(none)"));
		Out.Emplace(TEXT("Simulating"), bSimulating && !bBroken && !IsInert() ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Broken"), bBroken ? TEXT("yes") : TEXT("no"));
		// The mass Chaos is actually using, so the model's authored `.phy` mass can be checked
		// against the body rather than inferred from the asset.
		Out.Emplace(TEXT("Mass"), Visual
			? FString::Printf(TEXT("%.2f kg"), Visual->GetMass()) : TEXT("(none)"));
	}

private:
	void BuildBody()
	{
		if (CVarPropBodies.GetValueOnGameThread() == 0 || !World || !Def || Def->ModelMesh.IsEmpty())
		{
			return;   // gated off, bare test world, or a record with no decoded prop mesh
		}
		IElysiumEmbodiment* Embodiment = World->Embodiment();
		if (!Embodiment)
		{
			return;
		}
		Visual = Embodiment->BuildPhysPropVisual(Def->ModelMesh, Def->Origin, Def->ModelQuat,
			Embodiment->BodyScaleFor(*Def));
		if (!Visual)
		{
			return;
		}
		World->RegisterPropBody(Visual);
		if (Skin != 0)
		{
			Embodiment->ApplyPropSkin(Visual, Def->ModelMesh, Skin);   // authored on an alternate family
		}

		// A model with no collision model cannot simulate, and VtMB does not remove the entity over
		// it: CPhysicsProp::CreateVPhysics (vampire.dll @10191510) warns, drops the prop to
		// SOLID_NONE + MOVETYPE_NONE and returns true, leaving it standing as inert scenery. The
		// baked mesh carries that fact — no `.phy` meant no simple collision shapes — so read it off
		// the asset rather than tracking a second flag.
		if (!HasSimpleCollision(Visual))
		{
			UE_LOG(LogElysiumProp, Log,
				TEXT("%s model '%s' has no collision model — standing inert (VtMB parity)"),
				*DebugString(), *Def->ModelMesh);
			Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			bSimulating = false;
		}
		// Simulate (or stand static under the A/B toggle). Mass is the model's authored `.phy` value
		// (baked onto the mesh) unless the entity's own override_mass > 0, which outranks it —
		// Source's precedence. override_mass is -1 on every prop_physics in the exported maps, so
		// the authored mass is what nearly all of them weigh.
		else
		{
			bSimulating = CVarPhysicsProps.GetValueOnGameThread() != 0;
			if (bSimulating)
			{
				float Mass = FCString::Atof(*Def->Keys.FindRef(TEXT("override_mass")));
				if (Mass <= 0.0f)
				{
					Mass = AuthoredMassKg(Visual);
				}
				if (Mass > 0.0f)
				{
					Visual->SetMassOverrideInKg(NAME_None, Mass, true);
				}
				Visual->SetSimulatePhysics(true);
			}
			else
			{
				Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // static, non-solid parity
			}
		}

		if (IsInert() || bBroken)
		{
			GateBody();   // born hidden (start_hidden / a Spawn()-time Kill)
		}
	}

	// Hidden/broken → undrawn, non-colliding, not simulating. Live → restore draw + (if enabled)
	// simulation. Mirrors the whole-entity dormancy switch onto the physics body.
	void GateBody()
	{
		if (!Visual)
		{
			return;
		}
		const bool bDown = IsInert() || bBroken;
		Visual->SetVisibility(!bDown);
		if (bDown)
		{
			Visual->SetSimulatePhysics(false);
			Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		else if (bSimulating)
		{
			Visual->SetCollisionProfileName(TEXT("PhysicsActor"));
			Visual->SetSimulatePhysics(true);
		}
	}
};

// ============================================================================================
// FElysiumPhysHinge — the `phys_hinge` leaf: a Chaos hinge constraint (one free rotational DOF)
// between attach1's body and attach2's (or the world). Bodiless. RE'd from CPhysHinge/CPhysConstraint
// Fields attach1/attach2/forcelimit/torquelimit/hingefriction/hingeaxis;
// inputs TurnOn/TurnOff/Break; output OnBreak. The axis is the exporter's pre-converted Def->HingeAxis.
// ============================================================================================

class FElysiumPhysHinge final : public FElysiumEntity
{
public:
	UPhysicsConstraintComponent* Constraint = nullptr;

	// Second-phase init (both attached bodies must already exist — see FElysiumEntity::PostSpawn).
	virtual void PostSpawn() override
	{
		if (!World || !Def)
		{
			return;
		}
		// The constraint is a component like every other body: outer'd to the world's owner actor
		// (11.2 — the actor is the component outer, not a service).
		AActor* Outer = World->GetOwnerActor();
		USceneComponent* Root = Outer ? Outer->GetRootComponent() : nullptr;
		if (!Root)
		{
			return;
		}

		UPrimitiveComponent* Body1 = ResolveBody(TEXT("attach1"));
		UPrimitiveComponent* Body2 = ResolveBody(TEXT("attach2"));
		if (!Body1 && !Body2)
		{
			UE_LOG(LogElysiumProp, Log, TEXT("%s: no attach body resolved — constraint skipped"), *DebugString());
			return;
		}

		FVector Axis = Def->HingeAxis;
		if (!Axis.Normalize())
		{
			Axis = FVector::UpVector;   // degenerate / absent axis → world Z
		}

		Constraint = NewObject<UPhysicsConstraintComponent>(Outer);
		Constraint->SetupAttachment(Root);
		// The constraint's local +X is the twist axis; orient the frame so it lies along the hinge axis.
		// The map actor sits at world origin, so relative == world for these Unreal-space values.
		Constraint->SetRelativeLocationAndRotation(Def->Origin, FRotationMatrix::MakeFromX(Axis).ToQuat());
		Constraint->RegisterComponent();
		Outer->AddInstanceComponent(Constraint);

		ConfigureAsHinge();
		Constraint->SetConstrainedComponents(Body1, NAME_None, Body2, NAME_None);
		World->RegisterConstraintBody(Constraint);
		UE_LOG(LogElysiumProp, Verbose, TEXT("%s hinge: %s <-> %s"), *DebugString(),
			Body1 ? TEXT("attach1") : TEXT("world"), Body2 ? TEXT("attach2") : TEXT("world"));
	}

	// TurnOff/TurnOn enable-disable the constraint; Break is permanent + fires OnBreak.
	void InputTurnOff(const FElysiumInputArgs&) { if (Constraint) { Constraint->BreakConstraint(); } }
	void InputTurnOn(const FElysiumInputArgs&)  { ReinitConstraint(); }

	void InputBreak(const FElysiumInputArgs& Args)
	{
		if (Constraint)
		{
			Constraint->BreakConstraint();
		}
		static const FName OnBreak(TEXT("OnBreak"));
		FireOutput(OnBreak, Args.Activator);
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("attach1"), Def ? Def->Keys.FindRef(TEXT("attach1")) : FString());
		Out.Emplace(TEXT("attach2"), Def ? Def->Keys.FindRef(TEXT("attach2")) : FString());
		Out.Emplace(TEXT("Axis"), Def ? Def->HingeAxis.ToString() : FString());
		Out.Emplace(TEXT("Constraint"), Constraint ? TEXT("live") : TEXT("(none)"));
	}

private:
	UPrimitiveComponent* ResolveBody(const TCHAR* Key)
	{
		const FString Name = Def->Keys.FindRef(Key);
		if (Name.IsEmpty())
		{
			return nullptr;   // empty attach → the world frame
		}
		FElysiumEntity* Found = World->FindByName(Name);
		return Found ? Found->GetAttachBody() : nullptr;
	}

	void ConfigureAsHinge()
	{
		// One free rotational DOF about the twist axis; everything else locked.
		Constraint->SetLinearXLimit(LCM_Locked, 0.0f);
		Constraint->SetLinearYLimit(LCM_Locked, 0.0f);
		Constraint->SetLinearZLimit(LCM_Locked, 0.0f);
		Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
		Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
		Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Free, 0.0f);

		// forcelimit/torquelimit = 0 → unbreakable (Source semantics); > 0 → break threshold.
		const float ForceLimit = FCString::Atof(*Def->Keys.FindRef(TEXT("forcelimit")));
		const float TorqueLimit = FCString::Atof(*Def->Keys.FindRef(TEXT("torquelimit")));
		if (ForceLimit > 0.0f)
		{
			Constraint->SetLinearBreakable(true, ForceLimit);
		}
		if (TorqueLimit > 0.0f)
		{
			Constraint->SetAngularBreakable(true, TorqueLimit);
		}

		// hingefriction → resist rotation via a zero-velocity twist drive damped by the friction.
		const float HingeFric = FCString::Atof(*Def->Keys.FindRef(TEXT("hingefriction")));
		if (HingeFric > 0.0f)
		{
			Constraint->SetAngularVelocityDriveTwistAndSwing(true, false);
			Constraint->SetAngularDriveParams(0.0f, HingeFric, 0.0f);
		}
	}

	// Re-wire a TurnOff'd (broken) constraint from the stored attach targets.
	void ReinitConstraint()
	{
		if (!Constraint)
		{
			return;
		}
		UPrimitiveComponent* Body1 = ResolveBody(TEXT("attach1"));
		UPrimitiveComponent* Body2 = ResolveBody(TEXT("attach2"));
		ConfigureAsHinge();
		Constraint->SetConstrainedComponents(Body1, NAME_None, Body2, NAME_None);
	}
};

// --- Registration -----------------------------------------------------------------------------

static TUniquePtr<FElysiumEntity> MakeProp() { return MakeUnique<FElysiumProp>(); }
static TUniquePtr<FElysiumEntity> MakePropButton() { return MakeUnique<FElysiumPropButton>(); }
static TUniquePtr<FElysiumEntity> MakePhysProp() { return MakeUnique<FElysiumPhysProp>(); }
static TUniquePtr<FElysiumEntity> MakePhysHinge() { return MakeUnique<FElysiumPhysHinge>(); }

static void BuildPropClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Break"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputBreak(Args); });
	D.Input(TEXT("Skin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputSkin(Args); });
	D.Input(TEXT("SetAnimation"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputSetAnimation(Args); });

	// `skin` keyfield / script `.skin`: a write repaints the body, mirroring VtMB, where the input
	// and the keyfield are the *same* datamap record and both just write m_nSkin.
	AddPropSkinField<FElysiumProp>(D);

	// CDynamicProp's own animation keyfields. `demo_sequence` is deliberately absent: it is a
	// Hammer/FGD field the engine never reads.
	AddPropField(D, TEXT("LoopSequence"), &FElysiumProp::LoopSequence);
	AddPropField(D, TEXT("RandomAnimation"), &FElysiumProp::bRandomAnimator);
	AddPropField(D, TEXT("MinAnimTime"), &FElysiumProp::MinAnimTime);
	AddPropField(D, TEXT("MaxAnimTime"), &FElysiumProp::MaxAnimTime);
}

// The static-mesh prop classes that carry a model and a skin but whose interaction surface is not
// built yet (the `+use` family, roadmap 4.10/8.8). They stand the same body and honour the same
// `skin` keyfield -- 25 of the tutorial's 39 multi-family prop placements are these, and without a
// body they were inert records with nothing to draw. Deliberately no inputs: their real datamap I/O
// is not RE'd, and asserting prop_dynamic's here would advertise inputs they may not have.
static void BuildPropBodyClass(FElysiumClassDesc& D)
{
	AddPropSkinField<FElysiumProp>(D);
}

static void BuildPropButtonClass(FElysiumClassDesc& D)
{
	BuildPropBodyClass(D);
	D.Input(TEXT("Use"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
		{ static_cast<FElysiumPropButton&>(E).Use(A.Activator); });
	D.Input(TEXT("Lock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
		{ static_cast<FElysiumPropButton&>(E).InputLock(); });
	D.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
		{ static_cast<FElysiumPropButton&>(E).InputUnlock(); });
	D.Input(TEXT("ToggleLock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
		{ static_cast<FElysiumPropButton&>(E).InputToggleLock(); });
	D.Input(TEXT("SetState"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
		{ static_cast<FElysiumPropButton&>(E).InputSetState(A.Param.ToInt(), A.Activator); });
	AddPropField(D, TEXT("locked"), &FElysiumPropButton::bLocked);
	AddPropField(D, TEXT("current_state"), &FElysiumPropButton::CurrentState);
	AddPropField(D, TEXT("max_states"), &FElysiumPropButton::MaxStates);
}

// prop_physics (8.4): the RE'd CPhysicsProp/CBreakableProp input surface. Wake + Break are real;
// the skin inputs are stubs (skin 0 only exported); no EnableMotion/DisableMotion/Sleep exist in VtMB.
static void BuildPhysPropClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Wake"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputWake(Args); });
	D.Input(TEXT("Break"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputBreak(Args); });
	D.Input(TEXT("Skin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputSkin(Args); });
	D.Input(TEXT("SetSkin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputSkin(Args); });
	D.Input(TEXT("FadeToSkin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputFadeToSkin(Args); });
	D.Input(TEXT("SetSkinFadeTime"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputSetSkinFadeTime(Args); });

	AddPropSkinField<FElysiumPhysProp>(D);
}

// phys_hinge (8.4): the RE'd CPhysHinge/CPhysConstraint input surface.
static void BuildPhysHingeClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysHinge&>(E).InputTurnOn(Args); });
	D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysHinge&>(E).InputTurnOff(Args); });
	D.Input(TEXT("Break"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysHinge&>(E).InputBreak(Args); });
}

// One shared leaf per classname (class-for-class registration, so the registry's exact case-folded
// Find resolves each).
struct FElysiumPropRegistrar
{
	FElysiumPropRegistrar()
	{
		FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		static const TCHAR* const PropClasses[] = {
			TEXT("prop_dynamic"), TEXT("prop_dynamic_ornament"),
		};
		for (const TCHAR* Name : PropClasses)
		{
			BuildPropClass(Reg.Register(FName(Name), ElysiumBaseClassName(), &MakeProp));
		}
		// The `+use` static-mesh family: a body and its skin, no interaction surface (4.10/8.8).
		// The exporter already decodes their models (8.1), so without this they were logic-valid
		// but invisible records.
		static const TCHAR* const PropBodyClasses[] = {
			TEXT("prop_switch"), TEXT("prop_sign"), TEXT("prop_hacking"),
			TEXT("prop_doorknob"), TEXT("prop_doorknob_electronic"),
			TEXT("item_container"), TEXT("item_container_animated"), TEXT("item_container_lock"),
		};
		for (const TCHAR* Name : PropBodyClasses)
		{
			BuildPropBodyClass(Reg.Register(FName(Name), ElysiumBaseClassName(), &MakeProp));
		}
		BuildPropButtonClass(Reg.Register(
			FName(TEXT("prop_button")), ElysiumBaseClassName(), &MakePropButton));
		BuildPhysPropClass(Reg.Register(FName(TEXT("prop_physics")), ElysiumBaseClassName(), &MakePhysProp));
		BuildPhysHingeClass(Reg.Register(FName(TEXT("phys_hinge")), ElysiumBaseClassName(), &MakePhysHinge));
	}
};

static FElysiumPropRegistrar GElysiumPropRegistrar;
