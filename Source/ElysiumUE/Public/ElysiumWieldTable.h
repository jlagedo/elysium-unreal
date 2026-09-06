#pragma once

#include "CoreMinimal.h"
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
	 * The native `/ElysiumBaked/Models/.../SK_<model>` package. Null is the authored
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
 * What asking for a character's held geometry answered. Three of these are answers the corpus
 * authors deliberately and three are failures, which is the whole reason this is an enum: most of
 * the corpus holds nothing, so a caller that treated every empty hand as a missing asset would warn
 * constantly and say nothing.
 *
 * Native catalogue lookup preserves authored absence separately from preparation and attachment
 * failures. Debug surfaces use this result vocabulary alongside the typed catalogue result.
 */
UENUM()
enum class EElysiumWieldResult : uint8
{
	/** A row carrying geometry. The out-reference is set. */
	Found,

	/**
	 * The authored no-geometry answer: a `w_null.mdl` wield model, an empty model string, or an
	 * item definition naming no wield model. 296 of the corpus's 488 rows answer this way, so it is
	 * the common case and never a warning.
	 */
	NoGeometry,

	/**
	 * The definition clears `shows_view_model`, so equip skips the sex branch and the world model
	 * supplies the geometry instead. Not a wield-model answer at all.
	 */
	WorldModel,

	/** No row for this classname -- it is not one of the shipped item definitions. */
	UnknownItem,

	/** The wield bake has not run, so there is no table to resolve through. */
	NoTable,

	/**
	 * The row resolved, but its package is not on the mount -- a bake that did not produce something
	 * its own table references. A failure, not an authored answer.
	 */
	MeshMissing,

	/** There is no body to put the geometry on. */
	NoWearer,
};

/** Whether this answer means something is wrong, as opposed to the corpus saying "nothing". */
inline bool ElysiumWieldFailed(EElysiumWieldResult Result)
{
	return Result == EElysiumWieldResult::UnknownItem
		|| Result == EElysiumWieldResult::NoTable
		|| Result == EElysiumWieldResult::MeshMissing
		|| Result == EElysiumWieldResult::NoWearer;
}
