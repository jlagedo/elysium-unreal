#pragma once

#include "CoreMinimal.h"

// The camera's rule set (roadmap 11.7, `docs/vtmb/camera-view-modes.md`).
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

	// Source's rate-limited approach (`0x100fc000`): push Current toward Target by at most
	// `Speed * Dt`. Nothing eases — the clamp is the whole smoothing, which is why the camera reads as
	// mechanical until the spring damper runs over it. A Speed of 0 snaps.
	float Approach(float Current, float Target, float Speed, float Dt);

	// The same, in angle space, so a 359 -> 1 step is 2 degrees rather than 358.
	float ApproachAngle(float Current, float Target, float Speed, float Dt);

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
}

// The full-strength ordinary feed camera, relative to the player's eye.
struct FElysiumFeedCameraPose
{
	FVector Offset = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
};

// --------------------------------------------------------------------------------------------
// The four weights
// --------------------------------------------------------------------------------------------

// `CInput`'s camera block (`docs/vtmb/camera-view-modes.md` §2). Four weights and three latches; there is no
// state machine and no transition object, which is exactly why reversing mid-blend resumes from
// where it is instead of restarting.
struct FElysiumCameraWeights
{
	// --- The latches (CInput +0xf0 / +0xf8 / +0xf9) -----------------------------------------
	// The user's own toggle. `togglecamera`, `thirdperson` and `firstperson` all write this.
	bool bUserThird = false;
	// Weapon-class arbitration forced third person (class 0x10), and the feed camera's own hold.
	bool bForcedThird = false;
	// Weapon-class arbitration forced first person (class 0x08).
	bool bForcedFirst = false;
	// The feed / seduction / death camera's request latch. Ordinary feed uses the recovered solver
	// below; the other two producers still share only this weight channel.
	bool bFeed = false;

	// --- The weights (CInput +0xfc / +0x100 / +0x108 / +0x138) ------------------------------
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
	// delta by the world's dilation by the time this is reached — the same property 11.1's clock
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

// --------------------------------------------------------------------------------------------
// The draw policy
// --------------------------------------------------------------------------------------------

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

	// Named-shot policy only. An ordinary mode toggle and a `camera_track` both leave this true.
	bool bShowHud = true;
};

// --------------------------------------------------------------------------------------------
// The scripted-shot channel
// --------------------------------------------------------------------------------------------

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

	// `CInput+0x194` / `+0x198`: the shot's own roll and field of view, both lerped by the weight. A
	// FieldOfView of 0 keeps the player's.
	float Roll = 0.0f;
	float FieldOfView = 0.0f;

	// The timed ramp's duration (`+0x118`) — cutscene cameras get an explicit duration, unlike the
	// toggle's fixed-rate blend. 0 snaps.
	float BlendSeconds = 0.5f;

	// The shot file's `CameraConstraints`, in engine units: how fast the camera is allowed to chase
	// its own target once the shot is live. 0 = snap to it every frame.
	float MoveSpeed = 0.0f;                                   // cm/s
	FVector MaxTurnRate = FVector(90.0f, 90.0f, 90.0f);       // deg/s, (pitch, yaw, roll)

	// One-shot render-history reset. The camera component consumes and clears it while publishing
	// the value; it is never persistent shot state. Zero-time camera_track edits set this so Unreal
	// does not smear the previous view across an authored hard cut.
	bool bCameraCut = false;

	// The shot's own HUD/viewmodel keys, and whether it is a named `vdata/camerashots/` shot at all.
	// Only the named channel authors these; every value producer leaves the default.
	FElysiumShotPresentation Presentation;

	// The name it was pushed under, for the debug read-out.
	FString DebugName;
};

// The channel. Push/pop is **handle-based, not LIFO** — a conversation ends behind a cutscene that
// is still running, exactly like the input-scope stack (11.5) — so a pop removes a shot from
// wherever it sits and the top re-resolves. Ids are never reused, so a stale or doubled pop is a
// no-op.
class FElysiumCameraShotStack
{
public:
	// Returns the shot's id (from 1). The weight ramps in over the shot's own BlendSeconds.
	int32 Push(const FElysiumCameraShot& Shot);

	// Refresh a live shot's values — what a `Follow` attach type is. False for an id that is not up.
	bool Update(int32 Id, const FElysiumCameraShot& Shot);

	// Remove a shot. When it was the top one, the weight ramps back out over its BlendSeconds (or
	// toward whatever is left underneath, which stays at full weight).
	bool Pop(int32 Id, float BlendOutSeconds = -1.0f);

	// Drop everything, weight included — a map teardown, `RestoreCameraToPlayerControl` in the large.
	void Clear();

	int32 Num() const { return Shots.Num(); }
	bool IsActive() const { return Shots.Num() > 0 || Weight > 0.0f; }

	// The deciding shot: the most recently pushed one still up, or null / 0.
	const FElysiumCameraShot* Top() const { return Shots.Num() > 0 ? &Shots.Last().Shot : nullptr; }
	int32 TopId() const { return Shots.Num() > 0 ? Shots.Last().Id : 0; }
	const FElysiumCameraShot* Find(int32 Id) const;

	// Advance the timed ramp. Toward 1 while a shot is up, toward 0 when none is.
	void Advance(float DeltaSeconds);

	float GetWeight() const { return Weight; }

	FString Describe() const;

private:
	struct FEntry
	{
		int32 Id = 0;
		FElysiumCameraShot Shot;
	};

	TArray<FEntry> Shots;
	float Weight = 0.0f;
	// The ramp duration in force: the top shot's while one is up, the last-popped shot's on the way
	// out (so a shot fades away as fast as it arrived).
	float RampSeconds = 0.5f;
	int32 NextId = 1;
};

// --------------------------------------------------------------------------------------------
// The cvar surface
// --------------------------------------------------------------------------------------------

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

	// The player-model fade band (used once a player mesh exists — 8.11).
	float FadeStart = 32.0f * ElysiumCam::U;      // cam_fadestart 32
	float FadeEnd = 18.0f * ElysiumCam::U;        // cam_fadeend 18

	// The strafe bank. Degrees, and a speed in cm/s above which the bank is at full angle.
	float RollAngle = 2.0f;                       // cl_rollangle 2
	float RollSpeed = 200.0f * ElysiumCam::U;     // cl_rollspeed 200

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
	// The player-body fade band alone (`CInput+0x104`). True first person is zero, because the band
	// reads the third-person boom and that boom is scaled to nothing while the weight is zero.
	float SolveModelAlpha(const FVector& SolvedOffset, const FElysiumCameraWeights& Weights,
		const FElysiumCameraCvars& Cvars);

	// --- Weapon-class arbitration (`docs/vtmb/camera-view-modes.md` §2) -------------------------
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

	// **The one resolve of the switch-frame table.** Every draw decision in the frame comes from here
	// and from nowhere else, so the body, the world weapon, the viewmodel seam and the reticle cannot
	// drift apart. Pure: no pawn, no world, no RHI, asserted in `Elysium.Substrate.CameraDraw`.
	FElysiumCameraDrawPolicy SolveDrawPolicy(const FElysiumCameraWeights& Weights,
		const FVector& SolvedOffset, const FElysiumCameraCvars& Cvars,
		const FElysiumShotPresentation& Shot);

	// `T` is scaled seconds since the first feed frame and EntryYaw is the rendered yaw captured on
	// that frame. There is deliberately no collision query and no live look input in this solve.
	FElysiumFeedCameraPose SolveOrdinaryFeedCamera(float T, float EntryYaw,
		const FElysiumCameraCvars& Cvars);

	// `ApplyScriptedBlend`, the tail of `CAM_ApplyToView` (`0x100ffb00`): the scripted channel is
	// composed **over** whatever the base rig produced, at the shot stack's own timed weight. It is
	// not a rival viewpoint — VtMB has one camera, and this is the last term applied to it.
	//
	// Taking the three view values rather than an `FMinimalViewInfo` is what keeps this header on
	// Core types, so the composition is asserted with no engine view struct — and it is the reason
	// the same function serves the faithful evaluator and the modern rig without either owning it.
	// A `Weight` at or below zero leaves all three untouched; a `ShotFov` at or below zero keeps the
	// player's field of view, which is what a shot file with no `FieldOfView` authors.
	void ComposeScriptedShot(FVector& InOutLocation, FRotator& InOutRotation, float& InOutFov,
		const FVector& ShotLocation, const FRotator& ShotRotation, float ShotFov, float Weight);

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
