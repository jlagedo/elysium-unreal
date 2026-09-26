#include "Substrate/ElysiumNpcGargoyle.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcGargoyle::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VGargoyle"));
	return Row;
}
