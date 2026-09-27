// `CAI_BaseNPC`'s declarations of the `Anim` family (story 5 step 5),
// moved from `ElysiumNpcAnim*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseAnim.cpp`.

// +0x065c m_bSequenceFinished — the byte `IsActivityFinished` (slot 251) tests first. Family Hints
// reaches the same word through its own read-only seam `IsHintSequenceFinished()`, which answers
// false because no port member carried it; this IS the member, and the two should be joined the day
// the animating tier advances a sequence.
bool bSequenceFinished = false;

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

// `CUtlMemory::Grow` as `0x100b5e60` inlines it: 0 becomes 2, then a zero grow step DOUBLES and a
// non-zero one ADDS, until the capacity covers `Needed`. Static so the arithmetic is assertable on
// its own; returns the new capacity, or `Current` when the grow step is -1 (external memory).
static int32 GrowSceneEventCapacity(int32 Current, int32 GrowSize, int32 Needed);

// `CBaseFlex::AddSceneEvent` `0x100b5e60` — the base-line body of slot 286. Slot 286 ITSELF is
// `CAI_BaseNPCTroika`'s override (`0x102c1680`), which forwards here for every event type it does
// not claim, so this is a named method rather than the slot.
void AddSceneEventBase(const struct FElysiumSceneData* Scene, const struct FElysiumSceneEvent* Event);

// `m_hCine` (+0x5d74) resolving to a live entity — the port carries it as
// `FElysiumEntity::ScriptOwner`, which is what the shape map binds the offset to. Not a seam.
bool ScriptOwnerIsLive() const;

// `CBaseAnimating::LookupSequence(const char*)`. **SEAM**, answering -1 — retail's own "this model
// authors no such sequence" value, which is the arm every caller below already has a branch for.
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
// arm looks up. **SEAM**: the port's scripted sequence carries its custom-move label on the
// scripted-sequence entity rather than on a `CCineNPC` the kernel can reach by offset, so this
// answers empty and the arm takes retail's `"SCRIPT_CUSTOM_MOVE: %s has no sequence"` branch.
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

// `0x101a8ac0` — the dynamic-interaction check `CanPlaySequence` puts in front of a live cine.
// **SEAM**: this substrate models no dynamic scripted interaction, so it answers TRUE, the arm that
// lets the cine stand; answering false would make every scripted body refuse every sequence.
bool CineAllowsDynamicInteraction() const;

// `CBaseFlex::PlayScene`'s `instanced_scripted_scene` (`0x10084b40`). **SEAM**: standing a scene
// entity from the kernel needs the world's entity factory and the scene cache, neither of which the
// substrate's NPC reaches; it answers -1, the "unknown scene" length, and `PlayScene` then takes
// retail's own `Msg("Unknown scene specified: %s")` arm.
float PlayInstancedScene(const TCHAR* SceneFile);
