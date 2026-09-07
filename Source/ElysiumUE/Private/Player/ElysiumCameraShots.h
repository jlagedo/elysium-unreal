#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumEntityHandle.h"

class FElysiumEntityWorld;
class UElysiumCameraComponent;

// `vdata/camerashots/` — VtMB's cinematic shot files, and what `SetCamera(shotfile)` names
// (115 script calls; `docs/vtmb/script_api.md`). The format is documented by Troika themselves in the
// shipped `camera shots how-to.txt`, so this is a read of an authored grammar rather than a
// reconstruction:
//
//   CameraShotTable { <ShotName> { Start {...} End {...} Target { Point1 {...} Point2 {...} }
//                                  CameraConstraints {...} } }
//
//   * **Start** is where the shot begins; absent, it starts from wherever the camera is (and, for a
//     camera with no position yet, from the player's standard view).
//   * **End** is where it transitions to; absent, the camera does not move.
//   * **Target** is what it looks at: one point is tracked directly, two are tracked at their
//     midpoint.
//   * **CameraConstraints** rate-limit the shot's *tracking* of a moving subject, and carry its FOV.
//
// One file carries one shot, named after the file. The parse is plain C++ over the shared KeyValues
// reader and is asserted with no world (`Elysium.Substrate.CameraShots`); resolving an anchor to a
// world position is `FElysiumCameraDirector`'s, because that needs entities and bodies.

// Which entity an anchor hangs off.
enum class EElysiumShotPosition : uint8
{
	None,           // the block is absent
	Player,         // the local player
	DialogTarget,   // the NPC the player is talking to -- SetCamera's own receiver
	GrappleTarget,  // the entity the player is grappling (no grapple system yet -- P13)
	World,          // the world origin
	Named,          // a named entity in the map
};

// How the anchor follows what it is attached to.
//
// **`None` is not "sample once".** The shipped how-to reads that way, but retail's camera think
// (`vampire.dll` `FUN_1006e8e0`) re-resolves ALL FOUR anchors of the live shot every server tick
// (loop `0x1006ea90`, cache `this+0x598+i*12`) and `FUN_1006f010` merely reads that per-tick cache
// back; nothing latches. What `AttachType` selects is only the *frame the OffsetOrigin is added in*
// (`0x1006f430`): world axes for `None`/`FollowNoAngles`, the attach point's angles for `Follow`,
// the entity's abs angles for `FollowEntAngles`. The thing that keeps a camera still is the client
// tracker's tolerance deadbands (`FElysiumScriptedShotTracker`), never this enum.
enum class EElysiumShotAttach : uint8
{
	None,             // offset stays in world axes; the anchor still re-resolves every tick
	Follow,           // follow the position, and rotate the offset by the attachment's facing
	FollowNoAngles,   // follow the position only; the offset stays in world axes
	FollowEntAngles,  // follow the position, and rotate the offset by the *entity's* facing
};

// One Start / End / Target point.
struct FElysiumShotAnchor
{
	bool bPresent = false;

	EElysiumShotPosition Position = EElysiumShotPosition::None;
	FString NamedEntity;                              // Position == Named

	// `Origin` / `Center` / `EyePosition` / `Top` / `Bottom` / `Bone: <name>` / `Attachment: <name>`,
	// kept verbatim so the resolver can split the two prefixed forms off.
	FString AttachPos = TEXT("Origin");

	EElysiumShotAttach Attach = EElysiumShotAttach::None;

	// `OffsetOrigin "[40, -10, 25]"` -> **cm**. The how-to spells the axes out as
	// [Forward/Backward, Right/Left, Up/Down] and its own example reads -10 as "10 to our left", so
	// positive Y is *right* — which is Unreal's local frame already, and the one place in this repo a
	// Source-authored vector needs no Y negation. (VtMB's world vectors still do; this is a
	// designer-facing offset, not a world position.)
	FVector OffsetOrigin = FVector::ZeroVector;

	bool IsFollowing() const { return Attach != EElysiumShotAttach::None; }
};

// The `CameraConstraints` block. Distances/speeds arrive in Source units and are held in cm.
//
// **The defaults are retail's parse defaults**, seeded by `FUN_100721e0` before it reads the block
// and re-seeded identically when the block is absent entirely (`0x10072300`) — an unwritten key is
// not zero. They are the shot-record fields at stride 0x104: `+0xD8` MoveSpeed, `+0xDC` MoveAccel,
// `+0xE0` TurnAccel, `+0xE4..0xEC` MaxTurnRate[3], `+0xF0..0xF8` AngularTolerance[3], `+0xFC`
// DistanceTolerance, `+0x100` FieldOfView.
struct FElysiumShotConstraints
{
	float MoveSpeed = 150.0f * ElysiumCam::U;          // cm/s the camera may move, 0 = snap
	float MoveAccel = 50.0f * ElysiumCam::U;           // cm/s^2 toward MoveSpeed, and back down again
	FVector MaxTurnRate = FVector(90.0f, 90.0f, 90.0f);// deg/s, (pitch, yaw, roll)
	float TurnAccel = 30.0f;                           // deg/s^2 toward MaxTurnRate
	float DistanceTolerance = 10.0f * ElysiumCam::U;   // cm of goal drift tolerated while parked
	FVector AngularTolerance = FVector(1.0f, 1.0f, 1.0f); // deg of drift tolerated per axis
	float FieldOfView = 75.0f;                         // degrees, 4:3-referenced, clamped [20,120]
	bool bDialogPOV = false;                           // NPCs look at the camera, not the player's eye
	bool bAutoPositionFromTarget = false;              // frame the target points top-to-bottom
	bool bSyncRotateOnMove = false;
	bool bSnapOnShotChange = false;
	// Both parse with a default of **0** (`docs/vtmb/camera-view-modes.md` §5): an absent field asks
	// the shot to hide that surface. The corpus opts back in explicitly for interaction shots —
	// `special-case.txt` sets `ShowHud 1` for Hacking and both fields for Intrusion — while story
	// cinematics leave them out and are hidden.
	bool bShowHud = false;
	bool bDrawViewmodel = false;
};

// One parsed shot.
struct FElysiumCameraShotDef
{
	FString Name;
	FElysiumShotAnchor Start;
	FElysiumShotAnchor End;
	FElysiumShotAnchor Target1;
	FElysiumShotAnchor Target2;
	FElysiumShotConstraints Constraints;

	bool IsValid() const { return !Name.IsEmpty(); }
};

namespace ElysiumCameraShots
{
	// Normalize every observed `default_camera` value form to one identity: separators are folded,
	// directories/extensions removed, surrounding whitespace trimmed, and case lowered.
	FString NormalizeKey(const FString& ShotFile);

	// Parse one shot file's text. False when it carries no `CameraShotTable` with a shot in it.
	// **The file's FIRST block**, which is the one-shot-per-file convention every `SetCamera` caller
	// relies on.
	bool ParseText(const FString& Text, FElysiumCameraShotDef& Out);

	// Every shot block in the file, in authored order. Most of `vdata/camerashots/` is one shot per
	// file, but `special-case.txt` carries five siblings under one `CameraShotTable` and retail
	// addresses them by name (`FUN_10070470("Hacking", ...)`), so the reader has to be able to see
	// past block 0. False on the same input `ParseText` refuses.
	bool ParseAllText(const FString& Text, TArray<FElysiumCameraShotDef>& Out);

	// `vdata/camerashots/<Name>.txt` under the content root, parsed and cached per process (the table
	// is ~66 small files and a conversation re-reads the same one every line). Answers the file's
	// first block.
	const FElysiumCameraShotDef* Load(const FString& ShotFile);

	// The block named `ShotName` (case-insensitive) in the same file. Null when the file does not
	// load or carries no such block; the file's parse is cached exactly as `Load`'s is, misses
	// included.
	const FElysiumCameraShotDef* LoadNamed(const FString& ShotFile, const FString& ShotName);

	// Drop the cache — a re-export, and the test seam.
	void FlushCache();

	// Seed the cache with a parsed shot under a normalized key, so a fixture can stand in for a file
	// under the content root. The shipped `vdata/camerashots/` tree is read-only corpus and an
	// automation run has no export mounted, so this is how a headless test drives the source-shot
	// path at all. Paired with `FlushCache` by every caller.
	void Install(const FString& ShotFile, const FElysiumCameraShotDef& Def);

	// The same seam for a multi-shot file: install the whole authored list under one key, so a
	// fixture can drive `LoadNamed` with no export mounted.
	void InstallNamed(const FString& ShotFile, TArrayView<const FElysiumCameraShotDef> Defs);

	const TCHAR* LexToString(EElysiumShotPosition Position);
	const TCHAR* LexToString(EElysiumShotAttach Attach);
}

// The director

// What turns a parsed shot into the values `UElysiumCameraComponent` blends, and keeps them current
// while the shot is up. It lives on `AElysiumMapActor` because resolving an anchor needs the entity
// world and the bodies standing in it — the camera component itself knows nothing about entities,
// which is what keeps cutscene cameras out of the pawn.
class FElysiumCameraDirector
{
public:
	// `SetCamera` and everything else that pushes a shot. `Subject` is the entity the shot is about —
	// `SetCamera`'s own receiver — which is what `DialogTarget` resolves to. Returns the director's
	// handle (0 on a shot file that does not parse or with no camera to push onto).
	int32 Push(FElysiumEntityWorld* World, UElysiumCameraComponent* Camera, const FString& ShotFile,
		const FElysiumEntityHandle& Subject);

	// `Push` for one named block of a multi-shot file, plus the exposure policy the handle carries
	// for its whole lifetime. Retail authors no exposure key, so the clamp is the caller's ask
	// rather than the file's -- see `FElysiumShotPresentation::bClampExposure`.
	int32 PushNamed(FElysiumEntityWorld* World, UElysiumCameraComponent* Camera,
		const FString& ShotFile, const FString& ShotName, const FElysiumEntityHandle& Subject,
		EElysiumShotExposure Exposure = EElysiumShotExposure::Scene);

	// A raw value uses the same director-handle namespace as named shots, so callers can always
	// update/pop through one interface without colliding with the component's private ids.
	int32 PushValue(UElysiumCameraComponent* Camera, const FElysiumCameraShot& Shot);
	bool UpdateValue(UElysiumCameraComponent* Camera, int32 Id, const FElysiumCameraShot& Shot);
	bool Pop(UElysiumCameraComponent* Camera, int32 Id, float BlendOutSeconds = -1.0f);

	// Every shot the map has up, dropped — a teardown, or `RemoveCamera` in the large.
	void Clear(UElysiumCameraComponent* Camera);

	// Re-resolve every live `Follow*` shot against this frame's entity positions and hand the refreshed
	// values to the camera. Called from the map actor's post-move pass, so a shot tracking an NPC sees
	// where that NPC ended the frame.
	void Tick(FElysiumEntityWorld* World, UElysiumCameraComponent* Camera);

	int32 Num() const { return Live.Num(); }
	FString Describe() const;

	// Whether the shot in effect asks for `DialogPOV` — the shot table's own how-to states it as
	// "NPCs will look at the camera during dialog, rather than the player's eye position", and 51 of
	// the 66 shipped shot files set it, so it is the ordinary case for a conversation. The gaze
	// cascade reads it. Value shots carry no parsed constraints, so a `camera_track` layered over a
	// dialogue shot is skipped rather than answering false and masking the shot underneath.
	bool WantsDialogPOV() const;

	// Reusable legacy-shot adapter: resolve a parsed external definition to world-space values. The
	// dialogue director uses this without entering the legacy post-layer stack.
	static bool Resolve(FElysiumEntityWorld* World, const FElysiumCameraShotDef& Def,
		const FElysiumEntityHandle& Subject, FElysiumCameraShot& Out);

private:
	struct FLiveShot
	{
		int32 Id = 0;              // ours
		int32 CameraShotId = 0;    // the camera component's
		bool bValue = false;       // true for a camera_track value, false for a named shot file
		FElysiumCameraShotDef Def;
		FElysiumEntityHandle Subject;
		// The pusher's exposure ask, re-stamped on every re-resolve because no file authors it.
		EElysiumShotExposure Exposure = EElysiumShotExposure::Scene;
	};

	static void ApplyExposure(const FLiveShot& Entry, FElysiumCameraShot& Shot);

	// One anchor -> a world point. False when the entity it names is not there.
	static bool ResolveAnchor(FElysiumEntityWorld* World, const FElysiumShotAnchor& Anchor,
		const FElysiumEntityHandle& Subject, FVector& OutPoint);

	TArray<FLiveShot> Live;
	int32 NextId = 1;
};
