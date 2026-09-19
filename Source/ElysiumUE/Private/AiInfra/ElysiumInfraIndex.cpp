#include "AiInfra/ElysiumInfraIndex.h"

#include "Components/SceneComponent.h"

AElysiumInfraIndex::AElysiumInfraIndex(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
}

bool AElysiumInfraIndex::ConfigureDeclaredSet(const TArray<int32>& Indices, const TArray<FName>& Families)
{
	if (Indices.Num() != Families.Num())
	{
		return false;
	}
	DeclaredIndices = Indices;
	DeclaredFamilies = Families;
	return true;
}

void AElysiumInfraIndex::SetStageSha256(const FString& InSha256)
{
	StageSha256 = InSha256;
}

int32 AElysiumInfraIndex::CountOf(FName Family) const
{
	int32 Count = 0;
	for (const FName& Each : DeclaredFamilies)
	{
		Count += Each == Family ? 1 : 0;
	}
	return Count;
}
