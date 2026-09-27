// Story 0019/8 (29e under the strict verdict), family **RunTask19** -- `CAI_BaseNPCTroika`'s
// bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcRunTask19.inl` (included inside `class FElysiumNpc`) or generated
// in `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved
// here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the
// marker.
//
// Owns (RunTask19's `rule` rows): 0x102aacf0 CAI_BaseNPCTroika::RunTask.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumStub.h"

namespace
{
	// The generated slot stubs' tally, verbatim (`gen_kernel_shape`'s `Fire*Slot`), unit-prefixed for adaptive unity.
	void FireRunTask19Slot(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,
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

// STORY8-FORWARD slot 444 0x102aacf0 CAI_BaseNPCTroika::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
int32 FElysiumNpc::RunTaskSlot444(void*)
{
	FireRunTask19Slot(TEXT("CAI_BaseNPCTroika::RunTaskSlot444"), TEXT("0x102aacf0"), TEXT("29e"), DebugString());
	return {};
}
