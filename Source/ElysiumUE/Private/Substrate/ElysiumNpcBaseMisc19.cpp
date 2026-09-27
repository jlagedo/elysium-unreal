// Story 0019/8 (29e under the strict verdict), family **Misc19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBaseMisc19.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated
// stub moved here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops
// the marker.
//
// Owns (Misc19's `rule` rows): 0x10279dd0 CAI_BaseNPC::ChooseEnemy, 0x1026cdc0
// CAI_BaseNPC::EnterGrappleState, 0x1026cec0 CAI_BaseNPC::FUN_1026cec0, 0x10274e30
// CAI_BaseNPC::HandleAnimEvent.

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumStub.h"

namespace
{
	// The generated slot stubs' tally, verbatim (`gen_kernel_shape`'s `Fire*Slot`), unit-prefixed for adaptive unity.
	void FireMisc19BaseSlot(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,
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

// STORY8-FORWARD slot 354 0x1026cec0 CAI_BaseNPC::Slot354 — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
void FElysiumNpcBase::Slot354()
{
	FireMisc19BaseSlot(TEXT("CAI_BaseNPC::Slot354"), TEXT("0x1026cec0"), TEXT("29e"), DebugString());
}
