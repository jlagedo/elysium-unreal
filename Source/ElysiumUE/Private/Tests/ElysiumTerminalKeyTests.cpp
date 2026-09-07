// The terminal keyboard (`docs/project/plans/terminals.md`, slice E).
//
// Retail's client input is two bodies, not one: `C_BaseTerminal::vfunc25` `0x100c7090` classifies
// the key and *eats* every printable without inserting it, and `FUN_100c6d50` does the insert.
// Both are reproduced in `SElysiumTerminalInput`, and both are driven here directly rather than
// through `FSlateApplication::ProcessKeyDownEvent`, which needs a real `SWindow` and a real focus
// path no automation tier has.
//
// The third case is the composition: the typed line is on the GLASS in retail, because the client
// puts it into its own cell buffer before rasterizing (§8.1/§8.3, TERM13). `ComposeDraft` is that,
// pure.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumPresentationSubsystem.h"
#include "ElysiumViewState.h"
#include "UI/ElysiumTerminalCells.h"
#include "UI/ElysiumTerminalInputWidget.h"
#include "UI/ElysiumTerminalScreen.h"
#include "UI/SElysiumTerminalInput.h"

#include "Engine/World.h"
#include "ICommonInputModule.h"
#include "CommonInputSettings.h"
#include "InputCoreTypes.h"
#include "Tests/AutomationCommon.h"

namespace ElysiumTerminalKeyTests
{
static constexpr EAutomationTestFlags GKeyFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The tutorial's live prompt row, as `FUN_1021b410` leaves it: 36x24, the cursor parked on the
// last row at the left margin of a title-box screen.
static FElysiumTerminalView PromptView(uint8 InputMode = 0)
{
	FElysiumTerminalView View;
	View.Owner = FElysiumEntityHandle(7, 1);
	View.SessionSerial = 19;
	View.Revision = 3;
	View.Columns = 36;
	View.Rows = 24;
	View.Cells.Init(static_cast<uint16>(0x80 | ' '), View.Columns * View.Rows);
	View.ScreenRows.Init(FString::ChrN(View.Columns, TCHAR(' ')), View.Rows);
	View.CursorRow = 23;
	View.CursorColumn = 0;
	View.RightMargin = 0;
	View.CellStyle = 0x80;
	View.InputMode = InputMode;
	View.MaxInput = 0;              // `m_nMaxInput` as `CBaseTerminal::Spawn` leaves it: unlimited
	View.bAcceptsDirectoryKeys = false;
	return View;
}

static FKeyEvent Down(const FKey& Key, bool bControl = false, bool bRepeat = false)
{
	const FModifierKeysState Modifiers(false, false, bControl, false, false, false, false, false,
		false);
	return FKeyEvent(Key, Modifiers, 0, bRepeat, 0, 0);
}

static FCharacterEvent Typed(TCHAR Character)
{
	return FCharacterEvent(Character, FModifierKeysState(), 0, false);
}
}

using namespace ElysiumTerminalKeyTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalKeysTest,
	"Elysium.Substrate.Terminal.Keys", GKeyFlags)
bool FElysiumTerminalKeysTest::RunTest(const FString&)
{
	int32 Lines = 0;
	int32 Characters = 0;
	int32 Acknowledges = 0;
	int32 Quits = 0;
	int32 Breaks = 0;
	FString LastLine;
	FString TypedCharacters;

	const TSharedRef<SElysiumTerminalInput> Input = SNew(SElysiumTerminalInput);
	Input->OnSubmitLine.BindLambda([&](const FString& Line)
	{
		++Lines;
		LastLine = Line;
		return true;
	});
	Input->OnSubmitCharacter.BindLambda([&](TCHAR Character)
	{
		++Characters;
		TypedCharacters.AppendChar(Character);
		return true;
	});
	Input->OnAcknowledge.BindLambda([&]() { ++Acknowledges; });
	Input->OnQuit.BindLambda([&]() { ++Quits; });
	Input->OnBreak.BindLambda([&]() { ++Breaks; });
	Input->SetSession(PromptView());

	const FGeometry Geometry;

	// --- the backtick is the console's, and nothing else's -------------------------------------
	TestFalse(TEXT("the backtick key is left unhandled so the console can open"),
		Input->OnKeyDown(Geometry, Down(EKeys::Tilde)).IsEventHandled());
	TestFalse(TEXT("and so is the backtick character"),
		Input->OnKeyChar(Geometry, Typed(TCHAR('`'))).IsEventHandled());
	TestTrue(TEXT("neither reached the line"), Input->Draft().IsEmpty());

	// --- the eat: key-down consumes printables and inserts nothing ------------------------------
	TestTrue(TEXT("a printable key-down is consumed"),
		Input->OnKeyDown(Geometry, Down(EKeys::A)).IsEventHandled());
	TestTrue(TEXT("and inserts nothing — the character path does that"), Input->Draft().IsEmpty());

	// --- the character path inserts ---------------------------------------------------------
	Input->OnKeyChar(Geometry, Typed(TCHAR('l')));
	Input->OnKeyChar(Geometry, Typed(TCHAR('i')));
	Input->OnKeyChar(Geometry, Typed(TCHAR('s')));
	Input->OnKeyChar(Geometry, Typed(TCHAR('t')));
	TestEqual(TEXT("printables accumulate in the local line"), Input->Draft(), FString(TEXT("list")));

	// The five control codes `FUN_100c6d50` names, plus the port's range guard.
	for (const TCHAR Ignored : { TCHAR(8), TCHAR(9), TCHAR(10), TCHAR(13), TCHAR(27), TCHAR(0x1f),
		TCHAR(0x7f), TCHAR(0xe9) })
	{
		Input->OnKeyChar(Geometry, Typed(Ignored));
	}
	TestEqual(TEXT("no control code or out-of-range character is inserted"), Input->Draft(),
		FString(TEXT("list")));

	// --- backspace is local, and repeats --------------------------------------------------------
	TestTrue(TEXT("backspace is consumed"),
		Input->OnKeyDown(Geometry, Down(EKeys::BackSpace)).IsEventHandled());
	Input->OnKeyDown(Geometry, Down(EKeys::BackSpace, false, /*bRepeat*/ true));
	TestEqual(TEXT("backspace deletes at the caret and honours key repeat"), Input->Draft(),
		FString(TEXT("li")));
	Input->OnKeyDown(Geometry, Down(EKeys::BackSpace));
	Input->OnKeyDown(Geometry, Down(EKeys::BackSpace));
	Input->OnKeyDown(Geometry, Down(EKeys::BackSpace));
	TestTrue(TEXT("backspace on an empty line does nothing"), Input->Draft().IsEmpty());

	// --- arrows, Home and End move nothing, and still never reach movement ----------------------
	Input->OnKeyChar(Geometry, Typed(TCHAR('a')));
	Input->OnKeyChar(Geometry, Typed(TCHAR('b')));
	for (const FKey& Key : { EKeys::Left, EKeys::Right, EKeys::Up, EKeys::Down, EKeys::Home,
		EKeys::End, EKeys::Delete })
	{
		TestTrue(TEXT("a direction key is consumed by the terminal"),
			Input->OnKeyDown(Geometry, Down(Key)).IsEventHandled());
	}
	TestEqual(TEXT("m_bAllowDirKeys is false, so none of them moved the caret"), Input->Draft(),
		FString(TEXT("ab")));
	Input->OnKeyChar(Geometry, Typed(TCHAR('c')));
	TestEqual(TEXT("the caret is still the end of the line"), Input->Draft(), FString(TEXT("abc")));

	// --- Enter submits the line, and an accepted line clears it ----------------------------------
	TestTrue(TEXT("Enter is consumed"),
		Input->OnKeyDown(Geometry, Down(EKeys::Enter)).IsEventHandled());
	TestEqual(TEXT("Enter submits the local line"), LastLine, FString(TEXT("abc")));
	TestEqual(TEXT("once"), Lines, 1);
	TestTrue(TEXT("and the accepted line is cleared, as the server's type-3 message does"),
		Input->Draft().IsEmpty());
	Input->OnKeyDown(Geometry, Down(EKeys::Enter, false, /*bRepeat*/ true));
	TestEqual(TEXT("a repeated Enter submits nothing"), Lines, 1);

	// --- Escape quits, Ctrl+C breaks, plain C does not ------------------------------------------
	TestTrue(TEXT("Escape is consumed"),
		Input->OnKeyDown(Geometry, Down(EKeys::Escape)).IsEventHandled());
	TestEqual(TEXT("Escape in line mode sends the quit intent"), Quits, 1);
	Input->OnKeyDown(Geometry, Down(EKeys::Escape, false, /*bRepeat*/ true));
	TestEqual(TEXT("a repeated Escape does not"), Quits, 1);
	Input->OnKeyDown(Geometry, Down(EKeys::C));
	TestEqual(TEXT("a bare C is not a break"), Breaks, 0);
	Input->OnKeyDown(Geometry, Down(EKeys::C, /*bControl*/ true));
	TestEqual(TEXT("Ctrl+C is"), Breaks, 1);
	Input->OnKeyDown(Geometry, Down(EKeys::C, /*bControl*/ true, /*bRepeat*/ true));
	TestEqual(TEXT("and it does not repeat"), Breaks, 1);
	TestEqual(TEXT("no quit or break ever submitted a line"), Lines, 1);

	// --- digits-only (`m_HackFlags 0x2`) ---------------------------------------------------------
	{
		FElysiumTerminalView Digits = PromptView(/*InputMode*/ 1);
		Digits.bDigitsOnly = true;
		Input->SetSession(Digits);
		TestTrue(TEXT("a mode change cleared the draft"), Input->Draft().IsEmpty());
		Input->OnKeyChar(Geometry, Typed(TCHAR('7')));
		Input->OnKeyChar(Geometry, Typed(TCHAR('a')));
		TestEqual(TEXT("digits-only accepts '0'..'9' and refuses everything else"), Input->Draft(),
			FString(TEXT("7")));
		// A password is NOT masked: retail's client inserts the literal character into the cell
		// buffer, and there is no `*` anywhere in `FUN_100c6d50`.
		Input->OnKeyChar(Geometry, Typed(TCHAR('4')));
		TestEqual(TEXT("the password line holds the literal characters"), Input->Draft(),
			FString(TEXT("74")));
	}

	// --- m_nMaxInput -----------------------------------------------------------------------------
	{
		FElysiumTerminalView Capped = PromptView();
		Capped.MaxInput = 4;
		Capped.SessionSerial = 20;
		Input->SetSession(Capped);
		for (int32 Index = 0; Index < 10; ++Index)
		{
			Input->OnKeyChar(Geometry, Typed(TCHAR('x')));
		}
		TestEqual(TEXT("m_nMaxInput 4 stops the line at four"), Input->Draft().Len(), 4);

		FElysiumTerminalView Unlimited = PromptView();
		Unlimited.SessionSerial = 21;
		Input->SetSession(Unlimited);
		for (int32 Index = 0; Index < 40; ++Index)
		{
			Input->OnKeyChar(Geometry, Typed(TCHAR('x')));
		}
		// Zero is unlimited, but the client's own re-render guard is the real cap: both input paths
		// only write the edited line back when `origin + length < columns - rightMargin`, so on this
		// 36-column screen with the cursor at column 0 the line stops at 35.
		TestEqual(TEXT("m_nMaxInput 0 is unlimited up to the row's fit guard"),
			Input->Draft().Len(), 35);
	}

	// --- acknowledge mode ------------------------------------------------------------------------
	{
		Input->SetSession(PromptView(/*InputMode*/ 2));
		const int32 QuitsBefore = Quits;
		const int32 LinesBefore = Lines;
		Input->OnKeyChar(Geometry, Typed(TCHAR('a')));
		TestTrue(TEXT("acknowledge mode inserts nothing"), Input->Draft().IsEmpty());
		Input->OnKeyDown(Geometry, Down(EKeys::Enter));
		TestEqual(TEXT("Enter in acknowledge mode sends the bare hackcmd"), Acknowledges, 1);
		Input->OnKeyDown(Geometry, Down(EKeys::Escape));
		TestEqual(TEXT("and so does Escape — it is NOT quit in this mode"), Acknowledges, 2);
		TestEqual(TEXT("acknowledge mode never quits"), Quits, QuitsBefore);
		TestEqual(TEXT("nor submits a line"), Lines, LinesBefore);
	}

	// --- raw-character mode (`m_HackFlags 0x4`) ---------------------------------------------------
	{
		Input->SetSession(PromptView(/*InputMode*/ 3));
		Input->OnKeyChar(Geometry, Typed(TCHAR('n')));
		Input->OnKeyChar(Geometry, Typed(TCHAR('q')));
		TestEqual(TEXT("each printable is one hackcmd %c"), Characters, 2);
		TestEqual(TEXT("carrying the translated character"), TypedCharacters, FString(TEXT("nq")));
		TestTrue(TEXT("and nothing accumulates locally"), Input->Draft().IsEmpty());
		const int32 QuitsBefore = Quits;
		Input->OnKeyDown(Geometry, Down(EKeys::Escape));
		TestEqual(TEXT("Escape in raw mode still quits"), Quits, QuitsBefore + 1);
	}

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalDraftMirrorTest,
	"Elysium.Substrate.Terminal.DraftMirror", GKeyFlags)
bool FElysiumTerminalDraftMirrorTest::RunTest(const FString&)
{
	using namespace ElysiumTerminalCells;

	FElysiumTerminalView View = PromptView();
	// A prompt row exactly as `FUN_1021b410` leaves it inside a title-box screen: margins (1, 1),
	// the cursor on the last row at column 1.
	View.CursorColumn = 1;
	View.RightMargin = 1;

	{
		const FElysiumTerminalComposed Empty = ComposeDraft(View, FString());
		TestEqual(TEXT("an empty draft composes to the authority grid, cell for cell"),
			Empty.Cells, View.Cells);
		TestEqual(TEXT("and leaves the cursor where the authority put it"), Empty.CursorColumn,
			View.CursorColumn);
		TestFalse(TEXT("nothing was drawn"), Empty.bDraftDrawn);
	}

	{
		const FElysiumTerminalComposed Four = ComposeDraft(View, TEXT("Safe"));
		TestTrue(TEXT("a four-character draft is drawn"), Four.bDraftDrawn);
		TestEqual(TEXT("its first character sits at the authority cursor"),
			Four.CharAt(1, 23), TCHAR('S'));
		TestEqual(TEXT("and its last three follow"), Four.RowText(23).Mid(1, 4),
			FString(TEXT("Safe")));
		TestEqual(TEXT("the composed cursor moved by four"), Four.CursorColumn, 5);
		TestEqual(TEXT("the row it is on is the authority's cursor row"), Four.CursorRow, 23);
		TestEqual(TEXT("no other row was touched"), Four.RowText(22), View.ScreenRows[22]);
		TestEqual(TEXT("and the style bit came from the screen's current style"),
			static_cast<int32>(Four.Cell(1, 23) & 0x80), 0x80);
	}

	{
		// A password draft echoes literally — retail masks nothing (`FUN_100c6d50` inserts the byte
		// it was handed, and no draw body ever substitutes one).
		FElysiumTerminalView Password = View;
		Password.InputMode = 1;
		const FElysiumTerminalComposed Echoed = ComposeDraft(Password, TEXT("chopshop"));
		TestEqual(TEXT("a password draft is echoed literally, not masked"),
			Echoed.RowText(23).Mid(1, 8), FString(TEXT("chopshop")));
	}

	{
		// The fit guard, and its consequence: retail discards the whole keystroke rather than
		// wrapping, because the edited line is only written back inside the guard.
		TestTrue(TEXT("a line that ends one short of the right margin fits"),
			DraftFits(View, 33));
		TestFalse(TEXT("one character more does not"), DraftFits(View, 34));
		const FElysiumTerminalComposed TooLong =
			ComposeDraft(View, FString::ChrN(34, TCHAR('x')));
		TestFalse(TEXT("an over-long draft is not drawn at all"), TooLong.bDraftDrawn);
		TestEqual(TEXT("and the glass keeps the authority grid — there is no wrap"),
			TooLong.Cells, View.Cells);
		TestEqual(TEXT("nor does the cursor move"), TooLong.CursorColumn, View.CursorColumn);
	}

	{
		// The authority's own copy is never touched: `ComposeDraft` takes the view by const ref and
		// the retail equivalent restores its saved row before every re-render.
		const TArray<uint16> Before = View.Cells;
		ComposeDraft(View, TEXT("mutate"));
		TestEqual(TEXT("the authority grid is never mutated"), View.Cells, Before);
	}

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalIntentsTest,
	"Elysium.Substrate.Terminal.Intents", GKeyFlags)
bool FElysiumTerminalIntentsTest::RunTest(const FString&)
{
	ICommonInputModule::GetSettings().LoadData();

	FElysiumTerminalView View = PromptView();
	UElysiumTerminalScreen* Screen = NewObject<UElysiumTerminalScreen>();
	Screen->ApplyTerminal(View);

	struct FIntent
	{
		FString Name;
		FElysiumEntityHandle Owner;
		uint32 Serial = 0;
		FString Payload;
	};
	TArray<FIntent> Seen;
	Screen->OnCommand.BindLambda([&](const FElysiumEntityHandle& Owner, uint32 Serial,
		const FString& Command)
	{
		Seen.Add({ TEXT("command"), Owner, Serial, Command });
		return true;
	});
	Screen->OnCharacter.BindLambda([&](const FElysiumEntityHandle& Owner, uint32 Serial,
		TCHAR Character)
	{
		Seen.Add({ TEXT("character"), Owner, Serial, FString::Chr(Character) });
		return true;
	});
	Screen->OnAcknowledge.BindLambda([&](const FElysiumEntityHandle& Owner, uint32 Serial)
	{
		Seen.Add({ TEXT("acknowledge"), Owner, Serial, FString() });
		return true;
	});
	Screen->OnQuit.BindLambda([&](const FElysiumEntityHandle& Owner, uint32 Serial)
	{
		Seen.Add({ TEXT("quit"), Owner, Serial, FString() });
		return true;
	});
	Screen->OnBreak.BindLambda([&](const FElysiumEntityHandle& Owner, uint32 Serial)
	{
		Seen.Add({ TEXT("break"), Owner, Serial, FString() });
		return true;
	});
	FString Mirrored;
	Screen->OnDraft.BindLambda([&](const FElysiumEntityHandle&, const FString& Draft)
	{
		Mirrored = Draft;
	});

	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	UElysiumTerminalInputWidget* Keyboard =
		Cast<UElysiumTerminalInputWidget>(Screen->GetDesiredFocusTarget());
	if (!TestNotNull(TEXT("the keyboard leaf is the screen's focus target"), Keyboard))
	{
		return false;
	}
	const TSharedPtr<SElysiumTerminalInput> Input = Keyboard->GetInput();
	if (!TestTrue(TEXT("and it built its Slate leaf"), Input.IsValid()))
	{
		return false;
	}
	const FGeometry Geometry;

	// --- the five strings, each carrying the applied owner and serial ---------------------------
	Input->OnKeyChar(Geometry, Typed(TCHAR('l')));
	Input->OnKeyChar(Geometry, Typed(TCHAR('s')));
	TestEqual(TEXT("every keystroke is mirrored to the glass"), Mirrored, FString(TEXT("ls")));
	Input->OnKeyDown(Geometry, Down(EKeys::Enter));
	Input->OnKeyDown(Geometry, Down(EKeys::Escape));
	Input->OnKeyDown(Geometry, Down(EKeys::C, /*bControl*/ true));
	if (!TestEqual(TEXT("three keys, three intents"), Seen.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("Enter is the line command"), Seen[0].Name, FString(TEXT("command")));
	TestEqual(TEXT("carrying the typed line"), Seen[0].Payload, FString(TEXT("ls")));
	TestEqual(TEXT("Escape is the quit intent"), Seen[1].Name, FString(TEXT("quit")));
	TestEqual(TEXT("Ctrl+C is the break intent"), Seen[2].Name, FString(TEXT("break")));
	for (const FIntent& Intent : Seen)
	{
		TestEqual(TEXT("each intent re-resolves the applied owner"), Intent.Owner, View.Owner);
		TestEqual(TEXT("and the applied serial"), Intent.Serial, View.SessionSerial);
	}

	// --- a new serial is picked up; the old one is never sent again ------------------------------
	Seen.Reset();
	View.SessionSerial = 23;
	View.Revision = 4;
	View.InputMode = 2;
	Screen->ApplyTerminal(View);
	TestTrue(TEXT("the mode change cleared the draft"), Screen->GetDraftText().IsEmpty());
	Input->OnKeyDown(Geometry, Down(EKeys::Enter));
	if (!TestEqual(TEXT("acknowledge mode answers Enter with one intent"), Seen.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is the acknowledge intent"), Seen[0].Name, FString(TEXT("acknowledge")));
	TestEqual(TEXT("carrying the NEW serial"), Seen[0].Serial, 23u);

	// --- raw mode forwards one character ---------------------------------------------------------
	Seen.Reset();
	View.Revision = 5;
	View.InputMode = 3;
	Screen->ApplyTerminal(View);
	Input->OnKeyChar(Geometry, Typed(TCHAR('x')));
	if (!TestEqual(TEXT("one key, one character intent"), Seen.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("the raw arm forwards a character"), Seen[0].Name, FString(TEXT("character")));
	TestEqual(TEXT("and it is the one typed"), Seen[0].Payload, FString(TEXT("x")));

	// --- a closed session submits nothing ---------------------------------------------------------
	Seen.Reset();
	FElysiumTerminalView Closed;
	Closed.Owner = View.Owner;
	Closed.SessionSerial = 0;   // `IsOpen()` is the serial, not the owner
	Screen->ApplyTerminal(Closed);
	Input->OnKeyDown(Geometry, Down(EKeys::Enter));
	Input->OnKeyDown(Geometry, Down(EKeys::Escape));
	Input->OnKeyDown(Geometry, Down(EKeys::C, /*bControl*/ true));
	TestEqual(TEXT("a spent session submits nothing at all"), Seen.Num(), 0);

	// --- the subsystem's five wrappers all take the one world call --------------------------------
	// With no map actor standing there is no world to reach, and every one of them has to say so
	// rather than half-executing.
	FTestWorldWrapper TestWorld;
	if (TestWorld.CreateTestWorld(EWorldType::Game) && TestWorld.BeginPlayInTestWorld())
	{
		UElysiumPresentationSubsystem* Presentation =
			UElysiumPresentationSubsystem::Get(TestWorld.GetTestWorld());
		if (TestNotNull(TEXT("the world carries a presentation subsystem"), Presentation))
		{
			const FElysiumEntityHandle Owner(7, 1);
			TestFalse(TEXT("SubmitCommand refuses with no world"),
				Presentation->SubmitCommand(Owner, 19, TEXT("list")));
			TestFalse(TEXT("SubmitCharacter too"),
				Presentation->SubmitCharacter(Owner, 19, TCHAR('n')));
			TestFalse(TEXT("Acknowledge too"), Presentation->Acknowledge(Owner, 19));
			TestFalse(TEXT("Quit too"), Presentation->Quit(Owner, 19));
			TestFalse(TEXT("Break too"), Presentation->Break(Owner, 19));
			TestFalse(TEXT("and no session is reported open"),
				Presentation->IsTerminalSessionOpen());

			// The draft mirror is presentation-local and survives none of that.
			Presentation->SetTerminalDraft(Owner, TEXT("cho"));
			TestEqual(TEXT("the mirrored draft is held for its terminal"),
				Presentation->TerminalDraft(), FString(TEXT("cho")));
			TestEqual(TEXT("under that owner"), Presentation->TerminalDraftOwner(), Owner);
			Presentation->SetTerminalDraft(FElysiumEntityHandle::Invalid(), FString());
			TestTrue(TEXT("and dropped when the keyboard goes away"),
				Presentation->TerminalDraft().IsEmpty());
		}
	}
	else
	{
		TestWorld.ForwardErrorMessages(this);
	}

	(void)Slate;
	return !HasAnyErrors();
}

#endif   // WITH_DEV_AUTOMATION_TESTS
