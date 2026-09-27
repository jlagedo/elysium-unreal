// Story 0019/8 (29e under the strict verdict), family **Think19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcThink19.inl` (included inside `class FElysiumNpc`) or generated in
// `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved here
// unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the marker.
//
// Owns (Think19's `rule` rows): 0x10298070 CAI_BaseNPCTroika::UpdateCharacter, 0x10292de0
// CAI_BaseNPCTroika::NPCThink.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumStub.h"

namespace
{
	// The generated slot stubs' tally, verbatim (`gen_kernel_shape`'s `Fire*Slot`), unit-prefixed for adaptive unity.
	void FireThink19Slot(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,
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

// STORY8-FORWARD slot 431 0x10292de0 CAI_BaseNPCTroika::NPCThink — forwarding stub, not the port; the porter replaces this body.
// The verdict row named `FElysiumNpc::Think`; the slot's generated virtual is `NPCThink`, so the
// definition and the `hand:` target use `FElysiumNpc::NPCThink`.
// The body tallies `elysium.stubs` exactly as the generated stub did.
void FElysiumNpc::NPCThink()
{
	FireThink19Slot(TEXT("CAI_BaseNPCTroika::NPCThink"), TEXT("0x10292de0"), TEXT("29e"), DebugString());
}
