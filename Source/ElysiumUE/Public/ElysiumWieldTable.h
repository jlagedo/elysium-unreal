#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"

#include "ElysiumWieldTable.generated.h"

class USkeletalMesh;

/**
 * How a wield model is bound to its wearer. The editor bake maps the manifest's binding string onto
 * this enum; the runtime chooses its attachment mechanism from it.
 *
 * `None` covers the three authored ways a row carries no geometry -- a `w_null.mdl` wield model, an
 * empty model string, and an item definition that names no wield model at all. All three are
 * authored answers, not failures; `docs/vtmb/wielded_weapons.md` owns why.
 */
UENUM(BlueprintType)
enum class EElysiumWieldBinding : uint8
{
	/** No geometry: `w_null.mdl`, an empty model string, or an absent key. Nothing is rendered. */
	None,

	/** Manifest `socket_prop`: rigidly attached to the wearer's prop bone. */
	SocketProp,

	/** Manifest `socket_hand`: rigidly attached to the wearer's hand bone. */
	SocketHand,

	/** Manifest `leader_pose`: driven by the wearer through `SetLeaderPoseComponent`. */
	LeaderPose,

	/** Manifest `copy_pose`: the wearer's matched bones are copied over the model's own pose. */
	CopyPose,

	/** Manifest `projectile`: a free-standing actor playing its own clips, not attached to a wearer. */
	Projectile,
};

/**
 * One sex's answer for a wield row: the baked mesh, the bones the binding needs, and which binding
 * to use.
 */
USTRUCT(BlueprintType)
struct FElysiumWieldModelRef
{
	GENERATED_BODY()

	/**
	 * The baked `/ElysiumBaked/Items/Wield/<stem>/SK_<stem>` package. Null is the authored
	 * no-geometry answer -- a `w_null.mdl` or empty wield model -- and is not an error; it pairs
	 * with `Binding == EElysiumWieldBinding::None`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Wield")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** The wearer bone the model mounts on. `NAME_None` when the row carries no geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Wield")
	FName MountBone;

	/**
	 * The wearer's hand bone -- the nearest matched ancestor an unmatched mount runs FK off.
	 * `NAME_None` when the row carries no geometry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Wield")
	FName HandBone;

	/** How this model is bound to its wearer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Wield")
	EElysiumWieldBinding Binding = EElysiumWieldBinding::None;
};

/**
 * One item classname's wield answer. No shipped item definition authors one sex without the other,
 * so both members are always written -- including when both carry no geometry.
 */
USTRUCT(BlueprintType)
struct FElysiumWieldRow
{
	GENERATED_BODY()

	/** The model a female wearer holds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Wield")
	FElysiumWieldModelRef Female;

	/** The model a male wearer holds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Wield")
	FElysiumWieldModelRef Male;

	/**
	 * The item definition's `shows_view_model` gate, default 1. When false, equip skips the sex
	 * branch entirely and the world model supplies the geometry instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Wield")
	bool bShowsWieldModel = true;
};

/**
 * `/ElysiumBaked/Items/DA_WieldModels` -- the table the runtime resolves `(classname, sex)` through
 * to a held weapon's baked mesh and its binding.
 *
 * Written by the editor wield bake from the exporter's engine-neutral manifest; the bake sets these
 * properties directly, so every one of them is editable and Blueprint-writable.
 */
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumWieldTable final : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * One row per item classname. Keys are case-folded to lower, because VtMB compares classnames
	 * case-insensitively and the manifest is written from authored text of mixed case.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Wield")
	TMap<FName, FElysiumWieldRow> Rows;
};
