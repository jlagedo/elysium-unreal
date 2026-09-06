#include "Visual/ElysiumAnimSubsystem.h"

#include "ElysiumContentPaths.h"
#include "ElysiumCharacterProvenance.h"
#include "Visual/ElysiumNativeAnimationData.h"
#include "ElysiumBodyData.h"
#include "ElysiumClipData.h"
#include "ElysiumMoveSolve.h"          // the sv_*scale constants the gait tables are built with
#include "ElysiumStanceTypes.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Visual/ElysiumActionTables.h"   // the recovered task routes the restart rule reads
#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumAnimPostAdditive.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/GameInstance.h"
#include "Animation/BlendSpace.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAnim, Log, All);

UElysiumNativeAnimationData* UElysiumAnimSubsystem::NativeData() const
{
	auto* Game = GetGameInstance();
	return Game ? Game->GetSubsystem<UElysiumNativeAnimationData>() : nullptr;
}

const USkeletalMesh* UElysiumAnimSubsystem::PreparedRigMesh(const FString& Model, const USkeletalMesh* Mesh) const
{
	if (Mesh) return Mesh;
	auto* Native = NativeData();
	FString Error;
	return Native ? Native->Mesh(Model, Error) : nullptr;
}

TSharedPtr<FStreamableHandle> UElysiumAnimSubsystem::PrepareNativeModel(const FString& ModelId, FString& OutError)
{
	return GetGameInstance()->GetSubsystem<UElysiumNativeAnimationData>()->Prepare(ModelId,OutError);
}

const UElysiumClipData* UElysiumAnimSubsystem::NativeClipData(const FString& Owner,const FString& Label,
	const FString& OwnerRoot) const
{
	auto* Native = NativeData();
	return Native ? Native->ClipData(OwnerRoot.IsEmpty() ? Native->Body(Owner) : Native->CinematicBody(Owner, OwnerRoot), Label) : nullptr;
}

void UElysiumAnimSubsystem::ReleaseNativeModels()
{
	GetGameInstance()->GetSubsystem<UElysiumNativeAnimationData>()->ReleasePrepared();
}

const FElysiumNpcClip* UElysiumAnimSubsystem::ClipDescription(const FString& Model,const FString& Label)
{
	const auto* Data = NativeClipData(Model, Label);
	return Data ? &Data->Descriptor : nullptr;
}

namespace ElysiumCookedRig
{
	bool IsCookedMesh(const USkeletalMesh* Mesh)
	{
		return Mesh && (UElysiumCharacterProvenance::Find(Mesh)
			|| Mesh->GetPathName().StartsWith(TEXT("/ElysiumBaked/Models/")));
	}

	template<typename TRig>
	TSharedPtr<const TRig> Read(const USkeletalMesh* Mesh, const TCHAR* Domain,
		TRig UElysiumCharacterProvenance::*Member, bool (TRig::*Usable)() const,
		TMap<FString, TSharedPtr<const TRig>>& Cache)
	{
		const FString Key = Mesh->GetPathName();
		if (const auto* Existing = Cache.Find(Key)) return *Existing;
		TSharedPtr<const TRig> Result;
		const UElysiumCharacterProvenance* Data = UElysiumCharacterProvenance::Find(Mesh);
		if (!Data || !Data->bHasMeshData)
		{
			UE_LOG(LogElysiumAnim, Warning, TEXT("%s: cooked %s data is missing"), *Key, Domain);
		}
		else if (((Data->*Member).*Usable)())
		{
			Result = MakeShared<TRig>(Data->*Member);
		}
		Cache.Add(Key, Result);
		return Result;
	}
}

namespace
{
	// Resolve `Selection.LayerLabels` (the bake-time autolayer binding the base channel's
	// own resolved host declared) into the assets the graph's upper-body nodes read. Sorted by the
	// CLIP's own additive flag, never by declaration position: the order in `LayerLabels` is
	// composition order, not a kind tag, and exactly one shipped host (`throwing_star_midcrouch_idle`)
	// declares its additive first.
	// `ReportOnce(Reason, Key, Line)` throttles a diagnostic to one line per (reason, key) for the
	// session — the same shape `ResolveSlotLayer` uses, threaded in because this is a free function
	// and the set that remembers belongs to the subsystem.
	using FLayerReporter = TFunctionRef<void(const TCHAR*, const FString&, const FString&)>;


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
		// Every grid sample has the same mask (enforced by the bake). Its hard reference is
		// already resident, so this query needs neither a source table nor another asset load.
		if (Assets.Space != nullptr)
			for (const FBlendSample& Sample : Assets.Space->GetBlendSamples())
				if (Sample.Animation) return Sample.Animation->FindMetaDataByClass<UElysiumAnimLayerMask>();
		return nullptr;
	}
}

void UElysiumAnimSubsystem::Deinitialize()
{
	FacialRigs.Reset();
	CompositionRigs.Reset();
	BankRemaps.Reset();
	EyeSets.Reset();
	ReportedMisses.Reset();
	ReportedSlotMisses.Reset();
	Super::Deinitialize();
}

void FElysiumResolvedAnimation::AddReferencedObjects(FReferenceCollector& Collector)
{
	// `AddStableReference` rather than `AddReferencedObject`: this record lives for the whole life of
	// the driver that owns it, so the batching form is the right one, and the raw-pointer overloads
	// are deprecated because they race incremental GC.
	Collector.AddStableReference(&Sequence);
	Collector.AddStableReference(&Space);
	Collector.AddStableReference(&OverlaySequence);
	Collector.AddStableReference(&OverlaySpace);
	Collector.AddStableReference(&AdditiveSequence);
	for (FElysiumResolvedOverlaySlot& Slot : Slots)
	{
		Collector.AddStableReference(&Slot.Sequence);
		Collector.AddStableReference(&Slot.AimSpace);
		Collector.AddStableReference(&Slot.Additive);
	}
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

	// `GridFallback` is the one rung here that still binds something, so it does not get the "binds
	// no asset" sentence — it would send a reader hunting a missing clip instead of a missing fan.
	const FString Line = FString::Printf(TEXT("[elysium] '%s' on '%s' %s (%s): %s"),
		*Request, *Intent.Stem,
		Selection.Outcome == EElysiumAnimOutcome::GridFallback
			? TEXT("binds a fallback asset") : TEXT("binds no asset"),
		ElysiumAnimIntent::OutcomeName(Selection.Outcome),
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
	auto* Native = NativeData();
	return Native && !Stem.IsEmpty() ? Native->Vocabulary(Stem) : nullptr;
}

TSharedPtr<const FElysiumFacialRig> UElysiumAnimSubsystem::GetFacialRig(const FString& Stem, const USkeletalMesh* Mesh)
{
	const USkeletalMesh* Prepared = PreparedRigMesh(Stem, Mesh);
	return Prepared ? ElysiumCookedRig::Read(Prepared, TEXT("facial"), &UElysiumCharacterProvenance::Facial,
		&FElysiumFacialRig::IsValid, FacialRigs) : nullptr;
}

TSharedPtr<const FElysiumEyeSet> UElysiumAnimSubsystem::GetEyeSet(const FString& Stem, const USkeletalMesh* Mesh)
{
	const USkeletalMesh* Prepared = PreparedRigMesh(Stem, Mesh);
	return Prepared ? ElysiumCookedRig::Read(Prepared, TEXT("eye"), &UElysiumCharacterProvenance::Eyes,
		&FElysiumEyeSet::IsValid, EyeSets) : nullptr;
}

TSharedPtr<const FElysiumBlendTable> UElysiumAnimSubsystem::GetBlendTable(const FString& Stem)
{
	auto* Native = NativeData();
	return Native && !Stem.IsEmpty() ? Native->BlendTable(Stem) : nullptr;
}

FString UElysiumAnimSubsystem::ResolveGridClip(const FString& OwnerStem, const FString& Label,
	const FElysiumPoseParams& Pose, const FString& OwnerRoot)
{
	if (OwnerStem.IsEmpty() || Label.IsEmpty())
	{
		return Label;
	}
	// Nearly every label names one animation, and a model with no multi-cell sequence has no sidecar
	// at all — so the common path is a cached null and one map lookup that misses.
	TSharedPtr<const FElysiumBlendTable> Table;
	if (OwnerRoot.IsEmpty()) Table=GetBlendTable(OwnerStem);
	else
	{
		auto* Native=GetGameInstance()->GetSubsystem<UElysiumNativeAnimationData>();
		Table=Native->BlendTable(Native->CinematicBody(OwnerStem,OwnerRoot));
	}
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

TSharedPtr<const FElysiumCompositionRig> UElysiumAnimSubsystem::GetCompositionRig(
	const FString& Stem, const USkeletalMesh* Mesh)
{
	const USkeletalMesh* Prepared = PreparedRigMesh(Stem, Mesh);
	return Prepared ? ElysiumCookedRig::Read(Prepared, TEXT("procedural"), &UElysiumCharacterProvenance::Composition,
		&FElysiumCompositionRig::HasWork, CompositionRigs) : nullptr;
}

TSharedPtr<const FElysiumCompositionRig> UElysiumAnimSubsystem::GetAnimatedPropCompositionRig(
	const FString& ModelPath, const USkeletalMesh* Mesh)
{
	return GetCompositionRig(ModelPath, Mesh);
}

TSharedPtr<const FElysiumBankRemap> UElysiumAnimSubsystem::GetBankRemap(USkeletalMesh* Mesh,
	USkeleton* SourceSkeleton, FName RetargetSource)
{
	if (Mesh == nullptr || RetargetSource.IsNone())
	{
		// No retarget source is the ordinary case for a clip whose bake omitted one, or an asset
		// this closure is not actually playing off a bank sequence — nothing to correct, not a fault.
		return nullptr;
	}
	if (SourceSkeleton == nullptr)
	{
		UE_LOG(LogElysiumAnim, Warning,
			TEXT("bank remap '%s' on mesh '%s': the playing animation carries no source skeleton"),
			*RetargetSource.ToString(), *GetNameSafe(Mesh));
		return nullptr;
	}

	const FBankRemapKey Key(Mesh,
		TPair<TObjectKey<USkeleton>, FName>(SourceSkeleton, RetargetSource));
	if (const TSharedPtr<const FElysiumBankRemap>* Cached = BankRemaps.Find(Key))
	{
		return *Cached;
	}

	TSharedPtr<const FElysiumBankRemap> Result;
	const FReferencePose* Source = SourceSkeleton->AnimRetargetSources.Find(RetargetSource);
	if (Source == nullptr)
	{
		UE_LOG(LogElysiumAnim, Warning,
			TEXT("bank remap '%s' on mesh '%s': playing skeleton '%s' carries no such retarget source"),
			*RetargetSource.ToString(), *GetNameSafe(Mesh), *GetNameSafe(SourceSkeleton));
	}
	else
	{
		FElysiumBankRemap Table = FElysiumBankRemap::Build(Source->ReferencePose,
			SourceSkeleton->GetReferenceSkeleton(), Mesh->GetRefSkeleton());
		if (Table.HasWork())
		{
			UE_LOG(LogElysiumAnim, Verbose,
				TEXT("bank remap '%s' on '%s': %d translate, %d similarity"),
				*RetargetSource.ToString(), *GetNameSafe(Mesh), Table.Translate.Num(),
				Table.Similarity.Num());
			Result = MakeShared<const FElysiumBankRemap>(MoveTemp(Table));
		}
		// A body whose bind pose tracks this bank closely enough that every bone copies is the
		// ordinary case and is cached the same way a miss is — null, not an empty table nobody reads.
	}
	BankRemaps.Add(Key, Result);
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
	auto* Native = NativeData();
	UAnimSequence* Resolved = Native ? Native->Sequence(Owner, AnimName) : nullptr;
	if (UAnimSequence* Baked = Resolved)
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
	// as `<label>@<host>`; asking for the bare name misses 299 of 527 spaces.
	UBlendSpace* Space = nullptr;
	ElysiumAnimResolve::ELayerAssetForm Form = ElysiumAnimResolve::ELayerAssetForm::None;
	{
		const auto* Native=GetGameInstance()->GetSubsystem<UElysiumNativeAnimationData>();
		const auto* Body=Native->Body(Stem);
		const auto* HostRow=Body && !Host.IsEmpty()?Body->Find(Host):nullptr;
		const FElysiumBodyAnimationRef* Reference=nullptr;
		if (HostRow)
			Reference=HostRow->Layers.FindByPredicate([&](const auto& Ref){return Ref.Label.Equals(ClipName,ESearchCase::IgnoreCase);});
		if (!Reference && Body)
			if (const auto* Row=Body->Find(ClipName,Owner)) Reference=&Row->Assets;
		Space=Reference?Reference->BlendSpace.Get():nullptr;
		Form=Host.IsEmpty()?ElysiumAnimResolve::ELayerAssetForm::PlainGrid:ElysiumAnimResolve::ELayerAssetForm::DerivedGrid;
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
	Catalog.PropClips = nullptr; // Props use the same prepared BodyData vocabulary.
	Catalog.BlendTableFor = [this](const FString& Owner) -> const FElysiumBlendTable*
	{ return GetBlendTable(Owner).Get(); };
	return Catalog;
}

void UElysiumAnimSubsystem::ResolveSlotDeclaredAssets(const FString& OwnerStem,
	const FString& Label, USkeletalMesh* Mesh, TObjectPtr<UBlendSpace>& OutAimSpace, FName& OutAimMaskName,
	TObjectPtr<UAnimSequence>& OutAdditive)
{
	OutAimSpace = nullptr;
	OutAimMaskName = NAME_None;
	OutAdditive = nullptr;
		const auto* Native=GetGameInstance()->GetSubsystem<UElysiumNativeAnimationData>();
	const auto* Body=Native->Body(OwnerStem);
	const auto* Row=Body?Body->Find(Label,OwnerStem):nullptr;
	if (!Row) return;
	for (const auto& Ref : Row->Layers)
	{
		if (UBlendSpace* Space=Ref.BlendSpace.Get())
		{
			OutAimSpace=Space;
			UAnimSequence* Base=Ref.BaseCell.Get();
			const auto* Mask=Base?Base->FindMetaDataByClass<UElysiumAnimLayerMask>():nullptr;
			if (Mask) OutAimMaskName=Mask->Profile;
		}
		else if (UAnimSequence* Clip=Ref.Sequence.Get())
		{
			if (Clip->FindMetaDataByClass<UElysiumAnimPostAdditive>()) OutAdditive=Clip;
		}
	}
	return;
}

void UElysiumAnimSubsystem::ResolveSlotLayer(int32 SlotIndex, const FElysiumAnimationRequest& Claim,
	const FString& Stem, USkeletalMesh* Mesh, FElysiumAnimationSelection& OutSelection,
	FElysiumResolvedAnimation& OutAssets)
{
	if (SlotIndex < 0 || SlotIndex >= ElysiumOverlay::NumSlots)
	{
		return;
	}
	FElysiumOverlaySlotRecord& OutRecord = OutSelection.Slots[SlotIndex];
	FElysiumResolvedOverlaySlot& OutSlot = OutAssets.Slots[SlotIndex];
	if (Claim.Label.IsEmpty())
	{
		// A claim that names no clip is a claim on the channel and nothing else — a producer holding
		// the slot open. There is no layer to load and no miss to report.
		return;
	}
	// The label is published whether or not it binds, for the same reason the base record's is: a
	// layer that resolved nothing has to read as a named miss rather than as a body that is not
	// layering at all.
	OutRecord.Label = Claim.Label;
	OutRecord.Activity = Claim.Activity;

	// The owner column is the include DAG's own answer and is never re-derived — the same rule the
	// base resolution and every clip seam take. It is read even when the load below fails, so the
	// record names the bank that was asked.
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	if (const FElysiumNpcClip* Clip = Set != nullptr ? Set->Find(Claim.Label) : nullptr)
	{
		OutRecord.OwnerStem = Clip->IsOwnedBy(Stem) ? Stem : Clip->Owner;
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
	OutSlot.Sequence = Layer;

	// **The layers this clip itself declares, resolved the way every host's are.** Retail's autolayer
	// rule is recursive: the sequence in the overlay slot composes with its OWN declared layers,
	// exactly as the base does — the shot motion, then its aim grid over it, then its `_delta`
	// additive. The slot therefore carries the same trio the base channel does, resolved by the same
	// rule from the same table, rather than a special case per symptom.
	{
		const FString SlotOwner = OutRecord.OwnerStem.IsEmpty() ? Stem : OutRecord.OwnerStem;
		ResolveSlotDeclaredAssets(SlotOwner, Claim.Label, Mesh, OutSlot.AimSpace,
			OutSlot.AimMaskName, OutSlot.Additive);
	}

	// The mask is the whole difference between a partial-body layer and a full-body replacement, and
	// it lives on the asset because only the bake can answer it (`UElysiumAnimLayerMask`). A layer
	// that carries none would compose over every bone, which is the opposite of what an overlay is.
	if (const UElysiumAnimLayerMask* Mask = Layer->FindMetaDataByClass<UElysiumAnimLayerMask>())
	{
		OutSlot.MaskName = Mask->Profile;
		// Onto the record as well as the assets: every readout that shows a slot's weight has to be
		// able to say which bones that weight applies to, and the record is what those readouts see.
		OutRecord.MaskName = Mask->Profile;
	}
	else
	{
		// The body's own stem where the clip-set lookup above named no bank: a label the vocabulary
		// does not carry leaves the row's owner empty, and a report reading `'label'@''` names nothing
		// a reader could go and look at.
		const FString& Owner = OutRecord.OwnerStem.IsEmpty() ? Stem : OutRecord.OwnerStem;
		ReportOnce(TEXT("nomask"), FString::Printf(
			TEXT("'%s'@'%s' carries no baked bone mask, so it would compose at zero weight on every "
				 "bone and pose nothing"),
			*Claim.Label, *Owner));
	}

	// **Neither the envelope nor the rate is copied out beside the asset.** Both belong to the LAYER
	// (`FElysiumOverlayLayer`), which advances the one cycle its weight, its end and its phase are all
	// read against; the rate is exactly what `ClaimForSegment` divided the clip's authored length by
	// to get that cycle's length. A copy here would be a second place the layer's timing could be
	// stated from, and nothing keeps two of them in step.
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
	FElysiumResolvedAnimation& OutAssets, const FElysiumOverlayStack* Overlay)
{
	OutAssets = FElysiumResolvedAnimation();

	// The record comes out of the sidecars and is always producible. The asset needs a skeleton, and
	// that is a separate question with a separate answer. Kept rather than re-built below: the
	// layer resolution reads the same catalog the primary asset resolved against.
	const FElysiumAnimationCatalog Catalog = BuildCatalog(Intent.Stem);
	ElysiumAnimResolve::Resolve(Intent, Catalog, OutSelection);

	// **The overlay slots are resolved ahead of every base rung, because they do not depend on one.**
	// A layer composes over whatever the base turned out to be — including a base that resolved
	// nothing at all — so a slot resolved after the base's own early returns would silently stop
	// layering on exactly the bodies whose base pose already missed. `Resolve` above rebuilt the
	// record from scratch, which is why the slot rows are written after it rather than before.
	//
	// Every slot, in index order, which is composition order. A free slot resolves nothing and leaves
	// its row empty; there is no compaction, because a row's index is where the layer composes.
	if (Overlay != nullptr)
	{
		for (int32 SlotIndex = 0; SlotIndex < ElysiumOverlay::NumSlots; ++SlotIndex)
		{
			if (const FElysiumOverlayLayer* Layer = Overlay->LiveLayer(SlotIndex))
			{
				ResolveSlotLayer(SlotIndex, Layer->Request, Intent.Stem, Mesh, OutSelection, OutAssets);
			}
		}
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

		const auto* Native=GetGameInstance()->GetSubsystem<UElysiumNativeAnimationData>();
	const auto* Body=Native->Body(Intent.Stem);
	const auto* Row=Body?Body->Find(OutSelection.SequenceLabel,OutSelection.OwnerStem):nullptr;
	if (!Row)
	{
		OutSelection.Outcome=EElysiumAnimOutcome::NoAsset;
		OutSelection.Detail=TEXT("prepared body data has no resolved sequence row"); ReportMiss(Intent,OutSelection); return;
	}
	if (OutSelection.AssetKind==EElysiumAnimAssetKind::BlendSpace) OutAssets.Space=Row->Assets.BlendSpace.Get();
	else OutAssets.Sequence=Row->Assets.Sequence.Get();
	if (!OutAssets.IsValid())
	{
		OutSelection.Outcome=EElysiumAnimOutcome::NoAsset;
		OutSelection.Detail=TEXT("native animation was not prepared before resolution"); ReportMiss(Intent,OutSelection); return;
	}
	if (RefuseMaskedBase(Intent,Catalog,Mesh,OutSelection,OutAssets)) return;
	for (const auto& Ref : Row->Layers)
	{
		UAnimSequence* Clip=Ref.Sequence.Get(); UBlendSpace* Space=Ref.BlendSpace.Get();
		UAnimSequence* MaskSource=Space?Ref.BaseCell.Get():Clip;
		if (Space) OutAssets.OverlaySpace=Space;
		else if (Clip && Clip->FindMetaDataByClass<UElysiumAnimPostAdditive>()) { OutAssets.AdditiveSequence=Clip; continue; }
		else if (Clip) OutAssets.OverlaySequence=Clip;
		const auto* Mask=MaskSource?MaskSource->FindMetaDataByClass<UElysiumAnimLayerMask>():nullptr;
		if (Mask) OutAssets.OverlayMaskName=Mask->Profile;
	}
	return;
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
		// **The same chain the pose walks.** The body kind selects the pre-translation body,
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
	// The hard-cut bit itself, which an overlay layer's blend envelope reads directly — a zero fade
	// and a snap are not the same statement, so it is carried rather than inferred from the fade.
	Out.bSnap = Selection.bSnap;
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
	// The fan half, carried out so a producer can route a directional reaction without
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
	const FString& ClipName, USkeletalMesh* Mesh, FString& OutError, const FString& OwnerRoot)
{
	OutError.Reset();
	auto* Native = NativeData();
	if (!Native || BankStem.IsEmpty() || !Mesh)
	{ OutError = TEXT("native animation service, owner or target mesh is absent"); return nullptr; }
	const auto* Owner = OwnerRoot.IsEmpty() ? Native->Body(BankStem) : Native->CinematicBody(BankStem, OwnerRoot);
	if (!Owner || !Native->BlendTable(Owner).IsValid())
	{ OutError = FString::Printf(TEXT("native owner is not prepared: %s [%s]"), *BankStem, *OwnerRoot); return nullptr; }
	const FString AnimName = ResolveGridClip(BankStem, ClipName, FElysiumPoseParams::Neutral(), OwnerRoot);
	if (auto* Clip = Native->Sequence(Owner, AnimName)) return Clip;
	OutError = FString::Printf(TEXT("native clip is absent or not resident: %s [%s] %s"), *BankStem, *OwnerRoot, *AnimName);
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
	TArray<FElysiumClipRef> Candidates = Set->StanceClips(AnimName);
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
			Set->Clips.ForEachClip([&Candidates](const FString& Label, const FElysiumNpcClip& Clip)
			{
				if (Clip.Activity.IsEmpty() && Label.Contains(TEXT("idle"), ESearchCase::IgnoreCase))
				{
					Candidates.Add(FElysiumClipRef{ Label, Clip.Owner });
				}
			});
			if (!Candidates.IsEmpty())
			{
				OutTier = EElysiumIdleTier::Loose;
			}
		}
	}
	Set->SortByWeight(Candidates);
	// Labels out: an idle is addressed by name from here on, and the resolver re-reads the
	// owner off the label's first row the same way every label-only caller does. A label two
	// banks both declare is one candidate here rather than two, which is the resting pick this
	// tier has always made.
	TArray<FString> Labels;
	Labels.Reserve(Candidates.Num());
	for (const FElysiumClipRef& Ref : Candidates)
	{
		Labels.AddUnique(Ref.Label);
	}
	return Labels;
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
