#include "Player/ElysiumCameraShots.h"

#include "ElysiumCameraComponent.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"     // FElysiumEntityDef::Classname — the worldspawn lookup
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumPlayer.h"         // FElysiumCombatCharacter::Grapple — the two grapple anchors
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"  // IElysiumEmbodiment — the bounds and attachment seams

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

	// `Position` — retail's `_strstr` chain in `FUN_10071e00`, in its own order, with retail's
	// fallthrough. The chain tests substrings, so the order is what decides an ambiguous value; the
	// port keeps the order and matches whole values, because no shipped file writes a compound one.
	//
	// **There is no "the value is an entity name" arm.** The how-to's second reading of `Named` —
	// "the name of an entity in the map" — is documentation, not grammar: retail raises the `Named`
	// bit off the keyword alone and never reads a name from the record. An unrecognised value falls
	// through to `World`, which is also the default, and the warning names the token so the fall is
	// visible instead of silent.
	EElysiumShotPosition ParsePosition(const FString& Raw, const TCHAR* ShotName)
	{
		const FString V = Raw.TrimStartAndEnd();
		if (V.Equals(TEXT("Player"), ESearchCase::IgnoreCase))          { return EElysiumShotPosition::Player; }
		if (V.Equals(TEXT("DialogTarget"), ESearchCase::IgnoreCase))    { return EElysiumShotPosition::DialogTarget; }
		if (V.Equals(TEXT("GrappleVictim"), ESearchCase::IgnoreCase))   { return EElysiumShotPosition::GrappleVictim; }
		if (V.Equals(TEXT("GrappleAttacker"), ESearchCase::IgnoreCase)) { return EElysiumShotPosition::GrappleAttacker; }
		if (V.Equals(TEXT("Named"), ESearchCase::IgnoreCase))           { return EElysiumShotPosition::Named; }
		if (V.Equals(TEXT("World"), ESearchCase::IgnoreCase) || V.IsEmpty())
		{
			return EElysiumShotPosition::World;
		}
		UE_LOG(LogElysiumCamShots, Warning,
			TEXT("camera shot '%s': Position '%s' is not a keyword; retail's _strstr chain falls ")
			TEXT("through to World, so this anchor resolves to the world origin"),
			ShotName, *V);
		return EElysiumShotPosition::World;
	}

	// `AttachType` — an **exact byte compare including the NUL** (`Follow` 7 bytes,
	// `FollowNoAngles` 15, `FollowEntAngles` 16), case-sensitive, unlike every other key on this
	// path. M3, ruled 2026-09-07: the leniency the port used to have is not available, because the
	// value decides whether the anchor **latches** at shot start (`None`) or tracks, which is state.
	// A case-only mismatch warns, naming the retail spelling and what retail reads instead.
	EElysiumShotAttach ParseAttach(const FString& Raw, const TCHAR* ShotName)
	{
		const FString V = Raw.TrimStartAndEnd();
		if (V.Equals(TEXT("Follow"), ESearchCase::CaseSensitive))          { return EElysiumShotAttach::Follow; }
		if (V.Equals(TEXT("FollowNoAngles"), ESearchCase::CaseSensitive))  { return EElysiumShotAttach::FollowNoAngles; }
		if (V.Equals(TEXT("FollowEntAngles"), ESearchCase::CaseSensitive)) { return EElysiumShotAttach::FollowEntAngles; }

		static const TCHAR* const Spellings[] = { TEXT("Follow"), TEXT("FollowNoAngles"),
			TEXT("FollowEntAngles") };
		for (const TCHAR* Spelling : Spellings)
		{
			if (V.Equals(Spelling, ESearchCase::IgnoreCase))
			{
				UE_LOG(LogElysiumCamShots, Warning,
					TEXT("camera shot '%s': AttachType '%s' differs from retail's '%s' only in case, ")
					TEXT("and retail's compare is case-sensitive -- it reads the value as None, so ")
					TEXT("this anchor latches at shot start instead of following"),
					ShotName, *V, Spelling);
				break;
			}
		}
		return EElysiumShotAttach::None;
	}

	// `AttachPos` — retail's `_strstr` chain, in its order. `Bone:` and `Attachment:` carry an
	// inline name; every other keyword is the whole value.
	EElysiumShotAttachPos ParseAttachPos(const FString& Raw, FString& OutName)
	{
		OutName.Reset();
		const FString V = Raw.TrimStartAndEnd();
		// `Q_trimspace(hit + 5, rec + 4, 0x10)` / `Q_trimspace(hit + 0xb, rec + 4, 0x10)` — the
		// inline name is 16 bytes including the NUL, so 15 characters. `camera_position` is exactly
		// 15 and is the longest the corpus writes.
		constexpr int32 InlineNameChars = 15;
		auto TakeName = [&OutName](const FString& Value, int32 PrefixLen)
		{
			OutName = Value.Mid(PrefixLen).TrimStartAndEnd().Left(InlineNameChars);
		};
		if (V.StartsWith(TEXT("Bone:"), ESearchCase::IgnoreCase))
		{
			TakeName(V, 5);
			return EElysiumShotAttachPos::Bone;
		}
		if (V.StartsWith(TEXT("Attachment:"), ESearchCase::IgnoreCase))
		{
			TakeName(V, 11);
			return EElysiumShotAttachPos::Attachment;
		}
		if (V.Equals(TEXT("Center"), ESearchCase::IgnoreCase))      { return EElysiumShotAttachPos::Center; }
		if (V.Equals(TEXT("EyePosition"), ESearchCase::IgnoreCase)) { return EElysiumShotAttachPos::EyePosition; }
		if (V.Equals(TEXT("Top"), ESearchCase::IgnoreCase))         { return EElysiumShotAttachPos::Top; }
		if (V.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase))      { return EElysiumShotAttachPos::Bottom; }
		if (V.Equals(TEXT("AbsMin"), ESearchCase::IgnoreCase))      { return EElysiumShotAttachPos::AbsMin; }
		if (V.Equals(TEXT("AbsMax"), ESearchCase::IgnoreCase))      { return EElysiumShotAttachPos::AbsMax; }
		return EElysiumShotAttachPos::Origin;   // `Origin`, and retail's default
	}

	void ParseAnchor(const FKvNode* Node, const TCHAR* ShotName, FElysiumShotAnchor& Out)
	{
		if (!Node)
		{
			return;
		}
		Out.bPresent = true;
		Out.Position = ParsePosition(Node->Str(TEXT("Position"), FString()), ShotName);
		Out.AttachPos = Node->Str(TEXT("AttachPos"), TEXT("Origin")).TrimStartAndEnd();
		Out.AttachPoint = ParseAttachPos(Out.AttachPos, Out.AttachPointName);
		Out.Attach = ParseAttach(Node->Str(TEXT("AttachType"), FString()), ShotName);

		FVector Offset;
		// The corpus writes `OffsetOrigin`; the how-to's `End` block also lists a bare `Offset`. The
		// parse default on both halves of retail is the literal `"[0, 0, 0]"`, and the flag `0x20000`
		// that gates the offset step is raised only when the parsed vector is non-zero (RC11).
		if (ParseBracketVector(Node->Str(TEXT("OffsetOrigin"), FString()), Offset) ||
			ParseBracketVector(Node->Str(TEXT("Offset"), FString()), Offset))
		{
			Out.OffsetOrigin = Offset * ElysiumCam::U;
		}
		// **`OffsetAngles` (`+0x20`, flag `0x40000`) stays unparsed, on evidence.** It has no reader
		// anywhere in `vampire.dll` — `FUN_1006f080` only ever touches `+0x14` — and the client
		// parser stores it and never reads it either (RC11). The how-to says so in as many words:
		// "NOTE: This currently isn't implemented." Parsing it would create a field nothing may act
		// on. This settles the document's last open question on this path.
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
		ParseAnchor(Shot->Child(TEXT("Start")), *Def.Name, Def.Start);
		ParseAnchor(Shot->Child(TEXT("End")), *Def.Name, Def.End);
		if (const ElysiumKeyValues::FKvNode* Target = Shot->Child(TEXT("Target")))
		{
			// `flags |= 1 << (count + 2); count++` per sub-block found, exactly as retail: the flag
			// follows the ORDER of presence while the anchor goes into the fixed slot. `Point2`
			// alone therefore raises the `Point1` bit and leaves slot 2 empty — the look-at reads
			// the empty slot and the shot aims at the world origin (§4 row 1).
			auto RaisePresence = [&Def]()
			{
				(Def.TargetPointCount == 0 ? Def.bTargetPoint1Flagged : Def.bTargetPoint2Flagged) = true;
				++Def.TargetPointCount;
			};
			if (const ElysiumKeyValues::FKvNode* Point1 = Target->Child(TEXT("Point1")))
			{
				ParseAnchor(Point1, *Def.Name, Def.Target1);
				RaisePresence();
			}
			if (const ElysiumKeyValues::FKvNode* Point2 = Target->Child(TEXT("Point2")))
			{
				ParseAnchor(Point2, *Def.Name, Def.Target2);
				RaisePresence();
			}
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
	case EElysiumShotPosition::Player:          return TEXT("Player");
	case EElysiumShotPosition::DialogTarget:    return TEXT("DialogTarget");
	case EElysiumShotPosition::GrappleVictim:   return TEXT("GrappleVictim");
	case EElysiumShotPosition::GrappleAttacker: return TEXT("GrappleAttacker");
	case EElysiumShotPosition::World:           return TEXT("World");
	case EElysiumShotPosition::Named:           return TEXT("Named");
	default:                                    return TEXT("none");
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

const TCHAR* ElysiumCameraShots::LexToString(EElysiumShotAttachPos AttachPoint)
{
	switch (AttachPoint)
	{
	case EElysiumShotAttachPos::Bone:        return TEXT("Bone:");
	case EElysiumShotAttachPos::Attachment:  return TEXT("Attachment:");
	case EElysiumShotAttachPos::Center:      return TEXT("Center");
	case EElysiumShotAttachPos::EyePosition: return TEXT("EyePosition");
	case EElysiumShotAttachPos::Top:         return TEXT("Top");
	case EElysiumShotAttachPos::Bottom:      return TEXT("Bottom");
	case EElysiumShotAttachPos::AbsMin:      return TEXT("AbsMin");
	case EElysiumShotAttachPos::AbsMax:      return TEXT("AbsMax");
	default:                                 return TEXT("Origin");
	}
}

bool ElysiumCameraShots::LatchesAnchors(const FElysiumCameraShotDef& Def)
{
	// `FUN_1006f010(this, out, i)` opens with `thunk_FUN_1006ee10(this, 0)` — a **literal 0**, so it
	// reads anchor 0's flags whatever index it was asked for. That is retail's bug and it is what
	// decides which shots latch, so reproducing the latch without it would change 55 of the 66
	// shipped shots (`camera_scripted.md` §4 row 4, `docs/vtmb/retail-defects.md` §7).
	//
	// A shot with no `Start` block leaves anchor 0 zeroed, so the `0x2000` (`None`) bit is clear and
	// all four anchors re-resolve every think — which is why `jack.txt`'s head-bone target tracks and
	// why its 10-degree `AngularTolerance` is what actually holds the shot still. A `Start` block
	// with no `AttachType` key parses to `None` and does latch, because `None` is retail's parse
	// default too.
	return Def.Start.bPresent && Def.Start.Attach == EElysiumShotAttach::None;
}

// The director

namespace ElysiumCameraShotsImpl
{
	// `FindEntityByClassname(NULL, "worldspawn")` — what retail's `World` arm resolves to, and what
	// its unrecognised-keyword fallthrough lands on. Null on a fixture world that authors no
	// worldspawn, which every shipped map does author.
	FElysiumEntity* WorldSpawn(FElysiumEntityWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (const TUniquePtr<FElysiumEntity>& Ent : World->Entities())
		{
			if (Ent && Ent->Def && Ent->Def->Classname.Equals(TEXT("worldspawn"), ESearchCase::IgnoreCase))
			{
				return Ent.Get();
			}
		}
		return nullptr;
	}

	// The entity an anchor's `Position` names — retail's `SetShot` `FUN_1006e130` resolve loop,
	// verbatim. `Named` answers **NULL**: a named anchor is never resolved here, the caller supplies
	// it through `SetShotAnchorEntity`.
	FElysiumEntity* SetShotEntity(FElysiumEntityWorld* World, const FElysiumShotAnchor& Anchor,
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
			// `subject+0xFE8`, the subject's dialogue partner. `SetCamera`'s receiver IS the
			// conversation's other half, so the subject is the answer.
			return World->Resolve(Subject);
		case EElysiumShotPosition::GrappleVictim:
		case EElysiumShotPosition::GrappleAttacker:
		{
			FElysiumEntity* SubjectEnt = World->Resolve(Subject);
			const FElysiumCombatCharacter* Character =
				SubjectEnt ? SubjectEnt->AsCombatCharacter() : nullptr;
			FElysiumEntity* Partner =
				Character ? World->Resolve(Character->Grapple.Partner) : nullptr;
			// **Both arms require the `+0x1538` handle to be live**; a dead partner falls through to
			// the `World`/NULL tail, which is why a stealth-kill shot left up past the kill frames
			// the world origin rather than a corpse.
			if (!Partner || Character->Grapple.Role == EElysiumGrappleRole::None)
			{
				return WorldSpawn(World);
			}
			// `GrappleVictim` -> the partner when the subject is the ATTACKER (role 0), and **the
			// subject itself** when the subject is the victim (role 1). `GrappleAttacker` is the
			// mirror. The polarity is retail's, proved by `StartGrappleAttack`'s two role literals
			// (`PUSH 0x0` on the attacker, `PUSH 0x1` on the victim) — RC13.
			const bool bWantVictim = Anchor.Position == EElysiumShotPosition::GrappleVictim;
			const bool bSubjectIsAttacker = Character->Grapple.Role == EElysiumGrappleRole::Attacker;
			return (bWantVictim == bSubjectIsAttacker) ? Partner : SubjectEnt;
		}
		case EElysiumShotPosition::World:
			return WorldSpawn(World);
		default:
			// `Named`, and the `None` a shot with no block carries.
			return nullptr;
		}
	}

	// The seam that answered an anchor's `Bone:` / `Attachment:` name, cached in the binding's
	// `PointIndex` so the resolve never searches by name again. This is what the port keeps in place
	// of retail's integer bone/attachment index at `+0x620 + i*4`; `INDEX_NONE` is retail's `-1`.
	enum class EAttachSeam : int32
	{
		EntityAttachment = 0,   // `FElysiumEntity::GetBodyAttachmentPoint` -- a point, no angles
		BodySocket       = 1,   // the standing skeletal body's socket table -- position and angles
		Embodiment       = 2,   // the placed model's baked `$attachment` frame -- position and angles
	};

	// VtMB's own standing hull height, the stand-in bounds for an entity with no body to measure.
	constexpr float FallbackCenterHeight = 36.0f * ElysiumCam::U;

	// The world-space surrounding bounds retail reads **once at the top** of `FUN_1006f080`
	// (`ent->m_Collision (+0x270)->vfunc 0x3c`) and every `AttachPos` arm then indexes into.
	struct FAnchorBounds
	{
		FVector Min = FVector::ZeroVector;
		FVector Max = FVector::ZeroVector;

		FVector Center() const { return (Min + Max) * 0.5f; }
	};

	FAnchorBounds SurroundingBounds(const FElysiumEntity& Entity)
	{
		FAnchorBounds Out;
		if (const USkeletalMeshComponent* Body = Entity.GetSkeletalBody())
		{
			const FBoxSphereBounds Bounds = Body->Bounds;
			Out.Min = Bounds.Origin - Bounds.BoxExtent;
			Out.Max = Bounds.Origin + Bounds.BoxExtent;
			return Out;
		}
		// A placed prop keeps its box on the embodiment rather than on a skeletal body; this is the
		// same box retail's held-use maintenance measures reach against.
		const IElysiumEmbodiment* Embodiment = Entity.World ? Entity.World->Embodiment() : nullptr;
		FBox Box(ForceInit);
		if (Embodiment && Embodiment->GetUseBodyWorldBounds(Entity.Handle, Box) && Box.IsValid)
		{
			Out.Min = Box.Min;
			Out.Max = Box.Max;
			return Out;
		}
		// Nothing to measure. The stand-in is VtMB's standing hull on the entity's own origin, so a
		// bodiless conversation partner still frames the way a bodied one does; retail's answer here
		// is whatever the entity's collision prop holds and is not recoverable from a body-less port
		// state.
		Out.Min = Entity.Origin;
		Out.Max = Entity.Origin + FVector(0.0f, 0.0f, FallbackCenterHeight * 2.0f);
		return Out;
	}

	// One `AttachPos` sample: the point, and the rotation basis the `Follow` offset step turns in.
	struct FAnchorSample
	{
		FVector Point = FVector::ZeroVector;
		FRotator Basis = FRotator::ZeroRotator;
	};

	// Ask one seam for a bone's / attachment's whole frame. `bOutHasBasis` is false for a seam that
	// can only answer a point.
	bool SampleAttachSeam(const FElysiumEntity& Entity, EAttachSeam Seam, FName Socket,
		FTransform& OutFrame, bool& bOutHasBasis)
	{
		bOutHasBasis = false;
		switch (Seam)
		{
		case EAttachSeam::EntityAttachment:
		{
			// A placed prop is normally reduced to its static representation and has no skeletal
			// body to hold a socket table, while the model's `$attachment`s still exist on its baked
			// skeletal asset. This route is asked FIRST, so the terminal's screen cone and its
			// camera read the same two vectors (`screen`, `screen_axis`) by construction. It answers
			// a point only; retail's `GetAttachment02` also returns angles, and the port leaves the
			// basis at the entity's own — which changes nothing on shipped content, because no
			// `Attachment:` anchor in the corpus authors an `OffsetOrigin` for the basis to turn.
			FVector Point;
			if (!Entity.GetBodyAttachmentPoint(Socket, Point))
			{
				return false;
			}
			OutFrame.SetLocation(Point);
			return true;
		}
		case EAttachSeam::BodySocket:
		{
			const USkeletalMeshComponent* Body = Entity.GetSkeletalBody();
			if (!Body || !Body->DoesSocketExist(Socket))
			{
				return false;
			}
			OutFrame = Body->GetSocketTransform(Socket, RTS_World);
			bOutHasBasis = true;
			return true;
		}
		case EAttachSeam::Embodiment:
		{
			const IElysiumEmbodiment* Embodiment = Entity.World ? Entity.World->Embodiment() : nullptr;
			if (!Embodiment || !Embodiment->GetBodyAttachment(Entity.Handle, Socket, OutFrame))
			{
				return false;
			}
			bOutHasBasis = true;
			return true;
		}
		}
		return false;
	}

	// `SetShotAnchorEntity`'s lookup half: which seam holds this anchor's bone or attachment, asked
	// **once** and cached. Retail asks the entity's animating half (vfunc `0x224`) and stores an
	// index; a non-animating entity leaves it at `-1` and nothing here answers either.
	int32 LookupAttachPointIndex(const FElysiumEntity& Entity, const FElysiumShotAnchor& Anchor)
	{
		if (!Anchor.NeedsAttachPointIndex() || Anchor.AttachPointName.IsEmpty())
		{
			return INDEX_NONE;
		}
		const FName Socket(*Anchor.AttachPointName);
		static const EAttachSeam Seams[] =
			{ EAttachSeam::EntityAttachment, EAttachSeam::BodySocket, EAttachSeam::Embodiment };
		for (EAttachSeam Seam : Seams)
		{
			FTransform Frame;
			bool bHasBasis = false;
			if (SampleAttachSeam(Entity, Seam, Socket, Frame, bHasBasis))
			{
				return static_cast<int32>(Seam);
			}
		}
		return INDEX_NONE;
	}

	// `AttachPos` -> a point on the entity and the frame its offset turns in. The surrounding bounds
	// are read once, at the top, for every arm — retail's own shape.
	FAnchorSample AttachPoint(const FElysiumEntity& Entity, const FElysiumShotAnchor& Anchor,
		int32 PointIndex)
	{
		FAnchorSample Out;
		// Every arm but `Bone:` / `Attachment:` takes the entity's abs angles as its rotation basis.
		Out.Basis = ElysiumSkeletalBasis::FromSourceAngles(Entity.Angles);
		const FAnchorBounds Bounds = SurroundingBounds(Entity);

		switch (Anchor.AttachPoint)
		{
		case EElysiumShotAttachPos::Bone:
		case EElysiumShotAttachPos::Attachment:
		{
			// The index was resolved once, at bind time. An unresolved index is retail's `-1`, which
			// it hands straight to `GetBonePosition02` — what that call then writes is not recovered,
			// so the port answers the entity's abs origin, which is the same answer its `Origin` arm
			// gives. The old `Origin + Z(64u)` guess had no retail counterpart and is gone.
			FTransform Frame;
			bool bHasBasis = false;
			if (PointIndex != INDEX_NONE &&
				SampleAttachSeam(Entity, static_cast<EAttachSeam>(PointIndex),
					FName(*Anchor.AttachPointName), Frame, bHasBasis))
			{
				Out.Point = Frame.GetLocation();
				if (bHasBasis)
				{
					Out.Basis = Frame.Rotator();
				}
				return Out;
			}
			Out.Point = Entity.Origin;
			return Out;
		}
		case EElysiumShotAttachPos::Center:
			// `WorldSpaceCenter()`, vfunc `0x300` — the bounds centre on all three axes.
			Out.Point = Bounds.Center();
			return Out;
		case EElysiumShotAttachPos::EyePosition:
			// **A fixed offset from the origin, not a bounds fraction and not a bone.** Retail
			// reaches the eye through `CBaseCombatCharacter::CalcLookData`, which is
			// `GetAbsOrigin() + m_vecViewOffset` — the same value `CBaseEntity::EyePosition()`
			// answers. Measuring the animated bounds instead made an `EyePosition` anchor breathe
			// with the idle, where retail's holds still.
			Out.Point = Entity.EyePosition();
			return Out;
		case EElysiumShotAttachPos::Top:
			// `(absOrigin.x, absOrigin.y, bounds.maxs.z)` — the ORIGIN's XY, only Z from the bounds.
			// The port used to take a bounds fraction on all three axes, which moved the anchor
			// sideways on any body whose box is not centred on its origin.
			Out.Point = FVector(Entity.Origin.X, Entity.Origin.Y, Bounds.Max.Z);
			return Out;
		case EElysiumShotAttachPos::Bottom:
			Out.Point = FVector(Entity.Origin.X, Entity.Origin.Y, Bounds.Min.Z);
			return Out;
		case EElysiumShotAttachPos::AbsMin:
			// `0x800` — the surrounding bounds' mins, **all three components**. Shipped once, in
			// `centerfullview.txt`, and absent from the how-to.
			Out.Point = Bounds.Min;
			return Out;
		case EElysiumShotAttachPos::AbsMax:
			Out.Point = Bounds.Max;
			return Out;
		default:
			Out.Point = Entity.Origin;   // `Origin`, and retail's default
			return Out;
		}
	}

	// `SetShotAnchorEntity` `FUN_1006ef50` — store the handle, and resolve the inline name ONCE.
	void BindAnchor(FElysiumEntityWorld* World, const FElysiumShotAnchor& Anchor,
		const FElysiumEntityHandle& Entity, FElysiumShotAnchorBinding& Binding)
	{
		Binding.Entity = Entity;
		Binding.PointIndex = INDEX_NONE;
		const FElysiumEntity* Ent = World ? World->Resolve(Entity) : nullptr;
		if (!Ent)
		{
			// `if (ent == NULL) { this->+0x610[i] = 0xffffffff; return; }` — and the index array keeps
			// whatever the mode clear last put there, which is `-1`.
			Binding.Entity = FElysiumEntityHandle::Invalid();
			return;
		}
		Binding.PointIndex = LookupAttachPointIndex(*Ent, Anchor);
	}
}

void FElysiumCameraDirector::BindAnchors(FElysiumEntityWorld* World, const FElysiumCameraShotDef& Def,
	const FElysiumEntityHandle& Subject, FElysiumShotBindings& Bindings)
{
	const FElysiumShotAnchor* const Anchors[FElysiumShotBindings::Num] =
		{ &Def.Start, &Def.End, &Def.Target1, &Def.Target2 };
	for (int32 Index = 0; Index < FElysiumShotBindings::Num; ++Index)
	{
		const FElysiumShotAnchor& Anchor = *Anchors[Index];
		const FElysiumEntity* Ent = ElysiumCameraShotsImpl::SetShotEntity(World, Anchor, Subject);
		FElysiumEntityHandle Bound = Ent ? Ent->Handle : FElysiumEntityHandle::Invalid();
		if (!Bound.IsSet() && Anchor.Position == EElysiumShotPosition::Named)
		{
			// Retail's `Named` resolves to NULL and the **caller** fills the slot. For a one-entity
			// shot that call is `FUN_10070470("Hacking", NULL, terminal, terminal, NULL)`, which
			// hands the same entity into every Named slot
			// (`docs/vtmb/computer-terminals.md` §7.3 step 6). The port's shot subject is that
			// entity, so seeding from it reproduces the shipped call shape;
			// `SetShotAnchorEntity` overrides it for a caller that supplies a different one — the
			// death path's corpse for `special-case.txt`'s `DeathCam`, say.
			Bound = Subject;
		}
		ElysiumCameraShotsImpl::BindAnchor(World, Anchor, Bound, Bindings.Anchors[Index]);
	}
}

bool FElysiumCameraDirector::ResolveAnchor(FElysiumEntityWorld* World, const FElysiumShotAnchor& Anchor,
	FElysiumShotAnchorBinding& Binding, bool bLatched, FVector& OutPoint)
{
	if (!Anchor.bPresent)
	{
		return false;
	}
	// `FUN_1006f010`: an `AttachType None` shot returns the shot-start cache at `+0x598 + i*12`
	// instead of re-resolving. `bLatched` is that decision, taken once per shot from **anchor 0's**
	// flags (retail's own bug — see `ElysiumCameraShots::LatchesAnchors`).
	if (bLatched && Binding.bCached)
	{
		OutPoint = Binding.Cached;
		return true;
	}

	const FElysiumEntity* Entity = World ? World->Resolve(Binding.Entity) : nullptr;
	if (!Entity)
	{
		// A dead or unbound anchor. Retail's `FUN_1006f080` writes the zero vector through the same
		// path, which is why a flagged-but-empty target slot aims the shot at `(0,0,0)`; the origin
		// selector's callers treat "did not resolve" as "this anchor cannot drive the origin".
		return false;
	}

	const ElysiumCameraShotsImpl::FAnchorSample Sample =
		ElysiumCameraShotsImpl::AttachPoint(*Entity, Anchor, Binding.PointIndex);

	// The offset step, `LAB_1006f430`, gated on `OffsetOrigin` being non-zero (`0x20000`):
	//   `flags & 0xa000` (`None` | `FollowNoAngles`) -> add in world axes;
	//   `flags & 0x4000` (`Follow`)                  -> turn by the ATTACH POINT's own angles;
	//   `flags & 0x10000` (`FollowEntAngles`)        -> turn by the entity's abs angles.
	// The port used to turn `Follow` by the entity's angles too, which cost a `Bone:` `Follow`
	// anchor its bone-space offset frame.
	switch (Anchor.Attach)
	{
	case EElysiumShotAttach::Follow:
		OutPoint = Sample.Point + Sample.Basis.RotateVector(Anchor.OffsetOrigin);
		break;
	case EElysiumShotAttach::FollowEntAngles:
		OutPoint = Sample.Point
			+ ElysiumSkeletalBasis::FromSourceAngles(Entity->Angles).RotateVector(Anchor.OffsetOrigin);
		break;
	default:
		OutPoint = Sample.Point + Anchor.OffsetOrigin;
		break;
	}

	// The shot-start cache is filled by the resolve that runs at shot start, and read back by every
	// think after it while the shot latches.
	if (!Binding.bCached)
	{
		Binding.Cached = OutPoint;
		Binding.bCached = true;
	}
	return true;
}

bool FElysiumCameraDirector::Resolve(FElysiumEntityWorld* World, const FElysiumCameraShotDef& Def,
	const FElysiumEntityHandle& Subject, FElysiumCameraShot& Out,
	FElysiumShotBindings* Bindings, EElysiumShotResolvePass Pass)
{
	// A caller with no live shot behind it (the dialogue ladder) gets a one-shot binding table on
	// the stack: bound from the definition and the subject, resolved once, thrown away.
	FElysiumShotBindings Scratch;
	if (!Bindings)
	{
		BindAnchors(World, Def, Subject, Scratch);
		Bindings = &Scratch;
		Pass = EElysiumShotResolvePass::ShotStart;
	}
	const bool bLatched = Pass == EElysiumShotResolvePass::Think
		&& ElysiumCameraShots::LatchesAnchors(Def);

	// "If there is no End position specified, the camera will not move between the points" — so End is
	// where the shot lives and Start is only its entry. Until a shot is *animated* between the two
	// (the theatre's, 12.x), the framing is End, or Start when the file authors only that.
	FVector Origin;
	if (!ResolveAnchor(World, Def.End, Bindings->Anchors[1], bLatched, Origin) &&
		!ResolveAnchor(World, Def.Start, Bindings->Anchors[0], bLatched, Origin))
	{
		return false;
	}

	// The look-at, `FUN_1006f670`, verbatim — and it reads the **flags**, not the slots:
	//   `(0x4 && 0x8)` -> the midpoint of anchors 2 and 3; `0x4` -> anchor 2; `0x8` -> anchor 3;
	//   neither -> `(0,0,0)`.
	// Because the parser raises those two bits by order of presence while writing the fixed slot, a
	// shot that authors `Point2` alone raises the `Point1` bit and this reads the empty slot 2 — the
	// shot aims at the world origin (§4 row 1). An anchor that does not resolve leaves the zero
	// there for the same reason retail does: `FUN_1006f080` wrote nothing.
	FVector Point1 = FVector::ZeroVector;
	FVector Point2 = FVector::ZeroVector;
	ResolveAnchor(World, Def.Target1, Bindings->Anchors[2], bLatched, Point1);
	ResolveAnchor(World, Def.Target2, Bindings->Anchors[3], bLatched, Point2);

	FVector Look = FVector::ZeroVector;
	if (Def.bTargetPoint1Flagged && Def.bTargetPoint2Flagged)
	{
		// "If there are two points, the camera will track a point halfway between them."
		Look = (Point1 + Point2) * 0.5f;
	}
	else if (Def.bTargetPoint1Flagged)
	{
		Look = Point1;
	}
	else if (Def.bTargetPoint2Flagged)
	{
		Look = Point2;
	}
	// `if (rec->+0xd4 > 0)` — the mode-1 think derives the angle from the look-at only when the shot
	// authored a `Target` block at all; one without keeps the entity's own abs angles.
	const bool bHasTarget = Def.TargetPointCount > 0;

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
	// **A file shot is a cine camera, and a cine camera has no blend at all** (SC2, M1). `SetCamera`
	// adopts `C_BaseCineCamera` through `m_iCameraOverrideIdx`; `C_BasePlayer::CalcView`
	// (`0x100a7770`) hard-writes its pose from the next frame and every exit is a same-tick removal.
	// No blend field exists anywhere on the entity, so the 0.5 s here was invented — it is deleted,
	// and `bCine` is what the channel reads instead of a duration.
	Out.bCine = true;
	Out.BlendSeconds = 0.0f;

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

	// The entry lands first because retail's shot start writes the four anchor handles and their
	// bone/attachment indices ONTO the camera before it places anything (`SetShot`'s
	// `SetShotAnchorEntity` loop, then `FUN_1006e8e0`), and the placement reads them back. A shot
	// that then fails to anchor is dropped again.
	FLiveShot& Entry = Live.AddDefaulted_GetRef();
	Entry.Id = NextId++;
	Entry.bValue = false;
	Entry.Def = *Def;
	Entry.Subject = Subject;
	BindAnchors(World, Entry.Def, Subject, Entry.Bindings);

	FElysiumCameraShot Shot;
	if (!Resolve(World, Entry.Def, Subject, Shot, &Entry.Bindings, EElysiumShotResolvePass::ShotStart))
	{
		UE_LOG(LogElysiumCamShots, Warning, TEXT("camera shot '%s': nothing it anchors to resolved"),
			*Def->Name);
		Live.RemoveAt(Live.Num() - 1);
		return 0;
	}
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

	FLiveShot& Entry = Live.AddDefaulted_GetRef();
	Entry.Id = NextId++;
	Entry.bValue = false;
	Entry.Def = *Def;
	Entry.Subject = Subject;
	Entry.Exposure = Exposure;
	BindAnchors(World, Entry.Def, Subject, Entry.Bindings);

	FElysiumCameraShot Shot;
	if (!Resolve(World, Entry.Def, Subject, Shot, &Entry.Bindings, EElysiumShotResolvePass::ShotStart))
	{
		UE_LOG(LogElysiumCamShots, Warning,
			TEXT("camera shot '%s:%s': nothing it anchors to resolved"), *ShotFile, *ShotName);
		Live.RemoveAt(Live.Num() - 1);
		return 0;
	}
	ApplyExposure(Entry, Shot);
	Entry.CameraShotId = Camera->PushShot(Shot);
	return Entry.Id;
}

bool FElysiumCameraDirector::SetShotAnchorEntity(FElysiumEntityWorld* World,
	UElysiumCameraComponent* Camera, int32 Id, int32 AnchorIndex, const FElysiumEntityHandle& Entity)
{
	if (AnchorIndex < 0 || AnchorIndex >= FElysiumShotBindings::Num)
	{
		return false;
	}
	FLiveShot* Entry = Live.FindByPredicate(
		[Id](const FLiveShot& S) { return S.Id == Id && !S.bValue; });
	if (!Entry)
	{
		return false;
	}
	const FElysiumShotAnchor* const Anchors[FElysiumShotBindings::Num] =
		{ &Entry->Def.Start, &Entry->Def.End, &Entry->Def.Target1, &Entry->Def.Target2 };
	FElysiumShotAnchorBinding& Binding = Entry->Bindings.Anchors[AnchorIndex];
	ElysiumCameraShotsImpl::BindAnchor(World, *Anchors[AnchorIndex], Entity, Binding);
	// A newly supplied entity re-opens the shot-start cache: the anchor has not been sampled against
	// this one yet, so the next resolve fills it. Retail reaches the same state because the caller
	// that fills a `Named` slot does it inside `SetShot`, before `FUN_1006e8e0` writes the cache.
	Binding.bCached = false;

	// Re-resolve now so the supplied entity is on screen this frame rather than next think.
	FElysiumCameraShot Shot;
	if (Camera && Resolve(World, Entry->Def, Entry->Subject, Shot, &Entry->Bindings,
		EElysiumShotResolvePass::ShotStart))
	{
		ApplyExposure(*Entry, Shot);
		Camera->UpdateShot(Entry->CameraShotId, Shot);
	}
	return true;
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
	for (FLiveShot& Entry : Live)
	{
		if (Entry.bValue)
		{
			continue;
		}
		// **`AttachType None` latches; everything else re-resolves.** The per-tick anchor reader
		// `FUN_1006f010` returns the shot-start cache at `+0x598 + i*12` when the `0x2000` bit is
		// set, and re-resolves through `FUN_1006f080` otherwise — and it reads **anchor 0's** flags
		// for every index, so the whole shot latches or none of it does
		// (`ElysiumCameraShots::LatchesAnchors`, `docs/vtmb/retail-defects.md` §7).
		//
		// Exactly one shipped shot latches (`special-case.txt`'s `Follow`). Every dialogue shot has
		// no `Start` block at all, so all four of its anchors re-resolve every think — which is why
		// `jack.txt`'s head-bone target tracks the idle, and why its 10-degree `AngularTolerance`
		// deadband is the thing that actually holds the camera still.
		FElysiumCameraShot Shot;
		if (Resolve(World, Entry.Def, Entry.Subject, Shot, &Entry.Bindings,
			EElysiumShotResolvePass::Think))
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
