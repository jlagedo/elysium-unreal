// The tutorial terminal on the real map slice (docs/project/plans/terminals.md, slices A0 and A).
//
// `sp_tutorial_1`'s `tuthack` and everything its rows reach are cut out of the exported `.ents`,
// spawned headless, and driven through the same command bus the game uses. The authored wires —
// not a hand-built `math_counter` — are what the transaction is proven against, so a map/port
// mismatch fails here and nowhere later. Abstains, never fails, without the export.
//
// Slice A adds the screen: every draw is compared cell for cell against the owner's retail
// captures under `$ELYSIUM_WORK_ROOT/_terminal_explore/retail-shots/`, read through
// `FElysiumTerminalScreenBuffer::RowTextTrimmed`.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"
#include "Engine/GameInstance.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameClock.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumScriptHost.h"
#include "ElysiumViewState.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemContainer.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumLockable.h"
#include "Substrate/ElysiumTerminal.h"
#include "Tests/ElysiumEntityDebugStateTestHelpers.h"
#include "Tests/ElysiumMapSlice.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumTerminalSliceTests
{
static constexpr EAutomationTestFlags GSliceFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// UElysiumGameStateSubsystem is a UGameInstanceSubsystem, so it is outered to a throwaway
// UGameInstance for a valid, un-Initialized state that carries the `G` store and a script host.
static UElysiumGameStateSubsystem* MakeHeadlessGameState()
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	return NewObject<UElysiumGameStateSubsystem>(GameInstance);
}

// A trigger's enable state, whichever row the leaf publishes it under.
static bool TriggerEnabled(const FElysiumEntity* Entity)
{
	const FString Enabled = ElysiumEntityDebugTest::Row(Entity, TEXT("Enabled"));
	if (!Enabled.IsEmpty())
	{
		return Enabled == TEXT("yes");
	}
	const FString Disabled = ElysiumEntityDebugTest::Row(Entity, TEXT("Disabled"));
	if (!Disabled.IsEmpty())
	{
		return Disabled == TEXT("no");
	}
	return ElysiumEntityDebugTest::Row(Entity, TEXT("Armed")) == TEXT("yes");
}

static bool Locked(const FElysiumEntity* Entity)
{
	return ElysiumEntityDebugTest::Row(Entity, TEXT("Locked")) == TEXT("yes");
}

static const TCHAR* GSliceMap = TEXT("sp_tutorial_1");

static const TArray<FString>& TerminalSliceRoots()
{
	static const TArray<FString> Roots = {
		TEXT("tuthack"), TEXT("tutsafelock"), TEXT("tutsafe"), TEXT("trig_popup_safe"),
		TEXT("trig_popup_note"), TEXT("item_k_tutorial_chopshop_stairs_key"),
		TEXT("tutdoordknob"), TEXT("tutdoordknob-wesp"), TEXT("tutchopdoord") };
	return Roots;
}

// The framed-box rows the title box draws, as `FUN_1021b2b0` / `FUN_1021b330` build them: exactly
// `columns - 2` characters from the left margin, so on a 36-column screen the corners sit at
// columns 1 and 34 and column 0 stays blank. `RowTextTrimmed` keeps that leading blank.
//
// NOTE against the capture: `docs/vtmb/computer-terminals.md` §8.6 summarises the rule row as
// `'+' + (columns-5)*'-' + '+'`. The listing is `memset(buf,'-',n-3); buf[0]='+'; buf[n-3]='+';`,
// which is `'+' + (columns-4)*'-' + '+'` = `columns - 2` characters — the same total width §8.6
// states two clauses later. The decompile wins; 32 dashes on a 36-column screen.
static FString BoxRule(int32 Columns)
{
	return FString(TEXT(" +")) + FString::ChrN(Columns - 4, TEXT('-')) + TEXT("+");
}

// One framed row: `|`, the row's spaces, the text blitted at `Margin`, `|`.
static FString BoxRow(int32 Columns, const FString& Text, int32 Margin)
{
	FString Row = FString::ChrN(Columns - 2, TEXT(' '));
	Row[0] = TEXT('|');
	Row[Columns - 3] = TEXT('|');
	for (int32 Offset = 0; Offset < Text.Len() && Margin + Offset < Columns - 2; ++Offset)
	{
		Row[Margin + Offset] = Text[Offset];
	}
	return TEXT(" ") + Row;
}
}

using namespace ElysiumTerminalSliceTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTutorialTerminalSliceTest,
	"Elysium.Substrate.TutorialTerminalSlice", GSliceFlags)
bool FElysiumTutorialTerminalSliceTest::RunTest(const FString&)
{
	if (!FElysiumMapSlice::Available(GSliceMap))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: sp_tutorial_1.ents is not under $ELYSIUM_EXPORT_ROOT"));
		return true;
	}
	FElysiumMapSlice Slice;
	FString Error;
	if (!FElysiumMapSlice::Build(GSliceMap, TerminalSliceRoots(), Slice, Error))
	{
		AddError(Error);
		return false;
	}
	AddInfo(Slice.Report());
	for (const TCHAR* Name : { TEXT("tuthack"), TEXT("tutsafelock"), TEXT("tutsafe"),
		TEXT("trig_popup_safe"), TEXT("trig_popup_note"), TEXT("tutdoordknob"),
		TEXT("tutdoordknob-wesp"), TEXT("tutchopdoord"), TEXT("trig_jack_teleport_3") })
	{
		TestTrue(FString::Printf(TEXT("the slice keeps %s"), Name), Slice.Kept.Contains(Name));
	}

	// The keycard is an `equip0` seed on the safe; it spawns only through the installed catalogue,
	// exactly as the game installs it before a map's item entities exist.
	FElysiumItemTable Items;
	FString ItemsError;
	const bool bItems = Items.Load(ItemsError);
	if (!bItems)
	{
		AddInfo(FString::Printf(TEXT("seam: item catalogue unavailable (%s); the keycard cannot spawn"),
			*ItemsError));
	}
	else
	{
		ElysiumItems::Install(Items);
	}
	ON_SCOPE_EXIT { if (bItems) { ElysiumItems::Uninstall(Items); } };

	UElysiumGameStateSubsystem* State = ElysiumTerminalSliceTests::MakeHeadlessGameState();
	State->SetScriptHost(MakeUnique<FElysiumExprScriptHost>(State));

	FElysiumRecordingServices Services;
	// The slice has no bodies, so the double supplies the `screen` / `screen_axis` pair the cone
	// reads and stands the eye on its axis.
	Services.StandTerminalScreen();
	// Slice C: the idle terminal views are published for a terminal that has a BODY, and the double
	// answers "has a body" the way the map actor does — through the use anchor's world bounds.
	Services.bHasUseBodyBounds = true;
	Services.UseBodyBounds = FBox(FVector(-30.0f, -30.0f, 0.0f), FVector(30.0f, 30.0f, 60.0f));
	// The knobs parent to the rotating door's brush, which has no body headless (a slice seam).
	AddExpectedError(TEXT("resolved parent 'tutchopdoord', but its attachment body is unavailable"),
		EAutomationExpectedErrorFlags::Contains, 2);
	FElysiumEntityWorld World(nullptr, State, Services.Bundle());
	World.Load(MoveTemp(Slice.Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);
	// The world's clock is the game state's, and every delayed row is stamped on it — while the
	// queue is serviced with the tick argument. `AdvanceFrame` is bounded by `Host_FilterTime`'s
	// 0.1 s (`ElysiumFrame::ClampFrameDelta`), so a test that jumped half a second in one call
	// would stamp its rows on a clock that had only moved 0.1 s and fire them early. Step the
	// clock in real frames and tick on the clock's own reading, exactly as the map actor does.
	auto Advance = [&](double To)
	{
		for (int32 Guard = 0; Guard < 4096 && State->GameClock().GetNow() < To; ++Guard)
		{
			// The last step is whatever is left, which `ClampFrameDelta` raises to its 1 ms floor,
			// so the clock lands on or just past `To` and a row due exactly at `To` is delivered.
			State->TimeControl().AdvanceFrame(FMath::Min(0.05, To - State->GameClock().GetNow()));
			World.Tick(State->GameClock().GetNow());
		}
		World.Tick(State->GameClock().GetNow());
	};
	// Seeded before the first tick: the screensaver's row/column/style draws are the Terminal
	// stream's, and this case measures the schedule those draws sit on.
	ElysiumRng::SeedAll(20260907);
	Advance(0.0);   // seeds materialize on the safe's first think, before any command

	FElysiumEntity* TerminalEntity = World.FindByName(TEXT("tuthack"));
	FElysiumTerminal* BaseTerminal = TerminalEntity ? TerminalEntity->AsTerminal() : nullptr;
	if (!TestNotNull(TEXT("tuthack resolves as a terminal"), BaseTerminal))
	{
		return false;
	}
	FElysiumPropHacking* Terminal = static_cast<FElysiumPropHacking*>(BaseTerminal);
	TestTrue(TEXT("tuthack loaded its authored hack_file"), Terminal->bStartEnabled);
	if (!TestEqual(TEXT("tutorial_computer.txt presents one directory"),
		Terminal->Definition.Directories.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("the directory is Safe"), Terminal->Definition.Directories[0].Name, TEXT("Safe"));
	TestEqual(TEXT("the logon line is authored"), Terminal->Definition.LogonLines.Num(), 1);
	TestEqual(TEXT("the grid is 36x24"), Terminal->TextColumns * 100 + Terminal->TextRows, 3624);
	TestTrue(TEXT("the tutorial authors empty brackets"), Terminal->Definition.Brackets.IsEmpty());

	const int32 Columns = Terminal->TextColumns;
	auto Row = [Terminal](int32 Index) { return Terminal->Screen.RowTextTrimmed(Index); };
	auto DumpScreen = [this, Terminal](const TCHAR* Label)
	{
		FString Text = FString::Printf(TEXT("screen after %s:"), Label);
		for (int32 Index = 0; Index < Terminal->Screen.Rows(); ++Index)
		{
			Text += FString::Printf(TEXT("\n  %2d |%s|"), Index,
				*Terminal->Screen.RowTextTrimmed(Index));
		}
		AddInfo(Text);
	};

	// --- slice C: the machine draws itself before anyone touches it ---------------------------
	// `CPropHacking::vfunc113` `0x1021a270`: Activate puts the `LogonScreen` box on the glass and
	// arms the FIRST screensaver tick inside the first second, then floors the authored
	// `ss_delay 1.5` at 2.0. `ss_start 5.0` is never floored.
	TestEqual(TEXT("the slice authors ss_delay 1.5, floored to 2.0 at Activate"),
		Terminal->ScreenSaverDelay, FElysiumPropHacking::ScreenSaverDelayFloor);
	TestEqual(TEXT("and ss_start 5.0, unfloored"), Terminal->ScreenSaverStart, 5.0f);
	TestTrue(TEXT("Activate armed the first screensaver tick inside the first second"),
		Terminal->NextThink >= 0.0f && Terminal->NextThink < 1.0f);
	{
		TArray<FElysiumTerminalView> Idle;
		World.BuildIdleTerminalViews(Idle);
		const FElysiumTerminalView* Tuthack = Idle.FindByPredicate(
			[Terminal](const FElysiumTerminalView& Candidate)
			{ return Candidate.Owner == Terminal->Handle; });
		if (TestNotNull(TEXT("tuthack is published as an idle terminal before the first session"),
			Tuthack))
		{
			TestEqual(TEXT("with no session serial"), Tuthack->SessionSerial, 0u);
			TestEqual(TEXT("and the whole 36x24 grid"), Tuthack->Cells.Num(), 36 * 24);
			// An idle view is the GLASS and nothing else. The action list is a live session's
			// affordance, and building it walks every directory's and function's authored
			// `dependency` through the script host — per terminal, per frame, ahead of the redraw
			// gate. Retail's idle machine does no work beyond its think.
			TestEqual(TEXT("an idle view carries no session action list"), Tuthack->Actions.Num(), 0);
			// ... but it does carry the label, because that is what the screensaver draws.
			TestEqual(TEXT("and does carry the authored screensaver label"),
				Tuthack->ScreenSaverLabel, TEXT("Brothers Downtown Garage"));
			// And it is not a session: serial 0 is exactly what `IsOpen` has to refuse, or the
			// CommonUI screen would open over a machine nobody is standing at.
			TestFalse(TEXT("an idle view does not read as an open session"), Tuthack->IsOpen());
		}
	}
	// The first think, and the authored label is on the glass with nothing else. The reschedule is
	// measured off the moment the tick RAN, not off the end of the advance.
	const float FirstArmedAt = Terminal->NextThink;
	Advance(1.0);
	{
		int32 LabelRow = INDEX_NONE;
		int32 LabelColumn = INDEX_NONE;
		for (int32 Index = 0; Index < Terminal->Screen.Rows() && LabelRow == INDEX_NONE; ++Index)
		{
			LabelColumn = Terminal->Screen.RowText(Index).Find(TEXT("Brothers Downtown Garage"),
				ESearchCase::CaseSensitive);
			LabelRow = LabelColumn == INDEX_NONE ? INDEX_NONE : Index;
		}
		TestTrue(TEXT("the first screensaver tick prints the authored label"),
			LabelRow != INDEX_NONE);
		if (LabelRow != INDEX_NONE)
		{
			// Row `[1, rows-1]`, column `[1, max(0, columns - len)]` — the label never sits on row 0
			// or column 0 (`0x1021a788`-`0x1021a7a1`).
			TestTrue(FString::Printf(TEXT("at row %d, column %d, inside the recovered bounds"),
				LabelRow, LabelColumn),
				LabelRow >= 1 && LabelRow <= Terminal->Screen.Rows() - 1
					&& LabelColumn >= 1
					&& LabelColumn <= FMath::Max(0, Terminal->Screen.Columns() - 24));
		}
	}
	TestTrue(TEXT("the type-7 reset left both margins at 0"),
		Terminal->Screen.LeftMargin() == 0 && Terminal->Screen.RightMargin() == 0);
	TestTrue(TEXT("and it rescheduled itself the floored ss_delay later"),
		FMath::IsNearlyEqual(Terminal->NextThink,
			FirstArmedAt + FElysiumPropHacking::ScreenSaverDelayFloor, 0.06f));

	FElysiumEntity* Padlock = World.FindByName(TEXT("tutsafelock"));
	FElysiumEntity* SafeEntity = World.FindByName(TEXT("tutsafe"));
	FElysiumEntity* PopupSafe = World.FindByName(TEXT("trig_popup_safe"));
	FElysiumEntity* PopupNote = World.FindByName(TEXT("trig_popup_note"));
	FElysiumEntity* Teleport = World.FindByName(TEXT("trig_jack_teleport_3"));
	FElysiumEntity* KnobA = World.FindByName(TEXT("tutdoordknob"));
	FElysiumEntity* KnobB = World.FindByName(TEXT("tutdoordknob-wesp"));
	if (!TestNotNull(TEXT("tutsafelock resolves"), Padlock)
		|| !TestNotNull(TEXT("tutsafe resolves"), SafeEntity)
		|| !TestNotNull(TEXT("trig_popup_safe resolves"), PopupSafe)
		|| !TestNotNull(TEXT("trig_popup_note resolves"), PopupNote)
		|| !TestNotNull(TEXT("trig_jack_teleport_3 resolves"), Teleport)
		|| !TestNotNull(TEXT("tutdoordknob resolves"), KnobA)
		|| !TestNotNull(TEXT("tutdoordknob-wesp resolves"), KnobB))
	{
		return false;
	}
	TestTrue(TEXT("the padlock starts locked (requires_key, difficulty 5)"), Locked(Padlock));
	TestFalse(TEXT("trig_popup_safe starts disabled"), TriggerEnabled(PopupSafe));
	TestFalse(TEXT("trig_jack_teleport_3 starts disabled"), TriggerEnabled(Teleport));
	TestTrue(TEXT("the door knobs start locked"), Locked(KnobA) && Locked(KnobB));

	// --- slice F: the four `soundgroup` cues -------------------------------------------------
	// `tuthack` authors `"soundgroup" "old_computer"`, and `FElysiumTerminal::Spawn` resolves
	// `computers/old_computer` through the exported `usable/soundgroups.json`. The four cue SITES
	// are the retail ones (`docs/vtmb/computer-terminals.md` §14): `access` at entry
	// (`0x102181ae`), `error` on every password-prompt render and the first line of an invalid
	// command, `accept` at the end of every `EnterDirectory` arm (`0x1021c890`), `typing` at the
	// cracking start (`0x10217b30`) and at its flush (`FUN_10217d60`). The executor is silent.
	TestEqual(TEXT("tuthack authors soundgroup old_computer"), Terminal->SoundGroup,
		TEXT("old_computer"));
	const bool bCues = Terminal->HasCue(TEXT("access")) && Terminal->HasCue(TEXT("accept"))
		&& Terminal->HasCue(TEXT("error")) && Terminal->HasCue(TEXT("typing"));
	if (!bCues)
	{
		AddInfo(TEXT("seam: usable/soundgroups.json resolves no computers/old_computer group; ")
			TEXT("the cue-site assertions are skipped"));
	}
	auto CueCount = [&Services](const TCHAR* Cue)
	{
		return Services.Count(FString::Printf(
			TEXT("Submit usable/computers/old_computer/%s.wav"), Cue));
	};
	// The four counts as one snapshot, so a site can be asserted BOTH for what it plays and for
	// what it must not.
	struct FCueSnapshot { int32 Access = 0; int32 Accept = 0; int32 Error = 0; int32 Typing = 0; };
	auto SnapCues = [&CueCount]()
	{
		return FCueSnapshot{ CueCount(TEXT("access")), CueCount(TEXT("accept")),
			CueCount(TEXT("error")), CueCount(TEXT("typing")) };
	};
	auto ExpectCues = [this, &SnapCues, bCues](const TCHAR* Where, const FCueSnapshot& Before,
		int32 Access, int32 Accept, int32 Error, int32 Typing)
	{
		if (!bCues)
		{
			return;
		}
		const FCueSnapshot After = SnapCues();
		TestEqual(FString::Printf(TEXT("%s plays %d access"), Where, Access),
			After.Access - Before.Access, Access);
		TestEqual(FString::Printf(TEXT("%s plays %d accept"), Where, Accept),
			After.Accept - Before.Accept, Accept);
		TestEqual(FString::Printf(TEXT("%s plays %d error"), Where, Error),
			After.Error - Before.Error, Error);
		TestEqual(FString::Printf(TEXT("%s plays %d typing"), Where, Typing),
			After.Typing - Before.Typing, Typing);
	};

	// --- Unlock ---
	FCueSnapshot Cues = SnapCues();
	const FElysiumUseBeginResult Opened = World.BeginPlayerUseSession(Terminal->Handle, PlayerHandle);
	if (!TestEqual(TEXT("+use on tuthack starts the session"), Opened.Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		return false;
	}
	// `CBaseTerminal::vfunc39` step 2: one `access`, and the root draw and prompt are silent.
	ExpectCues(TEXT("entry"), Cues, /*access*/ 1, /*accept*/ 0, /*error*/ 0, /*typing*/ 0);
	const uint32 Serial = Terminal->SessionSerial;
	// Entry step 4 (`0x1021a5d6`): `ThinkSet(NULL)`. Nothing else guards the think.
	TestEqual(TEXT("entry cancels the screensaver think"), Terminal->NextThink,
		ELYSIUM_NEVER_THINK);
	{
		TArray<FElysiumTerminalView> Idle;
		World.BuildIdleTerminalViews(Idle);
		TestFalse(TEXT("and the idle list drops tuthack for the length of the session"),
			Idle.ContainsByPredicate([Terminal](const FElysiumTerminalView& Candidate)
				{ return Candidate.Owner == Terminal->Handle; }));
	}

	// --- slice B on the slice: the hold, the cone and the camera handle ----------------------
	FElysiumPlayer* SessionPlayer = World.FindPlayer();
	if (!TestNotNull(TEXT("the player entity resolves"), SessionPlayer))
	{
		return false;
	}
	TestFalse(TEXT("entry immobilizes the player"), SessionPlayer->IsMobile());
	TestTrue(TEXT("entry pushed the Hacking shot and kept its handle"), Terminal->CameraShot != 0);
	TestEqual(TEXT("exactly one Hacking shot for the session"),
		Services.Count(TEXT("PushCameraShotNamed special-case:Hacking exposure=clamped")), 1);
	{
		// The gate is the recovered cone over the two attachment points the double supplied.
		FElysiumUseContext Gate;
		Gate.Owner = Terminal->Handle;
		Gate.Activator = PlayerHandle;
		Gate.bHasEyeOrigin = true;
		Gate.EyeOrigin = Services.PlayerLocation;
		TestTrue(TEXT("the eye on the screen's axis is inside the cone"),
			Terminal->CanPlayerFocus(Gate));
		Gate.EyeOrigin = FVector(0.0f, 300.0f, 120.0f);   // 90 degrees off the glass
		TestFalse(TEXT("the same eye 90 degrees off is outside it"),
			Terminal->CanPlayerFocus(Gate));
	}

	FElysiumTerminalView View;
	TestTrue(TEXT("the session publishes a view"), World.BuildTerminalView(View));
	TestTrue(TEXT("and it reads as open"), View.IsOpen());
	TestTrue(TEXT("a live view DOES carry the action list"), View.Actions.Num() > 0);
	TestEqual(TEXT("the view carries the authored screensaver"), View.ScreenSaverLabel,
		TEXT("Brothers Downtown Garage"));
	TestEqual(TEXT("the view carries the whole 36x24 cell grid"), View.Cells.Num(), 36 * 24);

	// --- The root draw, cell for cell against `retail-shots/01-home-menu.png` ---
	// `FUN_1021aca0` at `+0x9dc == -1`: the LogonScreen title box, string 43 "Home menu" + two
	// blank rows, "Available menus:" and the one dependency-passing directory by its lowercased
	// name, "Available commands:" and — at root only — string 34 `help` and string 33 `quit`.
	// Then `FUN_1021b410`: string 42 on `rows - 2` at the margin, and the `"%c%s@%s%c "` prefix on
	// `rows - 1`, which the empty `brackets` truncate to nothing (§8.6). The margins are (1, 1)
	// for the whole draw, so every row starts at column 1 and the box corners sit at 1 and 34.
	DumpScreen(TEXT("+use (root draw)"));
	{
		TArray<FString> Expected;
		Expected.SetNum(24);
		Expected[0] = BoxRule(Columns);
		Expected[1] = BoxRow(Columns, FString(), 10);
		Expected[2] = BoxRow(Columns, TEXT("Welcome, Jack."), 10);   // (36 - 14 - 2) / 2 = 10
		Expected[3] = BoxRow(Columns, FString(), 10);
		Expected[4] = BoxRule(Columns);
		Expected[5] = TEXT(" Home menu");
		Expected[7] = TEXT(" Available menus:");
		Expected[8] = TEXT("    safe");
		Expected[10] = TEXT(" Available commands:");
		Expected[11] = TEXT("    help");
		Expected[12] = TEXT("    quit");
		Expected[22] = TEXT(" Type menu or command:");
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("root draw row %d"), Index), Row(Index), Expected[Index]);
		}
		TestEqual(TEXT("the input row is bare and the cursor waits on it"),
			Terminal->Screen.CursorRow() * 100 + Terminal->Screen.CursorColumn(), 2301);
	}

	// --- `Safe` asks for its password; the HUD hint is raised on every prompt render ---
	Cues = SnapCues();
	TestTrue(TEXT("Safe is accepted"), World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("Safe")));
	ExpectCues(TEXT("the Safe password prompt"), Cues, 0, 0, /*error*/ 1, 0);
	TestEqual(TEXT("Safe asks for its password"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Password);
	DumpScreen(TEXT("Safe (password required)"));
	TestEqual(TEXT("the first password prompt titles string 14"), Row(2),
		BoxRow(Columns, TEXT("Password required"), 8));   // (36 - 17 - 2) / 2 = 8
	TestEqual(TEXT("the login prompt sits on the last row"), Row(23), TEXT(" Password:"));
	TestTrue(TEXT("the prompt publishes a view"), World.BuildTerminalView(View));
	TestEqual(TEXT("InfoCtrl type 3 is raised on the first password prompt"), View.HudHintType, 3);

	// --- A wrong password reproduces `retail-shots/04-password-failed.png` ---
	// `FUN_1021b5e0(this, 1)`: title 15, the notify through `"\n%s %c%s%c\n\n"` whose first `%c`
	// is the empty `brackets`' NUL — so the print stops after the sentence and its trailing space,
	// dropping the directory name and both newlines, and string 21 continues the same wrapped line.
	Cues = SnapCues();
	TestTrue(TEXT("a wrong password is accepted as a line"),
		World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("x")));
	ExpectCues(TEXT("the retry prompt after a wrong password"), Cues, 0, 0, /*error*/ 1, 0);
	DumpScreen(TEXT("x (password failed)"));
	TestEqual(TEXT("the retry titles string 15"), Row(2),
		BoxRow(Columns, TEXT("PASSWORD FAILED"), 9));      // (36 - 15 - 2) / 2 = 9
	TestEqual(TEXT("the notify wraps at the right margin"), Row(6),
		TEXT(" A password is required to enter"));
	TestEqual(TEXT("the retry line follows the truncated notify on the same wrapped line"), Row(7),
		TEXT(" this subdirectory. [Press \"ENTER\""));
	TestEqual(TEXT("and finishes on the next row"), Row(8), TEXT(" to go back]"));
	TestEqual(TEXT("the login prompt is still on the last row"), Row(23), TEXT(" Password:"));
	TestEqual(TEXT("the failed attempt counted against the directory"),
		Terminal->DirectoryAttempts[0], 1);
	TestTrue(TEXT("the failed prompt publishes a view"), World.BuildTerminalView(View));
	TestEqual(TEXT("InfoCtrl type 3 is raised again on the retry prompt"), View.HudHintType, 3);

	// --- `quit` at a password prompt cancels to the directory, it does not end the session ---
	// `FUN_10217f50` slot 277: pending cleared, the current directory redrawn. The session lives.
	Cues = SnapCues();
	TestTrue(TEXT("quit at the password prompt is accepted"),
		World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("quit")));
	// `FUN_10217f50`'s cancel arm redraws through `DirectoryDraw`, not `EnterDirectory`, so it is
	// silent — the `accept` cue belongs to the directory transition, not to the redraw.
	ExpectCues(TEXT("quit at the password prompt"), Cues, 0, 0, 0, 0);
	TestTrue(TEXT("quit at the password prompt keeps the session open"),
		World.BuildTerminalView(View));
	TestEqual(TEXT("quit at the password prompt returns to line mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);
	TestEqual(TEXT("quit at the password prompt redraws the root directory"), Row(5),
		TEXT(" Home menu"));
	TestEqual(TEXT("with the root prompt back on row 22"), Row(22),
		TEXT(" Type menu or command:"));
	TestTrue(TEXT("and the session serial is unchanged"), Terminal->SessionSerial == Serial);

	// --- The password, then the Function ---
	World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("Safe"));
	Cues = SnapCues();
	TestTrue(TEXT("chopshop is accepted"),
		World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("chopshop")));
	ExpectCues(TEXT("the accepted password's directory entry"), Cues, 0, /*accept*/ 1, 0, 0);
	TestEqual(TEXT("chopshop enters Safe"), Terminal->CurrentDirectory, 0);
	DumpScreen(TEXT("chopshop (password succeeded)"));
	TestEqual(TEXT("the accepted password titles string 16"), Row(2),
		BoxRow(Columns, TEXT("Password succeeded"), 8));   // (36 - 18 - 2) / 2 = 8
	TestTrue(TEXT("string 19 echoes the accepted password back at the player"),
		Row(6).Contains(TEXT("chopshop")));
	TestEqual(TEXT("the unlock screen waits on the acknowledge prompt"), Row(23),
		TEXT(" [Press \"ENTER\" to continue]"));
	TestEqual(TEXT("a first unlock enters acknowledge mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Acknowledge);
	TestTrue(TEXT("the accepted line publishes a view"), World.BuildTerminalView(View));
	TestEqual(TEXT("the HUD hint is cleared on the next accepted line"), View.HudHintType, 0);

	World.SubmitTerminalCommand(Terminal->Handle, Serial, FString());   // Enter past the accept
	DumpScreen(TEXT("Enter (the Safe menu)"));
	// `FUN_1021aca0` inside a directory: `"%s %s\n\n"` (`0x105b0630`) with the loader-lowercased
	// name first-letter-uppercased (`toupper(buf[0])`, 0x1021ada6) and string 44 second, then
	// `home` under the menus and the directory's own functions under the commands.
	TestEqual(TEXT("the in-directory header is the name then string 44"), Row(5), TEXT(" Safe Menu"));
	TestEqual(TEXT("the in-directory menu list offers home"), Row(8), TEXT("    home"));
	TestEqual(TEXT("the directory's functions are listed lowercased"),
		Row(11) + Row(12), TEXT("    unlock    lock"));
	TestEqual(TEXT("Enter leaves acknowledge mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);

	Cues = SnapCues();
	TestTrue(TEXT("Unlock is accepted"),
		World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("Unlock")));
	// `FUN_1021c6d0` reaches no cue site at all: the function executor is silent in retail.
	ExpectCues(TEXT("the Unlock executor"), Cues, 0, 0, 0, 0);
	DumpScreen(TEXT("Unlock (the function executor)"));
	TestEqual(TEXT("the executor titles the current directory's description"), Row(2),
		BoxRow(Columns, TEXT("Safe Security Controls"), 6));   // (36 - 22 - 2) / 2 = 6
	TestEqual(TEXT("the authored runtext is printed after one blank row"), Row(6),
		TEXT(" Safe doors unlocked."));
	TestEqual(TEXT("the executor ends on the acknowledge prompt"), Row(23),
		TEXT(" [Press \"ENTER\" to continue]"));
	TestEqual(TEXT("the executor leaves acknowledge mode set"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Acknowledge);
	World.SubmitTerminalCommand(Terminal->Handle, Serial, FString());   // Enter past the runtext
	TestEqual(TEXT("Enter after the function redraws the Safe menu"), Row(5), TEXT(" Safe Menu"));

	// Relative to the clock the Unlock was issued on: the screensaver schedule above has already
	// moved it, and these two rows are `+0.0` and `+0.5` from the transaction, not from map load.
	const double UnlockIssued = State->GameClock().GetNow();
	Advance(UnlockIssued);
	TestFalse(TEXT("OnTrigger0 unlocked tutsafelock through the authored row"), Locked(Padlock));
	TestTrue(TEXT("OnTrigger0 enabled trig_popup_safe"), TriggerEnabled(PopupSafe));
	TestFalse(TEXT("OnTrigger0 disabled trig_popup_note"), TriggerEnabled(PopupNote));
	TestFalse(TEXT("the padlock is still drawn before the delayed hide"), Padlock->IsHidden());
	Advance(UnlockIssued + 0.5);
	TestTrue(TEXT("tutsafelock is hidden by the +0.5 s ScriptHide row"), Padlock->IsHidden());

	// `home` is `FUN_1021aaa0`'s last builtin and goes through `EnterDirectory(-1)`, whose tail is
	// the `accept` cue on BOTH arms — the root return sounds exactly like entering a directory.
	Cues = SnapCues();
	TestTrue(TEXT("home is accepted"),
		World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("home")));
	TestEqual(TEXT("home returns to the root directory"), Terminal->CurrentDirectory, INDEX_NONE);
	TestEqual(TEXT("and draws the root menu again"), Row(5), TEXT(" Home menu"));
	ExpectCues(TEXT("the home/root return"), Cues, 0, /*accept*/ 1, 0, 0);

	const int32 FirstShot = Terminal->CameraShot;
	Cues = SnapCues();
	const int32 StopsBeforeQuit = Services.Count(
		FString::Printf(TEXT("CancelAudioOwner %s"), *Terminal->CueOwnerId()));
	TestTrue(TEXT("quit closes the session"),
		World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("quit")));
	// `CBaseTerminal::vfunc42` step 6 (`10218251`-`10218278`): `IEngineSoundServer003` slot 5 on
	// the terminal's own entity index with the channel flag clear — engine.dll `0x20002050` writes
	// the stop sub-type of net message 6 with no sample name, which is "stop everything this
	// entity is playing". The port's equivalent is stop-by-owner on the cue owner.
	TestEqual(TEXT("the exit stops every cue the terminal owns, exactly once"),
		Services.Count(FString::Printf(TEXT("CancelAudioOwner %s"), *Terminal->CueOwnerId()))
			- StopsBeforeQuit, 1);
	// The exit itself is not a cue site: nothing new is submitted on the way out.
	ExpectCues(TEXT("the exit"), Cues, 0, 0, 0, 0);
	TestFalse(TEXT("the terminal no longer publishes"), World.BuildTerminalView(View));
	TestTrue(TEXT("quit mobilizes the player again"), SessionPlayer->IsMobile());
	TestEqual(TEXT("quit released the camera handle"), Terminal->CameraShot, 0);
	// Exit re-arms at `ss_start + now` (`0x1021a6f6`), unfloored and never `ss_delay`.
	TestTrue(TEXT("quit re-arms the screensaver at ss_start"),
		FMath::IsNearlyEqual(Terminal->NextThink,
			static_cast<float>(State->GameClock().GetNow()) + Terminal->ScreenSaverStart, 0.01f));
	{
		TArray<FElysiumTerminalView> Idle;
		World.BuildIdleTerminalViews(Idle);
		TestTrue(TEXT("and the idle list carries tuthack again"),
			Idle.ContainsByPredicate([Terminal](const FElysiumTerminalView& Candidate)
				{ return Candidate.Owner == Terminal->Handle; }));
	}
	TestEqual(TEXT("and popped exactly the shot it pushed"),
		Services.Count(FString::Printf(TEXT("PopCameraShot %d"), FirstShot)), 1);

	// --- The safe gives the keycard; the map wires G.Tut_Key and Jack's teleport ---
	FElysiumItemContainer* Safe = SafeEntity->AsItemContainer();
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("tutsafe is an item container"), Safe)
		|| !TestNotNull(TEXT("the player spawned"), Player))
	{
		return false;
	}
	const FString KeyCard = TEXT("item_k_tutorial_chopshop_stairs_key");
	if (bItems)
	{
		TestTrue(TEXT("the safe holds its equip0 keycard"), Safe->Inventory.Has(*Safe, KeyCard));
		bool bTaken = false;
		for (int32 Slot = 0; Slot < Safe->Inventory.Num() && !bTaken; ++Slot)
		{
			bTaken = Safe->TakeToPlayer(*Player, Slot);
		}
		TestTrue(TEXT("the keycard transfers to the player"), bTaken);
		TestTrue(TEXT("the player carries the keycard"), Player->Inventory.Has(*Player, KeyCard));
		Advance(State->GameClock().GetNow());
		TestEqual(TEXT("OnItemRemove wrote G.Tut_Key = 1"), State->GetGlobalInt(TEXT("Tut_Key")), 1);
		TestTrue(TEXT("OnItemRemove enabled trig_jack_teleport_3"), TriggerEnabled(Teleport));

		// The knobs are `prop_doorknob_electronic` with `key_name` = the keycard and `delete_key 1`:
		// the first accepts and consumes it; the far-side knob then has nothing to accept.
		World.BeginPlayerUseSession(KnobA->Handle, PlayerHandle);
		TestFalse(TEXT("tutdoordknob accepts the keycard"), Locked(KnobA));
		TestFalse(TEXT("delete_key consumed the keycard"), Player->Inventory.Has(*Player, KeyCard));
		World.BeginPlayerUseSession(KnobB->Handle, PlayerHandle);
		TestTrue(TEXT("tutdoordknob-wesp stays locked without the consumed keycard"), Locked(KnobB));
	}

	// --- Lock: the reverse rows ---
	TestEqual(TEXT("the terminal reopens"),
		World.BeginPlayerUseSession(Terminal->Handle, PlayerHandle).Outcome,
		EElysiumUseOutcome::SessionStarted);
	const uint32 Second = Terminal->SessionSerial;
	TestTrue(TEXT("a reopened session has a new serial"), Second != Serial);
	World.SubmitTerminalCommand(Terminal->Handle, Second, TEXT("Safe"));
	TestEqual(TEXT("an unlocked directory opens without the password"), Terminal->CurrentDirectory, 0);
	TestEqual(TEXT("and draws its menu straight away, with no acknowledge step"),
		Terminal->InputMode(), EElysiumTerminalInputMode::Line);
	const double LockIssued = State->GameClock().GetNow();
	TestTrue(TEXT("Lock is accepted"), World.SubmitTerminalCommand(Terminal->Handle, Second, TEXT("Lock")));
	World.SubmitTerminalCommand(Terminal->Handle, Second, FString());
	Advance(LockIssued + 0.25);
	TestFalse(TEXT("OnTrigger1 unhid tutsafelock at once"), Padlock->IsHidden());
	TestFalse(TEXT("the padlock is not yet relocked before the delay"), Locked(Padlock));
	Advance(LockIssued + 0.75);
	TestTrue(TEXT("tutsafelock relocked by the +0.5 s Lock row"), Locked(Padlock));
	// The `Lock` run holds the same contract: a live handle and an immobilized player throughout,
	// both released by the exit.
	TestFalse(TEXT("the reopened session immobilizes again"), SessionPlayer->IsMobile());
	const int32 SecondShot = Terminal->CameraShot;
	TestTrue(TEXT("and pushed a fresh camera handle"), SecondShot != 0 && SecondShot != FirstShot);
	World.SubmitTerminalCommand(Terminal->Handle, Second, TEXT("quit"));
	TestTrue(TEXT("the Lock run's quit mobilizes the player"), SessionPlayer->IsMobile());
	TestEqual(TEXT("and releases its own handle"), Terminal->CameraShot, 0);
	TestEqual(TEXT("popping exactly once"),
		Services.Count(FString::Printf(TEXT("PopCameraShot %d"), SecondShot)), 1);

	// --- slice F: the two `typing` sites, on the clock ----------------------------------------
	// `break` at a password prompt runs `BeginInput` `0x10217b30`: the attempt resolves at once,
	// the buffer is filled, and `typing` plays THERE — then the visible cracking runs on
	// `FUN_10217d60`, which plays `typing` a second time at the flush, when the real characters
	// replace the scrambled ones. Relock the directory so there is a password prompt to break.
	Terminal->DirectoryUnlocked[0] = 0;
	TestEqual(TEXT("the terminal opens once more for the cracking cues"),
		World.BeginPlayerUseSession(Terminal->Handle, PlayerHandle).Outcome,
		EElysiumUseOutcome::SessionStarted);
	const uint32 Third = Terminal->SessionSerial;
	World.SubmitTerminalCommand(Terminal->Handle, Third, TEXT("Safe"));
	TestEqual(TEXT("the relocked directory asks for its password again"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Password);
	Cues = SnapCues();
	TestTrue(TEXT("break is accepted at the password prompt"),
		World.SubmitTerminalCommand(Terminal->Handle, Third, TEXT("break")));
	if (TestTrue(TEXT("break started the timed attempt"), !Terminal->HackBuffer().IsEmpty()))
	{
		ExpectCues(TEXT("the cracking start"), Cues, 0, 0, 0, /*typing*/ 1);
		// One `typing` at the flush and no more: the per-frame cracking row is silent.
		Cues = SnapCues();
		const double BreakIssued = State->GameClock().GetNow();
		Advance(BreakIssued + 2.0);
		ExpectCues(TEXT("the cracking rows before the flush"), Cues, 0, 0, 0, /*typing*/ 0);
		Cues = SnapCues();
		Advance(BreakIssued + 6.0);
		TestTrue(TEXT("the buffer flushed"), Terminal->HackBuffer().IsEmpty());
		if (bCues)
		{
			TestEqual(TEXT("the flush plays typing exactly once"),
				CueCount(TEXT("typing")) - Cues.Typing, 1);
		}
	}
	if (Terminal->CurrentUser.IsSet())
	{
		World.SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial, TEXT("quit"));
	}
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
