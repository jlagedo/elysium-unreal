#include "Substrate/ElysiumNpcSabbatLeader.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcSabbatLeader::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VSabbatLeader"));
	return Row;
}
