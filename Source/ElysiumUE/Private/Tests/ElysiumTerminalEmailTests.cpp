// The computer terminal's mail area (docs/project/plans/terminals.md, slice G;
// docs/vtmb/computer-terminals.md §12).
//
// `Elysium.Substrate.TerminalEmail` drives the whole retail chain on a hand-built definition: the
// `email` builtin's gate, the `email_password` prompt and the acknowledged entry, the visible-index
// rebuild, the list draw cell for cell, the two input regimes (`FUN_1021b9c0`'s list arm has no
// length check; its open arm demands exactly one byte because the client is in single-key mode),
// the 128-flag bitmask, the once-only `runscript`, both retail off-by-ones, the save round-trip and
// the `global_email` reconciliation on the player.
//
// `Elysium.Content.TerminalEmailContent` runs the same list draw on the shipped `haven_pc.txt` —
// the only file in the corpus with more than ten `Email` records — and abstains without the export.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumScriptHost.h"
#include "ElysiumVariant.h"
#include "ElysiumViewState.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Substrate/ElysiumTerminal.h"
#include "Tests/ElysiumTestServices.h"
#include "UI/ElysiumTerminalCells.h"
#include "UI/SElysiumTerminalInput.h"

namespace ElysiumTerminalEmailTests
{
static constexpr EAutomationTestFlags GMailFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// A host whose answer is decided by the source string, plus a counter for the once-only
// `runscript`. `PASS`/`FAIL` drive the dependency gate onto either side of the integer-only
// `logic_pythoncheck` rule without a Python VM.
class FElysiumMailScriptHost final : public IElysiumScriptHost
{
public:
	TMap<FString, int32> Calls;

	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext&,
		FString* OutError = nullptr) override
	{
		if (OutError)
		{
			OutError->Reset();
		}
		if (Source.StartsWith(TEXT("SCRIPT:")))
		{
			++Calls.FindOrAdd(Source);
			return FElysiumVariant::Int(0);
		}
		return Source == TEXT("PASS") ? FElysiumVariant::Int(1) : FElysiumVariant::Int(0);
	}
	virtual const TCHAR* Name() const override { return TEXT("mail-sentinel"); }
};

// The `Mail` prefix is unity-blob safety, the same reason `MailBoxRule` / `MailBoxRow` carry it
// below: `ElysiumTerminalRouterTests.cpp` declares an identically named helper in its own namespace
// and both files put a file-scope `using namespace` over it, so the two go ambiguous the moment the
// build merges them into one translation unit — which it does, and which the blob composition
// decides, so adding an unrelated test file is enough to trip it.
static UElysiumGameStateSubsystem* MailHeadlessGameState()
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	return NewObject<UElysiumGameStateSubsystem>(GameInstance);
}

// Twelve visible mails — enough to cross the ten-row page — plus a thirteenth whose dependency
// fails, so the hidden record sits at the end of the record order and cannot renumber the twelve.
static FElysiumTerminalDefinition MailDefinition()
{
	FElysiumTerminalDefinition Definition;
	Definition.ScreenSaver = TEXT("Mail console");
	Definition.Brackets = TEXT("[]");
	Definition.EmailUsername = TEXT("Noa");
	Definition.EmailPassword = TEXT("sunrise");
	Definition.LogonLines.Add(TEXT("Mail console"));
	for (int32 Index = 1; Index <= 12; ++Index)
	{
		FElysiumTerminalEmail Email;
		Email.Subject = FString::Printf(TEXT("Mail%02d"), Index);
		Email.Sender = FString::Printf(TEXT("sender%02d"), Index);
		Email.Body = FString::Printf(TEXT("Body%02d"), Index);
		Email.Dependency = TEXT("PASS");
		Definition.Emails.Add(MoveTemp(Email));
	}
	// Record 0 carries the once-only script, so the first `[1]` open is also the `runscript` case.
	Definition.Emails[0].RunScript = TEXT("SCRIPT:opened");
	FElysiumTerminalEmail Hidden;
	Hidden.Subject = TEXT("Hidden");
	Hidden.Sender = TEXT("nobody");
	Hidden.Body = TEXT("Never listed.");
	Hidden.Dependency = TEXT("FAIL");
	Definition.Emails.Add(MoveTemp(Hidden));
	return Definition;
}

// The same shape with no `Email` blocks at all: the `email` builtin's gate is the record count.
static FElysiumTerminalDefinition NoMailDefinition()
{
	FElysiumTerminalDefinition Definition;
	Definition.Brackets = TEXT("[]");
	Definition.EmailPassword = TEXT("sunrise");
	Definition.LogonLines.Add(TEXT("Mail console"));
	return Definition;
}

// Three mails and no scripts — the fixture the `global_email` trio share.
static FElysiumTerminalDefinition HavenDefinition()
{
	FElysiumTerminalDefinition Definition;
	Definition.Brackets = TEXT("[]");
	Definition.EmailUsername = TEXT("Noa");
	Definition.EmailPassword = TEXT("sunrise");
	Definition.LogonLines.Add(TEXT("Haven PC"));
	for (int32 Index = 1; Index <= 3; ++Index)
	{
		FElysiumTerminalEmail Email;
		Email.Subject = FString::Printf(TEXT("Haven%02d"), Index);
		Email.Sender = TEXT("LaCroix");
		Email.Body = TEXT("Body.");
		Definition.Emails.Add(MoveTemp(Email));
	}
	return Definition;
}

// The framed box rows, built exactly as `FElysiumTerminal::RuleRow` / `FramedRow` build them, so a
// cell-for-cell comparison is against the same arithmetic and not against a transcription.
//
// Unity-blob safety: `ElysiumTerminalSliceTests.cpp` builds the same two rows for the same reason,
// and a named namespace plus a file-scope `using` does not separate two translation units the
// build merges — the calls go ambiguous the moment the two land in one blob. The `Mail` prefix is
// what keeps them apart.
static FString MailBoxRule(int32 Columns)
{
	return FString(TEXT(" +")) + FString::ChrN(Columns - 4, TEXT('-')) + TEXT("+");
}

static FString MailBoxRow(int32 Columns, const FString& Text, int32 Margin)
{
	const int32 Width = FMath::Max(2, Columns - 2);
	FString Row = FString::ChrN(Width, TEXT(' '));
	Row[0] = TEXT('|');
	Row[Width - 1] = TEXT('|');
	const int32 At = FMath::Clamp(Margin, 0, Width);
	const int32 Cap = FMath::Min(Text.Len(), 40 - At);
	for (int32 Offset = 0; Offset < Cap && At + Offset < Width; ++Offset)
	{
		Row[At + Offset] = Text[Offset];
	}
	return TEXT(" ") + Row;
}

// A `Hacking_Strings` table for the headless resolver.
//
// It is a project FIXTURE, not a copy of the shipped localization: what matters is the table's
// SHAPE, because that is what the mail router reads. Every mail command is
// `Q_strnicmp(line, Hacking_Strings[i] + 1, 1)` — the SECOND byte of the localized word, which is
// the letter inside the brackets of `"[n]ext"` — so a table whose entries are not bracketed makes
// the five hotkeys indistinguishable. With no table at all `FUN_10219400` hands back the compiled
// key names and all five collapse onto `T` (`"STRING_NEXT_CMD"[1]`), which is a real retail
// degradation but leaves nothing to assert; installing this one is what makes the mail cases run
// on every tree instead of only on one with the export mounted.
static TArray<FString> MailStringTable()
{
	TArray<FString> Table;
	Table.SetNum(48);
	Table[0] = TEXT("Press CTRL-C to use the Hacking feat");
	Table[1] = TEXT("Changing to %s");
	Table[2] = TEXT("Available menus");
	Table[3] = TEXT("Available commands");
	Table[4] = TEXT("No commands");
	Table[5] = TEXT("Invalid command");
	Table[6] = TEXT("Type \"list\" for a list");
	Table[7] = TEXT("Type \"help\" for help");
	Table[8] = TEXT("Type a menu name to enter it");
	Table[9] = TEXT("Type a command to run it");
	Table[10] = TEXT("Type \"home\" for the main menu");
	Table[11] = TEXT("Type \"quit\" to log off");
	Table[12] = TEXT("LOGON");
	Table[13] = TEXT("\n%s %c%s%c\n\n");
	Table[14] = TEXT("Password required");
	Table[15] = TEXT("PASSWORD FAILED");
	Table[16] = TEXT("Password accepted");
	Table[17] = TEXT("home");
	Table[18] = TEXT("[Press \"ENTER\" to continue]");
	Table[19] = TEXT("Access granted");
	Table[20] = TEXT("HELP");
	Table[21] = TEXT("[Press \"ENTER\" to go back]");
	Table[22] = TEXT("From");
	Table[23] = TEXT("Subject");
	// The five bracketed hotkey words. The bracket is not decoration: the letter inside it is the
	// byte the router compares.
	Table[24] = TEXT("[n]ext");
	Table[25] = TEXT("[p]rev");
	Table[26] = TEXT("[d]elete");
	Table[27] = TEXT("[m]enu");
	Table[28] = TEXT("[q]uit");
	Table[29] = TEXT("Inbox for");
	Table[30] = TEXT("email");
	Table[31] = TEXT("%d messages, %d unread");
	Table[32] = TEXT("Logging off");
	Table[33] = TEXT("quit");
	Table[34] = TEXT("help");
	Table[35] = TEXT("list");
	Table[36] = TEXT("email");
	Table[37] = TEXT("Difficulty ");
	Table[38] = TEXT("Skill too low at difficulty ");
	Table[39] = TEXT("Mail password: %s");
	Table[40] = TEXT("Making hack attempt at skill ");
	Table[41] = TEXT("Current menu");
	Table[42] = TEXT("Type menu or command:");
	Table[43] = TEXT("Home menu");
	Table[44] = TEXT("Menu");
	// 45-47 are the three list footers, the only indices `FUN_10219400` leaves NULL.
	Table[45] = TEXT("%d msgs, %d to %d");
	Table[46] = TEXT("%s / %s to page");
	Table[47] = TEXT("%s to exit");
	return Table;
}

// Installed for the length of one case and removed on every exit path, including the early
// `return false`s: the resolver is a process-wide static and a leaked table would silently rewrite
// every other terminal case's copy.
struct FMailStringScope
{
	FMailStringScope() { ElysiumHackingStrings::InstallForTests(MailStringTable()); }
	~FMailStringScope() { ElysiumHackingStrings::ResetForTests(); }
};

// One `prop_hacking` def, so the fixtures below differ only in name and `global_email`.
static FElysiumEntityDef TerminalDef(const TCHAR* Name, bool bGlobalEmail)
{
	FElysiumEntityDef Def;
	Def.Classname = TEXT("prop_hacking");
	Def.TargetName = Name;
	Def.Keys.Add(TEXT("start_enabled"), TEXT("1"));
	Def.Keys.Add(TEXT("difficulty"), TEXT("1"));
	Def.Keys.Add(TEXT("skilltype"), TEXT("2"));
	Def.Keys.Add(TEXT("global_email"), bGlobalEmail ? TEXT("1") : TEXT("0"));
	return Def;
}
}

using namespace ElysiumTerminalEmailTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalEmailTest,
	"Elysium.Substrate.TerminalEmail", GMailFlags)
bool FElysiumTerminalEmailTest::RunTest(const FString&)
{
	using namespace ElysiumHackingStrings;
	const FMailStringScope Strings;

	UElysiumGameStateSubsystem* State = MailHeadlessGameState();
	TUniquePtr<FElysiumMailScriptHost> OwnedHost = MakeUnique<FElysiumMailScriptHost>();
	FElysiumMailScriptHost* Host = OwnedHost.Get();
	State->SetScriptHost(MoveTemp(OwnedHost));

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__terminal_email__");
	Defs.Defs.Add(TerminalDef(TEXT("terminal"), /*bGlobalEmail*/ false));
	Defs.Defs.Add(TerminalDef(TEXT("haven_a"), /*bGlobalEmail*/ true));
	Defs.Defs.Add(TerminalDef(TEXT("haven_b"), /*bGlobalEmail*/ true));
	Defs.Defs.Add(TerminalDef(TEXT("haven_c"), /*bGlobalEmail*/ true));

	FElysiumRecordingServices Services;
	Services.StandTerminalScreen();
	FElysiumEntityWorld World(nullptr, State, Services.Bundle());
	AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
		EAutomationExpectedErrorFlags::Contains, 4);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);

	auto Resolve = [&World](const TCHAR* Name) -> FElysiumPropHacking*
	{
		FElysiumEntity* Entity = World.FindByName(Name);
		FElysiumTerminal* Base = Entity ? Entity->AsTerminal() : nullptr;
		return static_cast<FElysiumPropHacking*>(Base);
	};
	FElysiumPropHacking* Terminal = Resolve(TEXT("terminal"));
	if (!TestNotNull(TEXT("the terminal resolves"), Terminal))
	{
		return false;
	}

	// --- the five hotkey letters ---------------------------------------------------------------
	// Every mail command is `Q_strnicmp(line, Hacking_Strings[i] + 1, 1)` — the SECOND byte of the
	// localized word, which is the letter inside the brackets of `"[n]ext"`. With no table at all
	// `FUN_10219400` hands back the compiled key names and all five collapse onto `T`
	// (`"STRING_NEXT_CMD"[1]`), which is a real retail degradation but leaves nothing to assert.
	auto Letter = [&World](int32 Index)
	{
		const FString Word = Get(&World, Index);
		return Word.Len() > 1 ? FString::Chr(Word[1]) : FString();
	};
	const FString KeyNext = Letter(NextCmd);
	const FString KeyPrev = Letter(PrevCmd);
	const FString KeyDel = Letter(DelCmd);
	const FString KeyMenu = Letter(MenuCmd);
	const FString KeyQuit = Letter(QuitCmd);
	{
		// With the fixture table installed this is deterministic on every tree — it is the reason
		// the table is installed at all, and it is asserted rather than abstained on.
		const TSet<FString> Distinct = { KeyNext, KeyPrev, KeyDel, KeyMenu, KeyQuit };
		if (!TestEqual(TEXT("the installed table gives five distinct mail hotkeys"),
			Distinct.Num(), 5) || !TestFalse(TEXT("and none of them is empty"),
			Distinct.Contains(FString())))
		{
			return false;
		}
		TestEqual(TEXT("the hotkey letter is the SECOND byte of the bracketed word"), KeyNext,
			FString(TEXT("n")));
		TestEqual(TEXT("...for delete too"), KeyDel, FString(TEXT("d")));
	}

	// --- the gate: the `email` builtin only matches when the record count is non-zero ------------
	Terminal->InstallDefinition(NoMailDefinition());
	Terminal->InputEnable();
	if (!TestEqual(TEXT("+use opens the session"),
		World.BeginPlayerUseSession(Terminal->Handle, Player).Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		return false;
	}
	uint32 Serial = Terminal->SessionSerial;
	auto Submit = [&](const FString& Command)
	{
		return World.SubmitTerminalCommand(Terminal->Handle, Serial, Command);
	};
	auto Row = [Terminal](int32 Index) { return Terminal->Screen.RowTextTrimmed(Index); };
	const int32 Columns = Terminal->TextColumns;

	TestTrue(TEXT("`email` is accepted as a line with no Email records"),
		Submit(Get(&World, Email)));
	// `FUN_1021aaa0`: `strnicmp(str[36], line, 16) != 0 || emailCount == 0` falls through to the
	// `home` arm, which does not match `email` either — so the line ends as an invalid command.
	TestEqual(TEXT("but with no Email records it never reaches the mail area"),
		Terminal->CurrentDirectory, INDEX_NONE);
	TestEqual(TEXT("it falls past `home` and ends as an invalid command"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Acknowledge);
	TestFalse(TEXT("and it did not unlock the mail area"), Terminal->bEmailUnlocked);
	Submit(FString());

	// --- the password gate, the acknowledged entry and the unlock -------------------------------
	Terminal->InstallDefinition(MailDefinition());
	TestTrue(TEXT("`email` is accepted with records present"), Submit(Get(&World, Email)));
	TestEqual(TEXT("a non-empty email_password prompts before the mail area opens"),
		Terminal->InputMode(), EElysiumTerminalInputMode::Password);
	TestFalse(TEXT("the prompt has not unlocked anything yet"), Terminal->bEmailUnlocked);
	TestTrue(TEXT("a wrong email password is accepted as a line"), Submit(TEXT("moonset")));
	TestEqual(TEXT("and counts against m_nEmailAttempts"), Terminal->EmailAttempts, 1);
	TestTrue(TEXT("the right password is accepted"), Submit(TEXT("sunrise")));
	TestEqual(TEXT("which enters the mail area"), Terminal->CurrentDirectory,
		FElysiumPropHacking::MailArea);
	// `FUN_1021c890(-2)`: title 16, string 39 formatted with `email_password`, string 18 on the
	// last row, acknowledge mode, and `m_bEmailUnlocked = 1` — set on ENTRY, not on the password.
	TestEqual(TEXT("mail-area entry ends in acknowledge mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Acknowledge);
	TestTrue(TEXT("entry sets m_bEmailUnlocked"), Terminal->bEmailUnlocked);
	TestEqual(TEXT("the entry screen titles string 16"), Row(2),
		MailBoxRow(Columns, Get(&World, ValidPassword),
			(Columns - Get(&World, ValidPassword).Len() - 2) / 2));
	TestEqual(TEXT("and waits on the acknowledge prompt"), Row(Terminal->TextRows - 1),
		TEXT(" ") + Get(&World, Continue));

	// --- the empty line the acknowledgement produces misses the hotkey router and draws the list -
	TestTrue(TEXT("the acknowledging empty line is accepted"), Submit(FString()));
	TestEqual(TEXT("the inbox is a line-editor prompt again"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);
	// `FUN_10219120` is the type-3 message as well as the flag clear, so the list draw's tail also
	// opens the client's editor (`FUN_100c82e0`) — and it only does so if the three-row footer left
	// the cursor short of `columns - rightMargin - 1`.
	TestTrue(TEXT("and its client line editor is open on that footer row"),
		Terminal->IsLineEditActive());
	TestEqual(TEXT("the dependency-hidden record is absent from the visible table"),
		Terminal->MailVisible.Num(), 12);
	TestFalse(TEXT("so record 12 (the FAIL dependency) is never listed"),
		Terminal->MailVisible.Contains(12));
	TestEqual(TEXT("the list opens on page 0"), Terminal->MailPage, 0);
	TestEqual(TEXT("and leaves the open-message state"), Terminal->MailOpenIndex, INDEX_NONE);

	// --- page 1, cell for cell -------------------------------------------------------------------
	// `FUN_1021c040`: the title box, then up to ten `[N] subject` rows from the visible table, then
	// the three footer rows pinned to `rows-3 / rows-2 / rows-1`.
	auto ExpectedFooters = [&](int32 Count, int32 First, int32 Last)
	{
		TestEqual(TEXT("footer row rows-3 is string 45 with count, first and last"),
			Row(Terminal->TextRows - 3),
			(TEXT(" ") + ElysiumTerminalFormat(GetOrEmpty(&World, MailListCount),
				{ FElysiumTerminalArg::Num(Count), FElysiumTerminalArg::Num(First),
				  FElysiumTerminalArg::Num(Last) })).TrimEnd());
		TestEqual(TEXT("footer row rows-2 is string 46 with the next/prev words"),
			Row(Terminal->TextRows - 2),
			(TEXT(" ") + ElysiumTerminalFormat(GetOrEmpty(&World, MailListMore),
				{ Get(&World, NextCmd), Get(&World, PrevCmd) })).TrimEnd());
		TestEqual(TEXT("footer row rows-1 is string 47 with the quit word"),
			Row(Terminal->TextRows - 1),
			(TEXT(" ") + ElysiumTerminalFormat(GetOrEmpty(&World, MailListExit),
				{ Get(&World, QuitCmd) })).TrimEnd());
	};
	{
		const FString Title = ElysiumTerminalFormat(TEXT("%s %s"),
			{ Get(&World, EmailTitleBar), TEXT("Noa") });
		const int32 Margin = (Columns - Title.Len() - 2) / 2;
		TestEqual(TEXT("the inbox title box rules"), Row(0), MailBoxRule(Columns));
		TestEqual(TEXT("the inbox title is \"<string 29> <email_username>\""), Row(2),
			MailBoxRow(Columns, Title, Margin));
		TestEqual(TEXT("and closes on the second rule"), Row(4), MailBoxRule(Columns));
		for (int32 Number = 1; Number <= 10; ++Number)
		{
			TestEqual(FString::Printf(TEXT("page 1 row %d"), Number), Row(4 + Number),
				FString::Printf(TEXT(" [%d]Mail%02d"), Number, Number));
		}
		TestEqual(TEXT("page 1 draws exactly ten rows"), Row(15), FString());
		ExpectedFooters(/*Count*/ 12, /*First*/ 1, /*Last*/ 10);
	}
	// The unread bracket: entity message type 6 clears the style byte before the `[N]` and type 5
	// restores it after, and the rasterizer XORs on bit 7 — so bit CLEAR is reverse video, on the
	// bracketed number only.
	TestFalse(TEXT("an unread row's bracket is drawn in reverse video"),
		Terminal->Screen.StyleAt(1, 5));
	TestFalse(TEXT("...for the whole bracket"), Terminal->Screen.StyleAt(3, 5));
	TestTrue(TEXT("...and the subject after it is not"), Terminal->Screen.StyleAt(4, 5));

	// --- paging: no length check in the list state ----------------------------------------------
	TestTrue(TEXT("the next-page hotkey is accepted"), Submit(KeyNext));
	TestEqual(TEXT("and pages forward"), Terminal->MailPage, 1);
	{
		TestEqual(TEXT("page 2 row 11"), Row(5), TEXT(" [11]Mail11"));
		TestEqual(TEXT("page 2 row 12"), Row(6), TEXT(" [12]Mail12"));
		TestEqual(TEXT("page 2 draws exactly the two rows that are left"), Row(7), FString());
		ExpectedFooters(/*Count*/ 12, /*First*/ 11, /*Last*/ 12);
	}
	TestTrue(TEXT("the prev-page hotkey is accepted"), Submit(KeyPrev));
	TestEqual(TEXT("and pages back"), Terminal->MailPage, 0);
	// The list arm never measures the line: `atoi` runs on the whole thing and the letter compare
	// looks at one character, so a whole word beginning with the hotkey pages forward.
	TestTrue(TEXT("the spelled-out next word is accepted"), Submit(KeyNext + TEXT("ext")));
	TestEqual(TEXT("and pages forward, because the list state has no length check"),
		Terminal->MailPage, 1);
	Submit(KeyPrev);
	TestTrue(TEXT("nonsense beginning with the hotkey letter is accepted"),
		Submit(KeyNext + TEXT("onsense")));
	TestEqual(TEXT("and pages forward too"), Terminal->MailPage, 1);
	Submit(KeyPrev);
	TestEqual(TEXT("back on page 0"), Terminal->MailPage, 0);

	// --- the numeric open, the read bit and the once-only runscript ------------------------------
	TestEqual(TEXT("nothing has run the mail runscript yet"),
		Host->Calls.FindRef(TEXT("SCRIPT:opened")), 0);
	TestTrue(TEXT("a bare number is accepted in the list state"), Submit(TEXT("1")));
	TestEqual(TEXT("one-based over the VISIBLE table, so `1` opens record 0"),
		Terminal->MailOpenIndex, 0);
	TestEqual(TEXT("and remembers the clamped visible row"), Terminal->MailSelectedRow, 0);
	// `FUN_1021c260`'s tail is `FUN_10219240`, not the acknowledge helper: an open message runs in
	// single-key mode so the client sends one `hackcmd %c` per keypress.
	TestEqual(TEXT("an open message is raw single-key mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Raw);
	// `FUN_10219240` calls `FUN_10219120` FIRST, so a raw prompt carries the same type-3 activation
	// a line prompt does. It has to: `0x100c7090` tests `+0xe88` third, ahead of the raw arm, so a
	// closed editor would eat every hotkey before it could become a `hackcmd %c`.
	TestTrue(TEXT("and its client line editor is open, so a hotkey reaches the raw arm"),
		Terminal->IsLineEditActive());
	TestTrue(TEXT("opening set the read bit"), Terminal->IsEmailRead(0));
	TestEqual(TEXT("and fired the runscript exactly once"),
		Host->Calls.FindRef(TEXT("SCRIPT:opened")), 1);
	// `FUN_1021c260` prints the two headers, the body, and the hotkey row at `rows-2`. The hotkey
	// row is 42 characters against a 35-column right margin, so the client's word wrap breaks it
	// onto the last row and the newline that does it SCROLLS the whole message up one — real
	// behaviour of a 36-column screen, and the reason these rows are matched by content rather than
	// pinned to an index.
	auto FindRow = [Terminal](const FString& Text) -> int32
	{
		for (int32 Index = 0; Index < Terminal->Screen.Rows(); ++Index)
		{
			if (Terminal->Screen.RowTextTrimmed(Index) == Text)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	};
	const int32 SubjectRow = FindRow(TEXT(" ") + ElysiumTerminalFormat(TEXT("(%s) %s"),
		{ Get(&World, SubjectHeader), TEXT("Mail01") }));
	const int32 SenderRow = FindRow(TEXT(" ") + ElysiumTerminalFormat(TEXT("(%s) %s"),
		{ Get(&World, FromHeader), TEXT("sender01") }));
	const int32 BodyRow = FindRow(TEXT(" Body01"));
	TestTrue(TEXT("the body's subject row is \"(<string 23>) <subject>\""),
		SubjectRow != INDEX_NONE);
	TestTrue(TEXT("the sender row is \"(<string 22>) <sender>\""), SenderRow != INDEX_NONE);
	TestTrue(TEXT("the body is printed verbatim"), BodyRow != INDEX_NONE);
	TestTrue(TEXT("subject, then sender, then three blank rows, then the body"),
		SubjectRow != INDEX_NONE && SenderRow == SubjectRow + 1 && BodyRow == SenderRow + 3);
	{
		// The five hotkey words, in the format's order, across the wrapped tail. The wrap's newline
		// lands the cursor on the last row, which scrolls the buffer, so the two halves are not on
		// adjacent rows: they are whatever is left in the bottom three.
		FString Tail;
		for (int32 Index = Terminal->TextRows - 3; Index < Terminal->TextRows; ++Index)
		{
			const FString Line = Row(Index).TrimStart();
			if (!Line.IsEmpty())
			{
				Tail += Tail.IsEmpty() ? Line : TEXT(" ") + Line;
			}
		}
		TestEqual(TEXT("the hotkey row is string 24..28 in the `%s, %s, %s, %s, %s: ` format"),
			Tail.TrimEnd(),
			ElysiumTerminalFormat(TEXT("%s, %s, %s, %s, %s: "),
				{ Get(&World, NextCmd), Get(&World, PrevCmd), Get(&World, DelCmd),
				  Get(&World, MenuCmd), Get(&World, QuitCmd) }).TrimEnd());
	}

	// --- the open state demands exactly one byte --------------------------------------------------
	TestTrue(TEXT("a two-character line is accepted by the router"), Submit(KeyNext + KeyQuit));
	TestEqual(TEXT("but misses the length-1 test and redraws the list"), Terminal->MailOpenIndex,
		INDEX_NONE);
	TestEqual(TEXT("which returns the session to the line editor"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);
	Submit(TEXT("1"));
	TestEqual(TEXT("the mail reopens"), Terminal->MailOpenIndex, 0);
	TestEqual(TEXT("a second open is silent: the runscript is once per saved state"),
		Host->Calls.FindRef(TEXT("SCRIPT:opened")), 1);
	// `ESC` in an open message is the client's four-byte `hackcmd quit` (`0x102b7238`), which fails
	// the length-1 test — so it lands on the LIST, not at the root directory.
	TestTrue(TEXT("the client's ESC line is accepted"), Submit(TEXT("quit")));
	TestEqual(TEXT("ESC in an open message returns to the list, not to root"),
		Terminal->CurrentDirectory, FElysiumPropHacking::MailArea);
	TestEqual(TEXT("leaving the open-message state"), Terminal->MailOpenIndex, INDEX_NONE);
	// A row that has been read draws its bracket in normal video.
	TestTrue(TEXT("the read row's bracket is no longer reversed"), Terminal->Screen.StyleAt(1, 5));

	// --- `[m]enu` and the message walk ------------------------------------------------------------
	Submit(TEXT("2"));
	TestEqual(TEXT("`2` opens the second visible row"), Terminal->MailOpenIndex, 1);
	TestTrue(TEXT("the menu hotkey is accepted"), Submit(KeyMenu));
	TestEqual(TEXT("`[m]enu` returns to the list"), Terminal->MailOpenIndex, INDEX_NONE);
	TestEqual(TEXT("and to the line editor"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);
	Submit(TEXT("12"));
	TestEqual(TEXT("`12` opens the last visible row"), Terminal->MailSelectedRow, 11);
	// `FUN_1021bc20`'s guard is `+0x9f0 < count + 1`, one too lax — but `FUN_1021bc90` re-clamps and
	// writes the row back, so walking past the end stays on the last message.
	TestTrue(TEXT("the next-message hotkey is accepted at the end of the inbox"), Submit(KeyNext));
	TestEqual(TEXT("and the open re-clamp absorbs the retail off-by-one"),
		Terminal->MailSelectedRow, 11);
	TestEqual(TEXT("so the last message stays open"), Terminal->MailOpenIndex, 11);
	TestTrue(TEXT("the prev-message hotkey is accepted"), Submit(KeyPrev));
	TestEqual(TEXT("and walks back one row"), Terminal->MailSelectedRow, 10);
	TestEqual(TEXT("opening record 10"), Terminal->MailOpenIndex, 10);

	// --- `[d]elete`, and the page that is never re-clamped -----------------------------------------
	Submit(KeyMenu);
	Submit(KeyNext);   // page 1: rows 11 and 12
	TestEqual(TEXT("the delete case starts on page 1"), Terminal->MailPage, 1);
	Submit(TEXT("11"));
	TestTrue(TEXT("the delete hotkey is accepted"), Submit(KeyDel));
	TestTrue(TEXT("`[d]elete` sets bit 0x2 on the open record"), Terminal->IsEmailDeleted(10));
	TestEqual(TEXT("and the row leaves the visible table"), Terminal->MailVisible.Num(), 11);
	TestFalse(TEXT("record 10 is no longer listed"), Terminal->MailVisible.Contains(10));
	TestEqual(TEXT("the delete redrew the list"), Terminal->MailOpenIndex, INDEX_NONE);
	TestEqual(TEXT("and left the page exactly where it was"), Terminal->MailPage, 1);
	Submit(TEXT("11"));
	Submit(KeyDel);
	TestEqual(TEXT("a second delete leaves ten visible"), Terminal->MailVisible.Num(), 10);
	// `FUN_1021bd80` never re-clamps `+0xa0c`: page 1 now starts at index 10, which is the count, so
	// the inbox draws ZERO rows and the player is stranded until they press `[p]rev`. Retail
	// off-by-one, ported.
	TestEqual(TEXT("the page is still 1"), Terminal->MailPage, 1);
	TestEqual(TEXT("deleting the last page's rows strands the player on an empty page"), Row(5),
		FString());
	ExpectedFooters(/*Count*/ 10, /*First*/ 11, /*Last*/ 10);
	TestTrue(TEXT("the prev-page hotkey rescues it"), Submit(KeyPrev));
	TestEqual(TEXT("back to page 0"), Terminal->MailPage, 0);
	TestEqual(TEXT("which draws all ten remaining rows"), Row(14), TEXT(" [10]Mail10"));
	// The second off-by-one: `[n]ext` page is `min(page + 1, count / 10)` with INTEGER division, so
	// a visible count that is an exact multiple of ten has a reachable page that draws nothing.
	TestTrue(TEXT("the next-page hotkey is accepted at exactly ten visible mails"), Submit(KeyNext));
	TestEqual(TEXT("count / 10 lets the page advance"), Terminal->MailPage, 1);
	TestEqual(TEXT("onto a page whose first index equals the count"), Row(5), FString());
	Submit(KeyPrev);

	// --- `[q]uit` from the list goes to the root directory, not out of the terminal ----------------
	TestTrue(TEXT("the quit hotkey is accepted in the list state"), Submit(KeyQuit));
	TestEqual(TEXT("`[q]uit` returns to the root directory"), Terminal->CurrentDirectory,
		INDEX_NONE);
	FElysiumTerminalView View;
	TestTrue(TEXT("and does not end the session"), World.BuildTerminalView(View));
	// `0x1021ad40`: the directory row's two numbers are the VISIBLE count and the unread count among
	// those, both off the table the directory draw itself just rebuilt — not the raw record count.
	// Ten records survive the two deletes; two of those ten (records 0 and 1) were opened, so eight
	// are unread. Records 10 and 11 were read too, but they are deleted and therefore not counted.
	TestEqual(TEXT("the directory draw reports visible and unread off the rebuilt table"), Row(5),
		(TEXT(" ") + ElysiumTerminalFormat(Get(&World, EmailCount),
			{ FElysiumTerminalArg::Num(10), FElysiumTerminalArg::Num(8) })).TrimEnd());

	// --- the flags survive the archive -------------------------------------------------------------
	const TArray<int32> Saved = Terminal->EmailFlags;
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		Terminal->Serialize(Ar);
	}
	Terminal->EmailFlags.Reset();
	Terminal->EmailFlags.SetNumZeroed(FElysiumPropHacking::EmailFlagCount);
	Terminal->bEmailUnlocked = false;
	Terminal->EmailAttempts = 0;
	{
		FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		Terminal->Serialize(Ar);
	}
	TestTrue(TEXT("the 128-flag array survives the archive"), Terminal->EmailFlags == Saved);
	TestEqual(TEXT("and it is still 128 entries long"), Terminal->EmailFlags.Num(),
		FElysiumPropHacking::EmailFlagCount);
	TestTrue(TEXT("the unlock survives with it"), Terminal->bEmailUnlocked);
	TestEqual(TEXT("and so does the (inert) attempt count"), Terminal->EmailAttempts, 1);
	World.EndPlayerUseSession(Terminal->Handle, EElysiumUseEndReason::Released);

	// --- the `global_email` reconciliation ---------------------------------------------------------
	// Retail's four `global_email 1` entities are all named `haven_pc` and live in four different
	// maps. The port models that here by writing the live `targetname` — the key the two wrappers
	// use — onto three entities in one world, so the exact-name lookup is what is under test.
	FElysiumPropHacking* HavenA = Resolve(TEXT("haven_a"));
	FElysiumPropHacking* HavenB = Resolve(TEXT("haven_b"));
	FElysiumPropHacking* HavenC = Resolve(TEXT("haven_c"));
	FElysiumPlayer* PlayerEntity = World.FindPlayer();
	if (!TestNotNull(TEXT("the three haven terminals resolve"), HavenA)
		|| !TestNotNull(TEXT("...b"), HavenB) || !TestNotNull(TEXT("...c"), HavenC)
		|| !TestNotNull(TEXT("the player entity resolves"), PlayerEntity))
	{
		return false;
	}
	for (FElysiumPropHacking* Haven : { HavenA, HavenB, HavenC })
	{
		TestTrue(TEXT("the global_email keyfield parsed"), Haven->bGlobalEmail);
		Haven->InstallDefinition(HavenDefinition());
		Haven->InputEnable();
	}
	HavenA->TargetName = TEXT("haven_pc");
	HavenB->TargetName = TEXT("haven_pc");
	HavenC->TargetName = TEXT("haven_pc2");

	auto RunHavenSession = [&](FElysiumPropHacking* Haven, bool bOpenFirstMail)
	{
		World.BeginPlayerUseSession(Haven->Handle, Player);
		const uint32 HavenSerial = Haven->SessionSerial;
		if (bOpenFirstMail)
		{
			World.SubmitTerminalCommand(Haven->Handle, HavenSerial, Get(&World, Email));
			World.SubmitTerminalCommand(Haven->Handle, HavenSerial, TEXT("sunrise"));
			World.SubmitTerminalCommand(Haven->Handle, HavenSerial, FString());
			World.SubmitTerminalCommand(Haven->Handle, HavenSerial, TEXT("1"));
			World.SubmitTerminalCommand(Haven->Handle, HavenSerial, KeyQuit);
		}
		World.EndPlayerUseSession(Haven->Handle, EElysiumUseEndReason::Released);
	};

	RunHavenSession(HavenA, /*bOpenFirstMail*/ true);
	TestTrue(TEXT("terminal A read its first mail"), HavenA->IsEmailRead(0));
	TestTrue(TEXT("and A unlocked its own mail area"), HavenA->bEmailUnlocked);
	// Entry created the record (create-on-retrieve); the exit wrote A's flags into it.
	if (TestEqual(TEXT("one record now stands on the player"),
		PlayerEntity->GlobalEmail.Num(), 1))
	{
		TestEqual(TEXT("keyed on the entity's targetname"), PlayerEntity->GlobalEmail[0].Name,
			TEXT("haven_pc"));
		TestEqual(TEXT("carrying the read bit"), PlayerEntity->GlobalEmail[0].Flags[0],
			FElysiumPropHacking::EmailFlagRead);
	}

	// B carries the same name, so entry is global-wins and the flags cross.
	TestFalse(TEXT("terminal B starts with nothing read"), HavenB->IsEmailRead(0));
	World.BeginPlayerUseSession(HavenB->Handle, Player);
	TestTrue(TEXT("entry copies the player's flags onto the same-named terminal"),
		HavenB->IsEmailRead(0));
	TestFalse(TEXT("but the unlock does NOT cross: it is per-entity saved state"),
		HavenB->bEmailUnlocked);
	TestTrue(TEXT("`email` is accepted on B"),
		World.SubmitTerminalCommand(HavenB->Handle, HavenB->SessionSerial, Get(&World, Email)));
	TestEqual(TEXT("so B prompts for the email password on its own account"), HavenB->InputMode(),
		EElysiumTerminalInputMode::Password);
	World.EndPlayerUseSession(HavenB->Handle, EElysiumUseEndReason::Released);

	// C's name differs by one character. The lookup compares over the LONGER of the two lengths, so
	// it is an exact compare and `haven_pc2` does not match `haven_pc`.
	World.BeginPlayerUseSession(HavenC->Handle, Player);
	TestFalse(TEXT("a differently named terminal shares nothing"), HavenC->IsEmailRead(0));
	TestEqual(TEXT("and create-on-retrieve gave it its own record"),
		PlayerEntity->GlobalEmail.Num(), 2);
	TestEqual(TEXT("under its own exact name"), PlayerEntity->GlobalEmail[1].Name,
		TEXT("haven_pc2"));
	World.EndPlayerUseSession(HavenC->Handle, EElysiumUseEndReason::Released);

	// --- **no create-on-store**: the hole retail leaves and warns about --------------------------
	// `CBasePlayer::StoreGlobalEmailFlags` `0x1016f0f0` looks the record up and, on a miss, warns
	// and writes NOTHING. It never creates. Retrieve always runs first at OnUseBegin, so the hole
	// is unreachable through the entry/exit pair — but the accessor is public state and a caller
	// that skipped the entry would silently lose the flags, so the arm is proven directly.
	{
		const int32 Before = PlayerEntity->GlobalEmail.Num();
		TArray<int32> Flags;
		Flags.SetNumZeroed(FElysiumPropHacking::EmailFlagCount);
		Flags[0] = FElysiumPropHacking::EmailFlagRead;
		PlayerEntity->StoreGlobalEmailFlags(TEXT("never_retrieved"), Flags);
		TestEqual(TEXT("storing under an unknown terminal name creates no record"),
			PlayerEntity->GlobalEmail.Num(), Before);
		TestFalse(TEXT("and the flags are simply lost, as retail loses them"),
			PlayerEntity->GlobalEmail.ContainsByPredicate(
				[](const FElysiumGlobalEmailRecord& Record)
				{ return Record.Name == TEXT("never_retrieved"); }));
	}

	// --- the 128-flag cap: mail 129 and up are permanently unread and undeletable -----------------
	// `m_EmailFlags` is `DEFINE_ARRAY(FIELD_INTEGER, 128)` while the record vector is unbounded, and
	// all four accessors answer FALSE out of range rather than clamping (`XOR AL,AL` at
	// `0x1021a4d3`). The visible consequence is not the flag: it is the `runscript`, which
	// `MailOpen` fires whenever the read bit is CLEAR — so a mail past the cap re-runs its script on
	// every single open, forever.
	{
		FElysiumTerminalDefinition Big;
		Big.Brackets = TEXT("[]");
		Big.EmailUsername = TEXT("Noa");
		// No `email_password`, which is also the straight-in branch asserted just below.
		for (int32 Index = 0; Index < 130; ++Index)
		{
			FElysiumTerminalEmail Mail;
			Mail.Subject = FString::Printf(TEXT("Big%03d"), Index);
			Mail.Sender = TEXT("bulk");
			Mail.Body = TEXT("Body.");
			Big.Emails.Add(MoveTemp(Mail));
		}
		Big.Emails[128].RunScript = TEXT("SCRIPT:past_the_cap");
		Big.Emails[5].RunScript = TEXT("SCRIPT:inside_the_cap");
		Terminal->InstallDefinition(MoveTemp(Big));
		TestEqual(TEXT("installing a 130-record definition still leaves exactly 128 flags"),
			Terminal->EmailFlags.Num(), FElysiumPropHacking::EmailFlagCount);
		// `InstallDefinition` RESIZES the flag array and never clears it — retail's loader runs once
		// at Spawn and the flags are saved state, so nothing in the module zeroes them on a content
		// swap. The two records this case deleted above would otherwise stay deleted under the new
		// definition and hide two of the 130.
		Terminal->EmailFlags.Reset();
		Terminal->EmailFlags.SetNumZeroed(FElysiumPropHacking::EmailFlagCount);

		World.BeginPlayerUseSession(Terminal->Handle, Player);
		Serial = Terminal->SessionSerial;
		// An EMPTY `email_password` is the straight-in branch: `FUN_1021aaa0` enters the mail area
		// without a prompt, so the very next thing on the glass is the acknowledged entry screen.
		TestTrue(TEXT("`email` is accepted with an empty email_password"),
			Submit(Get(&World, Email)));
		TestNotEqual(TEXT("an empty email_password never prompts"), Terminal->InputMode(),
			EElysiumTerminalInputMode::Password);
		TestEqual(TEXT("it goes straight into the mail area"), Terminal->CurrentDirectory,
			FElysiumPropHacking::MailArea);
		TestTrue(TEXT("and entry still sets m_bEmailUnlocked"), Terminal->bEmailUnlocked);
		Submit(FString());   // the acknowledgement, which draws the list
		TestEqual(TEXT("all 130 records are visible"), Terminal->MailVisible.Num(), 130);

		// The list arm's `atoi` is one-based over the VISIBLE table and `MailOpen` clamps by row,
		// not by page — so a row on page 13 opens from page 0 without paging to it.
		TestEqual(TEXT("nothing has run the past-the-cap script yet"),
			Host->Calls.FindRef(TEXT("SCRIPT:past_the_cap")), 0);
		TestTrue(TEXT("row 129 is accepted"), Submit(TEXT("129")));
		TestEqual(TEXT("and opens record 128"), Terminal->MailOpenIndex, 128);
		TestFalse(TEXT("but record 128 is past the 128-flag array and stays UNREAD"),
			Terminal->IsEmailRead(128));
		TestEqual(TEXT("so its runscript fired"),
			Host->Calls.FindRef(TEXT("SCRIPT:past_the_cap")), 1);
		Submit(KeyMenu);
		Submit(TEXT("129"));
		TestEqual(TEXT("and fires AGAIN on the next open, because the read bit never took"),
			Host->Calls.FindRef(TEXT("SCRIPT:past_the_cap")), 2);
		// `[d]elete` on it is equally inert: `MailDelete` passes the record-count bound and the
		// accessor then drops the write on the floor.
		TestTrue(TEXT("the delete hotkey is accepted on a record past the cap"), Submit(KeyDel));
		TestFalse(TEXT("but record 128 cannot be deleted either"), Terminal->IsEmailDeleted(128));
		TestEqual(TEXT("so it is still listed"), Terminal->MailVisible.Num(), 130);

		// A record INSIDE the cap behaves the other way, which is what makes the cap the cause.
		Submit(TEXT("6"));
		TestEqual(TEXT("row 6 opens record 5"), Terminal->MailOpenIndex, 5);
		TestTrue(TEXT("which is inside the array and reads"), Terminal->IsEmailRead(5));
		TestEqual(TEXT("firing its script once"),
			Host->Calls.FindRef(TEXT("SCRIPT:inside_the_cap")), 1);
		Submit(KeyMenu);
		Submit(TEXT("6"));
		TestEqual(TEXT("and never again"),
			Host->Calls.FindRef(TEXT("SCRIPT:inside_the_cap")), 1);

		// --- `MailDelete`'s own record-count bound -------------------------------------------------
		// `FUN_1021bbf0` guards on the RECORD count before the accessor's 128 guard, so an index
		// past the definition writes nothing at all — including the negative "no message open".
		const TArray<int32> FlagsBefore = Terminal->EmailFlags;
		Terminal->MailDelete(130);      // one past the record count
		Terminal->MailDelete(9999);
		Terminal->MailDelete(INDEX_NONE);   // the "no message open" sentinel
		TestTrue(TEXT("an out-of-record-range delete leaves every flag byte alone"),
			Terminal->EmailFlags == FlagsBefore);
		Terminal->MailDelete(7);
		TestTrue(TEXT("while an in-range one takes"), Terminal->IsEmailDeleted(7));

		// --- single-key `q` from the OPEN state goes to the root directory -------------------------
		// The open arm demands `Line.Len() == 1`, and `[q]uit` there is `EnterDirectory(-1)` — the
		// same call the list arm's `[q]uit` makes. The four-byte `hackcmd quit` the client sends for
		// ESC misses the length test and lands on the list instead, which is the case above.
		Submit(KeyMenu);
		// Record 7 is deleted by now, so visible row 9 is NOT record 8 — the table is renumbered on
		// every draw and the row is read off it rather than assumed.
		Submit(TEXT("9"));
		if (TestTrue(TEXT("the visible table still holds nine rows"), Terminal->MailVisible.Num() > 8))
		{
			TestEqual(TEXT("a message is open for the single-key quit case"),
				Terminal->MailOpenIndex, Terminal->MailVisible[8]);
		}
		TestTrue(TEXT("the single-key quit is accepted in the open state"), Submit(KeyQuit));
		TestEqual(TEXT("and leaves the mail area for the root directory"),
			Terminal->CurrentDirectory, INDEX_NONE);
		// `[q]uit` from the open state is `EnterDirectory(-1)`, and `FUN_1021c890` does not touch
		// `+0x9f4` — only `FUN_1021c040`'s first statement does. So the open-message index is left
		// STALE on the way out, which is harmless because re-entering the mail area always draws
		// the list and that draw is what clears it. Ported, not tidied.
		TestTrue(TEXT("leaving the mail area does NOT clear the open-message index"),
			Terminal->MailOpenIndex != INDEX_NONE);
		FElysiumTerminalView StillOpen;
		TestTrue(TEXT("without ending the session"), World.BuildTerminalView(StillOpen));

		// --- the raw-mode local echo (`FUN_100c6d50` has no `0x4` test) -----------------------------
		// An open message runs in single-key mode, and the client's CHARACTER body has no raw-mode
		// arm at all: the byte goes out as `hackcmd %c` AND into the client's own line, and the
		// local re-render puts it on the glass over the command it just sent. The keyboard leaf is
		// driven here directly against the live view, so the composition asserted is the one the
		// projection paints.
		Submit(Get(&World, Email));
		Submit(FString());
		TestEqual(TEXT("and the list draw on the way back in is what clears the stale index"),
			Terminal->MailOpenIndex, INDEX_NONE);
		Submit(TEXT("2"));
		TestEqual(TEXT("the open message is raw single-key mode"), Terminal->InputMode(),
			EElysiumTerminalInputMode::Raw);
		FElysiumTerminalView RawView;
		if (TestTrue(TEXT("and the session publishes a view"), World.BuildTerminalView(RawView)))
		{
			int32 Sent = 0;
			FString SentText;
			const TSharedRef<SElysiumTerminalInput> Keyboard = SNew(SElysiumTerminalInput)
				.OnSubmitCharacter(SElysiumTerminalInput::FElysiumTerminalCharDelegate::
					CreateLambda([&Sent, &SentText](TCHAR Character)
					{
						++Sent;
						SentText.AppendChar(Character);
						return true;
					}));
			Keyboard->SetSession(RawView);
			Keyboard->OnKeyChar(FGeometry(),
				FCharacterEvent(KeyNext[0], FModifierKeysState(), 0, false));
			TestEqual(TEXT("the keypress went out as one hackcmd %c"), Sent, 1);
			TestEqual(TEXT("carrying the hotkey letter"), SentText, KeyNext);
			TestEqual(TEXT("and the SAME byte was echoed into the local line"), Keyboard->Draft(),
				KeyNext);
			// ...and it is genuinely on the glass, at the edit origin the open-message draw left.
			const ElysiumTerminalCells::FElysiumTerminalComposed Composed =
				ElysiumTerminalCells::ComposeDraft(RawView, Keyboard->Draft());
			TestTrue(TEXT("composed onto the monitor's cells"), Composed.bDraftDrawn);
			TestEqual(TEXT("at the edit origin"),
				FString::Chr(Composed.CharAt(RawView.EditOriginColumn, RawView.EditOriginRow)),
				KeyNext);

			// The authority's answer is the list redraw, and its print closes the client's editor
			// and moves the epoch — which is the only thing that ends the echo.
			TestTrue(TEXT("the authority answers the hotkey"), Submit(KeyNext));
			FElysiumTerminalView Answered;
			if (TestTrue(TEXT("and republishes"), World.BuildTerminalView(Answered)))
			{
				TestNotEqual(TEXT("the redraw moved the edit epoch"), Answered.EditEpoch,
					RawView.EditEpoch);
				Keyboard->SetSession(Answered);
				TestTrue(TEXT("so the echoed key is gone from the local line"),
					Keyboard->Draft().IsEmpty());
			}
		}
		World.EndPlayerUseSession(Terminal->Handle, EElysiumUseEndReason::Released);
	}
	return true;
}

// The shipped `haven_pc.txt` — 45 `Email` records, the only file in the corpus with more than the
// ten a page holds, and the one that authors dependencies, `runscript`s and the inert `autodelete`.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalEmailContentTest,
	"Elysium.Content.TerminalEmailContent", GMailFlags)
bool FElysiumTerminalEmailContentTest::RunTest(const FString&)
{
	using namespace ElysiumHackingStrings;

	FElysiumTerminalDefinition Definition;
	FString Error;
	if (!FElysiumTerminalDefinition::Load(TEXT("vdata/hackterminals/haven_pc.txt"),
		Definition, Error))
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: %s"), *Error));
		return true;
	}
	// 53, not the 45 the exploration report counted: eight of the `Email` block headers carry a
	// trailing `// added by wesp` comment, which a bare-line grep misses and the KeyValues parser
	// does not. The loader's vector is unbounded, so all 53 are records; only the 128-entry flag
	// array is capped, and 53 is well inside it.
	TestEqual(TEXT("haven_pc.txt authors 53 Email records"), Definition.Emails.Num(), 53);
	TestEqual(TEXT("with the authored mail username"), Definition.EmailUsername, TEXT("Noa"));
	TestEqual(TEXT("and the authored mail password"), Definition.EmailPassword, TEXT("sunrise"));
	// The loader reads five `Email` keys; `autodelete` is not one of them and nothing in the module
	// reads the parsed value, so it is carried and inert.
	TestTrue(TEXT("at least one record authors the inert `autodelete`"),
		Definition.Emails.ContainsByPredicate(
			[](const FElysiumTerminalEmail& Record) { return Record.bAutoDelete; }));

	UElysiumGameStateSubsystem* State = MailHeadlessGameState();
	State->SetScriptHost(MakeUnique<FElysiumExprScriptHost>(State));

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__terminal_email_content__");
	Defs.Defs.Add(TerminalDef(TEXT("haven_pc"), /*bGlobalEmail*/ true));

	FElysiumRecordingServices Services;
	Services.StandTerminalScreen();
	FElysiumEntityWorld World(nullptr, State, Services.Bundle());
	AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumEntity* Entity = World.FindByName(TEXT("haven_pc"));
	FElysiumTerminal* Base = Entity ? Entity->AsTerminal() : nullptr;
	if (!TestNotNull(TEXT("the terminal resolves"), Base))
	{
		return false;
	}
	FElysiumPropHacking* Terminal = static_cast<FElysiumPropHacking*>(Base);
	Terminal->InstallDefinition(Definition);
	Terminal->InputEnable();
	if (!TestEqual(TEXT("+use opens the session"),
		World.BeginPlayerUseSession(Terminal->Handle, Player).Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		return false;
	}
	const uint32 Serial = Terminal->SessionSerial;
	auto Submit = [&](const FString& Command)
	{
		return World.SubmitTerminalCommand(Terminal->Handle, Serial, Command);
	};
	Submit(Get(&World, Email));
	TestEqual(TEXT("the shipped mail area is behind its authored password"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Password);
	Submit(TEXT("sunrise"));
	TestEqual(TEXT("which opens onto the acknowledge prompt"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Acknowledge);
	Submit(FString());
	TestEqual(TEXT("and the acknowledging empty line draws the inbox"), Terminal->CurrentDirectory,
		FElysiumPropHacking::MailArea);

	// Every dependency in the file reads `G` globals that a fresh game state leaves unset, so the
	// visible set is whatever `FUN_1021bd80` makes of a story at state zero. What is asserted is
	// the SHAPE: a non-empty inbox, no more than ten rows on a page, and the first row and header
	// built from the same literals `FUN_1021c040` uses.
	const int32 Visible = Terminal->MailVisible.Num();
	AddInfo(FString::Printf(TEXT("haven_pc.txt lists %d of its %d records at story state zero"),
		Visible, Definition.Emails.Num()));
	if (!TestTrue(TEXT("the shipped inbox lists at least one mail"), Visible > 0))
	{
		return false;
	}
	const int32 Columns = Terminal->TextColumns;
	auto Row = [Terminal](int32 Index) { return Terminal->Screen.RowTextTrimmed(Index); };
	{
		const FString Title = ElysiumTerminalFormat(TEXT("%s %s"),
			{ Get(&World, EmailTitleBar), Definition.EmailUsername });
		TestEqual(TEXT("the header is \"<string 29> <email_username>\""), Row(2),
			MailBoxRow(Columns, Title, (Columns - Title.Len() - 2) / 2));
	}
	{
		const FString Subject = Definition.Emails[Terminal->MailVisible[0]].Subject;
		const FString Expected = ElysiumTerminalFormat(TEXT("[%d]"),
			{ FElysiumTerminalArg::Num(1) }) + ElysiumTerminalFormat(TEXT("%s"), { Subject });
		TestEqual(TEXT("the first row is `[1]` then the first visible record's subject"), Row(5),
			(TEXT(" ") + Expected).TrimEnd());
	}
	TestEqual(TEXT("a page holds at most ten rows"), Row(15), FString());
	TestEqual(TEXT("and the footer counts what the visible table holds"),
		Row(Terminal->TextRows - 3),
		(TEXT(" ") + ElysiumTerminalFormat(GetOrEmpty(&World, MailListCount),
			{ FElysiumTerminalArg::Num(Visible), FElysiumTerminalArg::Num(1),
			  FElysiumTerminalArg::Num(FMath::Min(Visible, 10)) })).TrimEnd());
	World.EndPlayerUseSession(Terminal->Handle, EElysiumUseEndReason::Released);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
