#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcDebug.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelDebugShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelDebugShared
{
	// The open capture, or empty. Game-thread only, like the rest of the substrate.
	inline TArray<FElysiumNpc::FDebugLine> GNpcKernelDebugCapture;
	inline bool GNpcKernelDebugCapturing = false;
	inline const TCHAR* const GNpcKernelDebugChannelOverlay = TEXT("Overlay");
	inline void GNpcKernelDebugRecord(const TCHAR* Channel, const TCHAR* Retail, FString&& Text,
		int32 Line)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("[%s] %s"), Channel, *Text);
		if (GNpcKernelDebugCapturing)
		{
			FElysiumNpc::FDebugLine Row;
			Row.Channel = Channel;
			Row.Retail = Retail;
			Row.Text = MoveTemp(Text);
			Row.Line = Line;
			GNpcKernelDebugCapture.Add(MoveTemp(Row));
		}
	}
	// `0x1027e7f0`'s jump table, in the same id order: three characters per condition, read straight
	// out of `.rdata`. The strings sit four bytes apart descending from `0x105cd62c` (id 0) to
	// `0x105cd458` (id 0x76) — except id 0x73, whose `"fog"` is the outlier at `0x1058b0a0` — and
	// `"***"` at `0x105cd454` is the `default:` arm. Each one abbreviates the `COND_*` name at the
	// same index above, which is what confirms both tables at once.
	inline const TCHAR* const GNpcKernelDebugShortConditionNames[] = {
		TEXT("non"), TEXT("seu"), TEXT("lun"), TEXT("iun"), TEXT("urt"), TEXT("uad"), TEXT("uho"),
		TEXT("ure"), TEXT("tcr"), TEXT("tfm"), TEXT("bat"), TEXT("dat"), TEXT("sdg"), TEXT("sbl"),
		TEXT("ssb"), TEXT("ski"), TEXT("sin"), TEXT("slo"), TEXT("c_w"), TEXT("cdw"), TEXT("oid"),
		TEXT("iid"), TEXT("oie"), TEXT("iie"), TEXT("oif"), TEXT("iif"), TEXT("iti"), TEXT("het"),
		TEXT("chi"), TEXT("chs"), TEXT("piv"), TEXT("pcf"), TEXT("pca"), TEXT("psf"), TEXT("psa"),
		TEXT("cpo"), TEXT("pas"), TEXT("iso"), TEXT("isi"), TEXT("cft"), TEXT("knb"), TEXT("hiv"),
		TEXT("kiv"), TEXT("psr"), TEXT("sbk"), TEXT("sss"), TEXT("ebf"), TEXT("wat"), TEXT("ofr"),
		TEXT("sse"), TEXT("sle"), TEXT("hfs"), TEXT("hbd"), TEXT("sch"), TEXT("fwh"), TEXT("fnh"),
		TEXT("bmp"), TEXT("cvf"), TEXT("ebk"), TEXT("poh"), TEXT("wtw"), TEXT("sco"), TEXT("scf"),
		TEXT("lpa"), TEXT("npa"), TEXT("nsa"), TEXT("nwp"), TEXT("seh"), TEXT("sef"), TEXT("sed"),
		TEXT("see"), TEXT("len"), TEXT("eoc"), TEXT("toc"), TEXT("els"), TEXT("tls"), TEXT("ldm"),
		TEXT("hdm"), TEXT("rdm"), TEXT("ra1"), TEXT("ra2"), TEXT("ma1"), TEXT("ma2"), TEXT("prv"),
		TEXT("nen"), TEXT("etf"), TEXT("efm"), TEXT("bhe"), TEXT("edd"), TEXT("eur"), TEXT("sep"),
		TEXT("sen"), TEXT("fai"), TEXT("sdn"), TEXT("sml"), TEXT("tca"), TEXT("tfa"), TEXT("nfa"),
		TEXT("wls"), TEXT("wbf"), TEXT("wps"), TEXT("wpn"), TEXT("woc"), TEXT("btw"), TEXT("gvw"),
		TEXT("wcl"), TEXT("hdg"), TEXT("hth"), TEXT("hbb"), TEXT("hcm"), TEXT("hwr"), TEXT("hpl"),
		TEXT("hbi"), TEXT("hpd"), TEXT("hfl"), TEXT("fog"), TEXT("ppu"), TEXT("frz"), TEXT("ufz"),
	};
	// The 119 `COND_*` symbols `CAI_BaseNPC`'s registrar `0x102c8ce0` puts into the one condition
	// namespace `DAT_109203dc`, indexed by the GLOBAL id it registers each under. Transcribed from
	// the registrar's argument list and the `.rdata` block `0x10602194`..`0x10602da1`; the registrar
	// calls them out of id order in two places (`COND_ENEMY_TOO_FAR` 0x55 between 0x49 and 0x4c, and
	// `COND_HEAR_BUGBAIT` 0x6c between 0x6b and 0x6e) and the namespace is keyed by id, not by call
	// order, so this array is in ID order.
	inline const TCHAR* const GNpcKernelDebugConditionNames[] = {
		TEXT("COND_NONE"),                      // 0x00
		TEXT("COND_SEE_UNKNOWN"),               // 0x01
		TEXT("COND_LOST_UNKNOWN"),              // 0x02
		TEXT("COND_IGNORE_UNKNOWN"),            // 0x03
		TEXT("COND_UNKNOWN_RUN_TIMER"),         // 0x04
		TEXT("COND_UNKNOWN_ADVANCING"),         // 0x05
		TEXT("COND_UNKNOWN_HOLDING"),           // 0x06
		TEXT("COND_UNKNOWN_RETREATING"),        // 0x07
		TEXT("COND_TOO_CLOSE_FOR_RANGED"),      // 0x08
		TEXT("COND_TOO_FAR_FOR_MELEE"),         // 0x09
		TEXT("COND_BEING_ATTACKED"),            // 0x0a
		TEXT("COND_DETECTED_ATTACK"),           // 0x0b
		TEXT("COND_SHOULD_DODGE"),              // 0x0c
		TEXT("COND_SHOULD_BLOCK"),              // 0x0d
		TEXT("COND_SHOULD_STEPBACK"),           // 0x0e
		TEXT("COND_SHOULD_KICK"),               // 0x0f
		TEXT("COND_SHOULD_INTERACT"),           // 0x10
		TEXT("COND_SHOULD_LOITER"),             // 0x11
		TEXT("COND_CROSSWALK_WALK"),            // 0x12
		TEXT("COND_CROSSWALK_DONTWALK"),        // 0x13
		TEXT("COND_OUTSIDE_INTERRUPT_DIST"),    // 0x14
		TEXT("COND_INSIDE_INTERRUPT_DIST"),     // 0x15
		TEXT("COND_OUTSIDE_INTERRUPT_DIST_E"),  // 0x16
		TEXT("COND_INSIDE_INTERRUPT_DIST_E"),   // 0x17
		TEXT("COND_OUTSIDE_INTERRUPT_DIST_F"),  // 0x18
		TEXT("COND_INSIDE_INTERRUPT_DIST_F"),   // 0x19
		TEXT("COND_INTERRUPT_TIME"),            // 0x1a
		TEXT("COND_HAVE_ENEMY_THROW_LOS"),      // 0x1b
		TEXT("COND_CLAW_HINT_INVALID"),         // 0x1c
		TEXT("COND_CLAW_HINT_SPECIAL_INVALID"), // 0x1d
		TEXT("COND_INVESTIGATE_LEVEL"),         // 0x1e
		TEXT("COND_CRIMINAL_FLEE_LEVEL"),       // 0x1f
		TEXT("COND_CRIMINAL_ATTACK_LEVEL"),     // 0x20
		TEXT("COND_SUPERNATURAL_FLEE_LEVEL"),   // 0x21
		TEXT("COND_SUPERNATURAL_ATTACK_LEVEL"), // 0x22
		TEXT("COND_CAN_POUNCE"),                // 0x23
		TEXT("COND_PASS_OUT"),                  // 0x24
		TEXT("COND_INVESTIGATE_SOUND"),         // 0x25
		TEXT("COND_INVESTIGATE_SIGHT"),         // 0x26
		TEXT("COND_COMFORT"),                   // 0x27
		TEXT("COND_KNOCKBACK"),                 // 0x28
		TEXT("COND_HINT_INVALID"),              // 0x29
		TEXT("COND_KICK_PROP_INVALID"),         // 0x2a
		TEXT("COND_PLAYER_SNARL_RANGE"),        // 0x2b
		TEXT("COND_STOP_BACKUP"),               // 0x2c
		TEXT("COND_SEE_SOUND_SOURCE"),          // 0x2d
		TEXT("COND_EXTENDED_BLOCKED_BY_FRIEND"),// 0x2e
		TEXT("COND_WAITING_ATTACK_TIME"),       // 0x2f
		TEXT("COND_ON_FIRE"),                   // 0x30
		TEXT("COND_SQUAD_SEE_ENEMY"),           // 0x31
		TEXT("COND_SQUAD_LOS_ENEMY"),           // 0x32
		TEXT("COND_HEAR_FLANK_SOUND"),          // 0x33
		TEXT("COND_HIT_BY_DOOR"),               // 0x34
		TEXT("COND_SHOULD_CHARGE"),             // 0x35
		TEXT("COND_FLYING_WALL_HIT"),           // 0x36
		TEXT("COND_FLYING_NPC_HIT"),            // 0x37
		TEXT("COND_WAS_BUMPED"),                // 0x38
		TEXT("COND_COVER_FAILURE"),             // 0x39
		TEXT("COND_ENEMY_BLOCKED"),             // 0x3a
		TEXT("COND_PLAYER_ON_HEAD"),            // 0x3b
		TEXT("COND_WEAPON_THROUGH_WALL"),       // 0x3c
		TEXT("COND_SEE_CORPSE"),                // 0x3d
		TEXT("COND_SEE_CORPSE_FRIEND"),         // 0x3e
		TEXT("COND_LOW_PRIMARY_AMMO"),          // 0x3f
		TEXT("COND_NO_PRIMARY_AMMO"),           // 0x40
		TEXT("COND_NO_SECONDARY_AMMO"),         // 0x41
		TEXT("COND_NO_WEAPON"),                 // 0x42
		TEXT("COND_SEE_HATE"),                  // 0x43
		TEXT("COND_SEE_FEAR"),                  // 0x44
		TEXT("COND_SEE_DISLIKE"),               // 0x45
		TEXT("COND_SEE_ENEMY"),                 // 0x46
		TEXT("COND_LOST_ENEMY"),                // 0x47
		TEXT("COND_ENEMY_OCCLUDED"),            // 0x48
		TEXT("COND_TARGET_OCCLUDED"),           // 0x49
		TEXT("COND_HAVE_ENEMY_LOS"),            // 0x4a
		TEXT("COND_HAVE_TARGET_LOS"),           // 0x4b
		TEXT("COND_LIGHT_DAMAGE"),              // 0x4c
		TEXT("COND_HEAVY_DAMAGE"),              // 0x4d
		TEXT("COND_REPEATED_DAMAGE"),           // 0x4e
		TEXT("COND_CAN_RANGE_ATTACK1"),         // 0x4f
		TEXT("COND_CAN_RANGE_ATTACK2"),         // 0x50
		TEXT("COND_CAN_MELEE_ATTACK1"),         // 0x51
		TEXT("COND_CAN_MELEE_ATTACK2"),         // 0x52
		TEXT("COND_PROVOKED"),                  // 0x53
		TEXT("COND_NEW_ENEMY"),                 // 0x54
		TEXT("COND_ENEMY_TOO_FAR"),             // 0x55
		TEXT("COND_ENEMY_FACING_ME"),           // 0x56
		TEXT("COND_BEHIND_ENEMY"),              // 0x57
		TEXT("COND_ENEMY_DEAD"),                // 0x58
		TEXT("COND_ENEMY_UNREACHABLE"),         // 0x59
		TEXT("COND_SEE_PLAYER"),                // 0x5a
		TEXT("COND_SEE_NEMESIS"),               // 0x5b
		TEXT("COND_TASK_FAILED"),               // 0x5c
		TEXT("COND_SCHEDULE_DONE"),             // 0x5d
		TEXT("COND_SMELL"),                     // 0x5e
		TEXT("COND_TOO_CLOSE_TO_ATTACK"),       // 0x5f
		TEXT("COND_TOO_FAR_TO_ATTACK"),         // 0x60
		TEXT("COND_NOT_FACING_ATTACK"),         // 0x61
		TEXT("COND_WEAPON_HAS_LOS"),            // 0x62
		TEXT("COND_WEAPON_BLOCKED_BY_FRIEND"),  // 0x63
		TEXT("COND_WEAPON_PLAYER_IN_SPREAD"),   // 0x64
		TEXT("COND_WEAPON_PLAYER_NEAR_TARGET"), // 0x65
		TEXT("COND_WEAPON_SIGHT_OCCLUDED"),     // 0x66
		TEXT("COND_BETTER_WEAPON_AVAILABLE"),   // 0x67
		TEXT("COND_GIVE_WAY"),                  // 0x68
		TEXT("COND_WAY_CLEAR"),                 // 0x69
		TEXT("COND_HEAR_DANGER"),               // 0x6a
		TEXT("COND_HEAR_THUMPER"),              // 0x6b
		TEXT("COND_HEAR_BUGBAIT"),              // 0x6c
		TEXT("COND_HEAR_COMBAT"),               // 0x6d
		TEXT("COND_HEAR_WORLD"),                // 0x6e
		TEXT("COND_HEAR_PLAYER"),               // 0x6f
		TEXT("COND_HEAR_BULLET_IMPACT"),        // 0x70
		TEXT("COND_HEAR_PHYSICS_DANGER"),       // 0x71
		TEXT("COND_HEAR_FLINCH"),               // 0x72
		TEXT("COND_FLOATING_OFF_GROUND"),       // 0x73
		TEXT("COND_PLAYER_PUSHING"),            // 0x74
		TEXT("COND_NPC_FREEZE"),                // 0x75
		TEXT("COND_NPC_UNFREEZE"),              // 0x76
	};
}
