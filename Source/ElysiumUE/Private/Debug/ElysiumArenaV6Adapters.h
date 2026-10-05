#pragma once
#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
class FElysiumEntityWorld;
struct FElysiumArenaScenario;
struct FElysiumArenaAction;
struct FElysiumArenaValue;
struct FElysiumMapSnapshot;
bool ElysiumArenaReadV6Witness(FElysiumEntityWorld&, const FString&, const FString&, FElysiumArenaValue&, FString&);
bool ElysiumArenaRunV6Fixture(FElysiumEntityWorld&, const FElysiumArenaAction&, FString&);
bool ElysiumArenaValidateV6Admission(FElysiumEntityWorld&, const FElysiumArenaScenario&, FString&);
bool ElysiumArenaCorruptV6Header(FElysiumMapSnapshot&, const FString&, FString&);
void ElysiumArenaInstallV6Adapters();
#endif
