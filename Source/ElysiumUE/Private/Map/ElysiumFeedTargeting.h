#pragma once

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"

// Engine-side identity rule for the ordinary feed hull's occlusion ray. The skeletal visual is
// attached below AElysiumNpcBody's blocking capsule, so a trace that reaches that ancestor has hit
// the intended victim rather than a wall. Kept private to the map/automation layer: entity logic
// still sees only IElysiumEmbodiment::QueryFeedTarget().
namespace ElysiumFeedTargeting
{
	inline bool HitBelongsToCandidate(const UPrimitiveComponent* Hit,
		const USceneComponent* CandidateBody)
	{
		return Hit && CandidateBody
			&& (Hit == CandidateBody || CandidateBody->IsAttachedTo(Hit));
	}
}
