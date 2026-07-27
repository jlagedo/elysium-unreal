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
// per class (R2) only pays off if the classes are the same ones. Design: `runtime-architecture.md`
// sections 5-6; the input inventory: `script_api.md`.

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
// the 1..7 `ClanNameFunc` display enum, and not `clandoc`'s ordering. `game_runtime.md` section 3
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
// (`savegame_format.md`). `AwardExperience` names a `vdata` entry, so the string is the key.
struct FElysiumXpEntry
{
	FString Entry;
	int32   Amount = 0;
};

// The criminal / supernatural / investigate counters `SetCriminalLevel`, `SetSupernaturalLevel` and
// `SetInvestigateLevel` write. Their decay timers are the police-response system's (10.7).
struct FElysiumLawState
{
	int32 Criminal = 0;
	int32 Supernatural = 0;
	int32 Investigate = 0;
};

// The durable half of the player: session lifetime, so it crosses a map boundary. The entity is the
// *live* view; this is the truth that survives the world it lived in. Hydrated into the player
// entity at map build, dehydrated back out when the world is torn down (travel, quit, reload) and,
// at 11.9, into the save.
//
// Health is deliberately NOT a "player stat" here: `m_iHealth` is a Save-flagged entity field on the
// chain (`save-architecture.md` section 4 — VtMB's own placement), and the copy below exists only
// because our entity dies with its map, so something has to carry the value across a travel. The
// entity's field stays the one the save walk enumerates.
//
// Inventory (`item_*` entities owned by the player) and the equipped-weapon handles join this record
// at 9.8; the quest map and `G` are still the game-state subsystem's own stores until 11.9 gathers
// the save blocks. Neither is stubbed here — an empty field nothing fills would read as support.
struct FElysiumPlayerRecord
{
	// Humanity, blood, masquerade, clan and sex are all slots on this — VtMB holds them in the
	// Attributes container, not as members beside it, so there is nothing to mirror.
	FElysiumSheet Sheet;

	int32 Money = 0;         // m_iMoney — not a Stat in `stats.txt`

	int32 Health = 0;        // carried across a travel; 0 = "not seeded yet" (the entity seeds it)
	int32 MaxHealth = 0;

	TArray<FElysiumXpEntry> ExperienceLog;   // EXPERIENCE_ENTRY — itemised, not a total
	TArray<FString>         Effects;         // m_tEffectList
	TArray<FString>         EmailFlags;      // the Player block, save-architecture.md section 3
	FElysiumLawState        Law;

	// `AddExperience`'s two accumulators. The award is in hundredths — every real
	// `experience_table` row is `N01` — and the division **keeps its remainder**, so the residue is
	// state, not a rounding artefact: one bonus point falls out per 100 awards.
	float ExperienceRemainder = 0.f;   // m_flExpRemainder
	float LifetimeExperience = 0.f;    // m_flLifetimeExp — raw, never divided

	// MakePlayerUnkillable / MakePlayerKillable (events_player). Latched here as well as on the
	// entity because the tutorial sets it at map load and it must survive the warp into the next map.
	bool bUnkillable = false;

	void Reset() { *this = FElysiumPlayerRecord(); }
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
	// (`entity_io.md`). Carried here for the whole chain; only the prop leaf paints with it today.
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
	virtual bool ResetAnimToIdle() override;
	virtual bool SetDispositionName(const FString& NewDisposition) override;

	// Body follow: SetOrigin/SetAngles move and re-face the component; SetModel rebuilds it.
	virtual void OnRuntimeTransformChanged() override;
	virtual void OnRuntimeModelChanged() override;
	// R6 — a ScriptHidden/dead character is undrawn and stops ticking its clip.
	virtual void OnDormancyChanged() override;
	// The body a camera shot's `Bone: Bip01 Head` attach point resolves against (11.7).
	virtual USkeletalMeshComponent* GetSkeletalBody() const override { return Visual; }

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
// `script_api.md` recovered from datamap 0x1061664c. Shared by the player and every NPC, which is
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

	int32 Money = 0;              // m_iMoney — the one counter `stats.txt` does not carry as a Stat

	bool bWillTalk = false;       // WillTalk (79 calls) — this character will start a conversation

	// `m_tEffectList` — the `TraitEffectGroup` names in force on this character: its clan's
	// `ClanEffect`, its History's `Effect`, and (later) its items' and its frenzy state's. Held as
	// names because that is what the save stores and what a re-read rulebook re-resolves.
	TArray<FString> Effects;

	// --- The 25 CBaseCombatCharacter inputs -------------------------------------------------
	// Backed: the four counters. VtMB's own InputMoneyAdd is
	//     if (value.fieldType == FIELD_INTEGER && value.int != 0) MoneyAdd(value.int);
	// i.e. a zero-valued input is a silent no-op (`script_api.md`, worked semantics). The typed
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
	// to the trait of that name (`script_api.md`, a marked divergence); neither resolving reads 0.
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
	// Spend `Blood` blood points to heal `BloodToHealthRatio` (10) damage each — `VampHeal_Info`'s
	// `VampFeedingHeal_Info`. Returns the damage actually healed.
	int32 BloodHeal(int32 Blood);

	// Log-and-no-op body for the inputs whose system has not landed. Public because the registration
	// thunks are free lambdas, not members. Named so the log line reads as a recorded gap.
	void PendingInput(const TCHAR* Input, const TCHAR* Owner, const FElysiumInputArgs& Args) const;

	// What is left after the datamap: a `base_*` name no compiled slot owns. It still has to read a
	// number rather than raise, because the gates that ask (`pc.base_Celerity > 0`) are written
	// against a sheet where every name resolves.
	virtual bool GetDynamicField(FName Name, FElysiumVariant& Out) const override;
	virtual bool SetDynamicField(FName Name, const FElysiumVariant& Value) override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

protected:
	bool bUnkillable = false;
	bool bDeathReported = false;   // OnKilled fires once, however much damage arrives after

	// Rebuilt from `Effects`, never copied between characters: it is a resolution of the names, and
	// the names are the truth. Null until something puts a group on this character.
	TSharedPtr<FElysiumSheetEffects> EffectLayer;
};

// ============================================================================================
// FElysiumPlayer — CBasePlayer / CHL2_Player. The player character: the sheet above, the 10
// recovered player-datamap inputs (`script_api.md`, datamap 0x10580edc — the header states 11 and
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
	// The player's model is the pawn, not a spawned skeletal body — nothing to rebuild.
	virtual void OnRuntimeModelChanged() override {}

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
