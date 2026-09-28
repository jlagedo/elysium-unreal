// Story 0019/8 (29e under the strict verdict), family **Boss19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Boss19's `rule` rows): 0x103aa3b0 CNPC_VSabbatLeader::StartTransformation.

#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"

#include "Substrate/ElysiumRelationships.h"

// Story 8, lane L12. Walked prose in `docs/vtmb/npc-ai/story8/Boss19.md`.

namespace NpcKernelBoss19Species
{
	// `CBaseCombatCharacter::AddClassRelationship(1, 1, 10)`: class 1 is `CLASS_PLAYER`, the class
	// family PlayerController spells `"player"` for its own `AddClassRelationship(1, 3, 0)`
	// (`ElysiumNpcPlayerController.cpp`); disposition 1 is `D_HT`; priority 10.
	const TCHAR* const GBoss19SpeciesPlayerClass = TEXT("player");
	constexpr int32 GBoss19SpeciesTransformHatePriority = 10;
	// The program and the trace line (`NPC_VSabbatLeader.cpp`, `0x1064ed7c`).
	constexpr int32 GBoss19SpeciesTransformSchedule = 0x163;
	constexpr int32 GBoss19SpeciesTransformLine = 0x549;
}

// -------------------------------------------------------------------------------------------------
// 0x103aa3b0 CNPC_VSabbatLeader::StartTransformation
// -------------------------------------------------------------------------------------------------

void FElysiumNpcSabbatLeader::SabbatLeaderStartTransformation()
{
	using namespace NpcKernelBoss19Species;
	// `m_bActivated` FIRST: the word slot 461 (`0x103a7450`) gates on, so the state selector starts
	// answering ALERT/COMBAT on this very think.
	bSabbatLeaderActivated = true;                                          // 0x103aa409 +0x66b8
	// A CLASS-wide relationship: every member of class 1 is re-dispositioned at once.
	// `0x10013cf5` -> `CBaseCombatCharacter::AddClassRelationship` `0x10332aa0`, which overwrites the
	// class row at any priority (`SetClass` would refuse a lower one).
	Relationships.AddClassRelationship(GBoss19SpeciesPlayerClass, EElysiumRelationship::Hate,
		GBoss19SpeciesTransformHatePriority);                               // 0x103aa410 0x10013cf5
	// `+0x1b30`/`+0x1b34` := `NPC_VSabbatLeader.cpp`, 0x549 — the selector trace, absent in this
	// runtime's shape map; recorded the landed way.
	RecordScheduleEvent(FString::Printf(TEXT("StartTransformation trace NPC_VSabbatLeader.cpp:%d"),
		GBoss19SpeciesTransformLine));                                      // 0x103aa41e / 0x103aa428
	SetSchedule(GBoss19SpeciesTransformSchedule, false);                    // 0x103aa432 0x102ae750
}
