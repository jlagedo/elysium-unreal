#include "Substrate/ElysiumNpcHumanCombatant.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcHumanCombatant::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VHumanCombatant"));
	return Row;
}
