#include "Substrate/ElysiumModelPhysics.h"
#include "ElysiumModelPhysicsInputs.h"
#include "ElysiumPhysicalMaterial.h"
#include "ElysiumPhysicsSurfaceProps.h"
#include "ElysiumPhysicsPropComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/ScopeExit.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"

namespace
{
	constexpr int32 NativeResponseChannelCount = sizeof(FCollisionResponseContainer().EnumArray);
	struct FPhysicsRecipeAssociation
	{
		void* Actor = nullptr;
		UStaticMeshComponent* Producer = nullptr; // equality only, valid also during component destruction
		TWeakObjectPtr<UStaticMeshComponent> Component;
		FElysiumPhysicsBodyRecipe Recipe;
	};
	// Semantic manager#2/model and manager#1/bbox associations (10044db0/10044f60).
	// No Source hash/tree layout; identity is the actual successful Chaos incarnation.
	TArray<FPhysicsRecipeAssociation> PhysicsRecipeAssociations;
	struct FNativeAssociation { void* Actor; TWeakObjectPtr<UStaticMeshComponent> Component; UStaticMeshComponent* Producer; };
	TArray<FNativeAssociation> NativeAssociations;
	struct FNativeReference { FElysiumEntity* Entity; void* Actor; };
	TArray<FNativeReference> NativeReferences;
	bool CreatePhysicsRecipe(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
		const FElysiumPhysicsBodyRecipe& Recipe, bool bStartAsleep, FString& OutError);
}

FArchive& operator<<(FArchive& Ar, FElysiumSavedPhysicsBody& State)
{
	uint8 Kind = static_cast<uint8>(State.Recipe.Kind);
	Ar << State.EntityIndex << State.FieldName << Kind;
	if (Kind > static_cast<uint8>(EElysiumPhysicsRecipeKind::Bbox)) Ar.SetError();
	State.Recipe.Kind = static_cast<EElysiumPhysicsRecipeKind>(Kind);
	Ar << State.bHasObject;
	Ar << State.Recipe.bStatic << State.Recipe.ModelAsset << State.Recipe.Surface << State.Recipe.BboxMin << State.Recipe.BboxMax;
	Ar << State.Recipe.MassKg << State.Recipe.InertiaScale << State.Recipe.LinearDamping << State.Recipe.AngularDamping;
	Ar << State.Recipe.bMassCenter << State.Recipe.MassCenterCm;
	Ar << State.Recipe.RotInertiaLimit << State.Recipe.Volume << State.Recipe.Drag << State.Recipe.RollingDrag << State.Recipe.SolidPairsJson;
	Ar << State.Pose << State.LinearVelocity << State.AngularVelocityRadians << State.bAsleep << State.bGravity;
	Ar << State.CurrentMassKg << State.CurrentLinearDamping << State.CurrentAngularDamping;
	Ar << State.CollisionProfile << State.CollisionMask << State.CollisionResponses << State.BboxCollisionEnabled;
	if (Ar.IsLoading() && State.bHasObject && (State.CollisionResponses.Num() != NativeResponseChannelCount
		|| State.CollisionResponses.ContainsByPredicate([](uint8 Response) { return Response > ECR_Block; })
		|| (State.Recipe.Kind == EElysiumPhysicsRecipeKind::Bbox && State.BboxCollisionEnabled > ECollisionEnabled::QueryAndProbe)))
		Ar.SetError();
	return Ar;
}

void ElysiumModelPhysics::RetireRecipe(UStaticMeshComponent* Component)
{
	// 10045110: first matching model association, otherwise first bbox; sleeping is not removal.
	for (EElysiumPhysicsRecipeKind Kind : { EElysiumPhysicsRecipeKind::Model, EElysiumPhysicsRecipeKind::Bbox })
	{
		const int32 Found = PhysicsRecipeAssociations.IndexOfByPredicate([Component, Kind](const FPhysicsRecipeAssociation& Entry)
			{ return Entry.Producer == Component && Entry.Recipe.Kind == Kind; });
		if (Found != INDEX_NONE) { PhysicsRecipeAssociations.RemoveAt(Found); return; }
	}
}

UStaticMeshComponent* ElysiumModelPhysics::ComponentFor(void* Actor)
{
	if (!Actor) return nullptr;
	for (const FNativeAssociation& Entry : NativeAssociations)
		if (Entry.Actor == Actor && ActorIdentity(Entry.Component.Get()) == Actor) return Entry.Component.Get();
	return nullptr;
}

void ElysiumModelPhysics::BindReference(FElysiumEntity* Entity, void* Actor)
{
	NativeReferences.RemoveAll([Entity](const FNativeReference& Entry) { return Entry.Entity == Entity; });
	if (Actor) NativeReferences.Add({ Entity, Actor });
}

void ElysiumModelPhysics::RetireNative(UStaticMeshComponent* Component)
{
	// Called before Super::OnDestroyPhysicsState releases Chaos. No field may retain a dead actor.
	TArray<void*> RetiringActors;
	for (int32 Index = NativeAssociations.Num() - 1; Index >= 0; --Index)
	{
		if (NativeAssociations[Index].Producer != Component) continue;
		RetiringActors.Add(NativeAssociations[Index].Actor);
		NativeAssociations.RemoveAt(Index);
	}
	RetireRecipe(Component); //10158520 ->10045110 precedes native deletion and observer callbacks.
	for (void* Actor : RetiringActors)
	{
		TArray<FElysiumEntity*> Referencing;
		for (const FNativeReference& Entry : NativeReferences)
			if (Entry.Actor == Actor) Referencing.Add(Entry.Entity);
		for (FElysiumEntity* Entity : Referencing)
			Entity->VPhysicsSwapObject(nullptr);
		for (FElysiumEntity* Entity : Referencing)
		{
			Entity->OnPhysicsActorRetired();
			if (Entity->World)
				Entity->World->EmitAiTrace(*Entity, TEXT("physics_retire"), TEXT("references=0 recipe=0 native_release=pending"));
		}
	}
}

void ElysiumModelPhysics::DestroyActor(void* Actor)
{
	Destroy(ComponentFor(Actor)); // generic225 destroys the supplied object, including an alias.
}

const FElysiumPhysicsBodyRecipe* ElysiumModelPhysics::RecipeFor(void* Actor)
{
	if (!Actor) return nullptr;
	for (const FPhysicsRecipeAssociation& Entry : PhysicsRecipeAssociations)
		if (Entry.Actor == Actor && ActorIdentity(Entry.Component.Get()) == Actor) return &Entry.Recipe;
	return nullptr;
}

bool ElysiumModelPhysics::Capture(void* Actor, FElysiumSavedPhysicsBody& OutState)
{
	UStaticMeshComponent* Component = ComponentFor(Actor);
	const FElysiumPhysicsBodyRecipe* Recipe = RecipeFor(Actor);
	const FBodyInstance* NativeBody = Identity(Component);
	if (!NativeBody || !Recipe) return false;
	OutState.Recipe = *Recipe;
	OutState.bHasObject = true;
	OutState.Pose = NativeBody->GetUnrealWorldTransform();
	OutState.LinearVelocity = NativeBody->GetUnrealWorldVelocity();
	OutState.AngularVelocityRadians = NativeBody->GetUnrealWorldAngularVelocityInRadians();
	OutState.bAsleep = !Recipe->bStatic && !NativeBody->IsInstanceAwake();
	OutState.bGravity = NativeBody->bEnableGravity;
	OutState.CurrentMassKg = Recipe->bStatic ? Recipe->MassKg : NativeBody->GetBodyMass();
	OutState.CurrentLinearDamping = NativeBody->LinearDamping;
	OutState.CurrentAngularDamping = NativeBody->AngularDamping;
	OutState.CollisionProfile = Component->GetCollisionProfileName();
	OutState.CollisionMask = NativeBody->GetMaskFilter();
	OutState.CollisionResponses.Reset();
	// EngineTypes.h: only the 64 response-container entries; deprecated pseudochannel has no entry.
	for (int32 Channel = 0; Channel < NativeResponseChannelCount; ++Channel)
		OutState.CollisionResponses.Add(static_cast<uint8>(Component->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Channel))));
	if (Recipe->Kind == EElysiumPhysicsRecipeKind::Bbox)
		OutState.BboxCollisionEnabled = static_cast<uint8>(NativeBody->GetShapeCollisionEnabled(0));
	return true;
}

FBodyInstance* ElysiumModelPhysics::Identity(UStaticMeshComponent* Component)
{
	FBodyInstance* Body = Component && !Component->IsBeingDestroyed() ? Component->GetBodyInstance() : nullptr;
	return Body && Body->IsValidBodyInstance() ? Body : nullptr;
}

bool ElysiumModelPhysics::IsActive(UStaticMeshComponent* Component)
{
	const FBodyInstance* Body = Identity(Component);
	return Body && Component->IsSimulatingPhysics() && Body->IsInstanceAwake();
}

Chaos::FSingleParticlePhysicsProxy* ElysiumModelPhysics::ActorIdentity(UStaticMeshComponent* Component)
{
	const FBodyInstance* NativeBody = Identity(Component);
	return NativeBody ? NativeBody->GetPhysicsActor() : nullptr;
}

void ElysiumModelPhysics::Destroy(UStaticMeshComponent* Component)
{
	if (Component)
	{
		RetireNative(Component); // fields and recipe retire before environment/native deletion.
		Component->SetSimulatePhysics(false);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// Collision changes can create a native static actor even for a non-simulating component.
		// Destroy last so failed creation/removal cannot leave that replacement behind.
		Component->DestroyPhysicsState();
	}
}

bool ElysiumModelPhysics::Create(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
	float MassBias, float OverrideMass, bool bStartAsleep, FString& OutError)
{
	OutError.Reset();
	if (!Component) { OutError = TEXT("missing placed component"); return false; }
	Destroy(Component); // retire any old native identity before validating its replacement.
	const UStaticMesh* Mesh = Component->GetStaticMesh();
	const auto* Inputs = UElysiumModelPhysicsInputs::Find(Mesh);
	const UBodySetup* Setup = Inputs ? Inputs->SelectedBody.Get() : nullptr;
	if (!Inputs || !Inputs->bSourceBody || !Setup || Setup->AggGeom.GetElementCount() == 0)
	{
		if (!Inputs) OutError = TEXT("model requires body-input reimport: uv run elysium import models");
		else if (!Inputs->bSourceBody) OutError = TEXT("model has no source VPhysics body");
		else OutError = TEXT("source body has missing selected native collision cook: reimport model");
		return false;
	}
	FElysiumPhysicsBodyRecipe Recipe;
	Recipe.ModelAsset = Mesh->GetPathName();
	Recipe.Surface = Inputs->SurfaceProperty;
	Recipe.MassKg = Inputs->MassKg;
	if (MassBias > 0.f) Recipe.MassKg = FMath::Clamp(Recipe.MassKg * MassBias, .5f, 1000000.f); // 10191510
	if (OverrideMass > 0.f) Recipe.MassKg = OverrideMass;
	Recipe.InertiaScale = Inputs->InertiaScale;
	Recipe.LinearDamping = Inputs->LinearDamping;
	Recipe.AngularDamping = Inputs->AngularDamping;
	Recipe.bMassCenter = Inputs->bMassCenter;
	Recipe.MassCenterCm = Inputs->MassCenterCm;
	Recipe.RotInertiaLimit = Inputs->RotInertiaLimit;
	Recipe.Volume = Inputs->Volume;
	Recipe.Drag = Inputs->Drag;
	Recipe.RollingDrag = Inputs->RollingDrag;
	Recipe.SolidPairsJson = Inputs->SolidPairsJson;
	return CreatePhysicsRecipe(Component, Surfaces, Recipe, bStartAsleep, OutError);
}

bool ElysiumModelPhysics::CreateBbox(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
	const FElysiumPhysicsBodyRecipe& Recipe, bool bStartAsleep, FString& OutError)
{
	if (Recipe.Kind != EElysiumPhysicsRecipeKind::Bbox)
	{ Destroy(Component); OutError = TEXT("bbox creator requires an authored bbox recipe"); return false; }
	return CreatePhysicsRecipe(Component, Surfaces, Recipe, bStartAsleep, OutError);
}

namespace
{
bool CreatePhysicsRecipe(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
	const FElysiumPhysicsBodyRecipe& Recipe, bool bStartAsleep, FString& OutError)
{
	OutError.Reset();
	ElysiumModelPhysics::Destroy(Component);
	bool bSucceeded = false;
	ON_SCOPE_EXIT { if (!bSucceeded) ElysiumModelPhysics::Destroy(Component); };
	auto* Proxy = Cast<UElysiumPhysicsPropComponent>(Component);
	if (!Proxy) { OutError = TEXT("body recipe requires native selected-body component"); return false; }
	Proxy->RecipeBody = nullptr;
	Proxy->bUseSelectedBody = true;
	if (Recipe.Kind == EElysiumPhysicsRecipeKind::Bbox)
	{
		const FVector Size = Recipe.BboxMax - Recipe.BboxMin;
		if (Recipe.BboxMin.ContainsNaN() || Recipe.BboxMax.ContainsNaN() || Size.GetMin() <= 0.f)
		{ OutError = TEXT("invalid authored bbox dimensions"); return false; }
		// Engine mesh is only a component carrier; Chaos geometry comes solely from saved authored bounds.
		Proxy->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Proxy->SetVisibility(false);
		UBodySetup* BboxSetup = NewObject<UBodySetup>(Proxy);
		BboxSetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		BboxSetup->bNeverNeedsCookedCollisionData = true; // analytic FKBoxElem, as native UBoxComponent
		FKBoxElem Box;
		Box.Center = (Recipe.BboxMin + Recipe.BboxMax) * .5;
		Box.X = Size.X; Box.Y = Size.Y; Box.Z = Size.Z;
		BboxSetup->AggGeom.BoxElems.Add(Box);
		Proxy->RecipeBody = BboxSetup;
		Proxy->DestroyPhysicsState(); // SetStaticMesh may have marked a no-setup physics state created
	}
	else
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Recipe.ModelAsset);
		const auto* Inputs = UElysiumModelPhysicsInputs::Find(Mesh);
		if (!Inputs || !Inputs->bSourceBody || !Inputs->SelectedBody || Inputs->SelectedBody->AggGeom.GetElementCount() == 0)
		{ OutError = TEXT("saved model recipe requires the selected source collision cook"); return false; }
		Proxy->SetStaticMesh(Mesh);
	}
	if (!FMath::IsFinite(Recipe.InertiaScale) || Recipe.InertiaScale == 0.f
		|| !FMath::IsFinite(Recipe.LinearDamping) || Recipe.LinearDamping < 0.f
		|| !FMath::IsFinite(Recipe.AngularDamping) || Recipe.AngularDamping < 0.f)
	{
		OutError = TEXT("authored inertia/damping input unsupported by Chaos adapter");
		return false;
	}
	int32 SurfaceIndex = Recipe.Surface.IsEmpty() ? -1 : Surfaces.GetSurfaceIndex(Recipe.Surface);
	if (SurfaceIndex < 0) SurfaceIndex = Surfaces.GetSurfaceIndex(TEXT("default")); // 26018ba0
	const auto* Surface = Surfaces.GetSurfaceData(SurfaceIndex);
	if (!Surface) { OutError = TEXT("source surface service has no default material"); return false; }
	auto* Material = NewObject<UElysiumPhysicalMaterial>(Component);
	Material->SourceName = Surfaces.GetSurfaceName(SurfaceIndex);
	Material->AssetId = TEXT("vtmb:surface-property:") + Material->SourceName;
	Material->Friction = Surface->Friction();
	Material->StaticFriction = Surface->Friction();
	Material->RawElasticity = Surface->Elasticity();
	Material->Restitution = Surface->Elasticity();
	Material->RawDensity = Surface->Density();
	Material->Density = Surface->Density() / 1000.f;
	Material->Thickness = Surface->Thickness();
	Component->SetPhysMaterialOverride(Material);
	// Direct authored inputs. Chaos integrates the selected hulls and owns the tensor/motion.
	const float Mass = Recipe.MassKg;
	if (!FMath::IsFinite(Mass) || Mass <= 0.f) { OutError = TEXT("non-positive authored mass unsupported by Chaos"); return false; }
	// 26018ba0 ->2602ea60: authored inertia multiplies the backend's principal moments.
	// Unreal's tensor scale is geometric (squared at fixed mass), so convert the input.
	Component->BodyInstance.InertiaTensorScale = FVector(FMath::Sqrt(Recipe.InertiaScale < 0.f ? 1.f : Recipe.InertiaScale));
	Component->BodyInstance.bStartAwake = !bStartAsleep;
	Component->SetLinearDamping(Recipe.LinearDamping);
	Component->SetAngularDamping(Recipe.AngularDamping);
	Component->SetMassOverrideInKg(NAME_None, Mass, true);
	Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Component->SetMobility(Recipe.bStatic ? EComponentMobility::Static : EComponentMobility::Movable);
	Component->SetSimulatePhysics(!Recipe.bStatic);
	if (Recipe.Kind == EElysiumPhysicsRecipeKind::Bbox) Component->RecreatePhysicsState();
	FBodyInstance* Body = ElysiumModelPhysics::Identity(Component);
	if (!Body) { ElysiumModelPhysics::Destroy(Component); OutError = TEXT("Chaos body creation failed"); return false; }
	if (Recipe.bMassCenter)
	{
		FTransform MassSpace = Body->GetMassSpaceLocal();
		MassSpace.SetTranslation(Recipe.MassCenterCm * Component->GetComponentScale());
		Body->SetMassSpaceLocal(MassSpace);
	}
	if (!Recipe.bStatic)
	{
		if (bStartAsleep) Body->PutInstanceToSleep();
		else Body->WakeInstance();
	}
	NativeAssociations.Add({ ElysiumModelPhysics::ActorIdentity(Component), Component, Component });
	FPhysicsRecipeAssociation& Entry = PhysicsRecipeAssociations.AddDefaulted_GetRef();
	Entry.Actor = ElysiumModelPhysics::ActorIdentity(Component);
	Entry.Producer = Component;
	Entry.Component = Component;
	Entry.Recipe = Recipe; // 1002e3a0 success ->10012f12 ->10044db0; bbox success ->10044f60.
	bSucceeded = true;
	return true;
}
}

bool ElysiumModelPhysics::Restore(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
	const FElysiumSavedPhysicsBody& State, FString& OutError)
{
	Destroy(Component);
	if (!Component || State.Pose.ContainsNaN() || State.LinearVelocity.ContainsNaN() || State.AngularVelocityRadians.ContainsNaN()
		|| static_cast<uint8>(State.Recipe.Kind) > static_cast<uint8>(EElysiumPhysicsRecipeKind::Bbox)
		|| !FMath::IsFinite(State.CurrentMassKg) || State.CurrentMassKg <= 0.f
		|| !FMath::IsFinite(State.CurrentLinearDamping) || State.CurrentLinearDamping < 0.f
		|| !FMath::IsFinite(State.CurrentAngularDamping) || State.CurrentAngularDamping < 0.f)
	{ OutError = TEXT("invalid saved native body state"); return false; }
	if (State.CollisionResponses.Num() != NativeResponseChannelCount
		|| State.CollisionResponses.ContainsByPredicate([](uint8 Response) { return Response > ECR_Block; })
		|| (State.Recipe.Kind == EElysiumPhysicsRecipeKind::Bbox && State.BboxCollisionEnabled > ECollisionEnabled::QueryAndProbe))
	{ OutError = TEXT("invalid saved native query filter state"); return false; }
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetWorldTransform(State.Pose, false, nullptr, ETeleportType::TeleportPhysics);
	Component->SetEnableGravity(State.bGravity);
	if (!State.CollisionProfile.IsNone()) Component->SetCollisionProfileName(State.CollisionProfile);
	Component->SetMaskFilterOnBodyInstance(State.CollisionMask);
	if (!CreatePhysicsRecipe(Component, Surfaces, State.Recipe, State.bAsleep, OutError)) return false;
	if (State.CollisionResponses.Num() == NativeResponseChannelCount)
		for (int32 Channel = 0; Channel < NativeResponseChannelCount; ++Channel)
			Component->SetCollisionResponseToChannel(static_cast<ECollisionChannel>(Channel), static_cast<ECollisionResponse>(State.CollisionResponses[Channel]));
	FBodyInstance* Body = Identity(Component);
	if (State.Recipe.Kind == EElysiumPhysicsRecipeKind::Bbox)
		Body->SetShapeCollisionEnabled(0, static_cast<ECollisionEnabled::Type>(State.BboxCollisionEnabled));
	Component->SetMassOverrideInKg(NAME_None, State.CurrentMassKg, true);
	if (State.Recipe.bMassCenter)
	{
		FTransform MassSpace = Body->GetMassSpaceLocal();
		MassSpace.SetTranslation(State.Recipe.MassCenterCm * Component->GetComponentScale());
		Body->SetMassSpaceLocal(MassSpace); // native mass update can reset the explicit authored COM
	}
	Component->SetLinearDamping(State.CurrentLinearDamping);
	Component->SetAngularDamping(State.CurrentAngularDamping);
	if (!State.Recipe.bStatic)
	{
		Body->SetLinearVelocity(State.LinearVelocity, false);
		Body->SetAngularVelocityInRadians(State.AngularVelocityRadians, false);
		if (State.bAsleep) Body->PutInstanceToSleep();
		else Body->WakeInstance();
	}
	return true; // 100443e0 re-registers only the actual successful reconstructed object.
}
