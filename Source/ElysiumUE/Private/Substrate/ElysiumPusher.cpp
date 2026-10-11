#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumModelPhysics.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumCollisionRules.h"
#include "Map/ElysiumRetailMaskRecipe.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Components/PrimitiveComponent.h"
#include "ElysiumPlayer.h"
#include "Misc/ScopeExit.h"
#include "Substrate/ElysiumNpcAccess.h"
#include "Chaos/GeometryQueries.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "CollisionShape.h"

namespace
{
	// Semantic trace output and exclusion set for the actual virtual102 caller. No retail
	// trace_t or partition layout crosses the seam; geometry remains native cooked geometry.
	struct FPusherTrace
	{
		FHitResult Hit;
		TArray<FElysiumEntityHandle> Ignore;
	};
	struct FPushFrame
	{
		float M[12];
		explicit FPushFrame(FElysiumEntity& Entity)
		{
			//10037c10 explicitly rebuilds AngleMatrix from217/219, independent of retained
			// raw coordinateframe.10139b90 stores each radians and sin/cos word as float.
			const FVector Angles = Entity.GetAbsAngles();
			const float Pitch = static_cast<float>(Angles.X) * 0.01745329238474369f;
			const float Yaw = static_cast<float>(Angles.Y) * 0.01745329238474369f;
			const float Roll = static_cast<float>(Angles.Z) * 0.01745329238474369f;
			const double Cp = static_cast<float>(FMath::Cos(static_cast<double>(Pitch)));
			const double Sp = static_cast<float>(FMath::Sin(static_cast<double>(Pitch)));
			const double Cy = static_cast<float>(FMath::Cos(static_cast<double>(Yaw)));
			const double Sy = static_cast<float>(FMath::Sin(static_cast<double>(Yaw)));
			const double Cr = static_cast<float>(FMath::Cos(static_cast<double>(Roll)));
			const double Sr = static_cast<float>(FMath::Sin(static_cast<double>(Roll)));
			M[0] = static_cast<float>(Cp*Cy); M[4] = static_cast<float>(Cp*Sy); M[8] = static_cast<float>(-Sp);
			M[1] = static_cast<float>(Cy*Sr*Sp-Cr*Sy); M[5] = static_cast<float>(Cr*Cy+Sr*Sp*Sy); M[9] = static_cast<float>(Sr*Cp);
			M[2] = static_cast<float>(Sr*Sy+Cy*Cr*Sp); M[6] = static_cast<float>(Cr*Sp*Sy-Sr*Cy); M[10] = static_cast<float>(Cr*Cp);
			const FVector Position = Entity.GetAbsOrigin();
			M[3] = static_cast<float>(Position.X/2.54); M[7] = static_cast<float>(-Position.Y/2.54); M[11] = static_cast<float>(Position.Z/2.54);
		}
		FVector PushCentre(const FPushFrame& New, const FVector& CentreCm) const
		{
			const float C[3] = { static_cast<float>(CentreCm.X/2.54), static_cast<float>(-CentreCm.Y/2.54), static_cast<float>(CentreCm.Z/2.54) };
			const double D[3] = { static_cast<float>(C[0]-M[3]), static_cast<float>(C[1]-M[7]), static_cast<float>(C[2]-M[11]) };
			float Local[3], Changed[3], DeltaWords[3];
			for (int32 Axis = 0; Axis < 3; ++Axis)
				Local[Axis] = static_cast<float>(D[0]*M[Axis]+D[1]*M[4+Axis]+D[2]*M[8+Axis]); //10138130
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const float* Row = New.M + Axis*4;
				Changed[Axis] = static_cast<float>(static_cast<double>(Local[0])*Row[0]+static_cast<double>(Local[1])*Row[1]+static_cast<double>(Local[2])*Row[2]+Row[3]); //10137fe0
				DeltaWords[Axis] = Changed[Axis]-C[Axis]; //100360a0 final float stores
			}
			return FVector(DeltaWords[0],-DeltaWords[1],DeltaWords[2])*2.54;
		}
	};
	struct FPushEntry
	{
		FElysiumEntityHandle Entity;
		FVector Origin;
		FHitResult Trace;
	};
	UPrimitiveComponent* QueryComponent(FElysiumEntity& Entity)
	{
		if (UPrimitiveComponent* Native = ElysiumModelPhysics::OwnedComponent(Entity)) return Native;
		return Entity.Body ? static_cast<UPrimitiveComponent*>(Entity.Body) : Entity.GetAttachBody();
	}
	FElysiumEntity* ComponentEntity(FElysiumEntityWorld& World, UPrimitiveComponent* Component)
	{
		// PhysicsImpact consumes the trace entity, including creation-owned worldspawn and
		// independent native aliases. Current field/component equality is not object6 ownership.
		if (FElysiumEntity* Owner = ElysiumModelPhysics::GameDataForObject(ElysiumModelPhysics::ActorIdentity(Component)))
			return Owner->World == &World ? Owner : nullptr;
		for (const auto& Entry : World.Entities())
			if (Entry && !Entry->IsDead() && QueryComponent(*Entry) == Component) return Entry.Get();
		return nullptr;
	}
	FElysiumEntity* RootEntity(FElysiumEntity& Entity)
	{
		FElysiumEntity* Root = &Entity;
		for (int32 Guard = 0; Guard < 8192 && Root->World; ++Guard)
		{
			FElysiumEntity* Parent = Root->World->Resolve(Root->MoveParent);
			if (!Parent || Parent == Root) break;
			Root = Parent;
		}
		return Root;
	}
	void CollectHierarchy(FElysiumEntity& Entity, TArray<FPushEntry>& Entries)
	{
		if (Entries.ContainsByPredicate([&](const FPushEntry& Entry) { return Entry.Entity == Entity.Handle; })) return;
		Entries.Add({ Entity.Handle, Entity.GetAbsOrigin(), {} }); //100379e0 depth first
		if (!Entity.World) return;
		FElysiumEntity* Child = Entity.World->Resolve(Entity.MoveChild);
		for (int32 Guard = 0; Child && Guard < 8192; ++Guard)
		{
			const auto Peer = Child->MovePeer;
			CollectHierarchy(*Child, Entries);
			Child = Entity.World->Resolve(Peer);
		}
	}
	bool Solid(FElysiumEntity& Entity) { return Entity.GetSolid() != 0 && (Entity.GetSolidFlags() & 4) == 0; }
	FVector CollisionPoint(FElysiumEntity& Entity, const FVector& SourcePoint)
	{
		const FVector Position = Entity.GetAbsOrigin(); // ensure retained collision frame is current
		if (Entity.GetSolid() == 2) return Position + FVector(SourcePoint.X,-SourcePoint.Y,SourcePoint.Z)*2.54;
		const float* Matrix = Entity.CoordinateFrame;
		float Words[3];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float* Row = Matrix + Axis*4;
			Words[Axis] = static_cast<float>(SourcePoint.X*Row[0]+SourcePoint.Y*Row[1]+SourcePoint.Z*Row[2]+Row[3]);
		}
		return FVector(Words[0],-Words[1],Words[2])*2.54;
	}
	FBox CollisionBounds(FElysiumEntity& Entity)
	{
		FBox Bounds(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
			Bounds += CollisionPoint(Entity, FVector(Corner&1 ? Entity.CollMaxs.X : Entity.CollMins.X,
				Corner&2 ? Entity.CollMaxs.Y : Entity.CollMins.Y, Corner&4 ? Entity.CollMaxs.Z : Entity.CollMins.Z));
		return Bounds;
	}
	FTransform LogicalPose(FElysiumEntity& Entity)
	{
		return FTransform(Entity.GetSolid() == 2 ? FQuat::Identity : ElysiumModelPhysics::SourceRotation(Entity.GetAbsAngles()), Entity.GetAbsOrigin());
	}
	bool Intersects(FElysiumEntity& First, FElysiumEntity& Second)
	{
		UPrimitiveComponent* FirstBody = QueryComponent(First);
		UPrimitiveComponent* SecondBody = QueryComponent(Second);
		if (!FirstBody || !SecondBody) return false;
		FBodyInstance* QueryBody = FirstBody->GetBodyInstance();
		FBodyInstance* OtherBody = SecondBody->GetBodyInstance();
		if (!QueryBody || !OtherBody) return false;
		// Both logical transforms are explicit. Rebase into the other's actual native frame;
		// the engine tests its cooked geometry without moving either native body.
		const FTransform QueryPose = LogicalPose(First).GetRelativeTransform(LogicalPose(Second)) * OtherBody->GetUnrealWorldTransform();
		return QueryBody->OverlapTestForBody(QueryPose.GetLocation(), QueryPose.GetRotation(), OtherBody);
	}
	bool Admits(FElysiumEntity& Query, FElysiumEntity& Other, uint32 Mask)
	{
		FElysiumEntityWorld* const EntityWorld = Query.World;
		const FElysiumEntityHandle QueryHandle = Query.Handle, OtherHandle = Other.Handle;
		auto Live = [&]() { return !EntityWorld || (EntityWorld->Resolve(QueryHandle) == &Query && EntityWorld->Resolve(OtherHandle) == &Other); };
		//101d3080 Standard BEFORE any callback-bearing91. Brush model contents are
		// authored inputs; nonbrush/model entities retain the separate MONSTER predicate.
		if (!Solid(Other) || (Other.GetSolidFlags() & 0x20) != 0) return false;
		const bool bBrush = Other.Def && Other.Def->Model >= 0 && Other.Def->Hulls.Num() != 0
			&& (Other.GetSolid() == 1 || Other.GetSolid() == 6);
		if (bBrush ? (Other.Def->Contents & Mask) == 0 : !(Mask & 0x2000000) && !Other.bBlocksTraces) return false;
		if (!(Mask & 2))
		{
			const bool bTransparent = Other.IsTransparent();
			if (!Live() || bTransparent) return false;
		}
		if (!(Mask & 0x4000))
		{
			const int32 MoveType = Other.GetMoveType();
			if (!Live() || MoveType == 8) return false;
		}
		//101d2fc0: owner, never move-parent/root.
		if (&Query == &Other || Other.GetOwnerEntity() == QueryHandle || Query.GetOwnerEntity() == OtherHandle) return false;
		const bool bShouldCollide = Other.ShouldCollide(Query.CollisionGroup, Mask);
		if (!Live() || !bShouldCollide) return false;
		if (!ElysiumNpcAccess::IsBccTargetable(Other) || (Other.AsCombatCharacter() && Other.bHidden)) return false;
		if (!ElysiumCollisionRules::ShouldCollide(Query.CollisionGroup, Other.CollisionGroup)) return false;
		if (Mask != 0x46004003)
		{
			const bool bCandidateIgnores = Other.ShouldIgnoreCollision(&Query);
			if (!Live() || bCandidateIgnores) return false;
			const bool bPassIgnores = Query.ShouldIgnoreCollision(&Other);
			if (!Live() || bPassIgnores) return false;
		}
		return true;
	}
	void ExcludeEntityBodies(FElysiumEntityWorld& World, FCollisionQueryParams& Params)
	{
		// Entity partition geometry is queried once at the logical pose. Independent saved
		// physics aliases remain real actors, but cannot become extra self/world blockers.
		for (UPrimitiveComponent* Component : ElysiumModelPhysics::WorldComponents(World)) Params.AddIgnoredComponent(Component);
		for (const auto& Entry : World.Entities())
			if (Entry && QueryComponent(*Entry)) Params.AddIgnoredComponent(QueryComponent(*Entry));
	}
	// Cooked-body sweep against another body's logical pose. Logical pusher writes never
	// move its native shadow merely to ask a query. Both geometry and narrowphase are Chaos.
	void SweepLogicalPair(FElysiumEntity& Query, FElysiumEntity& Other, const FVector& Start,
		const FVector& End, FHitResult& Best)
	{
		FBodyInstance* QueryBody = QueryComponent(Query) ? QueryComponent(Query)->GetBodyInstance() : nullptr;
		FBodyInstance* OtherBody = QueryComponent(Other) ? QueryComponent(Other)->GetBodyInstance() : nullptr;
		if (!QueryBody || !OtherBody || !QueryBody->IsValidBodyInstance() || !OtherBody->IsValidBodyInstance()) return;
		const FVector Motion = End - Start;
		const double Length = Motion.Size();
		if (Length == 0.) return;
		FPhysicsCommand::ExecuteRead(QueryBody->GetPhysicsActor(), OtherBody->GetPhysicsActor(),
			[&](const FPhysicsActorHandle& QueryActor, const FPhysicsActorHandle& OtherActor)
		{
			TArray<FPhysicsShapeHandle> QueryShapes, OtherShapes;
			FPhysicsInterface::GetAllShapes_AssumedLocked(QueryActor, QueryShapes);
			FPhysicsInterface::GetAllShapes_AssumedLocked(OtherActor, OtherShapes);
			const FTransform QueryPose(LogicalPose(Query).GetRotation(), Start);
			for (const auto& QueryShape : QueryShapes)
			{
				if (!FPhysicsInterface::IsQueryShape(QueryShape)) continue;
				const auto QueryGeometry = FPhysicsInterface::GetGeometryCollection(QueryShape);
				const FTransform QueryTransform = FPhysicsInterface::GetLocalTransform(QueryShape) * QueryPose;
				for (const auto& OtherShape : OtherShapes)
				{
					if (!FPhysicsInterface::IsQueryShape(OtherShape)) continue;
					const auto OtherGeometry = FPhysicsInterface::GetGeometryCollection(OtherShape);
					const FTransform OtherTransform = FPhysicsInterface::GetLocalTransform(OtherShape) * LogicalPose(Other);
					Chaos::FReal Time = Length; Chaos::FVec3 Position, Normal, FaceNormal; int32 Face = INDEX_NONE;
					if (Chaos::SweepQuery(OtherGeometry.GetGeometry(), Chaos::FRigidTransform3(OtherTransform.GetLocation(), OtherTransform.GetRotation()),
						QueryGeometry.GetGeometry(), Chaos::FRigidTransform3(QueryTransform.GetLocation(), QueryTransform.GetRotation()),
						Motion / Length, Length, Time, Position, Normal, Face, FaceNormal, 0., true))
					{
						const float Fraction = static_cast<float>(FMath::Max(static_cast<double>(Time), 0.) / Length);
						if (Fraction >= Best.Time && !(Time < 0. && !Best.bStartPenetrating)) continue;
						Best = FHitResult(); Best.bBlockingHit = true; Best.Time = Fraction;
						Best.bStartPenetrating = Time < 0.; Best.PenetrationDepth = static_cast<float>(FMath::Max(-static_cast<double>(Time), 0.));
						Best.Location = Start + Motion * Fraction; Best.ImpactPoint = Position; Best.ImpactNormal = Normal;
						Best.Component = QueryComponent(Other);
					}
				}
			}
		});
	}
	bool TestEntityPosition(FElysiumEntity& Entity)
	{
		if (!Entity.World) return false;
		FElysiumEntityWorld* const EntityWorld = Entity.World;
		const FElysiumEntityHandle Self = Entity.Handle;
		UWorld* NativeWorld = Entity.World->GetOwnerActor() ? Entity.World->GetOwnerActor()->GetWorld() : nullptr;
		if (!NativeWorld) return false;
		const uint32 Mask = Entity.PhysicsSolidMaskForEntity(); //10035a70:237 before bounds/origin
		if (EntityWorld->Resolve(Self) != &Entity) return false;
		const auto Recipe = ElysiumRetailMask::Recipe(Mask);
		if (Recipe.bNothing) return false;
		const FVector Mins = FVector(Entity.CollMins.X, -Entity.CollMaxs.Y, Entity.CollMins.Z) * 2.54;
		const FVector Maxs = FVector(Entity.CollMaxs.X, -Entity.CollMins.Y, Entity.CollMaxs.Z) * 2.54;
		const FVector Centre = (Mins + Maxs) * .5;
		const FCollisionShape Shape = FCollisionShape::MakeBox((Maxs - Mins) * .5);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumPusherPosition));
		ExcludeEntityBodies(*Entity.World, Params);
		Params.IgnoreMask = ElysiumRetailMask::QueryIgnoreMask(Recipe, false);
		if (NativeWorld->OverlapBlockingTestByChannel(Entity.GetAbsOrigin() + Centre, FQuat::Identity, Recipe.Channel, Shape, Params)) return true;
		// The current encounter set ends at query entry. Runtime creation appends slots;
		// filters may reallocate that array, so retain indices/handles, never iterators.
		const int32 EncounterEnd = EntityWorld->Entities().Num();
		for (int32 Encounter = 0; Encounter < EncounterEnd; ++Encounter)
		{
			FElysiumEntity* Other = EntityWorld->Entities().IsValidIndex(Encounter) ? EntityWorld->Entities()[Encounter].Get() : nullptr;
			if (!Other || Other == &Entity) continue;
			const FElysiumEntityHandle OtherHandle = Other->Handle;
			const bool bAdmitted = Admits(Entity, *Other, Mask);
			if (EntityWorld->Resolve(Self) != &Entity) return false;
			Other = EntityWorld->Resolve(OtherHandle);
			if (!bAdmitted || !Other) continue;
			FBodyInstance* Body = QueryComponent(*Other) ? QueryComponent(*Other)->GetBodyInstance() : nullptr;
			if (!Body || !Body->IsValidBodyInstance()) continue;
			const FTransform Asked(FQuat::Identity, Entity.GetAbsOrigin() + Centre);
			const FTransform Rebased = Asked.GetRelativeTransform(LogicalPose(*Other)) * Body->GetUnrealWorldTransform();
			FMTDResult Penetration;
			if (Body->OverlapTest(Rebased.GetLocation(), Rebased.GetRotation(), Shape, &Penetration) && Penetration.Distance > 0.) return true;
		}
		return false;
	}
	void Impact(FElysiumEntity& Entity, FElysiumEntity* Other)
	{
		if (Other && Entity.World && ((Entity.Flags | Other->Flags) & 0x4000000) == 0)
			Entity.World->RouteEntityTouch(Entity.Handle, Other->Handle, true, true); //1003e6c0 ->1003e2e0
	}
	bool PerformPush(FElysiumEntity& Root, const FVector& Delta, bool bRotation, float Interval)
	{
		if (!Root.World) return false;
		FElysiumEntityWorld& EntityWorld = *Root.World;
		const FElysiumEntityHandle RootHandle = Root.Handle;
		TArray<FPushEntry> Pushers, Candidates;
		CollectHierarchy(Root, Pushers);
		const FVector SavedLocal = bRotation ? Root.GetAngles() : Root.GetOrigin();
		const FPushFrame OldFrame(Root);
		if (bRotation)
		{
			const FVector NewAngles = SavedLocal + Delta;
			Root.SetAngles(FRotator(NewAngles.X, NewAngles.Y, NewAngles.Z)); //10037c10
		}
		else Root.SetOrigin(SavedLocal + Delta); //10038180; NEVER sweep the root against world
		const FPushFrame NewFrame(Root);
		UWorld* NativeWorld = EntityWorld.GetOwnerActor() ? EntityWorld.GetOwnerActor()->GetWorld() : nullptr;
		//10036fb0 partitions each reversed pusher's absolute surrounding box. Native encounter
		// order below remains provisional until the multi-candidate callback witness establishes
		// its observable service order; game append/dedup/reverse order is never sorted here.
		for (int32 Index = Pushers.Num() - 1; Index >= 0; --Index)
		{
			FElysiumEntity* Pusher = EntityWorld.Resolve(Pushers[Index].Entity);
			if (!Pusher || !Solid(*Pusher) || (Pusher->GetSolidFlags() & 0x20)) continue;
			TArray<FOverlapResult> Overlaps;
			if (NativeWorld && QueryComponent(*Pusher))
			{
				const FBox Bounds = CollisionBounds(*Pusher);
				FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumPushCandidates));
				NativeWorld->OverlapMultiByObjectType(Overlaps, Bounds.GetCenter(), FQuat::Identity,
					FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), FCollisionShape::MakeBox(Bounds.GetExtent()), Params);
			}
			TArray<FElysiumEntity*> Eligible;
			for (const auto& Overlap : Overlaps)
				if (auto* Entity = ComponentEntity(EntityWorld, Overlap.GetComponent())) Eligible.AddUnique(Entity);
			// Standing bodies are retained even when the broadphase pair is exactly tangent.
			for (const auto& Entry : EntityWorld.Entities())
				if (Entry && Entry->GetGroundEntity() == Pusher) Eligible.AddUnique(Entry.Get());
			for (FElysiumEntity* Entity : Eligible)
			{
				const int32 Move = Entity->GetMoveType();
				if (!Solid(*Entity) || Move == 8 || Move == 0 || Move == 7 || Move == 9
					|| !ElysiumCollisionRules::ShouldCollide(Entity->CollisionGroup, Root.CollisionGroup)) continue;
				FElysiumEntity* Candidate = RootEntity(*Entity);
				if (Candidate == &Root || Pushers.ContainsByPredicate([&](const FPushEntry& Entry) { return Entry.Entity == Candidate->Handle; })
					|| Candidates.ContainsByPredicate([&](const FPushEntry& Entry) { return Entry.Entity == Candidate->Handle; })) continue;
				if (Entity->GetGroundEntity() != Pusher && !Intersects(*Entity, *Pusher)) continue;
				Candidates.Add({ Candidate->Handle, Candidate->GetAbsOrigin(), {} });
				EntityWorld.EmitRetailSite(*Candidate, TEXT("pusher_append"), TEXT("CPushBlockerEnum::EnumElement"), 0x10037380u,
					TEXT("captured"), FString::Printf(TEXT("index=%d"), Candidates.Num()-1));
			}
		}
		FElysiumEntityHandle Blocker;
		EntityWorld.EmitRetailSite(Root, TEXT("pusher_candidates"), TEXT("GenerateBlockingEntityList"), 0x10036fb0u, TEXT("captured"), FString::Printf(TEXT("count=%d"), Candidates.Num()));
		for (int32 Index = Candidates.Num() - 1; Index >= 0; --Index)
		{
			FPushEntry& Entry = Candidates[Index];
			FElysiumEntity* Candidate = EntityWorld.Resolve(Entry.Entity);
			if (!Candidate) continue;
			FVector Move = bRotation ? OldFrame.PushCentre(NewFrame, CollisionPoint(*Candidate, (Candidate->CollMins+Candidate->CollMaxs)*.5))
				: Root.GetPusherAbsoluteVelocity() * Interval;
			FPusherTrace QueryTrace;
			QueryTrace.Hit.Time = 1.f;
			for (const auto& Pushed : Pushers)
				QueryTrace.Ignore.Add(Pushed.Entity); //10036270 UnlinkPusherList for the sweep only
			int32 Mask = Candidate->PhysicsSolidMaskForEntity(); //237 precedes solidity/parent gate
			Candidate = EntityWorld.Resolve(Entry.Entity);
			if (!Candidate) continue;
			const bool bTrace = Solid(*Candidate) && !(Candidate->GetSolidFlags() & 0x20);
			if (bTrace || !EntityWorld.Resolve(Candidate->MoveParent))
			{
				if (!bTrace) Mask &= ~0x2000000;
				//10036810: all497 vampire.dll class tables fill189 with100ad850 (literal0).
				// The nonzero engine49 arm has no retail class producer; do not invent an upward hook.
				const int32 Selector = Candidate->GetDamageType();
				Candidate = EntityWorld.Resolve(Entry.Entity);
				if (!Candidate) continue;
				if (Selector == 0)
					Candidate->Physics_TraceEntity(Candidate, Entry.Origin, Entry.Origin + Move, Mask, &QueryTrace);
				else
				{
					UE_LOG(LogTemp, Error, TEXT("pusher trace189 nonzero has no recovered retail producer (10036810 engine49)"));
					continue;
				}
			}
			Candidate = EntityWorld.Resolve(Entry.Entity);
			if (!Candidate) continue;
			Entry.Trace = QueryTrace.Hit;
			if (Entry.Trace.Time != 0.f) { FVector End = Entry.Origin + Move * Entry.Trace.Time; Candidate->SetAbsOrigin(End); } //216
			Candidate = EntityWorld.Resolve(Entry.Entity);
			if (!Candidate) continue;
			//10036270 relinks the hierarchy before10035a70's full-scene stationary hull.
			const bool bPenetrating = TestEntityPosition(*Candidate);
			Candidate = EntityWorld.Resolve(Entry.Entity);
			if (!Candidate) continue;
			EntityWorld.EmitRetailSite(*Candidate, TEXT("pusher_check"), TEXT("SpeculativelyCheckPush"), 0x10036270u, TEXT("position"),
				FString::Printf(TEXT("fraction=%.9g startsolid=%d endsolid=%d radius=%.9g"), Entry.Trace.Time, Entry.Trace.bStartPenetrating, bPenetrating, Candidate->CollisionRadius));
			Candidate = EntityWorld.Resolve(Entry.Entity);
			if (!Candidate) continue;
			if (bPenetrating && (bRotation || Entry.Trace.Time != 1.f) && Candidate->CollisionRadius != 0.f
				&& Solid(*Candidate) && !(Candidate->GetSolidFlags() & 0x20))
			{ Blocker = Candidate->Handle; Impact(*Candidate, ComponentEntity(EntityWorld, Entry.Trace.GetComponent())); break; }
		}
		if (Blocker.IsSet())
		{
			if (FElysiumEntity* LiveRoot = EntityWorld.Resolve(RootHandle))
			{
				if (bRotation) LiveRoot->SetAngles(FRotator(SavedLocal.X, SavedLocal.Y, SavedLocal.Z));
				else LiveRoot->SetOrigin(SavedLocal);
			}
			for (int32 Index = Candidates.Num() - 1; Index >= 0; --Index)
				if (auto* Entity = EntityWorld.Resolve(Candidates[Index].Entity)) Entity->SetAbsOrigin(Candidates[Index].Origin);
			return true; //10036e50 returned the captured candidate; deletion cannot undo refusal
		}
		auto Finish = [&](FPushEntry& Entry)
		{
			if (auto* Entity = EntityWorld.Resolve(Entry.Entity)) Entity->PhysicsTouchTriggers(&Entry.Origin);
			if (auto* Entity = EntityWorld.Resolve(Entry.Entity)) Entity->UpdatePhysicsShadowToCurrentPosition(0.f);
		};
		for (int32 Index = Pushers.Num() - 1; Index >= 0; --Index) Finish(Pushers[Index]);
		for (int32 Index = Candidates.Num() - 1; Index >= 0; --Index)
		{
			Finish(Candidates[Index]);
			if (auto* Entity = EntityWorld.Resolve(Candidates[Index].Entity))
			{
				Impact(*Entity, ComponentEntity(EntityWorld, Candidates[Index].Trace.GetComponent()));
				Entity = EntityWorld.Resolve(Candidates[Index].Entity); // callback deletion/reparenting precedes literal rotation reads
				if (!Entity) continue;
				if (bRotation)
				{
					if (FElysiumPlayer* Player = Entity->AsPlayer())
					{
						Entity->SetLocalAngularVelocity(FVector(Entity->AngularVelocity.X, Delta.Y, Entity->AngularVelocity.Z));
						Player->PendingPusherViewMode = 2;
						Player->PendingPusherYaw += static_cast<float>(Delta.Y);
					}
					else { const FVector Angles = Entity->GetAbsAngles() + Delta; FRotator Rotation(Angles.X, Angles.Y, Angles.Z); Entity->SetAbsAngles(Rotation); }
				}
			}
		}
		return false;
	}
}

void FElysiumEntity::Physics_TraceEntity(FElysiumEntity* Entity, const FVector& Start,
	const FVector& End, uint32 Mask, void* Output)
{
	if (!Entity || !Entity->World || !Output) return;
	FElysiumEntityWorld* const EntityWorld = Entity->World;
	const FElysiumEntityHandle Self = Entity->Handle;
	FPusherTrace& Trace = *static_cast<FPusherTrace*>(Output);
	Trace.Hit = FHitResult(); Trace.Hit.Time = 1.f;
	UWorld* NativeWorld = Entity->World->GetOwnerActor() ? Entity->World->GetOwnerActor()->GetWorld() : nullptr;
	const auto Recipe = ElysiumRetailMask::Recipe(Mask);
	if (!NativeWorld || Recipe.bNothing) return;
	FComponentQueryParams Params(SCENE_QUERY_STAT(ElysiumPusherEntityTrace));
	ExcludeEntityBodies(*Entity->World, Params);
	Params.IgnoreMask = ElysiumRetailMask::QueryIgnoreMask(Recipe, false);
	TArray<FHitResult> Hits;
	if (QueryComponent(*Entity))
		NativeWorld->ComponentSweepMultiByChannel(Hits, QueryComponent(*Entity), Start, End, LogicalPose(*Entity).GetRotation(), Recipe.Channel, Params);
	for (const auto& Hit : Hits)
		if (Hit.bBlockingHit && (Hit.Time < Trace.Hit.Time || Hit.bStartPenetrating)) { Trace.Hit = Hit; break; }
	const int32 EncounterEnd = EntityWorld->Entities().Num();
	for (int32 Encounter = 0; Encounter < EncounterEnd; ++Encounter)
	{
		FElysiumEntity* Other = EntityWorld->Entities().IsValidIndex(Encounter) ? EntityWorld->Entities()[Encounter].Get() : nullptr;
		if (!Other || Other == Entity || Trace.Ignore.Contains(Other->Handle)) continue;
		const FElysiumEntityHandle OtherHandle = Other->Handle;
		const bool bAdmitted = Admits(*Entity, *Other, Mask);
		if (EntityWorld->Resolve(Self) != Entity) return;
		Other = EntityWorld->Resolve(OtherHandle);
		if (!bAdmitted || !Other) continue;
		SweepLogicalPair(*Entity, *Other, Start, End, Trace.Hit);
	}
}

bool FElysiumEntity::PhysicsPushMove(float Interval)
{
	LocalTime += Interval; //10038710 before zero local velocity check
	if (Velocity == FVector::ZeroVector) return false;
	FElysiumEntityWorld* const EntityWorld = World;
	const FElysiumEntityHandle Self = Handle;
	const bool bRefused = PerformPush(*this, Velocity * Interval, false, Interval);
	if (bRefused && (!EntityWorld || EntityWorld->Resolve(Self) == this)) LocalTime -= Interval;
	return bRefused;
}

bool FElysiumEntity::PhysicsPushRotate(float Interval)
{
	LocalTime += Interval; //100389e0 before zero angular velocity check
	if (AngularVelocity == FVector::ZeroVector) return false;
	FElysiumEntityWorld* const EntityWorld = World;
	const FElysiumEntityHandle Self = Handle;
	const bool bRefused = PerformPush(*this, AngularVelocity * Interval, true, Interval);
	if (bRefused && (!EntityWorld || EntityWorld->Resolve(Self) == this)) LocalTime -= Interval;
	return bRefused;
}

bool FElysiumEntity::RunPusherThink()
{
	if (IsDead()) return false;
	if (!World) return true;
	FElysiumEntityWorld* const EntityWorld = World;
	const FElysiumEntityHandle Self = Handle;
	const double Now = EntityWorld->NowSeconds();
	if (!ThinkCallback.IsNone() && !IsInert() && NextThink != ELYSIUM_NEVER_THINK && NextThink <= Now + World->FrameSeconds())
	{
		const double ThinkTime = FMath::Max(NextThink, Now);
		NextThink = ELYSIUM_NEVER_THINK;
		const auto Prior = EntityWorld->ExchangePhysicsThinkClock(ThinkTime);
		{
			ON_SCOPE_EXIT { EntityWorld->ExchangePhysicsThinkClock(Prior); };
			ThinkAt(ThinkTime);
			//10033de0 writes the last-think word before restoring curtime and testing deletion.
			if (EntityWorld->Resolve(Self) == this) LastThink = static_cast<float>(ThinkTime);
		}
		if (EntityWorld->Resolve(Self) != this || World != EntityWorld) return false;
	}
	for (int32 Index = 0; Index < ThinkContexts.Num(); ++Index)
	{
		CurrentThinkContext = Index;
		const float Due = ThinkContexts[Index].NextThink;
		if (Due > 0.f && Due <= Now + World->FrameSeconds())
		{
			const float ThinkTime = FMath::Max(Due, static_cast<float>(Now));
			ThinkContexts[Index].NextThink = 0.f;
			const FContextThink Callback = ThinkContexts[Index].Callback;
			const auto Prior = EntityWorld->ExchangePhysicsThinkClock(ThinkTime);
			{
				ON_SCOPE_EXIT { EntityWorld->ExchangePhysicsThinkClock(Prior); };
				if (Callback) Callback(*this, ThinkTime);
				if (EntityWorld->Resolve(Self) == this && ThinkContexts.IsValidIndex(Index)) ThinkContexts[Index].LastThink = ThinkTime;
			}
			if (EntityWorld->Resolve(Self) != this || World != EntityWorld) return false;
		}
		CurrentThinkContext = -1;
		if (EntityWorld->Resolve(Self) != this) return false;
	}
	return true;
}

void FElysiumEntity::PhysicsPusher(float FrameInterval)
{
	FElysiumEntityWorld* const EntityWorld = World;
	const FElysiumEntityHandle Self = Handle;
	auto Alive = [&]() { return !EntityWorld || (EntityWorld->Resolve(Self) == this && World == EntityWorld); };
	if (!RunPusherThink()) return;
	float Interval = FrameInterval + LocalTime <= PhysicsMoveDoneTime ? FrameInterval : FMath::Max(PhysicsMoveDoneTime - LocalTime, 0.f);
	void* Object = VPhysicsGetObject(); //captured across callbacks/transaction, independently of current field
	const auto CapturedObject = ElysiumModelPhysics::CaptureIdentity(Object);
	if (Interval != 0.f)
	{
		Interval = Slot135(Interval);
		if (!Alive()) return;
		if (AngularVelocity == FVector::ZeroVector) PhysicsPushMove(Interval);
		else if (GetPusherAbsoluteVelocity() == FVector::ZeroVector) PhysicsPushRotate(Interval);
		else if (PhysicsPushRotate(Interval))
		{
			if (!Alive()) return;
			const float Previous = LocalTime;
			LocalTime = StartFrameLocalTime;
			PhysicsPushMove(Interval);
			if (!Alive()) return;
			LocalTime = FMath::Max(LocalTime, Previous);
		}
		if (!Alive()) return;
		if (Object)
		{
			if (IsDead() || !ElysiumModelPhysics::IsCurrentOutput(CapturedObject)) return;
			const FVector TargetAngles = GetAbsAngles(); const FVector TargetOrigin = GetAbsOrigin();
			ElysiumModelPhysics::UpdateShadow(Object, TargetOrigin, ElysiumModelPhysics::SourceRotation(TargetAngles), false, Interval);
			return;
		}
	}
	if (!Object && PhysicsMoveDoneTime <= LocalTime && PhysicsMoveDoneTime > 0.f)
	{ PhysicsMoveDoneTime = 0.f; MoveDone(); }
}

void FElysiumEntity::PhysicsParent(float FrameInterval)
{
	FElysiumEntityWorld* const EntityWorld = World;
	const FElysiumEntityHandle Self = Handle;
	auto Alive = [&]() { return !EntityWorld || (EntityWorld->Resolve(Self) == this && World == EntityWorld); };
	if (!RunPusherThink()) return;
	StartFrameLocalTime = LocalTime;
	const float IntervalWord = FrameInterval + LocalTime <= PhysicsMoveDoneTime ? FrameInterval : PhysicsMoveDoneTime - LocalTime;
	//10039bb6 admits nonnegative and unordered;10039cc4 rejects equal zero only.
	if (!(IntervalWord < 0.f) && IntervalWord != 0.f)
	{
		const float Interval = Slot135(IntervalWord);
		if (!Alive()) return;
		if (AngularVelocity == FVector::ZeroVector) PhysicsPushMove(Interval);
		else if (GetPusherAbsoluteVelocity() == FVector::ZeroVector) PhysicsPushRotate(Interval);
		else if (PhysicsPushRotate(Interval))
		{
			if (!Alive()) return;
			const float Previous = LocalTime; LocalTime = StartFrameLocalTime;
			PhysicsPushMove(Interval);
			if (!Alive()) return;
			LocalTime = FMath::Max(LocalTime, Previous);
		}
		if (!Alive()) return;
		if (void* Object = VPhysicsGetObject())
		{
			const FVector TargetAngles = GetAbsAngles(); const FVector TargetOrigin = GetAbsOrigin();
			ElysiumModelPhysics::UpdateShadow(Object, TargetOrigin, ElysiumModelPhysics::SourceRotation(TargetAngles), false, Interval);
			return;
		}
	}
	if (PhysicsMoveDoneTime <= LocalTime) { PhysicsMoveDoneTime = 0.f; MoveDone(); }
}

bool FElysiumEntity::InitBrushPhysicsShadow()
{
	if (!World || !Body || !Def || !bEdictAttached || IsRetailNotSolid()) return false;
	VPhysicsDestroyObject(); //InitSetup100a5ad0; Relink100b3b00 is profiling only
	FElysiumPhysicsBodyRecipe Recipe;
	Recipe.Kind = EElysiumPhysicsRecipeKind::Brush;
	Recipe.BrushSolidity = static_cast<uint8>(Body->GetSolidity());
	Recipe.BrushContentsSignature = Body->GetContentsSignature();
	//PhysModelParseSolid1002e210 copies1046ecb4; a brush has no studio solid block.
	for (const FElysiumConvexHull& Hull : Def->Hulls) Recipe.BrushHulls.Add(Hull.Vertices);
	FString Error;
	if (!ElysiumModelPhysics::CreateBrush(Body, World->PhysicsSurfaces(), Recipe, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("%s brush shadow creation: %s"), *DebugString(), *Error);
		return false;
	}
	void* Object = ElysiumModelPhysics::ActorIdentity(Body);
	ElysiumModelPhysics::SetGameData(Object, *this);
	VPhysicsSetObject(Object);
	ElysiumModelPhysics::SetShadow(Object, 10000.f, 10000.f, false, false);
	ElysiumModelPhysics::UpdateShadow(Object, GetAbsOrigin(), ElysiumModelPhysics::SourceRotation(GetAbsAngles()), false, 0.f);
	World->AddPhysicsShadow(*this);
	return true;
}

namespace
{
	FVector FloatWords(const FVector& V)
	{
		return FVector(static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z));
	}
	FVector4f SourceQuaternion(const FVector& Angles)
	{
		//1013b1f0 ->1013b390: radians and sin/cos terms are float stores.
		const FVector Radians = FloatWords(FloatWords(Angles) * .01745329238474369f);
		const float Sr = FMath::Sin(static_cast<float>(Radians.Z) * .5f), Cr = FMath::Cos(static_cast<float>(Radians.Z) * .5f);
		const float Sp = FMath::Sin(static_cast<float>(Radians.X) * .5f), Cp = FMath::Cos(static_cast<float>(Radians.X) * .5f);
		const float Sy = FMath::Sin(static_cast<float>(Radians.Y) * .5f), Cy = FMath::Cos(static_cast<float>(Radians.Y) * .5f);
		const float A = Sr * Cp, B = Cr * Sp, C = Cr * Cp, D = Sr * Sp;
		return FVector4f(A * Cy - B * Sy, B * Cy + A * Sy, C * Sy - D * Cy, C * Cy + D * Sy);
	}
	float SourceDeltaAngle(const FVector& Solved, const FVector& Logical)
	{
		const FVector4f First = SourceQuaternion(Solved);
		FVector4f Second = SourceQuaternion(Logical);
		//QuaternionScale1013add0(-1), including its epsilon and sign of w.
		const float SinAngle = FMath::Min(FMath::Sqrt(First.Z * First.Z + First.Y * First.Y + First.X * First.X), 1.f);
		const double ScaledSin = -FMath::Sin(FMath::Asin(static_cast<double>(SinAngle)));
		const double Scale = ScaledSin / (SinAngle + 1.1920928955078125e-7);
		FVector4f Inverse(static_cast<float>(Scale * First.X), static_cast<float>(Scale * First.Y), static_cast<float>(Scale * First.Z),
			static_cast<float>(FMath::Sqrt(FMath::Max(1. - ScaledSin * ScaledSin, 0.)) * (First.W < 0.f ? -1. : 1.)));
		//QuaternionAlign1013a9f0 precedes QuaternionMult1013af60.
		double Difference = 0., Sum = 0.;
		for (int32 Axis = 0; Axis < 4; ++Axis)
		{ Difference += FMath::Square(static_cast<double>(Inverse[Axis]) - Second[Axis]); Sum += FMath::Square(static_cast<double>(Inverse[Axis]) + Second[Axis]); }
		if (Sum < Difference) Second *= -1.f;
		FVector4f Result(
			Second.X * Inverse.W + ((Second.W * Inverse.X + Second.Z * Inverse.Y) - Second.Y * Inverse.Z),
			Second.Y * Inverse.W + Second.W * Inverse.Y + (Second.X * Inverse.Z - Second.Z * Inverse.X),
			Second.Z * Inverse.W + Second.W * Inverse.Z + (Second.Y * Inverse.X - Second.X * Inverse.Y),
			Second.W * Inverse.W - (Second.X * Inverse.X + Second.Z * Inverse.Z + Second.Y * Inverse.Y));
		const float LengthSquared = Result.W * Result.W + Result.Z * Result.Z + Result.Y * Result.Y + Result.X * Result.X;
		if (LengthSquared != 0.f) Result *= 1.f / FMath::Sqrt(LengthSquared); //1013ad40
		float Angle = static_cast<float>(2. * FMath::Acos(static_cast<double>(Result.W)) * 57.29578f);
		if (Angle > 180.f) Angle -= 360.f;
		return Angle;
	}
}

//100a55b0. This is game feedback around a Chaos-owned pose, never another solve.
void FElysiumEntity::VPhysicsUpdatePusher(void* Object)
{
	FElysiumEntityWorld* const EntityWorld = World;
	const FElysiumEntityHandle Self = Handle;
	const float Interval = LocalTime - StartFrameLocalTime;
	if (!(Interval > 0.f)) return;
	float CorrectedTime = LocalTime;
	FVector SolvedOrigin; FQuat SolvedRotation;
	if (ElysiumModelPhysics::GetShadowPosition(Object, &SolvedOrigin, &SolvedRotation))
	{
		if (Velocity != FVector::ZeroVector)
		{
			const FVector Delta = FloatWords(SolvedOrigin / 2.54) - FloatWords(GetAbsOrigin() / 2.54);
			const double DistanceSquared = Delta.Z * Delta.Z + Delta.X * Delta.X + Delta.Y * Delta.Y;
			if (DistanceSquared > 1.)
			{
				const float Distance = FMath::Sqrt(static_cast<float>(DistanceSquared));
				const FVector SpeedWords = FloatWords(Velocity / 2.54);
				//577a multiplies speed by interval BEFORE577e divides distance. Retail bug.
				const double Seconds = Distance / (FMath::Sqrt(static_cast<double>(static_cast<float>(SpeedWords.SizeSquared()))) * Interval);
				CorrectedTime = static_cast<float>(LocalTime - FMath::Clamp(Seconds, 0., static_cast<double>(Interval)));
			}
		}
		if (AngularVelocity != FVector::ZeroVector)
		{
			FRotator SolvedAngles; FVector UnusedOrigin;
			ElysiumModelPhysics::GetPosition(Object, UnusedOrigin, SolvedAngles); //47 shares26001310 Euler converter with37
			const float Angle = SourceDeltaAngle(FVector(SolvedAngles.Pitch, SolvedAngles.Yaw, SolvedAngles.Roll), GetAbsAngles());
			if (FMath::Abs(Angle) > .5f) CorrectedTime = StartFrameLocalTime;
		}
		const float Rollback = LocalTime - CorrectedTime;
		if (Rollback > 0.f)
		{
			const FVector OriginWord = GetOrigin(); //220 before221
			const FVector AngleWord = GetAngles();
			SetOrigin(FloatWords(FloatWords(OriginWord / 2.54) - FloatWords(Velocity / 2.54) * Rollback) * 2.54); //62 before64
			const FVector RolledAngles = FloatWords(AngleWord - AngularVelocity * Rollback);
			SetAngles(FRotator(RolledAngles.X, RolledAngles.Y, RolledAngles.Z));
			//100b3b00 Relink is empty profiling. No touch/child target publication here.
			if (CorrectedTime <= StartFrameLocalTime)
			{
				void* ContactObject = nullptr;
				if (ElysiumModelPhysics::GetContactPoint(Object, ContactObject))
				{
					FElysiumEntity* Other = ElysiumModelPhysics::GameDataForObject(ContactObject);
					if (EntityWorld) EntityWorld->EmitRetailSite(*this, TEXT("pusher_blocked"), TEXT("VPhysicsUpdatePusher"), 0x100a55b0u, TEXT("dispatch"),
						FString::Printf(TEXT("other=%s local=%.9g corrected=%.9g start=%.9g"), *FElysiumEntityWorld::AiTraceName(Other), LocalTime, CorrectedTime, StartFrameLocalTime));
					Blocked(Other); //178 before LocalTime write
				}
				if (EntityWorld && (EntityWorld->Resolve(Self) != this || World != EntityWorld)) return;
			}
			LocalTime = CorrectedTime; //callback clock writes overwritten by the captured value
		}
	}
	StartFrameLocalTime = LocalTime;
	//5983 JP skips greater/unordered. Native feedback admits equality and negative nonzero.
	if (PhysicsMoveDoneTime <= LocalTime && PhysicsMoveDoneTime != 0.f)
	{
		PhysicsMoveDoneTime = 0.f;
		MoveDone();
	}
}

void FElysiumEntity::Blocked(FElysiumEntity* Other)
{
	//100a4d40: callback can delete/reparent; resolve the live serial parent after it.
	FElysiumEntityWorld* EntityWorld = World;
	const FElysiumEntityHandle Self = Handle;
	FElysiumEntityWorld* const OtherWorld = Other ? Other->World : nullptr;
	const FElysiumEntityHandle OtherHandle = Other ? Other->Handle : FElysiumEntityHandle();
	if (BlockedCallback) BlockedCallback(*this, Other);
	if (EntityWorld && EntityWorld->Resolve(Self) == this && World == EntityWorld)
		if (FElysiumEntity* Parent = EntityWorld->Resolve(ParentHandle)) //100a4dbe: +254
			Parent->Blocked(OtherWorld ? OtherWorld->Resolve(OtherHandle) : nullptr);
}

void FElysiumEntity::StartTouch(FElysiumEntity* Other)
{
	//100a49d0: authored parent (+254), independent of the current move hierarchy (+25c).
	if (World)
		if (FElysiumEntity* Parent = World->Resolve(ParentHandle)) Parent->StartTouch(Other);
}

void FElysiumEntity::Touch(FElysiumEntity* Other)
{
	FElysiumEntityWorld* const EntityWorld = World;
	const FElysiumEntityHandle Self = Handle;
	FElysiumEntityWorld* const OtherWorld = Other ? Other->World : nullptr;
	const FElysiumEntityHandle OtherHandle = Other ? Other->Handle : FElysiumEntityHandle();
	//100a4af0 reads the live parent only AFTER the installed callback.
	if (TouchCallback) TouchCallback(*this, Other);
	if (EntityWorld && EntityWorld->Resolve(Self) == this && World == EntityWorld)
		if (FElysiumEntity* Parent = EntityWorld->Resolve(ParentHandle))
			Parent->Touch(OtherWorld ? OtherWorld->Resolve(OtherHandle) : nullptr);
}
