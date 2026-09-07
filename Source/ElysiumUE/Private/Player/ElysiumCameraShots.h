#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumEntityHandle.h"

class FElysiumEntity;
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
//
// The order is retail's `_strstr` chain in `FUN_10071e00`, which is what decides a value that
// matches two keywords, and the bit each keyword raises is named beside it. **Anything the chain
// does not recognise falls through to `World`** — there is no "take the value as an entity name"
// arm, in either the server parser or the client's (`client.dll FUN_10028a10`, RC11: zero
// divergences).
enum class EElysiumShotPosition : uint8
{
	None,             // the block is absent
	Player,           // 0x1     `UTIL_PlayerByIndex(1)`
	DialogTarget,     // 0x2     `subject+0xFE8`, the subject's dialogue partner
	GrappleVictim,    // 0x80000 the victim half of the subject's grapple pair
	GrappleAttacker,  // 0x100000 the attacker half
	Named,            // 0x8     NULL here; the caller supplies it via SetShotAnchorEntity
	World,            // 0x4     worldspawn -- and the fallthrough default
};

// Where on that entity the anchor sits. Retail's `AttachPos` `_strstr` chain, in its own order,
// with the bit each keyword raises. Every arm but `Bone:`/`Attachment:` takes the entity's abs
// angles as its rotation basis; those two take the bone's or the attachment's own.
enum class EElysiumShotAttachPos : uint8
{
	Bone,          // 0x200  `GetBonePosition02(idx, &pos, &ang)` -- position AND angles
	Attachment,    // 0x400  `GetAttachment02(idx, &pos, &ang)`
	Center,        // 0x20   `WorldSpaceCenter()` (vfunc 0x300)
	EyePosition,   // 0x40   `CalcLookData` / `EyePosition()` (vfunc 0x304)
	Top,           // 0x100  (absOrigin.x, absOrigin.y, surroundingBounds.maxs.z)
	Bottom,        // 0x80   the same with mins.z
	AbsMin,        // 0x800  surroundingBounds.mins, ALL THREE components
	AbsMax,        // 0x1000 surroundingBounds.maxs, ALL THREE components
	Origin,        // 0x10   `GetAbsOrigin()` -- and the default
};

// How the anchor follows what it is attached to.
//
// **`None` IS "sample once at shot start".** The shipped how-to says so and retail agrees: the
// per-tick anchor reader `FUN_1006f010` returns the shot-start cache at `this+0x598+i*12` whenever
// the `0x2000` (`None`) bit is set, and re-resolves through `FUN_1006f080` otherwise. The earlier
// reading of this file — "nothing latches, `FUN_1006e8e0` re-resolves everything every tick" —
// mistook a shot-*start* function for a think; `FUN_1006e8e0` has five call sites and all five are
// "a shot has just been set".
//
// `AttachType` also selects the *frame the OffsetOrigin is added in* (`LAB_1006f430`): world axes
// for `None`/`FollowNoAngles` (`flags & 0xa000`), the **attach point's** own angles for `Follow`
// (a bone's or an attachment's), the entity's abs angles for `FollowEntAngles`.
//
// Retail's parse is an **exact byte compare including the NUL** — case-sensitive, unlike every
// other key on this path — so a mis-cased `follow` silently becomes `None`, which latches instead
// of tracking. That is reproduced (M3, ruled 2026-09-07) with a parse warning naming the retail
// spelling.
enum class EElysiumShotAttach : uint8
{
	None,             // 0x2000  latch at shot start; offset in world axes
	Follow,           // 0x4000  follow the position, offset rotated by the ATTACH POINT's angles
	FollowNoAngles,   // 0x8000  follow the position only; the offset stays in world axes
	FollowEntAngles,  // 0x10000 follow the position, offset rotated by the *entity's* abs angles
};

// One Start / End / Target point. Retail's 0x2c-byte anchor record.
struct FElysiumShotAnchor
{
	bool bPresent = false;

	EElysiumShotPosition Position = EElysiumShotPosition::None;
	// Retail's `Named` is a bare keyword: `_strstr(value, "Named")` raises `0x8` and nothing reads a
	// name off the record, because the *caller* supplies the entity through `SetShotAnchorEntity`.
	// The field therefore parses empty on every shipped shot and survives only so a diagnostic can
	// say which entity a caller bound.
	FString NamedEntity;

	// `Origin` / `Center` / `EyePosition` / `Top` / `Bottom` / `AbsMin` / `AbsMax` /
	// `Bone: <name>` / `Attachment: <name>`, kept verbatim beside the parsed enum because it is what
	// a diagnostic prints and what the existing grammar assertions read.
	FString AttachPos = TEXT("Origin");
	EElysiumShotAttachPos AttachPoint = EElysiumShotAttachPos::Origin;
	// Retail's inline 16-byte name at anchor `+0x04` — `Q_trimspace` of whatever follows `Bone:` or
	// `Attachment:`, capped at 15 characters plus the NUL. Empty for every other `AttachPos`.
	FString AttachPointName;

	EElysiumShotAttach Attach = EElysiumShotAttach::None;

	// `OffsetOrigin "[40, -10, 25]"` -> **cm**. The how-to spells the axes out as
	// [Forward/Backward, Right/Left, Up/Down] and its own example reads -10 as "10 to our left", so
	// positive Y is *right* — which is Unreal's local frame already, and the one place in this repo a
	// Source-authored vector needs no Y negation. (VtMB's world vectors still do; this is a
	// designer-facing offset, not a world position.)
	FVector OffsetOrigin = FVector::ZeroVector;

	bool IsFollowing() const { return Attach != EElysiumShotAttach::None; }

	// The two prefixed forms are the only ones that carry an inline name and the only ones whose
	// index `SetShotAnchorEntity` resolves once (retail `+0x620 + i*4`).
	bool NeedsAttachPointIndex() const
	{
		return AttachPoint == EElysiumShotAttachPos::Bone
			|| AttachPoint == EElysiumShotAttachPos::Attachment;
	}
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

// One parsed shot — retail's 0x104-byte shot record.
struct FElysiumCameraShotDef
{
	FString Name;                    // +0x00, `Q_strncpy` into 0x20
	FElysiumShotAnchor Start;        // +0x24  anchor 0
	FElysiumShotAnchor End;          // +0x50  anchor 1
	FElysiumShotAnchor Target1;      // +0x7c  anchor 2 — the `Point1` SLOT
	FElysiumShotAnchor Target2;      // +0xa8  anchor 3 — the `Point2` SLOT
	FElysiumShotConstraints Constraints;

	// `+0xd4` — the count of `Target` sub-blocks the parser found (0, 1 or 2). It is not a key. The
	// mode-1 think gates the whole look-at derivation on `> 0`: a shot with a `Target` block gets a
	// look-derived angle, one without keeps the entity's abs angles.
	int32 TargetPointCount = 0;

	// **Retail's order-of-presence flag bug, reproduced (§4 row 1 of `camera_scripted.md`,
	// `retail-defects.md` §7).** The parser raises `flags |= 1 << (count + 2); count++` per sub-block
	// found, so the bit follows the ORDER the blocks appear in while the data goes into the fixed
	// slot above. A file that authors `Point2` without `Point1` writes anchor 3 but raises the
	// `Point1` bit, and the look-at (`FUN_1006f670`) then reads the empty slot 2 — the shot aims at
	// `(0,0,0)`. No shipped file does this; the client parser has the identical bug (RC11).
	bool bTargetPoint1Flagged = false;   // flags & 0x04
	bool bTargetPoint2Flagged = false;   // flags & 0x08

	bool IsValid() const { return !Name.IsEmpty(); }
};

// The runtime state `SetShot` / `SetShotAnchorEntity` keep beside the parsed record, per anchor.
// Retail holds three parallel arrays on the camera entity; the port keeps them together.
struct FElysiumShotAnchorBinding
{
	// `+0x610 + i*4` — the anchor's EHANDLE, written by `SetShotAnchorEntity`. Unset is retail's
	// `0xffffffff`.
	FElysiumEntityHandle Entity;

	// `+0x620 + i*4` — the bone / attachment index, resolved **once** at bind time against the
	// entity's animating half (vfunc `0x224`) and re-used by every resolve after. A non-animating
	// entity leaves it at retail's `-1`.
	//
	// The port has no integer bone table across the substrate seam, so what it caches is *which
	// seam answered the name*; `INDEX_NONE` carries retail's `-1` and means the same thing — this
	// anchor has no bone or attachment to read.
	int32 PointIndex = INDEX_NONE;

	// `+0x598 + i*12` — the shot-start anchor cache. `FUN_1006f010` returns it verbatim for an
	// `AttachType None` shot instead of re-resolving.
	FVector Cached = FVector::ZeroVector;
	bool bCached = false;

	bool IsPointIndexResolved() const { return PointIndex != INDEX_NONE; }
};

// The four anchors of one live shot: 0 Start, 1 End, 2 Point1 slot, 3 Point2 slot.
struct FElysiumShotBindings
{
	static constexpr int32 Num = 4;
	FElysiumShotAnchorBinding Anchors[Num];
};

// Which pass of retail's two-clock split a resolve is running under.
enum class EElysiumShotResolvePass : uint8
{
	// `FUN_1006e8e0`, the shot start: resolve every anchor live and fill the `+0x598` cache.
	ShotStart,
	// The 24 Hz mode-1 think: `FUN_1006f010` decides cache-versus-live per shot.
	Think,
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
	const TCHAR* LexToString(EElysiumShotAttachPos AttachPoint);

	// Whether a shot latches its anchors at shot start — retail's `FUN_1006f010` test, **including
	// its bug**: the function reads *anchor 0's* flags for every index, so what decides the whole
	// shot is the `Start` block's `AttachType`, and a shot with no `Start` reads a zeroed record and
	// re-resolves all four anchors every think. Exactly one shipped shot latches
	// (`special-case.txt`'s `Follow`). See `docs/vtmb/retail-defects.md` §7.
	bool LatchesAnchors(const FElysiumCameraShotDef& Def);

	// The world-space surrounding bounds retail reads **once at the top** of `FUN_1006f080`
	// (`ent->m_Collision (+0x270)->vfunc 0x3c`) and every `AttachPos` arm then indexes into: the
	// standing skeletal body's bounds, else the embodiment's use-anchor box, else VtMB's own
	// standing hull on the entity's origin as the stand-in for an entity with nothing to measure.
	// `WorldSpaceCenter()` (vfunc `0x300`) is this box's centre, which is what the `Center` anchor
	// and the mode-3 follow think both publish — so both read this one accessor.
	FBox SurroundingBounds(const FElysiumEntity& Entity);
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
	// A shot start on a handle that is already up — `camera_cinematic`'s re-shot branch (SC4). It
	// re-stamps `m_nClientResetFrame` and touches nothing else.
	bool RestartValue(UElysiumCameraComponent* Camera, int32 Id);
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

	// Retail `SetShotAnchorEntity` `FUN_1006ef50(this, ent, i)`: bind anchor `AnchorIndex`
	// (0 Start, 1 End, 2 Point1 slot, 3 Point2 slot) of the live shot `Id` to `Entity`, and resolve
	// that anchor's inline `Bone:` / `Attachment:` name to an index **once**, here, so the resolve
	// never does a name lookup per frame. A null/dead entity stores retail's `0xffffffff` and leaves
	// the index unresolved.
	//
	// This is the whole mechanism behind `Position Named`: retail's `SetShot` resolves `Named` to
	// NULL and the *caller* fills the slot — `FUN_10070470("Hacking", NULL, terminal, terminal,
	// NULL)` for the terminal, the death path for `special-case.txt`'s `DeathCam` corpse. `Push`
	// seeds every `Named` anchor from the shot's subject, which is that same call shape for a
	// one-entity shot; this overrides it.
	//
	// The extra `Id` argument is the port's, not retail's: retail's director owns exactly one shot,
	// while this one holds every shot the map has up.
	bool SetShotAnchorEntity(FElysiumEntityWorld* World, UElysiumCameraComponent* Camera, int32 Id,
		int32 AnchorIndex, const FElysiumEntityHandle& Entity);

	// Reusable legacy-shot adapter: resolve a parsed external definition to world-space values. The
	// dialogue director uses this without entering the legacy post-layer stack.
	//
	// `Bindings` carries the four anchor handles, the resolved bone/attachment indices and the
	// shot-start cache (retail's `+0x610` / `+0x620` / `+0x598`). Null means "a one-shot resolve with
	// no live shot behind it": the anchors are bound from the definition and the subject on the spot,
	// which is what every direct caller (the dialogue ladder) wants.
	//
	// `OriginSelector` is retail's `+0x594`, the mode-1 think's origin-source selector, which the
	// director entity owns and re-decides at every shot start (`FUN_1006e8e0`). It picks which anchor
	// drives the published origin and, on its third value, **suppresses `AutoPositionFromTarget`**
	// (SC5/SC7). The default is retail's shipped case: a shot with an `End` anchor drives from `End`.
	//
	// **It always succeeds.** Retail's resolve cannot fail — `FUN_1006f080` writes `vec3_origin` for
	// an anchor whose EHANDLE is dead and the caller cannot tell — so an unanchored shot frames the
	// world origin rather than being refused. The `bool` is kept because it reads as one at every
	// call site and because a future arm may want it; nothing may key behaviour off `false`.
	static bool Resolve(FElysiumEntityWorld* World, const FElysiumCameraShotDef& Def,
		const FElysiumEntityHandle& Subject, FElysiumCameraShot& Out,
		FElysiumShotBindings* Bindings = nullptr,
		EElysiumShotResolvePass Pass = EElysiumShotResolvePass::ShotStart,
		EElysiumShotOriginSelector OriginSelector = EElysiumShotOriginSelector::EndAnchor);

	// Retail's `SetShot` anchor loop: resolve each anchor's `Position` to an entity and bind it.
	// Exposed because the shot-start pass and `SetShotAnchorEntity` share it.
	static void BindAnchors(FElysiumEntityWorld* World, const FElysiumCameraShotDef& Def,
		const FElysiumEntityHandle& Subject, FElysiumShotBindings& Bindings);

	// `FUN_1006ef50` on one anchor of a shot the caller owns outright — the `camera_cinematic`
	// entity (SC4) keeps its own binding table rather than a director handle, so it needs the bind
	// without the "find my live shot and re-resolve it onto a camera component" wrapper above.
	static void BindAnchorEntity(FElysiumEntityWorld* World, const FElysiumCameraShotDef& Def,
		int32 AnchorIndex, const FElysiumEntityHandle& Entity, FElysiumShotBindings& Bindings);

	// One anchor of an already-bound table to a world point — retail's `FUN_1006f010`, the
	// cache-aware per-tick reader. `FUN_1006e8e0`'s `Start` arm reads anchor 0 through it directly
	// (rather than through the whole shot solve) because the placement is that anchor's position.
	// False when the anchor names nothing that resolves.
	static bool ResolveAnchorPoint(FElysiumEntityWorld* World, const FElysiumCameraShotDef& Def,
		int32 AnchorIndex, FElysiumShotBindings& Bindings, bool bLatched, FVector& OutPoint);

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
		// Retail's per-anchor runtime state, live for as long as the shot is.
		FElysiumShotBindings Bindings;
	};

	static void ApplyExposure(const FLiveShot& Entry, FElysiumCameraShot& Shot);

	// One anchor -> a world point. False when the entity it names is not there.
	static bool ResolveAnchor(FElysiumEntityWorld* World, const FElysiumShotAnchor& Anchor,
		FElysiumShotAnchorBinding& Binding, bool bLatched, FVector& OutPoint);

	TArray<FLiveShot> Live;
	int32 NextId = 1;
};
