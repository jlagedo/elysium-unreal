#include "Visual/ElysiumEntityBodies.h"

#include "ElysiumAnimationIntent.h"
#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumAnimGraph.h"
#include "Visual/ElysiumAnimationResolve.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Visual/ElysiumEntityBodiesLog.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Components/SkeletalMeshComponent.h"

bool UElysiumEntityBodies::PlayNpcLayer(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, float Weight, FString* OutError, FString* OutArmed,
	const FString* StandingHint)
{
	// Every refusal below says which one it was. Five paths answered one bare `false` before, and a
	// caller cannot tell "this body has no graph" from "this label is not in the vocabulary" from
	// "the mount has no asset" — three different fixes behind one silence.
	auto Refuse = [OutError, OutArmed](FString&& Why) -> bool
	{
		if (OutError != nullptr)
		{
			*OutError = MoveTemp(Why);
		}
		if (OutArmed != nullptr)
		{
			*OutArmed = ElysiumAnimResolve::DescribeLayerArmedForm(
				ElysiumAnimResolve::ELayerAssetForm::None, FString(), FString());
		}
		return false;
	};
	auto Armed = [OutArmed](ElysiumAnimResolve::ELayerAssetForm Form, const FString& Label,
		const FString& Host)
	{
		if (OutArmed != nullptr)
		{
			*OutArmed = ElysiumAnimResolve::DescribeLayerArmedForm(Form, Label, Host);
		}
	};

	UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr;
	if (Inst == nullptr)
	{
		return Refuse(TEXT("this body has no biped animation host"));
	}
	if (!Inst->HasCompiledGraph())
	{
		// The layer is composed by the graph's own layered blend (CCC10), so a body with no compiled
		// graph has nowhere to put one — the same refusal `PlayNpcGrid` gives for the same reason.
		return Refuse(TEXT("this body is on the native host, which carries no compiled graph — the "
			"generated ABP is not on the mount. Run `uv run elysium export bundle policy`"));
	}

	UElysiumAnimSubsystem* Anims = GetAnims();
	if (Anims == nullptr)
	{
		return Refuse(TEXT("no animation subsystem"));
	}

	USkeletalMesh* Mesh = Body->GetSkeletalMeshAsset();
	const FElysiumNpcClipSet* Set = Anims->GetClipSet(Stem);
	const FElysiumNpcClip* LayerClip = Set != nullptr ? Set->Find(ClipName) : nullptr;
	if (LayerClip == nullptr)
	{
		return Refuse(FString::Printf(
			TEXT("'%s' is not in %s's vocabulary (%d clips)"), *ClipName, *Stem,
			Set != nullptr ? Set->Clips.Num() : 0));
	}
	const FString LayerOwner = LayerClip->IsOwnedBy(Stem) ? Stem : LayerClip->Owner;

	// **A masked overlay ships once per DECLARING HOST, not under its plain label.** An overlay whose
	// mask owns the shared ancestor split bone has to be written against the chain that bone's
	// rotation is expressed in, and that chain is the host's — so the exporter emits it as
	// `<clip>@<host>` per host and suppresses the raw form outright, because ordinary FK reads the
	// raw one as the upper body folded about the waist. Asking for the plain label therefore finds
	// nothing for every aim layer, which is not a missing export.
	//
	// Standing sequence first (the published selection, else the lab's standing clip), table
	// fallback. Same rule the shipping resolver uses.
	FString Standing = Inst->GetAppliedSelection().SequenceLabel;
	if (Standing.IsEmpty() && StandingHint != nullptr)
	{
		Standing = *StandingHint;
	}
	const TSharedPtr<const FElysiumBlendTable> Table = Anims->GetBlendTable(LayerOwner);
	const FString Host = ElysiumAnimResolve::ResolveLayerHost(Standing, Table.Get(), ClipName);
	const FElysiumBlendGrid* Grid = Table.IsValid() ? Table->Find(ClipName) : nullptr;

	// An aim grid stands as a blend space and a melee overlay as a plain sequence — the same two
	// shapes the resolver's own layer path produces, decided the same way: does the label name a grid.
	UBlendSpace* Space = nullptr;
	ElysiumAnimResolve::ELayerAssetForm GridForm = ElysiumAnimResolve::ELayerAssetForm::None;
	if (!Host.IsEmpty())
	{
		Space = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, LayerOwner, ClipName, Host);
		if (Space != nullptr)
		{
			GridForm = ElysiumAnimResolve::ELayerAssetForm::DerivedGrid;
		}
	}
	if (Space == nullptr)
	{
		Space = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, LayerOwner, ClipName);
		if (Space != nullptr)
		{
			GridForm = ElysiumAnimResolve::ELayerAssetForm::PlainGrid;
		}
	}
	if (Space != nullptr)
	{
		// Every cell of a grid shares one mask (`docs/vtmb/animation_and_movers.md` A.4), so the
		// first sample that carries one names the whole grid's.
		FName MaskName;
		for (const FBlendSample& Sample : Space->GetBlendSamples())
		{
			if (Sample.Animation != nullptr)
			{
				if (const UElysiumAnimLayerMask* Mask =
					Sample.Animation->FindMetaDataByClass<UElysiumAnimLayerMask>())
				{
					MaskName = Mask->Profile;
					break;
				}
			}
		}
		Inst->ArmDebugUpperBodyOverlay(nullptr, Space, MaskName, Weight);
		Armed(GridForm, ClipName, Host);
		return true;
	}

	// **A grid stands as a blend space or it does not stand.** The sequence ladder below is not a
	// fallback for one, it is a different answer: `ResolveNpcClip` resolves a grid LABEL through the
	// grid at the neutral pose, so the arm would silently freeze the whole fan onto one cell that no
	// pose parameter can move again — and it loads that cell's raw, host-less form, which for an aim
	// layer is the split-bone pose ordinary FK reads as the arms folded over the head. Both present
	// as a body posed wrong rather than as a lookup that failed, which is the one thing this seam
	// exists to prevent. The resolver's own layer path already stops here; so does this one.
	if (Grid != nullptr && Grid->IsMultiCell())
	{
		return Refuse(ElysiumAnimResolve::DescribeLayerAssetMiss(ClipName, LayerOwner, Host,
			Table.Get()));
	}

	// The derived form first for the same reason, then the plain label — an additive ships both ways
	// and a mask-free overlay ships only plain, so trying both covers either without knowing which.
	UAnimSequence* Anim = nullptr;
	ElysiumAnimResolve::ELayerAssetForm SeqForm = ElysiumAnimResolve::ELayerAssetForm::None;
	if (!Host.IsEmpty())
	{
		Anim = ElysiumNpcVisual::LoadBakedClip(Mesh, LayerOwner,
			FString::Printf(TEXT("%s@%s"), *ClipName, *Host));
		if (Anim != nullptr)
		{
			SeqForm = ElysiumAnimResolve::ELayerAssetForm::DerivedSequence;
		}
	}
	if (Anim == nullptr)
	{
		Anim = ResolveNpcClip(Stem, ClipName, Mesh);
		if (Anim != nullptr)
		{
			SeqForm = ElysiumAnimResolve::ELayerAssetForm::PlainSequence;
		}
	}
	if (Anim == nullptr)
	{
		return Refuse(ElysiumAnimResolve::DescribeLayerAssetMiss(ClipName, LayerOwner, Host,
			Table.Get()));
	}
	// The same two-sided gate the retired accumulator carried, and it still keeps the composition
	// honest: an additive is read as a delta and needs no mask, while an ordinary layer is read as a
	// pose and is meaningless without one — composed unmasked it would pull every bone it does not
	// own toward the reference pose and lose the body's stance from the waist down.
	if (Anim->IsValidAdditive())
	{
		Inst->ArmDebugUpperBodyAdditive(Anim, Weight);
		Armed(SeqForm, ClipName, Host);
		return true;
	}
	if (const UElysiumAnimLayerMask* Mask = Anim->FindMetaDataByClass<UElysiumAnimLayerMask>())
	{
		Inst->ArmDebugUpperBodyOverlay(Anim, nullptr, Mask->Profile, Weight);
		Armed(SeqForm, ClipName, Host);
		return true;
	}
	return Refuse(FString::Printf(
		TEXT("'%s' loaded but is neither additive nor masked — an unmasked pose clip cannot be a "
			"layer, it would drag every bone it does not own to the reference pose"),
		*ClipName));
}

void UElysiumEntityBodies::SetNpcLayerAim(USkeletalMeshComponent* Body, float Yaw, float Pitch)
{
	if (UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr)
	{
		Inst->SetDebugUpperBodyAim(Yaw, Pitch);
	}
}

bool UElysiumEntityBodies::PlayNpcGrid(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, FElysiumResolvedGrid& OutGrid, EElysiumGraphState State,
	FString* OutError, FString* OutArmed, const FString* StandingHint)
{
	OutGrid = FElysiumResolvedGrid();
	auto Refuse = [OutError, OutArmed, &Stem, &ClipName](FString&& Why) -> bool
	{
		UE_LOG(LogElysiumBodies, Warning, TEXT("npc '%s' grid '%s': %s"),
			*Stem, *ClipName, *Why);
		if (OutError != nullptr)
		{
			*OutError = MoveTemp(Why);
		}
		if (OutArmed != nullptr)
		{
			*OutArmed = ElysiumAnimResolve::DescribeLayerArmedForm(
				ElysiumAnimResolve::ELayerAssetForm::None, FString(), FString());
		}
		return false;
	};

	UElysiumAnimSubsystem* Anims = GetAnims();
	UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr;
	if (Anims == nullptr)
	{
		return Refuse(TEXT("no animation subsystem"));
	}
	if (Inst == nullptr)
	{
		return Refuse(TEXT("this body has no biped animation host"));
	}
	if (!Inst->HasCompiledGraph())
	{
		// A grid is stood by publishing a selection that names it, so it needs the graph's own
		// blend-space player. A body with no compiled graph has nowhere to put one.
		return Refuse(TEXT("this body is on the native host, which carries no compiled graph — the "
			"generated ABP is not on the mount. Run `uv run elysium export bundle policy`"));
	}
	if (!Inst->CompiledStateCanPlayBlendSpace(State))
	{
		return Refuse(FString::Printf(
			TEXT("compiled state %s cannot play a blend space"),
			ElysiumAnimGraph::StateName(State)));
	}

	FString Standing = Inst->GetAppliedSelection().SequenceLabel;
	if (Standing.IsEmpty() && StandingHint != nullptr)
	{
		Standing = *StandingHint;
	}
	const FElysiumNpcClipSet* Set = Anims->GetClipSet(Stem);
	const FElysiumNpcClip* Clip = Set != nullptr ? Set->Find(ClipName) : nullptr;
	const FString LayerOwner = Clip != nullptr
		? (Clip->IsOwnedBy(Stem) ? Stem : Clip->Owner) : Stem;
	const TSharedPtr<const FElysiumBlendTable> Table = Anims->GetBlendTable(LayerOwner);
	const FString Host = ElysiumAnimResolve::ResolveLayerHost(Standing, Table.Get(), ClipName);

	FString Why;
	FString Armed;
	if (!Anims->ResolveGrid(Stem, ClipName, Body->GetSkeletalMeshAsset(), OutGrid, Host, &Why,
		&Armed))
	{
		return Refuse(MoveTemp(Why));
	}

	// The mirror of `PlayNpcLayer`'s gate, and it fails the same way from the other side. A grid whose
	// cells are partial-body `*_layer` overlays owns only the bones its mask names; stood as a BASE
	// pose there is no mask in the path at all, so every bone it does not own arrives at the shared
	// skeleton's reference pose and the body loses its stance from the waist down. Retail composes
	// those as layers and never as a base — a masked sequence reaching the base path is a defect,
	// not a mode. Arming one as a layer is what `PlayNpcLayer` above is for.
	for (const FBlendSample& Sample : OutGrid.Space->GetBlendSamples())
	{
		if (Sample.Animation != nullptr
			&& Sample.Animation->FindMetaDataByClass<UElysiumAnimLayerMask>() != nullptr)
		{
			OutGrid = FElysiumResolvedGrid();
			return Refuse(FString::Printf(
				TEXT("'%s' is a masked layer grid and cannot stand as a base pose — use gr_layer"),
				*ClipName));
		}
	}
	if (OutArmed != nullptr)
	{
		*OutArmed = MoveTemp(Armed);
	}

	// LabSetBody (and every other clip stand) plays through the one-shot slot as a looping montage.
	// That slot sits ON TOP of the state machine, so a grid published underneath it never reaches
	// the pose — the body keeps playing the clip it was stood on. End both owners so the graph's
	// blend-space player is what draws.
	Inst->StopOneShot(0.f);
	Inst->StopClip();
	StandGridSelection(*Inst, OutGrid, /*Axis0=*/0.f, /*Axis1=*/0.f, State);
	Body->TickAnimation(0.0f, false);
	Body->RefreshBoneTransforms();
	return true;
}

void UElysiumEntityBodies::SetNpcGridPosition(USkeletalMeshComponent* Body, float Axis0, float Axis1)
{
	UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr;
	if (Inst == nullptr || StandingGrid.Space == nullptr)
	{
		return;
	}
	// Re-published rather than written straight onto the instance: the graph reads its sample point
	// off the same record every other consumer reads, so a readout and a pose cannot disagree.
	// Moving the point does not restart the animations underneath it, because the generation is
	// unchanged and only a generation change asks the graph for a blend.
	StandGridSelection(*Inst, StandingGrid, Axis0, Axis1, StandingGridState);
}

void UElysiumEntityBodies::StandGridSelection(UElysiumBipedAnimInstance& Inst,
	const FElysiumResolvedGrid& Grid, float Axis0, float Axis1, EElysiumGraphState State)
{
	const bool bNewGrid = StandingGrid.Space != Grid.Space;
	StandingGrid = Grid;
	StandingGridState = State;

	FElysiumAnimationSelection Selection;
	Selection.Source = EElysiumAnimSource::Debug;
	Selection.Route = EElysiumAnimRoute::ExactLabel;
	// The state is explicit so a grid can be stood in any of the eight. Walk remains the default:
	// no one-shot completion contract. The activity is named beside it because every readout of the
	// record shows what was asked for, and a stand with no activity would read as a hole.
	Selection.GraphState = State;
	Selection.ResolvedActivity = ElysiumAnimGraph::ActivityForState(State);
	Selection.RequestedActivity = Selection.ResolvedActivity;
	Selection.SequenceLabel = Grid.Label;
	Selection.AssetKind = EElysiumAnimAssetKind::BlendSpace;
	Selection.Outcome = EElysiumAnimOutcome::Resolved;
	Selection.Axes = Grid.Axes;
	Selection.AxisValue[0] = Axis0;
	Selection.AxisValue[1] = Axis1;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		Selection.AxisName[Axis] = Grid.AxisName[Axis];
	}
	// Advanced only when the grid itself changes. A slider drag re-publishes the same generation, so
	// the graph moves the sample point instead of asking for a transition on every frame of the drag.
	StandingGridGeneration += bNewGrid ? 1 : 0;
	Selection.Generation = StandingGridGeneration;

	FElysiumResolvedAnimation Assets;
	Assets.Space = Grid.Space;
	Inst.PublishSelection(Selection, Assets);
}

void UElysiumEntityBodies::StopNpcGrid(USkeletalMeshComponent* Body)
{
	StandingGrid = FElysiumResolvedGrid();
	StandingGridState = EElysiumGraphState::Walk;
	UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr;
	if (Inst == nullptr)
	{
		return;
	}
	// A selection naming no asset, which the graph answers by HOLDING the pose it has rather than by
	// entering a state with an empty pin. Leaving the grid's selection published instead would keep
	// the fan playing underneath whatever clip is started next, and it would reappear the moment that
	// clip ended.
	FElysiumAnimationSelection Cleared;
	Cleared.Source = EElysiumAnimSource::Debug;
	Cleared.Outcome = EElysiumAnimOutcome::NoAsset;
	Cleared.Generation = ++StandingGridGeneration;
	Inst->PublishSelection(Cleared, FElysiumResolvedAnimation());
}

void UElysiumEntityBodies::StopNpcLayers(USkeletalMeshComponent* Body)
{
	if (UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr)
	{
		Inst->ClearDebugUpperBodyLayer();
	}
}
