// Story 0019/8 (29e under the strict verdict), family **Spawn19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcSpawn19.inl` (included inside `class FElysiumNpc`) or generated in
// `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved here
// unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the marker.
//
// Owns (Spawn19's `rule` rows): 0x102bf340 CAI_BaseNPCTroika::Event_Killed, 0x10298d30
// CAI_BaseNPCTroika::Spawn.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumStub.h"

namespace
{
	// The generated slot stubs' tally, verbatim (`gen_kernel_shape`'s `Fire*Slot`), unit-prefixed for adaptive unity.
	void FireSpawn19Slot(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,
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

// STORY8-FORWARD slot 144 0x102bf340 CAI_BaseNPCTroika::Event_Killed — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
void FElysiumNpc::Event_Killed(void*)
{
	FireSpawn19Slot(TEXT("CAI_BaseNPCTroika::Event_Killed"), TEXT("0x102bf340"), TEXT("29e"), DebugString());
}
