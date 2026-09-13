// Story 29c-1, family **Anim** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelAnim.cpp` and the tests in
// `Tests/ElysiumNpcKernelAnimTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The family is the gesture-layer table, the flex/expression controllers, the scene-event queue and
// the activity/sequence commit. Two of its four concerns are retail's own DATA STRUCTURES —
// `CBaseAnimatingOverlay::m_AnimOverlay` and `CBaseFlex::m_SceneEvents` — whose bookkeeping IS the
// rule, so the arrays, the strides, the search order and the compaction below are retail's verbatim.
// The other two are MECHANISMS in an animation instance (a pose parameter, a flex weight, a
// sequence index), and each of those goes through a seam that names the retail call it stands for.

// --- Words this family needed that 29b did not declare ------------------------------------------
//
// Every one sits OUTSIDE the hand-written shape map's band (`ElysiumNpcKernelShapeMap.cpp` binds
// `0x1a40`..`0x665a`, the words `CAI_BaseNPC` itself carries): they are `CBaseAnimating`'s,
// `CBaseAnimatingOverlay`'s, `CBaseFlex`'s, `CBaseCombatCharacter`'s and one species leaf's own.
// `docs/vtmb/npc-kernel/layout.md` types and names all of them.

// +0x0170 m_flPrevAnimTime and +0x0174 m_flAnimTime — `CBaseEntity`'s animation clock.
// `AddSceneEvent` reads the second to stamp a queued event's start, `ProcessGestureSceneEvent`
// measures its cycle against it, and `ForcePreTranslatedSequenceAndActivity` zeroes the first.
// **SEAM-ADJACENT**: nothing in this substrate advances them yet, so they stand at 0 and every body
// that writes one writes retail's own value.
float PrevAnimTime = 0.f;
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

// +0x067c m_nBody — the model's body-submodel selector `BodyGroup` (`0x10398800`) picks.
int32 NpcBody = 0;

// +0x0ff4 m_TranslatedActivity — the third of the activity triple
// `ForcePreTranslatedSequenceAndActivity` overwrites, beside `ActivityNumber` (+0x0fec, family
// Positions) and `IdealActivityNumber` (+0x0ff0, family Facing).
int32 TranslatedActivity = 0;

// +0x1590 m_bCutsceneForceLOD — the sendtable-registered byte slots 59/60/61 raise and clear beside
// `bInChoreoScene`. The census calls it "unbound"; `layout.md` names it, and this is the member.
bool bCutsceneForceLOD = false;

// +0x6710 m_iSeveredTentacleMask (`CNPC_VMingXiao`) — the body-submodel mask `BodyGroup` copies into
// `m_nBody` when its cvar is at the default. A species word above the shape map's band.
int32 SeveredTentacleMask = 0;

// --- `CBaseAnimatingOverlay`'s gesture-layer table (+0x0730 … +0x07c3) --------------------------
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

bool bNoFlinch = false;   // +0x0730 m_bNoFlinch
FAnimOverlayLayer AnimOverlay[ElysiumOverlay::NumSlots];   // +0x0734, stride 0x30

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

// --- `CBaseAnimatingOverlay`'s flinch table (+0x07f4 … +0x0847) ---------------------------------
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

static constexpr int32 NumFlinchRecords = 3;
FFlinchRecord Flinch[NumFlinchRecords];   // +0x07f4, stride 0x1c

// --- `CBaseFlex`'s flex weights (+0x0858) -------------------------------------------------------
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

// The studio flex-controller descriptor's `min`/`max` pair (`studiohdr + 0x164 + index * 0x14`,
// fields +0xc and +0x10), which slot 279 normalises a written weight through and slot 281
// de-normalises a read one through. **SEAM** — no studio header here, so it answers false and both
// slots take their own `min == max` arm, which passes the stored weight through untouched.
// This is 29c's named target for `0x100b5c50`; slot 281 is where its body actually lands.
bool FlexControllerRange(int32 Index, float& OutMin, float& OutMax) const;

// --- `CBaseFlex`'s scene-event queue (+0x0a58 … +0x0a6b) ----------------------------------------
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

// --- The Troika scene-event arms' own inputs ----------------------------------------------------

// `m_hDialogPartner` (+0x0fe8) resolving to a live entity — the gate the Silence and Python arms of
// slot 286 open with. This runtime carries the partner as the open dialogue SESSION rather than as a
// handle on the NPC, which is the reading `ElysiumNpcKernelSounds.cpp` already made for
// `CAI_BaseNPCTroika::IsInDialog`; repeated here as a function rather than a second reading.
bool HasLiveDialogPartner() const;

// `0x100ec5d0(&g_DispositionTable, m_nCurrDisposition, &threshold, &chance)` — the disposition row's
// stance-reaction pair (record +0x108 and +0x10c), with the index clamped to the table's default
// when it is out of range. **SEAM**: `FElysiumDisposition` carries the row's animation, fidget and
// eye blocks and not this pair, so it answers false and the Silence arm refuses.
bool DispositionStanceReaction(float& OutThreshold, float& OutChancePercent) const;

// `0x100ec450(&g_DispositionTable, m_nCurrDisposition, &fadeIn, &fadeOut, &min, &max, &unused)` —
// picks one of the row's authored expression names at random (`RandomInt(0, record[+0x21c] - 1)`
// over a 0x40-byte string table at record +0x11c) and hands back the four floats at +0x220..+0x22c.
// **SEAM**: the same row block this runtime does not carry; answers false.
bool DispositionLoudExpression(FString& OutExpression, float& OutFadeIn, float& OutFadeOut,
	float& OutMinLevel, float& OutMaxLevel) const;

// `CBaseCombatCharacter::AddScriptedExpression(name, fadeIn, fadeOut, scale, delay, duration)`.
// **SEAM**: the expression list is `CUtlVector<ScriptedExpression_t>` at +0x1568 and no port member
// claims it (`docs/vtmb/npc-kernel/layout.md`). Recorded so the two arms that raise one are
// assertable; read by the test and by nothing else.
struct FScriptedExpressionRequest
{
	FString Expression;
	float FadeIn = 0.f;
	float FadeOut = 0.f;
	float Scale = 0.f;
	float Delay = 0.f;
	float Duration = 0.f;
};
TArray<FScriptedExpressionRequest> ScriptedExpressions;
void AddScriptedExpression(const FString& Expression, float FadeIn, float FadeOut, float Scale,
	float Delay, float Duration);

// `ChangeStance` `0x102c1230` as slot 286's Silence arm consumes it: retail's body writes the new
// stance and the stamp and leaves the disposition table's TRANSITION SEQUENCE in `EAX`, which the
// caller reads as a sequence index. `ElysiumStance::ChangeStance` is that body in this runtime and
// answers by CLIP NAME, so this drives it over this NPC's own stance state and resolves the clip
// back through `LookupSequenceByName`.
int32 ChangeStanceForReaction();

// `CDialogDependency::CallPyDialogFunc(func, partner, this, 0x102, nullptr)` — slot 286's Python
// arm. **SEAM**: the port's Python dialogue surface is reached through the dialogue session rather
// than from the kernel, so the request is recorded and nothing is called.
TArray<FString> PythonDialogCalls;
void CallPythonDialogFunction(const FString& FunctionName);

// `0x101a8ac0` — the dynamic-interaction check `CanPlaySequence` puts in front of a live cine.
// **SEAM**: this substrate models no dynamic scripted interaction, so it answers TRUE, the arm that
// lets the cine stand; answering false would make every scripted body refuse every sequence.
bool CineAllowsDynamicInteraction() const;

// `m_hCine` (+0x5d74) resolving to a live entity — the port carries it as
// `FElysiumEntity::ScriptOwner`, which is what the shape map binds the offset to. Not a seam.
bool ScriptOwnerIsLive() const;

// `CSceneEntity + 0x57d`, the byte slots 59/60/61 gate `m_bCutsceneForceLOD` on. **SEAM** — the
// scene entity reaches the kernel as an opaque pointer through the generated `void*` signature and
// this substrate stands no `CSceneEntity` layout, so it answers false and the LOD byte is left
// alone, which is retail's own answer for a scene that does not force LOD.
bool SceneEntityForcesCutsceneLod(const void* SceneEntity) const;

// --- The animating-tier mechanisms every activity body ends in ----------------------------------

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

// --- The rows that fill no slot -----------------------------------------------------------------

// `CNPC_VPlayerController::AddExtraAnimationModels` `0x103a49c0` — slot 245's species branch. The
// slot itself is `CBaseAnimating`'s (`0x1008e0a0`) and stays the generator's stub; this is the
// override `CNPC_VFrenzyShadow`, `CNPC_VPlayerController` and `CNPC_VWolfMorph` share.
void AddExtraAnimationModelsPlayerController(FElysiumEntity* ExtraModel, const TCHAR* AttachmentA,
	const TCHAR* AttachmentB, int32 FlagsA, int32 FlagsB);

// `CNPC_VPlayerController::RemoveExtraAnimationModels` `0x103a4a60` — slot 246's species branch.
void RemoveExtraAnimationModelsPlayerController();

// `CBaseAnimating::AddExtraAnimationModels` / `RemoveExtraAnimationModels` (`0x1008e0a0` /
// `0x1008e310`), the base halves the two bodies above call first. **SEAM**: this runtime composes
// extra models through the character catalog rather than through a per-entity list on the NPC, so
// the request is recorded and the catalog is not touched. Read by the test and by nothing else.
struct FExtraAnimationModelRequest
{
	FElysiumEntityHandle Model;
	FString AttachmentA;
	FString AttachmentB;
	int32 FlagsA = 0;
	int32 FlagsB = 0;
	// Whether the forwarding arm reached a possessing player's `+0xa8` object (slot 245's `+0x3d4`
	// / slot 246's `+0x3d8` hop). False is the arm that warns instead.
	bool bForwardedToMaster = false;
};
TArray<FExtraAnimationModelRequest> ExtraAnimationModels;

// Slot 97 `GetOwnerEntity()` and that entity's `+0xa8` (`m_pPlayer`), which is the forwarding gate
// both bodies above test. Not a seam: this runtime's player entity is the one the world names.
bool OwnerIsThePlayer() const;

// `CNPC_VMingXiao::BodyGroup` `0x10398800` — writes `m_nBody` (+0x067c) from the severed-tentacle
// mask or from its own cvar. Three arms, in retail's order.
void BodyGroup();

// The cvar `BodyGroup` reads (`DAT_1093bb14`, through `ConVar::IsCommand()` and `m_nValue`). Its
// NAME and its DEFAULT are **unrecovered** — the pointer lives in uninitialised `.data` and no
// corpus function constructs it. **SEAM**: `bIsCommand` answers false and `Value` answers -1, which
// is the arm that takes the tentacle mask, i.e. the shipped default behaviour.
bool BodyGroupCvarIsCommand() const;
int32 BodyGroupCvarValue() const;

// `0x102b8a10` — gate `COND_ENEMY_DEAD` (0x58), then answer retail schedule number 8 only when the
// body authors a sequence for activity 0x61. Retail stamps its selector trace (+0x1b30 the source
// file, +0x1b34 line 0x5f20) on the way out; the shape map calls that word ABSENT because this
// runtime records selections in the mind's transition trace. Answers 0 (SCHED_NONE) otherwise.
int32 IdleSequenceGate() const;

// `ResolveActivityToSequence` `0x10272130` — the whole fallback ladder, including its own retry
// loop. Writes the three out-parameters exactly as retail writes `m_nIdealSequence`,
// `m_IdealTranslatedActivity` and `m_IdealWeaponActivity`.
void ResolveActivityToSequence(int32 Activity, int32& OutSequence, int32& OutTranslatedActivity,
	int32& OutWeaponActivity) const;

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

// `CAI_BaseNPC::TranslateActivity` `0x10271ff0`. **SEAM**: the recovered per-species translation is
// `Visual/ElysiumAnimationResolve.cpp`'s, keyed on activity NAMES, and there is no retail-numbered
// table at this tier — so it answers the activity unchanged, which is retail's own empty-table
// answer, and writes the weapon activity beside it.
int32 TranslateActivityNumber(int32 Activity, int32& OutWeaponActivity) const;

// `0x10295a80`, the Troika disposition resolver `ResolveActivityToSequence` hands ACT_DISPOSITION
// to when `m_pBaseNPCTroika` (+0x98) is set — which it always is on a spawned NPC. **SEAM**: the
// stance machine that answers it is `ElysiumStance::Select` and it answers by CLIP NAME, so there
// is no sequence index to give back; it leaves the sequence at -1 and the ladder falls through to
// retail's own `"%s has no sequence for act ACT_DISPOSITION"` arm.
void ResolveDispositionActivity(int32& OutSequence, int32& OutTranslatedActivity) const;

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

// `CAI_BaseHumanoid::vfunc250` `0x1025e4e0` — slot 250's humanoid branch. The slot itself is
// `CBaseAnimating`'s (`0x10098bb0`) and stays the generator's stub. Clears the two cached
// head/eye-direction bits family Facing declared (`HumanoidHeadCacheBits`, +0x5f4c) so the next
// read recomputes, then chains to the base.
float StudioFrameAdvanceHumanoid(float Interval);

// `CNPC_VCamera::HandleAnimEvent` `0x10368ec0` — slot 259's camera branch, an EMPTY body that
// swallows every animation event. `FElysiumNpc::HandleAnimEvent` reads this before its footstep arm.
// True for the two census classes that carry the empty override.
bool SwallowsAnimEvents() const;
