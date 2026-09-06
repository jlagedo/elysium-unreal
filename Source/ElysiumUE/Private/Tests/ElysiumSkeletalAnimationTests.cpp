// Headless contracts for the MDL -> `.eskm` -> baked-asset skeletal-animation seam. The structural
// test reads every generated container without constructing rendering resources; the theatre test
// then exercises the mount's own bodies and their UAnimSequence-to-USkeleton binding on PP2's cast.

#include "Misc/AutomationTest.h"
#include "Algo/AllOf.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/ElysiumNativeCharacterTestData.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumSceneData.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationCommon.h"

static constexpr EAutomationTestFlags GElysiumSkeletalContentFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The upright envelope, asserted on the BAKED clip.
	//
	// The export deliberately forwards VtMB's split-rotation rule instead of resolving it: a bank
	// ships `Bip01 Spine1`'s raw channel plus a `split_bones` inventory naming that bone, which the
	// catalogue test asserts is present. Ordinary FK over that channel is therefore not a pose -- it bends
	// the upper body sideways on all 67 clips of the shared male stances bank, and on any other
	// clip of any rig that flags the bone. The `.eskm` -> `.uasset` bake is where the rule is
	// resolved (`UE_mdl_skeletal._split_rotation_tracks`), so the mount is the only artifact that
	// can promise an upright neutral idle, and it is the one the game plays.
	bool ValidateBakedNeutralStance(FAutomationTestBase& Test, const FElysiumNpcIndex& Index)
	{
		// The first indexed body that can actually evaluate the clip: the bank is baked once against
		// a skeleton of its own, so what makes a body eligible is the compatibility declaration plus
		// carrying the two bones this measures -- not which stem it is.
		FString Stem;
		USkeletalMesh* Mesh = nullptr;
		UAnimSequence* Anim = nullptr;
		TArray<FString> Stems;
		Index.Npcs.GetKeys(Stems);
		Stems.Sort([](const FString& A, const FString& B) { return A < B; });
		for (const FString& Candidate : Stems)
		{
			USkeletalMesh* Body = ElysiumNpcVisual::LoadBakedMesh(Candidate);
			if (Body == nullptr || Body->GetRefSkeleton().FindBoneIndex(FName(TEXT("Bip01 Head")))
				== INDEX_NONE)
			{
				continue;
			}
			UAnimSequence* Clip = ElysiumNpcVisual::LoadBakedClip(
				Body, TEXT("character_shared_male_stances"), TEXT("Stance_Neutral_Idle_1"));
			const USkeleton* BodySkeleton = Body->GetSkeleton();
			if (Clip == nullptr || BodySkeleton == nullptr)
			{
				continue;
			}
			if (Clip->GetSkeleton() != BodySkeleton
				&& !BodySkeleton->IsCompatibleForEditor(Clip->GetSkeleton()))
			{
				continue;
			}
			Stem = Candidate;
			Mesh = Body;
			Anim = Clip;
			break;
		}
		if (Anim == nullptr)
		{
			Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no baked body can evaluate the shared male stances neutral idle"));
			return true;
		}
		const IAnimationDataModel* Model = Anim->GetDataModel();
		if (Model == nullptr)
		{
			Test.AddError(TEXT("neutral stance: the baked clip carries no animation model"));
			return false;
		}

		// Composed up the mesh's own reference skeleton. A bone the clip does not track holds its
		// bind pose, which is what the evaluator does with it too.
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		auto ComponentSpace = [&](const FName BoneName) -> FTransform
		{
			FTransform Out = FTransform::Identity;
			int32 Index = Ref.FindBoneIndex(BoneName);
			while (Index != INDEX_NONE)
			{
				const FName Name = Ref.GetBoneName(Index);
				FTransform Local = Ref.GetRefBonePose()[Index];
				if (Model->IsValidBoneTrackName(Name))
				{
					Local = Model->GetBoneTrackTransform(Name, FFrameNumber(0));
				}
				Out = Out * Local;
				Index = Ref.GetParentIndex(Index);
			}
			return Out;
		};

		const int32 PelvisIndex = Ref.FindBoneIndex(FName(TEXT("Bip01 Pelvis")));
		const int32 HeadIndex = Ref.FindBoneIndex(FName(TEXT("Bip01 Head")));
		if (PelvisIndex == INDEX_NONE || HeadIndex == INDEX_NONE)
		{
			Test.AddError(FString::Printf(
				TEXT("neutral stance: %s has no Bip01 Pelvis / Bip01 Head"), *Stem));
			return false;
		}
		const FVector Pelvis = ComponentSpace(FName(TEXT("Bip01 Pelvis"))).GetTranslation();
		const FVector Head = ComponentSpace(FName(TEXT("Bip01 Head"))).GetTranslation();

		// Unreal-native and in centimetres: Z is up, Y is lateral.
		const double Rise = FMath::Abs(Head.Z - Pelvis.Z);
		const double Lateral = FMath::Abs(Head.Y - Pelvis.Y);
		if (Rise < 1.0 || Lateral / Rise > 0.05)
		{
			Test.AddError(FString::Printf(
				TEXT("baked neutral stance leans sideways: lateral %.3f cm / rise %.3f cm"),
				Lateral, Rise));
			return false;
		}
		Test.AddInfo(FString::Printf(
			TEXT("baked neutral stance on %s: lateral %.3f cm / rise %.3f cm = %.4f"),
			*Stem, Lateral, Rise, Lateral / Rise));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkeletalCatalogueTest,
	"Elysium.Content.SkeletalCatalogue", GElysiumSkeletalContentFlags)
bool FElysiumSkeletalCatalogueTest::RunTest(const FString&)
{
	if (!ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native character cast (run: uv run elysium import characters)"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!ElysiumNativeTest::Load(Index, Error))
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: no NPC index (%s)"), *Error));
		return true;
	}

	bool bValid = true;
	int32 SplitBodies = 0;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
	{
		if (!Pair.Value.SplitRotationBones.IsEmpty())
		{
			++SplitBodies;
			// One split bone per biped root: `Bip01 Spine1` for a body, `Bip01`/`Bip02`/... `Spine1`
			// for a multi-biped group prop (lap_dancegroup_4 carries two bipeds). The corpus-wide
			// measurement (all 373 legacy bodies split at `Bip01 Spine1`) still holds per biped.
			const bool bEveryBipedSpine = Algo::AllOf(Pair.Value.SplitRotationBones, [](const FString& Bone)
			{
				return Bone.Len() == 12 && Bone.StartsWith(TEXT("Bip"), ESearchCase::CaseSensitive)
					&& FChar::IsDigit(Bone[3]) && FChar::IsDigit(Bone[4])
					&& Bone.EndsWith(TEXT(" Spine1"), ESearchCase::CaseSensitive);
			});
			if (Pair.Value.SplitRotationBones.IsEmpty() || !bEveryBipedSpine
				|| TSet<FString>(Pair.Value.SplitRotationBones).Num() != Pair.Value.SplitRotationBones.Num())
			{
				AddError(FString::Printf(
					TEXT("%s carries an unexpected Flags & 2 inventory: %s"),
					*Pair.Key, *FString::Join(Pair.Value.SplitRotationBones, TEXT(", "))));
				bValid = false;
			}
		}
	}
	bValid &= ValidateBakedNeutralStance(*this, Index);

	if (Index.Npcs.Num() >= 150)
	{
		TestTrue(TEXT("the complete generated cast preserves target-model Flags & 2 metadata"),
			SplitBodies > 100);
	}
	else
	{
		AddInfo(FString::Printf(TEXT("partial NPC index: validated Flags & 2 metadata on %d of %d bodies"),
			SplitBodies, Index.Npcs.Num()));
	}
	if (const FElysiumNpcIndexEntry* CourtroomBody =
		Index.Npcs.Find(TEXT("ventrue_female_armor_1")))
	{
		TestTrue(TEXT("the seated courtroom player body records Bip01 Spine1 Flags & 2"),
			CourtroomBody->SplitRotationBones.Contains(TEXT("Bip01 Spine1")));
	}
	else
	{
		AddError(TEXT("ventrue_female_armor_1 is absent from the generated target-model index"));
		bValid = false;
	}
	TestTrue(TEXT("the skeletal catalogue and the baked neutral stance hold"), bValid);
	return true;
}

namespace
{
	const FString* FindKeyIgnoreCase(const TMap<FString, FString>& Keys, const FString& Wanted)
	{
		for (const TPair<FString, FString>& Pair : Keys)
		{
			if (Pair.Key.Equals(Wanted, ESearchCase::IgnoreCase))
			{
				return &Pair.Value;
			}
		}
		return nullptr;
	}

	FString NormalizeModelPath(FString Path)
	{
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		Path.ToLowerInline();
		if (!Path.StartsWith(TEXT("models/")))
		{
			Path = TEXT("models/") + Path;
		}
		return Path;
	}

	const FElysiumEntityDef* FindEntityByTarget(
		const FElysiumEntityDefs& Defs, const FString& Target)
	{
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			if (Def.TargetName.Equals(Target, ESearchCase::IgnoreCase))
			{
				return &Def;
			}
		}
		return nullptr;
	}

	FString ResolveSceneActorTarget(const FElysiumEntityDef& SceneDef, const FString& ActorName)
	{
		if (ActorName.Equals(TEXT("Player"), ESearchCase::IgnoreCase)
			|| ActorName.Equals(TEXT("!player"), ESearchCase::IgnoreCase)
			|| ActorName.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
		{
			return TEXT("!playercontroller");
		}
		for (int32 TargetIndex = 1; TargetIndex <= 4; ++TargetIndex)
		{
			const FString Token = FString::Printf(TEXT("!target%d"), TargetIndex);
			if (ActorName.Equals(Token, ESearchCase::IgnoreCase))
			{
				const FString* Value = FindKeyIgnoreCase(
					SceneDef.Keys, FString::Printf(TEXT("target%d"), TargetIndex));
				return Value != nullptr ? *Value : FString();
			}
		}
		return ActorName;
	}

	FString StemForModel(const FElysiumNpcIndex& Index, const FString& Model)
	{
		const FString Wanted = NormalizeModelPath(Model);
		for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
		{
			if (NormalizeModelPath(Pair.Value.Model) == Wanted)
			{
				return Pair.Key;
			}
		}
		return FString();
	}
}

// PP2's failure-sensitive integration: parse sp_theatre's real scene entities and VCDs, map each
// `entire_scene` actor through bonerename -> cinematic bank and targetname -> character model,
// then ask glTFRuntime to bind that bank clip to the target mesh's actual USkeleton under -nullrhi.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTheatreSkeletonBindingTest,
	"Elysium.Content.TheatreSkeletonBinding", GElysiumSkeletalContentFlags)
bool FElysiumTheatreSkeletonBindingTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("maps"))
		|| !ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the maps export domain is incomplete or there is no native character cast"));
		return true;
	}
	const FString EntsPath = FElysiumContentPaths::MapEnts(TEXT("sp_theatre"));
	if (!IFileManager::Get().FileExists(*EntsPath))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: sp_theatre is not exported"));
		return true;
	}

	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sp_theatre.ents parses"), FElysiumEntityDefs::Parse(EntsPath, Defs)))
	{
		return false;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(TEXT("NPC/cinematic index loads"), ElysiumNativeTest::Load(Index, Error)))
	{
		AddError(Error);
		return false;
	}

	TMap<FString, USkeletalMesh*> MeshCache;
	TArray<UObject*> KeepAlive;
	int32 SceneSets = 0;
	int32 ActorRoots = 0;
	int32 RuntimeBinds = 0;
	int32 PlayerBinds = 0;
	bool bValid = true;

	for (const FElysiumEntityDef& SceneDef : Defs.Defs)
	{
		if (SceneDef.Classname != TEXT("logic_choreographed_scene"))
		{
			continue;
		}
		const FString* SceneFile = FindKeyIgnoreCase(SceneDef.Keys, TEXT("SceneFile"));
		if (SceneFile == nullptr) continue;
		const TSharedPtr<const FElysiumSceneData> Scene = ElysiumScene::Load(*SceneFile);
		if (!Scene.IsValid())
		{
			AddError(FString::Printf(TEXT("%s: VCD does not parse"), *SceneDef.TargetName));
			bValid = false;
			continue;
		}

		TSet<int32> WholeCastActors;
		for (const FElysiumSceneEvent& Event : Scene->Events)
		{
			if ((Event.Type == EElysiumChoreoEvent::Sequence || Event.Type == EElysiumChoreoEvent::Gesture)
				&& Event.Param.Equals(TEXT("entire_scene"), ESearchCase::IgnoreCase))
			{
				WholeCastActors.Add(Event.ActorIndex);
			}
		}
		if (WholeCastActors.IsEmpty()) continue;

		TArray<FString> AnimModels;
		for (const TCHAR* Key : { TEXT("BaseAnim"), TEXT("MaleAnim"), TEXT("FemaleAnim") })
		{
			if (const FString* Model = FindKeyIgnoreCase(SceneDef.Keys, Key))
			{
				if (!Model->IsEmpty()) AnimModels.AddUnique(*Model);
			}
		}

		for (const FString& AnimModel : AnimModels)
		{
			++SceneSets;
			const FElysiumCinematicSet* Set = Index.FindCinematic(AnimModel);
			if (Set == nullptr)
			{
				AddError(FString::Printf(TEXT("%s: anim set is not indexed: %s"),
					*SceneDef.TargetName, *AnimModel));
				bValid = false;
				continue;
			}

			for (int32 ActorIndex : WholeCastActors)
			{
				if (!Scene->Actors.IsValidIndex(ActorIndex))
				{
					AddError(FString::Printf(TEXT("%s: entire_scene has invalid actor index %d"),
						*SceneDef.TargetName, ActorIndex));
					bValid = false;
					continue;
				}
				const FElysiumSceneActor& Actor = Scene->Actors[ActorIndex];
				const FString Bank = Set->BankForRoot(Actor.BoneFrom);
				++ActorRoots;
				if (Bank.IsEmpty() || !Index.Banks.Contains(Bank))
				{
					AddError(FString::Printf(TEXT("%s: %s root '%s' does not resolve exactly in %s"),
						*SceneDef.TargetName, *Actor.Name, *Actor.BoneFrom, *AnimModel));
					bValid = false;
					continue;
				}

				const FString Target = ResolveSceneActorTarget(SceneDef, Actor.Name);
				const bool bPlayerController =
					Target.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase);
				if (bPlayerController)
				{
					++PlayerBinds;
				}
				const FElysiumEntityDef* TargetDef =
					bPlayerController ? nullptr : FindEntityByTarget(Defs, Target);
				const FString* TargetModel = TargetDef != nullptr
					? FindKeyIgnoreCase(TargetDef->Keys, TEXT("model")) : nullptr;
				const FString Stem = bPlayerController
					? TEXT("brujah_male_armor_0")
					: (TargetModel != nullptr ? StemForModel(Index, *TargetModel) : FString());
				if (Stem.IsEmpty())
				{
					AddError(FString::Printf(TEXT("%s: actor %s target '%s' has no indexed skeletal model"),
						*SceneDef.TargetName, *Actor.Name, *Target));
					bValid = false;
					continue;
				}

				USkeletalMesh* Mesh = MeshCache.FindRef(Stem);
				if (Mesh == nullptr)
				{
					Mesh = ElysiumNpcVisual::LoadBakedMesh(Stem);
					if (Mesh == nullptr)
					{
						AddError(FString::Printf(TEXT("%s: target mesh %s is not on the baked mount"),
							*SceneDef.TargetName, *Stem));
						bValid = false;
						continue;
					}
					MeshCache.Add(Stem, Mesh);
					KeepAlive.Add(Mesh);
					Mesh->AddToRoot();
				}

				// Off the mount, like the body. The shared bank clip must be compatible with its body.
				UAnimSequence* Anim = ElysiumNpcVisual::LoadBakedClip(Mesh, Bank,
					TEXT("entire_scene"));
				const USkeleton* BodySkeleton = Mesh->GetSkeleton();
				if (Anim == nullptr || Anim->GetPlayLength() <= 0.f
					|| (Anim->GetSkeleton() != BodySkeleton
						&& !(BodySkeleton != nullptr
							&& BodySkeleton->IsCompatibleForEditor(Anim->GetSkeleton()))))
				{
					AddError(FString::Printf(
						TEXT("%s: %s/%s did not bind a baked entire_scene from '%s' to %s"),
						*SceneDef.TargetName, *AnimModel, *Actor.BoneFrom, *Bank, *Stem));
					bValid = false;
					continue;
				}
#if WITH_EDITOR
				IAnimationDataModel* DataModel = Anim->GetDataModel();
				TArray<FName> TrackNames;
				if (DataModel != nullptr) DataModel->GetBoneTrackNames(TrackNames);
				if (TrackNames.IsEmpty())
				{
					AddError(FString::Printf(TEXT("%s: %s produced an animation with no bone tracks"),
						*SceneDef.TargetName, *Stem));
					bValid = false;
					continue;
				}
				// A bank sequence is baked ONCE against a skeleton of its own and reached through
				// each body's compatibility declaration, so it carries the union of the bank's
				// bones by construction and a body that lacks one simply takes no track for it.
				// What has to hold is that this body shares enough of the clip to be driven by it,
				// and that every track it does share is a finite pose. (The old form asserted every
				// track existed on every body, which was a property of the retargeter's
				// `RemoveTracks` filter -- there is no filter on the mount, and asserting it of a
				// shared sequence fails on a body missing an optional hair or footstep bone.)
				const int32 LastFrame = DataModel->GetNumberOfFrames();
				int32 Shared = 0;
				for (const FName TrackName : TrackNames)
				{
					if (Mesh->GetRefSkeleton().FindBoneIndex(TrackName) == INDEX_NONE)
					{
						continue;
					}
					++Shared;
					for (const int32 Frame : { 0, LastFrame / 2, LastFrame })
					{
						const FTransform Pose = DataModel->GetBoneTrackTransform(TrackName, FFrameNumber(Frame));
						if (Pose.ContainsNaN() || !Pose.IsRotationNormalized())
						{
							AddError(FString::Printf(TEXT("%s: %s track '%s' has an invalid pose at frame %d"),
								*SceneDef.TargetName, *Stem, *TrackName.ToString(), Frame));
							bValid = false;
							break;
						}
					}
				}
				if (Shared == 0)
				{
					AddError(FString::Printf(
						TEXT("%s: '%s' shares no bone with %s, so the clip drives nothing on it"),
						*SceneDef.TargetName, *Bank, *Stem));
					bValid = false;
					continue;
				}
#endif
				KeepAlive.Add(Anim);
				Anim->AddToRoot();
				++RuntimeBinds;
			}
		}
	}

	for (UObject* Object : KeepAlive)
	{
		if (Object != nullptr && Object->IsRooted()) Object->RemoveFromRoot();
	}
	AddInfo(FString::Printf(TEXT("sp_theatre: %d anim sets, %d exact actor roots, %d runtime "
		"USkeleton binds; %d bind the default player body"),
		SceneSets, ActorRoots, RuntimeBinds, PlayerBinds));
	TestTrue(TEXT("every theatre cinematic clip binds to its target model's USkeleton"), bValid);
	TestTrue(TEXT("the theatre test exercised runtime bindings"), RuntimeBinds > 0);
	return true;
}

// The binding test above proves that the cinematic clip can be constructed, but not that the
// native animation host evaluates it into a changing component pose. Drive one real theatre
// `sequence` event through the same UAnimSequence and UElysiumBipedAnimInstance used by play, seek it
// to two authored scene times, and compare the bone transforms skinning consumes. This is pose data
// only: no viewport, RHI, screenshot, or image comparison.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTheatreSequenceEvaluationTest,
	"Elysium.Content.TheatreSequenceEvaluation", GElysiumSkeletalContentFlags)
bool FElysiumTheatreSequenceEvaluationTest::RunTest(const FString&)
{
	const FString EntsPath = FElysiumContentPaths::MapEnts(TEXT("sp_theatre"));
	if (!IFileManager::Get().FileExists(*EntsPath))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: sp_theatre is not exported"));
		return true;
	}

	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sp_theatre.ents parses"), FElysiumEntityDefs::Parse(EntsPath, Defs)))
	{
		return false;
	}

	const FElysiumEntityDef* SceneDef = nullptr;
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		if (Def.TargetName.Equals(TEXT("courtroom_scene_bip4"), ESearchCase::IgnoreCase))
		{
			SceneDef = &Def;
			break;
		}
	}
	if (!TestNotNull(TEXT("Nines' courtroom scene exists"), SceneDef))
	{
		return false;
	}

	const FString* SceneFile = FindKeyIgnoreCase(SceneDef->Keys, TEXT("SceneFile"));
	const FString* AnimModel = FindKeyIgnoreCase(SceneDef->Keys, TEXT("BaseAnim"));
	if (!TestNotNull(TEXT("the scene names a VCD"), SceneFile)
		|| !TestNotNull(TEXT("the scene names a cinematic animation set"), AnimModel))
	{
		return false;
	}
	const TSharedPtr<const FElysiumSceneData> Scene = ElysiumScene::Load(*SceneFile);
	if (!TestTrue(TEXT("Nines' courtroom VCD parses"), Scene.IsValid()))
	{
		return false;
	}

	const FElysiumSceneEvent* SequenceEvent = nullptr;
	const FElysiumSceneActor* SceneActor = nullptr;
	for (const FElysiumSceneEvent& Event : Scene->Events)
	{
		if (Event.Type != EElysiumChoreoEvent::Sequence
			|| !Event.Param.Equals(TEXT("entire_scene"), ESearchCase::IgnoreCase)
			|| !Scene->Actors.IsValidIndex(Event.ActorIndex))
		{
			continue;
		}
		const FElysiumSceneActor& Candidate = Scene->Actors[Event.ActorIndex];
		if (Candidate.Name.Equals(TEXT("Nines"), ESearchCase::IgnoreCase))
		{
			SequenceEvent = &Event;
			SceneActor = &Candidate;
			break;
		}
	}
	if (!TestNotNull(TEXT("the VCD carries Nines' entire_scene sequence event"), SequenceEvent)
		|| !TestNotNull(TEXT("the sequence event has an actor"), SceneActor))
	{
		return false;
	}

	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(TEXT("NPC/cinematic index loads"), ElysiumNativeTest::Load(Index, Error)))
	{
		AddError(Error);
		return false;
	}
	const FElysiumCinematicSet* Set = Index.FindCinematic(*AnimModel);
	if (!TestNotNull(TEXT("the cinematic animation set is indexed"), Set))
	{
		return false;
	}
	const FString Bank = Set->BankForRoot(SceneActor->BoneFrom);
	if (!TestTrue(TEXT("Nines' bonerename root resolves a cinematic bank"),
		!Bank.IsEmpty() && Index.Banks.Contains(Bank)))
	{
		return false;
	}

	const FString Target = ResolveSceneActorTarget(*SceneDef, SceneActor->Name);
	const FElysiumEntityDef* TargetDef = FindEntityByTarget(Defs, Target);
	const FString* TargetModel = TargetDef != nullptr
		? FindKeyIgnoreCase(TargetDef->Keys, TEXT("model")) : nullptr;
	const FString Stem = TargetModel != nullptr ? StemForModel(Index, *TargetModel) : FString();
	if (!TestTrue(TEXT("the sequence actor resolves Nines' target model"), !Stem.IsEmpty()))
	{
		return false;
	}

	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(Stem);
	if (!TestNotNull(TEXT("Nines' target mesh is on the baked mount"), Mesh))
	{
		return false;
	}
	// Off the mount, resolved through the body's compatible shared bank.
	UAnimSequence* Anim = ElysiumNpcVisual::LoadBakedClip(Mesh, Bank, SequenceEvent->Param);
	if (!TestNotNull(TEXT("the sequence event's clip is on the mount"), Anim))
	{
		return false;
	}

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("a skeletal body owner spawned"), Owner))
	{
		return false;
	}

	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Mesh);
	Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Comp->SetAnimInstanceClass(UElysiumBipedAnimInstance::StaticClass());
	Owner->SetRootComponent(Comp);
	Comp->RegisterComponent();
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Comp->GetAnimInstance());
	if (!TestNotNull(TEXT("the production NPC animation host is installed"), Inst))
	{
		return false;
	}

	const auto PoseAt = [Comp, Inst, Anim](float Seconds, TArray<FTransform>& OutPose)
	{
		Inst->PlayClip(FElysiumClipIdentity(), Anim, /*bLoop=*/false);
		Inst->SeekClip(Seconds);
		// Seek is consumed by UpdateAnimationNode; refresh synchronously so the transforms copied below
		// are this authored pose rather than the component's previous evaluation.
		Comp->TickAnimation(0.f, /*bNeedsValidRootMotion=*/false);
		Comp->RefreshBoneTransforms(/*TickFunction=*/nullptr);
		OutPose = Comp->GetComponentSpaceTransforms();
	};

	const float EventSeconds = SequenceEvent->EndTime - SequenceEvent->StartTime;
	const float LaterSeconds = FMath::Min(120.f, FMath::Max(0.f, EventSeconds - 1.f / 30.f));
	if (!TestTrue(TEXT("the real sequence spans the later sample"), LaterSeconds > 1.f))
	{
		return false;
	}
	TArray<FTransform> StartPose;
	TArray<FTransform> LaterPose;
	PoseAt(0.f, StartPose);
	PoseAt(LaterSeconds, LaterPose);
	if (!TestEqual(TEXT("both evaluations pose the complete skeleton"),
		StartPose.Num(), LaterPose.Num()) || StartPose.Num() != Mesh->GetRefSkeleton().GetNum())
	{
		return false;
	}

	int32 ChangedBones = 0;
	float MaxAngleDegrees = 0.f;
	float MaxTranslationCm = 0.f;
	FName MostChangedBone = NAME_None;
	for (int32 BoneIndex = 1; BoneIndex < StartPose.Num(); ++BoneIndex)
	{
		const float Angle = FMath::RadiansToDegrees(StartPose[BoneIndex].GetRotation().AngularDistance(
			LaterPose[BoneIndex].GetRotation()));
		const float Translation = FVector::Distance(
			StartPose[BoneIndex].GetTranslation(), LaterPose[BoneIndex].GetTranslation());
		if (Angle > 0.01f || Translation > 0.01f)
		{
			++ChangedBones;
		}
		if (Angle > MaxAngleDegrees || Translation > MaxTranslationCm)
		{
			MaxAngleDegrees = FMath::Max(MaxAngleDegrees, Angle);
			MaxTranslationCm = FMath::Max(MaxTranslationCm, Translation);
			MostChangedBone = Mesh->GetRefSkeleton().GetBoneName(BoneIndex);
		}
	}

	AddInfo(FString::Printf(TEXT("%s.%s: evaluated '%s' at 0.000s and %.3fs; %d/%d non-root "
		"bones changed (max %.2f deg, %.2f cm; representative '%s')"),
		*SceneDef->TargetName, *SceneActor->Name, *SequenceEvent->Param, LaterSeconds,
		ChangedBones, StartPose.Num() - 1, MaxAngleDegrees, MaxTranslationCm,
		*MostChangedBone.ToString()));
	TestTrue(TEXT("the cinematic sequence event produces a changing skeletal pose"), ChangedBones > 0);
	return true;
}

// of its own, `hit_yaw`, and every fact this test asserts is a fact a producer depends on: the fan
// is single-axis so one angle selects it, it is bound to the SECOND declared parameter so a
// resolver reading index 0 would steer a flinch by the walk direction, it spans the whole circle so
// a hit from any side has a cell, and the four cardinals reach four DIFFERENT cells — a fan whose
// directions collapsed onto one clip would play a plausible flinch and be invisible.
//
// The nine cell names are logged rather than asserted. They are a recovered fact about the shipped
// content, and pinning them here would turn a re-export into a test failure; what has to hold is
// that each of them is on the baked mount, which is what a producer actually asks for.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDamageFlinchGridTest,
	"Elysium.Content.DamageFlinchGrid", GElysiumSkeletalContentFlags)
bool FElysiumDamageFlinchGridTest::RunTest(const FString&)
{
	if (!ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native character cast (run: uv run elysium import characters)"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(TEXT("native cast view builds"), ElysiumNativeTest::Load(Index, Error)))
	{
		AddError(Error);
		return false;
	}

	// Which bank owns the reaction for each carrier, and one BAKED carrier per bank to load its
	// cells against — a bank clip is retargeted onto a body's skeleton, so the load needs a body.
	TArray<FString> Stems;
	Index.Npcs.GenerateKeyArray(Stems);
	Stems.Sort();
	TMap<FString, FString> BodyForOwner;
	int32 Carriers = 0;
	for (const FString& Stem : Stems)
	{
		FElysiumNpcClipSet Clips;
		FString ClipError;
		if (!ElysiumNativeTest::Load(Clips, Stem, ClipError))
		{
			continue;
		}
		const FElysiumNpcClip* Hit = Clips.Find(TEXT("hit_torso"));
		if (Hit == nullptr)
		{
			continue;
		}
		++Carriers;
		const FString Owner = Hit->IsOwnedBy(Stem) ? Stem : Hit->Owner;
		if (!BodyForOwner.Contains(Owner) && ElysiumNpcVisual::IsStemBaked(Stem))
		{
			BodyForOwner.Add(Owner, Stem);
		}
	}
	if (Carriers == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no indexed body carries `hit_torso`; "
			"run: uv run elysium import characters"));
		return true;
	}

	TArray<FString> Owners;
	BodyForOwner.GenerateKeyArray(Owners);
	Owners.Sort();
	AddInfo(FString::Printf(TEXT("%d indexed bod(ies) carry `hit_torso`, owned by %d bank(s)"),
		Carriers, Owners.Num()));
	if (Owners.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no carrier of `hit_torso` stands on the baked mount; "
			"run: uv run elysium import characters"));
		return true;
	}

	TArray<UObject*> KeepAlive;
	for (const FString& Owner : Owners)
	{
		const FString BodyStem = BodyForOwner[Owner];
		FElysiumBlendTable Table;
		FString TableError;
		if (!ElysiumNativeTest::Load(Table, FString::Printf(TEXT("blends/%s.json"), *Owner), TableError))
		{
			AddError(FString::Printf(TEXT("'%s' owns `hit_torso` and has no blend sidecar (%s)"),
				*Owner, *TableError));
			continue;
		}
		const FElysiumBlendGrid* Grid = Table.Find(TEXT("hit_torso"));
		if (!TestNotNull(*FString::Printf(TEXT("'%s' declares a `hit_torso` grid"), *Owner), Grid))
		{
			continue;
		}

		// --- the shape -------------------------------------------------------------------------
		TestEqual(*FString::Printf(TEXT("%s: `hit_torso` is a nine-cell fan"), *Owner),
			Grid->Cells.Num(), 9);
		TestEqual(*FString::Printf(TEXT("%s: nine cells on axis 0"), *Owner), Grid->GroupSize[0], 9);
		TestEqual(*FString::Printf(TEXT("%s: and one on axis 1, so it is single-axis"), *Owner),
			Grid->GroupSize[1], 1);
		TestEqual(*FString::Printf(TEXT("%s: bound to pose parameter index 1"), *Owner),
			Grid->ParamIndex[0], 1);
		TestEqual(*FString::Printf(TEXT("%s: with no second axis parameter"), *Owner),
			Grid->ParamIndex[1], static_cast<int32>(INDEX_NONE));
		TestEqual(*FString::Printf(TEXT("%s: spanning -180 degrees"), *Owner), Grid->ParamStart[0],
			-180.0f);
		TestEqual(*FString::Printf(TEXT("%s: to +180"), *Owner), Grid->ParamEnd[0], 180.0f);

		const FElysiumPoseParamDesc* Desc = Table.Param(Grid->ParamIndex[0]);
		if (!TestNotNull(*FString::Printf(TEXT("%s: the bound parameter is declared"), *Owner), Desc))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s: and it is `hit_yaw`"), *Owner), Desc->Name,
			FString(TEXT("hit_yaw")));
		TestEqual(*FString::Printf(TEXT("%s: which wraps over the whole circle"), *Owner), Desc->Loop,
			360.0f);
		TestEqual(*FString::Printf(TEXT("%s: from -180"), *Owner), Desc->Start, -180.0f);
		TestEqual(*FString::Printf(TEXT("%s: to +180"), *Owner), Desc->End, 180.0f);

		// --- the four cardinals reach four different cells --------------------------------------
		static const float Cardinals[4] = { 0.0f, 90.0f, 180.0f, -90.0f };
		static const TCHAR* const CardinalNames[4] = {
			TEXT("front"), TEXT("right"), TEXT("back"), TEXT("left") };
		TSet<int32> Selected;
		for (int32 Which = 0; Which < 4; ++Which)
		{
			FElysiumPoseParams Pose;
			Pose.Set(TEXT("hit_yaw"), Cardinals[Which]);
			const FElysiumBlendPick Pick = ElysiumBlendGrids::SelectCell(*Grid, Table, Pose);
			if (!TestNotNull(*FString::Printf(TEXT("%s: a hit from the %s selects a cell"),
				*Owner, CardinalNames[Which]), Pick.Cell))
			{
				continue;
			}
			TestFalse(*FString::Printf(TEXT("%s: the %s cell names an animation"), *Owner,
				CardinalNames[Which]), Pick.Cell->Clip.IsEmpty());
			Selected.Add(Pick.Index[0]);
		}
		TestEqual(*FString::Printf(
			TEXT("%s: the four cardinals reach four DIFFERENT cells"), *Owner), Selected.Num(), 4);

		// --- every cell is on the mount, against a body that resolves this bank ------------------
		USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(BodyStem);
		if (Mesh == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: carrier '%s' is not on the baked mount"), *Owner,
				*BodyStem));
			continue;
		}
		Mesh->AddToRoot();
		KeepAlive.Add(Mesh);

		// In AXIS order, which is the fan's own order and not the sidecar's declaration order.
		TArray<FString> Readout;
		int32 Loaded = 0;
		for (int32 Cell = 0; Cell < Grid->GroupSize[0]; ++Cell)
		{
			const FElysiumBlendCell* Entry = Grid->CellAt(Cell, 0);
			if (Entry == nullptr || Entry->Clip.IsEmpty())
			{
				AddError(FString::Printf(TEXT("%s: `hit_torso` cell %d names no animation"), *Owner,
					Cell));
				Readout.Add(TEXT("-"));
				continue;
			}
			Readout.Add(Entry->Clip);
			if (ElysiumNpcVisual::LoadBakedClip(Mesh, Owner, Entry->Clip) != nullptr)
			{
				++Loaded;
			}
			else
			{
				AddError(FString::Printf(
					TEXT("%s: `hit_torso` cell %d ('%s') did not load off the mount for '%s'"),
					*Owner, Cell, *Entry->Clip, *BodyStem));
			}
		}
		TestEqual(*FString::Printf(TEXT("%s: all nine cells load off the baked mount"), *Owner),
			Loaded, Grid->GroupSize[0]);

		// The recovered readout: which authored reaction each direction of the fan actually names.
		AddInfo(FString::Printf(TEXT("%s (via '%s'), -180 -> +180: %s"), *Owner, *BodyStem,
			*FString::Join(Readout, TEXT(", "))));
	}

	for (UObject* Object : KeepAlive)
	{
		Object->RemoveFromRoot();
	}
	return true;
}


// The sequence event timelines: what a clip fires, and at what phase of its own cycle.
//
// The same shape as the autolayer test above — every assertion crosses the sidecar against something
// ANOTHER export declares. `event_sequences` in `npc_index.json` is the rollup the exporter counted
// as it wrote the file, so a timeline the parser refused shows up here as a disagreement between the
// two rather than as a silently shorter table. Whether the timeline is faithful to the `.mdl` is
// measured offline against the install, which bring-your-own-game keeps out of the repo.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSequenceEventTest,
	"Elysium.Content.SequenceEvents", GElysiumSkeletalContentFlags)
bool FElysiumSequenceEventTest::RunTest(const FString&)
{
	if (!ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native character cast (run: uv run elysium import characters)"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(TEXT("native cast view builds"), ElysiumNativeTest::Load(Index, Error)))
	{
		AddError(Error);
		return false;
	}
	if (Index.ManifestVersion < 8)
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: manifest v%d predates the event timelines"),
			Index.ManifestVersion));
		return true;
	}

	int32 Owners = 0, Sequences = 0, Records = 0;
	TSet<int32> Ids;
	bool bConformant = true;

	// One owner: its sidecar against the rollup the index carries for it. `Rollup` is how many
	// sequences the exporter wrote a timeline for, so the two disagreeing means the runtime dropped
	// rows — never a shorter table nobody noticed.
	auto Visit = [this, &Owners, &Sequences, &Records, &Ids, &bConformant](
		const FString& Stem, const FString& RelPath, int32 Rollup)
	{
		if (RelPath.IsEmpty())
		{
			if (Rollup != 0)
			{
				AddError(FString::Printf(TEXT("'%s' claims %d event timeline(s) and names no "
					"sidecar"), *Stem, Rollup));
				bConformant = false;
			}
			return;
		}
		FElysiumBlendTable Table;
		FString TableError;
		if (!ElysiumNativeTest::Load(Table, RelPath, TableError))
		{
			// An unreadable sidecar is the autolayer/grid tests' fault to report as well; say it
			// once here only when this owner was supposed to carry a timeline.
			if (Rollup != 0)
			{
				AddError(FString::Printf(TEXT("'%s' declares %d event timeline(s) and its sidecar "
					"does not load: %s"), *Stem, Rollup, *TableError));
				bConformant = false;
			}
			return;
		}
		if (Table.Events.Num() != Rollup)
		{
			AddError(FString::Printf(TEXT("'%s' parses %d event timeline(s); the index counted %d "
				"(%s)"), *Stem, Table.Events.Num(), Rollup,
				TableError.IsEmpty() ? TEXT("no reported fault") : *TableError));
			bConformant = false;
		}
		if (Table.Events.IsEmpty())
		{
			return;
		}
		++Owners;
		Sequences += Table.Events.Num();
		for (const TPair<FString, TArray<FElysiumAnimEvent>>& Timeline : Table.Events)
		{
			for (const FElysiumAnimEvent& Record : Timeline.Value)
			{
				++Records;
				Ids.Add(Record.Event);
				// The parser refuses a phase outside 0..1, so reaching this loop with one would mean
				// the refusal stopped working. Retail's census puts every shipped record at type 0;
				// the type is carried verbatim, so a non-zero one here is real and unhandled.
				if (Record.Cycle < 0.f || Record.Cycle > 1.f)
				{
					AddError(FString::Printf(TEXT("%s '%s': record %d sits at cycle %f"),
						*Stem, *Timeline.Key, Record.Event, Record.Cycle));
					bConformant = false;
				}
				if (Record.Type != 0)
				{
					AddError(FString::Printf(TEXT("%s '%s': record %d carries type %d; every "
						"shipped v2531 record is type 0"), *Stem, *Timeline.Key, Record.Event,
						Record.Type));
					bConformant = false;
				}
			}
		}
	};

	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
	{
		Visit(Pair.Key, Pair.Value.Blends, Pair.Value.EventSequences);
	}
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Banks)
	{
		Visit(Pair.Key, Pair.Value.Blends, Pair.Value.EventSequences);
	}
	for (const TPair<FString, FElysiumAnimatedPropEntry>& Pair : Index.PlacedModels)
	{
		Visit(Pair.Value.Stem, Pair.Value.Blends, Pair.Value.EventSequences);
	}

	if (Owners == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: this export declares no sequence event timeline"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d owner(s), %d sequence(s), %d record(s), %d distinct id(s)"),
		Owners, Sequences, Records, Ids.Num()));
	TestTrue(TEXT("every timeline matches its rollup and every record is an in-range type 0"),
		bConformant);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
