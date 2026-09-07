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
	View = InView;
	if (bNewSession || bModeChanged)
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

	// The Ctrl chord, before every mode arm: retail latches `0x85` and fires on the next `c`.
	if (Key == EKeys::C && KeyEvent.IsControlDown())
	{
		if (!KeyEvent.IsRepeat())
		{
			OnBreak.ExecuteIfBound();
		}
		return FReply::Handled();
	}

	// Raw-character transport (`m_HackFlags 0x4`) is tested BEFORE acknowledge in `0x100c7090`.
	// Escape still quits; every other key becomes one `hackcmd %c`, which the port sends from
	// `OnKeyChar` so the character is the translated one.
	if (IsRawMode())
	{
		if (Key == EKeys::Escape)
		{
			if (!KeyEvent.IsRepeat())
			{
				OnQuit.ExecuteIfBound();
			}
			return FReply::Handled();
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
			if (!KeyEvent.IsRepeat())
			{
				OnAcknowledge.ExecuteIfBound();
			}
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Enter)
	{
		if (!KeyEvent.IsRepeat() && OnSubmitLine.IsBound() && OnSubmitLine.Execute(DraftText))
		{
			// The authority accepted the line, which in retail means it redrew and its type-3
			// message reopened the editor over a cleared local line (`FUN_100c82e0`).
			ClearDraft();
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Escape)
	{
		if (!KeyEvent.IsRepeat())
		{
			OnQuit.ExecuteIfBound();
		}
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

	// `FUN_100c6d50`'s gate, in order: backtick, then the five control codes it names.
	if (Character == TCHAR('`'))
	{
		return FReply::Unhandled();
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
		// `hackcmd %c` per key, no accumulation. The whole line is one character.
		if (OnSubmitCharacter.IsBound())
		{
			OnSubmitCharacter.Execute(Character);
		}
		return FReply::Handled();
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
