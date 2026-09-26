#include "Substrate/ElysiumNpcHumanCombatPatrol.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcHumanCombatPatrol::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VHumanCombatPatrol"));
	return Row;
}
