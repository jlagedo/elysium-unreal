#include "UI/ElysiumTerminalCells.h"

#include "ElysiumViewState.h"

namespace ElysiumTerminalCells
{
	uint16 FElysiumTerminalComposed::Cell(int32 Column, int32 Row) const
	{
		if (Column < 0 || Column >= Columns || Row < 0 || Row >= Rows)
		{
			return 0;
		}
		const int32 Index = Row * Columns + Column;
		return Cells.IsValidIndex(Index) ? Cells[Index] : 0;
	}

	TCHAR FElysiumTerminalComposed::CharAt(int32 Column, int32 Row) const
	{
		// The low seven bits are the ASCII code; `0x80` is the style bit and the high byte is the
		// rasterizer's extended-glyph index, neither of which is a character.
		const TCHAR Code = static_cast<TCHAR>(Cell(Column, Row) & 0x7f);
		return Code < 0x20 ? TCHAR(' ') : Code;
	}

	FString FElysiumTerminalComposed::RowText(int32 Row) const
	{
		FString Out;
		Out.Reserve(Columns);
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			Out.AppendChar(CharAt(Column, Row));
		}
		return Out;
	}

	TArray<FString> FElysiumTerminalComposed::RowTexts() const
	{
		TArray<FString> Out;
		Out.Reserve(Rows);
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			Out.Add(RowText(Row));
		}
		return Out;
	}

	bool DraftFits(const FElysiumTerminalView& View, int32 DraftLength)
	{
		if (DraftLength <= 0)
		{
			return true;   // nothing to draw is always drawable
		}
		// `this[0xe8c]`, the edit origin — the column the type-3 message opened the edit at, which
		// does not move as the line grows.
		return View.EditOriginColumn + DraftLength < View.Columns - View.RightMargin;
	}

	FElysiumTerminalComposed ComposeDraft(const FElysiumTerminalView& View, const FString& Draft)
	{
		FElysiumTerminalComposed Out;
		Out.Cells = View.Cells;
		Out.Columns = FMath::Max(0, View.Columns);
		Out.Rows = FMath::Max(0, View.Rows);
		Out.CursorColumn = View.CursorColumn;
		Out.CursorRow = View.CursorRow;
		if (Out.Columns <= 0 || Out.Rows <= 0 || Out.Cells.Num() < Out.Columns * Out.Rows)
		{
			return Out;   // an unpublished or truncated grid composes to itself
		}
		if (Draft.IsEmpty())
		{
			return Out;
		}
		if (!View.bLineEditActive)
		{
			// `0x100c7090`'s third test: with `+0xe88` clear the body returns before it reaches any
			// re-render, so a line typed before a print or a clear is simply not on the glass.
			return Out;
		}
		if (!DraftFits(View, Draft.Len())
			|| View.EditOriginRow < 0 || View.EditOriginRow >= Out.Rows || View.EditOriginColumn < 0)
		{
			// Retail's guard, and its consequence: the keystroke is discarded, the saved row is not
			// restored and the glass keeps whatever it last showed.
			return Out;
		}

		const uint16 Style = static_cast<uint16>(View.CellStyle & 0x80);
		int32 Column = View.EditOriginColumn;
		for (const TCHAR Character : Draft)
		{
			if (Character < 0x20 || Character > 0x7e || Column >= Out.Columns)
			{
				// Unreachable through `SElysiumTerminalInput` (it accepts only `0x20..0x7e`, and the
				// fit guard above keeps the run inside the row); refusing rather than wrapping keeps
				// this pure function honest about retail's one-row editor.
				break;
			}
			Out.Cells[View.EditOriginRow * Out.Columns + Column] =
				static_cast<uint16>(static_cast<uint16>(Character) | Style);
			++Column;
		}
		// `+0xe78 = editOrigin + caret`, and the caret is the end of the line because the direction
		// keys are permanently disabled. The row is the edit's, which is the authority's cursor row
		// as well: every message that could move that row closes the edit first.
		Out.CursorColumn = Column;
		Out.CursorRow = View.EditOriginRow;
		Out.bDraftDrawn = true;
		return Out;
	}
}
