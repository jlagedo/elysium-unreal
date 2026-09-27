// Story 0019/8 (29e under the strict verdict), family **RunAi19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcRunAi19.inl` (included inside `class FElysiumNpc`) or generated in
// `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved here
// unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the marker.
//
// Owns (RunAi19's `rule` rows): 0x1028fd80 CAI_BaseNPCTroika::RunAlternateAI, 0x1028fcc0
// CAI_BaseNPCTroika::RunAI.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumStub.h"

namespace
{
	// The generated slot stubs' tally, verbatim (`gen_kernel_shape`'s `Fire*Slot`), unit-prefixed for adaptive unity.
	void FireRunAi19Slot(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,
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

// STORY8-FORWARD slot 432 0x1028fcc0 CAI_BaseNPCTroika::RunAI — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
void FElysiumNpc::RunAI(bool)
{
	FireRunAi19Slot(TEXT("CAI_BaseNPCTroika::RunAI"), TEXT("0x1028fcc0"), TEXT("29e"), DebugString());
}
