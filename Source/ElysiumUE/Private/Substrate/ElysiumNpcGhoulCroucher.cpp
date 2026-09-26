#include "Substrate/ElysiumNpcGhoulCroucher.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcGhoulCroucher::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VGhoulCroucher"));
	return Row;
}
