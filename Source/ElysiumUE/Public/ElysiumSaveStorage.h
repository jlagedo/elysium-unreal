#pragma once

#include "CoreMinimal.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSessionTypes.h"

struct FElysiumSaveSlotInfo
{
	FString Slot;
	FElysiumSaveHeaderData Header;
};

// Native storage only: no world lookup, capture, application or travel. Compression is
// synchronous; AsyncSaveGameToSlot performs the platform write from private serialized data.
class FElysiumSaveStorage
{
public:
	static const TCHAR* KindName(EElysiumSaveKind Kind);
	static bool NormalizeSlot(const FString& In, FString& Out, FString& Error);
	bool ResolveSlotName(EElysiumSaveKind Kind, const FString& Requested, FString& Out, FString& Error) const;
	void ListSlots(TArray<FElysiumSaveSlotInfo>& Out) const;
	bool ReadSlotHeader(const FString& Slot, FElysiumSaveHeaderData& Out) const;
	bool ReadSlotPayload(const FString& Slot, FElysiumSavePayload& Out, FString& Error) const;
	bool DeleteSlot(const FString& Slot);
	FElysiumSaveHeaderData MakeHeader(const FElysiumSavePayload& Payload, EElysiumSaveKind Kind) const;
	bool Write(const FString& Slot, EElysiumSaveKind Kind, const FElysiumSavePayload& Snapshot,
		TFunction<void(bool)> Completion, FString& Error);
	bool IsWriting() const { return !State->WritingSlot.IsEmpty(); }

private:
	// Shared with the completion so destroying the session cannot leave a dangling capture.
	struct FState { FString WritingSlot; int32 AutoRingCursor = 0; };
	TSharedRef<FState> State = MakeShared<FState>();
};
