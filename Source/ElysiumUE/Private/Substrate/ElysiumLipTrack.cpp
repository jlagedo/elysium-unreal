#include "Substrate/ElysiumLipTrack.h"

#include "ElysiumContentPaths.h"
#include "Substrate/ElysiumSceneData.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumLip, Log, All);

namespace
{
	// Every helper here carries a file-unique name. `TokenizeLine`, `GCacheLock`, `GCache`,
	// `GCacheHits`, `GCacheMisses` and `GMaxCacheEntries` already exist in the anonymous namespaces
	// of `ElysiumSceneData.cpp` and `ElysiumExpressionTable.cpp`, and all three files can land in one
	// unity blob.

	// One parsed line plus whatever block followed it — the same uniform node the `.vcd` reader uses,
	// because the two formats share their grammar.
	struct FLipNode
	{
		TArray<FString> Words;
		TArray<int32>   Children;
	};

	struct FLipNodeArena
	{
		TArray<FLipNode> Nodes;
		TArray<int32> Roots;

		int32 Alloc() { return Nodes.Emplace(); }
	};

	// Whitespace only — **no quote grouping**, unlike the `.vcd` tokenizer this otherwise mirrors,
	// and no comment test because a `.lip` carries none.
	//
	// A word's text is always one non-whitespace run, and Faceposer writes the caption's own
	// punctuation into it: `WORD "I'll 0.130 0.190` and `WORD elevator." 0.429 1.028` are what a line
	// beginning with a quotation mark actually produces. Treating that `"` as an opening quote
	// swallows the rest of the line, and the word loses every phoneme under it — 1,159 files across
	// the corpus, one word each, silently. The only quoted content in the format is the `PHRASE`
	// line, which this reader discards.
	void TokenizeLipLine(const FString& Line, TArray<FString>& Out)
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
			const TCHAR* Start = P;
			while (*P != TEXT('\0') && !FChar::IsWhitespace(*P))
			{
				++P;
			}
			Out.Emplace(FString::ConstructFromPtrSize(Start, static_cast<int32>(P - Start)));
		}
	}

	// `ElysiumSceneData.cpp`'s BuildTree, including the `pending` rule: a line becomes a block only
	// when a `{` follows it, on the same line or the next one.
	void BuildLipTree(const TArray<FString>& Lines, FLipNodeArena& Arena)
	{
		TArray<int32> Stack;
		Stack.Add(INDEX_NONE);
		int32 Pending = INDEX_NONE;

		auto AppendTo = [&Arena](int32 Parent, int32 Child)
		{
			if (Parent == INDEX_NONE)
			{
				Arena.Roots.Add(Child);
			}
			else
			{
				Arena.Nodes[Parent].Children.Add(Child);
			}
		};

		TArray<FString> Toks;
		for (const FString& Raw : Lines)
		{
			TokenizeLipLine(Raw, Toks);
			if (Toks.Num() == 0)
			{
				continue;
			}

			int32 First = 0;
			for (; First < Toks.Num(); ++First)
			{
				if (Toks[First] == TEXT("{"))
				{
					int32 Node = Pending;
					if (Node == INDEX_NONE)
					{
						Node = Arena.Alloc();
						AppendTo(Stack.Last(), Node);
					}
					Pending = INDEX_NONE;
					Stack.Add(Node);
					continue;
				}
				if (Toks[First] == TEXT("}"))
				{
					Pending = INDEX_NONE;
					if (Stack.Num() > 1)
					{
						Stack.Pop();
					}
					continue;
				}
				break;
			}
			if (First >= Toks.Num())
			{
				continue;
			}

			Pending = INDEX_NONE;

			const bool bOpens = Toks.Last() == TEXT("{");
			const int32 Node = Arena.Alloc();
			Arena.Nodes[Node].Words.Append(Toks.GetData() + First, Toks.Num() - First - (bOpens ? 1 : 0));
			AppendTo(Stack.Last(), Node);
			if (bOpens)
			{
				Stack.Add(Node);
			}
			else
			{
				Pending = Node;
			}
		}
	}

	bool WordIsLip(const FLipNode& N, int32 Index, const TCHAR* Lit)
	{
		return N.Words.IsValidIndex(Index) && N.Words[Index].Equals(Lit, ESearchCase::IgnoreCase);
	}

	// Token-shape test before trusting a split: a row with the right field count but a word where a
	// time belongs is malformed, and the count alone will not say so.
	bool IsLipNumber(const FString& Token)
	{
		if (Token.IsEmpty())
		{
			return false;
		}
		bool bDigit = false;
		for (const TCHAR C : Token)
		{
			if (FChar::IsDigit(C))
			{
				bDigit = true;
				continue;
			}
			if (C != TEXT('+') && C != TEXT('-') && C != TEXT('.') && C != TEXT('e') && C != TEXT('E'))
			{
				return false;
			}
		}
		return bDigit;
	}

	// --- the shared parse cache -----------------------------------------------------------------
	FCriticalSection GLipCacheLock;
	TMap<FString, TSharedPtr<const FElysiumLipTrack>> GLipCache;
	int32 GLipCacheHits = 0;
	int32 GLipCacheMisses = 0;

	// One `.lip` per spoken line and a conversation replays the same lines, so the entry count is
	// bounded here rather than left to grow — same reason as the scene cache.
	constexpr int32 GLipMaxCacheEntries = 512;
}

int32 FElysiumLipTrack::NumPhonemes() const
{
	int32 Total = 0;
	for (const FElysiumLipWord& Word : Words)
	{
		Total += Word.Phonemes.Num();
	}
	return Total;
}

void FElysiumLipTrack::SampleAt(float Seconds, float BlendMin, float BlendMax,
	TArray<FElysiumLipSample>& Out) const
{
	Out.Reset();

	// The header pair is authored, so it is sorted here rather than trusted.
	const float Lo = FMath::Min(BlendMin, BlendMax);
	const float Hi = FMath::Max(BlendMin, BlendMax);

	for (const FElysiumLipWord& Word : Words)
	{
		for (const FElysiumLipPhoneme& Phoneme : Word.Phonemes)
		{
			const float Span = Phoneme.End - Phoneme.Start;
			const float S = FMath::Clamp(Span, Lo, Hi);
			// 113 of the 339 loose models carry a (0,0) filter pair, which would divide by zero here.
			// All of them have NumFlexDescs 0 and never reach this path in retail either, so this is
			// the guard retail does not have rather than a behaviour of its own.
			if (S <= 0.f)
			{
				continue;
			}
			const float A = (Phoneme.Start - Seconds) / S;
			if (A >= 1.f)
			{
				continue;   // not yet — the window opens at Start - S
			}
			const float B = (Phoneme.End - Seconds) / S;
			if (B <= 0.f)
			{
				continue;   // done — the window closes at End
			}
			const float Scale = FMath::Min(B, 1.f) - FMath::Max(A, 0.f);
			if (Scale > 0.f)
			{
				Out.Add({ &Phoneme, Scale });
			}
		}
	}
}

int32 FElysiumLipSyncBinding::Accumulate(float LineSeconds, TMap<FString, float>& InOutPose,
	TArray<FString>* OutUnresolved) const
{
	if (!IsValid())
	{
		return 0;
	}

	// Points into `Track`, which this binding holds for the duration of the call.
	TArray<FElysiumLipSample> Live;
	Track->SampleAt(LineSeconds, BlendMin, BlendMax, Live);
	if (Live.IsEmpty())
	{
		return 0;
	}

	int32 Applied = 0;
	for (const FElysiumLipSample& Sample : Live)
	{
		// By CODE, through the table's class column — what client.dll indexes with. The string is
		// only the fallback for a hand-written table with no class codes at all, and only ever
		// reached by a test fixture.
		int32 RowIndex = Table->FindRowByPhonemeCode(Sample.Phoneme->Code);
		if (RowIndex == INDEX_NONE)
		{
			RowIndex = Table->FindRow(Sample.Phoneme->Phoneme);
		}
		if (!Table->Rows.IsValidIndex(RowIndex))
		{
			if (OutUnresolved != nullptr)
			{
				OutUnresolved->AddUnique(FString::Printf(TEXT("%d/%s"),
					Sample.Phoneme->Code, *Sample.Phoneme->Phoneme));
			}
			continue;
		}
		const FElysiumExpressionRow& Row = Table->Rows[RowIndex];
		for (int32 k = 0; k < Table->Keys.Num(); ++k)
		{
			if (!Row.Values.IsValidIndex(k))
			{
				break;
			}
			// The value alone — not `Row.Weights[k]`, which retail's accumulate never reads.
			if (Row.Values[k] != 0.f)
			{
				InOutPose.FindOrAdd(Table->Keys[k]) += Sample.Scale * Row.Values[k];
			}
		}
		++Applied;
	}

	// Clamp only what this table writes, so an expression key this binding never touched keeps the
	// value the caller composed for it.
	if (Applied > 0)
	{
		for (const FString& Key : Table->Keys)
		{
			if (float* Slot = InOutPose.Find(Key))
			{
				*Slot = FMath::Clamp(*Slot, 0.f, 1.f);
			}
		}
	}
	return Applied;
}

FString ElysiumLip::NormalizeLipRel(const FString& AudioPath)
{
	// The `lip/` and `scenes/` mirrors strip the same `sound/` prefix and keep the same subtree, so
	// the fold is the scene reader's, with the extension swapped.
	const FString Rel = ElysiumScene::NormalizeSceneRel(AudioPath);
	return Rel.IsEmpty() ? Rel : FPaths::SetExtension(Rel, TEXT("lip"));
}

void ElysiumLip::ParseText(const FString& Text, const FString& SourceRel, FElysiumLipTrack& Out)
{
	Out = FElysiumLipTrack();
	Out.SourceRel = SourceRel;

	TArray<FString> Lines;
	// The corpus is CRLF throughout; ParseIntoArrayLines is the only handling that needs.
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);

	FLipNodeArena Arena;
	BuildLipTree(Lines, Arena);

	for (const int32 RootIndex : Arena.Roots)
	{
		const FLipNode& Root = Arena.Nodes[RootIndex];
		if (Root.Words.Num() == 0)
		{
			continue;
		}

		if (WordIsLip(Root, 0, TEXT("VERSION")) && Root.Words.Num() >= 2)
		{
			Out.Version = Root.Words[1];
			continue;
		}

		if (WordIsLip(Root, 0, TEXT("OPTIONS")))
		{
			for (const int32 ChildIndex : Root.Children)
			{
				const FLipNode& Option = Arena.Nodes[ChildIndex];
				if (Option.Words.Num() < 2)
				{
					continue;
				}
				if (WordIsLip(Option, 0, TEXT("voice_duck")))
				{
					Out.bVoiceDuck = FCString::Atoi(*Option.Words[1]) != 0;
				}
				else if (WordIsLip(Option, 0, TEXT("speaker_name")))
				{
					// 7,074 of 7,136 carry one, and a name may be more than one token.
					TArray<FString> Parts(Option.Words.GetData() + 1, Option.Words.Num() - 1);
					Out.SpeakerName = FString::Join(Parts, TEXT(" "));
				}
			}
			continue;
		}

		if (!WordIsLip(Root, 0, TEXT("WORDS")))
		{
			// PLAINTEXT, EMPHASIS and CLOSECAPTION are read and dropped, deliberately without a
			// warning since every file carries them. EMPHASIS is empty on all 7,136; the caption is
			// not what VtMB displays (subtitles are the `.vcd` -> `.mp3` -> `.dlg` join), and the
			// plaintext block is prose that the tokenizer cannot round-trip.
			continue;
		}

		for (const int32 WordIndex : Root.Children)
		{
			const FLipNode& WordNode = Arena.Nodes[WordIndex];
			if (!WordIsLip(WordNode, 0, TEXT("WORD")) || WordNode.Words.Num() < 4
				|| !IsLipNumber(WordNode.Words[2]) || !IsLipNumber(WordNode.Words[3]))
			{
				++Out.NumMalformedRows;
				continue;
			}

			FElysiumLipWord& Word = Out.Words.AddDefaulted_GetRef();
			Word.Text = WordNode.Words[1];
			Word.Start = FCString::Atof(*WordNode.Words[2]);
			Word.End = FMath::Max(Word.Start, FCString::Atof(*WordNode.Words[3]));

			for (const int32 RowIndex : WordNode.Children)
			{
				const FLipNode& RowNode = Arena.Nodes[RowIndex];
				// `<code> <phoneme> <start> <end> <volume>` with an optional sixth flag — version
				// 1.0 and 1.1 omit it (11,331 five-field rows corpus-wide).
				if (RowNode.Words.Num() < 5 || !IsLipNumber(RowNode.Words[0])
					|| !IsLipNumber(RowNode.Words[2]) || !IsLipNumber(RowNode.Words[3]))
				{
					++Out.NumMalformedRows;
					continue;
				}
				FElysiumLipPhoneme& Phoneme = Word.Phonemes.AddDefaulted_GetRef();
				Phoneme.Code = FCString::Atoi(*RowNode.Words[0]);
				Phoneme.Phoneme = RowNode.Words[1];
				Phoneme.Start = FCString::Atof(*RowNode.Words[2]);
				Phoneme.End = FMath::Max(Phoneme.Start, FCString::Atof(*RowNode.Words[3]));
				Out.LatestTime = FMath::Max(Out.LatestTime, Phoneme.End);
			}
			Out.LatestTime = FMath::Max(Out.LatestTime, Word.End);
		}
	}

	Out.bValid = Out.NumPhonemes() > 0;
}

TSharedPtr<const FElysiumLipTrack> ElysiumLip::Load(const FString& AudioPath)
{
	const FString Key = NormalizeLipRel(AudioPath);
	if (Key.IsEmpty())
	{
		return nullptr;
	}

	{
		FScopeLock Lock(&GLipCacheLock);
		if (const TSharedPtr<const FElysiumLipTrack>* Found = GLipCache.Find(Key))
		{
			++GLipCacheHits;
			return *Found;   // may be null — a cached negative
		}
	}

	TSharedPtr<const FElysiumLipTrack> Result;
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *FElysiumContentPaths::LipFile(Key)))
	{
		TSharedPtr<FElysiumLipTrack> Track = MakeShared<FElysiumLipTrack>();
		ParseText(Text, Key, *Track);
		if (Track->bValid)
		{
			Result = Track;
		}
	}
	// No warning on either branch. 31 of the 7,136 files have no sibling audio, a handful carry a
	// placeholder word with no phonemes, and the level scripts probe for absent `.lip` files as
	// game logic — so a miss here is ordinary and the caller counts it.

	FScopeLock Lock(&GLipCacheLock);
	++GLipCacheMisses;
	if (GLipCache.Num() >= GLipMaxCacheEntries)
	{
		GLipCache.Reset();
	}
	GLipCache.Add(Key, Result);
	return Result;
}

void ElysiumLip::RegisterInline(const FString& Key, const FString& Text)
{
	const FString Norm = NormalizeLipRel(Key);
	TSharedPtr<FElysiumLipTrack> Track = MakeShared<FElysiumLipTrack>();
	ParseText(Text, Norm, *Track);

	FScopeLock Lock(&GLipCacheLock);
	GLipCache.Add(Norm, Track->bValid ? Track : nullptr);
}

void ElysiumLip::ClearCache()
{
	FScopeLock Lock(&GLipCacheLock);
	GLipCache.Reset();
	GLipCacheHits = 0;
	GLipCacheMisses = 0;
}

void ElysiumLip::CacheStats(int32& OutEntries, int32& OutHits, int32& OutMisses)
{
	FScopeLock Lock(&GLipCacheLock);
	OutEntries = GLipCache.Num();
	OutHits = GLipCacheHits;
	OutMisses = GLipCacheMisses;
}
