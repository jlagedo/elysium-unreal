#include "ElysiumMapActor.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumWorldCollisionActor.h"
#include "Map/ElysiumWorldGeometry.h"
#include "Visual/ElysiumNpcBody.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

// 0018 story 6: the map actor's half of the geometry seam. It resolves the two translations the
// Unreal queries cannot make for themselves -- which actor stands for an entity, and which entity
// stands behind a hit actor -- and hands the trace to `ElysiumWorldGeometry::Trace`.

bool AElysiumMapActor::TraceRetail(const FElysiumRetailTrace& Trace, FElysiumRetailTraceResult& Out) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		// The interface's stated headless answer: nothing traced, the result left clear with
		// `tr.endpos` at the trace's end.
		Out.EndPosCm = Trace.EndCm;
		return false;
	}
	// The pass entity's own actor is excluded up front. Every `Ignore` handle -- the pass entity
	// again, the second entity, and a brush entity whose body is a component of THIS actor and so
	// has no actor of its own -- is also refused per hit by handle inside the trace.
	const AActor* IgnoreSelf = Trace.Ignore.Num() > 0 ? ResolveQueryActor(Trace.Ignore[0]) : nullptr;
	return ElysiumWorldGeometry::Trace(*World, Trace, Out, IgnoreSelf,
		[this](const AActor* Actor) { return HandleForActor(Actor); });
}

AActor* AElysiumMapActor::ResolveQueryActor(const FElysiumEntityHandle& Entity) const
{
	if (!Entity.IsSet())
	{
		return nullptr;
	}
	// The player: its pawn is its collision.
	if (EntityWorld && Entity == EntityWorld->PlayerHandle())
	{
		return ResolvePlayerPawn();
	}
	// An NPC: its body actor (the capsule is the collision).
	for (AElysiumNpcBody* Body : NpcMotors)
	{
		if (Body != nullptr && Body->GetOwningEntity() == Entity)
		{
			return Body;
		}
	}
	// Nothing else has an actor of its own: a brush entity's body and a runtime prop's component
	// belong to THIS actor, which stands for every one of them, so they answer null here and are
	// refused inside the trace by handle (the brush component carries its own).
	return nullptr;
}

FElysiumEntityHandle AElysiumMapActor::HandleForActor(const AActor* Actor) const
{
	if (Actor == nullptr || Actor == this || Actor->IsA<AElysiumWorldCollisionActor>())
	{
		// The static world, or this actor (whose components the trace names by component).
		return FElysiumEntityHandle::Invalid();
	}
	// An NPC body carries its owning entity.
	if (const AElysiumNpcBody* NpcBody = Cast<AElysiumNpcBody>(Actor))
	{
		return NpcBody->GetOwningEntity();
	}
	// The player pawn carries the player entity.
	if (const IElysiumPlayerBody* PlayerBody = Cast<IElysiumPlayerBody>(Actor))
	{
		return PlayerBody->GetPlayerEntity();
	}
	// Classified, never walked: an NPC body and the player pawn are the only actors that carry a
	// handle, and a brush entity's body is named by its own component before this is asked.
	// Everything else -- a baked static prop's `StaticMeshActor` or placed model, the world
	// collision -- is no entity (runtime entity props are components of THIS actor).
	return FElysiumEntityHandle::Invalid();
}
