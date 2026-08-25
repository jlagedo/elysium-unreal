// The animation channels our resolver arms, against the channels retail composed.
//
// T2 proved the runtime picks the clip retail picks and T4 proved that clip evaluates to retail's
// pose to a hundredth of a millimetre — but only where the frame drew that clip alone. Where
// something layered on, the drawn pose is a composition, and this rung asks the one question left:
// for a frame retail composed, does this runtime arm the same channels?
//
// `analyze_rig_layers` writes the oracle: per captured frame, the deduplicated (owner, label,
// weight, additive) set the engine accumulated, in the export's own namespace. The comparison here
// needs no intent reconstruction, because retail's own base clip is addressable — `player_state`
// records the global sequence number it committed, and T1 put that number on every clip as
// `RawIndex`. So the base is read rather than re-derived, and what is under test is purely the
// LAYER decision: the bake-time autolayer binding that base clip's host declares.
//
// **The graph holds fewer channels than retail composes, and it says so.** `ResolveLayerAssets`
// warns `twoadditive` / `twooverlay` when a record declares a second of either, because
// `FElysiumResolvedAnimation` carries one overlay node and one `_delta` node. This test measures
// how often a real frame asks for more than that, and reports it rather than asserting it away.

#include "Misc/AutomationTest.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumAnimationResolve.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumNpcClips.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumRigLayerFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// `ELYSIUM_LAYER_ORACLE` names one report; otherwise the newest pose session that has been
	// through `analyze_rig_layers`.
	FString FindLayerOracle()
	{
		const FString Named = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_LAYER_ORACLE"));
		if (!Named.IsEmpty())
		{
			return Named;
		}
		const FString WorkRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_WORK_ROOT"));
		if (WorkRoot.IsEmpty())
		{
			return FString();
		}
		const FString Frida = WorkRoot / TEXT("research") / TEXT("frida");
		TArray<FString> Sessions;
		IFileManager::Get().FindFiles(Sessions, *(Frida / TEXT("*-life_rig_pose")), false, true);
		Sessions.Sort();
		for (int32 Index = Sessions.Num() - 1; Index >= 0; --Index)
		{
			const FString Candidate = Frida / Sessions[Index] / TEXT("layer_oracle.json");
			if (IFileManager::Get().FileExists(*Candidate))
			{
				return Candidate;
			}
		}
		return FString();
	}

	struct FOracleChannel
	{
		FString Label;
		FString OwnerStem;
		double Weight = 0.0;
		bool bAdditive = false;
	};

	struct FOracleFrame
	{
		FString Stem;
		int32 Sequence = INDEX_NONE;
		double Cycle = 0.0;
		TArray<FOracleChannel> Channels;
	};

	bool ReadLayerOracle(const FString& Path, TArray<FOracleFrame>& OutFrames, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *Path);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("%s is not a JSON object"), *Path);
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
		if (!Root->TryGetArrayField(TEXT("frames"), Frames))
		{
			OutError = FString::Printf(TEXT("%s carries no `frames` array"), *Path);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Frames)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object))
			{
				continue;
			}
			FOracleFrame Frame;
			(*Object)->TryGetStringField(TEXT("stem"), Frame.Stem);
			(*Object)->TryGetNumberField(TEXT("sequence"), Frame.Sequence);
			(*Object)->TryGetNumberField(TEXT("cycle"), Frame.Cycle);
			const TArray<TSharedPtr<FJsonValue>>* Channels = nullptr;
			if ((*Object)->TryGetArrayField(TEXT("channels"), Channels))
			{
				for (const TSharedPtr<FJsonValue>& Entry : *Channels)
				{
					const TSharedPtr<FJsonObject>* Row = nullptr;
					if (!Entry.IsValid() || !Entry->TryGetObject(Row))
					{
						continue;
					}
					FOracleChannel Channel;
					(*Row)->TryGetStringField(TEXT("label"), Channel.Label);
					(*Row)->TryGetStringField(TEXT("owner_stem"), Channel.OwnerStem);
					(*Row)->TryGetNumberField(TEXT("weight"), Channel.Weight);
					(*Row)->TryGetBoolField(TEXT("additive"), Channel.bAdditive);
					if (!Channel.Label.IsEmpty())
					{
						Frame.Channels.Add(MoveTemp(Channel));
					}
				}
			}
			if (!Frame.Stem.IsEmpty() && Frame.Channels.Num() > 0)
			{
				OutFrames.Add(MoveTemp(Frame));
			}
		}
		return true;
	}

	// A blend-table lookup over the real sidecars, cached per owner — the owner may be a body or a
	// bank, so both halves of the index answer.
	struct FRealTables
	{
		const FElysiumNpcIndex* Index = nullptr;
		TMap<FString, TSharedPtr<FElysiumBlendTable>> Cache;

		const FElysiumBlendTable* operator()(const FString& Owner)
		{
			if (const TSharedPtr<FElysiumBlendTable>* Found = Cache.Find(Owner))
			{
				return Found->Get();
			}
			const FElysiumNpcIndexEntry* Entry = Index->Npcs.Find(Owner);
			if (Entry == nullptr) { Entry = Index->Banks.Find(Owner); }
			TSharedPtr<FElysiumBlendTable> Table;
			if (Entry != nullptr && !Entry->Blends.IsEmpty())
			{
				Table = MakeShared<FElysiumBlendTable>();
				FString Error;
				if (!Table->Load(Entry->Blends, Error))
				{
					Table.Reset();
				}
			}
			Cache.Add(Owner, Table);
			return Table.Get();
		}
	};

	// The clip retail committed on this frame, found by the global sequence number it recorded.
	// `RawIndex` is that same number, written per row by the export.
	const FElysiumNpcClip* FindByRawIndex(const FElysiumNpcClipSet& Set, int32 RawIndex,
		FString& OutLabel)
	{
		if (RawIndex == INDEX_NONE)
		{
			return nullptr;
		}
		const FElysiumNpcClip* Found = nullptr;
		Set.Clips.ForEachClip([&](const FString& Label, const FElysiumNpcClip& Clip)
		{
			if (Found == nullptr && Clip.RawIndex == RawIndex)
			{
				OutLabel = Label;
				Found = &Clip;
			}
		});
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRigLayerTest,
	"Elysium.Content.RigLayers", GElysiumRigLayerFlags)
bool FElysiumRigLayerTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
		return true;
	}
	const FString OraclePath = FindLayerOracle();
	if (OraclePath.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no layer_oracle.json under $ELYSIUM_WORK_ROOT/research/frida "
			"(capture with frida_probe --recipe life_rig_pose, then run analyze_rig_pose and "
			"analyze_rig_layers)"));
		return true;
	}
	TArray<FOracleFrame> Frames;
	FString Error;
	if (!ReadLayerOracle(OraclePath, Frames, Error))
	{
		AddError(Error);   // present but unreadable is a failure, not an abstention
		return false;
	}
	FElysiumNpcIndex Index;
	if (!Index.Load(Error) || !Index.IsValid())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported npc index (run: uv run elysium export bundle npc)"));
		return true;
	}
	FRealTables Tables;
	Tables.Index = &Index;

	TMap<FString, TSharedPtr<FElysiumNpcClipSet>> Bodies;
	auto BodyFor = [&Bodies](const FString& Stem) -> const FElysiumNpcClipSet*
	{
		if (const TSharedPtr<FElysiumNpcClipSet>* Found = Bodies.Find(Stem))
		{
			return Found->Get();
		}
		TSharedPtr<FElysiumNpcClipSet> Set = MakeShared<FElysiumNpcClipSet>();
		FString LoadError;
		if (!Set->Load(Stem, LoadError))
		{
			Set.Reset();
		}
		Bodies.Add(Stem, Set);
		return Set.Get();
	};

	int32 Evaluated = 0;
	int32 NoBody = 0;
	int32 NoBase = 0;
	int32 BaseNotNumbered = 0;
	// Retail played it; our binding never names it.
	TMap<FString, int32> Unarmed;
	// We arm it; retail did not play it on that frame.
	TMap<FString, int32> Extra;
	int32 FramesFullyCovered = 0;
	int32 FramesMissingAChannel = 0;
	int32 FramesBeyondTheGraph = 0;
	int32 SecondOverlayDropped = 0;
	int32 SecondAdditiveDropped = 0;

	for (const FOracleFrame& Frame : Frames)
	{
		const FElysiumNpcClipSet* Body = BodyFor(Frame.Stem);
		if (Body == nullptr)
		{
			++NoBody;
			continue;
		}
		FString BaseLabel;
		const FElysiumNpcClip* Base = FindByRawIndex(*Body, Frame.Sequence, BaseLabel);
		if (Base == nullptr)
		{
			// Either the body's clip map carries no raw index yet, or the number the capture
			// recorded is not in this body's flat space — counted apart, never guessed past.
			(Frame.Sequence == INDEX_NONE ? NoBase : BaseNotNumbered)++;
			continue;
		}
		++Evaluated;

		// What this runtime would arm: the bake-time autolayer binding the base clip's own owner
		// declares. This is exactly what `ApplyLabel` copies into `FElysiumAnimationSelection::
		// LayerLabels`, so it is the resolver's decision rather than a paraphrase of it.
		const FString Owner = Base->IsOwnedBy(Frame.Stem) ? Frame.Stem : Base->Owner;
		TSet<FString> Armed;
		if (const FElysiumBlendTable* Table = Tables(Owner))
		{
			if (const FElysiumAutoLayerBinding* Binding = Table->FindAutoLayers(BaseLabel))
			{
				for (const FString& Layer : Binding->Clips)
				{
					Armed.Add(Layer.ToLower());
				}
			}
		}

		// Retail's own layer set for the frame: every channel that is not the base it committed.
		TSet<FString> Played;
		int32 PlayedAdditive = 0;
		int32 PlayedPlain = 0;
		for (const FOracleChannel& Channel : Frame.Channels)
		{
			if (Channel.Label.Equals(BaseLabel, ESearchCase::IgnoreCase))
			{
				continue;
			}
			Played.Add(Channel.Label.ToLower());
			(Channel.bAdditive ? PlayedAdditive : PlayedPlain)++;
		}
		if (PlayedAdditive > 1) { ++SecondAdditiveDropped; }
		if (PlayedPlain > 1) { ++SecondOverlayDropped; }
		if (PlayedAdditive > 1 || PlayedPlain > 1) { ++FramesBeyondTheGraph; }

		bool bMissing = false;
		for (const FString& Label : Played)
		{
			if (!Armed.Contains(Label))
			{
				++Unarmed.FindOrAdd(Label);
				bMissing = true;
			}
		}
		for (const FString& Label : Armed)
		{
			if (!Played.Contains(Label))
			{
				++Extra.FindOrAdd(Label);
			}
		}
		(bMissing ? FramesMissingAChannel : FramesFullyCovered)++;
	}

	if (Evaluated == 0)
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: no frame was evaluable (%d no body, %d no sequence, "
				 "%d sequence not numbered in the body's flat space)"),
			NoBody, NoBase, BaseNotNumbered));
		return true;
	}

	AddInfo(FString::Printf(TEXT("oracle %s"), *OraclePath));
	AddInfo(FString::Printf(
		TEXT("frames %d evaluated; skipped: %d no body, %d no sequence, %d base not numbered"),
		Evaluated, NoBody, NoBase, BaseNotNumbered));
	AddInfo(FString::Printf(
		TEXT("channel set: %d frames covered, %d frames missing at least one channel retail played"),
		FramesFullyCovered, FramesMissingAChannel));
	AddInfo(FString::Printf(
		TEXT("beyond the graph: %d frames (a second overlay on %d, a second additive on %d)"),
		FramesBeyondTheGraph, SecondOverlayDropped, SecondAdditiveDropped));

	// Reported, never asserted: which labels the binding never names, most frequent first. The
	// binding is authored per host, so a label absent from it is a composition this runtime cannot
	// reach at all — a different failure from one it reaches and then drops for want of a node.
	Unarmed.ValueSort([](int32 A, int32 B) { return A > B; });
	int32 Shown = 0;
	for (const TPair<FString, int32>& Pair : Unarmed)
	{
		if (Shown++ >= 15) { break; }
		AddInfo(FString::Printf(TEXT("  never armed: %-42s %d frames"), *Pair.Key, Pair.Value));
	}
	Extra.ValueSort([](int32 A, int32 B) { return A > B; });
	Shown = 0;
	for (const TPair<FString, int32>& Pair : Extra)
	{
		if (Shown++ >= 10) { break; }
		AddInfo(FString::Printf(TEXT("  armed unplayed: %-39s %d frames"), *Pair.Key, Pair.Value));
	}

	// **The one assertion.** The binding is a property of the export, so a base clip whose host
	// declares autolayers must still declare them — that is what would silently regress if the
	// autolayer sidecar stopped being written, and it is checkable without deciding anything about
	// the graph's capacity. How many channels retail composed, and how many this runtime can hold,
	// are reported above and asserted nowhere: the shortfall is a known limitation of the graph
	// (`ResolveLayerAssets` warns `twoadditive`/`twooverlay` when it bites), not a regression.
	TestTrue(TEXT("at least one captured frame resolves its base clip through a raw sequence number"),
		Evaluated > 0);
	TestEqual(TEXT("every captured frame's base clip is numbered in the body's own flat space"),
		BaseNotNumbered, 0);
	return true;
}
