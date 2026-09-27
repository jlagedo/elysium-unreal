// Story 0019/8 (29e under the strict verdict), family **Damage19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcDamage19.inl` (included inside `class FElysiumNpc`) or generated
// in `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved
// here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the
// marker.
//
// Owns (Damage19's `rule` rows): 0x1029fa50 CAI_BaseNPCTroika::FUN_1029fa50, 0x1029fcf0
// CAI_BaseNPCTroika::PlayerDefenderBlockReaction, 0x102a01b0
// CAI_BaseNPCTroika::PlayerKnockbackReaction, 0x102beda0 CAI_BaseNPCTroika::OnTakeDamage.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumStub.h"

namespace
{
	// The generated slot stubs' tally, verbatim (`gen_kernel_shape`'s `Fire*Slot`), unit-prefixed for adaptive unity.
	void FireDamage19Slot(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,
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

// STORY8-FORWARD slot 142 0x102bed30 CAI_BaseNPCTroika::OnTakeDamage — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
int32 FElysiumNpc::OnTakeDamage(void*)
{
	FireDamage19Slot(TEXT("CAI_BaseNPCTroika::OnTakeDamage"), TEXT("0x102bed30"), TEXT("29e"), DebugString());
	return {};
}

// STORY8-FORWARD slot 316 0x1029fa50 CAI_BaseNPCTroika::Slot316 — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
void FElysiumNpc::Slot316(FElysiumEntity*, bool, bool)
{
	FireDamage19Slot(TEXT("CAI_BaseNPCTroika::Slot316"), TEXT("0x1029fa50"), TEXT("29e"), DebugString());
}

// STORY8-FORWARD slot 318 0x1029fcf0 CAI_BaseNPCTroika::PlayerDefenderBlockReaction — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
bool FElysiumNpc::PlayerDefenderBlockReaction(FElysiumEntity*, void*, void*)
{
	FireDamage19Slot(TEXT("CAI_BaseNPCTroika::PlayerDefenderBlockReaction"), TEXT("0x1029fcf0"), TEXT("29e"), DebugString());
	return {};
}

// STORY8-FORWARD slot 320 0x102a01b0 CAI_BaseNPCTroika::PlayerKnockbackReaction — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
bool FElysiumNpc::PlayerKnockbackReaction(FElysiumEntity*, int32)
{
	FireDamage19Slot(TEXT("CAI_BaseNPCTroika::PlayerKnockbackReaction"), TEXT("0x102a01b0"), TEXT("29e"), DebugString());
	return {};
}

// STORY8-FORWARD slot 390 0x102beda0 CAI_BaseNPCTroika::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
// The body tallies `elysium.stubs` exactly as the generated stub did.
int32 FElysiumNpc::OnTakeDamage_Alive(void*)
{
	FireDamage19Slot(TEXT("CAI_BaseNPCTroika::OnTakeDamage_Alive"), TEXT("0x102beda0"), TEXT("29e"), DebugString());
	return {};
}
