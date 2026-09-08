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
//                                           `LipFile`, `SoundFile`, `SchemeFile`) resolve real files
//                                           under `CorpusRoot()`, including a body-sound path, so a
//                                           regression that pointed one of them back at the legacy
//                                           loose export fails here. `AudioCatalogFile()` and
//                                           `MoverSoundGroupsFile()` are deleted, so "no audio
//                                           accessor is rooted at Root()" is a compile-time fact.
//   * `Elysium.Content.SoundGroupResolver` — a `soundgroup` token resolves to the WAVs retail's own
//                                           directory walk would find, and a subkey the group ships
//                                           no file for comes back absent rather than substituted.
//   * `Elysium.Content.SoundDecode`       — `FElysiumSoundCache::LoadSoundDecoded` decodes one
//                                           shipped MS-ADPCM WAV and one shipped dialogue MP3.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumDlg.h"
#include "ElysiumLineService.h"
#include "ElysiumSoundCache.h"
#include "Substrate/ElysiumMoverSounds.h"

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

bool // ---------------------------------------------------------------------------------------------
// Elysium.Content.CorpusPathsFlip

bool // ---------------------------------------------------------------------------------------------
// Elysium.Content.SoundGroupResolver
//
// Retail resolves a `soundgroup` by walking `sound\Usable\<Category>\` and matching directory and
// file names verbatim, case-insensitively: FUN_101f41b0 @0x101f41b0 builds "Usable\<Category>" out
// of the vocabulary file's `Name` key and the registered category token (FUN_101f66c0 @0x101f66c0),
// FUN_101f3810 @0x101f3810 walks it, FUN_101f39d0 @0x101f39d0 is the `__strcmpi` lookup and
// FUN_101f42a0 @0x101f42a0 is the miss arm. The vocabulary is the `SoundList` of
// `vdata/system/sndscheme_{openable,switch,computer}.txt` (FUN_101f5390 @0x101f5390).

bool // ---------------------------------------------------------------------------------------------
// Elysium.Content.SoundDecode
//
// The decoder itself, over shipped bytes: one MS-ADPCM WAV (the ambient family) and one MP3 (a
// dialogue take). No test reached FElysiumSoundCache::LoadSoundDecoded before AUD0.5.

bool #endif // WITH_DEV_AUTOMATION_TESTS
