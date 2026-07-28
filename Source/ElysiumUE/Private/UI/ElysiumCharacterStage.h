#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class AActor;
class ACameraActor;
class APlayerController;
class UWorld;

// The character behind the character screen: a real animated body over a fixed backdrop, standing
// in the world rather than composited as a 2D render.
//
// **Shared by both hosts.** Chargen and the in-game screen show the same body the same way — VtMB's
// own screen draws it through its translucent panels, which is what proves it is geometry and not a
// picture — so this is owned by the screen's host, not by chargen.
//
// The rig is three transient actors: a camera the controller looks through, an unlit quad carrying
// the sheet's own `charactermaintenance/background` art, and the body itself with collision off.
// `UElysiumGameFlowSubsystem::EnterMenuBackdrop` is the precedent for the camera half.
//
// Every piece degrades independently. No `.glb`, no body; no backdrop texture, no quad; no world,
// no stage at all — and in each case the screen's panels stand on their own, which is the
// convention the screen's art already follows.
class FElysiumCharacterStage : public FGCObject
{
public:
	~FElysiumCharacterStage();

	// Raise the rig and take the view. `PrevViewTarget` is remembered so `Teardown` can give the
	// player's camera back — the in-game screen has one to restore, chargen does not.
	void Raise(UWorld* World, APlayerController* PC);
	void Teardown();
	bool IsUp() const { return Camera != nullptr; }

	// Swap the body for this clan / sex. A no-op when the stem has not changed, so a click on the
	// Base tab that lands on the same body does not reload a 2-35 MB asset.
	void SetBody(const FString& Stem);
	const FString& BodyStem() const { return Stem; }

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FElysiumCharacterStage"); }

private:
	void BuildBackdrop();

	TWeakObjectPtr<UWorld> WeakWorld;
	TWeakObjectPtr<APlayerController> WeakPC;
	TWeakObjectPtr<AActor> PrevViewTarget;

	TObjectPtr<ACameraActor> Camera;
	TObjectPtr<AActor> Backdrop;
	TObjectPtr<AActor> Body;

	FString Stem;
};
