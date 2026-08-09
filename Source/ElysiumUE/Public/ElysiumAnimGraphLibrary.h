#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumAnimGraphLibrary.generated.h"

class UBlueprint;

/** What one graph import did. Empty Errors means success. */
USTRUCT(BlueprintType)
struct FElysiumAnimGraphImportResult
{
	GENERATED_BODY()

	/** Nodes pasted into the graph. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Animation")
	int32 Nodes = 0;

	/** One line per failure, already naming the graph it belongs to. */
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Animation")
	TArray<FString> Errors;
};

/**
 * Editor-only implementation behind `pipeline/unreal/make_player_anim_bp.py`.
 *
 * The player animation graph is tracked as **text** and generated into a package, rather than
 * committed as a binary asset. What makes that possible is the engine's own clipboard format: the
 * Blueprint editor's copy and paste are `FEdGraphUtilities::ExportNodesToText` and
 * `ImportNodesFromText`, and an animation state machine survives the round trip because
 * `UAnimStateNode` and `UAnimStateTransitionNode` carry their bound graphs through
 * `PrepareForCopying`/`PostPasteNode`.
 *
 * Two functions live here rather than in Python because neither has a scripting surface at all:
 * `UEdGraphPin` is a plain class with no reflection, so no amount of Python can create a node or
 * connect a pin, and `FEdGraphUtilities` carries no `UFUNCTION`. Everything else about the asset —
 * creating it as a template Animation Blueprint, compiling it, saving it — is ordinary Python
 * against `UAnimBlueprintFactory`.
 *
 * The round trip is the authoring loop: generate, open the asset in the editor, edit it there, copy
 * the graph, and paste it back into the tracked `.t3d`. The editor stays the authoring tool and the
 * repository still stores something reviewable.
 */
UCLASS()
class ELYSIUMUE_API UElysiumAnimGraphLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Paste T3D text into one of a blueprint's graphs, replacing whatever nodes it already holds
	 * apart from the ones the schema created with it.
	 *
	 * The result nodes are the ones the paste produced, which is how a caller tells an empty import
	 * from a refused one: `CanImportNodesFromText` answering false is a text/graph mismatch and is
	 * reported as an error rather than as zero nodes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Animation")
	static FElysiumAnimGraphImportResult ImportGraphFromText(UBlueprint* Blueprint, FName GraphName,
		const FString& T3D);

	/**
	 * The inverse — every node in the named graph as T3D, in the same format the editor's Ctrl+C
	 * produces. This is what an owner who edited the generated asset copies back into the tracked
	 * text, and what a verifier compares against it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Animation")
	static FString ExportGraphToText(const UBlueprint* Blueprint, FName GraphName);
};
