#include "Visual/ElysiumExpressionTable.h"

#include "ElysiumContentPaths.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumExpressions, Log, All);

namespace
{
	// One table line -> tokens, quotes stripped. A quoted token keeps its spaces, which is what
	// carries `"Very Frightened"` and `"Big : voiced alveolar stop"` through as single words.
	void TokenizeLine(const FString& Line, TArray<FString>& Out)
	{
		Out.Reset();
		const TCHAR* P = *Line;
		while (*P != TEXT('\0'))
		{
			if (FChar::IsWhitespace(*P))
			{
				++P;
				continue;
			}
			if (*P == TEXT('"'))
			{
				++P;
				const TCHAR* Start = P;
				while (*P != TEXT('\0') && *P != TEXT('"'))
				{
					++P;
				}
				Out.Emplace(FString::ConstructFromPtrSize(Start, static_cast<int32>(P - Start)));
				if (*P == TEXT('"'))
				{
					++P;
				}
				continue;
			}
			const TCHAR* Start = P;
			while (*P != TEXT('\0') && !FChar::IsWhitespace(*P))
			{
				++P;
			}
			Out.Emplace(FString::ConstructFromPtrSize(Start, static_cast<int32>(P - Start)));
		}
	}

	// A token that is a number. The row split is by count, and the counts are right across all 249
	// shipped tables — but a row one number short still has the right token count when its trailing
	// description slides into the last numeric slot, and that misread lands every value one key late
	// with no visible fault. Checking the slots are numeric is what makes such a row detectable.
	bool IsNumber(const FString& Token)
	{
		bool bDigit = false;
		for (int32 i = 0; i < Token.Len(); ++i)
		{
			const TCHAR C = Token[i];
			if (FChar::IsDigit(C))
			{
				bDigit = true;
			}
			else if (C != TEXT('+') && C != TEXT('-') && C != TEXT('.')
				&& C != TEXT('e') && C != TEXT('E'))
			{
				return false;
			}
		}
		return bDigit;
	}

	// The cache, in the shape `ElysiumScene`'s is: one entry per resolved key, negatives included so
	// an unresolvable `param` costs one stat rather than one per event per frame.
	FCriticalSection GCacheLock;
	TMap<FString, TSharedPtr<const FElysiumExpressionTable>> GCache;
	int32 GCacheHits = 0;
	int32 GCacheMisses = 0;

	constexpr int32 GMaxCacheEntries = 512;

	// A `param` (or a phoneme class's model stem) -> the bare lowercased stem it names. The corpus
	// carries neither a directory nor an extension on any of them; both are tolerated anyway,
	// because the RE'd name format is a path (`expressions/%s_%s.vfe`).
	FString NormalizeStem(const FString& Param)
	{
		FString Stem = Param;
		Stem.ReplaceInline(TEXT("\\"), TEXT("/"));
		Stem.TrimStartAndEndInline();
		int32 Slash = INDEX_NONE;
		if (Stem.FindLastChar(TEXT('/'), Slash))
		{
			Stem.RightChopInline(Slash + 1, EAllowShrinking::No);
		}
		if (Stem.EndsWith(TEXT(".vfe"), ESearchCase::IgnoreCase)
			|| Stem.EndsWith(TEXT(".txt"), ESearchCase::IgnoreCase))
		{
			Stem.LeftChopInline(4, EAllowShrinking::No);
		}
		return Stem.ToLower();
	}
}

int32 FElysiumExpressionTable::FindRow(const FString& Name) const
{
	const FString Wanted = Name.TrimStartAndEnd();
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		if (Rows[i].Name.TrimStartAndEnd().Equals(Wanted, ESearchCase::IgnoreCase))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 FElysiumExpressionTable::FindRowByPhonemeCode(int32 Code) const
{
	const int32* Found = RowByPhonemeCode.Find(Code);
	return Found != nullptr ? *Found : INDEX_NONE;
}

bool FElysiumExpressionTable::ParseText(const FString& Text, const FString& InStem, FString& OutError)
{
	Stem = InStem;
	Keys.Reset();
	Rows.Reset();
	RowByPhonemeCode.Reset();
	bHasWeighting = false;
	NumMalformedRows = 0;

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);

	TArray<FString> Toks;
	for (const FString& Raw : Lines)
	{
		const FString Trimmed = Raw.TrimStart();
		if (Trimmed.IsEmpty())
		{
			continue;
		}
		if (Trimmed.StartsWith(TEXT("$")))
		{
			TokenizeLine(Raw, Toks);
			if (Toks.IsEmpty())
			{
				continue;
			}
			if (Toks[0].Equals(TEXT("$keys"), ESearchCase::IgnoreCase))
			{
				// A second `$keys` would re-base every row after it; no shipped file carries one, and
				// taking the first keeps the rows already read consistent with the key list.
				if (Keys.IsEmpty())
				{
					Keys.Append(Toks.GetData() + 1, Toks.Num() - 1);
				}
			}
			else if (Toks[0].Equals(TEXT("$hasweighting"), ESearchCase::IgnoreCase))
			{
				bHasWeighting = true;
			}
			// Any other directive is Faceposer's and unused here.
			continue;
		}
		if (!Trimmed.StartsWith(TEXT("\"")))
		{
			continue;   // not a row
		}

		TokenizeLine(Raw, Toks);
		// `"<name>" "<class>" <numbers…> ["<description>"]`. With `$hasweighting` a row carries two
		// floats per key — the value and its weight, interleaved — which is the one thing a reader
		// can get wrong and still produce a plausible-looking face.
		const int32 Stride = bHasWeighting ? 2 : 1;
		const int32 Expected = Keys.Num() * Stride;
		if (Toks.Num() < 2 + Expected)
		{
			++NumMalformedRows;
			continue;
		}
		bool bNumeric = true;
		for (int32 i = 2; i < 2 + Expected && bNumeric; ++i)
		{
			bNumeric = IsNumber(Toks[i]);
		}
		if (!bNumeric)
		{
			++NumMalformedRows;
			continue;
		}

		FElysiumExpressionRow Row;
		Row.Name = Toks[0];
		Row.Class = Toks[1];
		// The class as a code point: `0x0279` -> 633, `k` -> 107. Anything else (`_`, and the whole
		// expression half) has no code and is reachable only by name.
		if (Row.Class.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase) && Row.Class.Len() > 2)
		{
			Row.PhonemeCode = FParse::HexNumber(*Row.Class.RightChop(2));
		}
		else if (Row.Class.Len() == 1)
		{
			Row.PhonemeCode = static_cast<int32>(Row.Class[0]);
		}
		Row.Values.Reserve(Keys.Num());
		Row.Weights.Reserve(Keys.Num());
		for (int32 k = 0; k < Keys.Num(); ++k)
		{
			const int32 Base = 2 + k * Stride;
			Row.Values.Add(FCString::Atof(*Toks[Base]));
			// Without `$hasweighting` every key is claimed outright, which is what a weight of 1 says.
			Row.Weights.Add(bHasWeighting ? FCString::Atof(*Toks[Base + 1]) : 1.f);
		}
		if (Toks.Num() > 2 + Expected)
		{
			Row.Description = Toks[2 + Expected];
		}
		// First row wins a code, matching the `.vfe`'s own single-entry-per-code array. `_` rows carry
		// no code, so an expression table simply builds an empty map.
		if (Row.PhonemeCode != INDEX_NONE)
		{
			RowByPhonemeCode.FindOrAdd(Row.PhonemeCode, Rows.Num());
		}
		Rows.Add(MoveTemp(Row));
	}

	if (Keys.IsEmpty())
	{
		OutError = FString::Printf(TEXT("'%s': no $keys line"), *Stem);
		return false;
	}
	if (Rows.IsEmpty())
	{
		OutError = FString::Printf(TEXT("'%s': %d key(s) and no usable row"), *Stem, Keys.Num());
		return false;
	}
	return true;
}

TSharedPtr<const FElysiumExpressionTable> ElysiumExpressions::Load(const FString& Param, const FString& Class)
{
	const FString Stem = NormalizeStem(Param);
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	const FString Key = Stem + TEXT("|") + Class.ToLower();

	{
		FScopeLock Lock(&GCacheLock);
		if (const TSharedPtr<const FElysiumExpressionTable>* Found = GCache.Find(Key))
		{
			++GCacheHits;
			return *Found;
		}
	}

	// `<param>.txt` first, then `client.dll`'s `%s_%s` form with the class appended. Nineteen of the
	// 23 authored params resolve on the first candidate and four — bare model stems — on the second.
	TSharedPtr<const FElysiumExpressionTable> Result;
	const FString Candidates[] = { Stem, Stem + TEXT("_") + Class };
	for (const FString& Candidate : Candidates)
	{
		if (Candidate.IsEmpty())
		{
			continue;
		}
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FElysiumContentPaths::ExpressionFile(Candidate + TEXT(".txt"))))
		{
			continue;
		}
		TSharedPtr<FElysiumExpressionTable> Table = MakeShared<FElysiumExpressionTable>();
		FString Error;
		if (Table->ParseText(Text, Candidate, Error))
		{
			Result = Table;
		}
		else
		{
			UE_LOG(LogElysiumExpressions, Warning, TEXT("%s"), *Error);
		}
		break;
	}
	if (!Result.IsValid())
	{
		UE_LOG(LogElysiumExpressions, Log,
			TEXT("expression table '%s' (class '%s') did not resolve under %s"),
			*Stem, *Class, *FElysiumContentPaths::ExpressionsDir());
	}

	FScopeLock Lock(&GCacheLock);
	++GCacheMisses;
	if (GCache.Num() >= GMaxCacheEntries)
	{
		GCache.Reset();
	}
	GCache.Add(Key, Result);
	return Result;
}

void ElysiumExpressions::RegisterInline(const FString& Stem, const FString& Text)
{
	const FString Norm = NormalizeStem(Stem);
	TSharedPtr<FElysiumExpressionTable> Table = MakeShared<FElysiumExpressionTable>();
	FString Error;
	const bool bOk = Table->ParseText(Text, Norm, Error);

	FScopeLock Lock(&GCacheLock);
	// Registered against every class, so a test's inline table resolves whichever suffix the caller
	// asks for — the on-disk fallback is what the class exists to drive, and there is no disk here.
	GCache.Add(Norm + TEXT("|expressions"), bOk ? Table : nullptr);
	GCache.Add(Norm + TEXT("|phonemes"), bOk ? Table : nullptr);
}

void ElysiumExpressions::ClearCache()
{
	FScopeLock Lock(&GCacheLock);
	GCache.Reset();
	GCacheHits = 0;
	GCacheMisses = 0;
}

void ElysiumExpressions::CacheStats(int32& OutEntries, int32& OutHits, int32& OutMisses)
{
	FScopeLock Lock(&GCacheLock);
	OutEntries = GCache.Num();
	OutHits = GCacheHits;
	OutMisses = GCacheMisses;
}
