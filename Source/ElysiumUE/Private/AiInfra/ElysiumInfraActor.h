#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AiInfra/ElysiumInfraKeyfields.h"
#include "AiInfra/ElysiumKeyfieldAccess.h"

#include "ElysiumInfraActor.generated.h"

struct FElysiumEntityDef;
struct FElysiumOutputDef;
class UArrowComponent;
class UBillboardComponent;

// One authored keyvalue pair, in the order and spelling the map wrote it.
USTRUCT(BlueprintType)
struct FElysiumInfraKey
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Elysium")
	FString Key;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Elysium")
	FString Value;
};

// One authored output row, the entity table's `FElysiumOutputDef` fields verbatim. Repeats of one
// `Name` are separate rows, in authored order.
USTRUCT(BlueprintType)
struct FElysiumInfraOutput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium")
	FString Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium")
	FString Input;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium")
	FString Param;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium")
	float Delay = 0.0f;

	// As authored; an authored 0 is normalised to -1 (unlimited) when the def is rebuilt, exactly
	// once, as both entity-table transports do.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium")
	int32 Times = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium")
	FString Python;
};

/**
 * A BSP-authored AI infrastructure entity baked onto its level (0018 story 2): a hint, an
 * interesting place, a conversation place, a maker or a placed NPC. The actor is the entity's
 * starting data and world identity; the live entity is still the substrate's `FElysiumEntity`,
 * built from the def this actor rebuilds at map load (`ElysiumInfraAdoption`), at the entity's own
 * BSP index.
 *
 * The typed keyfield structs are authoritative. `AuthoredKeys` keeps every pair the map authored,
 * in order and with repeats; the def is rebuilt by walking it: a key whose property still holds
 * what its authored string parses to re-emits that string byte for byte, a key whose property was
 * changed emits the property's value, and a datamap row the map never authored is emitted only
 * once its property is non-zero. An untouched actor therefore rebuilds its def exactly, an absent
 * key stays absent, and an edit made in the editor reaches the entity.
 */
UCLASS(Abstract, NotBlueprintable)
class AElysiumInfraActor : public AActor
{
	GENERATED_BODY()

public:
	AElysiumInfraActor(const FObjectInitializer& ObjectInitializer);

	// The entity's position in the map's entity table — its handle index and save key.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Entity")
	int32 EntityIndex = INDEX_NONE;

	// The classname the map authored (`info_node_patrol_point`, `npc_maker`, ...). A hint row's
	// live entity is `ai_hint`; this stays the authored name.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Entity")
	FString SourceClassname;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Entity")
	FString TargetName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Keyfields")
	FElysiumBaseEntityKeyfields Base;

	// Every keyvalue pair the map authored, except the two the entity table hoists out
	// (`classname`, `targetname`), in order, repeats kept.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Authored")
	TArray<FElysiumInfraKey> AuthoredKeys;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Entity")
	TArray<FElysiumInfraOutput> Outputs;

	// --- Bake-only setters (Unreal Python) ---------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Elysium|Bake")
	void ConfigureBakedIdentity(int32 InEntityIndex, const FString& InSourceClassname,
		const FString& InTargetName);

	// Record the authored pairs and apply each to its keyfield property. False when the two arrays
	// differ in length (nothing is applied).
	UFUNCTION(BlueprintCallable, Category = "Elysium|Bake")
	bool ApplyBakedKeyvalues(const TArray<FString>& Keys, const TArray<FString>& Values);

	UFUNCTION(BlueprintCallable, Category = "Elysium|Bake")
	void SetBakedOutputs(const TArray<FElysiumInfraOutput>& InOutputs);

	// --- Runtime ---------------------------------------------------------------------------------

	// The family tag this class is adopted under (`ElysiumBakedTags::Infra*`).
	virtual FName FamilyTag() const PURE_VIRTUAL(AElysiumInfraActor::FamilyTag, return NAME_None;);

	// The def's keyvalues, rebuilt by the rule above and folded as the entity table folds them: one
	// slot per exact spelling, at its first position, holding its last value.
	void BuildDefKeys(TMap<FString, FString>& OutKeys) const;

	// The def's output rows, in order, `Times` 0 normalised to -1.
	void BuildDefOutputs(TArray<FElysiumOutputDef>& OutOutputs) const;

	// Rewrite `Def`'s authored data (keys, outputs, targetname, start-hidden) from this actor. Its
	// classname and brush fields stay the entity table's. When the actor no longer stands on the
	// def's origin (moved in the editor), its location becomes the def's origin and its `origin`
	// keyvalue; answers whether it did.
	bool ApplyToDef(FElysiumEntityDef& Def) const;

	// How far an actor may stand from its def's origin and still count as unmoved: the bake places
	// it on the table's own centimetre value, rounded to five decimals.
	static constexpr double MovedToleranceCm = 0.001;

	// The keyfield structs a key may belong to, in lookup order: the family's own first, the base
	// entity's last.
	virtual void GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews);
	void GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews) const;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Elysium")
	TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Billboard;

	UPROPERTY()
	TObjectPtr<UArrowComponent> Arrow;
#endif
};
