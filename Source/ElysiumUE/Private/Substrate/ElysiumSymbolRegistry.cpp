#include "Substrate/ElysiumSymbolRegistry.h"

namespace
{
	FString FoldSymbol(const FString& Name)
	{
		return Name.ToLower();
	}
}

int32 FElysiumSymbolRegistry::Intern(const FString& Name)
{
	const FString Folded = FoldSymbol(Name);
	if (const int32* Existing = Ids.Find(Folded))
	{
		return *Existing;
	}

	const int32 Id = Names.Add(Name);
	Ids.Add(Folded, Id);
	++NumInterned;
	return Id;
}

int32 FElysiumSymbolRegistry::Find(const FString& Name) const
{
	const int32* Existing = Ids.Find(FoldSymbol(Name));
	return Existing != nullptr ? *Existing : INDEX_NONE;
}

const FString* FElysiumSymbolRegistry::NameOf(int32 Id) const
{
	return Names.IsValidIndex(Id) ? &Names[Id] : nullptr;
}

void FElysiumSymbolRegistry::Reset()
{
	Ids.Reset();
	Names.Reset();
	NumInterned = 0;
}
