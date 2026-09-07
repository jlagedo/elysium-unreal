#pragma once

#include "CoreMinimal.h"
#include "ElysiumViewState.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

// The terminal keyboard: retail's `C_BaseTerminal` client input, as one focusable Slate leaf.
//
// It paints nothing. The picture is `UElysiumTerminalProjection`'s, on the monitor's own glass;
// this widget exists so the viewport has a focus target that consumes keys the way the retail
// client does and owns the local line the projection composes onto that glass.
//
// The two retail bodies, both reproduced below:
//
//  * **Key-down** `C_BaseTerminal::vfunc25` `0x100c7090` — the specials, and the *eat*: an ordinary
//    printable in `0x20..0x7e` returns 1 **without inserting**, so it never reaches the player's
//    movement bindings, and the character path does the insert. Order matters and is preserved,
//    test for test:
//      1. backtick -> **return 0** (the console keeps it);
//      2. `this[0xf04] == 0` (not in use) -> **return 0** — a closed session hands the key back;
//      3. `this[0xe88] == 0` (the line editor is not open) -> **return 1**, eaten, nothing drawn;
//      4. the Ctrl latch fires only for `c`, and the latch is CLEARED here for every other key;
//      5. raw mode (`m_HackFlags 0x4`), 6. acknowledge mode (`0x1`),
//      7. the line editor — and only here is the latch SET on `0x85`, which is why Ctrl+C is not a
//         break in raw or acknowledge mode: those arms returned before the set.
//  * **Character insert** `FUN_100c6d50` — ignores backtick and the control codes `8`/`9`/`10`/
//    `13`/`27`, honours `m_nMaxInput` (**zero means unlimited**) and the digits-only flag
//    `m_HackFlags 0x2`, and inserts at the caret. It has **no `0x4` test**: the raw-character arm
//    exists only in the key-down body, so in raw mode the byte is sent as `hackcmd %c` AND inserted
//    into the local line, and the local re-render puts it on the glass under the sent command. The
//    authority's answer is a print, which closes the editor and moves the epoch, and that is what
//    clears it again.
//
// Retail's key repeat is an ordinary key-down: `0x100c7090` is handed a code and has no repeat
// input at all, so a held Enter submits again, a held Escape quits again and a held Ctrl+C breaks
// again. Nothing here suppresses repeats.
//
// Named modernizations (`docs/vtmb/computer-terminals.md` §8.7):
//
//  * Retail latches Ctrl as a chord byte (`0x85`, then `c`). The port reads Slate's modifier state
//    on the `C` key-down instead, which is the same gesture through a real keyboard model. The
//    latch's POSITION is reproduced rather than its mechanism: the test sits after the raw and
//    acknowledge arms, so break exists only in the line editor.
//  * Retail forwards the raw key code in `hackcmd %c` for EVERY key-down in raw mode — Enter
//    (`0xd`), backspace (`0x7f`), Tab, the arrow codes `0x80..0x83`, the lot. The port forwards
//    the TRANSLATED character from `OnKeyChar`, so a non-US layout types what the player sees, and
//    the consequence is a named seam: a non-printable key in raw mode is eaten here and sent
//    nowhere. Nothing in the ported content needs it — the open mail message (`FUN_1021c260` ->
//    `FUN_10219240`) is the only shipped raw-mode prompt and its grammar is `n`/`p`/`d`/`m`/`q` —
//    and the one content that would (`CPropKeypad`, whose keys are the arrows and Enter) is not
//    ported. Recorded in §8.7.
//  * Retail's character path range-checks nothing but the five control codes, so a stray `0x7f`
//    reaches the cell buffer and reads there as a backspace. The port requires `0x20..0x7e`, the
//    same range key-down eats.
class SElysiumTerminalInput final : public SLeafWidget
{
public:
	DECLARE_DELEGATE_RetVal_OneParam(bool, FElysiumTerminalLineDelegate, const FString&);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FElysiumTerminalCharDelegate, TCHAR);

	SLATE_BEGIN_ARGS(SElysiumTerminalInput) {}
		SLATE_EVENT(FElysiumTerminalLineDelegate, OnSubmitLine)
		SLATE_EVENT(FElysiumTerminalCharDelegate, OnSubmitCharacter)
		SLATE_EVENT(FSimpleDelegate, OnAcknowledge)
		SLATE_EVENT(FSimpleDelegate, OnQuit)
		SLATE_EVENT(FSimpleDelegate, OnBreak)
		SLATE_EVENT(FSimpleDelegate, OnDraftChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// The published session. The draft is cleared on a new session (owner or serial), on a change of
	// input mode, and on a change of `EditEpoch` — the last being retail's own clear, since
	// `FUN_100c82e0` zeroes the local line `+0xed8` on every activation. The epoch is what makes the
	// third case reachable: a redraw that reopens the same mode's prompt for the same session (the
	// password retry after a failed crack, `FUN_1021b5e0`) changes neither owner, serial nor mode.
	void SetSession(const FElysiumTerminalView& InView);
	const FElysiumTerminalView& Session() const { return View; }

	const FString& Draft() const { return DraftText; }
	void ClearDraft();

	// Exposed for the key tests, which call the two handlers directly: routing through
	// `FSlateApplication::ProcessKeyDownEvent` needs a real `SWindow` and a real focus path, and no
	// automation tier has either.
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& Geometry, const FCharacterEvent& CharacterEvent) override;

	virtual bool SupportsKeyboardFocus() const override { return true; }

	FElysiumTerminalLineDelegate OnSubmitLine;
	FElysiumTerminalCharDelegate OnSubmitCharacter;
	FSimpleDelegate OnAcknowledge;
	FSimpleDelegate OnQuit;
	FSimpleDelegate OnBreak;
	// Raised on every change to the local line, so presentation can put it on the glass. Retail has
	// no counterpart because the client owns both the line and the cell buffer it composes into.
	FSimpleDelegate OnDraftChanged;

private:
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.0, 1.0); }

	bool IsAcknowledgeMode() const { return View.InputMode == 2; }
	bool IsRawMode() const { return View.InputMode == 3; }
	// `this[0xf04]`, the in-use flag `0x100c7090` tests second. `IsOpen()` is the port's answer.
	bool IsInUse() const { return View.IsOpen(); }
	// `this[0xe88]`, the line editor's own flag, tested third — independently of the mode.
	bool IsEditorOpen() const { return View.bLineEditActive; }
	// `FUN_100c6d50`'s insert gate, in retail's order: the acknowledge arm, `m_nMaxInput`, the
	// digits-only flag, then the fit guard that both client paths wrap the whole re-render in.
	bool CanAppend(TCHAR Character) const;

	FElysiumTerminalView View;
	FString DraftText;
};
