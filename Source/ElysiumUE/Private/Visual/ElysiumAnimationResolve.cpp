#include "Visual/ElysiumAnimationResolve.h"

#include "Visual/ElysiumAnimGraph.h"

namespace ElysiumAnimResolve
{

namespace
{
	const TCHAR* const GDispositionActivity = TEXT("ACT_DISPOSITION");
	const TCHAR* const GRunActivity = TEXT("ACT_RUN");
	const TCHAR* const GWalkActivity = TEXT("ACT_WALK");

	// Fill the asset half of the record from a resolved label. The owner is the include DAG's answer
	// and is never re-derived: `Clip->Owner` names the bank offline, and a resolver keyed on the label
	// alone hands the player the cast's gait.
	void ApplyLabel(const FString& Label, const FElysiumAnimationIntent& Intent,
		const FElysiumAnimationCatalog& Catalog, FElysiumAnimationSelection& Out)
	{
		const FElysiumNpcClip* Clip = Catalog.Clips->Find(Label);
		check(Clip != nullptr);

		Out.SequenceLabel = Label;
		Out.TargetSequence = Label;
		Out.Weight = Clip->Weight;
		Out.bAdditive = Clip->IsAdditive();
		Out.bLooping = (Clip->Flags & 1) != 0;
		Out.bSnap = Clip->IsSnap();
		Out.FadeSeconds = Clip->FadeSeconds();
		Out.OwnerStem = Clip->IsOwnedBy(Intent.Stem) ? Intent.Stem : Clip->Owner;

		// An additive stores the difference from a base pose, so standing one as a base folds the
		// skeleton up instead of animating it. The engine composes these and never selects one.
		if (Out.bAdditive && Intent.Channel == EElysiumAnimChannel::Base)
		{
			Out.bMasked = true;
			Out.AssetKind = EElysiumAnimAssetKind::None;
			Out.Outcome = EElysiumAnimOutcome::MaskedRejected;
			Out.Detail = FString::Printf(
				TEXT("'%s' is an additive layer (flags 0x%x) and cannot own the base channel"),
				*Label, Clip->Flags);
			return;
		}

		const FElysiumBlendTable* Table = Catalog.BlendTableFor
			? Catalog.BlendTableFor(Out.OwnerStem) : nullptr;
		const FElysiumBlendGrid* Grid = Table != nullptr ? Table->Find(Label) : nullptr;

		if (Table != nullptr)
		{
			// **In declaration order**, never sorted or deduped: an overlay declared after an additive
			// overwrites the bones it owns, so the order is what keeps both contributions.
			if (const FElysiumAutoLayerBinding* Layers = Table->FindAutoLayers(Label))
			{
				Out.LayerLabels = Layers->Clips;
			}
		}

		if (Grid == nullptr)
		{
			// The ordinary case: the label names one animation.
			Out.AssetKind = EElysiumAnimAssetKind::Sequence;
			Out.AnimationName = Label;
			Out.Outcome = EElysiumAnimOutcome::Resolved;
			return;
		}

		// A grid, resolved at the body's own pose parameters. The cell is named even for a blend space,
		// because a record that says only "a blend space" cannot be checked against a capture.
		const FElysiumPoseParams Pose = PoseFrom(Intent);
		const FElysiumBlendPick Pick = ElysiumBlendGrids::SelectCell(*Grid, *Table, Pose);
		if (Pick.Cell != nullptr && !Pick.Cell->Clip.IsEmpty())
		{
			Out.AnimationName = Pick.Cell->Clip;
			if (Pick.Cell->Motion.IsUsable())
			{
				Out.GroundSpeedCmPerSecond = Pick.Cell->Motion.GroundSpeedCmPerSecond;
			}
		}
		else
		{
			// Every shipped grid has at least two live cells, so this is a damaged sidecar. Naming the
			// label is what the runtime did before grids were read at all — worse, never silent.
			Out.AnimationName = Label;
		}

		Out.AssetKind = Grid->IsMultiCell()
			? EElysiumAnimAssetKind::BlendSpace : EElysiumAnimAssetKind::Sequence;
		Out.Axes = (Grid->GroupSize[1] > 1 && Grid->ParamIndex[1] != INDEX_NONE) ? 2 : 1;
		for (int32 Axis = 0; Axis < Out.Axes; ++Axis)
		{
			const FElysiumPoseParamDesc* Desc = Table->Param(Grid->ParamIndex[Axis]);
			Out.AxisName[Axis] = Desc != nullptr ? Desc->Name : FString::Printf(TEXT("axis%d"), Axis);
			Out.AxisValue[Axis] = Pose.Get(Out.AxisName[Axis]);
		}
		Out.Outcome = EElysiumAnimOutcome::Resolved;
		// A grid whose activity routes to a sequence-only state would play a null sequence — full
		// body reference pose — and the blend-space pointer would defeat `ShouldHoldPose`. Named
		// here, before any asset is loaded.
		ElysiumAnimGraph::RefuseUnplayableGrid(Out);
	}

	// Step 4's weighted choice for one activity, or empty.
	FString TryActivity(const FElysiumAnimationCatalog& Catalog, const FString& Activity,
		int32 Variant, int32& OutCandidates)
	{
		OutCandidates = 0;
		if (Activity.IsEmpty())
		{
			return FString();
		}
		OutCandidates = Catalog.Clips->ByActivity(Activity).Num();
		return OutCandidates > 0 ? PickWeighted(*Catalog.Clips, Activity, Variant) : FString();
	}

	void ResolveActivityRoute(const FElysiumAnimationIntent& Intent,
		const FElysiumAnimationCatalog& Catalog, FElysiumAnimationSelection& Out)
	{
		// The logical request stays un-translated. Retail's `m_Activity` is this: translation changes
		// the sequence set that realizes a request, not the AI-visible state.
		Out.RequestedActivity = Intent.Activity;

		const ElysiumAnimIntent::FElysiumTranslationResult Translation =
			ElysiumAnimIntent::TranslateActivity(Intent.Activity, Intent.WeaponTag, Intent.FormTag);
		Out.FirstWeaponActivity = Translation.FirstWeaponActivity;
		Out.WeaponActivity = Translation.WeaponActivity;
		Out.TranslationIterations = Translation.Iterations;
		Out.ResolvedActivity = Translation.Resolved;

		int32 Candidates = 0;
		FString Label = TryActivity(Catalog, Out.ResolvedActivity, Intent.Variant, Candidates);

		// An override that resolves nothing falls back to the activity that came in.
		//
		// **The authored `required` bit does not gate this**, and giving it a gate would be giving it
		// behaviour retail does not have: `CBaseCombatWeapon::ActivityOverride` never reads the third
		// dword, so the 201 flagged rows and the 9,013 optional ones take the same availability path
		// (`docs/vtmb/animation_and_movers.md` A.3). The bit rides on the record as provenance —
		// `activitydump` prints it — and nothing branches on it.
		if (Label.IsEmpty() && Translation.Iterations > 0)
		{
			int32 Fallback = 0;
			Label = TryActivity(Catalog, Translation.Incoming, Intent.Variant, Fallback);
			if (!Label.IsEmpty())
			{
				Out.ResolvedActivity = Translation.Incoming;
				Out.Candidates = Fallback;
				ApplyLabel(Label, Intent, Catalog, Out);
				if (Out.Outcome == EElysiumAnimOutcome::Resolved)
				{
					Out.Outcome = EElysiumAnimOutcome::TranslatedFallback;
					Out.Detail = FString::Printf(
						TEXT("'%s' has no sequence on '%s'; the override fell back to '%s'"),
						*Translation.Resolved, *Intent.Stem, *Translation.Incoming);
				}
				return;
			}
		}

		// **The recovered fallback ladder is the cast's, not the player's.** `CAI_BaseNPC` retries a
		// missing run as a walk, then the whole request as a disposition, then sequence zero. The
		// player's chain has no such ladder — the controlled corpus records a ducked ACT_LAND_CROUCH
		// request simply returning -1 — so a player miss is reported as the named miss it is.
		if (Label.IsEmpty() && Intent.Source == EElysiumAnimSource::Npc
			&& Intent.bAllowFallbackLadder)
		{
			if (Out.ResolvedActivity.Equals(GRunActivity, ESearchCase::IgnoreCase))
			{
				Label = TryActivity(Catalog, GWalkActivity, Intent.Variant, Candidates);
				if (!Label.IsEmpty())
				{
					Out.ResolvedActivity = GWalkActivity;
					Out.Candidates = Candidates;
					ApplyLabel(Label, Intent, Catalog, Out);
					if (Out.Outcome == EElysiumAnimOutcome::Resolved)
					{
						Out.Outcome = EElysiumAnimOutcome::RunToWalk;
						Out.Detail = TEXT("no ACT_RUN sequence; retried weighted ACT_WALK");
					}
					return;
				}
			}
			if (Label.IsEmpty())
			{
				Label = TryActivity(Catalog, GDispositionActivity, Intent.Variant, Candidates);
				if (!Label.IsEmpty())
				{
					Out.ResolvedActivity = GDispositionActivity;
					Out.Candidates = Candidates;
					ApplyLabel(Label, Intent, Catalog, Out);
					if (Out.Outcome == EElysiumAnimOutcome::Resolved)
					{
						Out.Outcome = EElysiumAnimOutcome::Disposition;
						Out.Detail = FString::Printf(
							TEXT("'%s' resolved nothing on '%s'; retried as ACT_DISPOSITION"),
							*Translation.Resolved, *Intent.Stem);
					}
					return;
				}
			}
			// The hard fallback. It cannot be named: exact identity is (owner, raw index) and the
			// character export writes no raw index, so what the record can say is that retail would
			// stand on sequence zero here and that we cannot name which clip that is.
			Out.AssetKind = EElysiumAnimAssetKind::None;
			Out.RawSequenceIndex = 0;
			Out.Outcome = EElysiumAnimOutcome::SequenceZero;
			Out.Detail = FString::Printf(
				TEXT("'%s' reached the sequence-zero fallback on '%s'; the export carries no raw ")
				TEXT("sequence index, so the clip cannot be named"),
				*Translation.Resolved, *Intent.Stem);
			return;
		}

		if (Label.IsEmpty())
		{
			// A miss is a miss whatever the row's authored bit said, because the translator does not
			// read it. What the record names is the activity and the body, which is what a fallback
			// has to be declared against.
			Out.AssetKind = EElysiumAnimAssetKind::None;
			Out.Outcome = EElysiumAnimOutcome::MissingSequence;
			Out.Detail = FString::Printf(TEXT("'%s' has no sequence on '%s'"),
				*Translation.Resolved, *Intent.Stem);
			return;
		}

		Out.Candidates = Candidates;
		ApplyLabel(Label, Intent, Catalog, Out);
	}

	void ResolveExactLabelRoute(const FElysiumAnimationIntent& Intent,
		const FElysiumAnimationCatalog& Catalog, FElysiumAnimationSelection& Out)
	{
		// A direct-sequence request bypasses activity choice and translation entirely, so both stay
		// empty on the record rather than being back-filled with something that never ran.
		if (Catalog.Clips->Find(Intent.SequenceLabel) != nullptr)
		{
			Out.Candidates = 1;
			ApplyLabel(Intent.SequenceLabel, Intent, Catalog, Out);
			return;
		}

		// The scripted-label miss, exactly as retail takes it: `LookupSequence` returns -1, the start
		// helper warns, sets sequence 0, zeroes the cycle and resets sequence info. An ACT_* token
		// arriving through this route is NOT sent through the activity resolver, which is why this is
		// a separate outcome from the ladder's own sequence-zero rung.
		Out.RawSequenceIndex = 0;
		Out.AssetKind = EElysiumAnimAssetKind::None;
		Out.Outcome = EElysiumAnimOutcome::ScriptedSequenceZero;
		Out.Detail = FString::Printf(
			TEXT("'%s' is not in '%s' vocabulary; retail sets sequence 0 and resets the cycle"),
			*Intent.SequenceLabel, *Intent.Stem);
	}

	void ResolveSetAnimationRoute(const FElysiumAnimationIntent& Intent,
		const FElysiumAnimationCatalog& Catalog, FElysiumAnimationSelection& Out)
	{
		if (Catalog.PropClips == nullptr)
		{
			Out.Outcome = EElysiumAnimOutcome::NoVocabulary;
			Out.Detail = FString::Printf(TEXT("no animated-prop vocabulary for '%s'"), *Intent.Stem);
			return;
		}

		// A prop owns every clip it can play, so there is no bank indirection and the record's owner is
		// the prop itself. Declaration order is semantic, which is why the index travels.
		if (const FElysiumPropClip* Clip = Catalog.PropClips->FindClip(Intent.SequenceLabel))
		{
			Out.SequenceLabel = Clip->Name;
			Out.TargetSequence = Clip->Name;
			Out.AnimationName = Clip->Name;
			Out.RawSequenceIndex = Clip->Index;
			Out.OwnerStem = Catalog.PropClips->Stem;
			Out.Weight = Clip->Weight;
			Out.bLooping = Clip->IsLooping();
			Out.bSnap = (Clip->Flags & 0x2) != 0;
			Out.Candidates = 1;
			Out.AssetKind = EElysiumAnimAssetKind::Sequence;
			Out.Outcome = EElysiumAnimOutcome::Resolved;
			return;
		}

		// `CBaseProp::Spawn`'s rest-pose fallback: sequence index 0, which a prop sidecar CAN name
		// because it carries declaration order.
		const FString Rest = Catalog.PropClips->RestSequence();
		if (!Rest.IsEmpty())
		{
			Out.SequenceLabel = Rest;
			Out.AnimationName = Rest;
			Out.RawSequenceIndex = 0;
			Out.OwnerStem = Catalog.PropClips->Stem;
			Out.AssetKind = EElysiumAnimAssetKind::Sequence;
			Out.Outcome = EElysiumAnimOutcome::SequenceZero;
			Out.Detail = FString::Printf(TEXT("'%s' is not on prop '%s'; stood on sequence 0"),
				*Intent.SequenceLabel, *Catalog.PropClips->Stem);
			return;
		}

		Out.Outcome = EElysiumAnimOutcome::MissingSequence;
		Out.Detail = FString::Printf(TEXT("prop '%s' bakes no clip at all"), *Catalog.PropClips->Stem);
	}

	void ResolveGestureRoute(const FElysiumAnimationIntent& Intent,
		const FElysiumAnimationCatalog& Catalog, FElysiumAnimationSelection& Out)
	{
		if (Catalog.Clips->Find(Intent.SequenceLabel) != nullptr)
		{
			Out.Candidates = 1;
			ApplyLabel(Intent.SequenceLabel, Intent, Catalog, Out);
			return;
		}
		// `SetGesture` simply returns when its lookup is negative. Both gesture calls the shipped
		// corpus makes address labels absent from the target's whole vocabulary, so both animate
		// nothing — which is behaviour to reproduce, not a defect to route around.
		Out.AssetKind = EElysiumAnimAssetKind::None;
		Out.Outcome = EElysiumAnimOutcome::GestureNoOp;
		Out.Detail = FString::Printf(TEXT("gesture '%s' is absent from '%s'; nothing plays"),
			*Intent.SequenceLabel, *Intent.Stem);
	}
}

FString PickWeighted(const FElysiumNpcClipSet& Set, const FString& Activity, int32 Variant)
{
	if (Activity.IsEmpty())
	{
		return FString();
	}
	TArray<FString> Candidates = Set.ByActivity(Activity);
	if (Candidates.IsEmpty())
	{
		return FString();
	}
	// Sorted so the walk is stable across two runs of the same map; the weights are what the pick
	// actually rides on.
	Candidates.Sort();
	int32 TotalWeight = 0;
	for (const FString& Label : Candidates)
	{
		const FElysiumNpcClip* Clip = Set.Find(Label);
		TotalWeight += FMath::Max(1, Clip ? Clip->Weight : 1);
	}
	const uint32 Seed = HashCombineFast(GetTypeHash(Set.Stem.ToLower()),
		static_cast<uint32>(FMath::Max(0, Variant)));
	int32 Pick = static_cast<int32>(Seed % static_cast<uint32>(TotalWeight));
	for (const FString& Label : Candidates)
	{
		const FElysiumNpcClip* Clip = Set.Find(Label);
		Pick -= FMath::Max(1, Clip ? Clip->Weight : 1);
		if (Pick < 0)
		{
			return Label;
		}
	}
	return Candidates[0];
}

FElysiumPoseParams PoseFrom(const FElysiumAnimationIntent& Intent)
{
	FElysiumPoseParams Pose;
	Pose.Set(TEXT("move_yaw"), Intent.Body.MoveYaw());
	Pose.Set(TEXT("aim_yaw"), Intent.AimYaw);
	Pose.Set(TEXT("aim_pitch"), Intent.AimPitch);
	return Pose;
}

void Resolve(const FElysiumAnimationIntent& Intent, const FElysiumAnimationCatalog& Catalog,
	FElysiumAnimationSelection& Out)
{
	Out = FElysiumAnimationSelection();
	Out.Source = Intent.Source;
	Out.Channel = Intent.Channel;
	Out.Route = Intent.Route;
	Out.Generation = Intent.Generation;
	Out.Stem = Intent.Stem;
	Out.Variant = Intent.Variant;

	// Step 6 — the continuous parameters, published whatever the selection turned out to be. They are
	// what the graph steers on, and a frame that resolved nothing still moved.
	Out.AirPhase = Intent.AirPhase;
	Out.MoveYaw = Intent.Body.MoveYaw();
	Out.Speed = Intent.Body.Speed2D();
	Out.AimYaw = Intent.AimYaw;
	Out.AimPitch = Intent.AimPitch;

	if (Intent.Route == EElysiumAnimRoute::SetAnimation)
	{
		ResolveSetAnimationRoute(Intent, Catalog, Out);
		return;
	}

	if (Catalog.Clips == nullptr || !Catalog.Clips->IsValid())
	{
		// The ordinary answer in the gym, on a menu backdrop, and for any model the character export
		// has not covered. A body with no vocabulary is a state, not an error.
		Out.RequestedActivity = Intent.Activity;
		Out.Outcome = EElysiumAnimOutcome::NoVocabulary;
		Out.Detail = Intent.Stem.IsEmpty()
			? TEXT("no model stem")
			: FString::Printf(TEXT("no clip vocabulary for '%s'"), *Intent.Stem);
		return;
	}

	switch (Intent.Route)
	{
	case EElysiumAnimRoute::ExactLabel:
		ResolveExactLabelRoute(Intent, Catalog, Out);
		break;
	case EElysiumAnimRoute::Gesture:
		ResolveGestureRoute(Intent, Catalog, Out);
		break;
	case EElysiumAnimRoute::Activity:
	default:
		ResolveActivityRoute(Intent, Catalog, Out);
		break;
	}
}

void CollectDeclaringHosts(const FElysiumBlendTable* Table, const FString& LayerLabel,
	TArray<FString>& OutHosts)
{
	OutHosts.Reset();
	if (Table == nullptr || LayerLabel.IsEmpty())
	{
		return;
	}
	for (const TPair<FString, FElysiumAutoLayerBinding>& Entry : Table->AutoLayers)
	{
		if (Entry.Value.Clips.Contains(LayerLabel))
		{
			OutHosts.Add(Entry.Key);
		}
	}
	OutHosts.Sort();
}

FString ResolveLayerHost(const FString& StandingSequence, const FElysiumBlendTable* Table,
	const FString& LayerLabel)
{
	if (!StandingSequence.IsEmpty())
	{
		return StandingSequence;
	}
	TArray<FString> Hosts;
	CollectDeclaringHosts(Table, LayerLabel, Hosts);
	return Hosts.IsEmpty() ? FString() : Hosts[0];
}

FString DescribeLayerAssetMiss(const FString& LayerLabel, const FString& LayerOwner,
	const FString& Host)
{
	return FString::Printf(
		TEXT("'%s' is owned by '%s' but neither its grid, its derived form '%s@%s' nor its plain "
			"label is on the mount (host %s)"),
		*LayerLabel, *LayerOwner, *LayerLabel, *Host,
		Host.IsEmpty() ? TEXT("was not found in the owner's autolayer table") : *Host);
}

FString DescribeLayerArmedForm(ELayerAssetForm Form, const FString& Label, const FString& Host)
{
	switch (Form)
	{
	case ELayerAssetForm::DerivedGrid:
		return FString::Printf(TEXT("derived grid '%s'@'%s'"), *Label, *Host);
	case ELayerAssetForm::PlainGrid:
		return FString::Printf(TEXT("plain-label grid '%s'"), *Label);
	case ELayerAssetForm::DerivedSequence:
		return FString::Printf(TEXT("derived form '%s@%s'"), *Label, *Host);
	case ELayerAssetForm::PlainSequence:
		return FString::Printf(TEXT("plain-label fallback '%s'"), *Label);
	case ELayerAssetForm::None:
	default:
		return TEXT("nothing");
	}
}

} // namespace ElysiumAnimResolve
