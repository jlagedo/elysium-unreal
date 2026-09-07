#pragma once

#include "Camera/CameraComponent.h"
#include "CoreMinimal.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumCommands.h"
#include "ElysiumUserCmd.h"

#include "ElysiumCameraComponent.generated.h"

class UElysiumUserSettings;
class UTexture2D;

namespace ElysiumCameraView
{
	// The aspect the frame is actually rendered at, which a `vdata/camerashots/` field of view has to
	// be widened to (`ElysiumCam::WidenSourceFov`). `FMinimalViewInfo::AspectRatio` carries the
	// camera component's *authored* ratio rather than the window's, and with `bConstrainAspectRatio`
	// false — which is this project's case — the window's is what the projection actually uses. With
	// no game viewport (a commandlet, an automation run) the caller's fallback stands, which keeps
	// the headless assertion of the FOV path deterministic.
	float RenderAspectRatio(float Fallback);
}

// The player camera. Design + the recovered solve: `docs/vtmb/camera-view-modes.md`;
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

// One channel's resolved pose. `Target` is what the composition interpolates — retail lerps the aim
// **point** (`m_vecCameraTargetOverride`), never the rotator — and `Rotation` is what the cine
// channel hard-writes.
struct FElysiumScriptedShotPose
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector Target = FVector::ZeroVector;
	// 0 keeps the player's field of view, which is what a shot file with no `FieldOfView` authors.
	float FieldOfView = 0.0f;
	float Roll = 0.0f;
	bool bLive = false;
};

// What the scripted channel resolved to this frame, **as retail's two channels** (SC2). It is
// published as values because the layer that composes it runs later in the frame than the solve that
// produced it — and because the same values have to compose over either rig once
// `elysium.ModernCamera` has a second one to choose.
struct FElysiumScriptedShotView
{
	// The adopted cine camera (`C_BaseCineCamera`, `C_BasePlayer::CalcView` `0x100a7770`). A hard
	// write of origin, angles and FOV with no weight of any kind; while it is live the third-person
	// boom is skipped entirely (`ClientModeShared::OverrideView` `0x100d4040`).
	FElysiumScriptedShotPose Cine;

	// The `camera_track` override (`CInput::OverrideView` `FUN_100ffb90`). Composed over whichever
	// base won — the cine pose when one is adopted, the rig's otherwise — at `Weight`.
	FElysiumScriptedShotPose Track;

	// The track channel's ramp. **Linear**: the `SimpleSpline` ease lives at the compose site, which
	// is retail's own division of labour (`FUN_100fc900`'s tail is linear, `FUN_100ffb90` opens with
	// the spline). This is the layer's alpha.
	float Weight = 0.0f;

	// False until the channel has framed its first shot, which is what stops a push being applied
	// from wherever the camera happened to be.
	bool bSeeded = false;

	// The top shot's own presentation record — the HUD/viewmodel keys and the pusher's exposure ask.
	FElysiumShotPresentation Presentation;

	// What `CAM_IsThirdPerson` and the modifier's alpha read: an adopted cine camera is full scripted
	// weight because it has none of its own.
	float ChannelWeight() const { return Cine.bLive ? 1.0f : Weight; }
};

// `CViewRender::CalcView`'s last arm (`client.dll` `0x10191200`, tail `0x1019158e`-`0x101915f2`):
// when the engine's view entity index is **strictly above** `IVEngineClient::GetMaxClients()` — 1 in
// single player, so any index of 2 or more — origin and angles are hard-replaced by that entity's
// abs origin and abs angles. The FOV is **not** touched. That is VtMB's death / observer view; there
// is no separate death, feed or seduction `CalcView` arm (RC10).
struct FElysiumSpectatedView
{
	FVector Origin = FVector::ZeroVector;
	FRotator Angles = FRotator::ZeroRotator;
};

UCLASS()
class UElysiumCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:
	UElysiumCameraComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// The frame, in two phases.
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

	// The base half: the strafe bank and the boom, at the third-person weight, then the water
	// clearance, the ordinary feed pose, the spectator replace and the `camortho` projection. This is
	// what a base request produces; nothing scripted is in it.
	//
	// **The boom is skipped entirely while a cine shot is live** (`ClientModeShared::OverrideView`
	// `0x100d4040` takes `CInput` slot 33 directly rather than slot 31), so a scripted shot is never
	// displaced by it and the track override lerps from the cine pose rather than from the rig's.
	void ApplyBaseToView(FMinimalViewInfo& View) const;

	// The scripted half, in retail's two branches: the adopted cine camera hard-writes the pose
	// (`C_BaseCineCamera::CalcView` `FUN_10001b50`), and the `camera_track` override then composes
	// over whatever base won (`CInput::OverrideView` `FUN_100ffb90`), plus the motion-blur
	// suppression an authored edit needs. One camera in series, never two rival viewpoints.
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

	// Intent.
	// The frame's user command, handed on by the body. Only the camera pairs are read here.
	void SetUserCmd(const FElysiumUserCmd& Cmd) { PendingCmd = Cmd; }

	// The mode.
	// `CAM_ToThirdPerson` / `CAM_ToFirstPerson` (`0x100ff7c0` / `0x100ff7e0`) — the minimal pair: set
	// the latch, clear `cam_command`. Neither runs the weapon arbitration or the holster check; only
	// `togglecamera` does, because both need weapons.
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

	// The body's water state (R7.1), pushed each pre-move tick by
	// `AElysiumMapActor::UpdatePlayerWater`: `CheckWater`'s level 0-3 and the surface plane of the
	// volume it settled on, in cm. A push rather than a query because the camera reaches only its
	// owner and the water volumes belong to the map actor; `(0, 0)` is "dry", which is what a map
	// with no water and a dry body both write. A frame with no player body (or a map outside
	// `Active`) pushes nothing at all and the component keeps its last state -- harmless, because
	// the camera it would write to is the one that went away with the body.
	void SetWaterState(int32 Level, float SurfaceZCm)
	{
		WaterLevel = Level;
		WaterSurfaceZCm = SurfaceZCm;
	}

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

	// The scripted-shot channel.
	// `SetCamera`, `camera_keyframe`, the conversation camera and the feed camera all arrive here.
	// Returns the shot's id (never reused, 0 on failure); `PopShot` gives control back.
	int32 PushShot(const FElysiumCameraShot& Shot);
	bool UpdateShot(int32 Id, const FElysiumCameraShot& Shot);
	bool PopShot(int32 Id, float BlendOutSeconds = -1.0f);
	void ClearShots() { Shots.Clear(); }
	const FElysiumCameraShotStack& GetShots() const { return Shots; }

	// `camortho` (`client.dll` `FUN_101001e0`) and `CInput+0x1b8`, the enable `CAM_IsOrthographic`
	// (slot 48) answers. A dev view: no shipped content sets it.
	void SetOrthographic(bool bOrtho) { bOrthographic = bOrtho; }
	bool IsOrthographic() const { return bOrthographic; }

	// The spectator replace (`CViewRender::CalcView`'s last arm, RC10). **The seam answers nothing
	// yet**: the port has no observer, death-cam or spectator producer, so nothing calls this, and it
	// stands for retail's `IVRenderView::GetViewEntity()` (slot 39 on `VEngineRenderView008`) with the
	// `index > GetMaxClients()` test already made by whoever would write it. When it is set, the base
	// view's origin and angles are replaced outright and the FOV is left alone, in retail's own place
	// in the order — after the ordinary view, ahead of the cine hard write.
	void SetSpectatedView(const FElysiumSpectatedView& View) { SpectatedView = View; }
	void ClearSpectatedView() { SpectatedView.Reset(); }
	const TOptional<FElysiumSpectatedView>& GetSpectatedView() const { return SpectatedView; }

	// Debug.
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
	// `firstperson`, `snapto`, `cam_command`, `centerview`, `force_centerview`. Implementations stack,
	// so a fresh pawn's camera takes the verbs over and hands them back when it dies.
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

	// The frame's water state, read by `ApplyBaseToView`'s clearance step. Never solved here.
	int32 WaterLevel = 0;
	float WaterSurfaceZCm = 0.0f;

	// Where the scripted channel's view has reached. The whole approach — the tolerance deadbands,
	// the accel/decel on `MoveSpeed`/`MoveAccel` and `MaxTurnRate`/`TurnAccel`, `SyncRotateOnMove` —
	// is `C_BaseCineCamera`, ported into `FElysiumScriptedShotTracker`, so the legacy `SetCamera`
	// stack and the dialogue director's request channel cannot drift apart.
	// **One tracker, because retail has one.** It follows the adopted cine shot when there is one and
	// the top track shot otherwise. The `camera_track` channel needs none of its own: retail's
	// `CInput` override re-derives `VectorAngles(target - origin)` from the replicated values every
	// frame with no deadband and no rate, which is exactly what the tracker's `bTracked == false`
	// copy-through does.
	FElysiumScriptedShotTracker ShotTracker;
	FVector ShotPosition = FVector::ZeroVector;
	FRotator ShotRotation = FRotator::ZeroRotator;
	bool bShotSeeded = false;
	// The shot the channel is currently framed on, so a push or a pop that changes the top re-seeds.
	int32 LastTopShotId = 0;
	// `CInput+0x1b8`, the `camortho` latch.
	bool bOrthographic = false;
	// Retail's spectated view entity, when something ever sets it. See `SetSpectatedView`.
	TOptional<FElysiumSpectatedView> SpectatedView;
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
