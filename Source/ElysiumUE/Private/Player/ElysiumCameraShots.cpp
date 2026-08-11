#include "Player/ElysiumCameraShots.h"

#include "ElysiumCameraComponent.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumPlayer.h"

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
		Out.MoveSpeed = Node->Flt(TEXT("MoveSpeed"), 0.0f) * ElysiumCam::U;
		Out.MoveAccel = Node->Flt(TEXT("MoveAccel"), 0.0f) * ElysiumCam::U;
		Out.TurnAccel = Node->Flt(TEXT("TurnAccel"), 0.0f);
		Out.DistanceTolerance = Node->Flt(TEXT("DistanceTolerance"), 0.0f) * ElysiumCam::U;
		Out.FieldOfView = Node->Flt(TEXT("FieldOfView"), 0.0f);
		Out.bDialogPOV = Node->Bool(TEXT("DialogPOV"), false);
		Out.bAutoPositionFromTarget = Node->Bool(TEXT("AutoPositionFromTarget"), false);
		Out.bSyncRotateOnMove = Node->Bool(TEXT("SyncRotateOnMove"), false);
		Out.bSnapOnShotChange = Node->Bool(TEXT("SnapOnShotChange"), false);
		Out.bShowHud = Node->Bool(TEXT("ShowHud"), true);
		Out.bDrawViewmodel = Node->Bool(TEXT("DrawViewmodel"), true);

		FVector V;
		if (ParseBracketVector(Node->Str(TEXT("MaxTurnRate"), FString()), V))      { Out.MaxTurnRate = V; }
		if (ParseBracketVector(Node->Str(TEXT("AngularTolerance"), FString()), V)) { Out.AngularTolerance = V; }
	}

	// The parsed table, keyed by the lowercased shot-file name. A conversation re-reads the same file
	// every line, and the whole directory is 66 files of a few hundred bytes.
	TMap<FString, TSharedPtr<FElysiumCameraShotDef>>& Cache()
	{
		static TMap<FString, TSharedPtr<FElysiumCameraShotDef>> Map;
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

bool ElysiumCameraShots::ParseText(const FString& Text, FElysiumCameraShotDef& Out)
{
	const TSharedPtr<ElysiumKeyValues::FKvNode> Root = ElysiumKeyValues::ParseText(Text);
	if (!Root.IsValid())
	{
		return false;
	}
	// The file's outer block is `CameraShotTable`; tolerate its absence, since the shot block is what
	// carries everything and a hand-edited file may drop the wrapper.
	const ElysiumKeyValues::FKvNode* Table = Root->Child(TEXT("CameraShotTable"));
	const ElysiumKeyValues::FKvNode* Outer = Table ? Table : Root.Get();
	if (Outer->Kids.Num() == 0)
	{
		return false;
	}

	// One file, one shot, named after the file.
	Out = FElysiumCameraShotDef();
	Out.Name = Outer->Kids[0].Key;
	const ElysiumKeyValues::FKvNode* Shot = Outer->Kids[0].Value.Get();
	if (!Shot)
	{
		return false;
	}

	ParseAnchor(Shot->Child(TEXT("Start")), Out.Start);
	ParseAnchor(Shot->Child(TEXT("End")), Out.End);
	if (const ElysiumKeyValues::FKvNode* Target = Shot->Child(TEXT("Target")))
	{
		ParseAnchor(Target->Child(TEXT("Point1")), Out.Target1);
		ParseAnchor(Target->Child(TEXT("Point2")), Out.Target2);
	}
	ParseConstraints(Shot->Child(TEXT("CameraConstraints")), Out.Constraints);
	return true;
}

const FElysiumCameraShotDef* ElysiumCameraShots::Load(const FString& ShotFile)
{
	if (ShotFile.IsEmpty())
	{
		return nullptr;
	}
	const FString Key = NormalizeKey(ShotFile);
	if (Key.IsEmpty())
	{
		return nullptr;
	}
	if (const TSharedPtr<FElysiumCameraShotDef>* Hit = Cache().Find(Key))
	{
		return Hit->Get();   // a null entry is a remembered miss
	}

	const FString Path = FElysiumContentPaths::Root() / TEXT("vdata/camerashots") / (Key + TEXT(".txt"));
	FString Text;
	TSharedPtr<FElysiumCameraShotDef> Def;
	if (FFileHelper::LoadFileToString(Text, *Path))
	{
		Def = MakeShared<FElysiumCameraShotDef>();
		if (!ParseText(Text, *Def))
		{
			UE_LOG(LogElysiumCamShots, Warning, TEXT("camera shot '%s' has no shot block"), *Key);
			Def.Reset();
		}
	}
	else
	{
		UE_LOG(LogElysiumCamShots, Warning, TEXT("camera shot '%s' not found (%s)"), *Key, *Path);
	}
	Cache().Add(Key, Def);
	return Def.Get();
}

void ElysiumCameraShots::FlushCache()
{
	Cache().Reset();
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

// =====================================================================================
// The director
// =====================================================================================

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
			return World->FindByName(Anchor.NamedEntity);
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
			return Body ? BoundsPoint(0.92f) : Origin + FVector(0.0f, 0.0f, FallbackEyeHeight);
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
		? Base + FRotator(0.0f, -Entity->Angles.Y, 0.0f).RotateVector(Anchor.OffsetOrigin)
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
	Out.FieldOfView = Def.Constraints.FieldOfView;
	Out.MoveSpeed = Def.Constraints.MoveSpeed;
	Out.MaxTurnRate = Def.Constraints.MaxTurnRate;
	// `SnapOnShotChange` is the file's own "cut, do not blend" flag.
	Out.BlendSeconds = Def.Constraints.bSnapOnShotChange ? 0.0f : 0.5f;
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
		const bool bFollows = Entry.Def.End.IsFollowing() || Entry.Def.Start.IsFollowing()
			|| Entry.Def.Target1.IsFollowing() || Entry.Def.Target2.IsFollowing();
		if (!bFollows)
		{
			// `AttachType None` is "set yourself there and don't follow it" — re-resolving would
			// undo exactly that.
			continue;
		}
		FElysiumCameraShot Shot;
		if (Resolve(World, Entry.Def, Entry.Subject, Shot))
		{
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
