#include "Visual/ElysiumExpressionTable.h"

#include "ElysiumContentPaths.h"
#include "ElysiumExpressionData.h"

#include "HAL/IConsoleManager.h"
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
	// an unresolvable `param` costs one diagnostic lookup rather than repeated package requests.
	FCriticalSection GCacheLock;
	TMap<FString, TSharedPtr<const FElysiumExpressionTable>> GCache;
#if WITH_DEV_AUTOMATION_TESTS
	TMap<FString, TSharedPtr<const FElysiumExpressionTable>> GInlineTables;
#endif
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

	// Diagnostic/test preparation only. Gameplay consumes the map's prepared native views.
	TSharedPtr<const FElysiumExpressionTable> Result;
	FString Error;
	if (!IsInGameThread())
	{
		UE_LOG(LogElysiumExpressions, Warning, TEXT("native expression inspection requires the game thread"));
		return nullptr;
	}
	const auto* Corpus = LoadObject<UElysiumExpressionTables>(nullptr, *FElysiumContentPaths::BakedExpressionTables());
	if (Corpus)
	{
		if (const auto* Data = Corpus->ResolveEvent(Stem, Class, Error)) Result = Data->PrepareLegacyView(Error);
	}
	else Error = TEXT("native expression corpus is absent; run elysium import expression-tables");
	if (!Result.IsValid())
		UE_LOG(LogElysiumExpressions, Warning, TEXT("expression table '%s' (class '%s'): %s"), *Stem, *Class, *Error);

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
	// asks for — the native candidate suffix is what the class selects; this fixture has no asset dependency.
	GCache.Add(Norm + TEXT("|expressions"), bOk ? Table : nullptr);
	GCache.Add(Norm + TEXT("|phonemes"), bOk ? Table : nullptr);
#if WITH_DEV_AUTOMATION_TESTS
	GInlineTables.Add(Norm + TEXT("|expressions"), bOk ? Table : nullptr);
	GInlineTables.Add(Norm + TEXT("|phonemes"), bOk ? Table : nullptr);
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
TSharedPtr<const FElysiumExpressionTable> ElysiumExpressions::FindInlineForTest(const FString& Param, const FString& Class)
{
	FString Stem = NormalizeStem(Param);
	if (Stem.EndsWith(TEXT(".mdl"))) Stem.LeftChopInline(4);
	FScopeLock Lock(&GCacheLock);
	for (const FString& Candidate : {Stem, Stem + TEXT("_") + Class.ToLower()})
		if (const auto* Table = GInlineTables.Find(Candidate + TEXT("|") + Class.ToLower())) return *Table;
	return nullptr;
}
#endif

void ElysiumExpressions::ClearCache()
{
	FScopeLock Lock(&GCacheLock);
	GCache.Reset();
	GCacheHits = 0;
	GCacheMisses = 0;
#if WITH_DEV_AUTOMATION_TESTS
	GInlineTables.Reset();
#endif
}

void ElysiumExpressions::CacheStats(int32& OutEntries, int32& OutHits, int32& OutMisses)
{
	FScopeLock Lock(&GCacheLock);
	OutEntries = GCache.Num();
	OutHits = GCacheHits;
	OutMisses = GCacheMisses;
}

// elysium.expression.
//
// The expression reader's own verb, in the role `elysium.scene parse` plays for the `.vcd` reader:
// resolve a scene's `param`/`param2` the way the runtime resolves it, before asking a face to move.
// No world and no map, so it works over the whole 249-file corpus.

static FAutoConsoleCommandWithArgsAndOutputDevice GElysiumExpressionCmd(
	TEXT("elysium.expression"),
	TEXT("Faceposer weight tables: `elysium.expression <param>` lists a table's rows, "
	     "`<param> <row>` dumps one row's controller weights."),
	FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, FOutputDevice& Ar)
		{
			if (Args.Num() < 1)
			{
				int32 Entries = 0, Hits = 0, Misses = 0;
				ElysiumExpressions::CacheStats(Entries, Hits, Misses);
				Ar.Logf(TEXT("expression tables under %s; cache: %d entries, %d hits, %d misses"),
					*FElysiumContentPaths::BakedExpressionTables(), Entries, Hits, Misses);
				return;
			}
			TSharedPtr<const FElysiumExpressionTable> Table =
				ElysiumExpressions::Load(Args[0], ElysiumExpressions::ExpressionClass);
			if (!Table.IsValid())
			{
				Ar.Logf(ELogVerbosity::Warning, TEXT("'%s' resolves to no table under %s"),
					*Args[0], *FElysiumContentPaths::BakedExpressionTables());
				return;
			}
			Ar.Logf(TEXT("%s: %d key(s), %d row(s)%s%s"), *Table->Stem, Table->Keys.Num(),
				Table->Rows.Num(), Table->bHasWeighting ? TEXT(", $hasweighting") : TEXT(""),
				Table->NumMalformedRows > 0
					? *FString::Printf(TEXT(", %d malformed row(s) dropped"), Table->NumMalformedRows) : TEXT(""));

			if (Args.Num() < 2)
			{
				for (const FElysiumExpressionRow& Row : Table->Rows)
				{
					Ar.Logf(TEXT("    \"%s\"  %s"), *Row.Name, *Row.Description);
				}
				return;
			}
			const int32 Index = Table->FindRow(Args[1]);
			if (Index == INDEX_NONE)
			{
				Ar.Logf(ELogVerbosity::Warning, TEXT("no row named '%s' in %s"), *Args[1], *Table->Stem);
				return;
			}
			const FElysiumExpressionRow& Row = Table->Rows[Index];
			Ar.Logf(TEXT("  row \"%s\" (%s)  %s"), *Row.Name, *Row.Class, *Row.Description);
			for (int32 k = 0; k < Table->Keys.Num(); ++k)
			{
				// A key at influence 0 is one this row does not participate in; printed anyway, so
				// what the row leaves alone is as visible as what it writes.
				Ar.Logf(TEXT("    %-24s %.3f  x%.3f"), *Table->Keys[k], Row.Values[k], Row.Weights[k]);
			}
		}));
