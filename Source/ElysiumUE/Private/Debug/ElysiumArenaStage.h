#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaScenario.h"
#include "ElysiumMapPlaces.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSaveStorage.h"
#include "Debug/ElysiumArenaSpec.h"   // the arena host's room, held by value
#include "UObject/WeakObjectPtr.h"

class AElysiumMapActor;
class FElysiumEntityWorld;
class UWorld;
class UElysiumSessionSubsystem;
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
	// Harness-only fences around retail capture/apply (0x200975f0/0x1011a620).
	enum class EFence : uint8 { Rebinding, Captured, Written, Applied, Ready, Failed };
	struct FTransactionFence
	{
		uint64 OperationId = 0;
		EFence Phase = EFence::Failed;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AElysiumMapActor> Map;
		FString MapIdentity, Reason;
	};
	struct FTransport;

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
		// Shared through the GI runner and lab host, including the original stage provenance.
		TSharedPtr<FTransport> Transport;
		TFunction<bool(FElysiumEntityWorld&, const FString&, const FString&, FElysiumArenaValue&, FString&)> ReadWitness;
		TFunction<bool(FElysiumEntityWorld&, const FElysiumArenaAction&, FString&)> RunFixture;
		TFunction<bool(FElysiumEntityWorld&, const FElysiumArenaScenario&, FString&)> ValidateWitnessAdmission; // integrator ledger, 0x10090180

		UWorld* GetWorld() const { return World.Get(); }
		AElysiumMapActor* GetMap() const { return Map.Get(); }
		FElysiumEntityWorld* GetEntityWorld() const;
	};

	// Common-codec checkpoint transport; stage identity is never admitted through production Load.
	struct FTransport : TSharedFromThis<FTransport>
	{
		FElysiumEntityDefs Defs;
		TArray<FElysiumPlaceRow> Network;
		FVector SeatFeet = FVector::ZeroVector;
		float SeatYaw = 0.0f;
		double StageInitialTime = -1.0;
		int32 StageInitialDraw = 0; // actual before-Load sample, engine 0x200f5bc4
		FElysiumSaveStorage Storage;
		TFunction<void(const FTransactionFence&)> Observer;
		TWeakObjectPtr<UElysiumSessionSubsystem> Session;
		FDelegateHandle ResultHandle;
		uint64 OperationId = 0;
		bool bPending = false, bArena = false, bLoading = false, bAppliedSent = false;
		bool bHasRestoreBase = false;
		double RestoreBase = 0.0;
		FString RestoreBaseSlot;
		TMap<FString, FString> Corruptions;
		TFunction<bool(FElysiumMapSnapshot&, const FString&, FString&)> MutateCheckpoint; // header fixture, 0x1027bf50
		FString ExpectedMap;
		TWeakObjectPtr<AElysiumMapActor> PendingMap;
		~FTransport();
		bool Begin(const FHost& Host, const FElysiumArenaAction& Action,
			TFunction<void(const FTransactionFence&)> InObserver, FString& OutError);
		void Poll();
		void Cancel();
		void Emit(EFence Phase, AElysiumMapActor* Map, const FString& Reason = FString());
	};
	// Called by the nonshipping map adapter immediately after construction, BEFORE Load/apply.
	// 0x101a2e40/0x1011a620: sink must precede all restoration/activation traces.
	void NotifyWorldConstructed(AElysiumMapActor* Map);
	void NotifyWorldApplied(AElysiumMapActor* Map); // fresh Load fence before activation, 0x1011a620
	void ConfigureHost(FHost& Host);
	// Integrator installs read-only NPC witness/fixture adapters once; missing source is unavailable.
	using FHostAdapter = TFunction<void(FHost&)>;
	void SetHostAdapter(FHostAdapter Adapter);

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

	// Return the player to what a new game's player has, at a record's seating: standing (the mover's
	// `ResetState`, the duck and its retained request), no `+duck` / `+attack` / `+use` latch held in the
	// input router and no replay running, no feed, grapple or stealth kill in progress, nothing wielded,
	// the damage counter healed, no discipline active -- each through the game's own door, never a field
	// write. `EntityWorld` is the world the player entity lives in (null: the pawn half only). The arena
	// host's rebuild already stands a new player entity; this is what its pawn and the session record
	// carry across, and the whole job on a map host, which is not rebuilt. Not part of `SeatPlayerAt`:
	// the runner's `player_teleport` seats mid-record and must not stand the player up. Returns what it
	// found to undo ("held buttons 0x1000, ducked"), empty when the player was already at rest. The
	// light pin, retail's `debug_stealth_light` (`0x109384d8`), goes back to `-1`, off.
	FString ResetPlayerState(UWorld* World, FElysiumEntityWorld* EntityWorld);

	// Stage `Record` on `Host` (see the header comment). `OutSummary` is one line for the log.
	//
	// A stage that failed to activate (`RuntimePhase` Failed: the barrier's wait ended) is not a dead
	// end: the next `Stage` rebuilds over it -- its pending character admissions cancelled, the failed
	// flag and the half-built entity world replaced -- so one bad record never errors the ones behind it.
	bool Stage(const FElysiumArenaScenario& Record, const FHost& Host, FString& OutSummary,
		FString& OutError);
}

#endif // !UE_BUILD_SHIPPING
