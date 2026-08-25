#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"

// Retail's `CBaseAnimatingOverlay` layer stack: four game-pushed animation layers, each carrying its
// own sequence, cycle, playback rate, weight envelope and lifetime, composed over whatever owns the
// base pose and faded independently of it (LIFE10).
//
// **A peer of the studio autolayer mechanism, not a member of it.** A base clip's `autolayers`
// binding is data the model declares and travels with the clip; a layer here is pushed by game code
// and outlives any number of base selections underneath it. That is why no walk of the studio data
// reaches `_attack_layer` or `_reload_layer` from a gait however deep it goes, and why an armed body
// without this stack composes a strict subset of retail's channels
// (`docs/vtmb/animation_rig_resolution.md` -> "A composed pose is a subset").
//
// Pure C++: requests in, layer state out. No UObject, no asset, no world — which is what lets
// `Elysium.Substrate.OverlayStack` assert the whole contract on the stack. The assets a layer
// resolves to are `FElysiumResolvedAnimation`'s, and the pose is the graph's.
//
// The recovered contract is `docs/vtmb/animation_rig_resolution.md` -> "The overlay contract".

// One slot of the stack — retail's 48-byte `m_AnimOverlay[i]` record, by meaning rather than by
// layout.
//
// `Request` carries what the producer stated (the clip label, the forced layer activity, the band it
// asked at, the clip's wall-clock length and its loop bit); everything beside it is the layer's own
// advancing state, which no caller writes.
struct FElysiumOverlayLayer
{
	// What the producer pushed. `ClipLengthSeconds` is the clip's authored length over the producer's
	// playback rate — `ElysiumAnimIntent::ClaimForSegment` performs that division once, and the cycle
	// below rides the result, so the rate is not applied twice.
	FElysiumAnimationRequest Request;

	// `m_flCycle`. Clamps at 1 on a non-looping sequence and wraps on a looping one.
	float Cycle = 0.0f;
	// `m_flPlaybackRate`. Retail's default, and it stays 1.0 here because the producer's rate is
	// already folded into `Request.ClipLengthSeconds`. It is kept rather than dropped because it is
	// the record's own field, and a pusher stating a length in AUTHORED seconds would need it.
	float PlaybackRate = 1.0f;
	// `m_flWeight`. **Computed every advance from the layer's own cycle, never written by a pusher** —
	// and it doubles as the slot's occupancy marker, which is why `SetLayer` seeds it non-zero.
	float Weight = 0.0f;
	// `m_flWeightMax`.
	float WeightMax = ElysiumOverlay::WeightMax;
	// `m_flBlendIn` / `m_flBlendOut`, as cycle fractions.
	ElysiumOverlay::FBlend Blend;
	// `m_fSequenceFinished`, raised when the cycle reaches 1.
	bool bFinished = false;
	// `m_bAutoKillWhenFinished`. A finished layer that carries it frees its slot and fires a
	// completion notification; **a finished layer without it holds its slot at cycle 1.0
	// indefinitely**, until something else clears it.
	//
	// It only bites on a SNAP layer, because only a SNAP layer is still live at cycle 1: an
	// enveloped layer's own out-ramp has already taken its weight — and therefore its slot — before
	// the reap runs. That asymmetry is the recovered arithmetic rather than a gap.
	bool bAutoKill = true;

	// Ours, not retail's: retail addresses a layer by slot index and this runtime's producers hold
	// handles (`SubmitBodyAnimRequest` / `ReleaseBodyAnimRequest`), so the handle is what a release
	// finds a layer by. Zero means the slot has never been written.
	uint32 Handle = 0;
	// How long this layer has stood, seconds. Diagnostics only — the cycle is the authority — but it
	// is what separates a layer mid-motion from one leaked at cycle 1 on the readouts.
	float AgeSeconds = 0.0f;

	// **Occupancy is the zero-weight test and nothing else**, exactly as `AllocateLayer` reads it.
	bool IsLive() const { return Weight > 0.0f; }
};

// One body's four layers.
struct FElysiumOverlayStack
{
	FElysiumOverlayLayer Layers[ElysiumOverlay::NumSlots];

	// The lowest slot whose weight is zero, or `INDEX_NONE` when all four are held.
	//
	// The scan starts at 0 because retail's `GetFirstGestureLayer` answers `0` for every class in the
	// hierarchy — no slot is reserved. There is no eviction and no priority displacement: a full stack
	// refuses, and `AddGesture` propagates that refusal to its caller.
	int32 AllocateLayer() const;

	// Write a slot, with retail's defaults. Returns the slot index, or `INDEX_NONE` for an index out
	// of range.
	//
	// `bSnap` is the sequence's own studio hard-cut bit, which is the only thing that decides the
	// envelope. `Handle` is the token a release finds this layer by.
	//
	// A request stating no clip length is **refused** rather than written: the cycle is what carries a
	// layer's weight, its end and its phase, and a layer with no length to ride would stand one frame
	// of its clip at a fixed weight for as long as its producer held the slot. The caller reports it.
	int32 SetLayer(int32 SlotIndex, const FElysiumAnimationRequest& Request, bool bSnap, uint32 Handle,
		bool bAutoKill = true);

	// Advance every live layer by one frame: its cycle on its own rate, then its weight through its
	// own envelope. Raises `bFinished` on a layer whose cycle reaches 1.
	//
	// **This is the whole of a layer's lifetime.** No claim expiry and no wall-clock deadline runs
	// over the stack: the cycle is what ends a layer, which is retail's rule and the reason a layer
	// survives the base pose changing hands underneath it.
	void Advance(float DeltaSeconds);

	// Free the slot of every **live**, finished, auto-kill layer — retail's owner loop, where death is
	// weight-zeroing — appending each one's `(slot, activity)` as the completion notification retail
	// raises through vfunc `+0x1c0`. Returns how many slots were freed.
	//
	// The liveness term is retail's and is load-bearing: an enveloped layer has already faded its own
	// weight to zero by cycle 1, so it is gone before this runs and raises no completion. Only a SNAP
	// layer, which holds full weight past its end, is reaped here.
	int32 Reap(TArray<TPair<int32, FString>>* OutCompleted = nullptr);

	// The slot a handle names, or `INDEX_NONE`. A handle that names nothing is the ordinary end of a
	// claim whose producer came back late, not a failure.
	int32 FindByHandle(uint32 Handle) const;

	// Give one slot back by index. False when the slot was not live.
	bool ClearSlot(int32 SlotIndex);

	// Give every slot back at once, whoever holds it. Returns how many were live — the death
	// transaction's answer, so a caller can report an unexpectedly layered body.
	int32 ClearAll();

	int32 LiveCount() const;
	bool IsEmpty() const { return LiveCount() == 0; }

	// The layer standing in a slot, or null when the slot is free or the index is out of range.
	const FElysiumOverlayLayer* LiveLayer(int32 SlotIndex) const;
};
