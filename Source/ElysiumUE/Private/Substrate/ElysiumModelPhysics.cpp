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
#include "Engine/World.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

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
	struct FNativeAssociation
	{
		void* Actor; TWeakObjectPtr<UStaticMeshComponent> Component; UStaticMeshComponent* Producer;
		uint64 Incarnation; FElysiumEntityWorld* World = nullptr; FElysiumEntityHandle Owner;
		bool bCollectNativeOutput = false;
		bool bPendingInactive = false;
	};
	TArray<FNativeAssociation> NativeAssociations;
	uint64 NextNativeIncarnation = 0;
	TArray<uint64> NativeSolveOutputs;
	TMap<UWorld*, TArray<uint64>> NativeActiveOrders; // each native environment has its observable order
	struct FNativeReference { FElysiumEntity* Entity; void* Actor; };
	TArray<FNativeReference> NativeReferences;
	struct FShadowAssociation { void* Actor; FElysiumPhysicsShadowState State; };
	TArray<FShadowAssociation> ShadowAssociations;
	FShadowAssociation* FindShadow(void* Actor)
	{
		return ShadowAssociations.FindByPredicate([Actor](const FShadowAssociation& Entry) { return Entry.Actor == Actor; });
	}
	bool ValidShadow(const FElysiumPhysicsShadowState& State)
	{
		return !State.bInstalled || (!State.Target.ContainsNaN() && !State.PredictedPosition.ContainsNaN()
			&& State.Target.GetRotation().IsNormalized() && FMath::IsFinite(State.RemainingSeconds) && State.RemainingSeconds >= 0.f
			&& FMath::IsFinite(State.LinearDampingLimitCm) && FMath::IsFinite(State.LinearDesiredLimitCm)
			&& FMath::IsFinite(State.AngularDampingLimitRadians) && FMath::IsFinite(State.AngularDesiredLimitRadians)
			&& FMath::IsFinite(State.TeleportDistanceCm) && FMath::IsFinite(State.SavedMassKg) && State.SavedMassKg > 0.f
			&& !State.SavedInertia.ContainsNaN() && State.SavedInertia.GetMin() >= 0.f);
	}
	void SetInertia(FBodyInstance& Body, const FVector& Tensor)
	{
		FPhysicsCommand::ExecuteWrite(Body.GetPhysicsActor(), [&Tensor](const FPhysicsActorHandle& Handle)
		{ FPhysicsInterface::SetLocalInertiaTensor_AssumesLocked(Handle, Tensor); });
	}
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
	Ar << State.Shadow.bInstalled << State.Shadow.bActive << State.Shadow.bAllowMovement << State.Shadow.bAllowRotation << State.Shadow.bGravityMode;
	Ar << State.Shadow.Target << State.Shadow.PredictedPosition << State.Shadow.RemainingSeconds;
	Ar << State.Shadow.LinearDampingLimitCm << State.Shadow.LinearDesiredLimitCm << State.Shadow.AngularDampingLimitRadians << State.Shadow.AngularDesiredLimitRadians;
	Ar << State.Shadow.TeleportDistanceCm << State.Shadow.SavedMassKg << State.Shadow.SavedInertia << State.Shadow.bSavedGravity << State.bShadowMember;
	if (Ar.IsLoading() && (!ValidShadow(State.Shadow) || (State.Shadow.bInstalled && State.Recipe.bStatic)
		|| (State.bShadowMember && (!State.bHasObject || !State.Shadow.bInstalled)))) Ar.SetError();
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

void ElysiumModelPhysics::SetGameData(void* Actor, FElysiumEntity& Entity)
{
	for (FNativeAssociation& Entry : NativeAssociations)
		if (Entry.Actor == Actor)
		{
			Entry.World = Entity.World; Entry.Owner = Entity.Handle;
			Entity.EnableNativeLocalPose();
			return;
		}
}

void ElysiumModelPhysics::ForgetGameData(FElysiumEntity& Entity)
{
	for (FNativeAssociation& Entry : NativeAssociations)
		if (Entry.World == Entity.World && Entry.Owner == Entity.Handle)
		{ Entry.World = nullptr; Entry.Owner = FElysiumEntityHandle::Invalid(); }
}

void ElysiumModelPhysics::RecordNativeOutput(UStaticMeshComponent* Component)
{
	const UWorld* NativeWorld = Component ? Component->GetWorld() : nullptr;
	// OnSyncBodies publishes in the engine's EndPhysics tick. SkipPhysicsUpdate alone also
	// occurs in game-side presentation calls; require the admitted scene phase and actual body pose.
	const FBodyInstance* Body = Identity(Component);
	if (!NativeWorld || !NativeWorld->bInTick || NativeWorld->TickGroup != TG_EndPhysics || !Body) return;
	const FTransform NativePose = Body->GetUnrealWorldTransform();
	if (!Component->GetComponentLocation().Equals(NativePose.GetLocation(), .001)
		|| !Component->GetComponentQuat().Equals(NativePose.GetRotation(), .00001)) return;
	for (const FNativeAssociation& Entry : NativeAssociations)
		if (Entry.Producer == Component && Entry.bCollectNativeOutput && Component->IsSimulatingPhysics())
		{
			NativeSolveOutputs.AddUnique(Entry.Incarnation);
			NativeActiveOrders.FindOrAdd(Component->GetWorld()).AddUnique(Entry.Incarnation); // includes final sleeping output
			RecordNativeActivity(Component, IsActive(Component)); return;
		}
}

void ElysiumModelPhysics::RecordNativeActivity(UStaticMeshComponent* Component, bool bAwake)
{
	for (FNativeAssociation& Entry : NativeAssociations)
		if (Entry.Producer == Component && Component->IsSimulatingPhysics())
		{
			//2600fbb0: a pending sleep canceled before removal keeps its position.
			Entry.bPendingInactive = !bAwake;
			if (bAwake) NativeActiveOrders.FindOrAdd(Component->GetWorld()).AddUnique(Entry.Incarnation);
			return;
		}
}

void ElysiumModelPhysics::Wake(UStaticMeshComponent* Component)
{
	if (FBodyInstance* Body = Identity(Component)) { Body->WakeInstance(); RecordNativeActivity(Component, true); }
}

TArray<ElysiumModelPhysics::FNativeOutput> ElysiumModelPhysics::ActiveOutputs(FElysiumEntityWorld& World)
{
	//1002ea40/26011450: freeze native outputs before game callbacks, visit forwards.
	// Native wake/sleep output supplies activity; semantic append/fast-removal order is observable.
	TArray<FNativeOutput> Result;
	auto Append = [&](const FNativeAssociation& Entry)
	{
		if (Entry.World == &World && ComponentFor(Entry.Actor)) Result.Add({ Entry.Actor, Entry.Incarnation });
	};
	for (const auto& ActiveWorld : NativeActiveOrders)
		for (uint64 Incarnation : ActiveWorld.Value)
			if (const auto* Entry = NativeAssociations.FindByPredicate([Incarnation](const FNativeAssociation& Item) { return Item.Incarnation == Incarnation; })) Append(*Entry);
	for (FNativeAssociation& Entry : NativeAssociations)
		if (Entry.World == &World) Entry.bCollectNativeOutput = false;
	NativeSolveOutputs.RemoveAll([&](uint64 Incarnation)
	{
		const auto* Entry = NativeAssociations.FindByPredicate([Incarnation](const FNativeAssociation& Item) { return Item.Incarnation == Incarnation; });
		return !Entry || Entry->World == &World;
	});
	return Result;
}

bool ElysiumModelPhysics::IsCurrentOutput(const FNativeOutput& Output)
{
	return NativeAssociations.ContainsByPredicate([&](const FNativeAssociation& Entry)
	{ return Entry.Actor == Output.Actor && Entry.Incarnation == Output.Incarnation && ComponentFor(Entry.Actor); });
}

FElysiumEntity* ElysiumModelPhysics::GameDataFor(const FNativeOutput& Output, FElysiumEntityWorld& World)
{
	//1002ea40 reads object#6 after freezing objects, not entity fields/owners before callbacks.
	for (const FNativeAssociation& Entry : NativeAssociations)
		if (Entry.Actor == Output.Actor && Entry.Incarnation == Output.Incarnation && Entry.World == &World && ComponentFor(Entry.Actor))
			return World.Resolve(Entry.Owner);
	return nullptr;
}

bool ElysiumModelPhysics::GetPosition(void* Actor, FVector& OriginCm, FRotator& SourceAngles)
{
	const FBodyInstance* Body = Identity(ComponentFor(Actor));
	if (!Body) return false;
	const FTransform Pose = Body->GetUnrealWorldTransform(); // object37 26017eb0
	OriginCm = Pose.GetLocation();
	//26017eb0 ->26001310: reflected native basis, float32 components, double xy threshold.
	// This object's converter differs from entity MatrixAngles: roll is atan2+180,
	// and the degenerate arm returns literal180 rather than0. UE Rotator has different arms.
	const FVector X = Pose.GetRotation().RotateVector(FVector::ForwardVector);
	const FVector Y = Pose.GetRotation().RotateVector(FVector::RightVector);
	const FVector Z = Pose.GetRotation().RotateVector(FVector::UpVector);
	const float Q0 = static_cast<float>(X.X), Q4 = static_cast<float>(-X.Y), Q8 = static_cast<float>(X.Z);
	const float Q1 = static_cast<float>(-Y.X), Q5 = static_cast<float>(Y.Y), Q9 = static_cast<float>(-Y.Z), Q10 = static_cast<float>(Z.Z);
	const double Xy = FMath::Sqrt(static_cast<double>(Q0) * Q0 + static_cast<double>(Q4) * Q4);
	constexpr float Degrees = 57.29578f; //260c919c
	SourceAngles.Pitch = static_cast<float>(FMath::Atan2(-static_cast<double>(Q8), Xy) * Degrees);
	if (.001 < Xy)
	{
		SourceAngles.Yaw = static_cast<float>(FMath::Atan2(static_cast<double>(Q4), static_cast<double>(Q0)) * Degrees);
		SourceAngles.Roll = static_cast<float>(FMath::Atan2(-static_cast<double>(Q9), -static_cast<double>(Q10)) * Degrees + 180.);
	}
	else
	{
		SourceAngles.Yaw = static_cast<float>(FMath::Atan2(static_cast<double>(Q1), -static_cast<double>(Q5)) * Degrees);
		SourceAngles.Roll = 180.f;
	}
	return true;
}

void ElysiumModelPhysics::RetireNative(UStaticMeshComponent* Component)
{
	// Called before Super::OnDestroyPhysicsState releases Chaos. No field may retain a dead actor.
	TArray<void*> RetiringActors;
	for (int32 Index = NativeAssociations.Num() - 1; Index >= 0; --Index)
	{
		if (NativeAssociations[Index].Producer != Component) continue;
		RemoveShadow(NativeAssociations[Index].Actor);
		if (auto* Order = NativeActiveOrders.Find(Component->GetWorld())) Order->RemoveSingleSwap(NativeAssociations[Index].Incarnation);
		RetiringActors.Add(NativeAssociations[Index].Actor);
		NativeAssociations.RemoveAt(Index);
	}
	if (const auto* Order = NativeActiveOrders.Find(Component->GetWorld()); Order && Order->IsEmpty()) NativeActiveOrders.Remove(Component->GetWorld());
	RetireRecipe(Component); //10158520 ->10045110 precedes native deletion and observer callbacks.
	for (void* Actor : RetiringActors)
	{
		ShadowAssociations.RemoveAll([Actor](const FShadowAssociation& Entry) { return Entry.Actor == Actor; });
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
	OutState.Shadow = ShadowFor(Actor) ? *ShadowFor(Actor) : FElysiumPhysicsShadowState();
	return true;
}

const FElysiumPhysicsShadowState* ElysiumModelPhysics::ShadowFor(void* Actor)
{
	const FShadowAssociation* Entry = FindShadow(Actor);
	return Entry && ComponentFor(Actor) ? &Entry->State : nullptr;
}

void ElysiumModelPhysics::RemoveShadow(void* Actor)
{
	const FShadowAssociation* Entry = FindShadow(Actor);
	if (!Entry) return;
	const FElysiumPhysicsShadowState State = Entry->State;
	if (UStaticMeshComponent* Component = ComponentFor(Actor))
		if (FBodyInstance* Body = Identity(Component))
		{
			Component->SetMassOverrideInKg(NAME_None, State.SavedMassKg, true);
			SetInertia(*Body, State.SavedInertia);
			Component->SetEnableGravity(State.bSavedGravity);
		}
	ShadowAssociations.RemoveAll([Actor](const FShadowAssociation& Item) { return Item.Actor == Actor; });
}

bool ElysiumModelPhysics::SetShadow(void* Actor, float LinearSpeedInches, float AngularSpeedDegrees,
	bool bAllowMovement, bool bAllowRotation)
{
	UStaticMeshComponent* Component = ComponentFor(Actor);
	FBodyInstance* Body = Identity(Component);
	if (!Body || !Component->IsSimulatingPhysics() || !FMath::IsFinite(LinearSpeedInches) || !FMath::IsFinite(AngularSpeedDegrees)) return false;
	FShadowAssociation* Entry = FindShadow(Actor);
	if (!Entry)
	{
		Entry = &ShadowAssociations.AddDefaulted_GetRef(); Entry->Actor = Actor;
		auto& State = Entry->State;
		State.bInstalled = true; State.bAllowMovement = bAllowMovement; State.bAllowRotation = bAllowRotation;
		State.SavedMassKg = Body->GetBodyMass(); State.SavedInertia = Body->GetBodyInertiaTensor(); State.bSavedGravity = Body->bEnableGravity;
		if (!bAllowMovement) { Component->SetMassOverrideInKg(NAME_None, 1.0e6f, true); Component->SetEnableGravity(false); }
		//2601a7e0: prevent contact torque, while controller angular velocity remains writable.
		if (!bAllowRotation) SetInertia(*Body, FVector::ZeroVector);
	}
	//2601ac40: speed changes retain permissions and original body state.
	const float RequestedLinear = LinearSpeedInches * 2.54f;
	Entry->State.LinearDampingLimitCm = FMath::Max(static_cast<float>(Body->GetUnrealWorldVelocity().Size()), RequestedLinear);
	Entry->State.LinearDesiredLimitCm = RequestedLinear - Entry->State.LinearDampingLimitCm;
	const float RequestedAngular = FMath::DegreesToRadians(AngularSpeedDegrees);
	Entry->State.AngularDampingLimitRadians = FMath::Max(static_cast<float>(Body->GetUnrealWorldAngularVelocityInRadians().Size()), RequestedAngular);
	Entry->State.AngularDesiredLimitRadians = RequestedAngular - Entry->State.AngularDampingLimitRadians;
	return true;
}

bool ElysiumModelPhysics::UpdateShadow(void* Actor, const FVector& OriginCm, const FQuat& Rotation,
	bool bGravityMode, float ArrivalSeconds)
{
	UStaticMeshComponent* Component = ComponentFor(Actor); FBodyInstance* Body = Identity(Component);
	if (!Body || OriginCm.ContainsNaN() || Rotation.ContainsNaN() || !Rotation.IsNormalized() || !FMath::IsFinite(ArrivalSeconds)) return false;
	FShadowAssociation* Entry = FindShadow(Actor);
	if (!Entry) return false;
	auto& State = Entry->State;
	if (State.bGravityMode != bGravityMode)
	{
		State.bGravityMode = bGravityMode;
		if (State.bAllowMovement) Component->SetEnableGravity(!bGravityMode); //260185b0
	}
	const FQuat Prior = State.Target.GetRotation();
	const double QuaternionChange = FMath::Abs(Prior.X - Rotation.X) + FMath::Abs(Prior.Y - Rotation.Y)
		+ FMath::Abs(Prior.Z - Rotation.Z) + FMath::Abs(Prior.W - Rotation.W);
	const bool bChanged = FVector::DistSquared(State.Target.GetLocation(), OriginCm) > 1.0e-4 || QuaternionChange > 1.0e-8;
	State.Target = FTransform(Rotation, OriginCm);
	State.RemainingSeconds = FMath::Max(ArrivalSeconds, 0.f);
	State.bActive = true;
	if (bChanged) Wake(Component); //2601aad0: repeated target still replaces countdown, no wake.
	return true;
}

bool ElysiumModelPhysics::SetShadowTeleportDistance(void* Actor, float DistanceInches)
{
	FShadowAssociation* Entry = FindShadow(Actor);
	if (!Entry || !ComponentFor(Actor) || !FMath::IsFinite(DistanceInches)) return false;
	Entry->State.TeleportDistanceCm = DistanceInches * 2.54f; return true; //2601aa40
}

int32 ElysiumModelPhysics::GetShadowPosition(void* Actor, FVector* OriginCm, FQuat* Rotation)
{
	const FBodyInstance* Body = Identity(ComponentFor(Actor));
	if (!Body) return 0;
	const FTransform Solved = Body->GetUnrealWorldTransform(); //26018640: native sample, optional outputs.
	if (OriginCm) *OriginCm = Solved.GetLocation();
	if (Rotation) *Rotation = Solved.GetRotation();
	return 1;
}

namespace
{
	FVector ShadowVelocity(FVector Actual, const FVector& Delta, float FractionOverDelta,
		float MaximumSpeed, float MaximumDampingSpeed, double ZeroSpeedSquared)
	{
		if (Actual.SizeSquared() < ZeroSpeedSquared) Actual = FVector::ZeroVector; //26019600
		FVector Desired = Delta * FractionOverDelta;
		if (MaximumSpeed > 0.f && Desired.SizeSquared() > FMath::Square(MaximumSpeed)) Desired = Desired.GetSafeNormal() * MaximumSpeed;
		FVector Damping = -Actual; //constructor2601a670 dampingfactor1
		if (MaximumDampingSpeed > 0.f && Damping.SizeSquared() > FMath::Square(MaximumDampingSpeed)) Damping = Damping.GetSafeNormal() * MaximumDampingSpeed;
		return Actual + Damping + Desired;
	}
	void PrepareShadow(FBodyInstance& Body, FElysiumPhysicsShadowState& State, float DeltaSeconds)
	{
		if (!State.bActive) { State.PredictedPosition = FVector::ZeroVector; return; }
		const float Fraction = State.RemainingSeconds <= 0.f ? 1.f : FMath::Min(DeltaSeconds / State.RemainingSeconds, 1.f);
		State.RemainingSeconds = FMath::Max(State.RemainingSeconds - DeltaSeconds, 0.f); //2601aa50
		if (Fraction <= 0.f) return;
		const FTransform Current = Body.GetUnrealWorldTransform();
		const FVector Displacement = State.Target.GetLocation() - Current.GetLocation();
		if (State.TeleportDistanceCm > 0.f)
		{
			const bool bNoPrediction = State.PredictedPosition.X == 0. && State.PredictedPosition.Y == 0. && State.PredictedPosition.Z == 0.;
			const FVector Error = bNoPrediction ? Displacement : Current.GetLocation() - State.PredictedPosition;
			if (Error.SizeSquared() > FMath::Square(State.TeleportDistanceCm))
			{
				const auto Collision = Body.GetShapeCollisionEnabled(0);
				Body.SetShapeCollisionEnabled(0, ECollisionEnabled::NoCollision);
				Body.SetBodyTransform(State.Target, ETeleportType::TeleportPhysics);
				Body.SetShapeCollisionEnabled(0, Collision); //2601a250 retains preteleport displacement.
			}
		}
		const FVector Linear = ShadowVelocity(Body.GetUnrealWorldVelocity(), Displacement, Fraction / DeltaSeconds,
			State.LinearDesiredLimitCm, State.LinearDampingLimitCm, .01);
		Body.SetLinearVelocity(Linear, false);
		State.PredictedPosition = Current.GetLocation() + Linear * DeltaSeconds;
		FQuat Relative = Current.GetRotation().Inverse() * State.Target.GetRotation(); Relative.Normalize();
		float Angle = 2.f * FMath::Acos(FMath::Clamp(static_cast<float>(Relative.W), -1.f, 1.f));
		if (Angle > PI) Angle -= 2.f * PI;
		const FVector Axis = FVector(Relative.X, Relative.Y, Relative.Z).GetSafeNormal();
		const FVector LocalActual = Current.GetRotation().UnrotateVector(Body.GetUnrealWorldAngularVelocityInRadians());
		const FVector LocalAngular = ShadowVelocity(LocalActual, Axis * Angle, Fraction / DeltaSeconds,
			State.AngularDesiredLimitRadians, State.AngularDampingLimitRadians, 1.0e-6);
		Body.SetAngularVelocityInRadians(Current.GetRotation().RotateVector(LocalAngular), false);
	}
}

float ElysiumModelPhysics::PreparePhysicsFrame(UWorld& World, float FrameSeconds, float TimeScale, bool bSimulating)
{
	FPhysScene* Scene = World.GetPhysicsScene(); if (!Scene) return 0.f;
	auto& NativeActiveOrder = NativeActiveOrders.FindOrAdd(&World);
	//26010900/26010c20: remove prior pending inactivity before solving, last-entry replacement.
	for (int32 Index = NativeActiveOrder.Num() - 1; Index >= 0; --Index)
	{
		const auto* Entry = NativeAssociations.FindByPredicate([&](const FNativeAssociation& Item) { return Item.Incarnation == NativeActiveOrder[Index]; });
		if (!Entry || (Entry->Component.IsValid() && Entry->Component->GetWorld() == &World
			&& (Entry->bPendingInactive || !IsActive(Entry->Component.Get())))) NativeActiveOrder.RemoveAtSwap(Index);
	}
	// Discard prior unconsumed output before each native frame, including paused/zero-dt frames.
	NativeSolveOutputs.RemoveAll([&](uint64 Incarnation)
	{
		const auto* Entry = NativeAssociations.FindByPredicate([Incarnation](const FNativeAssociation& Item) { return Item.Incarnation == Incarnation; });
		return !Entry || (Entry->Component.IsValid() && Entry->Component->GetWorld() == &World);
	});
	float Delta = (FrameSeconds < 0.f || FrameSeconds > 1.f) ? 0.f : FMath::Min(FrameSeconds, .1f);
	Delta = FMath::Min(Delta * TimeScale, .1f); //1002ea40: real console-store value supplied by map host
	if (!bSimulating || !FMath::IsFinite(Delta) || !(Delta > .0001f && Delta < 1.f)) Delta = 0.f; //26010900
	for (FNativeAssociation& Entry : NativeAssociations)
		if (Entry.Component.IsValid() && Entry.Component->GetWorld() == &World) Entry.bCollectNativeOutput = Delta > 0.f;
	const FVector Gravity(0., 0., World.GetGravityZ());
	Scene->SetUpForFrame(&Gravity, Delta, 0.f, .1f, .1f, 1, false);
	if (Delta > 0.f)
		for (FShadowAssociation& Entry : ShadowAssociations)
		{
			UStaticMeshComponent* Component = ComponentFor(Entry.Actor);
			if (Component && Component->GetWorld() == &World)
				if (FBodyInstance* Body = Identity(Component)) PrepareShadow(*Body, Entry.State, Delta);
		}
	return Delta;
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
	Component->BodyInstance.bGenerateWakeEvents = true;
	Proxy->OnComponentWake.AddUniqueDynamic(Proxy, &UElysiumPhysicsPropComponent::NativeWake);
	Proxy->OnComponentSleep.AddUniqueDynamic(Proxy, &UElysiumPhysicsPropComponent::NativeSleep);
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
	NativeAssociations.Add({ ElysiumModelPhysics::ActorIdentity(Component), Component, Component, ++NextNativeIncarnation });
	if (!Recipe.bStatic && !bStartAsleep) ElysiumModelPhysics::RecordNativeActivity(Component, true);
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
	if (!Component || !ValidShadow(State.Shadow) || (State.Shadow.bInstalled && State.Recipe.bStatic)
		|| (State.bShadowMember && !State.Shadow.bInstalled)
		|| State.Pose.ContainsNaN() || State.LinearVelocity.ContainsNaN() || State.AngularVelocityRadians.ContainsNaN()
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
	if (State.Shadow.bInstalled)
	{
		ShadowAssociations.Add({ ActorIdentity(Component), State.Shadow });
		if (!State.Shadow.bAllowRotation) SetInertia(*Body, FVector::ZeroVector);
	}
	if (!State.Recipe.bStatic)
	{
		Body->SetLinearVelocity(State.LinearVelocity, false);
		Body->SetAngularVelocityInRadians(State.AngularVelocityRadians, false);
		if (State.bAsleep) Body->PutInstanceToSleep();
		else Body->WakeInstance();
	}
	return true; // 100443e0 re-registers only the actual successful reconstructed object.
}
