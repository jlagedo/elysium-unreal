#include "Substrate/ElysiumTerminalScreenBuffer.h"

void FElysiumTerminalScreenBuffer::Reset(int32 InColumns, int32 InRows)
{
	NumColumns = FMath::Max(1, InColumns);
	NumRows = FMath::Max(1, InRows);
	MarginLeft = 0;
	MarginRight = 0;
	CurrentStyle = DefaultStyle;
	Cells.SetNumUninitialized(NumColumns * NumRows);
	Clear();
}

uint16 FElysiumTerminalScreenBuffer::Cell(int32 InColumn, int32 InRow) const
{
	return Inside(InColumn, InRow) ? Cells[Index(InColumn, InRow)] : uint16(0);
}

TCHAR FElysiumTerminalScreenBuffer::CharAt(int32 InColumn, int32 InRow) const
{
	const uint16 Value = Cell(InColumn, InRow);
	const uint16 Code = (Value >> 8) != 0 ? uint16(Value >> 8) : uint16(Value & 0x7f);
	return Code >= 0x20 ? TCHAR(Code) : TEXT(' ');
}

FString FElysiumTerminalScreenBuffer::RowText(int32 InRow) const
{
	FString Text;
	Text.Reserve(NumColumns);
	for (int32 InColumn = 0; InColumn < NumColumns; ++InColumn)
	{
		Text.AppendChar(CharAt(InColumn, InRow));
	}
	return Text;
}

void FElysiumTerminalScreenBuffer::Fill(int32 FromRow, int32 ToRow)
{
	const uint16 Blank = uint16(CurrentStyle) | uint16(' ');
	for (int32 InRow = FMath::Max(0, FromRow); InRow < FMath::Min(ToRow, NumRows); ++InRow)
	{
		for (int32 InColumn = 0; InColumn < NumColumns; ++InColumn)
		{
			Cells[Index(InColumn, InRow)] = Blank;
		}
	}
}

void FElysiumTerminalScreenBuffer::SetCursor(int32 InColumn, int32 InRow)
{
	// `FUN_100c7ec0`: `+0xe88 = 0` first, then the column is offset by the left margin and clamped
	// to `[0, columns-1]`; the row is clamped to `[0, rows-1]`.
	++NumLineEditBreaks;
	Column = FMath::Clamp(MarginLeft + InColumn, 0, NumColumns - 1);
	Row = FMath::Clamp(InRow, 0, NumRows - 1);
}

void FElysiumTerminalScreenBuffer::Print(const FString& Text)
{
	// `FUN_100c7fb0`, which also opens with `+0xe88 = 0`. `Limit` is `columns - rightMargin`, the
	// column a word may not cross.
	++NumLineEditBreaks;
	const int32 Limit = NumColumns - MarginRight;
	const int32 Len = Text.Len();
	int32 At = 0;
	while (At < Len)
	{
		int32 WordLen = 0;
		while (At + WordLen < Len)
		{
			const TCHAR C = Text[At + WordLen];
			if (C == TEXT('\r') || C == TEXT('\n') || C == TEXT(' ') || C == 0)
			{
				break;
			}
			++WordLen;
		}
		if (Limit < Column + WordLen && MarginLeft + WordLen < Limit)
		{
			PutChar(10);
		}
		for (int32 Offset = 0; Offset < WordLen; ++Offset)
		{
			PutChar(Text[At + Offset]);
		}
		At += WordLen;
		if (At >= Len)
		{
			break;
		}
		const TCHAR Separator = Text[At];
		if (Separator == 0)
		{
			break;
		}
		if (Column < Limit || Separator != TEXT(' '))
		{
			PutChar(Separator);
		}
		++At;
	}
}

void FElysiumTerminalScreenBuffer::Clear()
{
	// `FUN_100c7f50`: `+0xe88 = 0`, then the fill.
	++NumLineEditBreaks;
	Fill(0, NumRows);
	Row = 0;
	Column = MarginLeft;
}

void FElysiumTerminalScreenBuffer::SetMargins(int32 Left, int32 Right)
{
	// `FUN_100c7e60`.
	const int32 Max = NumColumns - 1;
	int32 L = Left;
	int32 R = Right;
	for (;;)
	{
		const int32 ClampedL = L <= Max ? FMath::Max(0, L) : Max;
		const int32 ClampedR = R <= Max ? FMath::Max(0, R) : Max;
		if (ClampedL + ClampedR < NumColumns - 2)
		{
			MarginLeft = ClampedL;
			MarginRight = ClampedR;
			return;
		}
		if (ClampedL == 0 && ClampedR == 0)
		{
			// A grid too narrow for any margin (columns <= 2): retail would spin; hold zero.
			MarginLeft = 0;
			MarginRight = 0;
			return;
		}
		L = ClampedL - 1;
		R = ClampedR - 1;
	}
}

void FElysiumTerminalScreenBuffer::Backspace()
{
	PutChar(0x7f);
	MarginLeft = 0;
}

void FElysiumTerminalScreenBuffer::Echo(TCHAR Character)
{
	PutChar(Character);
	MarginLeft = 0;
}

void FElysiumTerminalScreenBuffer::PutChar(int32 Code)
{
	if (Code == 0x7f)
	{
		if (Inside(Column, Row))
		{
			Cells[Index(Column, Row)] = 0;
		}
		if (Column <= MarginLeft)
		{
			Column = NumColumns - 1;
			Row = FMath::Max(0, Row - 1);   // retail does not clamp; a wrap above row 0 is unreachable
		}
		else
		{
			--Column;
		}
		return;
	}
	if (Code == 0x0d || Code == 0x0a)
	{
		++Row;
		Column = MarginLeft;
		if (NumRows - 1 <= Row)
		{
			ScrollUp(1);
			Row = NumRows - 1;
		}
		return;
	}
	if (Code > 0x1a && Code < 0x7f)
	{
		if (NumColumns <= Column)
		{
			PutChar(10);
		}
		if (Inside(Column, Row))
		{
			Cells[Index(Column, Row)] = uint16(Code) | uint16(CurrentStyle);
		}
		++Column;
		return;
	}
	if (Code > 0x7f)
	{
		if (NumColumns <= Column)
		{
			PutChar(10);
		}
		if (Inside(Column, Row))
		{
			Cells[Index(Column, Row)] = uint16((Code & 0xff) << 8) | uint16(CurrentStyle);
		}
		++Column;
	}
}

void FElysiumTerminalScreenBuffer::ScrollUp(int32 Lines)
{
	// `FUN_100c8210` zeroes `+0xe88` BEFORE its `0 < lines` guard, so even a no-op scroll closes an
	// open edit.
	++NumLineEditBreaks;
	if (Lines <= 0)
	{
		return;
	}
	if (NumRows <= Lines)
	{
		Clear();
		return;
	}
	const int32 Moved = (NumRows - Lines) * NumColumns;
	FMemory::Memmove(Cells.GetData(), Cells.GetData() + Lines * NumColumns, Moved * sizeof(uint16));
	Fill(NumRows - Lines, NumRows);
	Row = FMath::Clamp(Row - Lines, 0, NumRows - 1);
}
