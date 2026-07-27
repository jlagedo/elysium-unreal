#pragma once

#include "Camera/CameraComponent.h"
#include "CoreMinimal.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumCommands.h"
#include "ElysiumUserCmd.h"

#include "ElysiumCameraComponent.generated.h"

// The player camera (roadmap 11.7). Design + the recovered solve: `docs/camera-view-modes.md`;
// where it sits in the spine: `docs/runtime-architecture.md` §9.
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
	// top. The only place the view is modified.
	void ApplyToView(FMinimalViewInfo& View) const;

	// What both bodies' `CalcCamera` overrides call: `GetCameraView` first, then `UpdateCamera` +
	// `ApplyToView`. One implementation, so the `elysium.SourceMovement` A/B compares the movers and
	// not two camera paths.
	static bool CalcCameraFor(UElysiumCameraComponent* Camera, float DeltaSeconds, FMinimalViewInfo& Out);

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
	// The feed / seduction / death camera's hold. Its own solver is not recovered, so this raises the
	// weight (and with it `CAM_IsThirdPerson`) and nothing more.
	void SetFeedCamera(bool bActive) { Weights.bFeed = bActive; }

	// Drop the smoothing so the next solve snaps rather than eases — VtMB's re-seed flag (`+0x4`),
	// which is what makes re-entering third person not swing in from wherever the camera last was.
	void RequestReseed() { bNeedsReseed = true; }

	// --- The scripted-shot channel ----------------------------------------------------------
	// `SetCamera`, `camera_keyframe`, the conversation camera and the feed camera all arrive here.
	// Returns the shot's id (never reused, 0 on failure); `PopShot` gives control back.
	int32 PushShot(const FElysiumCameraShot& Shot);
	bool UpdateShot(int32 Id, const FElysiumCameraShot& Shot);
	bool PopShot(int32 Id);
	void ClearShots() { Shots.Clear(); }
	const FElysiumCameraShotStack& GetShots() const { return Shots; }

	// --- Debug ------------------------------------------------------------------------------
	// The solved boom length in cm (0 in first person), for `elysium_player_get` and the Cog window.
	float BoomLength() const { return SolvedOffset.Size() * Weights.ThirdBlend(); }
	// The player-model alpha the fade band produces (`CInput+0x104`). Nothing reads it until a player
	// mesh exists (4.8); it is solved now so the band is one number rather than a later guess.
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

	// The orbit/dolly pairs, from the frame's user command (`0x100fc170`): step the requested
	// distance/yaw/pitch and clamp against `c_min*` / `c_max*`.
	void ReadOrbitInput(const FElysiumUserCmd& Cmd, float Dt);

	// The third-person solver (`0x100fd350`): rate-limited approach on distance/yaw/pitch, the
	// collision sweep, the two-constant Hooke damper, and the offset + angles it leaves behind.
	void SolveBoom(const FVector& EyeLocation, const FRotator& ViewRotation, float Dt);

	// The scripted channel's own view: where the top shot has reached, rate-limited by the shot
	// file's `MoveSpeed` / `MaxTurnRate` once it is tracking.
	void SolveShot(float Dt);

	// The player-model fade ramp (`CAM_Think` tail).
	void SolveModelAlpha();

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

	// Requested (what the orbit input asks for) vs solved (what the approach has reached) — VtMB's
	// `+0x154`/`+0x158`/`+0x15c`.
	float RequestedDistance = 0.0f;
	float RequestedYaw = 0.0f;
	float RequestedPitch = 0.0f;
	float SolvedDistance = 0.0f;
	float SolvedYaw = 0.0f;
	float SolvedPitch = 0.0f;

	// Whether the player has moved an axis off its cvar. An un-orbited axis follows `cam_idealdist` /
	// `cam_targetangle` / `cam_yaw` live, so retuning them moves the camera at once; `snapto` puts a
	// hand-orbited axis back.
	bool bDollied = false;
	bool bPitchOrbited = false;
	bool bYawOrbited = false;

	// `m_vecCameraOffset` (+0x164) and `m_vecCameraAngles` (+0x170), at FULL weight; the blend is
	// applied at the point of use so the solve does not depend on it.
	FVector SolvedOffset = FVector::ZeroVector;
	FRotator SolvedAngles = FRotator::ZeroRotator;

	// The spring damper's own position, and whether the boom is wall-clipped (which picks the stiffer
	// constant).
	FVector SpringPosition = FVector::ZeroVector;
	bool bClipped = false;
	bool bNeedsReseed = true;

	// Where the scripted channel's view has reached, so `MoveSpeed` / `MaxTurnRate` rate-limit the
	// shot's own chase of its target rather than teleporting to it each frame.
	FVector ShotPosition = FVector::ZeroVector;
	FRotator ShotRotation = FRotator::ZeroRotator;
	bool bShotSeeded = false;
	// The shot the channel is currently framed on, so a push or a pop that changes the top re-seeds.
	int32 LastTopShotId = 0;

	float PlayerModelAlpha = 0.0f;

	// The frame this was last solved on, so `CalcCamera` can be called more than once without
	// double-advancing the blend. Seeded to a frame that cannot be the current one, so the very first
	// solve is not the one the guard eats.
	uint64 LastSolvedFrame = TNumericLimits<uint64>::Max();

	TArray<FElysiumCommandBinding> Bindings;
};
