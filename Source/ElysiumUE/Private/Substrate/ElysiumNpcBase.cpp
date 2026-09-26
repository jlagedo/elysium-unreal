#include "Substrate/ElysiumNpcBase.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

// --- The leaf's own answer ----------------------------------------------------------------------

const FElysiumNpcClass* FElysiumNpcBase::RetailClass() const
{
	// The C++ class is the answer: the classname's factory built it (story 5 step 2), so nothing
	// here reads the classname. The test latch stands only for the enumerated deferred classes.
	return bRetailClassForTests ? RetailClassForTests : OwnRetailClass();
}

const FElysiumNpcClass* FElysiumNpcBase::OwnRetailClass() const
{
	return nullptr;
}

bool FElysiumNpcBase::IsRetailClass(const TCHAR* RetailClassName) const
{
	return ElysiumNpcKernelClass::DerivesFrom(RetailClass(), RetailClassName);
}

bool FElysiumNpcBase::OwnRetailClassDerivesFrom(const TCHAR* RetailClassName) const
{
	return ElysiumNpcKernelClass::DerivesFrom(OwnRetailClass(), RetailClassName);
}
