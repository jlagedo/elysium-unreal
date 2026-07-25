#include "ElysiumCardBake.h"

#include "Engine/StaticMesh.h"
#include "Hash/xxhash.h"
#include "MeshCardBuild.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "StaticMeshResources.h"

#if ELYSIUM_WITH_CARDGEN
#include "MeshUtilities.h"
#include "Modules/ModuleManager.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCards, Log, All);

namespace
{
	// 'ECRD'. Bumping Version invalidates every sidecar, which is the right response to a format
	// change: the bake is reproducible from the export, so there is nothing to migrate.
	constexpr uint32 CardsMagic = 0x44524345;
	constexpr uint32 CardsVersion = 1;

	bool bBakingNow = false;
}

// --- the sidecar -----------------------------------------------------------------------------

namespace
{
	void SerializeEntryBody(FArchive& Ar, uint64& Hash, TArray<uint8>& Blob)
	{
		Ar << Hash;
		Ar << Blob;
	}

	void SerializeKey(FArchive& Ar, FIntVector4& Key)
	{
		Ar << Key.X << Key.Y << Key.Z << Key.W;
	}
}

bool FElysiumCardStore::Load(const FString& Path)
{
	World.Reset();
	Props.Reset();
	CellCm = 0.f;
	MaxCards = 0;
	Installed = 0;
	Missed = 0;
	Stale = 0;

	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *Path))
	{
		return false;   // no bake for this map: bounds cards everywhere
	}

	FMemoryReader Ar(Data);
	uint32 Magic = 0;
	uint32 Version = 0;
	Ar << Magic << Version;
	if (Magic != CardsMagic || Version != CardsVersion)
	{
		UE_LOG(LogElysiumCards, Warning,
			TEXT("cards: %s has magic/version %08x/%u (want %08x/%u) — ignoring, re-run cards.bat"),
			*FPaths::GetCleanFilename(Path), Magic, Version, CardsMagic, CardsVersion);
		return false;
	}

	Ar << CellCm << MaxCards;

	int32 WorldCount = 0;
	Ar << WorldCount;
	for (int32 I = 0; I < WorldCount && !Ar.IsError(); ++I)
	{
		FIntVector4 Key(0, 0, 0, -1);
		FEntry Entry;
		SerializeKey(Ar, Key);
		SerializeEntryBody(Ar, Entry.Hash, Entry.Blob);
		World.Add(Key, MoveTemp(Entry));
	}

	int32 PropCount = 0;
	Ar << PropCount;
	for (int32 I = 0; I < PropCount && !Ar.IsError(); ++I)
	{
		FString Stem;
		FEntry Entry;
		Ar << Stem;
		SerializeEntryBody(Ar, Entry.Hash, Entry.Blob);
		Props.Add(Stem, MoveTemp(Entry));
	}

	if (Ar.IsError())
	{
		UE_LOG(LogElysiumCards, Warning, TEXT("cards: %s is truncated — ignoring, re-run cards.bat"),
			*FPaths::GetCleanFilename(Path));
		World.Reset();
		Props.Reset();
		return false;
	}

	UE_LOG(LogElysiumCards, Log, TEXT("cards: %s — %d world chunks + %d prop models (cell %.0f cm, max %d)"),
		*FPaths::GetCleanFilename(Path), World.Num(), Props.Num(), CellCm, MaxCards);
	return true;
}

bool FElysiumCardStore::Install(const FEntry* Entry, uint64 Hash, UStaticMesh* Mesh) const
{
	if (Entry == nullptr)
	{
		++Missed;
		return false;
	}
	if (Entry->Hash != Hash)
	{
		++Stale;   // the identity still exists but its geometry moved: re-bake, don't guess
		return false;
	}

	FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
	if (!RenderData || RenderData->LODResources.Num() == 0)
	{
		++Missed;
		return false;
	}
	FStaticMeshLODResources& LOD = RenderData->LODResources[0];
	if (LOD.CardRepresentationData)
	{
		return true;   // already carries cards (a cached mesh reused across entities)
	}

	// The LOD owns the allocation from here on (~FStaticMeshLODResources deletes it).
	TUniquePtr<FCardRepresentationData> CardData = MakeUnique<FCardRepresentationData>();
	FMemoryReader Ar(Entry->Blob);
	Ar << CardData->MeshCardsBuildData;
	if (Ar.IsError() || CardData->ContainsNaN())
	{
		++Missed;
		return false;
	}

	LOD.CardRepresentationData = CardData.Release();
	++Installed;
	return true;
}

bool FElysiumCardStore::InstallWorld(const FIntVector4& Key, uint64 Hash, UStaticMesh* Mesh) const
{
	return Install(World.Find(Key), Hash, Mesh);
}

bool FElysiumCardStore::InstallProp(const FString& Stem, uint64 Hash, UStaticMesh* Mesh) const
{
	return Install(Props.Find(Stem), Hash, Mesh);
}

// --- the bake --------------------------------------------------------------------------------

bool ElysiumCardBake::IsBaking()
{
	return bBakingNow;
}

void ElysiumCardBake::SetBaking(bool bBaking)
{
	bBakingNow = bBaking;
}

#if ELYSIUM_WITH_CARDGEN

namespace
{
	// Run the editor surfel builder over one runtime-built mesh and serialize the result.
	// Mirrors elysium.cards.probe, which is what established that this works on a mesh
	// constructed in code rather than imported as an asset.
	bool FitOne(IMeshUtilities& MeshUtilities, UStaticMesh* Mesh, int32 MaxCards,
		TArray<uint8>& OutBlob, int32& OutNumCards)
	{
		FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
		if (!RenderData || RenderData->LODResources.Num() == 0)
		{
			return false;
		}
		const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
		if (LOD.IndexBuffer.GetNumIndices() < 3)
		{
			return false;
		}

		TArray<FSignedDistanceFieldBuildSectionData> SectionData;
		SectionData.SetNum(FMath::Max(1, LOD.Sections.Num()));
		for (FSignedDistanceFieldBuildSectionData& SD : SectionData)
		{
			// The fit is geometric: the surfel pass only needs to know which triangles exist and
			// whether to treat them as two-sided. Our world is opaque brush faces.
			SD.BlendMode = BLEND_Opaque;
			SD.bTwoSided = false;
			SD.bAffectDistanceFieldLighting = true;
		}

		FMeshDataForDerivedDataTask MeshData;
		MeshData.SourceMeshData = nullptr;
		MeshData.LODModel = &LOD;
		MeshData.SectionData = SectionData;
		MeshData.Bounds = (FBoxSphereBounds3f)RenderData->Bounds;

		FCardRepresentationData CardData;
		// No distance field: card generation treats it as optional and falls back to the mesh
		// bounds, which is what lets an HWRT-only project skip the SDF build entirely.
		if (!MeshUtilities.GenerateCardRepresentationData(
				TEXT("ElysiumCards"), MeshData, /*DistanceFieldVolumeData=*/nullptr,
				MaxCards, /*bGenerateAsIfTwoSided=*/false, CardData))
		{
			return false;
		}

		OutNumCards = CardData.MeshCardsBuildData.CardBuildData.Num();
		OutBlob.Reset();
		FMemoryWriter Ar(OutBlob);
		Ar << CardData.MeshCardsBuildData;
		return !Ar.IsError();
	}
}

int32 ElysiumCardBake::BakeAndWrite(const FString& Path, float CellCm, int32 MaxCards,
	const TArray<FElysiumCardBakeItem>& Items, int32& OutFailed)
{
	OutFailed = 0;

	IMeshUtilities& MeshUtilities =
		FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));

	struct FBaked { const FElysiumCardBakeItem* Item; TArray<uint8> Blob; };
	TArray<FBaked> WorldBaked;
	TArray<FBaked> PropBaked;
	int32 TotalCards = 0;

	// A model can be built more than once in a map load (the GAME_LUMP ISM path and a
	// prop_dynamic entity keep separate caches), and the fit is the expensive part — so each
	// identity is fitted once.
	TSet<FString> SeenStems;
	TSet<FIntVector4> SeenKeys;

	const double Start = FPlatformTime::Seconds();
	for (const FElysiumCardBakeItem& Item : Items)
	{
		bool bAlready = false;
		if (Item.IsProp())
		{
			SeenStems.Add(Item.Stem, &bAlready);
		}
		else
		{
			SeenKeys.Add(Item.Key, &bAlready);
		}
		if (bAlready)
		{
			continue;
		}

		TArray<uint8> Blob;
		int32 NumCards = 0;
		if (!FitOne(MeshUtilities, Item.Mesh, MaxCards, Blob, NumCards))
		{
			// Omitted rather than written empty: a missing entry falls back to bounds cards,
			// which is strictly better than serving a mesh no cards at all.
			UE_LOG(LogElysiumCards, Warning, TEXT("cards: fit failed for %s"),
				Item.IsProp() ? *Item.Stem
				              : *FString::Printf(TEXT("chunk %d,%d,%d dir %d"),
					              Item.Key.X, Item.Key.Y, Item.Key.Z, Item.Key.W));
			++OutFailed;
			continue;
		}
		TotalCards += NumCards;
		(Item.IsProp() ? PropBaked : WorldBaked).Add({ &Item, MoveTemp(Blob) });
	}
	const double FitSeconds = FPlatformTime::Seconds() - Start;

	TArray<uint8> Data;
	FMemoryWriter Ar(Data);
	uint32 Magic = CardsMagic;
	uint32 Version = CardsVersion;
	float Cell = CellCm;
	int32 Max = MaxCards;
	Ar << Magic << Version << Cell << Max;

	int32 WorldCount = WorldBaked.Num();
	Ar << WorldCount;
	for (FBaked& B : WorldBaked)
	{
		FIntVector4 Key = B.Item->Key;
		uint64 Hash = B.Item->Hash;
		SerializeKey(Ar, Key);
		SerializeEntryBody(Ar, Hash, B.Blob);
	}

	int32 PropCount = PropBaked.Num();
	Ar << PropCount;
	for (FBaked& B : PropBaked)
	{
		FString Stem = B.Item->Stem;
		uint64 Hash = B.Item->Hash;
		Ar << Stem;
		SerializeEntryBody(Ar, Hash, B.Blob);
	}

	if (!FFileHelper::SaveArrayToFile(Data, *Path))
	{
		UE_LOG(LogElysiumCards, Error, TEXT("cards: could not write %s"), *Path);
		return 0;
	}

	const int32 Written = WorldBaked.Num() + PropBaked.Num();
	UE_LOG(LogElysiumCards, Log,
		TEXT("cards: wrote %s — %d chunks + %d props, %d cards total, %d failed, %.1f s fit, %.1f KB"),
		*FPaths::GetCleanFilename(Path), WorldBaked.Num(), PropBaked.Num(), TotalCards, OutFailed,
		FitSeconds, Data.Num() / 1024.f);
	return Written;
}

#endif   // ELYSIUM_WITH_CARDGEN
