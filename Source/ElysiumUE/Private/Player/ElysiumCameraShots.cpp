#include "Player/ElysiumCameraShots.h"

#include "ElysiumCameraComponent.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"

#include "Components/SkeletalMeshComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCamShots, Log, All);

// The module builds with unity on, so the helpers here take a named namespace (the project's
// convention -- see `ElysiumMcpTools.cpp`).
namespace ElysiumCameraShotsImpl
{
	using ElysiumKeyValues::FKvNode;

	// `"[40, -10, 25]"` -> a vector. Brackets and separators are both optional in the corpus, so the
	// parse is "take the numbers in order"; a value that yields fewer than three is left at the
	// caller's default.
	bool ParseBracketVector(const FString& Raw, FVector& Out)
	{
		FString S = Raw;
		S.ReplaceInline(TEXT("["), TEXT(" "));
		S.ReplaceInline(TEXT("]"), TEXT(" "));
		S.ReplaceInline(TEXT(","), TEXT(" "));
		TArray<FString> Parts;
		S.ParseIntoArrayWS(Parts);
		if (Parts.Num() < 3)
		{
			return false;
		}
		Out = FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
		return true;
	}

	EElysiumShotPosition ParsePosition(const FString& Raw, FString& OutNamed)
	{
		const FString V = Raw.TrimStartAndEnd();
		if (V.Equals(TEXT("Player"), ESearchCase::IgnoreCase))        { return EElysiumShotPosition::Player; }
		if (V.Equals(TEXT("DialogTarget"), ESearchCase::IgnoreCase))  { return EElysiumShotPosition::DialogTarget; }
		if (V.Equals(TEXT("GrappleTarget"), ESearchCase::IgnoreCase)) { return EElysiumShotPosition::GrappleTarget; }
		if (V.Equals(TEXT("World"), ESearchCase::IgnoreCase))         { return EElysiumShotPosition::World; }
		if (V.Equals(TEXT("Named"), ESearchCase::IgnoreCase))         { return EElysiumShotPosition::Named; }
		// The how-to lists `Named` as the keyword *and* as "the name of an entity in the map", and the
		// corpus writes both, so anything unrecognised is taken as the name itself.
		OutNamed = V;
		return V.IsEmpty() ? EElysiumShotPosition::None : EElysiumShotPosition::Named;
	}

	EElysiumShotAttach ParseAttach(const FString& Raw)
	{
		const FString V = Raw.TrimStartAndEnd();
		if (V.Equals(TEXT("Follow"), ESearchCase::IgnoreCase))          { return EElysiumShotAttach::Follow; }
		if (V.Equals(TEXT("FollowNoAngles"), ESearchCase::IgnoreCase))  { return EElysiumShotAttach::FollowNoAngles; }
		if (V.Equals(TEXT("FollowEntAngles"), ESearchCase::IgnoreCase)) { return EElysiumShotAttach::FollowEntAngles; }
		return EElysiumShotAttach::None;
	}

	void ParseAnchor(const FKvNode* Node, FElysiumShotAnchor& Out)
	{
		if (!Node)
		{
			return;
		}
		Out.bPresent = true;
		Out.Position = ParsePosition(Node->Str(TEXT("Position"), FString()), Out.NamedEntity);
		Out.AttachPos = Node->Str(TEXT("AttachPos"), TEXT("Origin")).TrimStartAndEnd();
		Out.Attach = ParseAttach(Node->Str(TEXT("AttachType"), FString()));

		FVector Offset;
		// The corpus writes `OffsetOrigin`; the how-to's `End` block also lists a bare `Offset`.
		if (ParseBracketVector(Node->Str(TEXT("OffsetOrigin"), FString()), Offset) ||
			ParseBracketVector(Node->Str(TEXT("Offset"), FString()), Offset))
		{
			Out.OffsetOrigin = Offset * ElysiumCam::U;
		}
	}

	void ParseConstraints(const FKvNode* Node, FElysiumShotConstraints& Out)
	{
		if (!Node)
		{
			return;
		}
		// **The absent-key defaults are retail's, not zero.** `FUN_100721e0` seeds every field before
		// it reads the block, and the whole-block-absent path (`0x10072300`) seeds the identical set:
		// MoveSpeed 150 u/s, MoveAccel 50 u/s^2, TurnAccel 30 deg/s^2, MaxTurnRate [90,90,90] deg/s,
		// DistanceTolerance 10 u, AngularTolerance [1,1,1] deg, FieldOfView 75 clamped to [20,120],
		// all flags clear. A file that authors only `FieldOfView` still gets a rate-limited,
		// deadbanded camera, which is why so few shipped shots bother to write the rates.
		Out.MoveSpeed = Node->Flt(TEXT("MoveSpeed"), 150.0f) * ElysiumCam::U;
		Out.MoveAccel = Node->Flt(TEXT("MoveAccel"), 50.0f) * ElysiumCam::U;
		Out.TurnAccel = Node->Flt(TEXT("TurnAccel"), 30.0f);
		Out.DistanceTolerance = Node->Flt(TEXT("DistanceTolerance"), 10.0f) * ElysiumCam::U;
		Out.FieldOfView = FMath::Clamp(Node->Flt(TEXT("FieldOfView"), 75.0f), 20.0f, 120.0f);
		Out.bDialogPOV = Node->Bool(TEXT("DialogPOV"), false);
		Out.bAutoPositionFromTarget = Node->Bool(TEXT("AutoPositionFromTarget"), false);
		Out.bSyncRotateOnMove = Node->Bool(TEXT("SyncRotateOnMove"), false);
		Out.bSnapOnShotChange = Node->Bool(TEXT("SnapOnShotChange"), false);
		Out.bShowHud = Node->Bool(TEXT("ShowHud"), false);
		Out.bDrawViewmodel = Node->Bool(TEXT("DrawViewmodel"), false);

		FVector V;
		if (ParseBracketVector(Node->Str(TEXT("MaxTurnRate"), FString()), V))      { Out.MaxTurnRate = V; }
		if (ParseBracketVector(Node->Str(TEXT("AngularTolerance"), FString()), V)) { Out.AngularTolerance = V; }
	}

	// The parsed table, keyed by the lowercased shot-file name. A conversation re-reads the same file
	// every line, and the whole directory is 66 files of a few hundred bytes.
	//
	// The value is the file's WHOLE authored block list, because `special-case.txt` carries five
	// siblings that retail addresses by name. `Load` answers element 0, which is the one-shot-per-file
	// convention every other caller relies on; a null entry is a remembered miss.
	using FShotList = TArray<FElysiumCameraShotDef>;
	TMap<FString, TSharedPtr<FShotList>>& Cache()
	{
		static TMap<FString, TSharedPtr<FShotList>> Map;
		return Map;
	}
}

using namespace ElysiumCameraShotsImpl;

FString ElysiumCameraShots::NormalizeKey(const FString& ShotFile)
{
	FString Normalized = ShotFile.TrimStartAndEnd();
	Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (Normalized.EndsWith(TEXT("/")))
	{
		Normalized.LeftChopInline(1);
	}
	return FPaths::GetBaseFilename(Normalized).ToLower();
}

bool ElysiumCameraShots::ParseAllText(const FString& Text, TArray<FElysiumCameraShotDef>& Out)
{
	Out.Reset();
	const TSharedPtr<ElysiumKeyValues::FKvNode> Root = ElysiumKeyValues::ParseText(Text);
	if (!Root.IsValid())
	{
		return false;
	}
	// The file's outer block is `CameraShotTable`; tolerate its absence, since the shot block is what
	// carries everything and a hand-edited file may drop the wrapper.
	const ElysiumKeyValues::FKvNode* Table = Root->Child(TEXT("CameraShotTable"));
	const ElysiumKeyValues::FKvNode* Outer = Table ? Table : Root.Get();

	for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Kid : Outer->Kids)
	{
		const ElysiumKeyValues::FKvNode* Shot = Kid.Value.Get();
		if (!Shot || Kid.Key.IsEmpty())
		{
			continue;
		}
		FElysiumCameraShotDef Def;
		Def.Name = Kid.Key;
		ParseAnchor(Shot->Child(TEXT("Start")), Def.Start);
		ParseAnchor(Shot->Child(TEXT("End")), Def.End);
		if (const ElysiumKeyValues::FKvNode* Target = Shot->Child(TEXT("Target")))
		{
			ParseAnchor(Target->Child(TEXT("Point1")), Def.Target1);
			ParseAnchor(Target->Child(TEXT("Point2")), Def.Target2);
		}
		ParseConstraints(Shot->Child(TEXT("CameraConstraints")), Def.Constraints);
		Out.Add(MoveTemp(Def));
	}
	return Out.Num() > 0;
}

bool ElysiumCameraShots::ParseText(const FString& Text, FElysiumCameraShotDef& Out)
{
	// One file, one shot, named after the file — block 0 of the list above.
	TArray<FElysiumCameraShotDef> All;
	if (!ParseAllText(Text, All))
	{
		return false;
	}
	Out = All[0];
	return true;
}

namespace ElysiumCameraShotsImpl
{
	// The file's whole block list, loaded and cached once. Null is a remembered miss.
	const FShotList* LoadFile(const FString& ShotFile)
	{
		if (ShotFile.IsEmpty())
		{
			return nullptr;
		}
		const FString Key = ElysiumCameraShots::NormalizeKey(ShotFile);
		if (Key.IsEmpty())
		{
			return nullptr;
		}
		if (const TSharedPtr<FShotList>* Hit = Cache().Find(Key))
		{
			return Hit->Get();
		}

		const FString Path =
			FElysiumContentPaths::VdataFile(TEXT("camerashots") / (Key + TEXT(".txt")));
		FString Text;
		TSharedPtr<FShotList> List;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			List = MakeShared<FShotList>();
			if (!ElysiumCameraShots::ParseAllText(Text, *List))
			{
				UE_LOG(LogElysiumCamShots, Warning, TEXT("camera shot '%s' has no shot block"), *Key);
				List.Reset();
			}
		}
		else
		{
			UE_LOG(LogElysiumCamShots, Warning, TEXT("camera shot '%s' not found (%s)"), *Key, *Path);
		}
		Cache().Add(Key, List);
		return List.Get();
	}
}

const FElysiumCameraShotDef* ElysiumCameraShots::Load(const FString& ShotFile)
{
	const FShotList* List = LoadFile(ShotFile);
	return (List && List->Num() > 0) ? &(*List)[0] : nullptr;
}

const FElysiumCameraShotDef* ElysiumCameraShots::LoadNamed(const FString& ShotFile,
	const FString& ShotName)
{
	const FShotList* List = LoadFile(ShotFile);
	if (!List || ShotName.IsEmpty())
	{
		return nullptr;
	}
	// The corpus writes the block names in the authored case (`Hacking`, `Intrusion`); retail's own
	// lookup is case-insensitive, so this is too.
	return List->FindByPredicate([&ShotName](const FElysiumCameraShotDef& Def)
	{
		return Def.Name.Equals(ShotName, ESearchCase::IgnoreCase);
	});
}

void ElysiumCameraShots::FlushCache()
{
	Cache().Reset();
}

void ElysiumCameraShots::Install(const FString& ShotFile, const FElysiumCameraShotDef& Def)
{
	InstallNamed(ShotFile, MakeArrayView(&Def, 1));
}

void ElysiumCameraShots::InstallNamed(const FString& ShotFile,
	TArrayView<const FElysiumCameraShotDef> Defs)
{
	const FString Key = NormalizeKey(ShotFile);
	if (Key.IsEmpty())
	{
		return;
	}
	TSharedRef<FShotList> List = MakeShared<FShotList>();
	List->Append(Defs.GetData(), Defs.Num());
	Cache().Add(Key, List);
}

const TCHAR* ElysiumCameraShots::LexToString(EElysiumShotPosition Position)
{
	switch (Position)
	{
	case EElysiumShotPosition::Player:        return TEXT("Player");
	case EElysiumShotPosition::DialogTarget:  return TEXT("DialogTarget");
	case EElysiumShotPosition::GrappleTarget: return TEXT("GrappleTarget");
	case EElysiumShotPosition::World:         return TEXT("World");
	case EElysiumShotPosition::Named:         return TEXT("Named");
	default:                                  return TEXT("none");
	}
}

const TCHAR* ElysiumCameraShots::LexToString(EElysiumShotAttach Attach)
{
	switch (Attach)
	{
	case EElysiumShotAttach::Follow:          return TEXT("Follow");
	case EElysiumShotAttach::FollowNoAngles:  return TEXT("FollowNoAngles");
	case EElysiumShotAttach::FollowEntAngles: return TEXT("FollowEntAngles");
	default:                                  return TEXT("None");
	}
}

// The director

namespace ElysiumCameraShotsImpl
{
	// The entity an anchor names. Null when it is not there — a shot pointing at a dead NPC is a
	// missing shot, not a crash.
	FElysiumEntity* AnchorEntity(FElysiumEntityWorld* World, const FElysiumShotAnchor& Anchor,
		const FElysiumEntityHandle& Subject)
	{
		if (!World)
		{
			return nullptr;
		}
		switch (Anchor.Position)
		{
		case EElysiumShotPosition::Player:
			return World->FindPlayer();
		case EElysiumShotPosition::DialogTarget:
		case EElysiumShotPosition::GrappleTarget:
			// `SetCamera`'s receiver IS the dialogue target. The grapple system is P13's; until then a
			// grapple shot frames the same subject rather than resolving to nothing.
			return World->Resolve(Subject);
		case EElysiumShotPosition::Named:
			// A bare `Position: Named` with no entity name is the shot asking for **the entity it was
			// pushed about**. Retail's own call binds it: `FUN_10070470("Hacking", NULL, terminal,
			// terminal, NULL)` hands the terminal in as Named slots 1 and 2
			// (`docs/vtmb/computer-terminals.md` §7.3 step 6), and `special-case.txt`'s `Hacking` and
			// `Intrusion` blocks both write the keyword with no name for exactly that reason.
			// `FindByName("")` would answer nothing and lose the shot.
			return Anchor.NamedEntity.IsEmpty()
				? World->Resolve(Subject) : World->FindByName(Anchor.NamedEntity);
		default:
			return nullptr;
		}
	}

	// VtMB's own player eye height, used as the fallback for an entity with no body to measure.
	constexpr float FallbackEyeHeight = 64.0f * ElysiumCam::U;
	constexpr float FallbackCenterHeight = 36.0f * ElysiumCam::U;

	// `AttachPos` -> a point on the entity. `Bone:` and `Attachment:` resolve against the skeletal
	// body's socket table when one is standing; with no body (`elysium.NpcBodies 0`, a bodiless
	// entity) they fall back to the eye, which is what every dialogue shot is aiming at anyway.
	FVector AttachPoint(const FElysiumEntity& Entity, const FString& AttachPos)
	{
		const FVector Origin = Entity.Origin;
		USkeletalMeshComponent* Body = Entity.GetSkeletalBody();

		const auto BoundsPoint = [Body, Origin](float Alpha) -> FVector
		{
			if (!Body)
			{
				return Origin + FVector(0.0f, 0.0f, FallbackCenterHeight * 2.0f * Alpha);
			}
			const FBoxSphereBounds Bounds = Body->Bounds;
			const FVector Min = Bounds.Origin - Bounds.BoxExtent;
			const FVector Max = Bounds.Origin + Bounds.BoxExtent;
			return FVector(Bounds.Origin.X, Bounds.Origin.Y, FMath::Lerp(Min.Z, Max.Z, Alpha));
		};

		if (AttachPos.StartsWith(TEXT("Bone:")) || AttachPos.StartsWith(TEXT("Attachment:")))
		{
			int32 Colon = INDEX_NONE;
			AttachPos.FindChar(TEXT(':'), Colon);
			const FName Socket(*AttachPos.Mid(Colon + 1).TrimStartAndEnd());
			// A placed prop is normally reduced to its static representation and has no skeletal body
			// to hold a socket table, while the model's `$attachment`s still exist on its baked
			// skeletal asset. That route is asked FIRST, so the terminal's screen cone and its camera
			// read the same two vectors (`screen`, `screen_axis`) by construction.
			FVector Attachment;
			if (Entity.GetBodyAttachmentPoint(Socket, Attachment))
			{
				return Attachment;
			}
			if (Body && Body->DoesSocketExist(Socket))
			{
				return Body->GetSocketLocation(Socket);
			}
			return Origin + FVector(0.0f, 0.0f, FallbackEyeHeight);
		}
		if (AttachPos.Equals(TEXT("Center"), ESearchCase::IgnoreCase))       { return BoundsPoint(0.5f); }
		if (AttachPos.Equals(TEXT("Top"), ESearchCase::IgnoreCase))          { return BoundsPoint(1.0f); }
		if (AttachPos.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase))       { return BoundsPoint(0.0f); }
		if (AttachPos.Equals(TEXT("EyePosition"), ESearchCase::IgnoreCase))
		{
			// **A fixed offset from the origin, not a bounds fraction and not a bone.** Retail's
			// anchor step (`CBaseCineCam` `FUN_1006f080`) reaches the eye through
			// `CBaseCombatCharacter::CalcLookData`, which is `GetAbsOrigin() + m_vecViewOffset` — the
			// same value `CBaseEntity::EyePosition()` answers. Measuring the animated bounds instead
			// made an `EyePosition` anchor breathe with the idle, where retail's holds still.
			return Entity.EyePosition();
		}
		return Origin;   // "Origin", and anything unrecognised
	}
}

bool FElysiumCameraDirector::ResolveAnchor(FElysiumEntityWorld* World, const FElysiumShotAnchor& Anchor,
	const FElysiumEntityHandle& Subject, FVector& OutPoint)
{
	if (!Anchor.bPresent)
	{
		return false;
	}
	if (Anchor.Position == EElysiumShotPosition::World)
	{
		OutPoint = Anchor.OffsetOrigin;
		return true;
	}

	const FElysiumEntity* Entity = AnchorEntity(World, Anchor, Subject);
	if (!Entity)
	{
		return false;
	}

	const FVector Base = AttachPoint(*Entity, Anchor.AttachPos);

	// `Follow` and `FollowEntAngles` both rotate the offset by a facing (the how-to distinguishes the
	// attachment's from the entity's; with no attachment transforms of our own the two are the
	// entity's, and `FollowNoAngles` / `None` leave the offset in world axes). Source yaw negates
	// into Unreal.
	const bool bRotateOffset = Anchor.Attach == EElysiumShotAttach::Follow
		|| Anchor.Attach == EElysiumShotAttach::FollowEntAngles;
	OutPoint = bRotateOffset
		? Base + ElysiumSkeletalBasis::FromSourceAngles(Entity->Angles).RotateVector(Anchor.OffsetOrigin)
		: Base + Anchor.OffsetOrigin;
	return true;
}

bool FElysiumCameraDirector::Resolve(FElysiumEntityWorld* World, const FElysiumCameraShotDef& Def,
	const FElysiumEntityHandle& Subject, FElysiumCameraShot& Out)
{
	// "If there is no End position specified, the camera will not move between the points" — so End is
	// where the shot lives and Start is only its entry. Until a shot is *animated* between the two
	// (the theatre's, 12.x), the framing is End, or Start when the file authors only that.
	FVector Origin;
	if (!ResolveAnchor(World, Def.End, Subject, Origin) &&
		!ResolveAnchor(World, Def.Start, Subject, Origin))
	{
		return false;
	}

	FVector Look;
	const bool bHasTarget = ResolveAnchor(World, Def.Target1, Subject, Look);
	FVector Second;
	if (bHasTarget && ResolveAnchor(World, Def.Target2, Subject, Second))
	{
		// "If there are two points, the camera will track a point halfway between them."
		Look = (Look + Second) * 0.5f;
	}

	Out = FElysiumCameraShot();
	Out.DebugName = Def.Name;
	Out.Origin = Origin;
	Out.bUseLookAt = bHasTarget;
	Out.LookAt = Look;
	// The whole `CameraConstraints` block reaches the tracker; a field the port parsed and then never
	// read is a field retail's camera was using. A file shot is `SetShot(name, 1, ...)` — `CamMode`
	// 1, the one mode `C_BaseCineCamera::Update` (`FUN_10001a20`) tracks.
	Out.bTracked = true;
	Out.FieldOfView = Def.Constraints.FieldOfView;
	Out.MoveSpeed = Def.Constraints.MoveSpeed;
	Out.MoveAccel = Def.Constraints.MoveAccel;
	Out.MaxTurnRate = Def.Constraints.MaxTurnRate;
	Out.TurnAccel = Def.Constraints.TurnAccel;
	Out.DistanceTolerance = Def.Constraints.DistanceTolerance;
	Out.AngularTolerance = Def.Constraints.AngularTolerance;
	Out.bSyncRotateOnMove = Def.Constraints.bSyncRotateOnMove;
	Out.bSnapOnShotChange = Def.Constraints.bSnapOnShotChange;
	// `SnapOnShotChange` is the file's own "cut, do not blend" flag.
	Out.BlendSeconds = Def.Constraints.bSnapOnShotChange ? 0.0f : 0.5f;

	// **This is the one place a shot becomes "named".** `ShowHud` and `DrawViewmodel` are keys on a
	// `vdata/camerashots/` file and on nothing else, so only a shot that came through this converter
	// carries them. Every value producer — `camera_track`, a VCD edit, the green room — pushes a bare
	// `FElysiumCameraShot` and therefore cannot take the HUD down.
	Out.Presentation.bNamed = true;
	Out.Presentation.bShowHud = Def.Constraints.bShowHud;
	Out.Presentation.bDrawViewmodel = Def.Constraints.bDrawViewmodel;
	return true;
}

int32 FElysiumCameraDirector::Push(FElysiumEntityWorld* World, UElysiumCameraComponent* Camera,
	const FString& ShotFile, const FElysiumEntityHandle& Subject)
{
	if (!Camera)
	{
		// No body, no camera — a headless logic world runs the conversation without one, which is the
		// same null-service discipline every other seam follows.
		return 0;
	}
	const FElysiumCameraShotDef* Def = ElysiumCameraShots::Load(ShotFile);
	if (!Def || !Def->IsValid())
	{
		return 0;
	}

	FElysiumCameraShot Shot;
	if (!Resolve(World, *Def, Subject, Shot))
	{
		UE_LOG(LogElysiumCamShots, Warning, TEXT("camera shot '%s': nothing it anchors to resolved"),
			*Def->Name);
		return 0;
	}

	FLiveShot& Entry = Live.AddDefaulted_GetRef();
	Entry.Id = NextId++;
	Entry.bValue = false;
	Entry.Def = *Def;
	Entry.Subject = Subject;
	Entry.CameraShotId = Camera->PushShot(Shot);
	return Entry.Id;
}

int32 FElysiumCameraDirector::PushNamed(FElysiumEntityWorld* World, UElysiumCameraComponent* Camera,
	const FString& ShotFile, const FString& ShotName, const FElysiumEntityHandle& Subject,
	EElysiumShotExposure Exposure)
{
	if (!Camera)
	{
		return 0;
	}
	const FElysiumCameraShotDef* Def = ElysiumCameraShots::LoadNamed(ShotFile, ShotName);
	if (!Def || !Def->IsValid())
	{
		UE_LOG(LogElysiumCamShots, Warning, TEXT("camera shot '%s:%s' did not resolve"),
			*ShotFile, *ShotName);
		return 0;
	}

	FElysiumCameraShot Shot;
	if (!Resolve(World, *Def, Subject, Shot))
	{
		UE_LOG(LogElysiumCamShots, Warning,
			TEXT("camera shot '%s:%s': nothing it anchors to resolved"), *ShotFile, *ShotName);
		return 0;
	}

	FLiveShot& Entry = Live.AddDefaulted_GetRef();
	Entry.Id = NextId++;
	Entry.bValue = false;
	Entry.Def = *Def;
	Entry.Subject = Subject;
	Entry.Exposure = Exposure;
	ApplyExposure(Entry, Shot);
	Entry.CameraShotId = Camera->PushShot(Shot);
	return Entry.Id;
}

void FElysiumCameraDirector::ApplyExposure(const FLiveShot& Entry, FElysiumCameraShot& Shot)
{
	// The clamp belongs to the HANDLE, not to the file: nothing in `vdata/camerashots/` authors an
	// exposure key, so the pusher's ask has to be re-stamped every time `Tick` re-resolves the shot
	// or the resolve would silently drop it mid-session.
	Shot.Presentation.bClampExposure = Entry.Exposure == EElysiumShotExposure::Clamped;
}

int32 FElysiumCameraDirector::PushValue(UElysiumCameraComponent* Camera, const FElysiumCameraShot& Shot)
{
	if (!Camera)
	{
		return 0;
	}
	FLiveShot& Entry = Live.AddDefaulted_GetRef();
	Entry.Id = NextId++;
	Entry.bValue = true;
	Entry.CameraShotId = Camera->PushShot(Shot);
	return Entry.Id;
}

bool FElysiumCameraDirector::UpdateValue(UElysiumCameraComponent* Camera, int32 Id,
	const FElysiumCameraShot& Shot)
{
	const FLiveShot* Entry = Live.FindByPredicate(
		[Id](const FLiveShot& S) { return S.Id == Id && S.bValue; });
	return Entry && Camera && Camera->UpdateShot(Entry->CameraShotId, Shot);
}

bool FElysiumCameraDirector::Pop(UElysiumCameraComponent* Camera, int32 Id, float BlendOutSeconds)
{
	const int32 Index = Live.IndexOfByPredicate([Id](const FLiveShot& S) { return S.Id == Id; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	if (Camera)
	{
		Camera->PopShot(Live[Index].CameraShotId, BlendOutSeconds);
	}
	Live.RemoveAt(Index);
	return true;
}

void FElysiumCameraDirector::Clear(UElysiumCameraComponent* Camera)
{
	if (Camera)
	{
		for (const FLiveShot& Shot : Live)
		{
			Camera->PopShot(Shot.CameraShotId);
		}
	}
	Live.Reset();
}

void FElysiumCameraDirector::Tick(FElysiumEntityWorld* World, UElysiumCameraComponent* Camera)
{
	if (Live.Num() == 0 || !Camera)
	{
		return;
	}
	for (const FLiveShot& Entry : Live)
	{
		if (Entry.bValue)
		{
			continue;
		}
		// **Every anchor re-resolves, whatever its `AttachType`.** Retail's camera think
		// (`FUN_1006e8e0`, loop `0x1006ea90`) walks all four anchors every server tick and rewrites
		// the cache `FUN_1006f010` reads back, so `None` is a frame choice for the offset, not a
		// latch. Skipping the re-resolve here made a `None` anchor stale instead of still; what keeps
		// the camera still is the tracker's deadbands, and holding the shot values back robbed it of
		// the drift it is supposed to be measuring.
		FElysiumCameraShot Shot;
		if (Resolve(World, Entry.Def, Entry.Subject, Shot))
		{
			ApplyExposure(Entry, Shot);
			Camera->UpdateShot(Entry.CameraShotId, Shot);
		}
	}
}

bool FElysiumCameraDirector::WantsDialogPOV() const
{
	for (int32 Index = Live.Num() - 1; Index >= 0; --Index)
	{
		if (!Live[Index].bValue)
		{
			return Live[Index].Def.Constraints.bDialogPOV;
		}
	}
	return false;
}

FString FElysiumCameraDirector::Describe() const
{
	if (Live.Num() == 0)
	{
		return TEXT("no scripted shot");
	}
	FString Out;
	for (const FLiveShot& Entry : Live)
	{
		if (Entry.bValue)
		{
			Out += FString::Printf(TEXT("\n  #%d value (camera #%d)"), Entry.Id, Entry.CameraShotId);
		}
		else
		{
			Out += FString::Printf(TEXT("\n  #%d '%s' (camera #%d)  end %s/%s/%s  fov %.1f"),
				Entry.Id, *Entry.Def.Name, Entry.CameraShotId,
				ElysiumCameraShots::LexToString(Entry.Def.End.Position),
				*Entry.Def.End.AttachPos,
				ElysiumCameraShots::LexToString(Entry.Def.End.Attach),
				Entry.Def.Constraints.FieldOfView);
		}
	}
	return FString::Printf(TEXT("%d scripted shot(s):%s"), Live.Num(), *Out);
}
