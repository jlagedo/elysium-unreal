#include "ElysiumOverlayStack.h"

int32 FElysiumOverlayStack::AllocateLayer() const
{
	for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
	{
		if (!Layers[Index].IsLive())
		{
			return Index;
		}
	}
	// Every slot is held. Retail returns `-1` here and `AddGesture` propagates it; there is no
	// eviction and no priority displacement, so the push simply does not happen.
	return INDEX_NONE;
}

int32 FElysiumOverlayStack::SetLayer(int32 SlotIndex, const FElysiumAnimationRequest& Request,
	bool bSnap, uint32 Handle, bool bAutoKill)
{
	if (SlotIndex < 0 || SlotIndex >= ElysiumOverlay::NumSlots)
	{
		return INDEX_NONE;
	}
	if (!(Request.ClipLengthSeconds > 0.0f))
	{
		// A layer with no length has no cycle, and the cycle is what carries its weight, its end and
		// its phase. Refused here rather than written and then advanced to an answer no clip supports;
		// the caller names it, because only the caller knows which producer asked.
		return INDEX_NONE;
	}
	FElysiumOverlayLayer& Layer = Layers[SlotIndex];
	Layer = FElysiumOverlayLayer();
	Layer.Request = Request;
	Layer.Blend = ElysiumOverlay::BlendFor(bSnap);
	Layer.WeightMax = ElysiumOverlay::WeightMax;
	Layer.PlaybackRate = 1.0f;
	// The seed, and the one write to `Weight` any caller performs. Every later value comes out of
	// `Advance`'s envelope; this exists so the slot reads as occupied against `AllocateLayer` on the
	// frame it is pushed, before any advance has run.
	Layer.Weight = ElysiumOverlay::SeedWeight;
	Layer.bAutoKill = bAutoKill;
	Layer.Handle = Handle;
	return SlotIndex;
}

void FElysiumOverlayStack::Advance(float DeltaSeconds)
{
	for (FElysiumOverlayLayer& Layer : Layers)
	{
		if (!Layer.IsLive())
		{
			continue;
		}
		Layer.AgeSeconds += DeltaSeconds;

		// `SetLayer` refuses a length of zero, so every live layer has one to ride.
		Layer.Cycle += (Layer.PlaybackRate / Layer.Request.ClipLengthSeconds) * DeltaSeconds;

		if (Layer.Cycle >= 1.0f)
		{
			// **Finished is raised whichever way the cycle is treated.** Retail sets it on reaching
			// 1.0 and only then decides whether to clamp or wrap, so a looping layer reports itself
			// finished on every pass — which is what an auto-kill looping gesture ends on.
			Layer.bFinished = true;
			Layer.Cycle = Layer.Request.bLoop
				? Layer.Cycle - FMath::FloorToFloat(Layer.Cycle)
				: 1.0f;
		}

		// **Recomputed every tick from the layer's own cycle, and this is the whole difference
		// between an overlay and a studio autolayer.** An autolayer's weight follows the OWNING
		// sequence's cycle or a pose parameter, so it moves with the animation underneath it; an
		// overlay's follows its own independently advancing cycle, which is what produces the
		// `0.923 -> 0.02` fall across consecutive frames the capture records.
		Layer.Weight = ElysiumOverlay::WeightForCycle(Layer.Cycle, Layer.Blend, Layer.WeightMax);
	}
}

int32 FElysiumOverlayStack::Reap(TArray<TPair<int32, FString>>* OutCompleted)
{
	int32 Freed = 0;
	for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
	{
		FElysiumOverlayLayer& Layer = Layers[Index];
		// **Live, finished and auto-kill — retail's three terms, and the first one is not a guard.**
		// An enveloped layer's own out-ramp has already taken its weight to zero by cycle 1, which
		// freed its slot without passing through here and without raising a completion; only a SNAP
		// layer, which has no out-ramp and therefore stands at full weight past its own end, reaches
		// this. A finished SNAP layer whose auto-kill flag is clear holds its slot indefinitely.
		if (!Layer.IsLive() || !Layer.bFinished || !Layer.bAutoKill)
		{
			continue;
		}
		if (OutCompleted != nullptr)
		{
			// Retail's completion notification through vfunc `+0x1c0`, which is called with the layer
			// index and the layer's own activity. The activity and not the label: a caller waiting on
			// a family is asking what finished, not which clip realised it.
			OutCompleted->Emplace(Index, Layer.Request.Activity);
		}
		Layer = FElysiumOverlayLayer();
		++Freed;
	}
	return Freed;
}

int32 FElysiumOverlayStack::FindByHandle(uint32 Handle) const
{
	if (Handle == 0)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
	{
		if (Layers[Index].Handle == Handle)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool FElysiumOverlayStack::ClearSlot(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= ElysiumOverlay::NumSlots || Layers[SlotIndex].Handle == 0)
	{
		return false;
	}
	Layers[SlotIndex] = FElysiumOverlayLayer();
	return true;
}

int32 FElysiumOverlayStack::ClearAll()
{
	int32 Cleared = 0;
	for (FElysiumOverlayLayer& Layer : Layers)
	{
		if (Layer.Handle != 0)
		{
			++Cleared;
		}
		Layer = FElysiumOverlayLayer();
	}
	return Cleared;
}

int32 FElysiumOverlayStack::LiveCount() const
{
	int32 Count = 0;
	for (const FElysiumOverlayLayer& Layer : Layers)
	{
		if (Layer.IsLive())
		{
			++Count;
		}
	}
	return Count;
}

const FElysiumOverlayLayer* FElysiumOverlayStack::LiveLayer(int32 SlotIndex) const
{
	if (SlotIndex < 0 || SlotIndex >= ElysiumOverlay::NumSlots || !Layers[SlotIndex].IsLive())
	{
		return nullptr;
	}
	return &Layers[SlotIndex];
}
