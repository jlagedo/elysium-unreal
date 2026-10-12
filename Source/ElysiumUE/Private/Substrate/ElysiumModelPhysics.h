#pragma once
#include "CoreMinimal.h"
#include "ElysiumPhysicsBodyRecipe.h"
#include "ElysiumEntityHandle.h"
class UStaticMeshComponent;
class UPrimitiveComponent;
class UElysiumBrushComponent;
class UElysiumWorldCollisionComponent;
struct FBodyInstance;
class FElysiumPhysicsSurfaceProps;
class FElysiumEntity;
class FElysiumEntityWorld;
class UWorld;
namespace Chaos { class FSingleParticlePhysicsProxy; }

/** Retail object/controller contract over the native Chaos scene. */
namespace ElysiumModelPhysics
{
	//1003e7a0 shared formula; snapshots must not add callback-bearing virtual237 reads.
	inline int32 SolidMask(uint32 Flags, bool bPlayer) { return (Flags & 0x2000) != 0 ? 0x202400b : bPlayer ? 0x201400b : 0x200400b; }
	void BindWorldCreationOwners(FElysiumEntityWorld& World); //10158600 worldspawn GameData, separate from active/shadow roster
	void RetireWorldActor(UElysiumWorldCollisionComponent* Component);
	void RebindWorldActor(UElysiumWorldCollisionComponent* Component);
	bool CreateBrush(UElysiumBrushComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
		const FElysiumPhysicsBodyRecipe& Recipe, FString& OutError, bool* OutNativeAttempted = nullptr);
	bool GetContactPoint(void* Actor, void*& OtherActor, FString* Provenance = nullptr); //object44; optional GT diagnostic only
	void RefreshContactOutputs(FElysiumEntityWorld& World); //safe completed-native-frame snapshot
	FElysiumEntity* GameDataForObject(void* Actor); //object6 creation-owned serial game data
	bool Create(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
		float MassBias, float OverrideMass, bool bStartAsleep, FString& OutError);
	bool CreateStaticModel(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
		FString& OutError); //1002e580: selected .phy, parsed parameters then inertia=1, env#8
	// 1002dfe0/1002e7e0: bounds are authored local-space inputs, never visual mesh bounds.
	bool CreateBbox(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
		const FElysiumPhysicsBodyRecipe& Recipe, bool bStartAsleep, FString& OutError);
	bool Restore(UPrimitiveComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
		const FElysiumSavedPhysicsBody& State, FString& OutError);
	bool Capture(void* Actor, FElysiumSavedPhysicsBody& OutState);
	UPrimitiveComponent* ComponentFor(void* Actor);
	UPrimitiveComponent* OwnedComponent(const FElysiumEntity& Entity); //creation-owned geometry, independent of mutable object aliases
	TArray<UPrimitiveComponent*> WorldComponents(const FElysiumEntityWorld& World); //all native entity actors, including independent saved aliases
	const FElysiumPhysicsBodyRecipe* RecipeFor(void* Actor);
	// 10158520 ->10045110: remove the association before any native destruction.
	void RetireRecipe(UPrimitiveComponent* Component);
	// Native collection and referencing fields are independent of saved recipe eligibility.
	void BindReference(FElysiumEntity* Entity, void* Actor);
	// Creation game data is independent of aliases and remains attached to the incarnation.
	void SetGameData(void* Actor, FElysiumEntity& Entity);
	void ForgetGameData(FElysiumEntity& Entity);
	struct FNativeOutput { void* Actor; uint64 Incarnation; };
	FNativeOutput CaptureIdentity(void* Actor);
	TArray<FNativeOutput> ActiveOutputs(FElysiumEntityWorld& World);
	bool IsCurrentOutput(const FNativeOutput& Output);
	void RecheckCollisionFilter(const FNativeOutput& Object); //10158ab0/nonstatic object25
	void SetNativePairIgnored(const FNativeOutput& First, const FNativeOutput& Second, bool bIgnore);
	//1002e0a0 /26016d40 /26016d50: current semantic WORD, never a native collision profile.
	bool GetGameFlags(const FNativeOutput& Object, uint16& OutFlags);
	bool SetGameFlags(const FNativeOutput& Object, uint32 Flags);
	bool AddGameFlags(const FNativeOutput& Object, uint32 Flags);
	inline bool GetGameFlags(void* Actor, uint16& OutFlags) { return GetGameFlags(CaptureIdentity(Actor), OutFlags); }
	inline bool SetGameFlags(void* Actor, uint32 Flags) { return SetGameFlags(CaptureIdentity(Actor), Flags); }
	inline bool AddGameFlags(void* Actor, uint32 Flags) { return AddGameFlags(CaptureIdentity(Actor), Flags); }
	FElysiumEntity* GameDataFor(const FNativeOutput& Output, FElysiumEntityWorld& World);
	void RecordNativeOutput(UPrimitiveComponent* Component);
	void RecordNativeActivity(UPrimitiveComponent* Component, bool bAwake);
	void Wake(UPrimitiveComponent* Component);
	bool GetPosition(void* Actor, FVector& OriginCm, FRotator& SourceAngles);
	//100ad1a0 ->object40/26017f50: world cm/s and body-local Source QAngle degrees/s.
	bool GetVelocity(void* Actor, FVector* LinearCmPerSecond, FVector* SourceAngularDegrees);
	void RetireNative(UPrimitiveComponent* Component);
	void DestroyActor(void* Actor);
	FBodyInstance* Identity(UPrimitiveComponent* Component);
	// Unlike FBodyInstance storage, this identifies the actual native actor incarnation.
	Chaos::FSingleParticlePhysicsProxy* ActorIdentity(UPrimitiveComponent* Component);
	bool IsActive(UPrimitiveComponent* Component);
	void Destroy(UPrimitiveComponent* Component);
	void GateStaticModelCollision(UPrimitiveComponent* Component, bool bEnabled); //ScriptHide/Unhide, actual native shapes
	// 26018520/260185b0: object slots45/46, no immediate native pose write.
	bool SetShadow(void* Actor, float LinearSpeedInches, float AngularSpeedDegrees,
		bool bAllowMovement, bool bAllowRotation);
	bool UpdateShadow(void* Actor, const FVector& OriginCm, const FQuat& Rotation,
		bool bGravityMode, float ArrivalSeconds);
	bool SetShadowTeleportDistance(void* Actor, float DistanceInches);
	const FElysiumPhysicsShadowState* ShadowFor(void* Actor);
	void RemoveShadow(void* Actor); //2601a920 restores the native body before controller deletion
	int32 GetShadowPosition(void* Actor, FVector* OriginCm, FQuat* Rotation);
	struct FPostSolveContactSample
	{
		struct FPoint
		{
			double Phi = 0., StoredSeparation = 0., SolvedSeparation = 0.;
			double SurfacePhi[2] = {0.,0.}, OwnSurfacePhi[2] = {0.,0.}; uint32 Flags = 0;
			bool bSurfaceSample[2] = {false,false};
			FVector Normal = FVector::ZeroVector;
		};
		const void* Partner = nullptr; int32 PartnerUniqueIdx = INDEX_NONE; uint64 PartnerGeneration = 0;
		double Phi = TNumericLimits<double>::Max(); int32 ActivePoints = 0;
		bool bDisabled = false, bProbe = false, bQualified = false, bModifierObserved = false;
		bool bInitial = false;
		double ModifierSeparation = TNumericLimits<double>::Max();
		double StaticFriction = 0., DynamicFriction = 0., Restitution = 0.; //public native pair readback
		FVector ModifierPose[2] = {FVector::ZeroVector,FVector::ZeroVector}, StoredPose[2] = {FVector::ZeroVector,FVector::ZeroVector}, SolvedPose[2] = {FVector::ZeroVector,FVector::ZeroVector};
		FVector BoundsMin[2] = {FVector::ZeroVector,FVector::ZeroVector}, BoundsMax[2] = {FVector::ZeroVector,FVector::ZeroVector};
		FQuat SolvedRotation[2] = {FQuat::Identity,FQuat::Identity}; bool bGeometry[2] = {false,false}, bBounds[2] = {false,false};
		int32 GeometryType[2] = {0,0}; float GeometryMargin[2] = {0.f,0.f}; double CollisionMargin[2] = {0.,0.};
		TArray<FPoint> Points; //diagnostic copies only; no borrowed native geometry escapes PT
	};
	struct FPostSolveSample
	{
		double StepTime = 0., StepDelta = 0.;
		FVector Position = FVector::ZeroVector, SolvedPosition = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity, SolvedRotation = FQuat::Identity;
		FVector Velocity = FVector::ZeroVector, AngularVelocity = FVector::ZeroVector;
		TArray<FPostSolveContactSample> Contacts;
	};
	struct FShadowEventSample
	{
		double Time = 0., Delta = 0.; float RemainingBefore = 0.f, Remaining = 0.f, Fraction = 0.f;
		FVector Position = FVector::ZeroVector, Velocity = FVector::ZeroVector, CorrectedVelocity = FVector::ZeroVector;
		FVector PredictionBefore = FVector::ZeroVector, Target = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity, TargetRotation = FQuat::Identity;
		double LinearDrag = 0., AngularDrag = 0.;
		FVector AngularVelocity = FVector::ZeroVector, CorrectedAngularVelocity = FVector::ZeroVector;
		double TeleportErrorSquared = 0.;
		bool bTeleported = false, bCollisionRestored = true, bSleeping = false, bSimulated = false;
		TArray<FPostSolveSample> PostSolve; //read-only per-substep native observation, same revision/incarnation
	};
	TArray<FShadowEventSample> ShadowEvents(void* Actor); //completed, incarnation-fenced PT observations
	void ObservePostSolve(UWorld& World); //diagnostic opt-in at next Prepare, automatically retired with world
	struct FSemanticContactSample
	{
		int32 Side = INDEX_NONE, Point = INDEX_NONE, Face = INDEX_NONE, NativeFace = INDEX_NONE;
		uint16 Selector = 0; bool bHit = false, bDeepest = false, bNativeMaterialPoint = false;
		TArray<int32> CandidateFaces;
		TArray<uint16> CandidateSelectors;
		FVector Position = FVector::ZeroVector, Normal = FVector::ZeroVector;
		double ResidualCm = 0., SearchCm = 0.;
	};
	// Diagnostic only: immutable authored geometry queried by Chaos at real modifier points.
	// No production geometry, material or coefficient write; no source/native ordinal join.
	bool ObserveSemanticTriangles(UPrimitiveComponent& Component, const TArray<FVector>& Vertices,
		const TArray<int32>& Indices, const TArray<uint16>& Selectors, FString& OutError, bool bObserveCompetingTriangles = false);
	struct FPairMaterialSample
	{
		const void* Partner = nullptr; double Time = 0.;
		double StaticFriction = 0., DynamicFriction = 0., Restitution = 0.;
		TArray<FSemanticContactSample> SemanticContacts;
	};
	TArray<FPairMaterialSample> PairMaterials(void* Actor); //completed modifier POD, opt-in and incarnation-fenced
	// Called by the production Gameplay tick after thinks, before UE StartPhysics.
	// Unreal owns the scene, scheduling, contacts and solve; substrate owns retail dt/controller.
	float PreparePhysicsFrame(UWorld& World, float FrameSeconds, float TimeScale, bool bSimulating);
	// Full Source QAngle under the corpus Y reflection (skeletal facing is yaw-only).
	inline FQuat SourceRotation(const FVector& Angles) { return FRotator(-Angles.X, -Angles.Y, Angles.Z).Quaternion(); }
}
