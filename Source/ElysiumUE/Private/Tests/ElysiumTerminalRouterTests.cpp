// `CPropHacking::AcceptCmd` 0x1021a830, arm by arm, on a hand-built definition
// (docs/vtmb/computer-terminals.md §9).
//
// The router is a state machine before it is a parser: mail, then "no password pending", then
// "password pending", and only inside the middle arm is anything compared as a string — builtins,
// then subdirectory names, then the current directory's function names, then invalid. These tests
// drive each arm through the same command bus the game uses and assert the mode it leaves behind.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameClock.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumScriptHost.h"
#include "ElysiumVariant.h"
#include "ElysiumViewState.h"
#include "Substrate/ElysiumTerminal.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumTerminalRouterTests
{
static constexpr EAutomationTestFlags GRouterFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// A script host whose answer is determined by the source string, so a dependency can be driven
// onto either side of the integer-only gate without a Python VM or the export corpus.
class FElysiumRouterScriptHost final : public IElysiumScriptHost
{
public:
	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext&,
		FString* OutError = nullptr) override
	{
		if (OutError)
		{
			OutError->Reset();
		}
		return Source == TEXT("PASS") ? FElysiumVariant::Int(1) : FElysiumVariant::Int(0);
	}
	virtual const TCHAR* Name() const override { return TEXT("router-sentinel"); }
};

static UElysiumSessionSubsystem* MakeHeadlessGameState()
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	return NewObject<UElysiumSessionSubsystem>(GameInstance);
}

// The fixture: one `vault` directory behind a password, holding an `open` function and a `logs`
// function; plus a `logs` *directory* whose dependency fails. Retail's `FUN_1021b750` continues
// scanning past a name that matches but is blocked, so from inside `vault` the word `logs` runs
// the function.
static FElysiumTerminalDefinition RouterDefinition()
{
	FElysiumTerminalDefinition Definition;
	Definition.ScreenSaver = TEXT("Router console");
	Definition.Brackets = TEXT("[]");
	Definition.LogonLines.Add(TEXT("Router console"));

	FElysiumTerminalDirectory Vault;
	Vault.Name = TEXT("vault");
	Vault.Password = TEXT("needle");
	Vault.Description = TEXT("Vault controls");
	FElysiumTerminalFunction Open;
	Open.Name = TEXT("open");
	Open.RunText = TEXT("Vault opened.");
	Open.Trigger = 0;
	Vault.Functions.Add(MoveTemp(Open));
	FElysiumTerminalFunction Logs;
	Logs.Name = TEXT("logs");
	Logs.RunText = TEXT("Log printed.");
	Logs.Trigger = INDEX_NONE;
	Vault.Functions.Add(MoveTemp(Logs));
	Definition.Directories.Add(MoveTemp(Vault));

	FElysiumTerminalDirectory Blocked;
	Blocked.Name = TEXT("logs");
	Blocked.Description = TEXT("Audit log");
	Blocked.Dependency = TEXT("FAIL");
	Definition.Directories.Add(MoveTemp(Blocked));
	return Definition;
}
}

using namespace ElysiumTerminalRouterTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalRouterTest,
	"Elysium.Substrate.TerminalRouter", GRouterFlags)
bool FElysiumTerminalRouterTest::RunTest(const FString&)
{
	UElysiumSessionSubsystem* State = MakeHeadlessGameState();
	State->SetScriptHost(MakeUnique<FElysiumRouterScriptHost>());

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__terminal_router__");
	FElysiumEntityDef TerminalDef;
	TerminalDef.Classname = TEXT("prop_hacking");
	TerminalDef.TargetName = TEXT("terminal");
	TerminalDef.Keys.Add(TEXT("start_enabled"), TEXT("1"));
	TerminalDef.Keys.Add(TEXT("difficulty"), TEXT("1"));
	TerminalDef.Keys.Add(TEXT("skilltype"), TEXT("2"));
	Defs.Defs.Add(MoveTemp(TerminalDef));

	FElysiumRecordingServices Services;
	// The screen cone is a real gate now: a terminal with no `screen` / `screen_axis`, or a player
	// standing off its plan-view axis, refuses every session.
	Services.StandTerminalScreen();
	FElysiumEntityWorld World(nullptr, State, Services.Bundle());
	AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumEntity* Entity = World.FindByName(TEXT("terminal"));
	FElysiumTerminal* Base = Entity ? Entity->AsTerminal() : nullptr;
	if (!TestNotNull(TEXT("the terminal resolves"), Base))
	{
		return false;
	}
	FElysiumPropHacking* Terminal = static_cast<FElysiumPropHacking*>(Base);
	Terminal->InstallDefinition(RouterDefinition());
	Terminal->InputEnable();

	if (!TestEqual(TEXT("+use opens the session"),
		World.BeginPlayerUseSession(Terminal->Handle, Player).Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		return false;
	}
	uint32 Serial = Terminal->SessionSerial;
	auto Submit = [&](const TCHAR* Command)
	{
		return World.SubmitTerminalCommand(Terminal->Handle, Serial, Command);
	};
	FElysiumTerminalView View;
	// The terminal's think reads the game state's clock, so the test drives it in bounded frames
	// (`ElysiumFrame::ClampFrameDelta`) exactly as the map actor's gameplay tick does.
	auto Advance = [&](double To)
	{
		for (int32 Guard = 0; Guard < 4096 && State->GameClock().GetNow() < To; ++Guard)
		{
			State->TimeControl().AdvanceFrame(FMath::Min(0.1, To - State->GameClock().GetNow()));
			World.Tick(State->GameClock().GetNow());
		}
	};

	// --- Raw-character mode exists as a mode (`m_HackFlags 0x4`, `FUN_10219240`) even though no
	// computer content raises it; the keypad grammar does.
	TestEqual(TEXT("a fresh session is in line mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);
	Terminal->EnterRawCharacter();
	TestEqual(TEXT("the raw-character flag reads as raw mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Raw);
	Terminal->EnterLineEdit();
	TestEqual(TEXT("leaving it returns to line mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);

	// --- Password mode: `quit` and the empty line cancel to the directory, they are not guesses.
	// `FUN_10217f50` step 2 (slot 277): pending cleared, the current directory redrawn, session live.
	TestTrue(TEXT("a locked directory is reached by name"), Submit(TEXT("vault")));
	TestEqual(TEXT("and asks for its password"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Password);
	TestTrue(TEXT("quit is accepted at the password prompt"), Submit(TEXT("quit")));
	TestEqual(TEXT("quit at the password prompt cancels to line mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);
	TestEqual(TEXT("quit at the password prompt leaves the current directory alone"),
		Terminal->CurrentDirectory, INDEX_NONE);
	TestTrue(TEXT("quit at the password prompt does not end the session"),
		World.BuildTerminalView(View));

	Submit(TEXT("vault"));
	TestEqual(TEXT("the directory asks again"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Password);
	TestTrue(TEXT("the empty line is accepted at the password prompt"), Submit(TEXT("")));
	TestEqual(TEXT("an empty password cancels the same way"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);
	TestTrue(TEXT("an empty password does not end the session"), World.BuildTerminalView(View));
	TestEqual(TEXT("neither cancel counted as a failed attempt"), Terminal->DirectoryAttempts[0], 0);

	// --- A wrong password: the hint is hidden, then the retry prompt raises it again.
	Submit(TEXT("vault"));
	TestTrue(TEXT("a wrong password is accepted"), Submit(TEXT("wrong")));
	TestEqual(TEXT("it counts against that directory"), Terminal->DirectoryAttempts[0], 1);
	TestEqual(TEXT("and stays in password mode on the retry prompt"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Password);
	TestTrue(TEXT("the retry prompt publishes"), World.BuildTerminalView(View));
	TestEqual(TEXT("the retry prompt raises InfoCtrl type 3 again"), View.HudHintType, 3);

	// --- The cracking buffer owns the session: every `hackcmd` line is dropped while it runs.
	TestTrue(TEXT("break starts the skill bypass"), Submit(TEXT("break")));
	if (!TestTrue(TEXT("the bypass filled the cracking buffer"), !Terminal->HackBuffer().IsEmpty()))
	{
		return false;
	}
	TestTrue(TEXT("the buffer publishes InfoCtrl type 5 with the rating"),
		World.BuildTerminalView(View) && View.HudHintType == 5);
	TestFalse(TEXT("a line typed while the buffer runs is dropped"), Submit(TEXT("list")));
	TestFalse(TEXT("so is the empty line"), Submit(TEXT("")));
	TestFalse(TEXT("and so is a second break"), Submit(TEXT("break")));
	// Rating 0 against difficulty 1 rolls 1 deterministically: the skill arm fails, shows the
	// difficulty and returns to root without leaving the terminal (0x1021c560). The reveal runs for
	// `(5 - rating * 0.25) / speedScale` seconds, so the clock has to reach five.
	Advance(5.5);
	TestTrue(TEXT("the buffer is spent"), Terminal->HackBuffer().IsEmpty());
	TestEqual(TEXT("a failed bypass returns to root"), Terminal->CurrentDirectory, INDEX_NONE);
	TestTrue(TEXT("the terminal is still open"), World.BuildTerminalView(View));
	TestEqual(TEXT("the skill arm publishes InfoCtrl type 6"), View.HudHintType, 6);
	TestEqual(TEXT("with the directory's own difficulty"), View.HudHintValue, 1);
	TestEqual(TEXT("the failed bypass counted an attempt"), Terminal->DirectoryAttempts[0], 2);

	// --- `break` is only the password arm's word. At the directory prompt it is not a builtin, not
	// a subdirectory and not a function, so it falls through to the invalid-command body — which is
	// exactly what `AcceptCmd` does, because the `hackcmd break` test lives inside the
	// password-pending arm and nowhere else.
	TestTrue(TEXT("break at the directory prompt is accepted by the router"), Submit(TEXT("break")));
	TestEqual(TEXT("and is an invalid command there, not a bypass"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Acknowledge);
	TestTrue(TEXT("the invalid-command prompt is consumed"), Submit(TEXT("")));
	TestEqual(TEXT("leaving the player typing again"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);

	// --- The middle arm's string order: builtins first, then names, then invalid.
	TestTrue(TEXT("help is a builtin"), Submit(TEXT("help")));
	TestEqual(TEXT("help leaves the player typing"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);
	TestTrue(TEXT("list is a builtin"), Submit(TEXT("list")));
	TestTrue(TEXT("an unmatched word is an invalid command"), Submit(TEXT("banana")));
	TestEqual(TEXT("invalid command enters acknowledge mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Acknowledge);

	// --- Acknowledge mode is a client restriction, not a router one: `AcceptCmd` never tests
	// `m_HackFlags 0x1`, so a line that reaches the authority in acknowledge mode routes normally.
	// The empty command is what the widget is allowed to send, and it redraws and clears the mode.
	TestTrue(TEXT("a non-empty line still routes at the authority in acknowledge mode"),
		Submit(TEXT("nonsense")));
	TestEqual(TEXT("it routes as any other line would — here, invalid again"),
		Terminal->InputMode(), EElysiumTerminalInputMode::Acknowledge);
	TestTrue(TEXT("the empty command is consumed in acknowledge mode"), Submit(TEXT("")));
	TestEqual(TEXT("and the redraw leaves acknowledge mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);

	// --- Password success, then the function arms.
	Submit(TEXT("vault"));
	TestTrue(TEXT("the password is compared case-insensitively"), Submit(TEXT("NEEDLE")));
	TestEqual(TEXT("the accepted password enters the directory"), Terminal->CurrentDirectory, 0);
	TestEqual(TEXT("a first unlock ends in acknowledge mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Acknowledge);
	Submit(TEXT(""));
	TestTrue(TEXT("a blocked directory name falls through to the function of the same name"),
		Submit(TEXT("logs")));
	TestEqual(TEXT("the function ran, so the terminal waits on the acknowledge prompt"),
		Terminal->InputMode(), EElysiumTerminalInputMode::Acknowledge);
	TestEqual(TEXT("and the blocked directory was never entered"), Terminal->CurrentDirectory, 0);
	Submit(TEXT(""));

	// --- `home` returns to root; directory `quit` releases the player.
	TestTrue(TEXT("home is a builtin"), Submit(TEXT("home")));
	TestEqual(TEXT("home returns to root"), Terminal->CurrentDirectory, INDEX_NONE);
	TestTrue(TEXT("quit at the directory prompt is accepted"), Submit(TEXT("quit")));
	TestFalse(TEXT("quit at the directory prompt releases the player"),
		World.BuildTerminalView(View));
	TestFalse(TEXT("and the spent serial submits nothing"), Submit(TEXT("list")));
	return true;
}

// The `InfoCtrl` HUD hint (`docs/vtmb/computer-terminals.md` §8.4), end to end: the six producers
// `thunk_FUN_10218820` has, the type byte each sends, and the `Hacking_Strings` line the client's
// handler `FUN_10055d30` resolves it into. The authority resolves that line here, so what the HUD
// draws is already finished text and the widget layer has no table lookup of its own.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalHudHintTest,
	"Elysium.Substrate.TerminalHudHint", GRouterFlags)
bool FElysiumTerminalHudHintTest::RunTest(const FString&)
{
	using namespace ElysiumHackingStrings;

	UElysiumSessionSubsystem* State = MakeHeadlessGameState();
	State->SetScriptHost(MakeUnique<FElysiumRouterScriptHost>());

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__terminal_hudhint__");
	FElysiumEntityDef TerminalDef;
	TerminalDef.Classname = TEXT("prop_hacking");
	TerminalDef.TargetName = TEXT("terminal");
	TerminalDef.Keys.Add(TEXT("start_enabled"), TEXT("1"));
	TerminalDef.Keys.Add(TEXT("difficulty"), TEXT("3"));
	TerminalDef.Keys.Add(TEXT("skilltype"), TEXT("2"));
	// `colorscheme` is parsed, clamped `[0, 3]` at Spawn and published on the view; nine is the
	// out-of-range case the client's rasterizer would have clamped at `+0xf08`.
	TerminalDef.Keys.Add(TEXT("colorscheme"), TEXT("9"));
	Defs.Defs.Add(MoveTemp(TerminalDef));

	FElysiumRecordingServices Services;
	Services.StandTerminalScreen();
	FElysiumEntityWorld World(nullptr, State, Services.Bundle());
	AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumEntity* Entity = World.FindByName(TEXT("terminal"));
	FElysiumTerminal* Base = Entity ? Entity->AsTerminal() : nullptr;
	if (!TestNotNull(TEXT("the terminal resolves"), Base))
	{
		return false;
	}
	TestEqual(TEXT("an out-of-range colorscheme is clamped at spawn"), Base->ColorScheme, 3);

	FElysiumPropHacking* Terminal = static_cast<FElysiumPropHacking*>(Base);
	Terminal->InstallDefinition(RouterDefinition());
	Terminal->InputEnable();
	if (!TestEqual(TEXT("+use opens the session"),
		World.BeginPlayerUseSession(Terminal->Handle, Player).Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		return false;
	}
	const uint32 Serial = Terminal->SessionSerial;
	auto Submit = [&](const TCHAR* Command)
	{
		return World.SubmitTerminalCommand(Terminal->Handle, Serial, Command);
	};
	FElysiumTerminalView View;
	auto Advance = [&](double To)
	{
		for (int32 Guard = 0; Guard < 4096 && State->GameClock().GetNow() < To; ++Guard)
		{
			State->TimeControl().AdvanceFrame(FMath::Min(0.1, To - State->GameClock().GetNow()));
			World.Tick(State->GameClock().GetNow());
		}
	};

	// --- the glass half is published whether or not a session is open ----------------------------
	TestTrue(TEXT("the live view publishes"), World.BuildTerminalView(View));
	TestEqual(TEXT("the clamped colour scheme reaches the view"), View.ColorScheme, 3);
	TestFalse(TEXT("m_bAllowDirKeys is published false, as Spawn leaves it"),
		View.bAcceptsDirectoryKeys);
	TestEqual(TEXT("m_nMaxInput is published as Spawn leaves it: unlimited"), View.MaxInput, 0);

	// --- entry raises nothing: `FUN_1021a1c0` is not one of the six hint producers ---------------
	TestEqual(TEXT("entry raises no hint"), View.HudHintType, 0);
	TestTrue(TEXT("so the hint line is empty"), View.HudHintText.IsEmpty());

	// --- the password prompt raises type 3 on EVERY render, first and retry -----------------------
	TestTrue(TEXT("a locked directory is reached by name"), Submit(TEXT("vault")));
	TestTrue(TEXT("the first prompt publishes"), World.BuildTerminalView(View));
	TestEqual(TEXT("the FIRST password prompt raises InfoCtrl type 3"), View.HudHintType, 3);
	TestEqual(TEXT("resolved through Hacking_Strings index 0"), View.HudHintText,
		Get(&World, PressHackKey));
	TestFalse(TEXT("and that line is not empty"), View.HudHintText.IsEmpty());

	// The client line editor's activation travels with that type-3 message (§8.1.1, TERM20):
	// `FUN_10219120` sends it, `FUN_100c82e0` records the cursor column as the edit origin `+0xe8c`,
	// clears the local line `+0xed8` and sets `+0xe88`.
	TestTrue(TEXT("the password prompt leaves the client line editor open"), View.bLineEditActive);
	TestEqual(TEXT("anchored on the prompt row"), View.EditOriginRow, View.Rows - 1);
	TestEqual(TEXT("at the column the prompt left the cursor on"), View.EditOriginColumn,
		View.CursorColumn);
	const uint32 FirstPromptEpoch = View.EditEpoch;
	TestTrue(TEXT("and the activation is counted"), FirstPromptEpoch > 0);

	TestTrue(TEXT("a wrong password is accepted"), Submit(TEXT("wrong")));
	TestTrue(TEXT("the retry prompt publishes"), World.BuildTerminalView(View));
	TestEqual(TEXT("the RETRY prompt raises it again"), View.HudHintType, 3);
	TestEqual(TEXT("with the same line"), View.HudHintText, Get(&World, PressHackKey));
	// The retry is the same session in the same mode, so serial and mode say nothing changed. The
	// epoch is what tells the widget its line was cleared — `FUN_1021b5e0` redraws the whole screen
	// and its type-3 tail zeroes `+0xed8`, so the guess the player just made is gone from the glass.
	TestTrue(TEXT("the retry prompt re-opened the edit, clearing the pending line"),
		View.EditEpoch > FirstPromptEpoch);
	TestTrue(TEXT("and it is open again"), View.bLineEditActive);

	// --- and every message that zeroes `+0xe88` closes it -----------------------------------------
	{
		const uint32 OpenEpoch = View.EditEpoch;
		// `FUN_100c7fb0` (type 2) zeroes the flag as its first act, exactly like `FUN_100c7ec0`
		// (type 1), `FUN_100c7f50` (type 4) and the internal scroll `FUN_100c8210`.
		Terminal->ScreenPrint(TEXT("x"));
		TestFalse(TEXT("a print closes the client's line editor"), Terminal->IsLineEditActive());
		TestTrue(TEXT("the closed view publishes"), World.BuildTerminalView(View));
		TestFalse(TEXT("and the view says so, so nothing composes onto the glass"),
			View.bLineEditActive);
		TestEqual(TEXT("without pretending the edit was reopened"), View.EditEpoch, OpenEpoch);

		Terminal->ScreenClear();
		Terminal->EnterLineEdit();
		TestTrue(TEXT("the next type-3 message opens it again"), Terminal->IsLineEditActive());
		TestTrue(TEXT("the reopened view publishes"), World.BuildTerminalView(View));
		TestTrue(TEXT("with a bumped epoch"), View.EditEpoch > OpenEpoch);

		// The activation guard itself: `iVar1 < (columns - rightMargin) - 1`. A type-3 message that
		// arrives with the cursor on the last usable column activates nothing at all.
		const int32 Columns = Terminal->Screen.Columns();
		const int32 Right = Terminal->Screen.RightMargin();
		const int32 Left = Terminal->Screen.LeftMargin();
		const uint32 GuardEpoch = View.EditEpoch;
		Terminal->ScreenClear();   // closes the edit, so the next EnterLineEdit is a real attempt
		Terminal->ScreenSetCursor(Columns - Right - 1 - Left, 5);
		TestEqual(TEXT("the cursor is on the last column the guard refuses"),
			Terminal->Screen.CursorColumn(), Columns - Right - 1);
		Terminal->EnterLineEdit();
		TestFalse(TEXT("type 3 at that column activates nothing"), Terminal->IsLineEditActive());
		TestTrue(TEXT("the refused view publishes"), World.BuildTerminalView(View));
		TestEqual(TEXT("and no epoch was spent on it"), View.EditEpoch, GuardEpoch);

		Terminal->ScreenSetCursor(Columns - Right - 2 - Left, 5);
		Terminal->EnterLineEdit();
		TestTrue(TEXT("one column earlier it does activate"), Terminal->IsLineEditActive());
		TestEqual(TEXT("with the edit origin at that cursor"), Terminal->LineEditOriginColumn,
			Columns - Right - 2);
		TestEqual(TEXT("on that row"), Terminal->LineEditOriginRow, 5);
	}

	// --- the cracking buffer raises type 5 with the rating ----------------------------------------
	TestTrue(TEXT("break starts the skill bypass"), Submit(TEXT("break")));
	TestTrue(TEXT("the cracking view publishes"), World.BuildTerminalView(View));
	TestEqual(TEXT("the bypass raises InfoCtrl type 5"), View.HudHintType, 5);
	TestEqual(TEXT("index 40 plus the rating"), View.HudHintText,
		Get(&World, MakingHackAttempt) + FString::FromInt(View.HudHintValue));

	// --- the skill-blocked arm raises type 6 with the difficulty ----------------------------------
	Advance(5.5);
	TestTrue(TEXT("the failed bypass leaves the session open"), World.BuildTerminalView(View));
	TestEqual(TEXT("the skill arm raises InfoCtrl type 6"), View.HudHintType, 6);
	TestEqual(TEXT("carrying the entity's difficulty"), View.HudHintValue, 3);
	TestEqual(TEXT("index 38 plus that difficulty"), View.HudHintText,
		Get(&World, SkillInsufficient) + FString::FromInt(3));

	// --- and the NEXT accepted line hides it (`AcceptCmd` sends type 2 before either router) ------
	TestTrue(TEXT("a line is accepted"), Submit(TEXT("list")));
	TestTrue(TEXT("the redraw publishes"), World.BuildTerminalView(View));
	TestEqual(TEXT("the next accepted line hides the hint"), View.HudHintType, 0);
	TestTrue(TEXT("so its line is empty again"), View.HudHintText.IsEmpty());

	// --- type 4 has no terminal producer, but the resolver still answers for it -------------------
	Terminal->SetHudHint(4, 7);
	TestTrue(TEXT("the type-4 view publishes"), World.BuildTerminalView(View));
	TestEqual(TEXT("index 37 plus the value"), View.HudHintText,
		Get(&World, DifficultyLabel) + FString::FromInt(7));
	// Types 0 and 2 both hide, and hiding zeroes the value too.
	Terminal->SetHudHint(2, 99);
	TestTrue(TEXT("the hidden view publishes"), World.BuildTerminalView(View));
	TestEqual(TEXT("type 2 hides"), View.HudHintType, 0);
	TestEqual(TEXT("and drops the value with it"), View.HudHintValue, 0);
	TestTrue(TEXT("leaving no line"), View.HudHintText.IsEmpty());

	// --- exit hides it, before the user handle is released -----------------------------------------
	Terminal->SetHudHint(3, 0);
	World.EndPlayerUseSession(Terminal->Handle, EElysiumUseEndReason::Released);
	TestFalse(TEXT("the session is closed"), World.BuildTerminalView(View));
	TestEqual(TEXT("and the exit hid the hint on the way out"), Terminal->HudHintType, 0);
	TestTrue(TEXT("so nothing resolves"), Terminal->HudHintLine().IsEmpty());
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
