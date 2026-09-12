#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumDialogueCamera.h"
#include "ElysiumStanceTypes.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumDisposition.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcCombatSchedules.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScriptedCharacter.h"

struct FElysiumClanTemplate;
struct FElysiumDmg;
struct FElysiumSaveArchive;
struct FElysiumStatTable;
class FElysiumPlayer;

// The character leaf shared by every living `npc_*` classname. It stands a skeletal model at its
// origin, follows named patrols or interesting-place routes, and owns dialogue gates.

class FElysiumNpc final : public FElysiumScriptedCharacter, public IElysiumScheduleRunner
{
public:
	// The `WillTalk` latch (`FElysiumCombatCharacter::bWillTalk`, retail virtual `+0x49c`) SHIPS
	// SET on a character, so `WillTalk 0` is a disabler and `WillTalk 1` a re-enabler.
	//
	// INFERRED, not read off an initializer: `vampire.dll` has no constructor write and no keyfield
	// writer for the byte — `InputWillTalk` (`0x103418f0`) is its only writer — and `sp_tutorial_1`
	// fires `Jack,WillTalk 0` at map load (`docs/vtmb/game_runtime.md` ~line 1114). A load-time
	// disable is meaningless against a false default, and with one the 79 authored calls would be
	// the only way any character could ever be talked to, leaving `+use` dead on every NPC no
	// script cues. The real initializer remains unrecovered.
	FElysiumNpc() { bWillTalk = true; }

	bool  bUseInteresting = false;    // use_interesting — the NPC is a look/use target (seeded from the key)
	// The dialogue and use-to-talk state. `Substrate/ElysiumNpcDialogue.h`.
	FElysiumNpcDialogue Dialogue;
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
	// The resolved (bank, label) pair `TASK_SET_ACTIVITY` most recently made ideal. It is session
	// state: schedule restore restarts at task zero because neither the current body pose nor the
	// watchdog survives a load. The body phase, not this record, is the current sequence authority.
	FElysiumClipIdentity ScheduleIdealActivity;

	// `m_bfAINPCFlags` / `m_bfAINPCFlags2` and the obliviousness refcount. Written by
	// `TASK_SET_NPC_FLAG` / `TASK_MAKE_OBLIVIOUS` and released by every schedule install; saved,
	// because retail's are datamap members and an NPC left mesmerized across a save must not wake up
	// conversable.
	FElysiumNpcFlags NpcFlags;

	// --- The authored director's pushed order ---
	// What an `aiscripted_schedule` last pushed onto this NPC, live for exactly as long as the
	// program it started. Session state, not save state — the reasoning is on the struct.
	FElysiumScriptedScheduleOrder ScriptedScheduleOrder;

	// --- Combat loadout ---
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
	// `m_bInvincible` (+0x63d8), authored per NPC. `CNPC_VVampire::OnTakeDamage` (`0x102bed30`)
	// tests it first and returns immediately, so it is a total refusal rather than a soak: the
	// Sheriff authors it in `sp_tutorial_1` and no-sells the same scalar `TakeDamage 100` that kills
	// the Sabbat beside him.
	bool bInvincible = false;
	// `m_iEnemySightings` (+0x60a8). Retail has exactly two writers, both in the sense pass and
	// neither in the committed-enemy LOS edge: Troika `OnLooked` (`FUN_102b39a0`, slot 469) adds one
	// when `COND_NEW_ENEMY` stands after the base call, and the outer-band see-unknown path
	// (`FUN_102b3e00` @ `0x102b3e90`) adds one when `m_hBestSeeUnknown` takes a NEW candidate.
	// There is no player-only rule. `FElysiumNpcSenses::TickSight` reproduces both.
	// It drives the recovered alert-lookaround chance `min(30, (sightings+2)*5)`.
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
	// Inherited General.Disallow_Knockbacks — the authored refusal eight template files set on the
	// zombies, the cabbie, the crackhouse and tutorial casts and the bomberman. It is the one
	// recovered half of the knockback's template eligibility (`Substrate/ElysiumReactions.h`).
	bool bDisallowKnockbacks = false;
	// Inherited General.DamageFilter{Bashing,Lethal,Aggravated,Flame}. Authored as float
	// multipliers; absent means the template authors no filter for that family.
	float DamageFilters[4] = { 0.f, 0.f, 0.f, 0.f };
	bool  bHasDamageFilter[4] = { false, false, false, false };
	// The resolved template's authored `BloodPool` — the full pool this critter stands up with,
	// latched at seed because the live slot is drained by feeding. Retail re-reads the immutable
	// template record instead (`GetCharTemplate` -> `template+0xd0` `+0x30`); the value is the same
	// and the lookup is not repeated per frame. 0 when the template authors none.
	int32 TemplateBloodPoolValue = 0;

	// --- Senses, perception tuning and memory ---
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
	// CAI_Memory is the observed-actor admission store. This stays distinct from
	// `Senses.Memory.Enemy`, the committed sticky enemy selected from it.
	FElysiumNpcEnemyMemory EnemyMemory;

	// --- Player-law witnessing (`ElysiumNpcWitness.h`) ---
	// The four authored thresholds the two law lanes compare a player activity level against, and
	// `pl_investigate` beside them. 424 of the corpus's 426 NPC rows author all five. The default is
	// the authored-disable 6 and a negative authored value resolves to it; both are stated, with
	// their reasoning, at `ElysiumNpcWitness::DefaultThreshold` / `ResolveThreshold`.
	int32 PlCriminalFlee = ElysiumNpcWitness::DefaultThreshold;
	int32 PlCriminalAttack = ElysiumNpcWitness::DefaultThreshold;
	int32 PlSupernaturalFlee = ElysiumNpcWitness::DefaultThreshold;
	int32 PlSupernaturalAttack = ElysiumNpcWitness::DefaultThreshold;
	int32 PlInvestigate = ElysiumNpcWitness::DefaultThreshold;

	// The investigation POLICY keyfields. `investigate_mode` (4 on 378 rows) and
	// `investigate_mode_combat` (4 on 375) are DECODED: they are `EElysiumInvestigateMode`, the two
	// operands of the interest predicate `0x102b3270` (`ElysiumNpcCond::ShouldInvestigate`), the
	// second selected by the predicate's `bCombatMode` argument. Carried as the authored integers
	// so an unrecognised value reaches the predicate's own refusal, as it does in retail.
	//
	// `full_investigate` (`m_bFullInvestigate`, `+0x6340`) is read by the see-unknown sweep's
	// one-shot `ATTACK_UNKNOWN`/`IGNORE_UNKNOWN` roll, which is not built yet.
	int32 InvestigateMode = 0;
	int32 InvestigateModeCombat = 0;
	int32 FullInvestigate = 0;

	// The retained witness block: independent criminal and supernatural processed counts, witnessed
	// levels/locations/offenders, the flee-only policy and the three ignore deadlines.
	FElysiumNpcWitness Witness;

	FString InterestingPlaceGroups;   // authored group allowlist; prevents cross-district wandering
	FString PatrolType;               // raw SetupPatrolType contract (kept for save/debug and later modes)
	FString PatrolPath;               // authored space-separated info_node_patrol_point names
	int32 PatrolIndex = 0;            // next point in the looping authored sequence
	// The sheet, the WillTalk latch, `default_disposition`, the skeletal body and everything that
	// plays a clip on it come from the chain: FElysiumCombatCharacter over
	// FElysiumAnimating, which is where VtMB puts them. This leaf is the dialogue half.
	virtual bool ResistsFeeding() const override;

	// `EquipRules` is the `ExcludedEquipTables` block of `system/items.txt`: the template authors
	// `Excluded_Equipment` by NAME, and slot 31 has to hold the row id the wield rule
	// (`Inventory_Can_Wield`, 0x10335a70) selects with. Null leaves the seeded default (row 0).
	void ApplyResolvedTemplate(const FElysiumClanTemplate& Resolved,
		const FElysiumStatTable* Table,
		const struct FElysiumExcludedEquipTable* EquipRules = nullptr);

	// The authored creature classification wins over the clan slot: an `npctemplate` human carries
	// `Clan None` but a Sabbat vampire template carries `Clan Brujah` AND `Kindred 1`, and only the
	// key distinguishes a ghoul or a Sabbat thug from the clan it is descended from.
	virtual bool IsKindred() const override;

	virtual int32 TemplateBloodPool() const override { return TemplateBloodPoolValue; }

	virtual bool GetTemplateDamageFilter(EElysiumDmgFamily Family, bool bFlame,
		float& OutFilter) const override;

	virtual bool DisallowsKnockbacks() const override { return bDisallowKnockbacks; }

	// Retail's slot-400 class bypass. Exactly one class in the game overrides the stub, and this is
	// the leaf that knows its own registered classname, so the test is the classname and nothing
	// else — no template key, no authored flag, no per-body state
	// (`docs/vtmb/combat-and-damage.md` -> "Who may be knocked back").
	virtual bool BypassesKnockbackEligibility() const override;

	// Retail's NPC override saves the complete incoming damage packet before composing the base
	// transaction, and a surviving positive hit remembers its attacker. This is that record; the
	// schedule/senses consumers that read it arrive with the combat AI.
	virtual void OnDamageCommitted(const FElysiumDmg& Dmg) override;

	// The authored `invincible` refusal, tested before anything else damage-side.
	virtual bool RejectsAllDamage() const override { return bInvincible; }

	/**
	 * `CAI_BaseNPC::Event_Killed`'s NPC override (`0x10265ad0`) plus the Troika one over it
	 * (`0x102bf340`) — the whole death transaction, run once (`docs/vtmb/combat-and-damage.md` ->
	 * "NPC and player death transaction").
	 *
	 * The shared body's output, owner notification and log are the base's and stay there. What this
	 * adds is everything the recovered override does with the BODY and the MIND: every animation
	 * channel claim goes back, every body-owner token is vacated, current and ideal state become
	 * dead, the body is frozen where it stands and stops answering the character channel, and the
	 * death schedule starts. Nothing after this selects, senses or attacks.
	 *
	 * A duplicate kill is ignored, which is retail's own first clause — the base's `bDeathReported`
	 * latch is the same guard reached through one door.
	 */
	virtual void OnKilled() override;

	/**
	 * `CAI_BaseNPC::HandleAnimEvent` (`0x10274e30`) — the FOOTSTEP arm of it, and nothing else yet.
	 *
	 * 2050/2051 are the walk footfall ("normal", `0x1026d460(this, 0)`) and 2052/2053 the run one
	 * ("heavy", mode 1). Every other id in retail's switch — 1003, 2021/2022, 2040, 2070/2071 and
	 * 4150-4155 — is NOT claimed here yet and falls to `FElysiumCombatCharacter::HandleAnimEvent`,
	 * which is the weapon forward, the ornament/feed/4020 band, and then the census.
	 *
	 * Every footstep arm answers **claimed**, including the muted and the silent ones: retail's
	 * handler `return`s after each case rather than falling through to `CBaseAnimating`, so an id it
	 * decided to make no sound for still has a handler. That distinction is what keeps the anim-event
	 * census a work list (`docs/vtmb/animation_events.md` -> "Port status - NPC footstep band").
	 */
	virtual bool HandleAnimEvent(const struct FElysiumAnimEvent& Event) override;

	/**
	 * The species seam: retail's five `HandleAnimEvent` overrides that replace `0x1026d460` outright
	 * (`docs/vtmb/footsteps.md` §1.7). True means this NPC's own class handled the footfall and the
	 * shared chain must not run.
	 *
	 * **The policy table IS the seam, not a virtual.** Retail's five species are distinct C++
	 * classes each holding its own `HandleAnimEvent` slot; this port stands ONE leaf for every
	 * `npc_V*` classname, so the overrides arrive as the classname-keyed data table in
	 * `ElysiumFootsteps.h`. A `virtual` here would be unoverridable — `FElysiumNpc` is `final` — and
	 * would read as an extension point that does not exist. The day a species genuinely needs a leaf
	 * of its own, that leaf takes the `final` off and this becomes virtual in the same commit.
	 */
	bool OverrideFootstep(int32 EventId, bool bHeavy);

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

	// --- The named-schedule door, shared ---
	/**
	 * Assign a NAMED native schedule to this NPC and run it through the ordinary kernel.
	 *
	 * One door, two producers: the script input, and the Discipline runtime's `HitInfo.AI_Schedule`
	 * channel, which is how AI schedule assignment reaches the kernel. A `disciplinetgt` record
	 * names a schedule the victim is to run, which is the same operation a script's
	 * `ChangeSchedule` performs and must not become a second one.
	 *
	 * Resolution is `ElysiumScheduleIdFromName` and nothing else, so only a REGISTERED program
	 * starts; an unregistered name funnels to the stub surface keyed on the name and returns false.
	 * That is the correct posture and not a gap to paper over — starting some other schedule under
	 * an authored name would be behaviour invented out of a string.
	 *
	 * `Surface` is the stub key (`CAI_BaseNPC.ChangeSchedule`, `DisciplineTgt.<record>/<hit>`) and
	 * `Detail` the marshalled context that key's report carries. Returns whether a program started.
	 */
	bool StartNamedSchedule(const FString& Requested, const FString& Surface, const FString& Detail);

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

	// An open conversation owns this body as surely as a beat does, so it refuses a feed.
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

	// --- `OnStateChange`, vtable slot 463 -------------------------------------------------------
	//
	// Retail's slot 463 is called on the state EDGE and takes the new state as its second argument.
	// Most of the cast fill it with `CAI_BaseNPCTroika::OnStateChange` (0x102ae140), which does not
	// touch the weapon at all — `CNPC_VVampire`, `CNPC_VHuman`, `CNPC_VPedestrian` and 40-odd others.
	// Three bodies DO, and they are the same code:
	//
	//   * `CNPC_VGuard1::vfunc463`            (0x1037d020)
	//   * `CNPC_VHunter::vfunc463`            (0x10388880)
	//   * `CNPC_VGhoulCroucher::FUN_103871c0` (0x103871c0), shared by `CNPC_VHumanCombatant`,
	//     `CNPC_VHumanCombatPatrol`, `CNPC_VSabbatGunman`, `CNPC_VStalker`, `CNPC_VYukie`,
	//     `CNPC_ProneDialog` and `CNPC_VGhoulCroucher` itself.
	//
	// Their body is: new state 1 (IDLE) -> `GetActiveWeapon()->Hide()` (`+0x108`); new state 2
	// (ALERT), 3 (COMBAT) or 11 -> `GetActiveWeapon()->Unhide()` (`+0x10c`); then chain to the
	// Troika base either way. `CNPC_VGuard1`'s only addition is an unrelated `+0x29c`/`+0xa8` probe
	// ahead of the switch. So "an armed class holsters while idle and draws when it goes alert" is a
	// property of SEVEN concrete classes and of nothing else.
	//
	// **A polled edge rather than a callback**, because this runtime's state is written from three
	// places (the ideal-state pass, `forcestate`, and the body arbiter's scripted push) and a hook on
	// each is three chances to forget one. `bStateChangeSeen` starts false so the FIRST think fires
	// it, which is retail's own spawn-time `SetState(IDLE)` — that is what puts a freshly spawned
	// guard's weapon away. Public so a fixture can drive the edge without a whole think.
	void PumpStateChange();

	// Whether this NPC's authored classname is one of the seven that fill slot 463 with the
	// holster/draw body. Spelled from the retail class names with the port's `npc_` prefix.
	bool ClassHolstersOnState() const;

	// The recovered override body itself, taking the NEW state exactly as retail's second argument
	// does. Public because it IS the virtual — retail's callers reach it through the vtable, and a
	// fixture asserting the arms has to be able to state a transition without also driving the whole
	// decision pass that produces one.
	void ApplyStateWeaponVisibility(EElysiumNpcState NewState);

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
	virtual bool IsIdealActivityCurrent() const override;

	// One rung of `TASK_PLAY_DEATH_SEQUENCE`'s ladder. It goes through the same Reaction-band
	// producer every other combat reaction does, because a death pose has to replace whatever owns
	// the base channel and hold it — `PlayActivity`'s ambient claim is outranked by the next
	// locomotion publish, which would stand a corpse back up.
	virtual float PlayDeathActivity(const FString& Activity) override;

	virtual float RandomSeconds(float Max) override;

	virtual void RecordScheduleEvent(const FString& Row) override;
	virtual void DebugScheduleInstalled(EElysiumScheduleId InstalledSchedule) override;

	virtual bool FaceSavePosition() override;

	virtual bool StepAwayFromSavePosition(float DistanceCm) override;

	// --- The combat task bodies -----------------------------------------------------------------
	// Every one that drives the body claims `EElysiumBodyOwner::Schedule` through the arbiter first
	// and answers false when the claim is refused, which fails its task by name. The token is given
	// back once, where the program ends (`ReleaseScheduleBody`).

	virtual void StopMoving() override;
	virtual EElysiumTaskResult StopMovingTask() override;
	virtual EElysiumTaskResult BeginStopMovingTask() override;
	virtual void TaskStarting() override { ScheduleHost.FailureReason = ScheduleHost.PendingFailureReason = 0; }
	virtual void SetGoalTolerance(float Units) override;
	virtual void TaskFail(int32 Reason) override;
	virtual void ScheduleDone() override;
	virtual int32 TaskFailureReason() const override { return ScheduleHost.PendingFailureReason; }
	FElysiumNpcScheduleHost ScheduleHost;

	// --- The think cadence's own state (`Substrate/ElysiumNpcThinkCadence.h`) --------------------
	// Slot 614 `ResetThinkTimers()` (`0x102c23f0`), dispatched virtually by `FeedInterrupt` before
	// the trance, by the possession arm, and by the `TeleportToEntity` input's AI tail. It sets the
	// four stamps AND `m_flNextThink` to now, so the effect takes hold on the same frame. This is
	// NOT what `TaskFail` (`0x1029adb0`) does: that one writes the four and deliberately leaves
	// `m_flNextThink` alone, and the difference is observable -- see `FElysiumNpc::TaskFail`.
	void ResetThinkTimers(double Now);
	// `m_scriptState in {4,5,6}`, the third term of `ShouldThinkFrequently()` (`0x102c2430`) --
	// the aiscripted states in which a beat is actively driving this body. Mapped rather than
	// transcribed: this runtime spells the same fact as a scripted owner holding the body or a
	// scripted move in flight.
	bool IsScriptDriven() const
	{
		return ScriptOwner.IsSet() || ScriptPhase != EScriptPhase::None;
	}
	// `m_flTeleportMoveTimer` (+0x65dc, keyfield `teleport_move_timer`). Inside this window
	// `ShouldThinkFrequently()` is unconditionally true. UNRECOVERED producer: a `StartTask`
	// (`0x102a1910`) arm this runtime has not ported writes it, so the window is never open.
	double TeleportMoveUntil = 0.0;
	// `m_bForceFrequentThink` (+0x63f0). Its only writer in the image is the bare setter
	// `0x101aa750`, which has no recovered caller, so the flag stays false.
	bool bForceFrequentThink = false;

	// CBaseEntity::SetAttackExtents 0x1009af40; attack partition only, never the motor capsule.
	void SetAttackExtents(const FVector& MarginCm) { ScheduleHost.AttackExtentsCm = MarginCm; }
	FBox AttackBounds(const FBox& CollisionBounds) const
	{
		return FBox(CollisionBounds.Min - ScheduleHost.AttackExtentsCm,
			CollisionBounds.Max + ScheduleHost.AttackExtentsCm);
	}
	void ClearScheduleHint(float ReuseDelay);
	void ClearOwnedActivityCopyProps();
	void EndDisciplineSchedule();
	void DisconnectFromSquad();
	void ReconnectToSquad();

	virtual bool GetPathToEnemy(float ToleranceUnits) override;

	virtual void RunPath() override;

	virtual EElysiumMoveWatch WaitForMovement() override;

	virtual bool FaceEnemy() override;

	virtual bool AnnounceAttack(float Param) override;

	virtual bool MeleeAttack1() override;

	virtual bool RangeAttack1() override;

	virtual void RememberFact(float What) override;

	virtual bool GetPathToScriptedGoal() override;

	// --- The incapacitation task bodies and the install rules -----------------------------------

	virtual void MakeOblivious(bool bOblivious) override;

	virtual void SetNpcFlag(EElysiumNpcFlag Flag) override;

	virtual void ClearConditions() override;

	virtual void OnScheduleChange() override;

	virtual void BuildScheduleTestBits(FElysiumNpcConditions& InOutMask) override;

	/**
	 * `CBaseCombatCharacter::IsBusyWithDiscipline` (`0x1033e2b0`) — the sole reader of `D_IS_BUSY`,
	 * whose whole body is that one bit test.
	 *
	 * Named as its own predicate rather than left as a bit test at each site, because that is what
	 * its 17 retail callers see: the bit and the predicate are the same fact, and a caller that
	 * tested the bit directly would drift from them.
	 */
	virtual bool IsBusyWithDiscipline() const override
	{
		return NpcFlags.Has(EElysiumNpcFlag::D_IS_BUSY);
	}

	/**
	 * `m_iIsOblivious > 0` (`CAI_BaseNPC` `+0x5bb4`).
	 *
	 * Its four recovered consumers are: the sense pass (`CAI_BaseNPC::PerformSensing` `0x1026e4f0`
	 * skips sensing entirely), the weapon-aim pose (slot 314, `0x102bf070`, stops aiming), a
	 * reaction predicate (slot 587, `0x1028ef20`) and `CStealthKillRules::FindVictim`
	 * (`0x101be1f0`, which makes an oblivious body backstabbable from any angle).
	 */
	bool IsOblivious() const { return NpcFlags.IsOblivious(); }

	// `CAI_BaseNPCTroika::IsValidStealthKillTarget` `0x102c2300` (slot 294). The attacker argument
	// is unused in the listing, matching retail.
	bool IsValidStealthKillTarget(const FElysiumPlayer& Attacker) const;
	virtual bool EnterGrappleState(const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role,
		EElysiumGrappleType Type, int32 Position = INDEX_NONE, bool bHolster = true) override;
	virtual void LeaveGrappleState() override;

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

	void FinishAmbientUse(bool bFireLeft, bool bStopMovement = true);

	void ThinkAmbient();

	// StartPlayerDialogRemote opens a dialog session: fire OnDialogBegin, then run the NPC's `.dlg`
	// conversation. When the `dialogname` file is missing/unloadable the session falls back to the
	// manual seam — it waits for EndDialog (ent_fire), so the beat is still driveable by hand.
	virtual FElysiumBodyOwnerToken BeginDialogueBodySession() override;

	virtual void EndDialogueBodySession(const FElysiumBodyOwnerToken& Token, bool bSilent) override;

	// Read-only reach into the dialogue body claim for `FElysiumNpcDialogue`, which cannot reach
	// the private token directly (no friend is added for a plain-C++ value member).
	const FElysiumBodyOwnerToken& GetDialogueBodyOwner() const { return DialogueBodyOwner; }

	// The three operations `BeginDialog` ran before opening a session, split out because they
	// touch private leaf state (`FinishAmbientUse`, `Motor`) that `FElysiumNpcDialogue` cannot
	// reach: end the ambient visit, stop an active patrol move, then take the Dialogue body claim.
	bool PrepareBodyForDialogue();

	// `FElysiumNpcDialogue::Begin`.
	void BeginDialog(EElysiumDialogOpenerKind Opener, int32 RawFlags, const FElysiumInputArgs& Args)
	{
		Dialogue.Begin(*this, Opener, RawFlags, Args);
	}

	// --- Use-to-talk (`CBasePlayer::PlayerUse`, `0x10167850`) — `Substrate/ElysiumNpcDialogue.h` ---

	// `FElysiumNpcDialogue::Name`.
	FString DialogName() const { return Dialogue.Name(*this); }

	// `FElysiumNpcDialogue::IsUsable`.
	virtual bool IsUsable() const override { return Dialogue.IsUsable(*this); }

	// `FElysiumNpcDialogue::CanPlayerFocus`.
	virtual bool CanPlayerFocus(const FElysiumUseContext& Context) const override
	{
		return Dialogue.CanPlayerFocus(*this, Context);
	}

	// `FElysiumNpcDialogue::BeginPlayerUse`.
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext& Context) override
	{
		return Dialogue.BeginPlayerUse(*this, Context);
	}

	// `FElysiumNpcDialogue::ResolveUseIcon`.
	virtual int32 ResolveUseIcon(const FElysiumEntityHandle& Activator) const override
	{
		return Dialogue.ResolveUseIcon(*this, Activator);
	}

	/**
	 * The dialogue-suppression guard on every conversation entry — RECOVERED as TWO bits, not one.
	 *
	 * Retail's "can the player talk to me" predicate (virtual slot 295, `0x102c21c0`, with the
	 * `CPayphone` override `0x101aaee0`) tests both:
	 *
	 *   - `m_bfAINPCFlags & 0x00080000` — `NO_DIALOG`, the PER-SCHEDULE form. A schedule sets it with
	 *     `TASK_SET_NPC_FLAG` and the next schedule change releases it, so it lasts exactly as long
	 *     as the program that asked for it. `SCHED_TROIKA_MESMERIZED` is one such program.
	 *   - `m_bfAINPCFlags2 & 0x10000000` — `NO_DIALOG_PERSISTENT`, which the schedule-change clear
	 *     does NOT touch. This is the bit the seam this replaced was named after.
	 *
	 * Both are now decoded and both are honoured. `NO_DIALOG_PERSISTENT` still has no writer in this
	 * runtime — no recovered producer sets it — so today only the schedule form can block, and that
	 * is a missing producer rather than a missing rule.
	 */
	bool HasDialogSuppressFlag() const
	{
		return NpcFlags.Has(EElysiumNpcFlag::NO_DIALOG)
			|| NpcFlags.Has(EElysiumNpcFlag2::NO_DIALOG_PERSISTENT);
	}

	// `FElysiumNpcDialogue::EntryRefusalReason`.
	const TCHAR* DialogEntryRefusalReason() const { return Dialogue.EntryRefusalReason(*this); }

	// `FElysiumNpcDialogue::StartForced`.
	void InputStartPlayerDialog(const FElysiumInputArgs& Args) { Dialogue.StartForced(*this, Args); }

	// `FElysiumNpcDialogue::StartRemote`.
	void InputStartPlayerDialogRemote(const FElysiumInputArgs& Args) { Dialogue.StartRemote(*this, Args); }

	// `FElysiumNpcDialogue::StartUnforced`.
	void InputStartPlayerDialogUnforced(const FElysiumInputArgs& Args) { Dialogue.StartUnforced(*this, Args); }

	// `FElysiumNpcDialogue::End`.
	void InputEndDialog(const FElysiumInputArgs& Args) { Dialogue.End(*this, Args); }

	// `FElysiumNpcDialogue::OpenConversation`.
	bool OpenConversation(const FElysiumEntityHandle& Activator, EElysiumDialogOpenerKind Opener)
	{
		return Dialogue.OpenConversation(*this, Activator, Opener);
	}

	// The sheet, from `stats.txt`'s defaults overlaid with this NPC's `stattemplate`. That overlay
	// is the whole of an NPC's health track: `npctemplate*` authors `Max_Health` as a literal, and a
	// template that omits it inherits `stats.txt`'s `Default 100` (`docs/vtmb/vdata-catalog.md`). Without it
	// every NPC has a zero ceiling and TakeDamage only logs.
	void SeedSheet();

	virtual void Spawn() override;

	virtual void Activate() override;

	// SetModel: swap the NPC's appearance (bradbury Heather goth/normal, cemetery prostitute,
	// downtown Nines). The rebuild is FElysiumAnimating's; the A/B gate is this leaf's.
	virtual void OnRuntimeModelChanged() override;
	virtual void OnPreparedVisualAttached() override
	{
		// A lookup while admission was pending may have cached "no stance" for this ID.
		// Invalidate only that metadata result; do not re-arm the mind or replay a schedule.
		StanceResolvedFor.Reset();
		FElysiumScriptedCharacter::OnPreparedVisualAttached();
	}

	virtual void OnDormancyChanged() override;

	virtual void Serialize(FElysiumSaveArchive& Ar) override;

	virtual const TCHAR* SaveBlockReason() const override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

	virtual FElysiumNpc* AsNpc() override { return this; }

	// The gaze cascade's NPC-only subjects (`CAI_BaseNPC`'s eye maintainer, `0x1026b810`).
	// `GetEnemy()` is the committed enemy; the navigator's goal is the destination of the move in
	// flight; the heard sound is the last stimulus the hearing gather promoted to a HEAR_* condition.
	virtual const FElysiumEntity* GazeEnemy() const override;
	virtual bool GazeNavigationGoal(FVector& OutPoint) const override;
	virtual bool GazeHeardSound(FVector& OutPoint) const override;

	// `m_pNavigator`'s goal position: the feet destination the move in flight was issued for.
	// Written beside every `Motor->MoveTo`, read by the gaze arm above and nothing else — the
	// motor owns the route, this is only what the character asked it for.
	FVector MoveGoal = FVector::ZeroVector;

	// Read-only, for the debug layer. The mind is private because every WRITE to it has to go
	// through `RequestState` / `Acquire` / `Release` so the admission and the body arbitration
	// cannot be sidestepped; reading its state, its ideal state, its owner and its transition trace
	// sidesteps nothing, and those four together are the only account of why a character is doing
	// what it is doing. Const on purpose — a panel that could call `RequestState` would be a second
	// producer of NPC state.
	const FElysiumNpcMind& GetMind() const { return Mind; }

	// Requirement 23: the debugger may show the authored place this NPC currently owns, but it
	// must not reach into the private ambient phase/index state or acquire/release a claim.
	const FElysiumInterestingPlace* GetCurrentAmbientSpotForDebug() const
	{
		return CurrentAmbientSpot();
	}
	int32 GetAmbientPhaseForDebug() const
	{
		return static_cast<int32>(AmbientPhase);
	}

	// Read side for the automation tests and the inspector: whether `FollowPatrolPath` armed a
	// route on this leaf, and how many points that route resolved to (the count is kept whether
	// or not the route is currently armed).
	bool IsPatrolActiveForDebug() const { return bPatrolActive; }
	int32 NumPatrolPointsForDebug() const { return PatrolPoints.Num(); }

private:
	// --- Think(), phase by phase, in the order Think() calls them. A bool phase returns true
	// when it consumed this think, and Think() returns with it -----------------------------------

	// A dead NPC's whole think: advance the death program while it runs, then hand the body to
	// physics (or freeze it) and stop thinking. It is FIRST in the pass because a corpse admits
	// nothing, senses nothing and selects nothing.
	bool ThinkDead();

	// The activation barrier: the mind is admitted on its first frozen-time think.
	bool RunAdmissionBarrier();

	// The combat loadout, resolved once on the first ordinary think after admission.
	void ResolveLoadout();

	// A director's push that fired before this NPC's first think replays here.
	void ReplayDeferredScriptedOrder();

	// Senses and the recovered decision pass — or the stale-condition reset where a
	// scripted owner suppresses gathering.
	void RunConditionPass();

	// The edge tracker `PumpStateChange` keeps (see the pump's own comment, in the public section).
	EElysiumNpcState LastStateChange = EElysiumNpcState::Idle;
	bool bStateChangeSeen = false;

	// Watches a beat that stopped advancing its own move and releases the body rather than
	// freezing it.
	bool TickScriptWatchdog();

	// An open conversation: the per-line clip hold, else the stance machine's talking branch.
	bool ThinkInDialog();

	// A scripted owner drives this body's pose; the think only lands a deferred beat claim.
	bool ThinkScriptOwned();

	// Schedule selection pre-empts an autonomous executor.
	bool ThinkSchedulePolicy();

	// The autonomous executors: the patrol route, an interesting place, or the standing stance.
	void ThinkAutonomous();

	// --- Serialize(), one helper per version block, in exact archive order ----------------------

	// The pre-version patrol and ambient state. Returns false for a payload written before
	// ambient-place state existed, which is where the leaf's record ends.
	bool SerializePatrolBlock(FElysiumSaveArchive& Ar);
	void SerializeMakerBlock(FElysiumSaveArchive& Ar);
	void SerializeMindBlock(FElysiumSaveArchive& Ar);
	void SerializeScheduleBlock(FElysiumSaveArchive& Ar);
	void SerializeSocialBlock(FElysiumSaveArchive& Ar);
	void SerializeSensesBlock(FElysiumSaveArchive& Ar);
	void SerializeLoadoutBlock(FElysiumSaveArchive& Ar);
	void SerializeWitnessBlock(FElysiumSaveArchive& Ar);
	void SerializeDisciplineBlock(FElysiumSaveArchive& Ar);

	// ---------------------------------------------------------------------------------------------

	// A suspended patrol route comes back with a fresh generation, so the leaf's own token is
	// re-stamped wherever a release hands the body back to the route.
	void RestampPatrolToken();

	// The two program claims (`Schedule` and `ScriptedSchedule`) share one arbitration shape:
	// idempotent for a token already held, the patrol route parked rather than taken, and the
	// release stops whatever the program had the body doing. `Token` is the leaf's member for
	// `Owner`; the public pairs below are thin wrappers over these two.
	bool AcquireProgramBody(EElysiumBodyOwner Owner, FElysiumBodyOwnerToken& Token,
		const TCHAR* Reason);
	void ReleaseProgramBody(EElysiumBodyOwner Owner, FElysiumBodyOwnerToken& Token,
		const TCHAR* Reason);

	// Everything this NPC holds over its own body, given back at once: an open conversation, a
	// scripted move, an interesting place, a pushed director's order, the running program, and every
	// arbiter token behind them. Two callers — dormancy (`Kill`/`ScriptHide`) and death — because
	// both mean "this NPC stops driving its body", and the difference between them is only whether
	// the mind ends up dead.
	void ReleaseAllBodyOwnership(const TCHAR* Reason, bool bDeadMind);

	// The end of the death transaction, run once: hand the body to Unreal's physics, seeded from the
	// pose it is standing in. A body with no physics asset behind it holds that pose instead — the
	// shipped outcome, because the character bake writes none. The solid-body policy is deliberately
	// NOT here: it is re-asserted on every terminal dead think, because a corpse's body can be handed
	// back to it by something that took it before the kill.
	void CompleteDeathHandoff();

	// The death transaction's body half, re-applied after a restore. A load rebuilds the motor, so
	// frozen / non-solid-to-characters / held-pose all have to be stated again on it — and a corpse's
	// saved `NextThink` is `never`, so this cannot be deferred to a think the way the patrol and
	// discipline blocks defer theirs.
	void RestoreDeathBodyState();

	// --- The footfall (`0x1026d460`), sequenced ---------------------------------------------------
	/**
	 * One footfall, in retail's own order: the species override, the global player gate, the
	 * template/cvar source, this body's cached surface, the level, the coin flip, the emit.
	 *
	 * Always answers true. `EventId` is carried past the mode because the species overrides read the
	 * foot the shared chain throws away.
	 */
	bool NpcStep(int32 EventId, bool bHeavy);

	// The species row this NPC's classname selects, resolved ONCE. `bFootstepSpeciesResolved`
	// distinguishes "no row" from "not looked up yet"; the row itself is a pointer into a static
	// table, so it outlives every entity.
	const struct FElysiumFootstepSpecies* FootstepSpecies = nullptr;
	bool bFootstepSpeciesResolved = false;
	const struct FElysiumFootstepSpecies* ResolveFootstepSpecies();

	// This NPC's `stattemplate` record, RESOLVED through `ParentTemplateName` and latched at the one
	// site that already resolves it (`ApplyResolvedTemplate`). Retail re-resolves per footfall
	// (`1026d4a0`); the port resolves once, because `FElysiumClanTable::Resolve` merges seven maps
	// and a walking body asks two or three times a second. Null for an NPC with no `stattemplate` or
	// one whose template does not resolve, which `ElysiumFootsteps::NpcSource` answers with the
	// loader's own defaults.
	//
	// Shared rather than unique so the header needs no complete type: `TSharedPtr`'s deleter is
	// captured where `MakeShared` runs, which is the .cpp that has `ElysiumRulebook.h`.
	TSharedPtr<const FElysiumClanTemplate> FootstepTemplate;

	// The two silent arms of the footfall, counted rather than logged per occurrence — the anim-event
	// census's rule, and for its reason: a body with no surface under it fires 2050 every half
	// second. `bReportedNoStepSurface` is retail's null `surfacedata_t` (`+0x5b90`); the set is one
	// entry per surface whose baked record carries no step pool at all.
	bool bReportedNoStepSurface = false;
	TSet<FName> ReportedStepSurfacesWithoutPool;

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
	// Whether the death handoff has already run. Session state, not save state: it is derivable from
	// the mind's dead state, and a restored corpse re-runs the handoff on the body the load rebuilt.
	bool bDeathHandoffDone = false;
	// The interesting-place visit's phase machine and the state riding on it.
	enum class EAmbientPhase : uint8 { None, Moving, Into, Dwelling, Out };
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

// npc_VPlayerController — the scene-owned duplicate of the player. It shares only the authored
// scripted-sequence motor with ordinary NPCs: no dialogue, AI, use body, or autonomous think.
//
// It lives beside FElysiumNpc rather than in its own file because the two share exactly one thing:
// the `elysium.NpcBodies` A/B, which is a file-static in `ElysiumNpc.cpp`.

class FElysiumPlayerControllerNpc final : public FElysiumScriptedCharacter
{
public:
	virtual void Spawn() override;

	virtual void OnRuntimeModelChanged() override;

	virtual void SetIgnoreCharacterCollision(bool) override;

	virtual void OnDormancyChanged() override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	// The shared motor build, made non-solid: the duplicate navigates against the world but never
	// becomes a second solid character.
	virtual void BuildOwnMotor() override;
};
