#include "Substrate/ElysiumLockable.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumCameraCinematic.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

namespace
{
	const FName GOnUnlocked(TEXT("OnUnlocked"));
	const FName GOnUseBegin(TEXT("OnUseBegin"));
	const FName GOnUseEnd(TEXT("OnUseEnd"));

	// The two `$attachment`s on the lock's own model that `FUN_10224440` (slots 40/43) reads and
	// that `special-case.txt`'s `Intrusion` block names as its `End` and `Target Point1` anchors.
	// One dependency, two consumers: a lock model without them degrades BOTH the placement (to
	// retail's centre-to-centre fallback and its DevMsg) and the shot (to an unresolved index).
	const FName GCameraPositionAttachment(TEXT("camera_position"));
	const FName GCameraTargetAttachment(TEXT("camera_target"));

	// `FUN_10070470`'s five parameters are `(name, Start, End, TargetPoint1, TargetPoint2)`, which
	// map onto `FUN_1006ef50(cam, ent, 0..3)`. The lock passes itself twice, so anchors **1 and 2**
	// carry it and 0 and 3 stay unbound.
	constexpr int32 GEndAnchorIndex = 1;
	constexpr int32 GTargetPoint1AnchorIndex = 2;
}

void FElysiumLockableEntity::Spawn()
{
	LastRoll = Difficulty != 0 ? 1 : 3;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Def)
	{
		return;
	}
	VisualStem = Model;
	if (VisualStem.IsEmpty())
	{
		return;
	}
	const FQuat StaticRotation = Def->ModelMesh.IsEmpty()
		? FQuat(FRotator(0.0f, -Angles.Y, 0.0f)) : Def->ModelQuat;
	const FQuat SkeletalRotation = Def->ModelMesh.IsEmpty()
		? FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles)) : Def->ModelQuat;
	FElysiumPlacedModelRequest Request;
	Request.ModelPath = Model;
	Request.StaticStem = VisualStem;
	Request.Location = Origin;
	Request.Rotation = SkeletalRotation;
	Request.UniformScale = Embodiment->BodyScaleFor(*Def);
	Request.PlacementToken = Handle.Index;
	if (Embodiment->HasPlacedModelCatalogue())
	{
		const FElysiumPlacedModelBody Placed = Embodiment->BuildPlacedModelBody(Request);
		WorldBody = Placed.Visual;
		AnimatedStem = Placed.Stem;
	}
	else
	{
		AnimatedStem = Embodiment->AnimatedPropStemForModel(Model);
		if (!AnimatedStem.IsEmpty())
		{
			WorldBody = Embodiment->BuildAnimatedPropVisual(AnimatedStem, Origin, SkeletalRotation,
				Embodiment->BodyScaleFor(*Def), Handle.Index);
		}
		else
		{
			WorldBody = Embodiment->BuildPropVisual(
				VisualStem, Origin, StaticRotation, Embodiment->BodyScaleFor(*Def));
		}
	}
	if (WorldBody)
	{
		World->RegisterPropBody(WorldBody, Handle);
		if (!DrawsWorldBody())
		{
			WorldBody->SetVisibility(false, true);
		}
	}
	OnLockPresentationChanged();
}

void FElysiumLockableEntity::PostSpawn()
{
	FElysiumEntity::PostSpawn();
	FElysiumEntity* Parent = World && !ParentName.IsEmpty() ? World->FindByName(ParentName) : nullptr;
	if (!Parent || !AttachToParent(*Parent))
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s attached to invalid owner: %s"),
			*DebugString(), *ParentName);
		Kill();
	}
}

void FElysiumLockableEntity::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumSkillEntity::Serialize(Ar);
	if (!Ar.IsLoading())
	{
		return;
	}
	OnLockPresentationChanged();
	if (FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr)
	{
		if (FElysiumItemContainer* Container = Parent->AsItemContainer())
		{
			Container->NotifyLockState(Handle, IsUseLocked());
		}
	}
}

bool FElysiumLockableEntity::AttachToParent(FElysiumEntity& Parent)
{
	FElysiumDoorBase* Door = Parent.AsDoorBase();
	if (!Door)
	{
		return false;
	}
	AttachedOwner = Door->Handle;
	Door->RegisterDoorknob(*this);
	return !IsDead();
}

// Lock and Unlock write this entity's own lock and nothing else's. A knob attached to a door does
// not relay to the door: retail's CBaseLockableEnt owns m_LastRoll, and the door reads it back
// through IsUseRefused. A container still needs the push, because the container — not the lock —
// owns its own use anchor.
void FElysiumLockableEntity::InputLock()
{
	SetLockState(true);
	if (FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr)
	{
		if (FElysiumItemContainer* Container = Parent->AsItemContainer())
		{
			Container->NotifyLockState(Handle, true);
		}
	}
}

void FElysiumLockableEntity::InputUnlock(const FElysiumEntityHandle& Activator)
{
	FireOutput(GOnUnlocked, Activator);
	SetLockState(false);
	if (FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr)
	{
		if (FElysiumItemContainer* Container = Parent->AsItemContainer())
		{
			Container->NotifyLockState(Handle, false);
		}
	}
	OnUnlocked(Activator);
}

void FElysiumLockableEntity::SetLockState(bool bLocked)
{
	LastRoll = bLocked ? 1 : 3;
	OnLockPresentationChanged();
}

int32 FElysiumLockableEntity::ResolveUseIcon(const FElysiumEntityHandle& Activator) const
{
	if (IsUseLocked() && KeyIcon != 0 && !KeyName.IsEmpty() && World)
	{
		const FElysiumEntity* Entity = World->Resolve(Activator);
		const FElysiumCombatCharacter* User = Entity ? Entity->AsCombatCharacter() : nullptr;
		if (User && User->Inventory.Has(*User, KeyName))
		{
			return KeyIcon;
		}
	}
	return GetUseIcon();
}

FElysiumUseBeginResult FElysiumLockableEntity::BeginPlayerUse(const FElysiumUseContext& Context)
{
	FElysiumEntity* UserEntity = World ? World->Resolve(Context.Activator) : nullptr;
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (!User)
	{
		return FElysiumUseBeginResult::Completed();
	}
	if (!IsUseLocked())
	{
		Use(Context.Activator);
		return FElysiumUseBeginResult::Completed();
	}
	if (!KeyName.IsEmpty() && User->Inventory.Has(*User, KeyName))
	{
		if (bDeleteKey)
		{
			User->Inventory.ScriptRemove(*User, KeyName);
		}
		InputUnlock(Context.Activator);
		Use(Context.Activator);
		return FElysiumUseBeginResult::Completed();
	}
	// Slot 32's last arm (`FUN_10224ae0`): a key-only lock never picks, and without slot 36's item
	// there is nothing to pick with.
	if (bRequiresKey || !User->Inventory.Has(*User, RequiredItemClassname()))
	{
		UE_LOG(LogElysiumSkill, Display, TEXT("%s: locked use refused — %s"), *DebugString(),
			bRequiresKey ? TEXT("requires key") : TEXT("no item_g_lockpick"));
		return FElysiumUseBeginResult::Completed();
	}
	// `CBaseLockableEnt::BeginInteractiveUse` `0x10225070` (slot 39 on all four leaves) past its
	// key-item early-out, statement for statement:
	//
	//   1 `CBaseVampireSkillEntity::vfunc39` `0x1020acb0` — `OnUseBegin` plus the attempt
	//     bookkeeping (`m_flLastAttempt`, the stat resolve, the attempt-counter reset);
	//   2 `cam = FUN_10070470("Intrusion", NULL, this, this, NULL)`;
	//   3 `cam->m_bForcePlayerLook (+0x5e8) = 0`;
	//   4 `FUN_1017cef0(player, cam)`   — adopt;
	//   5 `FUN_1015ef40(player)`        — immobilize;
	//   6 `FireOutput(m_OnSkillAttemptBegin +0x7dc)`.
	//
	// **The adoption precedes the immobilize here**, which is the reverse of the terminal: its hold
	// happens inside `CBaseTerminal::vfunc39` before `CPropHacking` ever reaches `FUN_10070470`.
	// And four things this opener deliberately does NOT do, all of which the `Hacking` opener does:
	// no `AddVFlags(player, 1)` pose lock, no holster or weapon block (the pick is equipped later,
	// by the maintenance arm), no sound, and no "in use" latch of its own — the lock's latch is the
	// `+0x8c` user handle the session already holds.
	FireOutput(GOnUseBegin, Context.Activator);
	bUseOutputsOpen = true;
	// Steps 2-5 sit between the super's `OnUseBegin` and `OnSkillAttemptBegin`, and `StartAttempt`
	// is where this port fires the latter — so the camera is opened first and unwound when the
	// attempt is refused. That refusal is a PORT-side guard (no world, no feat for `skilltype`, an
	// attempt already running); retail's super cannot fail and every shipped lock authors
	// `skilltype 1`, so the unwind has no retail counterpart and exists only to keep the slot and
	// the hold from outliving a session that never opened.
	OpenIntrusionCamera();
	if (StartAttempt(*User))
	{
		return FElysiumUseBeginResult::Started(EElysiumUseSessionKind::Explicit);
	}
	CloseIntrusionCamera();
	FinishUseOutputs(Context.Activator);
	return FElysiumUseBeginResult::Completed();
}

void FElysiumLockableEntity::OpenIntrusionCamera()
{
	if (!World)
	{
		return;
	}
	// `FUN_10070470("Intrusion", NULL, this, this, NULL)` — a disposable runtime `camera_cinematic`
	// on `CamMode 1`, with this lock bound to anchors 1 (`End`) and 2 (`Target Point1`). That is
	// exactly the pair `vdata/camerashots/special-case.txt`'s `Intrusion` block authors:
	//
	//   End    { Position Named; AttachPos "Attachment: camera_position"; AttachType Follow }
	//   Target { Point1 { Position Named; AttachPos "Attachment: camera_target"; AttachType Follow } }
	//   CameraConstraints { ... FieldOfView 75; DrawViewmodel 1; SyncRotateOnMove 1; ShowHud 1 }
	//
	// There is **no `Start` block**, so the shot begins from wherever the camera already is — the
	// player's own eye — and eases in on constraints faster than `Hacking`'s in every axis
	// (MoveAccel 450 vs 250, TurnAccel 250 vs 180, MaxTurnRate 320 vs 200, tolerances 5/3 vs 1/1).
	// `ShowHud` and `DrawViewmodel` come out of the file, so no code carries them; the one thing the
	// port must not do is inherit `Hacking`'s `DrawViewmodel 0` by copying the terminal's call site.
	FElysiumEntityHandle Anchors[FElysiumShotBindings::Num];
	Anchors[GEndAnchorIndex] = Handle;
	Anchors[GTargetPoint1AnchorIndex] = Handle;
	const FElysiumEntityHandle Created = FElysiumCameraCinematic::CreateRuntimeCamera(*World,
		TEXT("Intrusion"), static_cast<int32>(EElysiumCineCamMode::NamedShot), Anchors);
	FElysiumEntity* CameraEntity = World->Resolve(Created);
	FElysiumCameraCinematic* Camera = CameraEntity ? CameraEntity->AsCameraCinematic() : nullptr;
	if (Camera)
	{
		// `*(byte*)(cam + 0x5e8) = 0` — the load-bearing line. `CBaseCineCam`'s constructor
		// (`FUN_1006d620`) seeds `m_bForcePlayerLook` to **1**, and the runtime value is what the
		// mode-1 think reads, so every other shot in the game snaps the subject's eye angles onto
		// the look target every tick. Zeroing it makes `Intrusion` one of exactly two shots that opt
		// out (`CFuncMonitor::vfunc39` is the other); `CPropHacking` and `CPropKeypad` do not.
		//
		// It lands AFTER the factory, as retail's does, so the one-frame window in which the camera
		// exists with the constructor's 1 still standing is reproduced rather than closed — nothing
		// thinks inside it, because `FUN_1006e8e0` publishes the pose without applying the look.
		Camera->bForcePlayerLook = false;
	}
	else
	{
		// Retail does NOT refuse. `FUN_10070470` returns NULL when `SetShot` cannot load the named
		// block, and slot 39 still runs `FUN_1017cef0(player, NULL)` — which clears the camera
		// fields and leaves the client on the player's own eye. The attempt proceeds cameraless.
		//
		// Logged at `Display`, not `Warning`: retail is silent here, a headless Substrate world has
		// no `vdata/camerashots/` mounted at all, and every lock use in such a world would otherwise
		// raise an automation warning for a condition that is normal there.
		UE_LOG(LogElysiumSkill, Display,
			TEXT("%s: the 'Intrusion' shot in special-case.txt did not resolve; ")
			TEXT("the attempt runs cameraless, as retail's NULL camera does"), *DebugString());
	}
	// `FUN_1017cef0(player, cam)` — the single adoption slot (M8). The camera is disposable, so the
	// next adoption or the closer destroys it and the lock never has to remember a handle.
	World->SetCineCamera(Created, Camera ? Camera->PublishedShotId : 0,
		/*bDisposable*/ Camera != nullptr,
		Camera ? Camera->ShotDef.Name : FString(TEXT("Intrusion")));
	// `FUN_1015ef40(player)` -> `m_bIsImmobilized = 1`. Unlike the terminal's, this runs AFTER the
	// adoption; there is no exposure clamp, because a lock has no emissive screen to clamp.
	if (FElysiumPlayer* PlayerEntity = World->FindPlayer())
	{
		PlayerEntity->SetImmobilized(true);
	}
}

void FElysiumLockableEntity::CloseIntrusionCamera()
{
	if (!World)
	{
		return;
	}
	// `FUN_1017cef0(player, NULL)`: `m_iCameraOverrideIdx = 0`, the handle to `-1`, and — because
	// the outgoing camera carries the disposable bit `FUN_10070470` set — `UTIL_Remove` on it.
	// **There is no `InputRemoveCamera` and no ramp**: a same-tick cut, the entity destroyed, the
	// client's next frame on the player's own eye.
	World->ClearScriptedCamera();
	// `FUN_1015ef60(player)` -> `m_bIsImmobilized = 0`, after the cut, exactly as retail orders it.
	// It clears no `m_iVFlags`, because the opener never raised any.
	if (FElysiumPlayer* PlayerEntity = World->FindPlayer())
	{
		PlayerEntity->SetImmobilized(false);
	}
}

void FElysiumLockableEntity::TickPlayerUse(const FElysiumUseContext& Context)
{
	// `CBasePlayer::PlayerUse`'s maintenance arm, `FUN_10167e00`, over this family's slots. The held
	// entity's collision mins/maxs go to world space, the player's eye is clamped onto that box, and
	// `d = |eye.x - clamped.x| + |eye.y - clamped.y|` — MANHATTAN, XY — is compared with slot 37:
	//
	//   d >= 80.0u -> slot 43 `FUN_10224440`: place the player at the lock (the FAR arm);
	//   d <  80.0u -> slot 36 answers `"item_g_lockpick"`, so:
	//                   the pick is already the active weapon -> slot 41 `FUN_102252f0`;
	//                   the pick is carried but not active    -> switch to it (`player+0x724`),
	//                                                            then slot 40 — `FUN_10224440` again;
	//                   the pick is not carried at all        -> NOTHING happens this tick.
	//
	// **Straying past 80 units is a reposition, not a break-off.** Nothing on this path ends the
	// session: the lock's slot 32 (`FUN_10224ae0`) has no distance arm, so a live lock session ends
	// only when that gate fails (already unlocked, held by someone else, the key rule) or the player
	// lets go. The reach test and the pin are the same pair the terminal runs, on the same constant.
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	FElysiumEntity* UserEntity = World ? World->Resolve(Context.Activator) : nullptr;
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (!Embodiment || !User)
	{
		return;
	}
	FBox BodyBounds(ForceInit);
	const bool bHasBounds = Context.bHasEyeOrigin
		&& Embodiment->GetUseBodyWorldBounds(Handle, BodyBounds);
	if (bHasBounds)
	{
		const FVector Clamped = BodyBounds.GetClosestPointTo(Context.EyeOrigin);
		const double Manhattan = FMath::Abs(Context.EyeOrigin.X - Clamped.X)
			+ FMath::Abs(Context.EyeOrigin.Y - Clamped.Y);
		if (Manhattan >= HoldReachCm)
		{
			PlaceUserAtLock(*User);   // slot 43
			return;
		}
	}
	else
	{
		// With no box to measure — a headless world, or a lock whose body never stood — retail's
		// reach test degenerates to the far arm, exactly as the terminal's does.
		PlaceUserAtLock(*User);
		return;
	}

	// The near arm. Slot 36 is never NULL on this family, so the `!req` shortcut into slot 41 is
	// unreachable here and is not written.
	const FElysiumItem* Active = User->Inventory.Active(*User);
	if (Active && Active->ClassName().Equals(RequiredItemClassname(), ESearchCase::IgnoreCase))
	{
		// Slot 41, `FUN_102252f0`. Its first statement is
		// `FUN_10178590(player, this->WorldSpaceCenter())` — the eye-angle snap, re-applied EVERY
		// tick at the lock's own collision-box centre (vfunc `0x300`), not at an attachment. That is
		// the half of slot 41 that belongs to the maintenance arm.
		//
		// The rest of slot 41 is the timed roll: `FUN_1020b040` measures
		// `interval + m_flLastAttempt - curtime` and hands off to `FUN_10224fa0` (the shared
		// `FUN_1020b090` roll, the lockpick viewmodel's result callback, and `FUN_10167fd0` when the
		// lock has opened) once it reaches zero. **The port already runs that under another name**:
		// `StartAttempt` arms `NextThink` at `AttemptIntervalSeconds`, `FElysiumSkillEntity::Think`
		// calls `ResolveAttempt`, and `OnSkillSucceeded` closes the session. It is not duplicated
		// here. What remains unported from slot 41 is its per-tick HUD progress byte (`+0x816`) and
		// the lockpick weapon's own attempt/success/botch feedback — the same surface
		// `ElysiumSkillClasses.cpp` already logs as "Intrusion HUD stubbed".
		Embodiment->SnapPlayerViewTo(BodyBounds.GetCenter());
		return;
	}
	if (FElysiumItem* Carried = User->Inventory.FindOrdinary(*User, RequiredItemClassname()))
	{
		// `player->vfunc(0x724)(req)` — the auto-switch, then slot 40 (the same `FUN_10224440`).
		// Retail EQUIPS the pick rather than holstering anything, which is why the `Intrusion` shot
		// authors `DrawViewmodel 1` where `Hacking` authors 0.
		User->Inventory.SetActiveWeapon(*User, *Carried);
		PlaceUserAtLock(*User);
	}
	// No pick carried at all: retail's chain falls off the end of the `if` and does nothing. Slot 32
	// has already refused the session on the same frame, so this is unreachable in practice.
}

void FElysiumLockableEntity::PlaceUserAtLock(FElysiumCombatCharacter& User)
{
	// `FUN_10224440`, the one body slots 40 and 43 share, in its own order.
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment)
	{
		return;
	}

	// 1. The two attachments on this lock's own model. `GetAttachment01("camera_position")` gives
	//    the ORIGIN of the standing axis and `GetAttachment01("camera_target")` its far end; either
	//    one missing degrades BOTH to retail's centre-to-centre fallback, behind its own DevMsg.
	FTransform PositionFrame;
	FTransform TargetFrame;
	const bool bHasPosition =
		Embodiment->GetBodyAttachment(Handle, GCameraPositionAttachment, PositionFrame);
	const bool bHasTarget =
		Embodiment->GetBodyAttachment(Handle, GCameraTargetAttachment, TargetFrame);
	FVector CameraPosition = PositionFrame.GetLocation();
	FVector CameraTarget = TargetFrame.GetLocation();
	if (!bHasPosition || !bHasTarget)
	{
		// `DevMsg("%s (%s) not set up correctly to be a doorknob!\n")` at `0x105b256c`, then
		// `pos = this->WorldSpaceCenter(); tgt = player->WorldSpaceCenter();`. The pair collapses
		// onto the two entities' own collision-box centres, which is a legible degradation rather
		// than a refusal — and it is the same box `SurroundingBounds` reads for vfunc `0x300`.
		//
		// `DevMsg`/`DevWarning` are Source's developer channel, suppressed unless `developer` is
		// raised; this body runs EVERY tick of the far arm, so the port's equivalent of "only when a
		// developer asks" is `Verbose` — not the automation-visible `Warning` channel.
		UE_LOG(LogElysiumSkill, Verbose, TEXT("%s not set up correctly to be a doorknob! ")
			TEXT("(missing model attachment%s%s)"), *DebugString(),
			bHasPosition ? TEXT("") : TEXT(" camera_position"),
			bHasTarget ? TEXT("") : TEXT(" camera_target"));
		CameraPosition = ElysiumCameraShots::SurroundingBounds(*this).GetCenter();
		CameraTarget = ElysiumCameraShots::SurroundingBounds(User).GetCenter();
	}

	// 2. `dir = VectorNormalize((pos - tgt) with z forced to 0)`, then
	//    `stand = this->GetAbsOrigin() + dir * 31.0` — the standing spot is measured from the
	//    ENTITY's origin, not from the attachment, and only its Z comes from the origin too.
	FVector Direction = CameraPosition - CameraTarget;
	Direction.Z = 0.0;
	Direction = Direction.GetSafeNormal();   // retail's `VectorNormalize` leaves a zero vector zero
	const FVector Stand = Origin + Direction * StandOffCm;

	// 3. The ground probe: a zero-extent ray from `stand` straight down 1024 units, filtered off the
	//    player, on `MASK_PLAYERSOLID`. Retail reads two flags off it and REFUSES TO MOVE THE PLAYER
	//    AT ALL on either:
	//      `startsolid`     -> DevWarning "(%d)%s too close to solid geometry to pick!"
	//      `fraction == 1`  -> DevWarning "(%d)%s no solid ground over lockpick position!"
	//
	//    The port has one swept-solid-world seam, `TraceCameraHull`, and a zero half-extent makes it
	//    the ray this needs. **Named divergence, stated:** that seam runs on `ELYSIUM_USE_CHANNEL`
	//    (Source's `MASK_PLAYERSOLID_BRUSHONLY`, `0x1400b`) where retail's probe uses `0x201400b`,
	//    so a character standing on the lockpick spot blocks retail's probe and not this one. `false`
	//    is the headless answer — no collision world ran — and it skips the probe rather than
	//    reporting "no ground", because a Substrate world has no floor to find.
	FVector Ground = Stand;
	float Fraction = 1.0f;
	bool bStartSolid = false;
	const FVector ProbeEnd = Stand - FVector(0.0, 0.0, GroundProbeCm);
	if (Embodiment->TraceCameraHull(Stand, ProbeEnd, FVector::ZeroVector, User.Handle,
		Fraction, bStartSolid))
	{
		if (bStartSolid)
		{
			// `DevWarning("(%d)%s too close to solid geometry to pick!\n")`, `0x105b2534`. Verbose
			// for the same reason as the DevMsg above: this is a per-tick developer channel.
			UE_LOG(LogElysiumSkill, Verbose, TEXT("%s too close to solid geometry to pick!"),
				*DebugString());
			return;
		}
		if (Fraction >= 1.0f)
		{
			// `DevWarning("(%d)%s no solid ground over lockpick position!\n")`, `0x105b24f8`.
			UE_LOG(LogElysiumSkill, Verbose, TEXT("%s no solid ground over lockpick position!"),
				*DebugString());
			return;
		}
		Ground = FMath::Lerp(Stand, ProbeEnd, static_cast<double>(Fraction));
	}

	// 4. `Ray_t::Init(start = player->GetAbsOrigin(), end = trace.endpos, mins/maxs = the player's
	//    own collision OBB)`, `CTraceFilterSimple(player, COLLISION_GROUP_PLAYER_MOVEMENT)`, and the
	//    result's `endpos` goes straight through `UTIL_SetOrigin` — unconditionally, with no second
	//    fraction test. `SweepPlayerHullToward` is that sweep.
	//
	//    **Named divergence, stated:** the seam flattens its end to the player's own Z, because this
	//    runtime's pawn root is the hull CENTRE where retail's abs origin is the feet — handing it a
	//    floor point would sink the capsule by half its height. On level ground the two agree
	//    exactly; across a step under the lock the port keeps the player's height where retail would
	//    drop him to the probed floor. The probe's Z therefore survives here only as the two
	//    refusals above.
	FVector Contact = FVector::ZeroVector;
	Embodiment->SweepPlayerHullToward(Ground, Contact);

	// 5. `player->LookAtEntity(this, false)` (slot 306, `CBasePlayer::FUN_10178660`). For a lock —
	//    which carries no combat-character look data — that resolves the target through slot 193,
	//    and `CPropDoorknob::vfunc193` `FUN_102243c0` is *"the `camera_position` attachment, else
	//    `WorldSpaceCenter()`"*. It reads only `camera_position`, so a model that authors it but not
	//    `camera_target` still aims here even though step 1 fell back for the standing axis.
	Embodiment->SnapPlayerViewTo(bHasPosition
		? PositionFrame.GetLocation()
		: ElysiumCameraShots::SurroundingBounds(*this).GetCenter());
}

void FElysiumLockableEntity::EndPlayerUse(const FElysiumUseContext& Context,
	EElysiumUseEndReason Reason)
{
	// `CBaseLockableEnt::EndInteractiveUse` `0x10225140` (slot 42 on all four leaves), in order:
	//   1 `CBaseVampireSkillEntity::vfunc42` `0x1020adc0` — `OnUseEnd`, `m_nLastSkillLevel = rating`;
	//   2 `FUN_1017cef0(player, NULL)` — the cut;
	//   3 `FUN_1015ef60(player)`       — un-immobilize;
	//   4 one `CReliableSingleUserRecipientFilter` usermessage, opcode 6.
	FElysiumEntity* UserEntity = World ? World->Resolve(Context.Activator) : nullptr;
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (User)
	{
		if (const TCHAR* Feat = FeatForSkillType(SkillType))
		{
			LastSkillLevel = User->CalcFeat(Feat);
		}
		OnAttemptStopped(*User, Reason);
	}
	else
	{
		StopAttempt();
	}
	FinishUseOutputs(Context.Activator);
	// Steps 2 and 3, in retail's order — the clear precedes the un-immobilize.
	CloseIntrusionCamera();
	// Step 4. The port has no lock HUD to send it to: the message carries the last roll (`+0x816`),
	// the difficulty (`FUN_1020b000`) and the player's effective rating (vfunc `+0x430`), the same
	// three values `ElysiumSkillClasses.cpp` already reports as "Intrusion HUD stubbed". Recorded
	// here so the closer's fourth statement has a named home rather than silently missing one.
	UE_LOG(LogElysiumSkill, Verbose,
		TEXT("%s: Intrusion exit usermessage 6 stubbed (roll %d, difficulty %d, rating %d)"),
		*DebugString(), LastRoll, AttemptDifficulty(),
		User && FeatForSkillType(SkillType) ? User->CalcFeat(FeatForSkillType(SkillType)) : 0);
}

void FElysiumLockableEntity::Use(const FElysiumEntityHandle& Activator)
{
	FireOutput(GOnUseBegin, Activator);
	ForwardUse(Activator);
	FireOutput(GOnUseEnd, Activator);
}

void FElysiumLockableEntity::OnSkillSucceeded(FElysiumCombatCharacter& User)
{
	InputUnlock(User.Handle);
	if (World && !World->EndPlayerUseSession(Handle, EElysiumUseEndReason::Completed))
	{
		UE_LOG(LogElysiumSkill, Warning,
			TEXT("%s skill success could not close its captured +use session"), *DebugString());
	}
	ForwardUse(User.Handle);
}

void FElysiumLockableEntity::OnAttemptStopped(FElysiumCombatCharacter&, EElysiumUseEndReason)
{
	StopAttempt();
}

void FElysiumLockableEntity::FinishUseOutputs(const FElysiumEntityHandle& Activator)
{
	if (bUseOutputsOpen)
	{
		bUseOutputsOpen = false;
		FireOutput(GOnUseEnd, Activator);
	}
}

void FElysiumLockableEntity::ForwardUse(const FElysiumEntityHandle&)
{
	UE_LOG(LogElysiumSkill, Warning,
		TEXT("%s accepted lockable use but has no specialized attachment forwarder"), *DebugString());
}

void FElysiumLockableEntity::OnLockPresentationChanged() {}

void FElysiumLockableEntity::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	if (WorldBody)
	{
		WorldBody->SetVisibility(!IsInert() && DrawsWorldBody());
	}
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, !IsInert());
	}
}

void FElysiumLockableEntity::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (!WorldBody)
	{
		return;
	}
	const FQuat Rotation = Cast<USkeletalMeshComponent>(WorldBody)
		? FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles))
		: FQuat(FRotator(0.0f, -Angles.Y, 0.0f));
	WorldBody->SetWorldLocationAndRotation(Origin, Rotation);
}

UPrimitiveComponent* FElysiumLockableEntity::GetAttachBody() const
{
	return WorldBody;
}

void FElysiumLockableEntity::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumSkillEntity::GetDebugState(Out);
	Out.Emplace(TEXT("Locked"), IsUseLocked() ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Attached owner"), AttachedOwner.IsSet() ? AttachedOwner.ToString() : TEXT("(none)"));
}

FElysiumPropDoorknob::FElysiumPropDoorknob()
{
	UseIcon = 10;
	LockedIcon = 3;
	KeyIcon = 4;
}

void FElysiumPropDoorknob::ForwardUse(const FElysiumEntityHandle& Activator)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumDoorBase* Door = Parent ? Parent->AsDoorBase() : nullptr)
	{
		Door->DoorUse(Activator);
		return;
	}
	UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot forward +use: attached door %s is invalid"),
		*DebugString(), *AttachedOwner.ToString());
}

void FElysiumPropDoorknob::OnLockPresentationChanged()
{
	USkeletalMeshComponent* Animated = Cast<USkeletalMeshComponent>(WorldBody);
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Animated || !Embodiment || AnimatedStem.IsEmpty())
	{
		if (Embodiment && Embodiment->HasPlacedModelCatalogue() && WorldBody)
		{
			UE_LOG(LogElysiumSkill, Warning,
				TEXT("%s cannot present doorknob lock state: animated body/stem did not resolve"),
				*DebugString());
		}
		return;
	}
	const FString Clip = IsUseLocked() ? TEXT("handle_locked") : TEXT("handle_unlocked");
	bool bLoops = false;
	if (Embodiment->FindAnimatedPropClip(AnimatedStem, Clip, bLoops))
	{
		if (!Embodiment->PlayAnimatedPropClip(
			Animated, AnimatedStem, Clip, /*bLoop*/ false, nullptr))
		{
			UE_LOG(LogElysiumSkill, Warning, TEXT("%s failed to play doorknob sequence '%s' on %s"),
				*DebugString(), *Clip, *AnimatedStem);
		}
	}
	else
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s has no doorknob sequence '%s' on %s"),
			*DebugString(), *Clip, *AnimatedStem);
	}
}

FElysiumElectronicDoorknob::FElysiumElectronicDoorknob()
{
	UseIcon = 53;
	LockedIcon = 54;
	KeyIcon = 5;
}

void FElysiumElectronicDoorknob::Spawn()
{
	bRequiresKey = true;
	FElysiumPropDoorknob::Spawn();
}

void FElysiumElectronicDoorknob::OnLockPresentationChanged()
{
	FElysiumPropDoorknob::OnLockPresentationChanged();
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!WorldBody || !Embodiment || VisualStem.IsEmpty())
	{
		return;
	}
	const int32 Family = IsUseLocked() ? 0 : 1;
	if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(WorldBody))
	{
		Embodiment->ApplyAnimatedPropSkin(Skeletal, VisualStem, Family);
	}
	else if (UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(WorldBody))
	{
		Embodiment->ApplyPropSkin(Static, VisualStem, Family);
	}
}

FElysiumContainerLock::FElysiumContainerLock()
{
	UseIcon = 10;
	LockedIcon = 3;
}

bool FElysiumContainerLock::AttachToParent(FElysiumEntity& Parent)
{
	FElysiumItemContainer* Container = Parent.AsItemContainer();
	if (!Container || !Container->RegisterLock(*this))
	{
		return false;
	}
	AttachedOwner = Container->Handle;
	return true;
}

void FElysiumContainerLock::ForwardUse(const FElysiumEntityHandle& Activator)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumItemContainer* Container = Parent ? Parent->AsItemContainer() : nullptr)
	{
		const FElysiumUseBeginResult Result = World->BeginPlayerUseSession(
			Container->Handle, Activator);
		if (Result.Outcome != EElysiumUseOutcome::SessionStarted)
		{
			UE_LOG(LogElysiumSkill, Warning,
				TEXT("%s unlocked but failed to forward +use to %s (outcome=%d)"),
				*DebugString(), *Container->DebugString(), static_cast<int32>(Result.Outcome));
		}
		return;
	}
	UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot forward +use: attached container %s is invalid"),
		*DebugString(), *AttachedOwner.ToString());
}

FElysiumPadlock::FElysiumPadlock()
{
	UseIcon = 10;
	LockedIcon = 3;
	KeyIcon = 4;
}

void FElysiumPadlock::ForwardUse(const FElysiumEntityHandle& Activator)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumDoorBase* Door = Parent ? Parent->AsDoorBase() : nullptr)
	{
		Door->DoorUse(Activator);
		return;
	}
	UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot forward +use: attached door %s is invalid"),
		*DebugString(), *AttachedOwner.ToString());
}

void FElysiumPadlock::OnUnlocked(const FElysiumEntityHandle&)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumDoorBase* Door = Parent ? Parent->AsDoorBase() : nullptr)
	{
		Door->UnregisterDoorknob(Handle);
	}
	else
	{
		UE_LOG(LogElysiumSkill, Warning,
			TEXT("%s unlocked without a live attached door %s"),
			*DebugString(), *AttachedOwner.ToString());
	}
	Kill();
}
