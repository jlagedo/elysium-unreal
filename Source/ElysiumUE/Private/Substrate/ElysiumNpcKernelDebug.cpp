#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **Debug** — the 21 `DevMsg` / debug-ring / text-overlay / `NDebugOverlay`
// bodies of `order.md` layers 0–9. The walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// THE STANDING FACT OF THIS FAMILY: none of these bodies has an output device in this runtime.
// Retail has four — `DevMsg`, the ConVar-gated message ring `0x10119750`, the engine's
// `AddEntityTextOverlay`, and `NDebugOverlay` — and this substrate has none. So every arm is ported
// verbatim, the format strings are retail's own, and the line lands on `FDebugLine`: written to
// `LogElysiumNpcEnt` and, while a capture is open, recorded in emission order. A test reads the
// capture, which is why the ORDER of these arms and the GATES on them are the deliverable and the
// picture is not.
//
// This file: the seam, the two id spaces, the three name tables, and the twelve bodies that are
// text. `ElysiumNpcKernelDebug2.cpp` carries the eight that are geometry.

namespace
{
	// Unit-prefixed because the module builds adaptive-unity and this anonymous namespace is
	// regularly merged with others.

	// The open capture, or empty. Game-thread only, like the rest of the substrate.
	TArray<FElysiumNpc::FDebugLine> GNpcKernelDebugCapture;
	bool GNpcKernelDebugCapturing = false;

	const TCHAR* const GNpcKernelDebugChannelDevMsg = TEXT("DevMsg");
	const TCHAR* const GNpcKernelDebugChannelMsg = TEXT("Msg");
	const TCHAR* const GNpcKernelDebugChannelEntityText = TEXT("EntityText");
	const TCHAR* const GNpcKernelDebugChannelOverlay = TEXT("Overlay");

	void GNpcKernelDebugRecord(const TCHAR* Channel, const TCHAR* Retail, FString&& Text,
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

	// Source units out of the port's centimetres. Retail's overlay arguments are all source units,
	// and reproducing a box half-extent of 5 as 12.7 would hide the recovered constant.
	FString GNpcKernelDebugVec(const FVector& Cm)
	{
		const FVector Units = Cm / ElysiumMove::U;
		return FString::Printf(TEXT("(%.1f %.1f %.1f)"), Units.X, Units.Y, Units.Z);
	}

	// A vector that is ALREADY in source units — a literal box extent out of the retail body.
	FString GNpcKernelDebugUnitVec(const FVector& Units)
	{
		return FString::Printf(TEXT("(%.1f %.1f %.1f)"), Units.X, Units.Y, Units.Z);
	}

	// `CAI_GlobalNamespace::IdToSymbol`'s answer for -1 (`s_<<null>>_1060f71c`, `0x102ea020`).
	const TCHAR* const GNpcKernelDebugNullSymbol = TEXT("<<null>>");

	// The 119 `COND_*` symbols `CAI_BaseNPC`'s registrar `0x102c8ce0` puts into the one condition
	// namespace `DAT_109203dc`, indexed by the GLOBAL id it registers each under. Transcribed from
	// the registrar's argument list and the `.rdata` block `0x10602194`..`0x10602da1`; the registrar
	// calls them out of id order in two places (`COND_ENEMY_TOO_FAR` 0x55 between 0x49 and 0x4c, and
	// `COND_HEAR_BUGBAIT` 0x6c between 0x6b and 0x6e) and the namespace is keyed by id, not by call
	// order, so this array is in ID order.
	const TCHAR* const GNpcKernelDebugConditionNames[] = {
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
	static_assert(UE_ARRAY_COUNT(GNpcKernelDebugConditionNames) == 0x77,
		"0x102c8ce0 registers exactly 119 conditions, ids 0x00..0x76");

	// `0x1027e7f0`'s jump table, in the same id order: three characters per condition, read straight
	// out of `.rdata`. The strings sit four bytes apart descending from `0x105cd62c` (id 0) to
	// `0x105cd458` (id 0x76) — except id 0x73, whose `"fog"` is the outlier at `0x1058b0a0` — and
	// `"***"` at `0x105cd454` is the `default:` arm. Each one abbreviates the `COND_*` name at the
	// same index above, which is what confirms both tables at once.
	const TCHAR* const GNpcKernelDebugShortConditionNames[] = {
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
	static_assert(UE_ARRAY_COUNT(GNpcKernelDebugShortConditionNames) == 0x77,
		"0x1027e7f0's switch covers ids 0x00..0x76 and nothing else");

	// `0x1027e7f0`'s `default:` arm, `&DAT_105cd454`.
	const TCHAR* const GNpcKernelDebugShortConditionDefault = TEXT("***");

	// `0x1027e760`'s jump table, the navigation-type names slot 407 forwards to. Read out of
	// `.rdata`: `0x105cd44c`, `0x105cd444`, `0x105cd440`, `0x105cd438` for 0..3 and `0x10547418` for
	// -1, with `0x105477a4` the `default:`.
	const TCHAR* const GNpcKernelDebugNavTypeNames[] = {
		TEXT("Ground"), TEXT("Jump"), TEXT("Fly"), TEXT("Climb") };
	const TCHAR* const GNpcKernelDebugNavTypeNone = TEXT("None");
	const TCHAR* const GNpcKernelDebugNavTypeUnknown = TEXT("**UNKNOWN**");

	// `CNPC_VMingXiao#408` (`0x103951d0`), ids 0x77..0x7e — `0x10647194` down to `0x10647178`.
	const TCHAR* const GNpcKernelDebugMingXiaoShort[] = {
		TEXT("xfr"), TEXT("xfl"), TEXT("xmr"), TEXT("xml"),
		TEXT("xbr"), TEXT("xbl"), TEXT("xsp"), TEXT("xmh") };

	// `CNPC_VMingXiaoTentacle#408` (`0x1039ece0`), ids 0x77..0x79 — `0x1064a42c`, `0x1064a430`,
	// `0x1064a434`. Note the ADDRESSES ascend here where every other block descends.
	const TCHAR* const GNpcKernelDebugTentacleShort[] = {
		TEXT("tfl"), TEXT("tsc"), TEXT("tpe") };

	// `CNPC_VWerewolf#408` (`0x103d0640`), ids 0x77..0x7b — `0x106623d0` down to `0x106623c0`.
	const TCHAR* const GNpcKernelDebugWerewolfShort[] = {
		TEXT("ww0"), TEXT("ww1"), TEXT("ww2"), TEXT("ww3"), TEXT("ww4") };

	// `CNPC_VTzimisce::GetEventName` (`0x103bdd10`), anim-event ids 2..8. The body `strcpy`s the
	// literal into the caller's buffer; ids outside the range fall through to
	// `CBaseAnimating::GetEventName`, which is what a null answer means here.
	const TCHAR* const GNpcKernelDebugTzimisceEventNames[] = {
		TEXT("START_IDLE"),      // 2, 0x1065c87c
		TEXT("START_FIDGET"),    // 3, 0x1065c86c
		TEXT("START_RUN"),       // 4, 0x1065c860
		TEXT("START_LANDHARD"),  // 5, 0x1065c84c
		TEXT("START_ATTACK"),    // 6, 0x1065c83c
		TEXT("START_ATTACKBIG"), // 7, 0x1065c828
		TEXT("START_POUNCE"),    // 8, 0x1065c818
	};
}

// -------------------------------------------------------------------------------------------------
// The output seam.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::BeginDebugCapture()
{
	GNpcKernelDebugCapture.Reset();
	GNpcKernelDebugCapturing = true;
}

TArray<FElysiumNpc::FDebugLine> FElysiumNpc::EndDebugCapture()
{
	GNpcKernelDebugCapturing = false;
	return MoveTemp(GNpcKernelDebugCapture);
}

void FElysiumNpc::EmitDevMsg(const TCHAR* RetailFormat, const FString& Text)
{
	// Retail's `DevMsg(fmt, …)`. `ReportAIState` is the only body in this family that uses it.
	GNpcKernelDebugRecord(GNpcKernelDebugChannelDevMsg, RetailFormat, CopyTemp(Text), INDEX_NONE);
}

void FElysiumNpc::EmitDebugMsg(const TCHAR* RetailFormat, const FString& Text)
{
	// Retail's `0x10119750`: four ConVar gates, then `Q_vsnprintf` into 512 bytes, then `strncpy`
	// of the first 0x60 into a growing ring of 0x60-byte rows. The gates are console variables this
	// runtime has no counterpart for, so the write is unconditional here — a stated divergence, and
	// the only one in this family: retail's gate decides WHETHER a developer sees the line, never
	// what the line says or in what order the arms ran.
	GNpcKernelDebugRecord(GNpcKernelDebugChannelMsg, RetailFormat, CopyTemp(Text), INDEX_NONE);
}

void FElysiumNpc::EmitEntityText(int32 Line, const TCHAR* RetailFormat, const FString& Text)
{
	// Retail's `DAT_1070b22c`+0x8c — `IVEngineServer::AddEntityTextOverlay(edictIndex, line, 0,
	// 255, 255, 255, 255, text)` followed by `0x101434b0`. The colour is white and opaque on every
	// call site in this family, so it is not carried.
	GNpcKernelDebugRecord(GNpcKernelDebugChannelEntityText, RetailFormat, CopyTemp(Text), Line);
}

void FElysiumNpc::EmitOverlayBox(const TCHAR* RetailCall, const FVector& OriginUnits,
	const FVector& MinsUnits, const FVector& MaxsUnits, int32 R, int32 G, int32 B, int32 A)
{
	GNpcKernelDebugRecord(GNpcKernelDebugChannelOverlay, RetailCall,
		FString::Printf(TEXT("%s mins=%s maxs=%s rgba=(%d %d %d %d)"),
			*GNpcKernelDebugUnitVec(OriginUnits), *GNpcKernelDebugUnitVec(MinsUnits),
			*GNpcKernelDebugUnitVec(MaxsUnits), R, G, B, A),
		INDEX_NONE);
}

void FElysiumNpc::EmitOverlayBoxDirection(const TCHAR* RetailCall, const FVector& OriginUnits,
	const FVector& MinsUnits, const FVector& MaxsUnits, const FVector& Direction, int32 R, int32 G,
	int32 B, int32 A)
{
	GNpcKernelDebugRecord(GNpcKernelDebugChannelOverlay, RetailCall,
		FString::Printf(TEXT("%s mins=%s maxs=%s dir=%s rgba=(%d %d %d %d)"),
			*GNpcKernelDebugUnitVec(OriginUnits), *GNpcKernelDebugUnitVec(MinsUnits),
			*GNpcKernelDebugUnitVec(MaxsUnits), *GNpcKernelDebugUnitVec(Direction), R, G, B, A),
		INDEX_NONE);
}

void FElysiumNpc::EmitOverlayLine(const TCHAR* RetailCall, const FVector& StartUnits,
	const FVector& EndUnits, int32 R, int32 G, int32 B, bool bNoDepthTest)
{
	GNpcKernelDebugRecord(GNpcKernelDebugChannelOverlay, RetailCall,
		FString::Printf(TEXT("%s -> %s rgb=(%d %d %d) nodepth=%d"),
			*GNpcKernelDebugUnitVec(StartUnits), *GNpcKernelDebugUnitVec(EndUnits), R, G, B,
			bNoDepthTest ? 1 : 0),
		INDEX_NONE);
}

void FElysiumNpc::EmitOverlayText(const TCHAR* RetailCall, const FVector& OriginUnits,
	const FString& Text)
{
	GNpcKernelDebugRecord(GNpcKernelDebugChannelOverlay, RetailCall,
		FString::Printf(TEXT("%s \"%s\""), *GNpcKernelDebugUnitVec(OriginUnits), *Text),
		INDEX_NONE);
}

void FElysiumNpc::EmitOverlayEntityBounds(const TCHAR* RetailCall, int32 R, int32 G, int32 B,
	int32 A) const
{
	GNpcKernelDebugRecord(GNpcKernelDebugChannelOverlay, RetailCall,
		FString::Printf(TEXT("%s rgba=(%d %d %d %d)"), *DebugString(), R, G, B, A), INDEX_NONE);
}

// -------------------------------------------------------------------------------------------------
// The two id spaces.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::IdSpaceLocalToGlobal(const FKernelIdSpace* Rows, int32 Count, int32 LocalId)
{
	// `0x102ea2d0` verbatim:
	//
	//     if (id != -1) do {
	//         if (this->localBase != 9999 && this->localBase <= id && id <= this->localTop)
	//             return (this->globalBase - this->localBase) + id;
	//         this = this->parent;                     // +0x10
	//     } while (this != NULL);
	//     return -1;
	//
	// The 9999 test comes FIRST, so an empty space is skipped even when the id would fall inside a
	// `[9999, -1]` range that cannot hold anything anyway.
	if (LocalId == INDEX_NONE || Rows == nullptr)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FKernelIdSpace& Space = Rows[Index];
		if (Space.LocalBase != 9999 && Space.LocalBase <= LocalId && LocalId <= Space.LocalTop)
		{
			return (Space.GlobalBase - Space.LocalBase) + LocalId;
		}
	}
	return INDEX_NONE;
}

const FElysiumNpc::FKernelIdSpace* FElysiumNpc::ConditionIdSpaceRows(int32& OutCount)
{
	// The chain slot 458 walks, from this leaf upward. Two rows, because this runtime stands the
	// Troika line for every classname and no SPECIES class registers a condition into a space of
	// its own that the port could carry (the three that DO add conditions — `CNPC_VMingXiao`,
	// `CNPC_VMingXiaoTentacle`, `CNPC_VWerewolf` — are slot 408's species table below, and their
	// spaces are runtime state no static read recovers).
	//
	//   * `CAI_BaseNPCTroika`'s own condition space is `DAT_10924248 + 0x30`. Nothing in the image
	//     registers a condition into it, so it keeps the empty state `0x102ea090(isRoot = false)`
	//     left: `LocalBase` 9999, and the walk falls straight through to its parent.
	//   * `CAI_BaseNPC`'s is `DAT_1090ff08 + 0x30` = `DAT_1090ff38`, and `0x102c8ce0` registers 119
	//     conditions into it at local ids 0x00..0x76 — the FIRST is `COND_NONE` at 0, so
	//     `0x102ea130`'s "expand an empty space" arm sets `LocalBase` 0 and the last raises
	//     `LocalTop` to 0x76. `GlobalBase` is 0 because this is the namespace's root space
	//     (`0x1030c4e0` seeds it with `0x102ea0e0(&DAT_1090ff38, &DAT_109203dc, 0)`).
	//
	// So the recovered translation for a base condition is the IDENTITY, which is why this runtime's
	// `EElysiumNpcCond` values are retail's registered numbers and read correctly here.
	static const FKernelIdSpace GRows[] = {
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x10924278"), INDEX_NONE, 9999, INDEX_NONE },
		{ TEXT("CAI_BaseNPC"), TEXT("0x1090ff38"), 0, 0, 0x76 },
	};
	OutCount = UE_ARRAY_COUNT(GRows);
	return GRows;
}

const FElysiumNpc::FKernelIdSpace* FElysiumNpc::TaskIdSpaceRows(int32& OutCount)
{
	// The same chain for TASKS — `DAT_10924248 + 0x18` and `DAT_1090ff08 + 0x18` = `DAT_1090ff20`.
	//
	// **Both rows are the empty sentinel, and the base one is UNRECOVERED rather than known-empty.**
	// `0x10316ff0` registers the 441 `TASK_*` symbols into `DAT_1090ff20` at runtime and this family
	// did not transcribe that table: this runtime's task vocabulary is `EElysiumTask`, a typed
	// ~30-identity subset with NO registered numbers at all (family BaseHelpers'
	// `CurrentRetailTaskNumber` answers false for exactly this reason), so a number to translate
	// never arrives. The row is left empty and says so rather than carrying a range nothing can use.
	static const FKernelIdSpace GRows[] = {
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x10924260"), INDEX_NONE, 9999, INDEX_NONE },
		{ TEXT("CAI_BaseNPC"), TEXT("0x1090ff20"), INDEX_NONE, 9999, INDEX_NONE },
	};
	OutCount = UE_ARRAY_COUNT(GRows);
	return GRows;
}

// -------------------------------------------------------------------------------------------------
// The three global name tables.
// -------------------------------------------------------------------------------------------------

const TCHAR* FElysiumNpc::GlobalConditionName(int32 GlobalConditionId)
{
	// `0x102ea020`: `if (id == -1) return "<<null>>"; return IdToSymbol(this->table, id);`, and the
	// table walk (`0x10249c70`) answers NULL for an id it does not carry — which retail then hands
	// to `printf` as a `%s`.
	if (GlobalConditionId == INDEX_NONE)
	{
		return GNpcKernelDebugNullSymbol;
	}
	if (GlobalConditionId >= 0
		&& GlobalConditionId < UE_ARRAY_COUNT(GNpcKernelDebugConditionNames))
	{
		return GNpcKernelDebugConditionNames[GlobalConditionId];
	}
	return nullptr;
}

const TCHAR* FElysiumNpc::GlobalTaskName(int32 GlobalTaskId)
{
	// The same body over `DAT_109203d4`. SEAM: see `TaskIdSpaceRows` — the 441 `TASK_*` symbols
	// `0x10316ff0` registers are not carried here, so every id but -1 answers the namespace's
	// "no such symbol", which is null.
	return GlobalTaskId == INDEX_NONE ? GNpcKernelDebugNullSymbol : nullptr;
}

const TCHAR* FElysiumNpc::ShortConditionNameTable(int32 ConditionId)
{
	// `0x1027e7f0`. A dense switch 0x00..0x76 and a `default:`; no -1 arm, so -1 gets `"***"` here
	// exactly as it does in retail.
	if (ConditionId >= 0 && ConditionId < UE_ARRAY_COUNT(GNpcKernelDebugShortConditionNames))
	{
		return GNpcKernelDebugShortConditionNames[ConditionId];
	}
	return GNpcKernelDebugShortConditionDefault;
}

// -------------------------------------------------------------------------------------------------
// Slot 408 `GetShortConditionName` — one method and a species table.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FShortConditionSpecies* FElysiumNpc::ShortConditionSpeciesRows(int32& OutCount)
{
	// All three bodies are the same shape: a switch over a contiguous block starting at 0x77 — the
	// id straight above the base table's last — and a `default:` that forwards to
	// `CAI_BaseNPC::GetShortConditionName` (`thunk_FUN_1027ede0`). Only the werewolf's wraps the
	// whole thing in a scope-trace push, which it pops on EVERY arm including the forward.
	static const FShortConditionSpecies GRows[] = {
		{ TEXT("CNPC_VMingXiao"), TEXT("0x103951d0"), false, 0x77,
			GNpcKernelDebugMingXiaoShort, UE_ARRAY_COUNT(GNpcKernelDebugMingXiaoShort) },
		{ TEXT("CNPC_VMingXiaoTentacle"), TEXT("0x1039ece0"), false, 0x77,
			GNpcKernelDebugTentacleShort, UE_ARRAY_COUNT(GNpcKernelDebugTentacleShort) },
		{ TEXT("CNPC_VWerewolf"), TEXT("0x103d0640"), true, 0x77,
			GNpcKernelDebugWerewolfShort, UE_ARRAY_COUNT(GNpcKernelDebugWerewolfShort) },
	};
	OutCount = UE_ARRAY_COUNT(GRows);
	return GRows;
}

const FElysiumNpc::FShortConditionSpecies* FElysiumNpc::ShortConditionSpeciesOf(
	const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	int32 Count = 0;
	const FShortConditionSpecies* Rows = ShortConditionSpeciesRows(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (FCString::Strcmp(Rows[Index].RetailClass, InRetailClass) == 0)
		{
			return &Rows[Index];
		}
	}
	return nullptr;
}

const TCHAR* FElysiumNpc::GetShortConditionName(int32 ConditionId)
{
	// slot 408, `CAI_BaseNPC::GetShortConditionName` `0x1027ede0` — sixteen bytes:
	//     thunk_FUN_1027e7f0(param_1); return;
	// plus the three species overrides, which this leaf resolves as a table rather than a vtable.
	//
	// `OverrideOf` walks the class chain upward, so a subclass of `CNPC_VWerewolf` inherits its
	// block exactly as the vtable would.
	const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 408);
	if (Override != nullptr)
	{
		const FShortConditionSpecies* Species = ShortConditionSpeciesOf(Override->Class);
		if (Species != nullptr)
		{
			// `CNPC_VWerewolf`'s scope-trace push, `g_ScopeTraceStack[depth] = { "CNPC_VWerewolf::
			// GetShortConditionName", m_iName ? m_iName : "", "" }` then `++depth`, popped on every
			// arm. The port has no scope-trace stack; the push is recorded so the arm is visible and
			// the entity it names is the one retail names.
			if (Species->bScopeTraced)
			{
				UE_LOG(LogElysiumNpcEnt, VeryVerbose, TEXT("%s::GetShortConditionName %s"),
					Species->RetailClass, TargetName.IsEmpty() ? TEXT("") : *TargetName);
			}
			const int32 Offset = ConditionId - Species->FirstId;
			if (Offset >= 0 && Offset < Species->NameCount)
			{
				return Species->Names[Offset];
			}
		}
	}
	return ShortConditionNameTable(ConditionId);
}

// -------------------------------------------------------------------------------------------------
// Slots 458 / 449 — `ConditionName` and `TaskName`.
// -------------------------------------------------------------------------------------------------

const TCHAR* FElysiumNpc::ConditionName(int32 ConditionId)
{
	// slot 458, `0x102cc300`:
	//
	//     if (param_1 < 1000000000 || param_1 == -1)
	//         param_1 = ConditionLocalToGlobal(GetClassScheduleIdSpace() + 0x30, param_1);
	//     IdToSymbol(&DAT_109203dc, param_1);
	//
	// The 1e9 gate is retail's "this is already a GLOBAL id" test — the same constant family Squad
	// found seeding the squad-slot namespace (`0x3b9aca00`) — so a script-registered id above it
	// skips the translation and goes straight to the namespace.
	int32 GlobalId = ConditionId;
	if (ConditionId < 1000000000 || ConditionId == INDEX_NONE)
	{
		int32 Count = 0;
		const FKernelIdSpace* Rows = ConditionIdSpaceRows(Count);
		GlobalId = IdSpaceLocalToGlobal(Rows, Count, ConditionId);
	}
	return GlobalConditionName(GlobalId);
}

TCHAR* FElysiumNpc::TaskName(int32 TaskId)
{
	// slot 449, `0x102cc350` — byte-for-byte `ConditionName` against the TASK sub-space (`+0x18`)
	// and the task namespace `DAT_109203d4`. Ten direct callers and eight dispatch sites, all of
	// them debug output.
	//
	// The return is retail's non-const `char*` into `.rdata`; the generated declaration keeps the
	// non-constness, so the literal is cast the same way retail's pointer is.
	int32 GlobalId = TaskId;
	if (TaskId < 1000000000 || TaskId == INDEX_NONE)
	{
		int32 Count = 0;
		const FKernelIdSpace* Rows = TaskIdSpaceRows(Count);
		GlobalId = IdSpaceLocalToGlobal(Rows, Count, TaskId);
	}
	return const_cast<TCHAR*>(GlobalTaskName(GlobalId));
}

// -------------------------------------------------------------------------------------------------
// Slots 407 / 451 / 14 — the three name answers.
// -------------------------------------------------------------------------------------------------

const TCHAR* FElysiumNpc::GetNavTypeName(int32 NavType)
{
	// slot 407, `0x1027e7d0` — sixteen bytes forwarding to the table `0x1027e760`:
	//     0 Ground, 1 Jump, 2 Fly, 3 Climb, -1 None, default **UNKNOWN**.
	//
	// 29c's walk read this as "Ground / two unnamed / Climb, with -1 answering the same empty string
	// state 0 does"; the `.rdata` says otherwise on both counts and is what is ported. This runtime
	// stands no `Navigation_t` yet — the motor family's navigator seam has no type word — so nothing
	// calls this with a live value; the table is the recovered answer all the same.
	if (NavType == INDEX_NONE)
	{
		return GNpcKernelDebugNavTypeNone;
	}
	if (NavType >= 0 && NavType < UE_ARRAY_COUNT(GNpcKernelDebugNavTypeNames))
	{
		return GNpcKernelDebugNavTypeNames[NavType];
	}
	return GNpcKernelDebugNavTypeUnknown;
}

const TCHAR* FElysiumNpc::BaseSchedulingErrorName()
{
	// `0x101a6660`, slot 451's BASE body — six bytes, `return s_CAI_BaseNPC_10594820;` = the
	// classname string `"CAI_BaseNPC"`. Not an error message: it is the name a scheduling error is
	// REPORTED AGAINST, which is why each tier answers its own class name.
	return TEXT("CAI_BaseNPC");
}

TCHAR* FElysiumNpc::GetSchedulingErrorName()
{
	// slot 451, `0x101aa7b0` — the Troika line's override, six bytes,
	// `return PTR_s_CAI_BaseNPCTroika_105d1060;` which dereferences to `"CAI_BaseNPCTroika"`
	// (`0x105d748c`). Every classname this runtime stands is on the Troika line, so this is the
	// answer for all of them and `BaseSchedulingErrorName` is reachable only from the classes
	// between `CAI_BaseNPC` and `CAI_BaseNPCTroika`, none of which is a spawnable leaf.
	return const_cast<TCHAR*>(TEXT("CAI_BaseNPCTroika"));
}

const TCHAR* FElysiumNpc::DebugGetClassName()
{
	// slot 14, `CAISound::FUN_1009af00` — four bytes, `return (int)&this->field_0x24;`, the ADDRESS
	// of the embedded classname buffer at `CBaseEntity+0x0024`. The shape map binds that word as
	// `FElysiumEntity::Class`, "retail's debug copy of the classname; the registry descriptor
	// carries it", so the answer is the classname the registry resolved.
	//
	// Note this is the address of a fixed-size char array, not a pointer read out of it: retail
	// answers a non-null string even for an entity whose classname was never written, because the
	// buffer is always there. Reproduced by answering the empty string rather than null when the
	// def carries no classname.
	return Def != nullptr ? *Def->Classname : TEXT("");
}

const TCHAR* FElysiumNpc::TzimisceEventName(int32 EventId)
{
	// `CNPC_VTzimisce::GetEventName` `0x103bdd10`, slot 241's only species override. Retail's
	// signature is `void GetEventName(char* out, animevent_t* event)` and each arm `strcpy`s its
	// literal into `out`; ids outside 2..8 tail into `CBaseAnimating::GetEventName`.
	//
	// Named `TzimisceEventName` and NOT `GetEventName`: slot 241's Troika-line body (`0x1008c170`)
	// is still a generated stub owned by a later story, and it is what holds the `GetEventName`
	// name. When that body lands it dispatches here for `CNPC_VTzimisce`.
	const int32 Offset = EventId - 2;
	if (Offset >= 0 && Offset < UE_ARRAY_COUNT(GNpcKernelDebugTzimisceEventNames))
	{
		return GNpcKernelDebugTzimisceEventNames[Offset];
	}
	return nullptr;
}

// -------------------------------------------------------------------------------------------------
// Slot 581 `ReportAIState` — `0x102779a0`, the SDK dev dump.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::ReportAIState()
{
	// `0x102779a0`, arm by arm and in retail's exact order. Every line is a separate `DevMsg`, and
	// several of them are deliberately unterminated (`"%s: "`, `"State: %s, "`) because the dump is
	// one console line assembled from fragments.
	//
	// Retail evaluates `m_NPCState` into a register BEFORE the first `DevMsg`, then uses that same
	// register as `GetClassname`'s dead second argument on all three calls — an artefact, not a
	// behaviour, and not reproduced.

	// 1. `DevMsg("%s: ", GetClassname())` then `DevMsg("State: %s, ", GetStateName(m_NPCState))`.
	EmitDevMsg(TEXT("%s: "), FString::Printf(TEXT("%s: "),
		Def != nullptr ? *Def->Classname : TEXT("")));
	const TCHAR* StateName = GetStateName(GetMind().State());
	EmitDevMsg(TEXT("State: %s, "), FString::Printf(TEXT("State: %s, "),
		StateName != nullptr ? StateName : TEXT("")));

	// 2. `if (m_Activity != -1 && m_IdealActivity != -1)`: BOTH must be set, and the line prints the
	//    activity names resolved through `SelectWeightedSequence(act, -1)` +
	//    `GetSequenceActivityName(seq)` — the name of the activity the CHOSEN SEQUENCE carries, not
	//    the name of the activity asked for. `ActivityNumber` (+0x0fec, family Positions) and
	//    `IdealActivityNumber` (+0x0ff0, family Facing) are the port's two words.
	if (ActivityNumber != INDEX_NONE && IdealActivityNumber != INDEX_NONE)
	{
		EmitDevMsg(TEXT("Activity: %s  -  Ideal Activity: %s\n"),
			FString::Printf(TEXT("Activity: %s  -  Ideal Activity: %s\n"),
				*ActivityNameForNumber(ActivityNumber),
				*ActivityNameForNumber(IdealActivityNumber)));
	}

	// 3. `if (!m_pSchedule) DevMsg("No Schedule, ")` else the name (or `"Unknown"` for a null name
	//    pointer) and, when `GetCurTask()` answers a task, `"Task %d (#%d), "` with the task's
	//    retail NUMBER and `m_ScheduleState`. The number is `CurrentRetailTaskNumber`'s seam.
	if (!Schedule.IsRunning())
	{
		EmitDevMsg(TEXT("No Schedule, "), TEXT("No Schedule, "));
	}
	else
	{
		const TCHAR* ScheduleName = ElysiumScheduleName(Schedule.Current);
		EmitDevMsg(TEXT("Schedule %s, "), FString::Printf(TEXT("Schedule %s, "),
			ScheduleName != nullptr ? ScheduleName : TEXT("Unknown")));
		int32 TaskNumber = INDEX_NONE;
		if (CurrentRetailTaskNumber(TaskNumber))
		{
			EmitDevMsg(TEXT("Task %d (#%d), "),
				FString::Printf(TEXT("Task %d (#%d), "), TaskNumber, Schedule.TaskIndex));
		}
	}

	// 4. `if (!GetEnemy()) DevMsg("No enemy ")` else — and this is the arm to read carefully —
	//    retail RE-DISPATCHES `GetEnemy()` three times, takes the enemy's `GetAbsOrigin()`, builds
	//    `(x, y, z + 64.0)` (`_DAT_10451acc` = 64.0) and hands it to `PTR_DAT_10566258`'s `+0xc`
	//    with `(1, 1, 0)`. That is a DEBUG OVERLAY call on the engine's effect interface, not a
	//    state write: the vector is built and dropped. The line itself is `"\nEnemy is %s"`.
	const FElysiumEntity* Enemy = GetEnemy();
	if (Enemy == nullptr)
	{
		EmitDevMsg(TEXT("No enemy "), TEXT("No enemy "));
	}
	else
	{
		// `_DAT_10451acc` = 64.0f, source units, added to the enemy's origin Z.
		const FVector MarkerUnits = Enemy->Origin / ElysiumMove::U + FVector(0.f, 0.f, 64.f);
		EmitOverlayText(TEXT("PTR_DAT_10566258+0xc"), MarkerUnits, FString());
		EmitDevMsg(TEXT("\nEnemy is %s"), FString::Printf(TEXT("\nEnemy is %s"),
			Enemy->Def != nullptr ? *Enemy->Def->Classname : TEXT("")));
	}

	// 5. `if (IsMoving())`: `" Moving "`, then ONE of two sub-arms.
	//    `m_flMoveWaitFinished > curtime` -> `": Stopped for %.2f. "` with the remaining wait;
	//    otherwise, and only if `m_IdealActivity == GetStoppedActivity()` (`0x1027a6c0`),
	//    `": In stopped anim. "`. Note the comparison is `<=` in the binary, so a wait that expires
	//    exactly on this frame takes the stopped-anim arm.
	if (IsMoving())
	{
		EmitDevMsg(TEXT(" Moving "), TEXT(" Moving "));
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		if (ScheduleHost.MoveWaitFinished > Now)
		{
			EmitDevMsg(TEXT(": Stopped for %.2f. "), FString::Printf(TEXT(": Stopped for %.2f. "),
				ScheduleHost.MoveWaitFinished - Now));
		}
		else if (IsIdealActivityCurrent())
		{
			// SEAM for `GetStoppedActivity()` (`0x1027a6c0`) compared against `m_IdealActivity`:
			// this runtime's ideal activity is a clip identity, and the nearest recovered question
			// it can answer is "is the ideal activity the one currently playing".
			EmitDevMsg(TEXT(": In stopped anim. "), TEXT(": In stopped anim. "));
		}
	}

	// 6. `DevMsg("Leader.")` then `DevMsg("\n")` — UNCONDITIONAL in retail. SDK 2013 gates the same
	//    line on `IsLeader()`; Troika's build prints it for every NPC, and that is reproduced.
	EmitDevMsg(TEXT("Leader."), TEXT("Leader."));
	EmitDevMsg(TEXT("\n"), TEXT("\n"));

	// 7. `DevMsg("Yaw speed:%3.1f,Health: %3d\n", m_pMotor->m_YawSpeed, m_iHealth)`.
	EmitDevMsg(TEXT("Yaw speed:%3.1f,Health: %3d\n"),
		FString::Printf(TEXT("Yaw speed:%3.1f,Health: %3d\n"), MotorYawSpeed(), Health));

	// 8. `GetGroundEntity()` twice: the classname line or the NULL line.
	const FElysiumEntity* Ground = GetGroundEntity();
	if (Ground != nullptr)
	{
		EmitDevMsg(TEXT("Groundent:%s\n\n"), FString::Printf(TEXT("Groundent:%s\n\n"),
			Ground->Def != nullptr ? *Ground->Def->Classname : TEXT("")));
	}
	else
	{
		EmitDevMsg(TEXT("Groundent: NULL\n\n"), TEXT("Groundent: NULL\n\n"));
	}
}

// -------------------------------------------------------------------------------------------------
// Slot 76 `DrawDebugStatOverlays` — three bodies.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::BaseDrawDebugStatOverlays()
{
	// `0x102775e0`. Recovered from the LISTING, not the decompiled C: the decompiler lost four of
	// the six `Q_snprintf` format strings to the 512-byte stack frame, and mislabelled which of the
	// task line's two markers is which.
	//
	// The whole body is gated on `GetModelPtr() != NULL` — no model, no lines at all. The port's
	// question for "does this entity have a model" is `FElysiumEntity::Model`; the studio HEADER
	// behind it is the seam every other read below goes through.
	//
	// 1. `Q_snprintf(buf, 512, "Seq: ")`, then `GetSeqDesc(m_nSequence)`:
	//      no descriptor -> `Q_strncat(buf, "(INVALID)")`
	//      a descriptor  -> `Q_strncat(buf, seqdesc->pszLabel)`, `" / "`, `seqdesc->pszActivityName`
	//    and the assembled buffer is the FIRST line.
	if (Model.IsEmpty())
	{
		return;
	}
	FString Line(TEXT("Seq: "));
	FString SequenceLabel;
	FString SequenceActivityName;
	if (!SequenceDescriptor(SequenceNumber, SequenceLabel, SequenceActivityName))
	{
		Line += TEXT("(INVALID)");
		EmitDebugMsg(TEXT("Seq: (INVALID)"), MoveTemp(Line));
	}
	else
	{
		Line += SequenceLabel;
		Line += TEXT(" / ");
		Line += SequenceActivityName;
		EmitDebugMsg(TEXT("Seq: %s / %s"), MoveTemp(Line));
	}

	// 2. `"Cycle: %.2f"` with `m_flCycle` (+0x6f8).
	EmitDebugMsg(TEXT("Cycle: %.2f"), FString::Printf(TEXT("Cycle: %.2f"), SequenceCycle));

	// 3. The activity line. THREE arms, and the order of the tests matters:
	//      m_Activity == -1 || m_IdealActivity == -1
	//          -> m_Activity == 0  ? "Actv: RESET"   : "Actv: INVALID"
	//      m_Activity == 0         -> "Actv: RESET"
	//      otherwise -> "Actv: %s (%s)\n" with both activities pushed through the SAME three-step
	//          translation retail applies before naming them: slot 375 (NPC_EarlyTranslateActivity),
	//          slot 381 (Weapon_TranslateActivity), slot 376 (NPC_TranslateActivity), then
	//          SelectWeightedSequence + GetSequenceActivityName.
	//    `ACT_RESET` is 0 and `ACT_INVALID` is -1, which is what the two literals name.
	if (ActivityNumber == INDEX_NONE || IdealActivityNumber == INDEX_NONE)
	{
		EmitDebugMsg(ActivityNumber == 0 ? TEXT("Actv: RESET") : TEXT("Actv: INVALID"),
			ActivityNumber == 0 ? TEXT("Actv: RESET") : TEXT("Actv: INVALID"));
	}
	else if (ActivityNumber == 0)
	{
		EmitDebugMsg(TEXT("Actv: RESET"), TEXT("Actv: RESET"));
	}
	else
	{
		EmitDebugMsg(TEXT("Actv: %s (%s)\n"), FString::Printf(TEXT("Actv: %s (%s)\n"),
			*ActivityNameForNumber(ActivityNumber),
			*ActivityNameForNumber(IdealActivityNumber)));
	}

	// 4. `"State: %s, "` with `GetStateName(m_NPCState)` (slot 406).
	const TCHAR* StateName = GetStateName(GetMind().State());
	EmitDebugMsg(TEXT("State: %s, "), FString::Printf(TEXT("State: %s, "),
		StateName != nullptr ? StateName : TEXT("")));

	// 5. `"Move: %s, "` with `GetNavTypeName(GetNavType())` (slot 407, the table above).
	//    SEAM: `GetNavType()` (`0x1027d990`) has no port counterpart — the motor carries no
	//    navigation type — so the name asked for is the one -1 answers, `"None"`.
	EmitDebugMsg(TEXT("Move: %s, "), FString::Printf(TEXT("Move: %s, "),
		GetNavTypeName(RetailNavType())));

	// 6. `if (!m_pSchedule) return;` — the schedule half is skipped entirely, with no "no schedule"
	//    line. `ReportAIState` prints one; this body does not.
	if (!Schedule.IsRunning())
	{
		return;
	}

	// 7. `"Schd: %s, "`, the program's name or `"Unknown"`.
	const TCHAR* ScheduleName = ElysiumScheduleName(Schedule.Current);
	EmitDebugMsg(TEXT("Schd: %s, "), FString::Printf(TEXT("Schd: %s, "),
		ScheduleName != nullptr ? ScheduleName : TEXT("Unknown")));

	// 8. `GetCurTask()`: `"Task: None"` for no task, else `"Task: %s (#%d), "` with `TaskName(id)`
	//    and `m_ScheduleState`. The listing pre-pushes `m_ScheduleState` before the `TaskName` call
	//    so that the one `%d` is fed by it; the decompiler dropped that push.
	const FElysiumSchedule* Program = ElysiumScheduleFor(Schedule.Current);
	if (Program == nullptr || !Program->Tasks.IsValidIndex(Schedule.TaskIndex))
	{
		EmitDebugMsg(TEXT("Task: None"), TEXT("Task: None"));
	}
	else
	{
		EmitDebugMsg(TEXT("Task: %s (#%d), "), FString::Printf(TEXT("Task: %s (#%d), "),
			ElysiumTaskName(Program->Tasks[Schedule.TaskIndex].Task), Schedule.TaskIndex));
	}

	// 9. Then EVERY task of the program, one line each, re-reading `m_pSchedule->numTasks` at the
	//    top of every iteration:
	//
	//      prefix = (i == 0) ? "Task:" : "       "
	//      if (i == m_ScheduleState) { lead = "->";  trail = "<-"; }
	//      else                      { lead = "   "; trail = "";   }
	//      Q_snprintf(buf, 512, "%s%s%s%s", prefix, lead, TaskName(tasks[i].iTask), trail)
	//
	//    The decompiled C swaps `lead` and `trail`; the listing's register assignment is what is
	//    ported. `tasks[i]` strides EIGHT bytes — `Task_t` is `{ int iTask; float flTaskData; }`.
	//
	//    Retail names each step through slot 449 `TaskName(number)`, which this runtime's seam
	//    cannot answer (see `TaskIdSpaceRows`). The step is named through `ElysiumTaskName` instead
	//    — the port's typed identity for the same step — and that substitution is stated here and in
	//    the story report. Nothing else about the line changes.
	if (Program != nullptr)
	{
		for (int32 Index = 0; Index < Program->Tasks.Num(); ++Index)
		{
			const TCHAR* Prefix = Index == 0 ? TEXT("Task:") : TEXT("       ");
			const bool bCurrent = Index == Schedule.TaskIndex;
			const TCHAR* Lead = bCurrent ? TEXT("->") : TEXT("   ");
			const TCHAR* Trail = bCurrent ? TEXT("<-") : TEXT("");
			EmitDebugMsg(TEXT("%s%s%s%s"), FString::Printf(TEXT("%s%s%s%s"), Prefix, Lead,
				ElysiumTaskName(Program->Tasks[Index].Task), Trail));
		}
	}
}

void FElysiumNpc::BossDrawDebugStatOverlays()
{
	// `0x10366290`, thirty-six bytes and all of `CNPC_VBaseBoss#76`:
	//
	//     Msg("Dist to player: %.3f", *(float *)(this + 0x6264));
	//     JMP CAI_BaseNPC::DrawDebugStatOverlays;     // 0x102775e0, a TAIL call
	//
	// The tail call is to the BASE body and not to `CAI_BaseNPCTroika`'s, so a boss never gets the
	// expression/gesture dump even when it has a dialogue. That is the recovered dispatch and it is
	// reproduced.
	//
	// `+0x6264` is `FElysiumNpcMemory::ClosestPlayerDistanceCm` in the shape map; retail's word is
	// SOURCE units, so the print divides.
	EmitDebugMsg(TEXT("Dist to player: %.3f"), FString::Printf(TEXT("Dist to player: %.3f"),
		Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U));
	BaseDrawDebugStatOverlays();
}

void FElysiumNpc::DrawDebugStatOverlays()
{
	// slot 76. Three retail bodies fill it across the census and this leaf dispatches between them
	// by the address the census says fills the slot for this NPC's retail class:
	//
	//   `0x10366290`  `CNPC_VBaseBoss` and its four subclasses — the distance line, then the BASE.
	//   `0x1029c010`  `CAI_BaseNPCTroika` and 55 more — the expression dump, but only when
	//                 `m_iDialog` is set; otherwise it tail-calls the base.
	//   `0x102775e0`  everything else — the base body itself.
	const TCHAR* SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 76);
	if (SlotBody != nullptr && FCString::Strcmp(SlotBody, TEXT("0x10366290")) == 0)
	{
		BossDrawDebugStatOverlays();
		return;
	}

	// `0x1029c010`'s own first arm: `if (m_iDialog == 0) { CAI_BaseNPC::DrawDebugStatOverlays();
	// return; }`. `m_iDialog` (+0x0128) is the dialogue file name, which `DialogName()` answers.
	if (DialogName().IsEmpty())
	{
		BaseDrawDebugStatOverlays();
		return;
	}
	TroikaDrawDebugStatOverlays();
}

void FElysiumNpc::TroikaDrawDebugStatOverlays()
{
	// `0x1029c010` past its `m_iDialog == 0` arm, recovered from the LISTING: the decompiled C lost
	// every `Msg` argument list to the frame reconstruction and misordered two of them.
	//
	// 1. The expression name, resolved BEFORE anything is printed:
	//      `IsValidExpressionIndex(m_idxDefExpression)` ? `GetExpressionName(m_idxDefExpression)`
	//                                                   : `"None"` (`0x10547418`)
	//    This runtime stores the expression by NAME (`DefExpression`, +0x10b4, family Conditions —
	//    "there is no index"), so a non-empty name IS the valid arm.
	const FString ExpressionName = DefExpression.IsEmpty() ? FString(TEXT("None")) : DefExpression;

	// 2. `Msg("Expression / Gesture information for %s:", GetDebugName())`. Retail's `GetDebugName`
	//    is `m_iName` when set and the classname otherwise.
	const FString Named = TargetName.IsEmpty()
		? (Def != nullptr ? Def->Classname : FString()) : TargetName;
	EmitDebugMsg(TEXT("Expression / Gesture information for %s:"),
		FString::Printf(TEXT("Expression / Gesture information for %s:"), *Named));

	// 3. `Msg("Seq (%.2f): %s / %s ", m_flCycle, seqdesc->pszLabel, seqdesc->pszActivityName)`.
	//    Retail calls `GetSeqDesc(m_nSequence)` TWICE for the two strings and does NOT guard the
	//    result, so a missing descriptor is a null dereference in retail. The port asks the seam
	//    once and prints empty strings where retail would crash — a stated divergence, forced by the
	//    absence of the studio header rather than chosen.
	FString SeqLabel;
	FString SeqActivity;
	SequenceDescriptor(SequenceNumber, SeqLabel, SeqActivity);
	EmitDebugMsg(TEXT("Seq (%.2f): %s / %s "), FString::Printf(TEXT("Seq (%.2f): %s / %s "),
		SequenceCycle, *SeqLabel, *SeqActivity));

	// 4. `Msg("Disposition:  %s,  Expression: %s (%.2f)", DispositionName(m_nCurrDisposition),
	//        expressionName, m_flDefExpressionIntensity)`. The float is `+0x10b8`, family
	//    TroikaHelpers' `ExpressionBlendWeight`; the disposition is `+0x64d4`, the chain's own
	//    `Disposition` name.
	EmitDebugMsg(TEXT("Disposition:  %s,  Expression: %s (%.2f)"),
		FString::Printf(TEXT("Disposition:  %s,  Expression: %s (%.2f)"), *Disposition,
			*ExpressionName, ExpressionBlendWeight));

	// 5. The scene line, three arms in this order:
	//      `m_hDialogScene` (+0x6554) does not resolve      -> `"No Scene Entity"`
	//      it resolves but its `+0x4bc` scene is null       -> `"No Scene"`
	//      otherwise                                        -> `"Scene time: %.2f"`
	//    `+0x4bc` is the `CChoreoScene*` on the scene entity and `0x1007dfa0` is its `GetTime()`.
	const FElysiumEntity* SceneEntity =
		World != nullptr ? World->Resolve(Dialogue.DialogScene) : nullptr;
	if (SceneEntity == nullptr)
	{
		EmitDebugMsg(TEXT("No Scene Entity"), TEXT("No Scene Entity"));
	}
	else
	{
		// SEAM for the scene entity's `+0x4bc CChoreoScene*` and `CChoreoScene::GetTime()`
		// (`0x1007dfa0`). The kernel has no reader for the scene object, so this takes the
		// `"No Scene"` arm — retail's answer for a scene entity that is not playing one.
		EmitDebugMsg(TEXT("No Scene"), TEXT("No Scene"));
	}

	// 6. Every row of `m_Expressions` (+0x10bc, count at +0x10c8, stride 0x20):
	//      level = (row[0x1c] && row[0x18]) ? Evaluate(row[0x1c], SceneTime(row[0x18]))
	//                                       : CalcExpressionIntensity(curtime, row)
	//      data  = CExpressionTable::GetExpressionData(&DAT_10709200, row[0])
	//      if (data) Msg((data[0x18c] & 1) ? "Expression:(O) %s level: %0.2f"
	//                                      : "Expression:(N) %s level: %0.2f", data, level)
	//    The `%s` is the DATA STRUCT's address, so the expression's name is its first field.
	//    SEAM: family Anim already records that the scripted-expression vector has no port member;
	//    the list is empty here, so the loop runs zero times. Nothing is invented to fill it.

	// 7. The four anim layers (`m_AnimOverlay` +0x748, stride 0x30), each printed only when its
	//    weight is strictly greater than `_DAT_104454c4` = 0.0:
	//      `Msg("Anim Layer %d: %s (weight: %f)", i, seqdesc(layer->m_nSequence)->pszLabel, weight)`
	//    SEAM: this runtime stands no overlay-layer array on the kernel surface, so no layer has a
	//    weight and the loop prints nothing. The threshold is recorded because it is the recovered
	//    gate: a layer at EXACTLY zero weight is silent, one at 0.0001 is not.

	// 8. `Msg("Eye targets -  current: %d  default: %d  step: %d", m_RelativeEyeTarget,
	//        DefaultEyeTarget(m_nCurrDisposition), m_nFidgetStep)`. The listing's push order is what
	//    settles which word is which: `+0x5b94` first, the disposition lookup second, `+0x6578`
	//    third.
	EmitDebugMsg(TEXT("Eye targets -  current: %d  default: %d  step: %d"),
		FString::Printf(TEXT("Eye targets -  current: %d  default: %d  step: %d"),
			RelativeEyeTarget, DispositionDefaultEyeTarget(), FidgetStep));

	// 9. `if (m_hEyeLookTarget (+0xe64) resolves)`:
	//      `Msg("Looking at entity %d (%s)", IndexOfEdict(target->edict), target->GetDebugName())`
	//    The index goes through the engine interface `DAT_1070b22c`+0x8c; the port's entity index is
	//    the handle's own slot, which is the same identity.
	const FElysiumEntity* LookTarget =
		World != nullptr ? World->Resolve(EyeLookTargetHandle) : nullptr;
	if (LookTarget != nullptr)
	{
		const FString LookNamed = LookTarget->TargetName.IsEmpty()
			? (LookTarget->Def != nullptr ? LookTarget->Def->Classname : FString())
			: LookTarget->TargetName;
		EmitDebugMsg(TEXT("Looking at entity %d (%s)"),
			FString::Printf(TEXT("Looking at entity %d (%s)"), LookTarget->Handle.Index,
				*LookNamed));
	}

	// 10. `if (m_szDialogQue[0]) Msg("Qued Dialog: %s", m_szDialogQue)` — the FIRST BYTE, so an
	//     empty queued line is silent.
	if (!Dialogue.DialogQue.IsEmpty())
	{
		EmitDebugMsg(TEXT("Qued Dialog: %s"),
			FString::Printf(TEXT("Qued Dialog: %s"), *Dialogue.DialogQue));
	}

	// 11. `Msg("Talk Time Remaining: %.2f", MAX(m_flTalkTime - curtime, 0.0))`. The clamp is an
	//     `FCOMP` against `_DAT_104454c4` = 0.0 with the constant in ST0, so the constant wins only
	//     when it is strictly greater — a remainder of exactly 0.0 prints as 0.0 either way.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const double Remaining = FMath::Max(TalkingUntil - Now, 0.0);
	EmitDebugMsg(TEXT("Talk Time Remaining: %.2f"),
		FString::Printf(TEXT("Talk Time Remaining: %.2f"), Remaining));
}

// -------------------------------------------------------------------------------------------------
// The seams the text bodies read through.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::SequenceDescriptor(int32 Sequence, FString& OutLabel,
	FString& OutActivityName) const
{
	// SEAM for `CBaseAnimating::GetSeqDesc(m_nSequence)` (`0x1000b4f6`) and the two string offsets
	// off the returned `mstudioseqdesc_t` (`+0x00` the label, `+0x04` the activity name, both
	// relative to the descriptor itself). No studio header stands here — family Anim records the
	// same refusal — so this answers FALSE and both strings stay empty, which is retail's
	// `"(INVALID)"` arm in `0x102775e0`.
	(void)Sequence;
	OutLabel.Reset();
	OutActivityName.Reset();
	return false;
}

FString FElysiumNpc::ActivityNameForNumber(int32 Activity) const
{
	// SEAM for `SelectWeightedSequence(activity, -1)` + `GetSequenceActivityName(sequence)`, the
	// two-step both stat bodies and `ReportAIState` use to NAME an activity. It ends in the studio
	// header `SequenceDescriptor` refuses, so it answers the empty string.
	(void)Activity;
	return FString();
}

int32 FElysiumNpc::RetailNavType() const
{
	// SEAM for `CAI_BaseNPC::GetNavType()` (`0x1027d990`). `m_pNavigator` is an
	// `ELYSIUM_NPC_WORD_CHAIN` row onto the motor and no navigation-type word exists; -1 is what
	// slot 407 names `"None"`.
	return INDEX_NONE;
}

int32 FElysiumNpc::DispositionDefaultEyeTarget() const
{
	// SEAM for `0x100eccf0(&DAT_10924980, m_nCurrDisposition)` — the disposition table's default
	// eye-target index. `FElysiumDisposition` carries `EyeTurnRate` and the keypad cells, not an
	// index into a global target list, so this answers -1.
	return INDEX_NONE;
}

float FElysiumNpc::MotorYawSpeed() const
{
	// SEAM for `CAI_Motor::m_YawSpeed` (`m_pMotor` `+0x38`), the STORED yaw speed `SetYawSpeed`
	// writes. Slot 516 `MaxYawSpeed` computes a different word — the CEILING — and answering with
	// it would be an invention, so this answers 0.
	return 0.f;
}

FElysiumEntity* FElysiumNpc::ActiveWeaponEntity() const
{
	// SEAM for `CBaseCombatCharacter::GetActiveWeapon()`. Family BaseHelpers seams the same object's
	// maximum range (`ActiveWeaponMaxRangeUnits`); there is no kernel accessor for the entity.
	return nullptr;
}
