#pragma once

#include "CoreMinimal.h"

// The terminal's character-cell screen: the authority's copy of `C_BaseTerminal`'s cell buffer
// (docs/vtmb/computer-terminals.md §8.3, TERM15). Retail streams entity messages from the server
// into this buffer on the client; here the authority owns the grid and the presentation reads it
// back, so every prompt, directory draw and screensaver label is a cursor write into it, exactly
// where retail's put-char would have landed it.
//
// Each cell is a `uint16`: the low 7 bits are the ASCII code, `0x80` is the style bit (the
// rasterizer XORs the foreground/background pair on it), and a high byte selects an extended glyph.
// Retail's storage stride is a fixed 36 cells; the stride here is the active column count, which
// no authored screen can tell apart (every `prop_hacking` in the corpus is 36×24).
//
// Client entity-message handlers (`C_BaseTerminal::vfunc10` `0x100c83a0`), each a method below:
//   1 `FUN_100c7ec0` set cursor      2 `FUN_100c7fb0` print (word-wrapping)
//   4 `FUN_100c7f50` clear           5 `FUN_100c7f40` style 0x80    6 `FUN_100c7f30` style 0
//   7 `FUN_100c7e60` set margins     8 backspace (`0x7f` through put-char, then left margin 0)
//   9 echo one character (put-char, then left margin 0)
// Every handler except 5/6 also clears the client's local line-edit flag, which the widget owns
// here and the view reports as the cursor being editable.
class FElysiumTerminalScreenBuffer
{
public:
	static constexpr uint16 StyleBit = 0x80;
	// `+0xe74` starts at the value the screensaver "resets" to (type 5), so the plain screen is
	// drawn with the bit set and the alternate style (type 6) is the inverse block.
	static constexpr uint8 DefaultStyle = 0x80;

	FElysiumTerminalScreenBuffer() { Reset(36, 24); }

	// Size the grid (clamped to at least 1×1), zero the margins, restore the default style, clear.
	void Reset(int32 InColumns, int32 InRows);

	int32 Columns() const { return NumColumns; }
	int32 Rows() const { return NumRows; }
	int32 CursorColumn() const { return Column; }
	int32 CursorRow() const { return Row; }
	int32 LeftMargin() const { return MarginLeft; }
	int32 RightMargin() const { return MarginRight; }
	uint8 Style() const { return CurrentStyle; }

	uint16 Cell(int32 InColumn, int32 InRow) const;
	TCHAR CharAt(int32 InColumn, int32 InRow) const;
	bool StyleAt(int32 InColumn, int32 InRow) const { return (Cell(InColumn, InRow) & StyleBit) != 0; }
	// The row's characters, one per column (NUL and control cells read as spaces).
	FString RowText(int32 InRow) const;
	// The row with trailing spaces removed — what a test compares against a retail capture.
	FString RowTextTrimmed(int32 InRow) const { return RowText(InRow).TrimEnd(); }

	// Type 1: cursor at (`LeftMargin + Column`, `Row`), each clamped to the grid.
	void SetCursor(int32 InColumn, int32 InRow);
	// Type 2: word-wrapping print. A word that would cross the right margin — and fits on a row —
	// starts on the next row; the single space after a word is dropped when the cursor is already
	// past the right margin. Every byte still lands through `PutChar`.
	void Print(const FString& Text);
	// Type 4: every cell becomes `style | ' '`, cursor to row 0 at the left margin.
	void Clear();
	// Type 5 / 6: the current style byte.
	void SetStyleDefault() { CurrentStyle = StyleBit; }
	void SetStyleAlternate() { CurrentStyle = 0; }
	// Type 7: left/right margins, each clamped to `columns - 1`, then both reduced together until
	// `left + right < columns - 2`.
	void SetMargins(int32 Left, int32 Right);
	// Type 8 / 9: a local edit byte through put-char; both drop the left margin to 0 afterwards.
	void Backspace();
	void Echo(TCHAR Character);

	// `FUN_100c8060`, the one consumer of every printed byte.
	void PutChar(int32 Code);
	// `FUN_100c8210(n)`: `n >= rows` clears; otherwise rows `n..` move to the top, the vacated rows
	// fill with `style | ' '`, and the cursor row moves up by `n` (clamped).
	void ScrollUp(int32 Lines);

	bool operator==(const FElysiumTerminalScreenBuffer& Other) const
	{
		return NumColumns == Other.NumColumns && NumRows == Other.NumRows && Cells == Other.Cells;
	}

private:
	int32 NumColumns = 36;
	int32 NumRows = 24;
	int32 Column = 0;
	int32 Row = 0;
	int32 MarginLeft = 0;
	int32 MarginRight = 0;
	uint8 CurrentStyle = DefaultStyle;
	TArray<uint16> Cells;

	int32 Index(int32 InColumn, int32 InRow) const { return InRow * NumColumns + InColumn; }
	bool Inside(int32 InColumn, int32 InRow) const
	{
		return InColumn >= 0 && InColumn < NumColumns && InRow >= 0 && InRow < NumRows;
	}
	void Fill(int32 FromRow, int32 ToRow);
};
