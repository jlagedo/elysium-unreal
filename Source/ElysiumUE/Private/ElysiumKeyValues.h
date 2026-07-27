#pragma once

#include "CoreMinimal.h"

// Minimal Source KeyValues reader, shared by every runtime consumer of VtMB's `.res`/`.vmt`
// grammar: the P6.3 sound schemes (`ElysiumSoundScheme.cpp`) and the P4.10 sign definitions
// (`ElysiumSignData.cpp`).
//
// Grammar: quoted or bare tokens, `{ }` nesting, `//` line comments outside quotes. Keys fold
// to lower (Source KV is case-insensitive); values keep their case. Repeated block keys are
// preserved in order (a scheme's `RandomSound`, a sign's `TextBlock`), and so are repeated *leaf*
// keys — `Values` keeps the last, `Pairs` keeps them all.
//
// Brace depth is the only structural signal. Several `vdata/` tables indent a child block at
// column 0 (`levelingtemplate_000.txt`'s templates, one `rules.txt` key) and write whole blocks
// inline on one line (`Level { "Strength" "2" }`), so indentation and line breaks mean nothing.
//
// A quoted value MAY SPAN LINES: 127 of the game's 187 loose sign definitions put a wrapped
// paragraph in a single `"Text"` value. The tokenizer is therefore a character stream over the
// whole file, not a per-line scan — a line-based scan truncates such a value at the first
// newline and turns its remainder into stray tokens.
//
// A quoted value may also carry `\"` — `clandoc000.txt`'s Malkavian description quotes the word
// "insight". Only that one escape is honoured; a lone backslash stays literal, so the Windows
// paths the same files carry (`models\props\x.mdl`) read verbatim. Both defects have the same
// consequence and it is not a truncated string: a stray quote shifts every following key/value
// pair by one, so the next `{` is consumed as a value and the block nesting collapses.

namespace ElysiumKeyValues
{
	struct FKvNode
	{
		TMap<FString, FString> Values;                     // leaf key -> value (last wins)
		TArray<TPair<FString, FString>> Pairs;             // the same leaves in file order, repeats kept
		TArray<TPair<FString, TSharedPtr<FKvNode>>> Kids;  // ordered child blocks (repeatable keys)

		const FString* Value(const TCHAR* Key) const { return Values.Find(FString(Key).ToLower()); }
		FString Str(const TCHAR* Key, const FString& Def) const { const FString* V = Value(Key); return V ? *V : Def; }
		float   Flt(const TCHAR* Key, float Def) const { const FString* V = Value(Key); return V ? FCString::Atof(**V) : Def; }
		int32   Int(const TCHAR* Key, int32 Def) const { const FString* V = Value(Key); return V ? FCString::Atoi(**V) : Def; }
		bool    Bool(const TCHAR* Key, bool Def) const { const FString* V = Value(Key); return V ? (FCString::Atoi(**V) != 0) : Def; }

		// True when the key is present at all (an authored-but-empty value is meaningful: a sign's
		// `"XPos" ""` selects CSignUI's centring branch, which differs from XPos being absent).
		bool Has(const TCHAR* Key) const { return Value(Key) != nullptr; }

		// Every value authored under Key, in file order. A block MAY repeat a leaf key and mean it:
		// 17 of `stats.txt`'s Active_Disciplines carry two `IncPredependency` gates ("BloodPool > 0"
		// and "Health < Max_Health"), and one `clandoc000.txt` General block names `M_Hands` twice.
		// `Values` keeps only the last of those, so a reader that needs the whole set reads here.
		void ValuesFor(const TCHAR* Key, TArray<FString>& Out) const
		{
			const FString L = FString(Key).ToLower();
			for (const TPair<FString, FString>& P : Pairs) { if (P.Key == L) { Out.Add(P.Value); } }
		}

		const FKvNode* Child(const TCHAR* Key) const
		{
			const FString L = FString(Key).ToLower();
			for (const TPair<FString, TSharedPtr<FKvNode>>& K : Kids) { if (K.Key == L) { return K.Value.Get(); } }
			return nullptr;
		}
	};

	// Whole-text character stream: quotes suppress `//` and survive newlines; `{`/`}` are their own
	// tokens; bare runs end at whitespace or a brace.
	inline void Tokenize(const FString& Text, TArray<FString>& Out)
	{
		const int32 N = Text.Len();
		int32 i = 0;
		while (i < N)
		{
			const TCHAR C = Text[i];
			if (FChar::IsWhitespace(C)) { ++i; continue; }
			if (C == TEXT('/') && i + 1 < N && Text[i + 1] == TEXT('/'))
			{
				while (i < N && Text[i] != TEXT('\n')) { ++i; }   // comment runs to end of line
				continue;
			}
			if (C == TEXT('{') || C == TEXT('}')) { Out.Add(FString(1, &Text[i])); ++i; continue; }
			if (C == TEXT('"'))
			{
				++i;
				FString Tok;
				while (i < N && Text[i] != TEXT('"'))
				{
					if (Text[i] == TEXT('\\') && i + 1 < N && Text[i + 1] == TEXT('"')) { ++i; }
					Tok.AppendChar(Text[i]);
					++i;
				}
				++i;   // closing quote (tolerates an unterminated final string)
				Out.Add(MoveTemp(Tok));
				continue;
			}
			FString Tok;
			while (i < N && !FChar::IsWhitespace(Text[i]) && Text[i] != TEXT('{') && Text[i] != TEXT('}'))
			{
				Tok.AppendChar(Text[i]);
				++i;
			}
			Out.Add(MoveTemp(Tok));
		}
	}

	inline TSharedPtr<FKvNode> ParseBlock(const TArray<FString>& Toks, int32& Pos)
	{
		TSharedPtr<FKvNode> Node = MakeShared<FKvNode>();
		while (Pos < Toks.Num())
		{
			const FString& T = Toks[Pos];
			if (T == TEXT("}")) { ++Pos; break; }
			const FString Key = T.ToLower();
			++Pos;
			if (Pos < Toks.Num() && Toks[Pos] == TEXT("{"))
			{
				++Pos;
				Node->Kids.Emplace(Key, ParseBlock(Toks, Pos));
			}
			else if (Pos < Toks.Num())
			{
				Node->Values.Add(Key, Toks[Pos]);
				Node->Pairs.Emplace(Key, Toks[Pos]);
				++Pos;
			}
		}
		return Node;
	}

	// Parse a whole file's text into a root node whose Kids are the top-level blocks. Returns null
	// when the text has no tokens.
	inline TSharedPtr<FKvNode> ParseText(const FString& Text)
	{
		TArray<FString> Toks;
		Tokenize(Text, Toks);
		if (Toks.Num() == 0)
		{
			return nullptr;
		}
		int32 Pos = 0;
		return ParseBlock(Toks, Pos);
	}
}
