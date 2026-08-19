#pragma once

#include "CoreMinimal.h"
#include "ElysiumAudioSubsystem.h"
#include "ElysiumEntity.h"

// P4.1 — the brush-mover substrate. `docs/vtmb/animation_and_movers.md` Part B (decompiled `vampire.dll`:
// the Source CBaseToggle/CBaseDoor lineage, RTTI-confirmed) is the reference for everything here.
//
// FElysiumMoverBase is the CBaseToggle primitive: LinearMove/AngularMove drive the entity's brush
// body (UElysiumBrushComponent) at a CONSTANT velocity toward a target transform — no easing —
// snapping and firing MoveDone() on arrival (B.4). Motion runs on the substrate's own think clock
// (R4: one clock, one queue, no engine timers), re-arming NextThink each moving frame.
//
// FElysiumDoorBase layers the CBaseDoor 4-state machine (m_toggle_state) + the Open/Close/Toggle/
// Lock/Unlock inputs + the OnOpen/OnClose/OnFullyOpen/OnFullyClosed outputs + `wait` autoclose +
// the locked path (OnLockedUse) + blocked-while-closing (deal `dmg`, reverse, OnBlockedClosing) +
// the full spawnflag table (B.5) + `linked_door` (the paired leaf) + the +use doorknob path. Two
// leaves derive from it: func_door_rotating (swings `distance` about the hinge) and func_door
// (slides `movedir` by its own depth, P4.3). A leaf supplies only how it computes its open transform
// and which primitive it issues. Registered as the "CBaseDoor" chain node so both leaves inherit the
// inputs/fields through one case-folded chain walk (R2).

class UElysiumBrushComponent;
class FElysiumLockableEntity;

// The one log category the mover family writes on. Declared here rather than defined per-file
// because the family is split across `ElysiumMover.cpp`, `ElysiumElevator.cpp` and
// `ElysiumMoverSounds.cpp`, and one `LogElysiumMover` filter shows the whole mover story.
// `ElysiumMover.cpp` owns the definition.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumMover, Log, All);

// Source keyvalues (speed, lip, distance-as-inches) are raw Source inches; the exporter emits
// geometry in cm (the UE_ convention). Linear travel must convert; angular (degrees) does not.
inline constexpr float MoverInchToCm = 2.54f;

// The CBaseToggle constant-velocity mover. Not a registered class — a pure-C++ base the door/
// button/rotating families derive from. Drives the entity's Body transform; a bodiless mover is
// inert (nothing to move).
class FElysiumMoverBase : public FElysiumEntity
{
public:
	// One in-flight constant-velocity move. Linear translates the body; Angular rotates it about
	// its own pivot (= the def origin = the hinge for rotating doors). None = at rest.
	enum class EMoveKind : uint8 { None, Linear, Angular };

	// Start a constant-velocity move to a body-relative destination transform. `Speed` is in the
	// source units the keyvalue carries: cm/s-equivalent for Linear (the caller converts Source
	// inches → cm), deg/s for Angular. Schedules per-frame thinking; MoveDone() fires on arrival.
	void LinearMove(const FVector& DestRelLoc, float SpeedCmPerSec);
	void AngularMove(const FRotator& DestRelRot, float SpeedDegPerSec);

	EMoveKind MoveKind() const { return CurrentMove; }
	bool IsMoving() const { return CurrentMove != EMoveKind::None; }

protected:
	// Fired once when a move reaches its target (the body is already snapped to the exact dest).
	// The door overrides this to route HitTop/HitBottom.
	virtual void MoveDone() {}

	// A swept move hit a blocker mid-flight. Base default: nothing (the move clamps and retries
	// next frame). The door overrides to reverse + damage when closing.
	virtual void OnMoveBlocked(const FHitResult& Hit) {}

	// Advance the active move for this think frame (called from the leaf's Think while moving).
	void TickMove(double Now);

	// The current body-relative rest transforms, cached at Spawn so the state machine can move
	// between them without re-deriving (closed = spawn pose, open = leaf-computed).
	FVector  ClosedLoc = FVector::ZeroVector;
	FRotator ClosedRot = FRotator::ZeroRotator;
	FVector  OpenLoc   = FVector::ZeroVector;
	FRotator OpenRot   = FRotator::ZeroRotator;

	// Apply a body-relative transform immediately (teleport, no sweep) — used to seat the initial
	// pose (START_OPEN) before any motion.
	void SnapBody(const FVector& RelLoc, const FRotator& RelRot);

	// --- Mover sounds (P6.4) -----------------------------------------------------------------
	// VtMB resolves a mover's `soundgroup` token by directory convention (no data file): the WAVs
	// live under sound/usable/<Category>/<soundgroup>/<subkey>.wav (RE: CBaseDoor::Spawn @0x100ef060
	// reads open/close/swing/locked; CBaseButton::Spawn @0x100c8810 reads on/off). InitMoverSounds
	// reads the `soundgroup` key + the SILENT spawnflag and resolves the shipped subkeys from the
	// offline manifest (out/sound/usable/soundgroups.json). Call from the leaf's Spawn(). Category is
	// "openable" (doors) or "switches" (buttons); SilentFlag is the SF bit that mutes the mover (0 = none).
	void InitMoverSounds(const TCHAR* Category, int32 SilentFlag);

	// Play a resolved subkey one-shot (open/close/on/off/locked) at the body — 3D, attached so it
	// tracks the mover, sphere attenuation. No-op if silent, the subkey isn't shipped, or bodiless.
	void PlayMoverSound(FName Sub);
	// Play an explicit WAV (sound-relative, e.g. button `unlocked_sound`) the same way. No-op if empty.
	void PlayMoverSoundRel(const FString& Rel);
	// The looping "moving" sound (door `swing`): started on motion, stopped on arrival. Tracks its
	// voice so StopMoverLoop can end it; StartMoverLoop replaces any running loop.
	void StartMoverLoop(FName Sub);
	void StopMoverLoop();

	// Append the mover-sound state (group/category/silent/resolved subkeys/last played) to a debug
	// list — the leaf's GetDebugState calls this so the Cog inspector shows what will play.
	void AppendSoundDebug(TArray<TPair<FString, FString>>& Out) const;

	// The `soundgroup` token (lower-cased; empty = none) and which usable/ subtree it draws from.
	FString SoundGroup;
	FString SoundCategory;
	bool    bMoverSilent = false;              // the SILENT spawnflag was set
	TMap<FName, FString> SoundSubs;            // resolved subkey -> sound-relative WAV (shipped only)
	FString LastMoverSound;                    // debug: the last sound played (rel), or empty

private:
	FElysiumVoiceHandle MoverLoopVoice;         // generation-safe looping travel voice


	EMoveKind CurrentMove = EMoveKind::None;
	FVector  MoveStartLoc = FVector::ZeroVector;
	FVector  MoveDestLoc  = FVector::ZeroVector;
	FRotator MoveStartRot = FRotator::ZeroRotator;
	FRotator MoveDestRot  = FRotator::ZeroRotator;
	double   MoveStartTime = 0.0;
	double   MoveDoneTime  = 0.0;

	void BeginMove(EMoveKind Kind, const FVector& DestLoc, const FRotator& DestRot, double TravelSeconds);
};

// The CBaseDoor 4-state machine over the mover primitive. func_door_rotating (this file) and
// func_door (P4.3) derive from it; both register with BaseName "CBaseDoor".
class FElysiumDoorBase : public FElysiumMoverBase
{
public:
	// m_toggle_state — VtMB's exact values (dword @0x4f8, B.4).
	enum class EToggleState : uint8 { AtTop = 0, AtBottom = 1, GoingUp = 2, GoingDown = 3 };

	// --- Door keyfields (B.2) ----------------------------------------------------------
	float Speed    = 100.0f;   // deg/s (rotating) or in/s (sliding) — leaf interprets
	float Distance = 90.0f;    // arc in degrees (rotating) / lip-adjusted travel (sliding)
	float Wait     = 4.0f;     // autoclose delay; -1 = stay open (STAY_OPEN)
	float Lip      = 0.0f;     // inches subtracted from a slide's travel (sliding leaf)
	int32 Dmg      = 0;        // crush damage dealt when blocked

	bool  bLocked  = false;    // Lock/Unlock + LOCKED spawnflag (0x800)

	// `linked_door` (B.2, 485 uses): the targetname of the paired leaf of a double door. Movement I/O
	// (Open/Close) is wired to both leaves by the map data; the runtime link is the +use doorknob —
	// a player +use on one leaf toggles both (VtMB's CBaseDoor::DoorknobUse). Resolved lazily.
	FString LinkedDoorName;
	FElysiumEntityHandle LinkedDoor;   // cached partner handle; resolved on first Use
	FString UseOverrideName;           // named entity that receives Use instead of this leaf

	// The activator that last opened the door — propagated onto its outputs (Source m_hActivator).
	FElysiumEntityHandle LastActivator;

	EToggleState State() const { return ToggleState; }

	// --- Inputs (registered on CBaseDoor; reach both leaves via the chain) --------------
	void InputOpen(const FElysiumEntityHandle& Activator);
	void InputClose(const FElysiumEntityHandle& Activator);
	void InputToggle(const FElysiumEntityHandle& Activator);
	void InputLock();
	void InputUnlock();
	void RegisterDoorknob(FElysiumLockableEntity& Doorknob);
	void UnregisterDoorknob(const FElysiumEntityHandle& Doorknob);
	// The +use doorknob path (CBaseDoor::DoorknobUse): toggle this leaf and, if a `linked_door` is
	// set, its partner too — the double-door swing. Reached by the +use look-cursor and `ent_fire Use`.
	void DoorUse(const FElysiumEntityHandle& Activator);

	// --- +use / debug hooks (P4.3) -----------------------------------------------------
	// PUSE (0x100) is the dominant door bit (105 doors): it arms the +use look-cursor. Doors fire no
	// OnIn/OnOut (those are button-only outputs), so the cursor enter/leave stays a base no-op — only
	// activation and the reticle-arming (world-side) matter.
	virtual bool IsUsable() const override;
	virtual bool IsUseLocked() const override { return bLocked; }   // locked_icon on the reticle (P4.4)
	virtual void OnDormancyChanged() override;
	virtual void Use(const FElysiumEntityHandle& Activator) override { DoorUse(Activator); }
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual void OnParentAttached(const FTransform& ParentWorldTransform) override;
	virtual FElysiumDoorBase* AsDoorBase() override { return this; }

	// --- Lifecycle ---------------------------------------------------------------------
	virtual void Spawn() override;
	virtual void Think() override;

	// 11.9 — the door's derived state is exactly the case `docs/architecture/save-architecture.md` §4 carves out for a
	// leaf hook: `m_toggle_state` and the lock are neither keyvalues nor registered fields, and a
	// rebuild from the def cannot re-derive them (it would put every door back at its spawn pose).
	virtual void Serialize(FElysiumSaveArchive& Ar) override;

protected:
	// Leaf hook: compute the OPEN body-relative transform from the door's keyvalues + spawnflags.
	// Closed is always the spawn pose (identity/origin). Rotating doors rotate about the hinge;
	// sliding doors translate along `angles`.
	virtual void ComputeOpenTransform(FVector& OutOpenLoc, FRotator& OutOpenRot) const = 0;

	// Leaf hook: issue the primitive move toward Open/Closed at Speed (AngularMove vs LinearMove).
	virtual void IssueMoveToOpen()   = 0;
	virtual void IssueMoveToClosed() = 0;

	virtual void MoveDone() override;               // HitTop / HitBottom
	virtual void OnMoveBlocked(const FHitResult& Hit) override;

	void DoorGoUp(const FElysiumEntityHandle& Activator);
	void DoorGoDown(const FElysiumEntityHandle& Activator);

	// The `DOOR_NORMAL` hearing stimulus, raised beside the audio one-shot at both motion starts.
	// One helper rather than two call sites so the silence rule cannot drift between open and close.
	void EmitDoorGameSound();

	// Resolve `LinkedDoorName` to the paired door leaf through the world name index (cached, no-RTTI
	// downcast via AsDoorBase). Null when unset or the partner is missing/not a door.
	FElysiumDoorBase* ResolveLinkedDoor();
	void SyncDoorknobs();
	void RefreshUseOwner();

	// True when the door rests open with no autoclose: `wait -1` or the NO_AUTO_RETURN (0x20) flag.
	bool StaysOpen() const;

	EToggleState ToggleState = EToggleState::AtBottom;

	// START_OPEN seats the body at the open pose on the first think — the brush body is built
	// *after* Spawn() (BuildBrushBody follows Spawn in the world's load pass), so the pose can't be
	// applied in Spawn() itself.
	bool bStartOpenSeatPending = false;

	// 11.9 — a restored door has to be re-seated at the pose its saved state implies, and the body
	// does not exist while the snapshot is being applied. Serialize arms this and forces an
	// immediate think; the seat pass snaps the pose and hands the saved think back.
	bool  bRestoreSeatPending = false;
	float RestoreResumeThink = ELYSIUM_NEVER_THINK;
	TArray<FElysiumEntityHandle> Doorknobs;
};

// Register the shared CBaseDoor input + field tables onto a descriptor (used by the CBaseDoor
// chain-node registration). Free function so the module-static registrars reference it without a
// static-init ordering hazard, mirroring BuildCBaseTrigger in ElysiumStarterClasses.cpp.
void ElysiumBuildCBaseDoor(struct FElysiumClassDesc& D);
