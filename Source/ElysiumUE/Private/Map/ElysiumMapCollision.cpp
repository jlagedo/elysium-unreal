#include "Map/ElysiumMapCollision.h"

#include "Debug/ElysiumPick.h"
#include "ElysiumContentPaths.h"
#include "ElysiumMapCollisionPayload.h"
#include "ElysiumMapTransportSettings.h"
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
	case EElysiumCollisionSource::Sidecar: return TEXT("sidecar");
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
	HullsBySignature.Reset();
	HullCollision = nullptr;
	DispCollision = nullptr;
	Payload = nullptr;
	Source = EElysiumCollisionSource::None;

	if (CVarBrushCollision.GetValueOnGameThread() == 0)
	{
		BuildState = EElysiumCollisionBuildState::Disabled;
		UE_LOG(LogElysiumCollision, Log, TEXT("brush collision disabled for '%s'"), *MapName);
		return false;
	}

	// Cooked content first, the loose sidecars second — whether this map is on the payload path at
	// all is R4.6's explicit `UElysiumMapTransportSettings` list, not the payload's own presence.
	const double Started = FPlatformTime::Seconds();
	if (AdoptPayload(MapName))
	{
		Source = EElysiumCollisionSource::Payload;
	}
	else if (LoadHulls(MapName))
	{
		Source = EElysiumCollisionSource::Sidecar;
		LoadDispCol(MapName);
	}
	else
	{
		BuildState = EElysiumCollisionBuildState::Failed;
		FailureReason = FString::Printf(TEXT("required world collision is missing or empty: %s"),
			*FElysiumContentPaths::MapHulls(MapName));
		UE_LOG(LogElysiumCollision, Error, TEXT("%s"), *FailureReason);
		return false;
	}

	bBrushCollision = true;
	BuildState = EElysiumCollisionBuildState::Cooking;
	// The one number that says what the transport cost: the payload path reads a cooked asset, the
	// sidecar path parses text and schedules a Chaos cook, and this is where the two are comparable.
	UE_LOG(LogElysiumCollision, Log,
		TEXT("world collider '%s' from %s: %d hulls, %d displacement triangles in %.1f ms"),
		*MapName, ElysiumCollisionSourceName(Source), HullCount, DispTriCount,
		(FPlatformTime::Seconds() - Started) * 1000.0);
	return true;
}

UElysiumHullCollisionComponent* UElysiumMapCollision::MakeHullComponent(AActor* Owner,
	const FBox& LocalBounds, EElysiumContentsSignature Signature)
{
	// No render sections (never drawn), simple = convex. One FKConvexElem per brush, so pawn
	// capsule sweeps (which query simple collision) hit the brushes and their invisible clip
	// volumes.
	//
	// The profile is the signature's own, which is what makes a brush answer each retail mask
	// separately — and what decides navigation, since Unreal calls a body navigation-relevant
	// exactly when it blocks ECC_Pawn. The profile already says Ignore on the +use and pick
	// channels, so no per-channel fixup follows it any more.
	const FName ProfileName = ElysiumContents::ProfileName(Signature);
	UElysiumHullCollisionComponent* Component = NewObject<UElysiumHullCollisionComponent>(
		Owner, FName(*FString::Printf(TEXT("HullCollision_%s"),
			*ElysiumContents::Spell(Signature))));
	Component->SetupAttachment(this);
	Component->bUseComplexAsSimpleCollision = false;
	Component->bUseAsyncCooking = true;
	Component->SetCollisionProfileName(ProfileName);
	Component->SetLocalCollisionBounds(LocalBounds);
	return Component;
}

UElysiumDispCollisionComponent* UElysiumMapCollision::MakeDispComponent(AActor* Owner)
{
	UElysiumDispCollisionComponent* Component =
		NewObject<UElysiumDispCollisionComponent>(Owner, TEXT("DispCollision"));
	Component->SetupAttachment(this);
	Component->bUseComplexAsSimpleCollision = true;
	Component->bUseAsyncCooking = true;
	Component->SetCollisionProfileName(TEXT("BlockAll"));
	Component->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Ignore);
	Component->SetCollisionResponseToChannel(ELYSIUM_PICK_CHANNEL, ECR_Ignore);
	Component->SetVisibility(false);
	return Component;
}

bool UElysiumMapCollision::AdoptPayload(const FString& MapName)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return false;
	}
	// R4.6: an unlisted map never attempts the payload, even if one exists on disk -- the tracked
	// flag list, not asset presence, decides the transport from here forward.
	if (!ElysiumMapTransport::IsMapOnNewTransport(MapName))
	{
		return false;
	}

	const FString AssetPath = FElysiumContentPaths::BakedMapCollision(MapName);
	// Quiet: a listed map whose payload is not yet baked falls back to the sidecar readers rather
	// than warning.
	UElysiumMapCollisionPayload* Asset = LoadObject<UElysiumMapCollisionPayload>(
		nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	// A world is required, and it may arrive either shape: one body per signature (version 2) or
	// the single player-solid set (version 1).
	const bool bHasWorld = Asset != nullptr
		&& (Asset->GetWorldBodies().Num() > 0 || Asset->GetWorldHulls() != nullptr)
		&& Asset->WorldHullCount() > 0;
	if (!bHasWorld)
	{
		return false;
	}
	if (Asset->PayloadVersion > ElysiumCollisionPayload::SupportedVersion)
	{
		// Newer than this build understands: refuse rather than adopt a world whose shape is only
		// half read. Falling back to the sidecar would be worse — it would look like it worked.
		UE_LOG(LogElysiumCollision, Error,
			TEXT("%s: payload version %d is newer than this build supports (%d)"),
			*AssetPath, Asset->PayloadVersion, ElysiumCollisionPayload::SupportedVersion);
		return false;
	}

	// The cook already happened offline; this materialises it (a DDC read in the editor, the
	// package's own buffers in a cooked build) before either component registers, because
	// UPrimitiveComponent creates its body from GetBodySetup() at registration.
	if (!Asset->CreatePhysicsMeshes())
	{
		UE_LOG(LogElysiumCollision, Error,
			TEXT("%s: cooked collision failed to create physics meshes; falling back to the sidecars"),
			*AssetPath);
		return false;
	}

	Payload = Asset;
	if (Asset->GetWorldBodies().Num() > 0)
	{
		// A version-2 payload carries one cooked body per contents signature, so the adopted world
		// partitions exactly as the sidecar path's does — same profiles, same navigation answer.
		for (const FElysiumSignatureCollisionBody& Row : Asset->GetWorldBodies())
		{
			const EElysiumContentsSignature Signature =
				static_cast<EElysiumContentsSignature>(Row.Signature);
			UElysiumHullCollisionComponent* Component =
				MakeHullComponent(Owner, Row.Bounds, Signature);
			Component->ProcMeshBodySetup = Row.Body;
			Component->RegisterComponent();
			HullsBySignature.Add(Row.Signature, Component);
			if (HullCollision == nullptr)
			{
				HullCollision = Component;
			}
		}
	}
	else
	{
		// A version-1 payload cooked ONE body, from the BLOCK_MASK-filtered `.hulls` it was staged
		// from, so it can only be given the signature that set stands for: blocks both pawns, and
		// answers nothing about sight. That is exactly what this body did as `BlockAll` — the
		// sight channel defaults to Ignore — so an unconverted payload behaves as it did before.
		const EElysiumContentsSignature Signature = static_cast<EElysiumContentsSignature>(
			UElysiumMapCollisionPayload::LegacyWorldSignature());
		HullCollision = MakeHullComponent(Owner, Asset->WorldHullBounds(), Signature);
		HullCollision->ProcMeshBodySetup = Asset->GetWorldHulls();
		HullCollision->RegisterComponent();
		HullsBySignature.Add(static_cast<uint8>(Signature), HullCollision);
	}
	HullCount = Asset->WorldHullCount();

	if (UBodySetup* DispSetup = Asset->GetDisplacement())
	{
		DispCollision = MakeDispComponent(Owner);
		DispCollision->SetLocalCollisionBounds(Asset->DisplacementBounds());
		DispCollision->ProcMeshBodySetup = DispSetup;
		DispCollision->RegisterComponent();
		DispTriCount = Asset->DisplacementTriangleCount();
	}

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

	// Every signature body must be ready, not just the first: an NPC-only clip still cooking is a
	// hole an NPC can walk through.
	EElysiumCollisionBuildState HullState = HullsBySignature.IsEmpty()
		? ComponentState(HullCollision)
		: EElysiumCollisionBuildState::Ready;
	for (const TPair<uint8, TObjectPtr<UElysiumHullCollisionComponent>>& Entry : HullsBySignature)
	{
		const EElysiumCollisionBuildState State = ComponentState(Entry.Value);
		if (State == EElysiumCollisionBuildState::Failed)
		{
			HullState = EElysiumCollisionBuildState::Failed;
			break;
		}
		if (State == EElysiumCollisionBuildState::Cooking)
		{
			HullState = EElysiumCollisionBuildState::Cooking;
		}
	}

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
	// The union of every signature, including the bodies that stop nothing: the nav bounds must
	// cover the whole playable volume, and a sight-only brush still stands inside it.
	for (const TPair<uint8, TObjectPtr<UElysiumHullCollisionComponent>>& Entry : HullsBySignature)
	{
		if (Entry.Value)
		{
			WorldBox += Entry.Value->Bounds.GetBox();
		}
	}
	if (HullsBySignature.IsEmpty() && HullCollision)
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
	for (const TPair<uint8, TObjectPtr<UElysiumHullCollisionComponent>>& Entry : HullsBySignature)
	{
		if (Entry.Value && Entry.Value->IsRegistered())
		{
			FNavigationSystem::UpdateComponentData(*Entry.Value);
		}
	}
	if (HullsBySignature.IsEmpty() && HullCollision && HullCollision->IsRegistered())
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

	// Each line is one world brush: its CONTENTS word as `0x%08x`, then a flat, unordered point
	// cloud in Unreal cm (x y z x y z ..., >= 4 verts). UE builds the convex hull from the points,
	// so order is irrelevant.
	//
	// The sidecar carries every brush answering ANY retail mask, not just the player-solid ones,
	// and the contents word is what tells them apart: the brushes are partitioned by signature and
	// each partition gets a body wearing that signature's profile. That is how an NPC-only clip
	// comes to stop an NPC and not the player, and how a sight-only brush stops neither pawn.
	//
	// An older sidecar wrote bare coordinates, so its rows are a multiple of three tokens and the
	// leading token does not parse as hex. Such a file is refused outright rather than read as if
	// its first coordinate were a contents word — re-export the map.
	TMap<uint8, TArray<TArray<FVector>>> BySignature;
	TMap<uint8, FBox> BoundsBySignature;
	int32 Parsed = 0;
	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() < 13 || Tok.Num() % 3 != 1 || !Tok[0].StartsWith(TEXT("0x")))
		{
			continue;   // need a contents word and >= 4 whole (x,y,z) triples
		}
		const uint32 Contents = FParse::HexNumber(*Tok[0].Mid(2));
		const EElysiumContentsSignature Signature = ElysiumContents::SignatureOf(Contents);
		if (Signature == EElysiumContentsSignature::None)
		{
			continue;   // answers no mask: it was never staged, and cannot be a body
		}
		TArray<FVector> Verts;
		Verts.Reserve((Tok.Num() - 1) / 3);
		FBox& SignatureBounds =
			BoundsBySignature.FindOrAdd(static_cast<uint8>(Signature), FBox(ForceInit));
		for (int32 I = 1; I + 2 < Tok.Num(); I += 3)
		{
			Verts.Emplace(FCString::Atod(*Tok[I]), FCString::Atod(*Tok[I + 1]),
				FCString::Atod(*Tok[I + 2]));
			SignatureBounds += Verts.Last();
		}
		BySignature.FindOrAdd(static_cast<uint8>(Signature)).Add(MoveTemp(Verts));
		++Parsed;
	}
	if (Parsed == 0)
	{
		UE_LOG(LogElysiumCollision, Error,
			TEXT("'%s' carries no readable brush rows; a sidecar written before the contents "
				"column was added must be re-exported"), *MapName);
		return false;
	}

	// Cooked async — hundreds of synchronous Chaos cooks stall the game thread, and the map actor's
	// spawn teleport waits for ground before releasing the pawn. (R4.2's payload path has no cook
	// to schedule; this one is the fallback for an unconverted map.)
	HullCount = Parsed;
	for (TPair<uint8, TArray<TArray<FVector>>>& Partition : BySignature)
	{
		const EElysiumContentsSignature Signature =
			static_cast<EElysiumContentsSignature>(Partition.Key);
		const FBox& SignatureBounds = BoundsBySignature[Partition.Key];
		if (!SignatureBounds.IsValid)
		{
			continue;
		}
		UElysiumHullCollisionComponent* Component =
			MakeHullComponent(Owner, SignatureBounds, Signature);
		Component->RegisterComponent();
		// Set the whole convex set in one call: SetCollisionConvexMeshes replaces the elements and
		// cooks collision once (AddCollisionConvexMesh would re-cook per hull).
		const int32 Count = Partition.Value.Num();
		Component->SetCollisionConvexMeshes(MoveTemp(Partition.Value));
		HullsBySignature.Add(Partition.Key, Component);
		if (HullCollision == nullptr)
		{
			HullCollision = Component;
		}
		UE_LOG(LogElysiumCollision, Log,
			TEXT("brush collision %s: %d convex hulls on profile '%s'%s"),
			*ElysiumContents::Spell(Signature), Count,
			*ElysiumContents::ProfileName(Signature).ToString(),
			ElysiumContents::AffectsNavigation(Signature) ? TEXT(" (cuts the NavMesh)") : TEXT(""));
	}
	if (HullsBySignature.Num() == 0)
	{
		return false;
	}
	UE_LOG(LogElysiumCollision, Log, TEXT("brush collision: %d convex hulls in %d signature(s)"),
		HullCount, HullsBySignature.Num());
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

	// The render section this path creates bounds the component; the payload path has none, which
	// is why it states the trimesh AABB explicitly instead.
	DispCollision = MakeDispComponent(Owner);
	DispCollision->RegisterComponent();

	DispTriCount = Tris.Num() / 3;
	const TArray<FVector> NoNormals;
	const TArray<FVector2D> NoUVs;
	const TArray<FLinearColor> NoColors;
	const TArray<FProcMeshTangent> NoTangents;
	DispCollision->CreateMeshSection_LinearColor(0, Verts, Tris, NoNormals, NoUVs, NoColors, NoTangents, true);
	UE_LOG(LogElysiumCollision, Log, TEXT("displacement collision: %d triangles"), DispTriCount);
}
