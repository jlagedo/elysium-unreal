#pragma once

#include "ElysiumDlg.h"
#include "ElysiumEntityHandle.h"

class FElysiumCombatCharacter;
class FElysiumEntityWorld;
class UElysiumRulebookSubsystem;

// The two injected interfaces `FElysiumDlgDependency` needs, bound to the live rulebook and to a
// live pair of characters. They live here rather than in `ElysiumDlg.h` because the dependency
// parser must stay free of the sheet, the rulebook subsystem and the entity world — a test binds
// fakes to the same two interfaces and never loads a table.
namespace ElysiumDlgSheet
{
	// `CVStatRef`'s resolver walk (`0x10204570`): the four `stats.txt` containers in declaration
	// order, then `feats.txt`. `Rules` may be null — the feat half then falls back to the headless
	// table binding (`ElysiumSheetRules::BoundTables`), exactly as `CalcFeat` does.
	TSharedRef<const IElysiumDlgTraitResolver> MakeTraitResolver(UElysiumRulebookSubsystem* Rules);

	// The player's sheet as a dependency reads it, plus the two writes `pc_charge_dependency`
	// (`0x100e8b90`) makes: the blood spend on the player and the faked discipline effect on the
	// conversation partner. Both characters are held as handles and re-resolved per call, so a
	// conversation that outlives its NPC answers zero rather than reading freed memory.
	TSharedRef<IElysiumDlgSheet> MakePlayerSheet(FElysiumEntityWorld& World,
		const FElysiumEntityHandle& Player, const FElysiumEntityHandle& Npc);
}

namespace ElysiumDlgCharge
{
	// Retail's `CBaseCombatCharacter::AddFakedDisciplineEffect(target, disciplineId, level, source)`
	// — the visible half of using a Discipline through a dialogue line. It belongs on
	// `FElysiumCombatCharacter`; it is a free function here only because that class's header is
	// owned by another lane while the dialogue plan lands, and moving it costs one signature.
	//
	// TODO(dialogue-plan): dialog_domination_emitter / dialog_presence_emitter (effects domain) —
	// the effect this fires has no emitter yet, so the seam logs the transaction and nothing is
	// played. The blood is still spent, which is the half that is gameplay.
	void AddFakedDisciplineEffect(FElysiumCombatCharacter& Target, const FString& Trait,
		int32 DisciplineId, int32 Level);
}
