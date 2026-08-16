#pragma once

#include "CoreMinimal.h"

#include "ElysiumDialogueCamera.h"
#include "ElysiumStanceTypes.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumDisposition.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcCombatSchedules.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScriptedCharacter.h"

struct FElysiumClanTemplate;
struct FElysiumDmg;
struct FElysiumSaveArchive;
struct FElysiumStatTable;

// ============================================================================================
// FElysiumNpc — the character leaf shared by every living `npc_*` classname. It stands a skeletal
// model at its origin, follows named patrols or interesting-place routes, and owns dialogue gates.
// ============================================================================================

class FElysiumNpc final : public FElysiumScriptedCharacter, public IElysiumScheduleRunner
{
public:
	bool  bUseInteresting = false;    // use_interesting — the NPC is a look/use target (seeded from the key)
	bool  bInDialog = false;          // a dialog session is open (OnDialogBegin fired, OnDialogEnd pending)
	int32 DialogFlags = 0;            // raw arg on ordinary/unforced; Remote ignores its variant
	int32 DecodedDialogFlags = 0;     // no bit is named until RE46 closes it
	EElysiumDialogOpenerKind DialogOpener = EElysiumDialogOpenerKind::Remote;
	FString DefaultCamera;            // definition-derived Tier-1 `default_camera`, never save state
	FString PlayerReaction;           // player_reaction — authored `D_* priority` seed
	FElysiumRelationships Relationships;
	int32 TimesTalked = 0;            // times_talked — dialogue interaction count (engine-written; script-read)

	// VtMB's disposition stance machine (`docs/vtmb/animation_and_movers.md`). The index and the
	// clock are retail's own saved pair; the two latches beside them are not saved, because retail's
	// datamap does not carry them either — a save taken mid-fidget restores as not fidgeting.
	FElysiumStanceState Stance;

	// Resolved once per (model, disposition) and re-resolved only when one of those two changes,
	// which is where retail resolves it: at model precache, with the fallback ladder baked in. The
	// row travels with the clips because they are two halves of the same table entry.
	FElysiumStanceClips StanceClips;
	FElysiumDisposition StanceTuning;
	FString StanceResolvedFor;        // "<stem>|<disposition>" the pair above was resolved for
	bool bStanceUnavailable = false;  // this model authors no stance set; do not ask again

	// The running schedule and the variant token its activity picks ride on.
	FElysiumScheduleState Schedule;
	int32 ScheduleActivityCycle = 0;

	// --- Cycle 7: the authored director's pushed order ------------------------------------------
	// What an `aiscripted_schedule` last pushed onto this NPC, live for exactly as long as the
	// program it started. Session state, not save state — the reasoning is on the struct.
	FElysiumScriptedScheduleOrder ScriptedScheduleOrder;

	// --- Cycle 6: the combat loadout ------------------------------------------------------------
	// `additionalequipment` (267 authored rows) and `alternateequipment` (184). The corpus authors
	// ONE classname per row, with the literal `0` as the "none" sentinel on 78 of them; the
	// resolution is `Substrate/ElysiumNpcLoadout.h`.
	FString AdditionalEquipment;
	FString AlternateEquipment;
	// `cantdropweapons` (78 authored rows; 71 write 0 and 7 write 1).
	//
	// SEAM (parsed, unread): the drop it suppresses is the death-time weapon drop, and this runtime
	// has no such path — `Event_Killed`'s weapon cleanup does not spawn a loose item yet. The
	// keyfield is carried so an authored NPC round-trips through a save with the policy it was
	// authored with, and so the drop path has a value to read the day it lands.
	bool bCantDropWeapons = false;
	// Whether the loadout has already run. It saves for the reason `FElysiumItemContainer`'s equip
	// seeds do: the weapon it granted is a runtime entity the snapshot restores, so a restored NPC
	// must not be handed a second one.
	bool bLoadoutResolved = false;

	// The melee selector's retained binary draw (`ElysiumNpcCombatSchedules.h`). Session state: it
	// is a decision in flight, not memory, and a load re-draws on the next pass.
	FElysiumNpcCombatSelector CombatSelector;

	// `m_bAllowAlertLookaround` (+0x6434), authored per NPC.
	bool bAllowAlertLookaround = false;
	// `m_bNoAlertState` (+0x65f6), authored per NPC. It skips the TROIKA layer's own damage and
	// sense promotions and nothing else — the base layer's tail call promotes anyway, which is why
	// this is not a suppression (`ElysiumNpcCond::SelectIdealState`).
	bool bNoAlertState = false;
	// `m_iEnemySightings` (+0x60a8). Incremented once per committed-enemy acquisition episode in
	// which the enemy is the player (`FElysiumNpcSenses::GatherEnemyLos`); it drives the recovered
	// alert-lookaround chance `min(30, (sightings+2)*5)`.
	int32 EnemySightings = 0;

	// The decision pass's gathered conditions and its once-latch diagnostics. Conditions are
	// session state by design (`ElysiumNpcConditions.h`); the memory they are derived from is what
	// a save carries.
	FElysiumNpcCognition Cognition;

	// The door-obstruction selector's own state. `m_hBlockedDoor` (+0x5d28) and `m_hCondHitByDoor`
	// (+0x5d2c) are the two obstruction sources this runtime can carry; `m_vSavePosition` (+0x5dd0)
	// is where the chosen one was standing when the schedules were picked.
	FElysiumEntityHandle BlockedDoor;
	double BlockedDoorExpiresAt = 0.0;
	FElysiumEntityHandle CondHitByDoor;
	bool bCondHitByDoor = false;
	FVector SavePosition = FVector::ZeroVector;

	// The stance index is what selects among a disposition's three idles, so it is this chain's
	// answer for the variant the animation layer asks for. Retail zero-initialises it, which is why
	// the first pose any body shows is `Stance_<Anim>_Idle_1` rather than an index-spread guess.
	virtual int32 IdleVariant() const override { return FMath::Clamp(Stance.Current, 0, ElysiumStance::Count - 1); }
	FString StatTemplate;             // stattemplate — the `npctemplate*.txt` stat block this NPC wears
	bool bFastFood = false;            // inherited General.FastFood — authored non-resistance to feeding
	// Inherited General.Kindred — the authored creature classification the soak table selects on.
	// An NPC with no resolved template falls back to the chain's clan-slot answer.
	bool bKindredTemplate = false;
	bool bHasKindredTemplate = false;
	// Inherited General.DamageFilter{Bashing,Lethal,Aggravated,Flame}. Authored as float
	// multipliers; absent means the template authors no filter for that family.
	float DamageFilters[4] = { 0.f, 0.f, 0.f, 0.f };
	bool  bHasDamageFilter[4] = { false, false, false, false };

	// --- Cycle 4: senses, perception tuning and memory ------------------------------------------
	// The three authored perception keyfields `InitPerceptionDistances` (`0x1028fb70`) reads, kept
	// exactly as authored; the RESOLVED pair lives on `Senses.Perception`.
	//
	// The defaults are retail's own zero-init for `npc_perception` and the recovered `-1.0`
	// sentinel for the two float channels. An NPC that authors none of the three is therefore
	// derived from perception 0, which is the faithful answer at that state — the FGD's authored
	// defaults are unrecovered, and 424 of the corpus's NPCs write all three.
	int32 AuthoredPerception = 0;
	float AuthoredVision = ElysiumNpcSense::DerivedSentinel;
	float AuthoredHearing = ElysiumNpcSense::DerivedSentinel;

	// The sensory transaction and everything it remembers, including the last damaging hit this
	// NPC took (`Senses.Memory.LastDamage*`, written by the typed commit below).
	FElysiumNpcSenses Senses;

	// ================== Cycle 10c — player-law witnessing (`ElysiumNpcWitness.h`) ==================
	// The four authored thresholds the two law lanes compare a player activity level against, and
	// `pl_investigate` beside them. 424 of the corpus's 426 NPC rows author all five. The default is
	// the authored-disable 6 and a negative authored value resolves to it; both are stated, with
	// their reasoning, at `ElysiumNpcWitness::DefaultThreshold` / `ResolveThreshold`.
	int32 PlCriminalFlee = ElysiumNpcWitness::DefaultThreshold;
	int32 PlCriminalAttack = ElysiumNpcWitness::DefaultThreshold;
	int32 PlSupernaturalFlee = ElysiumNpcWitness::DefaultThreshold;
	int32 PlSupernaturalAttack = ElysiumNpcWitness::DefaultThreshold;
	int32 PlInvestigate = ElysiumNpcWitness::DefaultThreshold;

	// SEAM (parsed, carried, unread): the investigation POLICY keyfields. `investigate_mode` (4 on
	// 378 rows) and `investigate_mode_combat` (4 on 375) are mode NUMBERS whose meanings are
	// unrecovered, and `full_investigate` is an unrecovered policy beside them. The 32-schedule
	// `INVESTIGAT` family they would select among is undecoded too, so there is nothing for a mode
	// to choose. They are carried as authored integers so an NPC round-trips through a save with the
	// policy it was authored with, and so the selector has values to read the day one is decoded.
	int32 InvestigateMode = 0;
	int32 InvestigateModeCombat = 0;
	int32 FullInvestigate = 0;

	// The retained witness block: independent criminal and supernatural processed counts, witnessed
	// levels/locations/offenders, the flee-only policy and the three ignore deadlines.
	FElysiumNpcWitness Witness;
	// ==============================================================================================

	FString InterestingPlaceGroups;   // authored group allowlist; prevents cross-district wandering
	FString PatrolType;               // raw SetupPatrolType contract (kept for save/debug and later modes)
	FString PatrolPath;               // authored space-separated info_node_patrol_point names
	int32 PatrolIndex = 0;            // next point in the looping authored sequence
	enum class EAmbientPhase : uint8 { None, Moving, Into, Dwelling, Out };
	// The sheet, the WillTalk latch, `default_disposition`, the skeletal body and everything that
	// plays a clip on it now come from the chain (11.4): FElysiumCombatCharacter over
	// FElysiumAnimating, which is where VtMB puts them. This leaf is the dialogue half.
	virtual bool ResistsFeeding() const override;

	void ApplyResolvedTemplate(const FElysiumClanTemplate& Resolved,
		const FElysiumStatTable* Table);

	// The authored creature classification wins over the clan slot: an `npctemplate` human carries
	// `Clan None` but a Sabbat vampire template carries `Clan Brujah` AND `Kindred 1`, and only the
	// key distinguishes a ghoul or a Sabbat thug from the clan it is descended from.
	virtual bool IsKindred() const override;

	virtual bool GetTemplateDamageFilter(EElysiumDmgFamily Family, bool bFlame,
		float& OutFilter) const override;

	// Retail's NPC override saves the complete incoming damage packet before composing the base
	// transaction, and a surviving positive hit remembers its attacker. This is that record; the
	// schedule/senses consumers that read it arrive with the combat AI.
	virtual void OnDamageCommitted(const FElysiumDmg& Dmg) override;

	void InputUseInteresting(const FElysiumInputArgs& Args);

	void InputTeleportToEntity(const FElysiumInputArgs& Args);

	void SeedPlayerRelationship();

	void InputSetRelationship(const FElysiumInputArgs& Args);

	/**
	 * `ChangeSchedule` / `StartSchedule` — the two policy-level commands that name a native schedule
	 * (`docs/vtmb/npc-ai-reverse-engineering.md` -> "Direct schedule changes"). One handler serves
	 * both: they are "policy-level commands: the named schedule still executes normal tasks,
	 * failures, interrupts, motor work, and activity translation", and nothing recovered states a
	 * difference between them.
	 *
	 * CHOSEN, NOT RECOVERED: that they are the same operation. The survey names both in one sentence
	 * and decodes neither body. `Args.Input` distinguishes them in every diagnostic, so the day one
	 * is decoded the split is a branch rather than a new door.
	 */
	void InputNamedSchedule(const FElysiumInputArgs& Args);

	/**
	 * The one door an `aiscripted_schedule` pushes through.
	 *
	 * Order of operations is the recovered entity's: the forced state is the policy and is applied
	 * first, then the mode decides what else happens — mode 3 commits the goal as this NPC's enemy
	 * through the ordinary `SetEnemy` transaction, and the four movement modes start their program
	 * under the `ScriptedSchedule` body owner. Returns whether anything was pushed.
	 */
	bool BeginScriptedSchedule(const FElysiumScriptedScheduleOrder& Order, bool bHasForcedState,
		EElysiumNpcState ForcedState);

	// Drop a pushed order and give its body back. Reached from program completion, program failure,
	// dormancy and death; idempotent, so every one of those may call it.
	void EndScriptedSchedule(const TCHAR* Reason);

	void InputSetupPatrolType(const FElysiumInputArgs& Args);

	void InputFollowPatrolPath(const FElysiumInputArgs& Args);

	void InputClearPatrolPath(const FElysiumInputArgs&);

	// One name out of a `FollowPatrolPath` list.
	//
	// A patrol point is NOT addressed by targetname. Every `info_node_patrol_point` the maps author
	// ships with an empty targetname and carries its name in a `Group` keyvalue — 34 of 34 on
	// `sm_hub_1`, which is what the level script's `FollowPatrolPath("s1 s2 s3 ...")` names. The
	// targetname path is kept ahead of it because it costs nothing and is what a hand-built fixture
	// uses.
	const FElysiumEntity* FindPatrolPoint(const FString& Name) const;

	bool ResolvePatrolPoints();

	bool IssuePatrolMove();

	bool StartWalkingAnimation(bool bRunning = false);

	// The ordinary NPC adds body arbitration around the shared scripted motor. The scene-owned
	// player duplicate deliberately takes the default no-mind claim on the same movement rules.
	//
	// This is the one door to `EElysiumBodyOwner::Sequence`. The owning beat and its movement motor
	// both come through it, and the mind is idempotent for the owner it already holds, so the two
	// share the one token in `SequenceOwner` rather than competing for it.
	bool AcquireSequenceBody(const TCHAR* Reason);

	// Give the token back. Only called once neither the beat nor the motor is still holding it.
	void ReleaseSequenceBody(const TCHAR* Reason);

	virtual bool ClaimScriptMove() override;

	virtual void ReleaseScriptMove(const TCHAR* Reason) override;

	virtual bool ClaimScriptBody(const TCHAR* Reason) override;

	virtual void ReleaseScriptBody(const TCHAR* Reason) override;

	// An open conversation owns this body as surely as a beat does, so it refuses a feed (B6).
	virtual bool IsFeedBusy() const override;

	virtual void Think() override;

	/**
	 * `CAI_BaseNPCTroika::SelectSchedule` case 1, in recovered priority order
	 * (`docs/vtmb/npc-ai-reverse-engineering.md` -> "The idle branch, decided"). First match wins.
	 *
	 * The steps this runtime cannot answer refuse by name rather than guessing, and the refusal
	 * records what would settle it -- a refusal that says nothing is indistinguishable from a step
	 * that silently did not apply.
	 */
	EElysiumScheduleId SelectIdleSchedule();

	// The state switch of the base selector (`0x1028a380`): case 1 idle, case 3 alert, case 2
	// combat. Everything else keeps the idle branch, which is where a state with no selector of its
	// own belongs.
	EElysiumScheduleId SelectSchedule();

	// Case 3. The recovered alert branch's own damage reactions are refused by name; what remains
	// is the lookaround program, which alert state is what makes reachable.
	EElysiumScheduleId SelectAlertSchedule();

	/**
	 * Case 2 — the concrete combat branch.
	 *
	 * `CNPC_VHuman::SelectSchedule` (`0x10384ee0`) queries the active weapon's capability bits and
	 * routes to one of two selectors; either may return zero, and a zero falls through to
	 * `CAI_BaseNPCTroika::SelectSchedule` so the weapon policy COMPOSES with the door, damage and
	 * idle reactions rather than replacing them. That composition is the tail of this function.
	 */
	EElysiumScheduleId SelectCombatSchedule();

	// `SelectIdealState` run for real: the two-layer rule over this pass's conditions, committed
	// through the mind's ordinary transition path.
	void UpdateIdealState(double Now);

	// The standing-pose arm, reached from both the idle fall-through and the dialogue arm.
	void ThinkStanceOrIdle(double Now);

	void ThinkPatrol();

	FElysiumInterestingPlace* CurrentAmbientSpot() const;

	const FElysiumInterestingPlaceType* AmbientType(const FElysiumInterestingPlace* Spot) const;

	FElysiumInterestingPlace* ClaimAmbientSpot();

	bool AcceptsAmbientGroup(int32 GroupId);

	bool PlayAmbientActivity(const TArray<FElysiumWeightedName>& Choices, bool bLoop,
		double Now, double& OutEnd);

	// --- the disposition stance machine ---------------------------------------------------------
	// `docs/vtmb/animation_and_movers.md` -> "The disposition stance machine". The decision itself is
	// `ElysiumStance::Select`, a pure rule over a resolved clip table and a tuning row; everything
	// here is the resolution and the cadence around it.

	// Bring `StanceClips`/`StanceTuning` up to date for the current model and disposition. Returns
	// false for a body that authors no stance set at all, which is an ordinary answer -- the monsters
	// and one-off models idle off ACT_IDLE instead, and the caller falls back to that.
	bool EnsureStanceResolved();

	// A disposition change re-keys the whole table: a new row means a new `AnimName`, so the clips
	// and the tuning both move. The stance *index* deliberately survives it -- retail's datamap
	// carries `m_CurrStance` across the change and never resets it.
	virtual bool SetDisposition(const FString& NewDisposition, int32 NewLevel) override;

	// --- IElysiumScheduleRunner: the task bodies -------------------------------------------------

	// `TASK_SPECIAL_IDLE_ACTIVITY`. One stance selection played on the body; the schedule holds the
	// task open for the clip's own length, which is retail's cadence -- the idle task re-requests
	// `ACT_DISPOSITION` only once the current sequence has finished, so nothing re-enters mid-clip.
	virtual float RunSpecialIdleActivity(double Now) override;

	virtual bool IsBodyVisible() const override;

	virtual float PlayActivity(const FString& Activity) override;

	virtual float RandomSeconds(float Max) override;

	virtual void RecordScheduleEvent(const FString& Row) override;

	virtual bool FaceSavePosition() override;

	virtual bool StepAwayFromSavePosition(float DistanceCm) override;

	// --- The combat task bodies -----------------------------------------------------------------
	// Every one that drives the body claims `EElysiumBodyOwner::Schedule` through the arbiter first
	// and answers false when the claim is refused, which fails its task by name. The token is given
	// back once, where the program ends (`ReleaseScheduleBody`).

	virtual void StopMoving() override;

	virtual bool GetPathToEnemy(float ToleranceUnits) override;

	virtual void RunPath() override;

	virtual EElysiumMoveWatch WaitForMovement() override;

	virtual bool FaceEnemy() override;

	virtual bool AnnounceAttack(float Param) override;

	virtual bool MeleeAttack1() override;

	virtual bool RangeAttack1() override;

	virtual void RememberFact(float What) override;

	virtual bool GetPathToScriptedGoal() override;

	// The combat schedules' movement claim. Idempotent for a token already held, and refused while
	// another owner has the body — which is what a task turns into its own named failure. A patrol
	// route in progress is SUSPENDED by the claim rather than lost, so the release below resumes it.
	bool AcquireScheduleBody(const TCHAR* Reason);

	// Hand it back and stop whatever the program had the body doing. Called wherever a schedule
	// stops running: the end of a tick that ended the program, a state change that discarded it,
	// dormancy, and a restore.
	void ReleaseScheduleBody(const TCHAR* Reason);

	// The scripted director's movement claim, and its release. Same shape as the pair above, one
	// rank higher in the arbiter.
	bool AcquireScriptedScheduleBody(const TCHAR* Reason);

	void ReleaseScriptedScheduleBody(const TCHAR* Reason);

	/**
	 * `CAI_BaseNPCTroika::SelectDoorObstructionSchedule` (`0x102b7370`), transcribed.
	 *
	 * Returns `None` when this policy declines -- which is every call today, because neither
	 * obstruction source has a producer yet: nothing sets `m_hBlockedDoor` (a door blocking this
	 * NPC's path) and nothing sets `COND_HIT_BY_DOOR`. The decision itself is complete and is what
	 * those producers will feed; the third recovered source is gated on `COND_ENEMY_UNREACHABLE`,
	 * which needs a reachability query this runtime's motor seam does not carry.
	 */
	EElysiumScheduleId SelectDoorObstructionSchedule();

	void BeginAmbientUse(FElysiumInterestingPlace& Spot, double Now);

	void BeginAmbientLeave(double Now);

	void FinishAmbientUse(bool bFireLeft);

	void ThinkAmbient();

	// StartPlayerDialogRemote opens a dialog session: fire OnDialogBegin, then run the NPC's `.dlg`
	// conversation (B4). When the `dialogname` file is missing/unloadable the session falls back to the
	// B3 seam — it waits for a manual EndDialog (ent_fire), so the beat is still driveable by hand.
	virtual FElysiumBodyOwnerToken BeginDialogueBodySession() override;

	virtual void EndDialogueBodySession(const FElysiumBodyOwnerToken& Token, bool bSilent) override;

	void BeginDialog(EElysiumDialogOpenerKind Opener, int32 RawFlags,
		const FElysiumInputArgs& Args);

	// K1: each Tier-1 name enters through its own handler. Only after that handler has applied the
	// recovered parameter posture does it join the shared dialogue-session primitive above.
	void InputStartPlayerDialog(const FElysiumInputArgs& Args);

	void InputStartPlayerDialogRemote(const FElysiumInputArgs& Args);

	void InputStartPlayerDialogUnforced(const FElysiumInputArgs& Args);

	// The dialog session ends: increment times_talked and fire OnDialogEnd. Reached both by the runner
	// (World::EndDialogSession routes EndDialog to `!self` when the conversation closes) and by a manual
	// ent_fire. Jack's OnDialogEnd wires DialogPostProcess(), which reads the `G` flags the dialogue's
	// field-5 actions wrote and warps the player.
	void InputEndDialog(const FElysiumInputArgs& Args);

	// Load this NPC's `dialogname` `.dlg`, open a branch conversation bound to the installed script host,
	// and hand it to the world (the visual-novel box renders it; the runner fires EndDialog on close).
	// Returns false when there is no dialogue to run, leaving bInDialog latched for the B3 manual seam.
	bool OpenConversation(const FElysiumEntityHandle& Activator, EElysiumDialogOpenerKind Opener);

	// The sheet, from `stats.txt`'s defaults overlaid with this NPC's `stattemplate`. That overlay
	// is the whole of an NPC's health track: `npctemplate*` authors `Max_Health` as a literal, and a
	// template that omits it inherits `stats.txt`'s `Default 100` (`docs/vtmb/vdata-catalog.md`). Without it
	// every NPC had a zero ceiling and TakeDamage only logged.
	void SeedSheet();

	virtual void Spawn() override;

	virtual void Activate() override;

	// SetModel: swap the NPC's appearance (bradbury Heather goth/normal, cemetery prostitute,
	// downtown Nines). The rebuild is FElysiumAnimating's; the A/B gate is this leaf's.
	virtual void OnRuntimeModelChanged() override;

	virtual void OnDormancyChanged() override;

	virtual void Serialize(FElysiumSaveArchive& Ar) override;

	virtual const TCHAR* SaveBlockReason() const override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

	virtual FElysiumNpc* AsNpc() override { return this; }

private:
	FElysiumNpcMind Mind;
	FElysiumBodyOwnerToken PatrolOwner;
	FElysiumBodyOwnerToken AmbientOwner;
	FElysiumBodyOwnerToken ScheduleOwner;
	FElysiumBodyOwnerToken ScriptedScheduleOwner;
	FElysiumBodyOwnerToken SequenceOwner;
	FElysiumBodyOwnerToken DialogueBodyOwner;
	TArray<FString> PatrolNames;
	TArray<FVector> PatrolPoints;
	bool bPatrolActive = false;
	// A scripted beat has taken this NPC and has not given it back, and whether the arbiter claim
	// behind that request is in hand. The two differ only while a claim is deferred: the beat-queue
	// lock is stamped synchronously, the arbiter claim can arrive a think later.
	bool bScriptBodyRequested = false;
	bool bScriptBodyHeld = false;
	bool bMoveIssued = false;
	bool bWalkingAnimation = false;
	EAmbientPhase AmbientPhase = EAmbientPhase::None;
	int32 CurrentSpotIndex = INDEX_NONE;
	double AmbientLeaveAt = 0.0;
	double AmbientNextActivityAt = 0.0;
	int32 AmbientActivityCycle = 0;
	bool bAmbientArrived = false;
	TSet<int32> FailedSpotIndices;
	TSet<int32> AmbientGroups;
	bool bAmbientGroupsParsed = false;
};

// ============================================================================================
// npc_VPlayerController — the scene-owned duplicate of the player. It shares only the authored
// scripted-sequence motor with ordinary NPCs: no dialogue, AI, use body, or autonomous think.
//
// It lives beside FElysiumNpc rather than in its own file because the two share exactly one thing:
// the `elysium.NpcBodies` A/B, which is a file-static in `ElysiumNpc.cpp`.
// ============================================================================================

class FElysiumPlayerControllerNpc final : public FElysiumScriptedCharacter
{
public:
	virtual void Spawn() override;

	virtual void OnRuntimeModelChanged() override;

	virtual void SetIgnoreCharacterCollision(bool) override;

	virtual void OnDormancyChanged() override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	void BuildControllerMotor();
};
