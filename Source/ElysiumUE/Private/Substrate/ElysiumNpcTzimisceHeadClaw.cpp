#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcTzimisceHeadClaw::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VTzimisceHeadClaw"));
	return Row;
}
