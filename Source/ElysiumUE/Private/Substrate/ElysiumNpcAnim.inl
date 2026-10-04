// Story 29c-1, family **Anim** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcAnim.cpp` and the tests in
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

// +0x067c m_nBody — the model's body-submodel selector `BodyGroup` (`0x10398800`) picks.
int32 NpcBody = 0;

// +0x1590 m_bCutsceneForceLOD — the sendtable-registered byte slots 59/60/61 raise and clear beside
// `bInChoreoScene`. The census calls it "unbound"; `layout.md` names it, and this is the member.
bool bCutsceneForceLOD = false;

// --- `CBaseAnimatingOverlay`'s gesture-layer table (+0x0730 … +0x07c3) --------------------------

// --- `CBaseAnimatingOverlay`'s flinch table (+0x07f4 … +0x0847) ---------------------------------

// --- `CBaseFlex`'s flex weights (+0x0858) -------------------------------------------------------

// --- `CBaseFlex`'s scene-event queue (+0x0a58 … +0x0a6b) ----------------------------------------

// --- The Troika scene-event arms' own inputs ----------------------------------------------------

// `m_hDialogPartner` (+0x0fe8) resolving to a live entity — the gate the Silence and Python arms of
// slot 286 open with. This runtime carries the partner as the open dialogue SESSION rather than as a
// handle on the NPC, which is the reading `ElysiumNpcSounds.cpp` already made for
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

// `CSceneEntity + 0x57d`, the byte slots 59/60/61 gate `m_bCutsceneForceLOD` on. **SEAM** — the
// scene entity reaches the kernel as an opaque pointer through the generated `void*` signature and
// this substrate stands no `CSceneEntity` layout, so it answers false and the LOD byte is left
// alone, which is retail's own answer for a scene that does not force LOD.
bool SceneEntityForcesCutsceneLod(const void* SceneEntity) const;

// --- The animating-tier mechanisms every activity body ends in ----------------------------------

// --- The bridge row's words for an overlay layer (spec 0002 V4o, lane O1) -----------------------

// `GetSeqDesc(seq)->flags & 2` (`STUDIO_SNAP`), which slot 268 `SetLayer` `0x10099020` zeroes both
// blends for (`0x100990a4`): the clip's baked bit (`FElysiumNpcClip::IsSnap()`), on the bridge row
// (`FSequenceRow::bSnap`), taken when the row is numbered (`SequenceRowFor`: by activity, by name,
// the stance set). False for row 0 and for an unknown number.
bool SequenceSnaps(int32 Sequence) const;

// `GetSequenceCycleRate(seq)` `0x10091230` as the layer bodies `0x10098830` / `0x10098cd0` read
// it: 1 / the row's length (`SequenceDurationSeconds`: the baked cycle length, else the length the
// last play or the layer's draw reported) when that is above 0, else retail's other arm, the float
// 10.0 at `0x1044e664` (`0x100912c8`) -- with one Warning per (model, clip), because in the port
// a row with no length is also one nothing has played yet.
float SequenceCycleRateOf(int32 Sequence) const;

// The layer's draw (visual-only): the row's clip through `PlayAnimSegment` on the `UpperBody`
// channel, one-shot, with the row's snap bit; the answered length is stored on the row. The
// kernel's layer words read nothing back from the body but that length.
virtual void OnOverlayLayerSet(int32 Layer) override;

// --- The rows that fill no slot -----------------------------------------------------------------

// `CBaseAnimating::AddExtraAnimationModels` / `RemoveExtraAnimationModels` (`0x1008e0a0` /
// `0x1008e310`), the base halves `CNPC_VPlayerController`'s slot 245/246 bodies (`0x103a49c0` /
// `0x103a4a60`, `FElysiumNpcPlayerController`, fold A2) call first. **SEAM**: this runtime composes
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
// the controller line's bodies test (245, 246, 300, 442). Not a seam: this runtime's player entity
// is the one the world names.
bool OwnerIsThePlayer() const;

// `0x102b8a10` — gate `COND_ENEMY_DEAD` (0x58), then answer retail schedule number 8 only when the
// body authors a sequence for activity 0x61. Retail stamps its selector trace (+0x1b30 the source
// file, +0x1b34 line 0x5f20) on the way out; the shape map calls that word ABSENT because this
// runtime records selections in the mind's transition trace. Answers 0 (SCHED_NONE) otherwise.
int32 IdleSequenceGate() const;

// `0x10295a80`, the Troika disposition resolver `ResolveActivityToSequence` hands ACT_DISPOSITION
// to when `m_pBaseNPCTroika` (+0x98) is set — which it always is on a spawned NPC: `*seq = slot 611`
// (`0x102c12a0`, the stance machine), whose clip the sequence bridge numbers.
void ResolveDispositionActivity(int32& OutSequence, int32& OutTranslatedActivity) const;

