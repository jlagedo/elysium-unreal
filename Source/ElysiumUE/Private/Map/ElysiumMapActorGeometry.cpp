#include "ElysiumMapActor.h"

// 0018 story 6: the map actor's half of the geometry seam. Wave 1 fixes the contract only: every
// body below answers today's default (no collision world, no actor, no entity) until lane A wires
// `TraceRetail` through `ElysiumWorldGeometry::Trace`.

bool AElysiumMapActor::TraceRetail(const FElysiumRetailTrace& Trace, FElysiumRetailTraceResult& Out) const
{
	// The interface's stated headless answer: nothing traced, the result left clear.
	Out.EndPosCm = Trace.EndCm;
	return false;
}

AActor* AElysiumMapActor::ResolveQueryActor(const FElysiumEntityHandle& Entity) const
{
	return nullptr;
}

FElysiumEntityHandle AElysiumMapActor::HandleForActor(const AActor* Actor) const
{
	return FElysiumEntityHandle::Invalid();
}
