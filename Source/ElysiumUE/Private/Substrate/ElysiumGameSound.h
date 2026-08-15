#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "ElysiumEntityHandle.h"

struct FElysiumSoundLevel;
struct FElysiumSoundVolumeTable;

// The substrate game-sound bus — the THIRD event kind
// (`docs/architecture/gameplay-systems-architecture.md` §2.5.3), and deliberately not a second
// transport. Nothing here delivers anything: a producer stamps a stimulus into a bounded retention
// window, and every consumer polls that window during its own think (§5.5.3, "hearing needs no
// service at all"). No queue entry, no receiver, no scheduler.
//
// The authored half — how far a named category carries and whether a wall stops it — is
// `vdata/system/sound_volume_table.txt`, loaded through `FElysiumRulebook` (K9). The bus resolves
// it at emission and stores the ANSWER, so a consumer never has to re-ask the rulebook to know how
// far a stimulus reached.
//
// The VtMB facts: `docs/vtmb/npc-ai-reverse-engineering.md` → "Visual and auditory input" and
// `docs/vtmb/stealth.md` → "Auditory stealth".

// The `SoundTypes` names this runtime's producers emit. These are KEYS into the authored table, not
// recovered constants — the radius and occlusion policy behind each one stays in the file (K9) — so
// naming them once here is what keeps a producer and the table from drifting apart on a spelling.
// Function-local statics rather than namespace-scope FNames, because an FName cannot be built
// before the engine's name table exists.
namespace ElysiumGameSounds
{
	// `PLAYER_GUNSHOT_BASE` — "the player fired a basic, non-classified weapon".
	//
	// SEAM: the shipped table classifies gunshots per weapon family (`_PISTOL`, `_SHOTGUN`,
	// `_SNIPER`, …) and names no NPC row at all. The join from a weapon record to its family is not
	// recovered, so every shot — the player's and an NPC's alike — emits the base row. All seven
	// gunshot rows carry LEVEL_3, so the resolved reach and occlusion are identical today; only the
	// category name a future consumer could switch on differs.
	inline const FName& Gunshot()
	{
		static const FName Name(TEXT("PLAYER_GUNSHOT_BASE"));
		return Name;
	}
	// `NPC_TAKE_DAMAGE` — a character took damage. Emitted from the one typed health commit.
	inline const FName& NpcTakeDamage()
	{
		static const FName Name(TEXT("NPC_TAKE_DAMAGE"));
		return Name;
	}
	// `PLAYER_AGGRESSIVE_FEED` — the feeding stimulus (LEVEL_5, 240 units, occludable).
	inline const FName& Feed()
	{
		static const FName Name(TEXT("PLAYER_AGGRESSIVE_FEED"));
		return Name;
	}
	// `DOOR_NORMAL` — a door moved audibly.
	//
	// SEAM: the table also carries `DOOR_STEALTH`, and which of the two a door emits is a stealth
	// decision that has no producer yet. The normal row is what an ordinary open/close is, so it is
	// what every door emits until the stealth surface lands.
	inline const FName& Door()
	{
		static const FName Name(TEXT("DOOR_NORMAL"));
		return Name;
	}
}

// What a producer asks for. A struct rather than a parameter list because the stealth reduction is
// a fifth term that only one producer family will ever set.
struct FElysiumGameSoundRequest
{
	FVector Position = FVector::ZeroVector;   // world, Unreal cm
	// The `SoundTypes` name, e.g. `PLAYER_GUNSHOT_BASE`, `NPC_TAKE_DAMAGE`, `DOOR_NORMAL`.
	FName Category;
	// <= 0 asks the sound-volume table for the category's own radius, which is the ordinary case.
	// A positive value is an explicit reach in CENTIMETRES and skips that lookup; the category's
	// occlusion policy still applies, because overriding how far a sound carries is not the same
	// as deciding whether a wall stops it.
	float RadiusCm = 0.f;
	// Who made it. Invalid is legal and means "the world did" (a door, an explosion with no owner).
	FElysiumEntityHandle Source;
	// `CBaseEntity::AdjustSoundDistForStealth` (`docs/vtmb/stealth.md` → "Auditory stealth"): the
	// emitter's `m_flStealthHearingDist`, subtracted from the radius at INSERTION and floored at
	// zero, so the adjusted stimulus enters the ordinary sound system.
	//
	// SEAM: no producer sources this yet — the player-stealth surface that owns
	// `m_flStealthHearingDist` has not landed, so every caller passes 0. That is not a failure and
	// is not warned: zero reduction is the correct answer for a character carrying no stealth
	// modifier, which is every character today.
	float StealthHearingReductionCm = 0.f;
};

// One emitted stimulus, with every authored question already answered.
struct FElysiumGameSoundEvent
{
	FVector Position = FVector::ZeroVector;
	FName Category;
	float RadiusCm = 0.f;                  // post-stealth-adjustment audible reach
	FElysiumEntityHandle Source;
	double Time = 0.0;                     // substrate clock seconds
	uint64 Serial = 0;                     // monotonic within one bus, 1-based; 0 is "none yet"
	bool bOccludable = false;              // the table's `OccludedVolumeLevels` answer
};

class FElysiumGameSoundBus
{
public:
	// The retention window. A consumer polls on its own think — NPC hearing at roughly 10 Hz — so
	// several seconds of slack cover a listener that missed a few passes, while the count cap keeps
	// a firefight from growing the buffer without bound. Both are ceilings, not budgets: nothing
	// depends on an event surviving to the edge of either.
	static constexpr double RetentionSeconds = 4.0;
	static constexpr int32 MaxRetained = 128;

	// The authored table, or null. Null is the supported case, not a failure: a headless substrate
	// world has no rulebook behind it, and `FElysiumRulebook` already logs a failed load once.
	void SetVolumeTable(const FElysiumSoundVolumeTable* InVolumes) { Volumes = InVolumes; }
	const FElysiumSoundVolumeTable* VolumeTable() const { return Volumes; }

	// Stamp one stimulus. Returns the stored record by value, which is what a producer's own test
	// asserts against; `Now` is the substrate clock, never a wall clock.
	FElysiumGameSoundEvent Emit(const FElysiumGameSoundRequest& Request, double Now);

	// Everything emitted after `LastSerial` that is still retained, oldest first. A consumer keeps
	// the serial it last saw and hands it back, so nothing is processed twice and a consumer that
	// arrived late sees exactly the retained window rather than the whole map's history.
	//
	// The view is invalidated by the next `Emit`, like any array view.
	TArrayView<const FElysiumGameSoundEvent> EventsSince(uint64 LastSerial) const;

	const TArray<FElysiumGameSoundEvent>& Retained() const { return Events; }
	// The serial of the most recent emission. A consumer that wants to ignore the backlog starts
	// its cursor here.
	uint64 LastSerial() const { return Serial; }
	int32 NumRetained() const { return Events.Num(); }
	// Everything ever emitted / everything the window has dropped, for the debug surface and for a
	// test that asserts eviction happened rather than inferring it from a count.
	uint64 NumEmitted() const { return Serial; }
	int32 NumEvicted() const { return EvictedCount; }

	void Reset();

private:
	// The level a category resolves to, with the unknown-category warning latched by name. Never
	// null: an unknown category takes the `normal` level, which is what retail's own default is.
	const FElysiumSoundLevel& ResolveLevel(FName Category);
	// Drop everything past the retention window, leaving room for one more event.
	void Evict(double Now);

	const FElysiumSoundVolumeTable* Volumes = nullptr;
	TArray<FElysiumGameSoundEvent> Events;   // ascending Time, ascending Serial
	uint64 Serial = 0;
	int32 EvictedCount = 0;
	// Log-once-per-key, never per emission: a category missing from the table is one authoring
	// fact, and a gunshot fires many times a second.
	TSet<FName> WarnedCategories;
};
