#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "Logging/LogMacros.h"

// The `camera_track` override channel — retail's `CBasePlayer` fade machinery, ported verbatim.
//
// This is the SERVER half of what `docs/vtmb/camera-view-modes.md` §"The `camera_track` override
// channel" recovers: one signed global fade weight over a per-channel stack of outgoing cameras,
// composed once per frame. The unreplicated state it reproduces, all on `CBasePlayer`:
//
//   +0x19b8  m_flCameraOverrideFadeMarkTime      -> Mark
//   +0x19bc  the fade duration, SIGN IS DIRECTION -> SignedDuration (>0 in, <0 out)
//   +0x19c0/c4/c8  view entity / set time / crossfade   -> ViewSlot
//   +0x19cc/d0/d4  target entity / set time / crossfade -> TargetSlot
//   +0x19d8/e4     CUtlVector of outgoing entries, stride 0x10 -> Entries
//
// and the five functions over it: `FUN_1017d900` (GetWeight, which is also the reaper),
// `FUN_1017d280` (SetViewEntity), `FUN_1017d460` (SetTargetEntity), `FUN_1017d0b0` (Arm),
// `FUN_1017d6d0` (FadeOut), and the fold in `CHL2_Player::SetupVisibility` `0x10352120` (Publish).
//
// Substrate rules hold: nothing here reaches a world, a clock or an actor. Time arrives as a
// parameter, and entities arrive through `IElysiumCameraOverrideResolver`, so the whole channel is
// assertable with no world at all (`Elysium.Substrate.CameraOverride`).
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumCameraOverride, Log, All);

// Which channel an outgoing entry belongs to — retail's kind byte at entry offset 0.
//
// **Retail defect (reproduced): both pushers write 0.** `FUN_1017d280` at `0x1017d386` and the
// target setter `FUN_1017d460` at `0x1017d55f` are both `MOV byte ptr [ESI],0x0`, so `Target` is
// never written by any shipped path and the fold's target arm (`0x1035259e`-`0x1035261f`) is
// unreachable. The arm is kept below as dead-but-present code because the shape is retail's; see
// `docs/vtmb/retail-defects.md` §7.
enum class EElysiumCameraOverrideKind : uint8
{
	View = 0,
	Target = 1,
};

namespace ElysiumCameraOverride
{
	// `CBaseEntity`'s own answers for slots 48 and 49 — `0x10026810` -> `_DAT_104454c4` = 0.0 and
	// `0x10026830` -> `_DAT_104454cc` = 75.0. An entity that is neither a `camera_track` nor a
	// combat character publishes THESE, not zero: a superseded NPC target folding through the view
	// arm (the kind-byte defect) drags the published FOV toward 75 and the roll toward 0.
	inline constexpr float DefaultRollDegrees = 0.0f;
	inline constexpr float DefaultFieldOfView = 75.0f;

	// M4, ruled 2026-09-07. The replicated encoder's ranges are contract; its BIT WIDTHS are not
	// (pixel-only, and the port has no wire behind them). `FUN_1018a730`'s `DT_Local` table:
	// `m_flCameraFOVOverride` +0x104, 10 bits over [0, 180]; `m_flCameraRollOverride` +0x108,
	// 12 bits over [-180, 180]; `m_flCameraOverrideFadeDuration` +0x114, 10 bits over [-10, +10].
	// A value outside a range would be silently mangled on the wire in retail, so the port clamps
	// and warns instead. The shipped corpus never reaches one (max `FromPlayerTime` 1.0,
	// `ToPlayerTime` 0), which is what makes the clamp an assertion rather than a behaviour.
	inline constexpr float MinFieldOfView = 0.0f;
	inline constexpr float MaxFieldOfView = 180.0f;
	inline constexpr float MaxRollDegrees = 180.0f;
	inline constexpr float MaxFadeSeconds = 10.0f;

	// `_DAT_10450aa4` = 0.00999999977f, read from `vampire.dll`. The dead band `FUN_1017d0b0`
	// compares the mark against at `0x1017d1c4`; the same 10 ms the client's ramp uses for its own
	// `|duration| <= 0.01` cut-in band (`_DAT_101e34e8` / `_DAT_10235278`).
	inline constexpr float MarkEpsilonSeconds = 0.00999999977f;

	// The three encoder clamps. Each warns once per bite, naming the SendProp range, because a bite
	// means authored content has left the range retail could replicate at all.
	float ClampFieldOfView(float Fov, const TCHAR* Site);
	float ClampRoll(float RollDegrees, const TCHAR* Site);
	// The authored fade duration. Signed: the sign carries direction on this path, so the clamp is
	// symmetric about zero exactly as the SendProp's [-10, +10] is.
	float ClampFadeSeconds(float Seconds, const TCHAR* Site);
}

// One entity's answers to vtable slots 46-53 — the interface `CBaseEntity` declares and only
// `CCameraTrack` and `CBaseCombatCharacter` fill (`rc_group_bc.md` RC7).
//
//   +0xB8 46  OnBecameCameraTarget      +0xC8 50  GetCameraViewpointPosition
//   +0xBC 47  OnBecameCameraView        +0xCC 51  GetCameraTargetPosition
//   +0xC0 48  GetCameraRoll             +0xD0 52  GetCameraFadeInTime
//   +0xC4 49  GetCameraFieldOfView      +0xD4 53  GetCameraFadeOutTime
//
// The defaults below are `CBaseEntity`'s own bodies. Only the two position getters are pure: they
// are `WorldSpaceCenter()` in retail, and the port has no generic world-space centre on the entity
// base, so each implementor answers for itself.
class IElysiumCameraOverrideSource
{
public:
	virtual ~IElysiumCameraOverrideSource() = default;

	// Slot 50. `CCameraTrack` answers `m_vecViewPos` (+0x4e0); a combat character answers its look
	// point (`CalcLookData(&out, NULL)`); `CBaseEntity` answers `WorldSpaceCenter()`.
	virtual FVector GetCameraViewpointPosition() const = 0;

	// Slot 51. **`AimFrom` is dead in every shipped implementation** — `CCameraTrack::vfunc51`
	// (`0x100cc320`) reads only `[ESP+8]`, the `CBaseEntity` default (`0x10026890`) does the same,
	// and `CBaseCombatCharacter::GetCameraTargetPosition` (`0x103320b0`) writes `[ESP+0x14]` in
	// both arms. It is carried because retail computes and passes it; relying on it would be a
	// divergence, not a port.
	virtual FVector GetCameraTargetPosition(const FVector& AimFrom) const = 0;

	// Slot 48 / 49 — `CBaseEntity` roll 0.0 and FOV 75.0.
	virtual float GetCameraRoll() const { return ElysiumCameraOverride::DefaultRollDegrees; }
	virtual float GetCameraFieldOfView() const { return ElysiumCameraOverride::DefaultFieldOfView; }

	// Slots 52 / 53 — the per-entity MINIMUM crossfade, in and out. A `camera_track` answers
	// `max(0, FromPlayerTime)` / `max(0, ToPlayerTime)`; a combat character answers ONE unclamped
	// runtime field, `m_flCameraOverrideFadeTime` (+0x10d0), for BOTH directions; anything else
	// answers 0. This is a virtual pair, not a straight read of the authored keyvalues — the plan's
	// original wording was corrected by `rc_group_bc.md` RC7.
	virtual float GetCameraFadeInTime() const { return 0.0f; }
	virtual float GetCameraFadeOutTime() const { return 0.0f; }

	// Slots 47 / 46 — "you are now the camera view" / "…the camera target". On a `camera_track`
	// these stamp the matching start time / key / paused trio and fire `OnReachedKeyframe`.
	virtual void OnBecameCameraView() {}
	virtual void OnBecameCameraTarget() {}

	// The EHANDLE liveness test the fold and the weight getter make. Separate from the resolver's
	// own null answer so a test can kill a source mid-fold without rebuilding its handle table.
	virtual bool IsCameraSourceAlive() const { return true; }
};

// How the channel reaches entities and the cine-camera slot. Everything the channel needs from the
// world, and the whole reason it never holds a world pointer.
class IElysiumCameraOverrideResolver
{
public:
	virtual ~IElysiumCameraOverrideResolver() = default;

	// Null for an unset, stale or dead handle — retail's `handleLive()`.
	virtual IElysiumCameraOverrideSource* ResolveCameraOverrideSource(
		const FElysiumEntityHandle& Handle) const = 0;

	// `FUN_1017cef0(this, NULL)` — `CBasePlayer::SetCineCamera(NULL)`. **Only `SetViewEntity` calls
	// it** (`FUN_1017d280` opens with `PUSH 0; CALL 0x100015cd` at `0x1017d285`; `FUN_1017d460` has
	// no such call), so setting the TARGET entity leaves a live cine shot alone.
	virtual void ClearCineCamera() = 0;
};

class FElysiumCameraOverrideChannel
{
public:
	// One live channel slot: `+0x19c0/c4/c8` (view) or `+0x19cc/d0/d4` (target).
	struct FSlot
	{
		FElysiumEntityHandle Entity;
		double SetTime = 0.0;
		float CrossfadeDuration = 0.0f;
	};

	// One outgoing camera, stride 0x10 in retail's `CUtlVector` at `+0x19d8`.
	struct FEntry
	{
		EElysiumCameraOverrideKind Kind = EElysiumCameraOverrideKind::View;
		FElysiumEntityHandle Entity;
		double SetTime = 0.0;
		float CrossfadeDuration = 0.0f;
	};

	// The replicated block `SetupVisibility` fills — `m_vecCameraViewOverride` (+0x1f2c),
	// `m_vecCameraTargetOverride` (+0x1f38), `m_flCameraFOVOverride` (+0x1f44),
	// `m_flCameraRollOverride` (+0x1f48) and the three time fields.
	//
	// **It persists across frames**, exactly as the player's own members do: `SetupVisibility`
	// writes the view triple only inside `if (handleLive(viewEntity))`, so with no live view entity
	// the fold lerps away from LAST frame's published value rather than from a fresh default.
	struct FPublished
	{
		bool bActive = false;          // the weight was positive this frame
		float Weight = 0.0f;
		FVector ViewOrigin = FVector::ZeroVector;
		FVector TargetPoint = FVector::ZeroVector;
		float Roll = ElysiumCameraOverride::DefaultRollDegrees;
		float FieldOfView = ElysiumCameraOverride::DefaultFieldOfView;
		bool bHasView = false;         // a view entity or a queued view entry contributed
		bool bHasTarget = false;
		// The two accumulated coverages, `[ESP+0x1c]` / `[ESP+0x20]` in retail's frame. Stack
		// locals there; surfaced here because the coverage rule `1 - PROD(1 - f_i)` is the thing
		// the fold has to be asserted on, and because they are what scales every published value.
		float ViewCoverage = 0.0f;
		float TargetCoverage = 0.0f;
		double Timestamp = 0.0;        // m_flCameraOverrideTimestamp, stamped only while active
		double FadeStartTime = 0.0;    // m_flCameraOverrideFadeStartTime — the mark, copied raw
		float FadeDuration = 0.0f;     // m_flCameraOverrideFadeDuration — signed, copied raw
	};

	// `FUN_1017d900` `GetCameraOverrideWeight`. Three arms, and **the getter is the reaper**: when
	// the mark is not positive, or both ends have gone invalid, it clears the channel and answers 0.
	// The reap order is observable — a query on the frame both entities die returns 0 AND leaves the
	// channel clean, so the next push behaves as fresh rather than as a mid-blend reversal.
	//
	// Non-const for that reason. `Now` is absolute substrate seconds and must be positive, because
	// retail's `mark > 0` test is against a `curtime` that always is.
	float GetWeight(double Now, const IElysiumCameraOverrideResolver& Resolver);

	// `FUN_1017d280(player, ent, crossfade)`. Clears the cine camera FIRST, clamps a negative
	// crossfade to 0, push_fronts the outgoing view camera when `Now > ViewSlot.SetTime` and the
	// outgoing handle is still live, raises the crossfade to the incoming entity's own minimum
	// (slot 0xD0), arms the fade, stores the trio, and notifies through slot 0xBC. A handle that
	// does not resolve routes to `FadeOut`.
	void SetViewEntity(double Now, const FElysiumEntityHandle& Entity, float Crossfade,
		IElysiumCameraOverrideResolver& Resolver);

	// `FUN_1017d460(player, ent, crossfade)`. **Not a twin of `SetViewEntity`**: it does not clear
	// the cine camera, it works the target trio, and it notifies through slot 0xB8.
	void SetTargetEntity(double Now, const FElysiumEntityHandle& Entity, float Crossfade,
		const IElysiumCameraOverrideResolver& Resolver);

	// `FUN_1017d6d0(player, dur)`. No-op at weight <= 0; raises `Duration` to each LIVE end's own
	// slot-0xD4 minimum; `Duration <= 0` hard-clears the mark (an instant snap back to the player);
	// otherwise writes the negative duration and back-dates the mark by `(1 - w) * Duration`.
	void FadeOut(double Now, float Duration, const IElysiumCameraOverrideResolver& Resolver);

	// `FUN_1017d0b0(player, dur)` — arm or re-time. The back-dating re-time is how retail gets a
	// symmetric mid-blend reversal with no separate weight variable: it moves the START, not a
	// weight. See the .cpp for the four arms, each read off the listing.
	void Arm(double Now, float Duration, const IElysiumCameraOverrideResolver& Resolver);

	// `CHL2_Player::SetupVisibility` `0x10352120`, the compose. Once per frame, never in a think.
	// `EyePosition` is the aim-from source used when there is no live view entity (and is dead in
	// every shipped implementation — see `GetCameraTargetPosition`). Answers the persistent
	// published block; `bActive` is false when the weight is not positive.
	const FPublished& Publish(double Now, const IElysiumCameraOverrideResolver& Resolver,
		const FVector& EyePosition);

	// **The port's per-ROLE release — retail has no counterpart.** `FUN_1017d6d0` releases the whole
	// channel and leaves both handles for the lazy reap, because retail's only per-slot writer is a
	// replacement. This port's `camera_track` leases the position and the target role
	// independently, so a track that gives up one lease has to give up the matching slot too;
	// otherwise the fold would keep publishing that entity forever, with no ramp underneath it.
	// Clears the handle only, exactly as the reap does.
	void ReleaseSlot(EElysiumCameraOverrideKind Kind);

	// What `GetWeight` does when it reaps. Note what it does NOT clear: the two slots' set times and
	// crossfade durations survive, exactly as retail's do (`FUN_1017d900` writes only the mark, the
	// duration, the two handles and the entry count).
	void Clear();

	// True when nothing is armed and no entity is held — what a reaped channel looks like.
	bool IsClear() const;

	// --- Inspection. The tests assert on the stored fields, so an equivalent-but-different
	// mechanism (a separate weight variable, say) fails rather than passes. ---
	double MarkTime() const { return Mark; }
	float SignedDuration() const { return Duration; }
	const FSlot& ViewSlot() const { return View; }
	const FSlot& TargetSlot() const { return Target; }
	const TArray<FEntry>& OutgoingEntries() const { return Entries; }
	const FPublished& PublishedState() const { return Published; }

private:
	// The push condition and the entry push, shared by both setters. Retail writes them twice; the
	// only thing that differs between the two copies is which slot is read — and the kind byte,
	// which is where the defect is.
	void PushOutgoing(double Now, EElysiumCameraOverrideKind Kind, const FSlot& Slot,
		const IElysiumCameraOverrideResolver& Resolver);

	// `clamp((Now - SetTime)/CrossfadeDuration, 0, 1)`, or 1.0 when the duration is <= 0
	// (`0x10352234` / `0x1035234c`).
	static float ChannelFraction(double Now, const FSlot& Slot);

	double Mark = 0.0;        // +0x19b8
	float Duration = 0.0f;    // +0x19bc — sign is direction
	FSlot View;               // +0x19c0/c4/c8
	FSlot Target;             // +0x19cc/d0/d4
	TArray<FEntry> Entries;   // +0x19d8 / +0x19e4, newest first
	FPublished Published;     // the replicated block, persistent
};
