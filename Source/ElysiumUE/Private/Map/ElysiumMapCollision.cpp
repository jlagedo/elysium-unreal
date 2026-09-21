#include "Map/ElysiumMapCollision.h"

#include "Debug/ElysiumPick.h"
#include "ElysiumContentPaths.h"
#include "ElysiumMapCollisionPayload.h"
#include "ElysiumWorldCollisionActor.h"
#include "EngineUtils.h"
#include "ElysiumUseIcons.h"

#include "AI/NavigationSystemBase.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/BodySetup.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCollision, Log, All);

// Adopt the map's baked world collision (1), or leave the map with no walkable surface (0, for
// debugging noclip flythroughs). The collision matches retail: it includes the invisible PLAYERCLIP
// volumes and drops collision on geometry the designer clipped off. Read at map load, so re-travel
// to apply a value.
static TAutoConsoleVariable<int32> CVarBrushCollision(
	TEXT("elysium.BrushCollision"), 1,
	TEXT("Adopt the baked world collider (1) or skip it (0). Applied at map load."),
	ECVF_Default);

UElysiumMapCollision::UElysiumMapCollision()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UElysiumCollisionOnlyMeshComponent::SetLocalCollisionBounds(const FBox& InBounds)
{
	LocalCollisionBounds = InBounds;
	UpdateBounds();
}

FBoxSphereBounds UElysiumCollisionOnlyMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return LocalCollisionBounds.IsValid
		? FBoxSphereBounds(LocalCollisionBounds.TransformBy(LocalToWorld))
		: Super::CalcBounds(LocalToWorld);
}

const TCHAR* ElysiumCollisionSourceName(EElysiumCollisionSource Source)
{
	switch (Source)
	{
	case EElysiumCollisionSource::Payload: return TEXT("payload");
	default:                               return TEXT("none");
	}
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
	LevelCollision = nullptr;
	Payload = nullptr;
	Source = EElysiumCollisionSource::None;

	if (CVarBrushCollision.GetValueOnGameThread() == 0)
	{
		BuildState = EElysiumCollisionBuildState::Disabled;
		UE_LOG(LogElysiumCollision, Log, TEXT("brush collision disabled for '%s'"), *MapName);
		return false;
	}

	// The cooked payload is the only transport. There is no sidecar fallback and no run-time
	// alternative: a map that cannot answer from its own baked content is a bake that did not
	// happen, and saying so here is worth more than a world quietly assembled from loose text
	// that no bake ever verified (0018 story 21, owner decision 2026-09-20).
	const double Started = FPlatformTime::Seconds();
	if (!AdoptPayload(MapName))
	{
		BuildState = EElysiumCollisionBuildState::Failed;
		FailureReason = FString::Printf(
			TEXT("'%s' has no usable baked world collision; run: "
				"uv run elysium bake map --maps %s"),
			*MapName, *MapName);
		UE_LOG(LogElysiumCollision, Error, TEXT("%s"), *FailureReason);
		return false;
	}
	Source = EElysiumCollisionSource::Payload;

	bBrushCollision = true;
	BuildState = EElysiumCollisionBuildState::Cooking;
	// What the transport cost, kept because it is the number that made the case for baking: the
	// same world took tens of seconds when it was parsed and cooked at load.
	UE_LOG(LogElysiumCollision, Log,
		TEXT("world collider '%s' from %s: %d hulls, %d displacement triangles in %.1f ms"),
		*MapName, ElysiumCollisionSourceName(Source), HullCount, DispTriCount,
		(FPlatformTime::Seconds() - Started) * 1000.0);
	return true;
}

bool UElysiumMapCollision::AdoptLevelCollisionActor(const FString& InMapName,
	const UElysiumMapCollisionPayload& Asset)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}
	AElysiumWorldCollisionActor* Found = nullptr;
	for (TActorIterator<AElysiumWorldCollisionActor> It(World); It; ++It)
	{
		if (Found != nullptr)
		{
			// Two of them is a bake that ran twice. Refuse rather than pick one: the second may
			// carry a different partition, and half a world is worse than none.
			UE_LOG(LogElysiumCollision, Error,
				TEXT("%s: the level carries more than one world-collision actor"), *InMapName);
			return false;
		}
		Found = *It;
	}
	if (Found == nullptr)
	{
		UE_LOG(LogElysiumCollision, Error,
			TEXT("%s: the level carries no world-collision actor; re-run "
				"`uv run elysium bake map --maps %s`"), *InMapName, *InMapName);
		return false;
	}
	if (Found->MapName != InMapName)
	{
		UE_LOG(LogElysiumCollision, Error,
			TEXT("%s: the level's world-collision actor belongs to '%s'"),
			*InMapName, *Found->MapName);
		return false;
	}
	if (Found->Payload != &Asset)
	{
		UE_LOG(LogElysiumCollision, Error,
			TEXT("%s: the level's world-collision actor points at a different payload"),
			*InMapName);
		return false;
	}
	// Against the rows the actor would actually author, not every row: `AuthorFromPayload` skips a
	// signature whose body is empty, so comparing with the full count would make a degenerate row
	// -- a signature staged with no usable hull -- render the level unloadable rather than merely
	// unrepresented.
	int32 Authorable = 0;
	for (const FElysiumSignatureCollisionBody& Row : Asset.GetWorldBodies())
	{
		if (Row.Body != nullptr && Row.HullCount > 0)
		{
			++Authorable;
		}
	}
	if (Found->Bodies.Num() != Authorable)
	{
		UE_LOG(LogElysiumCollision, Error,
			TEXT("%s: the level carries %d world body(ies), the payload authors %d of its %d"),
			*InMapName, Found->Bodies.Num(), Authorable, Asset.GetWorldBodies().Num());
		return false;
	}

	// Identity, not just arity: a payload re-author replaces every body setup, so a level whose
	// actor still points at the previous set would adopt objects the payload no longer owns and
	// stand a world the cook never verified. Counting alone cannot see that.
	TSet<const UBodySetup*> Owned;
	for (const FElysiumSignatureCollisionBody& Row : Asset.GetWorldBodies())
	{
		Owned.Add(Row.Body);
	}
	for (UElysiumWorldCollisionComponent* Component : Found->Bodies)
	{
		if (Component == nullptr || Component->Body == nullptr)
		{
			UE_LOG(LogElysiumCollision, Error,
				TEXT("%s: the level's world-collision actor carries an empty body"), *InMapName);
			return false;
		}
		if (!Owned.Contains(Component->Body))
		{
			UE_LOG(LogElysiumCollision, Error,
				TEXT("%s: the level's world-collision actor points at a body this payload does "
					"not own; re-run `uv run elysium bake map --maps %s`"),
				*InMapName, *InMapName);
			return false;
		}
		UE_LOG(LogElysiumCollision, Log, TEXT("adopted world body %s on '%s'%s"),
			*ElysiumContents::Spell(
				static_cast<EElysiumContentsSignature>(Component->Signature)),
			*Component->GetCollisionProfileName().ToString(),
			Component->CanEverAffectNavigation() ? TEXT(" (cuts the NavMesh)") : TEXT(""));
	}

	// The displacement terrain stands in the level too since 0018 story 21-2, and it has to: it is
	// walkable ground on the maps that have it, and the navigation mesh is cut from this level.
	// A payload that carries displacement and a level that does not is a level baked before that,
	// with a hole in its mesh where its ground is.
	const bool bWantsDisplacement = Asset.GetDisplacement() != nullptr;
	if (bWantsDisplacement != Found->HasDisplacement())
	{
		UE_LOG(LogElysiumCollision, Error,
			TEXT("%s: the payload carries %s displacement and the level's world-collision actor "
				"carries %s; re-run `uv run elysium bake map --maps %s`"),
			*InMapName, bWantsDisplacement ? TEXT("") : TEXT("no"),
			Found->HasDisplacement() ? TEXT("one") : TEXT("none"), *InMapName);
		return false;
	}
	if (Found->Displacement != nullptr && Found->Displacement->Body != Asset.GetDisplacement())
	{
		UE_LOG(LogElysiumCollision, Error,
			TEXT("%s: the level's displacement points at a body this payload does not own; "
				"re-run `uv run elysium bake map --maps %s`"), *InMapName, *InMapName);
		return false;
	}
	LevelCollision = Found;
	return true;
}

bool UElysiumMapCollision::AdoptPayload(const FString& MapName)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return false;
	}
	const FString AssetPath = FElysiumContentPaths::BakedMapCollision(MapName);
	UElysiumMapCollisionPayload* Asset = LoadObject<UElysiumMapCollisionPayload>(
		nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Asset == nullptr)
	{
		UE_LOG(LogElysiumCollision, Error,
			TEXT("%s: no collision payload; run: uv run elysium bake map --maps %s"),
			*AssetPath, *MapName);
		return false;
	}
	// One body per signature is the only shape this build adopts. A version-1 payload carried a
	// single BLOCK_MASK-filtered body that could answer nothing about sight or the pedestrian
	// volume; it is not readable as a world any more, and re-baking is the fix.
	if (Asset->PayloadVersion != ElysiumCollisionPayload::SupportedVersion
		|| Asset->GetWorldBodies().Num() == 0 || Asset->WorldHullCount() == 0)
	{
		UE_LOG(LogElysiumCollision, Error,
			TEXT("%s: payload version %d with %d world body(ies); this build reads version %d "
				"with at least one"),
			*AssetPath, Asset->PayloadVersion, Asset->GetWorldBodies().Num(),
			ElysiumCollisionPayload::SupportedVersion);
		return false;
	}

	// The cook already happened offline; this materialises it (a DDC read in the editor, the
	// package's own buffers in a cooked build) before either component registers, because
	// UPrimitiveComponent creates its body from GetBodySetup() at registration.
	if (!Asset->CreatePhysicsMeshes())
	{
		UE_LOG(LogElysiumCollision, Error,
			TEXT("%s: cooked collision failed to create physics meshes"), *AssetPath);
		return false;
	}

	Payload = Asset;

	// The level's own world-collision actor is the world. Its components are static, saved, and
	// already in the navigation octree, which is what lets a mesh be baked from them and adopted
	// here. Nothing spawns a world component at run time any more: a level without the actor has
	// no mesh either, and both are one re-bake away.
	if (!AdoptLevelCollisionActor(MapName, *Asset))
	{
		return false;
	}
	HullCount = Asset->WorldHullCount();
	// Reported, not built: the displacement stands in the level with the rest of the world since
	// 0018 story 21-2, and `AdoptLevelCollisionActor` has just checked that it is this payload's.
	DispTriCount = Asset->DisplacementTriangleCount();

	UE_LOG(LogElysiumCollision, Log,
		TEXT("cooked collision '%s': %d convex hulls, %d displacement triangles, %d brush bodies"),
		*AssetPath, HullCount, DispTriCount, Asset->BrushBodies.Num());
	return true;
}

EElysiumCollisionBuildState UElysiumMapCollision::GetBuildState() const
{
	if (BuildState != EElysiumCollisionBuildState::Cooking)
	{
		return BuildState;
	}
	// Nothing is left to wait for. Every body this map stands on -- the world's signature bodies
	// and, since 0018 story 21-2, the displacement terrain -- is a component of the level's own
	// saved actor, cooked by the bake and materialised by `CreatePhysicsMeshes` before adoption.
	// This object registers no component of its own any more, so `Cooking` has nobody to ask.
	return EElysiumCollisionBuildState::Ready;
}
