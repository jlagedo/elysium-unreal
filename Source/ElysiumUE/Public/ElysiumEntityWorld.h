#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumDialogueCamera.h"
#include "ElysiumEventQueue.h"
#include "ElysiumFootstepTuning.h"   // FElysiumFootstepTuning — held by value below
#include "ElysiumIOSink.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumUserCmd.h"   // EElysiumButton — the world retains the combat button field itself
#include "ElysiumVariant.h"
#include "ElysiumWireReport.h"
#include "ElysiumWorldServices.h"

struct FElysiumSignData;
struct FElysiumLootView;
struct FElysiumTerminalView;

class FElysiumDlgConversation;
struct FElysiumDialogueSession;
class FElysiumGameSoundBus;
class FElysiumLineService;
// The law-record store's own type, forward-declared through its namespace so this
// public header stays clear of the substrate's private ones (the sound bus's own posture).
namespace ElysiumNpcWitness { class FElysiumLawEventBus; }
class AActor;
class UElysiumBrushComponent;
class UElysiumGameStateSubsystem;
class UElysiumMapCollisionPayload;
class UPhysicsConstraintComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

// The substrate: one plain-C++ object per map, owned by AElysiumMapActor, that
// dies with it. It parses `.ents` into live entities, indexes them by name and class, and routes
// every input delivery and every deferred output through the two chokepoints (AcceptInput and the
// event queue) with the debug sinks always installed. It is driven twice per frame with the game
// clock's `now`, straddling the pawn's move the way retail does: RunPlayerThink before it, then
// Tick after it — think-first (retail order) — run due thinks, then service the queue.
//
// Identity is generation-checked: each world instance takes a unique epoch, every handle it
// mints carries that epoch, and Resolve returns null for a stale-epoch, out-of-range, or dead
// handle — the "falsy when dead/stale" contract VtMB scripts rely on. Teardown bumps the epoch,
// invalidating all outstanding handles at once.
//
// Everything it needs *from* the engine arrives as FElysiumWorldServices: bodies,
// voices, travel and presentation. `InOwner` is not a fifth service — it is the component outer and
// the VLOG context, nothing more. No code under this class casts it to a map actor or walks it to a
// subsystem, which is what lets the whole substrate run with `nullptr, nullptr, {}`.
class FElysiumEntityWorld
{
public:
	FElysiumEntityWorld(AActor* InOwner, UElysiumGameStateSubsystem* InGameState,
		const FElysiumWorldServices& InServices = FElysiumWorldServices());
	~FElysiumEntityWorld();

	FElysiumEntityWorld(const FElysiumEntityWorld&) = delete;
	FElysiumEntityWorld& operator=(const FElysiumEntityWorld&) = delete;

	// --- Lifecycle ---
	// Build one entity per def via the registry (inert record when the classname is
	// unregistered), index names/classes, run the spawn pass (Spawn() on each). The resulting
	// substrate is dormant: construction may queue work, but no think, event, cursor or physical
	// touch ingress is admitted until Activate.
	void Load(FElysiumEntityDefs&& InDefs);
	// The map's cooked collision payload (R4.2), or null when this map has none. Set before Load,
	// by the one caller that owns the map's transports; a brush entity whose lump ordinal has a
	// body there adopts it instead of cooking one. The world holds it weakly and reads nothing else
	// from it — the payload belongs to UElysiumMapCollision, which outlives the load pass.
	void SetCollisionPayload(const UElysiumMapCollisionPayload* InPayload);
	// Walk the dormant map's animation references after the player and restored state exist. This is
	// deliberately separate from Load: the map actor owns the engine-side batch completion that must
	// follow it before the activation gate opens.
	void PreloadMapAnimations();
	// A runtime SetModel invalidates skeleton-bound entries contributed by the dormant walk. Queue
	// the same exact reference closure against the replacement cast without activating or playing
	// anything; the next scene/sequence pre-roll closes the batch before starting its clock.
	void RefreshAnimationPreload();

	// Open the gameplay gate at the map actor's frozen game time. Idempotent: a second call does
	// not restart timers or replay construction. The map actor owns the initial think/event pass
	// that follows this transition, so activation itself only changes the lifecycle state.
	void Activate(double Now);
	bool IsActive() const { return bActive; }

	// Global exploration gate (`elysium.trigger on|off`). Off suspends all map-driven gameplay:
	// trigger-overlap ingress, +use dispatch, entity thinks/timers, and the deferred I/O/Python
	// queue. It deliberately leaves queued work intact, so `on` resumes the same map state without
	// a reload. This is process-wide so it can be set before a map is activated.
	static void SetTriggerResolutionEnabled(bool bEnabled);
	static bool IsTriggerResolutionEnabled();

	// The frame's PRE-move drive (map actor PreMoveTick): the player entity's own think, and only
	// that. Retail runs it inside CPlayerMove::RunCommand rather than in the think pass, so it is
	// the one entity whose think lands before the pawn moves.
	void RunPlayerThink(double Now);

	// The frame's POST-move drive (map actor Tick), which is retail's `GameFrame`: sample the moved
	// body into the player entity, then think-first — RunThinks(Now) then ServiceEvents(Now).
	void Tick(double Now);

	// --- Chokepoints ---
	// Deliver an input to a target: resolve `!self`/`!activator`, fan out over the name index,
	// walk each target's class-chain input table (case-folded), invoke the thunk, notify sinks.
	// Unknown target/input: notify (log-once) and keep going. The only input path in the game.
	void AcceptInput(const FString& Target, FName Input, const FElysiumVariant& Param,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller);
	// Single-target form for resolved-handle relationships such as door use_override. It keeps
	// the original caller provenance without fanning out over duplicate targetnames.
	void AcceptInput(const FElysiumEntityHandle& Target, FName Input, const FElysiumVariant& Param,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller);
	// Fire a named output from an entity: for each matching def row whose `times` is not spent,
	// count it down and queue the delivery at now + delay (attaching field-6 Python). The only
	// way outputs become queue entries. `ValueOverride` is a Source COutput<T> runtime value:
	// it fills any wire whose map-authored param is empty; a Void override leaves the empty param.
	void FireOutput(FElysiumEntity& Source, FName OutputName, const FElysiumEntityHandle& Activator,
		const FElysiumVariant& ValueOverride = FElysiumVariant::Void());

	// Debug/console injection (inspector fire buttons, `ent_fire`): queue a hand-made
	// input delivery through the real event queue (chokepoint 2) at now + delay — the same code
	// path a game output takes, so manual tests are faithful, show up in the queue window, and are
	// single-steppable. Targeting one specific entity uses Target "!self" with Caller = its handle.
	void EnqueueInput(const FString& Target, FName Input, const FElysiumVariant& Param, double Delay,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller);

	// Runtime entity creation (npc_maker.Spawn): synthesize a live entity from a def built at
	// runtime rather than parsed from the map. The def is stored (the entity holds Def*), the handle
	// index continues past the map's def array (Resolve indexes EntityList directly), name/class
	// indices are updated so the new entity is a live I/O target immediately, then Spawn() runs — a
	// leaf builds its body/visual there. Returns the new entity's handle (Invalid on a bad def).
	FElysiumEntityHandle SpawnRuntimeEntity(FElysiumEntityDef Def);

	// The scripted two-phase create (VtMB's CreateEntityNoSpawn / CallEntitySpawn). Phase 1
	// appends a live entity from a runtime def and indexes it by name/class WITHOUT running Spawn(),
	// so the script can SetModel/SetName/SetOrigin on it first; phase 2 runs Spawn() (once, gated by
	// bSpawnCalled) and builds its brush body. SpawnRuntimeEntity is the two fused (npc_maker's path).
	FElysiumEntityHandle CreateRuntimeEntityNoSpawn(FElysiumEntityDef Def);
	void CallEntitySpawn(FElysiumEntity& Ent);

	// Create this map's player entity: an ordinary runtime entity of classname `player`,
	// named `!player` so the 48 `point_teleport.target` keys the maps author resolve through the
	// name index like any other targetname. Hydrated from the session record when a game state is
	// attached. It also stands up the four engine-owned `viewmodel` companions patch Python expects.
	// Call it once, after Load and before the first Tick; a second call is a no-op.
	// A map built without a player (the menu backdrop, a headless logic test) simply never calls it,
	// and every reader handles FindPlayer() being null — the same null-service discipline the
	// outbound seam established.
	FElysiumEntityHandle SpawnPlayer();
	// The same clan/gender/armor selection used by SpawnPlayer, available before body construction.
	FString InitialPlayerModel() const;
	// This world's player entity, or null when the map was built without one.
	class FElysiumPlayer* FindPlayer() const;
	FElysiumEntityHandle PlayerHandle() const { return Player; }

	// The cutscene stand-in created by events_player. It is one real runtime entity per map epoch,
	// not a latch: scenes, I/O and scripts resolve it through !playercontroller, and removal transfers
	// its final embodied state back to the player before killing the stand-in.
	FElysiumEntityHandle CreatePlayerControllerEntity();
	bool RemovePlayerControllerEntity();
	FElysiumEntity* FindPlayerController() const;
	FElysiumEntityHandle PlayerControllerHandle() const { return PlayerControllerEntity; }

	// Drop the player without touching the entity, so Teardown has nothing to dehydrate. What
	// ending a session means: the run is over, and the dying world's numbers must not be written
	// back into the record that was just cleared (travel is deferred, so the teardown lands after
	// `EndSession` returns).
	void ForgetPlayer() { Player = FElysiumEntityHandle::Invalid(); }

	// --- Persistence (`docs/architecture/save-architecture.md` §5) ---
	// Freeze this map to a snapshot. Pure read: the same call serves a travel boundary and a save,
	// which is what keeps the two from drifting apart. Every entity is diffed against a **fresh
	// build of its own def** and contributes nothing when it matches — the generalisation of VtMB's
	// zero-value-omission rule, and most of why these payloads are small.
	void Freeze(FElysiumMapSnapshot& Out) const;

	// Apply a snapshot onto this freshly-built world. Runs after Load + SpawnPlayer and before the
	// first Tick, so the map's own spawn pass has already produced the baseline the snapshot edits.
	// It **replaces** the event queue rather than appending to it, because Spawn() will have queued
	// this load's own openers. Reports how many entity records were applied.
	int32 ApplySnapshot(const FElysiumMapSnapshot& Snapshot);
	// Leaf serializers that own handles use the same epoch re-stamping rule as the generic field
	// applier. This keeps the epoch private and avoids duplicating handle validity checks.
	FElysiumEntityHandle RebaseSavedHandle(const FElysiumEntityHandle& Saved) const
	{
		return RebaseHandle(Saved);
	}

	// Give up this world's claim on the session: forget the player (so Teardown dehydrates nothing)
	// and suppress the teardown freeze. What a load means for the world being replaced — the record
	// and the snapshots have already been overwritten from the payload, and travel is deferred, so
	// the dying world's teardown lands afterwards and must not write over them.
	void Detach();

	// VtMB's Entity.SetName: re-key the name index so the renamed entity is immediately findable
	// under its new targetname (and no longer under the old). Empty names are handled (add/remove skip).
	void RenameEntity(FElysiumEntity& Ent, const FString& NewName);

	// Register an NPC skeletal body (built by AElysiumMapActor::BuildNpcVisual) so the world tears
	// it down with the map. The FElysiumNpc leaf calls this from Spawn(); mirrors how brush bodies are
	// tracked, so a world rebuild on a surviving actor (reload) does not leak the components.
	void RegisterNpcBody(USkeletalMeshComponent* Component);

	// Register a dynamic-prop body (built by AElysiumMapActor::BuildPropVisual) so the world
	// tears it down with the map, exactly like NPC bodies. The FElysiumProp leaf calls this from Spawn().
	void RegisterPropBody(UPrimitiveComponent* Component,
		const FElysiumEntityHandle& UseOwner = FElysiumEntityHandle::Invalid());
	void RegisterUseAnchor(UPrimitiveComponent* Component, const FElysiumEntityHandle& Owner);
	void RegisterTouchAnchor(UPrimitiveComponent* Component, const FElysiumEntityHandle& Owner);
	// Keep a registered model anchor in step with ScriptHide/Kill without teaching the entity about
	// collision profiles. Brush bodies also report the edge, although their own SetDormant remains
	// the physical collision authority.
	void SetUseAnchorEnabled(const FElysiumEntityHandle& Owner, bool bEnabled);
	void SetTouchAnchorEnabled(const FElysiumEntityHandle& Owner, bool bEnabled);

	// Register a physics constraint (built by a phys_hinge leaf) so the world tears it down
	// with the map, like the prop/NPC bodies. The FElysiumPhysHinge leaf calls this from PostSpawn().
	void RegisterConstraintBody(UPhysicsConstraintComponent* Component);

	// ScheduleTask(delay, "<source>"): defer a field-6 Python source string on the same
	// event queue, evaluated at now+delay through the installed script host (DeliverEvent's Python
	// half). No I/O target — it is a python-only event, exactly the shape a field-6-only output
	// produces — so it single-steps in the queue window and serializes into a save. The source
	// resolves against the delivery's provenance (Caller = the scheduling entity, `!self`).
	void EnqueuePython(const FString& Source, double Delay,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller);

	// The logic_pythoncheck / condition path: evaluate an expression string through the
	// installed script host with the given provenance and return its value (Void when the source is
	// empty, there is no host/state, or the eval failed — error-to-false, so a gate over it reads
	// OnFalse). Goes through the host (not ElysiumExpr directly) so `elysium.script.live 0` disables
	// conditions in lockstep with field-6, and the eval lands in the recent-eval debug log.
	FElysiumVariant EvalCondition(const FString& Source, const FElysiumEntityHandle& Self,
		const FElysiumEntityHandle& Activator);

	// Overlap routing: a brush body's begin/end overlap lands here. Resolve the brush
	// entity, skip if inert, and call its OnTouchStart/OnTouchEnd (triggers override).
	void RouteEntityTouch(const FElysiumEntityHandle& Touched, const FElysiumEntityHandle& Activator,
		bool bBegin);
	void RouteBrushTouch(const FElysiumEntityHandle& Brush, const FElysiumEntityHandle& Activator,
		bool bBegin)
	{
		RouteEntityTouch(Brush, Activator, bBegin);
	}
	// Deterministically release every retained pair owned by a brush before its physical collision
	// is removed. Later engine end callbacks are harmless because the pairs are already absent.
	void EndBrushTouches(const FElysiumEntityHandle& Brush);
	// Replace the player's retained touch set with one authoritative post-movement containment
	// observation. Ends are emitted before begins; both groups are stable by entity index.
	void ReconcilePlayerTouches(TConstArrayView<FElysiumEntityHandle> CurrentBrushes);

	// Player interaction: command edges queue in the controller's pre-move sample and are consumed
	// only after this frame's post-move focus query. The focused entity and any captured session are
	// world state; candidates are an ephemeral embodiment result.
	void QueuePlayerUseEdge(EElysiumUseEdge Edge);
	void UpdatePlayerInteraction();

	// The `+feed` / `-feed` pair, queued in the controller's pre-move sample and consumed after
	// the move beside `+use` (`docs/vtmb/feeding.md` § "Command and initial request"). An
	// unpaired press runs one target query and `AttemptFeed`; a paired feeder press clears the
	// continuation latch, while button-up is inert. The action owns the latch independently of the
	// low-level button pair.
	void QueuePlayerFeedEdge(EElysiumUseEdge Edge);
	void UpdatePlayerFeed();

	// The player's own weapon frame, run once per post-move tick in retail's `PostThink`
	// order (`docs/vtmb/player-entity.md` § "Recovered `PostThink` body": controlled-use first
	// refusal, then `ItemPostFrame`). It computes this frame's press edges off the button field
	// above, applies the refusals, and hands the held/pressed pair to the active weapon's
	// `FElysiumWeapon::ItemPostFrame`. It is the player half of the attack producer; the AI half
	// stays its schedule tasks' direct `AttackIntent` calls.
	void UpdatePlayerWeaponFrame();

	// `CBasePlayer::PostThink`'s melee stop, run once per post-move tick immediately before
	// the frame's animation selection. No `docs/vtmb/` section owns this block yet: it sits between
	// the grounded fall-sound reset and the realized-action classifier, which is a gap in
	// `player-entity.md`'s recovered `PostThink` listing.
	//
	// Every frame from the swing's `w_hold` to the end of its clip, with no direction key held, the
	// swing's own authored lunge is discarded so the selector that runs next reads a standing body. The rule is
	// `ElysiumClipMovement::StopsMeleeTailMotion`; the two facts it needs come from the embodiment —
	// where the forced sequence stands, and the held direction bits this world already keeps — and
	// the stop itself goes back out through `IElysiumEmbodiment::StopPlayerBody`.
	//
	// The ORDER is the whole of it: retail zeroes the velocity and then, in the next instruction
	// block, asks the classifier and calls `SetAnimation`. Run after the selection instead and the
	// gait has already been applied over the swing.
	//
	// `State` is PUSHED rather than pulled: the caller reads the driver's own rebuild between the
	// pose read and the selector, which is the only point in the frame where the record means what
	// this rule needs. Asking for it back through the seam would be the same object answering its
	// own question a call later, and the bool that came with it carried nothing a cleared record
	// did not already say.
	void UpdatePlayerMeleeMovementStop(const FElysiumIdealActivityState& State);

	// One frame of the melee contact walk, for EVERY character holding a live melee swing.
	//
	// Retail runs the swept contact on the CHARACTER's own update rather than on the player's input
	// path, so this is not a sibling of the weapon frame above: the player and every swinging NPC
	// reach it through the same walk over the entity list. It sits in the post-move pass because the
	// sweep reads the frame's final positions and the pose the body is actually drawing.
	//
	// It takes the frame's delta because the sub-step count is `floor(dt * 100)` — the one place in
	// this layer that needs a delta rather than a clock, and the reason it is driven from the map
	// actor's tick instead of from `Tick(Now)`.
	void AdvanceMeleeSwings(float DeltaSeconds);

	// The whole combat button field, forwarded as a LEVEL rather than as an edge pair. This
	// is retail's one current-button field at player `+0x2088`: every consumer reads bits off it and
	// derives whatever edge it needs, rather than each verb queueing its own press/release history
	// (`docs/vtmb/controls.md` § "Attack, block and weapon commands"). The block classifier wants a
	// held bit, the weapon frame wants a press edge, and both come off this one field.
	//
	// A change arms the player think immediately: that think is deadline-driven, so without this a
	// press would wait out the stealth cadence before the block engaged.
	void SetPlayerButtons(uint64 Buttons);
	uint64 GetPlayerButtons() const { return PlayerButtons; }
	// The same field, translated into the FILE's own `IN_*` numbering and reduced to the bits attack
	// selection reads — retail's `+0x2088 & 0x79A`. The authored sequence masks are exported raw, so
	// the translation happens here rather than in the resolver: this runtime numbers its button bits
	// differently and the two only correspond by DIRECTION.
	//
	// It is the player's alone. A cast body has no button field, and `FillActivityClipRequest` states
	// `INDEX_NONE` for one rather than handing it an empty player state.
	int32 PlayerSelectionStateMask() const;
	bool IsPlayerBlockHeld() const
	{
		return (PlayerButtons & static_cast<uint64>(EElysiumButton::SecondaryAtk)) != 0;
	}
	// The explicit leaf/UI completion seam. Supplying the captured owner prevents a stale panel
	// from ending a newer entity's session; Invalid intentionally means cancel whatever is active.
	bool EndPlayerUseSession(const FElysiumEntityHandle& OwnerHandle, EElysiumUseEndReason Reason);
	// Start a captured use on a named logical owner without re-running spatial selection. Attached
	// lockables use this after their own accepted key/skill transaction forwards into a container.
	FElysiumUseBeginResult BeginPlayerUseSession(const FElysiumEntityHandle& OwnerHandle,
		const FElysiumEntityHandle& Activator);
	FElysiumEntityHandle GetFocusedUsable() const { return FocusedUsable; }
	FElysiumEntityHandle GetAimedUsable() const { return FocusedUsable; } // debug compatibility
	FElysiumInteractionView GetInteractionView() const;
	bool BuildLootView(FElysiumLootView& Out) const;
	bool PlayerLootTake(int32 Slot);
	bool PlayerLootGive(int32 Slot);
	bool PlayerCloseLoot();
	bool BuildTerminalView(FElysiumTerminalView& Out) const;
	bool SubmitTerminalCommand(const FElysiumEntityHandle& OwnerHandle, uint32 SessionSerial,
		const FString& Command);
	bool SubmitActiveTerminalCommand(const FString& Command);
	bool PlayerBeginTerminalHack(const FElysiumEntityHandle& OwnerHandle, uint32 SessionSerial);
	EElysiumUseOutcome GetLastUseOutcome() const { return LastUseOutcome; }

	// --- Screen fade (env_fade) ---
	// A full-screen colour fade driven by env_fade's `Fade` input, advanced off the game clock and
	// published each frame by UElysiumPresentationSubsystem. Held on the world so it dies with
	// the map. One active fade at a time — a new Fade replaces the running one; VtMB keeps a fade
	// *list*, but its colours sum and its alphas max, which is indistinguishable from one slot while
	// every fade on a map is the same colour (all of them are black on the tutorial).
	// `bFadeIn` = SF_FADE_IN, `bAutoReverse` = SF_FADE_STAYOUT — see GetScreenFade for the curve.
	void StartScreenFade(const FLinearColor& Color, float Duration, float HoldTime, float MaxAlpha,
		bool bFadeIn, bool bAutoReverse);
	// Fills OutColor (rgb = fade colour, a = current 0..1 alpha) and returns true while a fade is
	// visible; false when idle. Const — the HUD polls it; an expired fade reports idle.
	bool GetScreenFade(FLinearColor& OutColor) const;

	// --- Open sign window (game_sign) ---
	// The one sign panel currently on screen, driven by game_sign's OpenWindow/CloseWindow and
	// published each frame by UElysiumPresentationSubsystem. Same shape as the screen fade:
	// held on the world so it dies with the map, one at a time (a second OpenWindow replaces the
	// first, matching CSignUI's single panel). The handle identifies the owning entity so dismissal
	// can fire its OnUseEnd back through the real output path.
	// `Data` is the owning entity's parsed panel, shared so the HUD can draw it without knowing the
	// sign entity type (the class is file-local to ElysiumSignClasses.cpp).
	// `FadeInSeconds` is the owner's `fade_in` keyfield (an entity property, not panel content).
	void OpenSign(const FElysiumEntityHandle& Owner, TSharedPtr<const FElysiumSignData> Data,
		float FadeInSeconds);
	// Dismiss the open panel (left-click, CloseWindow, or the owner dying). Fires the owner's
	// OnUseEnd unless bSilent — a Kill/teardown must not resurrect outputs.
	void CloseSign(bool bSilent = false);
	// The entity whose sign is open, or Invalid. `OutOpenTime` is the game time it opened at
	// (the HUD derives fade-in and MinShowTime from it).
	FElysiumEntityHandle GetOpenSign(double* OutOpenTime = nullptr) const;
	// The open panel's parsed content, or null when nothing is open. What AElysiumHUD draws.
	const FElysiumSignData* GetOpenSignData() const { return OpenSignData.Get(); }
	// The player's left-click. Dismisses the open panel when its Rules allow it (CloseOnLeftClick)
	// and it has been up for at least MinShowTime; no-op when no sign is open. Firing OnUseEnd is
	// what advances the tutorial, so this is a game path, not a UI convenience.
	bool CanPlayerDismissSign() const;
	bool PlayerDismissSign();
	// The open sign's `fade_in` seconds (0 = appear instantly).
	float GetOpenSignFadeIn() const { return OpenSignFadeIn; }

	// --- Open dialogue (`.dlg` conversation) ---
	// The one conversation currently on screen, driven by an NPC's StartPlayerDialogRemote and drawn
	// by the visual-novel Slate box off the published view state (same held-on-the-world
	// lifetime as the sign/fade). The owning NPC's OnDialogEnd fires when it closes (the beat
	// machine's hinge — DialogPostProcess reads the `G` flags the dialogue's field-5 actions wrote).
	void OpenDialog(const FElysiumEntityHandle& Owner, TSharedRef<FElysiumDlgConversation> Conversation,
		EElysiumDialogOpenerKind Opener = EElysiumDialogOpenerKind::Remote,
		int32 RawFlags = 0, const FString& DefaultCamera = FString(),
		const FElysiumBodyOwnerToken& BodyOwner = FElysiumBodyOwnerToken());
	// The live conversation, or null when none is open. What the dialogue box renders.
	FElysiumDlgConversation* GetOpenDialog() const;
	// The NPC the open conversation belongs to (Invalid when none is open).
	FElysiumEntityHandle GetOpenDialogOwner() const;
	// True while the open line's authored VCD owns this speaker's body through a live clip event.
	bool HasActiveDialogueBodyClip(const FElysiumEntityHandle& Speaker) const;
	// Player picked the Nth visible PC choice: advance the branch machine; end the session (firing the
	// owner's OnDialogEnd) if the pick closed it. No-op when no conversation is open.
	void PlayerDialogChoose(int32 VisibleIndex);
	// Player advanced past a terminal NPC line. Also resolves a pending automatic row only when its
	// voice could not be started and presentation exposed the explicit Continue fallback.
	void PlayerDialogAdvance();
	// True only for that automatic-transition failure fallback; normal Auto-Link/Auto-End turns do
	// not accept input and advance from the current voice handle's completion.
	bool CanPlayerAdvanceAutomatic() const;
	// Force-close the open conversation. bSilent suppresses OnDialogEnd (a Kill/teardown must not
	// resurrect the beat machine); a normal close fires it.
	void CloseDialog(bool bSilent = false);
	// Re-resolve the selected source/profile anchors against settled body positions and update the
	// same scoped request. No candidate search occurs here; selection changes only at line boundaries.
	void RefreshDialogueCamera();
	bool GetDialogueCameraGaze(FVector& OutPoint) const;
	bool DialogueCameraHidesHud() const;
	void GetDialogueDebugState(TArray<TPair<FString, FString>>& Out) const;
	FString ScriptedSessionSaveBlockReason() const;

	// The one scripted camera the map has up, held here for exactly the reason the sign and the
	// conversation are: it is world state with a lifetime, and the thing that draws it is replaceable.
	// `SetCamera(shotfile)` (115 script calls) sets it and `RemoveCamera` clears it; setting a second
	// one replaces the first, which is what "*the* cinematic camera mode" means. Both no-op with no
	// embodiment, so a headless conversation runs the same beats without a camera to point.
	void SetScriptedCamera(const FString& ShotFile, const FElysiumEntityHandle& Subject);
	void ClearScriptedCamera();
	bool HasScriptedCamera() const { return ScriptedCameraShot != 0; }
	const FString& ScriptedCameraName() const { return ScriptedCameraFile; }

	// `camera_track` selects and publishes position and target independently. Selection is exclusive
	// per role: a newer track supersedes an older one, whose clock and outputs may continue without
	// reclaiming the view. The world composes both selected roles into one value shot so restoring one
	// track never tears down the other.
	bool SelectTrackCameraRole(bool bTargetRole, const FElysiumEntityHandle& Owner);
	void PublishTrackCamera(bool bTargetRole, const FElysiumEntityHandle& Owner,
		const FVector& Point, const FRotator& Rotation, float Roll, float FieldOfView,
		float BlendInSeconds, bool bCameraCut = false);
	void RestoreTrackCamera(bool bTargetRole, const FElysiumEntityHandle& Owner,
		float BlendOutSeconds);
	void ClearTrackCamera(float BlendOutSeconds = 0.0f);
	bool HasTrackCamera() const { return TrackCameraShot != 0; }
	FElysiumEntityHandle TrackCameraOwner(bool bTargetRole) const
	{
		return bTargetRole ? TrackCameraTargetOwner : TrackCameraPositionOwner;
	}

	// The game-state subsystem (the `G`/quest store, player sheet, script host). Outlives the world.
	UElysiumGameStateSubsystem* GetGameState() const { return GameState; }

	// --- The outbound seam ---
	// The five services, injected at construction. **Every one may be null** — a headless world has
	// none, `elysium.NpcBodies 0` runs without an embodiment, and Presenter has no production
	// implementation where nothing publishes a view. Call sites check; the world never manufactures
	// a substitute.
	const FElysiumWorldServices& Services() const { return WorldServices; }
	IElysiumEmbodiment* Embodiment() const { return WorldServices.Embodiment; }
	IElysiumAudio*      Audio() const      { return WorldServices.Audio; }
	IElysiumTravel*     Travel() const     { return WorldServices.Travel; }
	IElysiumPresenter*  Presenter() const  { return WorldServices.Presenter; }
	IElysiumWeather*    Weather() const    { return WorldServices.Weather; }
	IElysiumCameraService* Camera() const  { return WorldServices.Camera; }
	// --- Game-sound stimulus (the third event kind) ------------------------------------
	// `docs/architecture/gameplay-systems-architecture.md` §2.5.3. A domain that makes a noise the
	// world can react to calls this from its real producer site; nothing is delivered, and there is
	// no fifth transport. Consumers poll `GameSounds()` during their own think.
	//
	// `RadiusCm <= 0` asks `sound_volume_table.txt` for the category's own reach, which is the
	// ordinary call. `StealthHearingReductionCm` is `AdjustSoundDistForStealth`'s subtrahend: a
	// producer whose source is a character reads it off that character's committed stealth surface
	// through `ElysiumStealth::HearingReductionCmFor`, and a world-made noise (a door) passes 0.
	void EmitGameSound(const FVector& PositionCm, FName Category, float RadiusCm,
		const FElysiumEntityHandle& Source, float StealthHearingReductionCm = 0.f);

	// **The player's ONE permanently reserved locomotion stimulus** — `CBasePlayer::UpdatePlayerSound`
	// (`vampire.dll 0x1016b480`), which inserts nothing and rewrites one `CSound` record every think
	// (`docs/vtmb/footsteps.md` §2.5). `Slot` is the caller's handle into the bus, updated in place;
	// a `NAME_None` category retires it, which is retail's volume 0.
	//
	// `RadiusCm` is explicit here rather than table-resolved, because the producer's own radius is a
	// DECAYING value — the reserved slot rises instantly and falls at 250 units/s — and is only
	// equal to the category's authored reach on the think the sound is loudest. Ask
	// `GameSoundRadiusUnits` for the value it decays towards.
	void RefreshGameSound(uint64& Slot, const FVector& PositionCm, FName Category, float RadiusCm,
		const FElysiumEntityHandle& Source, float StealthHearingReductionCm = 0.f);
	// The authored reach of a named category, SOURCE UNITS, with the volume table bound on first
	// use exactly as `EmitGameSound` binds it. 0 for `NAME_None`.
	float GameSoundRadiusUnits(FName Category);
	// The retained window. Const for consumers and the debug surface; the mutable overload exists
	// for the two callers that own the bus's configuration — this world, and a Substrate-tier test
	// binding a fabricated table — the same shape `Queue()` already has. Defined in the .cpp
	// because the bus type is only forward-declared here.
	const FElysiumGameSoundBus& GameSounds() const;
	FElysiumGameSoundBus& GameSounds();

	// --- A2 (footsteps): the cvar surface both step producers read ------------------------
	// `footstep_normal_vol/_dist`, `footstep_heavy_vol/_dist`, `footstep_npc_use_templates`,
	// `footstep_pc_vol` and `sv_footsteps`, at their retail defaults until something refreshes
	// them. The mutable overload is the refresh (the map actor's pre-move pass calls `LoadFrom`
	// through it, the way `UElysiumMovementComponent` re-reads `FElysiumMoveTuning` every frame);
	// every consumer takes the const one. Held here rather than on a component because both
	// producers are substrate rules and neither can see the console.
	const FElysiumFootstepTuning& FootstepTuning() const { return FootstepTuningState; }
	FElysiumFootstepTuning& FootstepTuning() { return FootstepTuningState; }

	// --- The world-event law lane's record store ---
	// The expiring criminal/supernatural records an NPC's global witness lane polls, in exactly the
	// game-sound bus's shape and held by pointer for the same reason: a producer stamps a record, no
	// receiver is bound, and every consumer scans the retained window during its own think. It is
	// session state and is deliberately not saved — the reasoning is on
	// `ElysiumNpcWitness::FElysiumLawEventBus`.
	const ElysiumNpcWitness::FElysiumLawEventBus& LawEvents() const;
	ElysiumNpcWitness::FElysiumLawEventBus& LawEvents();

	const FElysiumWeatherState& GetWeatherState() const { return WeatherState; }
	void FadeGlobalWetness(float Target);
	FElysiumLineService* Lines() const { return LineService.Get(); }

	// Debug tap seam (`ent_*`): install an extra I/O sink, owned by the world and torn down
	// with it. The ent_* debug subsystem taps the two chokepoints for its overlay/break tooling
	// through the same sink interface the ring buffer and log stream already use — no I/O side
	// channel. Re-installed by the subsystem whenever a new world epoch appears.
	void AddSink(TUniquePtr<IElysiumIOSink> InSink);

	// Visual-change seam (retained gizmo layer): a callback fired whenever an entity's
	// dormancy/liveness flips (from FElysiumEntity::OnDormancyChanged / Kill). It lets a retained
	// visualizer dirty just that one instance on the event instead of polling every entity every
	// frame. Optional (unset in normal play); the debug subsystem sets it per epoch. Called by the
	// entity through its World back-pointer.
	void SetVisualChangedHook(TFunction<void(const FElysiumEntity&)> Hook) { VisualChangedHook = MoveTemp(Hook); }
	void NotifyVisualChanged(const FElysiumEntity& Ent) const { if (VisualChangedHook) { VisualChangedHook(Ent); } }

	// --- Resolution / iteration ---
	FElysiumEntity* Resolve(const FElysiumEntityHandle& Handle);
	const FElysiumEntity* Resolve(const FElysiumEntityHandle& Handle) const;
	FElysiumEntity* FindByName(const FString& Name);   // first live match, or null
	bool IsNpcMakerSceneBlocked() const;
	// First live info_landmark with this targetname (the landmark-transition anchor), or null.
	FElysiumEntity* FindLandmark(const FString& Name);
	void ForEachNamed(const FString& Pattern, TFunctionRef<void(FElysiumEntity&)> Fn);

	// How VtMB matches a targetname against a search string
	// (`CGlobalEntityList::FindEntityByName`, vampire.dll FUN_100f7770). A **trailing** `*` makes it a
	// case-insensitive prefix match over the characters before it (`_strnicmp`, n = len-1); anything
	// else is a case-insensitive exact match (`_stricmp`). Only the final character is special — a `*`
	// anywhere else is a literal. An empty pattern matches nothing, and an entity with no targetname is
	// never a candidate. Pure and static so the rule is testable without a world.
	//
	// NB the engine also accepts a leading `!` (`!player`, `!activator`, `!caller`, `!picker`,
	// `!pvsplayer`, `!playercontroller`) through a separate single-result path. ResolveTargets owns
	// that path ahead of this matcher, and a leading-`!` name it does not recognise resolves to
	// nothing rather than reaching here — `!picker` is a debug-verb argument, not a wire target.
	static bool NameMatches(const FString& TargetName, const FString& Pattern);

	// The map this world was built from (the snapshot key), or empty on a bare test world.
	const FString& MapName() const { return Defs.MapName; }

	int32 NumEntities() const { return EntityList.Num(); }
	const TArray<TUniquePtr<FElysiumEntity>>& Entities() const { return EntityList; }
	const FElysiumEventQueue& Queue() const { return EventQueue; }
	FElysiumEventQueue& Queue() { return EventQueue; }
	const FElysiumRingBufferSink& RingBuffer() const { return *Ring; }
	uint32 GetEpoch() const { return Epoch; }
	// The actor bodies are outer'd to and VLOGs are drawn against, or null in a headless world.
	// Not a service — nothing reads behaviour off it.
	AActor* GetOwnerActor() const { return Owner; }
	double NowSeconds() const;
	int32 UnknownTargets() const { return UnknownTargetCount; }
	int32 UnknownInputs() const { return UnknownInputCount; }
	// Service passes that hit the drain cap and deferred a still-due tail (the one enumerated
	// ordering divergence from retail, which drains unbounded).
	int32 LoopGuardTrips() const { return LoopGuardTripCount; }
	int32 NumBrushBodies() const { return Bodies.Num(); }
	int32 TouchBegins() const { return TouchBeginCount; }
	int32 TouchEnds() const { return TouchEndCount; }

	// --- Per-wire accounting (`docs/architecture/gameplay-systems-architecture.md` §7) -------------
	// The instrument that turns "are events working?" into a number. Every authored output row is a
	// wire; the tally says what each one did, so acceptance can tell "never fired" from "target not
	// found" from "receiver refused" instead of reading a whole map as one pass/fail. Accounting
	// only — nothing here participates in delivery, ordering or outcome.
	//
	// Live, keyed, and readable without the console: the map holds only the wires something
	// reached, which is why the report below joins it against the def array rather than being read
	// straight. It is per-session and deliberately NOT serialized: a restored save that claimed the
	// firing history of the run that wrote it would answer a question nobody asked — the question is
	// what THIS run's wires did. The wire identity on the pending queue records IS saved, so a
	// delivery that lands after a restore still attributes to its row.
	const TMap<FElysiumWireRef, FElysiumWireTally>& WireTallies() const { return WireTally; }
	// One wire's counts, all-zero for a wire nothing has reached (so a caller never distinguishes
	// "absent from the map" from "did nothing").
	FElysiumWireTally WireTallyFor(const FElysiumWireRef& Wire) const;
	// Every authored wire in this map joined to its tally, in (entity index, def row) order —
	// including the ones with no activity at all, which is the set acceptance exists to find.
	// Runtime-spawned entities' rows come last-ish (their entities sit past the def array) and are
	// flagged, because they are not part of the authored surface.
	void BuildWireReport(TArray<FElysiumWireReportRow>& Out) const;
	// Start the measurement over without reloading the map (one scene, one beat, one console run).
	void ResetWireTallies() { WireTally.Reset(); }

	// --- Formatting (used by the sinks; resolves handles to the canonical debug string) ----
	// `#<idx> <name>(<class>)` for a handle, or `#<null>` / `#<stale>` when it cannot resolve.
	FString DescribeHandle(const FElysiumEntityHandle& Handle) const;
	// `(t) <caller> -> <target>.Input(param)` plus a `[py]`/`[no target]`/`[no input]` note.
	FString FormatEventLine(double Now, const FElysiumIOEvent& Event, const FString& TargetLabel,
		const TCHAR* Note) const;

private:
	void Teardown();
	void TransitionUseFocus(const FElysiumUseCandidate* Candidate);
	void EndActiveUse(EElysiumUseEndReason Reason);
	float InteractionPromptAlpha(double Now) const;
	// Build the brush body for one entity, if it is a brush with hulls: cook the convex
	// UBodySetup from the def, place it at the def origin, attach it to the owner actor, store it
	// on the entity, and start it dormant when born hidden. Point/logic entities get no body.
	void BuildBrushBody(FElysiumEntity& Ent);
	void CallEntityActivate(FElysiumEntity& Ent);
	// The by-name AcceptInput, carrying the wire the delivery came from. The public overload is this
	// with no wire; DeliverEvent passes the queued record's, which is what attributes an outcome to
	// the authored row rather than to "some input, somewhere". Nothing else about the dispatch
	// changes with it.
	void AcceptInputFromWire(const FString& Target, FName Input, const FElysiumVariant& Param,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller,
		const FElysiumWireRef& Wire);
	// The tally row for a wire, created on first touch; null for an unset wire (a console injection,
	// a ScheduleTask, an AcceptInput nobody wired). Every accounting site goes through it, so an
	// unattributed event costs one branch and stores nothing.
	FElysiumWireTally* WireRowFor(const FElysiumWireRef& Wire);

	// The queue.Add wrapper: assigns time/serial upstream, notifies OnQueued.
	void AddEvent(FElysiumIOEvent&& Event);
	void ServiceEvents(double Now);
	// One frame of every bodied entity's sequence-event timelines, run before the thinks.
	//
	// It takes no clock: the dispatcher's whole rule is an interval over a NORMALIZED cycle the pose
	// layer publishes, so nothing in the walk reads world time (`Substrate/ElysiumAnimEvents.h`).
	void AdvanceAnimEvents();
	void RunThinks(double Now);
	void DeliverEvent(const FElysiumIOEvent& Event, double Now);
	void DeliverInputTo(FElysiumEntity& Target, const FElysiumIOEvent& Event, double Now);
	// Resolve a due event's target string to live entities (skips dead), honouring !self/!activator.
	void ResolveTargets(const FElysiumIOEvent& Event, TArray<FElysiumEntity*>& Out);
	// The one name-search walk, shared by ForEachNamed / FindByName / ResolveTargets so a
	// pattern can never mean different things to a script, the I/O bus and a console verb. Visits every
	// live match in entity-list order until `Fn` returns false. An exact pattern takes the NameIndex
	// hash and sorts what it finds; only a trailing-`*` pattern pays for the linear scan.
	void ForEachMatch(const FString& Pattern, TFunctionRef<bool(FElysiumEntity&)> Fn);
	// Re-stamp a handle read out of a payload with this world's epoch (Invalid when its index
	// no longer exists). The only place a saved handle becomes a live one.
	FElysiumEntityHandle RebaseHandle(const FElysiumEntityHandle& Saved) const;
	// One entity's full state, undiffed — the shared half of the freeze and the baseline.
	FElysiumEntityState CaptureState(const FElysiumEntity& Ent) const;
	// Restore one snapshot record onto its live entity (ApplySnapshot's per-record half):
	// fields, rename, origin, lifecycle flags, leaf state, then dormancy, in that order. Returns
	// false — with a warning naming the snapshot — when the record has no live entity at its index
	// or the entity's classname no longer matches the saved one.
	// `LeafSchemaVersion` is the snapshot's own `SchemaVersion` — the schema `S.LeafState` was
	// written at. It is a parameter rather than a constant because a blob carries no version of its
	// own and a leaf's `Ar.Version()` gate is meaningless without it.
	bool ApplyEntityRecord(const FElysiumEntityState& S, const FString& SnapshotMapName,
		int32 LeafSchemaVersion);
	// Record one entity's post-Load state as the omission baseline. Called for every entity at the
	// end of Load and for each runtime entity as it spawns.
	void CaptureBaseline(int32 Index);
	void PublishWetness();
	// Bind `sound_volume_table.txt` to the bus on first use, latched. Shared by the three entry
	// points that need an authored answer (`EmitGameSound`, `RefreshGameSound`,
	// `GameSoundRadiusUnits`) so a world can never resolve a category against an unbound table
	// through one of them and a bound one through another.
	void BindSoundVolumes();

	AActor* Owner = nullptr;                          // component outer + VLOG context; not owned
	UElysiumGameStateSubsystem* GameState = nullptr;  // clock + script host; outlives the world
	FElysiumWorldServices WorldServices;              // the outbound seam; members may be null
	FElysiumWeatherState WeatherState;
	// A2 (footsteps): VtMB's seven footstep cvars, held by value at their retail defaults. Refreshed
	// from the console store by the map actor's pre-move pass; a headless world never refreshes it
	// and therefore runs on exactly the defaults a stock install has.
	FElysiumFootstepTuning FootstepTuningState;
	// The game-sound stimulus window. Held by pointer so the substrate's own header stays out of
	// this public one, the way LineService below already does.
	TUniquePtr<FElysiumGameSoundBus> GameSoundBus;
	// The law-record store, held the same way and for the same reason.
	TUniquePtr<ElysiumNpcWitness::FElysiumLawEventBus> LawEventBus;
	// When the player's step clock last ran, substrate seconds. Negative until the first tick, which
	// is what makes that tick's delta zero rather than "everything since the epoch". Held here
	// rather than on the player entity because it measures THIS world's tick cadence, and a player
	// hydrated into a new map has not been ticked in it yet.
	double LastStepClockNow = -1.0;
	// Set once the rulebook has been asked for the sound-volume table. Latched rather than retried,
	// so a world with no game state (or with no exported `vdata`) costs one lookup and then runs on
	// the bus's own normal-level fallback.
	bool bSoundVolumesBound = false;
	TUniquePtr<FElysiumLineService> LineService;
	uint32 Epoch = 0;
	bool bActive = false;
	bool bSnapshotApplied = false;
	// Indices whose state came from the applied snapshot. On restored activation, untouched rebuilt
	// entities join the post-Activate omission baseline; these records keep the construction baseline
	// so their saved activation-derived differences remain explicit.
	TSet<int32> SnapshotEntityIndices;

	FElysiumEntityDefs Defs;
	// Defs synthesized at runtime (npc_maker.Spawn): held so an entity's Def* stays valid past the
	// map's immutable def array. Cleared on teardown.
	TArray<TUniquePtr<FElysiumEntityDef>> RuntimeDefs;
	TArray<TUniquePtr<FElysiumEntity>> EntityList;    // Defs.Defs then runtime-spawned; index = handle index
	TMultiMap<FName, int32> NameIndex;                // targetname -> entity index (non-unique)
	TMultiMap<FName, int32> ClassIndex;               // classname  -> entity index

	FElysiumEventQueue EventQueue;
	TArray<TUniquePtr<IElysiumIOSink>> Sinks;
	FElysiumRingBufferSink* Ring = nullptr;           // owned in Sinks; the always-on history
	TFunction<void(const FElysiumEntity&)> VisualChangedHook;   // gizmo dirty seam (debug-only)

	// Brush bodies: the map actor owns them (they are its components); we hold weak refs to
	// gate them and to destroy them on teardown (the world logically owns the embodiments).
	TArray<TWeakObjectPtr<UElysiumBrushComponent>> Bodies;
	// This map's cooked per-entity collision, when it has any (R4.2). Weak: owned by
	// UElysiumMapCollision, read only while brush bodies are being built.
	TWeakObjectPtr<const UElysiumMapCollisionPayload> CollisionPayload;
	// NPC skeletal bodies (built on the map actor, gated by their leaf on dormancy): weak refs held
	// so a world rebuild on a surviving actor destroys them, like Bodies.
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> NpcBodies;
	// Dynamic-prop bodies (built on the map actor, gated/moved by their FElysiumProp leaf): weak
	// refs held so a world rebuild on a surviving actor destroys them, like NpcBodies.
	TArray<TWeakObjectPtr<UPrimitiveComponent>> PropBodies;
	// phys_hinge constraints (built on the map actor by the leaf's PostSpawn): weak refs held so
	// a world rebuild on a surviving actor destroys them, like PropBodies.
	TArray<TWeakObjectPtr<UPhysicsConstraintComponent>> Constraints;
	// UE may report the same overlap once from its movement update and once from the explicit
	// post-teleport reconciliation. Keep touch edges idempotent at the engine-neutral terminus;
	// the packed key is (brush index, activator index), sufficient within this world's epoch.
	TSet<uint64> ActiveTouches;
	int32 TouchBeginCount = 0;
	int32 TouchEndCount = 0;

	// This map's player entity, or Invalid when the map was built without one.
	FElysiumEntityHandle Player;
	// The active npc_VPlayerController stand-in, restored by finding the saved runtime entity after
	// snapshot application. It is never valid outside this map epoch.
	FElysiumEntityHandle PlayerControllerEntity;

	// Set by Detach(): this world no longer owns any part of the session, so Teardown neither
	// dehydrates the player nor freezes a snapshot over the one a load just restored.
	bool bDetached = false;

	// The omission baseline: each entity's state as the spawn pass left it, index-aligned
	// with EntityList. A freeze records only what has moved since, which is what makes a 2,600-entity
	// map a few kilobytes. Captured once at Load and never updated by a restore, because a rebuild
	// always starts from the spawn pass.
	TArray<FElysiumEntityState> Baseline;

	// The only retained player-interaction state. Candidate lists stay inside one query frame.
	FElysiumEntityHandle FocusedUsable;
	FElysiumUseContext FocusContext;
	TArray<EElysiumUseEdge, TInlineAllocator<2>> PendingUseEdges;
	TArray<EElysiumUseEdge, TInlineAllocator<2>> PendingFeedEdges;
	// The combat button field (retail's player `+0x2088`) and the copy the last weapon frame
	// consumed, which is what a press edge is measured against. Live input state, not simulation
	// state: a save restores the player without a button held, which is what a load actually looks
	// like.
	uint64 PlayerButtons = 0;
	uint64 ConsumedPlayerButtons = 0;
	// The last verdict the player weapon frame logged, as its raw enumerator. Diagnostics only, and
	// not saved: that frame runs while ANY weapon button is held, so a held trigger reports the same
	// resting verdict every frame — hundreds of lines a second that say nothing changed. The line is
	// worth having on an EDGE and worthless repeated, so this is what makes it one. `INDEX_NONE` is
	// "nothing logged yet". Typed as an int rather than `FElysiumWeapon::EVerdict` because the weapon
	// class is a `Substrate/` private header and this one is the module's public API surface.
	int32 LastLoggedWeaponVerdict = INDEX_NONE;
	struct FActiveUse
	{
		FElysiumUseContext Context;
		EElysiumUseSessionKind Kind = EElysiumUseSessionKind::None;
	};
	TOptional<FActiveUse> ActiveUse;
	EElysiumUseOutcome LastUseOutcome = EElysiumUseOutcome::NoTarget;

	struct FInteractionPrompt
	{
		FElysiumEntityHandle DisplayOwner;
		int32 Icon = 0;
		bool bLocked = false;
		bool bFadingIn = false;
		float StartAlpha = 0.0f;
		double TransitionTime = 0.0;
	};
	FInteractionPrompt InteractionPrompt;

	// env_fade screen-fade state (one at a time). GetScreenFade derives the current alpha from
	// NowSeconds() against StartTime, so no per-frame advance is needed; an expired fade simply
	// reports idle (bActive stays set but the phase math returns false past the last phase).
	struct FScreenFade
	{
		bool         bActive = false;
		FLinearColor Color = FLinearColor::Black;
		float        MaxAlpha = 1.0f;       // 0..1 (renderamt/255)
		float        Duration = 0.0f;       // seconds to cover (and to uncover again when bAutoReverse)
		float        HoldTime = 0.0f;       // seconds held at MaxAlpha once covered
		bool         bFadeIn = false;       // SF_FADE_IN: hold the colour flat, no ramp either way
		bool         bAutoReverse = false;  // SF_FADE_STAYOUT: uncover again once the hold expires
		double       StartTime = 0.0;

		// The save type (FElysiumSavedFade) is this struct's shape field for field; Freeze and
		// ApplySnapshot convert through these two so the pair cannot drift.
		FElysiumSavedFade ToSaved() const
		{
			FElysiumSavedFade Saved;
			Saved.bActive      = bActive;
			Saved.Color        = Color;
			Saved.MaxAlpha     = MaxAlpha;
			Saved.Duration     = Duration;
			Saved.HoldTime     = HoldTime;
			Saved.bFadeIn      = bFadeIn;
			Saved.bAutoReverse = bAutoReverse;
			Saved.StartTime    = StartTime;
			return Saved;
		}
		static FScreenFade FromSaved(const FElysiumSavedFade& Saved)
		{
			FScreenFade Fade;
			Fade.bActive      = Saved.bActive;
			Fade.Color        = Saved.Color;
			Fade.MaxAlpha     = Saved.MaxAlpha;
			Fade.Duration     = Saved.Duration;
			Fade.HoldTime     = Saved.HoldTime;
			Fade.bFadeIn      = Saved.bFadeIn;
			Fade.bAutoReverse = Saved.bAutoReverse;
			Fade.StartTime    = Saved.StartTime;
			return Fade;
		}
	};
	FScreenFade ScreenFade;

	// Open-sign state (one at a time). The panel content itself is parsed and cached on the
	// game_sign entity; the world only tracks which entity owns the screen and since when.
	// The scripted camera's handle on IElysiumEmbodiment's channel; 0 = none up.
	int32 ScriptedCameraShot = 0;
	FString ScriptedCameraFile;

	// The paired camera_track director. Values remain cached when one role restores so the surviving
	// role does not snap to the player between independently authored chains.
	int32 TrackCameraShot = 0;
	FElysiumEntityHandle TrackCameraPositionOwner;
	FElysiumEntityHandle TrackCameraTargetOwner;
	FVector TrackCameraPosition = FVector::ZeroVector;
	FVector TrackCameraTarget = FVector::ZeroVector;
	FRotator TrackCameraRotation = FRotator::ZeroRotator;
	float TrackCameraRoll = 0.0f;
	float TrackCameraFov = 0.0f;

	FElysiumEntityHandle OpenSignOwner;
	double OpenSignTime = 0.0;
	TSharedPtr<const FElysiumSignData> OpenSignData;   // incomplete here; freed in the .cpp
	float OpenSignFadeIn = 0.0f;

	// Open-dialogue state (one at a time). The conversation owns the branch cursor; the world
	// tracks which NPC it belongs to so ending it can fire that NPC's OnDialogEnd.
	TUniquePtr<FElysiumDialogueSession> DialogueSession;  // incomplete here; freed in the .cpp
	// End the open session: clear the slot and (unless bSilent) enqueue the owner's EndDialog input so
	// OnDialogEnd fires through the real chokepoint (the same seam the runner reuses).
	void EndDialogSession(bool bSilent);
	void BeginDialogueTurn();
	void UpdateDialogueAutomatic();
	void SelectDialogueCamera(bool bLineBoundary);
	void UpdateSelectedDialogueCamera();

	// --- The dialogue half of lipsync ---
	// A conversation turn has no authored timeline — the line simply starts when the turn opens — so
	// unlike a choreo scene there is no scene clock to ride and no per-event latch to key off. The
	// world holds the one open turn's join and drives it from its own tick.
	TSharedPtr<struct FElysiumLipSyncBinding> DialogueLipsync;   // incomplete here; freed in the .cpp
	// Substrate seconds at which the current turn's audio was submitted; negative when none.
	double DialogueLineStart = -1.0;
	// The controllers this turn last wrote, so the ones it stops driving go back to zero — the same
	// bookkeeping FElysiumChoreoScene keeps per actor.
	TMap<FString, float> DialogueFacialPose;
	// Whose face `DialogueFacialPose` is on. Held separately from OpenDialogOwner because the session
	// ends BEFORE the pose is released, and the release still has to find the speaker.
	FElysiumEntityHandle DialogueFaceOwner;

	// Join the turn's `.lip` to the speaker's phoneme table and start its clock. Replaces whatever
	// the previous turn left.
	void BeginDialogueLipsync(const FString& DlgSourcePath, int32 LineId);
	// Compose and push this frame's phoneme pose onto the speaking NPC.
	void RefreshDialogueLipsync(double Now);

	// Unknown target/input aggregation: log once per unique (target.Input), count the rest.
	TSet<FString> UnknownLogged;
	int32 UnknownTargetCount = 0;
	int32 UnknownInputCount = 0;
	int32 LoopGuardTripCount = 0;

	// Per-wire accounting. Sparse: a row appears the first time something reaches it, and the report
	// supplies the authored wires that never did. Cleared with the world.
	TMap<FElysiumWireRef, FElysiumWireTally> WireTally;

	// The last time this world was ticked. NowSeconds() reads it when there is no game state to
	// hold the clock.
	double LastTickNow = 0.0;
};
