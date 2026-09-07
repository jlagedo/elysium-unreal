#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "Slate/WidgetRenderer.h"

#include "ElysiumTerminalProjection.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UTextureRenderTarget2D;
struct FElysiumTerminalView;

// One physical monitor's glass.
//
// The render target, its material instance and the widget renderer are **world** state, not session
// state: retail's screen is the entity's own cell buffer and its screensaver think keeps writing
// into it whether or not anyone is standing at the machine (`docs/vtmb/computer-terminals.md` §13).
// So this object is created when the terminal's body is registered, re-bound when that body is
// replaced, and destroyed with the body — never with a CommonUI screen.
//
// Extracted verbatim from `UElysiumTerminalScreen::SetProjectionTarget` / `RenderProjection`, which
// owned exactly these five members while a session was open and had to hand the last pixels over on
// the way out.
UCLASS()
class UElysiumTerminalProjection final : public UObject
{
	GENERATED_BODY()

public:
	// The terminal this glass belongs to. Set once at registration; the projection outlives every
	// session on it.
	FElysiumEntityHandle Owner;

	// Bind the exact authored `screen` material slot of `Target`, allocate the 1024x768 target and
	// install the material instance on the component. False = there is nothing to project onto and
	// the reason has been logged.
	bool Bind(UPrimitiveComponent* Target);
	void Release();
	bool IsBound() const;
	// True when this process can rasterize: `-nullrhi` binds the slot and records the surface it
	// would have written, but allocates no render target. Named rather than silent, because a
	// projection with no renderer is the ordinary headless answer and must not read as a failure.
	bool HasRenderer() const { return WidgetRenderer.IsValid(); }
	UPrimitiveComponent* BoundBody() const { return ProjectionTarget.Get(); }

	// The revision gate alone. The residency gate (`WasRecentlyRendered`) belongs to the caller: a
	// process that renders nothing would otherwise never redraw at all.
	bool NeedsRedraw(const FElysiumTerminalView& View) const;
	// Whether the bound body is on screen this frame, which is the caller's second gate. It is asked
	// through this object so a `-nullrhi` case can drive the redraw path at all: nothing is ever
	// rendered under `-nullrhi`, so `WasRecentlyRendered` is permanently false there and the whole
	// gate would be untestable.
	bool IsBodyResident() const;
	// Test-only override for the line above. Unset in the game, where the engine's own answer wins.
	TOptional<bool> ForcedResidency;
	// Rasterize the authority's grid onto the glass and consume the view's revision.
	void Draw(const FElysiumTerminalView& View);

	// The revision last drawn (or last consumed, where there is no renderer).
	uint32 DrawnRevision = 0;
	// How many times `Draw` ran, for the ownership tests and the slice-H diagnostic.
	int32 DrawCount = 0;

	// Pure exact-match helper shared with focused tests. Similar names such as `screensaver` are
	// deliberately not accepted: only the authored `screen` material is a terminal surface.
	static int32 FindScreenMaterialSlot(const TArray<FName>& SlotNames);

private:
	TSharedRef<SWidget> BuildSurface(const FElysiumTerminalView& View) const;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ProjectionMaterial;

	TWeakObjectPtr<UPrimitiveComponent> ProjectionTarget;
	// What the `screen` slot carried before the projection took it, restored by `Release`. Weak
	// rather than owning: it is the mesh's own authored material and outlives this object.
	TWeakObjectPtr<UMaterialInterface> OriginalMaterial;
	TUniquePtr<FWidgetRenderer> WidgetRenderer;
	int32 ProjectionMaterialIndex = INDEX_NONE;
};
