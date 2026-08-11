#include "Visual/ElysiumAnimSubsystem.h"

#include "ElysiumContentPaths.h"
#include "ElysiumMoveSolve.h"          // the sv_*scale constants the gait tables are built with
#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAnim, Log, All);

namespace
{
	// CCC10 — resolve `Selection.LayerLabels` (the bake-time autolayer binding the base channel's
	// own resolved host declared) into the assets the graph's upper-body nodes read. Sorted by the
	// CLIP's own additive flag, never by declaration position: the order in `LayerLabels` is
	// composition order, not a kind tag, and exactly one shipped host (`throwing_star_midcrouch_idle`)
	// declares its additive first.
	void ResolveLayerAssets(const FElysiumAnimationCatalog& Catalog,
		const FElysiumAnimationSelection& Selection, USkeletalMesh* Mesh,
		FElysiumResolvedAnimation& Assets)
	{
		if (Mesh == nullptr || Catalog.Clips == nullptr)
		{
			return;
		}

		for (const FString& LayerLabel : Selection.LayerLabels)
		{
			const FElysiumNpcClip* LayerClip = Catalog.Clips->Find(LayerLabel);
			if (LayerClip == nullptr)
			{
				// One of the five per-bank orphans the autolayer table can name but the catalog does
				// not carry (`docs/vtmb/animation_and_movers.md` A.3) — there is nothing to load.
				continue;
			}

			const FString LayerOwner = LayerClip->Owner;
			const FElysiumBlendTable* LayerTable = Catalog.BlendTableFor
				? Catalog.BlendTableFor(LayerOwner) : nullptr;
			const FElysiumBlendGrid* LayerGrid = LayerTable != nullptr
				? LayerTable->Find(LayerLabel) : nullptr;

			if (LayerGrid != nullptr && LayerGrid->IsMultiCell())
			{
				// An aim grid, baked once per declaring host (`_derived_bindings` in
				// `UE_mdl_skeletal.py`) — the loader's own `Host` parameter is the exporter's
				// dedicated seam for this, not a mangled label.
				Assets.OverlaySpace = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, LayerOwner,
					LayerLabel, Selection.SequenceLabel);
				if (Assets.OverlaySpace != nullptr)
				{
					// Every cell of a grid shares one bone mask (A.4), so reading it off the base
					// cell [0][0] is reading it off the whole grid.
					if (const FElysiumBlendCell* BaseCell = LayerGrid->CellAt(0, 0))
					{
						if (UAnimSequence* BaseCellSequence = ElysiumNpcVisual::LoadBakedClip(Mesh,
							LayerOwner, BaseCell->Clip))
						{
							if (const UElysiumAnimLayerMask* Mask =
								BaseCellSequence->FindMetaDataByClass<UElysiumAnimLayerMask>())
							{
								Assets.OverlayMaskName = Mask->Profile;
							}
						}
					}
				}
				continue;
			}

			// A plain sequence — additive or masked overlay. A clip whose mask owns the shared
			// ancestor split bone ships ONLY in derived `<clip>@<host>` form; one that does not
			// ships once under its plain label, and every additive ships both. Trying the derived
			// name first and falling back handles either shape without knowing which one applies.
			const FString DerivedLabel = FString::Printf(TEXT("%s@%s"), *LayerLabel,
				*Selection.SequenceLabel);
			UAnimSequence* LayerSequence = ElysiumNpcVisual::LoadBakedClip(Mesh, LayerOwner,
				DerivedLabel);
			if (LayerSequence == nullptr)
			{
				LayerSequence = ElysiumNpcVisual::LoadBakedClip(Mesh, LayerOwner, LayerLabel);
			}
			if (LayerSequence == nullptr)
			{
				continue;
			}

			if (LayerClip->IsAdditive())
			{
				Assets.AdditiveSequence = LayerSequence;
				continue;
			}

			Assets.OverlaySequence = LayerSequence;
			if (const UElysiumAnimLayerMask* Mask =
				LayerSequence->FindMetaDataByClass<UElysiumAnimLayerMask>())
			{
				Assets.OverlayMaskName = Mask->Profile;
			}
		}
	}
}

void UElysiumAnimSubsystem::Deinitialize()
{
	BankAssets.Reset();
	ClipSets.Reset();
	FacialRigs.Reset();
	CompositionRigs.Reset();
	EyeSets.Reset();
	BlendTables.Reset();
	Super::Deinitialize();
}

const FElysiumNpcIndex& UElysiumAnimSubsystem::GetIndex()
{
	if (!bIndexLoaded)
	{
		bIndexLoaded = true;
		FString Error;
		if (!Index.Load(Error))
		{
			UE_LOG(LogElysiumAnim, Warning, TEXT("npc index: %s"), *Error);
		}
		else
		{
			UE_LOG(LogElysiumAnim, Log, TEXT("npc index: %d NPCs, %d animation banks"),
				Index.Npcs.Num(), Index.Banks.Num());
		}
	}
	return Index;
}

const FElysiumDispositionTable& UElysiumAnimSubsystem::GetDispositions()
{
	if (!bDispositionsLoaded)
	{
		bDispositionsLoaded = true;
		FString Error;
		if (!Dispositions.Load(Error))
		{
			UE_LOG(LogElysiumAnim, Warning, TEXT("disposition table: %s"), *Error);
		}
	}
	return Dispositions;
}

const FElysiumNpcClipSet* UElysiumAnimSubsystem::GetClipSet(const FString& Stem)
{
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	if (const TSharedPtr<FElysiumNpcClipSet>* Cached = ClipSets.Find(Stem))
	{
		return Cached->Get();
	}

	TSharedPtr<FElysiumNpcClipSet> Set = MakeShared<FElysiumNpcClipSet>();
	FString Error;
	if (!Set->Load(Stem, Error))
	{
		UE_LOG(LogElysiumAnim, Warning, TEXT("npc clips '%s': %s"), *Stem, *Error);
		Set.Reset();   // remembered as a miss, so this is not retried per NPC sharing the stem
	}
	ClipSets.Add(Stem, Set);
	return Set.Get();
}

TSharedPtr<const FElysiumFacialRig> UElysiumAnimSubsystem::GetFacialRig(const FString& Stem)
{
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	if (const TSharedPtr<const FElysiumFacialRig>* Cached = FacialRigs.Find(Stem))
	{
		return *Cached;
	}

	// The index names the sidecar, so a model with no flex rig is answered without touching the
	// disk — and answered null, which is a normal load, not a failure.
	const FElysiumNpcIndexEntry* Entry = GetIndex().Npcs.Find(Stem);
	TSharedPtr<const FElysiumFacialRig> Result;
	if (Entry != nullptr && !Entry->Facial.IsEmpty())
	{
		TSharedPtr<FElysiumFacialRig> Rig = MakeShared<FElysiumFacialRig>();
		FString Error;
		if (!Rig->Load(Entry->Facial, Error))
		{
			UE_LOG(LogElysiumAnim, Warning, TEXT("facial '%s': %s"), *Stem, *Error);
		}
		else if (!Rig->IsValid())
		{
			// A rig with nothing to weight is answered the same way as no rig at all, so no body
			// carries a facial track that cannot move anything.
			UE_LOG(LogElysiumAnim, Verbose,
				TEXT("facial '%s': %d controllers, %d rules, no morph targets — no face to drive"),
				*Stem, Rig->Controllers.Num(), Rig->Rules.Num());
		}
		else
		{
			UE_LOG(LogElysiumAnim, Verbose,
				TEXT("facial '%s': %d controllers, %d rules, %d morphs, %d lid(s)"), *Stem,
				Rig->Controllers.Num(), Rig->Rules.Num(), Rig->Morphs.Num(), Rig->Lids.Num());
			Result = Rig;
		}
	}
	FacialRigs.Add(Stem, Result);
	return Result;
}

TSharedPtr<const FElysiumEyeSet> UElysiumAnimSubsystem::GetEyeSet(const FString& Stem)
{
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	const FString& CacheKey = Stem;
	if (const TSharedPtr<const FElysiumEyeSet>* Cached = EyeSets.Find(CacheKey))
	{
		return *Cached;
	}

	// As with the flex rig, the index names the sidecar — so a model with no eyeballs is answered
	// without touching the disk, and answered null, which is a normal load.
	const FElysiumNpcIndexEntry* Entry = GetIndex().Npcs.Find(Stem);
	TSharedPtr<const FElysiumEyeSet> Result;
	if (Entry != nullptr && !Entry->Eyes.IsEmpty())
	{
		TSharedPtr<FElysiumEyeSet> Set = MakeShared<FElysiumEyeSet>();
		FString Error;
		if (!Set->Load(Entry->Eyes, Error))
		{
			UE_LOG(LogElysiumAnim, Warning, TEXT("eyes '%s': %s"), *Stem, *Error);
		}
		else
		{
			UE_LOG(LogElysiumAnim, Verbose, TEXT("eyes '%s': %d record(s), lids %s"), *Stem,
				Set->Eyeballs.Num(),
				Set->Eyeballs[0].HasLids() ? TEXT("driven") : TEXT("absent (no flex rig)"));
			Result = Set;
		}
	}
	EyeSets.Add(CacheKey, Result);
	return Result;
}

TSharedPtr<const FElysiumBlendTable> UElysiumAnimSubsystem::GetBlendTable(const FString& Stem)
{
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	if (const TSharedPtr<const FElysiumBlendTable>* Cached = BlendTables.Find(Stem))
	{
		return *Cached;
	}

	// A grid can be declared by any of the three things that own animations: a character's own model,
	// a shared bank, or a skeletal prop. The stem arrives here already resolved to whichever owns the
	// clip, so all three groups are searched rather than assuming the caller knew which it was.
	const FElysiumNpcIndex& Loaded = GetIndex();
	const FElysiumNpcIndexEntry* Entry = Loaded.Npcs.Find(Stem);
	if (Entry == nullptr)
	{
		Entry = Loaded.Banks.Find(Stem);
	}
	FString RelPath = Entry != nullptr ? Entry->Blends : FString();
	if (RelPath.IsEmpty())
	{
		for (const TPair<FString, FElysiumAnimatedPropEntry>& Prop : Loaded.AnimatedProps)
		{
			if (Prop.Value.Stem.Equals(Stem, ESearchCase::IgnoreCase))
			{
				RelPath = Prop.Value.Blends;
				break;
			}
		}
	}

	TSharedPtr<const FElysiumBlendTable> Result;
	if (!RelPath.IsEmpty())
	{
		TSharedPtr<FElysiumBlendTable> Table = MakeShared<FElysiumBlendTable>();
		FString Error;
		if (!Table->Load(RelPath, Error) || !Table->IsValid())
		{
			UE_LOG(LogElysiumAnim, Warning, TEXT("blends '%s': %s"), *Stem,
				Error.IsEmpty() ? TEXT("no usable grid") : *Error);
		}
		else
		{
			UE_LOG(LogElysiumAnim, Verbose, TEXT("blends '%s': %d grid(s), %d pose parameter(s)"),
				*Stem, Table->Grids.Num(), Table->PoseParams.Num());
			Result = Table;
		}
	}
	BlendTables.Add(Stem, Result);
	return Result;
}

FString UElysiumAnimSubsystem::ResolveGridClip(const FString& OwnerStem, const FString& Label,
	const FElysiumPoseParams& Pose)
{
	if (OwnerStem.IsEmpty() || Label.IsEmpty())
	{
		return Label;
	}
	// Nearly every label names one animation, and a model with no multi-cell sequence has no sidecar
	// at all — so the common path is a cached null and one map lookup that misses.
	const TSharedPtr<const FElysiumBlendTable> Table = GetBlendTable(OwnerStem);
	if (!Table.IsValid())
	{
		return Label;
	}
	const FElysiumBlendGrid* Grid = Table->Find(Label);
	if (Grid == nullptr)
	{
		return Label;
	}

	const FElysiumBlendPick Pick = ElysiumBlendGrids::SelectCell(*Grid, *Table, Pose);
	if (Pick.Cell == nullptr || Pick.Cell->Clip.IsEmpty())
	{
		// Every shipped grid has at least two live cells, so this is a damaged sidecar. Playing the
		// label is what the runtime did before grids were read at all — worse, but never silent.
		UE_LOG(LogElysiumAnim, Warning,
			TEXT("blend grid '%s' on '%s' selected no playable cell; falling back to the label"),
			*Label, *OwnerStem);
		return Label;
	}

	UE_LOG(LogElysiumAnim, Verbose,
		TEXT("blend grid '%s' on '%s' -> cell [%d,%d] '%s'"), *Label, *OwnerStem,
		Pick.Index[0], Pick.Index[1], *Pick.Cell->Clip);
	return Pick.Cell->Clip;
}

namespace
{
	// Build one composition rig out of the two things that declare it: the index's own split-bone
	// inventory and, when the model declares any driven bone, the rule table beside its glb. Either
	// half may be empty; a model with neither is answered null, and that is a normal load.
	TSharedPtr<const FElysiumCompositionRig> BuildCompositionRig(const FString& Stem,
		const TArray<FString>& SplitBones, const FString& ProceduralRelPath,
		FString& OutError)
	{
		TSharedPtr<FElysiumCompositionRig> Rig = MakeShared<FElysiumCompositionRig>();
		Rig->Stem = Stem;
		Rig->SplitBones.Reserve(SplitBones.Num());
		for (const FString& BoneName : SplitBones)
		{
			Rig->SplitBones.Add(FName(*BoneName));
		}
		if (!ProceduralRelPath.IsEmpty()
			&& !Rig->LoadAxisRules(ProceduralRelPath, OutError))
		{
			// A named-but-unreadable table is a fault, not a model without one: the split half is
			// still installed so the body keeps whatever composition it can have.
			Rig->AxisRules.Reset();
		}
		return Rig->HasWork() ? TSharedPtr<const FElysiumCompositionRig>(Rig) : nullptr;
	}
}

TSharedPtr<const FElysiumCompositionRig> UElysiumAnimSubsystem::GetCompositionRig(
	const FString& Stem)
{
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	const FString& CacheKey = Stem;
	if (const TSharedPtr<const FElysiumCompositionRig>* Cached = CompositionRigs.Find(CacheKey))
	{
		return *Cached;
	}

	const FElysiumNpcIndexEntry* Entry = GetIndex().Npcs.Find(Stem);
	TSharedPtr<const FElysiumCompositionRig> Result;
	if (Entry != nullptr)
	{
		FString Error;
		Result = BuildCompositionRig(Stem, Entry->SplitRotationBones, Entry->Procedural, Error);
		if (!Error.IsEmpty())
		{
			UE_LOG(LogElysiumAnim, Warning, TEXT("procedural '%s': %s"), *Stem, *Error);
		}
		else if (Result.IsValid())
		{
			UE_LOG(LogElysiumAnim, Verbose, TEXT("composition '%s': %d split bone(s), %d rule(s)"),
				*Stem, Result->SplitBones.Num(), Result->AxisRules.Num());
		}
	}
	CompositionRigs.Add(CacheKey, Result);
	return Result;
}

TSharedPtr<const FElysiumCompositionRig> UElysiumAnimSubsystem::GetAnimatedPropCompositionRig(
	const FString& ModelPath)
{
	if (ModelPath.IsEmpty())
	{
		return nullptr;
	}
	const FElysiumAnimatedPropEntry* Entry = GetIndex().FindAnimatedProp(ModelPath);
	if (Entry == nullptr)
	{
		return nullptr;
	}
	if (const TSharedPtr<const FElysiumCompositionRig>* Cached = CompositionRigs.Find(Entry->Stem))
	{
		return *Cached;
	}

	FString Error;
	TSharedPtr<const FElysiumCompositionRig> Result =
		BuildCompositionRig(Entry->Stem, Entry->SplitRotationBones, Entry->Procedural, Error);
	if (!Error.IsEmpty())
	{
		UE_LOG(LogElysiumAnim, Warning, TEXT("procedural prop '%s': %s"), *Entry->Stem, *Error);
	}
	CompositionRigs.Add(Entry->Stem, Result);
	return Result;
}

UglTFRuntimeAsset* UElysiumAnimSubsystem::GetBankAsset(const FString& BankStem, FString& OutError)
{
	OutError.Reset();
	if (const TObjectPtr<UglTFRuntimeAsset>* Cached = BankAssets.Find(BankStem))
	{
		if (*Cached != nullptr)
		{
			return Cached->Get();
		}
	}

	const FString Path = GetIndex().BankGlbPath(BankStem);
	if (Path.IsEmpty())
	{
		OutError = FString::Printf(TEXT("'%s' is not a known animation bank"), *BankStem);
		return nullptr;
	}

	const double Start = FPlatformTime::Seconds();
	UglTFRuntimeAsset* Asset = ElysiumNpcVisual::LoadAssetFromPath(Path, OutError);
	if (Asset == nullptr)
	{
		return nullptr;
	}
	BankAssets.Add(BankStem, Asset);
	UE_LOG(LogElysiumAnim, Log, TEXT("npc bank '%s' parsed in %.0f ms (%.1f MB)"), *BankStem,
		(FPlatformTime::Seconds() - Start) * 1000.0,
		static_cast<double>(IFileManager::Get().FileSize(*Path)) / 1e6);
	return Asset;
}

UAnimSequence* UElysiumAnimSubsystem::ResolveClip(const FString& Stem, const FString& ClipName,
	USkeletalMesh* Mesh, UglTFRuntimeAsset* OwnAsset, FString& OutError)
{
	OutError.Reset();
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	if (Set == nullptr)
	{
		OutError = FString::Printf(TEXT("no clip vocabulary for '%s'"), *Stem);
		return nullptr;
	}
	const FElysiumNpcClip* Clip = Set->Find(ClipName);
	if (Clip == nullptr)
	{
		OutError = FString::Printf(TEXT("'%s' resolves no clip named '%s'"), *Stem, *ClipName);
		return nullptr;
	}

	// The owner column decides where the clip comes from either way: the NPC itself for a dialogue
	// clip, a bank for everything else.
	const FString Owner = Clip->IsOwnedBy(Stem) ? Stem : Clip->Owner;
	// A baked sequence is bound to the shared skeleton, so it is the same asset for every body and
	// is addressed by owner and resolved animation name rather than rebuilt per mesh.
	if (UAnimSequence* Baked =
		ElysiumNpcVisual::LoadBakedClip(Mesh, Owner, ResolveClipAnimName(Stem, ClipName)))
	{
		return Baked;
	}

	// The NPC's own dialogue clips live in the glb the mesh came from; everything else is a bank.
	UglTFRuntimeAsset* Asset = OwnAsset;
	if (!Clip->IsOwnedBy(Stem))
	{
		Asset = GetBankAsset(Clip->Owner, OutError);
	}
	else if (Asset == nullptr)
	{
		OutError = FString::Printf(TEXT("clip '%s' is owned by '%s' itself, but its glb was not passed"),
			*ClipName, *Stem);
	}
	if (Asset == nullptr)
	{
		return nullptr;
	}
	return ElysiumNpcVisual::RetargetClip(Asset, Mesh, ResolveClipAnimName(Stem, ClipName), OutError);
}

FString UElysiumAnimSubsystem::ResolveClipAnimName(const FString& Stem, const FString& ClipName,
	const FElysiumPoseParams& Pose)
{
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	const FElysiumNpcClip* Clip = Set != nullptr ? Set->Find(ClipName) : nullptr;
	if (Clip == nullptr)
	{
		return ClipName;
	}
	// The grid is declared by whoever owns the animation, which is a bank for anything but a dialogue
	// clip — so the owner column decides which table is asked, not the character.
	const FString Owner = Clip->IsOwnedBy(Stem) ? Stem : Clip->Owner;
	return ResolveGridClip(Owner, ClipName, Pose);
}

bool UElysiumAnimSubsystem::ResolveGrid(const FString& Stem, const FString& ClipName,
	USkeletalMesh* Mesh, FElysiumResolvedGrid& OutGrid)
{
	OutGrid = FElysiumResolvedGrid();
	// Same ownership rule as every other resolver here: a grid is declared by whoever owns the
	// animations, which is a bank for anything but a dialogue clip.
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	const FElysiumNpcClip* Clip = Set != nullptr ? Set->Find(ClipName) : nullptr;
	if (Clip == nullptr)
	{
		return false;
	}
	const FString Owner = Clip->IsOwnedBy(Stem) ? Stem : Clip->Owner;

	const TSharedPtr<const FElysiumBlendTable> Table = GetBlendTable(Owner);
	const FElysiumBlendGrid* Grid = Table.IsValid() ? Table->Find(ClipName) : nullptr;
	if (Grid == nullptr)
	{
		// Not a defect: most labels name one animation and declare no grid at all.
		return false;
	}

	UBlendSpace* Space = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, Owner, ClipName);
	if (Space == nullptr)
	{
		return false;
	}

	OutGrid.Space = Space;
	OutGrid.Label = ClipName;
	OutGrid.Axes = (Grid->GroupSize[1] > 1 && Grid->ParamIndex[1] != INDEX_NONE) ? 2 : 1;
	for (int32 Axis = 0; Axis < OutGrid.Axes; ++Axis)
	{
		const FElysiumPoseParamDesc* Desc = Table->Param(Grid->ParamIndex[Axis]);
		OutGrid.AxisName[Axis] = Desc != nullptr ? Desc->Name : FString::Printf(TEXT("axis%d"), Axis);
		OutGrid.AxisMin[Axis] = Grid->ParamStart[Axis];
		OutGrid.AxisMax[Axis] = Grid->ParamEnd[Axis];
	}
	return true;
}

FElysiumAnimationCatalog UElysiumAnimSubsystem::BuildCatalog(const FString& Stem)
{
	FElysiumAnimationCatalog Catalog;
	Catalog.Clips = GetClipSet(Stem);
	Catalog.PropClips = Stem.IsEmpty() ? nullptr : GetIndex().AnimatedProps.Find(Stem);
	// The owning bank is not known until the weighted pick has run, so the table arrives as a lookup
	// rather than as a preloaded map. The shared pointer lives in this subsystem's session-lifetime
	// cache, so the raw pointer outlives every resolve that reads it.
	Catalog.BlendTableFor = [this](const FString& Owner) -> const FElysiumBlendTable*
	{
		return GetBlendTable(Owner).Get();
	};
	return Catalog;
}

void UElysiumAnimSubsystem::ResolveAnimation(const FElysiumAnimationIntent& Intent,
	USkeletalMesh* Mesh, UglTFRuntimeAsset* OwnAsset, FElysiumAnimationSelection& OutSelection,
	FElysiumResolvedAnimation& OutAssets)
{
	OutAssets = FElysiumResolvedAnimation();

	// The record comes out of the sidecars and is always producible. The asset needs a skeleton, and
	// that is a separate question with a separate answer. Kept rather than re-built below: CCC10's
	// layer resolution reads the same catalog the primary asset resolved against.
	const FElysiumAnimationCatalog Catalog = BuildCatalog(Intent.Stem);
	ElysiumAnimResolve::Resolve(Intent, Catalog, OutSelection);
	if (!OutSelection.IsResolved() || OutSelection.AssetKind == EElysiumAnimAssetKind::None)
	{
		return;
	}

	if (Mesh == nullptr)
	{
		OutSelection.Outcome = EElysiumAnimOutcome::NoAsset;
		OutSelection.Detail = FString::Printf(
			TEXT("'%s' resolved to '%s'@'%s', but the body has no skeletal mesh to bind against yet"),
			*OutSelection.SequenceLabel, *OutSelection.AnimationName, *OutSelection.OwnerStem);
		return;
	}

	if (OutSelection.AssetKind == EElysiumAnimAssetKind::BlendSpace)
	{
		// A grid is addressed by the LABEL: it is the thing a label names when it does not name one
		// animation, so there is no cell to select first.
		OutAssets.Space = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, OutSelection.OwnerStem,
			OutSelection.SequenceLabel);
		if (OutAssets.Space != nullptr)
		{
			ResolveLayerAssets(Catalog, OutSelection, Mesh, OutAssets);
			return;
		}
		// A fan the bake has not covered still names its current cell, so the body can stand that one
		// clip rather than nothing — and the record already says which cell it is.
		OutSelection.AssetKind = EElysiumAnimAssetKind::Sequence;
	}

	// Addressed by owner and resolved animation name, matching what the record says rather than
	// re-resolving the label at the neutral pose.
	const bool bOwnsItself = OutSelection.OwnerStem.Equals(Intent.Stem, ESearchCase::IgnoreCase);
	OutAssets.Sequence = ElysiumNpcVisual::LoadBakedClip(Mesh, OutSelection.OwnerStem,
		OutSelection.AnimationName);
	if (OutAssets.Sequence == nullptr)
	{
		FString Error;
		UglTFRuntimeAsset* Asset = bOwnsItself ? OwnAsset : GetBankAsset(OutSelection.OwnerStem, Error);
		if (Asset != nullptr)
		{
			OutAssets.Sequence = ElysiumNpcVisual::RetargetClip(Asset, Mesh,
				OutSelection.AnimationName, Error);
		}
		if (OutAssets.Sequence == nullptr)
		{
			OutSelection.AssetKind = EElysiumAnimAssetKind::None;
			OutSelection.Outcome = EElysiumAnimOutcome::NoAsset;
			OutSelection.Detail = Error.IsEmpty()
				? FString::Printf(TEXT("'%s'@'%s' is not baked and its bank did not answer"),
					*OutSelection.AnimationName, *OutSelection.OwnerStem)
				: Error;
		}
	}

	// A layer rides a DIFFERENT pose than the one it composes onto, so it resolves whether or not
	// the primary asset above loaded.
	ResolveLayerAssets(Catalog, OutSelection, Mesh, OutAssets);
}

bool UElysiumAnimSubsystem::ResolveGaitSpeeds(const FElysiumGaitSpeedRequest& Request,
	FElysiumGaitSpeeds& Out)
{
	Out = FElysiumGaitSpeeds();
	if (!Request.IsValid())
	{
		return false;
	}

	// One catalog for all three gaits. The lookups behind it are cached per stem, but the view also
	// carries the blend-table callback, and rebuilding it three times would re-enter those caches for
	// an answer that cannot have changed between the calls.
	const FElysiumAnimationCatalog Catalog = BuildCatalog(Request.Stem);

	auto ResolveOne = [this, &Request, &Catalog](EElysiumAnimActivityCode Code, float Scale,
		FElysiumGaitSpeedTable& Table) -> bool
	{
		FElysiumAnimationIntent Intent;
		Intent.Stem = Request.Stem;
		Intent.Activity = ElysiumAnimIntent::ActivityName(Code);
		Intent.Variant = Request.Variant;
		Intent.WeaponTag = Request.WeaponTag;
		Intent.FormTag = Request.FormTag;
		Intent.Source = EElysiumAnimSource::Player;
		// A gait that resolves through the fallback ladder is not that gait. Reaching `walk` for a
		// missing `sneak` and then calling its speeds the sneak table is exactly the silent
		// substitution the record exists to prevent, and here it would also make the body move at
		// walking pace while playing a crouch.
		Intent.bAllowFallbackLadder = false;

		FElysiumAnimationSelection Selection;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Selection);
		if (Selection.SequenceLabel.IsEmpty() || Selection.OwnerStem.IsEmpty())
		{
			return false;
		}
		// The fan belongs to the bank the weighted pick landed in, not to the body — one body's walk
		// and its run routinely come from different banks.
		const TSharedPtr<const FElysiumBlendTable> Owner = GetBlendTable(Selection.OwnerStem);
		if (!Owner.IsValid())
		{
			return false;
		}
		const FElysiumBlendGrid* Grid = Owner->Find(Selection.SequenceLabel);
		if (Grid == nullptr)
		{
			return false;
		}
		return ElysiumBlendGrids::SpeedFan(*Grid, *Owner, Scale, Table);
	};

	// `sv_walkscale` 1.0, `sv_runscale` 1.0, `sv_sneakscale` 2.3, and the rate multiplier on two of
	// the three (`docs/vtmb/source_movement.md` -> "The scales, and the walk asymmetry").
	const float Rate = FMath::IsFinite(Request.SpeedScale) ? FMath::Max(Request.SpeedScale, 0.0f) : 1.0f;
	ResolveOne(EElysiumAnimActivityCode::Walk, ElysiumMove::WalkScale, Out.Walk);
	ResolveOne(EElysiumAnimActivityCode::Run, ElysiumMove::RunScale * Rate, Out.Run);
	ResolveOne(EElysiumAnimActivityCode::Sneak, ElysiumMove::SneakScale * Rate, Out.Sneak);

	// Strafing left and strafing right command the same speed. A divergence, and the reason it is
	// applied here rather than in the fan is that the fan is what the export says.
	Out.Walk.Symmetrize();
	Out.Run.Symmetrize();
	Out.Sneak.Symmetrize();

	return Out.IsValid();
}

bool UElysiumAnimSubsystem::ResolveActivityClip(const FString& Stem, const FString& Activity,
	int32 Variant, FString& OutLabel, FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	OutLabel.Reset();
	OutAnimName.Reset();
	OutGroundSpeedCmPerSecond = 0.f;

	// Expressed over the one resolver rather than beside it: two implementations of one weighted pick
	// and one owner rule are exactly how the player path and the cast path come to disagree about a
	// bank with nothing reporting it.
	FElysiumAnimationIntent Intent;
	Intent.Stem = Stem;
	Intent.Activity = Activity;
	Intent.Variant = Variant;
	Intent.Source = EElysiumAnimSource::Npc;
	// This adapter's contract predates the fallback ladder and its callers read the miss — a scripted
	// walk gait takes a false return as "no authored speed" and keeps its own. The ladder arrives with
	// those callers when they move onto the intent seam, not underneath them.
	Intent.bAllowFallbackLadder = false;

	FElysiumAnimationSelection Selection;
	ElysiumAnimResolve::Resolve(Intent, BuildCatalog(Stem), Selection);
	if (Selection.SequenceLabel.IsEmpty() || Selection.AnimationName.IsEmpty())
	{
		return false;
	}

	OutLabel = Selection.SequenceLabel;
	OutAnimName = Selection.AnimationName;
	OutGroundSpeedCmPerSecond = Selection.GroundSpeedCmPerSecond;
	return true;
}

UAnimSequence* UElysiumAnimSubsystem::ResolveClipFromBank(const FString& BankStem,
	const FString& ClipName, USkeletalMesh* Mesh, FString& OutError)
{
	OutError.Reset();
	if (BankStem.IsEmpty() || Mesh == nullptr)
	{
		OutError = TEXT("no bank or no mesh");
		return nullptr;
	}
	// This path never consults the clip vocabulary, so the bank is both the asset and the grid owner.
	const FString AnimName = ResolveGridClip(BankStem, ClipName);
	if (UAnimSequence* Baked = ElysiumNpcVisual::LoadBakedClip(Mesh, BankStem, AnimName))
	{
		return Baked;
	}
	UglTFRuntimeAsset* Asset = GetBankAsset(BankStem, OutError);
	if (Asset == nullptr)
	{
		return nullptr;
	}
	return ElysiumNpcVisual::RetargetClip(Asset, Mesh, AnimName, OutError);
}

TArray<FString> UElysiumAnimSubsystem::IdleCandidates(const FString& Stem,
	const FString& Disposition, EElysiumIdleTier& OutTier)
{
	OutTier = EElysiumIdleTier::None;
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	if (Set == nullptr)
	{
		return {};
	}

	// 1. The disposition stance set. `default_disposition` names a row whose "Animation Name"
	//    keys the `Stance_<Name>_Idle_*` clips in the NPC's gendered stances bank — the include
	//    DAG already picked male vs female, so there is no gender branch here.
	const FString AnimName = GetDispositions().AnimNameFor(Disposition);
	TArray<FString> Candidates = Set->StanceClips(AnimName);
	if (!Candidates.IsEmpty())
	{
		OutTier = EElysiumIdleTier::Stance;
	}
	else
	{
		// 2. ACT_IDLE. Weight discriminates here: `idle01` carries 30 against three fidgets at 1.
		Candidates = Set->ByActivity(ElysiumActivity::Idle);
		if (!Candidates.IsEmpty())
		{
			OutTier = EElysiumIdleTier::ActIdle;
		}
		else
		{
			// 3. The monsters and one-offs (`rat`, `tzim3`, `newscaster_male`) whose clips carry no
			//    activity at all. Only here does a label read decide anything.
			for (const TPair<FString, FElysiumNpcClip>& Pair : Set->Clips)
			{
				if (Pair.Value.Activity.IsEmpty() && Pair.Key.Contains(TEXT("idle"), ESearchCase::IgnoreCase))
				{
					Candidates.Add(Pair.Key);
				}
			}
			if (!Candidates.IsEmpty())
			{
				OutTier = EElysiumIdleTier::Loose;
			}
		}
	}
	Set->SortByWeight(Candidates);
	return Candidates;
}

FString UElysiumAnimSubsystem::PickIdleClip(const FString& Stem, const FString& Disposition,
	EElysiumIdleTier& OutTier, int32 Variant)
{
	const TArray<FString> Candidates = IdleCandidates(Stem, Disposition, OutTier);
	if (Candidates.IsEmpty())
	{
		return FString();
	}
	// Variant only spreads across a *stance* set, whose members are equal-weight alternatives of
	// one pose. An ACT_IDLE set is not interchangeable — `idle01` carries weight 30 against three
	// fidgets at 1, so index 0 is the resting pick and the rest are one-shot fidgets.
	if (OutTier != EElysiumIdleTier::Stance || Variant <= 0)
	{
		return Candidates[0];
	}
	return Candidates[Variant % Candidates.Num()];
}

FString UElysiumAnimSubsystem::PickActivityClip(const FString& Stem, const FString& Activity,
	int32 Variant)
{
	// The pick itself is a pure rule over a vocabulary, so it lives with the resolver and this is the
	// cache lookup in front of it.
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	return Set != nullptr ? ElysiumAnimResolve::PickWeighted(*Set, Activity, Variant) : FString();
}

const TCHAR* UElysiumAnimSubsystem::TierName(EElysiumIdleTier Tier)
{
	switch (Tier)
	{
	case EElysiumIdleTier::Stance:  return TEXT("stance");
	case EElysiumIdleTier::ActIdle: return TEXT("ACT_IDLE");
	case EElysiumIdleTier::Loose:   return TEXT("loose");
	default:                        return TEXT("none");
	}
}

void UElysiumAnimSubsystem::GetBankStats(int32& OutCount, int64& OutBytes) const
{
	OutCount = 0;
	OutBytes = 0;
	for (const TPair<FString, TObjectPtr<UglTFRuntimeAsset>>& Pair : BankAssets)
	{
		if (Pair.Value == nullptr)
		{
			continue;
		}
		++OutCount;
		const FElysiumNpcIndexEntry* E = Index.Banks.Find(Pair.Key);
		if (E != nullptr)
		{
			OutBytes += IFileManager::Get().FileSize(*FElysiumContentPaths::NpcBankGlb(E->Glb));
		}
	}
}
