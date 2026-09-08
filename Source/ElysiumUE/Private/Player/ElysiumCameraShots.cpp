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

	// `"[40, -10, 25]"` -> a vector — `FUN_10071cd0`, reproduced as the listing writes it.
	//
	// It is **three ordered scans over a running remainder**, not "take any three numbers":
	//
	//     x = y = z = 0                                     (0x10071ce6-0x10071cf6)
	//     p = strstr(s, "[");  if (p) { s = p+1; x = atof(s); }   (0x10071d04, sep 0x10547308)
	//     p = strstr(s, ",");  if (p) { s = p+1; y = atof(s); }   (0x10071d36, sep 0x10547304)
	//     p = strstr(s, ",");  if (p) { s = p+1; z = atof(s); }   (0x10071d68)
	//
	// Each `atof` runs on the remainder **after** its separator, a miss leaves that one component at
	// 0 without stopping the scan (a bracketless `"1,2,3"` therefore reads as `(0, 2, 3)`), and a
	// truncated `"[-60,"` reads as `(-60, 0, 0)` rather than failing.
	//
	// That truncation is shipped content, not a hypothetical: `npcfollowmove.txt` authors
	// `"OffsetOrigin"  [-60, 0, 72]` **unquoted** on both its `Start` and its `End`, and both
	// tokenizers end a bare run at the first whitespace, so the value the parse ever sees is the
	// token `[-60,`. Retail frames those anchors 60 u (152.4 cm) behind the eye; the port's old
	// "three numbers or nothing" reading dropped the offset entirely.
	//
	// (Retail copies through a 0x30-byte scratch buffer at each step, so a value 48 characters or
	// longer is truncated. Nothing in the corpus is a third that long, and the copy is not
	// reproduced.)
	//
	// The return says only "the key carried a value", which is what lets the caller fall through from
	// `OffsetOrigin` to the how-to's `Offset`; retail reaches the same place through the literal
	// `"[0, 0, 0]"` default it hands `GetString` (`0x1007201d`).
	bool ParseBracketVector(const FString& Raw, FVector& Out)
	{
		if (Raw.IsEmpty())
		{
			return false;
		}
		static const TCHAR* const Separators[3] = { TEXT("["), TEXT(","), TEXT(",") };
		float Components[3] = { 0.0f, 0.0f, 0.0f };
		const TCHAR* Cursor = *Raw;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (const TCHAR* Hit = FCString::Strstr(Cursor, Separators[Index]))
			{
				Cursor = Hit + 1;
				Components[Index] = FCString::Atof(Cursor);
			}
		}
		Out = FVector(Components[0], Components[1], Components[2]);
		return true;
	}

	// A case-only mismatch against one of a key's retail spellings, for the parse warning. Retail's
	// comparers on this path are all case-**sensitive**, so a mis-cased value is silently something
	// else; the warning is what makes that visible, and it names the spelling retail wanted and the
	// value retail actually reads.
	template <int32 N>
	const TCHAR* CaseOnlyMiss(const FString& Value, const TCHAR* const (&Spellings)[N])
	{
		for (const TCHAR* Spelling : Spellings)
		{
			if (Value.Equals(Spelling, ESearchCase::IgnoreCase))
			{
				return Spelling;
			}
		}
		return nullptr;
	}

	// `Position` — retail's `_strstr` chain in `FUN_10071e00` (`0x10071e1d`-`0x10071eaf`), verbatim:
	// five calls to the CRT `_strstr` (`0x10431510`) in the order `Player` `0x1` -> `DialogTarget`
	// `0x2` -> `GrappleVictim` `0x80000` -> `GrappleAttacker` `0x100000` -> `Named` `0x8`, else
	// `World` `0x4`.
	//
	// **A case-sensitive SUBSTRING test, not an equality test.** `"PlayerEye"` is `Player` because
	// the substring hits; `"player"` is **not**, and falls all the way through to `World`. The order
	// is what decides an ambiguous value, which is why it is written out rather than sorted. This is
	// the same leniency M3 reversed for `AttachType`, in the opposite direction.
	//
	// Corpus: all 156 shipped `Position` values are canonical spellings, so no shipped file changes
	// either way — the fix is for the grammar, not for a shot.
	//
	// **There is no "the value is an entity name" arm.** The how-to's second reading of `Named` —
	// "the name of an entity in the map" — is documentation, not grammar: retail raises the `Named`
	// bit off the keyword alone and never reads a name from the record. An unrecognised value falls
	// through to `World`, which is also the default, and the warning names the token so the fall is
	// visible instead of silent.
	EElysiumShotPosition ParsePosition(const FString& Raw, const TCHAR* ShotName)
	{
		const FString V = Raw.TrimStartAndEnd();
		if (V.Contains(TEXT("Player"), ESearchCase::CaseSensitive))          { return EElysiumShotPosition::Player; }
		if (V.Contains(TEXT("DialogTarget"), ESearchCase::CaseSensitive))    { return EElysiumShotPosition::DialogTarget; }
		if (V.Contains(TEXT("GrappleVictim"), ESearchCase::CaseSensitive))   { return EElysiumShotPosition::GrappleVictim; }
		if (V.Contains(TEXT("GrappleAttacker"), ESearchCase::CaseSensitive)) { return EElysiumShotPosition::GrappleAttacker; }
		if (V.Contains(TEXT("Named"), ESearchCase::CaseSensitive))           { return EElysiumShotPosition::Named; }
		if (V.Contains(TEXT("World"), ESearchCase::CaseSensitive) || V.IsEmpty())
		{
			// `World` has no arm of its own — it is the fallthrough — but a file that spells it out
			// means it, and saying so here keeps the warning below for values that really are
			// unknown.
			return EElysiumShotPosition::World;
		}
		static const TCHAR* const Spellings[] = { TEXT("Player"), TEXT("DialogTarget"),
			TEXT("GrappleVictim"), TEXT("GrappleAttacker"), TEXT("Named"), TEXT("World") };
		if (const TCHAR* Miss = CaseOnlyMiss(V, Spellings))
		{
			UE_LOG(LogElysiumCamShots, Warning,
				TEXT("camera shot '%s': Position '%s' differs from retail's '%s' only in case, and ")
				TEXT("retail's _strstr chain is case-sensitive -- it falls through to World, so this ")
				TEXT("anchor resolves to the world origin"),
				ShotName, *V, Miss);
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
		if (const TCHAR* Miss = CaseOnlyMiss(V, Spellings))
		{
			UE_LOG(LogElysiumCamShots, Warning,
				TEXT("camera shot '%s': AttachType '%s' differs from retail's '%s' only in case, ")
				TEXT("and retail's compare is case-sensitive -- it reads the value as None, so ")
				TEXT("this anchor latches at shot start instead of following"),
				ShotName, *V, Miss);
		}
		return EElysiumShotAttach::None;
	}

	// `AttachPos` — retail's `_strstr` chain (`0x10071ec6`-`0x10071fb7`), verbatim and in its order:
	// `Bone:` `0x200` -> `Attachment:` `0x400` -> `Center` `0x20` -> `EyePosition` `0x40` -> `Top`
	// `0x100` -> `Bottom` `0x80` -> `AbsMin` `0x800` -> `AbsMax` `0x1000`, else `Origin` `0x10`.
	// **Case-sensitive substrings**, like `Position` and unlike nothing else on this path, so
	// `"center"` is `Origin` and a compound value takes the first arm that hits.
	//
	// `Bone:` and `Attachment:` carry an inline name, and retail takes it from `value + 5` /
	// `value + 11` (`0x10071edd`, `0x10071f0c`) — the **value**, not the `_strstr` hit — which is
	// what the `Mid(5)` / `Mid(11)` below is.
	//
	// Corpus: all 156 shipped `AttachPos` values are canonical, so nothing shipped changes.
	EElysiumShotAttachPos ParseAttachPos(const FString& Raw, const TCHAR* ShotName, FString& OutName)
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
		if (V.Contains(TEXT("Bone:"), ESearchCase::CaseSensitive))
		{
			TakeName(V, 5);
			return EElysiumShotAttachPos::Bone;
		}
		if (V.Contains(TEXT("Attachment:"), ESearchCase::CaseSensitive))
		{
			TakeName(V, 11);
			return EElysiumShotAttachPos::Attachment;
		}
		if (V.Contains(TEXT("Center"), ESearchCase::CaseSensitive))      { return EElysiumShotAttachPos::Center; }
		if (V.Contains(TEXT("EyePosition"), ESearchCase::CaseSensitive)) { return EElysiumShotAttachPos::EyePosition; }
		if (V.Contains(TEXT("Top"), ESearchCase::CaseSensitive))         { return EElysiumShotAttachPos::Top; }
		if (V.Contains(TEXT("Bottom"), ESearchCase::CaseSensitive))      { return EElysiumShotAttachPos::Bottom; }
		if (V.Contains(TEXT("AbsMin"), ESearchCase::CaseSensitive))      { return EElysiumShotAttachPos::AbsMin; }
		if (V.Contains(TEXT("AbsMax"), ESearchCase::CaseSensitive))      { return EElysiumShotAttachPos::AbsMax; }
		// `Origin` is retail's fallthrough and its parse default, so a file that spells it out and a
		// file that omits the key land in the same place; only a value that is neither warns.
		if (!V.Contains(TEXT("Origin"), ESearchCase::CaseSensitive) && !V.IsEmpty())
		{
			static const TCHAR* const Spellings[] = { TEXT("Bone:"), TEXT("Attachment:"),
				TEXT("Center"), TEXT("EyePosition"), TEXT("Top"), TEXT("Bottom"), TEXT("AbsMin"),
				TEXT("AbsMax"), TEXT("Origin") };
			if (const TCHAR* Miss = CaseOnlyMiss(V, Spellings))
			{
				UE_LOG(LogElysiumCamShots, Warning,
					TEXT("camera shot '%s': AttachPos '%s' differs from retail's '%s' only in case, ")
					TEXT("and retail's _strstr chain is case-sensitive -- it falls through to Origin, ")
					TEXT("so this anchor samples the entity's own origin"),
					ShotName, *V, Miss);
			}
			else
			{
				UE_LOG(LogElysiumCamShots, Warning,
					TEXT("camera shot '%s': AttachPos '%s' is not a keyword; retail's _strstr chain ")
					TEXT("falls through to Origin"),
					ShotName, *V);
			}
		}
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
		Out.AttachPoint = ParseAttachPos(Out.AttachPos, ShotName, Out.AttachPointName);
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

void ElysiumCameraShots::InstallMiss(const FString& ShotFile)
{
	const FString Key = NormalizeKey(ShotFile);
	if (Key.IsEmpty())
	{
		return;
	}
	// The remembered-miss representation is `LoadFile`'s own: a null list under the key, which is
	// what it writes for a file that does not open. Seeding it here makes `Load`/`LoadNamed` take
	// the identical early-out without a disk read.
	Cache().Add(Key, TSharedPtr<FShotList>());
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
	// **The retail test is one term, not two.** `FUN_1006f010` tests only `anchor[0].flags & 0x2000`
	// (`None`); the record's `Start`-present bit is tested by the *caller* `FUN_1006e8e0`, and only
	// for its own index-0 call. The two-term formula below is equivalent because a shot with no
	// `Start` block leaves anchor 0 **zeroed**, so the `0x2000` bit is clear and all four anchors
	// re-resolve every think — which is why `jack.txt`'s head-bone target tracks and why its
	// 10-degree `AngularTolerance` is what actually holds the shot still. A `Start` block with no
	// `AttachType` key parses to `None` and does latch, because `None` is retail's parse default too.
	return Def.Start.bPresent && Def.Start.Attach == EElysiumShotAttach::None;
}

FBox ElysiumCameraShots::SurroundingBounds(const FElysiumEntity& Entity)
{
	if (const USkeletalMeshComponent* Body = Entity.GetSkeletalBody())
	{
		const FBoxSphereBounds Bounds = Body->Bounds;
		return FBox(Bounds.Origin - Bounds.BoxExtent, Bounds.Origin + Bounds.BoxExtent);
	}
	// A placed prop keeps its box on the embodiment rather than on a skeletal body; this is the same
	// box retail's held-use maintenance measures reach against.
	const IElysiumEmbodiment* Embodiment = Entity.World ? Entity.World->Embodiment() : nullptr;
	FBox Box(ForceInit);
	if (Embodiment && Embodiment->GetUseBodyWorldBounds(Entity.Handle, Box) && Box.IsValid)
	{
		return Box;
	}
	// Nothing to measure. The stand-in is VtMB's own standing hull on the entity's origin, so a
	// bodiless conversation partner still frames the way a bodied one does; retail's answer here is
	// whatever the entity's collision prop holds and is not recoverable from a body-less port state.
	// **One stand-in, not two**: the mode-3 follow think used to fall back to the raw origin here,
	// which put its camera a hull-height below every `Center` anchor on the same entity.
	constexpr float StandingHullHeight = 72.0f * ElysiumCam::U;
	return FBox(Entity.Origin, Entity.Origin + FVector(0.0f, 0.0f, StandingHullHeight));
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
			// **Retail is `subject + 0xFE8`** — `piVar9[0x3fa]`, the subject's own dialogue-partner
			// EHANDLE — and the arm additionally requires `UTIL_PlayerByIndex(1)` to be non-NULL
			// (`FUN_1006e130` arm 2). The subject itself is `param_3 ? param_3 : UTIL_PlayerByIndex(1)`,
			// and **every** shipped `SetShot` caller passes a NULL `param_3` (`FUN_10070470`,
			// `FUN_1017d020`, `FUN_100705d0`, `FUN_10070690`, `FUN_1006e4c0`) while the one that does
			// pass one, `FUN_10070780`, passes the activator, which is always the player. So on every
			// shipped path this anchor is **the player's dialogue partner**: the NPC being talked to.
			//
			// **Wired to the partner (SC9).** `FElysiumEntityWorld::GetOpenDialogOwner()` IS
			// `player+0xFE8` — the world holds one session and the player is always its listener —
			// so the anchor now answers the same entity retail's `piVar9[0x3fa]` does, on every path
			// and not only on a dialogue shot. With no conversation open it answers **NULL**, exactly
			// as retail's dead EHANDLE does, and the resolve falls through to the world-origin tail;
			// that is what a `DialogTarget` shot fired outside a conversation frames in retail too.
			//
			// The subject is no longer consulted here, which is also retail: `FUN_1017d020`,
			// `FUN_10070470`, `FUN_100705d0`, `FUN_10070690` and `FUN_1006e4c0` all pass a NULL
			// `param_3`, so `subject == UTIL_PlayerByIndex(1)` and `subject+0xFE8` is the player's
			// own partner however the shot was created.
			return World->FindPlayer() != nullptr
				? World->Resolve(World->GetOpenDialogOwner()) : nullptr;
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

	// The bounds `FUN_1006f080` reads once at the top and every `AttachPos` arm then indexes into.
	// The measurement itself is `ElysiumCameraShots::SurroundingBounds`, because the mode-3 follow
	// think publishes the same `WorldSpaceCenter()` off the same box.
	struct FAnchorBounds
	{
		FVector Min = FVector::ZeroVector;
		FVector Max = FVector::ZeroVector;

		FVector Center() const { return (Min + Max) * 0.5f; }
	};

	FAnchorBounds SurroundingBounds(const FElysiumEntity& Entity)
	{
		const FBox Box = ElysiumCameraShots::SurroundingBounds(Entity);
		FAnchorBounds Out;
		Out.Min = Box.Min;
		Out.Max = Box.Max;
		return Out;
	}

	// One `AttachPos` sample: the point, and the rotation basis the `Follow` offset step turns in.
	struct FAnchorSample
	{
		FVector Point = FVector::ZeroVector;
		FRotator Basis = FRotator::ZeroRotator;

		// Retail's early `return` out of `FUN_1006f080` — the two arms that answer `vec3_origin` and
		// leave the function without ever reaching `LAB_1006f430`, so the anchor's `OffsetOrigin` is
		// not applied on top of it.
		bool bSkipOffset = false;
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
			// The index was resolved once, at bind time. **An unresolved seam answers the WORLD
			// origin and skips the offset step**, which is retail: the `0x200` arm at `0x1006f32e`
			// (and its `0x400` twin at `0x1006f3af`) calls `GetBaseAnimating` (vfunc `0x224`) and, on
			// NULL, writes `vec3_origin` into the out vector and **returns** — never reaching
			// `LAB_1006f430`, so the anchor's `OffsetOrigin` is not added either. That is the
			// terminal case: `funcmonitor.txt` / `hackcam.txt` bind `Attachment: screen` /
			// `screen_axis` to brush-and-prop monitors, which do not animate.
			//
			// The port answered the entity's abs origin here, which is a different world position for
			// every such anchor. What remains unrecovered is only the narrower case of a **live
			// animating** entity whose `LookupBone` / `LookupAttachment` returned `-1`; the port
			// cannot tell the two apart and answers retail's non-animating arm for both.
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
			Out.Point = FVector::ZeroVector;
			Out.bSkipOffset = true;
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
		const FElysiumEntity* Ent = World ? World->Resolve(Entity) : nullptr;
		if (!Ent)
		{
			// `if (ent == NULL) { this->+0x610[i] = 0xffffffff; return; }` — the handle is cleared and
			// `+0x620` is **not written**, so the index keeps whatever it last held.
			Binding.Entity = FElysiumEntityHandle::Invalid();
			return;
		}
		// **`+0x620` is written on the `0x200` / `0x400` arms only** (`FUN_1006ef50`): an anchor that
		// is neither `Bone:` nor `Attachment:`, and an entity with no animating half, both leave the
		// index **stale** across shot changes rather than resetting it to `-1`. Nothing reads it on
		// those anchors — `AttachPoint`'s other arms never touch it — so the behaviour is identical
		// either way; it is written this way because that is what the listing does.
		if (Anchor.NeedsAttachPointIndex())
		{
			Binding.PointIndex = LookupAttachPointIndex(*Ent, Anchor);
		}
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

void FElysiumCameraDirector::BindAnchorEntity(FElysiumEntityWorld* World,
	const FElysiumCameraShotDef& Def, int32 AnchorIndex, const FElysiumEntityHandle& Entity,
	FElysiumShotBindings& Bindings)
{
	if (AnchorIndex < 0 || AnchorIndex >= FElysiumShotBindings::Num)
	{
		return;
	}
	const FElysiumShotAnchor* const Anchors[FElysiumShotBindings::Num] =
		{ &Def.Start, &Def.End, &Def.Target1, &Def.Target2 };
	FElysiumShotAnchorBinding& Binding = Bindings.Anchors[AnchorIndex];
	ElysiumCameraShotsImpl::BindAnchor(World, *Anchors[AnchorIndex], Entity, Binding);
	// A newly supplied entity re-opens the shot-start cache: the anchor has not been sampled
	// against this one yet. Retail reaches the same state because the caller that fills a `Named`
	// slot does it inside `SetShot`, before `FUN_1006e8e0` writes the cache.
	Binding.bCached = false;
}

bool FElysiumCameraDirector::ResolveAnchorPoint(FElysiumEntityWorld* World,
	const FElysiumCameraShotDef& Def, int32 AnchorIndex, FElysiumShotBindings& Bindings,
	bool bLatched, FVector& OutPoint)
{
	if (AnchorIndex < 0 || AnchorIndex >= FElysiumShotBindings::Num)
	{
		return false;
	}
	const FElysiumShotAnchor* const Anchors[FElysiumShotBindings::Num] =
		{ &Def.Start, &Def.End, &Def.Target1, &Def.Target2 };
	return ResolveAnchor(World, *Anchors[AnchorIndex], Bindings.Anchors[AnchorIndex], bLatched,
		OutPoint);
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
		// **A dead or unbound anchor is the world origin, and the caller cannot tell.**
		// `FUN_1006f080` opens by resolving the anchor's EHANDLE at `+0x610 + i*4` and, on a miss,
		// writes `vec3_origin` into the out vector and returns at `0x1006f09b`. That is one of the
		// function's two `vec3_origin` early returns — the other being the non-animating `Bone:` /
		// `Attachment:` arm below — so `LAB_1006f430` never runs and the anchor's `OffsetOrigin` is
		// **not** added on top of it either.
		//
		// The shot-start cache reaches the same value by its own road: `FUN_1006e8e0`'s fill loop
		// tests each of the four handles itself and writes `vec3_origin` into `+0x598 + i*12`
		// without calling `FUN_1006f080` at all, so a latched shot holds the origin too.
		//
		// So a `DialogTarget` shot fired with no conversation open, or a shot whose anchor entity
		// died, frames the world origin and keeps running: nothing falls back to another anchor,
		// nothing expires, and `SetShot` still succeeds.
		OutPoint = FVector::ZeroVector;
		if (!Binding.bCached)
		{
			Binding.Cached = OutPoint;
			Binding.bCached = true;
		}
		return true;
	}

	const ElysiumCameraShotsImpl::FAnchorSample Sample =
		ElysiumCameraShotsImpl::AttachPoint(*Entity, Anchor, Binding.PointIndex);

	if (Sample.bSkipOffset)
	{
		// `FUN_1006f080`'s two `vec3_origin` early returns: the answer is the world origin and
		// `LAB_1006f430` never runs, so the anchor's `OffsetOrigin` is not applied on top of it.
		OutPoint = Sample.Point;
		if (!Binding.bCached)
		{
			Binding.Cached = OutPoint;
			Binding.bCached = true;
		}
		return true;
	}

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

namespace ElysiumCameraShotsImpl
{
	// `m_ShotIndex`'s source — retail's row in the shot table `&DAT_106c8298`, which `SetShot`
	// (`FUN_1006e130`) looks the normalized name up in and stores at `+0x630`. Retail's table is
	// filled by the parse, so a row's index is stable for the run and two different records never
	// share one; this is that property, and only that property, over the port's per-name parse cache.
	//
	// The identity is the **normalized** name, so `LookAtTarget` and `vdata/CameraShots/lookattarget`
	// are one record here exactly as they are one row there (`SetShot` `Q_FileBase`s a name that
	// carries `.txt` and the table lookup supplies the case-insensitivity).
	int32 ShotTableIndex(const FString& ShotName)
	{
		static TMap<FString, int32> Table;
		static int32 Next = 0;
		if (ShotName.IsEmpty())
		{
			// Retail's "unknown shot" answer: `SetShot` returns 0 with `m_ShotIndex` still `-1`.
			return INDEX_NONE;
		}
		const FString Key = ElysiumCameraShots::NormalizeKey(ShotName);
		if (const int32* Found = Table.Find(Key))
		{
			return *Found;
		}
		return Table.Add(Key, Next++);
	}
}

void FElysiumCameraDirector::FillShotStartCache(FElysiumEntityWorld* World,
	const FElysiumCameraShotDef& Def, FElysiumShotBindings& Bindings)
{
	const FElysiumShotAnchor* const Anchors[FElysiumShotBindings::Num] =
		{ &Def.Start, &Def.End, &Def.Target1, &Def.Target2 };
	for (int32 Index = 0; Index < FElysiumShotBindings::Num; ++Index)
	{
		FElysiumShotAnchorBinding& Binding = Bindings.Anchors[Index];
		// `handleLive(+0x584 + i)` else `vec3_origin` — and an anchor the record does not declare
		// has no handle either, so it caches the origin too. `ResolveAnchor` leaves the zero in
		// place on both roads (an absent block returns false; a dead handle writes `vec3_origin`
		// with the offset **not** applied, `0x1006f09b`).
		FVector Point = FVector::ZeroVector;
		ResolveAnchor(World, *Anchors[Index], Binding, /*bLatched*/ false, Point);
		// **Unconditional.** The retail loop has no "already cached" test; this is the write that
		// makes a re-shot re-latch.
		Binding.Cached = Point;
		Binding.bCached = true;
	}
}

bool FElysiumCameraDirector::Resolve(FElysiumEntityWorld* World, const FElysiumCameraShotDef& Def,
	const FElysiumEntityHandle& Subject, FElysiumCameraShot& Out,
	FElysiumShotBindings* Bindings, EElysiumShotResolvePass Pass,
	EElysiumShotOriginSelector OriginSelector, const FElysiumShotEntityPose* EntityPose)
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

	// The shot-start pass **is** `FUN_1006e8e0`'s fill loop, which rewrites all four cache slots on
	// every shot start. It runs before the solve below rather than after it only because the port
	// has one resolve where retail has a placement and a fill; the values are identical either way,
	// since the shot-start pass never latches and re-resolves each anchor live.
	if (Pass == EElysiumShotResolvePass::ShotStart)
	{
		FillShotStartCache(World, Def, *Bindings);
	}

	// The origin selector `+0x594`, the mode-1 think's own order (`FUN_1006f8f0`): `sel == 1`
	// publishes anchor 0 (`Start`), `sel == 0` publishes anchor 1 (`End`), and the selected anchor is
	// published **unconditionally** — `FUN_1006f010` always writes, `vec3_origin` included. There is
	// no "try the other anchor" arm anywhere in the listing, and there is nothing left to try one
	// for: a dead or unbound anchor now answers the world origin, as retail's does.
	//
	// The selector's third value leaves the entity's own abs origin alone; the abs-origin source is
	// the director entity's transform, so `FElysiumCameraCinematic` applies that arm after this
	// returns and only the `AutoPositionFromTarget` suppression below is in force here.
	const bool bStartFirst = OriginSelector == EElysiumShotOriginSelector::StartAnchor;
	const FElysiumShotAnchor& OriginAnchor = bStartFirst ? Def.Start : Def.End;
	FElysiumShotAnchorBinding& OriginBinding = Bindings->Anchors[bStartFirst ? 0 : 1];

	FVector Origin = FVector::ZeroVector;
	if (!ResolveAnchor(World, OriginAnchor, OriginBinding, bLatched, Origin))
	{
		// The selected anchor has **no block at all**, which is a state retail's selector never
		// points at: its one writer (`FUN_1006e8e0`) sets `sel = 0` only when the `End` handle is
		// live, so an `End`-less shot keeps the constructed `2` and its origin is the entity's abs
		// origin — the pose shot start just put there, which for a `Start` shot is anchor 0's own
		// point (arm A). That is the how-to's "If there is no End position specified, the camera
		// will not move between the points", and it is why `kilpatrick.txt`, `center.txt` and
		// `stealth_kill.txt` ship without an `End`.
		//
		// A caller with a camera entity behind it (`ThinkNamedShot`) applies that arm itself. This
		// is the stand-in for the callers that have none — the dialogue ladder resolves a shot file
		// as pure values — and it answers the same world position at shot start by reading the
		// other anchor directly. It is reached only for an absent block, never for a miss.
		const FElysiumShotAnchor& EntityStandIn = bStartFirst ? Def.End : Def.Start;
		FElysiumShotAnchorBinding& StandInBinding = Bindings->Anchors[bStartFirst ? 1 : 0];
		ResolveAnchor(World, EntityStandIn, StandInBinding, bLatched, Origin);
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
	// **`AutoPositionFromTarget`** (`flags & 0x20`), the mode-1 think's block at `0x1006fa50`. It runs
	// **after** the origin selector has chosen the origin and the look-at is solved, and **before**
	// anything is published — which is where it sits here — and it is skipped outright on the
	// selector's third arm, because retail's whole `if (sel != 2)` body contains it.
	//
	// It reads the two `Target` **slots**, not the presence flags: retail resolves `anchorPos(2)` and
	// `anchorPos(3)` directly, so a `Point2`-only shot (the order-of-presence flag bug) frames against
	// the slot it actually filled even while its look-at reads the empty one.
	if (Def.Constraints.bAutoPositionFromTarget
		&& OriginSelector != EElysiumShotOriginSelector::Entity)
	{
		Origin = ElysiumCam::AutoPositionFromTarget(Origin, Look, Point1, Point2,
			Def.Constraints.FieldOfView);
	}

	// `if (rec->+0xd4 > 0)` — the mode-1 think derives the angle from the look-at only when the shot
	// authored a `Target` block at all; one without keeps the entity's own abs angles.
	const bool bHasTarget = Def.TargetPointCount > 0;

	// **`m_angCamAngles`, published every tick and as a real field of the goal.** `0x1006f8f0` seeds
	// `fStack_24..1c` from `GetAbsAngles()` (vfunc `0x36c`) unconditionally, replaces them with
	// `VectorAngles(lookAt - GetOrigin())` under the `+0xd4` gate — `GetOrigin()` is vfunc `0x370`,
	// the entity's **local** transform, i.e. the shot start's placement and *not* the origin the
	// selector just chose — and writes the triple to `param_1[0x181..0x183]`. A `Target`-less shot
	// therefore publishes the entity's abs angles every 24 Hz tick rather than nothing, and an
	// `End`-driven `Start` shot publishes an angle measured from a point its own origin is not at.
	//
	// With no camera entity behind the call the measuring point is the solved origin and the
	// `+0xd4 == 0` answer is a zero rotation, which is the state a value producer already carries.
	const FVector AngleOrigin = EntityPose ? EntityPose->Origin : Origin;
	const FRotator PublishedAngles = bHasTarget
		? (Look - AngleOrigin).Rotation()
		: (EntityPose ? EntityPose->Angles : FRotator::ZeroRotator);

	Out = FElysiumCameraShot();
	Out.DebugName = Def.Name;
	Out.Origin = Origin;
	Out.bUseLookAt = bHasTarget;
	Out.LookAt = Look;
	Out.Rotation = PublishedAngles;
	// This resolve **is** the server publish, so the copy-through readers take the triple straight
	// rather than re-deriving it (`FElysiumCameraShot::bAnglesPublished`).
	Out.bAnglesPublished = true;
	// The record's presence flags and its `+0xd4` count travel with the shot: shot start's arm test
	// (`(flags & 2) == 0 || (flags & 1) != 0`) and the `+0xd4` angle gate are both client-side reads
	// of the record, and the tracker has no other way to see them.
	Out.bHasStartAnchor = Def.Start.bPresent;
	Out.bHasEndAnchor = Def.End.bPresent;
	Out.bTargetPoint1Flagged = Def.bTargetPoint1Flagged;
	Out.bTargetPoint2Flagged = Def.bTargetPoint2Flagged;
	Out.TargetPointCount = Def.TargetPointCount;
	// `m_ShotIndex` — the row this record occupies in retail's shot table (`&DAT_106c8298`), which is
	// what `OnDataChanged` compares. The port's table is a per-name parse cache, so the index is
	// assigned per distinct shot name on first sight; the only property either side relies on is that
	// the same record answers the same index and a different record a different one.
	Out.ShotIndex = ElysiumCameraShotsImpl::ShotTableIndex(Def.Name);
	Out.OriginSelector = OriginSelector;
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
	// `m_bDrawPlayer` is deliberately **not** written here: it is not a shot key. It belongs to the
	// director entity (`spawnflags & 2`, copied onto the runtime camera by `FUN_10070780`) and to anim
	// event 4050, so the pusher stamps it onto the resolved shot after this returns.
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
	// `SetShotAnchorEntity` loop, then `FUN_1006e8e0`), and the placement reads them back.
	FLiveShot& Entry = Live.AddDefaulted_GetRef();
	Entry.Id = NextId++;
	Entry.bValue = false;
	Entry.Def = *Def;
	Entry.Subject = Subject;
	BindAnchors(World, Entry.Def, Subject, Entry.Bindings);

	// **A shot is refused only by the table lookup above.** `SetShot` (`FUN_1006e130`) returns false
	// on a name that is not in `&DAT_106c8298` and on a NULL subject, and on nothing else: the
	// anchor loop stores whatever each `Position` resolved to — NULL included — and `FUN_1006e8e0`
	// then places the camera from handles it never re-tests. A shot whose anchors are all dead runs,
	// framing the world origin.
	FElysiumCameraShot Shot;
	Resolve(World, Entry.Def, Subject, Shot, &Entry.Bindings, EElysiumShotResolvePass::ShotStart);
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

	// Same as `Push`: the `LoadNamed` miss above is the only refusal `SetShot` has.
	FElysiumCameraShot Shot;
	Resolve(World, Entry.Def, Subject, Shot, &Entry.Bindings, EElysiumShotResolvePass::ShotStart);
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

bool FElysiumCameraDirector::RestartValue(UElysiumCameraComponent* Camera, int32 Id)
{
	const FLiveShot* Entry = Live.FindByPredicate([Id](const FLiveShot& S) { return S.Id == Id; });
	return Entry && Camera && Camera->RestartShot(Entry->CameraShotId);
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
