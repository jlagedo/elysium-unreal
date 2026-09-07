// The whole shipped shot corpus, read against the port's grammar.
//
// `vdata/camerashots/` is 66 authored files and the port is a VM host for them, so an
// unimplemented keyword is not a gap to discover later — it is a shot that silently frames the
// wrong thing. This test walks every file, feeds every `Position` / `AttachPos` / `AttachType`
// token the corpus writes back through the port's own parser, and fails loudly on one that does not
// round-trip. The census beside it is the evidence behind several of `camera_scripted.md`'s
// rulings, so the numbers are asserted rather than described.
//
// Self-abstains when the corpus is not mounted (`Elysium.Content.*` convention).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCameraSolve.h"
#include "ElysiumContentPaths.h"
#include "Player/ElysiumCameraShots.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace ElysiumCameraShotGrammarTests
{
static constexpr EAutomationTestFlags GElysiumCameraShotGrammarFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The three keys whose values are a closed grammar, and the other keys a value line can carry —
// needed because the shipped how-to writes its alternatives as bare quoted tokens on continuation
// lines under the key they belong to, which is the only place `Top`, `Bottom` and `GrappleTarget`
// appear at all.
bool IsGrammarKey(const FString& Key)
{
	return Key.Equals(TEXT("Position"), ESearchCase::IgnoreCase)
		|| Key.Equals(TEXT("AttachPos"), ESearchCase::IgnoreCase)
		|| Key.Equals(TEXT("AttachType"), ESearchCase::IgnoreCase);
}

bool IsOtherKey(const FString& Key)
{
	static const TCHAR* const Keys[] = {
		TEXT("Offset"), TEXT("OffsetOrigin"), TEXT("OffsetOrig"), TEXT("OffsetAngle"),
		TEXT("OffsetAngles"), TEXT("MoveSpeed"), TEXT("MoveAccel"), TEXT("TurnAccel"),
		TEXT("MaxTurnRate"), TEXT("DistanceTolerance"), TEXT("AngularTolerance"),
		TEXT("FieldOfView"), TEXT("DialogPOV"), TEXT("AutoPositionFromTarget"),
		TEXT("SyncRotateOnMove"), TEXT("SnapOnShotChange"), TEXT("ShowHud"), TEXT("DrawViewmodel"),
	};
	for (const TCHAR* Candidate : Keys)
	{
		if (Key.Equals(Candidate, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

// Every quoted token on one line, in order.
void QuotedTokens(const FString& Line, TArray<FString>& Out)
{
	Out.Reset();
	int32 Cursor = 0;
	while (Cursor < Line.Len())
	{
		const int32 Open =
			Line.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor);
		if (Open == INDEX_NONE)
		{
			return;
		}
		const int32 Close =
			Line.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open + 1);
		if (Close == INDEX_NONE)
		{
			return;
		}
		Out.Add(Line.Mid(Open + 1, Close - Open - 1).TrimStartAndEnd());
		Cursor = Close + 1;
	}
}

// One token the corpus writes for one of the three grammar keys.
struct FTokenUse
{
	FString Key;
	FString Value;
	FString File;
};

// Walk one file's lines and collect every grammar-key value, comments included: the how-to's
// keyword catalogue is all comment, and it is the only witness for `Top`, `Bottom` and
// `GrappleTarget`.
void CollectTokens(const FString& Leaf, const FString& Text, TArray<FTokenUse>& Out)
{
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);
	FString Current;
	TArray<FString> Tokens;
	for (const FString& Raw : Lines)
	{
		FString Line = Raw.TrimStartAndEnd();
		if (Line.StartsWith(TEXT("//")))
		{
			Line = Line.RightChop(2).TrimStartAndEnd();
		}
		QuotedTokens(Line, Tokens);
		if (Tokens.Num() == 0)
		{
			Current.Reset();
			continue;
		}
		if (Tokens.Num() >= 2)
		{
			// A key/value pair. Anything that is not one of the three closes the run.
			Current = IsGrammarKey(Tokens[0]) ? Tokens[0] : FString();
			if (!Current.IsEmpty())
			{
				Out.Add({ Current, Tokens[1], Leaf });
			}
			continue;
		}
		// A lone quoted token: either the key on its own (the how-to's summary template) or one of
		// the alternatives listed under the key above it.
		if (IsGrammarKey(Tokens[0]))
		{
			Current = Tokens[0];
		}
		else if (IsOtherKey(Tokens[0]))
		{
			Current.Reset();
		}
		else if (!Current.IsEmpty())
		{
			Out.Add({ Current, Tokens[0], Leaf });
		}
	}
}

// Strip `//` comment lines the way the KeyValues reader does, for the "which files WRITE this key"
// census — the how-to documents every key and authors none.
FString StripComments(const FString& Text)
{
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);
	FString Out;
	for (const FString& Line : Lines)
	{
		int32 Slashes = Line.Find(TEXT("//"), ESearchCase::CaseSensitive);
		Out += (Slashes == INDEX_NONE) ? Line : Line.Left(Slashes);
		Out += TEXT("\n");
	}
	return Out;
}

int32 CountKeyWrites(const FString& StrippedText, const TCHAR* Key)
{
	const FString Needle = FString::Printf(TEXT("\"%s\""), Key);
	int32 Count = 0;
	int32 Cursor = 0;
	while (true)
	{
		const int32 Hit = StrippedText.Find(Needle, ESearchCase::IgnoreCase, ESearchDir::FromStart, Cursor);
		if (Hit == INDEX_NONE)
		{
			break;
		}
		++Count;
		Cursor = Hit + Needle.Len();
	}
	return Count;
}

// Feed one token back through the port's own parser and read the enum it produced. This is the
// resolver test: a token that does not round-trip is a keyword the port answers differently from
// the file that wrote it.
FString RoundTrip(const FString& Key, const FString& Value)
{
	const FString Text = FString::Printf(
		TEXT("CameraShotTable { RoundTrip { End { \"%s\" \"%s\" } } }"), *Key, *Value);
	FElysiumCameraShotDef Def;
	if (!ElysiumCameraShots::ParseText(Text, Def))
	{
		return FString();
	}
	if (Key.Equals(TEXT("Position"), ESearchCase::IgnoreCase))
	{
		return ElysiumCameraShots::LexToString(Def.End.Position);
	}
	if (Key.Equals(TEXT("AttachPos"), ESearchCase::IgnoreCase))
	{
		return ElysiumCameraShots::LexToString(Def.End.AttachPoint);
	}
	return ElysiumCameraShots::LexToString(Def.End.Attach);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraShotGrammarTest, "Elysium.Content.CameraShotGrammar",
	ElysiumCameraShotGrammarTests::GElysiumCameraShotGrammarFlags)

bool FElysiumCameraShotGrammarTest::RunTest(const FString&)
{
	using namespace ElysiumCameraShotGrammarTests;

	const FString Dir = FElysiumContentPaths::VdataDir() / TEXT("camerashots");
	TArray<FString> Leaves;
	IFileManager::Get().FindFiles(Leaves, *(Dir / TEXT("*.txt")), /*Files*/ true, /*Dirs*/ false);
	if (Leaves.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: vdata/camerashots is not mounted; the shot grammar was not sampled"));
		return true;
	}
	Leaves.Sort();

	// The how-to's keyword catalogue lists `GrappleTarget`, which the port implemented and retail
	// never did: retail's `_strstr` chain has no such arm and falls through to `World`. Feeding the
	// token back through the parser is what proves it, and the warning is that proof's output.
	AddExpectedError(TEXT("is not a keyword"), EAutomationExpectedErrorFlags::Contains, 0);

	ElysiumCameraShots::FlushCache();

	TArray<FTokenUse> Tokens;
	TArray<FElysiumCameraShotDef> AllShots;
	TMap<FString, int32> WrittenKeys;
	TSet<FString> FilesWithStart;
	TArray<FString> LatchingShots;
	TArray<FString> NaNBandShots;
	int32 SpecialCaseShots = 0;
	bool bDeathCam = false;
	bool bAnimated = false;
	// `npcfollowmove.txt`'s two unquoted `OffsetOrigin` values — the one place in the corpus where
	// the bracket-vector parser's exact shape is observable. Seeded with a sentinel so a file that
	// stopped shipping fails the assertion rather than passing vacuously.
	FVector FollowMoveStartOffset(-1.0e9);
	FVector FollowMoveEndOffset(-1.0e9);

	static const TCHAR* const CensusKeys[] = { TEXT("DialogPOV"), TEXT("SyncRotateOnMove"),
		TEXT("SnapOnShotChange"), TEXT("AutoPositionFromTarget"), TEXT("ShowHud"),
		TEXT("DrawViewmodel") };
	TMap<FString, int32> SetToOne;
	for (const TCHAR* Key : CensusKeys)
	{
		WrittenKeys.Add(Key, 0);
		SetToOne.Add(Key, 0);
	}

	for (const FString& Leaf : Leaves)
	{
		FString Text;
		if (!TestTrue(FString::Printf(TEXT("%s reads"), *Leaf),
			FFileHelper::LoadFileToString(Text, *(Dir / Leaf))))
		{
			continue;
		}
		CollectTokens(Leaf, Text, Tokens);

		const FString Stripped = StripComments(Text);
		for (const TCHAR* Key : CensusKeys)
		{
			WrittenKeys[Key] += CountKeyWrites(Stripped, Key);
		}

		TArray<FElysiumCameraShotDef> Shots;
		if (!ElysiumCameraShots::ParseAllText(Text, Shots))
		{
			// The how-to is all comment and carries no shot; every other file must yield one.
			TestTrue(FString::Printf(TEXT("%s is the how-to, the only file with no shot"), *Leaf),
				Leaf.Contains(TEXT("how-to")));
			continue;
		}
		if (Leaf.Equals(TEXT("npcfollowmove.txt"), ESearchCase::IgnoreCase) && Shots.Num() > 0)
		{
			FollowMoveStartOffset = Shots[0].Start.OffsetOrigin;
			FollowMoveEndOffset = Shots[0].End.OffsetOrigin;
		}
		if (Leaf.Equals(TEXT("special-case.txt"), ESearchCase::IgnoreCase))
		{
			SpecialCaseShots = Shots.Num();
			bDeathCam = Shots.ContainsByPredicate([](const FElysiumCameraShotDef& D)
				{ return D.Name.Equals(TEXT("DeathCam"), ESearchCase::IgnoreCase); });
			bAnimated = Shots.ContainsByPredicate([](const FElysiumCameraShotDef& D)
				{ return D.Name.Equals(TEXT("Animated"), ESearchCase::IgnoreCase); });
		}
		for (const FElysiumCameraShotDef& Def : Shots)
		{
			if (Def.Start.bPresent)
			{
				FilesWithStart.Add(Leaf);
			}
			if (ElysiumCameraShots::LatchesAnchors(Def))
			{
				LatchingShots.Add(FString::Printf(TEXT("%s:%s"), *Leaf, *Def.Name));
			}
			if (Def.Constraints.bDialogPOV)              { ++SetToOne[TEXT("DialogPOV")]; }
			if (Def.Constraints.bSyncRotateOnMove)       { ++SetToOne[TEXT("SyncRotateOnMove")]; }
			if (Def.Constraints.bSnapOnShotChange)       { ++SetToOne[TEXT("SnapOnShotChange")]; }
			if (Def.Constraints.bAutoPositionFromTarget) { ++SetToOne[TEXT("AutoPositionFromTarget")]; }
			if (Def.Constraints.bShowHud)                { ++SetToOne[TEXT("ShowHud")]; }
			if (Def.Constraints.bDrawViewmodel)          { ++SetToOne[TEXT("DrawViewmodel")]; }

			// M6's evidence: the `RemainingTime` triangle arm NaNs only when `MoveSpeed >
			// 2 * MoveAccel`, and no shipped `SyncRotateOnMove` shot reaches it — which is what
			// makes clamping the radicand a change to no shipped behaviour.
			if (Def.Constraints.bSyncRotateOnMove
				&& Def.Constraints.MoveSpeed > 2.0f * Def.Constraints.MoveAccel)
			{
				NaNBandShots.Add(FString::Printf(TEXT("%s:%s (MoveSpeed %.0f, MoveAccel %.0f)"),
					*Leaf, *Def.Name, Def.Constraints.MoveSpeed / ElysiumCam::U,
					Def.Constraints.MoveAccel / ElysiumCam::U));
			}
			AllShots.Add(Def);
		}
	}

	TestEqual(TEXT("the shipped shot corpus is 66 files"), Leaves.Num(), 66);
	TestEqual(TEXT("which parse to 72 shots (special-case carries 5, stealth_kill 4)"),
		AllShots.Num(), 72);

	// --- every token has a resolver ---------------------------------------------------------------
	TMap<FString, int32> PositionCensus;
	TMap<FString, int32> AttachPosCensus;
	TMap<FString, int32> AttachTypeCensus;
	TMap<FString, TSet<FString>> TokenFiles;
	TArray<FString> Unresolved;
	for (const FTokenUse& Use : Tokens)
	{
		TMap<FString, int32>& Census = Use.Key.Equals(TEXT("Position"), ESearchCase::IgnoreCase)
			? PositionCensus
			: (Use.Key.Equals(TEXT("AttachPos"), ESearchCase::IgnoreCase)
				? AttachPosCensus : AttachTypeCensus);
		++Census.FindOrAdd(Use.Value);
		TokenFiles.FindOrAdd(Use.Value).Add(Use.File);

		const FString Answer = RoundTrip(Use.Key, Use.Value);
		// `Bone:` / `Attachment:` carry an inline name, so the resolver answers the prefix.
		const bool bRoundTrips = Answer.Equals(Use.Value, ESearchCase::CaseSensitive)
			|| (Answer.EndsWith(TEXT(":")) && Use.Value.StartsWith(Answer, ESearchCase::IgnoreCase));
		if (!bRoundTrips)
		{
			Unresolved.Add(FString::Printf(TEXT("%s %s \"%s\" -> %s"),
				*Use.File, *Use.Key, *Use.Value, *Answer));
		}
	}

	// The ONE expected non-round-trip in the whole corpus, and it is documentation rather than
	// content: the how-to offers `GrappleTarget`, retail's `_strstr` chain does not implement it,
	// and the port now agrees by falling through to `World`. Anything else here is a keyword the
	// port has no resolver for.
	const FString ExpectedFall =
		TEXT("camera shots how-to.txt Position \"GrappleTarget\" -> World");
	TestTrue(TEXT("the how-to's GrappleTarget falls through to World, as retail does"),
		Unresolved.Contains(ExpectedFall));
	Unresolved.Remove(ExpectedFall);
	for (const FString& Miss : Unresolved)
	{
		AddError(FString::Printf(TEXT("no resolver for %s"), *Miss));
	}
	TestEqual(TEXT("every other Position/AttachPos/AttachType token the corpus writes resolves"),
		Unresolved.Num(), 0);

	// --- the one shipped value the bracket parser's exact shape decides ---------------------------
	// `npcfollowmove.txt` authors `"OffsetOrigin"  [-60, 0, 72]` **unquoted** on both its `Start` and
	// its `End`, and every KeyValues tokenizer — retail's and the port's — ends a bare run at the
	// first whitespace, so the value the parse ever sees is the token `[-60,`. Retail's
	// `FUN_10071cd0` reads that as `x = atof("-60,") = -60`, `y = 0` (the comma is the last
	// character, so the remainder is empty), `z = 0` (no third separator), and frames both anchors
	// **60 u behind the eye**. The port's old "three numbers or nothing" reading dropped the offset
	// entirely and framed the shot 152 cm away from where retail frames it.
	{
		const FVector Expected(-60.0f * ElysiumCam::U, 0.0f, 0.0f);
		TestTrue(TEXT("npcfollowmove's Start offset parses to (-60, 0, 0) u, as retail reads it"),
			FollowMoveStartOffset.Equals(Expected, 0.01f));
		TestTrue(TEXT("and its End offset with it"),
			FollowMoveEndOffset.Equals(Expected, 0.01f));
	}

	// --- the grapple keywords ----------------------------------------------------------------------
	TestEqual(TEXT("GrappleAttacker is written 6 times"),
		PositionCensus.FindRef(TEXT("GrappleAttacker")), 6);
	TestEqual(TEXT("GrappleVictim twice"), PositionCensus.FindRef(TEXT("GrappleVictim")), 2);
	for (const TCHAR* Keyword : { TEXT("GrappleAttacker"), TEXT("GrappleVictim") })
	{
		const TSet<FString>* Files = TokenFiles.Find(Keyword);
		TestTrue(FString::Printf(TEXT("%s appears only in stealth_kill.txt"), Keyword),
			Files && Files->Num() == 1 && Files->Contains(TEXT("stealth_kill.txt")));
	}
	const TSet<FString>* GrappleTargetFiles = TokenFiles.Find(TEXT("GrappleTarget"));
	TestTrue(TEXT("GrappleTarget ships nowhere but the how-to"),
		GrappleTargetFiles && GrappleTargetFiles->Num() == 1
			&& GrappleTargetFiles->Contains(TEXT("camera shots how-to.txt")));

	// --- AbsMin / AbsMax ---------------------------------------------------------------------------
	for (const TCHAR* Keyword : { TEXT("AbsMin"), TEXT("AbsMax") })
	{
		TestEqual(FString::Printf(TEXT("%s is written exactly once"), Keyword),
			AttachPosCensus.FindRef(Keyword), 1);
		const TSet<FString>* Files = TokenFiles.Find(Keyword);
		TestTrue(FString::Printf(TEXT("%s is centerfullview.txt's"), Keyword),
			Files && Files->Num() == 1 && Files->Contains(TEXT("centerfullview.txt")));
	}
	// `Top` and `Bottom` are documented and never authored — which is why the port's wrong reading
	// of them went unnoticed until the decompile was read.
	for (const TCHAR* Keyword : { TEXT("Top"), TEXT("Bottom") })
	{
		const TSet<FString>* Files = TokenFiles.Find(Keyword);
		TestTrue(FString::Printf(TEXT("%s appears only in the how-to"), Keyword),
			Files && Files->Num() == 1 && Files->Contains(TEXT("camera shots how-to.txt")));
	}

	// --- `AttachType` is case-exact across the whole corpus ---------------------------------------
	// No shipped file relies on the leniency the port used to have (M3): every value is one of the
	// three retail spellings or the literal `None`, byte for byte.
	for (const TPair<FString, int32>& Pair : AttachTypeCensus)
	{
		TestTrue(FString::Printf(TEXT("AttachType '%s' is spelled exactly as retail compares it"),
			*Pair.Key),
			Pair.Key == TEXT("Follow") || Pair.Key == TEXT("FollowNoAngles")
				|| Pair.Key == TEXT("FollowEntAngles") || Pair.Key == TEXT("None"));
	}

	// --- the `Start` census and the one latching shot ----------------------------------------------
	TestEqual(TEXT("ten files author a Start block"), FilesWithStart.Num(), 10);
	TestEqual(TEXT("and exactly one shot in the corpus latches its anchors"), LatchingShots.Num(), 1);
	if (LatchingShots.Num() == 1)
	{
		TestEqual(TEXT("which is special-case.txt's Follow"), LatchingShots[0],
			FString(TEXT("special-case.txt:Follow")));
	}

	// --- `special-case.txt` -------------------------------------------------------------------------
	TestEqual(TEXT("special-case.txt carries five shots"), SpecialCaseShots, 5);
	TestTrue(TEXT("DeathCam parses"), bDeathCam);
	TestTrue(TEXT("Animated parses"), bAnimated);

	// --- M6's evidence -------------------------------------------------------------------------------
	for (const FString& Shot : NaNBandShots)
	{
		AddError(FString::Printf(TEXT("SyncRotateOnMove shot inside the RemainingTime NaN band: %s"),
			*Shot));
	}
	TestEqual(TEXT("no shipped SyncRotateOnMove shot reaches MoveSpeed > 2 * MoveAccel"),
		NaNBandShots.Num(), 0);

	// --- the `CameraConstraints` census --------------------------------------------------------------
	// The plan's table (§9) labelled these "files"; read against the corpus they are **shots that
	// write the key**, and two of the six differ from the numbers recorded there. The real numbers
	// are asserted, and both metrics are stated so neither reading can drift again:
	//   written (shots): DialogPOV 59, SyncRotateOnMove 34, SnapOnShotChange 9,
	//                    AutoPositionFromTarget 5, ShowHud 4, DrawViewmodel 3
	//   set to 1:        50 / 33 / 7 / 3 / 2 / 1
	TestEqual(TEXT("DialogPOV is written by 59 shots"), WrittenKeys.FindRef(TEXT("DialogPOV")), 59);
	TestEqual(TEXT("SyncRotateOnMove by 34"), WrittenKeys.FindRef(TEXT("SyncRotateOnMove")), 34);
	TestEqual(TEXT("SnapOnShotChange by 9"), WrittenKeys.FindRef(TEXT("SnapOnShotChange")), 9);
	TestEqual(TEXT("AutoPositionFromTarget by 5"),
		WrittenKeys.FindRef(TEXT("AutoPositionFromTarget")), 5);
	TestEqual(TEXT("ShowHud by 4"), WrittenKeys.FindRef(TEXT("ShowHud")), 4);
	TestEqual(TEXT("DrawViewmodel by 3"), WrittenKeys.FindRef(TEXT("DrawViewmodel")), 3);

	TestEqual(TEXT("50 shots set DialogPOV"), SetToOne.FindRef(TEXT("DialogPOV")), 50);
	TestEqual(TEXT("33 set SyncRotateOnMove"), SetToOne.FindRef(TEXT("SyncRotateOnMove")), 33);
	TestEqual(TEXT("7 set SnapOnShotChange"), SetToOne.FindRef(TEXT("SnapOnShotChange")), 7);
	TestEqual(TEXT("3 set AutoPositionFromTarget"),
		SetToOne.FindRef(TEXT("AutoPositionFromTarget")), 3);
	TestEqual(TEXT("2 set ShowHud"), SetToOne.FindRef(TEXT("ShowHud")), 2);
	TestEqual(TEXT("1 sets DrawViewmodel"), SetToOne.FindRef(TEXT("DrawViewmodel")), 1);

	AddInfo(FString::Printf(
		TEXT("camera shot grammar: %d files, %d shots, %d Position/AttachPos/AttachType tokens"),
		Leaves.Num(), AllShots.Num(), Tokens.Num()));

	ElysiumCameraShots::FlushCache();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
