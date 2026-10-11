#pragma once
#include "CoreMinimal.h"
#include "ElysiumPhysicsBodyRecipe.h"
#include "ElysiumEntityHandle.h"
class UStaticMeshComponent;
struct FBodyInstance;
class FElysiumPhysicsSurfaceProps;
class FElysiumEntity;
class FElysiumEntityWorld;
class UWorld;
namespace Chaos { class FSingleParticlePhysicsProxy; }

/** Retail object/controller contract over the native Chaos scene. */
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
	// Native collection and referencing fields are independent of saved recipe eligibility.
	void BindReference(FElysiumEntity* Entity, void* Actor);
	// Creation game data is independent of aliases and remains attached to the incarnation.
	void SetGameData(void* Actor, FElysiumEntity& Entity);
	void ForgetGameData(FElysiumEntity& Entity);
	struct FNativeOutput { void* Actor; uint64 Incarnation; };
	TArray<FNativeOutput> ActiveOutputs(FElysiumEntityWorld& World);
	bool IsCurrentOutput(const FNativeOutput& Output);
	FElysiumEntity* GameDataFor(const FNativeOutput& Output, FElysiumEntityWorld& World);
	void RecordNativeOutput(UStaticMeshComponent* Component);
	void RecordNativeActivity(UStaticMeshComponent* Component, bool bAwake);
	void Wake(UStaticMeshComponent* Component);
	bool GetPosition(void* Actor, FVector& OriginCm, FRotator& SourceAngles);
	void RetireNative(UStaticMeshComponent* Component);
	void DestroyActor(void* Actor);
	FBodyInstance* Identity(UStaticMeshComponent* Component);
	// Unlike FBodyInstance storage, this identifies the actual native actor incarnation.
	Chaos::FSingleParticlePhysicsProxy* ActorIdentity(UStaticMeshComponent* Component);
	bool IsActive(UStaticMeshComponent* Component);
	void Destroy(UStaticMeshComponent* Component);
	// 26018520/260185b0: object slots45/46, no immediate native pose write.
	bool SetShadow(void* Actor, float LinearSpeedInches, float AngularSpeedDegrees,
		bool bAllowMovement, bool bAllowRotation);
	bool UpdateShadow(void* Actor, const FVector& OriginCm, const FQuat& Rotation,
		bool bGravityMode, float ArrivalSeconds);
	bool SetShadowTeleportDistance(void* Actor, float DistanceInches);
	const FElysiumPhysicsShadowState* ShadowFor(void* Actor);
	void RemoveShadow(void* Actor); //2601a920 restores the native body before controller deletion
	int32 GetShadowPosition(void* Actor, FVector* OriginCm, FQuat* Rotation);
	// Called by the production Gameplay tick after thinks, before UE StartPhysics.
	// Unreal owns the scene, scheduling, contacts and solve; substrate owns retail dt/controller.
	float PreparePhysicsFrame(UWorld& World, float FrameSeconds, float TimeScale, bool bSimulating);
	// Full Source QAngle under the corpus Y reflection (skeletal facing is yaw-only).
	inline FQuat SourceRotation(const FVector& Angles) { return FRotator(-Angles.X, -Angles.Y, Angles.Z).Quaternion(); }
}
