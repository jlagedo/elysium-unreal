// `CBaseFlex`'s hand-written slot bodies and the members they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Included inside `class FElysiumFlex`
// (`ElysiumFlex.h`), after its generated slot surface; the definitions are in
// `Private/Substrate/ElysiumFlexSlotBodies.cpp`.

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

float AnimTime = 0.f;

TArray<FSceneEventRecord> SceneEvents;   // +0x0a58 the block, +0x0a64 the count

//
// `float m_flexWeight[128]`, retail's own array, indexed by flex-controller number. The two slot
// bodies that read and write it by INDEX (279 / 281) are 29c's, not this family's; this family
// carries the two that address it BY NAME (280 / 282) and the two expression writers above them.
static constexpr int32 NumFlexWeightSlots = 128;

float FlexWeight[NumFlexWeightSlots] = {};

/** How many times that release ran, which is the only observable it has here. */
int32 SceneEventReleases = 0;

// +0x0848 m_viewtarget — the world point `SetViewtarget` (slot 277) copies in. Retail networks it
// to the client, which is the hop this runtime makes with `FElysiumCombatCharacter::CurEyeTarget`;
// this is retail's own word, written by the kernel and read by nothing in this substrate yet.
FVector Viewtarget = FVector::ZeroVector;

// The studio flex-controller descriptor's `min`/`max` pair (`studiohdr + 0x164 + index * 0x14`,
// fields +0xc and +0x10), which slot 279 normalises a written weight through and slot 281
// de-normalises a read one through. **SEAM** — no studio header here, so it answers false and both
// slots take their own `min == max` arm, which passes the stored weight through untouched.
// This is 29c's named target for `0x100b5c50`; slot 281 is where its body actually lands.
bool FlexControllerRange(int32 Index, float& OutMin, float& OutMax) const;

// `CChoreoScene::GetTime()` (`0x1007dfa0`), one float at scene+0x7c. **SEAM**: `FElysiumSceneData`
// is the PARSED file and holds no clock — the clock lives on `FElysiumScenePlayer`, which the
// kernel does not reach — so it answers 0.
float SceneTimeOf(const struct FElysiumSceneData& Scene) const;

// `CBaseAnimating::SequenceDuration(int)`. **SEAM**, answering 0.
float SequenceDurationOf(int32 Sequence) const;

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

/** `thunk_FUN_10075b70(record.event)` — the release both scene-event removers call before they
 *  compact. **SEAM**: the port's scene events are parsed data owned by the scene asset and are not
 *  reference-counted; the call is recorded so the SEQUENCE is assertable. */
void ReleaseSceneEvent(const FSceneEventRecord& Record);
