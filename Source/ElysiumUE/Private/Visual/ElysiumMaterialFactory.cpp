#include "Visual/ElysiumMaterialFactory.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UMaterialInstanceDynamic* FElysiumMaterialFactory::Create(UMaterialInterface* Imported, UObject* Outer)
{
	if (Imported == nullptr)
	{
		return nullptr;
	}
	return UMaterialInstanceDynamic::Create(Imported, Outer);
}
