#pragma once

#include "CoreMinimal.h"

// 0x1008dc40 / 0x1008dd30: ordered sequence rows with their raw actweights.
namespace ElysiumAnimationPick
{
	struct FCandidate
	{
		int32 Sequence = INDEX_NONE;
		int32 Weight = 0;
	};

	int32 Weighted(TConstArrayView<FCandidate> Candidates);
	int32 Heaviest(TConstArrayView<FCandidate> Candidates);
}
