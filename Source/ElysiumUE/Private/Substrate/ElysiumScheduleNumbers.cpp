#include "Substrate/ElysiumScheduleNumbers.h"

namespace
{
	// One row per constant in the header. A constant with no row here is not checked, which is the
	// only way a hand-decoded number gets back into this runtime -- so adding one means adding a
	// row, and the header says so.
	const FElysiumScheduleNumberRow GCheckedRows[] = {
		{ TEXT("cai_basenpc"), TEXT("NONE"), ElysiumSched::NONE },
		{ TEXT("cai_basenpc"), TEXT("IDLE_STAND"), ElysiumSched::IDLE_STAND },
		{ TEXT("cai_basenpc"), TEXT("TARGET_FACE"), ElysiumSched::TARGET_FACE },
		{ TEXT("cai_basenpc"), TEXT("TARGET_CHASE"), ElysiumSched::TARGET_CHASE },
		{ TEXT("cai_basenpc"), TEXT("STANDOFF"), ElysiumSched::STANDOFF },
		{ TEXT("cai_basenpc"), TEXT("DIE"), ElysiumSched::DIE },
		{ TEXT("cai_basenpc"), TEXT("SCHED_DIE_RAGDOLL"), ElysiumSched::SCHED_DIE_RAGDOLL },
		{ TEXT("cai_basenpc"), TEXT("WAIT_FOR_SCRIPT"), ElysiumSched::WAIT_FOR_SCRIPT },
		{ TEXT("cai_basenpc"), TEXT("AISCRIPT"), ElysiumSched::AISCRIPT },
		{ TEXT("cai_basenpc"), TEXT("SCRIPTED_WALK"), ElysiumSched::SCRIPTED_WALK },
		{ TEXT("cai_basenpc"), TEXT("SCRIPTED_RUN"), ElysiumSched::SCRIPTED_RUN },
		{ TEXT("cai_basenpc"), TEXT("SCRIPTED_CUSTOM_MOVE"), ElysiumSched::SCRIPTED_CUSTOM_MOVE },
		{ TEXT("cai_basenpc"), TEXT("SCRIPTED_WAIT"), ElysiumSched::SCRIPTED_WAIT },
		{ TEXT("cai_basenpc"), TEXT("SCRIPTED_FACE"), ElysiumSched::SCRIPTED_FACE },
		{ TEXT("cai_basenpc"), TEXT("SCHED_FLINCH_PHYSICS"), ElysiumSched::SCHED_FLINCH_PHYSICS },
		{ TEXT("cai_basenpc"), TEXT("FAIL"), ElysiumSched::FAIL },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_IDLE_STAND"),
			ElysiumSched::SCHED_TROIKA_IDLE_STAND },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_IDLE_DISPOSITION"),
			ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_CHASE_ENEMY_FAILED"),
			ElysiumSched::SCHED_TROIKA_CHASE_ENEMY_FAILED },
	};
}

TConstArrayView<FElysiumScheduleNumberRow> ElysiumSched::CheckedRows()
{
	return GCheckedRows;
}
