#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Visual/ElysiumNativeAnimationData.h"
#include "Visual/ElysiumAnimationResolve.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "ElysiumBodyData.h"
#include "ElysiumCastData.h"
#include "ElysiumClipData.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNativeAnimationDataTest,"Elysium.Content.NativeAnimationData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumNativeAnimationDataTest::RunTest(const FString&)
{
	UGameInstance* Game=NewObject<UGameInstance>();
	TStrongObjectPtr<UElysiumNativeAnimationData> KeepLibrary(NewObject<UElysiumNativeAnimationData>(Game));
	auto* Library=KeepLibrary.Get();
	const FString Id=TEXT("vtmb:model:character/npc/unique/downtown/lacroix/lacroix");
	TestNull(TEXT("lookup does not load unprepared body data"),Library->Vocabulary(Id));
	FString Error;
	TestNull(TEXT("mesh lookup cannot start preparation"),Library->Mesh(Id,Error));
	auto Load=Library->PrepareMapModels({Id,TEXT("LACROIX"),TEXT("*1"),TEXT("models/not_in_the_corpus.mdl")},Error,77);
	if (!TestTrue(TEXT("native preparation starts"),Load.IsValid())) { AddError(Error); return false; }
	Load->WaitUntilComplete();
	TestTrue(TEXT("native request finishes with every asset and compilation complete"),
		UElysiumNativeAnimationData::FinishPreparation(Load,Error));
	const UElysiumBodyData* Body=Library->Body(Id);
	TestNotNull(TEXT("body mesh is resident in the same native request"),Library->Mesh(Id,Error));
	const auto* Vocabulary=Library->Vocabulary(Id);
	if (!TestNotNull(TEXT("native vocabulary prepared"),Vocabulary) || !TestNotNull(TEXT("native body prepared"),Body)) return false;
	TestEqual(TEXT("cast key still seeds selection"),Vocabulary->Stem,FString(TEXT("lacroix")));
	FElysiumAnimationCatalog Catalog;
	Catalog.Clips=Vocabulary;
	Catalog.BlendTableFor=[Library](const FString& Owner){return Library->BlendTable(Owner).Get();};
	FElysiumAnimationIntent Intent;
	Intent.Stem=Id; Intent.Route=EElysiumAnimRoute::ExactLabel; Intent.SequenceLabel=TEXT("walk");
	FElysiumAnimationSelection Selection;
	ElysiumAnimResolve::Resolve(Intent,Catalog,Selection);
	TestEqual(TEXT("native metadata retains the locomotion grid"),Selection.AssetKind,EElysiumAnimAssetKind::BlendSpace);
	TestEqual(TEXT("native grid retains its steering axis"),Selection.AxisName[0],FString(TEXT("move_yaw")));
	const auto* Row=Body->Find(Selection.SequenceLabel,Selection.OwnerStem);
	if (!TestNotNull(TEXT("selection addresses its cooked row"),Row)) return false;
	TestNotNull(TEXT("grid reference is already resident"),Library->BlendSpace(Row->Assets));
	TestTrue(TEXT("native grid asset lookup preserves case-insensitive names"),
		Library->BlendSpace(Selection.OwnerStem,Selection.SequenceLabel.ToUpper())==Library->BlendSpace(Row->Assets));
	const auto Table=Library->BlendTable(Selection.OwnerStem);
	if (!TestTrue(TEXT("grid values read from native metadata"),Table.IsValid())) return false;
	const auto* Grid=Table->Find(TEXT("walk"));
	if (!TestNotNull(TEXT("native grid preserves cells"),Grid)) return false;
	TestEqual(TEXT("all locomotion directions retained"),Grid->Cells.Num(),9);
	const auto* Meta=Library->ClipData(Id,TEXT("walk"));
	TestNotNull(TEXT("loaded clip carries its authored metadata"),Meta);
	TestTrue(TEXT("native movement schema remains explicitly stated"),Table->bMovementStated);
	bool ComparedMovement=false, ComparedEvents=false;
	for (const auto& Pair : Library->Body(Selection.OwnerStem)->NativeSequences)
	{
		const auto* Clip=Pair.Value.Get();
		const auto* Facts=Clip?Clip->FindMetaDataByClass<UElysiumClipData>():nullptr;
		if (!Facts) continue;
		if (!Facts->Movement.Records.IsEmpty() && !ComparedMovement)
		{
			const auto* Path=Table->FindMovement(Facts->SourceLabel);
			if (TestNotNull(TEXT("native movement path reaches the existing mover view"),Path))
				TestEqual(TEXT("movement record count is preserved"),Path->Records.Num(),Facts->Movement.Records.Num());
			ComparedMovement=true;
		}
		if (!Facts->Events.IsEmpty() && !ComparedEvents)
		{
			const auto* Events=Table->FindEvents(Facts->SourceLabel);
			if (TestNotNull(TEXT("native events reach the existing timeline view"),Events))
				TestEqual(TEXT("event count is preserved"),Events->Num(),Facts->Events.Num());
			ComparedEvents=true;
		}
	}
	TestTrue(TEXT("real bank movement was exercised"),ComparedMovement);
	TestTrue(TEXT("real bank event timeline was exercised"),ComparedEvents);
	Library->ReleaseEpoch(76);
	TestNotNull(TEXT("retiring another map epoch preserves this body"),Library->Body(Id));
	Library->ReleaseEpoch(77);
	TestNull(TEXT("epoch release drops body lookup"),Library->Body(Id));
	TestNull(TEXT("epoch release drops mesh lookup even if the package is still loaded"),Library->Mesh(Id,Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNativeCinematicDataTest,"Elysium.Content.NativeCinematicData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumNativeCinematicDataTest::RunTest(const FString&)
{
	UGameInstance* Game=NewObject<UGameInstance>();
	TStrongObjectPtr<UElysiumNativeAnimationData> KeepLibrary(NewObject<UElysiumNativeAnimationData>(Game));
	auto* Library=KeepLibrary.Get();
	const FString Model=TEXT("models/cinematic/tutorial/jack_vs_sabbat.mdl");
	const FString Id=TEXT("vtmb:model:cinematic/tutorial/jack_vs_sabbat");
	FString Error;
	TestNull(TEXT("unprepared actor lookup does not load"),Library->CinematicBody(Model,TEXT("Bip01")));
	const auto* CastData=LoadObject<UElysiumCastData>(nullptr,TEXT("/ElysiumBaked/Models/_Corpus/DA_Cast.DA_Cast"));
	if (!TestNotNull(TEXT("whole cinematic cast table is present"),CastData)) return false;
	TArray<FString> Sets; CastData->Cinematics.GetKeys(Sets);
	if (!TestTrue(TEXT("whole cinematic corpus is nonempty"),!Sets.IsEmpty())) return false;
	auto Load=Library->PrepareMapModels({TEXT("smiling_jack")},Error,91,Sets);
	if (!TestTrue(TEXT("map prepares all cinematic owners"),Load.IsValid())) { AddError(Error); return false; }
	Load->WaitUntilComplete();
	if (!TestTrue(TEXT("cinematic request finishes"),Library->FinishPreparation(Load,Error))) { AddError(Error); return false; }
	TestNull(TEXT("multi-root set has no fabricated main owner"),Library->Body(Model));
	TestNull(TEXT("empty root cannot select one of four actors"),Library->CinematicBody(Model,FString()));
	TestNull(TEXT("wrong root cannot select another actor"),Library->CinematicBody(Model,TEXT("Bip99")));
	TSet<const UElysiumBodyData*> Owners;
	TSet<UAnimSequence*> Clips;
	TSet<const FElysiumBlendTable*> Tables;
	for (const TCHAR* Root : {TEXT("Bip01"),TEXT("Bip02"),TEXT("Bip03"),TEXT("Bip04")})
	{
		const auto* Owner=Library->CinematicBody(Model,Root);
		if (!TestNotNull(TEXT("authored actor owner is prepared"),Owner)) continue;
		Owners.Add(Owner);
		TestEqual(TEXT("owner retains unit id"),Owner->AssetId,Id);
		TestEqual(TEXT("owner retains actor root"),Owner->OwnerRoot,FString(Root));
		TestTrue(TEXT("case-folded root resolves identical owner"),Owner==Library->CinematicBody(Id,FString(Root).ToLower()));
		auto* Clip=Library->Sequence(Owner,TEXT("entire_scene"));
		if (TestNotNull(TEXT("actor's sequence is already resident"),Clip)) Clips.Add(Clip);
		TestTrue(TEXT("native scene clip lookup preserves case-insensitive names"),
			Clip==Library->Sequence(Owner,TEXT("ENTIRE_SCENE")));
		const auto* Meta=Library->ClipData(Owner,TEXT("entire_scene"));
		if (TestNotNull(TEXT("actor metadata is resident"),Meta))
			TestEqual(TEXT("clip belongs to the requested actor"),Meta->OwnerRoot,FString(Root));
		const auto Table=Library->BlendTable(Owner);
		if (TestTrue(TEXT("actor timeline view is complete"),Table.IsValid())) Tables.Add(Table.Get());
	}
	TestEqual(TEXT("all four owner tables remain distinct"),Owners.Num(),4);
	TestEqual(TEXT("all four performances remain distinct"),Clips.Num(),4);
	TestEqual(TEXT("timeline cache cannot alias actors of one unit"),Tables.Num(),4);
	int32 OwnerCount=0, ClipCount=0;
	for (const auto& Set : CastData->Cinematics)
		for (const auto& Root : Set.Value.Roots)
		{
			const auto* Owner=Library->CinematicBody(Set.Key,Root.Key);
			if (!TestNotNull(*FString::Printf(TEXT("prepared cinematic %s [%s]"),*Set.Key,*Root.Key),Owner)) continue;
			++OwnerCount;
			TestEqual(TEXT("cinematic root alias retains its actual owner"),Owner->OwnerRoot,Root.Value.OwnerRoot);
			if (Set.Value.Roots.Num()==1)
				TestTrue(TEXT("single-root set permits omitted root"),Owner==Library->CinematicBody(Set.Key,FString()));
			for (const auto& Pair : Owner->NativeSequences)
			{
				auto* Sequence=Library->Sequence(Owner,Pair.Key);
				if (!TestNotNull(TEXT("every cinematic sequence is resident"),Sequence)) continue;
				++ClipCount;
				const auto* Meta=Sequence->FindMetaDataByClass<UElysiumClipData>();
				if (TestNotNull(TEXT("every cinematic sequence retains metadata"),Meta))
				{
					TestEqual(TEXT("sequence has the correct model owner"),Meta->AssetId,Owner->AssetId);
					TestEqual(TEXT("sequence has the correct actor owner"),Meta->OwnerRoot,Owner->OwnerRoot);
				}
			}
		}
	AddInfo(FString::Printf(TEXT("Native cinematic preparation: %d sets, %d owner roots, %d clips"),Sets.Num(),OwnerCount,ClipCount));
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
		{
			TestWorld.ForwardErrorMessages(this); return false;
		}
		auto* Mesh=Library->Mesh(TEXT("smiling_jack"),Error);
		if (!TestNotNull(TEXT("native scene body is resident"),Mesh)) return false;
		auto* Actor=TestWorld.GetTestWorld()->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("native scene body owner spawns"),Actor)) return false;
		auto* Component=NewObject<USkeletalMeshComponent>(Actor);
		Component->SetSkeletalMeshAsset(Mesh);
		Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Component->SetAnimInstanceClass(UElysiumBipedAnimInstance::StaticClass());
		Actor->SetRootComponent(Component); Component->RegisterComponent();
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		auto* Instance=Cast<UElysiumBipedAnimInstance>(Component->GetAnimInstance());
		if (!TestNotNull(TEXT("native scene animation host installs"),Instance)) return false;
		for (const TCHAR* Root : {TEXT("Bip01"),TEXT("Bip02")})
		{
			const auto* Owner=Library->CinematicBody(Model,Root);
			auto* Clip=Library->Sequence(Owner,TEXT("entire_scene"));
			Instance->PlayClip({Id,TEXT("entire_scene"),Root},Clip,false);
			// The clip-player phase observes the preceding completed graph update.
			for (int32 Frame=0; Frame<2; ++Frame)
			{
				Component->TickAnimation(1.f/30.f,false);
				Component->RefreshBoneTransforms(nullptr);
			}
			FElysiumClipPhase Phase;
			if (TestTrue(TEXT("native cinematic host publishes a live phase"),Instance->GetClipPhase(EElysiumAnimChannel::Base,Phase)))
			{
				TestEqual(TEXT("published phase retains actor root"),Phase.OwnerRoot,FString(Root));
				TestEqual(TEXT("published phase retains unit id"),Phase.OwnerStem,Id);
				const auto* TimelineOwner=Library->CinematicBody(Phase.OwnerStem,Phase.OwnerRoot);
				TestTrue(TEXT("published phase rejoins the exact cooked actor timeline"),TimelineOwner==Owner);
				TestTrue(TEXT("native scene clock advanced"),Phase.Cycle>0.f);
			}
		}
		Instance->StopClip();
	}
	Library->ReleaseEpoch(91);
	TestNull(TEXT("released epoch drops actor lookup"),Library->CinematicBody(Model,TEXT("Bip01")));
	Load=Library->PrepareMapModels({},Error,92,{TEXT("models/missing_cinematic.mdl")});
	TestFalse(TEXT("missing authored cinematic refuses preparation"),Load.IsValid());
	TestFalse(TEXT("missing authored cinematic has a diagnostic"),Error.IsEmpty());
	TestNull(TEXT("failed discovery publishes no owner"),Library->CinematicBody(Model,TEXT("Bip01")));
	return true;
}
#endif
