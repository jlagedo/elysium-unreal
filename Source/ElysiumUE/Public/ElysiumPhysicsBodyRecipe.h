#pragma once
#include "CoreMinimal.h"

/** Semantic reconstruction inputs, not Source's tree/allocator or IVP object layout.
 * PhysObjectHeader_t's observer is the Physics save block (10043c70/100443e0). */
enum class EElysiumPhysicsRecipeKind : uint8 { Model, Bbox };
struct FElysiumPhysicsBodyRecipe
{
	EElysiumPhysicsRecipeKind Kind = EElysiumPhysicsRecipeKind::Model;
	bool bStatic = false; // 1002e7e0 sixth argument selects env#8, independently of surface.
	FString ModelAsset;
	FString Surface;
	FVector BboxMin = FVector::ZeroVector;
	FVector BboxMax = FVector::ZeroVector;
	float MassKg = 1.f;
	float InertiaScale = 1.f;
	float LinearDamping = .1f;
	float AngularDamping = .1f;
	float RotInertiaLimit = .5f;
	float Volume = 0.f;
	float Drag = 1.f;
	float RollingDrag = 0.f;
	FString SolidPairsJson; // owned ordered authored pairs; unadapted parameter observers stay named
	bool bMassCenter = false;
	FVector MassCenterCm = FVector::ZeroVector;
};

// 2601a670/2601aad0/2601a250: authored controller state around a Chaos-owned body.
// Submitted target is independent of both logical entity pose and native solved pose.
struct FElysiumPhysicsShadowState
{
	bool bInstalled = false;
	bool bActive = false;
	bool bAllowMovement = false;
	bool bAllowRotation = false;
	bool bGravityMode = false;
	FTransform Target = FTransform::Identity;
	FVector PredictedPosition = FVector::ZeroVector;
	float RemainingSeconds = 0.f;
	float LinearDampingLimitCm = 0.f;
	float LinearDesiredLimitCm = 0.f;
	float AngularDampingLimitRadians = 0.f;
	float AngularDesiredLimitRadians = 0.f;
	float TeleportDistanceCm = 0.f;
	float SavedMassKg = 1.f;
	FVector SavedInertia = FVector::ZeroVector;
	bool bSavedGravity = true;
};

struct FElysiumSavedPhysicsBody
{
	int32 EntityIndex = INDEX_NONE;
	FString FieldName = TEXT("m_pPhysicsObject");
	bool bHasObject = true; // PhysObjectHeader count0 still observes/restores an absent field
	FElysiumPhysicsBodyRecipe Recipe;
	FTransform Pose = FTransform::Identity;
	FVector LinearVelocity = FVector::ZeroVector;
	FVector AngularVelocityRadians = FVector::ZeroVector;
	bool bAsleep = false;
	bool bGravity = true;
	float CurrentMassKg = 1.f;
	float CurrentLinearDamping = .1f;
	float CurrentAngularDamping = .1f;
	FName CollisionProfile;
	uint8 CollisionMask = 0;
	TArray<uint8> CollisionResponses; // native query response channels, observed by sight/traces
	uint8 BboxCollisionEnabled = 3; // ECollisionEnabled::QueryAndPhysics; one analytic shape
	FElysiumPhysicsShadowState Shadow;
	bool bShadowMember = false; // serial entity registry belongs to owner field, not native alias
};
FArchive& operator<<(FArchive& Ar, FElysiumSavedPhysicsBody& State);
