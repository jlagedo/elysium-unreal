#pragma once

#include "CoreMinimal.h"
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
enum class EElysiumShotAttach : uint8
{
	None,             // set once, then stay put
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
struct FElysiumShotConstraints
{
	float MoveSpeed = 0.0f;                            // cm/s the camera may move, 0 = snap
	float MoveAccel = 0.0f;                            // cm/s^2 (carried; the solve is rate-limited)
	FVector MaxTurnRate = FVector(90.0f, 90.0f, 90.0f);// deg/s, (pitch, yaw, roll)
	float TurnAccel = 0.0f;                            // deg/s^2 (carried)
	float DistanceTolerance = 0.0f;                    // cm of subject drift before the camera moves
	FVector AngularTolerance = FVector::ZeroVector;    // degrees of drift before it tilts
	float FieldOfView = 0.0f;                          // degrees, 0 = keep the player's
	bool bDialogPOV = false;                           // NPCs look at the camera, not the player's eye
	bool bAutoPositionFromTarget = false;              // frame the target points top-to-bottom
	bool bSyncRotateOnMove = false;
	bool bSnapOnShotChange = false;
	bool bShowHud = true;
	bool bDrawViewmodel = true;
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
	bool ParseText(const FString& Text, FElysiumCameraShotDef& Out);

	// `vdata/camerashots/<Name>.txt` under the content root, parsed and cached per process (the table
	// is ~66 small files and a conversation re-reads the same one every line).
	const FElysiumCameraShotDef* Load(const FString& ShotFile);

	// Drop the cache — a re-export, and the test seam.
	void FlushCache();

	const TCHAR* LexToString(EElysiumShotPosition Position);
	const TCHAR* LexToString(EElysiumShotAttach Attach);
}

// --------------------------------------------------------------------------------------------
// The director
// --------------------------------------------------------------------------------------------

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
	};

	// One anchor -> a world point. False when the entity it names is not there.
	static bool ResolveAnchor(FElysiumEntityWorld* World, const FElysiumShotAnchor& Anchor,
		const FElysiumEntityHandle& Subject, FVector& OutPoint);

	TArray<FLiveShot> Live;
	int32 NextId = 1;
};
