#pragma once

#include "CoreMinimal.h"
#include "Substrate/ElysiumWeaponClasses.h"

class FElysiumEntityWorld;
class USkeletalMeshComponent;

// The melee weapon-trail VFX: a Niagara ribbon following a wielded weapon's mount and TrailTip
// sockets for as long as its wearer's swing transaction is live. Presentation only -- it reads
// FElysiumWeapon::Swing (already-computed substrate state) and engine render data, and mutates no
// gameplay state. Third-person only, for free: it rides the same USkeletalMeshComponent whose
// IsHiddenInGame() the wield model's own first-person submission gate already drives
// (AElysiumPawn::ApplyDrawPolicy over FElysiumCameraView::bDrawWorldWeapon), so a hidden wield
// mesh draws no trail either.
namespace ElysiumMeleeTrail
{
	// Once per frame, after the world's own melee-swing contact walk has advanced this frame's
	// Swing state (AElysiumMapActor's Step 8, alongside FElysiumEntityWorld::AdvanceMeleeSwings).
	void Advance(float DeltaSeconds, FElysiumEntityWorld& World);

	// Tear down any trail riding `Body`, unconditionally -- mirrors
	// ElysiumNpcVisual::ClearWieldModel/SweepWieldModels and is called from the same sites: a
	// weapon that stops showing geometry stops showing its trail too.
	void ClearTrail(USkeletalMeshComponent* Body);

	// The swing-window gate, pulled out for its own coverage: a trail is wanted for exactly as
	// long as an accepted MELEE swing transaction is active -- the whole clip, not just its
	// authored hit-contact window (`FElysiumSwingRecord`'s [Start,End] is narrower and only covers
	// a fraction of the corpus's clips; gating on it would flicker the trail and miss weapons with
	// no authored contact record at all).
	bool ShouldTrailBeActive(const FElysiumWeapon::FSwing& Swing);
}
