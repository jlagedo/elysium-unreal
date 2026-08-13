#pragma once

#include "ElysiumMoveSolve.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"

// Engine-side identity rule for the ordinary feed hull's occlusion ray. The skeletal visual is
// attached below AElysiumNpcBody's blocking capsule, so a trace that reaches that ancestor has hit
// the intended victim rather than a wall. Kept private to the map/automation layer: entity logic
// still sees only IElysiumEmbodiment::QueryFeedTarget().
namespace ElysiumFeedTargeting
{
	struct FProbe
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		FVector HullExtent = FVector::ZeroVector;
	};

	// `CBasePlayer::Replenish` traces a 16-unit cube from the player's eye/use origin toward the
	// local (32 forward, 0 right, -32 vertical) endpoint. Keep this separate from the final rendered
	// POV: a third-person camera is behind the pawn and would reverse which nearby victim is reached.
	inline FProbe MakeProbe(const FVector& UseOrigin, const FRotator& ControlRotation)
	{
		constexpr float ForwardUnits = 32.0f;
		constexpr float VerticalUnits = -32.0f;
		constexpr float HullHalfUnits = 8.0f;

		FProbe Probe;
		Probe.Start = UseOrigin;
		Probe.End = UseOrigin
			+ ControlRotation.Vector().GetSafeNormal() * (ForwardUnits * ElysiumMove::U)
			+ FVector::UpVector * (VerticalUnits * ElysiumMove::U);
		Probe.HullExtent = FVector(HullHalfUnits * ElysiumMove::U);
		return Probe;
	}

	inline bool HitBelongsToCandidate(const UPrimitiveComponent* Hit,
		const USceneComponent* CandidateBody)
	{
		return Hit && CandidateBody
			&& (Hit == CandidateBody || CandidateBody->IsAttachedTo(Hit));
	}
}
