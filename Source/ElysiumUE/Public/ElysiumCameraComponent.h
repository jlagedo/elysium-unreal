#pragma once

#include "Camera/CameraComponent.h"
#include "CoreMinimal.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumCommands.h"
#include "ElysiumUserCmd.h"

#include "ElysiumCameraComponent.generated.h"

class UElysiumUserSettings;
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
//   * **it does not tick.** `AdvanceFrame` and `FinalizeFrame` run from the camera update, once per
//     frame each (guarded on the frame counter), which is where VtMB runs `CAM_Think` too. A
//     component tick would solve the boom before the pawn has moved and leave the camera a frame
//     behind;
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

	// --- The frame, in two phases -------------------------------------------------------------
	// The frame splits where the boom lands, because the two halves have opposite dependencies: the
	// weights decide *whether* there is a boom, and the fade band reads *how long* it turned out to
	// be. Running them as one call is what made the body's alpha trail the boom by a frame.
	//
	// Phase one — everything that depends only on time and intent: the cvar read, `cam_command`, the
	// feed entry latch, the shot stack, the four weights, the shot chase. It runs BEFORE the rig
	// solves, because the rig reads the weights it advances.
	void AdvanceFrame(float DeltaSeconds);

	// Phase two — everything that depends on THIS frame's boom: the fade band. It runs AFTER
	// `SetSolvedBoom`, which is the whole point of the split.
	//
	// Guarded to once per frame, and **the guard belongs to the camera manager**: it is the only
	// caller that runs the rig first, so it is the only one entitled to declare the frame finalized.
	void FinalizeFrame();

	// The same phase, without taking the once-per-frame guard. For a door that may be reached before
	// the manager has solved the boom — a scene capture, a spectator, a bare `CalcCamera` — where
	// consuming the guard would silence the manager's own call and leave the draw policy a frame
	// behind the boom recorded beside it.
	void FinalizeFrameUnguarded();

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

	// What the pawn's `CalcCamera` override calls: the whole frame end to end, for a caller with no
	// rig of its own to run between the phases. The manager does not go through this — it has to put
	// the boom solve between `SolveFrameFor` and `FinalizeFrame` — so this is the fallback a
	// spectator, a scene capture and `UGameplayStatics` reach, and it composes the last solved boom.
	static bool CalcCameraFor(UElysiumCameraComponent* Camera, float DeltaSeconds, FMinimalViewInfo& Out);

	// `GetCameraView` and phase one, applying nothing. The manager needs the two phases apart,
	// because the rig it runs between them is what phase two reads.
	static bool SolveFrameFor(UElysiumCameraComponent* Camera, float DeltaSeconds, FMinimalViewInfo& Out);

	// Phase two, as a null-safe door matching `SolveFrameFor`.
	static void FinalizeFrameFor(UElysiumCameraComponent* Camera);

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
	// `CAM_ToggleCamera` (`0x100ff800`) in full: the latch flip, the per-class preference write, the
	// weapon arbitration and the forced-third holster branch.
	void ToggleCamera();

	// The equipped item's `camera_class` bits, pushed whenever the active weapon changes. Arbitration
	// re-runs on the change, which is how drawing a melee weapon forces third person and drawing the
	// lockpick forces first.
	void SetEquippedCameraClass(int32 CameraClass);
	int32 GetEquippedCameraClass() const { return EquippedCameraClass; }
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

	// `snapto` / `cam_restore`: hand every hand-orbited axis back to its cvar. Latched here and
	// consumed by whoever owns the orbit state, for the same reason the re-seed is — the verbs live
	// on this component and the rig state lives on the manager.
	void RequestOrbitRestore() { bOrbitRestoreRequested = true; }
	bool ConsumeOrbitRestoreRequest()
	{
		const bool bWas = bOrbitRestoreRequested;
		bOrbitRestoreRequested = false;
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
	// **The frame's resolved draw policy** — the one answer every draw decision reads. Solved in
	// `FinalizeFrame`, so it describes this frame's boom.
	const FElysiumCameraDrawPolicy& GetDrawPolicy() const { return DrawPolicy; }

	// The deciding shot's HUD/viewmodel keys, or the default when nothing is on the stack. Only a
	// named `vdata/camerashots/` shot carries them.
	FElysiumShotPresentation ShotPresentation() const;

	// The player-model alpha the fade band produces (`CInput+0x104`), for the read-outs.
	float ModelAlpha() const { return DrawPolicy.BodyAlpha; }
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

	// The switch-frame table and the player-model fade ramp (`CAM_Think` tail), resolved together.
	void SolveDrawPolicy();

	// Run `ApplyWeaponCameraPref` against the equipped class and the live `camera_prefs`.
	void ApplyEquippedCameraPref();
	// Write `camera_prefs` to the console store and mirror it into the durable user settings.
	void WriteCameraPrefs(int32 Prefs);
	// The other half: push the archived `camera_prefs` / `camera_weaponswitch` back into the console
	// store at BeginPlay, once the player has toggled at least once in this build.
	void RestoreCameraPrefs();
	// The settings object both halves go through, or null with a warning — never a silent skip.
	static UElysiumUserSettings* CameraSettings();
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
	bool bOrbitRestoreRequested = false;

	// Where the scripted channel's view has reached, so `MoveSpeed` / `MaxTurnRate` rate-limit the
	// shot's own chase of its target rather than teleporting to it each frame.
	FVector ShotPosition = FVector::ZeroVector;
	FRotator ShotRotation = FRotator::ZeroRotator;
	bool bShotSeeded = false;
	// The shot the channel is currently framed on, so a push or a pop that changes the top re-seeds.
	int32 LastTopShotId = 0;
	// Set by the shot mutation phase; cleared only when CalcCamera publishes the cut to Unreal.
	bool bTemporalCameraCutPending = false;

	FElysiumCameraDrawPolicy DrawPolicy;

	// The equipped item's authored `camera_class`. 0 covers `noswitch`, an unrecognized literal, an
	// absent key and an empty hand alike — retail's early-out is the same for all four.
	int32 EquippedCameraClass = 0;

	// Ordinary-feed entry state. The yaw is captured exactly once on the first requested frame and
	// the elapsed clock continues through the one-second blend-out, matching the native solver.
	float FeedElapsedSeconds = 0.0f;
	float FeedEntryYaw = 0.0f;
	bool bFeedPoseLive = false;
	bool bFeedVisionMaskAttempted = false;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FeedVisionMask = nullptr;

	// The frames the two phases last ran on, so each can be called more than once without
	// double-advancing the blend or re-evaluating the band. They are separate counters because the
	// phases are separate calls and a caller may legitimately reach one without the other — the
	// fallback door runs both, the manager runs them around its rig solve.
	//
	// Both are seeded to a frame that cannot be the current one, so the very first call is not the
	// one the guard eats. `ApplyCameraModifiers` runs once per *view target* rather than once per
	// frame, so a view-target blend re-enters this whole path twice at the same delta; these two
	// guards are what make that second pass read the frame's answer instead of computing a new one.
	uint64 LastAdvancedFrame = TNumericLimits<uint64>::Max();
	uint64 LastFinalizedFrame = TNumericLimits<uint64>::Max();

	TArray<FElysiumCommandBinding> Bindings;
};
