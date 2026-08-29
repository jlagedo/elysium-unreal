#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "Substrate/ElysiumTriggerBase.h"

// `trigger_player_activity_level` (`docs/vtmb/entity_io.md` § "`trigger_player_activity_level`"
// and `docs/vtmb/exported-map-event-surface.md` § "per-touch refresh, guarded release, invalid
// output").
//
// A `CBaseTrigger` leaf that authors the player's three activity channels directly. Six volumes in
// the corpus, all of them criminal-only: four enabled `restricted_section` brushes in `sm_medical_1`
// at level 3, a disabled `alley_criminal_trigger` in `sm_hub_1` at 1, and a disabled
// `post_robbery_cop_call` in `sm_diner_1` at 4.
//
// Four properties of the recovered body this one keeps:
//
//   * each field defaults to `-1`, which is **unset**, not "clear". Only a value at or above zero
//     is refreshed, which is why the five corpus volumes that author `supernatural_level -1` do not
//     wipe a supernatural level the player earned elsewhere;
//   * `Touch` is per-tick occupancy work, not an enter-only transaction, and it does **not** call
//     `PassesTriggerFilters`: it checks disabled state and a player/controller-bearing toucher and
//     nothing else. The base's own `OnStartTouch`/`OnTrigger`/filter work is unaffected and still
//     runs first;
//   * supernatural and criminal pass duration `-1` to the player setters, so each refresh installs
//     a finite `max(previously retained level, pl_min_act_timer)` deadline and increments that
//     channel's act count. Investigate uses its direct replacement setter;
//   * `EndTouch` is EXACT-MATCH release, not a reference count: it clears a channel only while the
//     current level still equals this volume's own authored value, so an overlapping volume that
//     replaced the value keeps it. Supernatural and criminal additionally require spawnflag `0x20`;
//     investigate does not.
//
// Every one of the six volumes carries `spawnflags 33` = `0x1 | 0x20`, so `ALLOW_CLIENTS` and the
// release bit are both set on all of them. The bit is therefore a spawnflag REINTERPRETATION rather
// than a second meaning: `0x20` is not a `CBaseTrigger` admission flag at all on this class, it is
// this leaf's own "release my value at EndTouch" switch.

class FElysiumActivityTrigger final : public FElysiumTriggerBase
{
public:
	// `supernatural_level` +0x598, `criminal_level` +0x59c, `investigate_level` +0x5a0. All three
	// default to -1 (unset).
	int32 SupernaturalLevel = -1;
	int32 CriminalLevel = -1;
	int32 InvestigateLevel = -1;

	// The `0x20` release bit, named rather than spelled at the two use sites.
	static constexpr int32 ReleaseSpawnFlag = 0x20;

	// How often an occupied volume re-asserts its levels. Retail refreshes on the engine's own
	// collision touch tick, which this substrate does not raise.
	//
	// **CHOSEN, NOT RECOVERED** — 0.1 s, the player think's own heartbeat. The refresh only has to
	// out-pace the deadline it renews, and the shortest deadline this domain can produce is
	// `pl_min_act_timer`; matching the heartbeat means the refresh and the expiry pass that would
	// undo it run at the same rate, which is the closest a deadline-driven think gets to "every
	// collision tick" without inventing a faster clock.
	static constexpr double RefreshIntervalSeconds = 0.1;

	// The recovered admission: disabled state and a player toucher. Deliberately NOT the base's
	// `PassesTriggerFilters`, whose spawnflag/filter verdict governs the base's OUTPUT work only.
	virtual bool CanBeginTouch(const FElysiumEntityHandle& Activator) const override;

	virtual void OnTouchStart(const FElysiumEntityHandle& Activator) override;
	virtual void OnTouchEnd(const FElysiumEntityHandle& Activator) override;
	virtual void Think() override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

	bool IsOccupied() const { return bPlayerInside; }
	// How many times this volume has re-asserted its levels, for the inspector and the tests.
	int32 RefreshCount() const { return Refreshes; }

private:
	// One refresh pass. Safe with no player and with every channel unset.
	void RefreshLevels();

	bool  bPlayerInside = false;
	int32 Refreshes = 0;
};
