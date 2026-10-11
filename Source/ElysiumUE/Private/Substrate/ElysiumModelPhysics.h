#pragma once
#include "CoreMinimal.h"
class UStaticMeshComponent;
struct FBodyInstance;
class FElysiumPhysicsSurfaceProps;
namespace Chaos { class FSingleParticlePhysicsProxy; }

/** Native adapter used by the placed physics-prop leaf; no simulation clock or solver here. */
namespace ElysiumModelPhysics
{
	bool Create(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
		float MassBias, float OverrideMass, bool bStartAsleep, FString& OutError);
	FBodyInstance* Identity(UStaticMeshComponent* Component);
	// Unlike FBodyInstance storage, this identifies the actual native actor incarnation.
	Chaos::FSingleParticlePhysicsProxy* ActorIdentity(UStaticMeshComponent* Component);
	bool IsActive(UStaticMeshComponent* Component);
	void Destroy(UStaticMeshComponent* Component);
}
