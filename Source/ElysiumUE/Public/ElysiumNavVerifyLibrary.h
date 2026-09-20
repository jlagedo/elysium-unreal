#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumNavVerifyLibrary.generated.h"

/**
 * Asking a baked level whether it agrees with retail's graph.
 *
 * Retail's `.ain` is the only machine-readable record of where its NPCs could walk: every link is
 * a designer-sanctioned "a body of this hull gets from here to there". The port routes on Recast
 * instead, so the graph cannot be a router -- but it can be an ANSWER KEY, and that is the only
 * way to judge a baked mesh by something other than looking at it.
 *
 * Batched deliberately. A map states thousands of links and a per-link editor round trip through
 * Python would dominate the bake; these take the whole set and answer the whole set.
 */
UCLASS()
class ELYSIUMUE_API UElysiumNavVerifyLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Path length in centimetres for each start/end pair on `AgentName`'s own mesh.
	 *
	 * -1 where no path exists, which is an answer and not an error: the rat's bridging links must
	 * path on the rat's mesh and must NOT path on the human's, so both outcomes are asked for.
	 * -2 where an endpoint does not project onto that mesh at all, so "the mesh does not reach
	 * here" is distinguishable from "the mesh reaches both ends but cannot join them".
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Nav")
	static TArray<double> PathLengths(UWorld* World, const FString& AgentName,
		const TArray<FVector>& StartsCm, const TArray<FVector>& EndsCm,
		const FVector& ProjectExtentCm);

	/** Whether each point projects onto `AgentName`'s mesh. */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Nav")
	static TArray<bool> ProjectPoints(UWorld* World, const FString& AgentName,
		const TArray<FVector>& PointsCm, const FVector& ProjectExtentCm);

	/** The agents this level actually carries a mesh for, as "<agent>=<tiles>". */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Nav")
	static TArray<FString> AgentMeshes(UWorld* World);
};
