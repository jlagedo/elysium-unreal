#pragma once

#include "CoreMinimal.h"

// The camera's rule set (`docs/vtmb/camera-view-modes.md`).
//
// VtMB ships **one** player camera with a **blend weight**, not two cameras: `togglecamera` flips a
// bool, a per-frame driver ramps a 0..1 weight, and every third-person effect — the boom offset, the
// view angles, the player-model draw, the crosshair — is a function of that weight. What lives here
// is that driver plus the scripted-shot channel that rides on top of it, as plain C++ with no
// UObject and no engine type, so the whole thing is asserted with no pawn, no world and no RHI
// (`Elysium.Substrate.Camera`) — the same split `ElysiumAppState.h` and `ElysiumInputScope.h` use.
//
// `UElysiumCameraComponent` is the engine half: it owns one of these, solves the boom against real
// geometry, and applies the result at `AElysiumPawn::CalcCamera`.

namespace ElysiumCam
{
	// One Source unit in cm. The cvar surface stays in **Source units** so a user's `config.cfg` and
	// the Unofficial Patch's aliases transfer unchanged; conversion happens once, at the point of use.
	inline constexpr float U = 2.54f;

	// The transition rate, per second (`FADD st,st` on the frame delta at `client.dll` 0x100fc900):
	// a full first<->third traversal takes 0.5 s.
	inline constexpr float BlendRate = 2.0f;
	// The feed channel advances independently: one complete traversal per scaled second.
	inline constexpr float FeedBlendRate = 1.0f;

	// The secondary scripted weight (`CInput+0x108`) decays at 0.5/s and has no riser in any recovered
	// path — it is set by whatever raises it and bleeds off on its own.
	inline constexpr float SecondaryDecayRate = 0.5f;

	// Source's `SimpleSpline` (`0x100fdb30`, constant 3.0 at 0x10227ee0). The weight ramps *linearly*
	// and is eased here, at the point of use — that ordering is the feel, so it is written literally
	// rather than routed through FMath::SmoothStep.
	inline float SimpleSpline(float T)
	{
		const float t = FMath::Clamp(T, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// `CInput::OverrideView`'s stand-in aim point (`FUN_100ffb90`, `_DAT_1022b298`): the composition
	// interpolates what the camera is *looking at* as a **point**, not as an angle, and the base
	// view's point is its own origin plus this far along its forward. **240 Source units** (609.6 cm),
	// recovered byte-exact by RC9 — the earlier reading of 100 was wrong by 2.4x, which materially
	// changes the arc of every scripted arrival.
	inline constexpr float ViewForwardPointUnits = 240.0f;
	inline constexpr float ViewForwardPointCm = ViewForwardPointUnits * U;

	// `m_flCameraFOVOverride`, the value the `camera_track` channel publishes — `CBaseEntity` slot
	// `0xC4`, whose constructor default is **75.0** (`0x10026830` -> `_DAT_104454cc`). It is why
	// `FUN_100ffb90`'s FOV lerp has no zero guard: retail cannot publish a 0 there. The port's value
	// shots may carry `FieldOfView == 0` ("keep the player's"), so the two publish sites seed this in
	// its place, which is the same number retail's own view entity would have carried.
	inline constexpr float CameraFovOverrideDefault = 75.0f;

	// The stand-in target a shot that carries a rotation rather than a look-at point aims at. At full
	// weight `VectorAngles(target - origin)` is exactly that rotation again, so the two authoring
	// forms land on the same pose and differ only in the arc between.
	FVector ScriptedShotTargetPoint(const FVector& ShotLocation, const FRotator& ShotRotation);

	// `FUN_1013c940` — the closest-point parameter on the line through `A` and `B`, **unclamped**
	// (RC1, 2026-09-07). The whole function is nine lines of x87 with exactly one branch, the
	// degenerate-length guard; there is no `FCOM` against `0.0` or `1.0` anywhere in it, and the image
	// contains no segment-clamped variant of it or of its 2-D twin `FUN_1013cd40`.
	//
	//     dir = B - A;  len2 = dir . dir;
	//     if (len2 < 1e-05f) return 0.0f;          // `_DAT_1046a5e4`, `_DAT_104454c4`
	//     return (P . dir - A . dir) / len2;       // no clamp
	//
	// So this is an **infinite line**, not a segment, and the distance a caller derives from it stays
	// the true perpendicular distance even when `P` projects behind `A` or beyond `B` — where a
	// clamped implementation would report a strictly larger one.
	float ClosestPointParameterOnLine(const FVector& P, const FVector& A, const FVector& B);

	// `FUN_1013ca00`: `A + t * (B - A)` for the `t` above.
	FVector ClosestPointOnLine(const FVector& P, const FVector& A, const FVector& B);

	// `AutoPositionFromTarget` — the mode-1 think's `flags & 0x20` block, disassembly
	// `0x1006fa50`-`0x1006fb85`, with the constants read from the image (`_DAT_104454d0 = 0.5`,
	// `_DAT_1044eb08 = 0.0174532924`):
	//
	//     if (P2.z > P1.z) swap(P1, P2)               // P1 ends HIGH, P2 ends LOW
	//     C = ClosestPointOnLine(P2, lookAt, camOrigin)
	//     A = FieldOfView * 0.5 * DEG2RAD
	//     d = |C - P2|
	//     h = d / sin(A)
	//     r = sqrt(h*h + d*d)
	//     camOrigin = lookAt - normalize(lookAt - camOrigin) * r
	//
	// **Only the lower-Z point participates**; the higher one exists solely to decide the swap. This
	// is deliberately *not* the tight `d / tan(A)` fit — retail takes the hypotenuse and then adds a
	// second `d` in quadrature, so it always backs off further than an exact frame. `FieldOfView` is
	// the raw authored 4:3-referenced horizontal number; the Hor+ widening is a render-time affair
	// and does not belong here. Points and the return are in cm; the FOV is degrees.
	FVector AutoPositionFromTarget(const FVector& CamOrigin, const FVector& LookAt,
		const FVector& Point1, const FVector& Point2, float FieldOfViewDeg);

	// Source's rate-limited approach (`0x100fc000`): push Current toward Target by at most
	// `Speed * Dt`. Nothing eases — the clamp is the whole smoothing, which is why the camera reads as
	// mechanical until the spring damper runs over it. A Speed of 0 snaps.
	float Approach(float Current, float Target, float Speed, float Dt);

	// The same, in angle space, so a 359 -> 1 step is 2 degrees rather than 358.
	float ApproachAngle(float Current, float Target, float Speed, float Dt);

	// How long the remaining translation still takes — `RemainingTime` (`client.dll` `FUN_100010f0`,
	// listing `0x100010f0`-`0x100011bb`), reproduced **verbatim, defects included** (M6). Only
	// `SyncRotateOnMove` reads it: `FUN_10001c80` divides the angular error by it so the pan lands
	// with the dolly. Distances and speeds in cm, the return in seconds.
	//
	// Two retail defects live in the arithmetic and are deliberately preserved, because every shipped
	// `SyncRotateOnMove` shot was tuned against the numbers they produce:
	//   * the trapezoid arm's acceleration distance is `(vmax - v)^2/(2a)`, not `(vmax^2 - v^2)/(2a)`
	//     - correct only from rest;
	//   * the triangle arm's radicand `2a - 0.5(d - R2)` subtracts a distance from an acceleration,
	//     so it goes negative whenever `d - R2 > 4a` and NaNs `vpeak` and the turn rate with it.
	// The radicand is **clamped at zero** — the one divergence, removing only the NaN state, which
	// needs `MoveSpeed > 2*MoveAccel` and is unreachable on shipped content.
	float RemainingTranslationSeconds(float Speed, float MaxSpeed, float Accel, float Distance);

	// The strafe bank — `V_CalcRoll` (`client.dll` `0x101907a0`), unchanged from Quake:
	//
	//     side = DotProduct(velocity, right);  sign = side >= 0 ? 1 : -1;  side = |side|
	//     side < rollspeed ? (side / rollspeed) * rollangle * sign : rollangle * sign
	//
	// A **view** effect only: the caller (`0x10190850`) adds the result to `view.roll` and adds a
	// literal 0.0 instead when its third-person gate fires, which is why the bank exists in first
	// person and vanishes in third.
	//
	// The pair that drives it is `cl_rollangle` / `cl_rollspeed`, **not** `sv_rollangle` /
	// `sv_rollspeed` — those two are registered by both game DLLs and read by neither (and the
	// string does not appear in `engine.dll` at all), so they are dead Source leftovers.
	// `VelocityCm` and `RollSpeedCm` are both in cm/s; the return is degrees.
	float SolveViewRoll(const FVector& VelocityCm, const FRotator& ViewRot,
		float RollAngleDeg, float RollSpeedCm);

	// The water clearance — `CViewRender::GetWaterOffset`, driven by `cl_waterdist` 4.
	// VtMB walks the view origin in one-unit Z
	// steps against `MASK_WATER` whenever the body's water level is above 1, and the two directions
	// are opposite: **treading** (level 2) raises the view until it is clear of the plane, so the
	// camera stays dry, while **submerged** (level 3) lowers it until it is back under, so the
	// camera stays wet. Either way the view never rests inside the `cl_waterdist` band around the
	// surface, which is what stops the underwater post-process from flickering on a bobbing eye.
	//
	// A Z-only rule, so it takes the two heights rather than the view: `ViewZ` is where the camera
	// ended up (boom included), `SurfaceZ` is the plane of the volume the body is in, both cm; the
	// return is the Z delta to add. Levels 0 and 1, and a surface further than `WaterDistCm` from
	// the view, return 0 — outside the band the original's step loop terminates immediately.
	//
	// Divergence: the 1-unit quantization is dropped. The loop's only purpose is to find the
	// clearance distance, and the closed form is that distance exactly rather than rounded up to
	// the next inch.
	float SolveWaterOffset(int32 WaterLevel, float ViewZ, float SurfaceZ, float WaterDistCm);
}

// The full-strength ordinary feed camera, relative to the player's eye.
struct FElysiumFeedCameraPose
{
	FVector Offset = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
};

// The four weights

// `CInput`'s camera block (`docs/vtmb/camera-view-modes.md` §2). Four weights and three latches; there is no
// state machine and no transition object, which is exactly why reversing mid-blend resumes from
// where it is instead of restarting.
struct FElysiumCameraWeights
{
	// The latches (CInput +0xf0 / +0xf8 / +0xf9).
	// The user's own toggle. `togglecamera`, `thirdperson` and `firstperson` all write this.
	bool bUserThird = false;
	// Weapon-class arbitration forced third person (class 0x10), and the feed camera's own hold.
	bool bForcedThird = false;
	// Weapon-class arbitration forced first person (class 0x08).
	bool bForcedFirst = false;
	// The feed / seduction / death camera's request latch. Ordinary feed uses the recovered solver
	// below; the other two producers still share only this weight channel.
	bool bFeed = false;

	// The weights (CInput +0xfc / +0x100 / +0x108 / +0x138).
	// The third-person blend, 0..1. The only one this struct ramps on its own.
	float Third = 0.0f;
	// The scripted-camera weight: dialogue and cutscene shots. Written from the shot stack, whose
	// ramp is *timed* (start + duration) rather than fixed-rate.
	float Scripted = 0.0f;
	// The second scripted weight; decays at 0.5/s with no riser in the recovered code.
	float Secondary = 0.0f;
	// Feed / seduction / death.
	float Feed = 0.0f;

	// One frame of the weight driver (`0x100fc900`, called at the top of `CAM_Think`).
	//
	// `TimeScale` is VtMB's `m_flTimeScale` (player +0x1078), so a bullet-time frame blends slower
	// rather than running on wall-clock. In the live game the engine has **already** scaled the tick
	// delta by the world's dilation by the time this is reached — the same property the game clock
	// relies on — so the component passes 1.0 and the parameter exists for the driver's own test.
	void Advance(float DeltaSeconds, float TimeScale = 1.0f);

	// `CAM_IsThirdPerson` (`0x100ffa20`), verbatim: a **disjunction**, so it is true throughout the
	// blend in both directions and a scripted camera counts as third person. Everything gated on it
	// (the player model, the crosshair, the world weapon) switches on the *first* frame of a
	// first->third transition and off only on the *last* frame of third->first.
	bool IsThirdPerson() const;

	// The eased weight the consumers actually use.
	float ThirdBlend() const { return ElysiumCam::SimpleSpline(Third); }
	float FeedBlend() const { return ElysiumCam::SimpleSpline(Feed); }

	// Which latch is deciding, for the debug read-out: forced-third and the feed camera win, then
	// forced-first, then the user toggle.
	const TCHAR* Driver() const;

	void Reset() { *this = FElysiumCameraWeights(); }
};

// The draw policy

// Which crosshair path the frame is on (`docs/vtmb/camera-view-modes.md` §5, `0x1009b9e0`). The mode
// toggle does not hide the HUD; it selects the other path.
enum class EElysiumReticlePath : uint8
{
	FirstPerson,   // the full use-icon / arrow cursor path
	ThirdPerson,   // the plain reticle at the crosshair rect
};

// A **named** `SetCamera` shot's presentation keys. `ShowHud` and `DrawViewmodel` are keys on
// `vdata/camerashots/` files and on nothing else: a value shot — a Worldcraft `camera_track`, a VCD
// edit, a dialogue-grammar shot — authors no such key and leaves `bNamed` false. That single flag is
// why a track cannot hide the HUD (`docs/vtmb/camera-view-modes.md` §5, "the ordinary mode toggle
// does not hide the rest of the HUD").
struct FElysiumShotPresentation
{
	bool bNamed = false;
	bool bShowHud = false;         // parses default 0, but only on a named shot
	bool bDrawViewmodel = false;   // same

	// **`m_bDrawPlayer`** — `DT_BaseCineCam +0x640` on the server, `0x464` on the client. **Not a shot
	// key**: it is the *director's*, raised by `CBaseCineCam::Spawn` from `spawnflags & 2` and copied
	// onto the runtime camera by `FUN_10070780` at shot start (SC4), or forced to 1 by anim event 4050
	// (SC8). It travels on the presentation block because that is what reaches the draw policy, and
	// `ElysiumCam::SolveDrawPolicy` is its **only** reader — exactly as retail's `0x464` is read
	// exactly once in the whole image, by `ShouldDrawLocalPlayer` (RC11).
	//
	// 27 of the 51 shipped `camera_cinematic` directors author `spawnflags & 2`, `sp_tutorial_1`'s
	// `feedcamera` among them, so this has a shipped non-zero input from content and not only from
	// the anim event.
	bool bDrawPlayerBody = false;

	// **Named Presentation modernization.** Nothing in `vdata/camerashots/` authors an exposure key -- retail's renderer has no
	// eye adaptation to fight -- so this is never parsed. It is the pusher's ask, carried for the
	// handle's lifetime: a terminal shot fills the frame with one bright emissive panel and UE's
	// auto-exposure would otherwise ramp the whole image down around it.
	bool bClampExposure = false;
	float ExposureBrightness = 1.0f;   // EV100-ish min == max while the clamp is on
};

// What a pusher asks of the frame's exposure while its shot has weight. `Scene` is every ordinary
// shot: the world's own auto-exposure stands.
enum class EElysiumShotExposure : uint8
{
	Scene,
	Clamped,
};

// One resolve of the switch-frame table (`docs/vtmb/camera-view-modes.md` §5, the render hand-off).
//
// **Eligibility and opacity are two numbers.** Retail's draw policy reads `CAM_IsThirdPerson`, a hard
// boolean, while the `CInput+0x104` alpha ramp is a separate near-camera *third-person* fade. Deriving
// the hide from the alpha cannot express the entry frame, where the body is draw-eligible **and**
// fully transparent because the boom has not left the eye yet.
struct FElysiumCameraDrawPolicy
{
	// `CAM_IsThirdPerson`, the six-term disjunction. Every gate below is this or its inverse.
	bool bThirdPerson = false;

	// The full local player model. Submitted whenever the predicate holds; `BodyAlpha` decides how
	// much of it is seen, and 0 is a legal eligible value.
	bool bBodyEligible = false;
	float BodyAlpha = 0.0f;

	// The carried world weapon and owned attachments. **Boolean in both directions** — no attachment
	// alpha consumer is recovered, so this never fades.
	bool bWorldWeaponEligible = false;

	// The first-person hands and weapon viewmodels. Suppression is **submission-only**: the consumer
	// never destroys a component, clears a model, or resets a sequence or cycle, so the frame the
	// weight reaches exactly 0 resumes the existing visual state rather than rebuilding it.
	bool bViewmodelEligible = false;

	EElysiumReticlePath Reticle = EElysiumReticlePath::FirstPerson;

	// **A latch, not a solve** (M14). Retail never enforces the HUD per frame: `HideHud(0xa06d)` /
	// `ShowHud(0xa06d)` are issued on the shot-index edge, on going inactive after having been
	// active, and from the destructor, and nothing re-asserts them in between — which is why a
	// HUD-hiding shot replaced by another HUD-hiding shot does not re-issue the call. This field
	// therefore carries `FElysiumShotHudGate::bHudVisible` through to the frame's consumers; it is
	// **not** derived from the live shot's keys here.
	bool bShowHud = true;
};

// The HUD edge — `C_BaseCineCamera::OnDataChanged` `0x100024c0`'s HUD arms and the destructor's
// (`FUN_10001920`), as a latch (**M14, ruled 2026-09-07**).
//
// Retail hides the elements whose `GetHudBits()` intersect the mask `0xa06d`. The **element set is
// not reproduced** — the port's HUD is a new asset, not a VtMB reproduction, so the port hides its
// own — but the **edge timing is contract** and is what this reproduces exactly:
//
//   * a **shot-index** change into a shot whose `ShowHud` key is clear hides the HUD;
//   * a shot-index change into a shot whose `ShowHud` key is set shows it;
//   * going inactive after having been active shows it (`!IsActive() && m_bWasActive`);
//   * destruction shows it when the live shot had hidden it;
//   * **nothing else**, and in particular nothing per frame.
//
// The port's HUD seam is a published boolean (`FElysiumCameraDrawPolicy::bShowHud` ->
// `FElysiumViewState::Camera.bShowHud` -> `UElysiumHUDModel`), so "issuing the call" is "writing the
// latch". `Issued` counts the writes, which is what makes the *absence* of a re-issue assertable.
struct FElysiumShotHudGate
{
	// The latched HUD state — what the frame publishes until the next edge.
	bool bHudVisible = true;

	// How many hide/show edges have been issued. Diagnostics and the acceptance assertion; retail's
	// equivalent is the count of `HideHud`/`ShowHud` calls.
	int32 Issued = 0;

	// `OnDataChanged`'s two HUD arms, in retail's order. `ShotIndex` is `m_ShotIndex` (`INDEX_NONE`
	// for a value shot, which authors no `ShowHud` key and therefore cannot take the HUD down);
	// `bShotShowsHud` is `(flags & 0x200) != 0` ORed with "this is not a named shot at all".
	// Returns true when an edge was issued this call.
	bool OnDataChanged(bool bActive, int32 ShotIndex, bool bShotShowsHud);

	// `FUN_10001920`, the destructor arm: restore the HUD if a shot had hidden it. Also the port's
	// teardown path (the map going away, the component ending play).
	bool OnDestroyed();

	void Reset() { *this = FElysiumShotHudGate(); }

private:
	// `m_ShotIndexCache` (`0x49c`) and `m_bWasActive` (`0x4a0`).
	int32 ShotIndexCache = INDEX_NONE;
	bool bWasActive = false;
};

// The scripted-shot channel

// `CBaseCineCam+0x594`, the **origin-source selector** the mode-1 think reads before it publishes
// (`0x1006f8f0`, `docs/vtmb/camera-view-modes.md` "The mode-1 think"). It is not a phase and not a
// mode: it names which anchor drives the published origin, and its third value suppresses
// `AutoPositionFromTarget` outright.
//
// Its only retail writer is the shot start `FUN_1006e8e0`, which sets it to `0` when the **End**
// anchor's handle is live and otherwise leaves whatever the entity was constructed with — so in
// shipped content a shot with an `End` anchor drives from `End`.
enum class EElysiumShotOriginSelector : uint8
{
	// `0` — the published origin is anchor 1 (`End`), and `AutoPositionFromTarget` may run.
	EndAnchor = 0,
	// `1` — the published origin is anchor 0 (`Start`), and `AutoPositionFromTarget` may run.
	StartAnchor = 1,
	// `2` — the entity's **own abs origin** stands, and the whole `AutoPositionFromTarget` block is
	// skipped. The abs-origin source is the director entity's transform, which is SC4's; until it
	// exists the resolve keeps the anchor-derived origin and only the suppression is in force.
	Entity = 2,
};

// `CViewRender::GetViewSetup()`'s origin and angles (`client.dll &DAT_105fbf38 + 0x10`, fields `+0x38`
// / `+0x50`) — the **live rendered view**, which is what shot start seeds from on its second arm. The
// tracker never reaches for one; the caller that owns a view passes it in.
struct FElysiumViewSetup
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
};

// One scripted shot, as **values**. `SetCamera` (115 script calls, keyed to `vdata/camerashots/`),
// `camera_keyframe`, the conversation camera and the feed camera all push one of these, which is
// what keeps cutscene cameras out of the pawn: the camera blends a value, and whoever pushed it
// keeps that value current (a `Follow` attach type is the pusher re-resolving each frame, not the
// camera knowing what an entity is).
struct FElysiumCameraShot
{
	// Where the shot renders from, world cm.
	FVector Origin = FVector::ZeroVector;

	// What it looks at, world cm. With `bUseLookAt` false the shot carries `Rotation` outright, which
	// is what a keyframed camera with no target point authors.
	FVector LookAt = FVector::ZeroVector;
	bool bUseLookAt = true;
	FRotator Rotation = FRotator::ZeroRotator;

	// **`Rotation` carries a real `m_angCamAngles`.** The server publishes that triple every 24 Hz
	// tick (`0x1006f8f0` seeds it from `GetAbsAngles()` — vfunc `0x36c` — and replaces it with
	// `VectorAngles(lookAt - GetOrigin())` only when the record declares a `Target` block), and the
	// client reads it at exactly two places: the shot-start seed `FUN_10002210` (`0x474..0x47c` =
	// `0x428..0x430`, copied through with **no** look-at re-derive) and the copy-through arm of
	// `FUN_10001a20` for every `CamMode` other than 1 and 4. The mode-1 tracker re-derives `desired`
	// from `m_vecLookAt - m_vecCurOrigin` every frame instead, which is why the two can disagree for
	// the whole shot: retail publishes the origin the `+0x594` selector chose and the angle measured
	// from the entity's own **placement** (`0x1006e8e0`'s `SetOrigin(+0x564)`).
	//
	// A port producer that pushes a bare value shot — `camera_track`, a VCD edit, the green room —
	// has no server half and states its aim as a look-at instead, so it leaves this false and the two
	// readers above derive the angle themselves. `FElysiumCameraDirector::Resolve` and
	// `FElysiumCameraCinematic` set it, because those two *are* the server publish.
	bool bAnglesPublished = false;

	// `CInput+0x194` / `+0x198`: the shot's own roll and field of view, both lerped by the weight. A
	// FieldOfView of 0 keeps the player's.
	float Roll = 0.0f;
	float FieldOfView = 0.0f;

	// The timed ramp's duration (`+0x118`) — a `camera_track` override gets an explicit duration,
	// unlike the toggle's fixed-rate blend. 0 snaps. **Read on the track channel only**: a cine shot
	// has no weight at all (see `bCine`).
	float BlendSeconds = 0.5f;

	// **Which of retail's two channels this shot is.** They are not two views: they are one camera in
	// series (`client.dll` `0x100d4040`).
	//
	//   * **cine** (`bCine` true) — an adopted `C_BaseCineCamera`: `SetCamera`, a `camera_cinematic`,
	//     a terminal shot, a dialogue shot. `C_BasePlayer::CalcView` (`0x100a7770`) hard-writes its
	//     origin, angles and FOV (`FUN_10001b50`) and `CInput` slot 31's third-person boom is then
	//     **skipped entirely** — there is no weight, no blend field anywhere on `C_BaseCineCamera`,
	//     and every exit is a same-tick cut (M1);
	//   * **value / track** (`bCine` false) — the `camera_track` override channel: the only ramped
	//     one, composed over whichever base won by `CInput::OverrideView` (`FUN_100ffb90`) at the
	//     signed-duration weight `FElysiumCameraShotStack` carries.
	//
	// A second axis from `bTracked`, deliberately: `bTracked` says *how the pose is solved* (retail's
	// `CamMode == 1`), `bCine` says *which channel writes it*.
	bool bCine = false;

	// The shot file's `CameraConstraints`, in engine units: how the camera is allowed to chase its
	// own goal once the shot is live. These are the client tracker's inputs — `C_BaseCineCamera`
	// (`client.dll`), reached every rendered frame from `C_BasePlayer`'s view calc
	// (`FUN_100a7770` -> `FUN_10001b50` -> `FUN_10001a20` -> `FUN_10001fa0`). See
	// `FElysiumScriptedShotTracker` for what reads them.
	float MoveSpeed = 0.0f;                                   // cm/s, 0 = snap
	float MoveAccel = 0.0f;                                   // cm/s^2, <=0 = instant to MoveSpeed
	FVector MaxTurnRate = FVector(90.0f, 90.0f, 90.0f);       // deg/s, (pitch, yaw, roll)
	float TurnAccel = 0.0f;                                   // deg/s^2, <=0 = instant to MaxTurnRate

	// The deadbands. **These are what keep a dialogue camera still while its subject animates**: the
	// shot's look-at is re-resolved every frame off an animated head bone (retail does this too), and
	// the 10-degree `AngularTolerance` the shipped conversation shots author is the only thing that
	// stops that jitter from panning the camera.
	float DistanceTolerance = 0.0f;                           // cm of goal drift tolerated when parked
	FVector AngularTolerance = FVector::ZeroVector;           // deg per axis, (pitch, yaw, roll)

	// `SyncRotateOnMove` (shot flags `+0x20` bit 0x100): while the camera is translating, the turn
	// rate is sized so the pan lands with the dolly and `MaxTurnRate` is bypassed (`FUN_10001c80`).
	bool bSyncRotateOnMove = false;
	// `SnapOnShotChange` (bit 0x80): `FUN_10002390` hard-copies goal -> current on the shot change.
	bool bSnapOnShotChange = false;

	// Retail's `CamMode` (`DT_BaseCineCam` `+0x638`, client `+0x45c`), reduced to the one bit the
	// client reads. `C_BaseCineCamera::Update` (`FUN_10001a20`) runs the tracker above **only for
	// mode 1, the named shot**: `if (CamMode == 1) FUN_10001fa0(); else current = replicated origin,
	// angles, FOV` — every other mode (`FollowEntity` 3, `Animated` 4) copies the server's pose
	// straight through, and the Worldcraft `camera_track` channel never reaches the cine camera at
	// all (`CInput` `FUN_100ffb90`: `VectorAngles(target - origin)` re-derived every frame, no
	// deadband, no rate). So a value shot is **direct** — origin and look-at are the pose — and only
	// a `vdata/camerashots/` shot (and the conversation profiles that stand in for one) is tracked.
	bool bTracked = false;

	// One-shot render-history reset. The camera component consumes and clears it while publishing
	// the value; it is never persistent shot state. Zero-time camera_track edits set this so Unreal
	// does not smear the previous view across an authored hard cut.
	bool bCameraCut = false;

	// The shot record's **presence flags** (`rec+0x20` bits `0x01` Start, `0x02` End, `0x04` Point1,
	// `0x08` Point2) and the `Target`-block count at `+0xd4`, carried onto the live shot because two
	// client-side decisions read them and nothing else can answer:
	//
	//   * shot start's arm test is `(flags & 2) == 0 || (flags & 1) != 0` — "no `End`, **or** has
	//     `Start`" — and the `else` seeds from the live view (`FUN_10002210`);
	//   * the `+0xd4 > 0` gate decides whether the angle is derived from the look-at at all; a shot
	//     with no `Target` block keeps its authored angles.
	//
	// The two `Point` flags are raised by **order of presence** (`flags |= 1 << (count + 2)`), which
	// is retail's bug and is reproduced at the parse (`FElysiumCameraShotDef`).
	bool bHasStartAnchor = false;
	bool bHasEndAnchor = false;
	bool bTargetPoint1Flagged = false;
	bool bTargetPoint2Flagged = false;
	int32 TargetPointCount = 0;

	// `m_ShotIndex` (`DT_BaseCineCam +0x630`, client `0x454`): **which shot record is live**, the
	// identity `OnDataChanged` compares against its cache `0x49c`. A change here arms the one-shot
	// snap *or* clears the three angle-settled flags, and issues the HUD edge — never the shot start.
	// Retail's `-1` (the mode clear `FUN_1006e0e0`) is `INDEX_NONE`, and every value shot carries it,
	// because a `camera_track` value has no row in `&DAT_106c8298`.
	int32 ShotIndex = INDEX_NONE;

	// `m_nClientResetFrame` (`+0x63c`, client `0x460`): **that a shot started**, stamped by `SetShot`
	// and by the shot start `FUN_1006e8e0` on every start including a re-shot of the same record. A
	// change here arms shot start and marks the camera active, and it clears **no** settle flags.
	// The two signals are deliberately separate: a re-shot of the same shot re-seeds the tracker
	// without re-snapping or re-acquiring the aim, and a shot swapped for a different record does the
	// converse.
	int32 ResetFrame = 0;

	// `+0x594`, the origin selector. `Entity` suppresses `AutoPositionFromTarget`.
	EElysiumShotOriginSelector OriginSelector = EElysiumShotOriginSelector::EndAnchor;

	// The shot's own HUD/viewmodel keys, and whether it is a named `vdata/camerashots/` shot at all.
	// Only the named channel authors these; every value producer leaves the default.
	FElysiumShotPresentation Presentation;

	// Retail's shot-start arm test, `FUN_10002210`'s first line: `(flags & 2) == 0 || (flags & 1) != 0`.
	// True takes the replicated-goal arm, false the live-view arm — so **only an `End`-without-`Start`
	// shot dollies in from wherever the player is looking**, which is the shipped `jack.txt` /
	// `dialogdefault.txt` / `centerfullview.txt` shape.
	bool StartsOnGoal() const { return !bHasEndAnchor || bHasStartAnchor; }

	// The name it was pushed under, for the debug read-out.
	FString DebugName;
};

// The client-side shot tracker — `C_BaseCineCamera`, ported whole.
//
// **The server resolves the goal; the client decides how much of it to take.** VtMB's camera think
// (`vampire.dll` `FUN_1006e8e0`) re-resolves all four anchors of the live shot every server tick
// (loop `0x1006ea90`, cache `this+0x598+i*12`) — `AttachType None` is *not* "sample once", it just
// reads that same per-tick cache back (`FUN_1006f010`). What holds a conversation camera still is
// therefore never the anchor mode; it is this tracker's deadbands.
//
// **Only a tracked shot is tracked.** `FElysiumCameraShot::bTracked` is retail's `CamMode == 1`
// test in `FUN_10001a20`; a direct shot (a `camera_track` value, a green-room pose) copies its
// origin and its look-at-derived angles through every frame, exactly as `CInput`'s override does.
//
// The three functions this is:
//   * position   `FUN_10001fe0` — hysteresis on `DistanceTolerance`, accel/decel on `MoveAccel`;
//   * angles     `FUN_10001d40` — per-axis hysteresis on `AngularTolerance[i]`;
//   * turn rate  `FUN_10001c80` — accel/decel on `TurnAccel` toward `MaxTurnRate[i]`, or the
//                                 `SyncRotateOnMove` timing solve.
// Shot start is `FUN_10002210`, the `SnapOnShotChange` hard copy `FUN_10002390`. Retail's fields:
// 0x410 goal origin, 0x41c look-at, 0x468 current origin, 0x474 current angles, 0x4a4 current
// speed, 0x4a8[3] per-axis turn rates, 0x4c0 position-settled, 0x4c1[3] per-axis angle-settled.
//
// Pure values: no world, no pawn, no `FMinimalViewInfo`, `DeltaTime` in — asserted headless by
// `Elysium.Substrate.Camera`.
struct FElysiumScriptedShotTracker
{
	// Where the camera actually is this frame. The goal is the shot; this is what renders.
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;

	// `m_flCurFov` (`0x480`), the FOV the frame renders. `FUN_10001c20` **copies** the shot record's
	// `FieldOfView` (`rec+0x100`) into it every frame — never a lerp, and the replicated `m_flFOV`
	// (`0x458`) is not consulted in `CamMode == 1`.
	float Fov = 0.0f;                                    // degrees, 4:3-referenced; 0 = keep the player's

	// `0x4a4` / `0x4a8[3]`, carried across frames because both are accelerated, not set.
	float Speed = 0.0f;                                  // cm/s
	FVector TurnRate = FVector::ZeroVector;              // deg/s, (pitch, yaw, roll)

	// `m_flDistRemaining` (`0x4c4`). The position step stores the distance from the current origin to
	// the goal **before** it moves (`FUN_10001fe0` `0x10002054`, an unconditional `FST` on both the
	// settled and the unsettled arm), and `SyncRotateOnMove` divides by *that* value the same frame
	// (`FUN_10001c80` `0x10001cb7` reads `this+0x4c4`). Recomputing it after the move would feed the
	// timing solve a distance one step short every frame, which shortens `T` and raises the turn rate
	// the shipped shots are tuned against (M6).
	float DistRemaining = 0.0f;                          // cm

	// `0x4c0` / `0x4c1[3]`. A settled axis uses the shot's tolerance as its deadband; an unsettled
	// one uses `UnsettledAngleTolerance` / `SettleDistance` below, so the camera parks precisely and
	// only comes out of the park on a real drift.
	bool bPositionSettled = true;
	bool bPitchSettled = false;
	bool bYawSettled = false;
	bool bRollSettled = false;

	// `m_bSnapPending` (`0x4a1`). Retail arms it in exactly two places — the shot-index change in
	// `OnDataChanged` (`0x100024c0`) and the tail of shot start (`FUN_10002210`), both when the
	// record carries `SnapOnShotChange` (flags `0x80`) — and `FUN_10001a20` **consumes** it at the
	// top of the very next rendered frame. It is a one-shot: the shot cuts on the change and then
	// tracks its anchor normally, which is what `sp_tutorial_1`'s `LookAtTarget_Snap` needs.
	bool bSnapPending = false;

	bool bSeeded = false;

	// `FUN_10002210`, **both arms** (SC5). `LiveView` is `CViewRender::GetViewSetup()`'s origin and
	// angles, supplied by the caller because the tracker reaches nothing:
	//
	//   * `(flags & 2) == 0 || (flags & 1) != 0` — no `End`, or has `Start` — seeds the current pose
	//     from the **replicated goal**, so the shot begins where it was authored;
	//   * otherwise — `End` **without** `Start` — seeds it from `LiveView`, so the shot **dollies in
	//     from wherever the player is looking**, under its own `MoveSpeed`/`MoveAccel`.
	//
	// Then: position settled, all three angle axes **unsettled**, speed and the three turn rates 0,
	// and `bSnapPending` from `SnapOnShotChange`. Neither FOV field is touched by shot start.
	//
	// The default argument is the zero view, which only a caller with no view at all can take; every
	// such caller pushes shots that carry a `Start` or no `End`, so it never reaches the seed.
	void Start(const FElysiumCameraShot& Shot, const FElysiumViewSetup& LiveView = FElysiumViewSetup());

	// One rendered frame — `C_BaseCineCamera::Update` (`FUN_10001a20`). The look-at is solved against
	// **this tracker's own current origin** (retail: `VectorAngles(lookAt - currentOrigin)`), after
	// the position step, because a look-at shot re-derives its aim as it dollies.
	//
	// **`DeltaSeconds` is guarded here, not by the caller.** Retail computes its own delta from
	// `engine->GetCurTime() - m_flLastTime`, clamps it to `FrameDeltaCeiling` and replaces anything
	// below `FrameDeltaFloor` with `FrameDeltaFloor` — the compare constant and the stored literal
	// are the same 0.01 (RC9), which is why one constant covers both and why a zero or a negative
	// delta needs no separate arm. The caller supplies the frame's delta and nothing else; the
	// tracker never reads a clock.
	//
	// `CameraFovCvar` is retail's `camera_fov` ConVar value, read from the VtMB console store by the
	// caller (`FElysiumCameraCvars::CameraFov`) and passed in rather than looked up, because the
	// tracker reaches nothing. Its default `-1` is retail's, and it never fires a shipped run.
	void Advance(const FElysiumCameraShot& Shot, float DeltaSeconds, float CameraFovCvar = -1.0f);

	// The hard copy — `FUN_10002390`. Goal -> current for origin and angles, position settled, speed
	// and the three turn rates zeroed, all three angle axes marked **unsettled** (so the aim
	// re-acquires at the tight 1 degree band), and the angles re-derived from the look-at **only for
	// a tracked shot** (retail's `CamMode == 1` test). Clears `bSnapPending` on the way out — it is
	// the one-shot's consume, and calling it directly is legal (retail's `FUN_10002390` has no other
	// precondition).
	void Snap(const FElysiumCameraShot& Shot);

	// `FUN_10001c20`. Returns the FOV this frame renders and, in the ordinary path, copies the shot
	// record's `FieldOfView` into `Fov`. Under the `camera_fov` guard it returns the cvar **without
	// writing `Fov`**, so the rendered FOV freezes at whatever it last was — retail's own behaviour,
	// reproduced (M12). `CameraFovCvar` is that cvar's value; its retail default is `-1`.
	float TrackFov(const FElysiumCameraShot& Shot, float CameraFovCvar = -1.0f);

	// `FUN_100019a0`'s first test: `m_flSpeed <= 1.0` (a *double* compare at `0x101e34e0`) reads as
	// "the camera has stopped dollying", and it is clean precisely because `MinTrackSpeed` is the
	// tracker's own speed floor — an unsettled camera can never sit below it. SC5's viewmodel
	// predicate is `!IsDollying() && Shot.bDrawViewmodel`.
	bool IsDollying() const { return Speed > MinTrackSpeed; }

	// The unsettled deadbands, retail's literals: 1.0 Source unit of position and **1.0 degree** of
	// angle (`_DAT_101e34ec`), so an acquiring camera lands on its target rather than near it. The
	// settled deadbands are the shot's own `DistanceTolerance` / `AngularTolerance`.
	static constexpr float SettleDistance = 1.0f * 2.54f;             // cm  (1.0 u)
	static constexpr float UnsettledAngleTolerance = 1.0f;            // deg (`_DAT_101e34ec`)

	// `clamp(speed, 1.0f, MoveSpeed)` at `0x10002137`-`0x1000215a`: an unsettled camera never runs
	// slower than 1 u/s. Load-bearing twice over — it is the floor a zero `MoveAccel` pins the speed
	// at (M7), and it is what makes `FUN_100019a0`'s `speed <= 1.0` a clean dolly test.
	static constexpr float MinTrackSpeed = 1.0f * 2.54f;              // cm/s (1.0 u/s)

	// The frame-delta guards from `FUN_10001a20`. `dt > 1.0 => 1.0` (`FCOMP _DAT_101e34ec`, then
	// `MOV 0x3f800000`); `dt < 0.01 => 0.01` (`FCOMP _DAT_101e34e8` at `0x10001a75`, then
	// `MOV 0x3c23d70a`) — the floor's compare constant and its stored literal are **the same
	// 0.01** (RC9 read `_DAT_101e34e8` as `0a d7 23 3c`; the earlier `1/255` reading was wrong), and
	// it covers zero and negative, which is why there is no separate early-out.
	static constexpr float FrameDeltaCeiling = 1.0f;                  // s  (`_DAT_101e34ec`)
	static constexpr float FrameDeltaFloor = 0.01f;                   // s  (`_DAT_101e34e8`, and the
	                                                                  //     immediate 0x3c23d70a)

	// `_DAT_101e34f4`, the `camera_fov` guard's threshold: the cvar wins only while
	// `FovOverrideThreshold < camera_fov`. RC9 read it as `00 00 20 41` = **10.0**, and `camera_fov`'s
	// own default is `-1`, so the guard never fires in a shipped run.
	static constexpr float FovOverrideThreshold = 10.0f;
};

// `C_BaseCineCamera::OnDataChanged` `0x100024c0`'s **first two arms**, which are two signals and not
// one (SC5). The third arm is the HUD's and lives in `FElysiumShotHudGate`.
//
//     if (m_nClientResetFrameCache /*0x498*/ != m_nClientResetFrame /*0x460*/) {
//         m_bShotStartPending /*0x4a2*/ = 1;  m_bActive /*0x465*/ = 1;
//     }
//     if (m_ShotIndexCache /*0x49c*/ != m_ShotIndex /*0x454*/) {
//         if (flags & 0x80) m_bSnapPending = 1;                  // SnapOnShotChange
//         else              m_bAngleSettled[0..2] = 0;           // re-acquire all three axes
//         ... the HUD arm ...
//     }
//
// **A reset frame is "a shot started"** — a push, or `SetShot` re-run on the camera already up — and
// it clears no settle flag. **A shot index is "a different record"** — and it snaps *or* re-acquires,
// never both, and never re-seeds the pose. Retail deliberately does **not** write the reset-frame
// cache back here: that write belongs to shot start (`FUN_10002210`), so the pending flag survives
// until the shot actually starts, which is why `bShotStartPending` is consumed by the caller and not
// here.
//
// One struct for both channels (the legacy stack in `UElysiumCameraComponent` and the request
// channel in `UElysiumCameraService`) because it is one retail function, and asserted headless by
// `Elysium.Substrate.CameraShotStart`.
struct FElysiumShotStartEdges
{
	// `m_bShotStartPending` (`0x4a2`). The caller consumes it by running shot start.
	bool bShotStartPending = false;

	// Both `OnDataChanged` arms against the live shot, applying the shot-index arm to `Tracker`.
	// Returns true when the shot index changed this call, which is the caller's cue to run the HUD
	// edge over the same change.
	bool OnDataChanged(const FElysiumCameraShot& Shot, FElysiumScriptedShotTracker& Tracker);

	// The camera going away: retail destroys the entity, so both caches go with it.
	void Reset() { *this = FElysiumShotStartEdges(); }

	int32 ResetFrameCache() const { return CachedResetFrame; }
	int32 ShotIndexCache() const { return CachedShotIndex; }

private:
	// `0x498` and `0x49c`.
	int32 CachedResetFrame = 0;
	int32 CachedShotIndex = INDEX_NONE;
};

// The channel. Push/pop is **handle-based, not LIFO** — a conversation ends behind a cutscene that
// is still running, exactly like the input-scope stack — so a pop removes a shot from
// wherever it sits and the top re-resolves. Ids are never reused, so a stale or doubled pop is a
// no-op.
//
// **Two channels, not one weighted list** (`docs/vtmb/camera-view-modes.md`, SC2). The entries share
// one array because push/pop ordering is shared, but they answer separately: `TopCine()` is the
// adopted camera — no weight, a hard write, and its release is a cut — while `TopTrack()` is the
// `camera_track` override, the only ramped one.
class FElysiumCameraShotStack
{
public:
	// Returns the shot's id (from 1). A **track** shot arms the ramp over its own `BlendSeconds`,
	// back-dated so an in-flight weight is preserved rather than restarted (retail's re-time,
	// `vampire.dll` `FUN_1017d0b0`). A **cine** shot arms nothing: it is live at full weight the
	// instant it is pushed.
	//
	// A push **is** a shot start, so the entry is stamped with a fresh `ResetFrame` — retail's
	// `m_nClientResetFrame`, written by `SetShot` and by `FUN_1006e8e0` — whatever the caller put in
	// the value it handed over.
	int32 Push(const FElysiumCameraShot& Shot);

	// Refresh a live shot's values — what a `Follow` attach type is. False for an id that is not up.
	//
	// This is the **think**, not a shot start: it re-publishes the goal and deliberately **preserves
	// the entry's `ResetFrame`**, exactly as retail's mode-1 think re-publishes `m_vecCamOrigin` every
	// tick without touching `m_nClientResetFrame`. Re-seeding the tracker off a `Follow` re-resolve
	// would restart the dolly every frame.
	bool Update(int32 Id, const FElysiumCameraShot& Shot);

	// **A re-shot of the shot already up** — `SetShot` on a live camera (`FUN_1006e130` +
	// `FUN_1006e8e0`), which stamps `m_nClientResetFrame` again without changing the entity or its
	// id. Bumps the entry's `ResetFrame`, so the consumer arms shot start; it changes no other state,
	// so whether the aim re-acquires is decided by `m_ShotIndex` as it is in retail. False for an id
	// that is not up.
	bool Restart(int32 Id);

	// Remove a shot.
	//
	// A **cine** shot is released **instantaneously** — M1, ruled: retail returns control on the same
	// tick the camera dies (`FUN_10070990`, `EndPlayerDialog` `0x10178400`, `InputRemoveCamera`
	// `0x10171f10`), no blend field exists anywhere on `C_BaseCineCamera`, and `BlendOutSeconds` is
	// therefore not consulted at all. A **track** shot that empties the track channel arms retail's
	// negative-duration blend-out over `BlendOutSeconds` (or its own `BlendSeconds` when the argument
	// is negative); one popped from under another track shot leaves the ramp where it is.
	bool Pop(int32 Id, float BlendOutSeconds = -1.0f);

	// Drop everything, weight included — a map teardown, `RestoreCameraToPlayerControl` in the large.
	void Clear();

	int32 Num() const { return Shots.Num(); }
	bool IsActive() const { return Shots.Num() > 0 || GetTrackWeight() > 0.0f; }

	// The deciding shot overall: the most recently pushed one still up, or null / 0. What the
	// presentation keys and the debug read-out come from.
	const FElysiumCameraShot* Top() const { return Shots.Num() > 0 ? &Shots.Last().Shot : nullptr; }
	int32 TopId() const { return Shots.Num() > 0 ? Shots.Last().Id : 0; }

	// The adopted cine camera (`m_iCameraOverrideIdx`'s entity), or null. One slot in retail; the
	// most recent push here until SC4 rebuilds the slot.
	const FElysiumCameraShot* TopCine() const;
	int32 TopCineId() const;

	// The `camera_track` override, or null.
	const FElysiumCameraShot* TopTrack() const;
	int32 TopTrackId() const;

	const FElysiumCameraShot* Find(int32 Id) const;

	// Advance the channel's own clock. The weight is a **function** of that clock and the two stored
	// ramp fields, exactly as retail's is of `engine->GetCurTime()` — nothing integrates.
	void Advance(float DeltaSeconds);

	// The track channel's ramp — `CInput+0x100`, `FUN_100fc900`'s tail. **Linear**; the
	// `SimpleSpline` ease belongs at the compose site, not here.
	float GetTrackWeight() const;

	// What `CAM_IsThirdPerson` and the layer alpha read: an adopted cine camera counts as full
	// scripted weight (it has none of its own), otherwise the track ramp.
	float GetWeight() const { return TopCine() ? 1.0f : GetTrackWeight(); }

	// Retail's ramp fields, for the debug read-out and for SC3's fade machine to build on.
	float RampStartSeconds() const { return RampStartTime; }
	float RampDurationSeconds() const { return RampDuration; }

	FString Describe() const;

	// The ramp's symmetric dead band, `|duration| <= 0.01 s` (RC9: `_DAT_101e34e8` = `+0.01`,
	// `_DAT_10235278` = `-0.01`, both read byte-exact out of `client.dll`). Inside it the weight is
	// **1 and stays 1** — a hard cut in — in either sign, so a "blend out" shorter than 10 ms is not
	// a fast fade, it is no fade at all. "Off" is not encoded here: it is `startTime <= 0`.
	static constexpr float RampDeadBandSeconds = 0.01f;

private:
	struct FEntry
	{
		int32 Id = 0;
		FElysiumCameraShot Shot;
	};

	// Arm the ramp, preserving the current weight by back-dating the start rather than writing the
	// weight — retail's `FUN_1017d0b0`. Two entry points rather than one signed argument, because the
	// direction has to survive a zero: `-0.0f >= 0.0f` is true, so a zero-length release and a
	// zero-length arrival are not distinguishable by sign.
	void ArmRampIn(float Seconds);
	void ArmRampOut(float Seconds);

	TArray<FEntry> Shots;

	// **Retail's own encoding** (M5, ruled): the pair of replicated fields
	// `m_flCameraOverrideFadeStartTime` (local `+0x114`) and `m_flCameraOverrideFadeDuration`
	// (`+0x118`), with the **sign of the duration** as the stored direction. `FUN_100fc900`'s tail:
	//   `startTime <= 0`            => weight 0, the override is off;
	//   `|duration| <= 0.01`        => weight 1, immediately, and it stays;
	//   `duration >  0.01`          => `(now - startTime)/duration`;
	//   `duration < -0.01`          => `1 + (now - startTime)/duration`, the blend out;
	// then clamp to [0,1]. `Pop(Id, BlendOutSeconds)` survives only as a facade that writes these.
	float RampStartTime = 0.0f;
	float RampDuration = 0.0f;

	// The channel's clock, standing for `engine->GetCurTime()`. It is advanced by the frame's
	// `DeltaSeconds` and never read from a real one (the substrate rule). It starts **above zero**
	// because `startTime <= 0` is retail's "off" sentinel, so a legitimate stamp has to be strictly
	// positive — retail gets that for free from an engine time that is never 0 while a map is up.
	float RampNow = 1.0f;

	int32 NextId = 1;

	// `m_nClientResetFrame`'s source. Retail stamps the server's frame number; what the client reads
	// is only "it differs from the cached one", so a monotonic counter is the same signal. It starts
	// at 1 because a shot's default `ResetFrame` is 0 and a consumer's cache starts there too.
	int32 NextResetFrame = 1;
};

// The cvar surface

// VtMB's camera cvars, reproduced 1:1 by name and default (`docs/vtmb/camera-view-modes.md` §1). They are
// **declared into the VtMB console store**, not registered as `elysium.*` engine cvars, so a user's
// `config.cfg` and the patch's `cam_restore` / `cam_rotateleft` aliases keep governing and
// `elysium.cmd cam_idealdist 50` is the same write the game itself would make.
//
// Values are held in Source units, exactly as they are typed; `FElysiumCameraCvars` converts once
// when it reads them.
struct FElysiumCameraCvars
{
	// The boom. Distances arrive in Source units and are exposed here in **cm**.
	float IdealDist = 85.0f * ElysiumCam::U;      // cam_idealdist 85
	float MinDistance = 30.0f * ElysiumCam::U;    // c_mindistance 30
	float MaxDistance = 200.0f * ElysiumCam::U;   // c_maxdistance 200
	float TargetAngle = 15.0f;                    // cam_targetangle 15 (degrees above the eye line)
	float Yaw = 0.0f;                             // cam_yaw

	// Orbit clamps, degrees, in Source's own sign convention (pitch positive = down).
	float MinPitch = 0.0f;                        // c_minpitch
	float MaxPitch = 90.0f;                       // c_maxpitch
	float MinYaw = -135.0f;                       // c_minyaw
	float MaxYaw = 135.0f;                        // c_maxyaw

	// Collision.
	bool bCollide = true;                         // cam_collide 1
	float TraceRadius = 9.0f * ElysiumCam::U;     // cam_trace_radius 9

	// The player-model fade band.
	float FadeStart = 32.0f * ElysiumCam::U;      // cam_fadestart 32
	float FadeEnd = 18.0f * ElysiumCam::U;        // cam_fadeend 18

	// The strafe bank. Degrees, and a speed in cm/s above which the bank is at full angle.
	float RollAngle = 2.0f;                       // cl_rollangle 2
	float RollSpeed = 200.0f * ElysiumCam::U;     // cl_rollspeed 200

	// The water clearance band `SolveWaterOffset` keeps the view out of (R7.1).
	float WaterDist = 4.0f * ElysiumCam::U;       // cl_waterdist 4

	// The lenses. **Both are 4:3-referenced horizontal angles and both are Hor+**, exactly like a
	// `vdata/camerashots/` `FieldOfView`: Source holds the vertical angle the 4:3 reference implies
	// (`vfov = 2*atan(tan(hfov/2)/(4/3))`, `docs/vtmb/source_movement.md` -> "View / camera") and a
	// wider window earns more horizontal. `ElysiumCam::WidenSourceFov` is the one conversion, so the
	// player view and every scripted shot lerp against each other in the same space.
	float DefaultFov = 75.0f;                     // default_fov 75 — the player view's lens
	// `viewmodel_fov 54` — the first-person weapon's own projection. **The seam, answering nothing
	// yet**: the port has no first-person viewmodel renderer at all (only `SolveDrawPolicy`'s
	// `bViewmodelEligible` gate, which has no draw consumer), so the value is declared, loaded and
	// readable and nothing projects with it. It stands for retail's `viewmodel_fov` ConVar.
	float ViewmodelFov = 54.0f;

	// `camera_fov`, the cine-FOV guard (`client.dll` ConVar object `0x102de308`, name string
	// `0x10270c88`, default `"-1"`, flags 0 — RC9). Above
	// `FElysiumScriptedShotTracker::FovOverrideThreshold` (10) the scripted-shot FOV **freezes**
	// rather than following the cvar, which is retail's behaviour and not a defect to smooth over
	// (M12). Degrees, unconverted; the default never fires.
	float CameraFov = -1.0f;

	// `c_orthowidth` / `c_orthoheight` (`client.dll` `0x100fb240`, both `FCVAR_ARCHIVE`, default
	// `"100"` — RC9). Source's orthographic debug view, toggled by the `camortho` command
	// (`FUN_101001e0`) through `CInput+0x1b8`: `CViewSetup::m_bOrtho` (`+0x16`) is raised and the
	// rect at `+0x18..0x24` is written `(-w*0.5, -h*0.5, w*0.5, h*0.5)`. Source units in, cm here.
	//
	// **This is not an off-centre projection.** The earlier reading of the block as `bOffCenter` was
	// wrong on both the flag and the offsets (M11, re-scoped by RC9).
	float OrthoWidth = 100.0f * ElysiumCam::U;
	float OrthoHeight = 100.0f * ElysiumCam::U;

	// The spring damper. **Two constants** — stiffer against a wall than in open space — which is the
	// single most characteristic part of the VtMB camera and the reason the stock spring arm is not
	// enough (`docs/vtmb/camera-view-modes.md` § Options considered and rejected).
	bool bDampOn = true;                          // cdamp_on 1
	float HookesConstant = 4.0f;                  // cdamp_hookesconstant
	float HookesConstantWall = 15.0f;             // cdamp_hookesconstantwall
	float SpringLength = 0.1f * ElysiumCam::U;    // cdamp_springlength
	float DampMaxDist = 50.0f * ElysiumCam::U;    // cdamp_maxdist

	// The orbit/dolly step rates and the wall pull-in are **not here**. No ConVar holds any of them —
	// `CAM_Think` polls the `kbutton_t`s and steps the values directly (`0x100fc170`), and retail
	// keeps the pull-in as a code constant — so they are the project's and live on
	// `ElysiumRig::FElysiumCameraRigTuning`. Carrying them in a struct named for the retail cvar
	// surface would imply a name a user could type, and there is none.

	// Ordinary feed (`client.dll` 0x100fe7f0). Distances are converted to centimetres here; angles
	// retain Source's down-positive convention until the solve emits an Unreal rotator.
	float FeedYaw = 50.0f;
	float FeedYawEnd = 2.0f;                       // registered, unused by the recovered solve
	float FeedPitch = 80.0f;
	float FeedPitchMin = 0.0f;                     // registered, unused by the recovered solve
	float FeedPitchMax = 60.0f;
	float FeedPitchPow1 = 2.0f;
	float FeedPitchPow2 = -0.35f;
	float FeedRoll = 0.0f;
	float FeedForwardBase = -50.0f * ElysiumCam::U;
	float FeedForwardPow = 0.5f;

	// Re-read the whole surface. `Lookup` returns a cvar's value string or empty for one the store
	// does not carry, which is how the console itself answers; an empty read keeps the default, so a
	// run with no `out/cfg` on disk behaves exactly like a stock install. Taking the reader as a
	// callback is what keeps this header free of the console (and testable with a hand-built store).
	void LoadFrom(TFunctionRef<FString(const TCHAR*)> Lookup);
};

namespace ElysiumCam
{
	// The shot's exposure policy, as the two post-process numbers it becomes. False leaves the
	// scene's own auto-exposure alone and does not touch `OutMin`/`OutMax`; true writes the same
	// brightness into both, which is what pins the eye. Pure so the clamp is assertable with no
	// camera (`Elysium.Substrate.CameraShots`).
	bool SolveExposureClamp(const FElysiumShotPresentation& Presentation, float& OutMin,
		float& OutMax);

	// The player-body fade band alone (`CInput+0x104`). True first person is zero, because the band
	// reads the third-person boom and that boom is scaled to nothing while the weight is zero.
	float SolveModelAlpha(const FVector& SolvedOffset, const FElysiumCameraWeights& Weights,
		const FElysiumCameraCvars& Cvars);

	// Weapon-class arbitration (`docs/vtmb/camera-view-modes.md` §2).
	// The authored `camera_class` bits, from the inlined case-**sensitive** `memcmp` ladder in the
	// item-record vdata parser (`client.dll` 0x101a5394-0x101a5438, byte-identical in `vampire.dll`).
	namespace CameraClass
	{
		inline constexpr int32 None = 0;
		inline constexpr int32 Ranged = 0x02;      // 18 shipped items; settable under `camera_prefs`
		inline constexpr int32 Thrown = 0x04;      // 4 shipped items; settable
		inline constexpr int32 ForceFirst = 0x08;  // `force_1st` — the lockpick and 3 dev items
		inline constexpr int32 ForceThird = 0x10;  // `melee`, and `force_3rd` under a second spelling
	}

	// Parse one authored literal. **`noswitch` is not a recognized literal**: it reaches 0 through the
	// same fall-through as a typo or a missing key, so a mis-cased `Ranged` silently reads 0. That is
	// authored-vocabulary convention rather than a checked enum, and it is reproduced, not repaired.
	// Bit `0x01` is dead — no ladder arm produces it and no item carries it, which is why the shipped
	// `camera_prefs 7` and the default `6` differ only in a bit nothing can match.
	int32 ParseCameraClass(const FString& Literal);

	// What equipping a weapon of this class asks the camera to do.
	enum class EWeaponCameraAction : uint8
	{
		None,            // class 0, or `camera_weaponswitch 0` — the early-out
		ToFirstPerson,
		ToThirdPerson,
		ForceThirdOn,    // `melee`: not a preference, a hold the player cannot toggle out of
	};

	// `ApplyWeaponCameraPref` (`0x1009c250`), verbatim.
	EWeaponCameraAction ApplyWeaponCameraPref(int32 Class, int32 Prefs, bool bWeaponSwitch);

	// `SaveWeaponCameraPref` (`0x1009c2e0`), verbatim. **A set bit means first person.** Classes
	// `0x08`, `0x10` and 0 are not user-settable and return `Prefs` unchanged, which is what makes
	// the preference sticky per class rather than global.
	int32 SaveWeaponCameraPref(int32 Class, int32 Prefs, bool bThirdPerson);

	// The frame state the two cine draw gates read, beside the shot's own keys: whether a
	// `C_BaseCineCamera` is **adopted** at all (`GetCineCamera() != NULL`, and *not* whether it is
	// active — retail's test does not call `IsActive`), whether its tracker is still translating
	// (`m_flSpeed > 1.0`, `FElysiumScriptedShotTracker::IsDollying`), and the latched HUD state.
	struct FElysiumShotDrawState
	{
		bool bCineAdopted = false;
		bool bDollying = false;
		bool bHudVisible = true;
	};

	// **The one resolve of the switch-frame table.** Every draw decision in the frame comes from here
	// and from nowhere else, so the body, the world weapon, the viewmodel seam and the reticle cannot
	// drift apart. Pure: no pawn, no world, no RHI, asserted in `Elysium.Substrate.CameraDraw` and
	// `Elysium.Substrate.CameraDrawGates`.
	FElysiumCameraDrawPolicy SolveDrawPolicy(const FElysiumCameraWeights& Weights,
		const FVector& SolvedOffset, const FElysiumCameraCvars& Cvars,
		const FElysiumShotPresentation& Shot,
		const FElysiumShotDrawState& State = FElysiumShotDrawState());

	// `T` is scaled seconds since the first feed frame and EntryYaw is the rendered yaw captured on
	// that frame. There is deliberately no collision query and no live look input in this solve.
	FElysiumFeedCameraPose SolveOrdinaryFeedCamera(float T, float EntryYaw,
		const FElysiumCameraCvars& Cvars);

	// `CInput::OverrideView` (`client.dll` `FUN_100ffb90`, slot 33), verbatim — the **track** channel
	// composed over whatever base won, and the last term applied to the one camera VtMB has.
	//
	//     e = SimpleSpline(w)                                   // the ease is HERE, the ramp is linear
	//     AngleVectors(angles, fwd)
	//     viewFwdPoint = origin + fwd * 240u                    // `_DAT_1022b298`
	//     origin       = origin + (shotOrigin - origin) * e
	//     dir          = lerp(viewFwdPoint, shotTarget, e) - origin
	//     VectorAngles(normalize(dir), angles)
	//     angles.roll  = e * shotRoll                           // the base roll is DISCARDED
	//     fov          = fov + (shotFov - fov) * e
	//
	// Two properties this shape has and a rotator lerp does not: the aim is interpolated as a
	// **point**, so a shot swings faster at the start and settles rather than sweeping uniformly; and
	// the base view's roll is thrown away outright the instant the weight is non-zero. The authored
	// `FromPlayerTime` values were tuned against exactly this (M9), so it is contract.
	//
	// Taking the three view values rather than an `FMinimalViewInfo` is what keeps this header on
	// Core types, so the composition is asserted with no engine view struct — and it is the reason
	// the same function serves the faithful evaluator and the modern rig without either owning it.
	// A `Weight` at or below zero leaves all three untouched; a `ShotFov` at or below zero keeps the
	// player's field of view, which is what a shot file with no `FieldOfView` authors.
	void ComposeScriptedShot(FVector& InOutLocation, FRotator& InOutRotation, float& InOutFov,
		const FVector& ShotLocation, const FVector& ShotTarget, float ShotRoll, float ShotFov,
		float Weight);

	// The same, for a shot that carries a rotation instead of a look-at point: the target becomes
	// `ScriptedShotTargetPoint(ShotLocation, ShotRotation)` and the roll the rotation's own, so the
	// two authoring forms reach the same pose at full weight.
	void ComposeScriptedShot(FVector& InOutLocation, FRotator& InOutRotation, float& InOutFov,
		const FVector& ShotLocation, const FRotator& ShotRotation, float ShotFov, float Weight);

	// The 4:3 reference a Source-authored field of view is written against.
	inline constexpr float SourceFovAspect = 4.0f / 3.0f;

	// **A `vdata/camerashots/` `FieldOfView` is a 4:3-referenced horizontal angle**, not the angle a
	// widescreen window renders. Source is Hor+: the vertical angle is fixed by the 4:3 reference and
	// a wider window earns more horizontal, which is why retail's own `default_fov 75` renders ~91
	// degrees at 16:9 (`docs/vtmb/source_movement.md` -> "View / camera",
	// `docs/vtmb/camera-view-modes.md` §5). Unreal's `FMinimalViewInfo::FOV` is the horizontal angle
	// *at the current aspect*, so handing the authored number straight over renders a shot ~1.4x too
	// tight on a 16:9 display — the "the dialogue camera is closer than retail" reading.
	//
	// This is retail Source FOV semantics, not a modernization; the player view is the one that
	// diverges, by authoring an absolute 90-degree horizontal baseline instead of a 4:3 reference.
	// A FOV at or below zero (no `FieldOfView` key, a value shot) passes through untouched.
	float WidenSourceFov(float SourceFovDegrees, float AspectRatio);

	// One row of the cvar surface: the VtMB name, its default **as typed** (Source units / degrees /
	// a flag), and what it does. The table is the declaration; `FElysiumCameraCvars::LoadFrom` is the
	// read. Ordered as `docs/vtmb/camera-view-modes.md` §1 lists them.
	struct FCvarDef
	{
		const TCHAR* Name;
		const TCHAR* Default;
		const TCHAR* Help;
	};
	TArrayView<const FCvarDef> CvarDefs();
}
