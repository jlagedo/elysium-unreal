// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- `CAI_BaseNPCTroika`'s
// bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcStartTask19.inl` (included inside `class FElysiumNpc`) or
// generated in `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated
// stub moved here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops
// the marker.
//
// `0x102a1910` is shared with `ElysiumNpcStartTask19_2.cpp`: the cut follows the packet's chunk
// boundaries, one `switch`, retail's default arm once.
//
// Owns (StartTask19's `rule` rows): 0x102a1910 CAI_BaseNPCTroika::StartTask.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumStub.h"

namespace
{
	// The generated slot stubs' tally, verbatim (`gen_kernel_shape`'s `Fire*Slot`), unit-prefixed for adaptive unity.
	void FireStartTask19Slot(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,
		const FString& Receiver)
	{
		ElysiumStub::FSurface Row;
		Row.Kind = TEXT("slot");
		Row.Surface = Surface;
		Row.Address = Address;
		Row.Story = Story;
		ElysiumStub::Fired(Row, Receiver, FString(), TEXT("the NPC kernel"));
	}
}

// STORY8-FORWARD slot 442 0x102a1910 CAI_BaseNPCTroika::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
int32 FElysiumNpc::StartTaskSlot442(void*)
{
	FireStartTask19Slot(TEXT("CAI_BaseNPCTroika::StartTaskSlot442"), TEXT("0x102a1910"), TEXT("29e"), DebugString());
	return {};
}
