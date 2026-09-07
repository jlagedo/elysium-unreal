// The dialogue VOICE join: which take a line resolves to, and where the corpus answers from.
//
//   * `Elysium.Audio.DialogueTakeLetter`  — the column chooser (`0x100e15c0`) and the letterless
//                                           ellipses route (`FUN_100df0b0` / `0x100e1680`) against
//                                           fabricated rows. No corpus.
//   * `Elysium.Content.DialogueTakes`     — every shipped `_col_f`/`_col_m`/`_col_n` take under the
//                                           deployed corpus is reachable by SOME (sex, clan) pair
//                                           from its own `.dlg` row. A take nothing can select is a
//                                           muted line.
//   * `Elysium.Content.CorpusPathsFlip`   — the DC accessors (`DlgFromDialogname`, `SceneFile`,
//                                           `LipFile`, `SoundFile`) resolve real files under
//                                           `CorpusRoot()`, including a body-sound path, so a
//                                           regression that pointed one of them back at the legacy
//                                           loose export fails here.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumDlg.h"
#include "ElysiumLineService.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

static constexpr EAutomationTestFlags GElysiumDialogueAudioTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// TCHAR has no unambiguous TestEqual overload, and the ellipses sentinel is NUL, so every
	// assertion below compares a readable spelling of the letter instead of the letter itself.
	FString Take(const FElysiumDlgLine& Line, bool bMale, int32 ClanOffset)
	{
		const TCHAR Letter = FElysiumLineService::TakeLetterFor(Line, bMale, ClanOffset);
		return Letter == FElysiumLineService::EllipsesTake ? FString(TEXT("<ellipses>"))
			: FString::Chr(Letter);
	}

	FElysiumDlgLine MakeNpcRow(int32 Id, const TCHAR* Male, const TCHAR* Female = nullptr)
	{
		FElysiumDlgLine Line;
		Line.Id = Id;
		Line.Role = EElysiumDlgRole::NpcLine;
		Line.TextMale = Male;
		if (Female != nullptr)
		{
			Line.TextFemale = Female;
		}
		return Line;
	}
}

// ---------------------------------------------------------------------------------------------
// Elysium.Audio.DialogueTakeLetter

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueTakeLetterTest, "Elysium.Audio.DialogueTakeLetter",
	GElysiumDialogueAudioTestFlags)
bool FElysiumDialogueTakeLetterTest::RunTest(const FString&)
{
	// A row with only col-1: every player reads the male column, so every player hears `e`.
	{
		const FElysiumDlgLine Line = MakeNpcRow(11, TEXT("You want in, you play by the rules."));
		TestEqual(TEXT("male PC, no variants -> e"),
			Take(Line, true, ElysiumDlgClan::None), FString(TEXT("e")));
		TestEqual(TEXT("female PC with no col-2 -> e"),
			Take(Line, false, ElysiumDlgClan::None), FString(TEXT("e")));
		TestEqual(TEXT("Brujah PC with an empty clan column -> e"),
			Take(Line, true, ElysiumDlgClan::Brujah), FString(TEXT("e")));
	}

	// col-2 present: only a FEMALE player reads it, and only she gets the `f` take.
	{
		const FElysiumDlgLine Line =
			MakeNpcRow(12, TEXT("Nice to meet you, mister."), TEXT("Nice to meet you, miss."));
		TestEqual(TEXT("female PC with a col-2 variant -> f"),
			Take(Line, false, ElysiumDlgClan::None), FString(TEXT("f")));
		TestEqual(TEXT("male PC ignores col-2 -> e"),
			Take(Line, true, ElysiumDlgClan::None), FString(TEXT("e")));
	}

	// The two clan letters shipped audio pins. The clan column wins over the gendered column, so a
	// FEMALE Malkavian still hears `n` — the letter names the text column, never the voice's sex.
	{
		FElysiumDlgLine Line =
			MakeNpcRow(23, TEXT("Okay. I could use the help."), TEXT("Okay. I could use the help."));
		Line.TextClan[ElysiumDlgClan::Malkavian] = TEXT("I shall undertake your dark tutelage.");
		TestEqual(TEXT("Malkavian PC -> n"),
			Take(Line, true, ElysiumDlgClan::Malkavian), FString(TEXT("n")));
		TestEqual(TEXT("female Malkavian PC still -> n"),
			Take(Line, false, ElysiumDlgClan::Malkavian), FString(TEXT("n")));
		// Col-2 here is the editor's copy of col-1, so no female take exists and none is asked for.
		TestEqual(TEXT("a female Tremere PC reading the same row -> e (col-2 is a copy)"),
			Take(Line, false, ElysiumDlgClan::Tremere), FString(TEXT("e")));
	}
	// `FUN_100e15c0` asks `FUN_100df030(row)` = `col2 != NULL && strcmpi(col1, col2) != 0`. Troika's
	// editor copies col-1 into col-2 on most rows (every one of Jack's tutorial rows), so a copy —
	// even one differing only in case — must answer `e`, or the line asks for a `_col_f` take that
	// was never recorded and goes silent.
	{
		const FElysiumDlgLine Copy = MakeNpcRow(11,
			TEXT("[laughing]You want in, you play by the rules."),
			TEXT("[LAUGHING]You want in, you play by the rules."));
		TestEqual(TEXT("female PC on an editor-copied col-2 -> e"),
			Take(Copy, false, ElysiumDlgClan::None), FString(TEXT("e")));
	}
	{
		FElysiumDlgLine Line = MakeNpcRow(87, TEXT("You will address me properly."));
		Line.TextClan[ElysiumDlgClan::Ventrue] = TEXT("You will address me as your Prince.");
		TestEqual(TEXT("Ventrue PC -> m"),
			Take(Line, true, ElysiumDlgClan::Ventrue), FString(TEXT("m")));
		TestEqual(TEXT("a Gangrel PC reading the same row -> e"),
			Take(Line, true, ElysiumDlgClan::Gangrel), FString(TEXT("e")));
	}
	// The five unpinned clan letters probe nothing: a filled Nosferatu column still answers the
	// gendered take rather than guessing a `_col_j` file no install ships.
	{
		FElysiumDlgLine Line = MakeNpcRow(88, TEXT("Get out of my sight."), TEXT("Get out."));
		Line.TextClan[ElysiumDlgClan::Nosferatu] = TEXT("...");
		TestEqual(TEXT("an unpinned clan column falls back to the gendered take"),
			Take(Line, false, ElysiumDlgClan::Nosferatu), FString(TEXT("f")));
	}

	// `FUN_100df0b0` — no A-Z, a-z or >= 0xC0 byte anywhere in raw col-1.
	TestTrue(TEXT("an ellipsis row is letterless"),
		FElysiumLineService::IsLetterlessText(TEXT("...")));
	TestTrue(TEXT("punctuation and digits are letterless"),
		FElysiumLineService::IsLetterlessText(TEXT("-- 123 ... !?")));
	TestTrue(TEXT("an empty row is letterless"),
		FElysiumLineService::IsLetterlessText(FString()));
	TestFalse(TEXT("one ASCII letter is enough"),
		FElysiumLineService::IsLetterlessText(TEXT("...a...")));
	TestFalse(TEXT("a Latin-1 accented byte (>= 0xC0) counts as a letter"),
		FElysiumLineService::IsLetterlessText(FString(TEXT(".")) + FString::Chr(0xC9)));
	TestTrue(TEXT("0xBF (inverted question mark) does not count"),
		FElysiumLineService::IsLetterlessText(FString::Chr(0xBF)));

	// Retail tests raw col-1 BEFORE the column chooser, so a female/clan variant of a letterless row
	// is still the shared ellipses take.
	{
		FElysiumDlgLine Line = MakeNpcRow(9, TEXT("..."), TEXT("..."));
		Line.TextClan[ElysiumDlgClan::Malkavian] = TEXT("The voices say nothing.");
		TestEqual(TEXT("a letterless row -> the ellipses sentinel"),
			Take(Line, false, ElysiumDlgClan::Malkavian), FString(TEXT("<ellipses>")));
		TestEqual(TEXT("the ellipses sentinel resolves the one shared take"),
			FElysiumLineService::DialogueLineSource(TEXT("dlg/Main Characters/jack_tutorial.dlg"), 9,
				FElysiumLineService::EllipsesTake),
			FString(TEXT("character/dlg/ellipses")));
	}

	// The stem itself: `sound/` owned by the resolver, `dlg/` stripped from the dialogname, the take
	// letter in the suffix, everything folded to the corpus's lower case.
	TestEqual(TEXT("the default take's stem"),
		FElysiumLineService::DialogueLineSource(TEXT("dlg/Main Characters/jack_tutorial.dlg"), 1001),
		FString(TEXT("character/dlg/main characters/jack_tutorial/line1001_col_e")));
	TestEqual(TEXT("the female take's stem"),
		FElysiumLineService::DialogueLineSource(TEXT("dlg/Downtown LA/prince1.dlg"), 1015, TEXT('f')),
		FString(TEXT("character/dlg/downtown la/prince1/line1015_col_f")));
	TestEqual(TEXT("the Ventrue take's stem"),
		FElysiumLineService::DialogueLineSource(TEXT("dlg\\Downtown LA\\prince1.dlg"), 87, TEXT('m')),
		FString(TEXT("character/dlg/downtown la/prince1/line87_col_m")));
	TestEqual(TEXT("an absolute source path still yields the same stem"),
		FElysiumLineService::DialogueLineSource(
			TEXT("E:/whatever/dlg/Santa Monica/mercurio.dlg"), 40, TEXT('n')),
		FString(TEXT("character/dlg/santa monica/mercurio/line40_col_n")));

	// The seam that has no source yet: `m_flSpeechVol` answers the unattenuated 1.0 for every NPC.
	TestEqual(TEXT("the speech-volume seam answers 1.0"),
		FElysiumLineService::SpeechVolumeFor(FElysiumEntityHandle::Invalid()), 1.f);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Elysium.Content.DialogueTakes

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueTakesTest, "Elysium.Content.DialogueTakes",
	GElysiumDialogueAudioTestFlags)
bool FElysiumDialogueTakesTest::RunTest(const FString&)
{
	const FString DlgDir = FElysiumContentPaths::DlgDir();
	const FString VoiceDir = FElysiumContentPaths::SoundDir() / TEXT("character/dlg");
	if (!IFileManager::Get().DirectoryExists(*DlgDir)
		|| !IFileManager::Get().DirectoryExists(*VoiceDir))
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: the dialogue corpus is not deployed (%s / %s); run "
				 "`uv run elysium import dialogue` and `... import sound`"), *DlgDir, *VoiceDir));
		return true;
	}

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *VoiceDir, TEXT("line*_col_*.*"), true, false);
	if (Files.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: no voice takes under %s"), *VoiceDir));
		return true;
	}

	// One parse per `.dlg`, reused across that file's hundreds of takes.
	TMap<FString, TSharedPtr<const FElysiumDlgFile>> Parsed;
	auto LoadDlg = [&Parsed, &DlgDir](const FString& DlgRel) -> TSharedPtr<const FElysiumDlgFile>
	{
		if (const TSharedPtr<const FElysiumDlgFile>* Found = Parsed.Find(DlgRel))
		{
			return *Found;
		}
		TSharedPtr<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
		FString Error;
		TSharedPtr<const FElysiumDlgFile> Result;
		if (FElysiumDlgFile::LoadFile(DlgDir / DlgRel, *File, &Error))
		{
			Result = File;
		}
		Parsed.Add(DlgRel, Result);
		return Result;
	};

	TMap<TCHAR, int32> LetterCounts;
	int32 Checked = 0;
	TArray<FString> Unreachable;
	TArray<FString> CopiedFemaleTakes;
	TArray<FString> NoRow;
	TArray<FString> NoFile;
	for (const FString& Abs : Files)
	{
		const FString Ext = FPaths::GetExtension(Abs).ToLower();
		if (Ext != TEXT("mp3") && Ext != TEXT("wav"))
		{
			continue;   // the `.lip` sidecar deployed beside its audio
		}
		const FString Stem = FPaths::GetBaseFilename(Abs);
		const int32 ColAt = Stem.Find(TEXT("_col_"), ESearchCase::CaseSensitive,
			ESearchDir::FromEnd);
		if (ColAt == INDEX_NONE || ColAt + 5 >= Stem.Len() || !Stem.StartsWith(TEXT("line")))
		{
			continue;
		}
		const TCHAR Letter = Stem[ColAt + 5];
		LetterCounts.FindOrAdd(Letter, 0)++;
		if (Letter == TEXT('e'))
		{
			continue;   // the default column is reachable by construction; the variants are the risk
		}

		// `<VoiceDir>/<hub path>/<dlg stem>/line<id>_col_<C>.<ext>` -> `<hub path>/<dlg stem>.dlg`.
		FString Rel = Abs;
		Rel.ReplaceInline(TEXT("\\"), TEXT("/"));
		Rel = Rel.RightChop(VoiceDir.Len() + 1);
		const FString DlgRel = FPaths::GetPath(Rel) + TEXT(".dlg");
		const int32 LineId = FCString::Atoi(*Stem.Mid(4, ColAt - 4));   // "line<id>_col_<C>"

		const TSharedPtr<const FElysiumDlgFile> File = LoadDlg(DlgRel);
		if (!File.IsValid())
		{
			NoFile.AddUnique(DlgRel);
			continue;
		}
		const FElysiumDlgLine* Line = File->FindById(LineId);
		if (Line == nullptr)
		{
			NoRow.Add(FString::Printf(TEXT("%s line %d"), *DlgRel, LineId));
			continue;
		}

		// Is there ANY (sex, clan) pair this engine would answer this letter for? Eight clans (the
		// seven columns plus "no clan") times two sexes is the whole selectable space.
		bool bReachable = false;
		for (int32 Clan = ElysiumDlgClan::None; Clan < ElysiumDlgClan::Num && !bReachable; ++Clan)
		{
			for (int32 Sex = 0; Sex < 2 && !bReachable; ++Sex)
			{
				bReachable = FElysiumLineService::TakeLetterFor(*Line, Sex == 0, Clan) == Letter;
			}
		}
		++Checked;
		if (!bReachable)
		{
			// A `_col_f` take recorded for a row whose col-2 is the editor's copy of col-1 is one
			// retail never selects either (`FUN_100df030` is a strcmpi): authoring residue, 17 in the
			// shipped install, reported rather than failed.
			if (Letter == TEXT('f') && !Line->TextFemale.IsEmpty()
				&& Line->TextFemale.Equals(Line->TextMale, ESearchCase::IgnoreCase))
			{
				CopiedFemaleTakes.Add(FString::Printf(TEXT("%s line %d"), *DlgRel, LineId));
				continue;
			}
			Unreachable.Add(FString::Printf(TEXT("%s line %d take '%c'"), *DlgRel, LineId, Letter));
		}
	}

	TArray<TCHAR> Letters;
	LetterCounts.GetKeys(Letters);
	Letters.Sort();
	FString Census;
	for (const TCHAR Letter : Letters)
	{
		Census += FString::Printf(TEXT("%c=%d "), Letter, LetterCounts[Letter]);
	}
	AddInfo(FString::Printf(TEXT("voice takes under %s: %s(%d non-default rows checked)"),
		*VoiceDir, *Census, Checked));

	// The shipped distribution: only `e`, `f`, `m` and `n` exist. A fifth letter would mean one of
	// the five unpinned clan columns IS voiced after all, which would change `ChosenTakeLetter`.
	for (const TCHAR Letter : Letters)
	{
		TestTrue(FString::Printf(TEXT("take letter '%c' is one of e/f/m/n"), Letter),
			Letter == TEXT('e') || Letter == TEXT('f') || Letter == TEXT('m') || Letter == TEXT('n'));
	}
	TestTrue(TEXT("the female column is voiced (~229 takes)"),
		LetterCounts.FindRef(TEXT('f')) >= 200);
	TestTrue(TEXT("the Malkavian column is voiced (~50 takes)"),
		LetterCounts.FindRef(TEXT('n')) >= 40);
	TestTrue(TEXT("the Ventrue column is voiced (~8 takes)"),
		LetterCounts.FindRef(TEXT('m')) >= 5);
	TestTrue(TEXT("the default column carries the bulk of the corpus"),
		LetterCounts.FindRef(TEXT('e')) > 4000);

	if (!NoFile.IsEmpty())
	{
		AddError(FString::Printf(TEXT("%d voiced conversations have no parsable `.dlg`: %s"),
			NoFile.Num(), *FString::Join(NoFile, TEXT(", "))));
	}
	// A take whose row was dropped at parse (negative id, col-1 under two characters) is retail's
	// own behaviour, so it is reported rather than failed.
	if (!NoRow.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("%d variant takes address a row `read_line_data` drops"),
			NoRow.Num()));
	}
	if (!CopiedFemaleTakes.IsEmpty())
	{
		AddInfo(FString::Printf(
			TEXT("%d female takes sit on rows whose col-2 copies col-1; retail's strcmpi never selects them"),
			CopiedFemaleTakes.Num()));
	}
	if (!Unreachable.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("%d shipped takes no (sex, clan) pair can select (first: %s)"),
			Unreachable.Num(), *Unreachable[0]));
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Elysium.Content.CorpusPathsFlip

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCorpusPathsFlipTest, "Elysium.Content.CorpusPathsFlip",
	GElysiumDialogueAudioTestFlags)
bool FElysiumCorpusPathsFlipTest::RunTest(const FString&)
{
	const FString CorpusRoot = FElysiumContentPaths::CorpusRoot();
	if (!IFileManager::Get().DirectoryExists(*FElysiumContentPaths::DlgDir()))
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: no deployed corpus at %s"), *CorpusRoot));
		return true;
	}

	// Every accessor DC flipped, with the authored case a real keyvalue carries.
	struct FCase { const TCHAR* What; FString Path; };
	const FString Stem =
		FElysiumLineService::DialogueLineSource(TEXT("dlg/Main Characters/jack_tutorial.dlg"), 1001);
	const TArray<FCase> Cases = {
		{ TEXT("DlgFromDialogname"),
			FElysiumContentPaths::DlgFromDialogname(TEXT("dlg/Main Characters/jack_tutorial.dlg")) },
		{ TEXT("DlgDir"), FElysiumContentPaths::DlgDir() / TEXT("main characters/jack_tutorial.dlg") },
		{ TEXT("SceneFile"), FElysiumContentPaths::SceneFile(Stem + TEXT(".vcd")) },
		{ TEXT("LipFile"), FElysiumContentPaths::LipFile(Stem + TEXT(".lip")) },
		{ TEXT("SoundFile (voice mp3)"), FElysiumContentPaths::SoundFile(Stem + TEXT(".mp3")) },
		{ TEXT("SoundFile (the `.lip` deployed beside its audio)"),
			FElysiumContentPaths::SoundFile(Stem + TEXT(".lip")) },
		{ TEXT("SoundFile (a footstep wav)"),
			FElysiumContentPaths::SoundFile(TEXT("Surfaces/Concrete/stepleft1.wav")) },
		{ TEXT("SoundFile (the shared ellipses take)"),
			FElysiumContentPaths::SoundFile(TEXT("character/dlg/ellipses.mp3")) },
	};
	for (const FCase& Case : Cases)
	{
		TestTrue(FString::Printf(TEXT("%s resolves (%s)"), Case.What, *Case.Path),
			IFileManager::Get().FileExists(*Case.Path));
		TestTrue(FString::Printf(TEXT("%s resolves under CorpusRoot()"), Case.What),
			Case.Path.StartsWith(CorpusRoot, ESearchCase::IgnoreCase));
	}

	// Scripts stay on the legacy loose export until their own slice; asserting that keeps a future
	// "flip everything" from silently taking them along without an import lane behind them.
	const FString ScriptsDir = FElysiumContentPaths::ScriptsDir();
	TestFalse(TEXT("ScriptsDir() is NOT on the corpus root"),
		!ScriptsDir.IsEmpty() && ScriptsDir.StartsWith(CorpusRoot, ESearchCase::IgnoreCase));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
