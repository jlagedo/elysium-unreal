// Story 0019/8 (29e under the strict verdict), family **Misc19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcMisc19.inl` (included inside `class FElysiumNpc`) or generated in
// `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved here
// unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the marker.
//
// Owns (Misc19's `rule` rows): 0x10279a50 SetEnemy, 0x102b4f60 CAI_BaseNPCTroika::FUN_102b4f60,
// 0x102b4fe0 CAI_BaseNPCTroika::FUN_102b4fe0, 0x10365a90 FUN_10365a90, 0x102b4cc0
// CAI_BaseNPCTroika::FUN_102b4cc0, 0x10395ce0 FUN_10395ce0, 0x1039ea60 FUN_1039ea60, 0x102b5c00
// CAI_BaseNPCTroika::EnterGrappleState, 0x1017f4a0 PlayerSupernaturalIncident, 0x1029b290
// CAI_BaseNPCTroika::HandleAnimEvent.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumStub.h"

namespace
{
	// The generated slot stubs' tally, verbatim (`gen_kernel_shape`'s `Fire*Slot`), unit-prefixed for adaptive unity.
	void FireMisc19Slot(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,
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

// STORY8-FORWARD slot 595 0x102b4cc0 CAI_BaseNPCTroika::AcquireNearestHatedTarget — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
void FElysiumNpc::AcquireNearestHatedTarget()
{
	FireMisc19Slot(TEXT("CAI_BaseNPCTroika::AcquireNearestHatedTarget"), TEXT("0x102b4cc0"), TEXT("29e"), DebugString());
}

// STORY8-FORWARD slot 596 0x102b4f60 CAI_BaseNPCTroika::Slot596 — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
void FElysiumNpc::Slot596(FElysiumEntity*)
{
	FireMisc19Slot(TEXT("CAI_BaseNPCTroika::Slot596"), TEXT("0x102b4f60"), TEXT("29e"), DebugString());
}

// STORY8-FORWARD slot 598 0x102b4fe0 CAI_BaseNPCTroika::Slot598 — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
void FElysiumNpc::Slot598(FElysiumEntity*)
{
	FireMisc19Slot(TEXT("CAI_BaseNPCTroika::Slot598"), TEXT("0x102b4fe0"), TEXT("29e"), DebugString());
}
