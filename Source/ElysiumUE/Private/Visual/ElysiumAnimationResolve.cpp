#include "Visual/ElysiumAnimationResolve.h"

#include "Visual/ElysiumActionTables.h"
#include "Visual/ElysiumAnimGraph.h"

namespace ElysiumAnimResolve
{

namespace
{
	const TCHAR* const GDispositionActivity = TEXT("ACT_DISPOSITION");
	const TCHAR* const GRunActivity = TEXT("ACT_RUN");
	const TCHAR* const GWalkActivity = TEXT("ACT_WALK");
	// The one request that is answered without an availability probe at all: a scripted custom move
	// names the sequence set the scene author meant, so retail commits it whatever the body carries.
	const TCHAR* const GScriptCustomMove = TEXT("ACT_SCRIPT_CUSTOM_MOVE");
	// `CAI_BaseNPC::TranslateActivity`'s own bound on the class/weapon alternation.
	constexpr int32 GMaxAlternations = 5;

	// Add one more fact to the record's line without erasing what is already there. The outcome
	// reports below are written AFTER `ApplyLabel` may have noted a cell-level fallback, and a plain
	// assignment would erase the note the fallback exists to make visible.
	void AppendDetail(FElysiumAnimationSelection& Out, FString&& Note)
	{
		Out.Detail = Out.Detail.IsEmpty() ? MoveTemp(Note) : Out.Detail + TEXT("; ") + Note;
	}

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
		//
		// Through `MaskedClipPlayableOn`, which is the one place "which channel may pose a partial
		// clip" is answered: this refusal and the two bone-mask refusals in `UElysiumAnimSubsystem`
		// are the same channel rule over two kinds of partial pose, and a second spelling here is a
		// second answer waiting to let one of them through.
		if (Out.bAdditive && !ElysiumAnimIntent::MaskedClipPlayableOn(Intent.Channel))
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

		// A grid, resolved at the body's own pose parameters. **One rule for every fan**: the floor
		// cell, with the fraction to the next riding on the pick — which is what a blend space
		// evaluates, and it is the same answer whether the axis is `move_yaw` or `hit_yaw`. A
		// directional hit is a blend between two authored reactions exactly as a strafing walk is a
		// blend between two authored gaits; quantizing one of them to its nearest cell would be a
		// snap the graph is perfectly able not to perform.
		//
		// The cell is named even for a blend space, because a record that says only "a blend space"
		// cannot be checked against a capture.
		const FElysiumPoseParams Pose = PoseFrom(Intent);
		const FElysiumBlendPick Pick = ElysiumBlendGrids::SelectCell(*Grid, *Table, Pose);
		if (Pick.Cell != nullptr && !Pick.Cell->Clip.IsEmpty())
		{
			Out.AnimationName = Pick.Cell->Clip;
			if (Pick.Cell->Motion.IsUsable())
			{
				Out.GroundSpeedCmPerSecond = Pick.Cell->Motion.GroundSpeedCmPerSecond;
			}
			// The second half of the pair, along axis 0 — the cell `AxisFraction[0]` weighs. Empty at
			// the top of a fan: `ResolveAxis` clamps there and the fraction is zero, so there is no
			// second cell and nothing to weigh.
			const FElysiumBlendCell* Next = Grid->CellAt(Pick.Index[0] + 1, Pick.Index[1]);
			if (Next != nullptr && !Next->Clip.IsEmpty())
			{
				Out.NextAnimationName = Next->Clip;
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
			Out.AxisFraction[Axis] = Pick.Fraction[Axis];
		}
		Out.Outcome = EElysiumAnimOutcome::Resolved;
		// A grid whose activity routes to a sequence-only state would play a null sequence — full
		// body reference pose — and the blend-space pointer would defeat `ShouldHoldPose`. Named
		// here, before any asset is loaded.
		ElysiumAnimGraph::RefuseUnplayableGrid(Out);
	}

	// Step 4's choice for one activity, or empty.
	//
	// **The direction-keyed selection stands AHEAD of the weighted draw**, which is where retail's
	// player selector puts it: it reads the candidates' authored state masks and, when one of them
	// answers the buttons being held, that sequence IS the answer and no draw happens
	// (`docs/vtmb/combat-and-damage.md` § "The melee sequence selector is two systems, forked on the
	// owner's class"). Every request
	// from a body with no button field, and every activity none of whose candidates authors a mask,
	// falls straight through to the draw unchanged.
	FString TryActivity(const FElysiumAnimationCatalog& Catalog, const FString& Activity,
		int32 Variant, int32 StateMask, bool bRequireStateMask, int32& OutCandidates)
	{
		OutCandidates = 0;
		if (Activity.IsEmpty())
		{
			return FString();
		}
		OutCandidates = Catalog.Clips->ByActivity(Activity).Num();
		if (OutCandidates <= 0)
		{
			return FString();
		}
		const FString Keyed = PickByStateMask(*Catalog.Clips, Activity, StateMask);
		if (!Keyed.IsEmpty())
		{
			return Keyed;
		}
		// **The player arm does not fall through.** `CBasePlayer`'s melee selector seeds its answer
		// with -1 and returns `answer >= 0`, so an activity whose candidates author no mask is simply
		// not answered — and the whole `ACT_MELEE_ATTACK_2COMBO_<FAMILY>` family authors none, which
		// is measured: 43 of 43 player presses at Melee 5 requested the combo and were refused here
		// (`docs/vtmb/combat-and-damage.md`). The weighted draw below is the CAST's arm reached
		// through the wrong door; taking it for the player is what plays a combo retail cannot.
		if (bRequireStateMask)
		{
			return FString();
		}
		return PickWeighted(*Catalog.Clips, Activity, Variant);
	}

	void ResolveActivityRoute(const FElysiumAnimationIntent& Intent,
		const FElysiumAnimationCatalog& Catalog, FElysiumAnimationSelection& Out)
	{
		// The logical request stays un-translated. Retail's `m_Activity` is this: translation changes
		// the sequence set that realizes a request, not the AI-visible state.
		Out.RequestedActivity = Intent.Activity;

		const FElysiumTranslationResult Translation = TranslateActivity(Intent, Catalog);
		Out.PreTranslationActivity = Translation.PreTranslation;
		Out.FirstWeaponActivity = Translation.FirstWeaponActivity;
		Out.WeaponActivity = Translation.WeaponActivity;
		Out.TranslationIterations = Translation.Iterations;
		Out.ResolvedActivity = Translation.Resolved;
		// The remaining hops the walk took (LIFE4): the class answer and the two rungs. Together
		// with the lines above they are the whole chain a readout renders, so a wrong pose names
		// the rung that produced it rather than only the final answer.
		Out.ClassActivity = Translation.ClassActivity;
		Out.WeaponRung = Translation.WeaponRung;
		Out.AvailabilityRung = Translation.AvailabilityRung;

		int32 Candidates = 0;
		FString Label = TryActivity(Catalog, Out.ResolvedActivity, Intent.Variant, Intent.StateMask,
			Intent.bRequireStateMask, Candidates);

		if (!Label.IsEmpty())
		{
			Out.Candidates = Candidates;
			ApplyLabel(Label, Intent, Catalog, Out);
			if (Out.Outcome != EElysiumAnimOutcome::Resolved)
			{
				return;
			}
			// **The authored `required` bit does not gate any of this**, and giving it a gate would
			// be giving it behaviour retail does not have: `CBaseCombatWeapon::ActivityOverride`
			// never reads the third dword, so the 201 flagged rows and the 9,013 optional ones take
			// the same availability path. The bit rides on the translation as provenance only.
			if (Translation.bRunToWalk)
			{
				Out.Outcome = EElysiumAnimOutcome::RunToWalk;
				AppendDetail(Out, FString::Printf(
					TEXT("nothing the translation named is on '%s'; the recovered ACT_RUN fallback ")
					TEXT("took ACT_WALK"), *Intent.Stem));
			}
			else if (Translation.AvailabilityRung > 1)
			{
				// Rung 1 is the translation's own answer; every rung below it is retail's ordered
				// availability fallback, and which one answered is what separates "the body plays
				// what the weapon named" from "the body plays what it had".
				Out.Outcome = EElysiumAnimOutcome::TranslatedFallback;
				AppendDetail(Out, FString::Printf(
					TEXT("'%s' has no sequence on '%s'; availability rung %d took '%s'"),
					*Translation.WeaponActivity, *Intent.Stem, Translation.AvailabilityRung,
					*Out.ResolvedActivity));
			}
			return;
		}

		// A class rule the walk could not decide is not an ordinary miss, and it outranks the
		// ladder's own report: the request may well have had an answer the RE did not enumerate.
		if (Translation.bUnresolvedFamily || Translation.bGrappleUnresolved)
		{
			Out.AssetKind = EElysiumAnimAssetKind::None;
			Out.Outcome = EElysiumAnimOutcome::MissingSequence;
			Out.Detail = FString::Printf(
				TEXT("'%s' resolved nothing on '%s', and %s, so the untranslated request is not a ")
				TEXT("verdict"), *Translation.Resolved, *Intent.Stem,
				Translation.bUnresolvedFamily
					? TEXT("a class rule names a request family the RE never enumerated")
					: TEXT("it is a paired-action base with no role state to select a variant"));
			return;
		}

		// **The rest of the recovered fallback ladder is the cast's, not the player's.** The run
		// retry lives in the translation, where retail puts it; what is left here is the whole
		// request as a disposition, then sequence zero. The player's chain has no ladder at all —
		// the controlled corpus records a ducked ACT_LAND_CROUCH request simply returning -1 — so a
		// player miss is reported as the named miss it is. It forks on the body's own chain, not on
		// the producer: a damage reaction on a cast body walks the ladder its class descends from.
		if (Intent.BodyKind == EElysiumAnimBodyKind::Cast && Intent.bAllowFallbackLadder)
		{
			// The disposition rung takes the same door, which costs nothing: no stance sequence in the
			// corpus authors a state mask, so the ladder's own retry is the weighted draw it always was.
			Label = TryActivity(Catalog, GDispositionActivity, Intent.Variant, Intent.StateMask,
				Intent.bRequireStateMask, Candidates);
			if (!Label.IsEmpty())
			{
				Out.ResolvedActivity = GDispositionActivity;
				Out.Candidates = Candidates;
				ApplyLabel(Label, Intent, Catalog, Out);
				if (Out.Outcome == EElysiumAnimOutcome::Resolved)
				{
					Out.Outcome = EElysiumAnimOutcome::Disposition;
					AppendDetail(Out, FString::Printf(
						TEXT("'%s' resolved nothing on '%s'; retried as ACT_DISPOSITION"),
						*Translation.Resolved, *Intent.Stem));
				}
				return;
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

		// A named miss: the activity and the body, which is what a fallback has to be declared
		// against. Nothing is substituted for it — the graph declares its own fallback off this line.
		Out.AssetKind = EElysiumAnimAssetKind::None;
		Out.Outcome = EElysiumAnimOutcome::MissingSequence;
		Out.Detail = FString::Printf(TEXT("'%s' has no sequence on '%s'"),
			*Translation.Resolved, *Intent.Stem);
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

FElysiumTranslationResult TranslateActivity(const FElysiumAnimationIntent& Intent,
	const FElysiumAnimationCatalog& Catalog)
{
	using namespace ElysiumActionTables;

	FElysiumTranslationResult Out;
	Out.Requested = Intent.Activity;
	Out.Resolved = Intent.Activity;
	Out.FirstWeaponActivity = Intent.Activity;
	Out.WeaponActivity = Intent.Activity;

	const FElysiumNpcClipSet* Clips = Catalog.Clips;
	auto Carries = [Clips](const FString& Activity)
	{
		return Clips != nullptr && Clips->HasActivity(Activity);
	};

	// The weapon's own ladder, keyed on the entity classname authored content spells. Null is the
	// ordinary answer twice over — empty hands, and 108 of the 169 weapon subclasses carry no table
	// — and it means "translate nothing", exactly as retail's unarmed body does.
	const FWeaponLadder* Ladder = Intent.WeaponClassname.IsEmpty()
		? nullptr : FindLadderByEntityClass(Intent.WeaponClassname);

	// **The chain a request takes is its BODY's**, the same discriminator the fallback ladder below
	// uses: `CBasePlayer` and `CAI_BaseNPC` are two different translators, not two configurations of
	// one, and retail picks between them on the receiver's RTTI class rather than on who asked. So a
	// damage reaction, a scene beat, an interaction and a debug stand all walk the cast chain on a
	// cast body, and every one of them walks the player's one pass on the player.
	const bool bCast = Intent.BodyKind == EElysiumAnimBodyKind::Cast;
	const FNpcClass* Class = (bCast && !Intent.ActorClassname.IsEmpty())
		? FindNpcClassByEntityClass(Intent.ActorClassname) : nullptr;
	const int32 PreBody = Class != nullptr ? Class->PreTranslate : INDEX_NONE;
	const int32 ClassBody = Class != nullptr ? Class->ClassTranslate : INDEX_NONE;

	// **The armed/alert branch, answered only where the decode is confirmed.**
	//
	// `CNPC_VHuman`'s pre-translation (`0x103854f0`) writes one flag that decides whether the body
	// stands in its alert set or its relaxed one, and the two arms of that branch are spelled as
	// their own predicates. The recovered decision tree reads six things; this runtime models two of
	// them, and the four it does not — `m_bfAINPCFlags & 0x10000` (`FORCE_RELAXED_ANIMS`),
	// `m_bfAINPCFlags2 & 0x400` (`MOVE_FACE_ENEMY`), `m_bfNPCFrenziedFlags & 0x200` and
	// `m_afMemory` bit 27 — are flags no system here can set, so no branch of the tree they gate can
	// be reached either way.
	//
	// What is left is decidable and is what these two rows answer:
	//   - no active weapon: the tree's own early-out, before any state is read;
	//   - `m_NPCState == NPC_STATE_ALERT`: sets the flag unconditionally;
	//   - any state that is neither alert nor combat: falls past every override to the tail, clear.
	//
	// **A body in combat answers NEITHER arm, and that is a stated absence rather than an oversight.**
	// The combat rung terminates in a ConVar whose default no static read of the image can recover —
	// the writers are computed and leave no literal anywhere in the binary — so a body there keeps
	// its untranslated request, which is what it did before any of this was decoded. Guessing that
	// ConVar is the one failure the committed tables exist to prevent.
	//
	// Every other predicate answers false for the same reason it always did: the state it reads is
	// the global gait override, the movement policy byte, cover and reload capability, and the form
	// and variant bits, none of which this runtime publishes.
	const bool bHasActiveWeapon = !Intent.WeaponClassname.IsEmpty();
	const EElysiumNpcState ActorState = Intent.ActorState;
	auto LiveState = [bHasActiveWeapon, ActorState](ENpcPredicate Predicate, int32)
	{
		switch (Predicate)
		{
		case ENpcPredicate::ArmedAlert:
			return bHasActiveWeapon && ActorState == EElysiumNpcState::Alert;
		case ENpcPredicate::NotArmedAlert:
			return !bHasActiveWeapon
				|| (ActorState != EElysiumNpcState::Alert
					&& ActorState != EElysiumNpcState::Combat);
		default:
			return false;
		}
	};

	int32 CoverContext = 0;

	auto Record = [&](const FNpcTranslation& Walk)
	{
		CoverContext = Walk.CoverContext;
		Out.bUnresolvedFamily |= Walk.bUnresolved;
		// The tail row matches every request, so reaching it says nothing on its own. What is
		// unanswerable is reaching it carrying a base that IS one of the 29 registered paired-action
		// families, because the variant then needs role and counterpart state.
		Out.bGrappleUnresolved |= Walk.bGrappleTail && FindGrappleFamily(Walk.Activity) != nullptr;
	};

	// One actor-side translation. The availability probe inside the weapon ladder does not record:
	// a rung retail rejected did not happen, and its side effects are not part of the record.
	auto ActorTranslate = [&](const FString& Base, bool bRecord) -> FString
	{
		if (!bCast)
		{
			// `CBasePlayer::NPC_TranslateActivity` (`0x101647a0`) — two rows, and they are what makes
			// an unarmed relaxed gait resolve at all: a player body carries no `ACT_WALK_RELAXED` or
			// `ACT_RUN_RELAXED` sequence, only weapon-suffixed ones.
			return TranslatePlayerActivity(Base);
		}
		if (ClassBody == INDEX_NONE)
		{
			return Base;
		}
		const FNpcTranslation Walk = NpcTranslate(ClassBody, Base, CoverContext, LiveState,
			Carries);
		if (bRecord)
		{
			Record(Walk);
		}
		return Walk.Activity;
	};

	// `CBaseCombatWeapon::ActivityOverride` (`0x1024f210`): walk the ladder front to back and take
	// the first rung the body can play. **The probe tests the candidate as the actor table would
	// leave it**, because retail passes each row's output through the owner's `NPC_TranslateActivity`
	// before asking whether a sequence exists.
	auto WeaponTranslate = [&](const FString& Base) -> FString
	{
		if (Ladder == nullptr)
		{
			return Base;
		}
		const FTranslation Result = Translate(*Ladder, Base, [&](const FString& Candidate)
			{ return Carries(ActorTranslate(Candidate, /*bRecord*/ false)); });
		if (Result.bTranslated)
		{
			Out.WeaponRung = Result.Rung;
			Out.bRequired = Result.bRequired;
		}
		return Result.Activity;
	};

	if (!bCast)
	{
		// The pinned player order: `SetIdealActivity` -> `+0x5f4` -> `+0x5e0` -> `SetActivity`. One
		// pass, no alternation, and no availability probe after it — a player miss resolves to
		// nothing and is named by the caller, which is what the controlled corpus records.
		Out.Iterations = 1;
		Out.FirstWeaponActivity = WeaponTranslate(Out.Requested);
		Out.WeaponActivity = Out.FirstWeaponActivity;
		Out.Resolved = ActorTranslate(Out.WeaponActivity, /*bRecord*/ false);
		return Out;
	}

	// 1. `+0x5dc` pre-translates the raw request.
	FString Current = Out.Requested;
	if (PreBody != INDEX_NONE)
	{
		const FNpcTranslation Walk = NpcTranslate(PreBody, Current, CoverContext, LiveState,
			Carries);
		Record(Walk);
		Current = Walk.Activity;
	}
	Out.PreTranslation = Current;

	// 2. the weapon translator, whose first answer is retained separately from the last.
	Current = WeaponTranslate(Current);
	Out.FirstWeaponActivity = Current;

	// 3. up to five (`+0x5e0`, `+0x5f4`) alternations, remembering the latest CHANGED class answer.
	// The loop stops when the weapon answer equals the activity that entered the iteration.
	for (int32 Pass = 0; Pass < GMaxAlternations; ++Pass)
	{
		++Out.Iterations;
		const FString ClassAnswer = ActorTranslate(Current, /*bRecord*/ true);
		if (!ClassAnswer.Equals(Current, ESearchCase::IgnoreCase))
		{
			Out.ClassActivity = ClassAnswer;
		}
		const FString WeaponAnswer = WeaponTranslate(ClassAnswer);
		if (WeaponAnswer.Equals(Current, ESearchCase::IgnoreCase))
		{
			break;
		}
		Current = WeaponAnswer;
	}
	Out.WeaponActivity = Current;

	// 4. a scripted custom move returns without an availability probe.
	if (Out.Requested.Equals(GScriptCustomMove, ESearchCase::IgnoreCase))
	{
		Out.Resolved = Current;
		return Out;
	}

	// 5. availability, in the recovered order: the final weapon answer, the remembered class answer,
	// the first weapon answer, then the original logical request.
	//
	// A caller whose contract predates the fallback ladder reads the miss and keeps its own answer,
	// so the probe is what its `bAllowFallbackLadder` switches off: a gait resolved through rung 3
	// is not that gait, and reporting it as one is the silent substitution the record exists to
	// prevent.
	if (!Intent.bAllowFallbackLadder)
	{
		Out.Resolved = Current;
		return Out;
	}

	const FString* const Rungs[] =
	{
		&Out.WeaponActivity, &Out.ClassActivity, &Out.FirstWeaponActivity, &Out.Requested
	};
	for (int32 Rung = 0; Rung < static_cast<int32>(UE_ARRAY_COUNT(Rungs)); ++Rung)
	{
		if (Rungs[Rung]->IsEmpty() || !Carries(*Rungs[Rung]))
		{
			continue;
		}
		Out.Resolved = *Rungs[Rung];
		Out.AvailabilityRung = Rung + 1;
		return Out;
	}

	// The recovered last resort, keyed on the ORIGINAL request rather than on the translated one.
	if (Out.Requested.Equals(GRunActivity, ESearchCase::IgnoreCase))
	{
		Out.Resolved = GWalkActivity;
		Out.bRunToWalk = true;
		return Out;
	}

	// Nothing was playable. The record names what the translation produced, which is what a miss has
	// to be declared against.
	Out.Resolved = Out.WeaponActivity;
	return Out;
}

FString PickByStateMask(const FElysiumNpcClipSet& Set, const FString& Activity, int32 StateMask)
{
	if (Activity.IsEmpty() || StateMask == INDEX_NONE)
	{
		return FString();   // a body with no button field selects by weight alone
	}
	TArray<FString> Candidates = Set.ByActivity(Activity);
	if (Candidates.IsEmpty())
	{
		return FString();
	}
	// Sorted for the same reason the weighted draw is: exact identity is (owner, raw sequence index)
	// and the character export writes no raw index, so label order is the only stable tie-break this
	// runtime can state. It decides nothing in the shipped corpus, where an activity's candidates
	// state at most one mask each.
	Candidates.Sort();

	FString Best;
	ElysiumCombo::EStateMatch BestMatch = ElysiumCombo::EStateMatch::None;
	for (const FString& Label : Candidates)
	{
		const FElysiumNpcClip* Clip = Set.Find(Label);
		if (Clip == nullptr || !Clip->Combo.HasStateMask())
		{
			// A sequence authoring `-1`, or none at all, is not a candidate for state selection. It
			// stays a candidate for the weighted draw, which is what the caller falls through to.
			continue;
		}
		const ElysiumCombo::EStateMatch Match =
			ElysiumCombo::RankStateMask(Clip->Combo.Mask, StateMask);
		if (ElysiumCombo::IsBetterStateMatch(Match, BestMatch))
		{
			BestMatch = Match;
			Best = Label;
		}
	}
	return Best;
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
	Pose.Set(TEXT("hit_yaw"), Intent.HitYaw);
	return Pose;
}

FElysiumAnimationIntent ActivityIntentFor(const FElysiumActivityClipRequest& Request)
{
	FElysiumAnimationIntent Intent;
	Intent.Stem = Request.Stem;
	Intent.Activity = Request.Activity;
	Intent.Variant = Request.Variant;
	// The producer the caller named, not a guess: a player-owned weapon resolving an attack is a
	// `Player` request, a scripted beat is a `Scene` one, and a record naming `Npc` for all three
	// would report a producer that never asked.
	Intent.Source = Request.Source;
	// The caller's own body, not a guess: `FElysiumWeapon` is the same entity on a player and on a
	// combatant, so a kind stamped here would walk the cast chain for the player.
	Intent.BodyKind = Request.BodyKind;
	// **The whole translation context, not a subset.** The `+0x5dc` class body reads the classname,
	// the committed ladders read the weapon and the armed/alert branch reads the state; a request
	// stamped with any of them here would select for a different body than the producer's.
	Intent.ActorClassname = Request.ActorClassname;
	Intent.WeaponClassname = Request.WeaponClassname;
	Intent.ActorState = Request.ActorState;
	// Where the hit came from, in the `hit_yaw` parameter's own degrees. Zero on every request that is
	// not a directional reaction, which is the parameter's resting value and the middle of any fan
	// bound to it.
	Intent.HitYaw = Request.HitYaw;
	// The player's own button field, reduced to the selection bits. It is the producer's to state for
	// the same reason the body kind is: the player and a combatant reach this seam through the same
	// weapon entity, and only the caller knows which one is holding it.
	Intent.StateMask = Request.StateMask;
	// The player arm of the melee sequence selector, stated by the producer: only the weapon knows it
	// is running a player-owned melee swing, which is the one place retail runs that arm.
	Intent.bRequireStateMask = Request.bRequireStateMask;
	// The availability probe and the run-to-walk, disposition and sequence-zero rungs are parts of
	// `CAI_BaseNPC`'s own translation, unconditional on the cast chain — the human pre-translation
	// rewrites an unarmed ACT_WALK to ACT_WALK_RELAXED whatever the body can play, and rung 4 of the
	// probe is what hands a body carrying only the plain walk its gait back. Refusing the ladder here
	// would make this seam's answer differ from the per-frame publish's for the same request. It
	// reaches only cast bodies: the player chain has no ladder at either gate.
	//
	// **The exception is the gesture path**, and it is why the request states this rather than this
	// mapping hard-coding true: `AddGesture` reaches `SelectWeightedSequence` and simply returns on
	// -1, walking no rung at all, so a reaction producer on that path clears the flag and reads the
	// miss it was given.
	Intent.bAllowFallbackLadder = Request.bAllowFallbackLadder;
	return Intent;
}

void Resolve(const FElysiumAnimationIntent& Intent, const FElysiumAnimationCatalog& Catalog,
	FElysiumAnimationSelection& Out)
{
	Out = FElysiumAnimationSelection();
	Out.Source = Intent.Source;
	Out.BodyKind = Intent.BodyKind;
	Out.Channel = Intent.Channel;
	Out.Route = Intent.Route;
	Out.Generation = Intent.Generation;
	Out.Stem = Intent.Stem;
	Out.Variant = Intent.Variant;

	// Step 6 — the graph state, projected once and from the LOGICAL request. Set BEFORE the routes
	// run, so every path out of this function names a state: a miss holds its pose somewhere, and
	// `RefuseUnplayableGrid` below asks the record rather than re-deriving an answer of its own.
	Out.GraphState = ElysiumAnimGraph::StateForActivity(
		ElysiumAnimIntent::ActivityCode(Intent.Activity));

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
	const FString& Host, const FElysiumBlendTable* Table)
{
	// A layer only ever rides a host the autolayer table binds it to: that binding is what names the
	// chain a split-bone overlay's rotation is expressed against, and it is the seam the exporter
	// derives `<layer>@<host>` over. So a host outside the list has no correct form of this layer at
	// all, which reads as the same missing asset as a bake that skipped one and is not the same
	// repair — the first is the wrong question, the second is a re-bake.
	TArray<FString> Hosts;
	CollectDeclaringHosts(Table, LayerLabel, Hosts);
	if (!Host.IsEmpty() && !Hosts.IsEmpty() && !Hosts.Contains(Host))
	{
		const int32 Shown = FMath::Min(Hosts.Num(), 4);
		const FString Named = FString::Join(TArray<FString>(Hosts.GetData(), Shown), TEXT(", "));
		const FString Rest = Hosts.Num() > Shown
			? FString::Printf(TEXT(" (+%d more)"), Hosts.Num() - Shown) : FString();
		return FString::Printf(
			TEXT("'%s' is owned by '%s' and '%s' does not declare it — it rides %s%s, and a layer "
				"composes only over a host that names it"),
			*LayerLabel, *LayerOwner, *Host, *Named, *Rest);
	}
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
