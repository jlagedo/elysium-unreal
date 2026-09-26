#include "Substrate/ElysiumNpcAsianVampire.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcAsianVampire::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VAsianVampire"));
	return Row;
}
