#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumMapBakeLibrary.generated.h"

class UStaticMesh;
class UWorld;

/**
 * The editor-only questions `pipeline/unreal/bake_map.py` asks about a level's reflection captures
 * (R5.5, `docs/architecture/seam_map_map.md` -> "## Import -- reflection captures (R5.5)"), which
 * have no Python scripting surface of their own: rendering the placed captures' contents into the
 * level's `UMapBuildDataRegistry`, and counting how many captures actually carry that data.
 *
 * The build is `UEditorEngine::BuildReflectionCaptures` -- the same call the editor's Build ->
 * Reflection Captures menu makes (waits for pending shader/asset compiles, refreshes the sky
 * captures, then `UReflectionCaptureComponent::UpdateReflectionCaptureContents`) -- so the bake's
 * capture and a hand build in the editor are one computation asked twice. It needs a rendering
 * commandlet (`-AllowCommandletRendering`), which `unreal.bake_maps` already launches with.
 *
 * Beside them, the four things that same commandlet must do to hand memory back between maps and
 * that Python has no way to say: simulate an engine frame, finish the async compiles, drop a
 * built mesh's source geometry, and unload the packages the finished map is done with. A
 * `-run=pythonscript` process never reaches `FEngineLoop::Tick`, so without these the bake's
 * freed pages stay committed and every mesh it authored keeps its source description resident --
 * the resident set then grows map over map until D3D12 refuses an upload heap.
 */
UCLASS()
class ELYSIUMUE_API UElysiumMapBakeLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Build every reflection capture in `World` into its level's MapBuildData and return how many
	 * capture components then carry built data (`CountBuiltReflectionCaptures`). Returns -1 when
	 * the build cannot run at all: null world, or no editor engine (a `-game` process). The caller
	 * compares the count against what it placed; a short count is a bake failure, never a warning.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Map Bake")
	static int32 BuildReflectionCaptures(UWorld* World);

	/**
	 * How many reflection capture components in `World`'s persistent level resolve their
	 * `MapBuildDataId` to a registry entry with a rendered cube (`CubemapSize > 0` and captured
	 * bytes). `OutComponents` receives the number of capture components found at all, so a caller
	 * can tell "none built" from "none placed". Reads the level's own `MapBuildData` directly,
	 * so it works on a world loaded from its package without a world context (the Content test)
	 * as well as on the live editor world (the bake and `bake_verify.py`). Null world -> 0 / 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Map Bake")
	static int32 CountBuiltReflectionCaptures(UWorld* World, int32& OutComponents);

	/**
	 * Simulate `Frames` engine frames through `CommandletHelpers::TickEngine` (Engine's own
	 * commandlet frame simulation: `GEngine->Tick`, the task graph, the core ticker, then -- with
	 * `-AllowCommandletRendering` and a world that has a scene -- a Begin/EndFrame pair whose
	 * `RHICmdList.EndFrame()` is followed by `FlushRenderingCommands`).
	 *
	 * This is the only path to `RHIEndFrame` in a `-run=` commandlet: nothing there runs
	 * `FEngineLoop::Tick`, and the D3D12 pool allocator queues every free behind the frame fence
	 * and drains it only in `RHIEndFrame`. Until a frame ticks, a released upload-heap page stays
	 * committed, which is how the map bake reached `E_OUTOFMEMORY` on heap type UPLOAD with the
	 * assets themselves long collected.
	 *
	 * Ticked in a small run rather than once because the D3D12 fast allocator retires at most one
	 * page per frame, so a single frame drains one page's worth of the backlog.
	 *
	 * The world is `GWorld` -- the editor world the commandlet is baking into -- and it must be
	 * the editor world context's: `TickEngine` guards only its rendering half against a missing
	 * world, while `UEditorEngine::Tick` asserts on both a null `GWorld` and one that is not the
	 * editor context's. No such world therefore ticks nothing at all rather than a cheaper frame.
	 * A world with no scene (every `-nullrhi` run) does tick, minus the rendering half.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Map Bake")
	static void TickCommandletFrames(int32 Frames);

	/**
	 * Block until every async asset and shader compile has finished, then flush the rendering
	 * commands they queued. Async texture and static-mesh compilation is on by default in a
	 * commandlet and nothing there pumps it, so a bake that never asks holds every in-flight
	 * build's source data resident and hands the collector nothing to free.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Map Bake")
	static void FinishAssetCompilation();

	/**
	 * Drop `Mesh`'s cached source `FMeshDescription`s (`UStaticMesh::ClearMeshDescriptions`).
	 * `UStaticMesh::PostLoad` does this after it builds, but `UStaticMesh::Build` -- the path a
	 * mesh authored in the bake takes -- does not, so every mesh the bake creates keeps its full
	 * source geometry in memory for the life of the process. Call it once the mesh's package is
	 * saved: the descriptions are on disk in the package's bulk data, and anything that needs them
	 * again reloads them from there. Null mesh is a no-op.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Map Bake")
	static void ReleaseMeshSourceData(UStaticMesh* Mesh);

	/**
	 * Unload every clean loaded package whose name is `PackagePath` or sits under it, and return
	 * how many of them were actually gone once the collect finished.
	 *
	 * A map bake resolves thousands of packages -- its own output plus every corpus texture,
	 * material instance and prop mesh it binds -- and each one carries `RF_Standalone`, which is
	 * exactly what `GARBAGE_COLLECTION_KEEPFLAGS` keeps while `GIsEditor`. So a collect between
	 * maps takes none of them and the batch's resident set is the union of every map it ran.
	 *
	 * The work is `UPackageTools::UnloadPackages`, the engine's own path (what
	 * `unreal.EditorLoadingAndSavingUtils.unload_packages` forwards to): it flushes async loading
	 * and the compiles that own the packages' build data, closes the current world through
	 * `CreateNewMapForEditing` when the world being unloaded is the editor's, clears
	 * `RF_Standalone`, `ResetLoaders` (so the linkers and their bulk-data handles go too) and
	 * collects -- restoring the flag afterwards on whatever turned out to still be reachable, so a
	 * package that could not be freed is left whole rather than half-released. A dirty package is
	 * unsaved output, not slack, and is never offered.
	 *
	 * The gather is here rather than in the caller because a Python-held `unreal` wrapper is a
	 * root for the collector for as long as it lives: a script that listed these packages to pass
	 * them in would pin the very set it is asking to free.
	 *
	 * `PackagePath` is matched on the path separator, so `/ElysiumBaked` claims nothing from a
	 * mount that merely starts with those letters.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Map Bake")
	static int32 UnloadBakedPackages(const FString& PackagePath);
};
