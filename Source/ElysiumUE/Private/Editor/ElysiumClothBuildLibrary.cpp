#include "ElysiumClothBuildLibrary.h"

#if WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "ElysiumClothTuningConfig.h"
#include "ElysiumContentPaths.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "ChaosCloth/ChaosClothingSimulationConfig.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothEngineTools.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ChaosClothAsset/ClothSimulationModel.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/CollectionClothSimPatternFacade.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Misc/FileHelper.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace
{
	using namespace UE::Chaos::ClothAsset;

	/** The weight map Chaos reads for per-vertex travel. Zero pins a vertex to its skinned position. */
	const FName MaxDistanceMap(TEXT("MaxDistance"));

	/**
	 * Retail gravity against Chaos's own, as a ratio.
	 *
	 * StudioRender integrates `gravity_scale * 384 * dt^2`, and 384 in/s^2 is 32 ft/s^2, so the
	 * authored scale is measured against 975.36 cm/s^2 while Chaos measures against 980.665. The
	 * difference is half a percent -- small enough to be invisible and cheap enough to remove.
	 *
	 * This is a conversion between two engines' constants, not a tuning value, which is why it is
	 * the only number in this file. Everything a garment's solve needs beyond what VtMB itself
	 * authored lives in the authored `UElysiumClothTuningConfig`.
	 */
	constexpr float RetailGravityRatio = 975.36f / 980.665f;

	TSharedPtr<FJsonObject> ReadJsonObject(const FString& Path, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *Path);
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("cannot parse %s"), *Path);
			return nullptr;
		}
		return Root;
	}

	FVector3f VectorFrom(const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		return Values.Num() >= 3
			? FVector3f(
				  static_cast<float>(Values[0]->AsNumber()),
				  static_cast<float>(Values[1]->AsNumber()),
				  static_cast<float>(Values[2]->AsNumber()))
			: FVector3f::ZeroVector;
	}

	/**
	 * Each particle's distance to the pinned set, measured ALONG the simulation mesh.
	 *
	 * How far a free particle may leave its skinned position is not a property of the garment as a
	 * whole. A waistband particle should barely move and a hem particle legitimately swings, and
	 * both are on the same sheet — so a single constant serves neither. Measured against each
	 * garment's own edge length, the 15 cm this used to hand out was eighteen edge lengths on
	 * Rosa's bead cord and a little over one on the Sheriff's cloak.
	 *
	 * Geodesic rather than straight-line because the leash bounds travel over the surface the
	 * constraints act on, which is also what `FClothEngineTools::GenerateTethers` measures when it
	 * builds the long range attachments from this same map.
	 *
	 * Dijkstra outward from every anchored particle at once. A particle no path reaches — a
	 * disconnected island, or a garment with no anchors at all — keeps its starting infinity, and
	 * the caller's own ceiling is what bounds it.
	 */
	TArray<float> ReachToPinned(const TArray<FVector3f>& Rest, const TArray<FIntVector3>& Faces,
		const TArray<bool>& Anchored)
	{
		TArray<TArray<int32>> Adjacent;
		Adjacent.SetNum(Rest.Num());
		auto Link = [&Adjacent](int32 A, int32 B)
		{
			if (Adjacent.IsValidIndex(A) && Adjacent.IsValidIndex(B))
			{
				Adjacent[A].AddUnique(B);
				Adjacent[B].AddUnique(A);
			}
		};
		for (const FIntVector3& Face : Faces)
		{
			Link(Face.X, Face.Y);
			Link(Face.Y, Face.Z);
			Link(Face.Z, Face.X);
		}

		TArray<float> Reach;
		Reach.Init(TNumericLimits<float>::Max(), Rest.Num());
		TArray<bool> Settled;
		Settled.Init(false, Rest.Num());
		for (int32 Index = 0; Index < Rest.Num(); ++Index)
		{
			if (Anchored.IsValidIndex(Index) && Anchored[Index])
			{
				Reach[Index] = 0.f;
			}
		}
		// A linear scan for the nearest unsettled particle rather than a heap: a garment is a few
		// hundred particles and this runs once per build.
		for (;;)
		{
			int32 Nearest = INDEX_NONE;
			for (int32 Index = 0; Index < Reach.Num(); ++Index)
			{
				if (!Settled[Index] && Reach[Index] < TNumericLimits<float>::Max()
					&& (Nearest == INDEX_NONE || Reach[Index] < Reach[Nearest]))
				{
					Nearest = Index;
				}
			}
			if (Nearest == INDEX_NONE)
			{
				break;
			}
			Settled[Nearest] = true;
			for (const int32 Next : Adjacent[Nearest])
			{
				const float Step =
					Reach[Nearest] + FVector3f::Distance(Rest[Nearest], Rest[Next]);
				if (Step < Reach[Next])
				{
					Reach[Next] = Step;
				}
			}
		}
		return Reach;
	}

	/**
	 * The garment's own UV layout, used as its flat pattern.
	 *
	 * Chaos keeps a 2D pattern beside the 3D mesh and sizes its arrays from it, so one has to
	 * exist. A garment's UV unwrap IS its flat pattern -- that is what unwrapping a piece of
	 * clothing means -- so the artist's layout is used rather than a shape invented here. It is
	 * scaled from unit UV space into centimetres by the garment's own size, because the pattern is
	 * measured against the 3D mesh for pattern-space anisotropy and a unit square would read as a
	 * garment one centimetre across.
	 */
	void FlatPatternFromUVs(const TArray<FVector3f>& Rest, const TArray<FVector2f>& UVs,
		TArray<FVector2f>& Out)
	{
		FBox3f Bounds(ForceInit);
		for (const FVector3f& P : Rest)
		{
			Bounds += P;
		}
		FBox2f UVBounds(ForceInit);
		for (const FVector2f& UV : UVs)
		{
			UVBounds += UV;
		}
		const float UVSpan = UVBounds.GetSize().Size();
		const float Scale = UVSpan > UE_SMALL_NUMBER ? Bounds.GetSize().Size() / UVSpan : 1.f;

		Out.SetNum(Rest.Num());
		for (int32 Index = 0; Index < Rest.Num(); ++Index)
		{
			Out[Index] = UVs.IsValidIndex(Index) ? UVs[Index] * Scale : FVector2f::ZeroVector;
		}
	}

	/**
	 * The material the baked body draws this garment's surface with, resolved by the name the
	 * decode recorded.
	 *
	 * The garment is a piece of the character, not a new asset: it has to reach the frame in the
	 * same material the body's own skirt section uses, or it draws in whatever the copy was
	 * handed -- which is the engine's cloth EDITOR material, a two-tone camera-lit shader that is
	 * unmistakable on a character and easy to mistake for a lighting fault.
	 */
	FSoftObjectPath ResolveGarmentMaterial(const USkeletalMesh& Mesh, const FString& MaterialName)
	{
		for (const FSkeletalMaterial& Slot : Mesh.GetMaterials())
		{
			const bool bMatches = Slot.MaterialSlotName.ToString() == MaterialName
				|| (Slot.MaterialInterface != nullptr
					&& Slot.MaterialInterface->GetName().Contains(MaterialName));
			if (bMatches && Slot.MaterialInterface != nullptr)
			{
				return FSoftObjectPath(Slot.MaterialInterface->GetPathName());
			}
		}
		return FSoftObjectPath();
	}

	/**
	 * Where a bone's bind pose puts it, in the mesh's own space.
	 *
	 * The authored colliders are stated in bind space, so placing one means asking THIS mesh's
	 * authored reference skeleton where that bone is. This also resolves bone-index renumbering
	 * without introducing a second rotation convention.
	 */
	FTransform BoneBindTransform(const FReferenceSkeleton& Ref, int32 BoneIndex)
	{
		FTransform Bind = FTransform::Identity;
		for (int32 Index = BoneIndex; Index != INDEX_NONE; Index = Ref.GetParentIndex(Index))
		{
			Bind = Bind * Ref.GetRefBonePose()[Index];
		}
		return Bind;
	}

	UPackage* MakePackage(const FString& Directory, const FString& Name, FString& OutObjectPath)
	{
		const FString PackagePath = Directory / Name;
		OutObjectPath = PackagePath + TEXT(".") + Name;
		UPackage* const Package = CreatePackage(*PackagePath);
		// `CreatePackage` hands back an empty package that has never been loaded, even when a file
		// of that name is already on disk. Anything that later resolves the asset BY PATH -- the
		// save call itself does -- sees a package that is not fully loaded and loads the old file
		// over the top, so a regenerated asset silently saves back as its previous self. Nothing
		// reports it: the build succeeds, the object is correct in memory, the file's timestamp
		// moves, and its bytes do not change. Claiming the package as fully loaded is what stops
		// the reload.
		Package->MarkAsFullyLoaded();
		return Package;
	}

	/**
	 * The authored capsules and spheres as a physics asset.
	 *
	 * Chaos gathers cloth collision from a `UPhysicsAsset` rather than from the cloth collection,
	 * so the primitives have to become body setups on their own bones. A VtMB capsule names two
	 * bones and two endpoints; where those bones differ the capsule spans a joint, which a single
	 * body setup cannot express, so it is attached to the first bone and its endpoints are used as
	 * authored. Every primitive in the measured corpus names the same bone twice.
	 */
	UPhysicsAsset* BuildPhysicsAsset(
		const TSharedPtr<FJsonObject>& Garment,
		const FString& Directory,
		const FString& Name,
		const USkeletalMesh* Mesh,
		int32& OutBodies,
		TArray<FString>& OutErrors)
	{
		FString ObjectPath;
		UPackage* Package = MakePackage(Directory, Name, ObjectPath);
		UPhysicsAsset* Asset = NewObject<UPhysicsAsset>(
			Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		if (Asset == nullptr)
		{
			OutErrors.Add(TEXT("could not create the physics asset"));
			return nullptr;
		}
		Asset->SetPreviewMesh(const_cast<USkeletalMesh*>(Mesh));

		TMap<FName, USkeletalBodySetup*> ByBone;
		auto BodyFor = [&](const FString& BoneName) -> USkeletalBodySetup*
		{
			const FName Bone(*BoneName);
			if (USkeletalBodySetup** Found = ByBone.Find(Bone))
			{
				return *Found;
			}
			if (Mesh != nullptr && Mesh->GetRefSkeleton().FindBoneIndex(Bone) == INDEX_NONE)
			{
				OutErrors.Add(FString::Printf(
					TEXT("collider bone '%s' is not on the bound skeleton"), *BoneName));
				return nullptr;
			}
			USkeletalBodySetup* Setup = NewObject<USkeletalBodySetup>(
				Asset, NAME_None, RF_Transactional);
			Setup->BoneName = Bone;
			Setup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			Setup->PhysicsType = PhysType_Kinematic;
			Asset->SkeletalBodySetups.Add(Setup);
			ByBone.Add(Bone, Setup);
			return Setup;
		};

		const TArray<TSharedPtr<FJsonValue>>* Capsules = nullptr;
		if (Garment->TryGetArrayField(TEXT("capsules"), Capsules))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Capsules)
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				if (!Object.IsValid())
				{
					continue;
				}
				FString BoneName;
				if (!Object->TryGetStringField(TEXT("bone_a_name"), BoneName))
				{
					continue;
				}
				USkeletalBodySetup* Setup = BodyFor(BoneName);
				if (Setup == nullptr)
				{
					continue;
				}
				// Bind space -> this mesh's authored bone frame. This is a frame expression, not a
				// facing correction; the reference transform comes straight from the ESKM bind.
				const FTransform Bind = BoneBindTransform(
					Mesh->GetRefSkeleton(), Mesh->GetRefSkeleton().FindBoneIndex(FName(*BoneName)));
				const FVector Start = Bind.InverseTransformPosition(
					FVector(VectorFrom(Object->GetArrayField(TEXT("a_bind")))));
				const FVector End = Bind.InverseTransformPosition(
					FVector(VectorFrom(Object->GetArrayField(TEXT("b_bind")))));

				FKSphylElem Sphyl;
				Sphyl.Center = (Start + End) * 0.5;
				Sphyl.Radius = static_cast<float>(Object->GetNumberField(TEXT("radius")));
				Sphyl.Length = static_cast<float>((End - Start).Size());
				// A sphyl runs along its own Z, so the rotation is whatever carries Z onto the
				// authored axis; a zero-length capsule keeps the identity and reads as a sphere.
				if (Sphyl.Length > KINDA_SMALL_NUMBER)
				{
					Sphyl.Rotation = FQuat::FindBetweenNormals(
						FVector::ZAxisVector, (End - Start).GetSafeNormal()).Rotator();
				}
				Setup->AggGeom.SphylElems.Add(Sphyl);
				++OutBodies;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Spheres = nullptr;
		if (Garment->TryGetArrayField(TEXT("spheres"), Spheres))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Spheres)
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				if (!Object.IsValid())
				{
					continue;
				}
				FString BoneName;
				if (!Object->TryGetStringField(TEXT("bone_name"), BoneName))
				{
					continue;
				}
				USkeletalBodySetup* Setup = BodyFor(BoneName);
				if (Setup == nullptr)
				{
					continue;
				}
				const FTransform Bind = BoneBindTransform(
					Mesh->GetRefSkeleton(), Mesh->GetRefSkeleton().FindBoneIndex(FName(*BoneName)));
				FKSphereElem Sphere;
				Sphere.Center = Bind.InverseTransformPosition(
					FVector(VectorFrom(Object->GetArrayField(TEXT("centre_bind")))));
				Sphere.Radius = static_cast<float>(Object->GetNumberField(TEXT("radius")));
				Setup->AggGeom.SphereElems.Add(Sphere);
				++OutBodies;
			}
		}

		Asset->UpdateBodySetupIndexMap();
		Asset->UpdateBoundsBodiesArray();
		FAssetRegistryModule::AssetCreated(Asset);
		Package->MarkPackageDirty();
		return Asset;
	}
}

TArray<FElysiumClothBuildResult> UElysiumClothBuildLibrary::BuildClothAssetsFromSidecar(
	const FString& SidecarPath,
	const FString& PackageDirectory,
	const FString& SkeletalMeshPath)
{
	TArray<FElysiumClothBuildResult> Results;

	FElysiumClothBuildResult Fatal;
	FString Error;
	const TSharedPtr<FJsonObject> Root = ReadJsonObject(SidecarPath, Error);
	if (!Root.IsValid())
	{
		Fatal.Errors.Add(Error);
		Results.Add(Fatal);
		return Results;
	}

	// The authored half of the build. `Load` has already logged the miss by name; the failure is
	// repeated into the result so the generator's own log carries it beside the garment it stopped.
	const UElysiumClothTuningConfig* const TuningTable = UElysiumClothTuningConfig::Load();
	if (TuningTable == nullptr)
	{
		Fatal.Errors.Add(FString::Printf(
			TEXT("cannot load the authored cloth tuning asset %s"),
			*FElysiumContentPaths::AuthoredClothTuning()));
		Results.Add(Fatal);
		return Results;
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
	if (Mesh == nullptr)
	{
		Fatal.Errors.Add(FString::Printf(TEXT("cannot load skeletal mesh %s"), *SkeletalMeshPath));
		Results.Add(Fatal);
		return Results;
	}

	FString Stem;
	Root->TryGetStringField(TEXT("stem"), Stem);

	const TArray<TSharedPtr<FJsonValue>>* Garments = nullptr;
	if (!Root->TryGetArrayField(TEXT("garments"), Garments) || Garments->Num() == 0)
	{
		Fatal.Errors.Add(TEXT("the sidecar declares no garments"));
		Results.Add(Fatal);
		return Results;
	}

	for (int32 GarmentIndex = 0; GarmentIndex < Garments->Num(); ++GarmentIndex)
	{
		FElysiumClothBuildResult Result;
		const TSharedPtr<FJsonObject> Garment = (*Garments)[GarmentIndex]->AsObject();
		if (!Garment.IsValid())
		{
			Result.Errors.Add(TEXT("garment entry is not an object"));
			Results.Add(Result);
			continue;
		}

		// The material call for this garment, resolved before anything is built with it. Every
		// number below that is not a decode of the authored payload comes from here.
		FName TuningMaterial;
		const FElysiumClothTuningLayer Tuning =
			TuningTable->ResolveGarment(Stem, GarmentIndex, TuningMaterial, Result.Errors);
		Result.Material = TuningMaterial.ToString();

		// --- the authored simulation mesh -------------------------------------------------
		TArray<FVector3f> Rest;
		for (const TSharedPtr<FJsonValue>& Value : Garment->GetArrayField(TEXT("rest_positions")))
		{
			// Read verbatim. The sidecar is Unreal-native — centimetres, Z-up, left-handed,
			// written by a `UE_` exporter — so there is no basis change here and there must
			// never be one: the repository's coordinate rule is that the runtime converts
			// nothing (`docs/project/rebuild-strategy.md`).
			Rest.Add(VectorFrom(Value->AsArray()));
		}
		TArray<FIntVector3> Faces;
		for (const TSharedPtr<FJsonValue>& Value : Garment->GetArrayField(TEXT("triangles")))
		{
			const TArray<TSharedPtr<FJsonValue>> Tri = Value->AsArray();
			if (Tri.Num() >= 3)
			{
				Faces.Add(FIntVector3(
					static_cast<int32>(Tri[0]->AsNumber()),
					static_cast<int32>(Tri[1]->AsNumber()),
					static_cast<int32>(Tri[2]->AsNumber())));
			}
		}
		if (Rest.Num() == 0 || Faces.Num() == 0)
		{
			Result.Errors.Add(TEXT("garment has no particles or no triangles"));
			Results.Add(Result);
			continue;
		}

		TArray<bool> Anchored;
		Anchored.Init(false, Rest.Num());
		{
			const TArray<TSharedPtr<FJsonValue>>& Flags = Garment->GetArrayField(TEXT("anchored"));
			for (int32 Index = 0; Index < Flags.Num() && Index < Anchored.Num(); ++Index)
			{
				Anchored[Index] = Flags[Index]->AsBool();
			}
		}

		// --- collision, first ---------------------------------------------------------------
		//
		// Built ahead of the collection because Chaos reads the physics asset through the
		// COLLECTION, not off the built asset: `Build` consumes what the collection names, so a
		// physics asset attached afterwards is simply not part of the simulation.
		const FString BaseName = Garments->Num() > 1
			? FString::Printf(TEXT("CLOTH_%s_%d"), *Stem, GarmentIndex)
			: FString::Printf(TEXT("CLOTH_%s"), *Stem);
		int32 Bodies = 0;
		UPhysicsAsset* Physics = BuildPhysicsAsset(
			Garment, PackageDirectory, BaseName + TEXT("_PHYS"), Mesh, Bodies, Result.Errors);

		// --- the collection ---------------------------------------------------------------
		const TSharedRef<FManagedArrayCollection> Collection = MakeShared<FManagedArrayCollection>();
		FCollectionClothFacade Cloth(Collection);
		Cloth.DefineSchema();
		// The collection names both assets it depends on. The skeleton is what its bone indices
		// are numbered against — without it the build rejects the simulation skinning through an
		// ensure rather than a bad weight — and the physics asset is where the solver gathers
		// collision from.
		Cloth.SetSkeletalMeshSoftObjectPathName(FSoftObjectPath(SkeletalMeshPath));
		if (Physics != nullptr)
		{
			Cloth.SetPhysicsAssetSoftObjectPathName(FSoftObjectPath(Physics->GetPathName()));
		}

		// One pattern per garment, sized and filled through the facade's own entry point:
		// the sizing calls are private, and `Initialize` is what the collection expects to
		// establish the 2D/3D vertex correspondence and the face lists together.
		// The garment's own texture coordinates, one per particle, straight off the sidecar.
		TArray<FVector2f> UVs;
		UVs.Reserve(Rest.Num());
		if (const TArray<TSharedPtr<FJsonValue>>* RestUVs = nullptr;
			Garment->TryGetArrayField(TEXT("rest_uvs"), RestUVs))
		{
			for (const TSharedPtr<FJsonValue>& Value : *RestUVs)
			{
				const TArray<TSharedPtr<FJsonValue>> Pair = Value->AsArray();
				UVs.Add(Pair.Num() >= 2
					? FVector2f(static_cast<float>(Pair[0]->AsNumber()),
						static_cast<float>(Pair[1]->AsNumber()))
					: FVector2f::ZeroVector);
			}
		}
		if (UVs.Num() != Rest.Num())
		{
			Result.Errors.Add(FString::Printf(
				TEXT("sidecar carries %d UVs for %d particles - regenerate the export"),
				UVs.Num(), Rest.Num()));
			UVs.SetNumZeroed(Rest.Num());
		}

		TArray<FVector2f> Flat;
		FlatPatternFromUVs(Rest, UVs, Flat);
		TArray<int32> Indices;
		Indices.Reserve(Faces.Num() * 3);
		for (const FIntVector3& Face : Faces)
		{
			Indices.Add(Face.X);
			Indices.Add(Face.Y);
			Indices.Add(Face.Z);
		}

		const int32 PatternIndex = Cloth.AddSimPattern();
		FCollectionClothSimPatternFacade Pattern = Cloth.GetSimPattern(PatternIndex);
		Pattern.Initialize(Flat, Rest, Indices);

		// --- binding ----------------------------------------------------------------------
		//
		// The engine's own converter binds the simulation mesh to the root bone and lets the
		// solver take it from there. That is done first here so every vertex has valid skinning
		// by construction, and the anchors' authored weights are written over it below: a
		// hand-built weight array that misses a vertex is not rejected, it is simulated.
		FClothGeometryTools::BindMeshToRootBone(Collection, /*bBindSimMesh*/ true,
			/*bBindRenderMesh*/ false);

		// Every particle's skin weights, resolved by name.
		//
		// Not just the anchors'. VtMB skins only its pinned prefix, because that is all it needs:
		// a free particle is never read back from the skeleton. Chaos measures max distance and
		// every tether FROM the skinned pose, so a free particle bound to the root instead makes
		// the garment chase a rest shape the root bone carries around the world -- visible as an
		// animation mesh splayed into a disc while the simulation itself looks correct.
		const TArray<TSharedPtr<FJsonValue>>* ParticleSkin = nullptr;
		Garment->TryGetArrayField(TEXT("particle_skin"), ParticleSkin);
		TArrayView<TArray<int32>> BoneIndices = Cloth.GetSimBoneIndices();
		TArrayView<TArray<float>> BoneWeights = Cloth.GetSimBoneWeights();
		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
		TSet<FString> MissingBones;

		for (int32 Index = 0; Index < BoneIndices.Num(); ++Index)
		{
			BoneIndices[Index].Reset();
			BoneWeights[Index].Reset();
			if (ParticleSkin != nullptr && ParticleSkin->IsValidIndex(Index))
			{
				for (const TSharedPtr<FJsonValue>& Value : (*ParticleSkin)[Index]->AsArray())
				{
					const TArray<TSharedPtr<FJsonValue>> Pair = Value->AsArray();
					if (Pair.Num() < 2)
					{
						continue;
					}
					const FName Bone(*Pair[0]->AsString());
					const int32 Resolved = RefSkeleton.FindBoneIndex(Bone);
					if (Resolved == INDEX_NONE)
					{
						MissingBones.Add(Pair[0]->AsString());
						continue;
					}
					BoneIndices[Index].Add(Resolved);
					BoneWeights[Index].Add(static_cast<float>(Pair[1]->AsNumber()));
				}
			}
			if (BoneIndices[Index].Num() == 0)
			{
				// Last resort only. Every particle should have arrived with weights; root binding
				// is what produces the splayed animation pose described above.
				BoneIndices[Index].Add(0);
				BoneWeights[Index].Add(1.f);
				++Result.RootBoundParticles;
			}
		}
		for (const FString& Bone : MissingBones)
		{
			Result.Errors.Add(FString::Printf(
				TEXT("anchor bone '%s' is not on the bound skeleton"), *Bone));
		}

		// --- pinning: VtMB's anchored prefix as a zero max distance ------------------------
		//
		// Legacy mask convention: the map holds CENTIMETRES and the property that reads it keeps
		// the unit range, because the solver evaluates `Low + Map * (High - Low)`. Zero is
		// kinematic — below `KinematicDistanceThreshold` the particle is mass-infinite and stays
		// on its skinned position, which is exactly what VtMB does by skinning its anchors from
		// the bone palette instead of solving them.
		//
		// The free particles' leash is ours rather than VtMB's: the retail solve has no
		// per-particle displacement limit at all and holds the surface on the body with its
		// constraint graph alone. Chaos needs one at real-time substep counts, so it is scaled by
		// each particle's own geodesic distance to the pinned set — a waistband particle gets a
		// tight leash and a hem particle a loose one, and one tuned fraction means the same thing
		// on a 35 cm necktie and a 228 cm cloak.
		const TArray<float> Reach = ReachToPinned(Rest, Faces, Anchored);
		const float LeashFraction = Tuning.LeashReachFraction;
		const float LeashMin = Tuning.LeashMinCm;
		const float LeashMax = Tuning.LeashMaxCm;

		Cloth.AddWeightMap(MaxDistanceMap);
		TArrayView<float> MaxDistance = Cloth.GetWeightMap(MaxDistanceMap);
		for (int32 Index = 0; Index < MaxDistance.Num(); ++Index)
		{
			const bool bPinned = Index < Anchored.Num() && Anchored[Index];
			const float Free = Reach.IsValidIndex(Index)
				? FMath::Clamp(LeashFraction * Reach[Index], LeashMin, LeashMax)
				: LeashMax;
			MaxDistance[Index] = bPinned ? 0.f : Free;
			Result.KinematicVertices += bPinned ? 1 : 0;
		}

		// --- the config -------------------------------------------------------------------
		//
		// Built the way the engine's own default-config node builds it: a `UChaosClothConfig`
		// pair is turned into a property collection by `FClothingSimulationConfig` and copied
		// onto the cloth collection whole. Hand-writing the handful of properties a garment
		// "obviously needs" produces a collection that is missing solver frequency, iteration
		// counts, tether stiffness and the rest — and every one of those absences is silent,
		// because a property the solver cannot find resolves to a default that disables its
		// constraint rather than to an error.
		double GravityScale = 1.0;
		Garment->TryGetNumberField(TEXT("gravity_scale"), GravityScale);

		UChaosClothConfig* const ClothConfig = NewObject<UChaosClothConfig>();
		UChaosClothSharedSimConfig* const SharedConfig = NewObject<UChaosClothSharedSimConfig>();

		// A weighted value with no weight map behind it: both ends are the same number, so the
		// property is constant over the garment.
		auto Weighted = [](const float Value)
		{
			return FChaosClothWeightedValue{ Value, Value };
		};

		// VtMB's authored gravity scale, carried as the solver's own multiplier rather than baked
		// into a gravity vector, so the panel can move it live. The authored number really is
		// about that many g -- StudioRender integrates `scale * 384 * dt^2`, and 384 in/s^2 is
		// 32 ft/s^2 -- so a scale of 5 is a garment falling at five gravities and not a unit
		// mismatch to be corrected away. The tuned multiplier rides on top of it, so a material
		// can be adjusted without discarding what the artist asked for.
		ClothConfig->GravityScale =
			static_cast<float>(GravityScale) * RetailGravityRatio * Tuning.GravityMultiplier;

		// What the garment is made of. VtMB states none of this — its solver had no material at
		// all — so every value here is the tuning table's, keyed by the material this garment was
		// read as. Density is Chaos's own kg scale, where the engine's reference points run from
		// melton wool at 0.7 down to silk at 0.1.
		ClothConfig->MassMode = EClothMassMode::Density;
		ClothConfig->Density = Tuning.Density;
		ClothConfig->EdgeStiffnessWeighted = Weighted(Tuning.EdgeStiffness);
		ClothConfig->BendingStiffnessWeighted = Weighted(Tuning.BendStiffness);
		// Zero by default in the table, and deliberately: VtMB authors exactly two constraint
		// sets, the mesh's edges and its bend diagonals, and no area-preservation set at all. The
		// engine's default of 1 buys a constraint the garment was never designed around and
		// charges for it every substep.
		ClothConfig->AreaStiffnessWeighted = Weighted(Tuning.AreaStiffness);

		// Retail retains 0.97 of the previous displacement per substep at a 300 Hz target, and
		// Chaos states damping as the fraction removed per 60 Hz frame: 1 - 0.97^5. That is where
		// the table's default came from; a material may still move it.
		ClothConfig->DampingCoefficient = Tuning.Damping;
		ClothConfig->LocalDampingCoefficient = Tuning.LocalDamping;

		ClothConfig->CollisionThickness = Tuning.CollisionThickness;
		ClothConfig->FrictionCoefficient = Tuning.Friction;
		ClothConfig->TetherStiffness = Weighted(Tuning.TetherStiffness);
		ClothConfig->TetherScale = Weighted(Tuning.TetherScale);

		ClothConfig->LinearVelocityScale = FVector(
			Tuning.LinearVelocityScale, Tuning.LinearVelocityScale, Tuning.LinearVelocityScale);
		ClothConfig->AngularVelocityScale = Tuning.AngularVelocityScale;
		ClothConfig->FictitiousAngularScale = Tuning.FictitiousAngularScale;

		// Continuous collision detection. A garment on a character who turns, sits or takes a hit
		// moves far enough in one substep to pass straight through a hip capsule, and a discrete
		// test only ever asks where the particle ENDED — so the miss is silent and the hem comes
		// out the far side. It is per material because it is a real cost and most of what it buys
		// is on the long coats: a necktie on a sternum never meets a fast collider.
		ClothConfig->bUseCCD = Tuning.bUseCCD;

		// The solver block, shared by every garment. The substep clock is VtMB's own --
		// `N = min(30, round(elapsed * 300))` -- and Chaos computes
		// `Clamp(Round(DeltaTime * 1000 / DynamicSubstepDeltaTime), 1, NumSubsteps)`, so the
		// table's 3.3333 ms target and cap of 30 reproduce it rather than approximate it.
		const FElysiumClothSolverTuning& SolverConfig = TuningTable->Solver;
		SharedConfig->IterationCount = SolverConfig.IterationCount;
		SharedConfig->MaxIterationCount = SolverConfig.MaxIterationCount;
		SharedConfig->SubdivisionCount = SolverConfig.MaxSubsteps;
		ClothConfig->bUseSelfCollisions = SolverConfig.bUseSelfCollisions;

		::Chaos::FClothingSimulationConfig SimulationConfig;
		SimulationConfig.Initialize(ClothConfig, SharedConfig);
		SimulationConfig.GetPropertyCollection(0)->CopyTo(&Collection.Get());

		// The substep target has no `UChaosClothConfig` member, so it is added to the collection
		// directly. Without it Chaos runs a fixed substep count and the garment's response to
		// frame rate stops matching the game's.
		{
			::Chaos::Softs::FCollectionPropertyMutableFacade Properties(Collection);
			Properties.AddValue(FName(TEXT("DynamicSubstepDeltaTime")),
				SolverConfig.SubstepTargetMs);
		}
		Result.ConfigProperties =
			::Chaos::Softs::FCollectionPropertyConstFacade(Collection).Num();

		// --- long range attachment --------------------------------------------------------
		//
		// Tethers are what actually hold a garment on a body. They cap the geodesic distance
		// from every free particle to the kinematic set it hangs from, so a skirt cannot stretch
		// off the hips no matter how the edge springs relax. Every stock config enables them
		// (`TetherStiffness` defaults to 1) and generates them at build time — without this call
		// the property is on and there is no tether data for it to act on, which reads as a
		// garment that pins correctly at the waistband and pours onto the floor below it.
		//
		// Runs after the max distance map is filled: the unit range is the signal to read that
		// map as distances, and its sub-threshold entries are the fixed ends.
		FClothEngineTools::GenerateTethers(Collection, MaxDistanceMap,
			ClothConfig->bUseGeodesicDistance, FVector2f(0.f, 1.f));

		// --- the render surface: the body's own geometry, driven per vertex ---------------
		//
		// VtMB does not add a garment to a character. It draws the character's own surface and
		// SUBSTITUTES simulated positions into the vertices its selector map names, leaving every
		// other vertex ordinarily skinned. A waistband, a collar, the torso a coat is sewn into --
		// none of those are cloth, and all of them are the same mesh as the garment.
		//
		// So the render mesh here is the material's entire surface rather than the simulation
		// mesh. What makes a vertex cloth is its skinning blend: 0 takes its position from the
		// particle the payload names, 1 leaves it to the bones. The `+52` map IS the binding, so
		// each influence is a degenerate triangle on that one particle with barycentric weight 1
		// -- no proximity search and no approximation, the same correspondence the game reads.
		// One incident simulation triangle per particle, rotated so the particle is corner A.
		//
		// A degenerate triangle would place the position perfectly and shade nothing: the deformer
		// does not store a normal, it DERIVES one from the difference between two interpolated
		// points on the triangle. Collapse the triangle and both points coincide, so every driven
		// vertex gets a zero normal and renders black while the skinned ones beside it look right.
		TMap<int32, FIntVector3> ParticleTriangle;
		ParticleTriangle.Reserve(Rest.Num());
		for (const FIntVector3& Face : Faces)
		{
			ParticleTriangle.FindOrAdd(Face.X, FIntVector3(Face.X, Face.Y, Face.Z));
			ParticleTriangle.FindOrAdd(Face.Y, FIntVector3(Face.Y, Face.Z, Face.X));
			ParticleTriangle.FindOrAdd(Face.Z, FIntVector3(Face.Z, Face.X, Face.Y));
		}

		// The same accumulation Chaos performs every frame — `cross(P2 - P0, P1 - P0)` summed over
		// a particle's faces — run once here on the rest pose. It answers two things: a sheet
		// whose faces cancel becomes a number in the build report rather than an unlit garment in
		// the viewport, and each driven vertex's normal sign becomes measurable below.
		TArray<FVector3f> ParticleNormal;
		ParticleNormal.SetNumZeroed(Rest.Num());
		for (const FIntVector3& Face : Faces)
		{
			const FVector3f Normal = FVector3f::CrossProduct(
				Rest[Face.Z] - Rest[Face.X], Rest[Face.Y] - Rest[Face.X]);
			ParticleNormal[Face.X] += Normal;
			ParticleNormal[Face.Y] += Normal;
			ParticleNormal[Face.Z] += Normal;
		}
		for (const TPair<int32, FIntVector3>& Incident : ParticleTriangle)
		{
			Result.DegenerateSimNormals += ParticleNormal[Incident.Key].IsNearlyZero() ? 1 : 0;
		}

		const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
		Garment->TryGetArrayField(TEXT("render_maps"), Maps);
		for (int32 MapIndex = 0; Maps != nullptr && MapIndex < Maps->Num(); ++MapIndex)
		{
			const TSharedPtr<FJsonObject> Map = (*Maps)[MapIndex]->AsObject();
			if (!Map.IsValid())
			{
				continue;
			}
			FString MaterialName;
			Map->TryGetStringField(TEXT("material"), MaterialName);
			FSoftObjectPath Material = ResolveGarmentMaterial(*Mesh, MaterialName);
			if (Material.IsNull())
			{
				// Deliberately a loud fallback: the engine's cloth editor material is a two-tone
				// camera-lit shader, so a garment that could not find its own material announces
				// it rather than looking subtly wrong.
				Result.Errors.Add(FString::Printf(
					TEXT("no material '%s' on %s - falling back to the cloth editor material"),
					*MaterialName, *Mesh->GetName()));
				Material = FSoftObjectPath(
					TEXT("/Engine/EditorMaterials/Cloth/CameraLitDoubleSided.CameraLitDoubleSided"));
			}

			const TArray<TSharedPtr<FJsonValue>>& Positions = Map->GetArrayField(TEXT("positions"));
			const TArray<TSharedPtr<FJsonValue>>& MapUVs = Map->GetArrayField(TEXT("uvs"));
			const TArray<TSharedPtr<FJsonValue>>& Skin = Map->GetArrayField(TEXT("skin"));
			const TArray<TSharedPtr<FJsonValue>>& Vertices = Map->GetArrayField(TEXT("vertices"));
			const TArray<TSharedPtr<FJsonValue>>& RenderFaces = Map->GetArrayField(TEXT("triangles"));

			FCollectionClothRenderPatternFacade Render = Cloth.AddGetRenderPattern();
			Render.SetRenderMaterialSoftObjectPathName(Material);
			Render.SetRenderDeformerNumInfluences(1);
			Render.SetNumRenderVertices(Positions.Num());
			Render.SetNumRenderFaces(RenderFaces.Num());

			const TArrayView<FVector3f> RenderPosition = Render.GetRenderPosition();
			const TArrayView<FVector3f> RenderNormal = Render.GetRenderNormal();
			const TArrayView<FVector3f> RenderTangentU = Render.GetRenderTangentU();
			const TArrayView<FVector3f> RenderTangentV = Render.GetRenderTangentV();
			const TArrayView<TArray<FVector2f>> RenderUVs = Render.GetRenderUVs();
			const TArrayView<FLinearColor> RenderColor = Render.GetRenderColor();
			const TArrayView<TArray<int32>> RenderBoneIndices = Render.GetRenderBoneIndices();
			const TArrayView<TArray<float>> RenderBoneWeights = Render.GetRenderBoneWeights();
			const TArrayView<FIntVector3> RenderIndices = Render.GetRenderIndices();
			const TArrayView<TArray<FVector4f>> DeformerPosition =
				Render.GetRenderDeformerPositionBaryCoordsAndDist();
			const TArrayView<TArray<FVector4f>> DeformerNormal =
				Render.GetRenderDeformerNormalBaryCoordsAndDist();
			const TArrayView<TArray<FVector4f>> DeformerTangent =
				Render.GetRenderDeformerTangentBaryCoordsAndDist();
			const TArrayView<TArray<FIntVector3>> DeformerIndices =
				Render.GetRenderDeformerSimIndices3D();
			const TArrayView<TArray<float>> DeformerWeight = Render.GetRenderDeformerWeight();
			const TArrayView<float> SkinningBlend = Render.GetRenderDeformerSkinningBlend();

			// The binding cannot be written until the tangent basis exists, and that is
			// accumulated over the faces below, so the particle each vertex follows is kept here
			// and spent in the final pass.
			TArray<int32> VertexParticle;
			VertexParticle.SetNumUninitialized(Positions.Num());

			for (int32 Index = 0; Index < Positions.Num(); ++Index)
			{
				RenderPosition[Index] = VectorFrom(Positions[Index]->AsArray());
				RenderNormal[Index] = FVector3f::ZeroVector;   // accumulated over the faces below
				RenderTangentU[Index] = FVector3f::ZeroVector;
				RenderTangentV[Index] = FVector3f::ZeroVector;
				RenderColor[Index] = FLinearColor::White;

				const TArray<TSharedPtr<FJsonValue>> UV = MapUVs.IsValidIndex(Index)
					? MapUVs[Index]->AsArray() : TArray<TSharedPtr<FJsonValue>>();
				RenderUVs[Index] = { UV.Num() >= 2
					? FVector2f(static_cast<float>(UV[0]->AsNumber()),
						static_cast<float>(UV[1]->AsNumber()))
					: FVector2f::ZeroVector };

				RenderBoneIndices[Index].Reset();
				RenderBoneWeights[Index].Reset();
				if (Skin.IsValidIndex(Index))
				{
					for (const TSharedPtr<FJsonValue>& Influence : Skin[Index]->AsArray())
					{
						const TArray<TSharedPtr<FJsonValue>> Pair = Influence->AsArray();
						const int32 Bone = Pair.Num() >= 2
							? Mesh->GetRefSkeleton().FindBoneIndex(FName(*Pair[0]->AsString()))
							: INDEX_NONE;
						if (Bone != INDEX_NONE)
						{
							RenderBoneIndices[Index].Add(Bone);
							RenderBoneWeights[Index].Add(static_cast<float>(Pair[1]->AsNumber()));
						}
					}
				}
				if (RenderBoneIndices[Index].IsEmpty())
				{
					RenderBoneIndices[Index].Add(0);
					RenderBoneWeights[Index].Add(1.f);
				}

				// The binding. A vertex with no row, or one the anchor pass claimed, is a vertex
				// VtMB skins -- the belt -- so it takes a zero blend and never reads the solver.
				const TSharedPtr<FJsonObject> Row = Vertices.IsValidIndex(Index)
					? Vertices[Index]->AsObject() : nullptr;
				int32 Particle = INDEX_NONE;
				if (Row.IsValid() && !Row->HasField(TEXT("anchor")))
				{
					Row->TryGetNumberField(TEXT("particle"), Particle);
				}
				const bool bDriven = Particle != INDEX_NONE && ParticleTriangle.Contains(Particle);
				// INVERTED against its name, and the engine is the authority: the deformer reads
				// this as the share that stays SKINNED, skips the deformation entirely at 1, and
				// weights the simulation by `1 - blend`. So a driven vertex is 0 and the belt is
				// 1. Reading it the other way round leaves the garment at its bind pose while the
				// waistband is the only thing the solver moves.
				SkinningBlend[Index] = bDriven ? 0.f : 1.f;
				VertexParticle[Index] = bDriven ? Particle : INDEX_NONE;
				Result.DrivenVertices += bDriven ? 1 : 0;
				Result.SkinnedVertices += bDriven ? 0 : 1;
			}

			// Faces, plus the normals and tangent basis accumulated over them. The shading normal
			// is the LEFT-hand one, which is the side Unreal's culling and its cloth path agree on.
			const int32 VertexOffset = Render.GetRenderVerticesOffset();
			for (int32 Face = 0; Face < RenderFaces.Num(); ++Face)
			{
				const TArray<TSharedPtr<FJsonValue>> Corners = RenderFaces[Face]->AsArray();
				if (Corners.Num() < 3)
				{
					continue;
				}
				const int32 A = static_cast<int32>(Corners[0]->AsNumber());
				const int32 B = static_cast<int32>(Corners[1]->AsNumber());
				const int32 C = static_cast<int32>(Corners[2]->AsNumber());
				RenderIndices[Face] =
					FIntVector3(A + VertexOffset, B + VertexOffset, C + VertexOffset);

				const FVector3f Edge1 = RenderPosition[B] - RenderPosition[A];
				const FVector3f Edge2 = RenderPosition[C] - RenderPosition[A];
				const FVector3f Normal = -FVector3f::CrossProduct(Edge1, Edge2);

				const FVector2f UV1 = RenderUVs[B][0] - RenderUVs[A][0];
				const FVector2f UV2 = RenderUVs[C][0] - RenderUVs[A][0];
				const float Denom = UV1.X * UV2.Y - UV1.Y * UV2.X;
				const float InvDenom = FMath::Abs(Denom) < UE_SMALL_NUMBER ? 0.f : 1.f / Denom;
				const FVector3f TangentU = (Edge1 * UV2.Y - Edge2 * UV1.Y) * InvDenom;
				const FVector3f TangentV = (Edge2 * UV1.X - Edge1 * UV2.X) * InvDenom;

				for (const int32 Corner : { A, B, C })
				{
					RenderNormal[Corner] += Normal;
					RenderTangentU[Corner] += TangentU;
					RenderTangentV[Corner] += TangentV;
				}
			}
			for (int32 Index = 0; Index < Positions.Num(); ++Index)
			{
				RenderNormal[Index] = RenderNormal[Index].GetSafeNormal(
					UE_SMALL_NUMBER, FVector3f::ZAxisVector);
				RenderTangentU[Index] = RenderTangentU[Index].GetSafeNormal(
					UE_SMALL_NUMBER, FVector3f::XAxisVector);
				RenderTangentV[Index] = RenderTangentV[Index].GetSafeNormal(
					UE_SMALL_NUMBER, FVector3f::YAxisVector);

				// --- the binding -------------------------------------------------------------
				//
				// The deformer evaluates `Bary.X*(A + NA*W) + ...` three times, for the position,
				// for a point offset along the normal, and for one offset along the tangent; it
				// then takes the two offsets MINUS the position as the shading basis. So the W
				// distances are what carry the normal and tangent, and the barycentric triple is
				// what carries the position. `GpuSkinCacheComputeShader.usf` is the authority --
				// the editor-side preview in `ClothGeometryTools.cpp` spells the same expression
				// with the opposite sign and is not what ships the frame.
				const int32 Particle = VertexParticle[Index];
				if (Particle == INDEX_NONE)
				{
					// Skinned. The deformer skips a blend of 1 outright, so these are inert; they
					// only have to be in range.
					DeformerIndices[Index] = { FIntVector3(0, 0, 0) };
					DeformerPosition[Index] = { FVector4f::Zero() };
					DeformerNormal[Index] = { FVector4f::Zero() };
					DeformerTangent[Index] = { FVector4f::Zero() };
					DeformerWeight[Index] = { 0.f };
					continue;
				}

				const FIntVector3 Triangle = ParticleTriangle.FindChecked(Particle);
				DeformerIndices[Index] = { Triangle };
				DeformerWeight[Index] = { 1.f };

				// Corner A is the particle itself, so the whole weight there and no offset is its
				// position exactly -- VtMB's own substitution, not an interpolation of it.
				DeformerPosition[Index] = { FVector4f(1.f, 0.f, 0.f, 0.f) };

				// The engine hands the deformer the simulation normal already NEGATED, so a
				// distance of -1 recovers it and +1 its opposite. Which one a vertex wants is
				// MEASURED here rather than taken from VtMB's high bit on the position index:
				// that bit is stated against VtMB's own sheet orientation, and the sheet this
				// build induces from the render surface carries its own. The authored render
				// normal is the answer the shading has to reproduce, so the sign that reproduces
				// it is the sign to store -- the bake resolving state instead of forwarding a
				// question the frame path would have to know a VtMB rule to answer.
				const bool bSimNormalAgrees = FVector3f::DotProduct(
					RenderNormal[Index], ParticleNormal[Particle]) >= 0.f;
				DeformerNormal[Index] = {
					FVector4f(1.f, 0.f, 0.f, bSimNormalAgrees ? -1.f : 1.f) };

				// The tangent is the one that needs the triangle to be real: it is recovered as a
				// point minus the position, so it has to be reachable across the face. The
				// authored tangent is resolved onto the two edges leaving A and stored as those
				// barycentric weights.
				const FVector3f A = Rest[Triangle.X];
				const FVector3f EdgeB = Rest[Triangle.Y] - A;
				const FVector3f EdgeC = Rest[Triangle.Z] - A;
				const float BB = FVector3f::DotProduct(EdgeB, EdgeB);
				const float BC = FVector3f::DotProduct(EdgeB, EdgeC);
				const float CC = FVector3f::DotProduct(EdgeC, EdgeC);
				const float TB = FVector3f::DotProduct(RenderTangentU[Index], EdgeB);
				const float TC = FVector3f::DotProduct(RenderTangentU[Index], EdgeC);
				const float Det = BB * CC - BC * BC;
				if (FMath::Abs(Det) > UE_SMALL_NUMBER)
				{
					const float V = (TB * CC - TC * BC) / Det;
					const float W = (TC * BB - TB * BC) / Det;
					DeformerTangent[Index] = { FVector4f(1.f - V - W, V, W, 0.f) };
				}
				else
				{
					// A sliver: fall back to the far corner, which is a real direction across the
					// face even when it is not the authored one.
					DeformerTangent[Index] = { FVector4f(0.f, 1.f, 0.f, 0.f) };
				}
			}
		}

		FClothGeometryTools::CleanupAndCompactMesh(Collection);

		// Compaction drops simulation vertices no triangle uses, and the collection remaps every
		// deformer index that named one to INDEX_NONE — correctly, because the particle is gone.
		// A render vertex still asking to be driven by it is now bound to nothing, and that is
		// the kind of thing the frame shows rather than says. Counted here so it is said.
		{
			const TConstArrayView<TArray<FIntVector3>> Bindings =
				Cloth.GetRenderDeformerSimIndices3D();
			const TConstArrayView<float> Blend = Cloth.GetRenderDeformerSkinningBlend();
			for (int32 Index = 0; Index < Bindings.Num(); ++Index)
			{
				// A blend of 1 is SKINNED, and the deformer skips those influences outright, so
				// their indices are inert whatever compaction did to them. Only a driven vertex
				// -- blend 0 -- is actually bound to the particle named here.
				if (!Blend.IsValidIndex(Index) || Blend[Index] >= 1.f)
				{
					continue;
				}
				for (const FIntVector3& Influence : Bindings[Index])
				{
					if (Influence.X == INDEX_NONE || Influence.Y == INDEX_NONE
						|| Influence.Z == INDEX_NONE)
					{
						++Result.OrphanedBindings;
						break;
					}
				}
			}
			if (Result.OrphanedBindings > 0)
			{
				Result.Errors.Add(FString::Printf(
					TEXT("%d driven render vertices lost their particle to mesh compaction - "
						 "they will draw at the component origin"),
					Result.OrphanedBindings));
			}
		}

		// --- the asset --------------------------------------------------------------------
		FString ObjectPath;
		UPackage* Package = MakePackage(PackageDirectory, BaseName, ObjectPath);
		UChaosClothAsset* Asset = NewObject<UChaosClothAsset>(
			Package, *BaseName, RF_Public | RF_Standalone | RF_Transactional);
		if (Asset == nullptr)
		{
			Result.Errors.Add(TEXT("could not create the cloth asset"));
			Results.Add(Result);
			continue;
		}

		TArray<TSharedRef<const FManagedArrayCollection>> Collections;
		Collections.Add(Collection);
		Asset->Build(Collections);

		// Read the built model back. What was written into the collection and what the solver
		// receives are separated by several stages that drop rather than complain, so this is the
		// only statement about the asset that is worth anything.
		if (const TSharedPtr<const FChaosClothSimulationModel> Model = Asset->GetClothSimulationModel())
		{
			Result.BuiltSimVertices = Model->GetNumVertices(0);
			if (Model->IsValidLodIndex(0))
			{
				const FChaosClothSimulationLodModel& Lod = Model->ClothSimulationLodModels[0];
				if (const TArray<float>* const Built = Lod.WeightMaps.Find(MaxDistanceMap))
				{
					for (const float Value : *Built)
					{
						// The solver's own threshold, not a tolerance of ours.
						Result.BuiltKinematicVertices += Value < 0.1f ? 1 : 0;
					}
				}
				for (const TArray<TTuple<int32, int32, float>>& Batch : Lod.TetherData.Tethers)
				{
					Result.BuiltTethers += Batch.Num();
				}
			}
		}

		FAssetRegistryModule::AssetCreated(Asset);
		Package->MarkPackageDirty();

		Result.AssetPath = ObjectPath;
		Result.SimVertices = Rest.Num();
		Result.SimFaces = Faces.Num();
		Result.CollisionBodies = Bodies;
		Results.Add(Result);
	}

	return Results;
}

#endif  // WITH_EDITOR
