#include "Visual/ElysiumAnimSubsystem.h"

#include "ElysiumContentPaths.h"
#include "ElysiumMoveSolve.h"          // the sv_*scale constants the gait tables are built with
#include "ElysiumStanceTypes.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Visual/ElysiumActionTables.h"   // the recovered task routes the restart rule reads
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

			// Standing sequence first, table fallback — the same host the lab path uses. A first-
			// sorted table pick would stand a different derived asset than the body is posing.
			const FString Host = ElysiumAnimResolve::ResolveLayerHost(Selection.SequenceLabel,
				LayerTable, LayerLabel);

			// Which of the two shapes below a layer took, and the three lookups that decide it. A
			// grid that resolves as a plain sequence still POSES — it stands the one cell it loaded
			// — so the failure is invisible in the frame and only a steerable parameter that never
			// moves reveals it. Verbose because it is per layer per publish.
			UE_LOG(LogElysiumAnim, Verbose,
				TEXT("[elysium] layer '%s' owner='%s' host='%s': table=%s grid=%s multicell=%s"),
				*LayerLabel, *LayerOwner, *Host,
				LayerTable != nullptr ? TEXT("yes") : TEXT("NO"),
				LayerGrid != nullptr ? TEXT("yes") : TEXT("NO"),
				LayerGrid != nullptr && LayerGrid->IsMultiCell() ? TEXT("yes") : TEXT("NO"));

			if (LayerGrid != nullptr && LayerGrid->IsMultiCell())
			{
				// An aim grid, baked once per declaring host (`_derived_bindings` in
				// `UE_mdl_skeletal.py`) — the loader's own `Host` parameter is the exporter's
				// dedicated seam for this, not a mangled label.
				if (!Host.IsEmpty())
				{
					Assets.OverlaySpace = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, LayerOwner,
						LayerLabel, Host);
				}
				if (Assets.OverlaySpace == nullptr)
				{
					Assets.OverlaySpace = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, LayerOwner,
						LayerLabel);
				}
				if (Assets.OverlaySpace != nullptr)
				{
					UE_LOG(LogElysiumAnim, Verbose,
						TEXT("[elysium] layer '%s' stands the GRID asset %s"), *LayerLabel,
						*GetNameSafe(Assets.OverlaySpace));
					// Every cell of a grid shares one bone mask (A.4), so reading it off the base
					// cell [0][0] is reading it off the whole grid.
					//
					// **The cell is asked for by its DERIVED name first, exactly as the grid's own
					// samples were baked.** A cell of an autolayer grid ships only as
					// `<cell>@<host>`: the raw form of a clip some host declares is suppressed, so
					// the bare label the blend table names has no asset behind it. Asking for the
					// bare name alone therefore finds nothing, and the mask stays unset — which is
					// not a neutral outcome. An unset mask writes a NULL blend profile, and a null
					// profile gives every bone a per-bone weight of zero, so the layer resolves,
					// binds, takes its aim parameters and then contributes nothing to the pose. The
					// body stands its base pose alone and the grid looks frozen rather than absent.
					if (const FElysiumBlendCell* BaseCell = LayerGrid->CellAt(0, 0))
					{
						UAnimSequence* BaseCellSequence = nullptr;
						if (!Host.IsEmpty())
						{
							BaseCellSequence = ElysiumNpcVisual::LoadBakedClip(Mesh, LayerOwner,
								FString::Printf(TEXT("%s@%s"), *BaseCell->Clip, *Host));
						}
						if (BaseCellSequence == nullptr)
						{
							BaseCellSequence = ElysiumNpcVisual::LoadBakedClip(Mesh, LayerOwner,
								BaseCell->Clip);
						}
						const UElysiumAnimLayerMask* Mask = BaseCellSequence != nullptr
							? BaseCellSequence->FindMetaDataByClass<UElysiumAnimLayerMask>()
							: nullptr;
						if (Mask != nullptr)
						{
							Assets.OverlayMaskName = Mask->Profile;
						}
						else
						{
							// Said out loud rather than composed at zero weight. The layer is
							// standing and steerable and still cannot reach the pose, which is the
							// one failure on this path that looks exactly like working content.
							UE_LOG(LogElysiumAnim, Warning,
								TEXT("[elysium] layer '%s' stands the grid %s but its base cell '%s' "
									 "(host '%s') carries no bone mask, so the layer would compose at "
									 "zero weight on every bone and the body poses its base alone"),
								*LayerLabel, *GetNameSafe(Assets.OverlaySpace), *BaseCell->Clip,
								*Host);
						}
					}
				}
				else
				{
					UE_LOG(LogElysiumAnim, Warning, TEXT("[elysium] layer %s"),
						*ElysiumAnimResolve::DescribeLayerAssetMiss(LayerLabel, LayerOwner, Host,
							LayerTable));
				}
				continue;
			}

			// A plain sequence — additive or masked overlay. A clip whose mask owns the shared
			// ancestor split bone ships ONLY in derived `<clip>@<host>` form; one that does not
			// ships once under its plain label, and every additive ships both. Trying the derived
			// name first and falling back handles either shape without knowing which one applies.
			UAnimSequence* LayerSequence = nullptr;
			if (!Host.IsEmpty())
			{
				LayerSequence = ElysiumNpcVisual::LoadBakedClip(Mesh, LayerOwner,
					FString::Printf(TEXT("%s@%s"), *LayerLabel, *Host));
			}
			if (LayerSequence == nullptr)
			{
				LayerSequence = ElysiumNpcVisual::LoadBakedClip(Mesh, LayerOwner, LayerLabel);
			}
			if (LayerSequence == nullptr)
			{
				UE_LOG(LogElysiumAnim, Warning, TEXT("[elysium] layer %s"),
					*ElysiumAnimResolve::DescribeLayerAssetMiss(LayerLabel, LayerOwner, Host,
						LayerTable));
				continue;
			}

			if (LayerClip->IsAdditive())
			{
				Assets.AdditiveSequence = LayerSequence;
				continue;
			}

			Assets.OverlaySequence = LayerSequence;
			UE_LOG(LogElysiumAnim, Verbose,
				TEXT("[elysium] layer '%s' stands the SEQUENCE asset %s — a single pose, so nothing "
					 "the aim parameters say can move it"),
				*LayerLabel, *GetNameSafe(LayerSequence));
			if (const UElysiumAnimLayerMask* Mask =
				LayerSequence->FindMetaDataByClass<UElysiumAnimLayerMask>())
			{
				Assets.OverlayMaskName = Mask->Profile;
			}
		}
	}

	// The bone mask a RESOLVED BASE asset carries, or none.
	//
	// **A grid is asked through its base cell**, exactly as the layer path above asks a layer grid:
	// every cell of a grid shares one mask, so cell [0][0] answers for the whole fan — while the
	// `UBlendSpace` itself carries no metadata at all and would report every masked fan as unmasked.
	const UElysiumAnimLayerMask* BaseLayerMask(const FElysiumAnimationCatalog& Catalog,
		const FElysiumAnimationSelection& Selection, USkeletalMesh* Mesh,
		const FElysiumResolvedAnimation& Assets)
	{
		if (Assets.Sequence != nullptr)
		{
			return Assets.Sequence->FindMetaDataByClass<UElysiumAnimLayerMask>();
		}
		if (Assets.Space == nullptr || Mesh == nullptr || !Catalog.BlendTableFor)
		{
			return nullptr;
		}
		const FElysiumBlendTable* Table = Catalog.BlendTableFor(Selection.OwnerStem);
		const FElysiumBlendGrid* Grid = Table != nullptr
			? Table->Find(Selection.SequenceLabel) : nullptr;
		const FElysiumBlendCell* BaseCell = Grid != nullptr ? Grid->CellAt(0, 0) : nullptr;
		if (BaseCell == nullptr)
		{
			return nullptr;
		}
		UAnimSequence* BaseCellSequence = ElysiumNpcVisual::LoadBakedClip(Mesh, Selection.OwnerStem,
			BaseCell->Clip);
		return BaseCellSequence != nullptr
			? BaseCellSequence->FindMetaDataByClass<UElysiumAnimLayerMask>()
			: nullptr;
	}
}

void UElysiumAnimSubsystem::Deinitialize()
{
	ClipSets.Reset();
	FacialRigs.Reset();
	CompositionRigs.Reset();
	EyeSets.Reset();
	BlendTables.Reset();
	ReportedMisses.Reset();
	ReportedSlotMisses.Reset();
	Super::Deinitialize();
}

void UElysiumAnimSubsystem::ReportMiss(const FElysiumAnimationIntent& Intent,
	const FElysiumAnimationSelection& Selection)
{
	// The request as the reader would name it: the label where the route resolved one and could not
	// bind it, the activity where nothing was ever named. Keying on the resolved activity instead
	// would fold every rung of one body's ladder into a single line.
	const FString& Request = Selection.SequenceLabel.IsEmpty()
		? Intent.Activity : Selection.SequenceLabel;
	const uint32 Key = HashCombine(HashCombine(GetTypeHash(Intent.Stem), GetTypeHash(Request)),
		GetTypeHash(static_cast<uint8>(Selection.Outcome)));
	if (ReportedMisses.Contains(Key))
	{
		return;
	}
	ReportedMisses.Add(Key);

	const FString Line = FString::Printf(TEXT("[elysium] '%s' on '%s' binds no asset (%s): %s"),
		*Request, *Intent.Stem, ElysiumAnimIntent::OutcomeName(Selection.Outcome),
		Selection.Detail.IsEmpty() ? TEXT("no detail recorded") : *Selection.Detail);

	// A stem with no clip vocabulary at all is the gym pawn, a menu backdrop or an unexported model
	// — an explicitly optional absence rather than a failure, so it is stated rather than warned.
	// Everything else here is a body that will pose whatever it last held while its record names
	// something else, which is the failure this exists to make observable.
	if (Selection.Outcome == EElysiumAnimOutcome::NoVocabulary)
	{
		UE_LOG(LogElysiumAnim, Log, TEXT("%s"), *Line);
		return;
	}
	UE_LOG(LogElysiumAnim, Warning, TEXT("%s"), *Line);
}

void UElysiumAnimSubsystem::ReportGaitFanMiss(const FElysiumGaitSpeedRequest& Request,
	EElysiumAnimActivityCode Code, const TCHAR* Reason,
	const FElysiumAnimationSelection& Selection)
{
	const uint32 Key = HashCombine(HashCombine(GetTypeHash(Request.Stem),
		GetTypeHash(static_cast<uint8>(Code))), GetTypeHash(FString(Reason)));
	if (ReportedGaitMisses.Contains(Key))
	{
		return;
	}
	ReportedGaitMisses.Add(Key);

	// Retail's own answer to a gait with no 9-blend fan is an unwritten table slot, which on a fresh
	// body reads zero — so this is reported rather than substituted, and the reason string is what
	// separates a body that declares no such activity from a fan that failed to resolve.
	UE_LOG(LogElysiumAnim, Warning,
		TEXT("[elysium] '%s' resolves no %s fan (%s): that gait commands zero, exactly as retail's ")
		TEXT("unwritten table slot does (%s chain, class '%s', weapon '%s', state %d, label ")
		TEXT("'%s'@'%s')"),
		*Request.Stem, ElysiumAnimIntent::ActivityName(Code), Reason,
		ElysiumAnimIntent::BodyKindName(Request.BodyKind),
		Request.ActorClassname.IsEmpty() ? TEXT("(none)") : *Request.ActorClassname,
		Request.WeaponClassname.IsEmpty() ? TEXT("(empty hands)") : *Request.WeaponClassname,
		static_cast<int32>(Request.ActorState),
		Selection.SequenceLabel.IsEmpty() ? TEXT("(none)") : *Selection.SequenceLabel,
		Selection.OwnerStem.IsEmpty() ? TEXT("(none)") : *Selection.OwnerStem);
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

const FElysiumDispositionTable& UElysiumAnimSubsystem::Dispositions()
{
	UGameInstance* GI = GetGameInstance();
	UElysiumRulebookSubsystem* Rules = GI ? GI->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;
	// A table rather than a null: an empty one resolves nothing, which is what the callers already
	// handle, and it keeps a reachable-but-unloaded rulebook from being a crash.
	static const FElysiumDispositionTable Empty;
	return Rules != nullptr ? Rules->Dispositions() : Empty;
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
		const TMap<FString, FElysiumAnimatedPropEntry>& Props =
			Loaded.PlacedModels.IsEmpty() ? Loaded.AnimatedProps : Loaded.PlacedModels;
		for (const TPair<FString, FElysiumAnimatedPropEntry>& Prop : Props)
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
		const bool bLoaded = Table->Load(RelPath, Error) && Table->IsValid();
		// **Reported whether or not the table loaded, because neither of these makes it fail.** A
		// sidecar with an unreadable movement schema and a valid grid installs and serves poses; the
		// only symptom is that no clip of the bank can say whether it authors a lunge. Logged here,
		// at the one place that turns a path into a table, so it is said once per bank per session
		// rather than once per swing.
		if (Table->bMovementSchemaUnreadable)
		{
			UE_LOG(LogElysiumAnim, Warning,
				TEXT("blends '%s' (%s): the sidecar states a `movement_fields` schema this build "
					 "cannot address, so no clip of this bank can say whether it authors a lunge — "
					 "the READER is behind the file's column set, and re-exporting will not change "
					 "it"), *Stem, *RelPath);
		}
		if (Table->MalformedMovementRows > 0)
		{
			UE_LOG(LogElysiumAnim, Warning,
				TEXT("blends '%s' (%s): %d malformed `movement` row(s) were dropped from a table that "
					 "still installed, so the clips they belonged to author a short or empty "
					 "displacement path"),
				*Stem, *RelPath, Table->MalformedMovementRows);
		}
		if (!bLoaded)
		{
			UE_LOG(LogElysiumAnim, Warning, TEXT("blends '%s': %s"), *Stem,
				Error.IsEmpty() ? TEXT("no usable grid, binding or timeline") : *Error);
		}
		else
		{
			UE_LOG(LogElysiumAnim, Verbose,
				TEXT("blends '%s': %d grid(s), %d pose parameter(s), %d event timeline(s)"),
				*Stem, Table->Grids.Num(), Table->PoseParams.Num(), Table->Events.Num());
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

UAnimSequence* UElysiumAnimSubsystem::ResolveClip(const FString& Stem, const FString& ClipName,
	USkeletalMesh* Mesh, FString& OutError, EElysiumAnimChannel Channel)
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
	// A baked bank sequence is the same asset for every compatible body and is addressed by owner
	// and resolved animation name rather than rebuilt per mesh.
	const FString AnimName = ResolveClipAnimName(Stem, ClipName);
	if (UAnimSequence* Baked = ElysiumNpcVisual::LoadBakedClip(Mesh, Owner, AnimName))
	{
		// **A masked partial-body layer is refused for a Base-channel caller, at the door that
		// actually poses one.** `ResolveAnimation` guards the locomotion resolve; every clip a
		// producer names by hand arrives HERE instead — the melee/ranged claim path, a scripted
		// beat's `m_iszPlay`, `SetAnimation`, an NPC idle, the green room — and a masked clip posed
		// as a base pose decodes its unowned bones to a zero quaternion and a zero position, which
		// collapses the character.
		//
		// It goes through `RefuseMaskedBase` rather than testing the metadata here, so the guard, the
		// detail line and the once-per-case report are ONE implementation. That funnel reads a
		// record, so the request is stated as one: the label the caller asked for, the bank the
		// include DAG named, the animation the grid collapsed to, and the channel the caller intends
		// to pose it on. `ReportMiss` keys on (stem, request, outcome), so a body that keeps asking
		// warns once.
		FElysiumAnimationIntent Intent;
		Intent.Stem = Stem;
		Intent.SequenceLabel = ClipName;
		Intent.Route = EElysiumAnimRoute::ExactLabel;
		Intent.Channel = Channel;

		FElysiumAnimationSelection Selection;
		Selection.Stem = Stem;
		Selection.Channel = Channel;
		Selection.Route = EElysiumAnimRoute::ExactLabel;
		Selection.SequenceLabel = ClipName;
		Selection.AnimationName = AnimName;
		Selection.OwnerStem = Owner;
		Selection.AssetKind = EElysiumAnimAssetKind::Sequence;
		Selection.Outcome = EElysiumAnimOutcome::Resolved;

		FElysiumResolvedAnimation Assets;
		Assets.Sequence = Baked;
		// **The catalog is built only where the refusal can fire.** `RefuseMaskedBase` returns on the
		// channel rule before it reads a thing, and the catalog it would have been handed allocates a
		// `TFunction` for the blend-table lookup — on every successful clip resolve in the game,
		// including every layer-channel call, which is the one this door exists to let through.
		if (!ElysiumAnimIntent::MaskedClipPlayableOn(Channel)
			&& RefuseMaskedBase(Intent, BuildCatalog(Stem), Mesh, Selection, Assets))
		{
			OutError = Selection.Detail;
			return nullptr;
		}
		return Baked;
	}

	// The mount is the only build of a clip, so this fails by name. There is no runtime format
	// decoder and no alternate transform path.
	// `uv run elysium verify characters` holds the mount to every clip a vocabulary can name.
	OutError = FString::Printf(
		TEXT("'%s'@'%s' is not on the baked mount -- run: uv run elysium export characters"),
		*AnimName, *Owner);
	return nullptr;
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
	USkeletalMesh* Mesh, FElysiumResolvedGrid& OutGrid, const FString& Host,
	FString* OutError, FString* OutArmed)
{
	OutGrid = FElysiumResolvedGrid();
	auto Fail = [OutError, OutArmed](FString&& Why) -> bool
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

	// Same ownership rule as every other resolver here: a grid is declared by whoever owns the
	// animations, which is a bank for anything but a dialogue clip.
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	const FElysiumNpcClip* Clip = Set != nullptr ? Set->Find(ClipName) : nullptr;
	if (Clip == nullptr)
	{
		return Fail(FString::Printf(
			TEXT("'%s' is not in %s's vocabulary (%d clips)"), *ClipName, *Stem,
			Set != nullptr ? Set->Clips.Num() : 0));
	}
	const FString Owner = Clip->IsOwnedBy(Stem) ? Stem : Clip->Owner;

	const TSharedPtr<const FElysiumBlendTable> Table = GetBlendTable(Owner);
	const FElysiumBlendGrid* Grid = Table.IsValid() ? Table->Find(ClipName) : nullptr;
	if (Grid == nullptr)
	{
		// Not a defect: most labels name one animation and declare no grid at all.
		return Fail(FString::Printf(TEXT("'%s' does not name a blend grid"), *ClipName));
	}

	// Derived form first when a host is known, then the standalone label. A layer grid ships only
	// as `<label>@<host>`; asking for the bare name is how 299 of 527 spaces used to look missing.
	UBlendSpace* Space = nullptr;
	ElysiumAnimResolve::ELayerAssetForm Form = ElysiumAnimResolve::ELayerAssetForm::None;
	if (!Host.IsEmpty())
	{
		Space = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, Owner, ClipName, Host);
		if (Space != nullptr)
		{
			Form = ElysiumAnimResolve::ELayerAssetForm::DerivedGrid;
		}
	}
	if (Space == nullptr)
	{
		Space = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, Owner, ClipName);
		if (Space != nullptr)
		{
			Form = ElysiumAnimResolve::ELayerAssetForm::PlainGrid;
		}
	}
	if (Space == nullptr)
	{
		return Fail(ElysiumAnimResolve::DescribeLayerAssetMiss(ClipName, Owner, Host, Table.Get()));
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
	if (OutArmed != nullptr)
	{
		*OutArmed = ElysiumAnimResolve::DescribeLayerArmedForm(Form, ClipName, Host);
	}
	return true;
}

FElysiumAnimationCatalog UElysiumAnimSubsystem::BuildCatalog(const FString& Stem)
{
	FElysiumAnimationCatalog Catalog;
	Catalog.Clips = GetClipSet(Stem);
	Catalog.PropClips = Stem.IsEmpty() ? nullptr : GetIndex().PlacedModels.Find(Stem);
	if (Catalog.PropClips == nullptr && !Stem.IsEmpty())
	{
		Catalog.PropClips = GetIndex().AnimatedProps.Find(Stem);
	}
	// The owning bank is not known until the weighted pick has run, so the table arrives as a lookup
	// rather than as a preloaded map. The shared pointer lives in this subsystem's session-lifetime
	// cache, so the raw pointer outlives every resolve that reads it.
	Catalog.BlendTableFor = [this](const FString& Owner) -> const FElysiumBlendTable*
	{
		return GetBlendTable(Owner).Get();
	};
	return Catalog;
}

void UElysiumAnimSubsystem::ResolveSlotDeclaredAssets(const FString& OwnerStem,
	const FString& Label, USkeletalMesh* Mesh, UBlendSpace*& OutAimSpace, FName& OutAimMaskName,
	UAnimSequence*& OutAdditive)
{
	OutAimSpace = nullptr;
	OutAimMaskName = NAME_None;
	OutAdditive = nullptr;
	if (Mesh == nullptr)
	{
		return;
	}

	// One warning per (host, layer, reason), through the same latch the slot resolution uses: this
	// seam is re-entered every publish and per trigger pull, and each fault it can name is a bake
	// property that does not change between frames.
	auto ReportOnce = [this, &OwnerStem, &Label](const TCHAR* Reason, const FString& Line)
	{
		const uint32 Key = HashCombine(HashCombine(GetTypeHash(OwnerStem), GetTypeHash(Label)),
			GetTypeHash(FString(Reason)));
		if (ReportedSlotMisses.Contains(Key))
		{
			return;
		}
		ReportedSlotMisses.Add(Key);
		UE_LOG(LogElysiumAnim, Warning, TEXT("[elysium] slot layer %s"), *Line);
	};

	const TSharedPtr<const FElysiumBlendTable> Table = GetBlendTable(OwnerStem);
	const FElysiumAutoLayerBinding* Declared = Table.IsValid()
		? Table->FindAutoLayers(Label) : nullptr;
	if (Declared == nullptr)
	{
		// An ordinary absence: a clip that declares no layers — every reload layer — composes bare.
		return;
	}

	for (const FString& DeclaredLayer : Declared->Clips)
	{
		const FElysiumBlendGrid* Grid = Table->Find(DeclaredLayer);
		if (Grid == nullptr || !Grid->IsMultiCell())
		{
			// Not a grid, so a sequence — and whether it is the `_delta` additive is asked of the
			// ASSET rather than a clip catalog: clip sets are per-NPC while this owner is a bank, so
			// a catalog lookup here answers nothing for every shared layer. The derived
			// `<layer>@<host>` form is composed against this host's own pose; the raw form is the
			// fallback for the containers that still carry one.
			//
			// **A resolved grid stands the additive DOWN.** The bake folds a masked host's motion
			// additives into the grid's own cells — a delta's meaning depends on the pose it rides,
			// so per-cell composition is the only place it can be right for every aim direction —
			// and composing the delta again here would apply the action twice.
			if (OutAdditive != nullptr || OutAimSpace != nullptr)
			{
				continue;
			}
			UAnimSequence* Loaded = ElysiumNpcVisual::LoadBakedClip(Mesh, OwnerStem,
				FString::Printf(TEXT("%s@%s"), *DeclaredLayer, *Label));
			if (Loaded == nullptr)
			{
				Loaded = ElysiumNpcVisual::LoadBakedClip(Mesh, OwnerStem, DeclaredLayer);
			}
			if (Loaded == nullptr)
			{
				ReportOnce(TEXT("nolayerasset"), FString::Printf(
					TEXT("'%s' declares the layer '%s' but no baked asset answers for it on this "
						 "body, so the shot composes without it"),
					*Label, *DeclaredLayer));
			}
			else if (Loaded->IsValidAdditive())
			{
				OutAdditive = Loaded;
			}
			else
			{
				// A declared non-grid, non-additive overlay — nothing a slot clip ships today. Said
				// out loud rather than composed wrong or dropped silently.
				ReportOnce(TEXT("layerkind"), FString::Printf(
					TEXT("'%s' declares '%s', which is neither a grid nor an additive; the slot "
						 "composes without it"),
					*Label, *DeclaredLayer));
			}
			continue;
		}
		if (OutAimSpace != nullptr)
		{
			continue;
		}
		OutAimSpace = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, OwnerStem, DeclaredLayer, Label);
		if (OutAimSpace == nullptr)
		{
			ReportOnce(TEXT("nogrid"), FString::Printf(
				TEXT("'%s' declares the aim grid '%s' but no composed asset for it exists on this "
					 "body, so the shot poses aim-neutral"),
				*Label, *DeclaredLayer));
			continue;
		}
		// The grid's own mask, read off its base cell exactly as the base channel's aim grid reads
		// it — derived name first, because a cell of an autolayer grid ships only as `<cell>@<host>`.
		if (const FElysiumBlendCell* BaseCell = Grid->CellAt(0, 0))
		{
			UAnimSequence* BaseCellSequence = ElysiumNpcVisual::LoadBakedClip(Mesh, OwnerStem,
				FString::Printf(TEXT("%s@%s"), *BaseCell->Clip, *Label));
			if (BaseCellSequence == nullptr)
			{
				BaseCellSequence = ElysiumNpcVisual::LoadBakedClip(Mesh, OwnerStem, BaseCell->Clip);
			}
			const UElysiumAnimLayerMask* Mask = BaseCellSequence != nullptr
				? BaseCellSequence->FindMetaDataByClass<UElysiumAnimLayerMask>() : nullptr;
			if (Mask != nullptr)
			{
				OutAimMaskName = Mask->Profile;
			}
			else
			{
				// An unmasked grid composes at zero weight on every bone — standing, steerable, and
				// invisible — so it is refused with its grid rather than composed silently.
				OutAimSpace = nullptr;
				ReportOnce(TEXT("nogridmask"), FString::Printf(
					TEXT("'%s' grid '%s': its base cell '%s' carries no bone mask, so the aim layer "
						 "would compose at zero weight and is refused"),
					*Label, *DeclaredLayer, *BaseCell->Clip));
			}
		}
	}
}

void UElysiumAnimSubsystem::ResolveSlotLayer(const FElysiumAnimationRequest& Claim,
	const FString& Stem, USkeletalMesh* Mesh, FElysiumAnimationSelection& OutSelection,
	FElysiumResolvedAnimation& OutAssets)
{
	if (Claim.Label.IsEmpty())
	{
		// A claim that names no clip is a claim on the channel and nothing else — a producer holding
		// the slot open. There is no layer to load and no miss to report.
		return;
	}
	// The label is published whether or not it binds, for the same reason the base record's is: a
	// layer that resolved nothing has to read as a named miss rather than as a body that is not
	// layering at all.
	OutSelection.SlotLabel = Claim.Label;

	// The owner column is the include DAG's own answer and is never re-derived — the same rule the
	// base resolution and every clip seam take. It is read even when the load below fails, so the
	// record names the bank that was asked.
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	if (const FElysiumNpcClip* Clip = Set != nullptr ? Set->Find(Claim.Label) : nullptr)
	{
		OutSelection.SlotOwnerStem = Clip->IsOwnedBy(Stem) ? Stem : Clip->Owner;
	}

	if (Mesh == nullptr)
	{
		// The ordinary answer before a body has a skeleton to bind against. Not reported, exactly as
		// the base path's own mesh-less rung is not.
		return;
	}

	auto ReportOnce = [this, &Stem, &Claim](const TCHAR* Reason, const FString& Line)
	{
		const uint32 Key = HashCombine(HashCombine(GetTypeHash(Stem), GetTypeHash(Claim.Label)),
			GetTypeHash(FString(Reason)));
		if (ReportedSlotMisses.Contains(Key))
		{
			return;
		}
		ReportedSlotMisses.Add(Key);
		UE_LOG(LogElysiumAnim, Warning, TEXT("[elysium] overlay layer %s"), *Line);
	};

	FString Error;
	// The CLAIM's own channel, which is the slot's: a masked clip on a layer channel is exactly what
	// the mask exists for, and asking through the base-channel default would refuse every layer this
	// function exists to load.
	UAnimSequence* Layer = ResolveClip(Stem, Claim.Label, Mesh, Error, Claim.Channel);
	if (Layer == nullptr)
	{
		// A layer the body cannot bind composes nothing, so the base pose stands alone while the
		// producer believes it armed one — the failure this report exists to make observable.
		ReportOnce(TEXT("noasset"), FString::Printf(TEXT("'%s' on '%s' binds no asset: %s"),
			*Claim.Label, *Stem, *Error));
		return;
	}
	OutAssets.SlotSequence = Layer;

	// **The layers this clip itself declares, resolved the way every host's are.** Retail's autolayer
	// rule is recursive: the sequence in the overlay slot composes with its OWN declared layers,
	// exactly as the base does — the shot motion, then its aim grid over it, then its `_delta`
	// additive. The slot therefore carries the same trio the base channel does, resolved by the same
	// rule from the same table, rather than a special case per symptom.
	{
		const FString SlotOwner = OutSelection.SlotOwnerStem.IsEmpty()
			? Stem : OutSelection.SlotOwnerStem;
		ResolveSlotDeclaredAssets(SlotOwner, Claim.Label, Mesh, OutAssets.SlotSpace,
			OutAssets.SlotAimMaskName, OutAssets.SlotAdditive);
	}

	// The mask is the whole difference between a partial-body layer and a full-body replacement, and
	// it lives on the asset because only the bake can answer it (`UElysiumAnimLayerMask`). A layer
	// that carries none would compose over every bone, which is the opposite of what an overlay is.
	if (const UElysiumAnimLayerMask* Mask = Layer->FindMetaDataByClass<UElysiumAnimLayerMask>())
	{
		OutAssets.SlotMaskName = Mask->Profile;
	}
	else
	{
		// The body's own stem where the clip-set lookup above named no bank: a label the vocabulary
		// does not carry leaves `SlotOwnerStem` empty, and a report reading `'label'@''` names nothing
		// a reader could go and look at.
		const FString& Owner = OutSelection.SlotOwnerStem.IsEmpty()
			? Stem : OutSelection.SlotOwnerStem;
		ReportOnce(TEXT("nomask"), FString::Printf(
			TEXT("'%s'@'%s' carries no baked bone mask, so it would own the whole rig rather than "
				 "composing over the base pose"),
			*Claim.Label, *Owner));
	}

	// **Neither the envelope nor the rate is copied out beside the asset.** Both are already the
	// claim's: `ElysiumAnimIntent::SlotWeightAt` rides the envelope over the claim's own phase into
	// the record's `SlotWeight`, and the rate is exactly what `ClaimForSegment` divided the clip's
	// authored length by to get the duration that phase is read against. A copy here would be a
	// second place the layer's timing could be stated from, and nothing keeps two of them in step.
}

bool UElysiumAnimSubsystem::RefuseMaskedBase(const FElysiumAnimationIntent& Intent,
	const FElysiumAnimationCatalog& Catalog, USkeletalMesh* Mesh,
	FElysiumAnimationSelection& OutSelection, FElysiumResolvedAnimation& OutAssets)
{
	if (ElysiumAnimIntent::MaskedClipPlayableOn(OutSelection.Channel))
	{
		// A masked clip asked for on a LAYER channel is the whole point of the mask. Only the base
		// pose is the illegal home for one, so every other channel passes through untouched.
		return false;
	}
	const UElysiumAnimLayerMask* Mask = BaseLayerMask(Catalog, OutSelection, Mesh, OutAssets);
	if (Mask == nullptr)
	{
		return false;
	}

	// Refused, not posed. The masked bones decode to a zero quaternion and a zero position, so a body
	// handed this as its base pose collapses — and the record has to say which of the two illegal
	// base clips this was, because "additive" and "partial-body layer" reach the base channel through
	// different producers and are fixed in different places.
	OutSelection.bMasked = true;
	OutSelection.AssetKind = EElysiumAnimAssetKind::None;
	OutSelection.Outcome = EElysiumAnimOutcome::LayerMaskRejected;
	OutSelection.Detail = FString::Printf(
		TEXT("'%s'@'%s' is a partial-body layer (bone mask '%s', %d bones) asked for on the %s channel "
			 "of '%s', and a masked clip can never own the base pose"),
		*OutSelection.SequenceLabel, *OutSelection.OwnerStem, *Mask->Profile.ToString(),
		Mask->OwnedBones, ElysiumAnimIntent::ChannelName(OutSelection.Channel), *Intent.Stem);
	OutAssets.Sequence = nullptr;
	OutAssets.Space = nullptr;
	// Through the ordinary miss funnel, which is already keyed on (stem, request, outcome) — so this
	// warns once per distinct case rather than once per frame for a body that keeps asking.
	ReportMiss(Intent, OutSelection);
	return true;
}

void UElysiumAnimSubsystem::ResolveAnimation(const FElysiumAnimationIntent& Intent,
	USkeletalMesh* Mesh, FElysiumAnimationSelection& OutSelection,
	FElysiumResolvedAnimation& OutAssets, const FElysiumAnimationRequest* SlotClaim)
{
	OutAssets = FElysiumResolvedAnimation();

	// The record comes out of the sidecars and is always producible. The asset needs a skeleton, and
	// that is a separate question with a separate answer. Kept rather than re-built below: CCC10's
	// layer resolution reads the same catalog the primary asset resolved against.
	const FElysiumAnimationCatalog Catalog = BuildCatalog(Intent.Stem);
	ElysiumAnimResolve::Resolve(Intent, Catalog, OutSelection);

	// **The overlay slot is resolved ahead of every base rung, because it does not depend on one.**
	// A layer composes over whatever the base turned out to be — including a base that resolved
	// nothing at all — so a slot resolved after the base's own early returns would silently stop
	// layering on exactly the bodies whose base pose already missed. `Resolve` above rebuilt the
	// record from scratch, which is why the slot's fields are written after it rather than before.
	if (SlotClaim != nullptr)
	{
		ResolveSlotLayer(*SlotClaim, Intent.Stem, Mesh, OutSelection, OutAssets);
	}

	if (OutSelection.Outcome == EElysiumAnimOutcome::GridStateRefused)
	{
		ReportMiss(Intent, OutSelection);
		return;
	}
	// **The record naming an asset is what binds it, not the outcome being `Resolved`.** A fallback
	// rung selected a real clip and then restated the outcome as the rung that answered, so gating
	// the load on `Resolved` left every stalker, every availability fallback and every ACT_RUN that
	// retried as a walk holding a pose nothing had selected while its record named the clip it was
	// supposed to be playing.
	if (!OutSelection.NamesBaseAsset())
	{
		ReportMiss(Intent, OutSelection);
		return;
	}

	if (Mesh == nullptr)
	{
		OutSelection.Outcome = EElysiumAnimOutcome::NoAsset;
		OutSelection.Detail = FString::Printf(
			TEXT("'%s' resolved to '%s'@'%s', but the body has no skeletal mesh to bind against yet"),
			*OutSelection.SequenceLabel, *OutSelection.AnimationName, *OutSelection.OwnerStem);
		// Not reported: a body resolves before its mesh is installed as an ordinary part of standing
		// up, and the next resolve after the mesh arrives is the one whose answer matters.
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
			if (RefuseMaskedBase(Intent, Catalog, Mesh, OutSelection, OutAssets))
			{
				// Nothing to compose onto: the base was refused rather than resolved, so the host's
				// own autolayers have no pose to overwrite bones on.
				return;
			}
			ResolveLayerAssets(Catalog, OutSelection, Mesh, OutAssets);
			return;
		}
		// A fan the bake has not covered still names its current cell, so the body can stand that one
		// clip rather than nothing — and the record already says which cell it is.
		OutSelection.AssetKind = EElysiumAnimAssetKind::Sequence;
	}

	// Addressed by owner and resolved animation name, matching what the record says rather than
	// re-resolving the label at the neutral pose.
	OutAssets.Sequence = ElysiumNpcVisual::LoadBakedClip(Mesh, OutSelection.OwnerStem,
		OutSelection.AnimationName);
	if (OutAssets.Sequence == nullptr)
	{
		OutSelection.AssetKind = EElysiumAnimAssetKind::None;
		OutSelection.Outcome = EElysiumAnimOutcome::NoAsset;
		OutSelection.Detail = FString::Printf(TEXT("'%s'@'%s' is not on the baked mount"),
			*OutSelection.AnimationName, *OutSelection.OwnerStem);
		// A named clip the mount does not carry, on a body that has its mesh: a bake gap rather than
		// a timing one, and the only warning that separates the two.
		ReportMiss(Intent, OutSelection);
	}
	else if (RefuseMaskedBase(Intent, Catalog, Mesh, OutSelection, OutAssets))
	{
		// Nothing to compose onto: the base was refused rather than resolved.
		return;
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
		Intent.WeaponClassname = Request.WeaponClassname;
		Intent.FormTag = Request.FormTag;
		// **The same chain the pose walks** (LIFE3). The body kind selects the pre-translation body,
		// the classname finds the recovered `+0x5dc`/`+0x5e0` class rows and the state picks the
		// alert or the relaxed set — so a cast body's speeds come off the sequences that body is
		// actually about to play rather than off the player fan the request happens to name.
		Intent.Source = Request.Source;
		Intent.BodyKind = Request.BodyKind;
		Intent.ActorClassname = Request.ActorClassname;
		Intent.ActorState = Request.ActorState;
		// A gait that resolves through the fallback ladder is not that gait. Reaching `walk` for a
		// missing `sneak` and then calling its speeds the sneak table is exactly the silent
		// substitution the record exists to prevent, and here it would also make the body move at
		// walking pace while playing a crouch.
		Intent.bAllowFallbackLadder = false;

		// Every way out of here is a gait that will command zero while its record names a cell, so
		// every one of them says so once rather than returning a quiet false into a caller that
		// discards it.
		FElysiumAnimationSelection Selection;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Selection);
		if (Selection.SequenceLabel.IsEmpty() || Selection.OwnerStem.IsEmpty())
		{
			ReportGaitFanMiss(Request, Code,
				TEXT("the body declares no such activity — an authored absence, not a defect"),
				Selection);
			return false;
		}
		// The fan belongs to the bank the weighted pick landed in, not to the body — one body's walk
		// and its run routinely come from different banks.
		const TSharedPtr<const FElysiumBlendTable> Owner = GetBlendTable(Selection.OwnerStem);
		if (!Owner.IsValid())
		{
			ReportGaitFanMiss(Request, Code, TEXT("the owning bank carries no blend table"),
				Selection);
			return false;
		}
		const FElysiumBlendGrid* Grid = Owner->Find(Selection.SequenceLabel);
		if (Grid == nullptr)
		{
			ReportGaitFanMiss(Request, Code, TEXT("the blend table carries no grid for that label"),
				Selection);
			return false;
		}
		if (!ElysiumBlendGrids::SpeedFan(*Grid, *Owner, Scale, Table))
		{
			ReportGaitFanMiss(Request, Code, TEXT("the grid produced no speed fan"), Selection);
			return false;
		}
		return true;
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

bool UElysiumAnimSubsystem::ResolveActivityClip(const FElysiumActivityClipRequest& Request,
	FElysiumActivityClip& Out)
{
	Out = FElysiumActivityClip();

	// Expressed over the one resolver rather than beside it: two implementations of one weighted pick
	// and one owner rule are exactly how the player path and the cast path come to disagree about a
	// bank with nothing reporting it. The request-to-intent mapping is pure and lives with the
	// resolver, so what this adapter forwards is asserted without a subsystem behind it.
	const FElysiumAnimationIntent Intent = ElysiumAnimResolve::ActivityIntentFor(Request);

	// Kept rather than built inline: the reach below is read back off the same vocabulary the pick
	// ran against, and re-fetching it would be a second lookup of the one clip set.
	const FElysiumAnimationCatalog Catalog = BuildCatalog(Request.Stem);
	FElysiumAnimationSelection Selection;
	ElysiumAnimResolve::Resolve(Intent, Catalog, Selection);
	if (Selection.SequenceLabel.IsEmpty() || Selection.AnimationName.IsEmpty())
	{
		// The record's own named miss, in words, with the classification the request selected through
		// — the same four facts the gait-fan miss renders, because "ACT_WALK missed on jack" is a
		// different report depending on which class body, weapon ladder and alert branch ran.
		//
		// Verbose rather than a warning because a body whose vocabulary carries no sequence for an
		// activity is asked again every time its schedule comes round, and one warning per ask would
		// bury the load it belongs to; the caller's fallback is the behaviour, and this line is why it
		// was taken.
		UE_LOG(LogElysiumAnim, Verbose,
			TEXT("'%s' resolved no clip for %s (%s): %s (%s chain, class '%s', weapon '%s', state %d)"),
			*Request.Stem, *Request.Activity, ElysiumAnimIntent::OutcomeName(Selection.Outcome),
			Selection.Detail.IsEmpty() ? TEXT("no detail recorded") : *Selection.Detail,
			ElysiumAnimIntent::BodyKindName(Request.BodyKind),
			Request.ActorClassname.IsEmpty() ? TEXT("(none)") : *Request.ActorClassname,
			Request.WeaponClassname.IsEmpty() ? TEXT("(empty hands)") : *Request.WeaponClassname,
			static_cast<int32>(Request.ActorState));
		return false;
	}

	Out.Label = Selection.SequenceLabel;
	Out.AnimationName = Selection.AnimationName;
	// The include DAG's own answer, carried out with the cell. A caller that re-derived the owner from
	// the label would re-resolve the grid at neutral pose parameters and collapse a directional fan
	// onto its forward cell.
	Out.OwnerStem = Selection.OwnerStem;
	Out.GroundSpeedCmPerSecond = Selection.GroundSpeedCmPerSecond;
	Out.bLooping = Selection.bLooping;
	// The selected row's own authored fade, already reduced to 0 by `FadeSeconds()` on a hard cut.
	// A producer that blends the clip in reads it here rather than re-finding the clip.
	Out.FadeSeconds = Selection.FadeSeconds;
	// The activity's reach, asked over the TRANSLATED activity rather than the request's: a melee
	// swing acquires at the maximum over every sequence the activity the vocabulary was actually
	// searched for returns, and asking under the pre-translation name would answer 0 for every
	// weapon-translated attack.
	Out.MaxReachCm = Catalog.Clips
		? Catalog.Clips->MaxReachCmForActivity(Selection.ResolvedActivity) : 0.0f;
	// The recovered restart rule, asked over the UNTRANSLATED request — retail picks the route at the
	// task, before `ActivityOverride` and the class bodies rename anything, so asking under the
	// resolved name would answer false for every weapon-translated attack.
	Out.bRestart = ElysiumActionTables::ActivityRestartsIdenticalRequest(Request.Activity);
	// LIFE5 — the fan half, carried out so a producer can route a directional reaction without
	// re-resolving anything. The axis value is read off the SELECTION rather than off the request: the
	// grid states which pose parameter it binds, and a producer's `HitYaw` is the answer only for a
	// fan that binds `hit_yaw`.
	Out.bGrid = Selection.AssetKind == EElysiumAnimAssetKind::BlendSpace;
	Out.AxisValue = Selection.AxisValue[0];
	Out.NextAnimationName = Selection.NextAnimationName;
	Out.AxisFraction = Selection.AxisFraction[0];
	return true;
}

FString UElysiumAnimSubsystem::ResolveNearestGridClip(const FString& OwnerStem, const FString& Label,
	float AxisValue)
{
	const TSharedPtr<const FElysiumBlendTable> Table = GetBlendTable(OwnerStem);
	const FElysiumBlendGrid* Grid = Table.IsValid() ? Table->Find(Label) : nullptr;
	if (Grid == nullptr)
	{
		// Not a defect: most labels name one animation and declare no grid to collapse.
		return FString();
	}
	// The axis the GRID binds, under its own declared name — the same binding `SelectCell` reads, so a
	// fan bound to a sidecar's second pose parameter is steered by the value it was handed rather than
	// by a name this caller guessed.
	FElysiumPoseParams Pose;
	if (const FElysiumPoseParamDesc* Desc = Table->Param(Grid->ParamIndex[0]))
	{
		Pose.Set(Desc->Name, AxisValue);
	}
	const FElysiumBlendPick Nearer = ElysiumBlendGrids::NearerCell(
		ElysiumBlendGrids::SelectCell(*Grid, *Table, Pose), *Grid);
	return Nearer.Cell != nullptr ? Nearer.Cell->Clip : FString();
}

bool UElysiumAnimSubsystem::ResolveSequenceClip(const FString& Stem, const FString& ClipName,
	EElysiumAnimBodyKind BodyKind, FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	OutAnimName.Reset();
	OutGroundSpeedCmPerSecond = 0.f;

	// Same one resolver as ResolveActivityClip, over the exact-label route instead of activity
	// choice: ClipName already names one clip, so there is nothing to weigh-pick and no translation
	// table to run it through.
	FElysiumAnimationIntent Intent;
	Intent.Stem = Stem;
	Intent.SequenceLabel = ClipName;
	Intent.Route = EElysiumAnimRoute::ExactLabel;
	Intent.Source = EElysiumAnimSource::Npc;
	// Threaded through for the same reason as `ResolveActivityClip`. The exact-label route runs no
	// translation, so nothing forks on it here — but the record it produces names the chain, and two
	// adapters that answered that differently would be two answers for one body.
	Intent.BodyKind = BodyKind;
	// Same reason as ResolveActivityClip: this adapter's caller reads the miss and keeps its own
	// speed, so the fallback ladder does not run underneath it.
	Intent.bAllowFallbackLadder = false;

	FElysiumAnimationSelection Selection;
	ElysiumAnimResolve::Resolve(Intent, BuildCatalog(Stem), Selection);
	if (Selection.SequenceLabel.IsEmpty() || Selection.AnimationName.IsEmpty())
	{
		return false;
	}

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
	// A cinematic bank is named by a scene rather than by any vocabulary, so the bake enumerates it
	// separately into the shared bank namespace.
	// `uv run elysium verify characters` holds the mount to every bank a scene can name.
	OutError = FString::Printf(
		TEXT("'%s'@'%s' is not on the baked mount -- run: uv run elysium export characters"),
		*AnimName, *BankStem);
	return nullptr;
}

TArray<FString> UElysiumAnimSubsystem::IdleCandidates(const FString& Stem,
	const FString& Disposition, EElysiumIdleTier& OutTier, int32 DispositionLevel)
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
	//    This is a listing of what the body carries, not an addressable table: the stance index is
	//    resolved against `ResolveStanceClips` instead, which fills the holes the way precache does.
	//    Here it only has to answer whether this body has a stance set at all.
	const FString AnimName = Dispositions().AnimNameFor(Disposition, DispositionLevel);
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
	EElysiumIdleTier& OutTier, int32 Variant, int32 DispositionLevel)
{
	const TArray<FString> Candidates = IdleCandidates(
		Stem, Disposition, OutTier, DispositionLevel);
	if (Candidates.IsEmpty())
	{
		return FString();
	}
	// An ACT_IDLE or loose set is not interchangeable — `idle01` carries weight 30 against three
	// fidgets at 1 — so index 0 is the resting pick and the variant has nothing to address.
	if (OutTier != EElysiumIdleTier::Stance)
	{
		return Candidates[0];
	}
	// The stance tier is addressed, not chosen: the variant is `m_CurrStance`, and it names a cell
	// of the disposition's table. Reading it out of a weight-sorted list of whatever clips happen to
	// exist would make stance 1 mean "the second-heaviest idle" — which is a different clip from
	// `Idle_2` on any body whose stance idles carry unequal weights, and drifts a restored save onto
	// a pose the index never meant.
	FElysiumStanceClips Clips;
	if (!ResolveStanceClips(
		Stem, Dispositions().AnimNameFor(Disposition, DispositionLevel), Clips))
	{
		return Candidates[0];
	}
	return Clips.Idle[FMath::Clamp(Variant, 0, ElysiumStance::Count - 1)];
}

bool UElysiumAnimSubsystem::ResolveStanceClips(const FString& Stem, const FString& AnimName,
	FElysiumStanceClips& OutClips)
{
	OutClips = FElysiumStanceClips();
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	if (Set == nullptr || AnimName.IsEmpty())
	{
		return false;
	}

	// The labels are built rather than searched. `StanceClips` answers "which stance clips does this
	// body have", which is the wrong question here: the machine addresses an exact index, and a body
	// that authored `Idle_1` and `Idle_3` must put `Idle_3` at index 2 rather than at whatever
	// position a filtered list happens to give it. Missing entries stay empty and the fallback ladder
	// below resolves them, once, exactly as retail's precache does.
	for (int32 Slot = 0; Slot < ElysiumStance::Count; ++Slot)
	{
		const FString Idle = FString::Printf(TEXT("Stance_%s_Idle_%d"), *AnimName, Slot + 1);
		if (Set->Find(Idle) != nullptr)
		{
			OutClips.Idle[Slot] = Idle;
		}
		const FString Fidget = FString::Printf(TEXT("Stance_%s_Fidget_%d"), *AnimName, Slot + 1);
		if (Set->Find(Fidget) != nullptr)
		{
			OutClips.Fidget[Slot] = Fidget;
		}
	}
	for (int32 From = 0; From < ElysiumStance::Count; ++From)
	{
		for (int32 To = 0; To < ElysiumStance::Count; ++To)
		{
			const FString Trans = FString::Printf(TEXT("Stance_%s_Trans_%d_%d"),
				*AnimName, From + 1, To + 1);
			if (Set->Find(Trans) != nullptr)
			{
				OutClips.Trans[From][To] = Trans;
			}
		}
	}

	ElysiumStance::ApplyPrecacheFallbacks(OutClips);
	return OutClips.IsValid();
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
