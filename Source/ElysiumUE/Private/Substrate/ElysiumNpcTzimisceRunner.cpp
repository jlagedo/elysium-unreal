#include "Substrate/ElysiumNpcTzimisceRunner.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcTzimisceRunner::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VTzimisceRunner"));
	return Row;
}
