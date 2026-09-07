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
static constexpr EAutomationTestFlags GFlags =
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

static const TCHAR* GMap = TEXT("sp_tutorial_1");

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
	"Elysium.Substrate.TutorialTerminalSlice", GFlags)
bool FElysiumTutorialTerminalSliceTest::RunTest(const FString&)
{
	if (!FElysiumMapSlice::Available(GMap))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: sp_tutorial_1.ents is not under $ELYSIUM_EXPORT_ROOT"));
		return true;
	}
	FElysiumMapSlice Slice;
	FString Error;
	if (!FElysiumMapSlice::Build(GMap, TerminalSliceRoots(), Slice, Error))
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

	UElysiumGameStateSubsystem* State = MakeHeadlessGameState();
	State->SetScriptHost(MakeUnique<FElysiumExprScriptHost>(State));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
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

	// --- Unlock ---
	const FElysiumUseBeginResult Opened = World.BeginPlayerUseSession(Terminal->Handle, PlayerHandle);
	if (!TestEqual(TEXT("+use on tuthack starts the session"), Opened.Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		return false;
	}
	const uint32 Serial = Terminal->SessionSerial;
	FElysiumTerminalView View;
	TestTrue(TEXT("the session publishes a view"), World.BuildTerminalView(View));
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
	TestTrue(TEXT("Safe is accepted"), World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("Safe")));
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
	TestTrue(TEXT("a wrong password is accepted as a line"),
		World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("x")));
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
	TestTrue(TEXT("quit at the password prompt is accepted"),
		World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("quit")));
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
	TestTrue(TEXT("chopshop is accepted"),
		World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("chopshop")));
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

	TestTrue(TEXT("Unlock is accepted"),
		World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("Unlock")));
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

	Advance(0.0);
	TestFalse(TEXT("OnTrigger0 unlocked tutsafelock through the authored row"), Locked(Padlock));
	TestTrue(TEXT("OnTrigger0 enabled trig_popup_safe"), TriggerEnabled(PopupSafe));
	TestFalse(TEXT("OnTrigger0 disabled trig_popup_note"), TriggerEnabled(PopupNote));
	TestFalse(TEXT("the padlock is still drawn before the delayed hide"), Padlock->IsHidden());
	Advance(0.5);
	TestTrue(TEXT("tutsafelock is hidden by the +0.5 s ScriptHide row"), Padlock->IsHidden());

	TestTrue(TEXT("quit closes the session"),
		World.SubmitTerminalCommand(Terminal->Handle, Serial, TEXT("quit")));
	TestFalse(TEXT("the terminal no longer publishes"), World.BuildTerminalView(View));

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
	World.SubmitTerminalCommand(Terminal->Handle, Second, TEXT("quit"));
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
