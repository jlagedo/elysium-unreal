#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaSpec.h"   // the arena host's room, held by value
#include "UObject/WeakObjectPtr.h"

class AElysiumMapActor;
class FElysiumEntityWorld;
class UWorld;
struct FElysiumArenaAt;
struct FElysiumArenaFace;
struct FElysiumArenaRow;
struct FElysiumArenaScenario;
struct FElysiumEntityDef;

// Staging a scenario record (`Debug/ElysiumArenaScenario.h`): the one function both hosts call.
//
// **The arena host** (the headless `-ElysiumArena` run, and the lab's `elysium.gr_scenario`) turns the
// record into the def list a map would carry -- the room's 8 anchors and 4 cover-node rows, then the
// record's `rows`, its `from_map` rows and its `cast`, each `ElysiumArena::AuthoredRow` plus the
// record's own keys and nothing else -- with the room's AI network plus a node per record node row,
// and hands both to `AElysiumMapActor::RebuildStageWorld`, which tears the stage's entity world down
// and runs the map sequence (adopt, `Load`, the player, the barrier, `Activate`). The random streams
// are seeded from the record immediately before, so `Load`, `Activate` and every draw after them
// start from the record's position.
//
// **A map host** (`-ElysiumMap=<map>`) stages nothing: the cast names entities the map already has,
// and the map is not rebuilt. It seeds the streams and seats the player.
namespace ElysiumArenaStage
{
	// Where a record runs.
	struct FHost
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AElysiumMapActor> Map;
		// The arena host: a stage world with `Spec` stood at `Origin`. False on a map host, whose places
		// are the map's own coordinates and targetnames.
		bool bArena = true;
		ElysiumArena::FSpec Spec;
		FVector Origin = FVector::ZeroVector;

		UWorld* GetWorld() const { return World.Get(); }
		AElysiumMapActor* GetMap() const { return Map.Get(); }
		FElysiumEntityWorld* GetEntityWorld() const;
	};

	// A resolved place: feet in world centimetres and an Unreal-native yaw.
	struct FPlace
	{
		FVector FeetCm = FVector::ZeroVector;
		float YawDeg = 0.0f;
	};

	// `at`. Arena: a seat (`start`, `cover_seat`, `cover_behind`), a pad, an anchor or a node of the
	// spec, or arena-relative centimetres. Map: a targetname (its origin; yaw 0), or map centimetres.
	bool ResolveAt(const FHost& Host, const FElysiumArenaAt& At, FPlace& Out, FString& OutError);

	// `face` for something standing at `From`: its own yaw, toward the player's feet, toward a named
	// place, or a stated yaw.
	bool ResolveFace(const FHost& Host, const FElysiumArenaFace& Face, const FPlace& From,
		const FVector& PlayerFeet, float& OutYaw, FString& OutError);

	// Where the record seats the player: its `player.at` (arena default: `start`) and `player.face`.
	// False with an empty `OutError` on a map host whose record names no seat (the player stays).
	bool PlayerPlace(const FHost& Host, const FElysiumArenaScenario& Record, FPlace& Out,
		FString& OutError);

	// One record row as a map-shaped def: `ElysiumArena::AuthoredRow` (origin, angles), the record's
	// keys, `body` written as `model`, and `bStartHidden` read off `StartHidden` the way the bake does.
	bool BuildRow(const FHost& Host, const FElysiumArenaRow& Row, const FVector& PlayerFeet,
		FElysiumEntityDef& Out, FString& OutError);

	// A body name (a stem such as `regular_cop`, an alias, a source path or a `vtmb:model:` id) as the
	// `model` key a shipped map row carries: `models/<unit>.mdl`, checked to read back through
	// `ElysiumCharacterModel::IdFromSource` to the same canonical id.
	bool BodyModelPath(const FString& Body, FString& OutModelPath, FString& OutError);

	// Seat the driven pawn: the mover reset, `ElysiumGym::SeatOrigin` feet to pawn centre, the control
	// yaw, a camera reseed -- `FElysiumGreenRoomRun::ArenaSeatPlayer`'s four steps at any feet.
	bool SeatPlayerAt(UWorld* World, const FVector& FeetWorld, float YawDeg, FString& OutError);

	// Stage `Record` on `Host` (see the header comment). `OutSummary` is one line for the log.
	bool Stage(const FElysiumArenaScenario& Record, const FHost& Host, FString& OutSummary,
		FString& OutError);
}

#endif // !UE_BUILD_SHIPPING
