#pragma once

#include "Substrate/ElysiumNpcBase.h"

struct FElysiumClassDesc;

// `CCineNPC` (primary vtable `0x104771e4`), built by the `scripted_sequence` factory — the script
// director, a direct `CAI_BaseNPC` subclass (story 5 fold A3). `CCineAI` (`aiscripted_sequence`,
// `ElysiumAiScriptedSequence.h`) and `CCineAISchedule` (`aiscripted_schedule`,
// `ElysiumAiScriptedSchedule.h`) derive from it.
//
// A director is a FULL `CAI_BaseNPC` in retail: its constructor (`0x101a62d0` over `0x1027c300`) sets
// `+0x94`, joins the AI list and `PostConstructor` builds its components, senses included. It never
// runs `NPCInit` or `NPCThink`: its `Spawn` (`0x101a6f10`) replaces the base's and chains to nothing,
// so it has no schedule, no motor use and no sense pass. What it runs is its own installed think,
// `CineThink` (`0x101a8070`), and `SUB_Remove` after a non-repeatable beat finishes.
//
// Twenty own vtable slots (the vtable diff against `CAI_BaseNPC`, `0x104995c4`); slot 5 is the
// deleting destructor, the C++ destructor's. Slots 583–586 are the branch's OWN virtuals past
// `CAI_BaseNPC`'s 583-slot table (`PossessEntity`, `StartSequence`, `FCanOverrideState`,
// `FixScriptNPCSchedule`, `docs/vtmb/npc-kernel/signatures.md`), unrelated to the Troika's virtuals at
// the same indices; no C++ name is shared, because the two branches never meet in one class.
//
// **The beat's body half is a named modernization.** Retail hands the possessed NPC to its own
// scripted schedules (`SCHED_AISCRIPT 0x2e` -> `SCHED_TROIKA_SCRIPTED_WALK 0xf2` …, the tasks
// `TASK_WALK_TO_TARGET` .. `TASK_PLAY_SCRIPT_POST_IDLE`; spec 0003 stories 1–2), which the port has
// not built. Until it does, the director drives the equivalent task order itself on a 0.05 s beat
// think (`TickBeat`): travel (8/9/10, the port's motor), plant/face, `TASK_ENABLE_SCRIPT`
// (`DelayStart(0)`), `TASK_WAIT_FOR_SCRIPT` (pre-idle, `IsTimeToStart`, `StartScript`),
// `TASK_PLAY_SCRIPT` (`SequenceDone`) and `TASK_PLAY_SCRIPT_POST_IDLE` (`Finish`). Every retail
// director body those tasks call is the retail body; only the task driver is the port's.
class FElysiumScriptedSequence : public FElysiumNpcBase
{
public:
	ELYSIUM_NPC_CLASS("CCineNPC", FElysiumNpcBase)


	// --- The own vtable slots (`CCineNPC`, `0x104771e4`) -----------------------------------------
	// Slot 72 `0x101a6e20` — `XOR AL,AL`: no discipline targets a director.
	virtual bool Slot72(int32 Discipline) override;
	// Slot 82 `0x101a5db0` — `&datamap_CCineNPC` (`0x10593628`): this port's datamap is the class
	// descriptor, so the answer is the `CCineNPC` descriptor.
	virtual void* GetDataDescMap() override;
	// Slot 103 `0x101a6f10`. Does NOT chain to `CAI_BaseNPC::Spawn`.
	virtual void Spawn() override;
	// Slot 113 `0x101a8de0`. `CBaseEntity::Activate`, the actor search, the precaches, `m_hNextCine`.
	virtual void Activate() override;
	// Slot 117 `0x101a6d20` — `CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION`.
	virtual int32 ObjectCaps() const override;
	// Slot 175 `0x101a75a0` / slot 178 `0x101a7580` — both a bare `RET 4`.
	virtual void Touch(FElysiumEntity* Other) override;
	virtual void Blocked(FElysiumEntity* Other) override;
	// Slot 180 `0x101a7140` — `CAI_BaseNPC::UpdateOnRemove` (direct), then `ScriptEntityCancel(this)`.
	virtual void UpdateOnRemove() override;
	// Slots 362–365 `0x101a6dc0` / `0x101a6da0` / `0x101a6e00` / `0x101a6de0` — every cone is false.
	virtual bool FInViewCone(const FVector& PointCm) override;
	virtual bool FInViewCone(FElysiumEntity* Candidate) override;
	virtual bool FInAimCone(const FVector& TargetCm) override;
	virtual bool FInAimCone(FElysiumEntity* AimTarget) override;
	// Slots 370/371 `0x101a6d40` / `0x101a6d70` — tail calls through slots 368/369 `BodyDirection*`.
	virtual FVector HeadDirection2D() override;
	virtual FVector HeadDirection3D() override;
	// Slot 459 `0x101a89a0` — the conditions a non-interruptible beat strips off its NPC.
	virtual void RemoveIgnoredConditions() override;

	// Slots 583–586, this branch's own virtuals (`CCineNPC` bodies; `CCineAI` and `CCineAISchedule`
	// override them).
	// Slot 583 `0x101a7880`.
	virtual void PossessEntity();
	// Slot 584 `0x101a82d0` — `StartSequence(npc, name, bCompleteOnEmpty)`.
	virtual bool StartSequence(FElysiumNpcBase& Npc, const FString& SequenceName, bool bCompleteOnEmpty);
	// Slot 585 `0x101a7210` — `(m_spawnflags >> 6) & 1`, `SF_SCRIPT_OVERRIDESTATE`.
	virtual bool FCanOverrideState() const;
	// Slot 586 `0x101a8840`.
	virtual void FixScriptNPCSchedule(FElysiumNpcBase& Npc);

	// --- The non-virtual bodies, by address -------------------------------------------------------
	// `CineThink` `0x101a8070`, the think `Spawn` / `StartSchedule` install.
	void CineThink(double Now);
	// `FindEntity` `0x101a7600`: the name/classname walk within `m_flRadius`, `CanPlaySequence`
	// precedence 1 > 2.
	FElysiumNpcBase* FindEntity();
	// `0x101a7760`: `FindEntity`, the `0x400` cyclic latch, `SetTarget(this, found)`.
	bool FindEntityWrap();
	// `ScriptEntityCancel` `0x101a7170` on `Cine`: only a director, only when its NPC is in SCRIPT.
	static void ScriptEntityCancel(FElysiumEntity& Cine);
	// `CancelScript` `0x101a8c30`: `ScriptEntityCancel` on every entity sharing this one's targetname.
	void CancelScript();
	// `DelayStart` `0x101a8cf0`: count `m_iDelay` on every same-named literal `scripted_sequence`.
	void DelayStart(bool bIncrement);
	// `IsTimeToStart` `0x101a7540`: `m_iDelay < 1 && m_startTime <= curtime` (an AND, not the SDK's OR).
	bool IsTimeToStart() const;
	// `StartScript` `0x101a81a0`: the linked sequence, then `OnBeginSequence`.
	void StartScript();
	// `0x101a8130`: `m_iszLinkedSequence` resolved to a director, or null.
	FElysiumScriptedSequence* LinkedSequence() const;
	// `SequenceDone` `0x101a8460`: post-idle or `Finish`, then `OnEndSequence` UNCONDITIONALLY.
	void SequenceDone(FElysiumNpcBase& Npc);
	// `Finish` `0x101a8640`: the `0x100` post-idle hold, `SUB_Remove`, `CineCleanup`, slot 586, the chain.
	void Finish(FElysiumNpcBase& Npc);
	// `CanInterrupt` `0x101a8930`: `m_interruptable` AND the resolved target answering `IsAlive`.
	bool CanInterrupt() const;
	// `CanOverride` `0x101a8ac0`: may another director take this one's NPC (the queue refusals).
	bool CanOverride() const;
	// `AllowInterrupt` `0x101a8890`, reached from the NPC's `HandleAnimEvent` (`0x10274e30`) script
	// events 0x3e9/0x3ea (`FElysiumNpcBase::HandleAnimEvent`).
	void AllowInterrupt(bool bAllow);
	// `m_OnScriptEvent[Index]` (`+0x5fcc` + 0x18·Index), fired by the 0x3eb arm (`0x101a7230`).
	void FireScriptEvent(int32 Index);

	// `CineCleanup` `0x1027d170` — a `CAI_BaseNPC` body on the NPC, which reads the NPC's `m_hCine`.
	// Ported here beside the only callers the port has (`ScriptEntityCancel`, `Finish`, slot 440's
	// "Script failed" arm) because every word it restores is the cine's.
	static void CineCleanup(FElysiumNpcBase& Npc);
	// `0x1026d130`: `SetEnemy(NULL)`, `DisconnectFromSquad`, `++m_iIsOblivious` — no output, no
	// bookkeeping bit (unlike `TASK_MAKE_OBLIVIOUS`).
	static void MakeNpcOblivious(FElysiumNpcBase& Npc);
	// `0x10007ea0`: `--m_iIsOblivious` (floored at 0), then `0x1026d0c0` `ReconnectToSquad`.
	static void ReleaseNpcOblivious(FElysiumNpcBase& Npc);
	// `0x101a77a0`: `m_bCineScriptHidden := 1` on the hidden NPC, and the "voodoo" warning block.
	void ScriptHiddenWarning(FElysiumNpcBase& Npc) const;

	// The NPC `m_hTargetEnt` (`+0x5ce4`) resolves to through its `+0x94`, or null.
	FElysiumNpcBase* TargetNpc() const;

	// --- The inputs (datamap `0x10593628`) --------------------------------------------------------
	// `InputBeginSequence` `0x101a7390`.
	void InputBeginSequence(const FElysiumInputArgs& Args);
	// `InputMoveToPosition` `0x101a72b0`.
	void InputMoveToPosition(const FElysiumInputArgs& Args);
	// `InputCancelSequence` `0x101a7500` — `ScriptEntityCancel(this)` ONLY (not `CancelScript`).
	void InputCancelSequence(const FElysiumInputArgs& Args);
	// `CBaseEntity::InputKill` -> `UTIL_Remove`, whose first act is slot 180 `UpdateOnRemove`. The
	// port's `Kill` runs no slot 180, so a director's `Kill` input states the pair (see the .cpp).
	void InputKill(const FElysiumInputArgs& Args);

	// The inputs and the `Kill` override on the director descriptors (`ElysiumNpcClasses.cpp`).
	static void AddInputs(FElysiumClassDesc& D, const TCHAR* RetailClass);

	// --- The datamap words (`CCineNPC`, `+0x5f44..+0x6074`) -------------------------------------
	// Bound by `gen_kernel_bindings` through the species shape map (`CCineNPC` rows).
	FString PreIdle;          // +0x5f44 m_iszPreIdle — KEY `m_iszIdle`
	FString Play;             // +0x5f48 m_iszPlay
	FString PostIdle;         // +0x5f4c m_iszPostIdle
	FString CustomMove;       // +0x5f50 m_iszCustomMove
	FString TargetEntity;     // +0x5f54 m_iszEntity
	FString NextScript;       // +0x5f58 m_iszNextScript
	FString LinkedSequenceName; // +0x5f5c m_iszLinkedSequence
	int32 MoveTo = 0;         // +0x5f60 m_fMoveTo
	int32 FinishSchedule = 0; // +0x5f64 m_iFinishSchedule
	float Radius = 0.f;       // +0x5f68 m_flRadius — READ by `FindEntity` (`0x101a7621`, `0x101a76c7`)
	float Repeat = 0.f;       // +0x5f6c m_flRepeat — no reader in `vampire.dll`
	int32 Delay = 0;          // +0x5f70 m_iDelay
	double StartTime = 0.0;   // +0x5f74 m_startTime
	int32 SavedMoveType = 0;      // +0x5f78 m_saved_movetype
	int32 SavedMoveCollide = 0;   // +0x5f7c m_saved_movecollide
	int32 SavedSolid = 0;         // +0x5f80 m_saved_solid — written, never read back (`CineCleanup`)
	int32 SavedSolidFlags = 0;    // +0x5f84 m_saved_solidflags
	int32 SavedEffects = 0;       // +0x5f88 m_saved_effects
	int32 SavedTroikaFlags = 0;   // +0x5f8c m_saved_troika_flags — the Troika's `m_bfAINPCFlags`
	bool bInterruptable = false;  // +0x5f90 m_interruptable
	bool bSequenceStarted = false; // +0x5f91 m_sequenceStarted
	FElysiumEntityHandle NextCine; // +0x5f94 m_hNextCine
	// +0x5f98 `m_pLastFoundEntity` (walked, unsaved): `FindEntity`'s start (`0x101a7636`), written by
	// `0x101a7760` under spawnflag `0x400`.
	FElysiumEntityHandle LastFoundEntity;

	/** SEAM for `AddFlag2(0x10)` (`Spawn`, `0x101a70bb`) — `CBaseEntity::m_fFlags2` (`+0x438`), a word
	 *  with no port owner (the controller line carries the same seam, fold A2). The meaning of bit
	 *  `0x10` is unrecovered. */
	uint32 Flags2Added = 0;

	// `m_scriptState` (`+0x5d70`) — an NPC word, held here on the director that owns the NPC
	// (`m_hCine`). It has no meaning without one: `CineCleanup` zeroes it in the same pass that clears
	// `m_hCine`, and every reader resolves `m_hCine` first. 0 playing, 1 wait, 2 post-idle, 3 cleanup,
	// 4/5/6 walk/run/custom to the mark.
	int32 NpcScriptState = 0;
	// `m_scriptState` of `Npc`: its owning director's word, or 0 when nothing owns it.
	static int32 ScriptStateOf(const FElysiumNpcBase& Npc);

	// --- The installed think (`m_pfnThink` / `m_flNextThink`) -------------------------------------
	enum class EThinkFunction : uint8 { None, CineThink, SubRemove };
	EThinkFunction ThinkFunction = EThinkFunction::None;
	// Retail's `m_flNextThink` for the installed function; `ELYSIUM_NEVER_THINK` when none is armed.
	// Separate from `FElysiumEntity::NextThink`, which also carries the beat stand-in's cadence: the
	// `BeginSequence` "before it had a chance to think" test reads THIS word.
	double CineThinkAt = ELYSIUM_NEVER_THINK;
	// `ThinkSet(fn); m_flNextThink = At`.
	void ArmCineThink(EThinkFunction Function, double At);
	// `m_hLastInputActivator` (`+0x10c`), the activator `OnBeginSequence` / `OnEndSequence` fire with,
	// and `m_hLastInputCaller` (`+0x110`): `CBaseEntity` words the port's input dispatch does not keep
	// on the entity, so a director records them itself at each of its inputs.
	FElysiumEntityHandle LastInputActivator;
	FElysiumEntityHandle LastInputCaller;
	void RecordInput(const FElysiumInputArgs& Args);

	// `0x100f7460`, a procedural (`!`) search name as a director's own searches resolve it: the
	// director is the searching entity and the activator argument is 0 (`FindEntity` `0x101a7600`,
	// `0x101a98c0`'s `0x100f7f20`), so `!activator` / `!caller` fall through to THIS entity's
	// `+0x10c` / `+0x110`. An unknown name warns "Invalid entity search name" and answers null.
	FElysiumEntity* ResolveProceduralName(const FString& Name);

	// --- The port's stand-in for the possessed NPC's scripted schedules (named modernization) ------
	enum class EBeatPhase : uint8 { None, Travel, WaitForScript, Play, PostIdle, PostIdleHeld };
	EBeatPhase Phase = EBeatPhase::None;
	double BeatThinkAt = ELYSIUM_NEVER_THINK;
	double BeatClipEndsAt = 0.0;
	bool bTravelled = false;
	bool bResumeTravel = false;
	bool bPreIdleStarted = false;
	// A beat restored from a map snapshot (a level left and revisited mid-beat): `m_hCine` is
	// re-stamped on the NPC at `OnPostRestore` and the beat RESTARTS its current phase on its first
	// think — the port's "restart, not resume" rule for restored programs
	// (`FElysiumNpc::RestartRestoredSchedule`). Retail saves `m_hCine` and the NPC's scripted
	// schedule and resumes both (`CAI_BaseNPC::OnRestore` `0x1027bf50`).
	bool bRestartBeat = false;
	int32 RestoredNpcIndex = INDEX_NONE;
	// Starts the task order for the NPC `PossessEntity` just took.
	void BeginBeat(FElysiumNpcBase& Npc);
	// Stops it without an output (cleanup, cancel, a later possession, removal).
	void EndBeat();
	void TickBeat(double Now);

	virtual void ThinkAt(double Now) override;
	virtual const TCHAR* SaveBlockReason() const override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void OnPostRestore(FElysiumEntityWorld& InWorld) override;
	virtual void PreloadForActivation() override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual bool CancelScriptedSequenceForDialogue(const FElysiumEntityHandle& NpcHandle) override;
	virtual bool IsScriptedSequenceInterruptable() const override { return bInterruptable; }

	// --- Slot 113's recorded halves (story 29d, family Lifecycle10) --------------------------------
	/** One `0x10428880` request — `PrecacheSequenceSounds(studiohdr, sequenceName)`. **SEAM**: this
	 *  substrate has no studio header at kernel level, so the request is RECORDED and nothing is
	 *  acquired. */
	struct FSequenceSoundPrecache
	{
		FString ActorName;
		FString SequenceName;
	};
	TArray<FSequenceSoundPrecache> ActivatePrecacheLog;
	/** What `Activate` printed, verbatim in retail's order, dividers included. */
	TArray<FString> ActivateDiagnostics;

	// --- The port's schedule-runner hooks (`IElysiumScheduleRunner`) --------------------------------
	// Not retail slots. A director runs no schedule, so each answers "this body has none".
	virtual float RunSpecialIdleActivity(double Now) override;
	virtual bool IsBodyVisible() const override;
	virtual float PlayActivity(const FString& Activity) override;

protected:
	// `SUB_Remove` `0x101c0b10` as the port stands it: slot 180, then the entity's terminal `Kill`.
	void RemoveSelf();
	// The next entity after `Start` (entity-list order) whose targetname / classname matches `Name`
	// within `RadiusUnits` of `CenterCm` (`0x100f7c30` / `0x100f7e30`), and the pair `0x100f7f70`.
	FElysiumEntity* FindGenericWithin(FElysiumEntity* Start, const FString& Name, const FVector& CenterCm,
		float RadiusUnits);
	// The NPC the beat stand-in drives: the target, while it is still this director's.
	FElysiumNpcBase* BeatNpc() const;
	void RescheduleThink();
	// Travel helpers of the stand-in (the port's motor seam, `FElysiumEntity::BeginScriptMove`).
	bool StartTravel(FElysiumNpcBase& Npc);
	void PlaceOnMark(FElysiumNpcBase& Npc) const;
	void Arrive(FElysiumNpcBase& Npc, double Now);
	// The `m_iszPlay` / post-idle clip on `Npc`; answers its length (0 when none resolves).
	float PlayBeatClip(FElysiumNpcBase& Npc, const FString& Clip, bool bLoop);
	// Presentation claim the port adds on top of `m_hCine` (the body arbiter, the collision view of
	// `NAV_IGNORE_NPC`), taken at possession and given back by `CineCleanup`.
	void ClaimBody(FElysiumNpcBase& Npc);
	// The retail diagnostics this class prints, kept for the inspector and tests.
	TArray<FString> Diagnostics;
	void Diagnostic(const FString& Line);
};
