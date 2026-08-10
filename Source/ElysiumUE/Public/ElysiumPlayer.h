#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"
#include "ElysiumSheetSlots.h"

class USkeletalMeshComponent;

struct FElysiumStatTable;      // Private/Substrate/ElysiumRulebook.h — the data half of the sheet
struct FElysiumClanTemplate;
struct FElysiumSheetEffects;   // Private/Substrate/ElysiumSheetMath.h — the trait-effect layer

// 11.4 (S3) — the player is an entity; the pawn is its body.
//
// This header holds the two middle chain nodes VtMB's own datamap chain has and the player leaf
// under them, plus the session-lifetime record the entity hydrates from:
//
//     FElysiumEntity              CBaseEntity           keyfields, dormancy, I/O, think
//      +- FElysiumAnimating       CBaseAnimating        a body to follow, clips, skin, disposition
//          +- FElysiumCombatCharacter  CBaseCombatCharacter  the SHEET + its 25 inputs
//              +- FElysiumNpc          CAI_BaseNPC       (ElysiumNpcClasses.cpp)
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

// The criminal / supernatural / investigate counters `SetCriminalLevel`, `SetSupernaturalLevel` and
// `SetInvestigateLevel` write. Their decay timers are the police-response system's (10.7).
struct FElysiumLawState
{
	int32 Criminal = 0;
	int32 Supernatural = 0;
	int32 Investigate = 0;
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

	// --- Declared, not built: slice (b) ---------------------------------------------------------
	// Player drop (`inven_drop`, world entity PRESERVED — never a shortcut through ScriptRemove),
	// container transfer (`SpawnItemInContainer`/`AddEntityToContainer`/`DeleteItems`) and the
	// barter verbs (`vbarter Take|Give|Buy|Sell`) are distinct operations with distinct
	// entity-lifetime effects (`inventory.md` §5.3, §7). They are absent rather than half-built.
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
	// The disposition idle is spread across the three standing idles VtMB authors per disposition,
	// seeded from the entity's own index so the pick survives a reload.
	int32 IdleVariant() const { return FMath::Max(0, Handle.Index); }
	void GateVisual();
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

	int32 Money = 0;              // m_iMoney — the one counter `stats.txt` does not carry as a Stat

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
	// The receiver `trigger_hurt`, a door closing, and (later) combat all reach.
	//
	// The number lands on the sheet: VtMB's `Health` stat (Attributes 15) counts damage TAKEN, with
	// `Max_Health` (17) the ceiling, and `CBaseEntity::m_iHealth` — our `health` keyfield — is the
	// engine-space projection of the pair. So this adds to the damage slot and re-derives the
	// keyfield, which is what the save walk and the body read. Unkillable characters stop at 1 hp.
	void TakeDamage(float Amount);

	// Re-derive the entity's `health`/`max_health` keyfields from the sheet's damage and ceiling
	// slots. Called after anything writes either one.
	void SyncHealthFromSheet();
	bool IsUnkillable() const { return bUnkillable; }
	void SetUnkillable(bool bValue) { bUnkillable = bValue; }

	// Fires the character's OnDeath output once. The player leaf overrides to end the run as well.
	virtual void OnKilled();

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
	// 4006 requests teardown, 5116 is presentation only. The bake carries no MDL animation events
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

	// `-feed`: the release edge clears the continuation latch and nothing else. Retail's release
	// publisher does not call FeedInterrupt; teardown still comes out of the release family.
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
	// Play this phase's clip on both bodies, best effort. A body that cannot answer is logged and
	// the transaction continues (K10: headless correctness never depends on a rendered body).
	void PlayFeedPhaseClips(EElysiumFeedPhase Phase, double Now);
	// The next think the pair needs: the earlier of the phase boundary and the next pulse.
	void ScheduleFeedThink(double Now);
	// The damage one pulse heals on the feeder. OPEN — see `ElysiumFeed.cpp`.
	int32 FeedHealAmount() const;
	// The victim half's teardown: unfreeze, forget the attacker, hand the body back.
	void EndFeedVictimRole();

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
	TArray<FElysiumXpEntry> ExperienceLog;
	TArray<FString> EmailFlags;

	// `AddExperience`'s accumulators — the record's, mirrored here for the map's lifetime.
	float ExperienceRemainder = 0.f;
	float LifetimeExperience = 0.f;

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
	virtual void OnRuntimeTransformChanged() override;
	// SetModel swaps the skeletal surface attached to the movement pawn. The pawn/hull itself stays
	// put; the same component remains the FElysiumAnimating visual used by choreo clip playback.
	virtual void OnRuntimeModelChanged() override;

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

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
};
