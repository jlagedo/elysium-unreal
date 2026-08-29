#pragma once

#include "Camera/PlayerCameraManager.h"
#include "CoreMinimal.h"
#include "ElysiumCameraRig.h"
#include "ElysiumCameraSolve.h"

#include "ElysiumPlayerCameraManager.generated.h"

class UElysiumCameraComponent;

// The frame's settled camera state, published at the tail of the view update.
//
// It exists because the two consumers that need camera values run at different points in the engine
// frame: the view update happens inside `UWorld::Tick`, while the `-ElysiumMove` harness samples
// from the core ticker afterwards. Handing the harness a stamped copy rather than letting it reach
// into the rig is what makes a recorded value settled **by construction** — and the stamp is what
// lets a sampler tell a genuinely still camera from a frame where the view never updated at all
// (a paused world, a body not yet possessed).
struct FElysiumCameraSample
{
	// `GFrameCounter` when this was published. Zero means the camera has never solved.
	uint64 Frame = 0;

	// The boom, in cm, with the third-person weight already applied — zero in true first person.
	float BoomLength = 0.0f;
	// The damper's result before the weight, so a recording separates "the boom is short" from
	// "the camera is barely in third person".
	float DamperDistance = 0.0f;
	// The boom's own angles.
	float BoomPitch = 0.0f;
	float BoomYaw = 0.0f;
	// Whether the collision sweep hit this frame. A state flip, never a rounding difference.
	bool bClipped = false;

	// The weights the whole rig is a function of.
	float ThirdWeight = 0.0f;
	float ScriptedWeight = 0.0f;

	// The frame's resolved switch-frame table. Published here so a Cog readout, `elysium_player_get`
	// and a channel recording all read the same one answer and cannot disagree about a mode switch.
	FElysiumCameraDrawPolicy Draw;
};

// The one final view per local player (`docs/architecture/camera-architecture.md` -> rule 1).
//
// The seam is **`UpdateViewTargetInternal`**, not `UpdateViewTarget`. The outer function owns four
// branches that must keep working and that this class has no business reimplementing: the
// `ACameraActor` view target (which character generation uses), the stock debug `CameraStyle`
// modes, the per-frame POV reset, and the modifier pass. The inner one is exactly the
// `Target->CalcCamera` dispatch — which is the only thing being replaced.
//
// The rig is resolved from the **live** view target every frame and never cached: character
// generation, a cutscene and a spectator each retarget the view, so a pointer taken at `BeginPlay`
// would be the wrong body's camera.
UCLASS()
class AElysiumPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	AElysiumPlayerCameraManager();

	// The frame's settled camera state. `Frame` is the caller's staleness check.
	const FElysiumCameraSample& GetCameraSample() const { return Sample; }

	// The player body's camera, or null when the view target is not a player body — a scripted
	// `ACameraActor`, the debug camera, a spectator. A null answer is a fall-through to the stock
	// dispatch, never an error. Public because the post layers resolve the same rig.
	static UElysiumCameraComponent* ResolveRig(AActor* Target);

	// The post layers in application order — priority 0 first. The base class keeps its list
	// protected, and the Cog window draws the same rows `showdebug camera` prints.
	const TArray<TObjectPtr<UCameraModifier>>& GetModifiers() const { return ModifierList; }

	virtual void DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay,
		float& YL, float& YPos) override;

protected:
	virtual void UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime) override;

private:
	// One frame of the modern rig. It lives on the manager rather than as a second component so its
	// state is resolved once from whatever is being viewed, rather than added to each body that can
	// be a view target and kept in step.
	void SolveModernRig(UElysiumCameraComponent& Camera, float DeltaSeconds);

	void PublishSample(const UElysiumCameraComponent& Camera);

	FElysiumCameraSample Sample;

	// The modern rig's tuning and its integrator state. The tuning is code defaults. It is
	// deliberately **not** the VtMB console store, which tunes the faithful evaluator alone — that
	// partition is what stops one value having two owners while both rigs are live.
	ElysiumRig::FElysiumCameraRigTuning RigTuning;
	// The damped pivot the boom hangs off. The damper lives here rather than on the camera position
	// so it covers the pivot's own motion — stairs, crouch, gait bob — without also lagging the
	// rotation, which is instant and must stay that way.
	FVector ModernPivot = FVector::ZeroVector;
	FVector ModernPosition = FVector::ZeroVector;
	FRotator ModernAngles = FRotator::ZeroRotator;
	float ModernDistance = 0.0f;
	bool bModernClipped = false;
	// The player's hand-orbit. Rig state like the pivot and the length, so it lives here rather than
	// on the component; the verbs latch a restore request on the component and this consumes it.
	ElysiumRig::FElysiumOrbitState ModernOrbit;
	// Snap rather than ease on the next solve — the same re-seed the faithful damper takes, so a
	// teleport does not record the spring flying in from where the body used to be.
	bool bModernNeedsReseed = true;
	uint64 ModernSolvedFrame = TNumericLimits<uint64>::Max();
};
