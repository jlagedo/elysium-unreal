#pragma once

#include "CoreMinimal.h"
#include "Substrate/ElysiumCameraOverride.h"

class FElysiumEntity;

// `CBaseEntity`'s OWN answers to camera vtable slots 46-53, for an entity that overrides none of
// them — which is every entity in the game except `CCameraTrack` and `CBaseCombatCharacter`.
//
// Retail has no adapter here because it has no choice: the eight slots are declared on
// `CBaseEntity`, so `FUN_1017d280` / `FUN_1017d460` call them on *any* entity they are handed and
// always get an answer. The port declares them on a separate interface (`FElysiumEntity` carries no
// RTTI and the substrate must not `dynamic_cast`), so `GetCameraOverrideSource()` answers null for
// a plain entity and the channel would otherwise treat "no override behaviour of its own" as "not a
// camera entity at all" — the `FadeOut`/no-op arm. This class closes that: it IS the base-class
// body, so a choreo `cameramove` naming an `info_target`, or an `InputSetAsCameraTarget` on a
// non-character, frames the entity's centre exactly as retail does.
//
// The defaults, read out of `vampire.dll` (`_camera_recovery/rc_group_bc.md` RC7 table):
//
//   +0xB8 46  OnBecameCameraTarget  `100267d0`  RET
//   +0xBC 47  OnBecameCameraView    `100267f0`  RET
//   +0xC0 48  GetCameraRoll         `10026810`  -> `_DAT_104454c4` = 0.0
//   +0xC4 49  GetCameraFieldOfView  `10026830`  -> `_DAT_104454cc` = 75.0
//   +0xC8 50  GetCameraViewpoint    `10026850`  -> `WorldSpaceCenter()` (vfunc `0x300`)
//   +0xCC 51  GetCameraTargetPos    `10026890`  -> `WorldSpaceCenter()`, ignoring the `from` arg
//   +0xD0 52  GetCameraFadeInTime   `100268d0`  -> 0.0
//   +0xD4 53  GetCameraFadeOutTime  `100268f0`  -> 0.0
//
// Six of the eight are already `IElysiumCameraOverrideSource`'s own inline defaults, which were
// written FROM these bodies; only the two position getters are pure there, because the interface
// lives below the entity layer and has no world-space centre to reach. Those two are the only
// overrides below.
class FElysiumBareEntityCameraSource final : public IElysiumCameraOverrideSource
{
public:
	void Bind(FElysiumEntity* InEntity) { Entity = InEntity; }
	const FElysiumEntity* BoundEntity() const { return Entity; }

	// Slots 50 and 51, both `WorldSpaceCenter()`. The port's `WorldSpaceCenter` is
	// `ElysiumCameraShots::SurroundingBounds(Entity).GetCenter()` — the same accessor the `Center`
	// shot anchor and the mode-3 follow think publish, i.e. retail's `m_Collision->vfunc 0x3c`
	// centre: the standing skeletal body's bounds, else the embodiment's use box, else VtMB's
	// standing hull on the origin.
	virtual FVector GetCameraViewpointPosition() const override;
	// `AimFrom` is ignored, exactly as `10026890` ignores its `from` argument.
	virtual FVector GetCameraTargetPosition(const FVector& AimFrom) const override;

	// `handleLive()`. The resolver only ever binds an entity its own `Resolve` just returned, so
	// this is the second half of the same test — the dead flag, re-read at use.
	virtual bool IsCameraSourceAlive() const override;

private:
	FElysiumEntity* Entity = nullptr;
};

// Stable storage for the adapters above, so the channel can hold two of them at once.
//
// It has to: `AdoptViewEntity` resolves the INCOMING entity, then resolves the OUTGOING one inside
// `PushOutgoing`, and only then calls `GetCameraFadeInTime()` / `OnBecameCameraView()` on the
// incoming pointer. `Publish` likewise holds the view source and the target source across one
// another. A single shared adapter would be rebound underneath the older pointer.
//
// Two live at once is the deepest the channel ever goes, so four slots is headroom rather than a
// limit; the pool is a fixed array, never allocates, and reuses the slot already bound to an entity
// so repeat resolves of the same handle answer the same address.
class FElysiumBareEntityCameraSourcePool
{
public:
	IElysiumCameraOverrideSource* Bind(FElysiumEntity& Entity);

private:
	static constexpr int32 NumSlots = 4;
	FElysiumBareEntityCameraSource Slots[NumSlots];
	int32 NextSlot = 0;
};
