// `CAI_BaseNPC`'s declarations of the `Anim` family (story 5 step 5),
// moved from `ElysiumNpcAnim*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseAnim.cpp`.

// +0x065c m_bSequenceFinished — the byte `IsActivityFinished` (slot 251) tests first and slot 250
// `StudioFrameAdvance` raises; family Hints' interest loop reads it too (`0x102aa25b`).
bool bSequenceFinished = false;

// `GetSequenceCycleRate(m_nSequence)` (`0x10091230`): `1 / SequenceDuration` when the duration is
// positive, else 10.0 (`_DAT_1044e664`). Retail asks the model on every frame advance; this runtime's
// sequence length is the committed clip's, which the sequence bridge answers at `ResetSequenceInfo`,
// so the rate is cached there. 0 is the named seam: a sequence whose length is unknown to the kernel
// (a row never played on this body) does not advance its cycle.
float SequenceCycleRate = 0.f;

// `+0x650` -- the sequence `ResetSequenceInfo` last set the attack extents for (`0x10090a92..
// 0x10090ab0`: a different `m_nSequence` dispatches slot 247 and stores the new one).
int32 AttackExtentsSequence = INDEX_NONE;

// Slot 250 for an NPC: `CBaseAnimatingOverlay::StudioFrameAdvance` `0x10098bb0` -> `CBaseAnimating::
// StudioFrameAdvance` `0x1008f120`, over the sequence words this port keeps on the NPC (`+0x65c`,
// `+0x65d`, `+0x6f0`, `+0x6f4`, `+0x6f8`, `+0x170`, `+0x174`). Answers the frame interval.
float StudioFrameAdvance(float Interval) override;

// The sequence bridge's play hook (a named modernization: the studio sequence index is swapped for
// the name-keyed clip resolver). `ResetSequence` (`0x10260a50` -> `ResetSequenceInfo` `0x10090950`)
// hands the committed sequence here; a body that can play it answers the clip's first-pass length
// and its OWN loop bit (`GetSequenceFlags & 1`, `+0x65d`). The base plays nothing.
virtual bool PlaySequenceClip(int32 Sequence, float& OutSeconds, bool& bOutLoops)
{
	(void)Sequence;
	(void)OutSeconds;
	(void)bOutLoops;
	return false;
}

// `0x103ea950 GetSequencesForActivity(owner, translated activity, …)` as `CWeaponMelee`'s band
// `0x103ea7e0` asks it (spec 0002 V5a-1): every sequence of the wielder's model carrying the
// activity once the weapon (`+0x5a4`) and the owner (`+0x5e0`, slot 376) have translated it, in the
// model's own sequence order. The base hands none (no model); `FElysiumNpc` answers through the
// name-keyed resolver (`ElysiumNpcAnim.cpp`).
virtual void MeleeSequencesForActivity(int32 Activity, TArray<struct FElysiumNpcClip>& OutSequences) const
{
	(void)Activity;
	(void)OutSequences;
}

// The held weapon's slot 361 (`+0x5a4`) `CBaseCombatWeapon::ActivityOverride 0x1024f210`, in the
// kernel's NUMBER space: the first row of the weapon class's ladder whose target the owner can play
// (the bat: `ACT_MELEE_ATTACK` -> `_BASEBALLBAT`, `_MELEESHARED_ONEHAND`, `_KATANA`; the knife ->
// `_KNIFE`; the katana -> `_KATANA`; fists -> `_FISTS`; `packets-S6.md` item 3), else the input
// unchanged (no weapon, no row, or no playable target). The ladder itself is the name-keyed
// resolver's (`ElysiumActionTables::Translate`), asked through `MeleeSequencesForActivity`: the
// activity its answer's sequences carry IS the row's target. `OutSequences`, when given, receives
// that list (`GetSequencesForActivity` over the result).
int32 WeaponActivityOverride(int32 Activity, TArray<struct FElysiumNpcClip>* OutSequences = nullptr) const;

// --- The bridge row's descriptor facts (spec 0002 V4a, lane A2) ---------------------------------
//
// What the kernel reads off the studio sequence descriptor of a sequence NUMBER, cached per
// sequence-bridge row (the index is the row's; `FElysiumNpc::SequenceRows`). Filled on first ask
// from `IElysiumEmbodiment::GetNpcSequenceDescriptor` (the baked clip data); a body whose
// embodiment answers nothing keeps the zero record. Session state, as the rows are.
struct FSequenceDescriptorRow
{
	static constexpr int32 MaxFanCells = 9;   // FElysiumGaitSpeedTable::MaxCells
	bool bAsked = false;            // the embodiment was asked (or a test wrote the record)
	bool bKnown = false;            // it answered: the fields below are the baked descriptor's
	bool bStudioLooping = false;    // mstudioseqdesc_t::flags & 1
	float DurationSeconds = 0.f;    // SequenceDuration where the bake states it; 0 = the played length
	float TurnYawDegrees = 0.f;     // GetSequenceTurnYaw 0x1008f8f0, a sequence with no grid
	float GroundSpeedCm = 0.f;      // GetSequenceGroundSpeed 0x10091490, no grid, cm/s
	int32 FanCells = 0;             // a one-axis fan's cell count; 0 = no grid
	float FanAxisMin = 0.f;         // the fan's pose-parameter span, degrees
	float FanAxisMax = 0.f;
	float FanSpeedCm[MaxFanCells] = {};          // per-cell ground speed, cm/s
	float FanTurnYawDegrees[MaxFanCells] = {};   // per-cell turn yaw, degrees
	FString FanParameter;           // the pose parameter the axis binds (`move_yaw`)
	const TArray<struct FElysiumAnimEvent>* Events = nullptr;   // the event table; null = none
};
mutable TArray<FSequenceDescriptorRow> SequenceDescriptorRows;   // filled by a const read

// N19 (J1): the model's own sequence 0, which retail plays when a `LookupSequence` misses
// (`StartSequence 0x101a82d0`: `m_nSequence := 0`, `0x101a833d`; `ResetSequenceInfo` then plays it
// at rate 1.0, `0x10090a23`). The bridge's row 0 is this clip -- the body's `RawIndex 0` row
// (`IElysiumEmbodiment::GetBodyClipByRawIndex`). Kept beside the row table rather than in row 0's
// label so the trace keeps naming it `seq 0`. `bKnown` false: the body answered none and row 0
// plays nothing, as before.
struct FSequenceZeroClip
{
	bool bAsked = false;
	bool bKnown = false;
	FString Label;
	FString OwnerStem;
	bool bLoops = false;      // the clip's own STUDIO_LOOPING
	float Seconds = 0.f;      // the length the clip player last reported
};
FSequenceZeroClip SequenceZero;

// Asks the embodiment for the body's `RawIndex 0` clip once; answers whether row 0 has a clip.
bool ResolveSequenceZeroClip();

// The descriptor record of a bridge row, asked of the embodiment on first sight. Null for a number
// that names no row.
const FSequenceDescriptorRow* SequenceDescriptorRow(int32 Sequence) const;

// `SequenceDuration(seq)` as `GetSequenceYawSpeed 0x10091310` divides by it: the bake's cycle
// length where stated, else the length the clip player reported; 0 when neither is known.
float SequenceDurationSeconds(int32 Sequence) const;

// `m_flYawSpeed (+0x560) = GetSequenceYawSpeed(m_nSequence)` and `m_flGroundSpeed (+0x654) =
// GetSequenceGroundSpeed(m_nSequence)`: the two writes `StudioFrameAdvance 0x1008f120`
// (`0x1008f2e5`..`0x1008f306`) and `ResetSequenceInfo 0x10090950` both make.
void WriteSequenceSpeedWords();

// `CAI_BaseNPC::RunAnimation` `0x1026c540`: slot 250 `StudioFrameAdvance(0)` (the sequence clock),
// the `CAP_AIM_GUN` (`0x20000000`) arm into slot 538 `AimGun`, and the idle re-pick — a body outside
// SCRIPT/DEAD whose `m_Activity` is `ACT_IDLE` (1) and whose sequence has finished picks the next
// idle sequence of `m_TranslatedActivity` (weighted when the sequence loops, heaviest otherwise)
// and resets onto it. Answers the frame interval slot 250 answered; its one caller is `PostRun`.
float RunAnimation();

// +0x06f0 m_nSequence and +0x06f8 m_flCycle — the playing sequence index and its phase.
// `IsActivityFinished` compares the first against `IdealSequence` (+0x5ccc);
// `ForcePreTranslatedSequenceAndActivity` and the Troika scene-event arm write both.
int32 SequenceNumber = 0;

float SequenceCycle = 0.f;

// +0x0ff4 m_TranslatedActivity — the third of the activity triple
// `ForcePreTranslatedSequenceAndActivity` overwrites, beside `ActivityNumber` (+0x0fec, family
// Positions) and `IdealActivityNumber` (+0x0ff0, family Facing).
int32 TranslatedActivity = 0;

int32 SceneEventsAllocated = 0;          // +0x0a5c m_nAllocationCount

int32 SceneEventsGrowSize = 0;           // +0x0a60 m_nGrowSize — 0 doubles, -1 refuses to grow

// Which rung of that ladder answered, so a case can assert the PATH and not only the number. Not
// retail's: retail distinguishes the rungs by which `DevMsg` it printed.
enum class EResolveActivityRung : uint8
{
	Weighted,          // SelectWeightedSequence(translated) answered
	ScriptCustomMove,  // ACT_SCRIPT_CUSTOM_MOVE resolved the cine's own m_iszCustomMove by name
	CustomMoveIdle,    // that lookup missed, so ACT_IDLE (9) answered
	RunToWalk,         // the translated activity was 0x13 and 9 answered instead
	DispositionTable,  // ACT_DISPOSITION went through the Troika disposition resolver
	DispositionRetry,  // the whole request was retried as ACT_DISPOSITION (0xf1)
	SequenceZero,      // even ACT_DISPOSITION missed, so sequence 0
	None,
};

mutable EResolveActivityRung LastResolveActivityRung = EResolveActivityRung::None;

// `CUtlMemory::Grow` as `CBaseFlex::AddSceneEvent` inlines it: 0 becomes 2, then a zero grow step DOUBLES and a
// non-zero one ADDS, until the capacity covers `Needed`. Static so the arithmetic is assertable on
// its own; returns the new capacity, or `Current` when the grow step is -1 (external memory).
static int32 GrowSceneEventCapacity(int32 Current, int32 GrowSize, int32 Needed);

// `CBaseFlex::AddSceneEvent` — the base-line body of slot 286. Slot 286 ITSELF is
// `CAI_BaseNPCTroika`'s override (`0x102c1680`), which forwards here for every event type it does
// not claim, so this is a named method rather than the slot.
void AddSceneEventBase(const struct FElysiumSceneData* Scene, const struct FElysiumSceneEvent* Event);

// `m_hCine` (+0x5d74) resolving to a live entity — the port carries it as
// `FElysiumEntity::ScriptOwner`, which is what the shape map binds the offset to. Not a seam.
bool ScriptOwnerIsLive() const;

// `CBaseAnimating::LookupSequence(const char*)`: the sequence-bridge row of the clip the body's model
// authors under that name (`HasNpcClip` / `NpcClipOwner`, then `FElysiumNpc::SequenceRowFor`), or -1
// -- retail's own "this model authors no such sequence", the arm every caller branches on. The row's
// loop bit is the clip's own baked `STUDIO_LOOPING`; where the embodiment answers no descriptor
// (a headless world) K2's residue stands: the cine's `m_iszPlay` once, every other loops.
int32 LookupSequenceByName(const TCHAR* Name) const;

// `CBaseAnimating::GetSequenceActivity(int)`. **SEAM**, answering -1.
int32 SequenceActivityOf(int32 Sequence) const;

// `CBaseAnimating::ResetSequenceInfo` `0x10090950`. **SEAM**: the port's clip funnel owns rate and
// length, so this records that retail would have re-read them and does nothing else.
void ResetSequenceInfo();

// `0x10260a50`, the helper `ForcePreTranslatedSequenceAndActivity` hands its forced sequence to.
// **SEAM**: it is `SetSequence` plus the studio bookkeeping around it; the number is stored on
// `SequenceNumber` here and nothing downstream reads it yet.
void CommitForcedSequence(int32 Sequence);

// `ResolveActivityToSequence` `0x10272130` — the whole fallback ladder, including its own retry
// loop. Writes the three out-parameters exactly as retail writes `m_nIdealSequence`,
// `m_IdealTranslatedActivity` and `m_IdealWeaponActivity`.
void ResolveActivityToSequence(int32 Activity, int32& OutSequence, int32& OutTranslatedActivity,
	int32& OutWeaponActivity) const;

// `CAI_BaseNPC::TranslateActivity` `0x10271ff0`. **SEAM**: the recovered per-species translation is
// `Visual/ElysiumAnimationResolve.cpp`'s, keyed on activity NAMES, and there is no retail-numbered
// table at this tier — so it answers the activity unchanged, which is retail's own empty-table
// answer, and writes the weapon activity beside it.
int32 TranslateActivityNumber(int32 Activity, int32& OutWeaponActivity) const;

// The cine's `m_iszCustomMove` (`m_hCine + 0x5f50`), the sequence name the ACT_SCRIPT_CUSTOM_MOVE
// arm looks up: the resolved director's own word (story 5 fold A3), empty with no live cine.
FString ScriptCustomMoveSequenceName() const;

// `CAI_BaseNPC::SetIdealActivity` `0x10272650` — the whole body: activity 0 tail-jumps to slot 310,
// and every other activity stores `m_IdealActivity` and re-resolves the ideal triple beside it.
// Family Facing's `SetIdealActivityNumber` is the STORE half of this and is called from here rather
// than respelt; what this adds is the reset arm and the translation.
void SetIdealActivity(int32 Activity);

// +0x0170 m_flPrevAnimTime and +0x0174 m_flAnimTime — `CBaseEntity`'s animation clock.
// `AddSceneEvent` reads the second to stamp a queued event's start, `ProcessGestureSceneEvent`
// measures its cycle against it, and `ForcePreTranslatedSequenceAndActivity` zeroes the first.
// **SEAM-ADJACENT**: nothing in this substrate advances them yet, so they stand at 0 and every body
// that writes one writes retail's own value.
float PrevAnimTime = 0.f;

// `0x101a8ac0` `CanOverride` on the live `m_hCine` — the queue refusals `CanPlaySequence` puts in
// front of a second director (story 5 fold A3). An owner that is not a director (the port's
// choreographed scene also stands in `ScriptOwner`) answers true, the arm that lets the cine stand.
bool CineAllowsDynamicInteraction() const;
