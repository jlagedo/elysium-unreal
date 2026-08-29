#pragma once

#include "CoreMinimal.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumSaveTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ElysiumSaveSubsystem.generated.h"

// One slot as the load menu sees it: the name to pass back, plus the header read off disk without
// inflating the payload.
struct FElysiumSaveSlotInfo
{
	FString Slot;
	FElysiumSaveHeaderData Header;
};

// The owner of the slot list, the autosave ring and the freeze/thaw pass
// (`docs/architecture/save-architecture.md` §9). GI-scoped, because a save outlives every world it describes.
//
// **Writing is off the game thread in the half that can be**: the freeze is a synchronous memory
// walk (it has to be atomic with respect to the frame), and the compress + write go through
// `UGameplayStatics::AsyncSaveGameToSlot`.
//
// **Loading always goes through `UElysiumGameFlowSubsystem::LoadGame`** → `Loading` → travel, so
// there is exactly one restore path and it is the one travel already uses.
UCLASS()
class UElysiumSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Slots.
	// The canonical slot name for a kind. An empty request takes the next free `Elysium-NNN` for a
	// manual save, the single `Quick` slot for a quicksave, and the next entry of the `Auto0..4`
	// ring for an autosave — VtMB's rotating ring, so a bad autosave never eats the only one.
	FString ResolveSlotName(EElysiumSaveKind Kind, const FString& Requested) const;

	// Every slot on disk with a readable header, newest first.
	void ListSlots(TArray<FElysiumSaveSlotInfo>& Out) const;
	bool ReadSlotHeader(const FString& Slot, FElysiumSaveHeaderData& Out) const;
	bool DeleteSlot(const FString& Slot);

	// The two verbs.
	// False and a readable reason rather than a wrong file: mid-travel, with a modal panel or a
	// conversation on screen (neither carries a resume point), or when the payload cannot be
	// produced. VtMB refuses saves in similar states; a broken slot is worse than a missing one.
	bool CanSave(FString& OutReason) const;

	// Freeze and write. Returns the slot actually written in OutSlot.
	bool Save(EElysiumSaveKind Kind, const FString& RequestedSlot, FString& OutSlot, FString& OutError);

	// Read, restore the session, and travel. The app-state transition is the flow subsystem's.
	bool Load(const FString& Slot, FString& OutError);

	// The payload, for the tests and `elysium.save.diff`.
	// Gather the four blocks off the live session. Freezes the current map too, through the same
	// FElysiumEntityWorld::Freeze a travel boundary uses.
	bool BuildPayload(FElysiumSavePayload& Out, FString& OutError) const;

	// Write a payload back over the live session (`G`, quests, clock, RNG, the player record and
	// every map snapshot). Does not travel — Load does that after this returns.
	void ApplyPayload(const FElysiumSavePayload& In);

	// Read a slot's payload without applying it.
	bool ReadSlotPayload(const FString& Slot, FElysiumSavePayload& Out, FString& OutError) const;

	// The header a payload describes itself with (map, clan, playtime, timestamp).
	FElysiumSaveHeaderData MakeHeader(const FElysiumSavePayload& Payload, EElysiumSaveKind Kind) const;

	static const TCHAR* KindName(EElysiumSaveKind Kind);

private:
	void RegisterCommands();

	// The `Auto0..4` cursor. Session state, not saved: which autosave is next only has to be
	// sensible, and a fresh process starting at 0 overwrites the oldest ring entry it finds.
	int32 AutoRingCursor = 0;
	static constexpr int32 AutoRingSize = 5;

	TArray<IConsoleObject*> ConsoleObjects;
};
