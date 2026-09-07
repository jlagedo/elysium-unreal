#pragma once

#include "CoreMinimal.h"
#include "ElysiumAppState.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumDlg.h"
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

// The victim blood meter — `client.dll` `CFeedBar`, projected.
//
// Gameplay remains on FElysiumFeedState; this is the immutable screen projection sampled by the
// publisher. bPaired suppresses only the reticle — the capture keeps the rest of the HUD visible.
//
// The panel is NOT owned by "who the player is paired with" or "what the player is looking at".
// Retail owns it with two things and only two: the last VALUE the server sent, and a hide deadline
// refreshed by that send. `ElysiumFeedBar::Update` below is the whole rule.
struct FElysiumFeedView
{
	bool bVisible = false;
	bool bPaired = false;
	FElysiumEntityHandle Target;
	int32 BloodPool = 0;
	int32 MaxBloodPool = 0;
	uint8 Phase = 0;

	// The DRAWN fraction, 0..1 — retail's integer percent at `CFeedBar+0x1a8` divided by 100, less
	// the pre-pulse anticipation `vfunc98` subtracts. The widget draws this and derives nothing.
	float Percent = 0.0f;

	// `CFeedBar+0x1b4`: the absolute substrate second after which the panel comes down once the
	// player is no longer holding the pair. Zero means "not held up".
	double HideDeadline = 0.0;

	// `CFeedBar+0x1b0`: when the value last changed, i.e. when the last `FeedBar` message would
	// have been sent. Seeds both the deadline and the anticipation ramp.
	double LastChangeSeconds = 0.0;
};

// The `CFeedBar` value and lifetime contract, as one total function over the view.
//
// RECOVERED (`docs/vtmb/feeding.md` § "CFeedBar — the victim blood meter"):
//   * The value arrives as the one-byte `FeedBar` usermsg, sent by `CBasePlayer::UpdateClientActionState`
//     `vampire.dll` `0x101755d0` ONLY when it differs from the cached `m_iClientFeedBloodPool`
//     (`+0x1a8c`), from the victim's stat slot 12 `BloodPool`, negatives masked to zero.
//   * `CFeedBar::vfunc114` `client.dll` `0x100503d0` is `(int iValue, bool bShow)`. It seeds the
//     denominator with the literal 15, overrides it with the player's replicated
//     `m_iClientFeedMaxBloodPool` (`+0x14cc`) when non-zero, then:
//       `iValue <= 0`  -> percent 0 and SetVisible(0)      (`JLE 0x100504b3`)
//       `iValue >= 15` -> percent 0 and SetVisible(0)      (`CMP EDI,0xf / JGE 0x100504d4`)
//       otherwise      -> percent = iValue*100/max, and if bShow: SetVisible(1) and
//                         deadline = curtime + 3.0 (`_DAT_10227ee0`, the same rdata double
//                         `SimpleSpline` `0x100fdb30` reads as its 3).
//   * `CFeedBar::vfunc98` `0x10050560` (paint) hides the panel when the player is not feeding
//     (`+0x14d4` clear) and curtime has passed that deadline.
// So retail NEVER draws an empty bar, and its post-feed persistence is a three-second timer, not
// a focus test.
namespace ElysiumFeedBar
{
	// `_DAT_10227ee0`.
	inline constexpr double HoldSeconds = 3.0;
	// The drawn window, closed at both ends by `vfunc114`. The upper bound is the same literal 15
	// the client hardcodes as the default denominator, and it is retail's own quirk: a victim
	// standing at exactly its 15-point stat ceiling shows no meter at all.
	inline constexpr int32 MinDrawnValue = 1;
	inline constexpr int32 HiddenAtOrAbove = 15;

	// One frame's worth of what the server would have had to say. `bHasSource` false is "no message
	// this frame", which is the ordinary state and is NOT the same as "hide".
	struct FUpdate
	{
		bool bHasSource = false;
		FElysiumEntityHandle Source;
		int32 BloodPool = 0;
		// The victim's authored pool — the value `CBaseCombatCharacter::EnterGrappleState`
		// `0x10329760` replicates into `m_iClientFeedMaxBloodPool`.
		int32 AuthoredMax = 0;
		// `+0x14d4`, the replicated feeding flag `vfunc98` reads as its keep-alive. This runtime has
		// no separate replicated flag: the pair exists exactly while the feed is held, so the pair
		// is the flag.
		bool bPaired = false;
		// `+0x14d0`, the replicated pulse interval, and whether it is running.
		bool bPulsing = false;
		float PulseInterval = 0.0f;
		uint8 Phase = 0;
	};

	inline FElysiumFeedView Update(const FElysiumFeedView& Previous, const FUpdate& In, double Now)
	{
		// Everything the last message left standing carries forward. A frame with no message is not
		// an instruction to hide — the deadline below is.
		FElysiumFeedView Out = Previous;
		Out.bPaired = In.bPaired;
		Out.Phase = In.Phase;

		if (In.bHasSource)
		{
			const int32 Value = FMath::Max(0, In.BloodPool);
			// The denominator is sticky for the life of one target, because nothing in retail ever
			// clears `m_iClientFeedMaxBloodPool` — the meter held through the release tail keeps the
			// denominator the grapple opened with.
			const int32 StickyMax = (Previous.Target == In.Source) ? Previous.MaxBloodPool : 0;
			const int32 Max = FMath::Max(1, FMath::Max(In.AuthoredMax, StickyMax));
			// The change gate is the usermsg's own. Retail caches only the value, so re-feeding a
			// second victim standing at the first one's number sends nothing; this keys on the
			// target as well, which is a named divergence from a retail defect, not from a rule.
			const bool bChanged = Previous.Target != In.Source || Previous.BloodPool != Value;

			Out.Target = In.Source;
			Out.BloodPool = Value;
			Out.MaxBloodPool = Max;

			if (Value < MinDrawnValue || Value >= HiddenAtOrAbove)
			{
				// `vfunc114`'s two hide arms. Applied on every observation rather than only on a
				// change: the first out-of-window message hides it and no later one could re-show
				// it, so the two are observationally identical and this one cannot leave an empty
				// bar standing.
				Out.bVisible = false;
				Out.HideDeadline = 0.0;
			}
			else if (bChanged)
			{
				// bShow. The one recovered `bShow == false` call is the constructor's
				// `vfunc114(15, false)`, which is already hidden by the upper arm, so every call
				// that can raise the panel raises it.
				Out.bVisible = true;
				Out.HideDeadline = Now + HoldSeconds;
			}
			if (bChanged)
			{
				Out.LastChangeSeconds = Now;
			}
		}

		// `vfunc98`'s paint-time hide. It only ever hides: a panel the value gate took down stays
		// down until a changed value raises it again.
		if (Out.bVisible && !Out.bPaired && Now >= Out.HideDeadline)
		{
			Out.bVisible = false;
		}

		if (!Out.bVisible)
		{
			Out.Percent = 0.0f;
			return Out;
		}

		const float Denominator = float(FMath::Max(1, Out.MaxBloodPool));
		float Percent = float(Out.BloodPool) / Denominator;
		if (In.bPulsing && In.PulseInterval > UE_KINDA_SMALL_NUMBER)
		{
			// The pre-pulse anticipation, recovered from `vfunc98` `0x100507a5`: while the feed is
			// pulsing it subtracts `(100/max) * clamp((curtime - lastChange) / pulseInterval, 0, 1)`
			// from the drawn percent, so the bar visibly slides one blood point's worth down across
			// each interval instead of stepping. Expressed here as a fraction rather than retail's
			// truncated integer percent.
			const float T = FMath::Clamp(
				float(Now - Out.LastChangeSeconds) / In.PulseInterval, 0.0f, 1.0f);
			Percent -= T / Denominator;
		}
		Out.Percent = FMath::Clamp(Percent, 0.0f, 1.0f);
		return Out;
	}
}

// FElysiumDialogueView — one conversation turn, snapshotted.
//
// The box is built from the strings here, not from the conversation: `Conversation` is identity
// only (what the reconcile compares against), and `Revision` is what changes when the turn does.
// Speaker resolution needs the entity world, which is precisely why it happens in the publisher.
// One response row, already resolved. The gate was evaluated once in the substrate
// (`FElysiumDlgDependency::Explain`) and once more never: the UI reads `bEnabled` and the
// pre-formatted `Label` and evaluates nothing (M-REQ / M-DISABLED).
struct FElysiumDialogueChoiceView
{
	FString Text;               // the sentence (DisplayText, stage directions stripped)
	FString Label;              // "[ PERSUASION 4/7 ]"; empty when the row has no labellable skill front
	bool bEnabled = true;       // false = M-DISABLED: shown dimmed, unpickable, non-focusable
	EElysiumDlgTraitClass Kind = EElysiumDlgTraitClass::Unknown;
	int32 Have = 0;             // the player's rating for the labelled trait
	int32 Required = 0;         // the authored threshold
	int32 BloodCost = 0;        // a discipline's blood price, 0 otherwise
	int32 LineId = INDEX_NONE;  // the `.dlg` row id — the durable action identity across a refresh
};

struct FElysiumDialogueView
{
	// Valid until the next publish and never stored — the map epoch it points into ends at travel.
	// Diagnostic/content identity only: it is NOT the reconcile key, because a closed conversation's
	// allocation can be handed straight back to the one that replaces it in the same frame.
	const FElysiumDlgConversation* Conversation = nullptr;
	uint32 Revision = 0;
	// `FElysiumEntityWorld::GetOpenDialogSerial()` — monotonic per OpenDialog, 0 when nothing is
	// open. This is the conversation's identity for reconcile: unlike the address it is never
	// reused, and unlike `Revision` (which restarts at 1 for every conversation) it never repeats.
	uint32 DialogSerial = 0;
	FElysiumEntityHandle Owner;

	FString Speaker;                 // the owning NPC's targetname
	FString Line;                    // the NPC subtitle for this turn (DisplayText, directions stripped)
	// The visible PC rows in AUTHOR order, enabled and disabled alike, numbered 1..N by position so
	// the numbering is stable whether or not the player has the skill (M-DISABLED).
	TArray<FElysiumDialogueChoiceView> Choices;
	TArray<int32> ChoiceIds;          // stable .dlg row ids, parallel to Choices
	bool bTerminal = false;          // authored terminal, or automatic voice-failure Continue fallback
	bool bAwaitingAutomatic = false; // spoken line is up; the synthetic control row remains hidden
	// M-REVEAL / M-SKIP. The band is published with the line rather than withheld behind
	// `ShowPlayerChoices`, so the UI needs to know whether the voice is still running: it draws the
	// skip hint and routes Space to the hurry verb instead of to Continue.
	bool bNpcSpeaking = false;
	bool bCanSkip = false;
	// The band gated every row out with no automatic continuation: the subtitle has already been
	// replaced with `NoValidReplyText()` and the turn carries one Continue (retail `0x100e82d0`).
	bool bNoValidReply = false;

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
	// The authority's cell screen, row-major `Rows * Columns`: ASCII in the low 7 bits, `0x80` the
	// style bit (`docs/vtmb/computer-terminals.md` §8.3). `ScreenRows` is the same grid as text,
	// one padded string per row, for readers that do not draw cells.
	TArray<uint16> Cells;
	TArray<FString> ScreenRows;
	int32 CursorRow = 0;
	int32 CursorColumn = 0;
	// The screen's right margin (`+0xe84`) and current style byte (`+0xe74`). Both are client state
	// in retail, and both are what the LOCAL line editor composes with: `0x100c7090` refuses to draw
	// the typed line at all unless `cursorColumn + length < columns - rightMargin`, and its put-char
	// stamps each character with the current style. The authority owns the buffer here, so it
	// publishes them rather than letting presentation guess.
	int32 RightMargin = 0;
	uint8 CellStyle = 0x80;
	// `m_nColorScheme` (`DT_BaseTerminal` `+0x818`, `FUN_102173e0`), clamped to `[0, 3]` — the range
	// the client's rasterizer reads its four palette records at `0x10233378` with (`client+0xf08`,
	// §8.3). Glass state, so an idle monitor carries it too.
	int32 ColorScheme = 0;
	// EElysiumTerminalInputMode: 0 line, 1 password, 2 acknowledge, 3 raw character.
	uint8 InputMode = 0;
	// The client line editor's activation (`docs/vtmb/computer-terminals.md` §8.1.1, TERM20).
	// Retail's editor is opened by entity message 3 (`FUN_100c82e0`, sent by `FUN_10219120` and so
	// by all three mode senders) and closed by every message that zeroes `+0xe88` — set cursor
	// (`FUN_100c7ec0`), print (`FUN_100c7fb0`), clear (`FUN_100c7f50`) and the scroll
	// (`FUN_100c8210`). Closed, `0x100c7090` returns 1 at its third test: the key is eaten, nothing
	// is inserted and nothing is drawn. The input MODE says how a key is answered; this says
	// whether it is answered at all, and the two are independent.
	bool bLineEditActive = false;
	// `+0xe8c` and the row the edit opened on. The draft is composed from HERE, not from the live
	// cursor: `0x100c7090`'s re-render restores the saved row, sets the cursor to `+0xe8c` and
	// walks the whole line back through put-char, so the origin is the only anchor and its fit
	// guard is `strlen(line) + editOrigin < columns - rightMargin`.
	int32 EditOriginColumn = 0;
	int32 EditOriginRow = 0;
	// Bumped on every activation. It has no retail field: retail's type-3 handler clears the local
	// line `+0xed8` itself, and the authority here cannot reach into the widget that holds it, so
	// a changed epoch is that clear. Serial and mode changes clear the draft too; this covers the
	// case they miss — the same session redrawing the same mode's prompt.
	uint32 EditEpoch = 0;
	// `m_nMaxInput`, zeroed by `CBaseTerminal::Spawn` `0x10217880` and raised by no shipped body.
	// **Zero or less means UNLIMITED**, exactly as the client's two input paths read it
	// (`if (m_nMaxInput != 0 && strlen(line) >= m_nMaxInput) return;`, `0x100c7090` and
	// `FUN_100c6d50`). The router's own 16-byte cap is a separate, authority-side truncation.
	int32 MaxInput = 0;
	bool bDigitsOnly = false;
	// `m_bAllowDirKeys` (`+0x824`). Zeroed at `CBaseTerminal::Spawn` and written NOWHERE else in
	// vampire.dll, so arrows / Home / End move no cursor on any retail terminal or keypad.
	bool bAcceptsDirectoryKeys = false;
	// The `InfoCtrl` HUD hint (§8.4): 0 hidden, 3 "Press CTRL-C to use the Hacking feat",
	// 5 "Making hack attempt at skill <value>", 6 "Skill too low ... difficulty <value>". The HUD
	// draws it bottom-centre in the project's own type.
	int32 HudHintType = 0;
	int32 HudHintValue = 0;
	// The resolved line the HUD draws, assembled by the authority through the same
	// `Hacking_Strings` table the screen draws with (type 3 -> index 0; 4 -> 37 + value;
	// 5 -> 40 + value; 6 -> 38 + value; 0/2 -> empty). Empty means the hint is hidden.
	FString HudHintText;
	TArray<FElysiumTerminalActionView> Actions;

	// A LIVE session. An idle projection carries the same owner and the same grid with serial 0
	// (`FElysiumTerminal::BuildIdleView`), so the owner alone cannot answer this — a machine
	// nobody is standing at would open the CommonUI screen and block saving.
	bool IsOpen() const { return Owner.IsSet() && SessionSerial != 0; }
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
	// The held session, if there is one.
	FElysiumTerminalView Terminal;
	// Every OTHER terminal on the map that has a body: the grid its screensaver think is writing,
	// with session serial 0. A monitor's glass is world state and outlives every session, so the
	// world-lifetime projections read this rather than the session view.
	TArray<FElysiumTerminalView> IdleTerminals;

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

	// `ShownSerial`/`ShownRev` are what the box currently has up — a zero ShownSerial means no box
	// is on screen. The comparison is identity + revision, never content, because two turns can read
	// the same and are still different turns.
	//
	// The identity is the world's open-dialog serial, NOT the conversation pointer. A one-turn
	// conversation that closes and opens another in the same frame can land its successor on the
	// freed allocation, and every conversation's revision restarts at 1 — so (address, revision)
	// could repeat and the box would keep the dead band up with no rebuild. The serial is
	// monotonic per `OpenDialog` and cannot.
	//
	// `bShownSpeaking` extends the key rather than the revision: the voice ending is not a new turn
	// (nothing in the band changes) but it does change what the box draws — the "Space: skip" hint
	// comes down and Space becomes Continue. It flips at most once per turn, so this costs one extra
	// in-place rebuild per line and never a per-frame one.
	inline EDialogueAction ReconcileDialogue(uint32 ShownSerial, uint32 ShownRev,
		const FElysiumDialogueView& Next, bool bShownSpeaking = false)
	{
		if (!Next.IsOpen())
		{
			return ShownSerial != 0 ? EDialogueAction::Teardown : EDialogueAction::None;
		}
		if (ShownSerial == Next.DialogSerial && ShownRev == Next.Revision
			&& bShownSpeaking == Next.bNpcSpeaking)
		{
			return EDialogueAction::None;
		}
		return EDialogueAction::Rebuild;
	}
}
