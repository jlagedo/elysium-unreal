#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumSkeletalBuild.h"
#include "ElysiumMapBakeLibrary.h"
#include "ElysiumCharacterProvenance.h"
#include "ElysiumCharacterBakeLibrary.h"
#include "ElysiumClipData.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Animation/BlendProfile.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMeshSocket.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace ElysiumSkeletalStageTest
{
template<typename T> void Append(TArray<uint8>& Bytes, T Value)
{
	Bytes.Append(reinterpret_cast<const uint8*>(&Value), sizeof(T));
}
void String(TArray<uint8>& Bytes, const TCHAR* Value)
{
	FTCHARToUTF8 Encoded(Value);
	Append<uint32>(Bytes, Encoded.Length());
	Bytes.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
}
void Local(TArray<uint8>& Bytes, float X)
{
	for (float Value : {X, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f}) Append(Bytes, Value);
}
TArray<uint8> Source(float HandLocalX = 10.f, bool bMixedSkin = false)
{
	TArray<uint8> Bones, Attachments, Materials, Mesh, Animations, Morphs;
	Append<uint32>(Bones, 2);
	String(Bones, TEXT("root")); Append<int32>(Bones, -1); Local(Bones, 0.f);
	String(Bones, TEXT("hand")); Append<int32>(Bones, 0); Local(Bones, HandLocalX);
	Append<uint32>(Attachments, 1); String(Attachments, TEXT("TrailTip"));
	Append<uint32>(Attachments, 1); Local(Attachments, 1.f);
	Append<uint32>(Materials, 1); String(Materials, TEXT("material")); String(Materials, TEXT(""));
	Append<uint32>(Mesh, 3); Append<uint32>(Mesh, 1); Append<uint32>(Mesh, 1);
	String(Mesh, TEXT("material")); Append<uint32>(Mesh, 0); Append<uint32>(Mesh, 1);
	for (const FVector3f Position : {FVector3f(10.f, 0.f, 0.f), FVector3f(11.f, 0.f, 0.f), FVector3f(10.f, 1.f, 0.f)})
	{
		for (float Value : {Position.X, Position.Y, Position.Z, bMixedSkin ? 1.f : 0.f, 0.f, bMixedSkin ? 0.f : 1.f, 0.f, 0.f}) Append(Mesh, Value);
		for (uint16 Bone : {uint16(1), uint16(0), uint16(0), uint16(0)}) Append(Mesh, Bone);
		for (float Weight : {bMixedSkin ? .5f : 1.f, bMixedSkin ? .5f : 0.f, 0.f, 0.f}) Append(Mesh, Weight);
	}
	for (uint32 Index : {0u, 2u, 1u}) Append(Mesh, Index);
	Append<uint32>(Animations, 1); String(Animations, TEXT("step.test")); String(Animations, TEXT(""));
	Append<uint32>(Animations, 2); Append<float>(Animations, 30.f);
	Append<uint32>(Animations, 0); Append<int32>(Animations, -1); Append<uint32>(Animations, 1);
	Append<uint32>(Animations, 0); Append<uint8>(Animations, 1); Append<uint8>(Animations, 1);
	for (float Value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f}) Append(Animations, Value);
	for (float Value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f}) Append(Animations, Value);
	Append<uint32>(Morphs, 1); String(Morphs, TEXT("stage_morph"));
	Append<uint32>(Morphs, 1); Append<uint32>(Morphs, 0);
	for (float Value : {2.f, 0.f, 0.f, bMixedSkin ? 0.f : 1.f, bMixedSkin ? 1.f : 0.f, 0.f}) Append(Morphs, Value);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>("ESKM"), 4);
	Append<uint32>(Bytes, 8); Append<uint32>(Bytes, 6); Append<uint32>(Bytes, 0);
	const char* Tags[] = {"SKEL", "ATCH", "MATL", "MESH", "ANIM", "MORF"};
	const TArray<uint8>* Parts[] = {&Bones, &Attachments, &Materials, &Mesh, &Animations, &Morphs};
	uint64 Offset = 16 + 6 * 20;
	for (int32 I = 0; I < 6; ++I)
	{
		Bytes.Append(reinterpret_cast<const uint8*>(Tags[I]), 4);
		Append(Bytes, Offset); Append<uint64>(Bytes, Parts[I]->Num()); Offset += Parts[I]->Num();
	}
	for (const auto* Part : Parts) Bytes.Append(*Part);
	return Bytes;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkeletalStageBuildTest,
	"Elysium.Substrate.SkeletalStageBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumSkeletalStageBuildTest::RunTest(const FString&)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Elysium/Tests/stage_build.skel");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!TestTrue(TEXT("synthetic stage written"), FFileHelper::SaveArrayToFile(ElysiumSkeletalStageTest::Source(), *Path))) return false;
	UMaterial* Material = UMaterial::GetDefaultMaterial(MD_Surface);
	const TMap<FString, FString> Bindings = {{TEXT("material"), Material->GetPathName()}};
	const TArray<FTransform> Pose = {FTransform::Identity,
		FTransform(FQuat(FVector::UpVector, PI / 2.), FVector(20., 0., 0.))};
	const FString Package = TEXT("/Game/ElysiumGenerated/Tests/R8/SK_StageBuild");
	const FString Error = UElysiumSkeletalBuildLibrary::BuildSkeletalMeshFromStage(
		Path, Package, FString(), Bindings, Pose, TEXT("synthetic-stage-build"));
	if (!TestEqual(TEXT("native stage build succeeds"), Error, FString())) return false;
	USkeletalMesh* AuthoredMesh = LoadObject<USkeletalMesh>(nullptr, *Package);
	FString ProvenanceError;
	const FString ProvenanceJson = FString::Printf(TEXT(R"JSON({
		"assetId":"vtmb:model:test/stage","unitSha256":"unit-hash","payloadSha256":"payload-hash",
		"meshData":{"schemaVersion":"1.0.0","modelPath":"models/test/stage.mdl","stem":"stage",
			"facial":{"flexdescs":["stage_morph"],"controllers":[{"name":"blink","type":"eyelid","min":0,"max":1}],
				"rules":[{"flexdesc":0,"ops":[["FETCH1",0],["CONST",2],["DIV"]]}],
				"morphs":[{"name":"stage_morph","flexdesc":0,"targets":[0,1,10,11]}],
				"mouths":[{"bone":0,"forward":[0,1,0],"flexdesc":0},{"bone":1,"forward":[1,0,0],"flexdesc":0}],
				"phoneme_filter":[0.08,0.105]},
			"eyes":{"eyeballs":[{"index":0,"bone":"hand","bone_index":1,"body_part":2,"body_model":3,
				"org":[1,2,3],"up":[1,0,0],"forward":[0,1,0],"radius":0.5,"iris_scale":2,
				"upperflexdesc":[0,-1,-1],"lowerflexdesc":[-1,-1,-1],"uppertarget":[-0.1,0.2,0.3],
				"lowertarget":[0,0,0],"material":"material","vampire":true,
				"iris_asset":"/Engine/EngineResources/DefaultTexture.DefaultTexture"}]},
			"composition":{"driver_axes":[[1,0,0],[0,-1,0],[0,0,1]],"rules":[{
				"bone":"hand","bone_index":1,"control":"root","control_index":0,"axis":[1,0,0],
				"pos":[[1,2,3],[2,2,3],[3,2,3],[4,2,3],[5,2,3],[6,2,3]],
				"quat":[[0,0,0,1],[0,0,0,1],[0,0,0,1],[0,0,0,1],[0,0,0,1],[0,0,0,1]]}]},
			"splitBones":["hand"],"materialSlots":[{"slot":0,"sourceName":"material","material":"vtmb:material:test/material"},
				{"slot":1,"sourceName":"variant","material":"vtmb:material:test/variant"}],
			"skinTable":[[1]],"skinFamilies":[["%s"]]}})JSON"), *Material->GetPathName());
	if (!TestNotNull(TEXT("source identity attaches to mesh"), UElysiumCharacterProvenance::ApplyJson(AuthoredMesh,
		ProvenanceJson, ProvenanceError))) return false;
	TestNull(TEXT("unknown RPN operations cannot be dropped during data attachment"),
		UElysiumCharacterProvenance::ApplyJson(AuthoredMesh,
			ProvenanceJson.Replace(TEXT("\"DIV\""), TEXT("\"UNKNOWN\"")), ProvenanceError));
	TestTrue(TEXT("dropped operation is diagnosed"), ProvenanceError.Contains(TEXT("dropped an operation")));
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!TestTrue(TEXT("mesh provenance saves"), UPackage::SavePackage(AuthoredMesh->GetPackage(), AuthoredMesh,
		*FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension()), SaveArgs))) return false;
	AuthoredMesh = nullptr;
	UElysiumMapBakeLibrary::FinishAssetCompilation();
	UElysiumMapBakeLibrary::UnloadBakedPackages(Package);
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Package);
	if (!TestNotNull(TEXT("saved mesh reloads"), Mesh)) return false;
	const auto* Provenance = Cast<UElysiumCharacterProvenance>(Mesh->GetAssetUserDataOfClass(UElysiumCharacterProvenance::StaticClass()));
	if (TestNotNull(TEXT("typed mesh provenance survives reload"), Provenance))
	{
		TestEqual(TEXT("GLB identity preserved"), Provenance->AssetId, FString(TEXT("vtmb:model:test/stage")));
		TestEqual(TEXT("payload hash preserved"), Provenance->PayloadSha256, FString(TEXT("payload-hash")));
		TestTrue(TEXT("mesh data completion marker survives"), Provenance->bHasMeshData);
		TestEqual(TEXT("all mouth declarations survive"), Provenance->Facial.Mouths.Num(), 2);
		TestEqual(TEXT("first mouth remains the evaluator view"), Provenance->Facial.Mouth.Bone, 0);
		TArray<float> Flex, MorphWeights;
		Provenance->Facial.EvalFlexWeights(TArray<float>{.6f}, Flex);
		Provenance->Facial.EvalMorphWeights(Flex, MorphWeights);
		TestEqual(TEXT("reloaded rule still divides the controller value"), Flex.Num(), 1);
		if (Flex.Num() == 1) TestTrue(TEXT("RPN result unchanged"), FMath::IsNearlyEqual(Flex[0], .3f));
		TestEqual(TEXT("morph ramp survives"), MorphWeights.Num(), 1);
		if (MorphWeights.Num() == 1) TestTrue(TEXT("morph weight unchanged"), FMath::IsNearlyEqual(MorphWeights[0], .3f));
		if (TestEqual(TEXT("eye survives"), Provenance->Eyes.Eyeballs.Num(), 1))
		{
			const auto& Eye = Provenance->Eyes.Eyeballs[0];
			TestTrue(TEXT("eye origin survives"), Eye.Org.Equals(FVector(1, 2, 3)));
			TestEqual(TEXT("eye source model survives"), Eye.BodyModel, 3);
			TestTrue(TEXT("third lid target survives fixed-array serialization"), FMath::IsNearlyEqual(Eye.UpperTarget[2], .3f));
			TestEqual(TEXT("iris remains a soft cook dependency"), Eye.IrisAsset.ToSoftObjectPath().ToString(),
				FString(TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture")));
			TestTrue(TEXT("vampire scalar fact survives"), Eye.bVampire);
		}
		TestTrue(TEXT("driver basis survives fixed-array serialization"), Provenance->Composition.DriverAxes[1].Equals(FVector(0, -1, 0)));
		if (TestEqual(TEXT("procedural rule survives"), Provenance->Composition.AxisRules.Num(), 1))
			TestTrue(TEXT("sixth procedural position survives"), Provenance->Composition.AxisRules[0].Pos[5].Equals(FVector(6, 2, 3)));
		if (TestEqual(TEXT("skin family survives"), Provenance->SkinFamilies.Num(), 1))
		{
			TestEqual(TEXT("material declarations may outnumber skin columns"), Provenance->MaterialSlots.Num(), 2);
			TestEqual(TEXT("skin keeps its original material index"), Provenance->SkinFamilies[0].TextureIndices[0], 1);
			if (TestEqual(TEXT("skin column survives"), Provenance->SkinFamilies[0].Materials.Num(), 1))
				TestEqual(TEXT("skin reference survives"), Provenance->SkinFamilies[0].Materials[0].ToSoftObjectPath().ToString(), Material->GetPathName());
		}
	}
	UGameInstance* Game = NewObject<UGameInstance>();
	UElysiumAnimSubsystem* Runtime = NewObject<UElysiumAnimSubsystem>(Game);
	const auto RuntimeFace = Runtime->GetFacialRig(TEXT("no-loose-record-with-this-name"), Mesh);
	const auto RuntimeEyes = Runtime->GetEyeSet(TEXT("no-loose-record-with-this-name"), Mesh);
	const auto RuntimeComposition = Runtime->GetCompositionRig(TEXT("no-loose-record-with-this-name"), Mesh);
	TestTrue(TEXT("runtime obtains facial data from the loaded mesh"), RuntimeFace.IsValid());
	TestTrue(TEXT("runtime obtains eye data from the loaded mesh"), RuntimeEyes.IsValid());
	TestTrue(TEXT("runtime obtains procedural data from the loaded mesh"), RuntimeComposition.IsValid());
	TestTrue(TEXT("mesh cache identity is independent of legacy stem"),
		Runtime->GetFacialRig(TEXT("a-second-alias"), Mesh) == RuntimeFace);
	TestEqual(TEXT("one material slot"), Mesh->GetMaterials().Num(), 1);
	if (Mesh->GetMaterials().Num() == 1)
		TestTrue(TEXT("existing material bound directly"), Mesh->GetMaterials()[0].MaterialInterface == Material);
	TestTrue(TEXT("geometry re-skinned into the new reference pose"),
		Mesh->GetImportedBounds().Origin.Equals(FVector(19.5, .5, 0.), 1.e-5));
	TestTrue(TEXT("mesh retains the reference transform after reload"),
		Mesh->GetRefSkeleton().GetRefBonePose()[1].Equals(Pose[1], 1.e-5));
	const USkeletalMeshSocket* Tip = Mesh->FindSocket(TEXT("TrailTip"));
	TestNotNull(TEXT("trail socket survives reload"), Tip);
	if (Tip) TestEqual(TEXT("trail remains local to its mounting bone"), Tip->BoneName, FName(TEXT("hand")));
	UMorphTarget* Morph = Mesh->FindMorphTarget(TEXT("stage_morph"));
	if (TestNotNull(TEXT("posed mesh retains its morph"), Morph))
	{
		const auto& Deltas = Morph->GetMorphLODModels()[0].Vertices;
		TestTrue(TEXT("posed morph retains its delta"), !Deltas.IsEmpty());
		for (const FMorphTargetDelta& Delta : Deltas)
		{
			TestTrue(TEXT("morph displacement follows the new bind"), Delta.PositionDelta.Equals(FVector3f(0.f, 2.f, 0.f), 1.e-5f));
			TestTrue(TEXT("morph normal follows the new bind"), Delta.TangentZDelta.Equals(FVector3f(0.f, 1.f, 0.f), 1.e-5f));
		}
	}
	TestTrue(TEXT("missing slot refuses the build"), !UElysiumSkeletalBuildLibrary::BuildSkeletalMeshFromStage(
		Path, Package + TEXT("_Invalid"), FString(), {}, {}, TEXT("bad")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkeletalStageMixedMorphTest,
	"Elysium.Substrate.SkeletalStageMixedMorph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumSkeletalStageMixedMorphTest::RunTest(const FString&)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Elysium/Tests/stage_mixed_morph.skel");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveArrayToFile(ElysiumSkeletalStageTest::Source(10.f, true), *Path)) return false;
	const FString Package = TEXT("/Game/ElysiumGenerated/Tests/R8/SK_MixedMorph");
	const TArray<FTransform> Pose = {FTransform::Identity,
		FTransform(FQuat(FVector::UpVector, PI / 2.), FVector(20., 0., 0.))};
	const FString Error = UElysiumSkeletalBuildLibrary::BuildSkeletalMeshFromStage(Path, Package, FString(),
		{{TEXT("material"), UMaterial::GetDefaultMaterial(MD_Surface)->GetPathName()}}, Pose);
	if (!TestEqual(TEXT("weighted morph build succeeds"), Error, FString())) return false;
	UElysiumMapBakeLibrary::FinishAssetCompilation();
	UElysiumMapBakeLibrary::UnloadBakedPackages(Package);
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Package);
	if (!TestNotNull(TEXT("mixed mesh reloads"), Mesh)) return false;
	UMorphTarget* Morph = Mesh->FindMorphTarget(TEXT("stage_morph"));
	if (!TestNotNull(TEXT("mixed morph reloads"), Morph)) return false;
	const auto& Deltas = Morph->GetMorphLODModels()[0].Vertices;
	TestTrue(TEXT("mixed morph retains deltas"), !Deltas.IsEmpty());
	for (const FMorphTargetDelta& Delta : Deltas)
	{
		TestTrue(TEXT("position uses both influences"), Delta.PositionDelta.Equals(FVector3f(1.f, 1.f, 0.f), 1.e-5f));
		TestTrue(TEXT("normal delta shares the normalized base scale"),
			Delta.TangentZDelta.Equals(FVector3f(-UE_SQRT_2 / 2.f, UE_SQRT_2 / 2.f, 0.f), 1.e-5f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkeletalStageMaskTest,
	"Elysium.Substrate.SkeletalStageMasks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumSkeletalStageMaskTest::RunTest(const FString&)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Elysium/Tests/stage_mask.skel");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveArrayToFile(ElysiumSkeletalStageTest::Source(), *Path)) return false;
	const FString SourcePackage = TEXT("/Game/ElysiumGenerated/Tests/R8/SKEL_MaskSource");
	const FString TargetPackage = TEXT("/Game/ElysiumGenerated/Tests/R8/SKEL_MaskTarget");
	int32 Bones = 0;
	if (!TestEqual(TEXT("source rig builds"), UElysiumSkeletalBuildLibrary::BuildFamilySkeleton(
		{Path}, SourcePackage, true, Bones), FString())) return false;
	if (!TestEqual(TEXT("target rig builds"), UElysiumSkeletalBuildLibrary::BuildFamilySkeleton(
		{Path}, TargetPackage, true, Bones), FString())) return false;
	USkeleton* Source = LoadObject<USkeleton>(nullptr, *SourcePackage);
	USkeleton* Target = LoadObject<USkeleton>(nullptr, *TargetPackage);
	UBlendProfile* Profile = Source->CreateNewBlendProfile(TEXT("ElysiumLayerMask_Test"));
	Profile->Mode = EBlendProfileMode::BlendMask;
	Profile->SetBoneBlendScale(TEXT("hand"), 1.f, false, true);
	FString Error;
	TestNotNull(TEXT("source units attach to shared rig"), UElysiumCharacterProvenance::ApplyJson(Target,
		TEXT("{\"sourceUnits\":[\"vtmb:model:test/bank\"],\"bankFamilyTreeSha256\":\"tree-hash\"}"), Error));
	TestEqual(TEXT("binary mask mirrors"), UElysiumSkeletalBuildLibrary::DeclareCompatibleSkeletons(
		TargetPackage, {SourcePackage}), FString());
	UBlendProfile* Mirrored = Target->GetBlendProfile(Profile->GetFName());
	if (!TestNotNull(TEXT("target owns mirrored profile"), Mirrored)) return false;
	TestEqual(TEXT("same binary mask reuses"), UElysiumSkeletalBuildLibrary::DeclareCompatibleSkeletons(
		TargetPackage, {SourcePackage}), FString());
	Mirrored->SetBoneBlendScale(TEXT("root"), 1.f, false, true);
	TestTrue(TEXT("stale target mask is rejected"), UElysiumSkeletalBuildLibrary::DeclareCompatibleSkeletons(
		TargetPackage, {SourcePackage}).Contains(TEXT("differs from source-owned bones")));
	Mirrored->SetBoneBlendScale(TEXT("root"), 0.f, false, true);
	Profile->SetBoneBlendScale(TEXT("hand"), .5f, false, true);
	TestTrue(TEXT("fractional source mask is rejected even when target exists"), UElysiumSkeletalBuildLibrary::DeclareCompatibleSkeletons(
		TargetPackage, {SourcePackage}).Contains(TEXT("non-binary mask weight")));
	Profile->SetBoneBlendScale(TEXT("hand"), 1.f, false, true);
	Source = nullptr; Target = nullptr; Profile = nullptr; Mirrored = nullptr;
	UElysiumMapBakeLibrary::UnloadBakedPackages(TargetPackage);
	Target = LoadObject<USkeleton>(nullptr, *TargetPackage);
	const auto* Provenance = Cast<UElysiumCharacterProvenance>(Target->GetAssetUserDataOfClass(UElysiumCharacterProvenance::StaticClass()));
	if (TestNotNull(TEXT("shared rig provenance reloads"), Provenance))
		TestEqual(TEXT("family tree digest survives"), Provenance->BankFamilyTreeSha256, FString(TEXT("tree-hash")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkeletalStageOwnerTest,
	"Elysium.Substrate.SkeletalStageOwners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumSkeletalStageOwnerTest::RunTest(const FString&)
{
	const FString First = FPaths::ProjectSavedDir() / TEXT("Elysium/Tests/owner_a/shared.skel");
	const FString Second = FPaths::ProjectSavedDir() / TEXT("Elysium/Tests/owner_b/shared.skel");
	for (const FString& Path : {First, Second}) IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveArrayToFile(ElysiumSkeletalStageTest::Source(10.f), *First)
		|| !FFileHelper::SaveArrayToFile(ElysiumSkeletalStageTest::Source(30.f), *Second)) return false;
	const FString Family = TEXT("/Game/ElysiumGenerated/Tests/R8/SKEL_StageOwners");
	const FString A = TEXT("/ElysiumBaked/Models/_Tests/stage_owners/owner_a");
	const FString B = TEXT("/ElysiumBaked/Models/_Tests/stage_owners/owner_b");
	int32 Count = 0, Dropped = 0, Suppressed = 0;
	TArray<FName> SuppressedBones;
	if (!TestEqual(TEXT("shared rig builds"), UElysiumSkeletalBuildLibrary::BuildFamilySkeleton(
		{First, Second}, Family, true, Count), FString())) return false;
	if (!TestEqual(TEXT("first owner clips build"), UElysiumSkeletalBuildLibrary::BuildAnimSequencesFromStage(
		First, A, Family, TEXT("actor"), Count, Dropped, Suppressed, SuppressedBones), FString())) return false;
	TestEqual(TEXT("one native clip"), Count, 1);
	TestEqual(TEXT("no dropped tracks"), Dropped, 0);
	TestEqual(TEXT("body tracks are not suppressed"), Suppressed, 0);
	if (!TestEqual(TEXT("second owner clips build"), UElysiumSkeletalBuildLibrary::BuildAnimSequencesFromStage(
		Second, B, Family, TEXT("actor"), Count, Dropped, Suppressed, SuppressedBones), FString())) return false;
	UElysiumMapBakeLibrary::FinishAssetCompilation();
	UElysiumMapBakeLibrary::UnloadBakedPackages(TEXT("/Game/ElysiumGenerated/Tests/R8"));
	UElysiumMapBakeLibrary::UnloadBakedPackages(TEXT("/ElysiumBaked/Models/_Tests/stage_owners"));
	UAnimSequence* SequenceA = LoadObject<UAnimSequence>(nullptr, *(A / TEXT("A_step_test_actor")));
	UAnimSequence* SequenceB = LoadObject<UAnimSequence>(nullptr, *(B / TEXT("A_step_test_actor")));
	if (!TestNotNull(TEXT("first folded actor clip reloads"), SequenceA)
		|| !TestNotNull(TEXT("second folded actor clip reloads"), SequenceB)) return false;
	TestNotEqual(TEXT("same filename keeps distinct donor identity"), SequenceA->RetargetSource, SequenceB->RetargetSource);
	TestTrue(TEXT("native model survives saving and reloading without Euler conversion"),
		SequenceA->GetDataModelInterface().GetObject()->IsA<UAnimDataModel>());
	int32 ComparedTracks=0, OmittedTracks=0; int64 ComparedKeys=0;
	TestEqual(TEXT("saved animation retains every staged source key"),UElysiumCharacterBakeLibrary::VerifyAnimationSamples(
		First,{{TEXT("step.test"),SequenceA}},{},ComparedTracks,ComparedKeys,OmittedTracks),FString());
	TestEqual(TEXT("one source track compared"),ComparedTracks,1);
	TestEqual(TEXT("both source keys compared"),ComparedKeys,int64(2));
	auto* Corrupt=DuplicateObject<UAnimSequence>(SequenceA,GetTransientPackage(),TEXT("R8CorruptSamples"));
	if (!TestNotNull(TEXT("sample corruption probe clones the native clip"),Corrupt)) return false;
	TestTrue(TEXT("probe changes only the final translation key"),Corrupt->GetController().SetBoneTrackKeys(TEXT("root"),
		{FVector3f::ZeroVector,FVector3f(2.f,0.f,0.f)},
		{FQuat4f::Identity,FQuat4f::Identity},{FVector3f::OneVector,FVector3f::OneVector},false));
	const FString SampleError=UElysiumCharacterBakeLibrary::VerifyAnimationSamples(
		First,{{TEXT("step.test"),Corrupt}},{},ComparedTracks,ComparedKeys,OmittedTracks);
	TestTrue(TEXT("sample verifier detects corruption by clip, bone and frame"),
		SampleError.Contains(TEXT("step.test / root key 1")));
	const auto* PoseA = SequenceA->GetSkeleton()->AnimRetargetSources.Find(SequenceA->RetargetSource);
	const auto* PoseB = SequenceB->GetSkeleton()->AnimRetargetSources.Find(SequenceB->RetargetSource);
	if (TestNotNull(TEXT("first donor pose survives"), PoseA) && TestNotNull(TEXT("second donor pose survives"), PoseB))
	{
		TestEqual(TEXT("first donor bind is unchanged"), PoseA->ReferencePose[1].GetTranslation().X, 10.);
		TestEqual(TEXT("second donor bind is independent"), PoseB->ReferencePose[1].GetTranslation().X, 30.);
	}
	const FString ClipJson = TEXT(R"JSON({"schemaVersion":"1.0.0","assetId":"vtmb:model:test/owner_a",
		"ownerRoot":"actor","label":"step.test","sourceLabel":"step.test",
		"slice":{"owners":["vtmb:model:test/owner_a"],"activities":["ACT_TEST"],
			"clips":{"step.test":[[0,0,2,0,2,30,0.2,25.4,"ACT_BLOCKED",
				[{"start":0.8,"end":0.2,"bone":"hand","a_cm":[1,2,3],"b_cm":[4,5,6],
				"kb_names":[["ACT_KNOCKBACK"],[],[],[]],"b8":3,"ba":255,"degenerate":true}],
				{"mask":0,"dodge":"ACT_DODGE","chain":"next","chain_alt":"alt","w_open":0.2,"w_close":0.8,"w_hold":0.1},
				0,[{"min":[1,2,3],"max":[4,5,6]}]]]},"seq":{"step.test":[7]}},
		"timelines":{"grids":{},"event_options":["payload with spaces; and punctuation"],
			"events":{"step.test":[[0.5,1000,2,0],[0.5,1001,2,0]]},
			"movement_fields":["end_frame","flags","v0_cm","v1_cm","yaw_deg","dir_x","dir_y","dir_z","pos_x_cm","pos_y_cm","pos_z_cm"],
			"movement":{"step.test":[[1,1,1.5,2.5,0,0,1,0,0,2,0]]}},
		"counts":{"swings":1,"envelopes":1,"events":2,"movement":1,"combo":true,"knockbacks":[[1,0,0,0]]},
		"cycleSeconds":0.0333333,"groundDistanceCm":2,"groundSpeedCmPerSecond":60,
		"axes":[{"name":"move_yaw","flags":1,"loop":360}]})JSON");
	FString ClipError;
	const FString EmptyTimeline = ClipJson
		.Replace(TEXT("[[0.5,1000,2,0],[0.5,1001,2,0]]"), TEXT("[]"))
		.Replace(TEXT("[[1,1,1.5,2.5,0,0,1,0,0,2,0]]"), TEXT("[]"))
		.Replace(TEXT("\"events\":2"), TEXT("\"events\":0"))
		.Replace(TEXT("\"movement\":1"), TEXT("\"movement\":0"));
	const auto* Empty = UElysiumClipData::ApplyJson(SequenceA, EmptyTimeline, ClipError);
	if (!TestNotNull(TEXT("an authored empty timeline is valid metadata"), Empty))
	{
		AddError(ClipError);
		return false;
	}
	TestTrue(TEXT("empty movement stays stated, without a synthetic row"), Empty->bMovementStated && Empty->Movement.Records.IsEmpty());
	TestTrue(TEXT("empty events stay empty"), Empty->Events.IsEmpty());
	if (!TestNotNull(TEXT("clip facts attach"), UElysiumClipData::ApplyJson(SequenceA, ClipJson, ClipError)))
	{
		AddError(ClipError);
		return false;
	}
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const FString ClipPackage = A / TEXT("A_step_test_actor");
	if (!TestTrue(TEXT("clip facts save"), UPackage::SavePackage(SequenceA->GetPackage(), SequenceA,
		*FPackageName::LongPackageNameToFilename(ClipPackage, FPackageName::GetAssetPackageExtension()), SaveArgs))) return false;
	SequenceA = nullptr;
	UElysiumMapBakeLibrary::UnloadBakedPackages(A);
	SequenceA = LoadObject<UAnimSequence>(nullptr, *ClipPackage);
	TestEqual(TEXT("all reflected clip facts survive reload"), UElysiumClipData::Verify(SequenceA, ClipJson), FString());
	const UElysiumClipData* Data = SequenceA ? SequenceA->FindMetaDataByClass<UElysiumClipData>() : nullptr;
	if (!TestNotNull(TEXT("cooked clip metadata reloads"), Data)) return false;
	TestTrue(TEXT("stated zero near reach survives"), Data->Descriptor.HasLowReach() && Data->Descriptor.LowReachCm == 0.f);
	TestTrue(TEXT("neutral combo mask survives"), Data->Descriptor.Combo.HasStateMask() && Data->Descriptor.Combo.Mask == 0);
	TestTrue(TEXT("authored reversed contact window survives"), Data->Descriptor.Swings[0].bDegenerate);
	TestEqual(TEXT("empty direction bucket remains in position"), Data->Descriptor.Swings[0].KnockbackNames[1].Names.Num(), 0);
	TestEqual(TEXT("event payload is not tokenized"), Data->Events[1].Options, FString(TEXT("payload with spaces; and punctuation")));
	TestEqual(TEXT("simultaneous event order remains authored"), Data->Events[1].Event, 1001);
	TestTrue(TEXT("movement is a native displacement path"), Data->Movement.Records[0].PositionCm.Equals(FVector(0, 2, 0)));
	return true;
}
#endif
