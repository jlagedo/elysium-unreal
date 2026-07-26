#include "ElysiumGizmoLayer.h"

#if !UE_BUILD_SHIPPING

#include "ElysiumBrushComponent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGizmoColor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumGizmo, Log, All);

namespace
{
	// A 100 cm engine cube, scaled to a ~28 cm gizmo marker (matches the old immediate-mode box).
	// The scale and the anchor live in ElysiumGizmoColor.h so the click-pick tests the same box
	// this draws.
	constexpr const TCHAR* CubePath  = TEXT("/Engine/BasicShapes/Cube.Cube");
	constexpr const TCHAR* DepthPath = TEXT("/Game/VtMB/Materials/M_Gizmo.M_Gizmo");
	constexpr const TCHAR* XRayPath  = TEXT("/Game/VtMB/Materials/M_Gizmo_XRay.M_Gizmo_XRay");
}

FElysiumGizmoLayer::~FElysiumGizmoLayer()
{
	Teardown();
}

bool FElysiumGizmoLayer::IsBuilt() const
{
	return Ism.IsValid();
}

void FElysiumGizmoLayer::ComputeRGBA(const FElysiumEntity& Ent, uint8 Mask, float Out[4])
{
	FColor C = Ent.Def ? ElysiumGizmoClassColor(Ent.Def->Classname) : FColor(191, 191, 199);
	float Alpha = 0.35f;
	const bool bFiltered = Ent.Def && !ElysiumGizmoClassVisible(Ent.Def->Classname, Mask);
	if (Ent.IsDead() || bFiltered)
	{
		Alpha = 0.0f;                 // killed or filtered out -> invisible (instance kept, transparent)
	}
	else if (Ent.IsHidden())
	{
		C = FColor(uint8(C.R * 0.45f), uint8(C.G * 0.45f), uint8(C.B * 0.45f));   // dormant -> dimmed
	}
	Out[0] = C.R / 255.0f;
	Out[1] = C.G / 255.0f;
	Out[2] = C.B / 255.0f;
	Out[3] = Alpha;
}

void FElysiumGizmoLayer::Rebuild(FElysiumEntityWorld& World, AActor* Owner)
{
	Teardown();
	if (!Owner)
	{
		return;
	}

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CubePath);
	UMaterialInterface* DepthMaster = LoadObject<UMaterialInterface>(nullptr, DepthPath);
	UMaterialInterface* XRayMaster = LoadObject<UMaterialInterface>(nullptr, XRayPath);
	if (!Cube || !DepthMaster)
	{
		UE_LOG(LogElysiumGizmo, Warning, TEXT("gizmo layer: missing cube mesh or M_Gizmo master — gizmos disabled"));
		return;
	}
	if (!XRayMaster)
	{
		XRayMaster = DepthMaster;   // x-ray asset absent: the All mode reads the same as Visible
	}

	DepthMID.Reset(UMaterialInstanceDynamic::Create(DepthMaster, Owner));
	XRayMID.Reset(UMaterialInstanceDynamic::Create(XRayMaster, Owner));

	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Owner);
	Component->SetupAttachment(Owner->GetRootComponent());
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(Cube);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	Component->NumCustomDataFloats = 4;                       // RGBA, read by M_Gizmo
	Component->SetMaterial(0, DepthMID.Get());
	Component->SetVisibility(false);                          // stays hidden until SetMode(Visible/All)
	Component->RegisterComponent();
	Ism = Component;

	// One instance per live entity; dead-at-build entities get no instance (dead is terminal, so
	// they never need one). Custom data seeds the colour; MarkRenderStateDirty once at the end.
	const TArray<TUniquePtr<FElysiumEntity>>& Entities = World.Entities();
	EntityToInstance.Init(INDEX_NONE, Entities.Num());
	float Data[4];
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		const FElysiumEntity* E = Entities[i].Get();
		if (!E || E->IsDead() || !E->Def)
		{
			continue;
		}
		const FTransform Xform(FQuat::Identity, ElysiumGizmoAnchor(*E), FVector(ElysiumGizmoScale));
		const int32 InstanceIndex = Component->AddInstance(Xform);
		ComputeRGBA(*E, AppliedClassMask, Data);
		Component->SetCustomData(InstanceIndex, TArrayView<const float>(Data, 4), /*bMarkRenderStateDirty*/ false);
		EntityToInstance[i] = InstanceIndex;
	}
	Component->MarkRenderStateDirty();

	BuiltWorld = &World;
	bModeApplied = false;   // first SetMode after a rebuild always applies

	// Event-driven updates: dormancy/liveness flips now dirty just the one instance.
	World.SetVisualChangedHook([this](const FElysiumEntity& E) { OnEntityVisualChanged(E); });
}

void FElysiumGizmoLayer::Teardown()
{
	if (UInstancedStaticMeshComponent* Component = Ism.Get())
	{
		Component->DestroyComponent();
	}
	Ism.Reset();
	DepthMID.Reset();
	XRayMID.Reset();
	EntityToInstance.Empty();
	BuiltWorld = nullptr;
	bModeApplied = false;
	// The world back-pointer's hook (if that world is still alive) is dropped when the next epoch's
	// Rebuild re-sets it; a torn-down world drops it with itself. Nothing to clear here.
}

void FElysiumGizmoLayer::SetMode(UElysiumEntityDebugSubsystem::EGizmoMode Mode)
{
	UInstancedStaticMeshComponent* Component = Ism.Get();
	if (!Component)
	{
		return;
	}
	if (bModeApplied && Mode == AppliedMode)
	{
		return;   // nothing changed — no per-frame work
	}
	AppliedMode = Mode;
	bModeApplied = true;

	using EGizmoMode = UElysiumEntityDebugSubsystem::EGizmoMode;
	if (Mode == EGizmoMode::Off)
	{
		Component->SetVisibility(false);
		return;
	}
	Component->SetMaterial(0, (Mode == EGizmoMode::All ? XRayMID : DepthMID).Get());
	Component->SetVisibility(true);
}

void FElysiumGizmoLayer::SetClassMask(uint8 Mask)
{
	UInstancedStaticMeshComponent* Component = Ism.Get();
	if (!Component || Mask == AppliedClassMask)
	{
		return;   // nothing changed — no per-frame work
	}
	AppliedClassMask = Mask;
	if (BuiltWorld == nullptr)
	{
		return;
	}

	// Re-pack every instance's RGBA. Only the alpha actually moves, but the pack is one function
	// (ComputeRGBA) so that filtered, dead and dormant never drift apart.
	const TArray<TUniquePtr<FElysiumEntity>>& Entities = BuiltWorld->Entities();
	float Data[4];
	for (int32 i = 0; i < Entities.Num() && i < EntityToInstance.Num(); ++i)
	{
		const int32 InstanceIndex = EntityToInstance[i];
		const FElysiumEntity* E = Entities[i].Get();
		if (InstanceIndex == INDEX_NONE || !E)
		{
			continue;
		}
		ComputeRGBA(*E, Mask, Data);
		Component->SetCustomData(InstanceIndex, TArrayView<const float>(Data, 4), /*bMarkRenderStateDirty*/ false);
	}
	Component->MarkRenderStateDirty();
}

void FElysiumGizmoLayer::OnEntityVisualChanged(const FElysiumEntity& Ent)
{
	UInstancedStaticMeshComponent* Component = Ism.Get();
	const int32 EntityIndex = Ent.Handle.Index;
	if (!Component || !EntityToInstance.IsValidIndex(EntityIndex))
	{
		return;
	}
	const int32 InstanceIndex = EntityToInstance[EntityIndex];
	if (InstanceIndex == INDEX_NONE)
	{
		return;   // dead-at-build entity, no instance
	}
	float Data[4];
	ComputeRGBA(Ent, AppliedClassMask, Data);
	Component->SetCustomData(InstanceIndex, TArrayView<const float>(Data, 4), /*bMarkRenderStateDirty*/ true);
}

#endif // !UE_BUILD_SHIPPING
