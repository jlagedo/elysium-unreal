#pragma once

#include "Camera/CameraComponent.h"
#include "CoreMinimal.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumCommands.h"
#include "ElysiumUserCmd.h"

#include "ElysiumCameraComponent.generated.h"

class UTexture2D;

// The player camera (roadmap 11.7). Design + the recovered solve: `docs/vtmb/camera-view-modes.md`;
// where it sits in the spine: `docs/architecture/runtime-architecture.md` §9.
//
// It is a `UCameraComponent` subclass rather than a state object beside one, because VtMB has
// **one** camera: the FOV, the post-process settings and the first-person-rendering flags all stay
// where the engine expects them, and what this adds is the weight stack and the boom solve on top.
// `AElysiumPawn::CalcCamera` is the single apply point — the structural analogue of
// `CAM_ApplyToView` — and it delegates to `UCameraComponent::GetCameraView` *first*, because
// overriding `CalcCamera` without doing so is the documented cause of first-person rendering
// silently not applying.
//
// Three things it deliberately does not do:
//
//   * **it does not tick.** `UpdateCamera` runs from `CalcCamera`, once per frame (guarded on the
//     frame counter), which is where VtMB runs `CAM_Think` too. A component tick would solve the
//     boom before the pawn has moved and leave the camera a frame behind;
//   * **it does not poll a key.** The orbit and dolly pairs arrive as latches in the frame's
//     `FElysiumUserCmd` (S5), like everything else;
//   * **it does not know what an entity is.** A scripted shot is pushed as *values* and whoever
//     pushed it keeps them current, so a `Follow` attach type is the pusher re-resolving each frame.
// What the scripted channel resolved to this frame: the pose the top shot has chased to, its field
// of view, and the stack's own timed weight. It is published as values because the layer that
// composes it runs later in the frame than the solve that produced it — and because the same values
// have to compose over either rig once `elysium.ModernCamera` has a second one to choose.
struct FElysiumScriptedShotView
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	// 0 keeps the player's field of view, which is what a shot file with no `FieldOfView` authors.
	float FieldOfView = 0.0f;
	// The shot stack's timed ramp. This is the layer's alpha; it is never re-eased.
	float Weight = 0.0f;
	// False until the channel has framed its first shot, which is what stops a push being applied
	// from wherever the camera happened to be.
	bool bSeeded = false;
};

UCLASS()
class UElysiumCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:
	UElysiumCameraComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// --- The apply point --------------------------------------------------------------------
	// Advance the weights and re-solve the boom for this frame. Idempotent within a frame, so a
	// second `CalcCamera` (a spectator, a scene capture) reads the same view rather than blending
	// twice as fast.
	void UpdateCamera(float DeltaSeconds);

	// `CAM_ApplyToView` (`0x100ffb00`): the boom offset and the angle lerp, then the scripted shot on
	// top. The composed whole, for the fallback path — the production path applies the two halves at
	// their own points in the frame, because the scripted layer runs after the base is chosen.
	void ApplyToView(FMinimalViewInfo& View) const;

	// The base half: the strafe bank and the boom, at the third-person weight. This is what a base
	// request produces; nothing scripted is in it.
	void ApplyBaseToView(FMinimalViewInfo& View) const;

	// The scripted half (`ApplyScriptedBlend`): composed **over** whatever base won, at the shot
	// stack's own weight, plus the motion-blur suppression an authored edit needs. Retail composes
	// rather than arbitrating — VtMB has one camera and this is the last term applied to it.
	void ApplyScriptedShotToView(FMinimalViewInfo& View) const;

	// What the scripted layer reads, as values.
	FElysiumScriptedShotView ScriptedShotView() const;

	// What the pawn's `CalcCamera` override calls: `GetCameraView` first, then `UpdateCamera` +
	// `ApplyToView` + the cut. The manager does not go through this — it needs the halves apart —
	// so this is the fallback a spectator, a scene capture and `UGameplayStatics` reach.
	static bool CalcCameraFor(UElysiumCameraComponent* Camera, float DeltaSeconds, FMinimalViewInfo& Out);

	// The same, stopping at the base: `SolveFrameFor` then `ApplyBaseToView`. The scripted layer and
	// the temporal cut are the modifier's, because both have to happen after the base request has
	// been chosen and applied.
	static bool CalcCameraBaseFor(UElysiumCameraComponent* Camera, float DeltaSeconds, FMinimalViewInfo& Out);

	// `GetCameraView` and the faithful solve, applying nothing. The manager needs the solve and the
	// apply apart, because both rigs evaluate every frame and only one of them supplies the base.
	static bool SolveFrameFor(UElysiumCameraComponent* Camera, float DeltaSeconds, FMinimalViewInfo& Out);

	// The frame's user command, for whoever solves the boom.
	const FElysiumUserCmd& GetUserCmd() const { return PendingCmd; }

	// The frame's boom, pushed in by whoever solved it (`AElysiumPlayerCameraManager`). The component
	// owns the weights, the shot stack and the fade band; the rig owns where the camera is. Keeping
	// the solve outside is what leaves exactly one boom in the frame.
	void SetSolvedBoom(const FVector& Offset, const FRotator& Angles, bool bInClipped)
	{
		SolvedOffset = Offset;
		SolvedAngles = Angles;
		bClipped = bInClipped;
	}

	// A cut request is latched when the entity world publishes it and consumed from CalcCamera, after
	// the shot has been applied. Keeping it until that phase prevents an earlier engine camera update
	// from clearing the one-frame temporal-history signal before the viewport builds its view.
	bool ConsumeTemporalCameraCutRequest();

	// --- Intent -----------------------------------------------------------------------------
	// The frame's user command, handed on by the body. Only the camera pairs are read here.
	void SetUserCmd(const FElysiumUserCmd& Cmd) { PendingCmd = Cmd; }

	// --- The mode ---------------------------------------------------------------------------
	// `CAM_ToThirdPerson` / `CAM_ToFirstPerson` (`0x100ff7c0` / `0x100ff7e0`) — the minimal pair: set
	// the latch, clear `cam_command`. Neither runs the weapon arbitration or the holster check; only
	// `togglecamera` does (4.9 owns that half, since it needs weapons).
	void SetThirdPerson(bool bThird);
	void ToggleCamera();
	bool IsThirdPerson() const { return Weights.IsThirdPerson(); }
	float ThirdPersonWeight() const { return Weights.Third; }
	const FElysiumCameraWeights& GetWeights() const { return Weights; }

	// Forced-third / forced-first, the latches weapon-class arbitration writes (`+0xf8` / `+0xf9`).
	void SetForcedThird(bool bForced) { Weights.bForcedThird = bForced; }
	void SetForcedFirst(bool bForced) { Weights.bForcedFirst = bForced; }
	// The feed / seduction / death camera's hold. Ordinary feeding has a recovered pose solver;
	// seduction and death currently use only the shared weight/third-person channel.
	void SetFeedCamera(bool bActive) { Weights.bFeed = bActive; }

	// Drop the smoothing so the next solve snaps rather than eases — VtMB's re-seed flag (`+0x4`),
	// which is what makes re-entering third person not swing in from wherever the camera last was.
	// Latched here and consumed by whoever solves the boom.
	void RequestReseed() { bReseedRequested = true; }
	bool ConsumeReseedRequest()
	{
		const bool bWas = bReseedRequested;
		bReseedRequested = false;
		return bWas;
	}

	// --- The scripted-shot channel ----------------------------------------------------------
	// `SetCamera`, `camera_keyframe`, the conversation camera and the feed camera all arrive here.
	// Returns the shot's id (never reused, 0 on failure); `PopShot` gives control back.
	int32 PushShot(const FElysiumCameraShot& Shot);
	bool UpdateShot(int32 Id, const FElysiumCameraShot& Shot);
	bool PopShot(int32 Id, float BlendOutSeconds = -1.0f);
	void ClearShots() { Shots.Clear(); }
	const FElysiumCameraShotStack& GetShots() const { return Shots; }

	// --- Debug ------------------------------------------------------------------------------
	// The solved boom length in cm (0 in first person), for `elysium_player_get` and the Cog window.
	float BoomLength() const { return SolvedOffset.Size() * Weights.ThirdBlend(); }
	// The damper's own result, before the weight is applied — what the boom solve left behind
	// (`m_vecCameraOffset` / `m_vecCameraAngles`), and whether the sweep hit this frame. The channel
	// recorder reads these rather than re-deriving them, so a recording cannot disagree with the
	// solve it came from.
	const FVector& SolvedBoomOffset() const { return SolvedOffset; }
	const FRotator& SolvedBoomAngles() const { return SolvedAngles; }
	bool IsBoomClipped() const { return bClipped; }
	// The player-model alpha the fade band produces (`CInput+0x104`). Nothing reads it until a player
	// mesh exists (8.11); it is solved now so the band is one number rather than a later guess.
	float ModelAlpha() const { return PlayerModelAlpha; }
	const FElysiumCameraCvars& GetCvars() const { return Cvars; }
	FString Describe() const;

private:
	// The camera verbs, installed while this component is alive: `togglecamera`, `thirdperson`,
	// `firstperson`, `snapto`, `cam_command`, `centerview`, `force_centerview`. Implementations stack
	// (11.6), so a fresh pawn's camera takes the verbs over and hands them back when it dies.
	void RegisterCommands();
	void UnregisterCommands();

	// Consume `cam_command` (`CAM_Think` step 1): 1 -> third person, 2 -> first, 0 -> nothing. It is
	// both a cvar and a bindable verb, and both write the same one-shot request.
	void ConsumeCamCommand();

	// The scripted channel's own view: where the top shot has reached, rate-limited by the shot
	// file's `MoveSpeed` / `MaxTurnRate` once it is tracking.
	void SolveShot(float Dt);

	// The player-model fade ramp (`CAM_Think` tail).
	void SolveModelAlpha();
	void EnsureFeedVisionMask();

	// The eye the boom hangs off: this component's world location, which is where the first-person
	// view already is.
	FVector EyeLocation() const { return GetComponentLocation(); }
	// The view the camera derives from — the controller's, so the player keeps authority over the
	// angles the whole time.
	FRotator ViewRotation() const;

	FElysiumCameraWeights Weights;
	FElysiumCameraShotStack Shots;
	FElysiumCameraCvars Cvars;

	FElysiumUserCmd PendingCmd;

	// The frame's boom, at FULL weight; the blend is applied at the point of use so the solve does
	// not depend on it. Written by `SetSolvedBoom`, never solved here.
	FVector SolvedOffset = FVector::ZeroVector;
	FRotator SolvedAngles = FRotator::ZeroRotator;
	bool bClipped = false;
	bool bReseedRequested = true;

	// Where the scripted channel's view has reached, so `MoveSpeed` / `MaxTurnRate` rate-limit the
	// shot's own chase of its target rather than teleporting to it each frame.
	FVector ShotPosition = FVector::ZeroVector;
	FRotator ShotRotation = FRotator::ZeroRotator;
	bool bShotSeeded = false;
	// The shot the channel is currently framed on, so a push or a pop that changes the top re-seeds.
	int32 LastTopShotId = 0;
	// Set by the shot mutation phase; cleared only when CalcCamera publishes the cut to Unreal.
	bool bTemporalCameraCutPending = false;

	float PlayerModelAlpha = 0.0f;

	// Ordinary-feed entry state. The yaw is captured exactly once on the first requested frame and
	// the elapsed clock continues through the one-second blend-out, matching the native solver.
	float FeedElapsedSeconds = 0.0f;
	float FeedEntryYaw = 0.0f;
	bool bFeedPoseLive = false;
	bool bFeedVisionMaskAttempted = false;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FeedVisionMask = nullptr;

	// The frame this was last solved on, so `CalcCamera` can be called more than once without
	// double-advancing the blend. Seeded to a frame that cannot be the current one, so the very first
	// solve is not the one the guard eats.
	uint64 LastSolvedFrame = TNumericLimits<uint64>::Max();

	TArray<FElysiumCommandBinding> Bindings;
};
