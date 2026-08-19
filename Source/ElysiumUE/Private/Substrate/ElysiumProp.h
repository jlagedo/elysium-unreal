#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"
#include "Logging/LogMacros.h"

class UBoxComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FElysiumSaveArchive;

// The one log category the prop family writes on. It is declared here rather than defined
// per-file because the family is split across `ElysiumProp`, `ElysiumPropLeaves` and
// `ElysiumPhysProp`, and one category is what makes a single `LogElysiumProp` filter show a
// whole prop's story. `ElysiumProp.cpp` owns the definition.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumProp, Log, All);

// The `elysium.PropBodies` A/B toggle's current value. The cvar is defined once in
// `ElysiumProp.cpp`; `prop_physics` gates its own body build on the same switch, so the read
// is exposed here rather than the variable.
bool ElysiumPropBodiesEnabled();

// ============================================================================================
// FElysiumProp — the AI-free static-mesh leaf shared by prop_dynamic / prop_dynamic_ornament. It
// stands a decoded prop mesh at its placement and exposes the dynamic-prop inputs over it.
// ============================================================================================

class FElysiumProp : public FElysiumEntity
{
public:
	int32 Skin = 0;

	// CDynamicProp's animation keyfields (datamap `0x1058d4f8`, 12 records at `0x1058d53c`). There is
	// no `demo_sequence` in the engine — the name appears nowhere in `vampire.dll`, so it is a
	// Hammer/FGD-only field and `LoopSequence` is the only authored default.
	FString LoopSequence;               // LoopSequence -> m_iszSequenceName @0x7d4
	bool    bRandomAnimator = false;    // RandomAnimation -> m_bRandomAnimator @0x7c4
	float   MinAnimTime = 0.0f;         // MinAnimTime -> m_flMinRandAnimTime @0x7cc
	float   MaxAnimTime = 0.0f;         // MaxAnimTime -> m_flMaxRandAnimTime @0x7d0

	void InputBreak(const FElysiumInputArgs& Args);
	void InputSkin(const FElysiumInputArgs& Args);
	void SetSkin(int32 Family);
	void InputSetAnimation(const FElysiumInputArgs& Args);

protected:
	virtual void Spawn() override;
	virtual void Think() override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void OnDormancyChanged() override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

	double AnimationEndTime = 0.0;      // absolute seconds the current one-shot ends; 0 = none pending

private:
	virtual bool PreloadAnimClip(const FString& ClipName) override;
	virtual void PreloadForActivation() override;
	virtual void Activate() override;
	virtual UPrimitiveComponent* GetAttachBody() const override;
	virtual void OnRuntimeTransformChanged() override;
	virtual void OnRuntimeModelChanged() override;

	void ApplySkin();

	static bool MeaningfulSequence(const FString& Sequence);

	// CDynamicPropAnimThink's re-arm interval (`_DAT_104493d0`).
	static constexpr double AnimThinkInterval = 0.1;

	// How far a clip may sit from its substrate-clock phase before the think corrects it. Two frames
	// at 60 Hz, and it has to be at least one: the body's component ticks in TG_PrePhysics, the same
	// group as the gameplay tick that runs this think and with no prerequisite between them, so the
	// position read here is either current or exactly one frame stale and which one is not
	// deterministic. A tolerance under one frame would therefore snap on every think — a 10 Hz
	// stutter, strictly worse than the drift it was correcting.
	//
	// A false trigger costs nothing: the correction applied is one frame of animation, the same step
	// the frame was about to take. That is also why it does not scale with frame time.
	//
	// If `bEnableUpdateRateOptimizations` is ever turned on for these bodies, the accumulator will
	// lag by the URO interval instead and this will fight it at 10 Hz — revisit the number then.
	static constexpr double AnimResyncTolerance = 0.033;

	bool SequenceFinished(double Now) const;
	double DrawRandomAnimInterval() const;
	static double DrawLoopStartDelay();
	bool PlayRandomAnimation();
	void DestroyBody();
	bool PlayAnimation(const FString& Clip, bool bLoop);
	void ResyncAnimation(double Now);
	bool StandRestPose();
	bool StartLoopSequence();
	void BuildBody(bool bFromSetModel);
	void BuildBoxCollisionProxy(UPrimitiveComponent* Mesh);
	void GateVisual();

	// Exactly one representation is live. Ordinary props retain the baked static mesh; a model in
	// npc_index v4's animated_props section stands a skeletal glTF component instead.
	UStaticMeshComponent* Visual = nullptr;
	USkeletalMeshComponent* AnimatedVisual = nullptr;
	FString AnimatedStem;
	FString VisualStem;          // baked/static skin-table stem for either representation

	// `solid`'s static collision body. `CollisionProxy` is the catalogue path's invisible
	// `.phy`-hulled proxy (`AnimatedVisual` is attached to it, mirrors FElysiumPhysProp's Visual/
	// PosedVisual pair but inverted: here the proxy, not the drawn mesh, is the parent); a plain
	// `solid`-driven collision profile on `Visual` itself covers the fallback (non-catalogue) path.
	// `BoxCollisionProxy` backs `solid 2` (SOLID_BBOX) on either path, since neither builder bakes a
	// box shape onto the mesh asset the way `.phy` hulls are baked for the other solid values.
	UStaticMeshComponent* CollisionProxy = nullptr;
	UBoxComponent* BoxCollisionProxy = nullptr;
	// The resolved `solid` keyfield, kept so GateVisual can tell whether the fallback-path `Visual`'s
	// collision is solid-driven (and must be regated with it) or was always NoCollision (solid 0).
	int32 SolidValue = 0;
	FString CurrentAnimation;
	bool bAnimationLoop = false;
	bool bBroken = false;

	// Derived. Retail carries a resolved sequence *index* (-1 when LoopSequence names nothing); the
	// animated-prop index exposes clip *names*, so the equivalent test is "it resolved to a real clip".
	bool   bLoopSequenceResolved = false;
	// The body is parked on a held pose — a clip seeked to frame 0 with the play rate at zero —
	// rather than playing. Retail's resting state (CBaseProp::Spawn); carried in the save so a
	// restore re-holds instead of starting the clip running.
	bool   bRestPoseHeld = false;
	// CDynamicProp::Activate armed the loop start and the think has not consumed it yet. Not saved:
	// a restored map re-runs Load -> Activate and re-arms, which is what retail's Activate does.
	bool   bLoopStartPending = false;
	double NextRandAnim = 0.0;          // m_flNextRandAnim @0x7c8

	// Absolute game seconds the current clip's frame 0 played at — retail's `m_flAnimTime`, which
	// `StudioFrameAdvance` measures every advance against so the cycle telescopes and a 10 Hz think
	// cannot drift from a per-frame one. The clip itself free-runs on engine time; this is what the
	// think measures that free run against. NOT saved: Serialize's replay restarts the clip, which
	// re-derives this exactly as it re-derives AnimationEndTime.
	double AnimStartTime = 0.0;
	// The clip's authored length, kept for loops too (unlike AnimationEndTime) so a looping phase
	// wraps in double at the source rather than narrowing an unbounded elapsed time to float.
	double AnimLengthSeconds = 0.0;
	// Drift telemetry for the inspector. `resyncs 0` beside a visibly wrong prop says the phase is
	// right and the clip ORIGIN is wrong, which is a different bug.
	int32  ResyncCount = 0;
	double LastResyncDrift = 0.0;
};
