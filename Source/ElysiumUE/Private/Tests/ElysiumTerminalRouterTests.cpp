// `CPropHacking::AcceptCmd` 0x1021a830, arm by arm, on a hand-built definition
// (docs/vtmb/computer-terminals.md §9; docs/project/plans/terminals.md, slice A).
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
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumScriptHost.h"
#include "ElysiumVariant.h"
#include "ElysiumViewState.h"
#include "Substrate/ElysiumTerminal.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumTerminalRouterTests
{
static constexpr EAutomationTestFlags GFlags =
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

static UElysiumGameStateSubsystem* MakeHeadlessGameState()
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	return NewObject<UElysiumGameStateSubsystem>(GameInstance);
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
	"Elysium.Substrate.TerminalRouter", GFlags)
bool FElysiumTerminalRouterTest::RunTest(const FString&)
{
	UElysiumGameStateSubsystem* State = MakeHeadlessGameState();
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
	Services.bHasPlayer = true;
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

#endif   // WITH_DEV_AUTOMATION_TESTS
