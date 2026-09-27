// Story 0019/8 (29e under the strict verdict), family **Conditions19** -- `CAI_BaseNPCTroika`'s
// bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcConditions19.inl` (included inside `class FElysiumNpc`) or
// generated in `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated
// stub moved here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops
// the marker.
//
// Owns (Conditions19's `rule` rows): 0x102b27f0 CAI_BaseNPCTroika::GatherConditions.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumStub.h"

namespace
{
	// The generated slot stubs' tally, verbatim (`gen_kernel_shape`'s `Fire*Slot`), unit-prefixed for adaptive unity.
	void FireConditions19Slot(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,
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

// STORY8-FORWARD slot 433 0x102b27f0 CAI_BaseNPCTroika::GatherConditions — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
void FElysiumNpc::GatherConditions()
{
	FireConditions19Slot(TEXT("CAI_BaseNPCTroika::GatherConditions"), TEXT("0x102b27f0"), TEXT("29e"), DebugString());
}
