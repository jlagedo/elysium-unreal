#include "ElysiumWaterVolumes.h"

#include "ElysiumFog.h"

#include "Components/BoxComponent.h"
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

namespace
{
	// One convex row, as either half of the carve stages it.
	//
	// A stage BRUSH carries the AABB its own plane hull solved, so the box rejects without touching
	// the planes -- and a brush the stage could not solve a hull for carries an invalid box and is
	// skipped rather than tested against a half-space set nothing bounds (`bBoundsRequired`).
	// A compiler PIECE (G18) carries a box too, but a DERIVED one: vbsp publishes no bounds for a
	// ledge, so the stage takes the AABB of the ledge's own vertices, which bounds its convex hull
	// exactly. `bBoundsRequired` therefore only ever decides the brush case today; it stays because
	// it is what says a planeless, boundless row contains nothing rather than everything.
	bool ConvexContains(const FElysiumWaterBrush& Row, const FVector& PointCm, bool bBoundsRequired)
	{
		if (Row.BoundsCm.IsValid)
		{
			if (!Row.BoundsCm.IsInsideOrOn(PointCm))
			{
				return false;
			}
		}
		else if (bBoundsRequired || Row.Planes.IsEmpty())
		{
			return false;
		}
		for (const FPlane& Plane : Row.Planes)
		{
			// Outward normals: `PlaneDot` is `n·p − d`, so inside is at or below zero.
			if (Plane.PlaneDot(PointCm) > 0.0)
			{
				return false;
			}
		}
		return true;
	}
}

int32 ElysiumWater::FindVolumeAt(TConstArrayView<FElysiumWaterVolume> Volumes, const FVector& PointCm)
{
	for (int32 VolumeIndex = 0; VolumeIndex < Volumes.Num(); ++VolumeIndex)
	{
		const FElysiumWaterVolume& Volume = Volumes[VolumeIndex];
		// G18: where the compiler emitted a convex decomposition of this water solid, THAT is the
		// shape `CheckWater` traces -- the authored brush is the pre-CSG one, and a carve out of it
		// answers differently at the cut. The brush rows are the whole answer only where vbsp
		// published no pieces.
		const bool bPieces = !Volume.Pieces.IsEmpty();
		const TArray<FElysiumWaterBrush>& Convex = bPieces ? Volume.Pieces : Volume.Brushes;
		for (const FElysiumWaterBrush& Row : Convex)
		{
			if (ConvexContains(Row, PointCm, !bPieces))
			{
				return VolumeIndex;
			}
		}
	}
	return INDEX_NONE;
}

int32 ElysiumWater::FindNearVolumeAt(TConstArrayView<FElysiumWaterVolume> Volumes,
	const FVector& PointCm)
{
	for (int32 VolumeIndex = 0; VolumeIndex < Volumes.Num(); ++VolumeIndex)
	{
		const FElysiumWaterVolume& Volume = Volumes[VolumeIndex];
		if (!Volume.NearBoxesCm.IsEmpty())
		{
			for (const FBox& Box : Volume.NearBoxesCm)
			{
				if (Box.IsValid && Box.IsInsideOrOn(PointCm))
				{
					return VolumeIndex;
				}
			}
			continue;
		}
		// A level baked before the visibility reader existed publishes no near set. Falling back
		// to the volume's own bounds degrades the answer to "inside the water", which is a subset
		// of the truth -- never a claim the point is near water when it is not.
		for (const FElysiumWaterBrush& Brush : Volume.Brushes)
		{
			if (Brush.BoundsCm.IsValid && Brush.BoundsCm.IsInsideOrOn(PointCm))
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

ElysiumWater::FSplashDecision ElysiumWater::DecideSplash(const FSplashInput& In)
{
	// Both timers come back unchanged unless the rule that owns one fires.
	FSplashDecision Out;
	Out.LastSplashSeconds = In.LastSplashSeconds;
	Out.NextWadeSeconds = In.NextWadeSeconds;

	// D2 first: one splash per entity per half second, over both rules. It is asked before either
	// because it is the flag VtMB itself carries on the entity, not a property of either effect.
	if (In.NowSeconds - In.LastSplashSeconds < SplashRetriggerSeconds)
	{
		return Out;
	}

	const FVector Horizontal(In.VelocityCmPerSec.X, In.VelocityCmPerSec.Y, 0.0);
	const float HorizontalSpeed = static_cast<float>(Horizontal.Size());

	// `GetRenderOrigin() - vel.xy * 0.035`, snapped to the water surface. The lead is a TIME, so a
	// body running in lands its splash ahead of itself by however far it travels in 35 ms.
	FVector At = In.OriginCm - Horizontal * SplashLeadSeconds;
	At.Z = In.SurfaceZCm;

	// The entry splash: the level transition 0 -> in-water, with enough downward speed that the
	// body fell rather than walked in.
	if (In.PreviousLevel == 0 && In.Level >= 1
		&& In.VelocityCmPerSec.Z < BigEntryVelocityZCmPerSec)
	{
		Out.Kind = ESplash::Big;
		Out.LocationCm = At;
		Out.LastSplashSeconds = In.NowSeconds;
		return Out;
	}

	// The wade splash, while the body is in the water but not under it, moving.
	if (In.Level > 0 && In.Level < 3 && HorizontalSpeed >= WadeSpeedCmPerSec
		&& In.NowSeconds >= In.NextWadeSeconds)
	{
		At.Z += In.WadeJitterUnits * ElysiumMove::U;
		Out.Kind = ESplash::Wade;
		Out.LocationCm = At;
		Out.LastSplashSeconds = In.NowSeconds;
		Out.NextWadeSeconds = In.NowSeconds + WadeCooldownSeconds(HorizontalSpeed);
	}
	return Out;
}

float ElysiumWater::SubmergedFraction(const FBox& BodyBoundsCm, float SurfaceZCm)
{
	if (!BodyBoundsCm.IsValid)
	{
		return 0.f;
	}
	const double Height = BodyBoundsCm.Max.Z - BodyBoundsCm.Min.Z;
	if (Height <= 0.0)
	{
		// A degenerate box is in or out, with nothing in between.
		return BodyBoundsCm.Min.Z <= SurfaceZCm ? 1.f : 0.f;
	}
	const double Under = FMath::Clamp(SurfaceZCm - BodyBoundsCm.Min.Z, 0.0, Height);
	return static_cast<float>(Under / Height);
}

float ElysiumWater::DisplacedVolumeM3(const FBox& BodyBoundsCm, float Fraction)
{
	if (!BodyBoundsCm.IsValid)
	{
		return 0.f;
	}
	const FVector Size = BodyBoundsCm.GetSize();
	// cm³ -> m³ is 1e-6, and the submerged share of the box is the displaced share of its volume.
	const double Cubic = Size.X * Size.Y * Size.Z * 1.0e-6;
	return static_cast<float>(Cubic * FMath::Clamp(Fraction, 0.f, 1.f));
}

float ElysiumWater::BuoyantForceZ(float FluidDensityKgPerM3, float DisplacedVolumeM3,
	float GravityZCmPerSec2)
{
	// Archimedes: rho * V * |g|, upward. `AddForce` takes kg·cm/s² with `bAccelChange` false, and
	// the world's gravity is already in cm/s², so the three multiply with no further conversion.
	return FMath::Max(FluidDensityKgPerM3, 0.f) * FMath::Max(DisplacedVolumeM3, 0.f)
		* FMath::Abs(GravityZCmPerSec2);
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

	// G7: the fluid pass exists only where a volume authored a `fluid` block AND named a solid.
	// Verdict B5: VtMB's creation guard is `fluid.index > 0` alone (`vampire.dll FUN_10158600`,
	// `101586ff JLE skip`), so a block naming index 0 gets no controller there and gets none here.
	// The stage refuses such a row outright (`map_geometry._water_fluid`), so this is the same
	// guard stated on the side that consumes it rather than a second policy. Both owner maps
	// author `index "5"`; a map that authors none pays for nothing -- no boxes, no overlap events,
	// no tick.
	auto HasController = [](const FElysiumWaterVolume& Volume)
	{
		return Volume.Fluid.bHasFluid && Volume.Fluid.Index > 0;
	};
	for (const FElysiumWaterVolume& Volume : Volumes)
	{
		bAnyFluidAuthored = bAnyFluidAuthored || HasController(Volume);
	}
	if (bAnyFluidAuthored)
	{
		for (int32 VolumeIndex = 0; VolumeIndex < Volumes.Num(); ++VolumeIndex)
		{
			const FElysiumWaterVolume& Volume = Volumes[VolumeIndex];
			if (!HasController(Volume))
			{
				continue;
			}
			// The broadphase box is the volume's merged extent -- its brush hulls where the stage
			// solved them, its leaf boxes otherwise, so a volume carrying only the compiler's
			// planeless pieces still gets one.
			FBox Merged(ForceInit);
			for (const FElysiumWaterBrush& Brush : Volume.Brushes)
			{
				if (Brush.BoundsCm.IsValid)
				{
					Merged += Brush.BoundsCm;
				}
			}
			for (const FBox& Leaf : Volume.LeafBoxesCm)
			{
				if (Leaf.IsValid)
				{
					Merged += Leaf;
				}
			}
			if (!Merged.IsValid)
			{
				continue;
			}
			UBoxComponent* Box = NewObject<UBoxComponent>(this,
				*FString::Printf(TEXT("FluidQuery_%d"), VolumeIndex));
			Box->SetupAttachment(SceneRoot);
			// Everything is set BEFORE registration: the actor stands static, so a placed box that
			// then moved would be an illegal transform write on a static component.
			Box->SetMobility(EComponentMobility::Static);
			Box->SetRelativeLocation(
				GetActorTransform().InverseTransformPosition(Merged.GetCenter()));
			Box->SetBoxExtent(Merged.GetExtent(), false);
			// Query only, and only against simulating bodies: this box decides nothing about where
			// anything can walk or stand -- the carve does that, through `FindVolumeAt`.
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
			Box->SetGenerateOverlapEvents(true);
			Box->SetHiddenInGame(true);
			Box->CanCharacterStepUpOn = ECB_No;
			Box->OnComponentBeginOverlap.AddDynamic(
				this, &AElysiumWaterVolumes::OnFluidBoxBeginOverlap);
			Box->OnComponentEndOverlap.AddDynamic(
				this, &AElysiumWaterVolumes::OnFluidBoxEndOverlap);
			Box->RegisterComponent();
			FluidQueryBoxes.Add(Box);
		}
	}

#if !UE_BUILD_SHIPPING
	CVarWaterDraw.AsVariable()->SetOnChangedCallback(
		FConsoleVariableDelegate::CreateWeakLambda(this,
			[this](IConsoleVariable*) { RefreshTickEnabled(); }));
#endif
	RefreshTickEnabled();
}

void AElysiumWaterVolumes::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// A body outlives the map on a level transition only if we leave our damping on it.
	for (FFluidBody& Body : FluidBodies)
	{
		ReleaseFluidBody(Body);
	}
	FluidBodies.Reset();

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

	TickFluidBodies(DeltaSeconds);

#if !UE_BUILD_SHIPPING
	if (CVarWaterDraw.GetValueOnGameThread() == 0)
	{
		// The tick may be on for the fluid pass alone.
		return;
	}
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
	// G9: the PVS of the water clusters is the cheap outer gate, one box test against the leaf set
	// the compiler already solved. Outside it the view cannot see the water at all, so neither the
	// carve walk nor the post-process has anything to say -- and the same answer is what the audio
	// seam reads through `IsViewNearWater`, rather than asking a second query of its own.
	bViewNearWater = ElysiumWater::IsNearWater(Volumes, ViewLocationCm);
	if (!bViewNearWater)
	{
		Properties.bIsEnabled = false;
		return INDEX_NONE;
	}

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

const FElysiumWaterFluid& AElysiumWaterVolumes::FluidAt(int32 VolumeIndex) const
{
	// An unset row rather than a null: every reader wants "no fluid here", not a branch.
	static const FElysiumWaterFluid Unset;
	return Volumes.IsValidIndex(VolumeIndex) ? Volumes[VolumeIndex].Fluid : Unset;
}

int32 AElysiumWaterVolumes::FluidBodyCount() const
{
	int32 Count = 0;
	for (const FFluidBody& Body : FluidBodies)
	{
		Count += Body.bInFluid ? 1 : 0;
	}
	return Count;
}

void AElysiumWaterVolumes::RefreshTickEnabled()
{
	bool bWanted = !FluidBodies.IsEmpty();
#if !UE_BUILD_SHIPPING
	bWanted = bWanted || CVarWaterDraw.GetValueOnGameThread() != 0;
#endif
	SetActorTickEnabled(bWanted);
}

void AElysiumWaterVolumes::OnFluidBoxBeginOverlap(UPrimitiveComponent*, AActor*,
	UPrimitiveComponent* OtherComp, int32, bool, const FHitResult&)
{
	// The box is the broadphase, not the answer: anything that got here is merely somewhere near
	// the water, and the exact carve test runs per tick over this short list.
	if (OtherComp == nullptr || !OtherComp->IsSimulatingPhysics())
	{
		return;
	}
	for (const FFluidBody& Body : FluidBodies)
	{
		if (Body.Component.Get() == OtherComp)
		{
			return;   // two volumes' boxes can overlap the same body
		}
	}
	FFluidBody Body;
	Body.Component = OtherComp;
	FluidBodies.Add(Body);
	RefreshTickEnabled();
}

void AElysiumWaterVolumes::OnFluidBoxEndOverlap(UPrimitiveComponent*, AActor*,
	UPrimitiveComponent* OtherComp, int32)
{
	if (OtherComp == nullptr)
	{
		return;
	}
	for (int32 Index = FluidBodies.Num() - 1; Index >= 0; --Index)
	{
		if (FluidBodies[Index].Component.Get() != OtherComp)
		{
			continue;
		}
		// A body can leave one volume's box while still inside another's; the tick would re-add
		// it, but the damping has to come off now either way.
		ReleaseFluidBody(FluidBodies[Index]);
		FluidBodies.RemoveAtSwap(Index);
	}
	RefreshTickEnabled();
}

void AElysiumWaterVolumes::ReleaseFluidBody(FFluidBody& Body)
{
	if (!Body.bInFluid)
	{
		return;
	}
	Body.bInFluid = false;
	Body.VolumeIndex = INDEX_NONE;
	if (UPrimitiveComponent* Comp = Body.Component.Get())
	{
		Comp->SetLinearDamping(Body.RestoreLinearDamping);
	}
}

void AElysiumWaterVolumes::TickFluidBodies(float DeltaSeconds)
{
	if (FluidBodies.IsEmpty())
	{
		return;
	}
	// The pass' own clock. Gameplay never reads wall time (`.claude/rules/cpp.md`), and the splash
	// cooldowns are measured on whatever clock ticked the bodies that raised them.
	FluidClockSeconds += DeltaSeconds;

	const UWorld* World = GetWorld();
	const float GravityZ = World != nullptr ? World->GetGravityZ() : -980.f;

	for (int32 Index = FluidBodies.Num() - 1; Index >= 0; --Index)
	{
		FFluidBody& Body = FluidBodies[Index];
		UPrimitiveComponent* Comp = Body.Component.Get();
		if (Comp == nullptr)
		{
			FluidBodies.RemoveAtSwap(Index);
			continue;
		}
		if (!Comp->IsSimulatingPhysics())
		{
			// A body put to sleep as static keeps nothing of ours.
			ReleaseFluidBody(Body);
			continue;
		}

		// The bottom of the body decides whether it is touching the water at all, exactly as the
		// feet do for a character; the centre is the fallback for a body whose base has already
		// passed under the floor of the carve.
		const FBox Bounds = Comp->Bounds.GetBox();
		const FVector Centre = Bounds.GetCenter();
		int32 Volume = FindVolumeAt(FVector(Centre.X, Centre.Y, Bounds.Min.Z));
		if (Volume == INDEX_NONE)
		{
			Volume = FindVolumeAt(Centre);
		}
		const FElysiumWaterFluid& Fluid = FluidAt(Volume);
		const float Fraction = Volume != INDEX_NONE
			? ElysiumWater::SubmergedFraction(Bounds, Volumes[Volume].SurfaceZCm) : 0.f;
		// `Index > 0` beside `bHasFluid`, as at BeginPlay: verdict B5's creation guard, asked
		// wherever the controller's existence decides anything.
		if (!Fluid.bHasFluid || Fluid.Index <= 0 || Fraction <= 0.f)
		{
			ReleaseFluidBody(Body);
			continue;
		}

		if (!Body.bInFluid)
		{
			// VtMB's `m_iEFlags 0x80000`: the body is in the fluid from here until it is not.
			Body.bInFluid = true;
			Body.RestoreLinearDamping = Comp->GetLinearDamping();
			if (Fluid.Damping > 0.f)
			{
				Comp->SetLinearDamping(Fluid.Damping);
			}

			// The same entry rule the player's transition uses, with the wade lane closed: a
			// crate does not wade, so the level it reports is "under".
			ElysiumWater::FSplashInput In;
			In.PreviousLevel = 0;
			In.Level = static_cast<int32>(EElysiumWaterLevel::Eyes);
			In.OriginCm = Centre;
			In.VelocityCmPerSec = Comp->GetPhysicsLinearVelocity();
			In.SurfaceZCm = Volumes[Volume].SurfaceZCm;
			In.NowSeconds = FluidClockSeconds;
			In.LastSplashSeconds = Body.LastSplashSeconds;
			const ElysiumWater::FSplashDecision Decision = ElysiumWater::DecideSplash(In);
			Body.LastSplashSeconds = Decision.LastSplashSeconds;
			if (Decision.Kind != ElysiumWater::ESplash::None)
			{
				OnSplash.ExecuteIfBound(Decision.Kind, Decision.LocationCm);
			}
		}
		Body.VolumeIndex = Volume;

		// Named modernization "vphysics buoyancy" (`ElysiumWaterVolumes.h`). The authored density
		// is VtMB's own kg/m³; a row that authors none floats the body in plain water.
		const float Density = Fluid.Density > 0.f
			? Fluid.Density : ElysiumWater::DefaultFluidDensityKgPerM3;
		const float Displaced = ElysiumWater::DisplacedVolumeM3(Bounds, Fraction);
		Comp->AddForce(
			FVector(0.0, 0.0, ElysiumWater::BuoyantForceZ(Density, Displaced, GravityZ)));
	}
	if (FluidBodies.IsEmpty())
	{
		// The last tracked body went away with its actor rather than through an end-overlap.
		RefreshTickEnabled();
	}
}

#if DEBUG_POST_PROCESS_VOLUME_ENABLE
FString AElysiumWaterVolumes::GetDebugName() const
{
	return FString::Printf(TEXT("ElysiumWaterVolumes (%d volumes)"), Volumes.Num());
}
#endif
