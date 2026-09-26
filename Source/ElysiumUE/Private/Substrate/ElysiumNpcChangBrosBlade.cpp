#include "Substrate/ElysiumNpcChangBrosBlade.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcChangBrosBlade::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VChangBrosBlade"));
	return Row;
}
