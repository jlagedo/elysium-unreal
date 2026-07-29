#include "Substrate/ElysiumSceneData.h"

#include "ElysiumContentPaths.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumScene, Log, All);

namespace
{
	// The parse-token table at 0x10547984, in enum order. Index == EElysiumChoreoEvent value.
	const TCHAR* GEventTokens[] = {
		TEXT("unknown"),
		TEXT("section"),
		TEXT("expression"),
		TEXT("lookat"),
		TEXT("moveto"),
		TEXT("speak"),
		TEXT("gesture"),
		TEXT("sequence"),
		TEXT("face"),
		TEXT("firetrigger"),
		TEXT("flexanimation"),
		TEXT("subscene"),
		TEXT("loop"),
		TEXT("silence"),
		TEXT("loud"),
		TEXT("python"),
		TEXT("cameramove"),
		TEXT("camerashot"),
		TEXT("camerarestore"),
		TEXT("bodysound"),
	};
	static_assert(UE_ARRAY_COUNT(GEventTokens) == static_cast<int32>(EElysiumChoreoEvent::Count),
		"event token table must match EElysiumChoreoEvent");

	// One parsed line plus whatever block followed it. The grammar is uniform, so this single node
	// shape covers actors, channels, events and every sub-block without special-casing any of them.
	struct FSceneNode
	{
		TArray<FString> Words;
		TArray<int32>   Children;   // indices into the arena
	};

	struct FSceneNodeArena
	{
		TArray<FSceneNode> Nodes;
		TArray<int32> Roots;

		int32 Alloc() { return Nodes.Emplace(); }
	};

	// One choreo line -> tokens, quotes stripped. A quoted token keeps its spaces; everything else
	// is a run of non-whitespace.
	//
	// Comments are LINE-LEADING ONLY, exactly as probe_scenes.py has it. An inline `//` must not be
	// stripped — a `param` is a file path and may legitimately contain one.
	void TokenizeLine(const FString& Line, TArray<FString>& Out)
	{
		Out.Reset();

		const TCHAR* P = *Line;
		// Leading whitespace, then the comment test.
		while (*P != TEXT('\0') && FChar::IsWhitespace(*P))
		{
			++P;
		}
		if (*P == TEXT('\0') || (P[0] == TEXT('/') && P[1] == TEXT('/')))
		{
			return;
		}

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
					++P;   // closing quote; an unterminated one just runs to end of line
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

	// `// Choreo version <n>` — matched against the raw line before comment stripping, since it IS
	// a comment. First match wins; 10 files in the corpus carry none.
	int32 ParseVersion(const TArray<FString>& Lines)
	{
		for (const FString& Raw : Lines)
		{
			FString L = Raw.TrimStartAndEnd();
			if (!L.StartsWith(TEXT("//")))
			{
				continue;
			}
			L.RightChopInline(2, EAllowShrinking::No);
			L.TrimStartInline();
			if (!L.StartsWith(TEXT("Choreo"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			L.RightChopInline(6, EAllowShrinking::No);
			L.TrimStartInline();
			if (!L.StartsWith(TEXT("version"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			L.RightChopInline(7, EAllowShrinking::No);
			L.TrimStartInline();
			if (L.Len() > 0 && FChar::IsDigit(L[0]))
			{
				return FCString::Atoi(*L);
			}
		}
		return INDEX_NONE;
	}

	// Build the node tree. This is probe_scenes.py's `parse` one-for-one, including the `pending`
	// rule: a line becomes a block only when a `{` follows it (on the same line or the next one).
	void BuildTree(const TArray<FString>& Lines, FSceneNodeArena& Arena)
	{
		TArray<int32> Stack;         // open blocks; INDEX_NONE sentinel = scene root
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
			TokenizeLine(Raw, Toks);
			if (Toks.Num() == 0)
			{
				continue;
			}

			// A line may open with a bare `{` (claiming the pending line as its block) or `}`.
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

			// A trailing `{` opens this line's block immediately.
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

	bool WordIs(const FSceneNode& N, int32 Index, const TCHAR* Lit)
	{
		return N.Words.IsValidIndex(Index) && N.Words[Index].Equals(Lit, ESearchCase::IgnoreCase);
	}

	// Strip control characters and trim — one actor name in sp_endsequences_b carries a stray one,
	// and binding has to survive it.
	FString CleanName(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (TCHAR C : In)
		{
			if (C >= TEXT(' '))
			{
				Out.AppendChar(C);
			}
		}
		return Out.TrimStartAndEnd();
	}

	void LiftEvent(const FSceneNodeArena& Arena, const FSceneNode& N, int32 ActorIndex, int32 ChannelIndex,
		FElysiumSceneData& Out)
	{
		FElysiumSceneEvent Ev;
		Ev.Type = ElysiumScene::EventTypeFromToken(N.Words.IsValidIndex(1) ? N.Words[1] : FString());
		Ev.Name = N.Words.IsValidIndex(2) ? N.Words[2] : FString();
		Ev.ActorIndex = ActorIndex;
		Ev.ChannelIndex = ChannelIndex;

		for (int32 ChildIdx : N.Children)
		{
			const FSceneNode& C = Arena.Nodes[ChildIdx];
			if (C.Words.Num() == 0)
			{
				continue;
			}
			if (WordIs(C, 0, TEXT("time")) && C.Words.Num() >= 2)
			{
				Ev.StartTime = FCString::Atof(*C.Words[1]);
				if (C.Words.Num() >= 3)
				{
					const float End = FCString::Atof(*C.Words[2]);
					// -1 is the authored "instantaneous" marker.
					Ev.bHasEnd = End >= 0.f;
					Ev.EndTime = Ev.bHasEnd ? End : Ev.StartTime;
				}
			}
			else if (WordIs(C, 0, TEXT("param")) && C.Words.Num() >= 2)
			{
				Ev.Param = C.Words[1];
			}
			else if (WordIs(C, 0, TEXT("param2")) && C.Words.Num() >= 2)
			{
				Ev.Param2 = C.Words[1];
			}
			else if (WordIs(C, 0, TEXT("fixedlength")))
			{
				Ev.bFixedLength = true;
			}
			else if (WordIs(C, 0, TEXT("sequenceduration")) && C.Words.Num() >= 2)
			{
				Ev.SequenceDuration = FCString::Atof(*C.Words[1]);
			}
			else if (WordIs(C, 0, TEXT("event_ramp")))
			{
				for (int32 SampleIdx : C.Children)
				{
					const FSceneNode& S = Arena.Nodes[SampleIdx];
					if (S.Words.Num() >= 2)
					{
						Ev.Ramp.Emplace(FCString::Atof(*S.Words[0]), FCString::Atof(*S.Words[1]));
					}
				}
				Ev.Ramp.Sort([](const FVector2f& A, const FVector2f& B) { return A.X < B.X; });
			}
			// Everything else (tags, absolutetags, relativetag, flextimingtags, flexanimations,
			// resumecondition, loopcount, yaw, targethead, mapname, ramp, …) is recognised by
			// VtMB's parser and used by nothing in the shipped corpus. Parsed into nodes, dropped
			// here — deliberately without a warning, since every file carries some.
		}

		// The corpus is dirty and a reader must tolerate it rather than assert: one event ends 86
		// seconds before it starts. Clamp and count.
		if (Ev.bHasEnd && Ev.EndTime < Ev.StartTime)
		{
			Ev.EndTime = Ev.StartTime;
			++Out.NumDegenerate;
		}

		++Out.TypeCounts[static_cast<int32>(Ev.Type)];
		Out.Events.Add(MoveTemp(Ev));
	}

	void LiftChannel(const FSceneNodeArena& Arena, const FSceneNode& N, int32 ActorIndex, FElysiumSceneData& Out)
	{
		FElysiumSceneChannel Ch;
		Ch.Name = N.Words.IsValidIndex(1) ? N.Words[1] : FString();
		const int32 ChannelIndex = Out.Channels.Add(MoveTemp(Ch));

		for (int32 ChildIdx : N.Children)
		{
			const FSceneNode& C = Arena.Nodes[ChildIdx];
			if (WordIs(C, 0, TEXT("event")))
			{
				LiftEvent(Arena, C, ActorIndex, ChannelIndex, Out);
			}
			else if (WordIs(C, 0, TEXT("active")) && C.Words.Num() >= 2)
			{
				Out.Channels[ChannelIndex].bActive = FCString::Atoi(*C.Words[1]) != 0;
			}
		}
	}

	void LiftActor(const FSceneNodeArena& Arena, const FSceneNode& N, FElysiumSceneData& Out)
	{
		FElysiumSceneActor A;
		A.Name = CleanName(N.Words.IsValidIndex(1) ? N.Words[1] : FString());
		const int32 ActorIndex = Out.Actors.Add(MoveTemp(A));

		for (int32 ChildIdx : N.Children)
		{
			const FSceneNode& C = Arena.Nodes[ChildIdx];
			if (WordIs(C, 0, TEXT("channel")))
			{
				LiftChannel(Arena, C, ActorIndex, Out);
			}
			else if (WordIs(C, 0, TEXT("event")))
			{
				LiftEvent(Arena, C, ActorIndex, INDEX_NONE, Out);   // actor-level event
			}
			else if (WordIs(C, 0, TEXT("bonerename")) && C.Words.Num() >= 3)
			{
				Out.Actors[ActorIndex].BoneFrom = C.Words[1];
				Out.Actors[ActorIndex].BoneTo = C.Words[2];
			}
			else if (WordIs(C, 0, TEXT("faceposermodel")) && C.Words.Num() >= 2)
			{
				Out.Actors[ActorIndex].FaceposerModel = C.Words[1];
			}
			else if (WordIs(C, 0, TEXT("active")) && C.Words.Num() >= 2)
			{
				Out.Actors[ActorIndex].bActive = FCString::Atoi(*C.Words[1]) != 0;
			}
		}
	}

	// --- the shared parse cache ---------------------------------------------------------------
	FCriticalSection GCacheLock;
	TMap<FString, TSharedPtr<const FElysiumSceneData>> GCache;
	int32 GCacheHits = 0;
	int32 GCacheMisses = 0;

	// 12.1 alone would not need a cache (≤13 scenes on the busiest map), but 12.2 parses one `.vcd`
	// per spoken line and a conversation replays the same lines, so the entry count is bounded here
	// rather than left to grow.
	constexpr int32 GMaxCacheEntries = 512;
}

float FElysiumSceneEvent::RampAt(float T) const
{
	if (Ramp.Num() == 0)
	{
		return 1.f;   // no authored envelope — full intensity
	}
	if (T <= Ramp[0].X)
	{
		return Ramp[0].Y;
	}
	if (T >= Ramp.Last().X)
	{
		return Ramp.Last().Y;
	}
	for (int32 i = 1; i < Ramp.Num(); ++i)
	{
		if (T <= Ramp[i].X)
		{
			const float Span = Ramp[i].X - Ramp[i - 1].X;
			if (Span <= UE_SMALL_NUMBER)
			{
				return Ramp[i].Y;
			}
			const float Alpha = (T - Ramp[i - 1].X) / Span;
			return FMath::Lerp(Ramp[i - 1].Y, Ramp[i].Y, Alpha);
		}
	}
	return Ramp.Last().Y;
}

const TCHAR* ElysiumScene::EventTypeName(EElysiumChoreoEvent Type)
{
	const int32 Index = static_cast<int32>(Type);
	return GEventTokens[(Index >= 0 && Index < UE_ARRAY_COUNT(GEventTokens)) ? Index : 0];
}

EElysiumChoreoEvent ElysiumScene::EventTypeFromToken(const FString& Token)
{
	for (int32 i = 1; i < UE_ARRAY_COUNT(GEventTokens); ++i)
	{
		if (Token.Equals(GEventTokens[i], ESearchCase::IgnoreCase))
		{
			return static_cast<EElysiumChoreoEvent>(i);
		}
	}
	return EElysiumChoreoEvent::Unknown;
}

FString ElysiumScene::NormalizeSceneRel(const FString& SceneFile)
{
	FString Rel = SceneFile;
	Rel.ReplaceInline(TEXT("\\"), TEXT("/"));
	Rel.TrimStartAndEndInline();
	while (Rel.StartsWith(TEXT("/")))
	{
		Rel.RightChopInline(1, EAllowShrinking::No);
	}
	if (Rel.StartsWith(TEXT("sound/"), ESearchCase::IgnoreCase))
	{
		Rel.RightChopInline(6, EAllowShrinking::No);
	}
	return Rel.ToLower();
}

void ElysiumScene::ParseText(const FString& Text, const FString& SourceRel, FElysiumSceneData& Out)
{
	Out = FElysiumSceneData();
	Out.SourceRel = SourceRel;

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);
	Out.Version = ParseVersion(Lines);

	FSceneNodeArena Arena;
	BuildTree(Lines, Arena);

	for (int32 RootIdx : Arena.Roots)
	{
		const FSceneNode& N = Arena.Nodes[RootIdx];
		if (N.Words.Num() == 0)
		{
			continue;
		}
		if (WordIs(N, 0, TEXT("actor")))
		{
			LiftActor(Arena, N, Out);
		}
		else if (WordIs(N, 0, TEXT("event")))
		{
			LiftEvent(Arena, N, INDEX_NONE, INDEX_NONE, Out);   // scene-level event
		}
		else if (WordIs(N, 0, TEXT("fps")) && N.Words.Num() >= 2)
		{
			Out.Fps = FCString::Atof(*N.Words[1]);
		}
		else if (WordIs(N, 0, TEXT("snap")) && N.Words.Num() >= 2)
		{
			Out.bSnap = N.Words[1].Equals(TEXT("on"), ESearchCase::IgnoreCase);
		}
	}

	// Resolve actor/channel activity after the whole tree has been lifted: Faceposer may place the
	// `active` token after the event blocks it governs. Disabled blocks remain in Events for corpus
	// inspection, but the player excludes them from timing and dispatch.
	for (FElysiumSceneEvent& Ev : Out.Events)
	{
		const bool bActorActive = Ev.ActorIndex == INDEX_NONE
			|| (Out.Actors.IsValidIndex(Ev.ActorIndex) && Out.Actors[Ev.ActorIndex].bActive);
		const bool bChannelActive = Ev.ChannelIndex == INDEX_NONE
			|| (Out.Channels.IsValidIndex(Ev.ChannelIndex) && Out.Channels[Ev.ChannelIndex].bActive);
		Ev.bActive = bActorActive && bChannelActive;
	}

	// Dispatch order is authored start time; a stable sort keeps same-time events in file order,
	// which is what decides e.g. which of two triggers on one frame fires first.
	Out.Events.StableSort([](const FElysiumSceneEvent& A, const FElysiumSceneEvent& B)
		{ return A.StartTime < B.StartTime; });

	for (const FElysiumSceneEvent& Ev : Out.Events)
	{
		if (Ev.bActive)
		{
			Out.LatestTime = FMath::Max(Out.LatestTime, Ev.bHasEnd ? Ev.EndTime : Ev.StartTime);
		}
	}

	// A scene with no actor names nothing and can bind nothing. That is the one shape this reader
	// calls invalid — it matches VtMB gating `Start` on a parsed scene pointer.
	Out.bValid = Out.Actors.Num() > 0;
}

TSharedPtr<const FElysiumSceneData> ElysiumScene::Load(const FString& SceneFile)
{
	const FString Key = NormalizeSceneRel(SceneFile);
	if (Key.IsEmpty())
	{
		return nullptr;
	}

	{
		FScopeLock Lock(&GCacheLock);
		if (const TSharedPtr<const FElysiumSceneData>* Found = GCache.Find(Key))
		{
			++GCacheHits;
			return *Found;   // may be null — a cached negative, so a broken path costs one stat
		}
	}

	TSharedPtr<const FElysiumSceneData> Result;
	const FString Full = FElysiumContentPaths::SceneFile(Key);
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *Full))
	{
		TSharedPtr<FElysiumSceneData> Data = MakeShared<FElysiumSceneData>();
		ParseText(Text, Key, *Data);
		if (Data->bValid)
		{
			Result = Data;
		}
		else
		{
			UE_LOG(LogElysiumScene, Warning, TEXT("scene '%s' parsed but names no actor"), *Key);
		}
	}
	else
	{
		UE_LOG(LogElysiumScene, Warning, TEXT("scene '%s' not found under %s"),
			*Key, *FElysiumContentPaths::ScenesDir());
	}

	FScopeLock Lock(&GCacheLock);
	++GCacheMisses;
	if (GCache.Num() >= GMaxCacheEntries)
	{
		GCache.Reset();   // the 12.2 per-line path is unbounded; drop the lot rather than grow
	}
	GCache.Add(Key, Result);
	return Result;
}

void ElysiumScene::RegisterInline(const FString& Key, const FString& Text)
{
	const FString Norm = NormalizeSceneRel(Key);
	TSharedPtr<FElysiumSceneData> Data = MakeShared<FElysiumSceneData>();
	ParseText(Text, Norm, *Data);

	FScopeLock Lock(&GCacheLock);
	GCache.Add(Norm, Data->bValid ? Data : nullptr);
}

void ElysiumScene::ClearCache()
{
	FScopeLock Lock(&GCacheLock);
	GCache.Reset();
	GCacheHits = 0;
	GCacheMisses = 0;
}

void ElysiumScene::CacheStats(int32& OutEntries, int32& OutHits, int32& OutMisses)
{
	FScopeLock Lock(&GCacheLock);
	OutEntries = GCache.Num();
	OutHits = GCacheHits;
	OutMisses = GCacheMisses;
}
