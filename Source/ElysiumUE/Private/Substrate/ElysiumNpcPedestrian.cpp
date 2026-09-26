#include "Substrate/ElysiumNpcPedestrian.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcPedestrian::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VPedestrian"));
	return Row;
}
