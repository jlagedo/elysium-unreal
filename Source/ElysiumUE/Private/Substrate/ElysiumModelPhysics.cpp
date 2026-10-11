#include "Substrate/ElysiumModelPhysics.h"
#include "ElysiumModelPhysicsInputs.h"
#include "ElysiumPhysicalMaterial.h"
#include "ElysiumPhysicsSurfaceProps.h"
#include "ElysiumPhysicsPropComponent.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumWorldCollisionActor.h"
#include "EngineUtils.h"
#include "Chaos/ContactModification.h"
#include "Chaos/SimCallbackObject.h"
#include "Chaos/Collision/CollisionConstraintAllocator.h"
#include "PBDRigidsSolver.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
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
		UPrimitiveComponent* Producer = nullptr; // equality only, valid also during component destruction
		TWeakObjectPtr<UPrimitiveComponent> Component;
		FElysiumPhysicsBodyRecipe Recipe;
	};
	// Semantic manager#2/model and manager#1/bbox associations (10044db0/10044f60).
	// No Source hash/tree layout; identity is the actual successful Chaos incarnation.
	TArray<FPhysicsRecipeAssociation> PhysicsRecipeAssociations;
	struct FNativeAssociation
	{
		void* Actor; TWeakObjectPtr<UPrimitiveComponent> Component; UPrimitiveComponent* Producer;
		uint64 Incarnation; FElysiumEntityWorld* World = nullptr; FElysiumEntityHandle Owner;
		bool bCollectNativeOutput = false;
		bool bPendingInactive = false;
	};
	TArray<FNativeAssociation> NativeAssociations;
	struct FWorldCreationAssociation
	{
		void* Actor; Chaos::FUniqueIdx UniqueIdx; TWeakObjectPtr<UElysiumWorldCollisionComponent> Component;
		UElysiumWorldCollisionComponent* Producer; FElysiumEntityWorld* World; FElysiumEntityHandle Owner;
		uint64 Generation;
	};
	TArray<FWorldCreationAssociation> WorldCreationAssociations; //object6 only, never active226
	uint64 NextWorldGeneration = 0;
	void RetireWorldContact(void* Actor);
	void ForgetCompletedWorld(UWorld* World);
	void ForgetShadowWorld(UWorld* World);
	struct FSolveFilterIdentity
	{
		const void* Proxy; Chaos::FUniqueIdx UniqueIdx; bool bWorld; bool bCommonSolid; bool bBaseMask; uint64 Generation;
	};
	struct FShadowEventInput
	{
		const void* Proxy; Chaos::FUniqueIdx UniqueIdx; uint64 Generation; uint64 Revision; FElysiumPhysicsShadowState State;
	};
	void SimulateShadow(Chaos::FRigidBodyHandle_Internal& Particle, FElysiumPhysicsShadowState& State,
		Chaos::FSingleParticlePhysicsProxy& Proxy, Chaos::FPBDRigidsSolver& NativeSolver, double EventDelta, ElysiumModelPhysics::FShadowEventSample& Sample);
	struct FSolveFilterInput : Chaos::FSimCallbackInput
	{
		TArray<FSolveFilterIdentity> Identities;
		TArray<FShadowEventInput> Shadows;
		bool bObservePostSolve = false;
		void Reset() { Identities.Reset(); Shadows.Reset(); bObservePostSolve = false; }
	};
	double FrictionContactSkinCm();
	struct FNativeContactOutput
	{
		const void* Shapes[2];
		const void* Geometry[2];
		const void* Proxies[2];
		Chaos::FUniqueIdx UniqueIds[2];
		uint64 Generations[2];
		bool bInitial;
		double ObservedSeparation = TNumericLimits<double>::Max();
		FVector ObservedPose[2] = {FVector::ZeroVector,FVector::ZeroVector};
	};
	struct FSolveFilterOutput : Chaos::FSimCallbackOutput
	{
		TArray<FNativeContactOutput> Contacts;
		struct FShadowSample { const void* Proxy; Chaos::FUniqueIdx UniqueIdx; uint64 Generation; uint64 Revision;
			FElysiumPhysicsShadowState State; ElysiumModelPhysics::FShadowEventSample Sample; };
		TArray<FShadowSample> Shadows;
		void Reset() { Contacts.Reset(); Shadows.Reset(); }
	};
	class FSolveFilterCallback final : public Chaos::TSimCallbackObject<FSolveFilterInput,
		FSolveFilterOutput, Chaos::ESimCallbackOptions::ContactModification | Chaos::ESimCallbackOptions::Presimulate | Chaos::ESimCallbackOptions::PostSolve>
	{
		TArray<FShadowEventInput> Controllers; //PT-owned countdown/prediction, never a mutable GT association
		TSharedRef<TMap<uint64,uint64>, ESPMode::ThreadSafe> RetiredControllers = MakeShared<TMap<uint64,uint64>, ESPMode::ThreadSafe>();
	public:
		void RetireShadow_External(uint64 Generation, uint64 Revision)
		{
			// The command owns its fence independently of callback destruction/world cleanup.
			GetSolver()->EnqueueCommandImmediate([Fence = RetiredControllers,Generation,Revision]()
			{ auto& Prior = Fence->FindOrAdd(Generation); Prior = FMath::Max(Prior,Revision); });
		}
	private:
		virtual FName GetFNameForStatId() const override { return FName(TEXT("ElysiumContentsPairFilter")); }
		virtual void OnPreSimulate_Internal() override
		{
			const auto* Input = GetConsumerInput_Internal(); if (!Input) return;
			auto* NativeSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
			Controllers.RemoveAll([&](const auto& Controller)
			{ return !Input->Shadows.ContainsByPredicate([&](const auto& Entry)
				{ return Entry.Proxy == Controller.Proxy && Entry.UniqueIdx == Controller.UniqueIdx && Entry.Generation == Controller.Generation; }); });
			for (const FShadowEventInput& Entry : Input->Shadows)
			{
				if (Entry.Revision <= RetiredControllers->FindRef(Entry.Generation)) continue;
				// Resolve current PT identity first; never dereference a marshaled proxy pointer.
				auto* Proxy = NativeSolver->GetParticleProxy_PT(Entry.UniqueIdx);
				if (!Proxy || Proxy != Entry.Proxy) continue;
				auto* Particle = Proxy->GetPhysicsThreadAPI(); if (!Particle) continue;
				auto* Controller = Controllers.FindByPredicate([&](const auto& Item)
				{ return Item.Proxy == Entry.Proxy && Item.UniqueIdx == Entry.UniqueIdx && Item.Generation == Entry.Generation; });
				if (!Controller) Controller = &Controllers.Add_GetRef(Entry);
				else if (Controller->Revision != Entry.Revision) *Controller = Entry;
				ElysiumModelPhysics::FShadowEventSample Sample;
				Sample.Time = GetSimTime_Internal(); Sample.Delta = GetDeltaTime_Internal();
				Sample.bSleeping = Particle->ObjectState() == Chaos::EObjectStateType::Sleeping;
				Sample.RemainingBefore = Controller->State.RemainingSeconds; Sample.Position = Particle->X(); Sample.Velocity = Particle->V();
				Sample.PredictionBefore = Controller->State.PredictedPosition; Sample.Target = Controller->State.Target.GetLocation();
				if (Particle->ObjectState() == Chaos::EObjectStateType::Dynamic)
					SimulateShadow(*Particle,Controller->State,*Proxy,*NativeSolver,GetDeltaTime_Internal(),Sample);
				Sample.Remaining = Controller->State.RemainingSeconds; Sample.CorrectedVelocity = Particle->V();
				GetProducerOutputData_Internal().Shadows.Add({Proxy,Entry.UniqueIdx,Entry.Generation,Entry.Revision,Controller->State,Sample});
			}
		}
		virtual void OnPostSolve_Internal() override
		{
			const auto* Input = GetConsumerInput_Internal(); if (!Input || !Input->bObservePostSolve) return;
			auto* NativeSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
			auto& Output = GetProducerOutputData_Internal();
			for (const auto& Controller : Controllers)
			{
				if (Controller.Revision <= RetiredControllers->FindRef(Controller.Generation)) continue;
				auto* Proxy = NativeSolver->GetParticleProxy_PT(Controller.UniqueIdx);
				if (!Proxy || Proxy != Controller.Proxy) continue;
				const auto* Particle = Proxy->GetPhysicsThreadAPI(); if (!Particle) continue;
				const auto* Rigid = Proxy->GetHandle_LowLevel()->CastToRigidParticle(); if (!Rigid) continue;
				const int32 EventIndex = Output.Shadows.FindLastByPredicate([&](const auto& Item)
				{ return Item.Proxy == Proxy && Item.UniqueIdx == Controller.UniqueIdx && Item.Generation == Controller.Generation && Item.Revision == Controller.Revision; });
				if (EventIndex == INDEX_NONE) continue;
				auto& Observation = Output.Shadows[EventIndex].Sample.PostSolve.AddDefaulted_GetRef();
				Observation.StepTime = GetSimTime_Internal(); Observation.StepDelta = GetDeltaTime_Internal();
				Observation.Position = Particle->X(); Observation.Rotation = Particle->R();
				Observation.SolvedPosition = Rigid->GetP(); Observation.SolvedRotation = Rigid->GetQ();
				Observation.Velocity = Particle->V(); Observation.AngularVelocity = Particle->W();
				Rigid->ParticleCollisions().VisitConstCollisions([&](const Chaos::FPBDCollisionConstraint& Contact)
				{
					const auto* Partner = Contact.GetParticle0() == Rigid ? Contact.GetParticle1() : Contact.GetParticle0();
					if (!Partner) return Chaos::ECollisionVisitorResult::Continue;
					auto& Evidence = Observation.Contacts.AddDefaulted_GetRef();
					Evidence.Partner = Partner->PhysicsProxy(); Evidence.PartnerUniqueIdx = Partner->UniqueIdx().Idx;
					if (const auto* Identity = Input->Identities.FindByPredicate([&](const auto& Item)
						{ return Item.Proxy == Evidence.Partner && Item.UniqueIdx == Partner->UniqueIdx(); })) Evidence.PartnerGeneration = Identity->Generation;
					Evidence.bDisabled = Contact.GetDisabled(); Evidence.bProbe = Contact.GetIsProbe();
					const Chaos::FRigidTransform3 CurrentShapes[2] = {
						Contact.GetShapeRelativeTransform0() * (Contact.GetParticle0()->CastToRigidParticle() ? Contact.GetParticle0()->CastToRigidParticle()->GetTransformPQ() : Contact.GetParticle0()->GetTransformXR()),
						Contact.GetShapeRelativeTransform1() * (Contact.GetParticle1()->CastToRigidParticle() ? Contact.GetParticle1()->CastToRigidParticle()->GetTransformPQ() : Contact.GetParticle1()->GetTransformXR())};
					for (int32 Side = 0; Side < 2; ++Side)
					{
						const auto* Geometry = Contact.GetImplicit(Side);
						Evidence.bGeometry[Side] = Geometry != nullptr;
						if (Geometry)
						{
							Evidence.GeometryType[Side] = static_cast<int32>(Geometry->GetType()); Evidence.GeometryMargin[Side] = Geometry->GetMarginf();
							Evidence.bBounds[Side] = Geometry->HasBoundingBox();
							if (Evidence.bBounds[Side]) { Evidence.BoundsMin[Side] = Geometry->BoundingBox().Min(); Evidence.BoundsMax[Side] = Geometry->BoundingBox().Max(); }
						}
						Evidence.StoredPose[Side] = Contact.GetShapeWorldTransform(Side).GetLocation();
						Evidence.SolvedPose[Side] = CurrentShapes[Side].GetLocation(); Evidence.SolvedRotation[Side] = CurrentShapes[Side].GetRotation();
					}
					Evidence.CollisionMargin[0] = Contact.GetCollisionMargin0(); Evidence.CollisionMargin[1] = Contact.GetCollisionMargin1();
					for (int32 PointIndex = 0; PointIndex < Contact.NumManifoldPoints(); ++PointIndex)
						if (Contact.IsManifoldPointActive(PointIndex) && Contact.GetManifoldPoint(PointIndex).ContactPoint.IsSet())
						{
							++Evidence.ActivePoints;
							const auto& Point = Contact.GetManifoldPoint(PointIndex);
							Evidence.Phi = FMath::Min(Evidence.Phi,static_cast<double>(Point.ContactPoint.Phi));
							auto& PointEvidence = Evidence.Points.AddDefaulted_GetRef();
							PointEvidence.Phi = Point.ContactPoint.Phi; PointEvidence.Flags = Point.Flags.Bits;
							const Chaos::FVec3 StoredNormal = Contact.GetShapeWorldTransform1().TransformVectorNoScale(Chaos::FVec3(Point.ContactPoint.ShapeContactNormal));
							PointEvidence.Normal = CurrentShapes[1].TransformVectorNoScale(Chaos::FVec3(Point.ContactPoint.ShapeContactNormal));
							const Chaos::FVec3 Stored0 = Contact.GetShapeWorldTransform0().TransformPositionNoScale(Chaos::FVec3(Point.ContactPoint.ShapeContactPoints[0]));
							const Chaos::FVec3 Stored1 = Contact.GetShapeWorldTransform1().TransformPositionNoScale(Chaos::FVec3(Point.ContactPoint.ShapeContactPoints[1]));
							const Chaos::FVec3 Current0 = CurrentShapes[0].TransformPositionNoScale(Chaos::FVec3(Point.ContactPoint.ShapeContactPoints[0]));
							const Chaos::FVec3 Current1 = CurrentShapes[1].TransformPositionNoScale(Chaos::FVec3(Point.ContactPoint.ShapeContactPoints[1]));
							PointEvidence.StoredSeparation = Chaos::FVec3::DotProduct(Stored0-Stored1,StoredNormal);
							PointEvidence.SolvedSeparation = Chaos::FVec3::DotProduct(Current0-Current1,Chaos::FVec3(PointEvidence.Normal));
							Chaos::FVec3 SurfaceNormal;
							for (int32 Side = 0; Side < 2; ++Side) if (const auto* Geometry = Contact.GetImplicit(Side))
							{
								PointEvidence.bSurfaceSample[Side] = true;
								PointEvidence.SurfacePhi[Side] = Geometry->PhiWithNormal(CurrentShapes[Side].InverseTransformPositionNoScale(Side == 0 ? Current1 : Current0),SurfaceNormal);
								PointEvidence.OwnSurfacePhi[Side] = Geometry->PhiWithNormal(Chaos::FVec3(Point.ContactPoint.ShapeContactPoints[Side]),SurfaceNormal);
							}
						}
					Evidence.bQualified = !Evidence.bDisabled && !Evidence.bProbe && Evidence.ActivePoints > 0
						&& FMath::Max(Evidence.Phi,0.) < FrictionContactSkinCm();
					Evidence.bModifierObserved = Output.Contacts.ContainsByPredicate([&](const auto& Item)
					{ return Item.Shapes[0] == Contact.GetShape0() && Item.Shapes[1] == Contact.GetShape1()
						&& Item.Geometry[0] == Contact.GetImplicit0() && Item.Geometry[1] == Contact.GetImplicit1()
						&& Item.Proxies[0] == Contact.GetParticle0()->PhysicsProxy() && Item.Proxies[1] == Contact.GetParticle1()->PhysicsProxy()
						&& Item.UniqueIds[0] == Contact.GetParticle0()->UniqueIdx() && Item.UniqueIds[1] == Contact.GetParticle1()->UniqueIdx(); });
					if (const auto* Modified = Output.Contacts.FindByPredicate([&](const auto& Item)
						{ return Item.Shapes[0] == Contact.GetShape0() && Item.Shapes[1] == Contact.GetShape1()
							&& Item.Geometry[0] == Contact.GetImplicit0() && Item.Geometry[1] == Contact.GetImplicit1()
							&& Item.Proxies[0] == Contact.GetParticle0()->PhysicsProxy() && Item.Proxies[1] == Contact.GetParticle1()->PhysicsProxy()
							&& Item.UniqueIds[0] == Contact.GetParticle0()->UniqueIdx() && Item.UniqueIds[1] == Contact.GetParticle1()->UniqueIdx(); }))
					{
						Evidence.bInitial = Modified->bInitial; Evidence.ModifierSeparation = Modified->ObservedSeparation;
						Evidence.ModifierPose[0] = Modified->ObservedPose[0]; Evidence.ModifierPose[1] = Modified->ObservedPose[1];
					}
					return Chaos::ECollisionVisitorResult::Continue;
				});
			}
		}
		virtual void OnContactModification_Internal(Chaos::FCollisionContactModifier& Modifier) override
		{
			const FSolveFilterInput* Input = GetConsumerInput_Internal();
			if (!Input) return;
			FSolveFilterOutput& Output = GetProducerOutputData_Internal();
			for (Chaos::FContactPairModifier& Pair : Modifier)
			{
				const auto Particles = Pair.GetParticlePair();
				const FSolveFilterIdentity* Identities[2] = { nullptr, nullptr };
				for (int32 Side = 0; Side < 2; ++Side)
					if (Particles[Side])
						Identities[Side] = Input->Identities.FindByPredicate([&](const FSolveFilterIdentity& Entry)
						{ return Entry.Proxy == Particles[Side]->PhysicsProxy() && Entry.UniqueIdx == Particles[Side]->UniqueIdx(); });
				bool bRejected = false;
				if (Identities[0] && Identities[1]) for (int32 Side = 0; Side < 2; ++Side)
					if (Identities[Side]->bWorld && !Identities[Side]->bCommonSolid && !Identities[1 - Side]->bWorld
						&& Identities[1 - Side]->bBaseMask)
					{ Pair.Disable(); bRejected = true; break; } //1002f603..63f: contents & OTHER237, before solve/44
				if (bRejected || !((Identities[0] && !Identities[0]->bWorld) || (Identities[1] && !Identities[1]->bWorld))) continue;
				const auto* First = Particles[0]; const auto* Second = Particles[1];
				if (!First || !Second) continue;
				Output.Contacts.Add({{Pair.GetShape(0),Pair.GetShape(1)}, {Pair.GetContactGeometry(0),Pair.GetContactGeometry(1)},
					{First->PhysicsProxy(),Second->PhysicsProxy()}, {First->UniqueIdx(),Second->UniqueIdx()},
					{Identities[0] ? Identities[0]->Generation : 0, Identities[1] ? Identities[1]->Generation : 0}, Pair.GetIsInitialContact()});
				if (Input->bObservePostSolve)
				{
					auto& Observation = Output.Contacts.Last();
					Observation.ObservedPose[0] = Pair.GetShapeToWorld(0).GetLocation(); Observation.ObservedPose[1] = Pair.GetShapeToWorld(1).GetLocation();
					for (int32 PointIndex = 0; PointIndex < Pair.GetNumContacts(); ++PointIndex)
						Observation.ObservedSeparation = FMath::Min(Observation.ObservedSeparation,static_cast<double>(Pair.GetSeparation(PointIndex)));
				}
			}
		}
	};
	struct FSolveFilterRegistration { Chaos::FPBDRigidsSolver* Solver; FSolveFilterCallback* Callback; bool bAdmitted = false; };
	TMap<UWorld*, FSolveFilterRegistration> SolveFilters;
	TSet<UWorld*> PostSolveObservationWorlds; //GT diagnostic opt-in; never accessed on PT
	void CleanupSolveFilter(UWorld* World, bool, bool)
	{
		if (const FSolveFilterRegistration* Registration = SolveFilters.Find(World))
			Registration->Solver->UnregisterAndFreeSimCallbackObject_External(Registration->Callback);
		SolveFilters.Remove(World);
		PostSolveObservationWorlds.Remove(World);
		ForgetCompletedWorld(World);
		ForgetShadowWorld(World); //callback destructor runs on PT and never touches UObjects
		WorldCreationAssociations.RemoveAll([World](const FWorldCreationAssociation& Entry)
		{
			const bool bRemove = !Entry.Component.IsValid() || Entry.Component->GetWorld() == World;
			if (bRemove) RetireWorldContact(Entry.Actor);
			return bRemove;
		});
	}
	void EnsureSolveFilterCleanup()
	{
		static const FDelegateHandle CleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&CleanupSolveFilter);
		(void)CleanupHandle;
	}
	void PrepareSolveFilter(UWorld& World, FPhysScene& Scene)
	{
		EnsureSolveFilterCleanup();
		auto* Solver = Scene.GetSolver(); if (!Solver) return;
		FSolveFilterRegistration* Registration = SolveFilters.Find(&World);
		if (!Registration)
			Registration = &SolveFilters.Add(&World, { Solver, Solver->CreateAndRegisterSimCallbackObject_External<FSolveFilterCallback>() });
		FSolveFilterInput* Input = Registration->Callback->GetProducerInputData_External();
		Input->bObservePostSolve = PostSolveObservationWorlds.Contains(&World);
		Input->Identities.Reset();
		for (const FNativeAssociation& Entry : NativeAssociations)
			if (UPrimitiveComponent* Component = Entry.Component.Get(); Component && Component->GetWorld() == &World
				&& ElysiumModelPhysics::ActorIdentity(Component) == Entry.Actor)
				if (FElysiumEntity* Owner = Entry.World ? Entry.World->Resolve(Entry.Owner) : nullptr)
				{
					ElysiumModelPhysics::Identity(Component)->SetContactModification(true);
					const auto* Proxy = static_cast<Chaos::FSingleParticlePhysicsProxy*>(Entry.Actor);
					Input->Identities.Add({ Proxy, Proxy->GetGameThreadAPI().UniqueIdx(), false, false,
						ElysiumModelPhysics::SolidMask(Owner->Flags, Owner->AsPlayer() != nullptr) == 0x200400b, Entry.Incarnation });
				}
		for (TActorIterator<AElysiumWorldCollisionActor> It(&World); It; ++It)
		{
			TArray<UElysiumWorldCollisionComponent*> Components;
			for (UElysiumWorldCollisionComponent* Component : It->Bodies) Components.Add(Component);
			if (It->Displacement) Components.Add(It->Displacement);
			for (UElysiumWorldCollisionComponent* Component : Components)
				if (auto* Proxy = ElysiumModelPhysics::ActorIdentity(Component))
					{
						const auto* Association = WorldCreationAssociations.FindByPredicate([Component, Proxy](const FWorldCreationAssociation& Entry)
						{ return Entry.Producer == Component && Entry.Actor == Proxy; });
						Input->Identities.Add({ Proxy, Proxy->GetGameThreadAPI().UniqueIdx(), true, Component->bCommonSolid, false,
							Association ? Association->Generation : 0 });
					}
		}
	}
	// Chaos can retain cached constraints until its next maintenance step after proxy removal.
	// A retired producer is distinct from a surviving world contact with null GameData.
	TSet<void*> RetiredNativeActors;
	struct FObservedContact
	{
		const void* Constraint;
		void* Partner;
		uint64 PartnerIncarnation;
		uint64 Birth;
	};
	struct FCompletedContactBirth
	{
		UWorld* World; FNativeContactOutput Contact; uint64 Sequence; uint64 Birth = 0;
		bool bQualified = false; bool bInitialTransition = false;
		double OutputTime = -1.; double BirthTime = -1.;
	};
	TArray<FCompletedContactBirth> CompletedContactBirths;
	void ForgetCompletedWorld(UWorld* World)
	{ CompletedContactBirths.RemoveAll([World](const FCompletedContactBirth& Entry){ return Entry.World == World; }); }
	void ForgetCompletedActor(void* Actor)
	{ CompletedContactBirths.RemoveAll([Actor](const FCompletedContactBirth& Entry){ return Entry.Contact.Proxies[0] == Actor || Entry.Contact.Proxies[1] == Actor; }); }
	TMap<uint64, TArray<FObservedContact>> ContactRosters;
	void RetireWorldContact(void* Actor)
	{
		if (!Actor) return;
		RetiredNativeActors.Add(Actor);
		ForgetCompletedActor(Actor);
		for (auto& Roster : ContactRosters)
			Roster.Value.RemoveAll([Actor](const FObservedContact& Contact) { return Contact.Partner == Actor; });
	}
	uint64 NextContactBirth = 0;
	void ConsumeShadowOutputs(UWorld* World, const FSolveFilterOutput& Output);
	void ConsumeContactOutputs(UWorld* World)
	{
		const FSolveFilterRegistration* Registration = SolveFilters.Find(World);
		if (!Registration) return;
		while (auto Output = Registration->Callback->PopOutputData_External())
		{
			ConsumeShadowOutputs(World, *Output);
			for (const FNativeContactOutput& Contact : Output->Contacts)
			{
				if (RetiredNativeActors.Contains(const_cast<void*>(Contact.Proxies[0])) || RetiredNativeActors.Contains(const_cast<void*>(Contact.Proxies[1]))) continue;
				bool bCurrent = true;
				for (int32 Side = 0; Side < 2 && bCurrent; ++Side)
				{
					if (!Contact.Generations[Side]) continue; //unowned native partner: validated against completed constraint before44
					const auto* Native = NativeAssociations.FindByPredicate([&](const FNativeAssociation& Entry)
					{ return Entry.Actor == Contact.Proxies[Side] && Entry.Incarnation == Contact.Generations[Side]; });
					const auto* StaticWorld = WorldCreationAssociations.FindByPredicate([&](const FWorldCreationAssociation& Entry)
					{ return Entry.Actor == Contact.Proxies[Side] && Entry.Generation == Contact.Generations[Side]; });
					UPrimitiveComponent* Component = Native ? Native->Component.Get() : StaticWorld ? StaticWorld->Component.Get() : nullptr;
					auto* Proxy = ElysiumModelPhysics::ActorIdentity(Component);
					FElysiumEntity* CreationOwner = Native ? (Native->World ? Native->World->Resolve(Native->Owner) : nullptr)
						: StaticWorld ? StaticWorld->World->Resolve(StaticWorld->Owner) : nullptr;
					bCurrent = Proxy && Proxy == Contact.Proxies[Side] && Proxy->GetGameThreadAPI().UniqueIdx() == Contact.UniqueIds[Side]
						&& CreationOwner != nullptr;
				}
				if (!bCurrent) continue; //pooled output from a retired proxy/owner epoch cannot repopulate its contacts
				FCompletedContactBirth* Prior = CompletedContactBirths.FindByPredicate([&](const FCompletedContactBirth& Entry)
				{ return Entry.World == World && Entry.Contact.Shapes[0] == Contact.Shapes[0] && Entry.Contact.Shapes[1] == Contact.Shapes[1]
					&& Entry.Contact.Geometry[0] == Contact.Geometry[0] && Entry.Contact.Geometry[1] == Contact.Geometry[1]
					&& Entry.Contact.Proxies[0] == Contact.Proxies[0] && Entry.Contact.Proxies[1] == Contact.Proxies[1]
					&& Entry.Contact.UniqueIds[0] == Contact.UniqueIds[0] && Entry.Contact.UniqueIds[1] == Contact.UniqueIds[1]; });
				if (!Prior)
				{
					const int32 NewIndex = CompletedContactBirths.Add({World,Contact,++NextContactBirth});
					CompletedContactBirths[NewIndex].OutputTime = Output->InternalTime;
				}
				else
				{
					Prior->bInitialTransition |= Contact.bInitial && !Prior->Contact.bInitial;
					Prior->Sequence = ++NextContactBirth;
					Prior->Contact = Contact;
					Prior->OutputTime = Output->InternalTime;
				}
			}
		}
	}
	// Environment2600f920 ->2603a730 ->260477d0/26050c20/2603e880.
	// The authored default is stored through float108 before its final float11c store.
	// Native geometry supplies surface separation; this threshold only qualifies44's roster.
	double FrictionContactSkinCm()
	{
		constexpr double DefaultToleranceMetres = 0.00634745988029331;
		const float FirstStore = static_cast<float>(DefaultToleranceMetres * (0.10000000149011612 + 0.8999999761581421));
		const float SkinMetres = static_cast<float>(static_cast<double>(FirstStore) + DefaultToleranceMetres + 2.5 * DefaultToleranceMetres);
		return static_cast<double>(SkinMetres) * 100.;
	}
	uint64 NextNativeIncarnation = 0;
	TArray<uint64> NativeSolveOutputs;
	TMap<UWorld*, TArray<uint64>> NativeActiveOrders; // each native environment has its observable order
	struct FNativeReference { FElysiumEntity* Entity; void* Actor; };
	TArray<FNativeReference> NativeReferences;
	uint64 NextShadowRevision = 0;
	struct FShadowAssociation { void* Actor; FElysiumPhysicsShadowState State; uint64 Revision = ++NextShadowRevision; };
	TArray<FShadowAssociation> ShadowAssociations;
	TArray<FSolveFilterOutput::FShadowSample> CompletedShadowEvents;
	void ForgetShadowWorld(UWorld* World)
	{
		CompletedShadowEvents.RemoveAll([World](const auto& Event)
		{ UPrimitiveComponent* Component = ElysiumModelPhysics::ComponentFor(const_cast<void*>(Event.Proxy)); return !Component || Component->GetWorld() == World; });
	}
	void ConsumeShadowOutputs(UWorld* World, const FSolveFilterOutput& Output)
	{
		for (const auto& Event : Output.Shadows)
		{
			const auto* Native = NativeAssociations.FindByPredicate([&](const FNativeAssociation& Entry)
			{ return Entry.Actor == Event.Proxy && Entry.Incarnation == Event.Generation; });
			UPrimitiveComponent* Component = Native ? Native->Component.Get() : nullptr;
			auto* Proxy = ElysiumModelPhysics::ActorIdentity(Component);
			if (Component && Component->GetWorld() == World && Proxy == Event.Proxy && Proxy->GetGameThreadAPI().UniqueIdx() == Event.UniqueIdx)
				if (auto* Shadow = ShadowAssociations.FindByPredicate([&](const auto& Entry) { return Entry.Actor == Proxy && Entry.Revision == Event.Revision; }))
				{ Shadow->State = Event.State; CompletedShadowEvents.Add(Event); }
		}
	}
	FShadowAssociation* FindShadow(void* Actor)
	{
		return ShadowAssociations.FindByPredicate([Actor](const FShadowAssociation& Entry) { return Entry.Actor == Actor; });
	}
	void RefreshShadowInput(const FShadowAssociation& Entry)
	{
		UPrimitiveComponent* Component = ElysiumModelPhysics::ComponentFor(Entry.Actor); if (!Component) return;
		const auto* Registration = SolveFilters.Find(Component->GetWorld()); if (!Registration || !Registration->bAdmitted) return;
		auto* Proxy = ElysiumModelPhysics::ActorIdentity(Component); if (!Proxy) return;
		auto& Shadows = Registration->Callback->GetProducerInputData_External()->Shadows;
		FShadowEventInput Value {Proxy,Proxy->GetGameThreadAPI().UniqueIdx(),ElysiumModelPhysics::CaptureIdentity(Entry.Actor).Incarnation,Entry.Revision,Entry.State};
		if (auto* Submitted = Shadows.FindByPredicate([&](const auto& Item) { return Item.Proxy == Proxy; })) *Submitted = Value;
		else Shadows.Add(Value);
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
	bool CreatePhysicsRecipe(UPrimitiveComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
		const FElysiumPhysicsBodyRecipe& Recipe, bool bStartAsleep, FString& OutError);
}

FArchive& operator<<(FArchive& Ar, FElysiumSavedPhysicsBody& State)
{
	uint8 Kind = static_cast<uint8>(State.Recipe.Kind);
	Ar << State.EntityIndex << State.FieldName << Kind;
	if (Kind > static_cast<uint8>(EElysiumPhysicsRecipeKind::Brush)) Ar.SetError();
	State.Recipe.Kind = static_cast<EElysiumPhysicsRecipeKind>(Kind);
	Ar << State.bHasObject;
	Ar << State.Recipe.bStatic << State.Recipe.ModelAsset << State.Recipe.Surface << State.Recipe.BboxMin << State.Recipe.BboxMax;
	Ar << State.Recipe.BrushSolidity << State.Recipe.BrushContentsSignature;
	int32 HullCount = State.Recipe.BrushHulls.Num(); Ar << HullCount;
	if (HullCount < 0 || HullCount > 65536) { Ar.SetError(); return Ar; }
	if (Ar.IsLoading()) State.Recipe.BrushHulls.SetNum(HullCount);
	for (TArray<FVector>& Hull : State.Recipe.BrushHulls)
	{
		int32 VertexCount = Hull.Num(); Ar << VertexCount;
		if (VertexCount < 4 || VertexCount > 65536) { Ar.SetError(); return Ar; }
		if (Ar.IsLoading()) Hull.SetNum(VertexCount);
		for (FVector& Vertex : Hull) Ar << Vertex;
	}
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

void ElysiumModelPhysics::RetireRecipe(UPrimitiveComponent* Component)
{
	// 10045110: first matching model association, otherwise first bbox; sleeping is not removal.
	for (EElysiumPhysicsRecipeKind Kind : { EElysiumPhysicsRecipeKind::Model, EElysiumPhysicsRecipeKind::Bbox, EElysiumPhysicsRecipeKind::Brush })
	{
		const int32 Found = PhysicsRecipeAssociations.IndexOfByPredicate([Component, Kind](const FPhysicsRecipeAssociation& Entry)
			{ return Entry.Producer == Component && Entry.Recipe.Kind == Kind; });
		if (Found != INDEX_NONE) { PhysicsRecipeAssociations.RemoveAt(Found); return; }
	}
}

UPrimitiveComponent* ElysiumModelPhysics::ComponentFor(void* Actor)
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
	WorldCreationAssociations.RemoveAll([&](const FWorldCreationAssociation& Entry)
	{
		const bool bRemove = Entry.World == Entity.World && Entry.Owner == Entity.Handle;
		if (bRemove) RetireWorldContact(Entry.Actor);
		return bRemove;
	});
	for (FNativeAssociation& Entry : NativeAssociations)
		if (Entry.World == Entity.World && Entry.Owner == Entity.Handle)
		{ Entry.World = nullptr; Entry.Owner = FElysiumEntityHandle::Invalid(); }
}

void ElysiumModelPhysics::RecordNativeOutput(UPrimitiveComponent* Component)
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

void ElysiumModelPhysics::RecordNativeActivity(UPrimitiveComponent* Component, bool bAwake)
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

void ElysiumModelPhysics::Wake(UPrimitiveComponent* Component)
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

ElysiumModelPhysics::FNativeOutput ElysiumModelPhysics::CaptureIdentity(void* Actor)
{
	const FNativeAssociation* Entry = NativeAssociations.FindByPredicate([Actor](const FNativeAssociation& Item) { return Item.Actor == Actor; });
	return { Actor, Entry ? Entry->Incarnation : 0 };
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

FElysiumEntity* ElysiumModelPhysics::GameDataForObject(void* Actor)
{
	if (!Actor) return nullptr;
	for (const FNativeAssociation& Entry : NativeAssociations)
		if (Entry.Actor == Actor && Entry.World && ComponentFor(Actor)) return Entry.World->Resolve(Entry.Owner);
	for (const FWorldCreationAssociation& Entry : WorldCreationAssociations)
		if (Entry.Actor == Actor && Entry.Component.IsValid() && ActorIdentity(Entry.Component.Get()) == Actor
			&& static_cast<Chaos::FSingleParticlePhysicsProxy*>(Actor)->GetGameThreadAPI().UniqueIdx() == Entry.UniqueIdx)
			return Entry.World->Resolve(Entry.Owner);
	return nullptr;
}

void ElysiumModelPhysics::BindWorldCreationOwners(FElysiumEntityWorld& World)
{
	WorldCreationAssociations.RemoveAll([&](const FWorldCreationAssociation& Entry)
	{
		if (Entry.World != &World) return false;
		RetireWorldContact(Entry.Actor); return true;
	});
	FElysiumEntity* WorldEntity = nullptr;
	for (const auto& Entity : World.Entities())
		if (Entity && Entity->Def && Entity->Def->Classname.Equals(TEXT("worldspawn"), ESearchCase::IgnoreCase))
		{ WorldEntity = Entity.Get(); break; }
	UWorld* NativeWorld = World.GetOwnerActor() ? World.GetOwnerActor()->GetWorld() : nullptr;
	if (!WorldEntity || !NativeWorld) return;
	for (TActorIterator<AElysiumWorldCollisionActor> It(NativeWorld); It; ++It)
	{
		TArray<UElysiumWorldCollisionComponent*> Components;
		for (UElysiumWorldCollisionComponent* Component : It->Bodies) Components.Add(Component);
		if (It->Displacement) Components.Add(It->Displacement);
		for (UElysiumWorldCollisionComponent* Component : Components)
			if (Component)
			{
				auto* Proxy = ActorIdentity(Component);
				WorldCreationAssociations.Add({ Proxy, Proxy ? Proxy->GetGameThreadAPI().UniqueIdx() : Chaos::FUniqueIdx(), Component, Component, &World, WorldEntity->Handle, ++NextWorldGeneration });
				if (Proxy) RetiredNativeActors.Remove(Proxy);
			}
	}
}

void ElysiumModelPhysics::RetireWorldActor(UElysiumWorldCollisionComponent* Component)
{
	for (FWorldCreationAssociation& Entry : WorldCreationAssociations)
		if (Entry.Producer == Component && Entry.Actor)
		{
			RetireWorldContact(Entry.Actor);
			Entry.Actor = nullptr; Entry.UniqueIdx = Chaos::FUniqueIdx(); //component/owner survive native recreation
		}
}

void ElysiumModelPhysics::RebindWorldActor(UElysiumWorldCollisionComponent* Component)
{
	if (auto* Proxy = ActorIdentity(Component))
		for (FWorldCreationAssociation& Entry : WorldCreationAssociations)
			if (Entry.Producer == Component)
			{
				Entry.Actor = Proxy; Entry.UniqueIdx = Proxy->GetGameThreadAPI().UniqueIdx(); Entry.Generation = ++NextWorldGeneration;
				RetiredNativeActors.Remove(Proxy);
			}
}

UPrimitiveComponent* ElysiumModelPhysics::OwnedComponent(const FElysiumEntity& Entity)
{
	for (const auto& Entry : NativeAssociations)
		if (Entry.World == Entity.World && Entry.Owner == Entity.Handle)
			if (UPrimitiveComponent* Component = ComponentFor(Entry.Actor)) return Component;
	return nullptr;
}

TArray<UPrimitiveComponent*> ElysiumModelPhysics::WorldComponents(const FElysiumEntityWorld& World)
{
	TArray<UPrimitiveComponent*> Components;
	for (const FNativeAssociation& Entry : NativeAssociations)
		if (Entry.World == &World)
			if (UPrimitiveComponent* Component = ComponentFor(Entry.Actor)) Components.AddUnique(Component);
	return Components;
}

bool ElysiumModelPhysics::GetContactPoint(void* Actor, void*& OtherActor, FString* Provenance)
{
	OtherActor = nullptr;
	if (Provenance) Provenance->Reset();
	const FNativeAssociation* Owner = NativeAssociations.FindByPredicate([Actor](const FNativeAssociation& Entry) { return Entry.Actor == Actor; });
	if (!Owner) return false;
	const uint64 OwnerIncarnation = Owner->Incarnation;
	FBodyInstance* Body = Identity(ComponentFor(Actor));
	if (!Body) return false;
	// Solver state is read only after its outstanding jobs finish. No UObject/owner table is
	// accessed by the solver thread; copy native contact identities before game-side arbitration.
	if (auto* Solver = Body->GetPhysicsActor()->GetSolver<Chaos::FPBDRigidsSolver>()) Solver->WaitOnPendingTasks_External();
	ConsumeContactOutputs(ComponentFor(Actor)->GetWorld());
	TArray<FObservedContact> Current;
	TSet<uint64> SurvivingNativePairs;
	FPhysicsCommand::ExecuteRead(Body->GetPhysicsActor(), [&](const FPhysicsActorHandle& NativeActor)
	{
		const auto* Particle = NativeActor->GetHandle_LowLevel();
		if (!Particle) return;
		Particle->ParticleCollisions().VisitConstCollisions([&](const Chaos::FPBDCollisionConstraint& Contact)
		{
			const auto* First = Contact.GetParticle0(); const auto* Second = Contact.GetParticle1();
			FCompletedContactBirth* Completed = CompletedContactBirths.FindByPredicate([&](const FCompletedContactBirth& Entry)
			{ return Entry.World == ComponentFor(Actor)->GetWorld() && Entry.Contact.Shapes[0] == Contact.GetShape0()
				&& Entry.Contact.Shapes[1] == Contact.GetShape1() && Entry.Contact.Geometry[0] == Contact.GetImplicit0()
				&& Entry.Contact.Geometry[1] == Contact.GetImplicit1() && First && Second
				&& Entry.Contact.Proxies[0] == First->PhysicsProxy() && Entry.Contact.Proxies[1] == Second->PhysicsProxy()
				&& Entry.Contact.UniqueIds[0] == First->UniqueIdx() && Entry.Contact.UniqueIds[1] == Second->UniqueIdx(); });
			if (Completed) SurvivingNativePairs.Add(Completed->Sequence);
			// A cached pair allocation is not an observed friction contact. Preserve the strict
			// entry/inclusive retirement skin from2603e6d0/2603e880 using current native points.
			bool bQualifyingPoint = false;
			if (!Contact.GetDisabled() && !Contact.GetIsProbe())
				for (int32 PointIndex = 0; PointIndex < Contact.NumManifoldPoints(); ++PointIndex)
					if (Contact.IsManifoldPointActive(PointIndex))
					{
						const auto& Point = Contact.GetManifoldPoint(PointIndex);
						if (Point.ContactPoint.IsSet() && FMath::Max(static_cast<double>(Point.ContactPoint.Phi), 0.) < FrictionContactSkinCm())
							bQualifyingPoint = true;
					}
			if (!bQualifyingPoint)
			{ if (Completed) Completed->bQualified = false; return Chaos::ECollisionVisitorResult::Continue; }
			const auto* Partner = Contact.GetParticle0() == Particle ? Contact.GetParticle1() : Contact.GetParticle0();
			void* PartnerObject = Partner ? const_cast<void*>(static_cast<const void*>(Partner->PhysicsProxy())) : nullptr;
			if (RetiredNativeActors.Contains(PartnerObject)) return Chaos::ECollisionVisitorResult::Continue;
			if (const auto* Association = NativeAssociations.FindByPredicate([PartnerObject](const FNativeAssociation& Entry) { return Entry.Actor == PartnerObject; }))
				if (static_cast<Chaos::FSingleParticlePhysicsProxy*>(Association->Actor)->GetHandle_LowLevel() != Partner)
					return Chaos::ECollisionVisitorResult::Continue; //cached old particle cannot impersonate a reused proxy
			if (const auto* Association = WorldCreationAssociations.FindByPredicate([PartnerObject](const FWorldCreationAssociation& Entry) { return Entry.Actor == PartnerObject; }))
				if (!Association->Component.IsValid() || ActorIdentity(Association->Component.Get()) != PartnerObject
					|| static_cast<Chaos::FSingleParticlePhysicsProxy*>(PartnerObject)->GetHandle_LowLevel() != Partner
					|| Partner->UniqueIdx() != Association->UniqueIdx)
					return Chaos::ECollisionVisitorResult::Continue;
			if (!Completed) return Chaos::ECollisionVisitorResult::Continue;
			if (!Completed->bQualified || Completed->bInitialTransition)
			{ Completed->Birth = Completed->Sequence; Completed->BirthTime = Completed->OutputTime; }
			Completed->bQualified = true; Completed->bInitialTransition = false;
			if (Provenance)
			{
				*Provenance += FString::Printf(TEXT("partner=%s sequence=%llu birth=%llu birthTime=%.17g outputTime=%.17g initial=%d; "),
					*FElysiumEntityWorld::AiTraceName(GameDataForObject(PartnerObject)), Completed->Sequence, Completed->Birth,
					Completed->BirthTime, Completed->OutputTime, Completed->Contact.bInitial);

			}
			Current.Add({ &Contact, PartnerObject, 0, Completed->Birth });
			return Chaos::ECollisionVisitorResult::Continue;
		});
	});
	// Absence from a modifier output batch is not retirement (sleep/no-advance).
	// Absence from the current completed native pair roster is actual retirement.
	CompletedContactBirths.RemoveAll([&](const FCompletedContactBirth& Entry)
	{ return (Entry.Contact.Proxies[0] == Actor || Entry.Contact.Proxies[1] == Actor) && !SurvivingNativePairs.Contains(Entry.Sequence); });
	TArray<FObservedContact>& Roster = ContactRosters.FindOrAdd(OwnerIncarnation);
	Roster.RemoveAll([&](const FObservedContact& Previous)
	{
		return !Current.ContainsByPredicate([&](const FObservedContact& Contact)
		{ return Contact.Constraint == Previous.Constraint && Contact.Partner == Previous.Partner; });
	});
	// Completed public native lifecycle supplies births, independent of query traversal order.
	// Sleeping/no-output survivors retain provenance. Native generation/service chronology
	// and the retained newest-surviving selection are separate observer contracts.
	for (FObservedContact& Contact : Current)
	{
		FObservedContact* Previous = Roster.FindByPredicate([&](const FObservedContact& Existing)
		{ return Contact.Constraint == Existing.Constraint && Contact.Partner == Existing.Partner; });
		if (Previous) Previous->Birth = Contact.Birth;
		else
		{
			if (const auto* Partner = NativeAssociations.FindByPredicate([&](const FNativeAssociation& Entry) { return Entry.Actor == Contact.Partner; }))
				Contact.PartnerIncarnation = Partner->Incarnation;
			Roster.Add(Contact); //2603b4f0 prepends; newest surviving birth is44's first
		}
	}
	const FObservedContact* Selected = nullptr;
	for (const auto& Contact : Roster) if (!Selected || Contact.Birth > Selected->Birth) Selected = &Contact;
	if (!Selected) return false;
	OtherActor = Selected->Partner;
	if (Selected->PartnerIncarnation != 0)
	{
		const auto* Partner = NativeAssociations.FindByPredicate([&](const FNativeAssociation& Entry)
		{ return Entry.Actor == Selected->Partner && Entry.Incarnation == Selected->PartnerIncarnation; });
		if (!Partner) OtherActor = nullptr;
	}
	return true; //selected native/world partner with no GameData still dispatches Blocked(nullptr)
}

bool ElysiumModelPhysics::CreateBrush(UElysiumBrushComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
	const FElysiumPhysicsBodyRecipe& Recipe, FString& OutError)
{
	if (Recipe.Kind != EElysiumPhysicsRecipeKind::Brush)
	{ OutError = TEXT("brush creator requires an authored brush recipe"); return false; }
	return CreatePhysicsRecipe(Component, Surfaces, Recipe, false, OutError);
}

void ElysiumModelPhysics::RefreshContactOutputs(FElysiumEntityWorld& World)
{
	TArray<void*> Actors;
	for (const auto& Entry : NativeAssociations) if (Entry.World == &World) Actors.Add(Entry.Actor);
	for (void* Actor : Actors) { void* Partner = nullptr; GetContactPoint(Actor, Partner); }
}

void ElysiumModelPhysics::RetireNative(UPrimitiveComponent* Component)
{
	// Called before Super::OnDestroyPhysicsState releases Chaos. No field may retain a dead actor.
	TArray<void*> RetiringActors;
	for (int32 Index = NativeAssociations.Num() - 1; Index >= 0; --Index)
	{
		if (NativeAssociations[Index].Producer != Component) continue;
		RemoveShadow(NativeAssociations[Index].Actor);
		if (auto* Order = NativeActiveOrders.Find(Component->GetWorld())) Order->RemoveSingleSwap(NativeAssociations[Index].Incarnation);
		RetiringActors.Add(NativeAssociations[Index].Actor);
		RetiredNativeActors.Add(NativeAssociations[Index].Actor);
		ForgetCompletedActor(NativeAssociations[Index].Actor);
		ContactRosters.Remove(NativeAssociations[Index].Incarnation);
		NativeAssociations.RemoveAt(Index);
	}
	if (const auto* Order = NativeActiveOrders.Find(Component->GetWorld()); Order && Order->IsEmpty()) NativeActiveOrders.Remove(Component->GetWorld());
	RetireRecipe(Component); //10158520 ->10045110 precedes native deletion and observer callbacks.
	for (void* Actor : RetiringActors)
	{
		for (auto& Roster : ContactRosters)
			Roster.Value.RemoveAll([Actor](const FObservedContact& Contact) { return Contact.Partner == Actor; }); //2603cf30: retire contact, expose survivor
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
	UPrimitiveComponent* Component = ComponentFor(Actor);
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
	if (UPrimitiveComponent* Component = ComponentFor(Actor)) ConsumeContactOutputs(Component->GetWorld());
	const FShadowAssociation* Entry = FindShadow(Actor);
	return Entry && ComponentFor(Actor) ? &Entry->State : nullptr;
}

TArray<ElysiumModelPhysics::FShadowEventSample> ElysiumModelPhysics::ShadowEvents(void* Actor)
{
	TArray<FShadowEventSample> Result;
	UPrimitiveComponent* Component = ComponentFor(Actor); if (!Component) return Result;
	ConsumeContactOutputs(Component->GetWorld());
	for (const auto& Event : CompletedShadowEvents) if (Event.Proxy == Actor) Result.Add(Event.Sample);
	return Result;
}

void ElysiumModelPhysics::ObservePostSolve(UWorld& World)
{
	EnsureSolveFilterCleanup(); //also retire an opt-in world that never reaches its first Prepare
	PostSolveObservationWorlds.Add(&World); //retired at actual world cleanup; takes effect at Prepare
}

void ElysiumModelPhysics::RemoveShadow(void* Actor)
{
	if (UPrimitiveComponent* Component = ComponentFor(Actor)) ConsumeContactOutputs(Component->GetWorld());
	const FShadowAssociation* Entry = FindShadow(Actor);
	if (!Entry) return;
	if (UPrimitiveComponent* Component = ComponentFor(Actor))
		if (const auto* Registration = SolveFilters.Find(Component->GetWorld()))
		{
			Registration->Callback->RetireShadow_External(CaptureIdentity(Actor).Incarnation,Entry->Revision);
			Registration->Callback->GetProducerInputData_External()->Shadows.RemoveAll([Actor](const auto& Input) { return Input.Proxy == Actor; });
		}
	const FElysiumPhysicsShadowState State = Entry->State;
	if (UPrimitiveComponent* Component = ComponentFor(Actor))
		if (FBodyInstance* Body = Identity(Component))
		{
			Component->SetMassOverrideInKg(NAME_None, State.SavedMassKg, true);
			SetInertia(*Body, State.SavedInertia);
			Component->SetEnableGravity(State.bSavedGravity);
		}
	ShadowAssociations.RemoveAll([Actor](const FShadowAssociation& Item) { return Item.Actor == Actor; });
	CompletedShadowEvents.RemoveAll([Actor](const auto& Item) { return Item.Proxy == Actor; });
}

bool ElysiumModelPhysics::SetShadow(void* Actor, float LinearSpeedInches, float AngularSpeedDegrees,
	bool bAllowMovement, bool bAllowRotation)
{
	UPrimitiveComponent* Component = ComponentFor(Actor);
	FBodyInstance* Body = Identity(Component);
	if (!Body || !Component->IsSimulatingPhysics() || !FMath::IsFinite(LinearSpeedInches) || !FMath::IsFinite(AngularSpeedDegrees)) return false;
	ConsumeContactOutputs(Component->GetWorld());
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
	Entry->Revision = ++NextShadowRevision;
	RefreshShadowInput(*Entry);
	return true;
}

bool ElysiumModelPhysics::UpdateShadow(void* Actor, const FVector& OriginCm, const FQuat& Rotation,
	bool bGravityMode, float ArrivalSeconds)
{
	UPrimitiveComponent* Component = ComponentFor(Actor); FBodyInstance* Body = Identity(Component);
	if (!Body || OriginCm.ContainsNaN() || Rotation.ContainsNaN() || !Rotation.IsNormalized() || !FMath::IsFinite(ArrivalSeconds)) return false;
	ConsumeContactOutputs(Component->GetWorld());
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
	Entry->Revision = ++NextShadowRevision;
	RefreshShadowInput(*Entry);
	if (bChanged) Wake(Component); //2601aad0: repeated target still replaces countdown, no wake.
	return true;
}

bool ElysiumModelPhysics::SetShadowTeleportDistance(void* Actor, float DistanceInches)
{
	if (UPrimitiveComponent* Component = ComponentFor(Actor)) ConsumeContactOutputs(Component->GetWorld());
	FShadowAssociation* Entry = FindShadow(Actor);
	if (!Entry || !ComponentFor(Actor) || !FMath::IsFinite(DistanceInches)) return false;
	Entry->State.TeleportDistanceCm = DistanceInches * 2.54f; Entry->Revision = ++NextShadowRevision; RefreshShadowInput(*Entry); return true; //2601aa40
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
	void SimulateShadow(Chaos::FRigidBodyHandle_Internal& Particle, FElysiumPhysicsShadowState& State,
		Chaos::FSingleParticlePhysicsProxy& Proxy, Chaos::FPBDRigidsSolver& NativeSolver, double EventDelta, ElysiumModelPhysics::FShadowEventSample& Sample)
	{
		if (!State.bActive) { State.PredictedPosition = FVector::ZeroVector; return; }
		const float DeltaSeconds = static_cast<float>(EventDelta); //2601aa50 passes FLOAT to2601a250
		if (!(DeltaSeconds > 0.f) || !FMath::IsFinite(DeltaSeconds)) return;
		Sample.bSimulated = true;
		const float Fraction = State.RemainingSeconds <= 0.f ? 1.f : FMath::Min(DeltaSeconds / State.RemainingSeconds, 1.f);
		Sample.Fraction = Fraction;
		State.RemainingSeconds = static_cast<float>(FMath::Max(static_cast<double>(State.RemainingSeconds) - EventDelta, 0.)); //2601aa50
		if (Fraction <= 0.f) return;
		const FTransform Current(Particle.R(), Particle.X());
		const FVector Displacement = State.Target.GetLocation() - Current.GetLocation();
		if (State.TeleportDistanceCm > 0.f)
		{
			const bool bNoPrediction = State.PredictedPosition.X == 0. && State.PredictedPosition.Y == 0. && State.PredictedPosition.Z == 0.;
			const FVector Error = bNoPrediction ? Displacement : Current.GetLocation() - State.PredictedPosition;
			Sample.TeleportErrorSquared = Error.SizeSquared();
			if (Error.SizeSquared() > FMath::Square(State.TeleportDistanceCm))
			{
				// Public PT shape flags bracket an immediate unswept pose write. Restore before
				// Chaos generates/solves contacts; never hold collision off for a simulation step.
				TArray<TPair<bool,bool>, TInlineAllocator<8>> Collision;
				for (const auto& Shape : Particle.ShapesArray())
				{
					Collision.Emplace(Shape->GetSimEnabled(),Shape->GetQueryEnabled());
					Shape->SetSimEnabled(false); Shape->SetQueryEnabled(false);
				}
				// Public evolution teleport maintains bounds, cached contacts and island state.
				NativeSolver.GetEvolution()->SetParticleTransform(Proxy.GetHandle_LowLevel(),State.Target.GetLocation(),State.Target.GetRotation(),true);
				int32 ShapeIndex = 0;
				for (const auto& Shape : Particle.ShapesArray())
				{
					Shape->SetSimEnabled(Collision[ShapeIndex].Key); Shape->SetQueryEnabled(Collision[ShapeIndex].Value);
					Sample.bCollisionRestored &= Shape->GetSimEnabled() == Collision[ShapeIndex].Key && Shape->GetQueryEnabled() == Collision[ShapeIndex].Value;
					++ShapeIndex;
				}
				Sample.bTeleported = true; //2601a250 retains preteleport displacement/current rotation.
			}
		}
		const FVector Linear = ShadowVelocity(Particle.V(), Displacement, Fraction / DeltaSeconds,
			State.LinearDesiredLimitCm, State.LinearDampingLimitCm, .01);
		Particle.SetV(Linear);
		State.PredictedPosition = Current.GetLocation() + Linear * DeltaSeconds;
		FQuat Relative = Current.GetRotation().Inverse() * State.Target.GetRotation(); Relative.Normalize();
		float Angle = 2.f * FMath::Acos(FMath::Clamp(static_cast<float>(Relative.W), -1.f, 1.f));
		if (Angle > PI) Angle -= 2.f * PI;
		const FVector Axis = FVector(Relative.X, Relative.Y, Relative.Z).GetSafeNormal();
		const FVector LocalActual = Current.GetRotation().UnrotateVector(Particle.W());
		const FVector LocalAngular = ShadowVelocity(LocalActual, Axis * Angle, Fraction / DeltaSeconds,
			State.AngularDesiredLimitRadians, State.AngularDampingLimitRadians, 1.0e-6);
		Particle.SetW(Current.GetRotation().RotateVector(LocalAngular));
	}
}

float ElysiumModelPhysics::PreparePhysicsFrame(UWorld& World, float FrameSeconds, float TimeScale, bool bSimulating)
{
	FPhysScene* Scene = World.GetPhysicsScene(); if (!Scene) return 0.f;
	ConsumeContactOutputs(&World); //publish completed controller state before replacing any input
	// A component physics rebuild retains its creation owner, but never the retired proxy.
	for (int32 Index = WorldCreationAssociations.Num() - 1; Index >= 0; --Index)
	{
		FWorldCreationAssociation& Entry = WorldCreationAssociations[Index];
		UElysiumWorldCollisionComponent* Component = Entry.Component.Get();
		if (Component && Component->GetWorld() != &World) continue;
		auto* Proxy = ActorIdentity(Component);
		if (!Proxy || Proxy != Entry.Actor || Proxy->GetGameThreadAPI().UniqueIdx() != Entry.UniqueIdx)
		{
			RetireWorldContact(Entry.Actor);
			if (!Component) { WorldCreationAssociations.RemoveAt(Index); continue; }
			if (!Proxy) { Entry.Actor = nullptr; Entry.UniqueIdx = Chaos::FUniqueIdx(); continue; }
			Entry.Actor = Proxy; Entry.UniqueIdx = Proxy->GetGameThreadAPI().UniqueIdx(); Entry.Generation = ++NextWorldGeneration;
			RetiredNativeActors.Remove(Proxy);
		}
	}
	PrepareSolveFilter(World, *Scene);
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
	CompletedShadowEvents.RemoveAll([&](const auto& Event)
	{ UPrimitiveComponent* Component = ComponentFor(const_cast<void*>(Event.Proxy)); return !Component || Component->GetWorld() == &World; });
	if (auto* Registration = SolveFilters.Find(&World))
	{
		Registration->bAdmitted = Delta > 0.f;
		auto* Input = Registration->Callback->GetProducerInputData_External(); Input->Shadows.Reset();
		if (Delta > 0.f) for (const FShadowAssociation& Entry : ShadowAssociations)
			if (UPrimitiveComponent* Component = ComponentFor(Entry.Actor); Component && Component->GetWorld() == &World)
			{
				auto* Proxy = ActorIdentity(Component);
				Input->Shadows.Add({Proxy,Proxy->GetGameThreadAPI().UniqueIdx(),CaptureIdentity(Entry.Actor).Incarnation,Entry.Revision,Entry.State});
			}
	}
	return Delta;
}

FBodyInstance* ElysiumModelPhysics::Identity(UPrimitiveComponent* Component)
{
	FBodyInstance* Body = Component && !Component->IsBeingDestroyed() ? Component->GetBodyInstance() : nullptr;
	return Body && Body->IsValidBodyInstance() ? Body : nullptr;
}

bool ElysiumModelPhysics::IsActive(UPrimitiveComponent* Component)
{
	const FBodyInstance* Body = Identity(Component);
	return Body && Component->IsSimulatingPhysics() && Body->IsInstanceAwake();
}

Chaos::FSingleParticlePhysicsProxy* ElysiumModelPhysics::ActorIdentity(UPrimitiveComponent* Component)
{
	const FBodyInstance* NativeBody = Identity(Component);
	return NativeBody ? NativeBody->GetPhysicsActor() : nullptr;
}

void ElysiumModelPhysics::Destroy(UPrimitiveComponent* Component)
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
bool CreatePhysicsRecipe(UPrimitiveComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
	const FElysiumPhysicsBodyRecipe& Recipe, bool bStartAsleep, FString& OutError)
{
	OutError.Reset();
	ElysiumModelPhysics::Destroy(Component);
	bool bSucceeded = false;
	ON_SCOPE_EXIT { if (!bSucceeded) ElysiumModelPhysics::Destroy(Component); };
	auto* Proxy = Cast<UElysiumPhysicsPropComponent>(Component);
	auto* Brush = Cast<UElysiumBrushComponent>(Component);
	if (!Proxy && !Brush) { OutError = TEXT("body recipe requires a native recipe component"); return false; }
	if (Brush)
	{
		if (Recipe.Kind != EElysiumPhysicsRecipeKind::Brush || Recipe.BrushHulls.IsEmpty())
		{ OutError = TEXT("brush requires authored convex reconstruction inputs"); return false; }
		TArray<FElysiumConvexHull> Hulls;
		for (const TArray<FVector>& Vertices : Recipe.BrushHulls)
		{
			if (Vertices.Num() < 4 || Vertices.ContainsByPredicate([](const FVector& V) { return V.ContainsNaN(); }))
			{ OutError = TEXT("invalid authored brush convex"); return false; }
			FElysiumConvexHull& Hull = Hulls.AddDefaulted_GetRef(); Hull.Vertices = Vertices;
		}
		if (Recipe.BrushSolidity > static_cast<uint8>(EElysiumBrushSolidity::Passable))
		{ OutError = TEXT("invalid brush solidity recipe"); return false; }
		Brush->InitBrush(Brush->GetOwningEntity(), Hulls, static_cast<EElysiumBrushSolidity>(Recipe.BrushSolidity), Recipe.BrushContentsSignature);
	}
	else
	{
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
	if (Proxy)
	{
		Proxy->OnComponentWake.AddUniqueDynamic(Proxy, &UElysiumPhysicsPropComponent::NativeWake);
		Proxy->OnComponentSleep.AddUniqueDynamic(Proxy, &UElysiumPhysicsPropComponent::NativeSleep);
	}
	else
	{
		Brush->OnComponentWake.AddUniqueDynamic(Brush, &UElysiumBrushComponent::NativeWake);
		Brush->OnComponentSleep.AddUniqueDynamic(Brush, &UElysiumBrushComponent::NativeSleep);
	}
	Component->SetLinearDamping(Recipe.LinearDamping);
	Component->SetAngularDamping(Recipe.AngularDamping);
	Component->SetMassOverrideInKg(NAME_None, Mass, true);
	Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Component->SetMobility(Recipe.bStatic ? EComponentMobility::Static : EComponentMobility::Movable);
	Component->SetSimulatePhysics(!Recipe.bStatic);
	if (Recipe.Kind != EElysiumPhysicsRecipeKind::Model) Component->RecreatePhysicsState();
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
	RetiredNativeActors.Remove(ElysiumModelPhysics::ActorIdentity(Component));
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

bool ElysiumModelPhysics::Restore(UPrimitiveComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
	const FElysiumSavedPhysicsBody& State, FString& OutError)
{
	Destroy(Component);
	if (!Component || !ValidShadow(State.Shadow) || (State.Shadow.bInstalled && State.Recipe.bStatic)
		|| (State.bShadowMember && !State.Shadow.bInstalled)
		|| State.Pose.ContainsNaN() || State.LinearVelocity.ContainsNaN() || State.AngularVelocityRadians.ContainsNaN()
		|| static_cast<uint8>(State.Recipe.Kind) > static_cast<uint8>(EElysiumPhysicsRecipeKind::Brush)
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
	if (!State.CollisionProfile.IsNone()) Component->SetCollisionProfileName(State.CollisionProfile);
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
