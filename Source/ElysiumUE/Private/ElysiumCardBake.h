#pragma once

#include "CoreMinimal.h"
#include "Hash/xxhash.h"

class UStaticMesh;

// The `<map>.cards` sidecar: surfel-fitted Lumen card representations for one map's runtime-built
// meshes (docs/lumen-coverage-spike.md).
//
// A runtime UStaticMesh has no card representation, so Lumen drops it from the surface cache.
// `FElysiumStaticMeshBuilder::AttachLumenCards` stands in six cards on the mesh bounds, which
// covers a prop but leaks badly on architecture: a box card captures only the nearest surface
// along its axis, so a bucket holding an exterior wall re-emits outdoor light inward. The real
// builder (`IMeshUtilities::GenerateCardRepresentationData`) fits cards to the actual surface, but
// it ray-traces through Embree and exists only in the editor — hence baking offline into a sidecar
// the runtime deserializes, which keeps Embree out of the shipping build entirely.
//
// The bake runs the *actual* runtime build (`-ElysiumCards` loads the map like any other run), so
// the meshes it fits cards to are the meshes the game builds — no second chunker to keep in step.
//
// Entries are addressed by identity, not by position in the file:
//   - a world chunk by its bucket key (cell x/y/z + the face-normal axis index it was binned on)
//   - a prop model by its OBJ stem
// and each carries a content hash of the geometry it was fitted to. A changed cell size moves the
// keys, a re-exported map moves the hashes, and either way the lookup misses and that mesh falls
// back to bounds cards. Nothing silently serves cards fitted to geometry that no longer exists.

// Rolling hash over the geometry a card fit was derived from. Geometry only — materials do not
// affect the fit, so re-texturing a map must not force a re-bake.
struct FElysiumCardHasher
{
	void Add(const FVector& V) { Builder.Update(&V, sizeof(FVector)); }
	void Add(TArrayView<const FVector> Vs) { Builder.Update(Vs.GetData(), Vs.Num() * sizeof(FVector)); }
	void Add(TArrayView<const int32> Is) { Builder.Update(Is.GetData(), Is.Num() * sizeof(int32)); }
	uint64 Finalize() const { return Builder.Finalize().Hash; }

private:
	FXxHash64Builder Builder;
};

// One mesh a bake covers: a world chunk (Key set, Stem empty) or a prop model (Stem set). The map
// actor fills these during a bake run; the harness fits and writes them.
struct FElysiumCardBakeItem
{
	FIntVector4 Key = FIntVector4(0, 0, 0, -1);
	FString Stem;
	uint64 Hash = 0;
	UStaticMesh* Mesh = nullptr;

	bool IsProp() const { return !Stem.IsEmpty(); }
};

// A loaded `<map>.cards`, queried during the map build. Holds each entry's serialized card blob
// rather than a live FCardRepresentationData, so this header does not drag in MeshCardBuild.h.
struct FElysiumCardStore
{
	// Read the sidecar. Returns false (and leaves the store empty) when it is absent, truncated,
	// or written by a different format version — every one of which just means "bounds cards".
	bool Load(const FString& Path);

	bool IsEmpty() const { return World.Num() == 0 && Props.Num() == 0; }
	int32 Num() const { return World.Num() + Props.Num(); }

	// Install this entry's baked cards onto Mesh. False when there is no entry under that
	// identity, or the entry was fitted to different geometry (hash mismatch) — the caller then
	// falls back to AttachLumenCards. Must run before anything renders the mesh: the scene proxy
	// copies the card pointer in its constructor.
	bool InstallWorld(const FIntVector4& Key, uint64 Hash, UStaticMesh* Mesh) const;
	bool InstallProp(const FString& Stem, uint64 Hash, UStaticMesh* Mesh) const;

	// The chunk cell size the world entries were cut at, for logging: a mismatch against
	// elysium.LumenCardCellCm explains a total world miss in one line.
	float CellCm = 0.f;
	int32 MaxCards = 0;
	// Tallied across the build so the map reports coverage once instead of per mesh.
	// Installed = fitted cards actually handed to a mesh; Missed = no entry under that identity
	// (a mesh the bake never saw, or a cell-size change); Stale = the identity exists but was
	// fitted to different geometry (a re-export since the last bake). The last two both mean
	// bounds cards for that mesh, and both are fixed by re-running cards.bat.
	mutable int32 Installed = 0;
	mutable int32 Missed = 0;
	mutable int32 Stale = 0;

private:
	struct FEntry
	{
		uint64 Hash = 0;
		TArray<uint8> Blob;
	};
	bool Install(const FEntry* Entry, uint64 Hash, UStaticMesh* Mesh) const;

	TMap<FIntVector4, FEntry> World;
	TMap<FString, FEntry> Props;
};

// Everything one map load needs from the card path, so the map actor can own it behind a single
// forward declaration: the sidecar it reads from, and the items a bake run records into.
struct FElysiumCardContext
{
	FElysiumCardStore Store;
	// Filled only under `-ElysiumCards`. The meshes are kept alive by the map actor's own
	// WorldChunkMeshes / PropMeshes / PropMeshCache for the map's lifetime, which is why these
	// are raw pointers.
	TArray<FElysiumCardBakeItem> BakeItems;
};

namespace ElysiumCardBake
{
	// True while a `-ElysiumCards` run is building the map. The mesh builders read it to keep CPU
	// access on the built LODs (the card builder reads the LOD index/vertex buffers on the CPU)
	// and to record bake items; nothing else changes, so the bake fits cards to exactly the
	// geometry a normal run produces.
	bool IsBaking();
	void SetBaking(bool bBaking);

#if ELYSIUM_WITH_CARDGEN
	// Fit cards to every item with the editor surfel builder and write the sidecar. Returns the
	// number of meshes fitted; OutFailed counts the ones the builder rejected (they are omitted,
	// so they fall back to bounds cards rather than serving nothing).
	int32 BakeAndWrite(const FString& Path, float CellCm, int32 MaxCards,
		const TArray<FElysiumCardBakeItem>& Items, int32& OutFailed);
#endif
}
