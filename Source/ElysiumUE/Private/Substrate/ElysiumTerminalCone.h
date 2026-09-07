#pragma once

#include "CoreMinimal.h"

// `CBaseTerminal`'s screen-facing test (`docs/vtmb/computer-terminals.md` §7.2), as a pure
// predicate over three world points.
//
// Retail's body is `FUN_10218710` (`vampire.dll`): it reads the placed model's `screen` and
// `screen_axis` attachments through `CBaseAnimating::GetAttachment01`, takes the **3D** normalize
// of `screen_axis.origin - screen.origin`, then hands `(screen.origin, eye, forward)` to
// `FUN_101d1120`, which answers
//
//     normalize(eye.xy - screen.xy) . forward.xy
//
// -- the forward vector is normalized in three dimensions and only then projected, so its XY part
// is generally shorter than one and the gate is stricter the more the screen is tilted. That
// number must be **strictly greater** than `_DAT_10457f54` = 0.7f (`102187ab` FCOMP, `102187b7`
// `AND EAX,0x4100` -> equal or less answers 0), the same cosine `CGameMovement` and
// `CWeaponMelee::RequestActivity` use. It is a plan-view position cone in front of the glass, not
// a view-direction facing test: where the player STANDS decides, not where they look.
//
// No world, no bodies, no entity: the whole gate is assertable headless
// (`Elysium.Substrate.TerminalCone`).
namespace ElysiumTerminalCone
{
	// `_DAT_10457f54` at `vampire.dll` 0x10457f54. arccos(0.7) ~= 45.57 degrees, halved either
	// side of the screen normal in plan view.
	inline constexpr float FacingCosine = 0.7f;

	// `FUN_101d1120`. A degenerate input -- the eye exactly over the screen attachment in XY, or a
	// `screen_axis` coincident with `screen` -- answers 0, which is retail's own answer (the
	// zero-length branch returns `0 * fwd.x + 0 * fwd.y`) and fails the gate.
	float FacingDot(const FVector& ScreenCm, const FVector& ScreenAxisCm, const FVector& EyeCm);

	// The gate itself: strictly greater than `FacingCosine`.
	bool Faces(const FVector& ScreenCm, const FVector& ScreenAxisCm, const FVector& EyeCm);
}
