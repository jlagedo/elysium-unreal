// The baked assets, composed the way the graph composes them, held to the reference compositor.
//
// `pipeline/src/elysium_pipeline/validation/retail_compositor.py` runs retail's own pose arithmetic
// offline from the raw model -- validated once against the capture -- and writes, per body, a
// dense set of states under `$ELYSIUM_EXPORT_ROOT/_oracle/<stem>.json`: a base host at a cycle,
// its pose parameters, and the root-relative position of every bone, in Unreal-native centimetres
// through the exporter's own conversion. This test stands the same state from the baked mount --
// the fan cell and its neighbour, the aim grid's cells, the delta -- and asserts that the two
// frames are the SAME frame. No capture, no search, no cohort: every input to the composition is
// stated, so a difference is a defect in the exporter, the bake or this composition, and the
// per-bone attribution says which bone carries it.
//
// The composition here is the one the running graph performs, stated in local space: a fan is the
// blend of its two neighbouring cells; a grid the blend of its four; an overlay replaces the bones
// its mask owns; a delta post-multiplies (`FAnimNode_ElysiumPostAdditive`). Where the graph does
// something this rebuild does not -- the mesh-space rotation blend on the split bone -- the
// oracle is what says whether the graph or the rebuild is right, which is the whole point of
// having it.

#include "Misc/AutomationTest.h"

#include "Tests/ElysiumNativeCharacterTestData.h"
#include "ElysiumContentPaths.h"
#include "ElysiumPoseOracle.h"
#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumAnimPostAdditive.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendProfile.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags GElysiumOracleIdentityFlags =
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	// Compression is the only thing allowed between the oracle and the mount, and it is bounded:
	// the bake's own contract asserts every key within 0.1 cm of the container.
	constexpr double GIdentityMedianToleranceCm = 0.5;

	struct FOracleFrame
	{
		FString Owner;
		FString Label;
		double Cycle = 0.0;
		FElysiumPoseParams Params;
		FString RootBone;
		TMap<FString, FVector> Bones;
	};

	bool LoadOracle(const FString& Path, FString& OutStem, TArray<FOracleFrame>& OutFrames)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return false;
		}
		OutStem = Root->GetStringField(TEXT("stem"));
		const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
		if (!Root->TryGetArrayField(TEXT("frames"), Frames))
		{
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
			Frame.Owner = (*Object)->GetStringField(TEXT("owner"));
			Frame.Label = (*Object)->GetStringField(TEXT("label"));
			Frame.Cycle = (*Object)->GetNumberField(TEXT("cycle"));
			Frame.RootBone = (*Object)->GetStringField(TEXT("root_bone"));
			const TSharedPtr<FJsonObject>* Params = nullptr;
			if ((*Object)->TryGetObjectField(TEXT("params"), Params))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Param : (*Params)->Values)
				{
					Frame.Params.Set(*Param.Key, static_cast<float>(Param.Value->AsNumber()));
				}
			}
			const TSharedPtr<FJsonObject>* Bones = nullptr;
			if ((*Object)->TryGetObjectField(TEXT("bones"), Bones))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Bone : (*Bones)->Values)
				{
					const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
					if (Bone.Value.IsValid() && Bone.Value->TryGetArray(Row) && Row->Num() >= 3)
					{
						Frame.Bones.Add(Bone.Key, FVector((*Row)[0]->AsNumber(),
							(*Row)[1]->AsNumber(), (*Row)[2]->AsNumber()));
					}
				}
			}
			OutFrames.Add(MoveTemp(Frame));
		}
		return true;
	}

	// The bone indices a baked layer's mask owns, resolved by name against the playing skeleton.
	TArray<int32> OwnedBones(const UAnimSequence* Sequence, USkeletalMesh* Mesh)
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

	struct FComposer
	{
		USkeletalMesh* Mesh = nullptr;
		const FElysiumNpcIndex* Index = nullptr;
		TMap<FString, TSharedPtr<FElysiumBlendTable>> Tables;
		TMap<FString, UAnimSequence*> Clips;
		TSet<FString> Missing;

		const FElysiumBlendTable* Table(const FString& Owner)
		{
			if (const TSharedPtr<FElysiumBlendTable>* Found = Tables.Find(Owner))
			{
				return Found->Get();
			}
			const FElysiumNpcIndexEntry* Entry = Index->Npcs.Find(Owner);
			if (Entry == nullptr) { Entry = Index->Banks.Find(Owner); }
			TSharedPtr<FElysiumBlendTable> Loaded;
			if (Entry != nullptr && !Entry->Blends.IsEmpty())
			{
				Loaded = MakeShared<FElysiumBlendTable>();
				FString Error;
				if (!ElysiumNativeTest::Load(*Loaded, Entry->Blends, Error))
				{
					Loaded.Reset();
				}
			}
			Tables.Add(Owner, Loaded);
			return Loaded.Get();
		}

		UAnimSequence* Clip(const FString& Owner, const FString& Name)
		{
			const FString Key = Owner / Name;
			UAnimSequence*& Cached = Clips.FindOrAdd(Key);
			if (Cached == nullptr)
			{
				Cached = ElysiumNpcVisual::LoadBakedClip(Mesh, Owner, Name);
			}
			return Cached;
		}

		// A cell in its derived form against the host first, then its plain label.
		UAnimSequence* CellClip(const FString& Owner, const FString& Cell, const FString& Host)
		{
			UAnimSequence* Asset = Clip(Owner, FString::Printf(TEXT("%s@%s"), *Cell, *Host));
			return Asset != nullptr ? Asset : Clip(Owner, Cell);
		}

		bool Evaluate(UAnimSequence* Asset, double Cycle, TArray<FTransform>& Out)
		{
			return Asset != nullptr && ElysiumPoseOracle::EvaluateLocalSpace(
				Asset, Mesh, FMath::Clamp(Cycle, 0.0, 1.0) * Asset->GetPlayLength(), Out);
		}

		// A grid's pose at the parameters: the blend of its neighbouring cells by the fractions
		// `SelectCell` reports -- two on a fan, up to four on a 3x3 grid.
		bool EvaluateGrid(const FString& Owner, const FElysiumBlendTable& BlendTable,
			const FElysiumBlendGrid& Grid, const FString& Host, const FElysiumPoseParams& Params,
			double Cycle, TArray<FTransform>& Out)
		{
			const FElysiumBlendPick Pick = ElysiumBlendGrids::SelectCell(Grid, BlendTable, Params);
			if (Pick.Cell == nullptr)
			{
				return false;
			}
			auto CellAt = [&](int32 Axis0, int32 Axis1) -> const FElysiumBlendCell*
			{
				return Grid.CellAt(Axis0, Axis1);
			};
			const int32 Step0 = Pick.Fraction[0] > 0.0f ? 1 : (Pick.Fraction[0] < 0.0f ? -1 : 0);
			const int32 Step1 = Pick.Fraction[1] > 0.0f ? 1 : (Pick.Fraction[1] < 0.0f ? -1 : 0);
			const float W0 = FMath::Abs(Pick.Fraction[0]);
			const float W1 = FMath::Abs(Pick.Fraction[1]);

			auto EvaluateCell = [&](const FElysiumBlendCell* Cell, TArray<FTransform>& Locals)
			{
				if (Cell == nullptr)
				{
					return false;
				}
				UAnimSequence* Asset = Host.IsEmpty() ? Clip(Owner, Cell->Clip)
					: CellClip(Owner, Cell->Clip, Host);
				if (Asset == nullptr)
				{
					Missing.Add(Owner / Cell->Clip);
					return false;
				}
				return Evaluate(Asset, Cycle, Locals);
			};
			auto Blend = [](TArray<FTransform>& A, const TArray<FTransform>& B, float W)
			{
				for (int32 Bone = 0; Bone < A.Num() && Bone < B.Num(); ++Bone)
				{
					A[Bone].BlendWith(B[Bone], W);
				}
			};

			if (!EvaluateCell(Pick.Cell, Out))
			{
				return false;
			}
			TArray<FTransform> Side;
			if (Step0 != 0 && W0 > 0.0f && EvaluateCell(CellAt(Pick.Index[0] + Step0, Pick.Index[1]), Side))
			{
				Blend(Out, Side, W0);
			}
			if (Step1 != 0 && W1 > 0.0f)
			{
				TArray<FTransform> Row;
				if (EvaluateCell(CellAt(Pick.Index[0], Pick.Index[1] + Step1), Row))
				{
					if (Step0 != 0 && W0 > 0.0f
						&& EvaluateCell(CellAt(Pick.Index[0] + Step0, Pick.Index[1] + Step1), Side))
					{
						Blend(Row, Side, W0);
					}
					Blend(Out, Row, W1);
				}
			}
			return true;
		}

		// The host's declared closure onto `Locals`, in declaration order: a grid or overlay
		// replaces the bones it owns; a delta post-multiplies. `bHostMasked` stands a delta down
		// behind a derived grid cell that already carries it (the bake's fold).
		void Closure(const FString& Owner, const FString& Host, const FElysiumPoseParams& Params,
			double Cycle, bool bHostMasked, TArray<FTransform>& Locals, int32& OutStoodDown)
		{
			const FElysiumBlendTable* BlendTable = Table(Owner);
			const FElysiumAutoLayerBinding* Declared =
				BlendTable != nullptr ? BlendTable->FindAutoLayers(Host) : nullptr;
			if (Declared == nullptr)
			{
				return;
			}
			const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
			const TArray<FTransform>& RefPose = Ref.GetRefBonePose();
			bool bDerivedGridComposed = false;
			for (const FString& Layer : Declared->Clips)
			{
				TArray<FTransform> Contribution;
				const FElysiumBlendGrid* Grid = BlendTable->Find(Layer);
				UAnimSequence* MaskSource = nullptr;
				if (Grid != nullptr && Grid->IsMultiCell())
				{
					if (!EvaluateGrid(Owner, *BlendTable, *Grid, Host, Params, Cycle, Contribution))
					{
						continue;
					}
					if (const FElysiumBlendCell* Base = Grid->CellAt(0, 0))
					{
						MaskSource = CellClip(Owner, Base->Clip, Host);
					}
					bDerivedGridComposed = true;
				}
				else
				{
					UAnimSequence* Asset = CellClip(Owner, Layer, Host);
					if (Asset == nullptr)
					{
						Missing.Add(Owner / Layer);
						continue;
					}
					if (Asset->FindMetaDataByClass<UElysiumAnimPostAdditive>() != nullptr)
					{
						if (bHostMasked && bDerivedGridComposed)
						{
							++OutStoodDown;
							continue;
						}
						if (!Evaluate(Asset, Cycle, Contribution))
						{
							continue;
						}
						for (int32 Bone = 0; Bone < Locals.Num() && Bone < Contribution.Num(); ++Bone)
						{
							if (RefPose.IsValidIndex(Bone)
								&& Contribution[Bone].Equals(RefPose[Bone], UE_SMALL_NUMBER))
							{
								continue;
							}
							Locals[Bone].SetRotation((Locals[Bone].GetRotation()
								* Contribution[Bone].GetRotation()).GetNormalized());
							Locals[Bone].AddToTranslation(Contribution[Bone].GetTranslation());
						}
						continue;
					}
					if (!Evaluate(Asset, Cycle, Contribution))
					{
						continue;
					}
					MaskSource = Asset;
				}
				// **Mesh space on the split bone, local everywhere else** -- the graph's own rule
				// (`bMeshSpaceRotationBlend` on every overlay blend). A derived overlay states
				// `Bip01 Spine1` against the BIND chain it ships, and its meaning is that bone's
				// component-space orientation; composed as a local onto the host's animated chain
				// it lands rotated by the host's own root turn, which is the whole upper body 16-53
				// cm off. So an owned bone whose parent the mask does not own takes the rotation
				// the overlay's own FK gives it, re-expressed against the host's parent; an owned
				// bone under an owned parent is the same either way and is replaced locally.
				const TArray<int32> Owned = OwnedBones(MaskSource, Mesh);
				const TSet<int32> OwnedSet(Owned);
				LastOwned = Owned.Num();
				bLastFinger0Owned = OwnedSet.Contains(Ref.FindBoneIndex(TEXT("Bip01 L Finger0")));
				TArray<FTransform> HostComponent;
				TArray<FTransform> LayerComponent;
				ElysiumPoseOracle::LocalToComponent(Ref, Locals, HostComponent);
				ElysiumPoseOracle::LocalToComponent(Ref, Contribution, LayerComponent);
				for (const int32 Bone : Owned)
				{
					if (!Locals.IsValidIndex(Bone) || !Contribution.IsValidIndex(Bone))
					{
						continue;
					}
					const int32 Parent = Ref.GetParentIndex(Bone);
					if (Parent != INDEX_NONE && !OwnedSet.Contains(Parent))
					{
						FTransform Local = Contribution[Bone];
						// `FTransform` composes `Local * Parent`, whose rotation is `Parent.Rot *
						// Local.Rot` as a quaternion product -- so the local is `Parent⁻¹ * Component`.
						Local.SetRotation((HostComponent[Parent].GetRotation().Inverse()
							* LayerComponent[Bone].GetRotation()).GetNormalized());
						Locals[Bone] = Local;
						continue;
					}
					Locals[Bone] = Contribution[Bone];
				}
			}
		}

		// One oracle state from the mount: the base (a fan blended at `move_yaw`, or a clip) and
		// its closure, as root-relative component-space positions by bone name.
		bool bLastHostMasked = false;
		int32 LastOwned = 0;
		bool bLastFinger0Owned = false;

		bool Compose(const FOracleFrame& Frame, TMap<FString, FVector>& Out, int32& OutStoodDown)
		{
			TArray<FTransform> Locals;
			// A masked host (an attack layer) is composed the way the slot branch composes it: its
			// delta is folded into the derived aim cells at bake, so the raw delta stands down.
			bool bHostMasked = false;
			const FElysiumBlendTable* BlendTable = Table(Frame.Owner);
			const FElysiumBlendGrid* Grid = BlendTable != nullptr ? BlendTable->Find(Frame.Label) : nullptr;
			if (Grid != nullptr && Grid->IsMultiCell())
			{
				if (!EvaluateGrid(Frame.Owner, *BlendTable, *Grid, FString(), Frame.Params,
						Frame.Cycle, Locals))
				{
					return false;
				}
			}
			else
			{
				UAnimSequence* Asset = Clip(Frame.Owner, Frame.Label);
				if (Asset == nullptr)
				{
					Missing.Add(Frame.Owner / Frame.Label);
					return false;
				}
				if (!Evaluate(Asset, Frame.Cycle, Locals))
				{
					return false;
				}
				bHostMasked = Asset->FindMetaDataByClass<UElysiumAnimLayerMask>() != nullptr;
			}
			bLastHostMasked = bHostMasked;
			Closure(Frame.Owner, Frame.Label, Frame.Params, Frame.Cycle, bHostMasked, Locals,
				OutStoodDown);

			const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
			TArray<FTransform> Component;
			ElysiumPoseOracle::LocalToComponent(Ref, Locals, Component);
			const int32 Root = Ref.FindBoneIndex(*Frame.RootBone);
			if (!Component.IsValidIndex(Root))
			{
				return false;
			}
			const FTransform RootInverse = Component[Root].Inverse();
			for (int32 Bone = 0; Bone < Component.Num(); ++Bone)
			{
				Out.Add(Ref.GetBoneName(Bone).ToString(),
					RootInverse.TransformPosition(Component[Bone].GetLocation()));
			}
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOracleIdentityTest,
	"Elysium.Content.OracleIdentity", GElysiumOracleIdentityFlags)
bool FElysiumOracleIdentityTest::RunTest(const FString&)
{
	if (!ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native character cast (run: uv run elysium import characters)"));
		return true;
	}
	const FString OracleDir = FElysiumContentPaths::Root() / TEXT("_oracle");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(OracleDir / TEXT("*.json")), true, false);
	if (Files.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no oracle under $ELYSIUM_EXPORT_ROOT/_oracle ")
			TEXT("(write one with: uv run elysium debug oracle --emit)"));
		return true;
	}
	FElysiumNpcIndex NpcIndex;
	FString IndexError;
	if (!ElysiumNativeTest::Load(NpcIndex, IndexError) || !NpcIndex.IsValid())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native cast view ")
			TEXT("(run: uv run elysium import characters)"));
		return true;
	}

	int32 Composed = 0;
	Files.Sort();
	for (const FString& File : Files)
	{
		FString Stem;
		TArray<FOracleFrame> Frames;
		if (!LoadOracle(OracleDir / File, Stem, Frames))
		{
			AddError(FString::Printf(TEXT("%s does not parse as an oracle"), *File));
			continue;
		}
		FComposer Composer;
		Composer.Index = &NpcIndex;
		Composer.Mesh = ElysiumNpcVisual::LoadBakedMesh(Stem);
		if (Composer.Mesh == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: no baked mesh for '%s'"), *File, *Stem));
			continue;
		}

		ElysiumPoseOracle::FDistribution All;
		TMap<FString, ElysiumPoseOracle::FDistribution> ByHost;
		TMap<FString, ElysiumPoseOracle::FDistribution> ByBone;
		TMap<FString, TMap<FString, ElysiumPoseOracle::FDistribution>> ByHostBone;
		int32 Failed = 0;
		int32 StoodDown = 0;
		for (const FOracleFrame& Frame : Frames)
		{
			TMap<FString, FVector> Ours;
			if (!Composer.Compose(Frame, Ours, StoodDown))
			{
				++Failed;
				continue;
			}
			if (Composed == 0 || (Frame.Label == TEXT("m37_ready") && Frame.Cycle == 0.0
				&& Frame.Params.Values.FindRef(TEXT("aim_pitch")) == 0.0f && !Ours.IsEmpty()
				&& ByHost.Find(Frame.Label) == nullptr))
			{
				// One state spelled out bone by bone, so a systematic miss can be read as the
				// frame it is rather than as a statistic.
				FString Line = FString::Printf(TEXT("%s %s cycle %.2f:"), *Stem, *Frame.Label,
					Frame.Cycle);
				for (const TCHAR* Name : { TEXT("Bip01 Pelvis"), TEXT("Bip01 Spine1"),
					TEXT("Bip01 Neck"), TEXT("Bip01 Head"), TEXT("Bip01 R Hand"),
					TEXT("Bip01 L Hand"), TEXT("Bip01 R Foot") })
				{
					const FVector* Mine = Ours.Find(Name);
					const FVector* Theirs = Frame.Bones.Find(Name);
					if (Mine != nullptr && Theirs != nullptr)
					{
						Line += FString::Printf(TEXT(" | %-14s ours (%7.2f %7.2f %7.2f)  oracle ")
							TEXT("(%7.2f %7.2f %7.2f)  d=%.2f"), Name + 6, Mine->X, Mine->Y, Mine->Z,
							Theirs->X, Theirs->Y, Theirs->Z, FVector::Dist(*Mine, *Theirs));
					}
				}
				AddInfo(Line);
			}
			ElysiumPoseOracle::FDistribution FrameError;
			for (const TPair<FString, FVector>& Bone : Frame.Bones)
			{
				const FVector* Mine = Ours.Find(Bone.Key);
				if (Mine == nullptr)
				{
					continue;
				}
				const double Error = FVector::Dist(*Mine, Bone.Value);
				FrameError.Add(Error);
				ByBone.FindOrAdd(Bone.Key).Add(Error);
				ByHostBone.FindOrAdd(Frame.Label).FindOrAdd(Bone.Key).Add(Error);
			}
			if (FrameError.Count() == 0)
			{
				++Failed;
				continue;
			}
			++Composed;
			All.Add(FrameError.Median());
			ByHost.FindOrAdd(Frame.Label).Add(FrameError.Median());
			if (Composer.bLastHostMasked)
			{
				// Every state of a masked host on its own line, so a residual can be read against
				// the phase and the pitch that produced it.
				FString WorstBone;
				double WorstError = 0.0;
				for (const TPair<FString, FVector>& Bone : Frame.Bones)
				{
					const FVector* Mine = Ours.Find(Bone.Key);
					const double Error = Mine != nullptr ? FVector::Dist(*Mine, Bone.Value) : 0.0;
					if (Error > WorstError)
					{
						WorstError = Error;
						WorstBone = Bone.Key;
					}
				}
				auto Dist = [&](const TCHAR* Name) -> double
				{
					const FVector* Mine = Ours.Find(Name);
					const FVector* Theirs = Frame.Bones.Find(Name);
					return Mine != nullptr && Theirs != nullptr ? FVector::Dist(*Mine, *Theirs) : -1.0;
				};
				AddInfo(FString::Printf(TEXT("    %s cycle %.3f pitch %+.0f: median %.3f cm, worst %s %.2f")
					TEXT(" | L Forearm %.2f L Hand %.2f L Finger0 %.2f L Finger01 %.2f | owned %d, finger0 %s"),
					*Frame.Label, Frame.Cycle, Frame.Params.Get(TEXT("aim_pitch")), FrameError.Median(),
					*WorstBone, WorstError, Dist(TEXT("Bip01 L Forearm")), Dist(TEXT("Bip01 L Hand")),
					Dist(TEXT("Bip01 L Finger0")), Dist(TEXT("Bip01 L Finger01")), Composer.LastOwned,
					Composer.bLastFinger0Owned ? TEXT("owned") : TEXT("NOT owned")));
			}
		}

		AddInfo(FString::Printf(TEXT("%s: %d states, %d composed, %d not composable, %d delta ")
			TEXT("evaluation(s) stood down; %s"),
			*Stem, Frames.Num(), Frames.Num() - Failed, Failed, StoodDown,
			*All.Describe(TEXT(" cm"))));
		TArray<FString> Hosts;
		ByHost.GetKeys(Hosts);
		Hosts.Sort();
		for (const FString& Host : Hosts)
		{
			ElysiumPoseOracle::FDistribution& D = ByHost[Host];
			AddInfo(FString::Printf(TEXT("  %-36s %s"), *Host, *D.Describe(TEXT(" cm"))));
			// The bones carrying THIS host's error, so a residual on one host is attributed on
			// its own rather than diluted by every other host's identity.
			TArray<TPair<FString, double>> HostWorst;
			for (TPair<FString, ElysiumPoseOracle::FDistribution>& Bone : ByHostBone.FindOrAdd(Host))
			{
				HostWorst.Emplace(Bone.Key, Bone.Value.Median());
			}
			HostWorst.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B)
				{ return A.Value > B.Value; });
			FString HostRanking;
			for (int32 I = 0; I < HostWorst.Num() && I < 24; ++I)
			{
				HostRanking += FString::Printf(TEXT("%s%s %.2f"), I ? TEXT(", ") : TEXT(""),
					*HostWorst[I].Key.Replace(TEXT("Bip01 "), TEXT("")), HostWorst[I].Value);
			}
			AddInfo(FString::Printf(TEXT("    worst: %s"), *HostRanking));
		}
		TArray<TPair<FString, double>> Worst;
		for (TPair<FString, ElysiumPoseOracle::FDistribution>& Bone : ByBone)
		{
			Worst.Emplace(Bone.Key, Bone.Value.Median());
		}
		Worst.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B)
			{ return A.Value > B.Value; });
		FString Ranking;
		for (int32 I = 0; I < Worst.Num() && I < 8; ++I)
		{
			Ranking += FString::Printf(TEXT("%s%s %.2f"), I ? TEXT(", ") : TEXT(""),
				*Worst[I].Key.Replace(TEXT("Bip01 "), TEXT("")), Worst[I].Value);
		}
		AddInfo(FString::Printf(TEXT("  bones carrying the error, worst first: %s"), *Ranking));
		for (const FString& Name : Composer.Missing)
		{
			AddError(FString::Printf(TEXT("%s: the oracle names '%s' and the mount does not carry it"),
				*Stem, *Name));
		}
		TestTrue(FString::Printf(
			TEXT("%s: the baked mount composes to the reference compositor's frame at every ")
			TEXT("stated state (median %.3f cm over %d states, bound %.1f)"),
			*Stem, All.Median(), All.Count(), GIdentityMedianToleranceCm),
			All.Count() > 0 && All.Median() <= GIdentityMedianToleranceCm);
		TestEqual(FString::Printf(TEXT("%s: every oracle state composes"), *Stem), Failed, 0);
	}
	TestTrue(TEXT("at least one oracle state composed"), Composed > 0);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
