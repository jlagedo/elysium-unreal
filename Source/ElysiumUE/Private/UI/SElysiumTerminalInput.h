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
//    movement bindings, and the character path does the insert. Order matters and is preserved:
//    backtick first (the console keeps it), then the Ctrl chord, then raw mode, then acknowledge
//    mode, then the ordinary line editor.
//  * **Character insert** `FUN_100c6d50` — ignores backtick and the control codes `8`/`9`/`10`/
//    `13`/`27`, honours `m_nMaxInput` (**zero means unlimited**) and the digits-only flag
//    `m_HackFlags 0x2`, and inserts at the caret.
//
// Named modernizations (`docs/vtmb/computer-terminals.md` §8.7):
//
//  * Retail latches Ctrl as a chord byte (`0x85`, then `c`). The port reads Slate's modifier state
//    on the `C` key-down instead, which is the same gesture through a real keyboard model.
//  * Retail forwards the raw key code in `hackcmd %c`. The port forwards the TRANSLATED character
//    from `OnKeyChar`, so a non-US layout types what the player sees.
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

	// The published session. A change of input mode clears the draft, which is what retail's type-3
	// message does when it reopens the line editor over a freshly cleared screen (`FUN_100c82e0`
	// zeroes `+0xed8`).
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
	// `FUN_100c6d50`'s insert gate, in retail's order: the acknowledge arm, `m_nMaxInput`, the
	// digits-only flag, then the fit guard that both client paths wrap the whole re-render in.
	bool CanAppend(TCHAR Character) const;

	FElysiumTerminalView View;
	FString DraftText;
};
