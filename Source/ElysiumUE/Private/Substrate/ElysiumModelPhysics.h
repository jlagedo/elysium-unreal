#pragma once
#include "CoreMinimal.h"
#include "ElysiumPhysicsBodyRecipe.h"
class UStaticMeshComponent;
struct FBodyInstance;
class FElysiumPhysicsSurfaceProps;
namespace Chaos { class FSingleParticlePhysicsProxy; }

/** Native adapter used by the placed physics-prop leaf; no simulation clock or solver here. */
namespace ElysiumModelPhysics
{
	bool Create(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
		float MassBias, float OverrideMass, bool bStartAsleep, FString& OutError);
	// 1002dfe0/1002e7e0: bounds are authored local-space inputs, never visual mesh bounds.
	bool CreateBbox(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
		const FElysiumPhysicsBodyRecipe& Recipe, bool bStartAsleep, FString& OutError);
	bool Restore(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
		const FElysiumSavedPhysicsBody& State, FString& OutError);
	bool Capture(void* Actor, FElysiumSavedPhysicsBody& OutState);
	UStaticMeshComponent* ComponentFor(void* Actor);
	const FElysiumPhysicsBodyRecipe* RecipeFor(void* Actor);
	// 10158520 ->10045110: remove the association before any native destruction.
	void RetireRecipe(UStaticMeshComponent* Component);
	FBodyInstance* Identity(UStaticMeshComponent* Component);
	// Unlike FBodyInstance storage, this identifies the actual native actor incarnation.
	Chaos::FSingleParticlePhysicsProxy* ActorIdentity(UStaticMeshComponent* Component);
	bool IsActive(UStaticMeshComponent* Component);
	void Destroy(UStaticMeshComponent* Component);
}
