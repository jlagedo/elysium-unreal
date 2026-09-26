#include "Substrate/ElysiumNpcChangBrosClaw.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcChangBrosClaw::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VChangBrosClaw"));
	return Row;
}
