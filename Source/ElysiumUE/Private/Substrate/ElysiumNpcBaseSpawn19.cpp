// Story 0019/8 (29e under the strict verdict), family **Spawn19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBaseSpawn19.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated
// stub moved here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops
// the marker.
//
// Owns (Spawn19's `rule` rows): 0x10265ad0 CAI_BaseNPC::Event_Killed, 0x10273200
// CAI_BaseNPC::Spawn.

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumStub.h"

namespace
{
	// The generated slot stubs' tally, verbatim (`gen_kernel_shape`'s `Fire*Slot`), unit-prefixed for adaptive unity.
	void FireSpawn19BaseSlot(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,
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

// STORY8-FORWARD slot 144 0x10265ad0 CAI_BaseNPC::Event_Killed — forwarding stub, not the port; the porter replaces this body.
// The verdict row named `FElysiumNpc::BaseEvent_Killed`; the slot's generated virtual is
// `Event_Killed`, so the definition and the `hand:` target use `FElysiumNpcBase::Event_Killed`.
// The body tallies `elysium.stubs` exactly as the generated stub did.
void FElysiumNpcBase::Event_Killed(void*)
{
	FireSpawn19BaseSlot(TEXT("CAI_BaseNPC::Event_Killed"), TEXT("0x10265ad0"), TEXT("29e"), DebugString());
}
