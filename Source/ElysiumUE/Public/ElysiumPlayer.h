#pragma once

#include "CoreMinimal.h"
#include "ElysiumAudioSubsystem.h"
#include "ElysiumCameraService.h"
#include "ElysiumEntity.h"
#include "ElysiumSheetSlots.h"

class USkeletalMeshComponent;

struct FElysiumStatTable;      // Private/Substrate/ElysiumRulebook.h — the data half of the sheet
struct FElysiumClanTemplate;
struct FElysiumSheetEffects;   // Private/Substrate/ElysiumSheetMath.h — the trait-effect layer
struct FElysiumDisposition;
struct FElysiumDmg;            // Private/Substrate/ElysiumDamage.h — the typed damage descriptor
enum class EElysiumDmgFamily : int32;

// 11.4 (S3) — the player is an entity; the pawn is its body.
//
// This header holds the two middle chain nodes VtMB's own datamap chain has and the player leaf
// under them, plus the session-lifetime record the entity hydrates from:
//
//     FElysiumEntity              CBaseEntity           keyfields, dormancy, I/O, think
//      +- FElysiumAnimating       CBaseAnimating        a body to follow, clips, skin, disposition
//          +- FElysiumCombatCharacter  CBaseCombatCharacter  the SHEET + its 25 inputs
//              +- FElysiumNpc          CAI_BaseNPC       (Substrate/ElysiumNpc.h)
//              +- FElysiumPlayer       CBasePlayer       the 10 recovered player inputs
//
// The chain mirrors VtMB's because VtMB's *data* is authored against it: a `.dlg` action calls
// `npc.SetDisposition(...)` and a Hammer wire fires `MoneyAdd` on the same class, so one name table
// per class (R2) only pays off if the classes are the same ones. Design: `docs/architecture/runtime-architecture.md`
// sections 5-6; the input inventory: `docs/vtmb/script_api.md`.

// The chain-node classnames. They never appear in a `.ents` file — they exist so the registry's
// base-chain walk reaches the inputs and fields they own.
inline FName ElysiumAnimatingClassName()      { return FName(TEXT("CBaseAnimating")); }
inline FName ElysiumCombatCharacterClassName(){ return FName(TEXT("CBaseCombatCharacter")); }
// The player's own classname and the targetname it is registered under. `!player` is what the maps
// themselves write (48 `point_teleport.target` keys across the exported maps), so putting it in the
// name index makes every one of those an ordinary targetname resolve — no magic target keyword.
inline FName ElysiumPlayerClassName()         { return FName(TEXT("player")); }
inline const TCHAR* ElysiumPlayerTargetName() { return TEXT("!player"); }
// VtMB creates four engine-owned viewmodel entities with the player. The patch indexes slot 3
// directly when selecting Tremere hands, so the entity/API shape exists before the first map-load
// callback even while their rendered first-person bodies remain a later programme.
inline FName ElysiumViewModelClassName()       { return FName(TEXT("viewmodel")); }
inline constexpr int32 ElysiumViewModelSlotCount = 4;

// The numeric character sheet — VtMB's own four containers, base and current.
//
// Storage is the compiled shape (`ElysiumSheetSlots.h`): four fixed-length int arrays of 35/13/13/13
// slots, doubled into a base and a current copy. **The base/current split is the whole buff system**
// — base is the character sheet, current is the sheet plus every active modifier — and both halves
// persist, which is what VtMB's save does. Every slot registers as a datamap field, so one R2 walk
// serves a script read (`pc.strength`), a Hammer keyvalue, the save enumeration and the inspector.
//
// The *values* come from `vdata/system/stats.txt` through the rulebook: `SeedFrom` writes each
// slot's authored `Default`, and `RecomputeCurrent` clamps to its authored `Min`/`Max`.
//
// `Clan` uses the LEVEL-SCRIPT indexing 2..8 (Brujah 2 ... Ventrue 8) that `pc.clan` carries — not
// the 1..7 `ClanNameFunc` display enum, and not `clandoc`'s ordering. `docs/vtmb/game_runtime.md` section 3
// records that two indexings exist; the scripts (`IsClan`, `unhidePlus`'s 9/10/11 patch-type
// sentinels) speak this one.
struct FElysiumSheet
{
	FElysiumSheet();

	// One entry per compiled slot, sized at construction so a sheet is always addressable.
	TArray<int32> Base[(uint8)EElysiumTraitContainer::Count];
	TArray<int32> Current[(uint8)EElysiumTraitContainer::Count];

	// What has no compiled slot. VtMB has no equivalent — its datamap is the whole namespace — so
	// this only catches a `base_*` name the shipped `stats.txt` does not own.
	TMap<FName, int32> Extra;

	// --- The CVStatList_t accessors -----------------------------------------------------------
	int32 GetBase(EElysiumTraitContainer Container, int32 Slot) const;
	int32 GetCurrent(EElysiumTraitContainer Container, int32 Slot) const;
	// Writes the base and re-derives the current. An out-of-range slot is ignored.
	void SetBase(EElysiumTraitContainer Container, int32 Slot, int32 Value);

	// `CVStatList_t::AddBase` — `+Delta` under the stat's effective max, which a **negative Delta
	// bypasses**. With no rule table the write is ungated, which is what a bare test world wants.
	void AddBase(EElysiumTraitContainer Container, int32 Slot, int32 Delta,
		const FElysiumStatTable* Stats = nullptr, const FElysiumSheetEffects* Effects = nullptr);

	// `CVStatList_t::IncBase` — `+1`, refused when the base is already at the effective max or the
	// stat's `IncPredependency` reads false. Returns whether the dot landed.
	bool IncBase(EElysiumTraitContainer Container, int32 Slot,
		const FElysiumStatTable* Stats, const FElysiumSheetEffects* Effects = nullptr);

	// base -> trait effects -> clamp to the effective Min/Max. A null table leaves current equal to
	// base; a null effect layer is the ordinary case (a character with no clan or history).
	void RecomputeCurrent(const FElysiumStatTable* Stats, const FElysiumSheetEffects* Effects = nullptr);

	// A slot's bounds as they apply to THIS sheet: the authored `Min`/`Max` (resolving the ones
	// authored as another stat's NAME against the base array), tightened by any `Max`/`Min` trait
	// effect. `Max < Min` means "unbounded" and no caller clamps.
	void BoundsFor(EElysiumTraitContainer Container, int32 Slot, const FElysiumStatTable* Stats,
		const FElysiumSheetEffects* Effects, int32& OutMin, int32& OutMax) const;

	// Every slot takes its `stats.txt` `Default`, base and current alike.
	void SeedFrom(const FElysiumStatTable& Stats);
	// Overlay a resolved clan / NPC template's authored ratings. An absent trait means inherit, so
	// only what the template holds is written (`FElysiumClanTable::Resolve` folds the parent chain).
	void ApplyTemplate(const FElysiumClanTemplate& Template, const FElysiumStatTable* Stats,
		const FElysiumSheetEffects* Effects = nullptr);

	// --- The named slots the runtime speaks ---------------------------------------------------
	int32 Clan() const  { return GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Clan); }
	void  SetClan(int32 Value) { SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Clan, Value); }
	bool  IsMale() const { return GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Gender) != 0; }
	void  SetMale(bool bValue) { SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Gender, bValue ? 1 : 0); }

	// Clan display names indexed by the 2..8 encoding; index 0/1 unused.
	static const TCHAR* ClanName(int32 Clan);
	// Case-insensitive name -> 2..8, or 0 when unrecognised (drives `elysium.newgame brujah`).
	static int32 ClanFromName(const FString& Name);
	static bool IsValidClan(int32 Clan);
};

// One EXPERIENCE_ENTRY row: the XP ledger is itemised in VtMB's save, not a total
// (`docs/vtmb/savegame_format.md`). `AwardExperience` names a `vdata` entry, so the string is the key.
struct FElysiumXpEntry
{
	FString Entry;
	int32   Amount = 0;
};

// One ASSIGNED_QUEST row — the journal as VtMB holds it (`docs/vtmb/savegame_format.md`). A quest is absent
// from the journal until it holds a state, and holds exactly one row for as long as it does.
struct FElysiumAssignedQuest
{
	// szTitle. The CATALOGUE's spelling, not the caller's: the engine matches a row by
	// `Q_strnicmp(title, 48)`, so two casings of one title cannot become two rows.
	FString Title;
	// idxQuestTable. VtMB stores the flat quest index across all five loaded files; we store the
	// (table, quest) pair the rulebook indexes by, because our catalogue keeps them separate.
	int32 Table = INDEX_NONE;
	int32 Quest = INDEX_NONE;
	// idxState — the completion state's 1-based ORDINAL in file order, which is what SetQuest's
	// second argument is. The authored `"ID"` is decorative; VtMB's loader never reads it.
	int32 State = 0;
	// iOrder — display order, assigned once when the quest is first assigned as max(order)+1, so
	// the first quest of a run gets 1.
	int32 Order = 0;
	// The unread marker VtMB sets on every write (the record's byte at +0x3c).
	bool bUnread = false;
};

// ============================================================================================
// Law, Masquerade and world response (cycle 10b) — `docs/vtmb/player-entity.md` § "Law,
// Masquerade and world response". The rules over these two structs are
// `Private/Substrate/ElysiumLaw.h`; this is only their storage, which lives on the player leaf
// because retail's fields do (`+0x1ccc..+0x1cec` and `+0x1d10..+0x1d1c` on CBasePlayer).
// ============================================================================================

// The three activity channels `SetCriminalLevel`, `SetSupernaturalLevel` and `SetInvestigateLevel`
// write. They are NOT one generic "wanted" value: criminal and supernatural each carry a deadline
// and a monotonic incident count, while investigate is a direct replacement with neither.
struct FElysiumLawState
{
	// The retained levels, clamped 0..5. `Criminal` is retail's *protected* level at `+0x1cd8`.
	int32 Criminal = 0;        // +0x1cd8
	int32 Supernatural = 0;    // +0x1ccc
	int32 Investigate = 0;     // +0x1cdc — no companion timer or count in the player setter

	// Absolute substrate-clock deadlines. `-1` is the recovered sentinel an explicit zero write
	// installs: no deadline in force, so the expiry pass has nothing to age out.
	double CriminalExpiry = -1.0;      // +0x1ce0
	double SupernaturalExpiry = -1.0;  // +0x1ce4

	// The player act counts. Monotonic — nothing decrements them, because an NPC's own processed
	// count is what makes an incident new to that NPC (the conditions 31-34 lane). Public through
	// `FElysiumPlayer::CriminalActCount()` / `SupernaturalActCount()` for that reader.
	int32 CriminalCount = 0;       // +0x1ce8
	int32 SupernaturalCount = 0;   // +0x1cec
};

// The delayed police response, the Masquerade rate limiter and the pursuit/alert state machine.
// Separate from `FElysiumLawState` on purpose: retail keeps them in a different field block, they
// are *consequences* rather than activity, and the discipline test's `Player->Law = {}` reset must
// not silently zero a pursuit count it never touched.
struct FElysiumPoliceState
{
	// `m_flMasqueradeTimerNext` — an admitted supernatural incident may increment Masquerade only
	// at or after this absolute time, and then reschedules by `debug_masquerade_timer`.
	double MasqueradeTimerNext = 0.0;

	// --- The retained, delayed response record ------------------------------------------------
	// One record, not a queue: a second incident before the deadline replaces it only when it is
	// strictly more severe, and never reschedules.
	bool  bResponsePending = false;
	int32 ResponseSeverity = 0;
	// The witness the incident was admitted through. The update refuses to spawn when it no longer
	// resolves, which is the one member of this block that names a map entity.
	FElysiumEntityHandle ResponseWitness;
	FVector ResponsePosition = FVector::ZeroVector;
	double ResponseDeadline = 0.0;

	// `debug_cop_grace_time`: what the last consumed response actually put on the street, and how
	// long that stays the baseline a duplicate/higher-severity response subtracts from.
	double GraceUntil = -1.0;
	int32  GraceSpawned = 0;

	// --- Pursuit and alert ---------------------------------------------------------------------
	int32 CopsInPursuit = 0;      // +0x1d10
	int32 HuntersInPursuit = 0;   // +0x1d14 — suppresses new response admission while non-zero
	bool  bHeightenedAlert = false;       // +0x1d18
	double HeightenedAlertExpiry = 0.0;   // +0x1d1c
};

// ============================================================================================
// Feeding (B6) — the paired action's phase and the authoritative transaction's field set.
// `docs/vtmb/feeding.md` owns the behaviour; `Substrate/ElysiumFeed.h` owns the rules over these.
// ============================================================================================

// The ordinary (paired mode 0) state family: engage, bite, feed loop, release. The attacker
// advances the pair; a role-1 victim never chooses the next base activity.
enum class EElysiumFeedPhase : uint8
{
	None = 0,
	Engage,    // ACT_FEEDING_ENGAGE is playing; no transaction yet
	Bite,      // the bite clip; its authored event 4007 sits at cycle 0.0, so FeedBegin runs here
	Loop,      // FeedBegin has run; Feed() pulses against the substrate clock
	Release,   // the release clip; its authored event 4006 requests teardown part-way through
	ReleaseTail, // 4006 ended gameplay/camera ownership; the authored release pose is still finishing
};

// `FeedBegin`'s recovered field block (`feeding.md` § "Authoritative transaction state"), plus the
// two members this runtime needs to own a pair without retail's grapple router: the peer handle and
// the phase schedule. The save schema names `m_flNextFeedPulse` and `m_flFeedStartTime`, which is
// what makes the cadence simulation state rather than an animation notification.
struct FElysiumFeedState
{
	// +0x1490 `m_flNextFeedPulse` — the next pulse's absolute substrate time.
	float NextPulse = 0.0f;
	// +0x1494 — the current pulse interval, accelerating toward the 0.30 s floor.
	float Interval = 0.0f;
	// +0x1498 `m_flFeedStartTime`.
	float StartTime = 0.0f;
	// +0x149c — the authoritative feed target, set by FeedBegin and cleared by FeedInterrupt.
	FElysiumEntityHandle Target;
	// +0x14a0 `m_iBloodStolen` — counted only when the feeder's blood-pool increment succeeded.
	int32 BloodStolen = 0;
	// +0x14a8 — the player feed-continuation latch.
	bool bContinuation = false;
	// +0x14a9 — FeedInterrupt's re-entry guard.
	bool bInterrupting = false;

	// The grapple peer. Retail keeps this on the common paired-action state rather than in the feed
	// block; this runtime has no grapple router (B6 is deliberately the transaction only), so the
	// one pairing link lives here. On the attacker it is the victim, on the victim the attacker.
	FElysiumEntityHandle Peer;
	// This character is the role-1 half of the pair.
	bool bVictim = false;
	// This character's motor was frozen by the pair and must be released at teardown.
	bool bFrozenByFeed = false;

	EElysiumFeedPhase Phase = EElysiumFeedPhase::None;
	// When the current phase's clip boundary is due, in absolute substrate seconds.
	float PhaseDeadline = 0.0f;

	// Part of a pair at all — what `Replenish` refuses a second request on.
	bool IsPaired() const { return Peer.IsSet() || Target.IsSet(); }
	// The authoritative transaction is open (event 4007 has fired and 4006 has not).
	bool IsTransacting() const { return Target.IsSet(); }
};

// `AttemptFeed`'s verdict, kept as separate answers rather than a boolean: the four acceptance
// routes of `feeding.md` § "Target acquisition and acceptance" are distinguishable in retail and
// have to stay so (`docs/vtmb/skills-and-checks.md` — the consumer owns the policy).
enum class EElysiumFeedVerdict : uint8
{
	AcceptedAutomaticState,   // ACT_DISPOSITION_MESMERIZED / ACT_DISORIENTED / ACT_LOST / ACT_COWER
	AcceptedNotResisting,     // the target's ResistsFeeding predicate read false
	AcceptedOpposedCheck,     // attacker Brawl RATING > victim's non-negative Hacking net at diff 6
	RefusedOpposedCheck,      // ... and the stealth override that could still authorise it is a seam
	RefusedBusy,              // either side is already paired, in dialogue, or script-owned
	RefusedInvalidTarget,     // no target, dead/hidden, or not a combat character
};

const TCHAR* LexToString(EElysiumFeedVerdict Verdict);
inline bool ElysiumFeedAccepted(EElysiumFeedVerdict Verdict)
{
	return Verdict == EElysiumFeedVerdict::AcceptedAutomaticState
		|| Verdict == EElysiumFeedVerdict::AcceptedNotResisting
		|| Verdict == EElysiumFeedVerdict::AcceptedOpposedCheck;
}

// ============================================================================================
// Disciplines (13.2) — the two execution families' live state.
// `docs/vtmb/disciplines.md` owns the behaviour; `Substrate/ElysiumDisciplines.h` owns the rules
// over these fields.
// ============================================================================================

// One targeted-Discipline effect running on the character it was applied to. Retail tracks active
// targeted effects as bits on the affected character so that replacement and
// `ClearActiveDisciplines` remove the actual effect owner rather than hiding its particle; this is
// the same tracking with the owning record named, which is what lets teardown remove exactly the
// trait-effect groups that hit installed and nothing else.
struct FElysiumActiveDisciplineEffect
{
	FString Record;              // the `DisciplineTgt` InternalName that cast it
	FString HitTable;            // the `HitInfo` this target resolved to
	TArray<FString> Effects;     // the TraitEffectGroup names this hit installed here
	// Absolute substrate seconds. A negative value is the authored infinite duration (`"-1"`),
	// which only `ClearActiveDisciplines` and the interruption flags end.
	double EndTime = 0.0;
	int32 Serial = 0;            // the owned queue event's guard; 0 = no timed expiry
	bool bRemoveOnTakeDamage = false;
	bool bRemoveOnHearCombat = false;
	bool bRemoveOnWasBumped = false;
	FElysiumEntityHandle Source; // the caster

	bool IsInfinite() const { return EndTime < 0.0; }
};

// The native active-state half plus the tracked targeted effects. It sits on
// CBaseCombatCharacter because both halves do in VtMB: the thirteen `Active_*` slots are sheet
// slots on this class, and a targeted effect lands on whichever character it hit.
struct FElysiumDisciplineState
{
	// The compiled array is thirteen, not `stats.txt`'s seventeen (`Public/ElysiumSheetSlots.h`).
	static constexpr int32 SlotCount = 13;

	// Per compiled Discipline index: when the owned expiry event is due (absolute substrate
	// seconds; 0 = the slot is not active), the serial that event carries, and the trait-effect
	// groups this activation installed into `FElysiumCombatCharacter::Effects`.
	double EndTime[SlotCount] = {};
	int32  ExpirySerial[SlotCount] = {};
	TArray<FString> Groups[SlotCount];

	// The caster's per-record recovery deadlines, keyed by the record's InternalName — step 8 of
	// the targeted transaction.
	TMap<FString, double> Recovery;

	TArray<FElysiumActiveDisciplineEffect> TargetEffects;

	// One counter for both families: a serial identifies an owned expiry event uniquely on this
	// character, which is what makes (character, discipline) the event's key. A renewal mints a new
	// serial, so the event still pending under the old one is dropped as stale on delivery — the
	// same guard the weapon commit/reload transactions use.
	int32 SerialCounter = 0;

	// The sound-bus cursor the `ShouldRemove_OnHearCombat` poll advances.
	uint64 SoundCursor = 0;

	bool IsActive(int32 Index) const
	{
		return Index >= 0 && Index < SlotCount && EndTime[Index] != 0.0;
	}
	void Reset() { *this = FElysiumDisciplineState(); }
};

// ============================================================================================
// 13.1 — the player's stealth target surface (`docs/vtmb/stealth.md` -> "Player target-surface
// update"). ONE player-owned surface: the retained three-point light cycle and the three values
// an observer reads off the TARGET — sight-range scalar, cone scalar, hearing-distance reduction.
//
// It lives on the player leaf because retail's fields do: `+0x1c6c..+0x1c8c` are `CBasePlayer`'s.
// The `trigger_stealth_mod` aggregate is the one piece that is NOT here — it sits on
// `FElysiumCombatCharacter` at retail's `+0x1084`, so any character can carry a contribution.
//
// The rules over it — the cadence, the sample rotation, the normalization and the row selection —
// are `Private/Substrate/ElysiumStealth.h`. This struct is storage and nothing else.
// ============================================================================================

struct FElysiumStealthSurface
{
	// feet / centre / head, refreshed ONE per pass. The full cycle therefore takes ~0.3 s and the
	// other two retained samples participate unchanged — that lag is part of the transaction, not
	// an approximation of it.
	static constexpr int32 NumSamples = 3;

	double NextUpdateTime = -1.0;      // m_flNextStealthUpdate  (+0x1c6c); negative = due now
	float VisionScalar = 1.f;          // m_flStealthVisionScalar (+0x1c70)
	float ConeScalar = 1.f;            // m_flStealthVisionCone   (+0x1c74)
	// m_flStealthHearingDist (+0x1c78), converted ONCE out of the table's Source game units so no
	// producer downstream carries a unit question.
	float HearingReductionCm = 0.f;
	int32 NextSampleIndex = 0;         // m_nNextLightPositionTest (+0x1c7c)
	float Samples[NumSamples] = { 1.f, 1.f, 1.f };   // m_flLightOnFeet/Center/Head (+0x1c80..0x1c88)
	// m_flLightOnMe (+0x1c8c): the normalized aggregate, or the `-4.0` inactive sentinel the
	// non-stealth fallback writes.
	float LightOnMe = -4.f;

	// The two resolved table indices. Retail keeps them as debug feat/light indices; they are kept
	// here for the same reason — a wrong scalar is traceable to a row rather than to arithmetic.
	int32 LightRow = 0;
	int32 StealthRow = 0;
	// Whether the last committed pass took the eligible arm. Diagnostic, and what the observer
	// meter reads to distinguish "not sneaking" from "sneaking in full light".
	bool bEligible = false;

	// Bumped by every COMMITTED recompute. A restored sample triplet without the derived values it
	// produced is invalid (`docs/vtmb/stealth.md`), so this is what makes the group one generation:
	// a reader that saw generation N knows every field it read came out of the same pass.
	int32 Generation = 0;

	void Reset() { *this = FElysiumStealthSurface(); }
};

// The HUD observability surface (`docs/vtmb/stealth.md` -> "HUD observability is not authority").
// Retail keeps the nearest eligible hostile observer's handle, distance and meter/status at
// `+0x1cc0..+0x1cc8`. Gameplay owns the transaction; this is the snapshot it publishes AFTER the
// transaction commits, and nothing that reads it may run range, cone, trace, memory or enemy work.
struct FElysiumStealthObserver
{
	FElysiumEntityHandle Observer;   // +0x1cc0 — the nearest eligible observer, or invalid
	float DistanceCm = 0.f;          // +0x1cc4
	// +0x1cc8 — 0 unobserved .. 1 on top of the observer, derived from the observer's own effective
	// radius (which already carries this player's sight scalar), never recomputed by a reader.
	float Meter = 0.f;
	// Whether that observer has committed detection of this player, as opposed to merely being the
	// nearest one. The senses pass supplies it; the HUD never derives it.
	bool bDetected = false;
	// When the offer that produced this snapshot was made. Substrate clock seconds; negative means
	// "nothing has been offered".
	double Time = -1.0;
	// Bumped only when the committed snapshot CHANGES. A reader that has seen this generation is
	// looking at current state; one that has not must re-read the whole struct rather than a field.
	int32 Generation = 0;

	bool IsSet() const { return Observer.IsSet(); }
	void Reset() { *this = FElysiumStealthObserver(); }
};

// The durable half of the player: session lifetime, so it crosses a map boundary. The entity is the
// *live* view; this is the truth that survives the world it lived in. Hydrated into the player
// entity at map build, dehydrated back out when the world is torn down (travel, quit, reload) and,
// at 11.9, into the save.
//
// Health is deliberately NOT a "player stat" here: `m_iHealth` is a Save-flagged entity field on the
// chain (`docs/architecture/save-architecture.md` section 4 — VtMB's own placement), and the copy below exists only
// because our entity dies with its map, so something has to carry the value across a travel. The
// entity's field stays the one the save walk enumerates.
//
// Inventory (`item_*` entities owned by the player) and the equipped-weapon handles join this record
// at 9.8; the quest map and `G` are still the game-state subsystem's own stores until 11.9 gathers
// the save blocks. Neither is stubbed here — an empty field nothing fills would read as support.
struct FElysiumPlayerRecord
{
	// The name the player types at chargen. Not a Stat and not a datamap field — VtMB carries it as
	// its own string on the character — so it sits beside the sheet rather than in a container.
	FString Name;

	// Humanity, blood, masquerade, clan and sex are all slots on this — VtMB holds them in the
	// Attributes container, not as members beside it, so there is nothing to mirror.
	FElysiumSheet Sheet;

	int32 Money = 0;         // m_iMoney — not a Stat in `stats.txt`

	// Which authored M_BodyN/F_BodyN the player wears. This is appearance identity, not the
	// Armor_Rating sheet trait; callers clamp it to the six body slots VtMB's clan templates expose.
	int32 ArmorSlot = 0;

	int32 Health = 0;        // carried across a travel; 0 = "not seeded yet" (the entity seeds it)
	int32 MaxHealth = 0;

	TArray<FElysiumXpEntry> ExperienceLog;   // EXPERIENCE_ENTRY — itemised, not a total
	TArray<FString>         Effects;         // m_tEffectList
	TArray<FString>         EmailFlags;      // the Player block, save-architecture.md section 3
	FElysiumLawState        Law;

	// Cycle 10b — the police-response / Masquerade-timer / pursuit block beside the activity
	// channels. Deliberately NOT map-scoped the way `FeedMap`, `DisciplineMap` and `StealthMap`
	// are: every deadline in both structs is on the session clock, which
	// `UElysiumGameStateSubsystem` owns and which persists across map travel, and a wanted level
	// with four seconds left means exactly the same thing in the next map. The one member that
	// names a map entity is `ResponseWitness`, so `FElysiumPlayer::Hydrate` rebases-or-drops that
	// single handle rather than scoping the whole block to a map name.
	FElysiumPoliceState     Police;

	// m_QuestList — the journal. It lives on the record ONLY and is not mirrored onto the live
	// player entity the way Money and ExperienceLog are: the quest map it reflects is session-scoped
	// (it outlives having no map at all), and no script name, datamap input or keyfield reads the
	// journal, so a hydrate/dehydrate round-trip would buy nothing.
	TArray<FElysiumAssignedQuest> Journal;

	// m_iCurrQuestLogArea — the quest log's selected hub tab, an integer datamap field on the player
	// at +0x1dac. It is player state in VtMB, not the panel's, so it persists with the character.
	// INDEX_NONE means "never opened"; the screen resolves that to a hub with work in it.
	int32 QuestLogArea = INDEX_NONE;

	// m_iVHistoryID — the History background the player picked at chargen, as an index into
	// `histories000.txt`. The trait effect it names is applied through `Effects` like any other, so
	// this is the choice, not its consequence. INDEX_NONE means unset.
	int32 HistoryId = INDEX_NONE;

	// `AddExperience`'s two accumulators. The award is in hundredths — every real
	// `experience_table` row is `N01` — and the division **keeps its remainder**, so the residue is
	// state, not a rounding artefact: one bonus point falls out per 100 awards.
	float ExperienceRemainder = 0.f;   // m_flExpRemainder
	float LifetimeExperience = 0.f;    // m_flLifetimeExp — raw, never divided

	// MakePlayerUnkillable / MakePlayerKillable (events_player). Latched here as well as on the
	// entity because the tutorial sets it at map load and it must survive the warp into the next map.
	bool bUnkillable = false;

	// B6 — an in-progress feed, so a save taken mid-transaction restores without duplicating a pulse
	// or losing the victim link (`docs/vtmb/feeding.md` recreation contract item 9; retail's own save
	// schema names `m_flNextFeedPulse` and `m_flFeedStartTime`).
	//
	// It rides on the record rather than in the map snapshot because the player entity is excluded
	// from that snapshot by design — the record IS the player's durable home. But the victim it names
	// is a map entity, so `FeedMap` scopes it: hydrating into any other map drops the pair instead of
	// resolving a stale index against a stranger. Every NPC's own half is an ordinary Save-flagged
	// chain field and travels in the map snapshot with the rest of that entity.
	FElysiumFeedState Feed;
	FString FeedMap;

	// 13.2 — the discipline block. The player entity is excluded from the map snapshot by design,
	// so its half of every domain rides this record; the sheet's own `Active_*` slots come across
	// with `Sheet` above, and this carries what the sheet cannot say — when each owned expiry event
	// is due, which trait-effect groups the activation installed, the tracked targeted effects and
	// the per-record recovery deadlines.
	//
	// `DisciplineMap` scopes it the way `FeedMap` scopes the feed: the expiry events themselves ride
	// that map's queue, and the tracked effects name entities in it, so hydrating into any other map
	// tears the block down instead of leaving active slots with no event to end them. That teardown
	// is the recovered world-area transition teardown (`docs/vtmb/disciplines.md` § "World-area
	// eligibility and transition teardown") applied at the map boundary.
	FElysiumDisciplineState Disciplines;
	FString DisciplineMap;

	// `vdiscipline_int`'s stored selection and the remembered tier beside it. INDEX_NONE is "nothing
	// selected", which is what `vdiscipline_last` refuses on.
	int32 SelectedDiscipline = INDEX_NONE;
	int32 SelectedTier = 0;
	// The player's Discipline cast counter, incremented by a committed targeted cast (step 6).
	int32 DisciplineCastCount = 0;

	// 13.1 — the stealth block, carried as ONE group. `docs/vtmb/stealth.md`: a restored sample
	// triplet must never be combined with newly defaulted derived values, so the surface travels
	// whole (samples, rotation index, derived scalars and the generation that ties them together)
	// or not at all.
	//
	// `StealthMap` scopes it the way `FeedMap` and `DisciplineMap` scope theirs, and for a stronger
	// reason than either: the three samples are measurements of THIS map's light at THIS position,
	// and the raw aggregate is the sum of the `trigger_stealth_mod` volumes of this map that the
	// player is standing inside. Both are meaningless one map later, so hydrating anywhere else
	// resets the group instead of carrying a stale generation across the boundary.
	FElysiumStealthSurface Stealth;
	int32 StealthModRaw = 0;
	FString StealthMap;

	void Reset() { *this = FElysiumPlayerRecord(); }
};

// ============================================================================================
// FElysiumInventory — CBaseCombatCharacter's carried-item storage (9.8, `docs/vtmb/inventory.md` §2).
//
// Inventory is **not** a bag of class names. An ordinary carried item stays a full server entity;
// what the character holds is a list of handles to those entities (`m_hMyWeapons[224]` at +0x1624)
// plus the active-weapon handle (+0x19a4). Players, NPCs and item containers all reuse this, which
// is why it sits on the shared chain node rather than on the player leaf.
//
// Item POLICY — stackability, droppability, permanence, ammunition — is never decided here. It is
// read from the item's own `vdata/items` definition (`FElysiumItemDef`), never inferred from a
// classname prefix.
// ============================================================================================

class FElysiumItem;
class FElysiumKeyring;

struct FElysiumInventory
{
	// Retail's array is 224 fixed slots kept COMPACT: `Inventory_Remove` closes the gap it leaves
	// and repairs the following items' `m_iInvenPos`. A dense array of the occupied prefix is the
	// same structure without 224 empty handles on every character in the map — an item's inventory
	// position IS its index here, so the compaction rule is the container's invariant.
	static constexpr int32 MaxSlots = 224;

	TArray<FElysiumEntityHandle> Slots;
	// The currently equipped/active weapon. `HasWeaponEquipped` compares against THIS entity's
	// classname and nothing else — it does not mean "owned".
	FElysiumEntityHandle ActiveWeapon;

	// Reserve ammunition, keyed by the item data's `Magazine.Type` folded to lower case. The
	// asymmetry that the tutorial's `.38` beat rides on lives across these two homes: `AmmoCount`
	// reports the item's LOADED MAGAZINE, `GiveAmmo` adds to this RESERVE.
	//
	// Not persisted yet: it is character state no entity field can hold, so it needs the record /
	// save-block half that slice (c) owns.
	TMap<FString, int32> AmmoReserve;

	int32 Num() const { return Slots.Num(); }
	bool IsFull() const { return Slots.Num() >= MaxSlots; }

	// `Inventory_Add` — the acquisition route every pickup, grant and container transfer passes
	// through. The item becomes owned by `Char` and takes a compact position; a stackable item
	// whose classname is already carried MERGES into that stack instead (and the passed entity is
	// left for the caller to dispose of, which is what makes the merge visible). Returns false when
	// the item is already owned, the list is full, or the merge target is at its stack limit.
	bool Add(FElysiumCombatCharacter& Char, FElysiumItem& Item);

	// `Weapon_Equip` — `Inventory_Add` plus the active-weapon switch for a wieldable item.
	bool Equip(FElysiumCombatCharacter& Char, FElysiumItem& Item);

	// The active-weapon switch on its own: holster what was held, name the new active weapon, run
	// its equip callback and republish the camera class. `Equip`'s wieldable branch is its one
	// ordinary caller; the NPC loadout is the other, because `is_wieldable` is authored by 100 of
	// the 226 shipped records and by none of the `item_w_*` weapons an NPC is spawned holding.
	// Returns false when `Item` is not carried by `Char`.
	bool SetActiveWeapon(FElysiumCombatCharacter& Char, FElysiumItem& Item);

	// `GiveNamedItem(classname, 0)` — create and spawn the named item entity, then run it through
	// the same equip/add route. Returns the new item's handle, or Invalid when the classname has no
	// `vdata/items` definition, there is no world to create in, or the add was refused.
	FElysiumEntityHandle GiveNamedItem(FElysiumCombatCharacter& Char, const FString& Classname);

	// Python `RemoveItem(classname)` — the DESTRUCTIVE removal. A stack of two or more decrements;
	// otherwise the entity is detached and killed. With no ordinary match it falls back to removing
	// a keyring record. Returns whether anything matched — the native returns None either way.
	bool ScriptRemove(FElysiumCombatCharacter& Char, const FString& Classname);

	// The `Inventory_Remove` INPUT — detach, compact and reindex. It never destroys the entity and
	// it is not Python's `RemoveItem`; the item becomes unowned and unslotted.
	bool Detach(FElysiumCombatCharacter& Char, FElysiumItem& Item);

	// Case-insensitive, ordinary slots first and then the carried keyring's records — `HasItem`'s
	// own order. The keyring arm answers with the keyring ENTITY, because a key record is not one.
	bool Has(const FElysiumCombatCharacter& Char, const FString& Classname) const;
	// Ordinary slots only, case-insensitive. What `AmmoCount`/`GiveAmmo` resolve against.
	FElysiumItem* FindOrdinary(const FElysiumCombatCharacter& Char, const FString& Classname) const;
	// The carried `item_g_keyring`, or null. It is an ordinary carried item like any other and is
	// governed by its own item data (`is_droppable 0`, `permanent_inventory 1`).
	FElysiumKeyring* FindKeyring(const FElysiumCombatCharacter& Char) const;
	// The item at a compact position, or null.
	FElysiumItem* At(const FElysiumCombatCharacter& Char, int32 Position) const;
	FElysiumItem* Active(const FElysiumCombatCharacter& Char) const;

	// Re-derive the handle list from the items themselves — every item entity whose owner is
	// `Char`, ordered by the `m_iInvenPos` it restored with. The list is a cache of what the items'
	// own Save-flagged fields already say, so a restore rebuilds rather than serializing it twice.
	void RebuildFrom(FElysiumCombatCharacter& Char);

	int32 Reserve(const FString& AmmoType) const;
	// No clamp of its own: `GiveAmmo`'s wrapper has none either (`inventory.md` §6).
	void AddReserve(const FString& AmmoType, int32 Amount);

	// Atomically transfer the item at `Position` into `To`. A multi-count stack transfers one unit;
	// the last unit/non-stackable moves its entity. Destination admission is checked before mutation,
	// so a full inventory or merge target leaves both inventories untouched.
	bool TransferSlot(FElysiumCombatCharacter& From, FElysiumCombatCharacter& To, int32 Position,
		FString* OutClassname = nullptr, int32* OutQuantity = nullptr);

	// Still distinct and unbuilt: player drop (`inven_drop`, world entity PRESERVED — never a
	// shortcut through ScriptRemove) and priced `Buy`/`Sell` barter. Take/Give use TransferSlot;
	// pricing belongs to 9.10 (`inventory.md` §5.3, §7).
};

// ============================================================================================
// FElysiumMeleeRoll — the opposed record a melee contact stages ON THE DEFENDER
// (`docs/vtmb/combat-and-damage.md` § "Opposed record and reaction margin").
//
// Retail appends a 16-byte record to the defender's result array at +0xA88 and keys it by
// attacker; `GetMeleeDiceRolls` searches that array and `GetNumAttackSuccesses` returns word 1.
// The signed reaction margin is `lethality - defense - soak`, and both the player block path and
// the NPC `RunTask` handlers classify it against `rules.txt`'s `Melee_Reactions` block.
//
// It is live combat state, not persistence: the record exists between one contact and the
// reaction it selects, so it rides no save block.
// ============================================================================================

struct FElysiumMeleeRoll
{
	FElysiumEntityHandle Attacker;   // word 0
	int32 Lethality = 0;             // word 1 — the attacking weapon's total lethality
	int32 Defense = 0;               // word 2 — defender `Defensive_Maneuvers` net + defense bonus
	int32 Soak = 0;                  // word 3 — defender soak selected from the mode's descriptor

	int32 Margin() const { return Lethality - Defense - Soak; }
};

// ============================================================================================
// FElysiumAnimating — CBaseAnimating. Everything that owns a skeletal body: standing it, moving
// it with the entity, gating it on dormancy, and playing clips on it. NPCs and the player share
// this because in VtMB they share the class.
// ============================================================================================

class FElysiumAnimating : public FElysiumEntity
{
public:
	// `skin` — a material family swap. One VtMB datamap record flagged both KEY and INPUT with a
	// null inputFunc, so the keyvalue, the wire and `.skin =` are the same direct write
	// (`docs/vtmb/entity_io.md`). Carried here for the whole chain; only the prop leaf paints with it today.
	int32 Skin = 0;

	// `default_disposition` — the emotional stance that selects the standing animation set through
	// `vdata/system/dispositiontable.txt` (8.5). Runtime state, not a spawn-time constant: 9.9's
	// `SetDisposition` (2,510 calls, 2,467 of them a `.dlg` line's action) writes it mid-conversation.
	FString Disposition;
	// SetDisposition's second argument. `default_disposition` starts at level 1; the resolved level
	// persists separately because several names author distinct expression/stance rows.
	int32 DispositionLevel = 1;

	// The standing skeletal body, or null (a bodiless entity, `elysium.NpcBodies 0`, or a missing
	// glb). Owned by the map actor; the world tears it down. This class only gates and moves it.
	USkeletalMeshComponent* Visual = nullptr;

	// Stand this entity's body at its current origin/facing, playing the idle its disposition
	// selects. Called from the leaf's Spawn(); no-op with no embodiment, no model, or bodies off.
	void BuildBody();

	// --- The animation seam (8.5), implemented once for every character ---------------------
	virtual bool PlayAnimClip(const FString& ClipName, bool bLoop, float* OutSeconds = nullptr) override;
	virtual bool PreloadAnimClip(const FString& ClipName) override;
	virtual bool PlayCinematicClip(const FString& AnimSetModel, const FString& BoneRoot,
		const FString& ClipName, bool bLoop, float* OutSeconds = nullptr) override;
	virtual bool PreloadCinematicClip(const FString& AnimSetModel, const FString& BoneRoot,
		const FString& ClipName) override;
	virtual bool SeekCinematicClip(float PositionSeconds) override;
	virtual void StopCinematicClip() override;
	virtual int32 SetFlexControllers(TArrayView<const FElysiumFlexWrite> Writes,
		TArray<FString>* OutMissing = nullptr) override;
	virtual bool SetMouthOpen(float Open) override;
	virtual bool GetPhonemeFilter(float& OutMin, float& OutMax) const override;
	virtual bool ResetAnimToIdle() override;
	virtual bool SetDispositionName(const FString& NewDisposition) override;
	virtual bool SetDisposition(const FString& NewDisposition, int32 NewLevel);

	// Dialogue owns the talking/default face switch. The current baseline is also exposed for the
	// dialogue lipsync compositor, which layers phonemes over it instead of erasing it.
	void SetDispositionTalking(bool bTalking);
	bool IsDispositionTalking() const { return bDispositionTalking; }
	void AccumulateDispositionFacialPose(TMap<FString, float>& InOutPose) const;

	// Body follow: SetOrigin/SetAngles move and re-face the component; SetModel rebuilds it.
	virtual void OnRuntimeTransformChanged() override;
	virtual void OnRuntimeModelChanged() override;
	// R6 — a ScriptHidden/dead character is undrawn and stops ticking its clip.
	virtual void OnDormancyChanged() override;
	// The body a camera shot's `Bone: Bip01 Head` attach point resolves against (11.7).
	virtual USkeletalMeshComponent* GetSkeletalBody() const override { return Visual; }

	// The `parentname` attach point for a character (an ornament or an emitter worn on an NPC).
	// A point character has no brush body, so the standing skeletal body is the only primitive
	// there is; null for a bodiless character, which cannot be a parent.
	virtual UPrimitiveComponent* GetAttachBody() const override { return Visual; }

	// The model stem the clip manifest is keyed by: the model file's lowercased basename.
	FString ModelStem() const;

protected:
	// Which of the three standing idles a disposition's stance set poses. Virtual because only the
	// NPC chain carries VtMB's stance machine — the `+0x98` self-pointer that reaches it is set in
	// `CAI_BaseNPCTroika`'s constructor, so the player has none. The base answer spreads the pick by
	// entity index, which keeps a crowd from posing identically and survives a reload.
	virtual int32 IdleVariant() const { return FMath::Max(0, Handle.Index); }
	bool CommitDisposition(const FString& NewDisposition, int32 NewLevel, bool& bOutChanged,
		FElysiumDisposition* OutOld = nullptr, FElysiumDisposition* OutNew = nullptr);
	void RefreshDispositionExpression();
	// Apply this entity's own draw gate to its body. Virtual because the player's surface is owned by
	// the pawn, not by this entity: the player override publishes the gate through the embodiment and
	// lets the pawn AND it with the camera's eligibility, so the two never race on one flag.
	virtual void GateVisual();

private:
	bool bDispositionTalking = false;
	TMap<FString, float> DispositionFacialPose;
};

// ============================================================================================
// FElysiumCombatCharacter — CBaseCombatCharacter. The sheet, and the 25 datamap inputs
// `docs/vtmb/script_api.md` recovered from datamap 0x1061664c. Shared by the player and every NPC, which is
// where VtMB put it: `MoneyAdd` on a Hammer wire and `pc.MoneyAdd(50)` from a level script are the
// same input on the same class.
//
// The counters below are real fields with real arithmetic behind their inputs; the systems that
// give them *meaning* — the economy (9.10), the sheet and its meters (9.4), disciplines and frenzy
// (P13), inventory (9.8), barter (9.8), the look-at rig (P12) — are not here, and every input they
// belong to logs and no-ops. That is the fail-closed contract 11.4 signs up for: the name resolves
// through the R2 walk and reaches a defined place, so the system that lands later replaces a stub
// rather than inventing a dispatch.
// ============================================================================================

class FElysiumCombatCharacter : public FElysiumAnimating
{
public:
	FElysiumSheet Sheet;

	// The 224 item-entity handles, the active weapon and the reserve ammo pools (9.8). On this node
	// because VtMB puts them here: the player, every NPC and every `item_container` own one.
	FElysiumInventory Inventory;

	// 13.2 — the native active states and the tracked targeted effects. On this node because the
	// thirteen `Active_*` slots are, and because a targeted effect lands on whichever character the
	// cast resolved onto. The rules over it are `Substrate/ElysiumDisciplines.h`.
	FElysiumDisciplineState Disciplines;

	// Push the equipped item's authored `camera_class` at the camera, so drawing a weapon re-runs the
	// arbitration that can force the view mode. **Every writer of `Inventory.ActiveWeapon` calls
	// this**, including the save reconcile — miss that one and a loaded game arbitrates against the
	// class of whatever the last run was holding. A no-op for anything that is not the player.
	void PublishEquippedCameraClass() const;

	int32 Money = 0;              // m_iMoney — the one counter `stats.txt` does not carry as a Stat

	// 13.1 — `trigger_stealth_mod`'s raw aggregate (retail `+0x1084`, which is a
	// CBaseCombatCharacter offset: the trigger's own body is guarded by combat-character
	// embodiment, not by player-ness, so every character can carry one).
	//
	// **Stored unclamped.** Overlapping volumes add here and leaving one subtracts its own
	// contribution back, which only works while the sum is raw — clamping at storage would lose the
	// remainder of a stack the player is still standing in.
	int32 StealthModRaw = 0;

	// `CBaseCombatCharacter::GetStealthModifier` — the ONE clamp, applied at the read. It is added
	// while resolving the category-1 CharacterData walk, which is how a volume moves the Sneaking
	// rating the stealth tables are indexed by (`ElysiumFeats::FeatValue`).
	int32 GetStealthModifier() const { return FMath::Clamp(StealthModRaw, -10, 10); }

	bool bWillTalk = false;       // WillTalk (79 calls) — this character will start a conversation

	// `m_tEffectList` — the `TraitEffectGroup` names in force on this character: its clan's
	// `ClanEffect`, its History's `Effect`, and (later) its items' and its frenzy state's. Held as
	// names because that is what the save stores and what a re-read rulebook re-resolves.
	TArray<FString> Effects;

	// `CBaseCombatCharacter::MoneyAdd` — the one write both the datamap input and a quest's
	// `AwardMoney` go through. Raw `+=`, no floor, as the engine's is.
	void AddMoney(int32 Delta);

	// --- The 25 CBaseCombatCharacter inputs -------------------------------------------------
	// Backed: the four counters. VtMB's own InputMoneyAdd is
	//     if (value.fieldType == FIELD_INTEGER && value.int != 0) MoneyAdd(value.int);
	// i.e. a zero-valued input is a silent no-op (`docs/vtmb/script_api.md`, worked semantics). The typed
	// half of that test cannot be reproduced — a Hammer wire hands us a string param — so the
	// non-zero half is what survives, applied to every counter input for consistency.
	void InputMoneyAdd(const FElysiumInputArgs& Args);
	void InputMoneyRemove(const FElysiumInputArgs& Args);
	void InputHumanityAdd(const FElysiumInputArgs& Args);
	void InputChangeMasqueradeLevel(const FElysiumInputArgs& Args);
	void InputBloodloss(const FElysiumInputArgs& Args);
	void InputBloodgain(const FElysiumInputArgs& Args);
	void InputBloodHeal(const FElysiumInputArgs& Args);
	void InputWillTalk(const FElysiumInputArgs& Args);
	// `Inventory_Remove` — a CLASSPTR input: it takes an ENTITY and only detaches/reindexes it.
	// It is NOT Python's `RemoveItem(classname)`, which also destroys the final entity.
	void InputInventoryRemove(const FElysiumInputArgs& Args);

	// --- Damage and death -------------------------------------------------------------------
	// Two entries, one commit (K6). Typed damage — weapons, disciplines, `trigger_hurt` — carries a
	// descriptor through the shared resolver; the scalar overload is the compatibility fallback and
	// never grows semantics. Both spend what they resolved through `CommitDamage`.
	//
	// The number lands on the sheet: VtMB's `Health` stat (Attributes 15) counts damage TAKEN, with
	// `Max_Health` (17) the ceiling, and `CBaseEntity::m_iHealth` — our `health` keyfield — is the
	// engine-space projection of the pair.
	//
	// `Attacker` may be null (an environmental volume, a script call). It is the roll's source, not
	// the descriptor's activator: the descriptor carries its own `Source` handle for the outputs.
	//
	// `bDisallowFirearmsToBashing` is the attacker's active weapon record's key, which gates the
	// Kindred lethal->bashing conversion in the resolver's step 2. Only the weapon controller can
	// answer it; every other producer (a volume, a script, a discipline) leaves the authored default.
	void TakeDamage(const FElysiumDmg& Dmg, FElysiumCombatCharacter* Attacker,
		bool bDisallowFirearmsToBashing = false);

	// The scalar fallback. Retail's alive path takes its positive damage either from the descriptor
	// apply callback or from here — the scalar route does NOT pass through the resolver, so this
	// builds a direct-input descriptor whose result is the rounded amount and commits it.
	void TakeDamage(float Amount);

	// The one typed health commit (`docs/vtmb/combat-and-damage.md` § "Health commit"):
	// `HealthBuffer` absorbs first, then the unkillable cap, then the damage counter, then Kindred
	// aggravated tracking, then the outputs and the death test. Nothing else writes the health
	// slots from a damage path.
	void CommitDamage(const FElysiumDmg& Dmg);

	// --- The melee opposed records (`combat-and-damage.md` § "Opposed record and reaction margin")
	// The defender's own array, keyed by attacker. A second contact from the same attacker REPLACES
	// its row rather than appending a duplicate, because every reader searches by attacker and would
	// otherwise read a stale margin.
	TArray<FElysiumMeleeRoll> MeleeRolls;

	void StageMeleeRoll(const FElysiumMeleeRoll& Roll)
	{
		for (FElysiumMeleeRoll& Existing : MeleeRolls)
		{
			if (Existing.Attacker == Roll.Attacker)
			{
				Existing = Roll;
				return;
			}
		}
		MeleeRolls.Add(Roll);
	}

	// `GetMeleeDiceRolls` — the record this attacker staged, or null.
	const FElysiumMeleeRoll* FindMeleeRoll(const FElysiumEntityHandle& Attacker) const
	{
		for (const FElysiumMeleeRoll& Roll : MeleeRolls)
		{
			if (Roll.Attacker == Attacker)
			{
				return &Roll;
			}
		}
		return nullptr;
	}

	// `GetNumAttackSuccesses` — word 1 of the matching record, or 0.
	int32 GetNumAttackSuccesses(const FElysiumEntityHandle& Attacker) const
	{
		const FElysiumMeleeRoll* Roll = FindMeleeRoll(Attacker);
		return Roll ? Roll->Lethality : 0;
	}

	// Whether this character soaks as a vampire — the mortal/Kindred half of the soak table. The
	// base answer is the sheet's own clan slot; the NPC leaf overrides it with the authored
	// `Kindred` key off its resolved `npctemplate*.txt` block, which is where retail keeps the
	// classification.
	virtual bool IsKindred() const;

	// The victim's authored template damage filters (`DamageFilterBashing` / `_Lethal` /
	// `_Aggravated`, and `DamageFilterFlame` when `bFlame`). Returns false when this character
	// authors none. Read by the damage resolver to populate the descriptor's filter accumulator,
	// which is not yet applied — see `ElysiumDamage::Apply` step 9.
	virtual bool GetTemplateDamageFilter(EElysiumDmgFamily /*Family*/, bool /*bFlame*/,
		float& /*OutFilter*/) const
	{
		return false;
	}

	// Re-derive the entity's `health`/`max_health` keyfields from the sheet's damage and ceiling
	// slots. Called after anything writes either one.
	void SyncHealthFromSheet();
	bool IsUnkillable() const { return bUnkillable; }
	void SetUnkillable(bool bValue) { bUnkillable = bValue; }

	// Fires the character's OnDeath output once. The player leaf overrides to end the run as well.
	virtual void OnKilled();
	bool HasReportedDeath() const { return bDeathReported; }
	void SetDeathReportedForRestore(bool bValue) { bDeathReported = bValue; }

	// The masquerade counter hit its ceiling. Only the player's ends the run — the counter is on
	// this class because the sheet is — so the base only reports and the player leaf overrides.
	virtual void OnMasqueradeBreached();

	virtual FElysiumCombatCharacter* AsCombatCharacter() override { return this; }

	// `stats.txt`'s four containers, or null in a bare world — the sheet's clamps come from here.
	const FElysiumStatTable* SheetRules() const;
	// The resolved trait-effect layer, or null when this character carries none / no rulebook is up.
	const FElysiumSheetEffects* SheetEffects() const { return EffectLayer.Get(); }
	// Re-resolve `Effects` against the rulebook and re-derive every current value from it. Called
	// after anything writes the list — the clan lands, a History is chosen, a save is restored.
	void RebuildEffects();
	// base -> effects -> bounds, over the rules and the effect layer this character holds.
	void RecomputeSheet();

	// Move a trait's base by Delta and re-derive the current value under the effective bounds.
	void AddTrait(EElysiumTraitContainer Container, int32 Slot, int32 Delta);

	// --- The sheet reads the script surface calls (9.4c) --------------------------------------
	// `CalcFeat(feat)` — the feat RATING, not a roll. A name the feat table does not own falls back
	// to the trait of that name (`docs/vtmb/script_api.md`, a marked divergence); neither resolving reads 0.
	int32 CalcFeat(const FString& Name) const;
	// `BumpStat(stat, times)` — `times` dots onto the BASE, each under VtMB's own hardcoded
	// `GetBase < 5` ceiling and the stat's own gate. Cannot decrement. Returns the dots that landed.
	int32 BumpStat(const FString& Stat, int32 Times);
	// `GetMasqueradeLevel()` — the 0..5 violation counter.
	int32 GetMasqueradeLevel() const;
	// `DialogDiscipline` is deliberately NOT here. Its whole spec is one doc string ("uses a
	// discipline in dialog, doesn't deduct blood points"), the corpus never calls it, and its
	// handler is undecompiled — so any number it returned would be invented. It stays a logged
	// stub until the discipline layer (P13) defines what using a power in dialogue does.

	// --- The counters, with their rules ------------------------------------------------------
	// Humanity moves by `Delta`, DOUBLED when the character's effect layer carries
	// `Fx_Humanity_Mods_Doubled` — Toreador's gift and its bane are the same flag.
	void AddHumanity(int32 Delta);
	// The masquerade counter. Reaching its authored ceiling (5) is the second game-over condition.
	void ChangeMasqueradeLevel(int32 Delta);
	void AddBlood(int32 Delta);
	// The `BloodPool` slot's current value — what a feed pulse moves and what teardown reads.
	int32 BloodPoolValue() const;
	// Spend `Blood` blood points to heal `BloodToHealthRatio` (10) damage each — `VampHeal_Info`'s
	// `VampFeedingHeal_Info`. Returns the damage actually healed.
	int32 BloodHeal(int32 Blood);
	// Take `Points` off the damage counter without spending anything. The blood-for-health trade
	// above is one caller; the feed pulse is the other, and it must NOT spend, because the pulse's
	// blood-pool increment already happened. Returns the damage actually healed.
	int32 HealDamage(int32 Points);

	// --- Feeding (B6, `docs/vtmb/feeding.md`) --------------------------------------------------
	// The whole transaction is here rather than in a service because retail puts it here: `FeedBegin`,
	// `Feed` and `FeedInterrupt` are CBaseCombatCharacter virtuals, and the fields they own are this
	// class's. Implemented in `Substrate/ElysiumFeed.cpp`.
	FElysiumFeedState FeedState;

	// `CBasePlayer::Replenish` + `AttemptFeed`: eligibility, then the paired start. Returns the
	// verdict whether or not it accepted; on acceptance the pair is running and this character is
	// its attacker. Refuses while either side is already paired.
	EElysiumFeedVerdict AttemptFeed(FElysiumCombatCharacter& Victim);

	// The eligibility half on its own, with no side effects — what the acceptance-policy tests drive
	// and what `AttemptFeed` calls. Rolls the victim's Hacking pool when it reaches the opposed
	// branch, so it is not const.
	EElysiumFeedVerdict EvaluateFeedAcceptance(FElysiumCombatCharacter& Victim);

	// The four automatic-acceptance activity states. OPEN: this runtime has no ACT_* state machine,
	// so the closest observable stand-in is the character's current disposition name.
	bool IsFeedAutoAcceptState() const;
	// Retail's `ResistsFeeding` predicate. OPEN: its body is unrecovered; what IS recovered is the
	// `Fx_No_Resist_Feeding` trait-effect flag that names a target which does not resist.
	virtual bool ResistsFeeding() const;

	// Another owner already has this body, so it cannot be grappled (K7). The base answers for the
	// one owner every character can have — a `scripted_sequence` beat; the NPC leaf adds its own
	// dialogue session.
	virtual bool IsFeedBusy() const;

	// The animation-event bridge (`feeding.md` recreation contract item 5). 4007 opens FeedBegin,
	// 4006 completes the transaction/camera lease and leaves the remaining release pose to finish;
	// 5116 is presentation only. The bake carries no MDL animation events
	// (see `ElysiumFeed.cpp`), so the feed state machine raises these itself off the decoded clip
	// cycles; a real notify path replaces the caller, never this handler.
	void OnFeedAnimEvent(int32 EventId);

	// Event 4007's handler. Refuses a null/invalid target, a disallowed attacker and a second active
	// feed; clears the per-feed counters, stores the victim, calls the victim's feed-begin callback
	// (which fires `OnFedUponBegin`) and seeds the cadence from the victim's blood pool.
	bool FeedBegin(FElysiumCombatCharacter& Victim);

	// At most one scheduled pulse per update, against the substrate clock. Returns whether a pulse
	// was performed.
	bool Feed(double Now);

	// The one idempotent teardown (`feeding.md` § "Interruption, completion and outputs"). Safe to
	// call on a character that is not feeding.
	void FeedInterrupt();

	// End any pairing this character is part of, from either role — what incoming damage, death and
	// world teardown reach for. On the victim it routes to the attacker's FeedInterrupt.
	void BreakFeed();

	// One step of the paired state machine, driven from the feeder's think. Returns true while the
	// pair is still running (the caller then leaves the body alone).
	bool TickFeed(double Now);

	// The second Feed press clears the continuation latch. The physical button-up edge is inert;
	// teardown still comes out of the paired release family rather than the input handler.
	void SetFeedContinuation(bool bContinue) { FeedState.bContinuation = bContinue; }

	bool IsFeedPaired() const { return FeedState.IsPaired(); }

	// Log-and-no-op body for the inputs whose system has not landed. Public because the registration
	// thunks are free lambdas, not members. Named so the log line reads as a recorded gap.
	// `DeclaringClass` is the chain level the input belongs to, so one reported row covers every
	// classname that inherits it; null reads as CBaseCombatCharacter, where most of them live.
	void PendingInput(const TCHAR* Input, const TCHAR* Owner, const FElysiumInputArgs& Args,
		const TCHAR* DeclaringClass = nullptr) const;

	// --- Gaze (12.4) --------------------------------------------------------------------------
	// VtMB puts this on CBaseCombatCharacter and so do we. The class decides *where to look*; the
	// visual layer decides what that looks like, and the only thing crossing between them is one
	// world point per character per frame — which is exactly the hop retail networks as
	// `m_viewtarget`.
	//
	// Datamap-backed, at the recovered offsets: m_vEyeLookTarget@0x0E44 (commanded),
	// m_vCurEyeTarget@0x0E50 (smoothed), m_hEyeLookTarget@0x0E64, m_flEyeIntegRate@0x0E3C.
	FVector EyeLookTarget = FVector::ZeroVector;
	FVector CurEyeTarget = FVector::ZeroVector;
	float EyeIntegRate = 0.f;
	// The smoothed point starts at nothing rather than at the origin, and the difference matters:
	// testing it against zero instead would re-seed a character that is legitimately looking at the
	// world origin, snapping its eyes every frame it stayed there.
	bool bCurEyeTargetSeeded = false;
	// `m_hEyeLookTarget` is a handle, which is not a field type the registry carries, so the saved
	// form is the target's targetname — the same thing the save would have to re-resolve anyway.
	FString EyeLookTargetName;

	// Retail's scripted-mode int lives at 0x0E68 and the datamap does NOT carry it, so a scripted
	// look-at does not survive a save. Registered with EElysiumField::None to reproduce that.
	int32 EyeLookMode = 0;

	// The saccade layer. Not in retail's datamap either: a fidget in progress is re-derived, not
	// restored.
	// Two independent clocks, and they must stay independent: the scan re-picks what to look at
	// every 1-5 s, while the fidget holds each keypad cell for a fraction of a second. Sharing one
	// field would make a saccade cancel the scan's schedule and vice versa.
	float NextEyeLookTime = 0.f;   // when the autonomous scan next re-picks a subject
	float NextFidgetTime = 0.f;    // when the current fidget hold expires
	int32 FidgetStep = -1;         // -1 = not fidgeting, else 0..2 into the disposition's triple
	int32 FidgetCell = 0;          // the keypad cell the current step resolved to

	// Integration is a fixed 0.1 s step, so the leftover frame time has to be carried rather than
	// discarded — dropping it would make the convergence rate depend on the frame rate.
	float EyeIntegAccumulator = 0.f;

	// Head yaw/pitch, integrated through retail's 0.8/0.2 filter and driving nothing — see
	// TickGaze. Kept so the values can be inspected and so the filter stays reproducible.
	float HeadYaw = 0.f;
	float HeadPitch = 0.f;

	// One gaze step. `HeadPos`/`HeadForward` come from the live animated head bone when the body
	// has one, and fall back to EyePosition()/EyeAngles() when it does not — the input side of the
	// look-at is live in retail even though the output side is inert. Returns the smoothed world
	// point the eyes should converge on.
	//
	// The tuning is passed in rather than looked up: every rate and interval here is content, and
	// taking it as an argument keeps this class free of the engine subsystem that owns the parsed
	// table, so the whole cascade is assertable under -nullrhi.
	//
	// `DialogPovPoint`, when set, is where the camera is for a shot whose `DialogPOV` is on: it
	// replaces the player as the dialogue arm's subject, and nothing else. Passed as a point rather
	// than a camera so this stays free of the engine half.
	FVector TickGaze(float Now, float DeltaSeconds, const FVector& HeadPos, const FVector& HeadForward,
		const struct FElysiumEyeTargetTuning& Tuning, const FVector* DialogPovPoint = nullptr);

	// The four scripted look-at inputs. Center deliberately behaves as Eye — see the .cpp.
	void InputLookAtEntityEye(const FElysiumInputArgs& Args);
	void InputLookAtEntityCenter(const FElysiumInputArgs& Args);
	void InputLookAtEntityOrigin(const FElysiumInputArgs& Args);
	void InputLookAtEntityDefault(const FElysiumInputArgs& Args);

	virtual FVector EyePosition() const override;

	// What is left after the datamap: a `base_*` name no compiled slot owns. It still has to read a
	// number rather than raise, because the gates that ask (`pc.base_Celerity > 0`) are written
	// against a sheet where every name resolves.
	virtual bool GetDynamicField(FName Name, FElysiumVariant& Out) const override;
	virtual bool SetDynamicField(FName Name, const FElysiumVariant& Value) override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

protected:
	// Called by `CommitDamage` once the health commit has landed, before the outputs fire. The NPC
	// leaf records the attacker/time/amount its senses and memory read; the base does nothing,
	// because the player has no memory of who hit it.
	virtual void OnDamageCommitted(const FElysiumDmg& /*Dmg*/) {}

	// Drop the Bloodshield effect an exhausted `HealthBuffer` ends, and rebuild the effect layer.
	void EndBloodshield();

	// --- Feeding internals ---------------------------------------------------------------------
	// The pair's other half as a combat character, or null.
	FElysiumCombatCharacter* ResolveFeedPeer() const;
	// `StartGrappleAttack(target, mode 0)` reduced to what B6 owns: claim both bodies, face the
	// attacker at the victim, freeze the victim's motor, and start the engage clip on both.
	void StartFeedPair(FElysiumCombatCharacter& Victim, double Now);
	// `EndGrapple` — release both bodies and clear the pairing on both halves. Idempotent.
	void EndFeedGrapple();
	// Move to the next phase of the ordinary state family.
	void AdvanceFeedPhase(double Now);
	// Whether the loop should hand over to the release family. OPEN — see `ElysiumFeed.cpp`.
	bool ShouldReleaseFeed() const;
	void EnterFeedRelease(double Now);
	// Resolve the complementary height/side pair and play it atomically. Returns the authoritative
	// attacker duration; a missing rendered half warns and falls back to decoded metadata so the
	// transaction continues headlessly without a mismatched one-sided pose.
	float PlayFeedPhaseClips(EElysiumFeedPhase Phase, double Now);
	// The current partner-height cell, measured from live body bounds when both halves exist and
	// deterministically short-victim/tall-attacker in a headless tie.
	uint8 FeedVictimHeightCell() const;
	void EnsureFeedCamera();
	void ReleaseFeedCamera();
	void PlayFeedStartAudio(FElysiumCombatCharacter& Victim);
	void PlayFeedLoopAudio(FElysiumCombatCharacter& Victim);
	void PlayFeedEndAudio(FElysiumCombatCharacter* Victim);
	void StopFeedLoopAudio();
	FElysiumVoiceHandle SubmitFeedCue(const TCHAR* Cue, bool bLooping, bool bHeartbeat = false);
	// Shared transaction/output teardown. Event 4006 asks to keep the release pose; damage, death and
	// invalidation do not.
	void CompleteFeedTransaction(bool bKeepReleaseTail);
	// The next think the pair needs: the earlier of the phase boundary and the next pulse.
	void ScheduleFeedThink(double Now);
	// The damage one pulse heals on the feeder. OPEN — see `ElysiumFeed.cpp`.
	int32 FeedHealAmount() const;
	// The victim half's teardown: unfreeze, forget the attacker, hand the body back.
	void EndFeedVictimRole();

	// Engine-service capability, never save state. A restored logical pair reacquires it from
	// TickFeed; stale map-epoch handles are discarded by the service.
	FElysiumCameraHandle FeedCameraHandle;
	bool bFeedCameraAcquireFailed = false;
	FElysiumVoiceHandle FeedLoopVoice;
	FElysiumVoiceHandle FeedHeartbeatVoice;
	bool bFeedAudioAcquireFailed = false;

	bool bUnkillable = false;
	bool bDeathReported = false;   // OnKilled fires once, however much damage arrives after

	// Rebuilt from `Effects`, never copied between characters: it is a resolution of the names, and
	// the names are the truth. Null until something puts a group on this character.
	TSharedPtr<FElysiumSheetEffects> EffectLayer;
};

// ============================================================================================
// FElysiumPlayer — CBasePlayer / CHL2_Player. The player character: the sheet above, the 10
// recovered player-datamap inputs (`docs/vtmb/script_api.md`, datamap 0x10580edc — the header states 11 and
// one is still unrecovered), and the link to the pawn that is its body.
//
// Its origin and facing are the pawn's, sampled once a frame by the world, so `GetOrigin()`,
// a landmark offset and a `trigger_look` all read the same place every other entity reads. Writing
// them (`point_teleport`, a scripted `pc.SetOrigin(...)`) moves the pawn through the embodiment,
// which is the same shape as an NPC moving its skeletal body.
// ============================================================================================

class FElysiumPlayer final : public FElysiumCombatCharacter
{
public:
	FElysiumLawState Law;
	// Cycle 10b — the response/Masquerade-timer/pursuit half of the same domain. Mirrored from the
	// record for the map's lifetime exactly as `Law` is.
	FElysiumPoliceState Police;
	TArray<FElysiumXpEntry> ExperienceLog;
	TArray<FString> EmailFlags;

	// 13.2 — `vdiscipline_int`'s stored compiled index and the remembered tier beside it, plus the
	// cast counter a committed targeted cast increments. Mirrored from the record for the map's
	// lifetime, exactly as `ExperienceLog` and `Money` are.
	int32 SelectedDiscipline = INDEX_NONE;
	int32 SelectedTier = 0;
	int32 DisciplineCastCount = 0;

	// `AddExperience`'s accumulators — the record's, mirrored here for the map's lifetime.
	float ExperienceRemainder = 0.f;
	float LifetimeExperience = 0.f;

	// 13.1 — the stealth target surface and the observer snapshot it publishes. Both live on the
	// player leaf because retail's fields do (`+0x1c6c..` and `+0x1cc0..`). The rules are
	// `Private/Substrate/ElysiumStealth.h`; the recompute hangs off `Think` below, which is reached
	// only through `FElysiumEntityWorld::RunPlayerThink`.
	FElysiumStealthSurface Stealth;
	FElysiumStealthObserver Observer;
	// The candidate the senses passes have offered since the last commit. Session state: it is
	// rebuilt from the observers' own caches within one sight cadence, so it is not saved.
	FElysiumStealthObserver PendingObserver;

	// Offer this player an observer, from an NPC's own sight pass. Nearest wins; an offer for the
	// incumbent refreshes it. Nothing is published here — `Think` commits, which is what keeps the
	// HUD downstream of gameplay rather than beside it.
	void OfferStealthObserver(const FElysiumEntityHandle& Who, float DistanceCm, float RadiusCm,
		bool bDetected, double Now);

	virtual void Spawn() override;

	// The player's own think, run PRE-move by `FElysiumEntityWorld::RunPlayerThink` — which is
	// where retail runs it, inside CPlayerMove::RunCommand. B6 drives the feed transaction from
	// here, so a pulse deadline is measured on the substrate clock and never on a timer.
	virtual void Think() override;

	// `AwardExperience("<key>")` — the whole walk: refuse a key already in the give-once ledger,
	// look it up (a miss awards and appends nothing, so it retries on every fire), add
	// `Experience_Modifier` above 2 XP, then bank `floor(value/100)` into the `Experience` slot
	// KEEPING the sub-100 residue. Returns the whole XP points banked by this award.
	int32 AwardExperience(const FString& Key);
	// Whether the give-once ledger already holds the key.
	bool HasAwarded(const FString& Key) const;

	// Resolve this character's clan into its `ClanEffect` group and rebuild the effect layer. The
	// clan is a sheet slot, so this is called whenever that slot could have moved.
	void RefreshClanEffects();

	// Copy the session record in / out. The entity is the live view for the map's lifetime; the
	// record is what crosses the boundary.
	void Hydrate(const FElysiumPlayerRecord& Record);
	void Dehydrate(FElysiumPlayerRecord& Record) const;

	// Sample the body's transform into the entity's own fields (once a frame, from the world's
	// tick). Writes the fields directly — going through SetRuntimeOrigin would teleport the pawn
	// back to where it already is, every frame.
	void SyncFromBody();

	// The pawn follows the entity: a write to origin/angles places the body.
	// If !playercontroller is live, explicit writes also update its teardown anchor; SyncFromBody's
	// ordinary movement sampling does not, so choreography can still stage that duplicate itself.
	virtual void OnRuntimeTransformChanged() override;
	// SetModel swaps the skeletal surface attached to the movement pawn. The pawn/hull itself stays
	// put; the same component remains the FElysiumAnimating visual used by choreo clip playback.
	virtual void OnRuntimeModelChanged() override;

	// The player's surface has exactly one writer — the pawn, from the camera's draw policy. This
	// entity contributes only its own hide state and never touches the component's flags directly.
	virtual void GateVisual() override;

	// The run ends: fire OnDeath, then tell the session (which raises the game-over screen).
	virtual void OnKilled() override;
	// The other way a run ends — the masquerade counter at 5.
	virtual void OnMasqueradeBreached() override;

	// --- The 10 recovered player inputs ------------------------------------------------------
	void InputGiveItem(const FElysiumInputArgs& Args);            // STRING, 126 calls — 9.8
	void InputAwardExperience(const FElysiumInputArgs& Args);      // STRING (a vdata key), 77 calls
	void InputSetCriminalLevel(const FElysiumInputArgs& Args);
	void InputSetInvestigateLevel(const FElysiumInputArgs& Args);
	void InputSetSupernaturalLevel(const FElysiumInputArgs& Args);

	// Cycle 10b — the monotonic act counts, read by the NPC condition lane (conditions 31-34) that
	// compares them against its own processed counts. Read-only here: only a channel write through
	// `ElysiumLaw` increments them.
	int32 CriminalActCount() const { return Law.CriminalCount; }
	int32 SupernaturalActCount() const { return Law.SupernaturalCount; }

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
};
