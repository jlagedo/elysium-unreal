#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
// By value: the clan options the preset picker holds are `ElysiumArenaCast::FClanOption`s, so the
// cast header rather than a declaration. It is `!UE_BUILD_SHIPPING`, which `ENABLE_COG` implies.
#include "Debug/ElysiumArenaCast.h"
#include "Debug/ElysiumCogWindow.h"
#include "Substrate/ElysiumRelationships.h"
#include "imgui.h"

class FElysiumEntityWorld;
class FElysiumNpc;

// The cast window: who is standing, what they are thinking, and how to put more of them there.
//
// **What this window is for.** Every part of the NPC decision chain is landed plain C++ with its own
// trace — the mind's admission and body arbitration, the gathered condition set, the schedule
// runner's task program, the enemy-selection transaction, the sensory memory — and none of it has a
// live surface. A decision that can only be read out of a log after the fact is a decision nobody
// watches while it happens, which is how an AI acquires a reputation for being "broken" when what
// it actually did was refuse a claim, fail a task by name, or never gather the condition that would
// have promoted it. Every tab below draws one link of that chain from the runtime's own structures.
//
// **It observes; it does not substitute.** The one exception is deliberate and is stated where it
// happens: the Cast tab CREATES characters and arms the player, because a room with nobody in it
// answers no question at all. Those two go through `Debug/ElysiumArenaCast.h`, which uses the same
// runtime-spawn and inventory doors `npc_maker` and `GiveNamedItem` use. Nothing here writes a
// state, starts a schedule, commits an enemy or moves a body — a character that will not fight is a
// finding, and a panel that could make it fight would have destroyed the finding.
//
// **It is not gated on the green room.** The green room owns a PLACE (`Debug/ElysiumArenaSpec.h`);
// this window owns the CAST, and it reads the entity world, so it works identically in the arena
// and in a loaded map. The arena's pads only appear as spawn destinations when one is standing.
class FElysiumCogWindow_Npc : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;
	// The world overlay is drawn from here rather than from RenderContent, so it survives the window
	// being closed. Watching a fight means not looking at a panel.
	virtual void GameTick(float DeltaTime) override;

private:
	// The selected character, or null. Selection is the shared Elysium one, so picking a row here
	// also points the Inspector at it.
	FElysiumNpc* SelectedNpc() const;
	// Every live `npc_*` character leaf in the world, in entity order.
	void GatherNpcs(TArray<FElysiumNpc*>& Out) const;

	// --- The cast ---------------------------------------------------------------------------------
	// The roster, the spawn controls, and the player's loadout. The one tab that writes.
	void RenderCast(FElysiumEntityWorld& World);
	void RenderSpawnControls(FElysiumEntityWorld& World);
	void RenderPlayerLoadout(FElysiumEntityWorld& World);
	void RenderRoster(FElysiumEntityWorld& World, const TArray<FElysiumNpc*>& Npcs);

	// --- The decision chain, one tab per link -----------------------------------------------------
	// Admission, state vs ideal state, the body-owner arbiter and its generation, and the mind's own
	// 16-row transition trace — which the schedule runner also writes into, so one read shows
	// stimulus, state, owner and program in order.
	void RenderMind(FElysiumNpc& Npc);
	// Resolved perception tuning and everything the NPC remembers: the committed enemy, last-seen by
	// relation category, the last heard stimulus, the last damage packet and the occlusion debounce.
	// "Not currently visible" is not "forgotten", and this is where the difference is legible.
	void RenderSenses(FElysiumEntityWorld& World, FElysiumNpc& Npc);
	// This pass's gathered conditions BESIDE the running schedule's interrupt mask, with the
	// intersection called out. That pairing is the whole of why a program was or was not
	// re-selected: a condition alone interrupts nothing — the schedule decides.
	void RenderConditions(FElysiumNpc& Npc);
	// The running task program: which schedule, its retail number, every task in order with the
	// current one marked, and the two per-run values a task can have written (the fail-schedule
	// override and the tolerance).
	void RenderSchedule(FElysiumNpc& Npc);
	// Health, the weapon-capability split combat selection branches on, the active weapon, the
	// relationship rows against the player, and the last committed damage packet.
	void RenderCombat(FElysiumEntityWorld& World, FElysiumNpc& Npc);

	// --- Kept from the body-facing half -----------------------------------------------------------
	// The facial flex rig (12.3): one slider per flex controller on every live rigged body, and the
	// weights the rules and ramps derive from them.
	void RenderFacial();
	// The body sample both producers publish (CCC1): the player's mover and every live NPC motor
	// filling one `FElysiumLocomotionSample`. Side by side on purpose — the contract's whole claim is
	// that the cast's locomotion and the player's are the same record.
	void RenderLocomotion();

	// The over-the-head readout: state, running schedule, and a line to the committed enemy. Drawn
	// in the world because a fight is watched in the world; everything it says is also in the tabs.
	void DrawWorldOverlay() const;

	// --- Spawn form state -------------------------------------------------------------------------
	FString PendingStem;
	FString PendingTemplate;
	FString PendingWeapon;
	FString PendingClass = TEXT("npc_VHumanCombatant");
	FString StemFilter;
	FString TemplateFilter;
	int32 PendingReaction = 1;      // index into the D_* list below; Hate by default
	int32 PendingPriority = 99;
	int32 PendingPad = 0;           // index into the arena's pads, or the two synthetic entries
	int32 PendingCount = 1;
	int32 ReservePerAmmoType = 200;
	FString LastError;
	FString LastNotice;

	// Rescanned on first open and on demand: an export or a bake can land while the game is up.
	bool bCatalogsDirty = true;
	TArray<FString> Stems;
	TArray<FString> Templates;
	// Classname + printname, flattened for the picker. Weapons only — ammunition is stocked as a
	// reserve rather than handed over as items (`ElysiumArenaCast::ArmPlayerWithArsenal`).
	TArray<FString> Weapons;
	TArray<FString> WeaponLabels;
	// The seven playable clans and the body stem each resolves. Gathered once on first use rather
	// than with the other catalogs: it needs the rulebook, which loads lazily, and an empty list
	// simply retries next frame.
	TArray<ElysiumArenaCast::FClanOption> Clans;
	int32 PendingClan = 0;
	bool bPendingMale = true;

	// The overlay, and what it draws. On by default: it is the only reading of a fight that does not
	// require looking away from it.
	bool bDrawOverlay = true;
	bool bOverlayEnemyLines = true;
	// How far from the camera an overlay label is still drawn, in centimetres. A room-sized default;
	// a map full of characters would otherwise stack forty labels into one smear.
	float OverlayRangeCm = 4000.0f;

	// --- Per-tab view state -----------------------------------------------------------------------
	// Hide the condition identities that are clear. Off by default: a bit that is ABSENT is half of
	// every AI diagnosis, and a filtered list cannot show you the one that failed to gather.
	bool bConditionsSetOnly = false;
	FString FacialFilter;
	bool bFacialNonZeroOnly = true;
};

#endif // ENABLE_COG
