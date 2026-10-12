// The mover base (CBaseToggle constant-velocity primitive + the CBaseDoor 4-state machine) and
// both door leaves (func_door_rotating swings, func_door slides).
// Reference: `docs/vtmb/animation_and_movers.md` Part B + the decompiled `vampire.dll` (CBaseDoor::Spawn
// FUN_100ef260, CBaseDoor::Use FUN_100efc90). Completes the door: the full spawnflag table (B.5),
// the sliding leaf (open pose = pos + movedir·(|size·movedir| − lip)), NO_AUTO_RETURN, the PUSE
// +use doorknob path, and `linked_door` (the paired-leaf swing). The use-only trace channel and
// use-icon HUD live elsewhere; here doors ride the look-cursor.

#include "Substrate/ElysiumMover.h"

#include "Substrate/ElysiumLockable.h"
#include "Substrate/ElysiumSkillClasses.h"

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumMoverSounds.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumModelPhysics.h"

#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY(LogElysiumMover);

// `m_vecMoveDir` (Source axes, the words `CBaseEntity::SetMovedir` 0x100ad550 wrote) on the Unreal axes:
// the Source->Unreal Y reflection, no scale and no renormalization (retail uses the words as written;
// `AngleVectors`' forward is unit to an ulp). The exporter leaves `angles` in raw Source space in the
// `.ents` keys (only geometry is converted), so SetMovedir reads Source angles and this reflects its
// output the way the hulls were converted.
FVector ElysiumMoveDirToUnreal(const FVector& SourceMoveDir)
{
	return FVector(SourceMoveDir.X, -SourceMoveDir.Y, SourceMoveDir.Z);
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
	// swing, Source inches for a slide.
	constexpr float DoorResyncTolerance = 0.001f;

	// |a-b| <= tol on each component, mirroring retail ResolveToggleStateFromTransform's float compares.
	bool VectorComponentsWithin(const FVector& A, const FVector& B, float Tol)
	{
		return FMath::Abs(static_cast<float>(A.X/2.54) - static_cast<float>(B.X/2.54)) <= Tol
			&& FMath::Abs(static_cast<float>(A.Y/2.54) - static_cast<float>(B.Y/2.54)) <= Tol
			&& FMath::Abs(static_cast<float>(A.Z/2.54) - static_cast<float>(B.Z/2.54)) <= Tol;
	}

	//100f2520 compares retained Source QAngle words, including full turns, without normalization.
	bool RotatorComponentsWithin(const FRotator& A, const FRotator& B, float Tol)
	{
		return FMath::Abs(static_cast<float>(A.Pitch) - static_cast<float>(B.Pitch)) <= Tol
			&& FMath::Abs(static_cast<float>(A.Yaw) - static_cast<float>(B.Yaw)) <= Tol
			&& FMath::Abs(static_cast<float>(A.Roll) - static_cast<float>(B.Roll)) <= Tol;
	}

	float DoorAngleDifference(float First, float Second)
	{
		//1013d580: raw subtraction followed by repeated float stores, preserving +/-180 ties.
		float Delta = First - Second;
		if (First > Second) { while (Delta > 180.f) Delta -= 360.f; }
		else { while (Delta < -180.f) Delta += 360.f; }
		return Delta;
	}
	float DoorFartherFromZero(float A, float B);

}

// --- CBaseToggle constant-velocity primitive ---

void FElysiumMoverBase::SnapBody(const FVector& RelLoc, const FRotator& RelRot)
{
	SetOrigin(RelLoc);
	SetAngles(RelRot);
	if (Body)
	{
		Body->SetRelativeLocationAndRotation(RelLoc,
			ElysiumModelPhysics::SourceRotation(FVector(RelRot.Pitch, RelRot.Yaw, RelRot.Roll)), /*bSweep*/ false);
	}
}

void FElysiumMoverBase::BeginMove(EMoveKind Kind, const FVector& DestLoc, const FRotator& DestRot, double TravelSeconds)
{
	CurrentMove = Kind;
	MoveDestLoc = DestLoc;
	MoveDestRot = DestRot;
	PhysicsMoveDoneTime = static_cast<float>(LocalTime + TravelSeconds);
}

void FElysiumMoverBase::BeginRebound(EMoveKind Kind)
{
	if (MoveReboundDuration > 0.f)
	{
		MoveReboundStartTime = PhysicsMoveDoneTime;
		PhysicsMoveDoneTime += MoveReboundDuration;
		if (Kind == EMoveKind::Linear) MoveReboundVelocity = -MoveReboundAmount * Velocity;
		else MoveReboundAngularVelocity = -MoveReboundAmount * AngularVelocity;
	}
}

void FElysiumMoverBase::LinearMove(const FVector& DestRelLoc, float SpeedSourceUnitsPerSec)
{
	auto SourceWords = [](const FVector& Cm)
	{ return FVector(static_cast<float>(Cm.X / 2.54), static_cast<float>(Cm.Y / 2.54), static_cast<float>(Cm.Z / 2.54)); };
	const FVector Destination = SourceWords(DestRelLoc);
	const FVector Start = SourceWords(GetOrigin());
	CurrentMove = EMoveKind::Linear; MoveDestLoc = Destination * 2.54;
	if (Start == Destination) { MoveDone(); return; } //101c1520 equality completes synchronously
	const FVector Difference = Destination - Start;
	const FVector Delta(static_cast<float>(Difference.X), static_cast<float>(Difference.Y), static_cast<float>(Difference.Z));
	//159c..15d1: float X/Y deltas, Z retained through FST, then float squared argument.
	const float Squared = static_cast<float>(Difference.Z * Delta.Z + Delta.Y * Delta.Y + Delta.X * Delta.X);
	const double Seconds = FMath::Sqrt(static_cast<double>(Squared)) / SpeedSourceUnitsPerSec;
	BeginMove(EMoveKind::Linear, MoveDestLoc, FRotator::ZeroRotator, Seconds);
	const double Reciprocal = 1. / Seconds;
	SetLocalVelocity(FVector(static_cast<float>(Delta.X * Reciprocal), static_cast<float>(Delta.Y * Reciprocal), static_cast<float>(Delta.Z * Reciprocal)) * 2.54);
	BeginRebound(EMoveKind::Linear);
}

void FElysiumMoverBase::AngularMove(const FRotator& DestRelRot, float SpeedDegPerSec)
{
	const FVector Destination(static_cast<float>(DestRelRot.Pitch), static_cast<float>(DestRelRot.Yaw), static_cast<float>(DestRelRot.Roll));
	const FVector Current = GetAngles();
	const FVector Start(static_cast<float>(Current.X), static_cast<float>(Current.Y), static_cast<float>(Current.Z));
	CurrentMove = EMoveKind::Angular; MoveDestRot = FRotator(Destination.X, Destination.Y, Destination.Z);
	if (Destination == Start) { MoveDone(); return; }
	const FVector Difference = Destination - Start; //101c17e0 raw Euler delta, no shortest arc
	const FVector Delta(static_cast<float>(Difference.X), static_cast<float>(Difference.Y), static_cast<float>(Difference.Z));
	const float Squared = static_cast<float>(Difference.Z * Delta.Z + Delta.Y * Delta.Y + Delta.X * Delta.X);
	const double Seconds = FMath::Sqrt(static_cast<double>(Squared)) / SpeedDegPerSec;
	BeginMove(EMoveKind::Angular, FVector::ZeroVector, MoveDestRot, Seconds);
	const double Reciprocal = 1. / Seconds;
	SetLocalAngularVelocity(FVector(static_cast<float>(Delta.X * Reciprocal), static_cast<float>(Delta.Y * Reciprocal), static_cast<float>(Delta.Z * Reciprocal)));
	BeginRebound(EMoveKind::Angular);
}

void FElysiumMoverBase::TickMove(double Now)
{
	(void)Now;
	PhysicsPusher(World ? static_cast<float>(World->FrameSeconds()) : 0.f);
}

void FElysiumMoverBase::MoveDone()
{
	//101c1720 ->101c1790/101c19e0: snap, stop, deadline=-1 and helper output;
	//then movement type clears before the installed arrival receiver. NextThink is untouched.
	FElysiumEntityWorld* const EntityWorld = World;
	const FElysiumEntityHandle Self = Handle;
	if (EntityWorld)
	{
		EntityWorld->EmitRetailSite(*this, TEXT("toggle_movedone"), TEXT("CBaseToggle::MoveDone"), 0x101c1720u, TEXT("entry"),
			FString::Printf(TEXT("deadline=%.9g nextthink=%.17g kind=%d"), PhysicsMoveDoneTime, NextThink, static_cast<int32>(CurrentMove)));
		if (EntityWorld->Resolve(Self) != this) return;
	}
	if (CurrentMove == EMoveKind::Linear)
	{
		SetOrigin(MoveDestLoc); SetLocalVelocity(FVector::ZeroVector);
		PhysicsMoveDoneTime = -1.f;
		FireOutput(FName(TEXT("OnLinearMoveDone")), FElysiumEntityHandle::Invalid());
	}
	else if (CurrentMove == EMoveKind::Angular)
	{
		SetAngles(MoveDestRot);
		SetLocalAngularVelocity(FVector::ZeroVector);
		PhysicsMoveDoneTime = -1.f;
		FireOutput(FName(TEXT("OnAngularMoveDone")), FElysiumEntityHandle::Invalid());
	}
	if (EntityWorld && EntityWorld->Resolve(Self) != this) return;
	CurrentMove = EMoveKind::None;
}

float FElysiumMoverBase::Slot135(float Interval)
{
	//101c10d0, all five gates and the shipped polynomial retained.
	if (!(Interval > 0.f && PhysicsMoveDoneTime > 0.f && MoveReboundStartTime > 0.f
		&& MoveReboundStartTime < PhysicsMoveDoneTime && MoveReboundDuration > 0.f)) return Interval;
	float Elapsed = (Interval + LocalTime) - MoveReboundStartTime;
	if (Elapsed <= 0.f) return Interval;
	Elapsed = FMath::Min(Elapsed, MoveReboundDuration);
	const float Factor = (Elapsed * Elapsed + 1.f) * Elapsed
		- (Elapsed / MoveReboundDuration) * (MoveReboundDuration * MoveReboundDuration + 1.f) * Elapsed;
	if (MoveReboundVelocity != FVector::ZeroVector) SetLocalVelocity((MoveDestLoc + Factor * MoveReboundVelocity - GetOrigin()) / Interval);
	if (MoveReboundAngularVelocity != FVector::ZeroVector)
		SetLocalAngularVelocity((FVector(MoveDestRot.Pitch, MoveDestRot.Yaw, MoveDestRot.Roll) + Factor * MoveReboundAngularVelocity - GetAngles()) / Interval);
	return Interval;
}

void FElysiumMoverBase::Serialize(FElysiumSaveArchive& Ar)
{
	uint8 Kind = static_cast<uint8>(CurrentMove); Ar << Kind;
	Ar << MoveDestLoc << MoveDestRot;
	Ar << MoveReboundDuration << MoveReboundAmount << MoveReboundStartTime << MoveReboundVelocity << MoveReboundAngularVelocity;
	if (Ar.IsLoading())
	{
		if (Kind > static_cast<uint8>(EMoveKind::Angular) || MoveDestLoc.ContainsNaN() || MoveDestRot.ContainsNaN()
			|| !FMath::IsFinite(MoveReboundDuration) || !FMath::IsFinite(MoveReboundAmount) || !FMath::IsFinite(MoveReboundStartTime)
			|| MoveReboundVelocity.ContainsNaN() || MoveReboundAngularVelocity.ContainsNaN()) { Ar.SetError(); return; }
		CurrentMove = static_cast<EMoveKind>(Kind);
	}
}

void FElysiumMoverBase::OnPostRestore(FElysiumEntityWorld& InWorld)
{
	(void)InWorld;
	//100440c0 reconstructs a registered non-saving field through class223 in Physics,
	//before OnRestore. An omitted null field has only MakeEmpty; do not invent a body here.
}
// Mover sounds — per-mover playback (the `soundgroup` resolver lives in ElysiumMoverSounds.cpp).
//
// Reference: `docs/vtmb/audio_pipeline.md` + the decompiled CBaseDoor::Spawn (FUN_100ef060, reads the
// subkeys "close"/"open"/"swing"/"locked") and CBaseButton::Spawn (FUN_100c8810, reads "on"/"off").
// The movers resolve their `soundgroup` through ElysiumSoundGroups::Resolve — the corpus directory
// convention retail itself walks — and play through the GI audio subsystem's voice pool.

namespace
{
	// Doors/buttons are heard across a room, not map-wide; sphere falloff radius for a mover voice.
	constexpr float ElysiumMoverSoundRadiusCm = 2500.f;
}

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
	SoundSubs = ElysiumSoundGroups::Resolve(SoundCategory, SoundGroup);   // shipped subkeys only
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

// --- CBaseDoor 4-state machine ---

void FElysiumDoorBase::Spawn()
{
	// Cache the two rest poses. Closed is the spawn pose (brush authored at origin; body already
	// seated there by BuildBrushBody). Open is leaf-computed from the keyvalues + spawnflags.
	ClosedLoc = GetOrigin();
	const FVector AuthoredAngles = GetAngles();
	ClosedRot = FRotator(AuthoredAngles.X, AuthoredAngles.Y, AuthoredAngles.Z);
	// `CBaseDoor::vfunc103` 0x100ef260 (Spawn): slot 104 Precache (empty), then slot 171 `SetMovedir`
	// (`[EAX+0x2ac]`, 0x100ad550: `m_vecMoveDir` from the angles, then `SetAngles(0)`, L0-r021) on the
	// sliding leaf, then `SetModel(STRING(GetModelName()) or "")` (slot 105 -> `CBaseEntity::SetModel`
	// 0x100ad460 -> `UTIL_SetModel` 0x101cf4a0): the collision box and `m_vecSize` become the brush
	// model's bounds -- what the open-pose arithmetic below reads through slot 214 `GetSize`
	// (`walks/L0-r016.md`). The brush body is built after Spawn, so the bounds come from the def's
	// hulls, as `UtilSetModel` reads them.
	if (SpawnRunsSetMovedir())
	{
		SetMovedir();                                                          // 0x100ef260 [vtbl+0x2ac]
	}
	else
	{
		//100f1c60 computes rotating endpoints before MoveType8 and SetModel.
		ComputeOpenTransform(OpenLoc, OpenRot);
		SetMoveType(8, 0);
	}
	FString ModelArg = Model;
	SetModel(ModelArg.GetCharArray().GetData() != nullptr ? ModelArg.GetCharArray().GetData() : const_cast<TCHAR*>(TEXT("")));
	if (SpawnRunsSetMovedir()) ComputeOpenTransform(OpenLoc, OpenRot);

	// Mover sounds: open/close/swing/locked from usable/openable/<soundgroup>/. SF_DOOR_SILENT
	// (0x1000) mutes them (RE: FUN_100ee4e0 gates on `m_spawnflags & 0x1000`).
	InitMoverSounds(TEXT("openable"), SF_DOOR_SILENT);

	bLocked = (SpawnFlags & SF_DOOR_LOCKED) != 0;

	if (SpawnFlags & SF_DOOR_START_OPEN)
	{
		if (SpawnRunsSetMovedir())
		{
			//100ef260: the sliding leaf keeps its endpoints and starts at position2/state0.
			SetOrigin(OpenLoc);
			ToggleState = EToggleState::AtTop;
		}
		else
		{
			//100f1c60: rotate to angle2, swap angle1/2 and negate movedir; state remains1.
			SetAngles(OpenRot);
			Swap(ClosedRot, OpenRot);
			MoveDir *= -1.;
			ToggleState = EToggleState::AtBottom;
		}
	}
	else
	{
		ToggleState = EToggleState::AtBottom;
	}
	//100ef260 sets MoveType8 after sliding endpoints/START_OPEN. Both leaf Spawns
	//set Solid1/passable flags after the pose work, before the post-Spawn brush223.
	if (SpawnRunsSetMovedir()) SetMoveType(8, 0);
	SetSolid(1);
	if (SpawnFlags & SF_DOOR_PASSABLE) SetSolidFlags(GetSolidFlags() | 4);
}

void FElysiumDoorBase::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumMoverBase::Serialize(Ar);
	uint8 State = static_cast<uint8>(ToggleState);
	Ar << State;
	Ar << bLocked;
	// `+0x640` / `+0x644`, both SAVE rows of `CBaseDoor`'s datamap (findings § 8). The link's stale
	// words are not: `CAI_Link` has no datamap.
	Ar << NpcFailedTimer;
	Ar << NpcFailedFlags;
	Ar << LastActivator;

	if (Ar.IsLoading())
	{
		if (State > static_cast<uint8>(EToggleState::GoingDown)) { Ar.SetError(); return; }
		// Preserve the actual in-flight clock, destination and arrival receiver. Physical
		// reconstruction occurs after all saved logical words have been applied.
		const EToggleState Saved = static_cast<EToggleState>(State);
		ToggleState = Saved;

		// The Physics block reconstructs registered native geometry at the saved logical pose. The saved
		// scheduled think remains due at its original time; restoration emits no arrival.
	}
}

void FElysiumDoorBase::OnPostRestore(FElysiumEntityWorld& InWorld)
{
	LastActivator = InWorld.RestoreHandle(LastActivator);
	bBlockedReopenAllowed = bCharacterBlockedSwing = false; //654/655 are not SAVE fields
	FElysiumMoverBase::OnPostRestore(InWorld);
}

// Lock/Unlock write the door's own byte only. Retail's InputLock/InputUnlock (FUN_100f0120 /
// FUN_100f02b0) do not reach the doorknobs, and a knobbed door's refusal is decided by the knob —
// so a script that unlocks a door still leaves a locked knob gating the player's +use.
void FElysiumDoorBase::InputLock()
{
	bLocked = true;
}

void FElysiumDoorBase::InputUnlock()
{
	bLocked = false;
}

void FElysiumDoorBase::RegisterDoorknob(FElysiumLockableEntity& Doorknob)
{
	if (Doorknobs.Contains(Doorknob.Handle))
	{
		return;
	}
	// Drop any handle whose knob died without unregistering, so a stale entry cannot consume one of
	// the two slots and turn a legitimate second knob away.
	PruneDoorknobs();
	if (Doorknobs.Num() >= 2)
	{
		UE_LOG(LogElysiumMover, Warning, TEXT("Door %s already has 2 doorknobs"), *DebugString());
		Doorknob.Kill();
		return;
	}
	Doorknobs.Add(Doorknob.Handle);
	// The knob arrives with the lock it seeded from its own `difficulty`. Attaching to a door only
	// re-poses its handle; writing the door's lock over it would open every keypad and padlock whose
	// door carries no LOCKED spawnflag.
	Doorknob.RefreshHandlePose();
	RefreshUseOwner();
}

void FElysiumDoorBase::UnregisterDoorknob(const FElysiumEntityHandle& Doorknob)
{
	Doorknobs.Remove(Doorknob);
	RefreshUseOwner();
}

void FElysiumDoorBase::PruneDoorknobs()
{
	// Resolve is already falsy for a dead or stale handle, so a null result is the whole test.
	for (int32 Index = Doorknobs.Num() - 1; Index >= 0; --Index)
	{
		const FElysiumEntity* Entity = World ? World->Resolve(Doorknobs[Index]) : nullptr;
		if (!Entity || !Entity->AsLockableEntity())
		{
			Doorknobs.RemoveAt(Index);
		}
	}
	RefreshUseOwner();
}

// Retail's "user" is never the activator entity itself: FUN_100f0170/FUN_100f0210 pass the
// activator's character sub-object (+0x9c) and FUN_100eef50 passes +0xa8, DevMsg'ing "Non player
// entity %s trying to use" when it is absent. That pointer is null for anything that is not a
// character, so a relay, button or trigger propagating a non-character activator resolves to NO
// user — the same fall-through as a script-fired input with no activator at all.
const FElysiumCombatCharacter* FElysiumDoorBase::ResolveUser(
	const FElysiumEntityHandle& Activator) const
{
	if (!World || !Activator.IsSet())
	{
		return nullptr;
	}
	// A set handle that no longer resolves is the documented falsy-when-dead/stale contract
	// (ElysiumEntityHandle.h), not a failure, so it takes the same no-user branch silently — and
	// it must, because the reticle calls this every frame with the player's handle.
	const FElysiumEntity* Entity = World->Resolve(Activator);
	return Entity ? Entity->AsCombatCharacter() : nullptr;
}

const FElysiumLockableEntity* FElysiumDoorBase::FindNearestDoorknob(
	const FElysiumEntityHandle& Activator) const
{
	// Retail returns null for a null user before it ever looks at the handles, so an I/O-driven
	// Open/Toggle never consults a knob and falls through to the door's own byte.
	const FElysiumCombatCharacter* User = ResolveUser(Activator);
	if (!User)
	{
		return nullptr;
	}
	const FElysiumLockableEntity* Nearest = nullptr;
	double NearestDist = 0.0;
	for (const FElysiumEntityHandle& Knob : Doorknobs)
	{
		const FElysiumEntity* Entity = World->Resolve(Knob);
		const FElysiumLockableEntity* Doorknob = Entity ? Entity->AsLockableEntity() : nullptr;
		if (!Doorknob)
		{
			continue;
		}
		// Manhattan, not Euclidean — FUN_100ee950 sums the per-axis absolute differences. `<=` keeps
		// the retail tie-break, where the second-registered knob wins an exact draw.
		//
		// Divergence, deliberate: retail measures between WorldSpaceCenter()s (vtable +0x304) while
		// these are entity origins — the player's is its feet, a knob's is its def origin. The Z
		// term is therefore offset, but by the SAME amount for both candidates: across the whole
		// export corpus the two knobs of a door differ in height by at most 2.5 cm while sitting
		// tens of cm apart horizontally, so the offset cancels in the comparison and cannot change
		// which handle is picked. There is no world-space-centre accessor on the substrate to use.
		const FVector ToKnob = Doorknob->Origin - User->Origin;
		const double Dist = FMath::Abs(ToKnob.X) + FMath::Abs(ToKnob.Y) + FMath::Abs(ToKnob.Z);
		if (!Nearest || Dist <= NearestDist)
		{
			Nearest = Doorknob;
			NearestDist = Dist;
		}
	}
	return Nearest;
}

bool FElysiumDoorBase::IsUseLocked() const
{
	return IsUseRefused(World ? World->PlayerHandle() : FElysiumEntityHandle::Invalid());
}

bool FElysiumDoorBase::IsUseRefused(const FElysiumEntityHandle& Activator) const
{
	// `0x100eec70`, four arms in retail's order.
	//
	// Arm 1, `0x100eef10`: `noopenwanted` (`+0x648`) AND a live player whose police-response
	// counter (`0x1017f770`, `player+0x1d10`) is above zero -- for EVERY user, NPC or player. The
	// port had this arm only in `DoorUse`; the lock test itself left it out (findings, correction 3).
	if (bNoOpenWanted)
	{
		const FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr;   // 0x101cd9e0(1)
		if (Player && Player->Police.CopsInPursuit > 0)
		{
			return true;
		}
	}
	// NONPCS (0x200) reports "locked" for an NPC activator, ahead of the knob lookup.
	if ((SpawnFlags & SF_DOOR_NONPCS) != 0)
	{
		const FElysiumCombatCharacter* User = ResolveUser(Activator);
		if (User && User->AsNpcBase())   // `param_1[0x25]`, `+0x94 m_pBaseNPC`
		{
			return true;
		}
	}
	if (const FElysiumLockableEntity* Knob = FindNearestDoorknob(Activator))
	{
		return Knob->IsUseLocked();
	}
	return bLocked;
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

	if (MoveKind() != EMoveKind::None)
	{
		return;
	}

	// At rest: the only scheduled resting-think is the AtTop autoclose (Wait >= 0). A `wait -1`
	// door never schedules a think here, so it stays open.
	if (ToggleState == EToggleState::AtTop)
	{
		// The auto-close think `0x100f09d0`: `IsCloseBlocked(this, 1)`. Blocked -> the same think
		// again at `curtime + 0.5` (`_DAT_10449270`, the double 0.5); clear -> think cleared and
		// `DoorGoDown` (vtable `+0x3d0`, argument 1).
		constexpr double CloseBlockedRethinkSeconds = 0.5;
		if (IsCloseBlocked(/*bAskLinked*/ true))
		{
			NextThink = Now + CloseBlockedRethinkSeconds;
			return;
		}
		NextThink = ELYSIUM_NEVER_THINK;
		DoorGoDown(LastActivator);
	}
}

bool FElysiumDoorBase::IsCloseBlocked(bool bAskLinked)
{
	// `CBaseDoor::IsCloseBlocked` `0x100f0c00`.
	if (World == nullptr)
	{
		return false;
	}
	// Slot 245 (`+0x3d4`) fills the closing volume first.
	const FBox Close = ComputeCloseBounds();
	// The linked door (`m_hLinkedDoor`), asked with the recursion flag cleared, when this call
	// carries it. A blocked partner blocks this leaf too.
	if (bAskLinked)
	{
		if (FElysiumDoorBase* Partner = ResolveLinkedDoor())
		{
			if (Partner != this && Partner->IsCloseBlocked(/*bAskLinked*/ false))
			{
				return true;
			}
		}
	}
	if (!Close.IsValid)
	{
		return false;
	}
	// `gEntList` walk (the entity-handle list stands for it), entity-list order: skip one marked for deletion
	// (`0x100b5190`); keep `FL_CLIENT` (0x80, the player) or `FL_NPC` (0x2000); slot 158 `IsAlive`;
	// the box `m_Collision +0x3c` (`WorldSpaceAABB`) against the volume (`0x10240250`). The FIRST
	// intersecting body decides: an NPC is told, and the door is blocked either way.
	const FElysiumPlayer* Player = World->FindPlayer();
	for (const TUniquePtr<FElysiumEntity>& Entry : World->Entities())
	{
		FElysiumEntity* const Other = Entry.Get();
		if (Other == nullptr || Other->IsDead())
		{
			continue;
		}
		FElysiumNpcBase* const Npc = Other->AsNpcBase();
		const bool bClient = Player != nullptr && static_cast<const FElysiumEntity*>(Player) == Other;
		if (!bClient && Npc == nullptr)
		{
			continue;
		}
		if (Other->LifeState != ElysiumLifeState::Alive)
		{
			continue;
		}
		FVector MinsUnits = FVector::ZeroVector;
		FVector MaxsUnits = FVector::ZeroVector;
		if (!FElysiumNpcBase::RetailCollisionExtents(*Other, MinsUnits, MaxsUnits))
		{
			continue;
		}
		// Retail's OBB is in Source axes; the port's Y is mirrored, so the Y pair swaps sign.
		const float U = ElysiumMove::U;
		const FVector Lo(MinsUnits.X * U, -MaxsUnits.Y * U, MinsUnits.Z * U);
		const FVector Hi(MaxsUnits.X * U, -MinsUnits.Y * U, MaxsUnits.Z * U);
		const FBox OtherBox(Other->Origin + Lo, Other->Origin + Hi);
		if (!OtherBox.Intersect(Close))
		{
			continue;
		}
		if (Npc != nullptr)                                                   // +0x94 m_pBaseNPC
		{
			AddNpcFailedFlags(0x80u);                                         // 0x100f0e90(door, 0x80)
			Npc->HitByDoor(*this);                                            // 0x1027dfb0
		}
		return true;
	}
	return false;
}

void FElysiumDoorBase::StartBlocked(const FElysiumEntityHandle& Blocker)
{
	// `CBaseDoor::StartBlocked` `0x100f1340`. SEAM AT THE CALLER: slot 177 (`+0x2c4`) has no
	// recovered dispatcher, so this body is ported and never reached (findings § 3).
	static const FName OnBlockedClosing(TEXT("OnBlockedClosing"));
	static const FName OnBlockedOpening(TEXT("OnBlockedOpening"));
	if (ToggleState == EToggleState::GoingDown)                               // toggle state 3
	{
		FireOutput(OnBlockedClosing, Blocker);
		return;
	}
	if (World != nullptr)
	{
		// The activator's NPC: `+0x644 |= 2`, then its door-blocked notice.
		if (FElysiumEntity* Activator = World->Resolve(LastActivator))
		{
			if (FElysiumNpcBase* ActivatorNpc = Activator->AsNpcBase())
			{
				AddNpcFailedFlags(0x2u);
				ActivatorNpc->OnDoorBlocked(*this);                           // 0x1027de00
			}
		}
		// The blocker's NPC: `+0x644 |= 0x80`, then hit-by-door.
		if (FElysiumEntity* BlockerEntity = World->Resolve(Blocker))
		{
			if (FElysiumNpcBase* BlockerNpc = BlockerEntity->AsNpcBase())
			{
				AddNpcFailedFlags(0x80u);
				BlockerNpc->HitByDoor(*this);                                 // 0x1027dfb0
			}
		}
	}
	FireOutput(OnBlockedOpening, Blocker);
}

void FElysiumDoorBase::NotifyActivatorFullyOpen()
{
	if (World == nullptr)
	{
		return;
	}
	// `m_hActivator (+0x53c)` -> `+0x94 m_pBaseNPC` -> `0x1027dd10(npc, door)`.
	if (FElysiumEntity* Activator = World->Resolve(LastActivator))
	{
		if (FElysiumNpcBase* Npc = Activator->AsNpcBase())
		{
			Npc->OnDoorFullyOpen(this);
		}
	}
	// The engine half: every body this door's smart link holds at the doorway walks on. Retail has
	// no hold (its navigator is simply unpaused above); the port's link hold is the custom-link
	// wait, which only the door reaching its top ends.
	if (IElysiumEmbodiment* Embodiment = World->Embodiment())
	{
		Embodiment->ReleaseDoorLink(Handle);
	}
}

void FElysiumDoorBase::InputOpen(const FElysiumEntityHandle& Activator)
{
	// Locked door: the direct `Open` input refuses SILENTLY — no sound, no output, no motion
	// (RE: CBaseDoor::InputOpen FUN_100f0170 opens iff `!IsDoorLocked` and fires nothing on the
	// locked path). CBaseDoor carries no OnLockedUse output — that output belongs to prop_switch —
	// and the `locked` sound is a +use affordance played only by CBaseDoor::Use (see DoorUse).
	// The gate is IsUseRefused, not the raw byte: a knobbed door defers to its knob, and an
	// activator-less input degrades to the byte so script-driven opens still work.
	if (IsUseRefused(Activator))
	{
		return;
	}
	// Admission is `!= AT_TOP`, so the input also re-issues from GOING_DOWN *and* GOING_UP — wider
	// than the +use path, which refuses a moving door outright.
	if (ToggleState != EToggleState::AtTop)
	{
		// Retail fires OnOpen twice on an admitted edge: once here at the input, once again inside
		// DoorGoUp. The +use path reaches DoorGoUp without this handler, so a +use fires it once.
		static const FName OnOpen(TEXT("OnOpen"));
		FireOutput(OnOpen, Activator);
		DoorGoUp(Activator);
	}
}

void FElysiumDoorBase::InputClose(const FElysiumEntityHandle& Activator)
{
	// FUN_100f00a0 carries NO lock test at all — a direct Close shuts a locked door.
	if (ToggleState != EToggleState::AtBottom)
	{
		static const FName OnClose(TEXT("OnClose"));
		FireOutput(OnClose, Activator);
		DoorGoDown(Activator);
	}
}

void FElysiumDoorBase::InputToggle(const FElysiumEntityHandle& Activator)
{
	// Locked door: retail InputToggle (FUN_100f0210) gates on `IsDoorLocked` at the top and does
	// nothing at all when locked — no output, no sound, no motion. The `locked` +use affordance
	// belongs to CBaseDoor::Use, not to the Toggle input (see DoorUse).
	if (IsUseRefused(Activator))
	{
		return;
	}
	// FUN_100f0210 reaches the motion helpers DIRECTLY rather than routing through the Open/Close
	// handlers, so Toggle fires OnOpen/OnClose once where those two fire twice. It reverses
	// in-flight from either direction.
	switch (ToggleState)
	{
	case EToggleState::AtBottom:
	case EToggleState::GoingDown:
		DoorGoUp(Activator);
		break;
	case EToggleState::AtTop:
	case EToggleState::GoingUp:
		DoorGoDown(Activator);
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
		/*RadiusCm, table-resolved*/ -1.f, LastActivator, 0.f,
		/*CBaseDoor inserts SOUND_PLAYER type 4 for two seconds*/ ElysiumGameSounds::Player, 2.0);
}

void FElysiumDoorBase::DoorGoUp(const FElysiumEntityHandle& Activator, bool bResolveSwing, bool bPropagateLinked)
{
	LastActivator = Activator;
	if (bPropagateLinked)
		if (FElysiumDoorBase* Partner = ResolveLinkedDoor())
		{
			const FElysiumEntityHandle PartnerHandle = Partner->Handle, PriorActivator = Partner->LastActivator, Self = Handle;
			FElysiumEntityWorld& EntityWorld = *World;
			Partner->DoorGoUp(LastActivator, true, false);
			if (FElysiumEntity* Live = EntityWorld.Resolve(PartnerHandle))
				if (FElysiumDoorBase* LiveDoor = Live->AsDoorBase()) LiveDoor->LastActivator = EntityWorld.Resolve(PriorActivator) ? PriorActivator : FElysiumEntityHandle::Invalid();
			if (EntityWorld.Resolve(Self) != this) return;
		}
	// Sound on motion start: the `open` one-shot + the looping `swing` moving sound (stopped on
	// arrival in MoveDone). A group without `swing` just plays `open`; without `open`, silent travel.
	static const FName Open(TEXT("open")), Swing(TEXT("swing"));
	PlayMoverSound(Open);
	PlayMoverSound(Swing); //100ee4e0 one sound-group event; no explicit loop lifecycle
	EmitDoorGameSound();
	ToggleState = EToggleState::GoingUp;
	static const FName OnOpen(TEXT("OnOpen"));
	IssueMoveToOpen(bResolveSwing);
	FireOutput(OnOpen, Activator); //100f3030: issue AngularMove before output dispatch
}

void FElysiumDoorBase::DoorGoDown(const FElysiumEntityHandle& Activator, bool bPropagateLinked)
{
	LastActivator = Activator;
	if (bPropagateLinked)
		if (FElysiumDoorBase* Partner = ResolveLinkedDoor())
		{
			const FElysiumEntityHandle PartnerHandle = Partner->Handle, PriorActivator = Partner->LastActivator, Self = Handle;
			FElysiumEntityWorld& EntityWorld = *World;
			Partner->DoorGoDown(LastActivator, false);
			if (FElysiumEntity* Live = EntityWorld.Resolve(PartnerHandle))
				if (FElysiumDoorBase* LiveDoor = Live->AsDoorBase()) LiveDoor->LastActivator = EntityWorld.Resolve(PriorActivator) ? PriorActivator : FElysiumEntityHandle::Invalid();
			if (EntityWorld.Resolve(Self) != this) return;
		}
	ToggleState = EToggleState::GoingDown;
	static const FName Swing(TEXT("swing"));
	PlayMoverSound(Swing);
	static const FName OnClose(TEXT("OnClose"));
	IssueMoveToClosed();
	FireOutput(OnClose, Activator);
}

bool FElysiumDoorBase::StaysOpen() const
{
	// Source: a door stays open (no autoclose) when `wait -1` OR the NO_AUTO_RETURN spawnflag is set.
	return Wait < 0.0f || (SpawnFlags & SF_DOOR_NO_AUTO_RETURN) != 0;
}

void FElysiumDoorBase::MoveDone()
{
	FElysiumEntityWorld* const EntityWorld = World;
	const FElysiumEntityHandle Self = Handle;
	FElysiumMoverBase::MoveDone();
	if (EntityWorld && EntityWorld->Resolve(Self) != this) return;
	if (ToggleState == EToggleState::GoingUp)
	{
		bCharacterBlockedSwing = false; //100f0860 head write
		// HitTop, `CBaseDoor::DoorHitTop` `0x100f0860`, in its order: the state; the autoclose think
		// unless the door stays open (`wait -1` or NO_AUTO_RETURN); the activator's NPC told
		// (`OnDoorFullyOpen 0x1027dd10`); `+0x640 = 0`; then `OnFullyOpen` (`0x100cd660`).
		ToggleState = EToggleState::AtTop;
		if (!StaysOpen())
		{
			NextThink = (World ? World->NowSeconds() : 0.0) + Wait;
		}
		else
		{
			NextThink = ELYSIUM_NEVER_THINK;
		}
		NotifyActivatorFullyOpen();
		if (EntityWorld && EntityWorld->Resolve(Self) != this) return;
		NpcFailedTimer = 0.0;                                                 // param_1[400] = 0

		FireOutput(FName((SpawnFlags & SF_DOOR_START_OPEN) ? TEXT("OnFullyClosed") : TEXT("OnFullyOpen")),
			(SpawnFlags & SF_DOOR_START_OPEN) ? Handle : LastActivator);
		if (EntityWorld && EntityWorld->Resolve(Self) != this) return;
		ResetBlockedTracking(); //100f0975 tail249
		// USE_CLOSES installs a separate retail think; that independent body remains open.
	}
	else if (ToggleState == EToggleState::GoingDown)
	{
		PlayMoverSound(FName(TEXT("close"))); //100f11b0 before state, touch rearm and output
		ToggleState = EToggleState::AtBottom;
		FireOutput(FName((SpawnFlags & SF_DOOR_START_OPEN) ? TEXT("OnFullyOpen") : TEXT("OnFullyClosed")), LastActivator);
		if (EntityWorld && EntityWorld->Resolve(Self) != this) return;
		NextThink = ELYSIUM_NEVER_THINK;
		ResetBlockedTracking();
	}
}

void FElysiumDoorBase::Blocked(FElysiumEntity* Other)
{
	//CBaseDoor100f14a0. Null native GameData is a valid selected contact, but the receiver returns.
	if (!Other || !World) return;
	FElysiumEntityWorld& EntityWorld = *World;
	const FElysiumEntityHandle Self = Handle;
	//100a1250 TakeDamage(DMG_CRUSH, this inflictor/attacker, Dmg) is the named L2 hook
	//in hooks.tsv. Its character damage-info consumer is not recovered in this L0 packet.
	if (Wait >= 0.f && bBlockedReopenAllowed && ToggleState != EToggleState::GoingUp)
		DoorGoUp(LastActivator, true);
	if (EntityWorld.Resolve(Self) != this || TargetName.IsEmpty()) return;
	TArray<FElysiumEntityHandle> Group;
	EntityWorld.ForEachNamed(TargetName, [&](FElysiumEntity& Entity)
	{
		if (&Entity != this && Entity.AsDoorBase() && Group.Num() < 64) Group.Add(Entity.Handle);
	}); //100f12a0 retains FindEntityByName order, excluding self
	for (const FElysiumEntityHandle& PeerHandle : Group)
	{
		FElysiumDoorBase* Peer = nullptr;
		if (FElysiumEntity* Entity = EntityWorld.Resolve(PeerHandle)) Peer = Entity->AsDoorBase();
		if (!Peer || !(Peer->Wait >= 0.f)) continue;
		if (MoveDir == Peer->MoveDir && GetPusherAbsoluteVelocity() == Peer->GetPusherAbsoluteVelocity()
			&& AngularVelocity == Peer->AngularVelocity)
		{
			Peer->SimulationTick = SimulationTick;
			if (!Peer->ResolvesEndpointFromRotation())
			{ Peer->SetOrigin(GetOrigin()); Peer->SetAbsoluteVelocity(FVector::ZeroVector); }
			else
			{ const FVector PeerSnapAngles = GetAngles(); Peer->SetAngles(FRotator(PeerSnapAngles.X, PeerSnapAngles.Y, PeerSnapAngles.Z)); Peer->SetLocalAngularVelocity(FVector::ZeroVector); }
		}
		if (ToggleState == EToggleState::GoingUp && Peer->ToggleState != EToggleState::GoingUp)
			Peer->DoorGoUp(Peer->LastActivator, true);
		else if (ToggleState == EToggleState::GoingDown && Peer->ToggleState != EToggleState::GoingDown)
			Peer->DoorGoDown(Peer->LastActivator);
		if (EntityWorld.Resolve(Self) != this) return; //outputs can delete/reparent either leaf
	}
}

void FElysiumDoorBase::OnMoveBlocked(const FHitResult& Hit)
{
	//Compatibility entry for the old sweep caller; selection itself belongs to the native contact.
	FElysiumEntity* Other = nullptr;
	if (World) for (const auto& Entity : World->Entities())
		if (Entity && !Entity->IsDead() && Entity->GetAttachBody() == Hit.GetComponent()) Other = Entity.Get();
	Blocked(Other);
}

// --- +use doorknob path + linked_door ---

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
		const FVector LiveWords = GetAngles(); //100f2520 reads slot221, not native solved Euler
		const FRotator Live(LiveWords.X, LiveWords.Y, LiveWords.Z);
		if (RotatorComponentsWithin(Live, ClosedRot, DoorResyncTolerance))
		{
			ToggleState = EToggleState::AtBottom;
			ResetBlockedTracking();
		}
		else if (RotatorComponentsWithin(Live, OpenRot, DoorResyncTolerance))
		{
			ToggleState = EToggleState::AtTop;
			ResetBlockedTracking();
		}
		// else: genuinely mid-travel — leave the GoingUp/GoingDown state as-is.
	}
	else
	{
		// Sliding leaf (retail CBaseDoor): compare the live body ORIGIN to the two endpoints.
		const FVector Live = GetOrigin(); //100eff90 reads retained local origin
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
	// Step 1 — record the activator, before the override redirect and before any early return.
	// This is not bookkeeping: DoorGoUp/DoorGoDown fire their outputs with it, and the rotating
	// leaf resolves its swing away from it, so a door +used by a second character must re-aim at
	// that character and not at whoever opened it last. A null activator clears it.
	LastActivator = Activator;

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

	// Step 3 — the `noopenwanted` refusal (FUN_100eef10). The key alone never refuses: retail also
	// requires a live player whose police-response counter is above zero, so a flagged exit only
	// shuts while the player is actually being hunted. Nothing else runs — no output, no sound, no
	// state change — which is what stops a hunted player leaving through a wired transition.
	if (bNoOpenWanted)
	{
		const FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr;
		if (Player && Player->Police.CopsInPursuit > 0)
		{
			// DIVERGENCE: retail also sends the refused player a usermessage here (FUN_101cebc0,
			// a CSingleUserRecipientFilter carrying DAT_10726084). That message's registered name
			// is not recovered, so the refusal is reproduced without its player feedback.
			return;
		}
	}

	// 100efc90 step 4: +558 must be zero before reissuing an interrupted move.
	// LinearMove writes 1 and AngularMove (101c17e0) writes 2; slot153's linear
	// velocity predicate is a different observer and cannot stand for this gate.
	if (MoveKind() == EMoveKind::None && ToggleState == EToggleState::GoingDown)
	{
		DoorGoDown(LastActivator);
	}
	else if (MoveKind() == EMoveKind::None && ToggleState == EToggleState::GoingUp)
	{
		DoorGoUp(LastActivator, /*bResolveSwing*/ true);
	}

	// Step 5 (@0x100efc90, vt +0x3e0): UNCONDITIONAL, after the use_override delegation and the
	// self-heal, BEFORE the admission and locked branches. Re-stamp the toggle state from the live
	// body pose so a door whose body never actually moved snaps back to its true endpoint and the
	// following toggle re-opens it. A genuinely mid-travel body is left alone — which, combined
	// with step 7's admission set, is why a moving non-NO_AUTO_RETURN door is a silent no-op.
	ResolveToggleStateFromTransform();

	// Step 6 — the doorknob route. Retail branches on whether the FIRST knob handle still resolves,
	// not on whether one was ever registered, and returns — so a knobbed door never reaches the
	// admission test below. Pruning first keeps a destroyed knob's stale handle from routing a door
	// down this path and thereby skipping step 7.
	PruneDoorknobs();
	if (Doorknobs.Num() > 0)
	{
		DoorknobUse(Activator);
		return;
	}

	// Step 7 — admission. Only a resting door, or one flagged NO_AUTO_RETURN, is admitted; a
	// GOING_* leaf without that flag drops out here.
	if (ToggleState != EToggleState::AtTop && ToggleState != EToggleState::AtBottom
		&& (SpawnFlags & SF_DOOR_NO_AUTO_RETURN) == 0)
	{
		return;
	}

	// Step 8 — the knobless locked branch: a locked door reached by +use plays ONLY the `locked`
	// sound and returns — no output (CBaseDoor has no OnLockedUse) and no motion. This is the
	// single path that voices the locked door; the direct Open/Toggle inputs stay silent.
	if (bLocked)
	{
		static const FName Locked(TEXT("locked"));
		PlayMoverSound(Locked);
		// A refused door is audible to NPCs as well as to the player: retail raises the CSoundEnt
		// stimulus beside the one-shot on this branch too, not only on the doorknob refusal, and
		// only when the activator is a character.
		if (ResolveUser(Activator))
		{
			EmitDoorGameSound();
		}
		return;
	}

	DoorActivate(Activator);
}

void FElysiumDoorBase::DoorknobUse(const FElysiumEntityHandle& Activator)
{
	// Retail requires the activator's character sub-object (+0xa8) and DevMsg's
	// "Non player entity %s trying to use ..." otherwise, without touching the door. Display, not
	// Warning: it is a routine refusal of a non-player user, and an authored relay can drive it
	// every press.
	if (!ResolveUser(Activator))
	{
		UE_LOG(LogElysiumMover, Display,
			TEXT("%s: non-character activator reached the doorknob path; no knob can be selected "
				 "for it, so the use is dropped"), *DebugString());
		return;
	}

	FElysiumLockableEntity* Knob = FindNearestDoorknob(Activator);
	const bool bRefused = Knob ? Knob->IsUseLocked() : bLocked;
	if (!bRefused)
	{
		DoorActivate(Activator);
		return;
	}
	if (Knob)
	{
		// Retail re-poses the handle on exactly this branch, and only on the knob the user reached
		// for (FUN_100eef50 @0x100eef88) — not on every knob and not on every use, or the clip
		// restarts from frame 0 each press.
		Knob->RefreshHandlePose();
	}
	static const FName Locked(TEXT("locked"));
	PlayMoverSound(Locked);
	// The refusal is audible to NPCs too: retail raises a CSoundEnt stimulus beside the one-shot.
	EmitDoorGameSound();
}

void FElysiumDoorBase::DoorActivate(const FElysiumEntityHandle& Activator)
{
	// FUN_100f0340: no lock test and no admission test of its own — the caller already made both
	// decisions. AT_TOP/GOING_UP close, AT_BOTTOM/GOING_DOWN open.
	switch (ToggleState)
	{
	case EToggleState::AtBottom:
	case EToggleState::GoingDown:
		DoorGoUp(Activator);
		break;
	case EToggleState::AtTop:
	case EToggleState::GoingUp:
		DoorGoDown(Activator);
		break;
	}

	// Retail re-poses every registered knob's handle here (+0x444 per knob), so the handle-turn clip
	// plays on a successful open and not only on a refusal. Iterate a copy: a knob can die during
	// this pass and unregister itself, mutating Doorknobs.
	//
	// OPEN RE QUESTION, deliberately not reproduced: each of those +0x444 calls is preceded by
	// `thunk_FUN_10224170`, which resolves to CBaseLockableEnt::InputUnlock -> vtable +0x450 ->
	// FUN_10224950 — the single function in the image that writes `m_LastRoll = 3`. Taken at face
	// value that would mean opening a door unlocks both its knobs, but FUN_10224950 *ends* by
	// calling its owner's +0x3c8 (DoorActivate), so DoorActivate unlocking its knobs would recurse
	// without a terminating condition. The call is also emitted with one argument where
	// InputUnlock takes two. Something in that reading is wrong, so the unlock half is left out
	// until it is understood; only the pose, which is unambiguous and cannot recurse, is applied.
	const TArray<FElysiumEntityHandle> KnobsAtActivate = Doorknobs;
	for (const FElysiumEntityHandle& Knob : KnobsAtActivate)
	{
		FElysiumEntity* Entity = World ? World->Resolve(Knob) : nullptr;
		if (FElysiumLockableEntity* Doorknob = Entity ? Entity->AsLockableEntity() : nullptr)
		{
			Doorknob->RefreshHandlePose();
		}
	}

	// Linked same-direction transport occurs in DoorGoUp / DoorGoDown.

}

void FElysiumDoorBase::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	static const TCHAR* StateNames[] = { TEXT("AtTop (open)"), TEXT("AtBottom (closed)"),
		TEXT("GoingUp (opening)"), TEXT("GoingDown (closing)") };
	Out.Emplace(TEXT("Toggle state"), StateNames[(uint8)ToggleState]);
	Out.Emplace(TEXT("Moving"), MoveKind() != EMoveKind::None
		? (MoveKind() == EMoveKind::Angular ? TEXT("yes (angular)") : TEXT("yes (linear)")) : TEXT("no"));
	// Two authorities, reported separately: the door's own byte, and whichever one actually decides
	// a refusal. A knobbed door's answer comes from the knob.
	Out.Emplace(TEXT("Locked (door byte)"), bLocked ? TEXT("yes") : TEXT("no"));
	if (Doorknobs.Num() > 0)
	{
		Out.Emplace(TEXT("Lock authority"), FString::Printf(TEXT("%d doorknob(s) — the nearest one decides"),
			Doorknobs.Num()));
	}
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
	Out.Emplace(TEXT("NPC failure (+0x644 / +0x640)"),
		FString::Printf(TEXT("0x%x / %.2f"), NpcFailedFlags, NpcFailedTimer));
	if (LinkWords.bStale)
	{
		Out.Emplace(TEXT("Smart link"), FString::Printf(TEXT("stale until %.2f (blocker %s)"),
			LinkWords.StaleUntil, *LinkWords.StaleDoor.ToString()));
	}
	AppendSoundDebug(Out);
}

void FElysiumDoorBase::OnParentAttached(const FTransform& ParentWorldTransform)
{
	ClosedLoc = ParentWorldTransform.InverseTransformPosition(ClosedLoc);
	OpenLoc = ParentWorldTransform.InverseTransformPosition(OpenLoc);
	const FQuat ParentInverse = ParentWorldTransform.GetRotation().Inverse();
	const FRotator ClosedNative = (ParentInverse * ElysiumModelPhysics::SourceRotation(FVector(ClosedRot.Pitch, ClosedRot.Yaw, ClosedRot.Roll))).Rotator();
	const FRotator OpenNative = (ParentInverse * ElysiumModelPhysics::SourceRotation(FVector(OpenRot.Pitch, OpenRot.Yaw, OpenRot.Roll))).Rotator();
	ClosedRot = FRotator(-ClosedNative.Pitch, -ClosedNative.Yaw, ClosedNative.Roll);
	OpenRot = FRotator(-OpenNative.Pitch, -OpenNative.Yaw, OpenNative.Roll);
}

// func_door_rotating — the prototype swinging door (the workhorse: 1236 uses / 22 on the tutorial).

class FElysiumFuncDoorRotating final : public FElysiumDoorBase
{
public:
	void Spawn() override
	{
		//101c1a70: the flags choose a Source QAngle component, independently of authored angles.
		MoveDir = (SpawnFlags & 0x40) ? FVector(0, 0, 1) : (SpawnFlags & 0x80) ? FVector(1, 0, 0) : FVector(0, 1, 0);
		if (SpawnFlags & SF_DOOR_REVERSE) MoveDir *= -1.;
		FElysiumDoorBase::Spawn();
		//100f2290 ->100f3f20: immutable collision bounds produce the blocker angle frame.
		const FBox Local = HullLocalBounds(Def);
		if (Local.IsValid)
		{
			const FVector Min = FVector(Local.Min.X, -Local.Max.Y, Local.Min.Z) / MoverInchToCm;
			const FVector Max = FVector(Local.Max.X, -Local.Min.Y, Local.Max.Z) / MoverInchToCm;
			const int32 FirstAxis = (SpawnFlags & 0x40) ? 1 : (SpawnFlags & 0x80) ? 2 : 0;
			const int32 SecondAxis = (SpawnFlags & 0x40) ? 2 : (SpawnFlags & 0x80) ? 0 : 1;
			const float First = DoorFartherFromZero(static_cast<float>(Min[FirstAxis]), static_cast<float>(Max[FirstAxis]));
			const float Second = DoorFartherFromZero(static_cast<float>(Min[SecondAxis]), static_cast<float>(Max[SecondAxis]));
			const double Rest = FMath::Atan2(static_cast<double>(Second), static_cast<double>(First)) * 57.295780181884766;
			DoorAngleAtRest = static_cast<float>(Rest);
			DoorAngleForward = static_cast<float>(Rest + 90.);
			DoorAngleBackward = static_cast<float>(Rest - 90.);
		}
		LastBlockedAngle = 0.f;
		ResetBlockedTracking();
	}
	void Serialize(FElysiumSaveArchive& Ar) override
	{
		FElysiumDoorBase::Serialize(Ar);
		Ar << DoorAngleAtRest; Ar << DoorAngleForward; Ar << DoorAngleBackward;
		Ar << LastBlockedAngle; Ar << BlockedCount; Ar << BlockedForcedState;
		if (Ar.IsLoading() && (!FMath::IsFinite(DoorAngleAtRest) || !FMath::IsFinite(DoorAngleForward)
			|| !FMath::IsFinite(DoorAngleBackward) || !FMath::IsFinite(LastBlockedAngle)
			|| BlockedCount < 0 || BlockedForcedState < 0 || BlockedForcedState > 3)) Ar.SetError();
	}
	void Blocked(FElysiumEntity* Other) override;
protected:
	virtual void ComputeOpenTransform(FVector& OutOpenLoc, FRotator& OutOpenRot) const override
	{
		//100f1c60: m_vecAngle2 = m_vecAngle1 + distance * movedir, in Source QAngle words.
		OutOpenLoc = ClosedLoc;
		OutOpenRot = ClosedRot + FRotator(MoveDir.X * Distance, MoveDir.Y * Distance, MoveDir.Z * Distance);
	}

	// Retail CRotDoor::DoorGoUp -> OpenAwayFromEntity (FUN_100f3390, ComputeSwingData FUN_100f19b0):
	// the leaf opens AWAY from the activator. Only the swing SIGN/axis is activator-relative here; the
	// swing MAGNITUDE is the door's configured open pose (`distance` degrees about the hinge, already in
	// OpenRot), never re-applied. Speed = deg/s.
	// A live activator and resolve=false retain Blocked's pre-issued move. Null activator or
	// ONEWAY issues OpenRot; resolve=true runs the actual activator-relative swing branch.
	virtual void IssueMoveToOpen(bool bResolveSwing) override
	{
		//100f3030: a valid activator with resolve=false preserves the AngularMove pre-issued
		//by Blocked. Null activator or ONEWAY instead issues the fixed endpoint.
		if (!bResolveSwing && !(SpawnFlags & SF_DOOR_ONEWAY) && World && World->Resolve(LastActivator)) return;
		AngularMove(bResolveSwing ? ChooseOpenTarget() : OpenRot, Speed);
	}
	virtual void IssueMoveToClosed() override { AngularMove(ClosedRot, Speed); }

	// A rotating door resolves its endpoint from body angles (retail CRotDoor::ResolveToggleStateFromTransform).
	virtual bool ResolvesEndpointFromRotation() const override { return true; }
	// `CRotDoor::Spawn` 0x100f1c60 never dispatches slot 171.
	virtual bool SpawnRunsSetMovedir() const override { return false; }
	void ResetBlockedTracking() override { BlockedCount = BlockedForcedState = 0; } //100f1990

	virtual FElysiumDoorNpcOpenData GetNPCOpenData(const FElysiumEntity* Npc, bool bOpening) const override;
	virtual FBox ComputeCloseBounds() const override;

private:
	// The activator-relative open target — OpenRot (forward, +m_vecAngle2) or its mirror (back,
	// -m_vecAngle2) — chosen by comparing the activator's world centre to the two swing reference
	// centres and swinging toward the farther one. Falls back to the deterministic forward pose when
	// there is no activator (the ordinary logic-driven open) or one that no longer resolves.
	FRotator ChooseOpenTarget();
	float DoorAngleAtRest = 0.f, DoorAngleForward = 0.f, DoorAngleBackward = 0.f, LastBlockedAngle = 0.f;
	int32 BlockedCount = 0, BlockedForcedState = 0; //6f8..70c, SAVE
};

void FElysiumFuncDoorRotating::Blocked(FElysiumEntity* Other)
{
	//100f3900: both opening and closing enter the geometric arbitration.
	if (!Other || !World) return;
	FElysiumEntityWorld& EntityWorld = *World;
	const FElysiumEntityHandle Self = Handle, BlockerHandle = Other->Handle;
	bBlockedReopenAllowed = true;
	if (Other->AsCombatCharacter()) bCharacterBlockedSwing = true; //+9c identity, L2 owns its classification
	const FVector ActualAngles = GetAbsAngles();
	const float Current = static_cast<float>(((SpawnFlags & 0x40) ? ActualAngles.Z : (SpawnFlags & 0x80) ? ActualAngles.X : ActualAngles.Y) + DoorAngleAtRest);
	const FVector RootOrigin = GetAbsOrigin(), BlockerOrigin = Other->GetAbsOrigin();
	FVector Direction(static_cast<float>(BlockerOrigin.X / MoverInchToCm) - static_cast<float>(RootOrigin.X / MoverInchToCm),
		static_cast<float>(-BlockerOrigin.Y / MoverInchToCm) - static_cast<float>(-RootOrigin.Y / MoverInchToCm),
		static_cast<float>(BlockerOrigin.Z / MoverInchToCm) - static_cast<float>(RootOrigin.Z / MoverInchToCm));
	if (SpawnFlags & 0x40) Direction.X = 0.; else if (SpawnFlags & 0x80) Direction.Y = 0.; else Direction.Z = 0.;
	float Pitch = 0.f, Yaw = 0.f;
	if (Direction.X == 0. && Direction.Y == 0.) Pitch = Direction.Z > 0. ? 270.f : 90.f;
	else
	{
		Yaw = static_cast<float>(FMath::Atan2(Direction.Y, Direction.X) * 57.29577951308232);
		if (Yaw < 0.f) Yaw += 360.f;
		const float HorizontalSquared = static_cast<float>(Direction.X * Direction.X + Direction.Y * Direction.Y);
		Pitch = static_cast<float>(FMath::Atan2(-Direction.Z, FMath::Sqrt(static_cast<double>(HorizontalSquared))) * 57.29577951308232);
		if (Pitch < 0.f) Pitch += 360.f;
	}
	//100f3e60 selects pitch for SF40, roll (literal zero) for SF80, yaw otherwise.
	const float Bearing = (SpawnFlags & 0x40) ? Pitch : (SpawnFlags & 0x80) ? 0.f : Yaw;
	const float ForwardDiff = DoorAngleDifference(Current, DoorAngleForward), BackwardDiff = DoorAngleDifference(Current, DoorAngleBackward);
	if (BlockedCount < 1) BlockedCount = 1;
	else if (!(FMath::Abs(DoorAngleDifference(Current, LastBlockedAngle)) <= 1.f)) ResetBlockedTracking();
	else if (++BlockedCount > 14)
	{ ResetBlockedTracking(); BlockedForcedState = ToggleState == EToggleState::GoingUp ? 3 : 2; }
	LastBlockedAngle = Current;
	if (ToggleState == EToggleState::GoingUp)
	{
		bBlockedReopenAllowed = false;
		if (BlockedForcedState == 3) DoorGoDown(LastActivator);
		else if (BlockedForcedState != 2)
		{
			const float ChosenAngle = FMath::Abs(ForwardDiff) < FMath::Abs(BackwardDiff) ? DoorAngleForward : DoorAngleBackward;
			if (FMath::Abs(DoorAngleDifference(Bearing, ChosenAngle)) < FMath::Abs(DoorAngleDifference(Current, ChosenAngle))) DoorGoDown(LastActivator);
		}
	}
	else if (ToggleState == EToggleState::GoingDown)
	{
		bBlockedReopenAllowed = false;
		if (BlockedForcedState == 2) DoorGoUp(LastActivator, true);
		else if (BlockedForcedState != 3
			&& FMath::Abs(DoorAngleDifference(Bearing, DoorAngleAtRest)) < FMath::Abs(DoorAngleDifference(Current, DoorAngleAtRest))
			&& ToggleState != EToggleState::GoingUp)
		{
			AngularMove(FMath::Abs(ForwardDiff) < FMath::Abs(BackwardDiff) ? OpenRot : OpenRot * -1., Speed);
			DoorGoUp(LastActivator, false);
		}
	}
	if (EntityWorld.Resolve(Self) != this) return;
	FElysiumDoorBase::Blocked(EntityWorld.Resolve(BlockerHandle));
	if (EntityWorld.Resolve(Self) == this) bBlockedReopenAllowed = true;
}

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
	const FRotator BackRot = OpenRot * -1.; //100f3390 negates all three m_vecAngle2 words

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
		const FQuat Rotation = ElysiumModelPhysics::SourceRotation(FVector(R.Pitch, R.Yaw, R.Roll));
		const FVector A = Rotation.RotateVector(Local.Min);
		const FVector B = Rotation.RotateVector(Local.Max);
		const FVector Lo(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z));
		const FVector Hi(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z));
		return ClosedLoc + (Lo + Hi) * 0.5f;
	};
	const FVector ForwardCentre = SwingCentre(OpenRot);   // retail 0x6b0
	const FVector BackCentre    = SwingCentre(BackRot);   // retail 0x6d4

	// Swing toward the farther centre: b = (d1 < d2) picks the negated (back) target when the activator
	// is nearer the forward centre. DistSquared preserves the same ordering as retail's Euclidean sqrt
	// compare (both non-negative, monotonic), so the chosen side is identical.
	auto DistanceWord = [](const FVector& First, const FVector& Second)
	{
		const FVector Delta(static_cast<float>(First.X / MoverInchToCm) - static_cast<float>(Second.X / MoverInchToCm),
			static_cast<float>(First.Y / MoverInchToCm) - static_cast<float>(Second.Y / MoverInchToCm),
			static_cast<float>(First.Z / MoverInchToCm) - static_cast<float>(Second.Z / MoverInchToCm));
		return static_cast<float>(FMath::Sqrt(static_cast<double>(static_cast<float>(Delta.SizeSquared()))));
	};
	const float D1 = DistanceWord(Activator->WorldSpaceCenter(), ForwardCentre);
	const float D2 = DistanceWord(Activator->WorldSpaceCenter(), BackCentre);
	const bool bSwingBack = bCharacterBlockedSwing ? D2 <= D1 : D1 < D2;
	return bSwingBack ? BackRot : OpenRot;
}

namespace
{
	// `0x100f3ea0(0, a, b)`: whichever of `a` / `b` lies further from zero, by more than
	// `_DAT_104546c4` (4.0, read from the image); a tie inside that band answers 0.
	float DoorFartherFromZero(float A, float B)
	{
		constexpr float Tie = 4.0f;
		const float Diff = FMath::Abs(B) - FMath::Abs(A);
		if (Tie < Diff)
		{
			return B;
		}
		if (Diff < -Tie)
		{
			return A;
		}
		return 0.0f;
	}

	// `0x101d2f40`: the yaw vector `(cos, sin, 0)` of an angle in degrees (x 0.017453292).
	FVector DoorYawVector(float Degrees)
	{
		const float Radians = Degrees * 0.017453292f;
		return FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.0);
	}
}

FElysiumDoorNpcOpenData FElysiumFuncDoorRotating::GetNPCOpenData(const FElysiumEntity* Npc, bool bOpening) const
{
	// `CRotDoor::GetNPCOpenData` `0x100f2bd0`. Worked in retail's own frame (Source units, Y not
	// mirrored) so every constant stays checkable; the answer is mirrored into the port's at the end.
	FElysiumDoorNpcOpenData Out;                                              // out+0x18 = -1 on every refusal
	// A null NPC, or spawnflags 0xc0 (the two non-yaw swing axes) -> -1 (`100f2c31`).
	if (Npc == nullptr || (SpawnFlags & 0xc0) != 0)
	{
		return Out;
	}
	// `CRotDoor::ComputeDoorAngles` `0x100f3f20` (the spawn-time words `+0x6f8` / `+0x6fc` /
	// `+0x700`), yaw arm: the leaf's far edge from the hinge in the collision OBB, local axes.
	// `_DAT_10446758` = 57.29578 and `_DAT_10455050` = 90.0, both read from the image.
	const float U = ElysiumMove::U;
	const FBox Local = HullLocalBounds(Def);
	if (!Local.IsValid)
	{
		return Out;
	}
	const float EdgeX = DoorFartherFromZero(static_cast<float>(Local.Min.X / U), static_cast<float>(Local.Max.X / U));
	const float EdgeY = DoorFartherFromZero(static_cast<float>(-Local.Max.Y / U), static_cast<float>(-Local.Min.Y / U));
	const float AngleAtRest = FMath::Atan2(EdgeY, EdgeX) * 57.29578f;         // m_fDoorAngleAtRest
	const FVector Rest = DoorYawVector(AngleAtRest);
	const FVector Forward = DoorYawVector(AngleAtRest + 90.0f);              // m_fDoorAngleOpenForward
	const FVector Backward = DoorYawVector(AngleAtRest - 90.0f);             // m_fDoorAngleOpenBackward

	// `d = npc.AbsOrigin - door.AbsOrigin` (both slot 217), 2-D.
	auto SourceOf = [U](const FVector& Cm) { return FVector(Cm.X / U, -Cm.Y / U, Cm.Z / U); };
	const FVector DoorSrc = SourceOf(Origin);
	const FVector D = SourceOf(Npc->Origin) - DoorSrc;
	const double ForwardDot = Forward.X * D.X + Forward.Y * D.Y;
	const double BackwardDot = Backward.X * D.X + Backward.Y * D.Y;
	// `_DAT_104454c4` = 0.0. Written as "not below" so a NaN falls through to -1 (`100f2e90`).
	FVector Open;
	if (!(ForwardDot < 0.0) && !FMath::IsNaN(ForwardDot))
	{
		Open = Forward;
	}
	else if (!(BackwardDot < 0.0) && !FMath::IsNaN(BackwardDot))
	{
		Open = Backward;
	}
	else
	{
		return Out;
	}

	// `bOpening`: stand 100 out (`_DAT_10450564`) and play ACT_IDLE (1); otherwise 50 out
	// (`_DAT_1044ffe8`) and 0xc84. Both 24 along the leaf (`_DAT_1044dba8`) and 54 down
	// (`_DAT_1045504c`). The face is always `-open`.
	const float StandOut = bOpening ? 100.0f : 50.0f;
	FVector StandSrc = DoorSrc + Rest * 24.0f + Open * StandOut;
	StandSrc.Z -= 54.0f;
	Out.StandPosCm = FVector(StandSrc.X * U, -StandSrc.Y * U, StandSrc.Z * U);
	Out.FaceDir = FVector(-Open.X, Open.Y, -Open.Z);                          // -open, Y mirrored
	Out.Activity = bOpening ? 1 : 0xc84;
	return Out;
}

FBox FElysiumFuncDoorRotating::ComputeCloseBounds() const
{
	// `CRotDoor` slot 245 `0x100f2a00`: at `+angle2` the cached forward box (`+0x698..+0x6ac`), at
	// `-angle2` the cached back box (`+0x6bc..+0x6d0`), else (DevMsg "ComputeCloseBounds() called
	// when...") `ComputeSwingData` at the current angles -- and both caches are `ComputeSwingData`
	// at those angles, so the answer is always `ComputeSwingData(current)` (`0x100f19b0`): the
	// collision OBB's mins/maxs, unioned per axis with those two corners rotated by the angles,
	// plus the origin. The port states it in the body's parent frame, the frame `ChooseOpenTarget`
	// compares an activator's origin in.
	const FBox Local = HullLocalBounds(Def);
	if (!Local.IsValid)
	{
		return FBox(ForceInit);
	}
	const FRotator Live = Body ? Body->GetRelativeRotation() : ClosedRot;
	const FQuat Swing = ClosedRot.Quaternion().Inverse() * Live.Quaternion();
	FBox Out = Local;
	Out += Swing.RotateVector(Local.Min);
	Out += Swing.RotateVector(Local.Max);
	return Out.ShiftBy(ClosedLoc);
}

// func_door — the sliding door / drawer / cabinet (214 uses / 40 maps; 7 on the tutorial).
//
// Reference: animation_and_movers.md B.2/B.4 + the decompiled CBaseDoor::Spawn (`CBaseDoor::vfunc103`
// 0x100ef260), which computes the open pose as `pos2 = pos1 + movedir * T`, `T = |(size.x - 2) *
// movedir.x| + |(size.y - 2) * movedir.y| + |(size.z - 2) * movedir.z| - lip` (`walks/L0-r016.md`).
// The door slides along `angles` (SetMovedir) by its own depth in that direction, less a 2-unit
// inset and the `lip`; `speed` is in/s.
class FElysiumFuncDoor final : public FElysiumDoorBase
{
protected:
	virtual void ComputeOpenTransform(FVector& OutOpenLoc, FRotator& OutOpenRot) const override
	{
		// `m_vecMoveDir` (+0x190), which Spawn's slot-171 `SetMovedir` 0x100ad550 wrote (Source axes,
		// raw f32 words), on the Unreal axes (the Y reflection). REVERSE (0x2) negates it.
		FVector Dir = ElysiumMoveDirToUnreal(MoveDir);
		if (SpawnFlags & SF_DOOR_REVERSE)
		{
			Dir = -Dir;
		}
		OutOpenLoc = ClosedLoc + Dir * (ElysiumRetailMoverTravelUnits(*this, Dir, Lip) * MoverInchToCm);
		OutOpenRot = ClosedRot;   // a slide does not rotate
	}

	// A sliding door has no activator-relative swing, so bResolveSwing is ignored — the slide is
	// always the same fixed open pose. (Retail CBaseDoor::DoorGoUp carries no swing resolution.)
	virtual void IssueMoveToOpen(bool /*bResolveSwing*/) override { LinearMove(OpenLoc, Speed); } // authored in/s
	virtual void IssueMoveToClosed() override { LinearMove(ClosedLoc, Speed); }

	// A sliding door resolves its endpoint from body origin (retail CBaseDoor::ResolveToggleStateFromTransform).
	virtual bool ResolvesEndpointFromRotation() const override { return false; }

	// `CBaseDoor` slot 245 `0x100f0a40`: the current origin moved by `-movedir` (`_DAT_104492dc` =
	// -1.0) times the travel (`|(size - 2)·movedir| - lip`, `_DAT_10452dc4` = 2.0, the same sum
	// `ComputeOpenTransform` takes from slot 214, no clamp), plus the collision OBB -- the box the
	// leaf fills when closed. Retail's call sites run it on a door at its open pose, where the move
	// lands back on `m_vecPosition1`; the port asks it of the closed pose, `ClosedLoc` itself, so the
	// box is the hulls there. GetNPCOpenData stays the base's -1 (`0x100f0ef0`).
	virtual FBox ComputeCloseBounds() const override
	{
		const FBox Local = HullLocalBounds(Def);
		return Local.IsValid ? Local.ShiftBy(ClosedLoc) : FBox(ForceInit);
	}
};

double ElysiumRetailMoverTravelUnits(const FElysiumEntity& Mover, const FVector& MoveDir, float LipUnits)
{
	// The one extent sum of `CBaseDoor::vfunc103` 0x100ef260 (`100ef2d1-100ef31d`), `CBaseDoor::vfunc245`
	// 0x100f0a40, `CBaseButton::Spawn` 0x100c8d60 and `CFuncMoveLinear::Spawn` 0x10116030: slot 214
	// `GetSize` called three times (x from the first, z from the second, y from the third), then
	// `T = |(size.x - 2.0) * dir.x| + |(size.y - 2.0) * dir.y| + |(size.z - 2.0) * dir.z| - lip`, 2.0 the
	// f32 at `0x10452dc4`, the z term spilled as a double between steps, and NO clamp at zero: a lip
	// past the depth moves the mover the other way (`walks/L0-r016.md`). Source units in, Source
	// units out. `MoveDir` is a unit vector on the Unreal axes; the Y reflection does not change the
	// absolute values the sum takes.
	FElysiumEntity& Dispatch = const_cast<FElysiumEntity&>(Mover);   // slot 214 is non-const in retail
	const FVector& First = Dispatch.GetSize();                                           // call 1: x, read last
	const double TermZ = FMath::Abs((static_cast<double>(Dispatch.GetSize().Z) - 2.0) * MoveDir.Z);   // call 2: z (spilled)
	const double TermY = FMath::Abs((static_cast<double>(Dispatch.GetSize().Y) - 2.0) * MoveDir.Y);   // call 3: y
	const double TermX = FMath::Abs((static_cast<double>(First.X) - 2.0) * MoveDir.X);
	return ((TermX + TermY) + TermZ) - static_cast<double>(LipUnits);
}

// --- Registration ---

void ElysiumBuildCBaseDoor(FElysiumClassDesc& D)
{
	// Inputs — the door I/O surface (B.3). Reach both leaves through the CBaseDoor chain node.
	D.Input(TEXT("Open"),   [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumDoorBase&>(E).InputOpen(A.Activator); });
	D.Input(TEXT("Close"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumDoorBase&>(E).InputClose(A.Activator); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumDoorBase&>(E).InputToggle(A.Activator); });
	D.Input(TEXT("Lock"),   [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumDoorBase&>(E).InputLock(); });
	D.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumDoorBase&>(E).InputUnlock(); });
	// Use is the +use doorknob path (look-cursor / the pawn E key drive it in-world): a player
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
	ElysiumAddClassField(D, TEXT("noopenwanted"), &FElysiumDoorBase::bNoOpenWanted);
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
