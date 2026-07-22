#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"

// P4.1 — the brush-mover substrate. `animation_and_movers.md` Part B (decompiled `vampire.dll`:
// the Source CBaseToggle/CBaseDoor lineage, RTTI-confirmed) is the reference for everything here.
//
// FElysiumMoverBase is the CBaseToggle primitive: LinearMove/AngularMove drive the entity's brush
// body (UElysiumBrushComponent) at a CONSTANT velocity toward a target transform — no easing —
// snapping and firing MoveDone() on arrival (B.4). Motion runs on the substrate's own think clock
// (R4: one clock, one queue, no engine timers), re-arming NextThink each moving frame.
//
// FElysiumDoorBase layers the CBaseDoor 4-state machine (m_toggle_state) + the Open/Close/Toggle/
// Lock/Unlock inputs + the OnOpen/OnClose/OnFullyOpen/OnFullyClosed outputs + `wait` autoclose +
// the locked path (OnLockedUse) + blocked-while-closing (deal `dmg`, reverse, OnBlockedClosing).
// A leaf (func_door_rotating here; func_door slides in at P4.3) supplies only how it computes its
// open transform and which primitive it issues. Registered as the "CBaseDoor" chain node so both
// door leaves inherit the inputs/fields through one case-folded chain walk (R2).

class UElysiumBrushComponent;

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

private:
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

	// The activator that last opened the door — propagated onto its outputs (Source m_hActivator).
	FElysiumEntityHandle LastActivator;

	EToggleState State() const { return ToggleState; }

	// --- Inputs (registered on CBaseDoor; reach both leaves via the chain) --------------
	void InputOpen(const FElysiumEntityHandle& Activator);
	void InputClose(const FElysiumEntityHandle& Activator);
	void InputToggle(const FElysiumEntityHandle& Activator);
	void InputLock()   { bLocked = true; }
	void InputUnlock() { bLocked = false; }

	// --- Lifecycle ---------------------------------------------------------------------
	virtual void Spawn() override;
	virtual void Think() override;

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

	EToggleState ToggleState = EToggleState::AtBottom;

	// START_OPEN seats the body at the open pose on the first think — the brush body is built
	// *after* Spawn() (BuildBrushBody follows Spawn in the world's load pass), so the pose can't be
	// applied in Spawn() itself.
	bool bStartOpenSeatPending = false;
};

// Register the shared CBaseDoor input + field tables onto a descriptor (used by the CBaseDoor
// chain-node registration). Free function so the module-static registrars reference it without a
// static-init ordering hazard, mirroring BuildCBaseTrigger in ElysiumStarterClasses.cpp.
void ElysiumBuildCBaseDoor(struct FElysiumClassDesc& D);
