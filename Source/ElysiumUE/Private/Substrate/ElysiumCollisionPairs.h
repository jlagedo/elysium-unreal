#pragma once
#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "Substrate/ElysiumModelPhysics.h"
class FElysiumEntityWorld;

//10723948: level-owned observable membership. Producers own persistence, not this table.
class FElysiumCollisionPairs
{
public:
	explicit FElysiumCollisionPairs(FElysiumEntityWorld& InWorld) : World(InWorld) {}
	bool ContainsEntities(FElysiumEntityHandle First, FElysiumEntityHandle Second) const;
	bool ContainsObjects(void* First, void* Second) const;
	bool ContainsRawObjects(void* First, void* Second) const; //1002f1e0 raw-key arms, no object6 translation
	bool AddTranslated(void* First, void* Second); //10158b40, object6 -> entity keys
	bool RemoveTranslated(void* First, void* Second); //10158af0
	bool AddDirect(void* First, void* Second); //10158c00, native keys
	bool RemoveDirect(void* First, void* Second); //10158bc0
	void Reset();
	int32 Num() const { return Entities.Num() + Objects.Num(); }
private:
	struct FEntityPair { FElysiumEntityHandle First, Second; };
	struct FObjectPair { ElysiumModelPhysics::FNativeOutput First, Second; };
	FElysiumEntityWorld& World;
	TArray<FEntityPair> Entities;
	TArray<FObjectPair> Objects;
	bool Mutate(void* First, void* Second, bool bDirect, bool bAdd);
};
