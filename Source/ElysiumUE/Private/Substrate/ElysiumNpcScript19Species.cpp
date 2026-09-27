// Story 0019/8 (29e under the strict verdict), family **Script19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Script19's `rule` rows): 0x1037c1c0 CNPC_VGhoulCroucher::ScriptHide, 0x1038b120
// CNPC_VManBat::OverrideMove and its body 0x1038b1a0. The director rows the shape commit listed here
// (0x101a7140, 0x101a7880, 0x101a9080, 0x101a82d0, 0x101a9510, 0x101a9790) are the directors' own
// class files (`ElysiumScriptedSequence.cpp`, `ElysiumAiScriptedSequence.cpp`,
// `ElysiumAiScriptedSchedule.cpp`). Walked prose: `docs/vtmb/npc-ai/story8/Script19.md`.

#include "Substrate/ElysiumNpcManBat.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcKernelTunables.h"

namespace
{
	// `s_E__Vampire_main_dlls_hl2_dll_NPC_VManBat_cpp` (`0x10642bcc`), the stun bail's `+0x1b30`.
	const TCHAR* const GScript19ManBatFile = TEXT("E:\\Vampire\\main\\dlls\\hl2_dll\\NPC_VManBat.cpp");
	constexpr int32 GScript19ManBatStunLine = 0x1ad;   // +0x1b34

	// `CAI_Navigator::GetNavType` (`0x1027d990`) 2, the flying navigator.
	constexpr int32 GScript19ManBatNavFly = 2;
	// `0x1038b120`'s encoded compare constant, run through `0x1042fbf0` like the word it meets.
	constexpr uint32 GScript19ManBatModeWordConstant = 0xfa0b0699u;

	// The four activities `0x1038b1a0` skips the facing block for (`0x1038b24e..0x1038b26d`), and the
	// fifth `0x1038e720` also refuses (`0x1038e72d`, `0x28`).
	constexpr int32 GScript19ManBatAct30 = 0x30;
	constexpr int32 GScript19ManBatActB0 = 0xb0;
	constexpr int32 GScript19ManBatAct4B = 0x4b;
	constexpr int32 GScript19ManBatAct1171 = 0x1171;
	constexpr int32 GScript19ManBatAct28 = 0x28;
	// `0x1038e805 CMP EDI,0x24` -- the glide activity that flaps again instead of gliding.
	constexpr int32 GScript19ManBatActGlide = 0x24;

	// `_DAT_10449280` (1.0, f64) is `ElysiumNpcTunables::OneDouble`; `_DAT_104492a8` (30.0, f32) is
	// `ElysiumNpcTunables::Thirty`; `_DAT_1044fab0` (0.0, f64) is `ZeroDouble`. The selector's other
	// four cells are not in the tunables table and were read out of the pinned image (f64):
	constexpr double GScript19ManBatFullTurn = 360.0;       // _DAT_10450570
	constexpr double GScript19ManBatTurnLow = 30.0;         // _DAT_1044dcf0
	constexpr double GScript19ManBatTurnHigh = 330.0;       // _DAT_104bc6a0
	constexpr double GScript19ManBatTurnSplit = 180.0;      // _DAT_10452918
	// `0x101d2c70` / `0x101d2ce0`: `_DAT_10446758` (57.29578, f32) radians-to-degrees, `_DAT_10450568`
	// (360.0, f32) the yaw wrap, `_DAT_10462948` (-180.0, f32) the straight-up pitch.
	constexpr float GScript19RadToDeg = 57.29578f;
	constexpr float GScript19YawWrap = 360.0f;
	constexpr float GScript19PitchStraightUp = -180.0f;
}

// --- `CNPC_VManBat` ------------------------------------------------------------------------------

int32& FElysiumNpcManBat::ManBatStunConVar()
{
	static int32 Value = 0;   // "0", `ConVar::Create`'s `m_nValue = atoi(default)`
	return Value;
}

float FElysiumNpcManBat::ManBatVecToYaw(const FVector& PortVector)
{
	// `0x101d2c70`: `(x, y) == (0, 0)` answers 0; else `atan2(y, x) * 57.29578`, `+ 360` when negative.
	const float X = static_cast<float>(PortVector.X);
	const float Y = static_cast<float>(-PortVector.Y);   // Source Y
	if (Y == ElysiumNpcTunables::Zero && X == ElysiumNpcTunables::Zero)
	{
		return ElysiumNpcTunables::Zero;
	}
	float Yaw = FMath::Atan2(Y, X) * GScript19RadToDeg;
	if (Yaw < ElysiumNpcTunables::Zero)
	{
		Yaw += GScript19YawWrap;
	}
	return Yaw;
}

float FElysiumNpcManBat::ManBatVecToPitch(const FVector& PortVector)
{
	// `0x101d2ce0`: `(x, y) == (0, 0)` answers 180 for a DOWNWARD z and -180 otherwise; else
	// `atan2(-z, sqrt(x*x + y*y)) * 57.29578`.
	const float X = static_cast<float>(PortVector.X);
	const float Y = static_cast<float>(-PortVector.Y);
	const float Z = static_cast<float>(PortVector.Z);
	if (Y == ElysiumNpcTunables::Zero && X == ElysiumNpcTunables::Zero)
	{
		return Z < ElysiumNpcTunables::Zero ? ElysiumNpcTunables::OneEighty : GScript19PitchStraightUp;
	}
	return FMath::Atan2(-Z, FMath::Sqrt(Y * Y + X * X)) * GScript19RadToDeg;
}

// Slot 525: `0x1038b120` (`CNPC_VManBat::OverrideMove`), 96 bytes.
bool FElysiumNpcManBat::OverrideMove(float Arg0)
{
	if (NavGetType() == GScript19ManBatNavFly)                // 0x1038b123 0x1027d990 (family Motor's word) / 0x1038b128 / 0x1038b12b
	{
		ManBatOverrideMoveFly(Arg0);                          // 0x1038b134 0x1038b1a0(interval)
		return true;                                          // 0x1038b139 MOV AL,1 / 0x1038b13c
	}
	// `+0x6670` through the caller-side ladder and `0x1042fbf0` (`ManBatHintMode`), against the
	// constant through `0x1042fbf0` alone: `SETZ AL`.
	return ManBatHintMode(ManBatHintModeWord)                 // 0x1038b13f..0x1038b163
		== HintObfuscationFold(GScript19ManBatModeWordConstant); // 0x1038b168 / 0x1038b16f 0x1042fbf0 .. 0x1038b179
}                                                             // 0x1038b17d

// `0x1038b1a0`, 366 bytes.
void FElysiumNpcManBat::ManBatOverrideMoveFly(float Interval)
{
	// `cvar_manbat_stun`: `(*0x1093b85c)->IsCommand()` (`0x1038b1af` vtable +4; `0x1038b1b4 JNZ`
	// skips the stun on a command) false (always, for a ConVar) AND `m_nValue`.
	if (ManBatStunConVar() != 0)                              // 0x1038b1a6..0x1038b1c1
	{
		RecordScheduleEvent(FString::Printf(TEXT("SetSchedule trace %s:%d"), GScript19ManBatFile,
			GScript19ManBatStunLine));                        // 0x1038b1cc / 0x1038b1d6 +0x1b30/+0x1b34
		SetSchedule(ManBatStunScheduleRetailId, false);       // 0x1038b1e0 0x102ae750(0x15b, 0)
		ManBatStunConVar() = 0;                               // 0x1038b1ec ConVar::SetValue(0) -- fires once
		return;                                               // 0x1038b1f6
	}
	if (static_cast<double>(Interval) > ElysiumNpcTunables::OneDouble) // 0x1038b1f9..0x1038b20a (NaN kept)
	{
		Interval = static_cast<float>(ElysiumNpcTunables::OneDouble);      // 0x1038b20c
	}
	FVector VelocityUnits = FVector::ZeroVector;
	FUN_1038b370(Interval, VelocityUnits);                    // 0x1038b220 0x1038b370(this, &out, interval)
	Velocity = VelocityUnits * ElysiumMove::U;                // 0x1038b240 SetAbsVelocity
	if (ActivityNumber == GScript19ManBatAct30 || ActivityNumber == GScript19ManBatActB0   // 0x1038b245..0x1038b259
		|| ActivityNumber == GScript19ManBatAct4B || ActivityNumber == GScript19ManBatAct1171) // 0x1038b262 / 0x1038b26d
	{
		return;                                               // 0x1038b306
	}
	// `0x102e1c10(m_pMotor, VecToYaw(vel), -1.0)`: the `+0x28` flip, the `+0x1c == 180` direct write
	// (SEAM: no motor `+0x1c`; `MotorIdealYaw` takes the direct write, as `NPCInit` does), then the
	// rate `-1.0` equals `_DAT_104492dc` and takes `0x102e1cf0` (yaw speed := `MaxYawSpeed`), and
	// `0x102e1e20(-1)`.
	float Yaw = ManBatVecToYaw(VelocityUnits);                // 0x1038b283 0x101d2c70
	if (BaseScheduleHost.bMotorAnimationMovement)
	{
		Yaw = Yaw < MotorYawHalfTurn ? Yaw + MotorYawHalfTurn : Yaw - MotorYawHalfTurn;
	}
	MotorIdealYaw = Yaw;                                      // 0x1038b28d 0x102e1c10
	// Rate -1.0 == `_DAT_104492dc` (`0x1038b27d`) takes `0x102e1cf0` on the motor (`+0x38` := the
	// outer's `MaxYawSpeed`): the SAME call on the same `m_pMotor` (`+0x5d44`) that
	// `SetActivityAndSequence` ends in (`0x10272569` / `0x10272575`), counted by family Anim10's seam.
	++NavigatorActivityNotices;
	ReleaseMotorHintYaw();                                    // 0x102e1e20(motor, -1)
	FVector NewAngles = Angles;                               // 0x1038b296 slot 221 GetAngles
	NewAngles.X = ManBatVecToPitch(VelocityUnits);            // 0x1038b2b5 0x101d2ce0 -> pitch
	SetRuntimeAngles(NewAngles);                              // 0x1038b2ca slot 64 SetAngles
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;   // 0x1038b2d0 gpGlobals->curtime
	if (ManBatFlapTimer <= Now)                               // 0x1038b2d6..0x1038b2e4 (JP skips on > or unordered)
	{
		ManBatWingTurnSelect(VelocityUnits);                  // 0x1038b301 0x1038e720(vel)
	}
}                                                             // 0x1038b30b

// `0x1038e720`.
void FElysiumNpcManBat::ManBatWingTurnSelect(const FVector& VelocityUnits)
{
	const int32 Activity = ActivityNumber;                    // 0x1038e727 m_Activity +0xfec
	if (Activity == GScript19ManBatAct28 || Activity == GScript19ManBatAct30          // 0x1038e72d / 0x1038e736
		|| Activity == GScript19ManBatActB0 || Activity == GScript19ManBatAct4B     // 0x1038e73f / 0x1038e74b
		|| Activity == GScript19ManBatAct1171)                                        // 0x1038e754
	{
		return;                                               // 0x1038e820
	}
	auto Flap = [this](const TCHAR* FlapBody)
	{
		if (const FFlapActivity* Row = FlapActivityOf(FlapBody))
		{
			SetFlapActivity(Row->Activity, Row->Seconds);
		}
	};
	if (VelocityUnits.Z >= ElysiumNpcTunables::Thirty)        // 0x1038e760..0x1038e771 vel.z vs _DAT_104492a8
	{
		Flap(TEXT("0x1038e640"));                             // 0x1038e773
		return;
	}
	double Turn = static_cast<double>(ManBatVecToYaw(VelocityUnits)) - Angles.Y; // 0x1038e780..0x1038e7a6
	if (Turn < ElysiumNpcTunables::ZeroDouble)                // 0x1038e7ad / 0x1038e7b8
	{
		Turn += GScript19ManBatFullTurn;                      // 0x1038e7ba
	}
	// Written as retail's two refusals so an unordered turn proceeds as the x87 flags do:
	// `0x1038e7cb JNP` leaves only on C0 alone (below 30), `0x1038e7da JZ` only on C0 = C3 = 0 (above
	// 330); `0x1038e7e9 JP` takes `0x1038e6e0` on even parity, which an unordered turn also has.
	if (!(Turn < GScript19ManBatTurnLow) && !(Turn > GScript19ManBatTurnHigh)) // 0x1038e7c0..0x1038e7da
	{
		if (Turn < GScript19ManBatTurnSplit)                  // 0x1038e7dc / 0x1038e7e9
		{
			Flap(TEXT("0x1038e6a0"));                         // 0x1038e7eb
			return;
		}
		Flap(TEXT("0x1038e6e0"));                             // 0x1038e7f8
		return;
	}
	if (Activity == GScript19ManBatActGlide)                  // 0x1038e805 / 0x1038e80c
	{
		Flap(TEXT("0x1038e640"));                             // 0x1038e80e
		return;
	}
	Flap(TEXT("0x1038e670"));                                 // 0x1038e81b
}

// --- `CNPC_VGhoulCroucher` -----------------------------------------------------------------------

// Slot 77: `0x1037c1c0`, 232 bytes. The scope-trace push/pop keyed on `m_iName`
// (`0x1037c1c5..0x1037c227`, its null-name default `0x1037c1cf JNZ` / `0x1037c1d1`, and `0x1037c292` /
// `0x1037c2a5`) is debugger bookkeeping and stays absent.
void FElysiumNpcGhoulCroucher::GhoulCroucherScriptHide()
{
	// `0x1037c229` calls `CAI_BaseNPCTroika::ScriptHide` (`0x102c1ce0`) DIRECT. That Troika body (the
	// cine-cancel gate, forced schedule `0x6b`, the active weapon's slot 77) is family Damaged19's row
	// and unported; the base half it ends in (`CBaseEntity::ScriptHide 0x100a8710`) stands for it,
	// called qualified so a species-level `ScriptHide` never shadows it.
	FElysiumEntity::ScriptHide();                             // 0x1037c229 -> 0x102c1ce0 (base half only)
	if (World == nullptr || !BurningParticle.IsSet())         // 0x1037c22e / 0x1037c237 m_hBurningParticle == -1
	{
		return;                                               // 0x1037c29f..0x1037c2a7
	}
	// Stale serial / null slot skip (`0x1037c259` / `0x1037c25e`). The re-validation of the same
	// handle (`0x1037c260..0x1037c280`, its -1 test `0x1037c269 JZ 0x1037c295`) cannot fail with
	// nothing between the reads, so its
	// null-receiver arm (`0x1037c295 XOR ECX,ECX / 0x1037c299`) is dead.
	if (FElysiumEntity* Particle = World->Resolve(BurningParticle))
	{
		Particle->ScriptHide();                               // 0x1037c286 slot 77 on the particle
	}
}                                                             // 0x1037c294
