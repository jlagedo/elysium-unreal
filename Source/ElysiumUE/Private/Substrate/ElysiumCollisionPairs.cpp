#include "Substrate/ElysiumCollisionPairs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumEntity.h"

namespace
{
	bool Same(ElysiumModelPhysics::FNativeOutput A, ElysiumModelPhysics::FNativeOutput B)
	{ return A.Actor == B.Actor && A.Incarnation == B.Incarnation; }
}
bool FElysiumCollisionPairs::ContainsEntities(FElysiumEntityHandle First, FElysiumEntityHandle Second) const
{
	if ((First.IsSet() && !World.Resolve(First)) || (Second.IsSet() && !World.Resolve(Second))) return false;
	return Entities.ContainsByPredicate([&](const FEntityPair& Pair)
	{ return (Pair.First == First && Pair.Second == Second) || (Pair.First == Second && Pair.Second == First); });
}
bool FElysiumCollisionPairs::ContainsObjects(void* First, void* Second) const
{
	if (ContainsRawObjects(First,Second)) return true;
	const auto A = ElysiumModelPhysics::CaptureIdentity(First), B = ElysiumModelPhysics::CaptureIdentity(Second);
	if (!ElysiumModelPhysics::IsCurrentOutput(A) || !ElysiumModelPhysics::IsCurrentOutput(B)) return false;
	const auto* AOwner = ElysiumModelPhysics::GameDataFor(A, World);
	const auto* BOwner = ElysiumModelPhysics::GameDataFor(B, World);
	return ContainsEntities(AOwner ? AOwner->Handle : FElysiumEntityHandle::Invalid(), BOwner ? BOwner->Handle : FElysiumEntityHandle::Invalid());
}
bool FElysiumCollisionPairs::ContainsRawObjects(void* First, void* Second) const
{
	const auto A = ElysiumModelPhysics::CaptureIdentity(First), B = ElysiumModelPhysics::CaptureIdentity(Second);
	if (!ElysiumModelPhysics::IsCurrentOutput(A) || !ElysiumModelPhysics::IsCurrentOutput(B)) return false;
	return Objects.ContainsByPredicate([&](const FObjectPair& Pair)
		{ return (Same(Pair.First,A) && Same(Pair.Second,B)) || (Same(Pair.First,B) && Same(Pair.Second,A)); });
}
bool FElysiumCollisionPairs::Mutate(void* First, void* Second, bool bDirect, bool bAdd)
{
	const auto A = ElysiumModelPhysics::CaptureIdentity(First), B = ElysiumModelPhysics::CaptureIdentity(Second);
	if (!ElysiumModelPhysics::IsCurrentOutput(A) || !ElysiumModelPhysics::IsCurrentOutput(B)) return false;
	if (bDirect)
	{
		const int32 At = Objects.IndexOfByPredicate([&](const FObjectPair& Pair)
		{ return (Same(Pair.First,A) && Same(Pair.Second,B)) || (Same(Pair.First,B) && Same(Pair.Second,A)); });
		if (bAdd && At == INDEX_NONE) Objects.Add({A,B});
		else if (!bAdd && At != INDEX_NONE) Objects.RemoveAt(At);
	}
	else
	{
		const auto* AOwner = ElysiumModelPhysics::GameDataFor(A, World);
		const auto* BOwner = ElysiumModelPhysics::GameDataFor(B, World);
		const FElysiumEntityHandle AH = AOwner ? AOwner->Handle : FElysiumEntityHandle::Invalid();
		const FElysiumEntityHandle BH = BOwner ? BOwner->Handle : FElysiumEntityHandle::Invalid();
		const int32 At = Entities.IndexOfByPredicate([&](const FEntityPair& Pair)
		{ return (Pair.First == AH && Pair.Second == BH) || (Pair.First == BH && Pair.Second == AH); });
		if (bAdd && At == INDEX_NONE) Entities.Add({AH,BH});
		else if (!bAdd && At != INDEX_NONE) Entities.RemoveAt(At);
	}
	//10158ab0: first then second nonstatic object25, after the membership write.
	ElysiumModelPhysics::RecheckCollisionFilter(A);
	ElysiumModelPhysics::RecheckCollisionFilter(B);
	//A manager prefilter would precede1002f1e0's null/sameGameData/trigger early returns.
	//Release only our former contribution; actual candidates read membership in retail order.
	ElysiumModelPhysics::SetNativePairIgnored(A, B, false);
	return true;
}
bool FElysiumCollisionPairs::AddTranslated(void* A, void* B) { return Mutate(A,B,false,true); }
bool FElysiumCollisionPairs::RemoveTranslated(void* A, void* B) { return Mutate(A,B,false,false); }
bool FElysiumCollisionPairs::AddDirect(void* A, void* B) { return Mutate(A,B,true,true); }
bool FElysiumCollisionPairs::RemoveDirect(void* A, void* B) { return Mutate(A,B,true,false); }
void FElysiumCollisionPairs::Reset()
{
	// The restore transaction clears construction membership; actual owner Activate republishes.
	const auto Components = ElysiumModelPhysics::WorldComponents(World);
	for (int32 A = 0; A < Components.Num(); ++A)
		for (int32 B = A + 1; B < Components.Num(); ++B)
			ElysiumModelPhysics::SetNativePairIgnored(ElysiumModelPhysics::CaptureIdentity(ElysiumModelPhysics::ActorIdentity(Components[A])),
					ElysiumModelPhysics::CaptureIdentity(ElysiumModelPhysics::ActorIdentity(Components[B])), false);
	Entities.Reset(); Objects.Reset();
}
