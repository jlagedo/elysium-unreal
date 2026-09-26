#include "Substrate/ElysiumNpcMingXiaoTentacle.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcMingXiaoTentacle::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VMingXiaoTentacle"));
	return Row;
}
