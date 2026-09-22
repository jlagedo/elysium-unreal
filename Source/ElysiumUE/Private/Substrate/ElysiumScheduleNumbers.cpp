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
		{ TEXT("cai_basenpc"), TEXT("ALERT_SMALL_FLINCH"), ElysiumSched::ALERT_SMALL_FLINCH },
		{ TEXT("cai_basenpc"), TEXT("SMALL_FLINCH"), ElysiumSched::SMALL_FLINCH },
		{ TEXT("cai_basenpc"), TEXT("TAKE_COVER_FROM_ORIGIN"),
			ElysiumSched::TAKE_COVER_FROM_ORIGIN },
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
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_ALERT_LOOK_AROUND_NI"),
			ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE"),
			ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE"),
			ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_TAKE_COVER_HINT_DOOR"),
			ElysiumSched::SCHED_TROIKA_TAKE_COVER_HINT_DOOR },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_MELEE_ATTACK1_NR"),
			ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_NR },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_CHASE_ENEMY"),
			ElysiumSched::SCHED_TROIKA_CHASE_ENEMY },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_RUN_AWAY_FROM_ENEMY"),
			ElysiumSched::SCHED_TROIKA_RUN_AWAY_FROM_ENEMY },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_STANDOFF"),
			ElysiumSched::SCHED_TROIKA_STANDOFF },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_MELEE_IDLE"),
			ElysiumSched::SCHED_TROIKA_MELEE_IDLE },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_MELEE_ADVANCE"),
			ElysiumSched::SCHED_TROIKA_MELEE_ADVANCE },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_MELEE_STEPBACK"),
			ElysiumSched::SCHED_TROIKA_MELEE_STEPBACK },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_MELEE_DODGE"),
			ElysiumSched::SCHED_TROIKA_MELEE_DODGE },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_MELEE_PREBLOCK"),
			ElysiumSched::SCHED_TROIKA_MELEE_PREBLOCK },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_MELEE_KICK"),
			ElysiumSched::SCHED_TROIKA_MELEE_KICK },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_MELEE_ATTACK1"),
			ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1 },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_MELEE_ATTACK1_SWING"),
			ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_MELEE_CIRCLE_ADVANCE"),
			ElysiumSched::SCHED_TROIKA_MELEE_CIRCLE_ADVANCE },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_RANGE_ATTACK1_SHOOT_AT_HINT"),
			ElysiumSched::SCHED_TROIKA_RANGE_ATTACK1_SHOOT_AT_HINT },
		{ TEXT("cai_basenpctroika"), TEXT("SCHED_TROIKA_MESMERIZED"),
			ElysiumSched::SCHED_TROIKA_MESMERIZED },
	};
}

TConstArrayView<FElysiumScheduleNumberRow> ElysiumSched::CheckedRows()
{
	return GCheckedRows;
}
