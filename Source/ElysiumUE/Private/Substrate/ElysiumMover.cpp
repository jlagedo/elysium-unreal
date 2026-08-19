// P4.1-4.3 — the mover base (CBaseToggle constant-velocity primitive + the CBaseDoor 4-state
// machine) and both door leaves (func_door_rotating swings, func_door slides).
// Reference: `docs/vtmb/animation_and_movers.md` Part B + the decompiled `vampire.dll` (CBaseDoor::Spawn
// FUN_100ef260, CBaseDoor::Use FUN_100efc90). P4.3 completes the
// door: the full spawnflag table (B.5), the sliding leaf (open pose = pos + movedir·(|size·movedir|
// − lip)), NO_AUTO_RETURN, the PUSE +use doorknob path, and `linked_door` (the paired-leaf swing).
// The full use-only trace channel + use-icon HUD are P4.4; here doors ride the P4.2 look-cursor.

#include "Substrate/ElysiumMover.h"

#include "Substrate/ElysiumSkillClasses.h"

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumMoverSounds.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"

#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY(LogElysiumMover);

// Source movedir from raw-Source `angles` (pitch,yaw,roll degrees), returned in Unreal space.
// Mirrors Source's SetMovedir: the sentinels (0,-1,0)=up and (0,-2,0)=down, else the forward of
// `angles`. The exporter leaves `angles` in raw Source space in the `.ents` keys (only geometry is
// converted), so the forward is computed in Source and then Y-negated into Unreal (source_dir_to_
// unreal), matching how the hulls were converted — the reflection preserves axis-aligned lengths.
FVector SourceAnglesToUnrealDir(const FVector& AnglesDeg)
{
	FVector SrcDir;
	if (AnglesDeg.Equals(FVector(0.f, -1.f, 0.f)))
	{
		SrcDir = FVector(0.f, 0.f, 1.f);    // straight up
	}
	else if (AnglesDeg.Equals(FVector(0.f, -2.f, 0.f)))
	{
		SrcDir = FVector(0.f, 0.f, -1.f);   // straight down
	}
	else
	{
		// Source AngleVectors forward: pitch=X, yaw=Y, roll=Z (degrees).
		const float Pitch = FMath::DegreesToRadians(AnglesDeg.X);
		const float Yaw   = FMath::DegreesToRadians(AnglesDeg.Y);
		SrcDir = FVector(
			FMath::Cos(Yaw) * FMath::Cos(Pitch),
			FMath::Sin(Yaw) * FMath::Cos(Pitch),
			-FMath::Sin(Pitch));
	}
	// source_dir_to_unreal: negate Y (no scale), then normalize.
	return FVector(SrcDir.X, -SrcDir.Y, SrcDir.Z).GetSafeNormal();
}

namespace
{
	// Door spawnflag bits (B.5 — decompiled, per-bit confirmed against CBaseDoor::Spawn FUN_100ef260 /
	// CBaseDoor::Use FUN_100efc90). Every value observed across the exported maps decodes from these.
	constexpr int32 SF_DOOR_START_OPEN     = 0x1;     // spawn at the open pose (AT_TOP)
	constexpr int32 SF_DOOR_REVERSE        = 0x2;     // negate movedir (slide dir / swing sign)
	constexpr int32 SF_DOOR_PASSABLE       = 0x8;     // non-solid (unused by any exported door)
	constexpr int32 SF_DOOR_ONEWAY         = 0x10;    // NPC-nav one-way (consumed by the NPC AI)
	constexpr int32 SF_DOOR_NO_AUTO_RETURN = 0x20;    // stay open; permit re-use mid-motion
	constexpr int32 SF_DOOR_PUSE           = 0x100;   // player +use opens (the dominant bit, 105 doors)
	constexpr int32 SF_DOOR_NONPCS         = 0x200;   // blocks NPC activators (returns "locked")
	constexpr int32 SF_DOOR_PTOUCH         = 0x400;   // touch opens (obsolete; cleared when PUSE set)
	constexpr int32 SF_DOOR_LOCKED         = 0x800;   // starts locked
	constexpr int32 SF_DOOR_SILENT         = 0x1000;  // suppress move sounds (P6 audio)
	constexpr int32 SF_DOOR_USE_CLOSES     = 0x2000;  // CloseWhenUnblocked autoclose think

	// A human-readable decode of the door spawnflag word, for the Cog inspector's live state.
	FString DescribeDoorSpawnFlags(int32 SF)
	{
		if (SF == 0)
		{
			return TEXT("(none)");
		}
		TArray<FString> On;
		auto Add = [&](int32 Bit, const TCHAR* Name) { if (SF & Bit) { On.Add(Name); } };
		Add(SF_DOOR_START_OPEN, TEXT("START_OPEN"));
		Add(SF_DOOR_REVERSE, TEXT("REVERSE"));
		Add(SF_DOOR_PASSABLE, TEXT("PASSABLE"));
		Add(SF_DOOR_ONEWAY, TEXT("ONEWAY"));
		Add(SF_DOOR_NO_AUTO_RETURN, TEXT("NO_AUTO_RETURN"));
		Add(SF_DOOR_PUSE, TEXT("PUSE"));
		Add(SF_DOOR_NONPCS, TEXT("NONPCS"));
		Add(SF_DOOR_PTOUCH, TEXT("PTOUCH"));
		Add(SF_DOOR_LOCKED, TEXT("LOCKED"));
		Add(SF_DOOR_SILENT, TEXT("SILENT"));
		Add(SF_DOOR_USE_CLOSES, TEXT("USE_CLOSES"));
		return FString::Join(On, TEXT(" | "));
	}

	// The entity-local AABB (Unreal cm) of a def's convex hulls — the door's own size, available at
	// Spawn() (the brush body is built after Spawn, so its bounds can't be read yet).
	FBox HullLocalBounds(const FElysiumEntityDef* Def)
	{
		FBox Box(ForceInit);
		if (Def)
		{
			for (const FElysiumConvexHull& Hull : Def->Hulls)
			{
				for (const FVector& V : Hull.Vertices)
				{
					Box += V;
				}
			}
		}
		return Box;
	}

	// The per-component tolerance ResolveToggleStateFromTransform matches a live body pose against an
	// endpoint with — PE-verified `_DAT_10454b8c = 0.001f` in retail CBaseDoor/CRotDoor. Degrees for a
	// swing, cm for a slide.
	constexpr float DoorResyncTolerance = 0.001f;

	// |a-b| <= tol on each component, mirroring retail ResolveToggleStateFromTransform's float compares.
	bool VectorComponentsWithin(const FVector& A, const FVector& B, float Tol)
	{
		return FMath::Abs(A.X - B.X) <= Tol
			&& FMath::Abs(A.Y - B.Y) <= Tol
			&& FMath::Abs(A.Z - B.Z) <= Tol;
	}

	// Same per-component test on Euler angles, taken on the shortest signed delta so a body angle that
	// round-trips through a 360-wrapped representation still reads as its endpoint. Retail compared raw
	// QAngle floats; a door swing stays well under 180°, so the normalized delta is the same reading.
	bool RotatorComponentsWithin(const FRotator& A, const FRotator& B, float Tol)
	{
		return FMath::Abs(FRotator::NormalizeAxis(A.Pitch - B.Pitch)) <= Tol
			&& FMath::Abs(FRotator::NormalizeAxis(A.Yaw   - B.Yaw))   <= Tol
			&& FMath::Abs(FRotator::NormalizeAxis(A.Roll  - B.Roll))  <= Tol;
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
// Mover sounds (P6.4) — per-mover playback (the manifest loader lives in ElysiumMoverSounds.cpp)
// ============================================================================================
//
// Reference: `docs/vtmb/audio_pipeline.md` + the decompiled CBaseDoor::Spawn (FUN_100ef060, reads the
// subkeys "close"/"open"/"swing"/"locked") and CBaseButton::Spawn (FUN_100c8810, reads "on"/"off").
// The movers resolve their `soundgroup` through the offline manifest (ElysiumMoverSoundManifest)
// and play through the GI audio subsystem's voice pool (the 6.3 path).

namespace
{
	// Doors/buttons are heard across a room, not map-wide; sphere falloff radius for a mover voice.
	constexpr float ElysiumMoverSoundRadiusCm = 2500.f;
}

// cat -> group(lower) -> subkey -> sound-relative WAV. Loaded once from the offline manifest.
using FMoverSoundTable = TMap<FString, TMap<FString, TMap<FName, FString>>>;

void FElysiumMoverBase::InitMoverSounds(const TCHAR* Category, int32 SilentFlag)
{
	SoundCategory = Category;
	bMoverSilent  = SilentFlag != 0 && (SpawnFlags & SilentFlag) != 0;
	SoundGroup    = Def ? Def->Keys.FindRef(TEXT("soundgroup")).TrimStartAndEnd().ToLower() : FString();
	SoundSubs.Reset();
	if (SoundGroup.IsEmpty())
	{
		return;
	}
	const FMoverSoundTable& Table = ElysiumMoverSoundManifest();
	if (const TMap<FString, TMap<FName, FString>>* Groups = Table.Find(SoundCategory))
	{
		if (const TMap<FName, FString>* Subs = Groups->Find(SoundGroup))
		{
			SoundSubs = *Subs;   // shipped subkeys only (a group may lack e.g. swing)
		}
	}
}

void FElysiumMoverBase::PlayMoverSoundRel(const FString& Rel)
{
	if (bMoverSilent || Rel.IsEmpty() || !World)
	{
		return;
	}
	IElysiumAudio* Audio = World->Audio();
	if (!Audio)
	{
		return;
	}
	FElysiumAudioRequest Request;
	Request.Source = FElysiumAudioSource::Path(Rel);
	Request.Owner.Kind = EElysiumAudioOwnerKind::MapEntity;
	Request.Owner.StableId = FString::Printf(TEXT("entity:%u:%d"), Handle.Epoch, Handle.Index);
	Request.Category = EElysiumAudioCategory::Sfx;
	Request.Placement.bSpatialized = true;
	Request.AttenuationRadiusCm = ElysiumMoverSoundRadiusCm;
	Request.Placement.AttachTo = Body;
	Request.Placement.Location =
		Body ? Body->GetComponentLocation() : (Def ? Def->Origin : FVector::ZeroVector);
	Audio->Submit(MoveTemp(Request));
	LastMoverSound = Rel;
}

void FElysiumMoverBase::PlayMoverSound(FName Sub)
{
	if (const FString* Rel = SoundSubs.Find(Sub))
	{
		PlayMoverSoundRel(*Rel);
	}
}

void FElysiumMoverBase::StartMoverLoop(FName Sub)
{
	StopMoverLoop();
	const FString* Rel = SoundSubs.Find(Sub);
	if (bMoverSilent || !Rel || !World)
	{
		return;
	}
	IElysiumAudio* Audio = World->Audio();
	if (!Audio)
	{
		return;
	}
	FElysiumAudioRequest Request;
	Request.Source = FElysiumAudioSource::Path(*Rel);
	Request.Owner.Kind = EElysiumAudioOwnerKind::MapEntity;
	Request.Owner.StableId = FString::Printf(TEXT("entity:%u:%d"), Handle.Epoch, Handle.Index);
	Request.Category = EElysiumAudioCategory::Sfx;
	Request.Placement.bSpatialized = true;
	Request.bLooping = true;
	Request.AttenuationRadiusCm = ElysiumMoverSoundRadiusCm;
	Request.Placement.AttachTo = Body;
	Request.Placement.Location =
		Body ? Body->GetComponentLocation() : (Def ? Def->Origin : FVector::ZeroVector);
	MoverLoopVoice = Audio->Submit(MoveTemp(Request));
	LastMoverSound = *Rel;
}

void FElysiumMoverBase::StopMoverLoop()
{
	if (MoverLoopVoice.IsValid() && World)
	{
		if (IElysiumAudio* Audio = World->Audio())
		{
			Audio->StopVoice(MoverLoopVoice, /*FadeSeconds=*/0.f);
		}
	}
	MoverLoopVoice = FElysiumVoiceHandle::Invalid();
}

void FElysiumMoverBase::AppendSoundDebug(TArray<TPair<FString, FString>>& Out) const
{
	if (SoundGroup.IsEmpty())
	{
		Out.Emplace(TEXT("Soundgroup"), bMoverSilent ? TEXT("(none) · SILENT") : TEXT("(none)"));
		return;
	}
	Out.Emplace(TEXT("Soundgroup"), FString::Printf(TEXT("%s (%s)%s"), *SoundGroup, *SoundCategory,
		bMoverSilent ? TEXT(" · SILENT") : TEXT("")));
	if (SoundSubs.Num() == 0)
	{
		Out.Emplace(TEXT("Sound subkeys"), TEXT("(unresolved — no shipped usable/ dir)"));
	}
	else
	{
		TArray<FString> Names;
		for (const TPair<FName, FString>& S : SoundSubs)
		{
			Names.Add(S.Key.ToString());
		}
		Names.Sort();
		Out.Emplace(TEXT("Sound subkeys"), FString::Join(Names, TEXT(", ")));
	}
	if (!LastMoverSound.IsEmpty())
	{
		Out.Emplace(TEXT("Last sound"), LastMoverSound);
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

	// Mover sounds (P6.4): open/close/swing/locked from usable/openable/<soundgroup>/. SF_DOOR_SILENT
	// (0x1000) mutes them (RE: FUN_100ee4e0 gates on `m_spawnflags & 0x1000`).
	InitMoverSounds(TEXT("openable"), SF_DOOR_SILENT);

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

void FElysiumDoorBase::Serialize(FElysiumSaveArchive& Ar)
{
	uint8 State = static_cast<uint8>(ToggleState);
	Ar << State;
	Ar << bLocked;

	if (Ar.IsLoading())
	{
		// A door caught mid-swing resolves to the end it was travelling toward. VtMB saves the move
		// itself and resumes it; one frame of tween is animation, not game state, and resolving it
		// keeps the restored world's collision honest from the first frame.
		const EToggleState Saved = static_cast<EToggleState>(State);
		ToggleState =
			(Saved == EToggleState::GoingUp)   ? EToggleState::AtTop :
			(Saved == EToggleState::GoingDown) ? EToggleState::AtBottom : Saved;

		// Force one think now and hand the saved one back from the seat pass; a `wait -1` door's
		// think is NEVER, and it still has to be re-seated.
		RestoreResumeThink = NextThink;
		bRestoreSeatPending = true;
		bStartOpenSeatPending = false;
		NextThink = 0.0f;
	}
}

void FElysiumDoorBase::InputLock()
{
	bLocked = true;
	SyncDoorknobs();
}

void FElysiumDoorBase::InputUnlock()
{
	bLocked = false;
	SyncDoorknobs();
}

void FElysiumDoorBase::RegisterDoorknob(FElysiumLockableEntity& Doorknob)
{
	if (Doorknobs.Contains(Doorknob.Handle))
	{
		return;
	}
	if (Doorknobs.Num() >= 2)
	{
		UE_LOG(LogElysiumMover, Warning, TEXT("Door %s already has 2 doorknobs"), *DebugString());
		Doorknob.Kill();
		return;
	}
	Doorknobs.Add(Doorknob.Handle);
	Doorknob.ApplyDoorLockState(bLocked);
	RefreshUseOwner();
}

void FElysiumDoorBase::UnregisterDoorknob(const FElysiumEntityHandle& Doorknob)
{
	Doorknobs.Remove(Doorknob);
	RefreshUseOwner();
}

void FElysiumDoorBase::SyncDoorknobs()
{
	for (int32 Index = Doorknobs.Num() - 1; Index >= 0; --Index)
	{
		FElysiumEntity* Entity = World ? World->Resolve(Doorknobs[Index]) : nullptr;
		FElysiumLockableEntity* Doorknob = Entity ? Entity->AsLockableEntity() : nullptr;
		if (!Doorknob || Doorknob->IsDead())
		{
			Doorknobs.RemoveAt(Index);
			continue;
		}
		Doorknob->ApplyDoorLockState(bLocked);
	}
	RefreshUseOwner();
}

void FElysiumDoorBase::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	RefreshUseOwner();
}

void FElysiumDoorBase::RefreshUseOwner()
{
	if (World)
	{
		// The slab stays a use target. Retail FindEntityFOV hits the door brush; a knob is only
		// selected when the look-ray actually strikes it. Lock/key live on the knob's own Use.
		World->SetUseAnchorEnabled(Handle, IsUsable() && !IsInert());
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

	if (bRestoreSeatPending)
	{
		// 11.9 — the pose a restored door rests in. Same reason START_OPEN seats on the first think:
		// the brush body is built after Spawn, and the snapshot is applied after that.
		bRestoreSeatPending = false;
		const bool bOpen = (ToggleState == EToggleState::AtTop);
		SnapBody(bOpen ? OpenLoc : ClosedLoc, bOpen ? OpenRot : ClosedRot);
		NextThink = RestoreResumeThink;   // the saved think, e.g. a pending autoclose
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
	// Locked door: the direct `Open` input refuses SILENTLY — no sound, no output, no motion
	// (RE: CBaseDoor::InputOpen FUN_100f0170 opens iff `!IsDoorLocked` and fires nothing on the
	// locked path). CBaseDoor carries no OnLockedUse output — that output belongs to prop_switch —
	// and the `locked` sound is a +use affordance played only by CBaseDoor::Use (see DoorUse).
	if (bLocked)
	{
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
	// Locked door: retail InputToggle (FUN_100f0210) gates on `IsDoorLocked` at the top and does
	// nothing at all when locked — no output, no sound, no motion. The `locked` +use affordance
	// belongs to CBaseDoor::Use, not to the Toggle input (see DoorUse).
	if (bLocked)
	{
		return;
	}
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

void FElysiumDoorBase::EmitDoorGameSound()
{
	// A door that plays no audio makes no stimulus either: `SF_DOOR_SILENT` is the authored
	// statement that this mover is quiet, and it has to mean the same thing to an NPC's ears as it
	// does to the mixer.
	if (bMoverSilent || World == nullptr)
	{
		return;
	}
	World->EmitGameSound(Body ? Body->GetComponentLocation() : Origin, ElysiumGameSounds::Door(),
		/*RadiusCm, table-resolved*/ -1.f, Handle);
}

void FElysiumDoorBase::DoorGoUp(const FElysiumEntityHandle& Activator, bool bResolveSwing)
{
	LastActivator = Activator;
	ToggleState = EToggleState::GoingUp;
	// Sound on motion start: the `open` one-shot + the looping `swing` moving sound (stopped on
	// arrival in MoveDone). A group without `swing` just plays `open`; without `open`, silent travel.
	static const FName Open(TEXT("open")), Swing(TEXT("swing"));
	PlayMoverSound(Open);
	StartMoverLoop(Swing);
	EmitDoorGameSound();
	static const FName OnOpen(TEXT("OnOpen"));
	FireOutput(OnOpen, Activator);
	IssueMoveToOpen(bResolveSwing);
}

void FElysiumDoorBase::DoorGoDown(const FElysiumEntityHandle& Activator)
{
	LastActivator = Activator;
	ToggleState = EToggleState::GoingDown;
	static const FName Close(TEXT("close")), Swing(TEXT("swing"));
	PlayMoverSound(Close);
	StartMoverLoop(Swing);
	EmitDoorGameSound();
	static const FName OnClose(TEXT("OnClose"));
	FireOutput(OnClose, Activator);
	IssueMoveToClosed();
}

bool FElysiumDoorBase::StaysOpen() const
{
	// Source: a door stays open (no autoclose) when `wait -1` OR the NO_AUTO_RETURN spawnflag is set.
	return Wait < 0.0f || (SpawnFlags & SF_DOOR_NO_AUTO_RETURN) != 0;
}

void FElysiumDoorBase::MoveDone()
{
	StopMoverLoop();   // the leaf arrived: end the looping `swing` moving sound
	if (ToggleState == EToggleState::GoingUp)
	{
		// HitTop: fully open. Schedule the autoclose think unless the door stays open (`wait -1` or
		// NO_AUTO_RETURN).
		ToggleState = EToggleState::AtTop;
		static const FName OnFullyOpen(TEXT("OnFullyOpen"));
		FireOutput(OnFullyOpen, LastActivator);

		if (!StaysOpen())
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
			// 11.4 — the same two halves trigger_hurt uses: the entity owns the health, the body
			// gets the engine damage event.
			if (FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr)
			{
				Player->TakeDamage((float)Dmg);
			}
			if (IElysiumEmbodiment* PlayerBody = World ? World->Embodiment() : nullptr)
			{
				PlayerBody->DamagePlayer((float)Dmg);
			}
		}
		static const FName OnBlockedClosing(TEXT("OnBlockedClosing"));
		FireOutput(OnBlockedClosing, LastActivator);
		// Reverse to re-open. Retail CRotDoor::Blocked pre-issues its own AngularMove and calls
		// DoorGoUp with bResolveSwing == 0 (T1.4): the block-reverse open is always fixed-forward, never
		// activator-relative — the (possibly stale) block-path LastActivator must not steer the swing.
		DoorGoUp(LastActivator, /*bResolveSwing*/ false);
	}
}

// --- +use doorknob path + linked_door (P4.3) --------------------------------------------

bool FElysiumDoorBase::IsUsable() const
{
	// PUSE (0x100) arms the +use look-cursor. A hidden/dead door disarms (the world also re-checks
	// IsInert, but keep the class honest). Locked doors are still "usable" — a +use plays the
	// `locked` sound (and fires no output).
	return (SpawnFlags & SF_DOOR_PUSE) != 0 && !IsInert();
}

FElysiumDoorBase* FElysiumDoorBase::ResolveLinkedDoor()
{
	if (LinkedDoorName.IsEmpty() || !World)
	{
		return nullptr;
	}
	// Re-resolve when the cached handle is stale/dead (an epoch reload rebuilds every entity).
	if (FElysiumEntity* Cached = World->Resolve(LinkedDoor))
	{
		return Cached->AsDoorBase();
	}
	if (FElysiumEntity* Found = World->FindByName(LinkedDoorName))
	{
		LinkedDoor = Found->Handle;
		return Found->AsDoorBase();
	}
	return nullptr;
}

void FElysiumDoorBase::ResolveToggleStateFromTransform()
{
	// CBaseDoor::Use step 5 (vt +0x3e0): re-derive the toggle state from the live body transform.
	// Ordinary bookkeeping — no motion, no output, no sound; a bodiless door has nothing to read.
	if (!Body)
	{
		return;
	}

	if (ResolvesEndpointFromRotation())
	{
		// Rotating leaf (retail CRotDoor): compare the live body ANGLES to the two endpoints.
		const FRotator Live = Body->GetRelativeRotation();
		if (RotatorComponentsWithin(Live, ClosedRot, DoorResyncTolerance))
		{
			ToggleState = EToggleState::AtBottom;
			// Retail CRotDoor also calls ResetBlockedTracking here; this runtime carries no
			// swing-inversion latch. The base swing direction IS activator-relative (the rotating leaf's
			// ChooseOpenTarget), but the block-armed inversion that the latch gates is held — its retail
			// field identity is unrecovered — so there is still nothing to reset here.
		}
		else if (RotatorComponentsWithin(Live, OpenRot, DoorResyncTolerance))
		{
			ToggleState = EToggleState::AtTop;
		}
		// else: genuinely mid-travel — leave the GoingUp/GoingDown state as-is.
	}
	else
	{
		// Sliding leaf (retail CBaseDoor): compare the live body ORIGIN to the two endpoints.
		const FVector Live = Body->GetRelativeLocation();
		if (VectorComponentsWithin(Live, ClosedLoc, DoorResyncTolerance))
		{
			ToggleState = EToggleState::AtBottom;
		}
		else if (VectorComponentsWithin(Live, OpenLoc, DoorResyncTolerance))
		{
			ToggleState = EToggleState::AtTop;
		}
	}
}

void FElysiumDoorBase::DoorUse(const FElysiumEntityHandle& Activator)
{
	// CBaseDoor::Use @ vampire.dll 0x100efc90: use_override receives the ordinary Use input
	// with the original activator and this door as caller. The leaf does not toggle as fallback.
	//
	// Transport: synchronous through chokepoint 1, not the queue. DoorUse is itself running inside
	// the queue's (or +use's) already-executing Use handler, which is §2.5.1's sanctioned seam, and
	// the recovered behaviour is a direct in-handler call that lands "exactly once" — hence the
	// single resolved target rather than a by-name fan-out over every entity sharing the name.
	if (!UseOverrideName.IsEmpty())
	{
		if (World)
		{
			if (FElysiumEntity* Override = World->FindByName(UseOverrideName))
			{
				World->AcceptInput(Override->Handle, FName(TEXT("Use")), FElysiumVariant::Void(),
					Activator, Handle);
			}
			else
			{
				// Use the ordinary unresolved-target path so the I/O inspector and log-once
				// diagnostic identify the authored name.
				World->AcceptInput(UseOverrideName, FName(TEXT("Use")), FElysiumVariant::Void(),
					Activator, Handle);
			}
		}
		return;
	}

	// CBaseDoor::Use step 5 (@0x100efc90, vt +0x3e0): UNCONDITIONAL on every +use, after the
	// use_override delegation (step 2) and BEFORE the locked branch (step 8). Re-stamp the toggle
	// state from the live body pose so a door whose body never actually moved snaps back to its true
	// endpoint and the following toggle re-opens it. (Retail steps 3/4/6 — the noopenwanted refusal,
	// the mid-motion self-heal and the doorknob delegation — are not modelled in this runtime.)
	ResolveToggleStateFromTransform();

	// CBaseDoor::Use @0x100efc90 locked branch: a locked door reached by +use plays ONLY the
	// `locked` sound and returns — no output (CBaseDoor has no OnLockedUse) and no motion. This is
	// the single path that voices the locked door; the direct Open/Toggle inputs stay silent.
	if (bLocked)
	{
		static const FName Locked(TEXT("locked"));
		PlayMoverSound(Locked);
		return;
	}

	// CBaseDoor::DoorknobUse: toggle this leaf, then the linked partner (the double-door swing). The
	// partner receives `Toggle` — NOT `Use` — so it never mirrors back (no recursion), and each leaf
	// still runs its own locked check (a locked half stays put and silent).
	//
	// The partner's half goes through chokepoint 1 so the sinks, the I/O ring and the queue debugger
	// see the second leaf move; it stays synchronous because DoorUse is already inside an executing
	// handler (§2.5.1's seam) and the swing is one action. `linked_door`'s transport is not itself
	// recovered — the conservative call preserves the current timing and only adds visibility.
	InputToggle(Activator);
	if (FElysiumDoorBase* Partner = ResolveLinkedDoor())
	{
		World->AcceptInput(Partner->Handle, FName(TEXT("Toggle")), FElysiumVariant::Void(),
			Activator, Handle);
	}
}

void FElysiumDoorBase::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	static const TCHAR* StateNames[] = { TEXT("AtTop (open)"), TEXT("AtBottom (closed)"),
		TEXT("GoingUp (opening)"), TEXT("GoingDown (closing)") };
	Out.Emplace(TEXT("Toggle state"), StateNames[(uint8)ToggleState]);
	Out.Emplace(TEXT("Moving"), IsMoving()
		? (MoveKind() == EMoveKind::Angular ? TEXT("yes (angular)") : TEXT("yes (linear)")) : TEXT("no"));
	Out.Emplace(TEXT("Locked"), bLocked ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Stays open"), StaysOpen() ? TEXT("yes (wait -1 / NO_AUTO_RETURN)") : TEXT("no (autoclose)"));
	Out.Emplace(TEXT("Closed pose"), FString::Printf(TEXT("%s / %s"), *ClosedLoc.ToString(), *ClosedRot.ToString()));
	Out.Emplace(TEXT("Open pose"),   FString::Printf(TEXT("%s / %s"), *OpenLoc.ToString(), *OpenRot.ToString()));
	if (!LinkedDoorName.IsEmpty())
	{
		const bool bLinked = World && World->Resolve(LinkedDoor) != nullptr;
		Out.Emplace(TEXT("Linked door"), FString::Printf(TEXT("%s%s"), *LinkedDoorName,
			bLinked ? TEXT(" (resolved)") : TEXT("")));
	}
	if (!UseOverrideName.IsEmpty())
	{
		Out.Emplace(TEXT("Use override"), UseOverrideName);
	}
	Out.Emplace(TEXT("Spawnflags"), FString::Printf(TEXT("%d = %s"), SpawnFlags, *DescribeDoorSpawnFlags(SpawnFlags)));
	AppendSoundDebug(Out);
}

void FElysiumDoorBase::OnParentAttached(const FTransform& ParentWorldTransform)
{
	ClosedLoc = ParentWorldTransform.InverseTransformPosition(ClosedLoc);
	OpenLoc = ParentWorldTransform.InverseTransformPosition(OpenLoc);
	const FQuat ParentInverse = ParentWorldTransform.GetRotation().Inverse();
	ClosedRot = (ParentInverse * ClosedRot.Quaternion()).Rotator();
	OpenRot = (ParentInverse * OpenRot.Quaternion()).Rotator();
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
		// tutorial door is `angles 0 0 0`). Retail adds the signed distance to Source yaw; the
		// Source->Unreal Y reflection reverses that turn, just as it does for func_rotating below.
		// REVERSE (0x2) negates the Source swing before that reflection. Non-default swing axes from
		// `angles` are a later refinement (no exported rotating door uses one).
		const float SourceSign = (SpawnFlags & SF_DOOR_REVERSE) ? -1.0f : 1.0f;
		OutOpenLoc = ClosedLoc;
		OutOpenRot = ClosedRot + FRotator(0.0f, -SourceSign * Distance, 0.0f);   // (Pitch, Yaw, Roll)
	}

	// Retail CRotDoor::DoorGoUp -> OpenAwayFromEntity (FUN_100f3390, ComputeSwingData FUN_100f19b0):
	// the leaf opens AWAY from the activator. Only the swing SIGN/axis is activator-relative here; the
	// swing MAGNITUDE is the door's configured open pose (`distance` degrees about the hinge, already in
	// OpenRot), never re-applied. Speed = deg/s.
	// bResolveSwing == false (the block-reverse reissue) forces the fixed-forward OpenRot with no
	// activator resolution and no warning; true resolves the activator-relative swing (retail T1.4).
	virtual void IssueMoveToOpen(bool bResolveSwing) override
	{
		AngularMove(bResolveSwing ? ChooseOpenTarget() : OpenRot, Speed);
	}
	virtual void IssueMoveToClosed() override { AngularMove(ClosedRot, Speed); }

	// A rotating door resolves its endpoint from body angles (retail CRotDoor::ResolveToggleStateFromTransform).
	virtual bool ResolvesEndpointFromRotation() const override { return true; }

private:
	// The activator-relative open target — OpenRot (forward, +m_vecAngle2) or its mirror (back,
	// -m_vecAngle2) — chosen by comparing the activator's world centre to the two swing reference
	// centres and swinging toward the farther one. Falls back to the deterministic forward pose when
	// there is no activator (the ordinary logic-driven open) or one that no longer resolves.
	FRotator ChooseOpenTarget();
};

// Reference: research/event-surface/swing-centres-findings.md ITEM 1 + door-largeitems-spec.md
// TARGET 1 (both 100%-CONFIRMED). Retail caches two swing reference centres and, at open time, swings
// toward whichever is farther from the activator's world-space centre ("open away from the activator").
FRotator FElysiumFuncDoorRotating::ChooseOpenTarget()
{
	// SF_DOOR_ONEWAY forces the fixed-forward open regardless of the activator (retail gates
	// OpenAwayFromEntity on `!SF_DOOR_ONEWAY` in DoorGoUp, before the activator is ever resolved, T1.4):
	// a ONEWAY door never swings activator-relatively and never enters the activator/resolve path.
	if (SpawnFlags & SF_DOOR_ONEWAY)
	{
		return OpenRot;
	}

	// The two candidate open poses. OpenRot is the forward swing (retail +m_vecAngle2). Its mirror
	// about the closed pose is the back swing (-m_vecAngle2). Deriving the back target from OpenRot's
	// OWN closed->open delta reuses the existing open magnitude (`distance` degrees) exactly — only the
	// sign is negated, so the swing arc is never double-applied.
	const FQuat ClosedQ = ClosedRot.Quaternion();
	const FQuat OpenQ   = OpenRot.Quaternion();
	const FQuat DeltaQ  = ClosedQ.Inverse() * OpenQ;                 // closed -> forward open
	const FRotator BackRot = (ClosedQ * DeltaQ.Inverse()).Rotator(); // closed -> back open (negated arc)

	// No activator: the retail m_hActivator==INVALID branch (a relay/autoclose-driven open). Swing the
	// deterministic forward pose. Not a failure — an ordinary absence, so no warning.
	if (!LastActivator.IsSet())
	{
		return OpenRot;
	}
	FElysiumEntity* Activator = World ? World->Resolve(LastActivator) : nullptr;
	if (!Activator)
	{
		// A bound activator handle that no longer resolves is unexpected on the open path. Runtime
		// failures are never silent: warn, then fall back to the deterministic forward swing.
		UE_LOG(LogElysiumMover, Warning,
			TEXT("door %s: activator %s did not resolve at open time; using the fixed swing direction"),
			*DebugString(), *LastActivator.ToString());
		return OpenRot;
	}

	// ComputeSwingData (FUN_100f19b0): rotate the door's LOCAL collision OBB's two OPPOSITE corners by a
	// candidate open rotation, take the per-axis min/max, average, and add the door's local origin (the
	// hinge = the body's relative location, ClosedLoc). Retail rotates only those two corners, not all
	// eight — reproduce that exactly. Everything stays in the body's parent frame, which is the same
	// map-root frame the activator's Origin lives in, so the comparison needs no transform. When the
	// closed pose is identity (every authored rotating door is `angles 0 0 0`) these rotations equal
	// retail's AngleMatrix(±m_vecAngle2).
	const FBox Local = HullLocalBounds(Def);
	if (!Local.IsValid)
	{
		return OpenRot;   // no readable local bounds; a real door always has hulls
	}
	auto SwingCentre = [&](const FRotator& R)
	{
		const FVector A = R.RotateVector(Local.Min);
		const FVector B = R.RotateVector(Local.Max);
		const FVector Lo(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z));
		const FVector Hi(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z));
		return ClosedLoc + (Lo + Hi) * 0.5f;
	};
	const FVector ForwardCentre = SwingCentre(OpenRot);   // retail 0x6b0
	const FVector BackCentre    = SwingCentre(BackRot);   // retail 0x6d4

	// Swing toward the farther centre: b = (d1 < d2) picks the negated (back) target when the activator
	// is nearer the forward centre. DistSquared preserves the same ordering as retail's Euclidean sqrt
	// compare (both non-negative, monotonic), so the chosen side is identical.
	const FVector P = Activator->Origin;   // retail activator WorldSpaceCenter (vtable +0x300)
	const double D1 = FVector::DistSquared(P, ForwardCentre);
	const double D2 = FVector::DistSquared(P, BackCentre);
	const bool bSwingBack = D1 < D2;

	// HELD — the blocked-latch inversion. Retail flips this choice when a prior block armed CRotDoor's
	// 0x655 latch, set when the blocker's CBaseEntity +0x98 dword is non-zero. That field's identity is
	// NOT-STATICALLY-RECOVERABLE (swing-centres-findings.md ITEM 3), so the inversion stays unmodelled.
	return bSwingBack ? BackRot : OpenRot;
}

// ============================================================================================
// func_door — the sliding door / drawer / cabinet (214 uses / 40 maps; 7 on the tutorial)
// ============================================================================================
//
// Reference: animation_and_movers.md B.2/B.4 + the decompiled CBaseDoor::Spawn (FUN_100ef260),
// which computes the open pose as pos2 = pos1 + movedir · (|size·movedir| − lip). The door slides
// along `angles` (SetMovedir) by its own depth in that direction, minus `lip`; `speed` is in/s.
class FElysiumFuncDoor final : public FElysiumDoorBase
{
protected:
	virtual void ComputeOpenTransform(FVector& OutOpenLoc, FRotator& OutOpenRot) const override
	{
		// movedir from `angles` (Unreal space), REVERSE (0x2) negates it. Travel = the door's own
		// extent projected on movedir minus the lip. The body isn't built yet at Spawn, so the size
		// comes from the def hulls (entity-local cm), not the (absent) brush bounds.
		FVector Dir = SourceAnglesToUnrealDir(Angles);
		if (SpawnFlags & SF_DOOR_REVERSE)
		{
			Dir = -Dir;
		}
		const FVector Size = HullLocalBounds(Def).GetSize();   // full width in cm
		const double Span = FMath::Abs(FVector::DotProduct(Size, Dir.GetAbs()));
		const double Travel = FMath::Max(0.0, Span - Lip * MoverInchToCm);

		OutOpenLoc = ClosedLoc + Dir * Travel;
		OutOpenRot = ClosedRot;   // a slide does not rotate
	}

	// A sliding door has no activator-relative swing, so bResolveSwing is ignored — the slide is
	// always the same fixed open pose. (Retail CBaseDoor::DoorGoUp carries no swing resolution.)
	virtual void IssueMoveToOpen(bool /*bResolveSwing*/) override { LinearMove(OpenLoc, Speed * MoverInchToCm); }   // Speed = in/s
	virtual void IssueMoveToClosed() override { LinearMove(ClosedLoc, Speed * MoverInchToCm); }

	// A sliding door resolves its endpoint from body origin (retail CBaseDoor::ResolveToggleStateFromTransform).
	virtual bool ResolvesEndpointFromRotation() const override { return false; }
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
	// Use is the +use doorknob path (P4.2 look-cursor / the pawn E key drive it in-world): a player
	// use on an unlocked door toggles it (and its linked partner); on a locked door it plays the
	// `locked` sound and fires no output. Wired here so `ent_fire <door> Use` exercises the same path.
	D.Input(TEXT("Use"),    [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumDoorBase&>(E).DoorUse(A.Activator); });

	// Keyfields (B.2).
	ElysiumAddClassField(D, TEXT("speed"),    &FElysiumDoorBase::Speed);
	ElysiumAddClassField(D, TEXT("distance"), &FElysiumDoorBase::Distance);
	ElysiumAddClassField(D, TEXT("wait"),     &FElysiumDoorBase::Wait);
	ElysiumAddClassField(D, TEXT("lip"),      &FElysiumDoorBase::Lip);
	ElysiumAddClassField(D, TEXT("dmg"),      &FElysiumDoorBase::Dmg);
	ElysiumAddClassField(D, TEXT("linked_door"), &FElysiumDoorBase::LinkedDoorName);
	ElysiumAddClassField(D, TEXT("use_override"), &FElysiumDoorBase::UseOverrideName);
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

static TUniquePtr<FElysiumEntity> MakeFuncDoor() { return MakeUnique<FElysiumFuncDoor>(); }

// func_door (sliding) is the second CBaseDoor leaf; it inherits the door inputs/fields through the
// CBaseDoor chain node exactly as the rotating leaf does — only the open-transform + primitive differ.
static FElysiumClassRegistrar GRegFuncDoor(
	TEXT("func_door"), FName(TEXT("CBaseDoor")), &MakeFuncDoor,
	[](FElysiumClassDesc& /*D*/) { /* inherits the door inputs/fields from CBaseDoor via the chain */ });
