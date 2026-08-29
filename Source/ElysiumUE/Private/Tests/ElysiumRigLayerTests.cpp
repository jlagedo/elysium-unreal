// The animation channels this runtime can compose, against the channels retail composed.
//
// T2 proved the runtime picks the clip retail picks and T4 proved that clip evaluates to retail's
// pose to a hundredth of a millimetre — but only where the frame drew that clip alone. Where
// something layered on, the drawn pose is a composition, and this rung asks the one question left:
// for a frame retail composed, can this runtime reach the same channel set?
//
// `analyze_rig_layers` writes the oracle: per captured frame, the deduplicated (owner, label,
// weight, additive) set the engine accumulated, in the export's own namespace. The comparison here
// needs no intent reconstruction, because retail's own base clip is addressable — `player_state`
// records the global sequence number it committed, and T1 put that number on every clip as
// `RawIndex`. So the base is read rather than re-derived.
//
// **The model under test is the whole composition, not one channel of it**:
//
//  * the base clip's own autolayer closure, walked TRANSITIVELY — retail's resolution walk recurses
//    once per `numautolayers` entry at a hardcoded weight of 1.0, so a host brings its layers'
//    layers too;
//  * plus, per overlay slot, one PUSHED layer and that layer's own closure by the same rule. Retail
//    carries four slots (`FElysiumOverlayStack`), and `<weapon>_attack_layer` declaring
//    `<weapon>_aim_layer` and `<weapon>_attack_delta` is exactly why one push reaches three
//    channels.
//
// **What may be pushed is a closed set, computed rather than guessed.** The overlay producers name
// three layer activities (`FElysiumWeapon::BeginRangedShot` and its reload arm), each translated
// through the committed weapon ladders against the body's own vocabulary — so the set of labels this
// runtime can ever compose as a layer on a given body is a pure function of the export plus
// `ElysiumActionTables`, with no join against the capture. Anything outside it is genuinely
// unreachable, which is what makes the coverage assertion mean something.
//
// **A played channel that is a full-body base pose is not a layer.** It is the outgoing clip of a
// blend-stack cross-fade, which Unreal composes as a transition rather than as a channel; retail
// records it as a contribution because it fades sequences the same way. Excluded by the criterion —
// the body's vocabulary carries it, it is neither additive nor named as anyone's autolayer, and no
// overlay producer can reach it — never by name, and counted so the exclusion is visible.

#include "Misc/AutomationTest.h"

#include "ElysiumContentPaths.h"
#include "ElysiumOverlayStack.h"
#include "Visual/ElysiumActionTables.h"
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
	//
	// Named apart from `FElysiumRigOracleTests`' own cache of the same shape: two anonymous-namespace
	// structs sharing a name compile alone and collide the moment a unity build puts them in one
	// translation unit, which is a link-order accident rather than a property of either file.
	struct FLayerBlendTables
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
	const FElysiumNpcClip* FindLayerClipByRawIndex(const FElysiumNpcClipSet& Set, int32 RawIndex,
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

	// Everything one body's composition can reach, read once per stem.
	struct FBodyComposition
	{
		// Every label the owner's autolayer table names as a TARGET — the labels something in the
		// shipped data composes as a layer rather than plays as a pose.
		TSet<FString> AutoLayerTargets;
		// Every label an overlay producer can push on this body, closed over the committed weapon
		// ladders. See the file header: this is what makes the coverage assertion falsifiable.
		TSet<FString> Pushable;
	};

	// The three layer activities the overlay producers name. `FElysiumWeapon::BeginRangedShot` arms
	// the first for both bodies and the player's reload arm the third; the dry-fire layer rides the
	// same seam. A base activity, never a per-weapon rename — the ladders below do the renaming, the
	// same walk `ElysiumAnimResolve::TranslateActivity` performs at play time.
	const TCHAR* const GOverlayActivities[] = {
		TEXT("ACT_RANGE_ATTACK1_LAYER"),
		TEXT("ACT_DRYFIRE_LAYER"),
		TEXT("ACT_RELOAD_LAYER"),
	};

	void CollectComposition(const FElysiumNpcClipSet& Body, FLayerBlendTables& Tables,
		FBodyComposition& Out)
	{
		// The autolayer targets, over every owner this body's vocabulary reaches — a bank names its
		// own layers, and a body resolves labels out of several banks.
		TSet<FString> Owners;
		Body.Clips.ForEachClip([&](const FString&, const FElysiumNpcClip& Clip)
		{
			Owners.Add(Clip.Owner);
		});
		for (const FString& Owner : Owners)
		{
			if (const FElysiumBlendTable* Table = Tables(Owner))
			{
				for (const TPair<FString, FElysiumAutoLayerBinding>& Pair : Table->AutoLayers)
				{
					for (const FString& Target : Pair.Value.Clips)
					{
						Out.AutoLayerTargets.Add(Target.ToLower());
					}
				}
			}
		}

		// What a producer can push: each layer activity, translated through every committed weapon
		// ladder against THIS body's vocabulary, then every candidate that activity answers with.
		// Every candidate rather than the pick, because retail's draw is weighted and can answer any
		// of them — the arm is not deterministic, and a coverage claim must not depend on which one
		// this run happens to roll.
		auto HasActivity = [&Body](const FString& Activity) { return Body.HasActivity(Activity); };
		auto AddCandidates = [&Body, &Out](const FString& Activity)
		{
			for (const FElysiumClipRef& Ref : Body.ByActivity(Activity))
			{
				Out.Pushable.Add(Ref.Label.ToLower());
			}
		};
		for (const TCHAR* Base : GOverlayActivities)
		{
			// The untranslated base too: an unarmed body walks retail's own empty table.
			AddCandidates(FString(Base));
			for (const ElysiumActionTables::FWeaponLadder& Ladder : ElysiumActionTables::WeaponLadders())
			{
				const ElysiumActionTables::FTranslation Translated =
					ElysiumActionTables::Translate(Ladder, FString(Base), HasActivity);
				AddCandidates(Translated.Activity);
			}
		}
	}

	// One label's transitive autolayer closure, by retail's own rule: a sequence composes with the
	// entries its `numautolayers` names, and each of those brings its own. Bounded by the visited set
	// rather than by a depth, because the shipped data contains cycles — `<weapon>_attack_layer`
	// names `<weapon>_aim_layer`, whose own closure points back.
	void Closure(const FElysiumNpcClipSet& Body, FLayerBlendTables& Tables, const FString& Owner,
		const FString& Label, TSet<FString>& InOut)
	{
		TArray<TPair<FString, FString>> Pending;
		Pending.Emplace(Owner, Label);
		TSet<FString> Visited;
		while (!Pending.IsEmpty())
		{
			const TPair<FString, FString> Node = Pending.Pop();
			bool bSeen = false;
			Visited.Add(Node.Value.ToLower(), &bSeen);
			if (bSeen)
			{
				continue;
			}
			const FElysiumBlendTable* Table = Tables(Node.Key);
			const FElysiumAutoLayerBinding* Binding =
				Table != nullptr ? Table->FindAutoLayers(Node.Value) : nullptr;
			if (Binding == nullptr)
			{
				continue;
			}
			for (const FString& Target : Binding->Clips)
			{
				InOut.Add(Target.ToLower());
				// The target's own owner, which is the include DAG's answer and is never re-derived.
				// A target the vocabulary does not carry keeps the owner it was declared under, which
				// is the only owner there is to walk from.
				const FElysiumNpcClip* Clip = Body.Find(Target);
				Pending.Emplace(Clip != nullptr ? Clip->Owner : Node.Key, Target);
			}
		}
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
	FLayerBlendTables Tables;
	Tables.Index = &Index;

	TMap<FString, TSharedPtr<FElysiumNpcClipSet>> Bodies;
	TMap<FString, TSharedPtr<FBodyComposition>> Compositions;
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
	auto CompositionFor = [&Compositions, &Tables](const FString& Stem,
		const FElysiumNpcClipSet& Body) -> const FBodyComposition&
	{
		if (const TSharedPtr<FBodyComposition>* Found = Compositions.Find(Stem))
		{
			return **Found;
		}
		TSharedPtr<FBodyComposition> Built = MakeShared<FBodyComposition>();
		CollectComposition(Body, Tables, *Built);
		Compositions.Add(Stem, Built);
		return *Built;
	};

	int32 Evaluated = 0;
	int32 NoBody = 0;
	int32 NoBase = 0;
	int32 BaseNotNumbered = 0;
	// Retail played it and no composition this runtime can reach names it.
	TMap<FString, int32> Unreachable;
	// A played channel that is a full-body base pose: the outgoing half of a blend-stack cross-fade.
	TMap<FString, int32> CrossFades;
	int32 FramesWithACrossFade = 0;
	// A played channel the base channel carries, brought by a clip other than the committed one —
	// the outgoing pose's own autolayer closure, still fading.
	TMap<FString, int32> TransitionCarried;
	int32 FramesWithATransitionChannel = 0;
	int32 FramesCovered = 0;
	int32 FramesShort = 0;
	// How many overlay slots each frame's composition needed, indexed by count.
	int32 SlotsNeeded[ElysiumOverlay::NumSlots + 2] = {};
	int32 FramesBeyondTheStack = 0;

	for (const FOracleFrame& Frame : Frames)
	{
		const FElysiumNpcClipSet* Body = BodyFor(Frame.Stem);
		if (Body == nullptr)
		{
			++NoBody;
			continue;
		}
		FString BaseLabel;
		const FElysiumNpcClip* Base = FindLayerClipByRawIndex(*Body, Frame.Sequence, BaseLabel);
		if (Base == nullptr)
		{
			// Either the body's clip map carries no raw index yet, or the number the capture
			// recorded is not in this body's flat space — counted apart, never guessed past.
			(Frame.Sequence == INDEX_NONE ? NoBase : BaseNotNumbered)++;
			continue;
		}
		++Evaluated;
		const FBodyComposition& Composition = CompositionFor(Frame.Stem, *Body);

		// What the base clip alone brings: its own transitive autolayer closure, which is retail's
		// resolution walk rather than one hop of it.
		const FString BaseOwner = Base->IsOwnedBy(Frame.Stem) ? Frame.Stem : Base->Owner;
		TSet<FString> Armed;
		Closure(*Body, Tables, BaseOwner, BaseLabel, Armed);

		// Retail's own layer set for the frame: every channel that is not the base it committed.
		TSet<FString> Remaining;
		bool bCrossFaded = false;
		bool bCarriedByATransition = false;
		for (const FOracleChannel& Channel : Frame.Channels)
		{
			const FString Lowered = Channel.Label.ToLower();
			if (Channel.Label.Equals(BaseLabel, ESearchCase::IgnoreCase) || Armed.Contains(Lowered))
			{
				continue;
			}
			// The cross-fade exclusion, by criterion rather than by name: a channel the vocabulary
			// carries that is neither additive nor anyone's declared layer, and that no overlay
			// producer can push, is a full-body pose. Composed as an overlay it would own the whole
			// rig, which is exactly what this runtime refuses; what it really is is the outgoing clip
			// of a blend-stack transition, which Unreal composes as a fade rather than a channel.
			const bool bLayerish = Channel.bAdditive
				|| Composition.AutoLayerTargets.Contains(Lowered)
				|| Composition.Pushable.Contains(Lowered);
			if (!bLayerish)
			{
				++CrossFades.FindOrAdd(Channel.Label);
				bCrossFaded = true;
				continue;
			}
			Remaining.Add(Lowered);
		}
		// The minimum number of overlay slots that reaches what is left — each push contributing its
		// own closure, exactly as retail's accumulate does. Greedy over the pushable candidates,
		// taking the one that covers most, which is exact here because the closures the shipped data
		// declares are nested rather than overlapping.
		TSet<FString> Covered;
		int32 Slots = 0;
		bool bProgress = true;
		while (!Remaining.Difference(Covered).IsEmpty() && bProgress)
		{
			bProgress = false;
			FString BestLabel;
			TSet<FString> BestReach;
			for (const FString& Candidate : Remaining.Difference(Covered))
			{
				if (!Composition.Pushable.Contains(Candidate))
				{
					continue;
				}
				TSet<FString> Reach;
				Reach.Add(Candidate);
				const FElysiumNpcClip* Clip = Body->Find(Candidate);
				Closure(*Body, Tables, Clip != nullptr ? Clip->Owner : Frame.Stem, Candidate, Reach);
				if (BestLabel.IsEmpty() || Reach.Intersect(Remaining).Num()
					> BestReach.Intersect(Remaining).Num())
				{
					BestLabel = Candidate;
					BestReach = MoveTemp(Reach);
				}
			}
			if (BestLabel.IsEmpty())
			{
				break;
			}
			Covered.Append(BestReach);
			++Slots;
			bProgress = true;
		}

		// What the stack could not reach, classified only NOW — after it had its chance, so a channel
		// a push brings through its own closure is never mistaken for something else.
		//
		// **A channel the shipped data declares as somebody's autolayer is reachable through the BASE
		// channel**, carried by a clip other than the committed one: Unreal's blend stack fades a
		// whole outgoing pose, and a pose brings its own closure, exactly as retail's does. So a
		// `_bobble_delta` recorded beside a `_ready` base is the previous gait still fading rather
		// than a layer this runtime cannot compose. Counted apart, because whether those channels
		// fade the same way is `Elysium.Content.RigPose`'s question and not this one.
		TSet<FString> Missed;
		for (const FString& Label : Remaining.Difference(Covered))
		{
			if (Composition.AutoLayerTargets.Contains(Label)
				&& !Composition.Pushable.Contains(Label))
			{
				++TransitionCarried.FindOrAdd(Label);
				bCarriedByATransition = true;
				continue;
			}
			Missed.Add(Label);
			++Unreachable.FindOrAdd(Label);
		}
		(Missed.IsEmpty() ? FramesCovered : FramesShort)++;
		if (bCarriedByATransition)
		{
			++FramesWithATransitionChannel;
		}
		if (bCrossFaded)
		{
			// Per frame as well as per channel, because the two figures answer different questions:
			// how much of the corpus a transition touched, and which clips did it.
			++FramesWithACrossFade;
		}
		SlotsNeeded[FMath::Min(Slots, ElysiumOverlay::NumSlots + 1)]++;
		if (Slots > ElysiumOverlay::NumSlots)
		{
			++FramesBeyondTheStack;
		}
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
		TEXT("channel set: %d frames covered, %d frames short at least one channel"),
		FramesCovered, FramesShort));
	AddInfo(FString::Printf(
		TEXT("blend-stack cross-fades excluded: %d distinct clips over %d frames"),
		CrossFades.Num(), FramesWithACrossFade));
	AddInfo(FString::Printf(
		TEXT("carried by a base transition rather than by a layer: %d distinct clips over %d frames"),
		TransitionCarried.Num(), FramesWithATransitionChannel));
	TransitionCarried.ValueSort([](int32 A, int32 B) { return A > B; });
	int32 TransitionsShown = 0;
	for (const TPair<FString, int32>& Pair : TransitionCarried)
	{
		if (TransitionsShown++ >= 10) { break; }
		AddInfo(FString::Printf(TEXT("  by a transition: %-40s %d frames"), *Pair.Key, Pair.Value));
	}
	// The histogram, printed rather than asserted per bucket: what a regression moves is its SHAPE,
	// and a literal per-bucket count would fire on every legitimate re-capture.
	FString Histogram;
	for (int32 Count = 0; Count <= ElysiumOverlay::NumSlots + 1; ++Count)
	{
		Histogram += FString::Printf(TEXT("%s%d:%d"),
			Count > 0 ? TEXT("  ") : TEXT(""),
			Count, SlotsNeeded[Count]);
	}
	AddInfo(FString::Printf(TEXT("overlay slots needed per frame — %s (%d slots exist)"),
		*Histogram, ElysiumOverlay::NumSlots));

	Unreachable.ValueSort([](int32 A, int32 B) { return A > B; });
	int32 Shown = 0;
	for (const TPair<FString, int32>& Pair : Unreachable)
	{
		if (Shown++ >= 15) { break; }
		AddError(FString::Printf(TEXT("  unreachable: %-42s %d frames"), *Pair.Key, Pair.Value));
	}

	// **The acceptance.** Every channel retail composed is reachable by this runtime's own
	// composition — the base clip's closure plus what its overlay producers can push — and no frame
	// asks for more slots than retail's stack has. Both are properties of the export and the
	// committed tables, so either can regress without a live run and neither can be repaired by
	// editing a number here.
	TestTrue(TEXT("at least one captured frame resolves its base clip through a raw sequence number"),
		Evaluated > 0);
	TestEqual(TEXT("every captured frame's base clip is numbered in the body's own flat space"),
		BaseNotNumbered, 0);
	TestEqual(TEXT("every captured frame's channel set is reachable by this runtime's composition"),
		FramesShort, 0);
	TestEqual(TEXT("...and no frame needs more overlay slots than the stack carries"),
		FramesBeyondTheStack, 0);
	return true;
}
