// The tokenizer the schedule-text parser reads through.
//
// It is NOT the parser's own: retail calls the engine's (`VEngineServer` slot 98, `+0x188`; engine
// side `0x2003c800`), and the grammar in `ElysiumScheduleText.h` is written against whatever that
// hands back. Four rules, from `docs/vtmb/npc-ai/schedule-kernel.md` § "The schedule-text parser
// `0x1030d850`, walked":
//
//   * every byte at or below `0x20` separates -- so a text's line endings, tabs and indentation
//     carry no meaning at all;
//   * `//` runs to the end of the line;
//   * a double quote groups a token, with NO escape processing (a `\` is a `\`);
//   * each of `{ } ( ) ' :` is a token on its own however it is spelled against its neighbours.
//
// The last rule is the one that shapes the grammar: `NPCFlag:FORCE_RELAXED_ANIMS` arrives as THREE
// tokens, and the parser recognises a prefixed operand by testing the middle one against the
// literal `":"` (`0x106142ec`) rather than by splitting a token itself.

#pragma once

#include "CoreMinimal.h"

/** One token, and the span of the text it was cut from. */
struct FElysiumScheduleToken
{
	FString Text;
	int32 Offset = 0;
	int32 Length = 0;

	int32 End() const { return Offset + Length; }

	/** Every keyword, prefix and value compare in the parser is `strcmpi`. */
	bool Equals(const TCHAR* Other) const { return Text.Equals(Other, ESearchCase::IgnoreCase); }

	/** Whether this is the one-character token `Character` -- the `:` test, mostly. */
	bool Is(TCHAR Character) const { return Text.Len() == 1 && Text[0] == Character; }
};

namespace ElysiumScheduleTokenizer
{
	/** Cut `Body` into tokens. A text that is entirely separators and comments yields none. */
	TArray<FElysiumScheduleToken> Tokenize(const FString& Body);
}
