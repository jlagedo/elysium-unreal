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

// Three bodies of the same table that are NOT this family's rows — slots 268, 271 and 272 are still
// 29c's `FireKernelSlot` stubs — but whose BODIES this family's rows dispatch through, so they are
// reproduced here as named methods beside the stubs. A family that ported `HasLayer`,
// `RemoveLayerByOwner` and `RestartGesture` onto a stub that answers 0 for "not found" would have
// ported the opposite of retail's search.
//
// `SetOverlayLayer` is `CBaseAnimatingOverlay::SetLayer` `0x10099020`, field for field and in
// retail's own write order; `FindGestureLayerByOwner` is `FindGestureLayer` `0x100994c0`;
// `AllocateGestureLayer` is `AllocateLayer` `0x10099470`.
void SetOverlayLayer(int32 SlotIndex, int32 Activity, int32 Sequence, bool bAutoKill);

int32 FindGestureLayerByOwner(int32 Activity) const;

int32 AllocateGestureLayer() const;

// The studio flex-controller descriptor's `min`/`max` pair (`studiohdr + 0x164 + index * 0x14`,
// fields +0xc and +0x10), which slot 279 normalises a written weight through and slot 281
// de-normalises a read one through. **SEAM** — no studio header here, so it answers false and both
// slots take their own `min == max` arm, which passes the stored weight through untouched.
// This is 29c's named target for `0x100b5c50`; slot 281 is where its body actually lands.
bool FlexControllerRange(int32 Index, float& OutMin, float& OutMax) const;

// `CUtlMemory::Grow` as `0x100b5e60` inlines it: 0 becomes 2, then a zero grow step DOUBLES and a
// non-zero one ADDS, until the capacity covers `Needed`. Static so the arithmetic is assertable on
// its own; returns the new capacity, or `Current` when the grow step is -1 (external memory).
static int32 GrowSceneEventCapacity(int32 Current, int32 GrowSize, int32 Needed);

// `CBaseFlex::AddSceneEvent` `0x100b5e60` — the base-line body of slot 286. Slot 286 ITSELF is
// `CAI_BaseNPCTroika`'s override (`0x102c1680`), which forwards here for every event type it does
// not claim, so this is a named method rather than the slot.
void AddSceneEventBase(const struct FElysiumSceneData* Scene, const struct FElysiumSceneEvent* Event);

// `CChoreoScene::GetTime()` (`0x1007dfa0`), one float at scene+0x7c. **SEAM**: `FElysiumSceneData`
// is the PARSED file and holds no clock — the clock lives on `FElysiumScenePlayer`, which the
// kernel does not reach — so it answers 0.
float SceneTimeOf(const struct FElysiumSceneData& Scene) const;

// `m_hCine` (+0x5d74) resolving to a live entity — the port carries it as
// `FElysiumEntity::ScriptOwner`, which is what the shape map binds the offset to. Not a seam.
bool ScriptOwnerIsLive() const;

// `CBaseAnimating::LookupSequence(const char*)`. **SEAM**, answering -1 — retail's own "this model
// authors no such sequence" value, which is the arm every caller below already has a branch for.
int32 LookupSequenceByName(const TCHAR* Name) const;

// `CBaseAnimating::GetSequenceFlags(int)`. **SEAM**, answering 0. Bit 0 is the "looping" flag the
// gesture arm of `AddSceneEvent` warns about; bit 1 is the SNAP bit `SetLayer` zeroes an envelope
// for (`ElysiumOverlay::BlendFor`).
int32 SequenceFlagsOf(int32 Sequence) const;

// `CBaseAnimating::GetSequenceActivity(int)`. **SEAM**, answering -1.
int32 SequenceActivityOf(int32 Sequence) const;

// `CBaseAnimating::SequenceDuration(int)`. **SEAM**, answering 0.
float SequenceDurationOf(int32 Sequence) const;

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

//
// Three records of 0x1c bytes. `AddFlinchGesture` (slot 265) is the only writer in this band.
struct FFlinchRecord
{
	int32 Sequence = 0;          // +0x00 nSequence
	int32 Latch = 0;             // +0x04 nLatch — `(old + 1) & 3`, retail's own 2-bit rotation
	float FadeIn = 0.f;          // +0x08 flFadeIn
	float FadeOut = 0.f;         // +0x0c flFadeOut
	int32 PoseParamIndex = 0;    // +0x10 nPoseParamIndex — seeded 0x18 before the lookup
	float PoseParamValue = 0.f;  // +0x14 flPoseParamValue
	float ExpireTime = 0.f;      // +0x18 flExpireTime
};

// +0x0170 m_flPrevAnimTime and +0x0174 m_flAnimTime — `CBaseEntity`'s animation clock.
// `AddSceneEvent` reads the second to stamp a queued event's start, `ProcessGestureSceneEvent`
// measures its cycle against it, and `ForcePreTranslatedSequenceAndActivity` zeroes the first.
// **SEAM-ADJACENT**: nothing in this substrate advances them yet, so they stand at 0 and every body
// that writes one writes retail's own value.
float PrevAnimTime = 0.f;

bool bNoFlinch = false;   // +0x0730 m_bNoFlinch

static constexpr int32 NumFlinchRecords = 3;

FFlinchRecord Flinch[NumFlinchRecords];   // +0x07f4, stride 0x1c

//
// `float m_flexWeight[128]`, retail's own array, indexed by flex-controller number. The two slot
// bodies that read and write it by INDEX (279 / 281) are 29c's, not this family's; this family
// carries the two that address it BY NAME (280 / 282) and the two expression writers above them.
static constexpr int32 NumFlexWeightSlots = 128;

float FlexWeight[NumFlexWeightSlots] = {};

// `CBaseAnimating::GetNumFlexControllers`. **SEAM** — this substrate's animating tier stands no
// studio header, so it answers 0 and every name lookup below walks an empty table.
int32 NumFlexControllers() const;

// `CBaseAnimating::GetFlexControllerName(int)`. **SEAM**, answering an empty name.
FString FlexControllerName(int32 Index) const;

// `LookupFlexController` `0x100b5d10` — the real body: a linear `__strcmpi` scan over
// `GetNumFlexControllers()`, re-reading the count every iteration. **It answers 0, not -1, when
// nothing matches**, which is retail's own behaviour and the reason a misspelt flex name writes
// controller zero rather than being dropped. Ported verbatim; the two sub-calls are the seams above.
int32 LookupFlexController(const TCHAR* Name) const;

// `0x101a8ac0` — the dynamic-interaction check `CanPlaySequence` puts in front of a live cine.
// **SEAM**: this substrate models no dynamic scripted interaction, so it answers TRUE, the arm that
// lets the cine stand; answering false would make every scripted body refuse every sequence.
bool CineAllowsDynamicInteraction() const;

// `CBaseAnimating::LookupPoseParameter(const char*)`. **SEAM**, answering -1.
int32 LookupPoseParameter(const TCHAR* Name) const;

// `0x100c43e0` — the studio pose-parameter normaliser `AddFlinchGesture` runs its authored value
// through before stashing it. **SEAM**: with no studio header there is no range, so it answers the
// value unchanged, which is what a 0..1 parameter's identity range gives.
float NormalizePoseParameter(int32 Index, float Value) const;

// `CBaseFlex::PlayScene`'s `instanced_scripted_scene` (`0x10084b40`). **SEAM**: standing a scene
// entity from the kernel needs the world's entity factory and the scene cache, neither of which the
// substrate's NPC reaches; it answers -1, the "unknown scene" length, and `PlayScene` then takes
// retail's own `Msg("Unknown scene specified: %s")` arm.
float PlayInstancedScene(const TCHAR* SceneFile);
