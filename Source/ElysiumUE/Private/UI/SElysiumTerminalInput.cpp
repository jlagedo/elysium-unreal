#include "UI/SElysiumTerminalInput.h"

#include "UI/ElysiumTerminalCells.h"

#include "InputCoreTypes.h"

void SElysiumTerminalInput::Construct(const FArguments& InArgs)
{
	OnSubmitLine = InArgs._OnSubmitLine;
	OnSubmitCharacter = InArgs._OnSubmitCharacter;
	OnAcknowledge = InArgs._OnAcknowledge;
	OnQuit = InArgs._OnQuit;
	OnBreak = InArgs._OnBreak;
	OnDraftChanged = InArgs._OnDraftChanged;
	SetCanTick(false);
}

void SElysiumTerminalInput::ClearDraft()
{
	if (DraftText.IsEmpty())
	{
		return;
	}
	DraftText.Reset();
	OnDraftChanged.ExecuteIfBound();
}

void SElysiumTerminalInput::SetSession(const FElysiumTerminalView& InView)
{
	const bool bNewSession = View.Owner != InView.Owner || View.SessionSerial != InView.SessionSerial;
	const bool bModeChanged = View.InputMode != InView.InputMode;
	// `FUN_100c82e0` clears `+0xed8` inside the activation guard, so every reopened edit starts on
	// an empty line — including the one that reopens over an unchanged owner, serial and mode.
	const bool bReopened = View.EditEpoch != InView.EditEpoch;
	View = InView;
	if (bNewSession || bModeChanged || bReopened)
	{
		ClearDraft();
	}
}

int32 SElysiumTerminalInput::OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
	FSlateWindowElementList&, int32 LayerId, const FWidgetStyle&, bool) const
{
	// Nothing. Every terminal pixel is on the monitor's glass.
	return LayerId;
}

bool SElysiumTerminalInput::CanAppend(TCHAR Character) const
{
	if (IsAcknowledgeMode())
	{
		// `FUN_100c6d50`: with `m_HackFlags 0x1` set, the only character still forwarded is the
		// Enter alias, and nothing is ever inserted.
		return false;
	}
	// `if (m_nMaxInput != 0 && strlen(line) >= m_nMaxInput) return;` — zero (or, here, anything at
	// or below it) is unlimited.
	if (View.MaxInput > 0 && DraftText.Len() >= View.MaxInput)
	{
		return false;
	}
	if (View.bDigitsOnly && !(Character >= TCHAR('0') && Character <= TCHAR('9')))
	{
		return false;
	}
	// The re-render's fit guard. Retail only writes the edited line back inside it, so a keystroke
	// that would push the line past the right margin never happened.
	return ElysiumTerminalCells::DraftFits(View, DraftText.Len() + 1);
}

FReply SElysiumTerminalInput::OnKeyDown(const FGeometry&, const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();

	// `if (param_1 == 0x60) return 0;` — the console owns the backtick, first test in the body.
	if (Key == EKeys::Tilde)
	{
		return FReply::Unhandled();
	}

	// `if (this[0xf04] == 0) return 0;` — the second test, and it returns ZERO: a terminal that is
	// not in use hands the key straight back, so it reaches whatever binding would have had it.
	if (!IsInUse())
	{
		return FReply::Unhandled();
	}

	// `if (this[0xe88] == 0) return 1;` — the third test. The line editor is closed (nothing has
	// sent type 3 since the last print, clear or scroll), so the key is eaten and NOTHING else
	// happens: no break, no quit, no submit, no raw send, no draw. It is deliberately ahead of
	// every mode arm.
	if (!IsEditorOpen())
	{
		return FReply::Handled();
	}

	// Raw-character transport (`m_HackFlags 0x4`) is tested BEFORE acknowledge in `0x100c7090`, and
	// both are tested before the Ctrl latch is ever SET. Escape still quits; every other key
	// becomes one `hackcmd %c`, which the port sends from `OnKeyChar` so the character is the
	// translated one.
	if (IsRawMode())
	{
		if (Key == EKeys::Escape)
		{
			OnQuit.ExecuteIfBound();
		}
		return FReply::Handled();
	}

	// Acknowledge mode (`m_HackFlags 0x1`): Enter, the Enter alias and Escape all send the bare
	// `"hackcmd "` at client `0x102b7210` — an EMPTY command, never `quit`. Everything else is
	// eaten and does nothing.
	if (IsAcknowledgeMode())
	{
		if (Key == EKeys::Enter || Key == EKeys::Escape)
		{
			OnAcknowledge.ExecuteIfBound();
		}
		return FReply::Handled();
	}

	// The Ctrl chord, and its position is the point: retail sets the latch (`this[0xf0c] = 1` on
	// `0x85`) only in this arm, AFTER the raw and acknowledge arms have returned. Every other key
	// clears the latch, so the break can only fire when the immediately preceding key-down went
	// through the line editor — which is never true in raw or acknowledge mode.
	if (Key == EKeys::C && KeyEvent.IsControlDown())
	{
		OnBreak.ExecuteIfBound();
		return FReply::Handled();
	}

	if (Key == EKeys::Enter)
	{
		if (OnSubmitLine.IsBound() && OnSubmitLine.Execute(DraftText))
		{
			// The authority accepted the line, which in retail means it redrew and its type-3
			// message reopened the editor over a cleared local line (`FUN_100c82e0`).
			ClearDraft();
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Escape)
	{
		OnQuit.ExecuteIfBound();
		return FReply::Handled();
	}

	if (Key == EKeys::BackSpace)
	{
		// `if ((param_1 == 0x7f) && (0 < caret))` — a local delete, no server traffic. Key repeat is
		// allowed here, as it is for printables.
		if (!DraftText.IsEmpty())
		{
			DraftText = DraftText.LeftChop(1);
			OnDraftChanged.ExecuteIfBound();
		}
		return FReply::Handled();
	}

	// Arrows / Home / End / Delete: retail moves the local caret only while `m_bAllowDirKeys` is
	// set, and that field is zeroed at `CBaseTerminal::Spawn` and written nowhere else. The key is
	// still consumed, so it never leaks into movement while a session is open.
	if (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Up || Key == EKeys::Down
		|| Key == EKeys::Home || Key == EKeys::End || Key == EKeys::Delete)
	{
		return FReply::Handled();
	}

	if (Key.IsGamepadKey())
	{
		// Gamepad is out of scope for the terminal by owner call, so the navigable screen above
		// keeps its face-button and D-pad handling.
		return FReply::Unhandled();
	}
	// The eat: `else if ((0x1f < param_1) && (param_1 < 0x7f)) return 1;`. Consumed here so the key
	// never reaches a movement binding; `OnKeyChar` does the insert. Everything else (function
	// keys, modifiers, media keys) is retail's fall-through — it re-renders and returns 1, having
	// changed nothing — so the whole tail is Handled either way.
	return FReply::Handled();
}

FReply SElysiumTerminalInput::OnKeyChar(const FGeometry&, const FCharacterEvent& CharacterEvent)
{
	const TCHAR Character = CharacterEvent.GetCharacter();

	// `FUN_100c6d50`'s gate, in its own order: backtick, `this[0xf04]` (in use), `this[0xe88]` (the
	// line editor is open), then the five control codes it names. The insert path carries the same
	// two state guards as the key-down path, so a closed session and a closed editor each refuse
	// the character as well as the key.
	if (Character == TCHAR('`'))
	{
		return FReply::Unhandled();
	}
	if (!IsInUse())
	{
		return FReply::Unhandled();
	}
	if (!IsEditorOpen())
	{
		return FReply::Handled();
	}
	if (Character == 8 || Character == 9 || Character == 10 || Character == 13 || Character == 27)
	{
		return FReply::Handled();
	}
	// The port's own range guard (see the header): retail leaves `0x7f` and everything above the
	// ASCII range to fall into the cell buffer as a glyph index.
	if (Character < 0x20 || Character > 0x7e)
	{
		return FReply::Handled();
	}

	if (IsRawMode())
	{
		// `hackcmd %c` per key, no accumulation on the WIRE. The whole command is one character.
		if (OnSubmitCharacter.IsBound())
		{
			OnSubmitCharacter.Execute(Character);
		}
		// ...and then it falls through to the insert, because `FUN_100c6d50` has **no `0x4` test**.
		// The raw arm lives entirely in the key-down body (`0x100c7090`, `if ((flags & 4) != 0)`);
		// the character body only ever tests `(flags & 1) == 0` — acknowledge — so in raw mode the
		// same keystroke ALSO goes into the client's local line `+0xed8` and is re-rendered on top
		// of the `hackcmd %c` it just sent. The typed letter is therefore visible on the glass until
		// the authority's answer arrives: that answer is a print, which closes the client's editor
		// (`+0xe88`) and moves the epoch, and the epoch clears the draft here.
		//
		// This is the retail chain and it is what the mail area looks like: pressing `n` in an open
		// message shows `n` at the prompt for exactly as long as it takes the list redraw to come
		// back.
	}

	if (!CanAppend(Character))
	{
		return FReply::Handled();
	}
	// The caret is always the end of the line, because the direction keys move nothing.
	DraftText.AppendChar(Character);
	OnDraftChanged.ExecuteIfBound();
	return FReply::Handled();
}
