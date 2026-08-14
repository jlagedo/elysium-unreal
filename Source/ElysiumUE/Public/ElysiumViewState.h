#pragma once

#include "CoreMinimal.h"
#include "ElysiumAppState.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumInteraction.h"

class FElysiumDlgConversation;
struct FElysiumSignData;

// The presentation seam's value type and its rules (runtime-architecture.md section 11, roadmap 11.8).
//
// Plain C++, no UObject reflection, exactly like ElysiumAppState.h and ElysiumInputScope.h: the
// struct is data and the rules below are total functions over it, so the whole set is asserted with
// no world, no HUD, no viewport and no RHI — `Elysium.Substrate.ViewState`.
//
// UElysiumPresentationSubsystem builds one of these per frame and is its only writer. AElysiumHUD,
// the CommonUI screens and the dialogue box read it and nothing else: no widget resolves the map
// actor, walks it to FElysiumEntityWorld, or polls the substrate for what to draw.

// ============================================================================================
// FElysiumVitals — the player's meters.
//
// Health is the player entity's own `health` field (VtMB's m_iHealth); blood/humanity/masquerade
// are the combat character's counters (11.4). 8.9 draws them; nothing else derives from them here.
// `bValid` is false whenever there is no player entity — a menu backdrop and a headless logic world
// both run without one, so "no player" is a state, not an error.
// ============================================================================================
struct FElysiumVitals
{
	bool  bValid = false;

	int32 Health = 0;
	int32 MaxHealth = 0;
	int32 BloodPool = 0;
	int32 MaxBloodPool = 0;
	int32 Humanity = 0;
	int32 Masquerade = 0;

	bool operator==(const FElysiumVitals& Other) const
	{
		return bValid == Other.bValid
			&& Health == Other.Health && MaxHealth == Other.MaxHealth
			&& BloodPool == Other.BloodPool && MaxBloodPool == Other.MaxBloodPool
			&& Humanity == Other.Humanity
			&& Masquerade == Other.Masquerade;
	}
	bool operator!=(const FElysiumVitals& Other) const { return !(*this == Other); }
};

// The currently fed-on target, or the just-released target while ordinary +use focus still owns it.
// Gameplay remains on FElysiumFeedState; this is the immutable screen projection sampled by the
// publisher. bPaired suppresses only the reticle — the capture keeps the rest of the HUD visible.
struct FElysiumFeedView
{
	bool bVisible = false;
	bool bPaired = false;
	FElysiumEntityHandle Target;
	int32 BloodPool = 0;
	int32 MaxBloodPool = 0;
	uint8 Phase = 0;
};

// ============================================================================================
// FElysiumDialogueView — one conversation turn, snapshotted.
//
// The box is built from the strings here, not from the conversation: `Conversation` is identity
// only (what the reconcile compares against), and `Revision` is what changes when the turn does.
// Speaker resolution needs the entity world, which is precisely why it happens in the publisher.
// ============================================================================================
struct FElysiumDialogueView
{
	// Valid until the next publish and never stored — the map epoch it points into ends at travel.
	const FElysiumDlgConversation* Conversation = nullptr;
	uint32 Revision = 0;
	FElysiumEntityHandle Owner;

	FString Speaker;                 // the owning NPC's targetname
	FString Line;                    // the NPC subtitle for this turn (DisplayText, directions stripped)
	TArray<FString> Choices;         // the visible PC choices, in author order
	TArray<int32> ChoiceIds;          // stable .dlg row ids, parallel to Choices
	bool bTerminal = false;          // authored terminal, or automatic voice-failure Continue fallback
	bool bAwaitingAutomatic = false; // spoken line is up; the synthetic control row remains hidden

	bool IsOpen() const { return Conversation != nullptr; }
};

// One immutable loot projection. Slot is the authoritative compact inventory position submitted
// back by the UI; labels are resolved here so widgets never read item data or entity state.
struct FElysiumLootEntryView
{
	int32 Slot = INDEX_NONE;
	FString Classname;
	FString Label;
	int32 Quantity = 1;
};

struct FElysiumLootView
{
	FElysiumEntityHandle Owner;
	uint32 Revision = 0;
	FString Title;
	TArray<FElysiumLootEntryView> ContainerItems;
	TArray<FElysiumLootEntryView> PlayerItems;

	bool IsOpen() const { return Owner.IsSet(); }
};

struct FElysiumTerminalActionView
{
	FString Id;
	FString Label;
	FString Command;
	bool bEnabled = true;
	FString Explanation;
};

// Immutable projection of one authoritative terminal session. The editable line remains local UI
// state; these rows and actions are only what gameplay has already authorized. World presentation
// resolves the owning entity's physical screen mesh separately, so gameplay never carries viewport
// geometry or a material/render-target handle.
struct FElysiumTerminalView
{
	FElysiumEntityHandle Owner;
	uint32 SessionSerial = 0;
	uint32 Revision = 0;
	FString ScreenSaverLabel;
	int32 Columns = 36;
	int32 Rows = 24;
	TArray<FString> ScreenRows;
	int32 CursorRow = 0;
	int32 CursorColumn = 0;
	uint8 InputMode = 0;
	int32 MaxInput = 16;
	bool bAcceptsDirectoryKeys = true;
	TArray<FElysiumTerminalActionView> Actions;

	bool IsOpen() const { return Owner.IsSet(); }
};

// ============================================================================================
// FElysiumViewState — everything on screen, rebuilt each frame in TG_PostUpdateWork.
// ============================================================================================
struct FElysiumViewState
{
	EElysiumAppState App = EElysiumAppState::Boot;

	// The one gating rule's answer (ShowsPlayerSurface below). False means every field after this
	// one is at its default: the publisher does not fill a surface it is not showing, so a reader
	// cannot draw one by forgetting to check.
	bool bPlayerSurface = false;

	// A scripted camera currently owns the player's view. This suppresses the heads-up layer only:
	// fades, dialogue and future cutscene subtitles remain part of the published player surface.
	// Derived from actual camera ownership, not from logic_choreographed_scene activity — ambient
	// NPC choreography is allowed to run during ordinary play.
	bool bCinematic = false;

	// --- Interaction (P4.4) -----------------------------------------------------------------
	// The complete +use presentation projection. A retained fade-out remains visible but is never
	// actionable, so presentation cannot imply an entity which the same frame would use.
	FElysiumInteractionView Interaction;

	// --- Screen fade (P4.5 env_fade) --------------------------------------------------------
	// rgb = the authored fade colour, a = the current 0..1 alpha. Alpha 0 means no fade is up.
	FLinearColor Fade = FLinearColor(0, 0, 0, 0);

	// --- Sign panel (P4.10 game_sign) -------------------------------------------------------
	// The parsed panel, valid until the next publish and never stored. `SignAlpha` is the fade_in
	// ramp already resolved against the game clock, so nothing downstream needs the clock.
	// `bSignHidesHUD` is the panel's own HideHUD flag lifted out, because FElysiumSignData is a
	// private type and the rules below have to stay readable from the public header.
	const FElysiumSignData* Sign = nullptr;
	FElysiumEntityHandle SignOwner;
	float SignAlpha = 1.0f;
	bool bSignHidesHUD = false;
	bool bSignDismissible = false;

	// --- Conversation (9.1 / B4) ------------------------------------------------------------
	FElysiumDialogueView Dialogue;

	// --- Loot container (9.8) --------------------------------------------------------------
	// An explicit +use session. The CommonUI screen submits Take/Give/Close intents only; slot
	// validation and entity transfer remain on the substrate.
	FElysiumLootView Loot;

	// --- Computer terminal (13.4) ---------------------------------------------------------
	FElysiumTerminalView Terminal;

	// --- Meters (8.9 draws them) ------------------------------------------------------------
	FElysiumVitals Vitals;
	FElysiumFeedView Feed;
};

namespace ElysiumView
{
	// **The one gating rule (S8).** The player-facing surface — reticle, sign panel, dialogue box,
	// meters, and the env_fade quad with them — is shown while a session is actually being played
	// and at no other time. Two writers can raise a screen over the world, so both are asked:
	// `UElysiumGameFlowSubsystem`'s app state covers Paused/GameOver/FrontEnd/Loading, and the
	// menu's own open flag covers the hand `elysium.menu` verb, which raises a screen without
	// moving the state.
	//
	// This replaces the IsMenuUp() checks that used to sit in every draw path, and it is a rule
	// about *publishing* rather than about drawing: a conversation already on screen when the pause
	// menu opens is republished as closed, so the box comes down instead of drawing through.
	inline bool ShowsPlayerSurface(EElysiumAppState App, bool bMenuOpen)
	{
		return App == EElysiumAppState::Playing && !bMenuOpen;
	}

	// What the crosshair is this frame.
	enum class EReticle : uint8
	{
		None,      // no surface at all, or a panel with HideHUD covering the game
		Cross,     // the plain aim cross
		UseIcon,   // the +use context cursor: the ring frame around Interaction.Icon's atlas cell
	};

	inline EReticle ResolveReticle(const FElysiumViewState& V)
	{
		if (!V.bPlayerSurface || V.bCinematic || V.bSignHidesHUD || V.Feed.bPaired)
		{
			return EReticle::None;
		}
		return V.Interaction.bVisible && V.Interaction.Icon > 0
			? EReticle::UseIcon : EReticle::Cross;
	}

	// What the retained dialogue box has to do about this frame's state.
	enum class EDialogueAction : uint8
	{
		None,       // the box already shows this turn (or there is nothing to show)
		Rebuild,    // a new conversation, or the same one on a new turn
		Teardown,   // the conversation is gone (ended, or withheld behind a screen)
	};

	// `ShownConv`/`ShownRev` are what the box currently has up — a null ShownConv means no box is on
	// screen. The holder keeps the pointer for identity only and never dereferences it, which is what
	// lets it outlive a publish; the comparison is identity + revision, never content, because two
	// turns can read the same and are still different turns.
	inline EDialogueAction ReconcileDialogue(const FElysiumDlgConversation* ShownConv, uint32 ShownRev,
		const FElysiumDialogueView& Next)
	{
		if (!Next.IsOpen())
		{
			return ShownConv ? EDialogueAction::Teardown : EDialogueAction::None;
		}
		if (ShownConv == Next.Conversation && ShownRev == Next.Revision)
		{
			return EDialogueAction::None;
		}
		return EDialogueAction::Rebuild;
	}
}
