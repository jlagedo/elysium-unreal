#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumEventQueue.h"
#include "ElysiumIOSink.h"
#include "ElysiumVariant.h"

struct FElysiumSignData;

class FElysiumDlgConversation;
class AActor;
class APawn;
class UElysiumAudioSubsystem;
class UElysiumBrushComponent;
class UElysiumGameStateSubsystem;
class UElysiumMapSubsystem;
class UPhysicsConstraintComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

// R1/R5 — the Track-B substrate: one plain-C++ object per map, owned by AElysiumMapActor, that
// dies with it. It parses `.ents` into live entities, indexes them by name and class, and routes
// every input delivery and every deferred output through the two chokepoints (AcceptInput and the
// event queue) with the debug sinks always installed. It is ticked once per frame with the game
// clock's `now`, think-first (retail order): run due thinks, then service the queue.
//
// Identity (R3) is generation-checked: each world instance takes a unique epoch, every handle it
// mints carries that epoch, and Resolve returns null for a stale-epoch, out-of-range, or dead
// handle — the "falsy when dead/stale" contract VtMB scripts rely on. Teardown bumps the epoch,
// invalidating all outstanding handles at once.
class FElysiumEntityWorld
{
public:
	FElysiumEntityWorld(AActor* InOwner, UElysiumGameStateSubsystem* InGameState);
	~FElysiumEntityWorld();

	FElysiumEntityWorld(const FElysiumEntityWorld&) = delete;
	FElysiumEntityWorld& operator=(const FElysiumEntityWorld&) = delete;

	// --- Lifecycle ---------------------------------------------------------------------
	// Build one entity per def via the registry (inert record when the classname is
	// unregistered), index names/classes, run the spawn pass (Spawn() on each).
	void Load(FElysiumEntityDefs&& InDefs);

	// Per-frame drive (map actor Tick). Think-first: RunThinks(Now) then ServiceEvents(Now).
	void Tick(double Now);

	// --- Chokepoints (R5) --------------------------------------------------------------
	// Deliver an input to a target: resolve `!self`/`!activator`, fan out over the name index,
	// walk each target's class-chain input table (case-folded), invoke the thunk, notify sinks.
	// Unknown target/input: notify (log-once) and keep going. The only input path in the game.
	void AcceptInput(const FString& Target, FName Input, const FElysiumVariant& Param,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller);
	// Fire a named output from an entity: for each matching def row whose `times` is not spent,
	// count it down and queue the delivery at now + delay (attaching field-6 Python). The only
	// way outputs become queue entries. `ValueOverride` (P4.5) is a Source COutput<T> runtime value:
	// it fills any wire whose map-authored param is empty; a Void override leaves the empty param.
	void FireOutput(FElysiumEntity& Source, FName OutputName, const FElysiumEntityHandle& Activator,
		const FElysiumVariant& ValueOverride = FElysiumVariant::Void());

	// Debug/console injection (P2.2 inspector fire buttons, P2.3 `ent_fire`): queue a hand-made
	// input delivery through the real event queue (chokepoint 2) at now + delay — the same code
	// path a game output takes, so manual tests are faithful, show up in the queue window, and are
	// single-steppable. Targeting one specific entity uses Target "!self" with Caller = its handle.
	void EnqueueInput(const FString& Target, FName Input, const FElysiumVariant& Param, double Delay,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller);

	// B3 — runtime entity creation (npc_maker.Spawn): synthesize a live entity from a def built at
	// runtime rather than parsed from the map. The def is stored (the entity holds Def*), the handle
	// index continues past the map's def array (Resolve indexes EntityList directly), name/class
	// indices are updated so the new entity is a live I/O target immediately, then Spawn() runs — a
	// leaf builds its body/visual there. Returns the new entity's handle (Invalid on a bad def).
	FElysiumEntityHandle SpawnRuntimeEntity(FElysiumEntityDef Def);

	// 9.3 — the scripted two-phase create (VtMB's CreateEntityNoSpawn / CallEntitySpawn). Phase 1
	// appends a live entity from a runtime def and indexes it by name/class WITHOUT running Spawn(),
	// so the script can SetModel/SetName/SetOrigin on it first; phase 2 runs Spawn() (once, gated by
	// bSpawnCalled) and builds its brush body. SpawnRuntimeEntity is the two fused (npc_maker's path).
	FElysiumEntityHandle CreateRuntimeEntityNoSpawn(FElysiumEntityDef Def);
	void CallEntitySpawn(FElysiumEntity& Ent);

	// 9.3 — VtMB's Entity.SetName: re-key the name index so the renamed entity is immediately findable
	// under its new targetname (and no longer under the old). Empty names are handled (add/remove skip).
	void RenameEntity(FElysiumEntity& Ent, const FString& NewName);

	// B3 — register an NPC skeletal body (built by AElysiumMapActor::BuildNpcVisual) so the world tears
	// it down with the map. The FElysiumNpc leaf calls this from Spawn(); mirrors how brush bodies are
	// tracked, so a world rebuild on a surviving actor (reload) does not leak the components.
	void RegisterNpcBody(USkeletalMeshComponent* Component);

	// 8.3 — register a dynamic-prop body (built by AElysiumMapActor::BuildPropVisual) so the world
	// tears it down with the map, exactly like NPC bodies. The FElysiumProp leaf calls this from Spawn().
	void RegisterPropBody(UStaticMeshComponent* Component);

	// 8.4 — register a physics constraint (built by a phys_hinge leaf) so the world tears it down
	// with the map, like the prop/NPC bodies. The FElysiumPhysHinge leaf calls this from PostSpawn().
	void RegisterConstraintBody(UPhysicsConstraintComponent* Component);

	// P5 5.4 — ScheduleTask(delay, "<source>"): defer a field-6 Python source string on the same
	// event queue, evaluated at now+delay through the installed script host (DeliverEvent's Python
	// half). No I/O target — it is a python-only event, exactly the shape a field-6-only output
	// produces — so it single-steps in the queue window and serializes into a save (R8). The source
	// resolves against the delivery's provenance (Caller = the scheduling entity, `!self`).
	void EnqueuePython(const FString& Source, double Delay,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller);

	// P5 5.4 — the logic_pythoncheck / condition path: evaluate an expression string through the
	// installed script host with the given provenance and return its value (Void when the source is
	// empty, there is no host/state, or the eval failed — error-to-false, so a gate over it reads
	// OnFalse). Goes through the host (not ElysiumExpr directly) so `elysium.script.live 0` disables
	// conditions in lockstep with field-6 for A/B, and the eval lands in the recent-eval debug log.
	FElysiumVariant EvalCondition(const FString& Source, const FElysiumEntityHandle& Self,
		const FElysiumEntityHandle& Activator);

	// Overlap routing (P1.5): a brush body's begin/end overlap lands here. Resolve the brush
	// entity, skip if inert (R6), and call its OnTouchStart/OnTouchEnd (P1.6 triggers override).
	void RouteBrushTouch(const FElysiumEntityHandle& Brush, const FElysiumEntityHandle& Activator, bool bBegin);

	// +use look-cursor (P4.2): the minimal reticle slice. UpdateUseCursor camera-ray-picks the
	// nearest usable, non-inert brush entity within arm's reach and arbitrates OnIn/OnOut as the aim
	// enters/leaves it (fired through the entities, so a StartHidden→ScriptUnhide arm gates it for
	// free); PlayerUse presses whatever the cursor is on. Driven from AElysiumMapActor::Tick and the
	// pawn's E key. The full use-only channel + use-icon HUD are P4.4; this lands activation + OnIn/
	// OnOut. Player is not an entity yet (P4-later), so the activator is Invalid, matching triggers.
	void UpdateUseCursor();
	void PlayerUse();
	FElysiumEntityHandle GetAimedUsable() const { return AimedUsable; }
	// The reticle icon index for the currently aimed usable (0 = nothing usable aimed at). The HUD
	// (P4.4) reads this each frame to pick the atlas cell; resolves locked -> locked_icon (GetUseIcon).
	int32 GetAimedUseIcon() const;

	// --- Screen fade (P4.5 env_fade) ---------------------------------------------------
	// A full-screen colour fade driven by env_fade's `Fade` input, advanced off the game clock and
	// drawn by AElysiumHUD (which polls GetScreenFade each frame). Held on the world so it dies with
	// the map. One active fade at a time — a new Fade replaces the running one; VtMB keeps a fade
	// *list*, but its colours sum and its alphas max, which is indistinguishable from one slot while
	// every fade on a map is the same colour (all of them are black on the tutorial).
	// `bFadeIn` = SF_FADE_IN, `bAutoReverse` = SF_FADE_STAYOUT — see GetScreenFade for the curve.
	void StartScreenFade(const FLinearColor& Color, float Duration, float HoldTime, float MaxAlpha,
		bool bFadeIn, bool bAutoReverse);
	// Fills OutColor (rgb = fade colour, a = current 0..1 alpha) and returns true while a fade is
	// visible; false when idle. Const — the HUD polls it; an expired fade reports idle.
	bool GetScreenFade(FLinearColor& OutColor) const;

	// --- Open sign window (P4.10 game_sign) --------------------------------------------
	// The one sign panel currently on screen, driven by game_sign's OpenWindow/CloseWindow and
	// drawn by AElysiumHUD (which polls GetOpenSign each frame). Same shape as the screen fade:
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
	void PlayerDismissSign();
	// The open sign's `fade_in` seconds (0 = appear instantly).
	float GetOpenSignFadeIn() const { return OpenSignFadeIn; }

	// --- Open dialogue (P9 9.1 / B4 `.dlg` conversation) --------------------------------
	// The one conversation currently on screen, driven by an NPC's StartPlayerDialogRemote and drawn
	// by the visual-novel Slate box (AElysiumHUD polls GetOpenDialog each frame — same held-on-the-
	// world lifetime as the sign/fade). The owning NPC's OnDialogEnd fires when it closes (the beat
	// machine's hinge — DialogPostProcess reads the `G` flags the dialogue's field-5 actions wrote).
	void OpenDialog(const FElysiumEntityHandle& Owner, TSharedRef<FElysiumDlgConversation> Conversation);
	// The live conversation, or null when none is open. What the dialogue box renders.
	FElysiumDlgConversation* GetOpenDialog() const { return OpenDialogConv.Get(); }
	// The NPC the open conversation belongs to (Invalid when none is open).
	FElysiumEntityHandle GetOpenDialogOwner() const { return OpenDialogOwner; }
	// Player picked the Nth visible PC choice: advance the branch machine; end the session (firing the
	// owner's OnDialogEnd) if the pick closed it. No-op when no conversation is open.
	void PlayerDialogChoose(int32 VisibleIndex);
	// Player advanced past a terminal NPC line (the "continue" affordance) — ends the session.
	void PlayerDialogAdvance();
	// Force-close the open conversation. bSilent suppresses OnDialogEnd (a Kill/teardown must not
	// resurrect the beat machine); a normal close fires it.
	void CloseDialog(bool bSilent = false);

	// The game-state subsystem (the `G`/quest store, player sheet, script host). Outlives the world.
	UElysiumGameStateSubsystem* GetGameState() const { return GameState; }

	// The player's pawn via the owning world's first controller, or null. The seam point_teleport /
	// trigger_hurt use to reach the player from the plain-C++ substrate (the player is not an entity).
	APawn* GetPlayerPawn() const;

	// Debug tap seam (P2.3 `ent_*`): install an extra I/O sink, owned by the world and torn down
	// with it. The ent_* debug subsystem taps the two chokepoints for its overlay/break tooling
	// through the same sink interface the ring buffer and log stream already use — no I/O side
	// channel (R5). Re-installed by the subsystem whenever a new world epoch appears.
	void AddSink(TUniquePtr<IElysiumIOSink> InSink);

	// Visual-change seam (P2.4 retained gizmo layer): a callback fired whenever an entity's
	// dormancy/liveness flips (from FElysiumEntity::OnDormancyChanged / Kill). It lets a retained
	// visualizer dirty just that one instance on the event instead of polling every entity every
	// frame. Optional (unset in normal play); the debug subsystem sets it per epoch. Called by the
	// entity through its World back-pointer.
	void SetVisualChangedHook(TFunction<void(const FElysiumEntity&)> Hook) { VisualChangedHook = MoveTemp(Hook); }
	void NotifyVisualChanged(const FElysiumEntity& Ent) const { if (VisualChangedHook) { VisualChangedHook(Ent); } }

	// --- Resolution / iteration --------------------------------------------------------
	FElysiumEntity* Resolve(const FElysiumEntityHandle& Handle);
	const FElysiumEntity* Resolve(const FElysiumEntityHandle& Handle) const;
	FElysiumEntity* FindByName(const FString& Name);   // first live match, or null
	// First live info_landmark with this targetname (the P4.6 landmark-transition anchor), or null.
	FElysiumEntity* FindLandmark(const FString& Name);
	void ForEachNamed(FName Name, TFunctionRef<void(FElysiumEntity&)> Fn);

	int32 NumEntities() const { return EntityList.Num(); }
	const TArray<TUniquePtr<FElysiumEntity>>& Entities() const { return EntityList; }
	const FElysiumEventQueue& Queue() const { return EventQueue; }
	FElysiumEventQueue& Queue() { return EventQueue; }
	const FElysiumRingBufferSink& RingBuffer() const { return *Ring; }
	uint32 GetEpoch() const { return Epoch; }
	AActor* GetOwnerActor() const { return Owner; }
	// The GameInstance audio subsystem (voice pool + decode cache), or null. The seam ambient_generic
	// and the SoundScheme manager use to reach playback from the plain-C++ substrate.
	UElysiumAudioSubsystem* AudioSubsystem() const;
	// The map-lifecycle subsystem (Travel + the P4.6 deferred landmark-transition queue), or null.
	// trigger_changelevel reaches it through here to request a travel to another map.
	UElysiumMapSubsystem* MapSubsystem() const;
	double NowSeconds() const;
	int32 UnknownTargets() const { return UnknownTargetCount; }
	int32 UnknownInputs() const { return UnknownInputCount; }
	int32 NumBrushBodies() const { return Bodies.Num(); }
	int32 TouchBegins() const { return TouchBeginCount; }
	int32 TouchEnds() const { return TouchEndCount; }

	// --- Formatting (used by the sinks; resolves handles to the canonical debug string) ----
	// `#<idx> <name>(<class>)` for a handle, or `#<null>` / `#<stale>` when it cannot resolve.
	FString DescribeHandle(const FElysiumEntityHandle& Handle) const;
	// `(t) <caller> -> <target>.Input(param)` plus a `[py]`/`[no target]`/`[no input]` note.
	FString FormatEventLine(double Now, const FElysiumIOEvent& Event, const FString& TargetLabel,
		const TCHAR* Note) const;

private:
	void Teardown();
	// Build the brush body (P1.5) for one entity, if it is a brush with hulls: cook the convex
	// UBodySetup from the def, place it at the def origin, attach it to the owner actor, store it
	// on the entity, and start it dormant when born hidden. Point/logic entities get no body (R1).
	void BuildBrushBody(FElysiumEntity& Ent);
	// The queue.Add wrapper: assigns time/serial upstream, notifies OnQueued.
	void AddEvent(FElysiumIOEvent&& Event);
	void ServiceEvents(double Now);
	void RunThinks(double Now);
	void DeliverEvent(const FElysiumIOEvent& Event, double Now);
	// Resolve a due event's target string to live entities (skips dead), honouring !self/!activator.
	void ResolveTargets(const FElysiumIOEvent& Event, TArray<FElysiumEntity*>& Out);

	AActor* Owner = nullptr;                          // for VLOG; not owned
	UElysiumGameStateSubsystem* GameState = nullptr;  // clock + script host; outlives the world
	uint32 Epoch = 0;

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
	TFunction<void(const FElysiumEntity&)> VisualChangedHook;   // P2.4 gizmo dirty seam (debug-only)

	// Brush bodies (P1.5): the map actor owns them (they are its components); we hold weak refs to
	// gate them and to destroy them on teardown (the world logically owns the embodiments).
	TArray<TWeakObjectPtr<UElysiumBrushComponent>> Bodies;
	// B3 NPC skeletal bodies (built on the map actor, gated by their leaf on dormancy): weak refs held
	// so a world rebuild on a surviving actor destroys them, like Bodies.
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> NpcBodies;
	// 8.3 dynamic-prop bodies (built on the map actor, gated/moved by their FElysiumProp leaf): weak
	// refs held so a world rebuild on a surviving actor destroys them, like NpcBodies.
	TArray<TWeakObjectPtr<UStaticMeshComponent>> PropBodies;
	// 8.4 phys_hinge constraints (built on the map actor by the leaf's PostSpawn): weak refs held so
	// a world rebuild on a surviving actor destroys them, like PropBodies.
	TArray<TWeakObjectPtr<UPhysicsConstraintComponent>> Constraints;
	int32 TouchBeginCount = 0;
	int32 TouchEndCount = 0;

	// The usable brush entity currently under the +use look-cursor (P4.2), or Invalid when the aim
	// is off every usable body / out of reach. OnIn/OnOut fire on the transitions of this handle.
	FElysiumEntityHandle AimedUsable;

	// P4.5 env_fade screen-fade state (one at a time). GetScreenFade derives the current alpha from
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
	};
	FScreenFade ScreenFade;

	// P4.10 open-sign state (one at a time). The panel content itself is parsed and cached on the
	// game_sign entity; the world only tracks which entity owns the screen and since when.
	FElysiumEntityHandle OpenSignOwner;
	double OpenSignTime = 0.0;
	TSharedPtr<const FElysiumSignData> OpenSignData;   // incomplete here; freed in the .cpp
	float OpenSignFadeIn = 0.0f;

	// 9.1 / B4 open-dialogue state (one at a time). The conversation owns the branch cursor; the world
	// tracks which NPC it belongs to so ending it can fire that NPC's OnDialogEnd.
	FElysiumEntityHandle OpenDialogOwner;
	TSharedPtr<FElysiumDlgConversation> OpenDialogConv;   // incomplete here; freed in the .cpp
	// End the open session: clear the slot and (unless bSilent) enqueue the owner's EndDialog input so
	// OnDialogEnd fires through the real chokepoint (the B3 seam the runner reuses).
	void EndDialogSession(bool bSilent);

	// Unknown target/input aggregation: log once per unique (target.Input), count the rest.
	TSet<FString> UnknownLogged;
	int32 UnknownTargetCount = 0;
	int32 UnknownInputCount = 0;
};
