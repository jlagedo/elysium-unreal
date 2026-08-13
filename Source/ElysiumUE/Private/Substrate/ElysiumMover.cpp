// P4.1-4.3 — the mover base (CBaseToggle constant-velocity primitive + the CBaseDoor 4-state
// machine), both door leaves (func_door_rotating swings, func_door slides), and func_button.
// Reference: `docs/vtmb/animation_and_movers.md` Part B + the decompiled `vampire.dll` (CBaseDoor::Spawn
// FUN_100ef260, CBaseDoor::Use FUN_100efc90, CBaseButton::Spawn FUN_100c8d60). P4.3 completes the
// door: the full spawnflag table (B.5), the sliding leaf (open pose = pos + movedir·(|size·movedir|
// − lip)), NO_AUTO_RETURN, the PUSE +use doorknob path, and `linked_door` (the paired-leaf swing).
// The full use-only trace channel + use-icon HUD are P4.4; here doors ride the P4.2 look-cursor.

#include "Substrate/ElysiumMover.h"

#include "Substrate/ElysiumSkillClasses.h"

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumMoverSounds.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"

#include "Dom/JsonObject.h"
#include "GameFramework/Pawn.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMover, Log, All);

namespace
{
	// Source keyvalues (speed, lip, distance-as-inches) are raw Source inches; the exporter emits
	// geometry in cm (the UE_ convention). Linear travel must convert; angular (degrees) does not.
	constexpr float MoverInchToCm = 2.54f;

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

	// Register a field backed by a *subclass* member (FElysiumClassDesc::Field only takes base
	// FElysiumEntity members; door keyfields live on FElysiumDoorBase). Mirrors AddSubclassField in
	// ElysiumStarterClasses.cpp — the accessor static_casts, always valid since a class's field
	// table is only walked for entities of that class or a subclass.
	// File-unique name: ElysiumStarterClasses.cpp has an identical template, and both can land in
	// one unity blob.
	template <typename TClass, typename TMember>
	void AddDoorSubclassField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, EElysiumField Flags = ElysiumFieldDefault)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(Flags);
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
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
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
// Mover sounds (P6.4) — the soundgroup manifest + per-mover playback
// ============================================================================================
//
// Reference: `docs/vtmb/audio_pipeline.md` + the decompiled CBaseDoor::Spawn (FUN_100ef060, reads the
// subkeys "close"/"open"/"swing"/"locked") and CBaseButton::Spawn (FUN_100c8810, reads "on"/"off").
// VtMB has no soundgroup *data file*: a `soundgroup` token resolves by directory convention to
// sound/usable/<category>/<token>/<subkey>.wav (openable=doors, switches=buttons). The offline
// UE_extract_sounds.py mirrors those WAVs and writes out/sound/usable/soundgroups.json; this loads
// it once and the movers play through the GI audio subsystem's voice pool (the 6.3 path).

namespace
{
	// Doors/buttons are heard across a room, not map-wide; sphere falloff radius for a mover voice.
	constexpr float ElysiumMoverSoundRadiusCm = 2500.f;
}

// cat -> group(lower) -> subkey -> sound-relative WAV. Loaded once from the offline manifest.
using FMoverSoundTable = TMap<FString, TMap<FString, TMap<FName, FString>>>;

const FMoverSoundTable& ElysiumMoverSoundManifest()
{
	static FMoverSoundTable Table;
	static bool bLoaded = false;
	if (bLoaded)
	{
		return Table;
	}
	bLoaded = true;   // load-once, even on failure (missing manifest = movers stay silent)

	const FString Path = FElysiumContentPaths::SoundDir() / TEXT("usable/soundgroups.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogElysiumMover, Log, TEXT("mover sounds: no manifest at %s (movers silent)"), *Path);
		return Table;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogElysiumMover, Warning, TEXT("mover sounds: manifest parse failed (%s)"), *Path);
		return Table;
	}

	// { category: { group: { subkey: relpath } } }
	for (const auto& CatPair : Root->Values)
	{
		const TSharedPtr<FJsonObject>* CatObj;
		if (!CatPair.Value.IsValid() || !CatPair.Value->TryGetObject(CatObj))
		{
			continue;
		}
		TMap<FString, TMap<FName, FString>>& Groups = Table.FindOrAdd(FString(CatPair.Key).ToLower());
		for (const auto& GroupPair : (*CatObj)->Values)
		{
			const TSharedPtr<FJsonObject>* GroupObj;
			if (!GroupPair.Value.IsValid() || !GroupPair.Value->TryGetObject(GroupObj))
			{
				continue;
			}
			TMap<FName, FString>& Subs = Groups.FindOrAdd(FString(GroupPair.Key).ToLower());
			for (const auto& SubPair : (*GroupObj)->Values)
			{
				FString Rel;
				if (SubPair.Value.IsValid() && SubPair.Value->TryGetString(Rel))
				{
					Subs.Add(FName(FString(SubPair.Key)), Rel);
				}
			}
		}
	}
	return Table;
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
	// Locked door: the locked path plays the `locked` sound, fires OnLockedUse, and does not move
	// (B.4 — RE: CBaseDoor::Use FUN_100efc90 plays the locked index DAT_106eb3e4 on the locked path).
	if (bLocked)
	{
		static const FName Locked(TEXT("locked"));
		PlayMoverSound(Locked);
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
	// Sound on motion start: the `open` one-shot + the looping `swing` moving sound (stopped on
	// arrival in MoveDone). A group without `swing` just plays `open`; without `open`, silent travel.
	static const FName Open(TEXT("open")), Swing(TEXT("swing"));
	PlayMoverSound(Open);
	StartMoverLoop(Swing);
	static const FName OnOpen(TEXT("OnOpen"));
	FireOutput(OnOpen, Activator);
	IssueMoveToOpen();
}

void FElysiumDoorBase::DoorGoDown(const FElysiumEntityHandle& Activator)
{
	LastActivator = Activator;
	ToggleState = EToggleState::GoingDown;
	static const FName Close(TEXT("close")), Swing(TEXT("swing"));
	PlayMoverSound(Close);
	StartMoverLoop(Swing);
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
		DoorGoUp(LastActivator);   // reverse: re-open away from the blocker
	}
}

// --- +use doorknob path + linked_door (P4.3) --------------------------------------------

bool FElysiumDoorBase::IsUsable() const
{
	// PUSE (0x100) arms the +use look-cursor. A hidden/dead door disarms (the world also re-checks
	// IsInert, but keep the class honest). Locked doors are still "usable" — a use fires OnLockedUse.
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

	// CBaseDoor::DoorknobUse: toggle this leaf, then the linked partner (the double-door swing). The
	// partner receives `Toggle` — NOT `Use` — so it never mirrors back (no recursion), and each leaf
	// still runs its own locked check (a locked half fires OnLockedUse and stays put).
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
		// tutorial door is `angles 0 0 0`). REVERSE (0x2) negates the swing. Non-default swing axes
		// from `angles` are a later refinement (no exported rotating door uses one).
		const float Sign = (SpawnFlags & SF_DOOR_REVERSE) ? -1.0f : 1.0f;
		OutOpenLoc = ClosedLoc;
		OutOpenRot = ClosedRot + FRotator(0.0f, Sign * Distance, 0.0f);   // (Pitch, Yaw, Roll)
	}

	virtual void IssueMoveToOpen() override   { AngularMove(OpenRot,   Speed); }   // Speed = deg/s
	virtual void IssueMoveToClosed() override { AngularMove(ClosedRot, Speed); }
};

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

	virtual void IssueMoveToOpen() override   { LinearMove(OpenLoc,   Speed * MoverInchToCm); }   // Speed = in/s
	virtual void IssueMoveToClosed() override { LinearMove(ClosedLoc, Speed * MoverInchToCm); }
};

// ============================================================================================
// func_elevator — VtMB CFuncElevator (vampire.dll 0x1020e740 / datamap 0x105aadf0)
// ============================================================================================

class FElysiumElevator final : public FElysiumMoverBase
{
public:
	float Speed = 100.0f;
	int32 NumFloors = 0;
	bool bLocked = false;
	FString StartSound;
	FString StopSound;

	virtual void Spawn() override
	{
		CurrentFloor = 0;       // CFuncElevator::Spawn 0x1020f220
		TargetFloor = INDEX_NONE;
		bRestoreSeatPending = false;
		Passed.Init(false, 8);
	}

	void InputLock() { bLocked = true; }
	void InputUnlock() { bLocked = false; }

	void InputGotoFloor(int32 OneBased, const FElysiumEntityHandle& Activator)
	{
		const int32 Requested = OneBased - 1;
		if (CurrentFloor == INDEX_NONE || bLocked)
		{
			return;   // retail ignores retargets while moving and all requests while locked
		}
		if (Requested == CurrentFloor)
		{
			FireReachOutputs(CurrentFloor, Activator);   // synchronous in retail
			return;
		}
		// Retail's exact guard is index < 9 && index <= numfloors && index >= 0. The table itself
		// has eight rows, so cap to its real storage while preserving the inclusive numfloors test.
		if (Requested < 0 || Requested >= 8 || Requested > NumFloors
			|| !Def || !Def->ElevatorFloors.IsValidIndex(Requested))
		{
			UE_LOG(LogElysiumMover, Warning, TEXT("%s asked to go to invalid floor %d"),
				*DebugString(), Requested);
			return;
		}
		if (!Body)
		{
			UE_LOG(LogElysiumMover, Warning, TEXT("%s cannot move without a brush body"),
				*DebugString());
			return;
		}

		LastActivator = Activator;
		MoveStartZ = Body->GetRelativeLocation().Z;
		TargetFloor = Requested;
		Passed.Init(false, 8);
		const FVector Destination(Origin.X, Origin.Y, Def->ElevatorFloors[Requested]);
		LinearMove(Destination, Speed * MoverInchToCm);
		CurrentFloor = INDEX_NONE;
		PlayMoverSoundRel(StartSound);
		static const FName OnMoveStart(TEXT("OnMoveStart"));
		FireOutput(OnMoveStart, Activator);
	}

	void InputSnapToFloor(int32 OneBased)
	{
		const int32 Requested = OneBased - 1;
		if (!Def || !Def->ElevatorFloors.IsValidIndex(Requested) || Requested >= 8)
		{
			return;
		}
		SnapBody(FVector(Origin.X, Origin.Y, Def->ElevatorFloors[Requested]), FRotator::ZeroRotator);
		CurrentFloor = Requested;
		TargetFloor = INDEX_NONE;
		NextThink = ELYSIUM_NEVER_THINK;
	}

	void InputCallCurrentFloorOutputs(const FElysiumEntityHandle& Activator)
	{
		if (CurrentFloor != INDEX_NONE)
		{
			FireReachOutputs(CurrentFloor, Activator);
		}
	}

	virtual void Think() override
	{
		if (bRestoreSeatPending)
		{
			bRestoreSeatPending = false;
			if (Def && Def->ElevatorFloors.IsValidIndex(CurrentFloor))
			{
				SnapBody(FVector(Origin.X, Origin.Y, Def->ElevatorFloors[CurrentFloor]),
					FRotator::ZeroRotator);
			}
			NextThink = ELYSIUM_NEVER_THINK;
			return;
		}
		if (!IsMoving())
		{
			return;
		}
		TickMove(World ? World->NowSeconds() : 0.0);
		if (IsMoving())
		{
			FirePassedFloors();
		}
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		int32 SavedFloor = CurrentFloor == INDEX_NONE ? TargetFloor : CurrentFloor;
		Ar << SavedFloor;
		Ar << bLocked;
		if (Ar.IsLoading())
		{
			CurrentFloor = FMath::Clamp(SavedFloor, 0, 7);
			TargetFloor = INDEX_NONE;
			bRestoreSeatPending = true;
			NextThink = 0.0f;
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Current floor"), CurrentFloor == INDEX_NONE
			? TEXT("(moving)") : FString::FromInt(CurrentFloor + 1));
		Out.Emplace(TEXT("Target floor"), TargetFloor == INDEX_NONE
			? TEXT("(none)") : FString::FromInt(TargetFloor + 1));
		Out.Emplace(TEXT("Locked"), bLocked ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Speed"), FString::Printf(TEXT("%.2f in/s"), Speed));
	}

protected:
	virtual void MoveDone() override
	{
		FirePassedFloors();
		const int32 Arrived = TargetFloor;
		TargetFloor = INDEX_NONE;
		PlayMoverSoundRel(StopSound);
		FireReachOutputs(Arrived, LastActivator);
	}

private:
	int32 CurrentFloor = 0;
	int32 TargetFloor = INDEX_NONE;
	float MoveStartZ = 0.0f;
	FElysiumEntityHandle LastActivator;
	TArray<bool> Passed;
	bool bRestoreSeatPending = false;

	void FireReachOutputs(int32 Floor, const FElysiumEntityHandle& Activator)
	{
		if (Floor < 0 || Floor >= 8)
		{
			return;
		}
		static const FName OnReachFloorAny(TEXT("OnReachFloorAny"));
		FireOutput(OnReachFloorAny, Activator);
		FireOutput(FName(*FString::Printf(TEXT("OnReachFloor%d"), Floor + 1)), Activator);
		CurrentFloor = Floor;   // retail writes m_nCurrFloor after firing both outputs
	}

	void FirePassedFloors()
	{
		if (!Body || !Def || TargetFloor == INDEX_NONE)
		{
			return;
		}
		const float Z = Body->GetRelativeLocation().Z;
		const float TargetZ = Def->ElevatorFloors[TargetFloor];
		// Retail only owns OnPassFloor2..OnPassFloor7 (indices 1..6).
		for (int32 Floor = 1; Floor < 7 && Def->ElevatorFloors.IsValidIndex(Floor); ++Floor)
		{
			if (Floor == TargetFloor || Passed[Floor])
			{
				continue;
			}
			const float FloorZ = Def->ElevatorFloors[Floor];
			const bool bCrossed = MoveStartZ <= TargetZ
				? (MoveStartZ < FloorZ && Z >= FloorZ)
				: (MoveStartZ > FloorZ && Z <= FloorZ);
			if (bCrossed)
			{
				Passed[Floor] = true;
				static const FName OnPassFloorAny(TEXT("OnPassFloorAny"));
				FireOutput(OnPassFloorAny, LastActivator);
				FireOutput(FName(*FString::Printf(TEXT("OnPassFloor%d"), Floor + 1)),
					LastActivator);
			}
		}
	}
};

// ============================================================================================
// func_button — the pressable switch (CBaseButton). 162 uses / 6 on the tutorial.
// ============================================================================================
//
// Reference: animation_and_movers.md B.4 (the press → TriggerAndWait → return/latch cycle) + B.5,
// reconciled against the decompiled CBaseButton::Spawn (vampire.dll FUN_100c8d60). The spawnflag
// bits below are per-bit confirmed from that decompile — notably 0x100=TOUCH / 0x400=USE (VtMB
// matches stock Source, NOT swapped: the 0x400-armed handler FUN_100c9250 gates on
// CBaseEntity::PassesUseFilter, i.e. it is the +use path; all six tutorial buttons carry 0x400 +
// use_icon). It is a CBaseToggle mover (press-in via the base LinearMove); the physical slide is
// best-effort and untested because every exported func_button is DONTMOVE (the logical path).
class FElysiumButton final : public FElysiumMoverBase
{
public:
	// Button spawnflags (B.5 — decompiled, per-bit confirmed in FUN_100c8d60).
	static constexpr int32 SF_DONTMOVE = 0x1;      // pressed pose == rest pose (no physical slide)
	static constexpr int32 SF_TOGGLE   = 0x20;     // stays pressed until re-used (toggle up<->down)
	static constexpr int32 SF_TOUCH    = 0x100;    // touch-activates (m_pfnTouch handler)
	static constexpr int32 SF_DAMAGE   = 0x200;    // shootable (damage-activates; FUN_100c8b80)
	static constexpr int32 SF_USE      = 0x400;    // +use-activates (m_pfnUse; gated by PassesUseFilter)
	static constexpr int32 SF_LOCKED   = 0x800;    // starts locked (byte +0x5c4)
	static constexpr int32 SF_USEGATE  = 0x1000;   // secondary use-gate: activator must carry a flag (+0x5c5)

	// Keyfields (B.2). Speed is the press-in speed in Source in/s; wait is the pressed hold before
	// spring-back (-1 = latch open); lip trims the press-in travel.
	float Speed = 40.0f;
	float Wait  = 3.0f;
	float Lip   = 4.0f;
	bool  bLocked = false;

	// Explicit press sounds (B.2): `unlocked_sound` = the press-success WAV, `locked_sound` = the
	// press-denied WAV (direct sound-relative paths, e.g. environmental/electronic/button_beep.wav).
	// These take priority over the soundgroup on/off when set (a keypad ships explicit beeps, a switch
	// a soundgroup). Sound-relative, `\`→`/`.
	FString UnlockedSoundRel;
	FString LockedSoundRel;

	// The activator that last pressed the button — propagated onto OnPressed (Source m_hActivator).
	FElysiumEntityHandle LastActivator;

	enum class EState : uint8 { Rest, Pressing, Pressed, Returning };
	EState State() const { return ButtonState; }

	// --- Inputs (registered on func_button) --------------------------------------------
	void InputLock()   { bLocked = true; }
	void InputUnlock() { bLocked = false; }
	// A scripted press (ent_fire / a wire), bypassing the +use look-cursor's SF_USE gate — the way
	// the button is exercised before P4.4's full +use HUD, mirroring how doors expose Use.
	void InputPress(const FElysiumEntityHandle& Activator) { ActivateButton(Activator); }

	// --- +use look-cursor terminus (P4.2) ----------------------------------------------
	// Only USE-armed buttons (0x400) show the reticle / take a +use; touch-only buttons do not.
	virtual bool IsUsable() const override { return (SpawnFlags & SF_USE) != 0; }
	virtual bool IsUseLocked() const override { return bLocked; }   // locked_icon on the reticle (P4.4)
	virtual void OnUseCursorEnter() override
	{
		static const FName OnIn(TEXT("OnIn"));
		FireOutput(OnIn, FElysiumEntityHandle::Invalid());   // arms the reticle (player activator)
	}
	virtual void OnUseCursorLeave() override
	{
		static const FName OnOut(TEXT("OnOut"));
		FireOutput(OnOut, FElysiumEntityHandle::Invalid());
	}
	virtual void Use(const FElysiumEntityHandle& Activator) override { ActivateButton(Activator); }

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		static const TCHAR* StateNames[] = { TEXT("Rest"), TEXT("Pressing"), TEXT("Pressed"), TEXT("Returning") };
		Out.Emplace(TEXT("Button state"), StateNames[(uint8)ButtonState]);
		Out.Emplace(TEXT("Locked"), bLocked ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Latching"), (SpawnFlags & SF_TOGGLE) || Wait < 0.0f ? TEXT("yes (toggle / wait -1)") : TEXT("no (spring-back)"));
		TArray<FString> FlagNames;
		auto Add = [&](int32 Bit, const TCHAR* Name) { if (SpawnFlags & Bit) { FlagNames.Add(Name); } };
		Add(SF_DONTMOVE, TEXT("DONTMOVE")); Add(SF_TOGGLE, TEXT("TOGGLE")); Add(SF_TOUCH, TEXT("TOUCH"));
		Add(SF_DAMAGE, TEXT("DAMAGE")); Add(SF_USE, TEXT("USE")); Add(SF_LOCKED, TEXT("LOCKED"));
		Add(SF_USEGATE, TEXT("USEGATE"));
		Out.Emplace(TEXT("Spawnflags"), FString::Printf(TEXT("%d = %s"), SpawnFlags,
			FlagNames.Num() ? *FString::Join(FlagNames, TEXT(" | ")) : TEXT("(none)")));
		AppendSoundDebug(Out);
		if (!UnlockedSoundRel.IsEmpty()) { Out.Emplace(TEXT("Unlocked sound"), UnlockedSoundRel); }
		if (!LockedSoundRel.IsEmpty())   { Out.Emplace(TEXT("Locked sound"), LockedSoundRel); }
	}

	virtual void Spawn() override
	{
		bLocked = (SpawnFlags & SF_LOCKED) != 0;
		ButtonState = EState::Rest;
		// Mover sounds (P6.4): soundgroup on/off from usable/switches/<soundgroup>/ (RE: CBaseButton::
		// Spawn FUN_100c8810 reads "on"/"off"), plus the explicit locked/unlocked press WAVs. Buttons
		// have no SILENT spawnflag (0x1000 is USEGATE here), so pass 0.
		InitMoverSounds(TEXT("switches"), 0);
		auto Key = [this](const TCHAR* K) { return Def ? Def->Keys.FindRef(K).Replace(TEXT("\\"), TEXT("/")) : FString(); };
		UnlockedSoundRel = Key(TEXT("unlocked_sound"));
		LockedSoundRel   = Key(TEXT("locked_sound"));
		// Rest/pressed poses are captured lazily on the first press: BuildBrushBody runs after Spawn(),
		// so the body (and its bounds, needed for the press-in travel) does not exist yet here.
	}

	virtual void Think() override
	{
		const double Now = World ? World->NowSeconds() : 0.0;
		if (IsMoving())
		{
			TickMove(Now);
			return;
		}
		// The only resting think a button schedules is the pressed-state autoclose.
		if (ButtonState == EState::Pressed)
		{
			ButtonReturn();
		}
	}

protected:
	virtual void MoveDone() override
	{
		if (ButtonState == EState::Pressing)
		{
			ButtonState = EState::Pressed;
			TriggerAndWait();
		}
		else if (ButtonState == EState::Returning)
		{
			ButtonBackHome();
		}
	}

private:
	// The CBaseButton activation gate + direction pick (B.4). Locked/inert swallow it; otherwise a
	// rest button presses in, and a TOGGLE button that is already pressed springs back (re-use).
	void ActivateButton(const FElysiumEntityHandle& Activator)
	{
		if (IsInert())
		{
			return;   // hidden = disarmed
		}
		if (bLocked)
		{
			// Locked press: the deny sound (explicit `locked_sound`), no state change (P4.4 also shows
			// the locked_icon on the reticle). Matches the door's locked path (OnLockedUse + locked sfx).
			PlayMoverSoundRel(LockedSoundRel);
			return;
		}
		switch (ButtonState)
		{
		case EState::Rest:
			ButtonActivate(Activator);
			break;
		case EState::Pressed:
			if (SpawnFlags & SF_TOGGLE)
			{
				ButtonReturn();   // toggle release
			}
			break;
		case EState::Pressing:
		case EState::Returning:
			break;                // mid-motion re-use is ignored (no NO_AUTO_RETURN equivalent here)
		}
	}

	void ButtonActivate(const FElysiumEntityHandle& Activator)
	{
		LastActivator = Activator;
		EnsurePositions();
		// Press sound: the explicit `unlocked_sound` (a keypad beep) if set, else the soundgroup `on`
		// (RE: CBaseButton plays the "on" index DAT_106e6f78 on press). The spring-back plays `off`.
		static const FName On(TEXT("on"));
		if (!UnlockedSoundRel.IsEmpty()) { PlayMoverSoundRel(UnlockedSoundRel); }
		else                            { PlayMoverSound(On); }
		if ((SpawnFlags & SF_DONTMOVE) || !Body)
		{
			ButtonState = EState::Pressed;
			TriggerAndWait();   // no physical slide — fire immediately (the tutorial path)
		}
		else
		{
			ButtonState = EState::Pressing;
			LinearMove(PressedLoc, Speed * MoverInchToCm);   // MoveDone -> TriggerAndWait
		}
	}

	// At the pressed pose: fire OnPressed, then either latch (TOGGLE or wait -1) or schedule the
	// spring-back after `wait` seconds on the substrate clock (R4, no engine timer).
	void TriggerAndWait()
	{
		static const FName OnPressed(TEXT("OnPressed"));
		FireOutput(OnPressed, LastActivator);

		if ((SpawnFlags & SF_TOGGLE) || Wait < 0.0f)
		{
			NextThink = ELYSIUM_NEVER_THINK;   // stays pressed (toggle: until re-used; wait -1: latched)
		}
		else
		{
			NextThink = (World ? World->NowSeconds() : 0.0) + Wait;
		}
	}

	void ButtonReturn()
	{
		static const FName Off(TEXT("off"));
		PlayMoverSound(Off);   // spring-back / toggle-release sound (RE: "off" index DAT_106e6f7c)
		if ((SpawnFlags & SF_DONTMOVE) || !Body)
		{
			ButtonState = EState::Returning;
			ButtonBackHome();
		}
		else
		{
			ButtonState = EState::Returning;
			LinearMove(RestLoc, Speed * MoverInchToCm);   // MoveDone -> ButtonBackHome
		}
	}

	void ButtonBackHome()
	{
		ButtonState = EState::Rest;
		NextThink = ELYSIUM_NEVER_THINK;   // re-armed; waits for the next press
	}

	// Capture the rest pose and derive the pressed pose once the body exists. DONTMOVE (every
	// exported button) collapses the two. The moving path is best-effort and UNTESTED — no exported
	// func_button clears DONTMOVE — so the movedir/travel derivation lands but is unexercised.
	void EnsurePositions()
	{
		if (bPositionsCached || !Body)
		{
			return;
		}
		bPositionsCached = true;
		RestLoc = Body->GetRelativeLocation();
		if (SpawnFlags & SF_DONTMOVE)
		{
			PressedLoc = RestLoc;
			return;
		}
		// Source SetMovedir: angles (0,-1,0) = up, (0,-2,0) = down, else the forward of `angles`.
		FVector Dir;
		if (Angles.Equals(FVector(0.f, -1.f, 0.f)))      { Dir = FVector(0.f, 0.f, 1.f); }
		else if (Angles.Equals(FVector(0.f, -2.f, 0.f))) { Dir = FVector(0.f, 0.f, -1.f); }
		else { Dir = FRotator(Angles.X, Angles.Y, Angles.Z).Vector(); }
		// Travel = the body's depth along movedir minus the lip (both in cm). Bounds are world-axis;
		// good enough for the axis-aligned press this approximates until content exercises it.
		const FVector Ext = Body->Bounds.BoxExtent;
		const double Depth = 2.0 * FMath::Abs(FVector::DotProduct(Ext, Dir.GetAbs()));
		const double Travel = FMath::Max(0.0, Depth - Lip * MoverInchToCm);
		PressedLoc = RestLoc + Dir * Travel;
	}

	EState  ButtonState = EState::Rest;
	bool    bPositionsCached = false;
	FVector RestLoc = FVector::ZeroVector;
	FVector PressedLoc = FVector::ZeroVector;
};

// ============================================================================================
// func_rotating — VtMB CFuncRotating (vampire.dll CFuncRotating::Spawn FUN_100bfaa0)
// ============================================================================================
//
// The continuous spinner (fans, gears, clock hands, the sky's cloud/lightning rotators). Not a
// FElysiumMoverBase leaf: that base drives finite constant-velocity moves to a target transform,
// while this turns forever about one axis. It drives the same brush Body, so the adopted baked mesh
// (UElysiumBrushComponent::SetVisual) and every `parentname` child ride the rotation.

namespace
{
	// func_rotating spawnflag bits (B.5 / animation_and_movers.md, read off CFuncRotating::Spawn).
	// The two axis bits carry Source's own misleading names: the flag Hammer labels "Z axis" spins
	// the brush about X, and the one labelled "X axis" spins it about Y. Spawn stores each as a
	// QAngle *component selector* (pitch, yaw, roll) in m_vecMoveAng, not as a direction — so the
	// Z_AXIS flag's Vector(0,0,1) picks roll (a turn about X), and the unflagged default's
	// Vector(0,1,0) picks yaw (a turn about Z). The names below are the flags'; SourceSpinAxis
	// resolves what each one actually turns.
	constexpr int32 SF_ROT_START_ON  = 0x1;
	constexpr int32 SF_ROT_REVERSE   = 0x2;
	constexpr int32 SF_ROT_Z_AXIS    = 0x4;
	constexpr int32 SF_ROT_X_AXIS    = 0x8;
	constexpr int32 SF_ROT_ACCDEC    = 0x10;   // inferred; fanfriction ramps are not implemented
	constexpr int32 SF_ROT_HURT      = 0x20;   // crush touch; `dmg` is 0 on every exported instance
	constexpr int32 SF_ROT_NOT_SOLID = 0x40;   // solidity is applied at body build, not here

	// The spin axis, as a raw-Source direction, after resolving the QAngle component each flag
	// selects. Every exported instance agrees: the ceiling fans, the junkyard fan, the la_hub blade
	// and the sky's cloud/lightning rotators are all unflagged and turn about Z, while the wall
	// clocks' three hands carry Z_AXIS and sweep about the wall normal, X.
	FVector SourceSpinAxis(int32 SpawnFlags)
	{
		if (SpawnFlags & SF_ROT_Z_AXIS) { return FVector(1.f, 0.f, 0.f); }   // roll
		if (SpawnFlags & SF_ROT_X_AXIS) { return FVector(0.f, 1.f, 0.f); }   // pitch
		return FVector(0.f, 0.f, 1.f);                                       // yaw — the default
	}

	FString DescribeRotatingSpawnFlags(int32 SF)
	{
		if (SF == 0)
		{
			return TEXT("(none)");
		}
		TArray<FString> On;
		auto Add = [&](int32 Bit, const TCHAR* Name) { if (SF & Bit) { On.Add(Name); } };
		Add(SF_ROT_START_ON, TEXT("START_ON"));
		Add(SF_ROT_REVERSE, TEXT("REVERSE"));
		Add(SF_ROT_Z_AXIS, TEXT("Z_AXIS"));
		Add(SF_ROT_X_AXIS, TEXT("X_AXIS"));
		Add(SF_ROT_ACCDEC, TEXT("ACCDEC"));
		Add(SF_ROT_HURT, TEXT("HURT"));
		Add(SF_ROT_NOT_SOLID, TEXT("NOT_SOLID"));
		// The remaining 0x80/0x100/0x200 are the small/medium/large sound radii.
		if (SF & 0x80)  { On.Add(TEXT("SND_SMALL")); }
		if (SF & 0x100) { On.Add(TEXT("SND_MEDIUM")); }
		if (SF & 0x200) { On.Add(TEXT("SND_LARGE")); }
		return FString::Join(On, TEXT(" | "));
	}
}

class FElysiumFuncRotating final : public FElysiumEntity
{
public:
	float MaxSpeed = 0.0f;      // `maxspeed`, degrees/second (the authored rate)
	float FanFriction = 0.0f;   // `fanfriction`, spin-down percent — stored, no ramp implemented
	float Volume = 0.0f;        // `volume`  — the running-sound loudness (no rotating sound yet)
	float Dmg = 0.0f;           // `dmg`     — crush damage for the HURT flag (0 on every instance)
	int32 Sounds = 0;           // `sounds`  — the running-sound index (0 on every instance)

	virtual void Spawn() override
	{
		Speed = MaxSpeed;
		Direction = (SpawnFlags & SF_ROT_REVERSE) ? -1.0f : 1.0f;
		AngleDeg = 0.0f;
		bRunning = (SpawnFlags & SF_ROT_START_ON) != 0;
		DeferRebase();   // Spawn runs before the world is activated: there is no clock to read yet
	}

	void InputStart()
	{
		if (!bRunning)
		{
			bRunning = true;
			Rebase();
		}
	}

	void InputStop()
	{
		if (bRunning)
		{
			AngleDeg = AngleAt(World ? World->NowSeconds() : RunStartTime);
			bRunning = false;
			NextThink = ELYSIUM_NEVER_THINK;
		}
	}

	void InputReverse()
	{
		Settle();
		Direction = -Direction;
		Rebase();
	}

	void InputSetSpeed(const FElysiumVariant& Value)
	{
		Settle();
		Speed = FMath::Max(0.0f, Value.ToFloat());
		Rebase();
	}

	virtual void Think() override
	{
		if (!bRunning)
		{
			NextThink = ELYSIUM_NEVER_THINK;
			return;
		}
		const double Now = World ? World->NowSeconds() : 0.0;
		if (RunStartTime < 0.0)
		{
			RunStartTime = Now;   // first think after Spawn or a save load: start the clock here
		}
		ApplyAngle(AngleAt(Now));
		NextThink = static_cast<float>(Now);   // keep turning every frame
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		float Frozen = bRunning ? AngleAt(World ? World->NowSeconds() : RunStartTime) : AngleDeg;
		Ar << Frozen << bRunning << Speed << Direction;
		if (Ar.IsLoading())
		{
			AngleDeg = Frozen;
			DeferRebase();   // a load lands before Activate too; re-seat on the next think
			NextThink = 0.0f;
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		const FVector Axis = SourceSpinAxis(SpawnFlags);
		Out.Emplace(TEXT("Running"), bRunning ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Speed"), FString::Printf(TEXT("%g deg/s %s"),
			Speed, Direction < 0.0f ? TEXT("(reversed)") : TEXT("")));
		Out.Emplace(TEXT("Angle"), FString::Printf(TEXT("%.2f deg"),
			bRunning ? AngleAt(World ? World->NowSeconds() : RunStartTime) : AngleDeg));
		Out.Emplace(TEXT("Source axis"), Axis.ToString());
		Out.Emplace(TEXT("Spawnflags"), FString::Printf(TEXT("%d = %s"),
			SpawnFlags, *DescribeRotatingSpawnFlags(SpawnFlags)));
	}

	// The angle the body would be at, for a test or a debug read, without moving anything.
	float CurrentAngleDeg() const
	{
		return bRunning ? AngleAt(World ? World->NowSeconds() : RunStartTime) : AngleDeg;
	}

private:
	// Angle from elapsed time rather than accumulated per-frame deltas: the hour hand turns at
	// 0.0083 deg/s for twelve hours, where float accumulation would visibly drift.
	float AngleAt(double Now) const
	{
		if (!bRunning || RunStartTime < 0.0)
		{
			return AngleDeg;
		}
		const double Turned = Speed * Direction * (Now - RunStartTime);
		const double Wrapped = FMath::Fmod(AngleDeg + Turned, 360.0);
		return static_cast<float>(Wrapped < 0.0 ? Wrapped + 360.0 : Wrapped);   // Fmod keeps the sign
	}

	// Freeze the angle reached so far so a speed/direction change starts from where the body is.
	void Settle()
	{
		AngleDeg = AngleAt(World ? World->NowSeconds() : RunStartTime);
	}

	// An input arrives with a live clock, so the new rate takes effect at the moment it lands
	// rather than at whatever time the next think happens to run.
	void Rebase()
	{
		RunStartTime = World ? World->NowSeconds() : 0.0;
		Arm();
	}

	// Spawn and a save load both run before Activate, where the clock means nothing yet: -1 defers
	// the reference time to the first think.
	void DeferRebase()
	{
		RunStartTime = -1.0;
		Arm();
	}

	void Arm()
	{
		if (bRunning)
		{
			NextThink = 0.0f;
		}
	}

	void ApplyAngle(float NewAngleDeg)
	{
		if (!Body)
		{
			return;   // a bodiless rotator turns nothing; keep the angle for when a body exists
		}
		if (!bSpawnRotCached)
		{
			bSpawnRotCached = true;
			SpawnRot = Body->GetRelativeRotation().Quaternion();
		}
		// One uniform rule, the same reflection every other coordinate read uses: the Source spin
		// axis maps to Unreal by negating Y, and the reflection reverses the turn, so the angle
		// negates with it. Pre-multiplied — the axis is a world axis, not a body-local one.
		const FVector Src = SourceSpinAxis(SpawnFlags);
		const FVector UnrealAxis(Src.X, -Src.Y, Src.Z);
		const FQuat Delta(UnrealAxis, FMath::DegreesToRadians(-NewAngleDeg));
		Body->SetRelativeRotation(Delta * SpawnRot);
	}

	float  Speed = 0.0f;         // the live rate; SetSpeed writes it, Spawn seeds it from maxspeed
	float  Direction = 1.0f;     // +1, or -1 from REVERSE / the Reverse input
	float  AngleDeg = 0.0f;      // the angle at RunStartTime (the whole angle while stopped)
	double RunStartTime = -1.0;  // < 0 = rebase on the next think
	bool   bRunning = false;
	bool   bSpawnRotCached = false;
	FQuat  SpawnRot = FQuat::Identity;
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
	// use on an unlocked door toggles it (and its linked partner); on a locked door it fires
	// OnLockedUse. Wired here so `ent_fire <door> Use` exercises the same path.
	D.Input(TEXT("Use"),    [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumDoorBase&>(E).DoorUse(A.Activator); });

	// Keyfields (B.2).
	AddDoorSubclassField(D, TEXT("speed"),    &FElysiumDoorBase::Speed);
	AddDoorSubclassField(D, TEXT("distance"), &FElysiumDoorBase::Distance);
	AddDoorSubclassField(D, TEXT("wait"),     &FElysiumDoorBase::Wait);
	AddDoorSubclassField(D, TEXT("lip"),      &FElysiumDoorBase::Lip);
	AddDoorSubclassField(D, TEXT("dmg"),      &FElysiumDoorBase::Dmg);
	AddDoorSubclassField(D, TEXT("linked_door"), &FElysiumDoorBase::LinkedDoorName);
	AddDoorSubclassField(D, TEXT("use_override"), &FElysiumDoorBase::UseOverrideName);
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

static TUniquePtr<FElysiumEntity> MakeElevator() { return MakeUnique<FElysiumElevator>(); }

static FElysiumClassRegistrar GRegFuncElevator(
	TEXT("func_elevator"), ElysiumBaseClassName(), &MakeElevator,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("GotoFloor"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumElevator&>(E).InputGotoFloor(A.Param.ToInt(), A.Activator); });
		D.Input(TEXT("SnapToFloor"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumElevator&>(E).InputSnapToFloor(A.Param.ToInt()); });
		D.Input(TEXT("Lock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ static_cast<FElysiumElevator&>(E).InputLock(); });
		D.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ static_cast<FElysiumElevator&>(E).InputUnlock(); });
		D.Input(TEXT("CallCurrentFloorOutputs"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumElevator&>(E).InputCallCurrentFloorOutputs(A.Activator); });
		AddDoorSubclassField(D, TEXT("speed"), &FElysiumElevator::Speed);
		AddDoorSubclassField(D, TEXT("numfloors"), &FElysiumElevator::NumFloors);
		AddDoorSubclassField(D, TEXT("locked"), &FElysiumElevator::bLocked);
		AddDoorSubclassField(D, TEXT("startsound"), &FElysiumElevator::StartSound);
		AddDoorSubclassField(D, TEXT("stopsound"), &FElysiumElevator::StopSound);
	});

static TUniquePtr<FElysiumEntity> MakeFuncButton() { return MakeUnique<FElysiumButton>(); }

// func_button is concrete (unlike CBaseDoor). Registered directly on the base chain; when
// func_rot_button lands it derives from FElysiumButton the same way the door leaves derive from
// CBaseDoor. Press/Lock/Unlock are the stock CBaseButton inputs; the +use path is driven by the
// look-cursor (IsUsable/Use), so no Use input is registered here.
static FElysiumClassRegistrar GRegFuncButton(
	TEXT("func_button"), ElysiumBaseClassName(), &MakeFuncButton,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Press"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumButton&>(E).InputPress(A.Activator); });
		D.Input(TEXT("Lock"),   [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumButton&>(E).InputLock(); });
		D.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumButton&>(E).InputUnlock(); });
		AddDoorSubclassField(D, TEXT("speed"), &FElysiumButton::Speed);
		AddDoorSubclassField(D, TEXT("wait"),  &FElysiumButton::Wait);
		AddDoorSubclassField(D, TEXT("lip"),   &FElysiumButton::Lip);
	});

static TUniquePtr<FElysiumEntity> MakeFuncRotating() { return MakeUnique<FElysiumFuncRotating>(); }

// func_rotating sits directly on the base chain — it shares no state with the CBaseDoor lineage.
// Inputs are the B.3 surface; ScriptHide/ScriptUnhide/Kill come from the base.
static FElysiumClassRegistrar GRegFuncRotating(
	TEXT("func_rotating"), ElysiumBaseClassName(), &MakeFuncRotating,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Start"),    [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumFuncRotating&>(E).InputStart(); });
		D.Input(TEXT("Stop"),     [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumFuncRotating&>(E).InputStop(); });
		D.Input(TEXT("Reverse"),  [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumFuncRotating&>(E).InputReverse(); });
		D.Input(TEXT("SetSpeed"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumFuncRotating&>(E).InputSetSpeed(A.Param); });
		AddDoorSubclassField(D, TEXT("maxspeed"),    &FElysiumFuncRotating::MaxSpeed);
		AddDoorSubclassField(D, TEXT("fanfriction"), &FElysiumFuncRotating::FanFriction);
		AddDoorSubclassField(D, TEXT("volume"),      &FElysiumFuncRotating::Volume);
		AddDoorSubclassField(D, TEXT("dmg"),         &FElysiumFuncRotating::Dmg);
		AddDoorSubclassField(D, TEXT("sounds"),      &FElysiumFuncRotating::Sounds);
	});
