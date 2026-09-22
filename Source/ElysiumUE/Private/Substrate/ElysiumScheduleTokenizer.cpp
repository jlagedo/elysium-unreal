#include "Substrate/ElysiumScheduleTokenizer.h"

namespace
{
	/** `{ } ( ) ' :` -- each its own token, always. */
	bool IsSingleCharacterToken(TCHAR Character)
	{
		switch (Character)
		{
		case TEXT('{'):
		case TEXT('}'):
		case TEXT('('):
		case TEXT(')'):
		case TEXT('\''):
		case TEXT(':'):
			return true;
		default:
			return false;
		}
	}

	/** Everything at or below space separates. Not `FChar::IsWhitespace`: retail's test is the
	 *  byte value, so a NUL, a bell and a form feed all separate here and all would not there. */
	bool IsSeparator(TCHAR Character)
	{
		return Character <= TEXT(' ');
	}
}

TArray<FElysiumScheduleToken> ElysiumScheduleTokenizer::Tokenize(const FString& Body)
{
	TArray<FElysiumScheduleToken> Tokens;

	const TCHAR* const Data = *Body;
	const int32 Limit = Body.Len();
	int32 Cursor = 0;

	while (Cursor < Limit)
	{
		const TCHAR Character = Data[Cursor];

		if (IsSeparator(Character))
		{
			++Cursor;
			continue;
		}

		// `//` to end of line. A `/` that is not doubled is an ordinary word character, which is
		// why this tests the pair rather than the slash.
		if (Character == TEXT('/') && Cursor + 1 < Limit && Data[Cursor + 1] == TEXT('/'))
		{
			int32 LineEnd = Cursor;
			while (LineEnd < Limit && Data[LineEnd] != TEXT('\n'))
			{
				++LineEnd;
			}
			Cursor = (LineEnd < Limit) ? LineEnd + 1 : Limit;
			continue;
		}

		// A quoted group. No escape processing at all, and an unterminated quote runs to the end
		// of the text rather than failing -- there is no tokenizer failure path to take.
		if (Character == TEXT('"'))
		{
			int32 Close = Cursor + 1;
			while (Close < Limit && Data[Close] != TEXT('"'))
			{
				++Close;
			}
			FElysiumScheduleToken& Token = Tokens.AddDefaulted_GetRef();
			Token.Text = Body.Mid(Cursor + 1, Close - Cursor - 1);
			Token.Offset = Cursor;
			Token.Length = FMath::Min(Close + 1, Limit) - Cursor;
			Cursor = FMath::Min(Close + 1, Limit);
			continue;
		}

		if (IsSingleCharacterToken(Character))
		{
			FElysiumScheduleToken& Token = Tokens.AddDefaulted_GetRef();
			Token.Text = FString::Chr(Character);
			Token.Offset = Cursor;
			Token.Length = 1;
			++Cursor;
			continue;
		}

		const int32 Start = Cursor;
		while (Cursor < Limit)
		{
			const TCHAR Here = Data[Cursor];
			if (IsSeparator(Here) || IsSingleCharacterToken(Here) || Here == TEXT('"'))
			{
				break;
			}
			++Cursor;
		}
		FElysiumScheduleToken& Token = Tokens.AddDefaulted_GetRef();
		Token.Text = Body.Mid(Start, Cursor - Start);
		Token.Offset = Start;
		Token.Length = Cursor - Start;
	}

	return Tokens;
}
