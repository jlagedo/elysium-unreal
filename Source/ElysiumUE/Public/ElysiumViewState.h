#pragma once

#include "CoreMinimal.h"
#include "ElysiumAppState.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumInteraction.h"
#include "ElysiumInventorySections.h"

class FElysiumDlgConversation;
struct FElysiumSignData;

// The presentation seam's value type and its rules (`docs/architecture/runtime-architecture.md` §11).
//
// Plain C++, no UObject reflection, exactly like ElysiumAppState.h and ElysiumInputScope.h: the
// struct is data and the rules below are total functions over it, so the whole set is asserted with
// no world, no HUD, no viewport and no RHI — `Elysium.Substrate.ViewState`.
//
// UElysiumPresentationSubsystem builds one of these per frame and is its only writer. AElysiumHUD,
// the CommonUI screens and the dialogue box read it and nothing else: no widget resolves the map
// actor, walks it to FElysiumEntityWorld, or polls the substrate for what to draw.

// FElysiumVitals — the player's meters.
//
// Health is the player entity's own `health` field (VtMB's m_iHealth); blood/humanity/masquerade
// are the combat character's counters. The HUD draws them; nothing else derives from them here.
// `bValid` is false whenever there is no player entity — a menu backdrop and a headless logic world
// both run without one, so "no player" is a state, not an error.
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

// FElysiumDialogueView — one conversation turn, snapshotted.
//
// The box is built from the strings here, not from the conversation: `Conversation` is identity
// only (what the reconcile compares against), and `Revision` is what changes when the turn does.
// Speaker resolution needs the entity world, which is precisely why it happens in the publisher.
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

// FElysiumStealthView — how exposed the player is, and who is looking.
//
// `bSneaking` is the body's own settled posture. Concealment and the observer are gameplay's:
// gameplay owns every range, cone, trace and enemy-selection decision, and this view only carries
// the answer it committed (`docs/vtmb/stealth.md` -> "HUD observability is not authority"). Each
// half states its own validity, so an unmeasured gauge renders as unmeasured rather than as a
// confident zero.
enum class EElysiumDetection : uint8
{
	Unaware,
	Searching,
	Detected,
};

struct FElysiumStealthView
{
	// The duck stance, including its ramp — one fact with one producer, the locomotion sample.
	bool bSneaking = false;

	// The concealment gauge's step, 0 (fully lit) to 4 (fully dark), mirroring the five exported
	// `lightgauge` frames. False validity means no light sample has a producer yet.
	bool bConcealmentValid = false;
	int32 ConcealmentStep = 0;

	// The nearest eligible hostile observer. Absent is an ordinary state and clears the readout;
	// it is never a failure and never a reason to invent a distance.
	bool bObserverValid = false;
	float ObserverDistanceCm = 0.0f;
	EElysiumDetection Detection = EElysiumDetection::Unaware;

	bool operator==(const FElysiumStealthView& Other) const
	{
		return bSneaking == Other.bSneaking
			&& bConcealmentValid == Other.bConcealmentValid && ConcealmentStep == Other.ConcealmentStep
			&& bObserverValid == Other.bObserverValid
			&& ObserverDistanceCm == Other.ObserverDistanceCm && Detection == Other.Detection;
	}
	bool operator!=(const FElysiumStealthView& Other) const { return !(*this == Other); }
};

// FElysiumEquipmentView — the carried weapons and which one is in hand.
//
// The families are mirrored as a plain enum so this header keeps its no-UObject rule; the HUD model
// maps them onto its own Blueprint-readable `EElysiumWeaponClass`. Rows arrive in the order the
// item records author (`bucket`, then `bucket_position`), which is the order the selector cycles.
enum class EElysiumViewWeaponFamily : uint8
{
	None,
	Unarmed,
	Melee,
	Firearm,
	Thrown,
};

struct FElysiumInventoryEntryView
{
	// The entity classname, which is what a selection intent names back to the substrate. The
	// compact inventory position is deliberately absent: it shifts whenever any item is removed,
	// so a row that survives one frame of looting would otherwise point at a different item.
	FString Classname;
	// `printname`, or the classname when the record authors none.
	FString Label;
	EElysiumViewWeaponFamily Family = EElysiumViewWeaponFamily::None;
	// A stack's count. 0 for a non-stackable item, which is one of a thing rather than a stack of
	// one, so the row shows no quantity at all.
	int32 Quantity = 0;
	// The loaded magazine and the owner's reserve for this weapon's ammunition type. Both stay 0
	// for a weapon that carries no magazine, which `bHasMagazine` distinguishes from "empty".
	int32 AmmoCurrent = 0;
	int32 AmmoReserve = 0;
	bool bHasMagazine = false;

	bool operator==(const FElysiumInventoryEntryView& Other) const
	{
		return Classname == Other.Classname && Label == Other.Label && Family == Other.Family
			&& Quantity == Other.Quantity
			&& AmmoCurrent == Other.AmmoCurrent && AmmoReserve == Other.AmmoReserve
			&& bHasMagazine == Other.bHasMagazine;
	}
	bool operator!=(const FElysiumInventoryEntryView& Other) const { return !(*this == Other); }
};

struct FElysiumEquipmentView
{
	// False whenever there is no player entity or no item catalogue is installed. A player carrying
	// nothing is a valid empty list, not an invalid view.
	bool bValid = false;

	// The weapon in hand. Persistent, and independent of whichever section is being browsed.
	bool bEquippedValid = false;
	FElysiumInventoryEntryView Equipped;

	// What the player is wearing — `items.txt` authors `IsWorn` per item type.
	bool bWornValid = false;
	FElysiumInventoryEntryView Worn;

	// The category being browsed and its rows, in the order the records author.
	EElysiumInvSection Section = EElysiumInvSection::None;
	TArray<FElysiumInventoryEntryView> Entries;
	// Index into `Entries`, or INDEX_NONE while the section's cursor names nothing carried.
	int32 SelectedIndex = INDEX_NONE;

	// The cycle peek's opacity, 0..1. The publisher raises it to 1 when the selection changes
	// and lets it fall, so the selector shows itself on a switch and fades without the HUD owning
	// a timer of its own. Presentation derives this; no gameplay state carries it.
	float PeekAlpha = 0.0f;

	const FElysiumInventoryEntryView* Selected() const
	{
		return Entries.IsValidIndex(SelectedIndex) ? &Entries[SelectedIndex] : nullptr;
	}

	bool operator==(const FElysiumEquipmentView& Other) const
	{
		return bValid == Other.bValid && bEquippedValid == Other.bEquippedValid
			&& Equipped == Other.Equipped && bWornValid == Other.bWornValid && Worn == Other.Worn
			&& Section == Other.Section && SelectedIndex == Other.SelectedIndex
			&& PeekAlpha == Other.PeekAlpha && Entries == Other.Entries;
	}
	bool operator!=(const FElysiumEquipmentView& Other) const { return !(*this == Other); }
};

// The camera's contribution to the frame — its resolved draw policy, projected once.
struct FElysiumCameraView
{
	// A camera manager published a sample for this frame. False on a backdrop or during character
	// generation, where the view target is an `ACameraActor` and no player rig runs.
	bool bValid = false;

	bool bThirdPerson = false;

	// A scripted shot or a map camera track owns the view. This is **presentation context, not a HUD
	// gate**: it suppresses toasts and the interaction prompt, because those describe an interaction
	// the player is not currently having. Retail's ordinary mode toggle and a Worldcraft
	// `camera_track` both leave the HUD up (`docs/vtmb/camera-view-modes.md` §5).
	bool bScriptedCameraOwnsView = false;

	// Named-`SetCamera`-shot policy, ANDed from exactly two contributors: the deciding shot on the
	// legacy stack (through the draw policy's `bShowHud`) and the dialogue session's own selected
	// source shot (through `FElysiumEntityWorld::DialogueCameraHidesHud`). A `camera_track` authors no
	// `ShowHud` key and contributes to neither. The camera **service** is not a third contributor —
	// only the dialogue session's stored request is read, and only through that predicate.
	bool bShowHud = true;

	// **The first-person hands/weapon submission gate.** Its consumer suppresses
	// submission only: never destroy either component, never clear a model, never reset a sequence or
	// cycle. The frame the third-person weight reaches exactly zero resumes the existing visual state
	// rather than rebuilding it.
	bool bDrawViewmodel = false;

	// The local player's own body and the world weapon it carries. The body's band is the only soft
	// hand-off recovered; the world weapon is boolean in both directions.
	bool bDrawPlayerBody = false;
	float PlayerBodyAlpha = 0.0f;
	bool bDrawWorldWeapon = false;

	EElysiumReticlePath ReticlePath = EElysiumReticlePath::FirstPerson;
};

// FElysiumViewState — everything on screen, rebuilt each frame in TG_PostUpdateWork.
struct FElysiumViewState
{
	EElysiumAppState App = EElysiumAppState::Boot;

	// The one gating rule's answer (ShowsPlayerSurface below). False means every field after this
	// one is at its default: the publisher does not fill a surface it is not showing, so a reader
	// cannot draw one by forgetting to check.
	bool bPlayerSurface = false;

	// The camera's resolved draw policy, projected once.
	// The only camera facts on this state. Widgets read these and never query the pawn or the camera
	// manager (`docs/architecture/camera-architecture.md` → Input, settings and presentation).
	FElysiumCameraView Camera;

	// Interaction.
	// The complete +use presentation projection. A retained fade-out remains visible but is never
	// actionable, so presentation cannot imply an entity which the same frame would use.
	FElysiumInteractionView Interaction;

	// Screen fade (`env_fade`).
	// rgb = the authored fade colour, a = the current 0..1 alpha. Alpha 0 means no fade is up.
	FLinearColor Fade = FLinearColor(0, 0, 0, 0);

	// Sign panel (`game_sign`).
	// The parsed panel, valid until the next publish and never stored. `SignAlpha` is the fade_in
	// ramp already resolved against the game clock, so nothing downstream needs the clock.
	// `bSignHidesHUD` is the panel's own HideHUD flag lifted out, because FElysiumSignData is a
	// private type and the rules below have to stay readable from the public header.
	const FElysiumSignData* Sign = nullptr;
	FElysiumEntityHandle SignOwner;
	float SignAlpha = 1.0f;
	bool bSignHidesHUD = false;
	bool bSignDismissible = false;

	// Conversation.
	FElysiumDialogueView Dialogue;

	// Loot container.
	// An explicit +use session. The CommonUI screen submits Take/Give/Close intents only; slot
	// validation and entity transfer remain on the substrate.
	FElysiumLootView Loot;

	// Computer terminal.
	FElysiumTerminalView Terminal;

	// Stealth.
	FElysiumStealthView Stealth;

	// Equipment and the weapon selector.
	// The carried weapons and the one in hand. The selector renders this and submits `invnext` /
	// `invprev` / `lastinv` / `slotN` back through the command bus; it never switches a weapon
	// itself.
	FElysiumEquipmentView Equipment;

	// Meters.
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
	// Every draw path reads this rather than calling `IsMenuUp()`, and it is a rule about
	// *publishing* rather than about drawing: a conversation already on screen when the pause
	// menu opens is republished as closed, so the box comes down instead of drawing through.
	inline bool ShowsPlayerSurface(EElysiumAppState App, bool bMenuOpen)
	{
		return App == EElysiumAppState::Playing && !bMenuOpen;
	}

	// What the crosshair is this frame.
	enum class EReticle : uint8
	{
		None,        // no surface at all, or a panel with HideHUD covering the game
		Cross,       // the plain aim cross
		UseIcon,     // the +use context cursor: the ring frame around Interaction.Icon's atlas cell
		ThirdPerson, // the plain white reticle drawn at the crosshair rect in third person
	};

	// **The mode toggle selects a crosshair path; it does not hide the HUD** (`0x1009b9e0`,
	// `docs/vtmb/camera-view-modes.md` §5). Third person draws the plain reticle; first person runs
	// the full use-icon / arrow cursor path.
	inline EReticle ResolveReticle(const FElysiumViewState& V)
	{
		if (!V.bPlayerSurface || !V.Camera.bShowHud || V.bSignHidesHUD || V.Feed.bPaired)
		{
			return EReticle::None;
		}
		if (V.Camera.ReticlePath == EElysiumReticlePath::ThirdPerson)
		{
			return EReticle::ThirdPerson;
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
