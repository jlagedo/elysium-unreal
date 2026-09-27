#pragma once

#include "Substrate/ElysiumNpcCamera.h"

// `CNPC_VCameraSecurity` (primary vtable `0x104ace5c`), built by `npc_VCameraSecurity` factory
// `0x10369d80`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcCameraSecurity : public FElysiumNpcCamera
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VCameraSecurity", FElysiumNpcCamera)

	virtual bool FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4) override;
	virtual bool QuerySeeEntity(FElysiumEntity* Candidate) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcDialogueBodies.inl`.
	/** `+0x6660 m_iszLinkedCamera` — the authored `targetname` of the `CSecCamera` this security-camera
	 *  NPC watches through. A datamap keyfield in retail (`linked_camera`), bound on this class since
	 *  story 5 step 4, so the authored name lands through `Construct`. */
	FString LinkedCameraName;   // +0x6660
	/** `+0x6664 m_hLinkedCamera` — the resolved handle, cached by `0x10369e70` and re-resolved whenever
	 *  it goes dead. */
	FElysiumEntityHandle LinkedCamera;   // +0x6664
	/** `+0x6668` — "this NPC has been linked at least once". Set the first time the handle resolves and
	 *  never cleared; its only consumer is `0x10369e70`'s own teardown arm, which `UTIL_Remove`s the NPC
	 *  when a camera that WAS linked has gone. Retail name **unrecovered**. */
	bool bLinkedCameraBound = false;   // +0x6668
	/** `CNPC_VCameraSecurity`'s linked-camera resolve (`0x10369e70`, 252 bytes). Retail is unnamed; the
	 *  name is inferred from the RTTI cast it performs (`CSecCamera`) and the census field names. */
	FElysiumEntity* ResolveSecCameraLink();

	// From `ElysiumNpcSenses10.inl`.
	/** `CNPC_VCameraSecurity::FInViewCone` (`0x10369fb0`), slot 363's body: like slot 201 (`FVisible`)
	 *  it REPLACES the body it overrides outright, and answers true only for a candidate carrying a
	 *  player record when the link resolves and the camera's CONE test alone (`0x1020cc60`) passes. A
	 *  missing link is false. Not wired: slot 363 is story-8 residue (`overrides-step3.tsv`). */
	bool CameraSecurityFInViewCone(FElysiumEntity* Candidate);
};
