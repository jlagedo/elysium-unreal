#include "Map/ElysiumMapCollision.h"

#include "Debug/ElysiumPick.h"
#include "ElysiumContentPaths.h"
#include "ElysiumUseIcons.h"

#include "AI/NavigationSystemBase.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/BodySetup.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCollision, Log, All);

// Build the world collider from the pipeline's brush sidecars (.hulls convex + .dispcol trimesh)
// (1), or leave the map with no walkable surface (0, for debugging noclip flythroughs). Brush
// collision matches retail: it includes the invisible PLAYERCLIP volumes and drops collision on
// geometry the designer clipped off. Read at map load, so re-travel to apply a value.
static TAutoConsoleVariable<int32> CVarBrushCollision(
	TEXT("elysium.BrushCollision"), 1,
	TEXT("Build the .hulls/.dispcol world collider (1) or skip it (0). Applied at map load."),
	ECVF_Default);

UElysiumMapCollision::UElysiumMapCollision()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UElysiumHullCollisionComponent::SetLocalCollisionBounds(const FBox& InBounds)
{
	LocalCollisionBounds = InBounds;
	UpdateBounds();
}

FBoxSphereBounds UElysiumHullCollisionComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return LocalCollisionBounds.IsValid
		? FBoxSphereBounds(LocalCollisionBounds.TransformBy(LocalToWorld))
		: Super::CalcBounds(LocalToWorld);
}

const TCHAR* ElysiumCollisionBuildStateName(EElysiumCollisionBuildState State)
{
	switch (State)
	{
	case EElysiumCollisionBuildState::Disabled: return TEXT("Disabled");
	case EElysiumCollisionBuildState::Cooking:  return TEXT("Cooking");
	case EElysiumCollisionBuildState::Ready:    return TEXT("Ready");
	case EElysiumCollisionBuildState::Failed:   return TEXT("Failed");
	default:                                    return TEXT("Unknown");
	}
}

bool UElysiumMapCollision::Build(const FString& MapName)
{
	HullCount = 0;
	DispTriCount = 0;
	bBrushCollision = false;
	FailureReason.Reset();
	HullCollision = nullptr;
	DispCollision = nullptr;

	if (CVarBrushCollision.GetValueOnGameThread() == 0)
	{
		BuildState = EElysiumCollisionBuildState::Disabled;
		UE_LOG(LogElysiumCollision, Log, TEXT("brush collision disabled for '%s'"), *MapName);
		return false;
	}

	if (!LoadHulls(MapName))
	{
		BuildState = EElysiumCollisionBuildState::Failed;
		FailureReason = FString::Printf(TEXT("required world collision is missing or empty: %s"),
			*FElysiumContentPaths::MapHulls(MapName));
		UE_LOG(LogElysiumCollision, Error, TEXT("%s"), *FailureReason);
		return false;
	}

	bBrushCollision = true;
	LoadDispCol(MapName);
	BuildState = EElysiumCollisionBuildState::Cooking;
	return true;
}

EElysiumCollisionBuildState UElysiumMapCollision::GetBuildState() const
{
	if (BuildState != EElysiumCollisionBuildState::Cooking)
	{
		return BuildState;
	}

	auto ComponentState = [](const UProceduralMeshComponent* Component)
	{
		if (!Component)
		{
			return EElysiumCollisionBuildState::Ready;
		}
		const UBodySetup* Setup = Component->ProcMeshBodySetup;
		if (!Setup)
		{
			return EElysiumCollisionBuildState::Cooking;
		}
		if (Setup->bFailedToCreatePhysicsMeshes)
		{
			return EElysiumCollisionBuildState::Failed;
		}
		return Setup->bCreatedPhysicsMeshes
			? EElysiumCollisionBuildState::Ready
			: EElysiumCollisionBuildState::Cooking;
	};

	const EElysiumCollisionBuildState HullState = ComponentState(HullCollision);
	const EElysiumCollisionBuildState DispState = ComponentState(DispCollision);
	if (HullState == EElysiumCollisionBuildState::Failed
		|| DispState == EElysiumCollisionBuildState::Failed)
	{
		return EElysiumCollisionBuildState::Failed;
	}
	return HullState == EElysiumCollisionBuildState::Ready
		&& DispState == EElysiumCollisionBuildState::Ready
		? EElysiumCollisionBuildState::Ready
		: EElysiumCollisionBuildState::Cooking;
}

FBox UElysiumMapCollision::GetWorldBounds() const
{
	FBox WorldBox(ForceInit);
	if (HullCollision)
	{
		WorldBox += HullCollision->Bounds.GetBox();
	}
	if (DispCollision)
	{
		WorldBox += DispCollision->Bounds.GetBox();
	}
	return WorldBox;
}

void UElysiumMapCollision::RefreshNavigationData()
{
	if (HullCollision && HullCollision->IsRegistered())
	{
		FNavigationSystem::UpdateComponentData(*HullCollision);
	}
	if (DispCollision && DispCollision->IsRegistered())
	{
		FNavigationSystem::UpdateComponentData(*DispCollision);
	}
}

bool UElysiumMapCollision::LoadHulls(const FString& MapName)
{
	AActor* Owner = GetOwner();
	TArray<FString> Lines;
	if (Owner == nullptr
		|| !FFileHelper::LoadFileToStringArray(Lines, *FElysiumContentPaths::MapHulls(MapName)))
	{
		return false;   // no .hulls sidecar: this map has no brush collider
	}

	// Each line is one solid world brush as a flat, unordered point cloud in Unreal cm:
	// x y z x y z ...  (>= 4 verts). UE builds the convex hull from the points, so order is
	// irrelevant. The sidecar is pre-filtered at export to player-blocking contents
	// (SOLID|WINDOW|GRATE|MOVEABLE|PLAYERCLIP), so invisible clip brushes are in and passable
	// water/monsterclip is out.
	TArray<TArray<FVector>> Hulls;
	Hulls.Reserve(Lines.Num());
	FBox HullBounds(ForceInit);
	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() < 12 || Tok.Num() % 3 != 0)
		{
			continue;   // need >= 4 verts, whole (x,y,z) triples
		}
		TArray<FVector> Verts;
		Verts.Reserve(Tok.Num() / 3);
		for (int32 I = 0; I + 2 < Tok.Num(); I += 3)
		{
			Verts.Emplace(FCString::Atod(*Tok[I]), FCString::Atod(*Tok[I + 1]), FCString::Atod(*Tok[I + 2]));
			HullBounds += Verts.Last();
		}
		Hulls.Add(MoveTemp(Verts));
	}
	if (Hulls.Num() == 0 || !HullBounds.IsValid)
	{
		return false;
	}

	// No render sections (never drawn), simple = convex. One FKConvexElem per solid brush, so pawn
	// capsule sweeps (which query simple collision) hit the brushes and their invisible clip
	// volumes. Cooked async — hundreds of synchronous Chaos cooks stall the game thread, and the
	// map actor's spawn teleport waits for ground before releasing the pawn.
	HullCollision = NewObject<UElysiumHullCollisionComponent>(Owner, TEXT("HullCollision"));
	HullCollision->SetupAttachment(this);
	HullCollision->bUseComplexAsSimpleCollision = false;
	HullCollision->bUseAsyncCooking = true;
	HullCollision->SetCollisionProfileName(TEXT("BlockAll"));
	// .hulls include PLAYERCLIP. BlockAll would steal the +use ray (and the debug pick) from
	// door/button brushes the way unprofiled baked world would — ElysiumPickOnly exists for that.
	HullCollision->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Ignore);
	HullCollision->SetCollisionResponseToChannel(ELYSIUM_PICK_CHANNEL, ECR_Ignore);
	HullCollision->SetLocalCollisionBounds(HullBounds);
	HullCollision->RegisterComponent();

	HullCount = Hulls.Num();
	// Set the whole convex set in one call: SetCollisionConvexMeshes replaces the elements and
	// cooks collision once (AddCollisionConvexMesh would re-cook per hull).
	HullCollision->SetCollisionConvexMeshes(MoveTemp(Hulls));
	UE_LOG(LogElysiumCollision, Log, TEXT("brush collision: %d convex hulls"), HullCount);
	return true;
}

void UElysiumMapCollision::LoadDispCol(const FString& MapName)
{
	AActor* Owner = GetOwner();
	TArray<FString> Lines;
	if (Owner == nullptr
		|| !FFileHelper::LoadFileToStringArray(Lines, *FElysiumContentPaths::MapDispCol(MapName)))
	{
		return;   // no .dispcol: map has no displacements
	}

	// Each line is one collision triangle: 9 Unreal-cm floats = A,B,C. Concave terrain, so it
	// becomes one complex-as-simple trimesh section. Winding is irrelevant (Chaos trimesh is
	// two-sided). The section stays invisible (component visibility off).
	TArray<FVector> Verts;
	TArray<int32> Tris;
	Verts.Reserve(Lines.Num() * 3);
	Tris.Reserve(Lines.Num() * 3);
	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() != 9)
		{
			continue;
		}
		const int32 Base = Verts.Num();
		for (int32 V = 0; V < 3; ++V)
		{
			Verts.Emplace(FCString::Atod(*Tok[V * 3]), FCString::Atod(*Tok[V * 3 + 1]), FCString::Atod(*Tok[V * 3 + 2]));
		}
		Tris.Add(Base);
		Tris.Add(Base + 1);
		Tris.Add(Base + 2);
	}
	if (Tris.Num() == 0)
	{
		return;
	}

	DispCollision = NewObject<UElysiumDispCollisionComponent>(Owner, TEXT("DispCollision"));
	DispCollision->SetupAttachment(this);
	DispCollision->bUseComplexAsSimpleCollision = true;
	DispCollision->bUseAsyncCooking = true;
	DispCollision->SetCollisionProfileName(TEXT("BlockAll"));
	DispCollision->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Ignore);
	DispCollision->SetCollisionResponseToChannel(ELYSIUM_PICK_CHANNEL, ECR_Ignore);
	DispCollision->SetVisibility(false);
	DispCollision->RegisterComponent();

	DispTriCount = Tris.Num() / 3;
	const TArray<FVector> NoNormals;
	const TArray<FVector2D> NoUVs;
	const TArray<FLinearColor> NoColors;
	const TArray<FProcMeshTangent> NoTangents;
	DispCollision->CreateMeshSection_LinearColor(0, Verts, Tris, NoNormals, NoUVs, NoColors, NoTangents, true);
	UE_LOG(LogElysiumCollision, Log, TEXT("displacement collision: %d triangles"), DispTriCount);
}
