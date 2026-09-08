#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ElysiumSaveGame.generated.h"

// The container. `USaveGame` buys slot management, platform-safe
// paths and `UGameplayStatics::AsyncSaveGameToSlot` **without owning the content**: the payload is
// our own versioned, compressed block stream, written by `FElysiumSaveArchive`, because none of the
// game state is UPROPERTY-reflected and none of it should become so.
//
// The header fields are reflected and sit **ahead of the payload**, uncompressed, so the load menu
// can list slots by reading them without inflating anything — the reason `userName`, `comment` and
// `mapName` sit in VtMB's own global stream too.
UCLASS()
class ELYSIUMUE_API UElysiumSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// Header (uncompressed, listable).
	UPROPERTY() int32   PayloadVersion = 0;
	UPROPERTY() FString Map;
	UPROPERTY() FString Label;
	UPROPERTY() FString ClanName;
	UPROPERTY() int32   Clan = 0;
	UPROPERTY() double  PlaytimeSeconds = 0.0;
	UPROPERTY() FDateTime Timestamp;
	UPROPERTY() FString Kind;          // "manual" | "quick" | "auto"

	// The payload.
	// `ELYS` prologue + one compressed stream over the four blocks. Opaque to reflection on purpose.
	UPROPERTY() TArray<uint8> Payload;
};
