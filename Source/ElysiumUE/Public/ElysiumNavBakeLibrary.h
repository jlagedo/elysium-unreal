#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumNavBakeLibrary.generated.h"

class UWorld;

/** What one agent's Recast build cost and produced, so the bake can report it rather than guess. */
USTRUCT(BlueprintType)
struct FElysiumNavAgentBuild
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Nav") FString Agent;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Nav") int32 Tiles = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Nav") int32 Bytes = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Nav") float Seconds = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Nav") float AgentRadius = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Nav") float AgentHeight = 0.0f;
};

/**
 * The navigation half of `bake map`: give a level the agents its own graph needs, build their
 * Recast meshes in the editor, and save them with the level.
 *
 * Why this exists at all. Navigation is generated at RUN time today -- one mesh, at whatever agent
 * the engine defaults to, rebuilt on every load of every map. Nothing about it can be inspected
 * offline, a jump link points at a mesh that does not exist until the map is playing, and the
 * agent has no relation to any hull retail ships. A mesh that is baked is a mesh that can be
 * measured, and the acceptance this story owes -- every ground link paths on its agent's mesh, the
 * rat-only links path on the rat's and not the human's -- can only be asked of a mesh that exists
 * before the game runs.
 *
 * Which agents a map builds is the map's own answer, not the project's: `UsedHullBits` in its nav
 * graph is an OR of the hulls its links were built for, and both witness maps are `0x80001` --
 * human and rat. The project declares all 14 agents that carry links anywhere; a map builds the
 * ones its graph names and no others, so no map pays for a Ming Xiao mesh it can never use.
 */
UCLASS()
class ELYSIUMUE_API UElysiumNavBakeLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Restrict `World` to the agents named by `HullBits` (bit N is hull N, as `UsedHullBits`
	 * spells it) and give it a navigation system configured for exactly those.
	 *
	 * Returns the agent names that survived, or empty when the world has no navigation system or
	 * the mask names no supported agent. A hull the project does not declare an agent for is
	 * reported and skipped rather than silently dropped: that is a map whose graph uses a hull
	 * carrying no links anywhere, which should not be possible and is worth failing over.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Nav")
	static TArray<FString> SetMapNavAgents(UWorld* World, int32 HullBits);

	/**
	 * Build every navigation mesh the world now supports, synchronously, and report each one.
	 *
	 * `UNavigationSystemV1::Build` ends in `EnsureBuildCompletion`, so this returns with the tiles
	 * actually built rather than with a build queued -- which is what makes saving the level
	 * immediately afterwards meaningful. The costs come back measured, because the rat's 5 cm cell
	 * on a map the size of the hub is the one number this story is asked to accept rather than
	 * avoid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Nav")
	static TArray<FElysiumNavAgentBuild> BuildAgentNavMeshes(UWorld* World);

	/**
	 * Place a navigation bounds volume covering `BoundsCm`, so the built meshes cover the playable
	 * world. Returns false when the world has no navigation system or the bounds are empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Nav")
	static bool PlaceNavBounds(UWorld* World, const FBox& BoundsCm);

	/** The agent names the project declares, in `SupportedAgents` order -- the bake's own check
	 *  that the generated ini and the generated hull table still agree. */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Nav")
	static TArray<FString> SupportedAgentNames(UWorld* World);
};
