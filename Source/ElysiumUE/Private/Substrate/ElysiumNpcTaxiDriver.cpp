#include "Substrate/ElysiumNpcTaxiDriver.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcTaxiDriver::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VTaxiDriver"));
	return Row;
}
