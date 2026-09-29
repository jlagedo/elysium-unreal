#include "Substrate/ElysiumNpcSightTrace.h"

#include "ElysiumWorldServices.h"

namespace ElysiumNpcSight
{
	bool Visible(const IElysiumEmbodiment& Embodiment, const FVisibleQuery& Query,
		FElysiumEntityHandle* OutBlocker)
	{
		// Wave 1 stub: the brush-only answer every sight caller reads today, so behaviour is unchanged
		// until lane B folds `TraceRetail`'s character list in under the rule above. A block here is
		// always the world's.
		if (Embodiment.QueryLineOfSight(Query.EyeCm, Query.TargetCm))
		{
			return true;
		}
		if (OutBlocker != nullptr)
		{
			*OutBlocker = FElysiumEntityHandle::Invalid();
		}
		return false;
	}
}
