// P4.1 — the mover base (CBaseToggle constant-velocity primitive + the CBaseDoor 4-state machine)
// and the first prototype leaf, func_door_rotating, to de-risk Chaos kinematic sweeps/blocking.
// Reference: `animation_and_movers.md` Part B (decompiled `vampire.dll`). The full spawnflag table,
// sliding func_door, linked_door, and the `+use` path are P4.2/4.3/4.4 — this lands the shared
// substrate and one working swinging door driven through the I/O inputs.

#include "ElysiumMover.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"

#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMover, Log, All);

namespace
{
	// Source keyvalues (speed, lip, distance-as-inches) are raw Source inches; the exporter emits
	// geometry in cm (the UE_ convention). Linear travel must convert; angular (degrees) does not.
	constexpr float ElysiumSourceInchToCm = 2.54f;

	// Door spawnflag bits (B.5 — decompiled, per-bit confirmed).
	constexpr int32 SF_DOOR_START_OPEN = 0x1;
	constexpr int32 SF_DOOR_REVERSE    = 0x2;
	constexpr int32 SF_DOOR_LOCKED     = 0x800;

	// Register a field backed by a *subclass* member (FElysiumClassDesc::Field only takes base
	// FElysiumEntity members; door keyfields live on FElysiumDoorBase). Mirrors AddSubclassField in
	// ElysiumStarterClasses.cpp — the accessor static_casts, always valid since a class's field
	// table is only walked for entities of that class or a subclass.
	template <typename TClass, typename TMember>
	void AddSubclassField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, bool bKeyable = true)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.bKeyable = bKeyable;
		if constexpr (std::is_same_v<TMember, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<TMember, float>)
		{
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToFloat(); };
		}
		else if constexpr (std::is_same_v<TMember, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddSubclassField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}
}

// ============================================================================================
// FElysiumMoverBase — the CBaseToggle constant-velocity primitive
// ============================================================================================

void FElysiumMoverBase::SnapBody(const FVector& RelLoc, const FRotator& RelRot)
{
	if (Body)
	{
		Body->SetRelativeLocationAndRotation(RelLoc, RelRot, /*bSweep*/ false);
	}
}

void FElysiumMoverBase::BeginMove(EMoveKind Kind, const FVector& DestLoc, const FRotator& DestRot, double TravelSeconds)
{
	if (!Body)
	{
		return;   // R1 — a bodiless mover has nothing to move; stay inert
	}
	CurrentMove   = Kind;
	MoveStartLoc  = Body->GetRelativeLocation();
	MoveStartRot  = Body->GetRelativeRotation();
	MoveDestLoc   = DestLoc;
	MoveDestRot   = DestRot;
	MoveStartTime = World ? World->NowSeconds() : 0.0;
	// A degenerate (zero-travel) move still needs one think to snap + fire MoveDone.
	MoveDoneTime  = MoveStartTime + FMath::Max(TravelSeconds, 0.0);
	NextThink     = MoveStartTime;   // due on the next Tick (think-first, R4)
}

void FElysiumMoverBase::LinearMove(const FVector& DestRelLoc, float SpeedCmPerSec)
{
	const FVector Start = Body ? Body->GetRelativeLocation() : FVector::ZeroVector;
	const double Dist = (DestRelLoc - Start).Size();
	const double Secs = SpeedCmPerSec > KINDA_SMALL_NUMBER ? Dist / SpeedCmPerSec : 0.0;
	BeginMove(EMoveKind::Linear, DestRelLoc, Body ? Body->GetRelativeRotation() : FRotator::ZeroRotator, Secs);
}

void FElysiumMoverBase::AngularMove(const FRotator& DestRelRot, float SpeedDegPerSec)
{
	const FRotator Start = Body ? Body->GetRelativeRotation() : FRotator::ZeroRotator;
	// Constant angular velocity: travel = the shortest arc between the two orientations, in degrees.
	const double TravelDeg = FMath::RadiansToDegrees(Start.Quaternion().AngularDistance(DestRelRot.Quaternion()));
	const double Secs = SpeedDegPerSec > KINDA_SMALL_NUMBER ? TravelDeg / SpeedDegPerSec : 0.0;
	BeginMove(EMoveKind::Angular, Body ? Body->GetRelativeLocation() : FVector::ZeroVector, DestRelRot, Secs);
}

void FElysiumMoverBase::TickMove(double Now)
{
	if (CurrentMove == EMoveKind::None || !Body)
	{
		return;
	}

	const double Denom = MoveDoneTime - MoveStartTime;
	const bool bArrived = (Denom <= KINDA_SMALL_NUMBER) || (Now >= MoveDoneTime);
	const float Alpha = bArrived ? 1.0f : (float)((Now - MoveStartTime) / Denom);

	// Constant velocity, no easing (B.4): lerp position, slerp orientation.
	const FVector  Loc = FMath::Lerp(MoveStartLoc, MoveDestLoc, Alpha);
	const FRotator Rot = FQuat::Slerp(MoveStartRot.Quaternion(), MoveDestRot.Quaternion(), Alpha).Rotator();

	// Swept so a solid kinematic body pushes the pawn and reports a blocker (the Chaos behaviour
	// P4.1 exists to de-risk). SetDormant-gated bodies don't reach here (an inert mover doesn't think).
	FHitResult Hit;
	Body->SetRelativeLocationAndRotation(Loc, Rot, /*bSweep*/ true, &Hit);

	if (Hit.bBlockingHit)
	{
		const EMoveKind Before = CurrentMove;
		OnMoveBlocked(Hit);
		// If the block handler didn't start a fresh move (e.g. reverse), keep retrying this one
		// next frame — the sweep clamps against the obstruction until it clears.
		if (CurrentMove == Before && CurrentMove != EMoveKind::None)
		{
			NextThink = Now;
		}
		return;
	}

	if (bArrived)
	{
		// Snap to the exact target (float drift over the arc) and settle.
		Body->SetRelativeLocationAndRotation(MoveDestLoc, MoveDestRot, /*bSweep*/ false);
		CurrentMove = EMoveKind::None;
		MoveDone();
	}
	else
	{
		NextThink = Now;   // keep thinking every frame while moving
	}
}

// ============================================================================================
// FElysiumDoorBase — the CBaseDoor 4-state machine
// ============================================================================================

void FElysiumDoorBase::Spawn()
{
	// Cache the two rest poses. Closed is the spawn pose (brush authored at origin; body already
	// seated there by BuildBrushBody). Open is leaf-computed from the keyvalues + spawnflags.
	ClosedLoc = Body ? Body->GetRelativeLocation() : (Def ? Def->Origin : FVector::ZeroVector);
	ClosedRot = Body ? Body->GetRelativeRotation() : FRotator::ZeroRotator;
	ComputeOpenTransform(OpenLoc, OpenRot);

	bLocked = (SpawnFlags & SF_DOOR_LOCKED) != 0;

	if (SpawnFlags & SF_DOOR_START_OPEN)
	{
		// Placed open, at rest. The body doesn't exist yet (built after Spawn), so seat the open
		// pose on the first think.
		ToggleState = EToggleState::AtTop;
		bStartOpenSeatPending = true;
		NextThink = 0.0f;
	}
	else
	{
		ToggleState = EToggleState::AtBottom;
	}
}

void FElysiumDoorBase::Think()
{
	const double Now = World ? World->NowSeconds() : 0.0;

	if (IsMoving())
	{
		TickMove(Now);
		return;
	}

	if (bStartOpenSeatPending)
	{
		bStartOpenSeatPending = false;
		SnapBody(OpenLoc, OpenRot);
		NextThink = ELYSIUM_NEVER_THINK;   // START_OPEN rests open (no immediate autoclose)
		return;
	}

	// At rest: the only scheduled resting-think is the AtTop autoclose (Wait >= 0). A `wait -1`
	// door never schedules a think here, so it stays open.
	if (ToggleState == EToggleState::AtTop)
	{
		DoorGoDown(LastActivator);
	}
}

void FElysiumDoorBase::InputOpen(const FElysiumEntityHandle& Activator)
{
	// Locked door: the locked path fires OnLockedUse and does not move (B.4). Sound is P6.
	if (bLocked)
	{
		static const FName OnLockedUse(TEXT("OnLockedUse"));
		FireOutput(OnLockedUse, Activator);
		return;
	}
	if (ToggleState == EToggleState::AtBottom || ToggleState == EToggleState::GoingDown)
	{
		DoorGoUp(Activator);
	}
}

void FElysiumDoorBase::InputClose(const FElysiumEntityHandle& Activator)
{
	if (ToggleState == EToggleState::AtTop || ToggleState == EToggleState::GoingUp)
	{
		DoorGoDown(Activator);
	}
}

void FElysiumDoorBase::InputToggle(const FElysiumEntityHandle& Activator)
{
	// Reverse in-flight or from a rest state (NO_AUTO_RETURN permits mid-motion re-use, B.4).
	switch (ToggleState)
	{
	case EToggleState::AtBottom:
	case EToggleState::GoingDown:
		InputOpen(Activator);
		break;
	case EToggleState::AtTop:
	case EToggleState::GoingUp:
		InputClose(Activator);
		break;
	}
}

void FElysiumDoorBase::DoorGoUp(const FElysiumEntityHandle& Activator)
{
	LastActivator = Activator;
	ToggleState = EToggleState::GoingUp;
	static const FName OnOpen(TEXT("OnOpen"));
	FireOutput(OnOpen, Activator);
	IssueMoveToOpen();
}

void FElysiumDoorBase::DoorGoDown(const FElysiumEntityHandle& Activator)
{
	LastActivator = Activator;
	ToggleState = EToggleState::GoingDown;
	static const FName OnClose(TEXT("OnClose"));
	FireOutput(OnClose, Activator);
	IssueMoveToClosed();
}

void FElysiumDoorBase::MoveDone()
{
	if (ToggleState == EToggleState::GoingUp)
	{
		// HitTop: fully open. Schedule the autoclose think unless `wait -1` (stay open).
		ToggleState = EToggleState::AtTop;
		static const FName OnFullyOpen(TEXT("OnFullyOpen"));
		FireOutput(OnFullyOpen, LastActivator);

		if (Wait >= 0.0f)
		{
			NextThink = (World ? World->NowSeconds() : 0.0) + Wait;
		}
		else
		{
			NextThink = ELYSIUM_NEVER_THINK;
		}
	}
	else if (ToggleState == EToggleState::GoingDown)
	{
		// HitBottom: fully closed, at rest (no autoclose from closed).
		ToggleState = EToggleState::AtBottom;
		static const FName OnFullyClosed(TEXT("OnFullyClosed"));
		FireOutput(OnFullyClosed, LastActivator);
		NextThink = ELYSIUM_NEVER_THINK;
	}
}

void FElysiumDoorBase::OnMoveBlocked(const FHitResult& Hit)
{
	// Only a pawn (the player; NPCs are a later subsystem) counts as a door blocker — world/prop
	// contacts are ignored so the door doesn't reverse off its own frame.
	APawn* BlockedPawn = Cast<APawn>(Hit.GetActor());
	if (!BlockedPawn)
	{
		return;
	}

	// Blocked-while-closing: deal `dmg`, reverse to opening, fire OnBlockedClosing (B.4). Opening
	// into a blocker just clamps (handled by TickMove's retry) — Source waits it out.
	if (ToggleState == EToggleState::GoingDown)
	{
		if (Dmg > 0)
		{
			UGameplayStatics::ApplyDamage(BlockedPawn, (float)Dmg, nullptr,
				Body ? Body->GetOwner() : nullptr, UDamageType::StaticClass());
		}
		static const FName OnBlockedClosing(TEXT("OnBlockedClosing"));
		FireOutput(OnBlockedClosing, LastActivator);
		DoorGoUp(LastActivator);   // reverse: re-open away from the blocker
	}
}

// ============================================================================================
// func_door_rotating — the prototype swinging door (the workhorse: 1236 uses / 22 on the tutorial)
// ============================================================================================

class FElysiumFuncDoorRotating final : public FElysiumDoorBase
{
protected:
	virtual void ComputeOpenTransform(FVector& OutOpenLoc, FRotator& OutOpenRot) const override
	{
		// Rotate `Distance` degrees about the hinge. The hinge is the def origin, which is the body's
		// own pivot (BuildBrushBody seats entity-local hulls there), so a relative rotation swings the
		// leaf about it — no location change. Default axis is yaw/Z (B.2: `angles` default yaw/Z; every
		// tutorial door is `angles 0 0 0`). REVERSE (0x2) negates the swing. Non-default axes from
		// `angles`/axis spawnflags are a P4.3 refinement.
		const float Sign = (SpawnFlags & SF_DOOR_REVERSE) ? -1.0f : 1.0f;
		OutOpenLoc = ClosedLoc;
		OutOpenRot = ClosedRot + FRotator(0.0f, Sign * Distance, 0.0f);   // (Pitch, Yaw, Roll)
	}

	virtual void IssueMoveToOpen() override   { AngularMove(OpenRot,   Speed); }   // Speed = deg/s
	virtual void IssueMoveToClosed() override { AngularMove(ClosedRot, Speed); }
};

// ============================================================================================
// Registration
// ============================================================================================

void ElysiumBuildCBaseDoor(FElysiumClassDesc& D)
{
	// Inputs — the door I/O surface (B.3). Reach both leaves through the CBaseDoor chain node.
	D.Input(TEXT("Open"),   [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumDoorBase&>(E).InputOpen(A.Activator); });
	D.Input(TEXT("Close"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumDoorBase&>(E).InputClose(A.Activator); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumDoorBase&>(E).InputToggle(A.Activator); });
	D.Input(TEXT("Lock"),   [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumDoorBase&>(E).InputLock(); });
	D.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumDoorBase&>(E).InputUnlock(); });
	// Use (the +use verb lands in P4.4) shares the Open path: a player use on an unlocked closed
	// door opens it; on a locked door it fires OnLockedUse. Wired here so ent_fire can exercise it.
	D.Input(TEXT("Use"),    [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumDoorBase&>(E).InputToggle(A.Activator); });

	// Keyfields (B.2).
	AddSubclassField(D, TEXT("speed"),    &FElysiumDoorBase::Speed);
	AddSubclassField(D, TEXT("distance"), &FElysiumDoorBase::Distance);
	AddSubclassField(D, TEXT("wait"),     &FElysiumDoorBase::Wait);
	AddSubclassField(D, TEXT("lip"),      &FElysiumDoorBase::Lip);
	AddSubclassField(D, TEXT("dmg"),      &FElysiumDoorBase::Dmg);
}

static TUniquePtr<FElysiumEntity> MakeFuncDoorRotate() { return MakeUnique<FElysiumFuncDoorRotating>(); }

// FElysiumDoorBase is abstract (pure virtuals), so "CBaseDoor" cannot be instantiated on its own —
// it only ever appears as a chain node. No `.ents` record carries the classname "CBaseDoor", so its
// factory is never called; a defensive factory would still need a concrete type, so point it at the
// rotating leaf (harmless: unreachable in practice).
static FElysiumClassRegistrar GRegCBaseDoor(
	FName(TEXT("CBaseDoor")), ElysiumBaseClassName(), &MakeFuncDoorRotate, &ElysiumBuildCBaseDoor);

static FElysiumClassRegistrar GRegFuncDoorRotating(
	TEXT("func_door_rotating"), FName(TEXT("CBaseDoor")), &MakeFuncDoorRotate,
	[](FElysiumClassDesc& /*D*/) { /* inherits the door inputs/fields from CBaseDoor via the chain */ });
