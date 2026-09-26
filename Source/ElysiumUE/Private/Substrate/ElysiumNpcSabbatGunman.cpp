#include "Substrate/ElysiumNpcSabbatGunman.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcSabbatGunman::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VSabbatGunman"));
	return Row;
}
