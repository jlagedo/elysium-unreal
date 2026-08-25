// The composed pose against the pose retail drew, whole body.
//
// `Elysium.Content.RigPose` scores one clip and only on a frame that had **nothing layered on**;
// `Elysium.Content.RigLayers` scores which **channels** a frame needs and whether four slots reach
// them. Neither asks what the layer composes TO, and that is the half an overlay stack owns
// (LIFE10).
//
// The capture answers it. A `life_rig_pose` session writes both reports over the same frames:
// `pose_oracle.json` carries the bone-to-world matrices `C_BaseAnimating::SetupBones` left in the
// entity's array plus the base sequence number and cycle the body committed, and
// `layer_oracle.json` carries the deduplicated channel set that frame accumulated. Joined on
// `(curtime, stem)` they give, per frame, the pose retail drew beside everything needed to rebuild
// it.
//
// **The base is rebuilt, not excluded, and that is what makes the measurement mean anything.**
// An earlier form of this test evaluated the LAYER alone and compared only the bones its mask owns.
// That is a real quantity and it passed at a third of a centimetre -- but every mask an overlay
// family carries roots at `Bip01 Spine1`, whose parent is not owned, so the owned bones form ONE
// RIGID SUBTREE, and a set of distances taken inside a rigid group is invariant to every isometry
// of that group. Measured on this corpus: rotating the whole upper body 47 degrees and translating
// it 250 cm changed the score by 5.7e-15 cm, and mirroring it changed the score by exactly zero.
// The class of defect that reads as "the weapon swings while the arm's own pose looks right" is
// precisely a boundary orientation error, and a layer-only comparison scores it at zero.
//
// So the frame is composed the way retail composes it:
//
//  * the **base** clip the capture's own `player_state.sequence` names, at the cycle it recorded --
//    retail's identity for a clip, carried per row by the export as `RawIndex`;
//  * then the layer, **in local space**, replacing exactly the bones its baked mask owns.
//
// The base's own declared closure is deliberately not rebuilt, and that is not an approximation: an
// overlay family's mask and its base's aim/bobble closure own the same 49 bones, so at full weight
// every channel the base's closure could contribute to is one the overlay replaces outright.
//
// Composed that way the body is no longer one rigid group -- the lower half is the base's and the
// upper half is the layer's -- so the pairwise distance set now measures how the two halves sit
// against each other, which is the quantity the earlier form was blind to.
//
// **The comparison still never converts a matrix.** Retail's frame is Source-space and ours is
// Unreal-native, and the basis change between them contains a reflection, so a rotation compared
// across it is a sign bug waiting to read as a finding. What is compared is the set of distances
// between bones -- a quantity every isometry preserves and a pose determines.
//
// **The aim cell is bound to the capture, not searched.** A layer that declares an aim grid is
// steered by the aim parameters, and the capture records the `aim_yaw` the body held; the cell is
// resolved through the runtime's own `SelectCell` against that value. Letting a search pick the
// best-fitting cell instead made a gross aim defect -- every frame pinned to the centre cell --
// score inside tolerance on this corpus.
//
// **Every layer is asserted on its own.** A global median cannot fail on a one-family defect: the
// largest single family is under a fifth of the corpus, so replacing any one weapon's asset with a
// knowingly wrong one moves the overall median by hundredths of a centimetre while that family's
// own median goes to eight.

#include "Misc/AutomationTest.h"

#include "ElysiumContentPaths.h"
#include "ElysiumPoseOracle.h"
#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/BlendProfile.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumRigComposeFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The overlay families a producer can push onto the stack. Suffixes rather than labels: the set
	// is one per weapon family per body, and naming them would be a census this file has to be
	// edited to keep true.
	const TCHAR* const GOverlaySuffixes[] =
	{
		TEXT("_attack_layer"), TEXT("_reload_layer"), TEXT("_dryfire_layer"),
	};

	bool IsOverlayLabel(const FString& Label)
	{
		for (const TCHAR* Suffix : GOverlaySuffixes)
		{
			if (Label.EndsWith(Suffix, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	// Retail replaces rather than blends only at full weight; below it the drawn pose is a mix whose
	// base contribution this test does not weigh. The epsilon is a float-compare tolerance, not a
	// band.
	constexpr double GFullWeight = 0.999;

	// One captured frame that had exactly one overlay standing, and the pose retail drew for it.
	struct FComposeFrame
	{
		FString Stem;
		FString LayerLabel;
		FString LayerOwner;
		double Weight = 0.0;
		// The base the body committed, by retail's own global sequence number, and where on it the
		// frame stood. Both come off `player_state`, which is the same hook's own record.
		int32 BaseSequence = INDEX_NONE;
		double BaseCycle = 0.0;
		// **What the capture records under `aim_yaw` is a world FACING yaw, not the aim pose
		// parameter.** The parameter's own declared range is -45..45 and the recorded values run
		// 0..360, so feeding it to a grid steered by `aim_yaw` clamps every frame to one edge cell.
		// It is kept for what it actually is -- the direction the body was pointing -- and the aim
		// fan is handled by search instead, because its parameter is genuinely unrecorded.
		double FacingYaw = 0.0;
		FVector Velocity = FVector::ZeroVector;
		// The move fan's parameter, derived: the angle between where the body was going and where
		// it was pointing. Both come out of the capture's own frame, so nothing is compared across
		// the basis change. Unset (and the fan unsteered) while the body is not moving, which is
		// what `move_yaw` means at rest.
		bool bMoving = false;
		double MoveYaw = 0.0;
		// **The control cohort.** A frame with no overlay at all is composed through the very same
		// path with the layer step skipped, which is what makes the layered number readable: if the
		// base rebuild were the thing that is wrong, these would be wrong by the same amount, and
		// the layered figure would say nothing about layering.
		bool bLayered = false;
		// Whether another full-body base was still fading under the committed one. Retail blends
		// surviving previous base sequences newest to oldest on their own clocks, so a frame in a
		// cross-fade is drawn from a base this test does not rebuild -- reported apart rather than
		// mixed into either cohort's verdict.
		bool bCrossFading = false;
		TMap<FString, FVector> BonePositions;   // bone-to-world translation, centimetres
	};

	// Both reports of one session, joined on `(curtime, stem)`.
	//
	// The join key is the pose hook's own clock: `analyze_rig_pose` and `analyze_rig_layers` read
	// the same `SetupBones` call, so a frame present in one is present in the other and the times
	// are bit-identical rather than merely close.
	bool ReadSession(const FString& SessionDir, TArray<FComposeFrame>& OutFrames,
		int32& OutFractional, int32& OutMultiple, int32& OutUnjoined, FString& OutError)
	{
		const FString LayerPath = SessionDir / TEXT("layer_oracle.json");
		const FString PosePath = SessionDir / TEXT("pose_oracle.json");
		if (!IFileManager::Get().FileExists(*LayerPath)
			|| !IFileManager::Get().FileExists(*PosePath))
		{
			return true;   // a session that has not been through both analyzers carries no frames
		}

		const auto LoadArray = [&OutError](const FString& Path,
			const TArray<TSharedPtr<FJsonValue>>*& OutArray, TSharedPtr<FJsonObject>& OutRoot)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *Path))
			{
				OutError = FString::Printf(TEXT("cannot read %s"), *Path);
				return false;
			}
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
			{
				OutError = FString::Printf(TEXT("%s is not a JSON object"), *Path);
				return false;
			}
			if (!OutRoot->TryGetArrayField(TEXT("frames"), OutArray))
			{
				OutError = FString::Printf(TEXT("%s carries no `frames` array"), *Path);
				return false;
			}
			return true;
		};

		TSharedPtr<FJsonObject> LayerRoot;
		TSharedPtr<FJsonObject> PoseRoot;
		const TArray<TSharedPtr<FJsonValue>>* LayerFrames = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* PoseFrames = nullptr;
		if (!LoadArray(LayerPath, LayerFrames, LayerRoot)
			|| !LoadArray(PosePath, PoseFrames, PoseRoot))
		{
			return false;   // present but unreadable is a failure, not an absence
		}

		TMap<TPair<double, FString>, const TSharedPtr<FJsonObject>*> Poses;
		for (const TSharedPtr<FJsonValue>& Value : *PoseFrames)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object))
			{
				continue;
			}
			double CurTime = 0.0;
			FString Stem;
			(*Object)->TryGetNumberField(TEXT("curtime"), CurTime);
			(*Object)->TryGetStringField(TEXT("stem"), Stem);
			if (!Stem.IsEmpty())
			{
				Poses.Add(TPair<double, FString>(CurTime, Stem), Object);
			}
		}

		for (const TSharedPtr<FJsonValue>& Value : *LayerFrames)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object))
			{
				continue;
			}
			double CurTime = 0.0;
			FString Stem;
			(*Object)->TryGetNumberField(TEXT("curtime"), CurTime);
			(*Object)->TryGetStringField(TEXT("stem"), Stem);
			const TArray<TSharedPtr<FJsonValue>>* Channels = nullptr;
			if (Stem.IsEmpty() || !(*Object)->TryGetArrayField(TEXT("channels"), Channels))
			{
				continue;
			}

			FComposeFrame Frame;
			int32 Overlays = 0;
			for (const TSharedPtr<FJsonValue>& Entry : *Channels)
			{
				const TSharedPtr<FJsonObject>* Row = nullptr;
				if (!Entry.IsValid() || !Entry->TryGetObject(Row))
				{
					continue;
				}
				FString Label;
				bool bAdditive = false;
				double Weight = 0.0;
				(*Row)->TryGetStringField(TEXT("label"), Label);
				(*Row)->TryGetBoolField(TEXT("additive"), bAdditive);
				(*Row)->TryGetNumberField(TEXT("weight"), Weight);
				if (!IsOverlayLabel(Label))
				{
					// A plain channel below full weight is a base still fading, which is the one
					// contribution shape this rebuild does not carry.
					if (!bAdditive && Weight < GFullWeight)
					{
						Frame.bCrossFading = true;
					}
					continue;
				}
				++Overlays;
				Frame.LayerLabel = Label;
				(*Row)->TryGetStringField(TEXT("owner_stem"), Frame.LayerOwner);
				Frame.Weight = Weight;
			}
			Frame.bLayered = Overlays > 0;
			if (Overlays > 1)
			{
				// Two overlays at once is a composition whose arithmetic this rung does not
				// describe -- the second replaces part of the first. Counted so the exclusion is
				// visible rather than silently narrowing the corpus.
				++OutMultiple;
				continue;
			}
			if (Frame.bLayered && Frame.Weight < GFullWeight)
			{
				++OutFractional;
				continue;
			}
			(*Object)->TryGetNumberField(TEXT("aim_yaw"), Frame.FacingYaw);

			const TSharedPtr<FJsonObject>* const* Pose =
				Poses.Find(TPair<double, FString>(CurTime, Stem));
			if (Pose == nullptr)
			{
				++OutUnjoined;
				continue;
			}
			const TSharedPtr<FJsonObject>* State = nullptr;
			if ((**Pose)->TryGetObjectField(TEXT("player_state"), State))
			{
				(*State)->TryGetNumberField(TEXT("sequence"), Frame.BaseSequence);
				(*State)->TryGetNumberField(TEXT("cycle"), Frame.BaseCycle);
				const TArray<TSharedPtr<FJsonValue>>* Velocity = nullptr;
				if ((*State)->TryGetArrayField(TEXT("velocity"), Velocity) && Velocity->Num() >= 3)
				{
					Frame.Velocity = FVector((*Velocity)[0]->AsNumber(),
						(*Velocity)[1]->AsNumber(), (*Velocity)[2]->AsNumber());
				}
				// A body barely moving has no travel direction to speak of, and the fan's own
				// answer at rest is its neutral cell. The floor is in the capture's own units.
				const double Speed = FVector2D(Frame.Velocity.X, Frame.Velocity.Y).Size();
				if (Speed > 20.0)
				{
					Frame.bMoving = true;
					const double TravelYaw = FMath::RadiansToDegrees(
						FMath::Atan2(Frame.Velocity.Y, Frame.Velocity.X));
					Frame.MoveYaw = FRotator::NormalizeAxis(TravelYaw - Frame.FacingYaw);
				}
			}
			const TSharedPtr<FJsonObject>* Bones = nullptr;
			if (!(**Pose)->TryGetObjectField(TEXT("bones"), Bones))
			{
				++OutUnjoined;
				continue;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Bone : (*Bones)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
				if (!Bone.Value.IsValid() || !Bone.Value->TryGetArray(Row)
					|| Row->Num() < ElysiumPoseOracle::MatrixFloats)
				{
					continue;
				}
				// Only the translation column is read. The basis rows would need the reflection
				// this comparison exists to avoid.
				Frame.BonePositions.Add(Bone.Key, FVector(
					(*Row)[3]->AsNumber(), (*Row)[7]->AsNumber(), (*Row)[11]->AsNumber())
					* ElysiumPoseOracle::SourceUnitToCm);
			}
			if (!Frame.BonePositions.IsEmpty())
			{
				Frame.Stem = Stem;
				OutFrames.Add(MoveTemp(Frame));
			}
		}
		return true;
	}

	// The bone indices a baked layer's mask owns, resolved by name against the PLAYING skeleton --
	// which is the skeleton the runtime's blend resolves them against, and the reason the count on
	// the metadata is a readout rather than the answer.
	TArray<int32> OwnedBoneIndices(const UAnimSequence* Sequence, USkeletalMesh* Mesh)
	{
		TArray<int32> Out;
		const UElysiumAnimLayerMask* Mask = Sequence != nullptr
			? Sequence->FindMetaDataByClass<UElysiumAnimLayerMask>() : nullptr;
		USkeleton* Skeleton = Mesh != nullptr ? Mesh->GetSkeleton() : nullptr;
		if (Mask == nullptr || Skeleton == nullptr || Mask->Profile.IsNone())
		{
			return Out;
		}
		const UBlendProfile* Profile = Skeleton->GetBlendProfile(Mask->Profile);
		if (Profile == nullptr)
		{
			return Out;
		}
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		for (int32 Index = 0; Index < Profile->GetNumBlendEntries(); ++Index)
		{
			const FBlendProfileBoneEntry& Entry = Profile->GetEntry(Index);
			// In `BlendMask` mode the scale IS the per-bone weight, so a zero entry is a bone the
			// mask does not own however it got into the table.
			if (Entry.BlendScale <= 0.0f || Entry.BoneReference.BoneName.IsNone())
			{
				continue;
			}
			const int32 Bone = Ref.FindBoneIndex(Entry.BoneReference.BoneName);
			if (Bone != INDEX_NONE)
			{
				Out.Add(Bone);
			}
		}
		return Out;
	}

	// One pose's signature: the distance between every pair of the named bones. Isometry-invariant,
	// and -- over a set spanning both halves of the body -- determined by the pose.
	void Signature(const TArray<FVector>& Positions, TArray<double>& Out)
	{
		Out.Reset(Positions.Num() * (Positions.Num() - 1) / 2);
		for (int32 A = 0; A < Positions.Num(); ++A)
		{
			for (int32 B = A + 1; B < Positions.Num(); ++B)
			{
				Out.Add(FVector::Dist(Positions[A], Positions[B]));
			}
		}
	}

	double SignatureError(const TArray<double>& Ours, const TArray<double>& Theirs)
	{
		if (Ours.Num() != Theirs.Num() || Ours.IsEmpty())
		{
			return TNumericLimits<double>::Max();
		}
		double Accumulated = 0.0;
		for (int32 Index = 0; Index < Ours.Num(); ++Index)
		{
			Accumulated += FMath::Square(Ours[Index] - Theirs[Index]);
		}
		return FMath::Sqrt(Accumulated / Ours.Num());
	}

	// A blend-table lookup over the real sidecars, cached per owner. Named apart from the caches in
	// `ElysiumRigLayerTests` and `ElysiumRigOracleTests`: two anonymous-namespace structs sharing a
	// name compile alone and collide the moment a unity build puts them in one translation unit.
	struct FComposeBlendTables
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

	// The clip retail committed, found by the global sequence number it recorded. `RawIndex` is that
	// same number, written per row by the export.
	const FElysiumNpcClip* FindByRawIndex(const FElysiumNpcClipSet& Set, int32 RawIndex,
		FString& OutLabel)
	{
		if (RawIndex == INDEX_NONE)
		{
			return nullptr;
		}
		const FElysiumNpcClip* Found = nullptr;
		Set.Clips.ForEachClip([&Found, &OutLabel, RawIndex]
			(const FString& Label, const FElysiumNpcClip& Clip)
		{
			if (Found == nullptr && Clip.RawIndex == RawIndex)
			{
				OutLabel = Label;
				Found = &Clip;
			}
		});
		return Found;
	}

	// What one layer resolves to on one body, read once.
	struct FLayerAssets
	{
		UAnimSequence* Raw = nullptr;
		TArray<int32> Owned;
	};

	// **A host's own declared closure, composed onto it — retail's autolayer walk, at the hardcoded
	// weight of 1.0 the dispatcher supplies.** Every base a captured frame commits declares one: an
	// aim grid and, on a moving gait, a `_bobble_delta`. Leaving it out is not a small omission — on
	// a frame with no overlay the aim layer IS what the drawn upper body is, and a rebuild without it
	// misses by 18 cm, which is how the control cohort caught this model being incomplete.
	//
	// Which cell of the grid is the CAPTURE's to say, through the aim value the body held, resolved
	// by the runtime's own `SelectCell` so this cannot pick a cell the game would not.
	void ComposeClosure(const FElysiumBlendTable* Table, const FString& Owner,
		const FString& HostLabel, const FElysiumPoseParams& Params, int32 AimCell, double Phase,
		USkeletalMesh* Mesh, TMap<FString, UAnimSequence*>& Clips, const FString& CacheStem,
		TArray<FTransform>& Locals)
	{
		const FElysiumAutoLayerBinding* Declared =
			Table != nullptr ? Table->FindAutoLayers(HostLabel) : nullptr;
		if (Declared == nullptr)
		{
			return;   // an ordinary absence: most clips declare nothing
		}
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		TArray<FTransform> Contribution;

		// **In declaration order.** An overlay blends toward its own pose and overwrites an additive
		// already accumulated onto the bones it owns, so walking the array in the order the model
		// states it is what keeps both contributions.
		for (const FString& DeclaredLayer : Declared->Clips)
		{
			const FElysiumBlendGrid* Grid = Table->Find(DeclaredLayer);
			FString Label = DeclaredLayer;
			if (Grid != nullptr && Grid->IsMultiCell())
			{
				// A fan whose parameter the capture records is STEERED; the aim fan's is not, so
				// its cell is the caller's — searched, and said out loud rather than pinned to a
				// value the capture never held.
				const FElysiumPoseParamDesc* Desc = Table->Param(Grid->ParamIndex[0]);
				const bool bSteered =
					Desc != nullptr && Params.Values.Contains(Desc->Name);
				if (bSteered)
				{
					const FElysiumBlendPick Pick =
						ElysiumBlendGrids::SelectCell(*Grid, *Table, Params);
					if (Pick.Cell == nullptr)
					{
						continue;
					}
					Label = Pick.Cell->Clip;
				}
				else if (Grid->Cells.IsValidIndex(AimCell))
				{
					Label = Grid->Cells[AimCell].Clip;
				}
				else
				{
					continue;
				}
			}
			// The derived form first: a cell of an autolayer grid ships only as `<label>@<host>`,
			// because its meaning is completed by the host it was composed onto.
			const FString Derived = FString::Printf(TEXT("%s@%s"), *Label, *HostLabel);
			UAnimSequence* Asset = nullptr;
			for (const FString& Name : { Derived, Label })
			{
				const FString Key = CacheStem / Owner / Name;
				UAnimSequence*& Cached = Clips.FindOrAdd(Key);
				if (Cached == nullptr)
				{
					Cached = ElysiumNpcVisual::LoadBakedClip(Mesh, Owner, Name);
				}
				if (Cached != nullptr)
				{
					Asset = Cached;
					break;
				}
			}
			if (Asset == nullptr)
			{
				continue;
			}
			const double Time = FMath::Clamp(Phase, 0.0, 1.0) * Asset->GetPlayLength();
			if (!ElysiumPoseOracle::EvaluateLocalSpace(Asset, Mesh, Time, Contribution))
			{
				continue;
			}

			if (Asset->IsValidAdditive())
			{
				// Unreal states an additive on the LEFT, which is the conversion the bake performed
				// when it marked the sequence additive against its base.
				for (int32 Bone = 0; Bone < Locals.Num() && Bone < Contribution.Num(); ++Bone)
				{
					Locals[Bone].SetRotation(
						(Contribution[Bone].GetRotation() * Locals[Bone].GetRotation())
							.GetNormalized());
					Locals[Bone].AddToTranslation(Contribution[Bone].GetTranslation());
				}
				continue;
			}
			// An overlay replaces the bones its mask owns and leaves every other one to the host.
			for (const int32 Bone : OwnedBoneIndices(Asset, Mesh))
			{
				if (Locals.IsValidIndex(Bone) && Contribution.IsValidIndex(Bone))
				{
					Locals[Bone] = Contribution[Bone];
				}
			}
		}
		(void)Ref;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRigComposeTest,
	"Elysium.Content.RigCompose", GElysiumRigComposeFlags)
bool FElysiumRigComposeTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
		return true;
	}
	const TArray<FString> Sessions = ElysiumPoseOracle::FindSessions();
	if (Sessions.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no life_rig_pose session under ")
			TEXT("$ELYSIUM_WORK_ROOT/research/frida (capture with frida_probe --recipe ")
			TEXT("life_rig_pose, then run analyze_rig_pose and analyze_rig_layers)"));
		return true;
	}

	TArray<FComposeFrame> Frames;
	int32 Fractional = 0;
	int32 Multiple = 0;
	int32 Unjoined = 0;
	for (const FString& Session : Sessions)
	{
		FString Error;
		if (!ReadSession(Session, Frames, Fractional, Multiple, Unjoined, Error))
		{
			AddError(Error);
			return false;
		}
	}
	if (Frames.IsEmpty())
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: no captured frame stands exactly one overlay layer at full ")
			TEXT("weight (%d stood one at a fraction, %d stood more than one, %d did not join a ")
			TEXT("pose); run analyze_rig_layers over a session analyze_rig_pose has already read"),
			Fractional, Multiple, Unjoined));
		return true;
	}

	FElysiumNpcIndex NpcIndex;
	FString IndexError;
	if (!NpcIndex.Load(IndexError) || !NpcIndex.IsValid())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported npc index ")
			TEXT("(run: uv run elysium export bundle npc)"));
		return true;
	}
	FComposeBlendTables BlendTableFor{ &NpcIndex };

	TMap<FString, USkeletalMesh*> Meshes;
	TMap<FString, TSharedPtr<FElysiumNpcClipSet>> ClipSets;
	TMap<FString, UAnimSequence*> Clips;
	TMap<FString, TSharedPtr<FLayerAssets>> LayerCache;

	// The layered cohort, its control, and the cross-fading frames both cohorts set aside.
	ElysiumPoseOracle::FDistribution AllError;
	ElysiumPoseOracle::FDistribution BaseOnly;
	ElysiumPoseOracle::FDistribution CrossFaded;
	TMap<FString, ElysiumPoseOracle::FDistribution> ByLayer;
	// **Which bones carry the error.** A single number says a composition is wrong; this says
	// where, which is the difference between a red test and a diagnosis. Accumulated as the mean
	// absolute distance error over every pair a bone takes part in, on the layered cohort only.
	TMap<FString, ElysiumPoseOracle::FDistribution> ByBone;
	// How many frames committed a base that is a multi-cell grid. Such a base is steered by a
	// parameter -- `move_yaw` on a gait fan -- that this capture does not record, so the cell this
	// rebuild picks is the neutral one and need not be the cell retail played. Counted and
	// reported rather than silently folded into a verdict.
	int32 SteeredBase = 0;
	// Bones left out because the playing bank addresses no track for them -- hair, garment and
	// weapon-attachment chains a clip evaluation cannot answer for.
	int32 UntrackedExcluded = 0;
	TSet<FString> ExcludedBones;
	int32 NoMesh = 0;
	int32 NoClipSet = 0;
	int32 NoBaseSequence = 0;
	int32 NoBaseAsset = 0;
	int32 NoLayerAsset = 0;
	int32 NoMask = 0;
	int32 NoCommonBones = 0;
	TSet<FString> MissingAssets;
	TSet<FString> MasklessLayers;

	for (const FComposeFrame& Frame : Frames)
	{
		USkeletalMesh*& Mesh = Meshes.FindOrAdd(Frame.Stem);
		if (Mesh == nullptr)
		{
			Mesh = ElysiumNpcVisual::LoadBakedMesh(Frame.Stem);
		}
		if (Mesh == nullptr)
		{
			++NoMesh;
			continue;
		}
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();

		TSharedPtr<FElysiumNpcClipSet>& Set = ClipSets.FindOrAdd(Frame.Stem);
		if (!Set.IsValid())
		{
			Set = MakeShared<FElysiumNpcClipSet>();
			FString LoadError;
			if (!Set->Load(Frame.Stem, LoadError))
			{
				Set.Reset();
			}
		}
		if (!Set.IsValid())
		{
			++NoClipSet;
			continue;
		}

		// --- the base retail committed ----------------------------------------------------------
		FString BaseLabel;
		const FElysiumNpcClip* BaseClip = FindByRawIndex(*Set, Frame.BaseSequence, BaseLabel);
		if (BaseClip == nullptr)
		{
			++NoBaseSequence;
			continue;
		}
		// **The base is a fan more often than not**, and the cell is the capture's to say through the
		// `move_yaw` derived above. Standing the label's own asset instead stands the fan's neutral
		// cell, which is a forward walk on a body that was strafing — measured at six centimetres of
		// leg error, which is what sent an earlier form of this test red for the wrong reason.
		// **A fan is a blend, not a pick.** `SelectCell` answers the nearest cell and carries the
		// fraction the parameter actually sat at; retail weighs the two neighbours by it. Snapping
		// instead puts a body strafing at seventy degrees onto the ninety-degree cell outright, and
		// the derived `move_yaw` sits more than sixteen degrees off a spoke on a tenth of the frames
		// -- a third of the way between cells on a forty-five degree fan, which lands on the legs.
		FString BaseCellLabel = BaseLabel;
		FString BaseNeighbourLabel;
		float NeighbourWeight = 0.0f;
		if (const FElysiumBlendTable* BaseTable = BlendTableFor(BaseClip->Owner))
		{
			const FElysiumBlendGrid* BaseGrid = BaseTable->Find(BaseLabel);
			if (BaseGrid != nullptr && BaseGrid->IsMultiCell())
			{
				FElysiumPoseParams BaseParams;
				if (Frame.bMoving)
				{
					BaseParams.Set(TEXT("move_yaw"), static_cast<float>(Frame.MoveYaw));
				}
				const FElysiumBlendPick Pick =
					ElysiumBlendGrids::SelectCell(*BaseGrid, *BaseTable, BaseParams);
				if (Pick.Cell != nullptr)
				{
					BaseCellLabel = Pick.Cell->Clip;
					const float Fraction = Pick.Fraction[0];
					if (!FMath::IsNearlyZero(Fraction))
					{
						const int32 Step = Fraction > 0.0f ? 1 : -1;
						const FElysiumBlendCell* Neighbour =
							BaseGrid->CellAt(Pick.Index[0] + Step, Pick.Index[1]);
						if (Neighbour != nullptr && Neighbour->Clip != BaseCellLabel)
						{
							BaseNeighbourLabel = Neighbour->Clip;
							NeighbourWeight = FMath::Abs(Fraction);
						}
					}
				}
			}
		}
		const FString BaseKey = Frame.Stem / BaseClip->Owner / BaseCellLabel;
		UAnimSequence*& Base = Clips.FindOrAdd(BaseKey);
		if (Base == nullptr)
		{
			Base = ElysiumNpcVisual::LoadBakedClip(Mesh, BaseClip->Owner, BaseCellLabel);
		}
		if (Base == nullptr)
		{
			++NoBaseAsset;
			MissingAssets.Add(BaseKey);
			continue;
		}

		// --- the layer, and the bones its mask owns ----------------------------------------------
		// A control frame has none: it is composed by the same code with this whole step skipped.
		TSharedPtr<FLayerAssets> NoAssets;
		const FString LayerKey = Frame.bLayered
			? Frame.Stem / Frame.LayerOwner / Frame.LayerLabel : FString();
		TSharedPtr<FLayerAssets>& Assets =
			Frame.bLayered ? LayerCache.FindOrAdd(LayerKey) : NoAssets;
		if (Frame.bLayered && !Assets.IsValid())
		{
			Assets = MakeShared<FLayerAssets>();
			Assets->Raw = ElysiumNpcVisual::LoadBakedClip(Mesh, Frame.LayerOwner, Frame.LayerLabel);
			if (Assets->Raw == nullptr)
			{
				MissingAssets.Add(LayerKey);
			}
			else
			{
				Assets->Owned = OwnedBoneIndices(Assets->Raw, Mesh);
				if (Assets->Owned.IsEmpty())
				{
					MasklessLayers.Add(LayerKey);
				}
			}
		}
		if (Frame.bLayered && Assets->Raw == nullptr)
		{
			++NoLayerAsset;
			continue;
		}
		if (Frame.bLayered && Assets->Owned.IsEmpty())
		{
			// A layer that reached the frame carrying no mask composes at zero weight on every bone
			// in `BlendMask` mode -- it poses nothing. Reported as an error below, not skipped
			// quietly.
			++NoMask;
			continue;
		}

		// **The clip that actually stands in the slot.** A layer declaring an aim grid composes its
		// own derived cell -- the host's motion with the aim pose resolved onto it and the host's
		// `_delta` folded in -- and WHICH cell is the capture's to say, through the aim value the
		// body held. Resolved by the runtime's own selector so the test cannot pick a cell the game
		// would not.
		// **What stands in the slot is the raw layer; its aim cell and its `_delta` are its own
		// declared closure**, composed inside the search below by the same rule the base's is. It is
		// resolved there rather than here because the aim fan's cell is one of the two things being
		// searched, and picking it once up front would pin it.
		UAnimSequence* Standing = Frame.bLayered ? Assets->Raw : nullptr;
		const FElysiumBlendTable* Table =
			Frame.bLayered ? BlendTableFor(Frame.LayerOwner) : nullptr;

		// --- our side: base at the recorded cycle -------------------------------------------------
		TArray<FTransform> BaseLocals;
		const double BaseTime = FMath::Clamp(Frame.BaseCycle, 0.0, 1.0) * Base->GetPlayLength();
		if (!ElysiumPoseOracle::EvaluateLocalSpace(Base, Mesh, BaseTime, BaseLocals))
		{
			++NoBaseAsset;
			continue;
		}

		// --- retail's side, over the bones the ANIMATION is responsible for -----------------------
		//
		// **A bone no clip in the playing bank addresses is not a composed-pose answer.** The shared
		// `move_and_ranged` banks carry sixty bones; a body carries seventy-nine or eighty-eight, and
		// the difference is hair, garment and weapon-attachment chains that retail drives by its own
		// secondary motion and that no clip evaluation can reproduce. Left in, the female bodies'
		// five-bone hair chain off `Bip01 Head` was the single largest term in the error at twelve
		// centimetres, measuring a simulation this test does not run.
		//
		// The criterion is the evaluator's own "no track" signal rather than a list of names:
		// `EvaluateLocalSpace` seeds every bone with the mesh reference pose and overwrites only the
		// ones the clip carries, so a bone still holding it bit-for-bit is one the clip did not
		// address. Counted, so the exclusion is visible rather than a silent narrowing.
		const TArray<FTransform>& RefPose = Ref.GetRefBonePose();
		TArray<int32> Order;
		TArray<FVector> Retail;
		int32 Untracked = 0;
		for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
		{
			const FVector* Position =
				Frame.BonePositions.Find(Ref.GetBoneName(Index).ToString());
			if (Position == nullptr)
			{
				continue;
			}
			if (RefPose.IsValidIndex(Index) && BaseLocals.IsValidIndex(Index)
				&& BaseLocals[Index].Equals(RefPose[Index], 0.0f))
			{
				++Untracked;
				ExcludedBones.Add(Ref.GetBoneName(Index).ToString());
				continue;
			}
			Order.Add(Index);
			Retail.Add(*Position);
		}
		UntrackedExcluded += Untracked;
		if (Order.Num() < 16)
		{
			++NoCommonBones;
			continue;
		}
		TArray<double> Target;
		Signature(Retail, Target);
		// The fan's other neighbour, weighed in by the fraction the parameter sat at.
		if (!BaseNeighbourLabel.IsEmpty() && NeighbourWeight > 0.0f)
		{
			const FString NeighbourKey = Frame.Stem / BaseClip->Owner / BaseNeighbourLabel;
			UAnimSequence*& Neighbour = Clips.FindOrAdd(NeighbourKey);
			if (Neighbour == nullptr)
			{
				Neighbour = ElysiumNpcVisual::LoadBakedClip(
					Mesh, BaseClip->Owner, BaseNeighbourLabel);
			}
			TArray<FTransform> NeighbourLocals;
			if (Neighbour != nullptr && ElysiumPoseOracle::EvaluateLocalSpace(Neighbour, Mesh,
					FMath::Clamp(Frame.BaseCycle, 0.0, 1.0) * Neighbour->GetPlayLength(),
					NeighbourLocals))
			{
				for (int32 Bone = 0; Bone < BaseLocals.Num() && Bone < NeighbourLocals.Num(); ++Bone)
				{
					BaseLocals[Bone].BlendWith(NeighbourLocals[Bone], NeighbourWeight);
				}
			}
		}

		// **The base's closure is composed only where it survives.** On a layered frame the overlay
		// replaces the very 49 bones the base's aim overlay and bobble additive own, so every
		// contribution the closure could make is overwritten outright — provably, from the masks
		// rather than by assumption. Skipping it there is exact, and it is what keeps the search
		// below from multiplying by nine for nothing.
		FElysiumPoseParams Params;
		if (Frame.bMoving)
		{
			Params.Set(TEXT("move_yaw"), static_cast<float>(Frame.MoveYaw));
		}
		const TArray<FTransform> BaseAlone = BaseLocals;

		// The layer's own phase is the one thing the capture does not record -- the hook writes a
		// contribution's model, sequence and weight, and no cycle -- so it is searched over the
		// clip's own keys and the best is kept. That is a lower bound on the error, which is the
		// honest direction: it cannot manufacture a divergence, only miss one.
		const int32 Keys = Standing != nullptr
			? FMath::Max(1, Standing->GetNumberOfSampledKeys()) : 1;
		const double LayerLength = Standing != nullptr ? Standing->GetPlayLength() : 0.0;
		// The aim fan's own parameter is not in the capture, so its cell is searched. Nine cells,
		// stated rather than hidden: this is the one axis of the rebuild the capture cannot bind.
		constexpr int32 AimCells = 9;
		double Best = TNumericLimits<double>::Max();
		TArray<FTransform> LayerLocals;
		TArray<FTransform> Composed;
		TArray<FTransform> Component;
		TArray<FVector> Ours;
		TArray<FVector> BestOurs;
		TArray<double> Mine;
		for (int32 AimCell = 0; AimCell < AimCells; ++AimCell)
		{
		for (int32 Key = 0; Key < Keys; ++Key)
		{
			const double Time = Keys > 1
				? LayerLength * static_cast<double>(Key) / (Keys - 1) : 0.0;
			Composed = BaseAlone;
			if (Standing != nullptr)
			{
				if (!ElysiumPoseOracle::EvaluateLocalSpace(Standing, Mesh, Time, LayerLocals))
				{
					continue;
				}
				for (const int32 Bone : Assets->Owned)
				{
					if (Composed.IsValidIndex(Bone) && LayerLocals.IsValidIndex(Bone))
					{
						Composed[Bone] = LayerLocals[Bone];
					}
				}
				// The layer's OWN closure — its aim cell and its `_delta` — composed onto it by the
				// same rule the base's is, because retail's autolayer walk is one rule.
				ComposeClosure(Table, Frame.LayerOwner, Frame.LayerLabel, Params, AimCell,
					Keys > 1 ? static_cast<double>(Key) / (Keys - 1) : 0.0,
					Mesh, Clips, Frame.Stem, Composed);
			}
			else
			{
				ComposeClosure(BlendTableFor(BaseClip->Owner), BaseClip->Owner, BaseLabel, Params,
					AimCell, Frame.BaseCycle, Mesh, Clips, Frame.Stem, Composed);
			}
			ElysiumPoseOracle::LocalToComponent(Ref, Composed, Component);

			Ours.Reset(Order.Num());
			for (const int32 Bone : Order)
			{
				Ours.Add(Component.IsValidIndex(Bone)
					? Component[Bone].GetLocation() : FVector::ZeroVector);
			}
			Signature(Ours, Mine);
			const double Score = SignatureError(Mine, Target);
			if (Score < Best)
			{
				Best = Score;
				BestOurs = Ours;
			}
		}
		}
		if (Best >= TNumericLimits<double>::Max())
		{
			continue;
		}
		if (const FElysiumBlendTable* BaseTable = BlendTableFor(BaseClip->Owner))
		{
			const FElysiumBlendGrid* BaseGrid = BaseTable->Find(BaseLabel);
			if (BaseGrid != nullptr && BaseGrid->IsMultiCell())
			{
				++SteeredBase;
			}
		}
		if (Frame.bLayered && !Frame.bCrossFading && !BestOurs.IsEmpty())
		{
			// The pair set is symmetric, so a bone's share is the mean over the pairs it is in.
			for (int32 A = 0; A < Order.Num(); ++A)
			{
				double Sum = 0.0;
				int32 Count = 0;
				for (int32 B = 0; B < Order.Num(); ++B)
				{
					if (A == B)
					{
						continue;
					}
					Sum += FMath::Abs(FVector::Dist(BestOurs[A], BestOurs[B])
						- FVector::Dist(Retail[A], Retail[B]));
					++Count;
				}
				if (Count > 0)
				{
					ByBone.FindOrAdd(Ref.GetBoneName(Order[A]).ToString()).Add(Sum / Count);
				}
			}
		}
		if (Frame.bCrossFading)
		{
			// Another base was still fading under the committed one, so the drawn pose is a blend
			// this rebuild does not carry. Held apart from both verdicts rather than counted
			// against the composition.
			CrossFaded.Add(Best);
		}
		else if (Frame.bLayered)
		{
			AllError.Add(Best);
			ByLayer.FindOrAdd(Frame.LayerLabel).Add(Best);
		}
		else
		{
			BaseOnly.Add(Best);
		}
	}

	// **Every counter is surfaced on the success path, not only when the run abstains.** A run that
	// scored eight frames of six hundred and passed is the failure mode a hidden counter produces.
	AddInfo(FString::Printf(
		TEXT("corpus: %d frames stood exactly one overlay at full weight; set aside %d at a ")
		TEXT("fraction, %d with more than one, %d unjoined"),
		Frames.Num(), Fractional, Multiple, Unjoined));
	AddInfo(FString::Printf(
		TEXT("not scored: no mesh %d, no clip set %d, unnumbered base %d, base asset %d, ")
		TEXT("layer asset %d, no mask %d, too few shared bones %d"),
		NoMesh, NoClipSet, NoBaseSequence, NoBaseAsset, NoLayerAsset, NoMask, NoCommonBones));

	if (AllError.Count() == 0 && BaseOnly.Count() == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no captured frame could be composed and scored"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("layered   (base + masked overlay): %s"),
		*AllError.Describe(TEXT(" cm"))));
	AddInfo(FString::Printf(TEXT("control   (the same base, nothing layered): %s"),
		*BaseOnly.Describe(TEXT(" cm"))));
	AddInfo(FString::Printf(TEXT("set aside (a previous base still fading): %s"),
		*CrossFaded.Describe(TEXT(" cm"))));
	AddInfo(FString::Printf(
		TEXT("%d scored frames committed a multi-cell base; its `move_yaw` is derived from the ")
		TEXT("capture's own velocity and facing"), SteeredBase));
	{
		TArray<FString> Names = ExcludedBones.Array();
		Names.Sort();
		AddInfo(FString::Printf(
			TEXT("%d bone-frames excluded as untracked by the playing bank (%d distinct: %s)"),
			UntrackedExcluded, Names.Num(), *FString::Join(Names, TEXT(", "))));
	}

	// **The control is what makes the layered figure readable.** Both cohorts rebuild the base the
	// same way from the same recorded sequence and cycle; only the layered one then composes an
	// overlay over it. So a control that matches retail and a layered set that does not is a
	// statement about LAYERING, and a control that misses by the same margin would say the base
	// rebuild is what is wrong and the layered number means nothing. Asserted rather than printed,
	// because a test whose own premise has quietly stopped holding is worse than one that fails.
	constexpr double MedianToleranceCm = 1.0;
	if (BaseOnly.Count() >= 5)
	{
		TestTrue(FString::Printf(
			TEXT("the control composes to the pose retail drew, so the base rebuild is sound ")
			TEXT("(median %.4f cm over %d frames)"),
			BaseOnly.Median(), BaseOnly.Count()), BaseOnly.Median() <= MedianToleranceCm);
	}
	else
	{
		AddWarning(FString::Printf(
			TEXT("only %d unlayered frames control this run; the layered figures below are ")
			TEXT("uncontrolled"), BaseOnly.Count()));
	}

	if (!MissingAssets.IsEmpty())
	{
		TArray<FString> Names = MissingAssets.Array();
		Names.Sort();
		AddError(FString::Printf(
			TEXT("%d clips retail composed bind no baked asset on the mount: %s"),
			Names.Num(), *FString::Join(Names, TEXT(", "))));
	}
	if (!MasklessLayers.IsEmpty())
	{
		TArray<FString> Names = MasklessLayers.Array();
		Names.Sort();
		AddError(FString::Printf(
			TEXT("%d overlay layers carry no baked bone mask, so they would compose at zero weight ")
			TEXT("on every bone and pose nothing: %s"),
			Names.Num(), *FString::Join(Names, TEXT(", "))));
	}

	// **Per layer, not on the aggregate.** The largest single family is under a fifth of the corpus,
	// so a global median cannot fail on a one-family defect: replacing any one weapon's asset with a
	// knowingly wrong one moves the overall median by hundredths of a centimetre while that family's
	// own median goes to eight.
	//
	// A family the capture barely saw would let one lucky frame stand for it.
	constexpr int32 MinimumFramesPerLayer = 5;
	TArray<FString> Layers;
	ByLayer.GetKeys(Layers);
	Layers.Sort();
	for (const FString& Layer : Layers)
	{
		ElysiumPoseOracle::FDistribution& Distribution = ByLayer[Layer];
		AddInfo(FString::Printf(TEXT("  %-34s %s"), *Layer, *Distribution.Describe(TEXT(" cm"))));
		if (Distribution.Count() < MinimumFramesPerLayer)
		{
			continue;
		}
		TestTrue(FString::Printf(
			TEXT("'%s' composes to the pose retail drew (median %.4f cm over %d frames)"),
			*Layer, Distribution.Median(), Distribution.Count()),
			Distribution.Median() <= MedianToleranceCm);
	}

	// Where the error sits, worst first. Reported rather than asserted: the per-layer verdicts above
	// are what fails, and this is what a reader opens the report to find out next.
	if (!ByBone.IsEmpty())
	{
		TArray<TPair<FString, double>> Ranked;
		for (TPair<FString, ElysiumPoseOracle::FDistribution>& Bone : ByBone)
		{
			Ranked.Emplace(Bone.Key, Bone.Value.Median());
		}
		Ranked.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B)
		{
			return A.Value > B.Value;
		});
		AddInfo(TEXT("the bones carrying the layered error, worst first:"));
		for (int32 Index = 0; Index < Ranked.Num() && Index < 12; ++Index)
		{
			AddInfo(FString::Printf(TEXT("  %-28s %.3f cm"),
				*Ranked[Index].Key, Ranked[Index].Value));
		}
	}
	return true;
}
