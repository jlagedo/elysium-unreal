// The terminal screensaver (`docs/vtmb/computer-terminals.md` §13, slice C of
// `docs/project/plans/terminals.md`).
//
// `CPropHackingSS_Think` `0x1021a740` is one repositioned label, not an animation, and its whole
// observable surface is a schedule and two bounded draws. Both are proven here on a seeded
// `EElysiumRngStream::Terminal`, with no world, no body and no renderer:
//
//   * Activate arms the FIRST tick at `RandomFloat(0,1) + curtime` and only THEN floors `ss_delay`
//     at 2.0 (`_DAT_10449400`); the exit re-arm uses `ss_start` and is never floored (C15);
//   * entry cancels the think outright, which is the only thing that keeps the label off a live
//     session — the think body carries no `m_bInUse` guard (C16);
//   * the label sits at row `[1, rows-1]`, column `[1, max(0, columns - len)]`, in one of two
//     styles, on a screen whose margins the think just reset to (0, 0) (C13, C14).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumRng.h"
#include "ElysiumViewState.h"
#include "Engine/GameInstance.h"
#include "Substrate/ElysiumTerminal.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumTerminalScreenSaverTests
{
static constexpr EAutomationTestFlags GScreenSaverFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

static const TCHAR* GScreenSaverLabel = TEXT("screen saver");

static FElysiumTerminalDefinition SaverDefinition()
{
	FElysiumTerminalDefinition Definition;
	Definition.ScreenSaver = GScreenSaverLabel;
	Definition.LogonLines.Add(TEXT("Idle console"));
	return Definition;
}

// Where the label landed this tick: the first row that contains it, and the column it starts at.
struct FLabelHit
{
	int32 Row = INDEX_NONE;
	int32 Column = INDEX_NONE;
	bool bAlternateStyle = false;
};

static FLabelHit FindLabel(const FElysiumTerminalScreenBuffer& Screen, const FString& Label)
{
	FLabelHit Hit;
	for (int32 Row = 0; Row < Screen.Rows(); ++Row)
	{
		const FString Text = Screen.RowText(Row);
		const int32 Column = Text.Find(Label, ESearchCase::CaseSensitive);
		if (Column != INDEX_NONE)
		{
			Hit.Row = Row;
			Hit.Column = Column;
			// The style byte is written before the print, so every cell of the label carries it.
			Hit.bAlternateStyle = !Screen.StyleAt(Column, Row);
			return Hit;
		}
	}
	return Hit;
}
}

using namespace ElysiumTerminalScreenSaverTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalScreenSaverTest,
	"Elysium.Substrate.TerminalScreenSaver", GScreenSaverFlags)
bool FElysiumTerminalScreenSaverTest::RunTest(const FString&)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UElysiumGameStateSubsystem* State = NewObject<UElysiumGameStateSubsystem>(GameInstance);

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__terminal_screensaver__");
	FElysiumEntityDef TerminalDef;
	TerminalDef.Classname = TEXT("prop_hacking");
	TerminalDef.TargetName = TEXT("terminal");
	TerminalDef.Keys.Add(TEXT("start_enabled"), TEXT("1"));
	TerminalDef.Keys.Add(TEXT("skilltype"), TEXT("2"));
	// The authored floor case: 0.5 is below `_DAT_10449400` and Activate raises it to 2.0.
	TerminalDef.Keys.Add(TEXT("ss_delay"), TEXT("0.5"));
	TerminalDef.Keys.Add(TEXT("ss_start"), TEXT("7.25"));
	Defs.Defs.Add(MoveTemp(TerminalDef));

	FElysiumRecordingServices Services;
	Services.StandTerminalScreen();
	FElysiumEntityWorld World(nullptr, State, Services.Bundle());
	AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();

	// One named, seeded stream owns every draw in the body, so the schedule and the placement are
	// both reproducible (`.claude/rules/cpp.md` — randomness is never `FMath::Rand*`).
	ElysiumRng::SeedAll(20260907);

	World.Activate(0.0);
	FElysiumEntity* Entity = World.FindByName(TEXT("terminal"));
	FElysiumTerminal* Base = Entity ? Entity->AsTerminal() : nullptr;
	if (!TestNotNull(TEXT("the terminal resolves"), Base))
	{
		return false;
	}
	FElysiumPropHacking* Terminal = static_cast<FElysiumPropHacking*>(Base);
	Terminal->InstallDefinition(SaverDefinition());
	Terminal->InputEnable();

	// --- the three schedules ------------------------------------------------------------------
	// Activate: `RandomFloat(0, 1) + curtime`, and the floor applied only AFTER that.
	TestTrue(TEXT("Activate arms the first tick inside the first second"),
		Terminal->NextThink >= 0.0f && Terminal->NextThink < 1.0f);
	TestEqual(TEXT("an authored ss_delay of 0.5 is floored at 2.0 by Activate"),
		Terminal->ScreenSaverDelay, FElysiumPropHacking::ScreenSaverDelayFloor);
	TestEqual(TEXT("ss_start is never floored"), Terminal->ScreenSaverStart, 7.25f);

	auto Advance = [&](double To)
	{
		for (int32 Guard = 0; Guard < 8192 && State->GameClock().GetNow() < To; ++Guard)
		{
			State->TimeControl().AdvanceFrame(FMath::Min(0.05, To - State->GameClock().GetNow()));
			World.Tick(State->GameClock().GetNow());
		}
		World.Tick(State->GameClock().GetNow());
	};

	// The reschedule is measured from the moment the tick RAN, not from the end of the advance: the
	// think fires on the first frame at or past its schedule and re-arms on that frame's clock.
	const float FirstArmedAt = Terminal->NextThink;
	const uint32 RevisionBeforeFirst = Terminal->ViewRevision;
	Advance(1.0);
	TestEqual(TEXT("the first tick fired exactly once inside the first second"),
		Terminal->ViewRevision, RevisionBeforeFirst + 1);
	TestTrue(TEXT("and rescheduled itself the floored ss_delay later"),
		FMath::IsNearlyEqual(Terminal->NextThink,
			FirstArmedAt + FElysiumPropHacking::ScreenSaverDelayFloor, 0.06f));

	// --- entry cancels, exit re-arms at ss_start ----------------------------------------------
	const FElysiumUseBeginResult Opened = World.BeginPlayerUseSession(Terminal->Handle, Player);
	if (!TestEqual(TEXT("+use opens the session"), Opened.Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		return false;
	}
	TestEqual(TEXT("entry cancels the screensaver think outright (ThinkSet(NULL))"),
		Terminal->NextThink, ELYSIUM_NEVER_THINK);
	const uint32 RevisionInSession = Terminal->ViewRevision;
	Advance(State->GameClock().GetNow() + 6.0);
	TestEqual(TEXT("and no screensaver tick lands while the session is held"),
		Terminal->ViewRevision, RevisionInSession);

	const double QuitAt = State->GameClock().GetNow();
	TestTrue(TEXT("quit closes the session"),
		World.SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial, TEXT("quit")));
	TestTrue(TEXT("exit re-arms at ss_start + now, unfloored and not ss_delay"),
		FMath::IsNearlyEqual(Terminal->NextThink,
			static_cast<float>(QuitAt) + Terminal->ScreenSaverStart, 0.01f));

	// --- the body, over 200 ticks --------------------------------------------------------------
	const FString Label(GScreenSaverLabel);
	const int32 Columns = Terminal->Screen.Columns();
	const int32 Rows = Terminal->Screen.Rows();
	const int32 MaxColumn = FMath::Max(0, Columns - Label.Len());
	int32 Ticks = 0;
	int32 DefaultStyleTicks = 0;
	int32 AlternateStyleTicks = 0;
	bool bBoundsHeld = true;
	bool bLabelHeld = true;
	bool bMarginsHeld = true;
	bool bRevisionHeld = true;
	FString FirstFailure;
	while (Ticks < 200)
	{
		const uint32 Before = Terminal->ViewRevision;
		Advance(Terminal->NextThink + 0.01f);
		++Ticks;
		if (Terminal->ViewRevision != Before + 1 && bRevisionHeld)
		{
			bRevisionHeld = false;
			FirstFailure = FString::Printf(
				TEXT("tick %d advanced the revision by %u, not 1"), Ticks,
				Terminal->ViewRevision - Before);
		}
		if (Terminal->Screen.LeftMargin() != 0 || Terminal->Screen.RightMargin() != 0)
		{
			bMarginsHeld = false;
		}
		const FLabelHit Hit = FindLabel(Terminal->Screen, Label);
		if (Hit.Row == INDEX_NONE)
		{
			bLabelHeld = false;
			continue;
		}
		if (Hit.Row < 1 || Hit.Row > Rows - 1 || Hit.Column < 1 || Hit.Column > MaxColumn)
		{
			if (bBoundsHeld)
			{
				bBoundsHeld = false;
				FirstFailure = FString::Printf(TEXT("tick %d placed the label at (%d, %d)"),
					Ticks, Hit.Column, Hit.Row);
			}
		}
		Hit.bAlternateStyle ? ++AlternateStyleTicks : ++DefaultStyleTicks;
	}
	TestTrue(FString::Printf(TEXT("each tick advances the revision exactly once (%s)"),
		*FirstFailure), bRevisionHeld);
	TestTrue(TEXT("the type-7 reset leaves both margins at 0"), bMarginsHeld);
	TestTrue(TEXT("every tick prints the authored `screen saver` label"), bLabelHeld);
	TestTrue(FString::Printf(TEXT("the label always lands in [1, %d] x [1, %d] (%s)"),
		MaxColumn, Rows - 1, *FirstFailure), bBoundsHeld);
	TestTrue(TEXT("the default style (5) is drawn"), DefaultStyleTicks > 0);
	TestTrue(TEXT("and the alternate style (6) is too"), AlternateStyleTicks > 0);
	AddInfo(FString::Printf(TEXT("200 screensaver ticks: %d default style, %d alternate"),
		DefaultStyleTicks, AlternateStyleTicks));

	// The clear is the type-4 body's, so nothing of the previous label survives it: the label is the
	// only text on the screen after a tick.
	int32 NonBlankRows = 0;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		NonBlankRows += Terminal->Screen.RowTextTrimmed(Row).IsEmpty() ? 0 : 1;
	}
	TestEqual(TEXT("the type-4 clear leaves the label as the only row with text"), NonBlankRows, 1);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
