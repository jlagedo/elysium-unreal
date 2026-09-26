#include "Substrate/ElysiumNpcAndreiBlood.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcAndreiBlood::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VAndreiBlood"));
	return Row;
}
