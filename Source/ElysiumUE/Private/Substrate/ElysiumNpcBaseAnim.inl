// `CAI_BaseNPC`'s declarations of the `Anim` family (story 5 step 5),
// moved from `ElysiumNpcAnim*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseAnim.cpp`.

float AnimTime = 0.f;

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

//
// Four records of 0x30 bytes from +0x0734, with `m_bNoFlinch` at +0x0730 just before them. This is
// the SAME table `FElysiumOverlayStack` (`Public/ElysiumOverlayStack.h`) carries on the render side
// — same four slots, same 0.1 seed weight, same 1.0 ceiling, same 0.2 blend fractions, same
// "occupancy IS the zero-weight test" — and the shared numbers are taken from `ElysiumOverlay::`
// rather than respelt, so the two cannot disagree. What the render stack does NOT carry is retail's
// two integer keys: `m_nSequence` (a studio index) and `m_nActivity` (the OWNER activity every
// lookup in this family searches by). The render stack keys a layer by its resolved clip LABEL,
// which is a name and cannot answer `FindLayerByOwner(Activity)`. So the kernel keeps retail's own
// record, and the day the animating tier stands sequence indices the two become one table.
struct FAnimOverlayLayer
{
	int32 Flags = 0;              // +0x00 m_fFlags — `SetLayer` does NOT write it
	int32 SequenceFinished = 0;   // +0x04 m_fSequenceFinished
	int32 Sequence = 0;           // +0x08 m_nSequence (+0x073c on layer 0)
	float Cycle = 0.f;            // +0x0c m_flCycle
	float PlaybackRate = 0.f;     // +0x10 m_flPlaybackRate
	float Weight = 0.f;           // +0x14 m_flWeight — the occupancy marker
	float WeightMax = 0.f;        // +0x18 m_flWeightMax
	float BlendIn = 0.f;          // +0x1c m_flBlendIn
	float BlendOut = 0.f;         // +0x20 m_flBlendOut
	int32 Activity = 0;           // +0x24 m_nActivity — the owner key, -1 when none
	bool bAutoKillWhenFinished = false;  // +0x28 m_bAutoKillWhenFinished
	float LastEventCheck = 0.f;   // +0x2c m_flLastEventCheck
};

FAnimOverlayLayer AnimOverlay[ElysiumOverlay::NumSlots];   // +0x0734, stride 0x30

//
// Retail's `CUtlVector<CSceneEventInfo>`: a 0x1c-byte record, a heap block at +0x0a58, its capacity
// at +0x0a5c, its grow step at +0x0a60, its element count at +0x0a64 and a debug mirror of the
// block pointer at +0x0a68 that nothing reads back. The growth arithmetic, the always-at-the-end
// insert and the `memmove` compaction the two removers perform are the rule this family ports, so
// the capacity and the grow step are carried as members even though `TArray` would not need them.
struct FSceneEventRecord
{
	// +0x00 the CChoreoEvent, +0x04 the CChoreoScene. `RemoveSceneEvent` searches by the first and
	// `ClearSceneEvents` by the second, which is why both are kept rather than one.
	const struct FElysiumSceneEvent* Event = nullptr;
	const struct FElysiumSceneData* Scene = nullptr;
	// +0x08 the byte `AddFlexAnimation` raises when it has resolved this record's controllers.
	bool bResolved = false;
	// +0x0c. Seeded -1 by both live arms of `AddSceneEvent` and read as a "is this record armed"
	// gate by all three `Process*` bodies (`param_1[3] >= 0`), which is why a record that took
	// neither arm is inert: retail leaves +0x0c and +0x10 UNINITIALISED for every other event type,
	// and this port leaves them at the seeded -1 instead. **Divergence, named**: reading a retail
	// uninitialised stack word is not reproducible, and -1 is the value both live arms write.
	int32 Handle = -1;
	// +0x10 the sequence `LookupSequence` resolved for a gesture or sequence event.
	int32 Sequence = -1;
	// +0x14 the animation-clock stamp the event's cycle is measured from.
	float StartTime = 0.f;
	// +0x18. Retail never writes it in `AddSceneEvent`; carried so the record is retail's 7 words.
	int32 Reserved = 0;

	// NOT retail's: the two numbers `ProcessGestureSceneEvent` and `ProcessSequenceSceneEvent`
	// COMPUTE AND DISCARD (`0x100b7040` / `0x100b70e0` both end each call with `FSTP ST0`). They are
	// recorded rather than dropped so the arithmetic those bodies perform is assertable without
	// inventing an effect retail does not have.
	float LastGestureCycle = 0.f;
	float LastIntensity = 0.f;
};

TArray<FSceneEventRecord> SceneEvents;   // +0x0a58 the block, +0x0a64 the count

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
