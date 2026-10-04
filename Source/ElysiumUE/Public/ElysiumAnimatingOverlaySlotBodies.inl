// `CBaseAnimatingOverlay`'s hand-written slot bodies and the members they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Included inside `class FElysiumAnimatingOverlay`
// (`ElysiumAnimatingOverlay.h`), after its generated slot surface; the definitions are in
// `Private/Substrate/ElysiumAnimatingOverlaySlotBodies.cpp`.

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

FAnimOverlayLayer AnimOverlay[ElysiumOverlay::NumSlots];   // +0x0734, stride 0x30

static constexpr int32 NumFlinchRecords = 3;

FFlinchRecord Flinch[NumFlinchRecords];   // +0x07f4, stride 0x1c

// `FindGestureLayerByOwner` is `CBaseAnimatingOverlay::FindGestureLayer` `0x100994c0`, the scan
// slot 271 answers through (a stub answering 0 for "not found" would port the opposite of retail's
// search). Its siblings `SetLayer` (slot 268 `0x10099020`), `RemoveLayer` (269 `0x10099660`),
// `HasLayer` (270 `0x10099540`) and `AllocateLayer` (272 `0x10099470`) are hand-written beside it
// (spec 0002 V4o), under their generated slot declarations.
int32 FindGestureLayerByOwner(int32 Activity) const;

// `CBaseAnimatingOverlay::AddGesture` `0x100991b0`, with `0x100990f0` as its two slot calls: the
// layer already held for the activity answers its index (slots 270 / 271); else
// `SelectWeightedSequence(Activity)`, a sequence `< 1` refused; slot 272 `AllocateLayer`; slot 268
// `SetLayer(i, -1, seq, bAutoKill)`; then `m_nActivity = Activity`. The layer index, or -1.
int32 AddGesture(int32 Activity, bool bAutoKill);

// The layer half of slot 250 `CBaseAnimatingOverlay::StudioFrameAdvance` `0x10098bb0`: for each
// layer whose weight is not 0, `CAnimationLayer::StudioFrameAdvance` `0x10098830` with the interval
// the base body (`0x1008f120`) returned -- 0 included: the cycle then stays and the weight is still
// recomputed -- and on a finished auto-kill layer `m_flWeight = 0` and slot 112 `(i, m_nActivity)`.
void AdvanceOverlayLayers(float Interval);

// Not retail's: the body's notice that slot 268 `SetLayer` seeded layer `Layer`, where the DRAW of
// the layer's clip starts (visual-only; the kernel's cycle, events and finish are the record above).
// The base draws nothing; `FElysiumNpc` plays the row's clip on the `UpperBody` channel.
virtual void OnOverlayLayerSet(int32 Layer) { (void)Layer; }

// `CBaseAnimating::GetSequenceFlags(int)` / `GetSeqDesc(seq)->flags`, from the sequence bridge's
// row: bit 0 `STUDIO_LOOPING` (the gesture arm of `AddSceneEvent` warns about it; the layer
// advance's wrap), bit 1 the SNAP bit `SetLayer` zeroes an envelope for
// (`ElysiumOverlay::BlendFor`). 0 on a body with no bridge.
int32 SequenceFlagsOf(int32 Sequence) const;

// `CBaseAnimating::GetSequenceCycleRate(int)` `0x10091230` as the layer bodies read it for a
// layer's sequence: the bridge row's (`FElysiumNpc::SequenceCycleRateOf`). 0 on a body with no bridge.
float OverlaySequenceCycleRate(int32 Sequence) const;

// `CBaseAnimating::LookupPoseParameter(const char*)`. **SEAM**, answering -1.
int32 LookupPoseParameter(const TCHAR* Name) const;

/** `0x100994c0`'s companion read, exposed so a case can assert slot 271 against the table's own
 *  starting index. `GetFirstGestureLayer()` (slot 267) answers 0 for every class in the hierarchy;
 *  retail's scan starts THERE and refuses outright when it is 4 or more. */
int32 FirstGestureLayerOrRefusal() const;

// `CBaseAnimating::SelectWeightedSequence(Activity, -1)` on the base line and
// `CAI_BaseNPCTroika`'s stat-filtered twin (`0x10295460`) on the Troika line: which sequence this
// body would play for an activity, or -1 when it authors none. The ladder branches on `!= -1`.
// **SEAM** — this substrate resolves activities by NAME through the action tables and its animating
// tier stands no sequence index, so this answers -1 and the ladder falls through to its `ACT_IDLE`
// tail, which is retail's own answer for a body with no turn clips.
int32 SelectWeightedSequenceForActivity(int32 Activity) const;
