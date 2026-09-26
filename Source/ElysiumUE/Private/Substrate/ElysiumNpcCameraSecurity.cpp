#include "Substrate/ElysiumNpcCameraSecurity.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcCameraSecurity::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VCameraSecurity"));
	return Row;
}
