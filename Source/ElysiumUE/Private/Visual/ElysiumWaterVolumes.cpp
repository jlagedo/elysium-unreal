#include "ElysiumWaterVolumes.h"

#include "ElysiumFog.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SceneView.h"
#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogElysiumWater, Log, All);

#if !UE_BUILD_SHIPPING
// Draw every staged brush's AABB in its volume's fog hue. "The editor is the tuning
// surface": a volume has no drawn surface of its own on the two divergence families
// (`invisible_water`, `cheap_water`), so this is the only way to see where one stands. Live: the
// callback flips the actor's tick, so the actor costs nothing per frame while it is off.
static TAutoConsoleVariable<int32> CVarWaterDraw(
	TEXT("elysium.Water.Draw"), 0,
	TEXT("Draw the map's water brush bounds (1) or nothing (0)."),
	ECVF_Cheat);
#endif

int32 ElysiumWater::FindVolumeAt(TConstArrayView<FElysiumWaterVolume> Volumes, const FVector& PointCm)
{
	for (int32 VolumeIndex = 0; VolumeIndex < Volumes.Num(); ++VolumeIndex)
	{
		for (const FElysiumWaterBrush& Brush : Volumes[VolumeIndex].Brushes)
		{
			// The AABB is the plane hull's own, so it rejects without touching the planes; a brush
			// the stage could not solve a hull for carries an invalid box and is skipped rather
			// than tested against a half-space set nothing bounds.
			if (!Brush.BoundsCm.IsValid || !Brush.BoundsCm.IsInsideOrOn(PointCm))
			{
				continue;
			}
			bool bInside = true;
			for (const FPlane& Plane : Brush.Planes)
			{
				// Outward normals: `PlaneDot` is `n·p − d`, so inside is at or below zero.
				if (Plane.PlaneDot(PointCm) > 0.0)
				{
					bInside = false;
					break;
				}
			}
			if (bInside)
			{
				return VolumeIndex;
			}
		}
	}
	return INDEX_NONE;
}

EElysiumWaterLevel ElysiumWater::ClassifyBody(TConstArrayView<FElysiumWaterVolume> Volumes,
	const FVector& FeetCm, const FVector& WaistCm, const FVector& EyesCm, int32* OutVolume)
{
	if (OutVolume != nullptr)
	{
		*OutVolume = INDEX_NONE;
	}

	// `CheckWater` traces the feet first and returns immediately when they are dry — a body whose
	// waist is inside a volume its feet are not in is not in water at all, which is what keeps a
	// bridge over a canal dry.
	const int32 FeetVolume = FindVolumeAt(Volumes, FeetCm);
	if (FeetVolume == INDEX_NONE)
	{
		return EElysiumWaterLevel::None;
	}

	EElysiumWaterLevel Level = EElysiumWaterLevel::Feet;
	int32 Volume = FeetVolume;

	const int32 WaistVolume = FindVolumeAt(Volumes, WaistCm);
	if (WaistVolume != INDEX_NONE)
	{
		Level = EElysiumWaterLevel::Waist;
		Volume = WaistVolume;

		const int32 EyesVolume = FindVolumeAt(Volumes, EyesCm);
		if (EyesVolume != INDEX_NONE)
		{
			Level = EElysiumWaterLevel::Eyes;
			Volume = EyesVolume;
		}
	}

	if (OutVolume != nullptr)
	{
		*OutVolume = Volume;
	}
	return Level;
}

AElysiumWaterVolumes::AElysiumWaterVolumes()
{
	// The debug draw is the only per-frame work here, so the tick starts off and the cvar's
	// callback turns it on; a map with water costs nothing while nobody is looking at it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// A bare root, so the bake can stand the actor at its first volume's centre and the Outliner
	// and a debug pick have somewhere to find it. Nothing hangs off it and nothing reads it: every
	// staged plane and bound is already world-space, which is why the actor draws nothing.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Static);

	// A bounded volume at priority 1, above the map's neutral unbound PPV (`elysium.ppv`, priority
	// 0), so the underwater fog composes over whatever art direction that volume carries. The guid
	// is fixed rather than minted per instance because the engine uses it as the last sort key
	// between equal-priority volumes, and a per-run value would make that order vary by load.
	Properties.Settings = &UnderwaterSettings;
	Properties.Priority = 1.f;
	Properties.BlendRadius = 0.f;
	Properties.BlendWeight = 1.f;
	Properties.bIsEnabled = false;
	Properties.bIsUnbound = false;
	Properties.VolumeGuid = FGuid(0x7a13c5e2, 0x4d0f4b91, 0x9c86e30a, 0x2b5f7d64);
}

const TCHAR* AElysiumWaterVolumes::UnderwaterMasterPath()
{
	return TEXT("/Game/ElysiumGenerated/Materials/V2/M_ElysiumUnderwater.M_ElysiumUnderwater");
}

void AElysiumWaterVolumes::BeginPlay()
{
	Super::BeginPlay();

	// One MID for the map, parented to the generated post-process master and added to the settings
	// once: `AddBlendable` updates in place, so re-adding per view would be the same write done
	// sixty times a second. The three fog scalars move per view instead.
	if (UMaterialInterface* Master = LoadObject<UMaterialInterface>(
			nullptr, UnderwaterMasterPath(), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UnderwaterMID = UMaterialInstanceDynamic::Create(Master, this);
		UnderwaterSettings.AddBlendable(UnderwaterMID, 1.f);
	}
	else
	{
		// A checkout that has not run `uv run elysium export bundle policy`. The volume still
		// classifies the body and still writes `UnderwaterDepth`; only the fog is missing, and an
		// empty settings block blends to nothing rather than to black.
		UE_LOG(LogElysiumWater, Warning,
			TEXT("'%s' is not generated — the underwater view will not be fogged "
			     "(run: uv run elysium export bundle policy)"), UnderwaterMasterPath());
	}

	if (UWorld* World = GetWorld())
	{
		World->AddPostProcessVolume(TWeakInterfacePtr<IInterface_PostProcessVolume>(this));
		PostProcessHandle = World->OnBeginPostProcessSettings.AddUObject(
			this, &AElysiumWaterVolumes::ComputeUnderwaterPostProcess);
	}

#if !UE_BUILD_SHIPPING
	CVarWaterDraw.AsVariable()->SetOnChangedCallback(
		FConsoleVariableDelegate::CreateWeakLambda(this,
			[this](IConsoleVariable* Var) { SetActorTickEnabled(Var->GetInt() != 0); }));
	SetActorTickEnabled(CVarWaterDraw.GetValueOnGameThread() != 0);
#endif
}

void AElysiumWaterVolumes::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->OnBeginPostProcessSettings.Remove(PostProcessHandle);
		World->RemovePostProcessVolume(this);
	}
	PostProcessHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void AElysiumWaterVolumes::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	for (const FElysiumWaterVolume& Volume : Volumes)
	{
		// The authored hue at full value. The corpus fog colours are 0.02-0.15 (and darker still
		// once decoded), so drawn as authored every box would be black; what tells two volumes
		// apart on screen is which colour they are, not how dark.
		const float Peak = FMath::Max3(Volume.FogColor.R, Volume.FogColor.G, Volume.FogColor.B);
		const FColor Colour =
			(Peak > 0.f ? Volume.FogColor / Peak : FLinearColor::White).ToFColor(false);
		for (const FElysiumWaterBrush& Brush : Volume.Brushes)
		{
			if (Brush.BoundsCm.IsValid)
			{
				DrawDebugBox(GetWorld(), Brush.BoundsCm.GetCenter(), Brush.BoundsCm.GetExtent(),
					Colour, false, -1.f, 0, 2.f);
			}
		}
	}
#endif
}

int32 AElysiumWaterVolumes::UpdateViewPostProcess(const FVector& ViewLocationCm)
{
	const int32 Volume = FindVolumeAt(ViewLocationCm);
	Properties.bIsEnabled = Volume != INDEX_NONE;
	if (Volume == INDEX_NONE)
	{
		return Volume;
	}

	// One packer, one application: the underwater master declares the same three
	// `ElysiumSurfaceParamsDecal` names the decal master does, so this is `ApplyToDecalMID`'s
	// write verbatim -- the authored colour decoded, the start in cm, and the inverse range that
	// reads as 0 (unfogged) for a volume whose `$fogenable` is off or whose range is degenerate.
	// It null-checks the MID, which is the other half of the early return above.
	const FElysiumWaterVolume& Row = Volumes[Volume];
	ElysiumFog::ApplyToDecalMID(
		UnderwaterMID, Row.bFogEnabled, Row.FogColor, Row.FogStartCm, Row.FogEndCm);
	return Volume;
}

void AElysiumWaterVolumes::ComputeUnderwaterPostProcess(FVector ViewLocation, FSceneView* SceneView)
{
	const int32 Volume = UpdateViewPostProcess(ViewLocation);
	if (SceneView == nullptr)
	{
		return;
	}

	// The renderer reads these two for pass ordering (underwater translucency before the single
	// layer water pass) rather than for shading; 5.8's shader-side `CameraIsUnderWater` is a
	// compile-time false, which is why the look itself is the post-process above.
	// -1 is the engine's own "out of water" (`FSceneView::IsUnderwater`).
	SceneView->UnderwaterDepth = Volume != INDEX_NONE
		? static_cast<float>(Volumes[Volume].SurfaceZCm - ViewLocation.Z)
		: -1.f;
	SceneView->WaterIntersection = Volume != INDEX_NONE
		? EViewWaterIntersection::InsideWater
		: EViewWaterIntersection::OutsideWater;
}

bool AElysiumWaterVolumes::EncompassesPoint(FVector Point, float SphereRadius, float* OutDistanceToPoint)
{
	// Distance 0 unconditionally: `UWorld::DoPostProcessVolume` discards this function's return
	// value for a bounded volume and blends on the distance alone, so the real gate is
	// `bIsEnabled` — already resolved for this view by the handler above, which runs first
	// (`OnBeginPostProcessSettings` is broadcast before the volume walk). A distance of 0 with
	// `BlendRadius` 0 is full weight, which is what an underwater view is: no soft edge, because
	// a partial blend of a fog lerp reads as haze rather than as water.
	if (OutDistanceToPoint != nullptr)
	{
		*OutDistanceToPoint = 0.f;
	}
	return Properties.bIsEnabled;
}

#if DEBUG_POST_PROCESS_VOLUME_ENABLE
FString AElysiumWaterVolumes::GetDebugName() const
{
	return FString::Printf(TEXT("ElysiumWaterVolumes (%d volumes)"), Volumes.Num());
}
#endif
