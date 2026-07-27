#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"
#include "ElysiumSheetSlots.h"

class USkeletalMeshComponent;

struct FElysiumStatTable;      // Private/Substrate/ElysiumRulebook.h — the data half of the sheet
struct FElysiumClanTemplate;

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
	void AddBase(EElysiumTraitContainer Container, int32 Slot, int32 Delta);

	// base -> [trait effects] -> clamp to the authored Min/Max. The trait-effect pass is 9.4c/f;
	// until then this is base and the clamp. A null table leaves current equal to base.
	void RecomputeCurrent(const FElysiumStatTable* Stats);

	// Every slot takes its `stats.txt` `Default`, base and current alike.
	void SeedFrom(const FElysiumStatTable& Stats);
	// Overlay a resolved clan / NPC template's authored ratings. An absent trait means inherit, so
	// only what the template holds is written (`FElysiumClanTable::Resolve` folds the parent chain).
	void ApplyTemplate(const FElysiumClanTemplate& Template, const FElysiumStatTable* Stats);

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

	virtual FElysiumCombatCharacter* AsCombatCharacter() override { return this; }

	// `stats.txt`'s four containers, or null in a bare world — the sheet's clamps come from here.
	const FElysiumStatTable* SheetRules() const;
	// Move a trait's base by Delta and re-derive the current value under the authored bounds.
	void AddTrait(EElysiumTraitContainer Container, int32 Slot, int32 Delta);

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
	TArray<FString> Effects;
	TArray<FString> EmailFlags;

	virtual void Spawn() override;

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

	// --- The 10 recovered player inputs ------------------------------------------------------
	void InputGiveItem(const FElysiumInputArgs& Args);            // STRING, 126 calls — 9.8
	void InputAwardExperience(const FElysiumInputArgs& Args);      // STRING (a vdata key), 77 calls
	void InputSetCriminalLevel(const FElysiumInputArgs& Args);
	void InputSetInvestigateLevel(const FElysiumInputArgs& Args);
	void InputSetSupernaturalLevel(const FElysiumInputArgs& Args);

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
};
