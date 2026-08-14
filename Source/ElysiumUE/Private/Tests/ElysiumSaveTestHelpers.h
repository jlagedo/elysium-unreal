#pragma once

#include "CoreMinimal.h"

class FElysiumEntity;
struct FElysiumEntityDefs;
struct FElysiumMapSnapshot;

namespace ElysiumSaveTestHelpers
{
FElysiumEntityDefs MakeSaveTestDefs();
TArray<uint8> ElysiumSaveDigest(const FElysiumMapSnapshot& Snapshot);
float SaveTestCounterValue(const FElysiumEntity* Entity);
}
