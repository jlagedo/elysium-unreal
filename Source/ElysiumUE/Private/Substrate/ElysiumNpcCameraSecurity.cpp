#include "Substrate/ElysiumNpcCameraSecurity.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcCameraSecurity::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VCameraSecurity"));
	return Row;
}

// Slot 201: `0x10369ff0`
bool FElysiumNpcCameraSecurity::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	return CameraSecurityFVisible(SeenTarget);
}

// Slot 468: `0x1036a030`
bool FElysiumNpcCameraSecurity::QuerySeeEntity(FElysiumEntity* Candidate)
{
	return Candidate != nullptr && CameraSecurityQuerySeeEntity(*Candidate);
}
