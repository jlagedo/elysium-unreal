// The handful of retail schedule ids this runtime names in code, and the check that keeps them
// honest.
//
// Almost nothing should be here. A schedule is content: the corpus registers 695 names and every
// one of them is reachable by name through `FElysiumScheduleManager::FindByName`. What lands in
// this header is only the ids a C++ BODY has to name -- a selector that must return `FAIL`, the
// scripted-director map that turns a cine command into a base program -- and each is a number this
// file asserts rather than remembers.
//
// `FElysiumScheduleCorpus::VerifyNumbers` looks every row below up in the unit that registers it
// and Errors naming the pair that disagreed. That is what makes these recovered numbers rather than
// hand-decoded ones: a corpus re-exported from a different image, or a typo here, is a load-time
// failure with both numbers in the message instead of an NPC quietly running the wrong program.
//
// Base names carry NO `SCHED_` prefix (`IDLE_STAND`, `CHASE_ENEMY`, `FAIL`) except
// `SCHED_DIE_RAGDOLL` and `SCHED_FLINCH_PHYSICS`; Troika's all do. That is retail's inconsistency,
// not a transcription slip.

#pragma once

#include "CoreMinimal.h"

/** One row of the check: the unit that registers the name, the name, and the local id. */
struct FElysiumScheduleNumberRow
{
	const TCHAR* Unit = nullptr;
	const TCHAR* Name = nullptr;
	int32 LocalId = INDEX_NONE;
};

/**
 * The CLASS-LOCAL schedule ids, in the space named beside each.
 *
 * Local, not global: a global id depends on the order the corpus loads, and these are compared
 * against the corpus's own registration table, which is local. A body that wants the global id
 * asks the space to translate.
 */
namespace ElysiumSched
{
	// --- `CAI_BaseNPC`, local ids 0x00..0x43 dense -----------------------------------------------

	/** `0x102cadd0` registers this first, and `CAI_LocalIdSpace::Register` needs it: a root space
	 *  takes its base from the FIRST id registered, so a class that registered `0x01` before `0x00`
	 *  would translate `0x01` to -1 and insert nothing. */
	inline constexpr int32 NONE = 0x00;
	inline constexpr int32 IDLE_STAND = 0x01;
	inline constexpr int32 IDLE_WALK = 0x02;
	inline constexpr int32 TARGET_FACE = 0x12;
	inline constexpr int32 TARGET_CHASE = 0x13;
	inline constexpr int32 ALERT_SMALL_FLINCH = 0x07;
	inline constexpr int32 SMALL_FLINCH = 0x14;
	inline constexpr int32 TAKE_COVER_FROM_ORIGIN = 0x19;
	inline constexpr int32 STANDOFF = 0x25;
	inline constexpr int32 DIE = 0x2b;
	inline constexpr int32 SCHED_DIE_RAGDOLL = 0x2c;
	inline constexpr int32 WAIT_FOR_SCRIPT = 0x2d;
	inline constexpr int32 AISCRIPT = 0x2e;

	// The five the scripted director routes a cine command onto. `FElysiumNpcBase::TranslateSchedule` already
	// carries this map; these are the numbers it maps ONTO, and they are why the port's two
	// invented `ScriptedMoveToGoal` / `ScriptedFollowPath` ids die with no replacement.
	inline constexpr int32 SCRIPTED_WALK = 0x2f;
	inline constexpr int32 SCRIPTED_RUN = 0x30;
	inline constexpr int32 SCRIPTED_CUSTOM_MOVE = 0x31;
	inline constexpr int32 SCRIPTED_WAIT = 0x32;
	inline constexpr int32 SCRIPTED_FACE = 0x33;

	inline constexpr int32 SCHED_FLINCH_PHYSICS = 0x42;
	/** `CAI_BaseNPC::PreSelectSchedule`'s freeze answer (`AI_BaseNPC.cpp:3627`). */
	inline constexpr int32 NPC_FREEZE = 0x3a;

	/** The failure route every schedule falls to, and the one id a selector must be able to name.
	 *  The decompiler leaves it unnamed; it is `0x10608410`, cell 0 of the base's text table. */
	inline constexpr int32 FAIL = 0x43;

	// --- `CAI_BaseNPCTroika`, local ids 0x44..0x155 ----------------------------------------------

	inline constexpr int32 SCHED_TROIKA_IDLE_STAND = 0x44;
	inline constexpr int32 SCHED_TROIKA_IDLE_DISPOSITION = 0x6b;

	/** 0019 story 3's witness: twelve tasks, twelve interrupts, one task op this runtime did not
	 *  have before pass C. */
	inline constexpr int32 SCHED_TROIKA_CHASE_ENEMY_FAILED = 0xb7;

	// The combat selectors' own answers. These were decoded numbers living in comments beside
	// invented enum names; they are constants now, and the corpus checks every one. Three of the
	// names the port carried beside them were WRONG, which is what the check is for: `0xe0` is
	// `MELEE_CIRCLE_ADVANCE` rather than `MELEE_CIRCLE`, `0xec` is `RANGE_ATTACK1_SHOOT_AT_HINT`
	// rather than `RANGE_ATTACK1`, and `0xb9` is `RUN_AWAY_FROM_ENEMY` rather than `RUN_AWAY`.
	inline constexpr int32 SCHED_TROIKA_ALERT_LOOK_AROUND_NI = 0x4f;
	inline constexpr int32 SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE = 0x91;
	inline constexpr int32 SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE = 0x96;
	inline constexpr int32 SCHED_TROIKA_TAKE_COVER_HINT_DOOR = 0x9c;
	inline constexpr int32 SCHED_TROIKA_CHASE_ENEMY = 0xb1;
	inline constexpr int32 SCHED_TROIKA_RUN_AWAY_FROM_ENEMY = 0xb9;
	/** The Troika line's own standoff, which is NOT base `STANDOFF` 0x25 -- both names exist and
	 *  `SCHED_TROIKA_RUN_AWAY_FROM_ENEMY`'s `TASK_SET_FAIL_SCHEDULE` names this one. */
	inline constexpr int32 SCHED_TROIKA_STANDOFF = 0xc1;
	inline constexpr int32 SCHED_TROIKA_MELEE_IDLE = 0xc7;
	inline constexpr int32 SCHED_TROIKA_MELEE_ADVANCE = 0xca;
	inline constexpr int32 SCHED_TROIKA_MELEE_STEPBACK = 0xd3;
	inline constexpr int32 SCHED_TROIKA_MELEE_DODGE = 0xd5;
	inline constexpr int32 SCHED_TROIKA_MELEE_PREBLOCK = 0xd6;
	inline constexpr int32 SCHED_TROIKA_MELEE_KICK = 0xdb;
	inline constexpr int32 SCHED_TROIKA_MELEE_ATTACK1 = 0xdc;
	inline constexpr int32 SCHED_TROIKA_MELEE_ATTACK1_NR = 0xdd;
	/** The terminal swing, and the one program whose EMPTY interrupt mask is recovered rather than
	 *  merely undecoded: once that task owns the NPC it is not reevaluated as a fresh attack choice
	 *  each tick. Its text declares `Interrupts` with nothing after it. */
	inline constexpr int32 SCHED_TROIKA_MELEE_ATTACK1_SWING = 0xde;
	inline constexpr int32 SCHED_TROIKA_MELEE_CIRCLE_ADVANCE = 0xe0;
	inline constexpr int32 SCHED_TROIKA_RANGE_ATTACK1_SHOOT_AT_HINT = 0xec;

	/** The post-feed trance, from its one producer `CBaseCombatCharacter::FeedInterrupt`
	 *  (`0x1033a9e0`), which is the only `SetSchedule(0xfb)` site in the image. */
	inline constexpr int32 SCHED_TROIKA_MESMERIZED = 0xfb;

	// --- Species spaces, local ids from 0x156. The same number names a DIFFERENT program in each
	// class's space (0x16b is Ming Xiao's HIT_IN_HEAD and the tentacle's EXPLODE_DIE), so every
	// constant carries its class in its name and its unit in its row. ----------------------------

	// `CNPC_VVampireBoss` (`cnpc_vvampireboss`)
	inline constexpr int32 SCHED_VVAMPIREBOSS_TRANSFORM = 0x158;
	inline constexpr int32 SCHED_VVAMPIREBOSS_TRANSFORM_TO_BEAST = 0x159;

	// `CNPC_VWerewolf` (`cnpc_vwerewolf`)
	inline constexpr int32 SCHED_VWEREWOLF_RUN_TO_SPECIAL_MOVEMENT = 0x15a;
	inline constexpr int32 SCHED_VWEREWOLF_DO_SPECIAL_MOVEMENT = 0x15b;
	inline constexpr int32 SCHED_VWEREWOLF_DO_JUMP_HINT = 0x15f;
	inline constexpr int32 SCHED_VWEREWOLF_DO_DEATH_HINT = 0x160;

	// `CNPC_VGargoyle` (`cnpc_vgargoyle`)
	inline constexpr int32 SCHED_VGARGOYLE_DEATH = 0x15c;

	// `CNPC_VZombie` (`cnpc_vzombie`)
	inline constexpr int32 SCHED_VZOMBIE_ANIMATED_DEATH = 0x162;

	// `CNPC_VSabbatLeader` (`cnpc_vsabbatleader`)
	inline constexpr int32 SCHED_VSABBATLEADER_TRANSFORM_TO_BEAST = 0x163;

	// `CNPC_VMingXiao` (`cnpc_vmingxiao`)
	inline constexpr int32 SCHED_VMING_XIAO_HIT_IN_HEAD = 0x16b;
	inline constexpr int32 SCHED_VMING_XIAO_HIT_IN_TENTACLE = 0x16c;
	inline constexpr int32 SCHED_VMING_XIAO_DIE = 0x16d;
	inline constexpr int32 SCHED_VMING_XIAO_DIE_PROXY = 0x16e;

	// `CNPC_VMingXiaoTentacle` (`cnpc_vmingxiaotentacle`)
	inline constexpr int32 SCHED_VMING_XIAO_TENTACLE_DELAYED_DIE = 0x16a;
	inline constexpr int32 SCHED_VMING_XIAO_TENTACLE_EXPLODE_DIE = 0x16b;
	inline constexpr int32 SCHED_VMING_XIAO_TENTACLE_SIMPLE_DIE = 0x16c;

	// `CNPC_VHengeyokai` (`cnpc_vhengeyokai`)
	inline constexpr int32 SCHED_VHENGEYOKAI_STUNNED = 0x16e;

	/** Every row above, for `VerifyNumbers`. A constant that is not in this table is not checked,
	 *  which is the one way to get a hand-decoded number back into the runtime -- so adding a
	 *  constant means adding a row. */
	TConstArrayView<FElysiumScheduleNumberRow> CheckedRows();
}
