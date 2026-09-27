// Story 0019/8 (29e under the strict verdict), family **Misc19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2); ported by lane L11.
// Every body keeps its class's declaration; three (`CNPC_VHengeyokai`, `CNPC_VMingXiao`,
// `CNPC_VTzimisceHeadClaw` slot 259) REPLACE the footstep-table bodies their class files carried
// (`ElysiumNpcHengeyokai.cpp`, `ElysiumNpcMingXiao.cpp`, `ElysiumNpcTzimisceHeadClaw.cpp`), which
// the integrator deletes. Bach's camper pass (`0x10365a90`) lives here too.
//
// Owns (Misc19's `rule` rows): 0x1035dc30 CNPC_VAndreiBlood::Activate, 0x103cfc50
// CNPC_VWerewolf::CheckAllMoveHints, 0x10372c50 CNPC_VCop::vfunc596, 0x10372dd0
// CNPC_VCop::vfunc598, 0x103a3850 CNPC_VPedestrian::vfunc27, 0x101a98c0 CCineAISchedule::vfunc586,
// 0x101aade0 CPayphone::EnterGrappleState, 0x1037b500 CNPC_VGhoulCroucher::EnterGrappleState,
// 0x10374280 CNPC_VDog::HandleAnimEvent, 0x103786c0 CNPC_VGargoyle::HandleAnimEvent, 0x1037fb60
// CNPC_VHengeyokai::HandleAnimEvent, 0x1038e000 CNPC_VManBat::HandleAnimEvent, 0x10392a70
// CNPC_VMingXiao::HandleAnimEvent, 0x103a7000 CNPC_VSabbatLeader::HandleAnimEvent, 0x103ba410
// CNPC_VTzimisce::HandleAnimEvent, 0x103c1540 CNPC_VTzimisceHeadClaw::HandleAnimEvent, 0x103d88e0
// CNPC_VWerewolf::HandleAnimEvent.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcPayphone.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcSoundsShared.h"
#include "ElysiumAnimEvent.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumSchedule.h"

#include <limits>

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumScheduleNumbers.h"

namespace
{
	// `CNPC_VPedestrian#27` (`0x103a3884`): the ideal state written directly, retail `NPC_STATE` 8.
	constexpr int32 Misc19PedestrianIdealStateRetail = 8;
	// `0x103a3887 PUSH 0x15a`: the raw forced-schedule id `0x102ae7f0` stores in `m_iForcedSchedule`.
	constexpr int32 Misc19PedestrianForcedScheduleRetailId = 0x15a;
	// `0x103a3870`/`0x103a387a`: the ideal-state trace line (`NPC_VPedestrian.cpp`).
	constexpr int32 Misc19PedestrianIdealTraceLine = 0x405;
	// `CNPC_VAndreiBlood::Activate` (`0x1035dc93`/`0x1035dc99`): both state words := 2.
	constexpr int32 Misc19AndreiBloodStateRetail = 2;
	// `0x1035dc9f`/`0x1035dca9`: the schedule trace line (`NPC_VAndreiBlood.cpp`).
	constexpr int32 Misc19AndreiBloodScheduleTraceLine = 0x1cc;
	// `CNPC_VGhoulCroucher#379` (`0x1037b51c`): `BurnPlayer(player, 10.0f)` (`0x41200000`).
	constexpr float Misc19GhoulGrappleBurnDamage = 10.0f;
	// `CNPC_VCop#598` (`0x10372ea4`): the literal handed `InputSetRelationship` (`0x10273790`).
	const TCHAR* const Misc19CopForgiveRelationship = TEXT("Player D_NU 10");
	// `Disposition_t` `D_NU` (4), the literal `CNPC_VCop#598` hands `AddEntityRelationship`.
	constexpr EElysiumRelationship Misc19CopForgetDisposition = EElysiumRelationship::Neutral;

	// --- The species `HandleAnimEvent` tables, verbatim off the retail image ---------------------
	// `CNPC_VGargoyle` slot 617 `0x10379f90` (`0x10639480`) and the roar at `0x10378751`.
	const TCHAR* const Misc19GargoyleStomps[] = {
		TEXT("character/monster/gargoyle/stomp_1.wav"), TEXT("character/monster/gargoyle/stomp_2.wav"),
		TEXT("character/monster/gargoyle/stomp_3.wav"), TEXT("character/monster/gargoyle/stomp_4.wav"),
	};
	const TCHAR* const Misc19GargoyleRoar = TEXT("character/monster/gargoyle/roar2.wav");
	// `CNPC_VHengeyokai` slot 617 `0x103817f0` (`0x1063bd94`).
	const TCHAR* const Misc19HengeyokaiStomps[] = {
		TEXT("character/monster/hengeyokai/stomp_1.wav"), TEXT("character/monster/hengeyokai/stomp_2.wav"),
		TEXT("character/monster/hengeyokai/stomp_3.wav"), TEXT("character/monster/hengeyokai/stomp_4.wav"),
	};
	// `CNPC_VManBat` (`0x10640d10`, `0x10640d1c`, and the fixed screech).
	const TCHAR* const Misc19ManBatWingflaps[] = {
		TEXT("character/male/sheriff_manbat/wingflap_1.wav"), TEXT("character/male/sheriff_manbat/wingflap_2.wav"),
		TEXT("character/male/sheriff_manbat/wingflap_3.wav"),
	};
	const TCHAR* const Misc19ManBatExerts[] = {
		TEXT("character/male/sheriff_manbat/exert_heavy_1.wav"), TEXT("character/male/sheriff_manbat/exert_heavy_2.wav"),
		TEXT("character/male/sheriff_manbat/exert_heavy_3.wav"),
	};
	const TCHAR* const Misc19ManBatScreech = TEXT("character/male/sheriff_manbat/screech.wav");
	// `CNPC_VTzimisce` slot 620 `0x103b9810` (`0x10653114`) and slot 619 `0x103b96a0` (`0x106530fc`).
	const TCHAR* const Misc19TzimisceSwishes[] = {
		TEXT("character/monster/spiderchick/spi_attack_swish_1.wav"),
		TEXT("character/monster/spiderchick/spi_attack_swish_2.wav"),
		TEXT("character/monster/spiderchick/spi_attack_swish_3.wav"),
	};
	const TCHAR* const Misc19TzimisceFootsteps[] = {
		TEXT("character/monster/spiderchick/spi_footstep_indiv_1.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_2.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_3.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_4.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_5.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_6.wav"),
	};
	// `CNPC_VTzimisceHeadClaw` slot 619 `0x103c2640`: argument 1 (`0x1065ca70`), argument 0
	// (`0x1065ca68`).
	const TCHAR* const Misc19HeadClawStepsArgOne[] = {
		TEXT("character/monster/TC_FatGuy/Foot_Step3.wav"), TEXT("character/monster/TC_FatGuy/Foot_Step4.wav"),
	};
	const TCHAR* const Misc19HeadClawStepsArgZero[] = {
		TEXT("character/monster/TC_FatGuy/Foot_Step1.wav"), TEXT("character/monster/TC_FatGuy/Foot_Step2.wav"),
	};
	// `CNPC_VWerewolf`'s footstep `0x103d8c10` (`0x1065f4d0`).
	const TCHAR* const Misc19WerewolfFootsteps[] = {
		TEXT("character/monster/TC_FatGuy/Foot_Step1.wav"), TEXT("character/monster/TC_FatGuy/Foot_Step2.wav"),
		TEXT("character/monster/TC_FatGuy/Foot_Step3.wav"), TEXT("character/monster/TC_FatGuy/Foot_Step4.wav"),
	};
	// `werewolf_footstep_sounds` (`0x1093f8a0`) and `werewolf_footstep_shakes` (`0x1093d648`), both
	// shipped "0" (`0x103d8b10` registers them): retail's werewolf footfalls are silent and shake
	// nothing by default. Read as `!IsCommand() && m_nValue != 0`.
	int32 Misc19WerewolfFootstepSounds = 0;
	int32 Misc19WerewolfFootstepShakes = 0;
	// `CNPC_VHengeyokai`'s pickup element table `{"Bone01","Bone04",""}` (`0x1063bd88`) is indexed by
	// `+0x6668`; the port's `AttachPickupAnimlink` takes the index.

	// Retail's fixed-wav `EmitSound(filter, entindex, channel, wav, volume, 0.8, 0, 100, …)` with no
	// draw (the roar, the screech): the body-sound request `SoundsEmitSpeciesWav` makes, minus its
	// pool draw.
	void Misc19EmitFixedWav(FElysiumNpcBase& Npc, const TCHAR* Wav, float Volume, EElysiumSoundChannel Channel)
	{
		IElysiumAudio* const Audio = Npc.World != nullptr ? Npc.World->Audio() : nullptr;
		if (Audio == nullptr)
		{
			return;
		}
		FElysiumBodySound Sound;
		Sound.Rel = Wav;
		Sound.Volume = Volume;
		Sound.Channel = Channel;
		Audio->PlayBodySound(Npc.Handle, Sound);
	}

	// The Tzimisce voice slots 618/621..626: the three `tzimisce_voice_*` ConVars and
	// `SENTENCEG_PlayRndSz(edict, group, volume, attn, 0, pitch)` (`0x101aeb60`).
	void Misc19TzimisceVoice(FElysiumNpcBase& Npc, const TCHAR* Group)
	{
		NpcKernelSoundsShared::SoundsPlaySentenceGroup(Npc, Group,
			ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::TzimisceVoiceVolume),
			ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::TzimisceVoiceAttn), 0,
			ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::TzimisceVoicePitch));
	}
}

// Slot 27: `CNPC_VPedestrian::vfunc27` `0x103a3850`, 72 bytes, no branch.
void FElysiumNpcPedestrian::Slot27(FElysiumEntity* Arg0)
{
	// 1. The Troika body `0x1029f8f0`, DIRECT (the bump-condition hit chain).
	FElysiumNpc::Slot27(Arg0);                                            // 0x103a3859
	// 2. Slot 596 with the same entity, through the vtable.
	Slot596(Arg0);                                                        // 0x103a3863
	// 3. The ideal-state trace (`+0x1b3c`/`+0x1b40`, ABSENT words: recorded as the arm's line) and
	//    `m_IdealNPCState := 8` by DIRECT write — not `SetState`, so no state-change virtual fires.
	RecordScheduleEvent(FString::Printf(TEXT("SelectIdealState :%d -> %d"),
		Misc19PedestrianIdealTraceLine, Misc19PedestrianIdealStateRetail)); // 0x103a3870 / 0x103a387a
	WriteIdealStateRetail(Misc19PedestrianIdealStateRetail);              // 0x103a3884
	// 4. `0x102ae7f0(0x15a)`, whose whole body is `m_iForcedSchedule (+0x65c8) := param`.
	ScheduleHost.ForcedSchedule = Misc19PedestrianForcedScheduleRetailId; // 0x103a388e
}

// Slot 113: `CNPC_VAndreiBlood::Activate` `0x1035dc30`, 145 bytes, no branch. The scope-trace frame
// (`0x1035dc30`-`0x1035dc7f`, `0x1035dcb8`-`0x1035dcbe`) has no observable and stays absent.
void FElysiumNpcAndreiBlood::Activate()
{
	// 1. The Troika `Activate` `0x1028e310`, DIRECT and FIRST.
	FElysiumNpc::Activate();                                              // 0x1035dc83
	// 2. `m_NPCState` (`+0x5cc0`) and `m_IdealNPCState` (`+0x5cc4`) := 2 by DIRECT write — not
	//    `SetState` `0x1026e340`, so `m_flLastStateChangeTime` is not stamped and slot 463
	//    `OnStateChange` never fires for this transition.
	WriteNpcStateRetail(Misc19AndreiBloodStateRetail);                    // 0x1035dc93
	WriteIdealStateRetail(Misc19AndreiBloodStateRetail);                  // 0x1035dc99
	// 3. The selector trace (`+0x1b30`/`+0x1b34`, ABSENT words: recorded as the arm's line), then
	//    `0x102ae750(0x6b, force = false)`.
	RecordScheduleEvent(FString::Printf(TEXT("SelectSchedule :%d -> 0x%x"),
		Misc19AndreiBloodScheduleTraceLine, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION)); // 0x1035dc9f / 0x1035dca9
	SetSchedule(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, false);      // 0x1035dcb3
}

// Slot 259: `CNPC_VDog::HandleAnimEvent` `0x10374280`, 63 bytes.
bool FElysiumNpcDog::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	if (Event.Event != 0xbb9)                                             // 0x10374289 / 0x10374291
	{
		return FElysiumNpc::HandleAnimEvent(Event);                       // 0x10374294 -> 0x1029b290
	}
	// The bite, only with an active weapon; without one the event is SWALLOWED (`0x103742a4`).
	if (ActiveWeaponEntity() == nullptr)                                  // 0x1037429d
	{
		return true;
	}
	++DogBiteCalls;                                                       // 0x103742a8 -> 0x10374f40 (SEAM)
	EmitDebugMsg(TEXT("Gots a doggie bite!\n"), TEXT("Gots a doggie bite!")); // 0x103742ad / 0x103742b2 Msg
	return true;
}

// Slot 259: `CNPC_VGargoyle::HandleAnimEvent` `0x103786c0`.
bool FElysiumNpcGargoyle::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	const int32 Id = Event.Event;
	if (Id > 2)                                                           // 0x103786cf
	{
		if (Id < 0x802 || Id > 0x803)                                     // 0x10378808 / 0x10378813
		{
			return FElysiumNpc::HandleAnimEvent(Event);
		}
		// `UTIL_ScreenShake(GetOrigin(), 2.0, 0.2, 0.2, 1024.0, 0, false)`, then slot 617
		// `0x10379f90`: `RandomInt(0, 3)` over the stomps, channel 4, 1.0 / 0.8 / 100.
		RecordAnimEventShake(Origin / ElysiumMove::U, 2.0f, 0.2f, 0.2f, 1024.0f, false); // 0x1037881b .. 0x1037883c
		NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, Misc19GargoyleStomps,
			UE_ARRAY_COUNT(Misc19GargoyleStomps), 1.0f, NpcKernelSoundsShared::GSoundsChanBody); // 0x10378848
		return true;
	}
	if (Id == 2)                                                          // 0x103786d5
	{
		Misc19EmitFixedWav(*this, Misc19GargoyleRoar, 1.0f, EElysiumSoundChannel::Body); // 0x10378751 .. 0x1037878b
		return true;
	}
	if (Id == 1)                                                          // 0x103786d8: swallowed
	{
		return true;
	}
	return FElysiumNpc::HandleAnimEvent(Event);                           // 0x103786dd
}

// Slot 259: `CNPC_VHengeyokai::HandleAnimEvent` `0x1037fb60`. Every failed guard RETURNS — the event
// is swallowed, not passed to the base (packet correction).
bool FElysiumNpcHengeyokai::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	const int32 Id = Event.Event;
	if (Id > 0x803)                                                       // 0x1037fb6e
	{
		if (Id != 0xbbd)                                                  // 0x1037fc24
		{
			return FElysiumNpc::HandleAnimEvent(Event);                   // 0x1037fc29
		}
		if (HengeyokaiCarryFormBit())                                     // 0x10381c80, +0x14b8 bit 5
		{
			ReleasePickupAnimlink(GetEnemyEntity());                      // 0x10382400(GetEnemy())
		}
		return true;
	}
	if (Id >= 0x802)                                                      // 0x1037fb79
	{
		if (!bHengeyokaiInSharkForm)                                      // 0x1037fbdc / 0x1037fbe4, +0x6694
		{
			return true;
		}
		RecordAnimEventShake(Origin / ElysiumMove::U, 2.0f, 0.2f, 0.2f, 1024.0f, false);
		NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, Misc19HengeyokaiStomps,
			UE_ARRAY_COUNT(Misc19HengeyokaiStomps), 1.0f, NpcKernelSoundsShared::GSoundsChanBody); // slot 617 0x103817f0
		return true;
	}
	if (Id != 0x7f8)                                                      // 0x1037fb80
	{
		return FElysiumNpc::HandleAnimEvent(Event);
	}
	if (HengeyokaiCarryFormBit())                                         // 0x1037fb8f
	{
		return true;
	}
	// `+0x6664` resolved (-1 or a serial mismatch -> null), then `0x10382670(target,
	// {"Bone01","Bone04",""}[+0x6668])`.
	AttachPickupAnimlink(World != nullptr ? World->Resolve(HengeyokaiPickupTarget) : nullptr,
		HengeyokaiPickupTargetGrabBone);                                  // 0x1037fb95 .. 0x1037fc3b
	return true;
}

// Slot 259: `CNPC_VManBat::HandleAnimEvent` `0x1038e000`: the id decremented and tested 1, 2, 3.
bool FElysiumNpcManBat::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	switch (Event.Event)
	{
	case 1:                                                               // 0x1038e00e -> 0x1038e1b0
		NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, Misc19ManBatWingflaps,
			UE_ARRAY_COUNT(Misc19ManBatWingflaps), 1.0f, NpcKernelSoundsShared::GSoundsChanBody); // RandomInt(0,2)
		return true;
	case 2:                                                               // -> 0x1038e110
		Misc19EmitFixedWav(*this, Misc19ManBatScreech, 1.0f, EElysiumSoundChannel::Body);
		return true;
	case 3:                                                               // -> 0x1038e02e
		NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, Misc19ManBatExerts,
			UE_ARRAY_COUNT(Misc19ManBatExerts), 1.0f, NpcKernelSoundsShared::GSoundsChanBody); // RandomInt(0,2)
		return true;
	default:
		break;
	}
	return FElysiumNpc::HandleAnimEvent(Event);                           // 0x1038e021
}

// Slot 259: `CNPC_VMingXiao::HandleAnimEvent` `0x10392a70`.
bool FElysiumNpcMingXiao::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	const int32 Id = Event.Event;
	if (Id > 0x835)                                                       // 0x10392a7f
	{
		if (Id == 0xbbd)                                                  // 0x10392b31
		{
			if (MingXiaoThrowableObjectMode == 4)                         // +0x673c
			{
				LaunchRagdollTowardTarget();                              // 0x103990c0
			}
			else
			{
				EmitDevMsg(TEXT("WARNING: Ming Xiao is getting EVENT_WEAPON_THROW but is not at VMING_XIAO_TO_MODE_THROWING.\n"),
					TEXT("WARNING: Ming Xiao is getting EVENT_WEAPON_THROW but is not at VMING_XIAO_TO_MODE_THROWING."));
			}
			return true;
		}
		if (Id == 0xbd7)                                                  // 0x10392b36
		{
			// `0x102c42a0(this, "Ming_xiao_vomit_emitter", this, "Bip01 MouthRoot", 0)` (SEAM), then
			// ALSO the base (`0x10392b5c`).
			++MingXiaoVomitEmitterCalls;
		}
		return FElysiumNpc::HandleAnimEvent(Event);
	}
	if (Id == 0x835)                                                      // 0x10392a85
	{
		RecordAnimEventShake(Origin / ElysiumMove::U, 15.0f, 1.0f, 1.5f, 1024.0f, false); // 0x10392afe
		return true;
	}
	switch (Id)                                                           // byte table 0x10392ba0 / 0x10392b90
	{
	case 0x7f8:
		if (MingXiaoThrowableObjectMode == 2)
		{
			++MingXiaoGrabCalls;                                          // 0x10398db0 (SEAM)
		}
		else
		{
			EmitDevMsg(TEXT("WARNING: Ming Xiao is getting NPC_EVENT_PICKUP but is not at VMING_XIAO_TO_MODE_GRABBING.\n"),
				TEXT("WARNING: Ming Xiao is getting NPC_EVENT_PICKUP but is not at VMING_XIAO_TO_MODE_GRABBING."));
		}
		return true;
	case 0x802:
	case 0x803:                                                           // swallowed, silent
		return true;
	case 0x834:
		RecordAnimEventShake(Origin / ElysiumMove::U, 5.0f, 0.6f, 0.2f, 512.0f, false); // 0x10392ace
		return true;
	default:
		break;
	}
	return FElysiumNpc::HandleAnimEvent(Event);
}

// Slot 259: `CNPC_VSabbatLeader::HandleAnimEvent` `0x103a7000` (inside its scope-trace frame).
bool FElysiumNpcSabbatLeader::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	if (Event.Event >= 0x802 && Event.Event <= 0x803)                     // 0x103a705b / 0x103a7062
	{
		FootstepSound();                                                  // 0x103a7066, slot 620
		return true;
	}
	return FElysiumNpc::HandleAnimEvent(Event);                           // 0x103a7077
}

// Slot 259: `CNPC_VTzimisce::HandleAnimEvent` `0x103ba410`. Every failed guard RETURNS (packet
// correction).
bool FElysiumNpcTzimisce::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	const int32 Id = Event.Event;
	if (Id > 0x7f8)                                                       // 0x103ba41e
	{
		if (Id > 0xbbb)
		{
			if (Id != 0xbbd)                                              // 0x103ba580
			{
				return FElysiumNpc::HandleAnimEvent(Event);
			}
			if (TzimisceCarryFormBit())                                   // 0x103be130
			{
				FUN_103bea90(GetEnemyEntity());
			}
			return true;
		}
		if (Id == 0xbbb)
		{
			// Slot 620 `0x103b9810`: `RandomInt(0, 2)` over the swishes, CHANNEL 1.
			NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, Misc19TzimisceSwishes,
				UE_ARRAY_COUNT(Misc19TzimisceSwishes), 1.0f, static_cast<int32>(EElysiumSoundChannel::Weapon));
			return true;
		}
		if (Id == 0x802 || Id == 0x803)
		{
			RecordAnimEventShake(Origin / ElysiumMove::U, 2.0f, 0.2f, 0.2f, 1024.0f, false);
			// Slot 619 `0x103b96a0`: `RandomInt(0, 5)` over the footsteps, channel 4.
			NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, Misc19TzimisceFootsteps,
				UE_ARRAY_COUNT(Misc19TzimisceFootsteps), 1.0f, NpcKernelSoundsShared::GSoundsChanBody);
			return true;
		}
		return FElysiumNpc::HandleAnimEvent(Event);
	}
	if (Id == 0x7f8)
	{
		if (TzimisceCarryFormBit())
		{
			return true;
		}
		// `+0x6670` resolved, then `0x103bef20(target, {"Bip01 L Forearm", "Bip01 R Forearm",
		// "Bip01 L Foot", "Bip01 R Foot"}[+0x6680])` — the port's body takes the index.
		FUN_103bef20(World != nullptr ? World->Resolve(PickupTarget) : nullptr, TzimiscePickupGrabBone); // 0x103ba4d6 / 0x103ba503
		return true;
	}
	switch (Id)                                                           // jump table 0x103ba5b0
	{
	case 2:
		if (NpcStateRetail() == 1)                                        // 0x103ba444
		{
			IdleSound();                                                  // slot 490
		}
		return true;
	case 3:
		if (NpcStateRetail() == 1 && FOkToMakeSound())                    // 0x103ba45f; slot 618 0x103b9440
		{
			Misc19TzimisceVoice(*this, TEXT("SPI_FIDGET"));
		}
		return true;
	case 4:
		Misc19TzimisceVoice(*this, TEXT("SPI_RUNNING"));                  // slot 623
		return true;
	case 5:
		Misc19TzimisceVoice(*this, TEXT("SPI_LAND_HARD"));                // slot 624
		return true;
	case 6:
		Misc19TzimisceVoice(*this, TEXT("SPI_ATTACK_HIT"));               // slot 621
		++TzimisceExpressionTwoCalls;                                     // 0x103b9f90(2, 0.5) SEAM
		return true;
	case 7:
		Misc19TzimisceVoice(*this, TEXT("SPI_ATTACK_HIT_MASSIVE"));       // slot 622
		++TzimisceExpressionTwoCalls;                                     // 0x103b9f90(2, 0.5) SEAM
		return true;
	case 8:
		Misc19TzimisceVoice(*this, TEXT("SPI_JUMP_ATTACK"));              // slot 625
		return true;
	case 9:
		Misc19TzimisceVoice(*this, TEXT("SPI_AGGRO"));                    // slot 626
		++TzimisceExpressionTwoCalls;                                     // 0x103b9f90(2, 2.0) SEAM
		return true;
	default:
		break;
	}
	return FElysiumNpc::HandleAnimEvent(Event);
}

// Slot 259: `CNPC_VTzimisceHeadClaw::HandleAnimEvent` `0x103c1540`.
bool FElysiumNpcTzimisceHeadClaw::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	const int32 Id = Event.Event;
	if (Id != 0x802 && Id != 0x803)                                       // 0x103c1549 / 0x103c1550
	{
		return FElysiumNpc::HandleAnimEvent(Event);                       // 0x103c1556
	}
	RecordAnimEventShake(Origin / ElysiumMove::U, 1.3f, 0.2f, 0.2f, 1024.0f, false);
	// Slot 619 `0x103c2640(bool)`: argument 1 (event 0x802, `0x103c15c9`) draws Foot_Step3/4,
	// argument 0 (event 0x803, `0x103c158e`) Foot_Step1/2; `RandomInt(0, 1)`, channel 4, one sound.
	if (Id == 0x802)
	{
		NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, Misc19HeadClawStepsArgOne,
			UE_ARRAY_COUNT(Misc19HeadClawStepsArgOne), 1.0f, NpcKernelSoundsShared::GSoundsChanBody); // 0x103c15cb
	}
	else
	{
		NpcKernelSoundsShared::SoundsEmitSpeciesWav(*this, Misc19HeadClawStepsArgZero,
			UE_ARRAY_COUNT(Misc19HeadClawStepsArgZero), 1.0f, NpcKernelSoundsShared::GSoundsChanBody); // 0x103c1590
	}
	return true;
}

// Slot 259: `CNPC_VWerewolf::HandleAnimEvent` `0x103d88e0` (inside its scope-trace frame).
bool FElysiumNpcWerewolf::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	const int32 Id = Event.Event;
	if (Id > 0x834)                                                       // 0x103d8959
	{
		if (Id == 0x835)
		{
			++WerewolfActivityVoiceCalls;                                 // 0x103d8df0 (SEAM)
			return true;
		}
		if (Id >= 0x836 && Id <= 0x83d)                                   // swallowed
		{
			return true;
		}
		return FElysiumNpc::HandleAnimEvent(Event);
	}
	if (Id == 0x834)
	{
		// `GetBonePosition("Bip01")`, then `UTIL_ScreenShake(bone, 16.0, 2.0, 2.0, 1500.0, 0, TRUE)`.
		// The bone position is a seam; the origin stands for it.
		RecordAnimEventShake(Origin / ElysiumMove::U, 16.0f, 2.0f, 2.0f, 1500.0f, true); // 0x103d8a00 .. 0x103d8a1d
		return true;
	}
	if (Id == 0x3eb)
	{
		// The first set of `m_pTeleportHint` (+0x66b0), `m_pMoveHint` (+0x66bc), `m_pBreakHint`
		// (+0x66c4); none -> the base.
		const int32 Hint = TeleportHintNode != INDEX_NONE ? TeleportHintNode
			: (MoveHintNode != INDEX_NONE ? MoveHintNode : WerewolfBreakHintNode);
		if (Hint == INDEX_NONE)
		{
			return FElysiumNpc::HandleAnimEvent(Event);
		}
		// `0x102d09b0(hint, this, atoi(options))`: `0 < n < 9` fires the hint's `OnAnimEvent<n>`
		// with this NPC as activator. SEAM: no hint-to-entity output path.
		++WerewolfHintAnimEventCalls;
		WerewolfHintAnimEventIndex = FCString::Atoi(*Event.Options);
		return true;
	}
	if (Id >= 0x802 && Id <= 0x805)
	{
		// `0x103d8c10`: both draws happen BEFORE the ConVar gates — `RandomInt(0, 3)` over the
		// Foot_Step pool, then the pitch `RandomInt(90, 105)` from the non-engine stream.
		const int32 Index = ElysiumRng::Stream(EElysiumRngStream::Footsteps)
			.RandRange(0, UE_ARRAY_COUNT(Misc19WerewolfFootsteps) - 1);
		const int32 Pitch = ElysiumRng::Stream(EElysiumRngStream::Reaction).RandRange(90, 105);
		if (Misc19WerewolfFootstepSounds != 0)
		{
			IElysiumAudio* const Audio = World != nullptr ? World->Audio() : nullptr;
			if (Audio != nullptr)
			{
				FElysiumBodySound Sound;
				Sound.Rel = Misc19WerewolfFootsteps[Index];
				Sound.Volume = 0.75f;
				Sound.SoundLevelDb = 75;
				Sound.Pitch = static_cast<float>(Pitch) / 100.f;
				Sound.Channel = EElysiumSoundChannel::Body;
				Audio->PlayBodySound(Handle, Sound);
			}
		}
		if (Misc19WerewolfFootstepShakes != 0)
		{
			RecordAnimEventShake(Origin / ElysiumMove::U, 4.0f, 5.0f, 0.45f, 750.0f, false);
		}
		return true;
	}
	return FElysiumNpc::HandleAnimEvent(Event);
}

// Slot 379: `CPayphone::EnterGrappleState` `0x101aade0`, 48 bytes — the only slot-379 fill that
// does NOT run the Troika body `0x102b5c00`: all seven arguments go to `CAI_BaseNPC::
// EnterGrappleState` (`0x1026cdc0`) and its `AL` comes back through `SETNZ`. What must NOT run for
// a payphone is the Troika half (the queued-burn refusal, the dialogue stop, the cine cancel and
// the schedule clear); what it keeps is the base body.
bool FElysiumNpcPayphone::EnterGrappleState(const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role, EElysiumGrappleType Type, int32 Position, bool bHolster)
{
	return FElysiumNpcBase::EnterGrappleState(Partner, Role, Type, Position, bHolster); // 0x101aae03 / 0x101aae08
}

// Slot 379: `CNPC_VGhoulCroucher::EnterGrappleState` `0x1037b500`, 69 bytes. One gate ahead of the
// Troika body: a burning croucher REFUSES a player's grapple and burns the player instead.
bool FElysiumNpcGhoulCroucher::EnterGrappleState(const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role, EElysiumGrappleType Type, int32 Position, bool bHolster)
{
	if (bGhoulSpawnBurning && World != nullptr)                           // 0x1037b500 / 0x1037b50c, +0x6665
	{
		FElysiumEntity* const PartnerEntity = World->Resolve(Partner);
		// `0x1037b510`: a non-null grappler; `0x1037b512`/`0x1037b51a`: its `+0xa8` `m_pPlayer`
		// non-null. This runtime's player is the one `PlayerHandle()` names, and `m_pPlayer` is the
		// player's own self-downcast, so the burn target is the partner itself.
		if (PartnerEntity != nullptr && PartnerEntity->Handle == World->PlayerHandle())
		{
			BurnPlayer(PartnerEntity, Misc19GhoulGrappleBurnDamage);      // 0x1037b522
			return false;                                                 // 0x1037b527 XOR AL,AL
		}
	}
	// `0x1037b540`: tail-jump to `CAI_BaseNPCTroika::EnterGrappleState` `0x102b5c00`, DIRECT.
	return FElysiumNpc::EnterGrappleState(Partner, Role, Type, Position, bHolster);
}

// Slot 596: `CNPC_VCop::vfunc596` `0x10372c50`, 72 bytes. The Troika body's own work run once in
// front of it, then the Troika body `0x102b4f60` DIRECT with the RESOLVED entity — which repeats the
// redirect, the `SetEnemy` and the memory write. The doubled work is retail's; reproduced.
void FElysiumNpcCop::Slot596(FElysiumEntity* Arg0)
{
	FElysiumEntity* Resolved = nullptr;
	if (Arg0 != nullptr)                                                  // 0x10372c5a
	{
		Resolved = SummonerRedirect(Arg0);                                // 0x10372c5e, 0x102707d0
		ElysiumNpcEnemy::SetEnemy(*this,
			Resolved != nullptr ? Resolved->Handle : FElysiumEntityHandle::Invalid()); // 0x10372c68
		if (Resolved != nullptr)
		{
			// Crash guard only where retail dereferences a null redirect answer (a summoned body
			// whose owner handle no longer resolves). Slot 544 with the slot-217 origin; the third
			// word retail pushes (`&entity->+0x3d4`) is unread by the port's slot-544 body.
			UpdateEnemyMemory(Resolved, Resolved->GetAbsOrigin(), nullptr); // 0x10372c7a / 0x10372c84
		}
	}
	FElysiumNpc::Slot596(Resolved);                                       // 0x10372c8e -> 0x102b4f60
}

// Slot 598: `CNPC_VCop::vfunc598` `0x10372dd0`, 238 bytes — the forgive-the-player arm.
void FElysiumNpcCop::Slot598(FElysiumEntity* Arg0)
{
	// `0x10372dd4`-`0x10372e0a`: only when the argument IS the entity `m_hClosestPlayer`
	// (`+0x628c`) resolves to; a stale handle resolves null and a null argument then matches.
	const FElysiumEntity* const Closest =
		World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	if (Arg0 != Closest)                                                  // 0x10372e0a
	{
		FElysiumNpc::Slot598(Arg0);                                       // 0x10372eb4 -> 0x102b4fe0
		return;
	}
	// 1. `AddEntityRelationship(player, 4 D_NU, 0)`.
	if (Arg0 != nullptr)
	{
		// Crash guard: a null entity adds no row.
		Relationships.SetEntity(Arg0->Handle, Misc19CopForgetDisposition, 0); // 0x10372e15
	}
	// 2. `m_eOldPlayerRelationType` (`+0x6668`) := 0.
	CopOldPlayerRelationType = 0;                                         // 0x10372e1e
	// 3. Slot 167 `GetEnemy()` IS the player -> `SetEnemy(NULL)`.
	if (static_cast<const FElysiumNpcBase&>(*this).GetEnemy() == Arg0)    // 0x10372e28 / 0x10372e30
	{
		ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid()); // 0x10372e36
	}
	// 4. `m_hLastEnemy` (`+0x1a94`) resolving to the player -> `0x10279b70(NULL)`.
	const FElysiumEntity* const Last =
		World != nullptr ? World->Resolve(BaseMemory.LastEnemy) : nullptr; // 0x10372e3b .. 0x10372e63
	if (Last == Arg0)                                                     // 0x10372e6b
	{
		SetLastEnemy(nullptr);                                            // 0x10372e71
	}
	// 5. `CAI_Enemies::ClearMemory` on slot 541 with the `"%s(%d) :"` reason (`NPC_VCop.cpp`,
	//    `0x73b`, debug text).
	ClearEnemyMemoryRecord(Arg0);                                         // 0x10372e93 / 0x10372e9b
	// 6. `InputSetRelationship("Player D_NU 10", 0)` (`0x10273790`).
	FElysiumInputArgs Args;
	Args.Param = FElysiumVariant::String(Misc19CopForgiveRelationship);
	Args.Activator = Arg0 != nullptr ? Arg0->Handle : FElysiumEntityHandle::Invalid();
	Args.Caller = Handle;
	Args.Input = FName(TEXT("SetRelationship"));
	InputSetRelationship(Args);                                           // 0x10372ea9
}

// =================================================================================================
// `0x10365a90` — `CNPC_VBach`'s camper pass (the tail of `GatherConditions` `0x10365a70`)
// =================================================================================================

namespace
{
	// `FVisible(target, 0x2804091, &blocker, attempt)` ten times (`0x10365ac7`-`0x10365ae9`).
	constexpr int32 Misc19BachVisibleMask = 0x2804091;
	constexpr int32 Misc19BachVisibleAttempts = 10;
	constexpr float Misc19BachSmallMoveProduct = 20000.0f;   // `_DAT_104aaad0`
	constexpr double Misc19BachOccludeWaitSeconds = 4.0;     // `_DAT_10450aa0`
	constexpr float Misc19BachSmallMoveMax = 200.0f;          // `_DAT_104492b8`
	// `0x10365f91`/`0x10365f9b`: the schedule trace line (`NPC_VBach.cpp`) and `0x102ae750(0x15f, 0)`.
	constexpr int32 Misc19BachReacquireTraceLine = 0x57b;
	constexpr int32 Misc19BachReacquireScheduleRetailId = 0x15f;
	// The sound arm's `EmitSound(filter, entindex, CHAN_VOICE 2, wav, 1.0, 0.8, 0, 100, …)`.
	constexpr int32 Misc19BachSoundChannel = 2;
}

void FElysiumNpcBach::BachGatherCamperConditions()
{
	// `0x10365a90`. The target is slot 167 `GetEnemy`, else the local player (`0x101cda50`).
	FElysiumEntity* Target = static_cast<const FElysiumNpcBase&>(*this).GetEnemy(); // 0x10365a9b
	bool bNewCamper = false;                                              // [ESP+0x12], 0x10365ab0
	bool bGrenadeSound = true;                                            // [ESP+0x13], 0x10365aab
	if (Target == nullptr)                                                // 0x10365ab4
	{
		Target = World != nullptr ? World->FindPlayer() : nullptr;        // 0x10365ab6 -> 0x101cda50
		if (Target == nullptr)                                            // 0x10365abf
		{
			return;
		}
	}
	for (int32 Attempt = 0; Attempt < Misc19BachVisibleAttempts; ++Attempt) // 0x10365ac7 / 0x10365ae9
	{
		// Retail's third argument is a non-null local cell; the port reads only its nullness.
		if (FVisible(Target, Misc19BachVisibleMask, this, Attempt))       // 0x10365ad7 / 0x10365adf
		{
			// The re-acquire arm (`0x10365f6a`).
			if (BachWasOccluded == 0)                                     // 0x10365f72
			{
				return;
			}
			const bool bFlag = bBachCamperFlag;                           // 0x10365f74
			BachWasOccluded = 0;                                          // 0x10365f7a
			if (!bFlag)                                                   // 0x10365f86
			{
				return;
			}
			RecordScheduleEvent(FString::Printf(TEXT("SelectSchedule :%d -> 0x%x"),
				Misc19BachReacquireTraceLine, Misc19BachReacquireScheduleRetailId)); // 0x10365f91 / 0x10365f9b
			SetSchedule(Misc19BachReacquireScheduleRetailId, false);      // 0x10365fa5 -> 0x102ae750
			return;
		}
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const FVector TargetUnits = Target->GetAbsOrigin() / ElysiumMove::U;  // slot 217
	auto RaiseCamper = [this, &bNewCamper]()
	{
		if (!bBachCamperFlag)
		{
			bNewCamper = true;
		}
		bBachCamperFlag = true;
	};
	if (BachWasOccluded == 0)                                             // 0x10365af5
	{
		BachWasOccluded = 1;                                              // 0x10365afb
		BachOccludeEnterTime = Now;                                       // 0x10365b10
		const float Product = FMath::Abs(static_cast<float>(TargetUnits.Y - BachLastOccludeOriginUnits.Y))
			* FMath::Abs(static_cast<float>(TargetUnits.X - BachLastOccludeOriginUnits.X)); // 0x10365b2a .. 0x10365b40
		if (Product < Misc19BachSmallMoveProduct)                         // 0x10365b42 / 0x10365b4d (>= or NaN resets)
		{
			if (++BachReusedOccludeCount >= 2)                            // 0x10365b58 / 0x10365b61
			{
				RaiseCamper();                                            // 0x10365b69 / 0x10365b70
			}
		}
		else
		{
			BachReusedOccludeCount = 0;                                   // 0x10365b79
		}
		BachLastOccludeOriginUnits = TargetUnits;                         // 0x10365b83 .. 0x10365b9d
	}
	else if (Now - BachOccludeEnterTime > Misc19BachOccludeWaitSeconds)   // 0x10365bb7 / 0x10365bc4, strict
	{
		float MaxDelta = FMath::Abs(static_cast<float>(TargetUnits.X - BachLastOccludeOriginUnits.X)); // 0x10365be2
		const float DeltaY = FMath::Abs(static_cast<float>(TargetUnits.Y - BachLastOccludeOriginUnits.Y));
		if (MaxDelta < DeltaY)                                            // 0x10365c02
		{
			MaxDelta = DeltaY;
		}
		if (MaxDelta < Misc19BachSmallMoveMax)                            // 0x10365c10 / 0x10365c1b
		{
			RaiseCamper();                                                // 0x10365c23 / 0x10365c2a
		}
		BachLastOccludeOriginUnits = TargetUnits;                         // 0x10365c35 .. 0x10365c4f
		BachOccludeEnterTime = Now;                                       // 0x10365c5e
	}
	// Grenade zones 10 and 7 force the flag AND the sound edge on every occluded pass.
	if (BachGrenadeActive == 10 || BachGrenadeActive == 7)                // 0x10365c6d / 0x10365c72
	{
		bBachCamperFlag = true;                                           // 0x10365c74
		bNewCamper = true;                                                // 0x10365c7f
		BachLastOccludeOriginUnits = TargetUnits;                         // 0x10365c84 .. 0x10365c9e
		BachOccludeEnterTime = Now;                                       // 0x10365cad
	}
	if (!bBachCamperFlag)                                                 // 0x10365cbb
	{
		bGrenadeSound = false;                                            // 0x10365d7e
	}
	else if (BachGrenadeActive != 0 && bBachInStartingPosition)           // 0x10365cc9 / 0x10365cd1
	{
		switch (BachGrenadeActive)                                        // 0x10365cd9 / table 0x10365fb4
		{
		case 5:  ThrowGrenade(TEXT("grenade_spawn_5"), 300.0f); break;    // 0x10365cf2
		case 6:  ThrowGrenade(TEXT("grenade_spawn_6"), 80.0f); break;     // 0x10365d08
		case 7:  ThrowGrenade(TEXT("grenade_spawn_7"), -26.0f); break;    // 0x10365d1b
		case 9:  ThrowGrenade(TEXT("grenade_spawn_9"), 85.0f); break;     // 0x10365d41
		case 10: ThrowGrenade(TEXT("grenade_spawn_10"), -26.0f); break;   // 0x10365d2e
		default: break;   // 8, and anything outside 5..10 (`0x10365cd9 JA`): no spawn, grenade sound kept
		}
	}
	else if (BachGrenadeActive != 0 && BachTeleportState == 0)            // 0x10365d54 / 0x10365d5c / 0x10365d64
	{
		if (BachGrenadeActive == 8)                                       // 0x10365d69
		{
			ThrowGrenade(TEXT("grenade_spawn_8"), 15.0f);                 // 0x10365d77
		}
	}
	else
	{
		bGrenadeSound = false;                                            // 0x10365d7e
	}
	if (!bNewCamper)                                                      // 0x10365d86
	{
		return;
	}
	EmitNamedWav(this, Misc19BachSoundChannel, bGrenadeSound             // 0x10365d90
		? TEXT("Character/Boss/Bach/bach_grenade.wav")                   // 0x10365e2a
		: TEXT("Character/Boss/Bach/bach_camp_warn.wav"),                // 0x10365f02
		1.0f, 0.8f, 100);
}

// =================================================================================================
// `0x103cfc50` — `CNPC_VWerewolf::CheckAllMoveHints`
// =================================================================================================

namespace
{
	// The four move-hint programs, in retail's compare order.
	constexpr int32 Misc19WerewolfMoveHintSchedules[] = { 0x15f, 0x15b, 0x15a, 0x160 };
}

bool FElysiumNpcWerewolf::CheckAllMoveHintsImperativeSeam(const FHintWords& Hint) const
{
	// SEAM for `IsImperativeMoveHint` (`0x103d2070`, lane L12) — see the declaration.
	(void)Hint;
	return false;
}

bool FElysiumNpcWerewolf::CheckAllMoveHints()
{
	// `0x103cfc50`, inside its named scope-trace frame (absent).
	// 1. `m_pSchedule` (`+0x5c38`) set and equal to `0x102cc1f0(id)` — slot 440, then slot 446, a
	//    miss answering schedule 1 untranslated — for any of the four programs -> TRUE.
	if (Schedule.IsRunning())                                             // 0x103cfcc8
	{
		const FElysiumScheduleProgram* const Running = ElysiumScheduleFor(Schedule.Current);
		for (const int32 RetailId : Misc19WerewolfMoveHintSchedules)     // 0x103cfcd1 / cfef / d0d / d25
		{
			const void* Program = GetScheduleOfType(TranslateScheduleRetail(RetailId));
			if (Program == nullptr)
			{
				Program = GetScheduleOfType(ElysiumSched::IDLE_STAND);    // 0x102cc227
			}
			if (Program != nullptr && Program == Running)
			{
				return true;                                              // 0x103d0143
			}
		}
	}
	// 2. `ShouldPursueEnemy` refusing -> FALSE, with no search timer.
	if (!WerewolfShouldPursueEnemy())                                     // 0x103cfd38 / 0x103cfd3f
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	// 3. A held move hint whose target groundpoint has a path to the enemy point -> TRUE.
	if (MoveHintNode != INDEX_NONE)                                       // 0x103cfd55 / 0x103cfd5d
	{
		FHintWords Held;
		if (HintWords(MoveHintNode, Held))   // crash guard: retail holds a live CAI_Hint*
		{
			const FVector HeldGroundpoint = GetHintTargetGroundpoint(Held); // 0x103cfd67
			FVector EnemyPoint;
			FUN_103d9c90(EnemyPoint);                                     // 0x103cfd73
			if (WerewolfHasPath(HeldGroundpoint, EnemyPoint))             // 0x103cfdac / 0x103cfdb3
			{
				return true;
			}
		}
	}
	// 4. The search.
	StartSearchTimer();                                                   // 0x103cfdbb
	WerewolfMorphTimerB = static_cast<float>(EngineFrameNumber());        // 0x103cfdce +0x66d4 m_iLastHintSearchTick
	WerewolfMoveHintSearchStart = 0;                                      // 0x103cfddf +0x66b8
	WerewolfMorphTimerC = static_cast<float>(Now);                        // 0x103cfde9 +0x66d8 m_flLastHintSearchTime
	const TArray<int32> Hints = GlobalHintList();                         // 0x103cfdef DAT_10925450
	const FVector OriginUnits = GetAbsOrigin() / ElysiumMove::U;          // 0x103cfdf7 slot 217
	FVector EnemyPoint;
	FUN_103d9c90(EnemyPoint);                                             // 0x103cfe18
	// `werewolf_pursuit_distance` read twice into two limits (`0x103cfe25` A, `0x103cfe45` B).
	const float LimitA = WerewolfPursuePlayerDistLimitUnits();
	const float LimitB = WerewolfPursuePlayerDistLimitUnits();
	TArray<FVector> Seen;                                                 // 0x103cfe6a CUtlVector<Vector>
	// `[ESP+0x18]`, the local every iteration appends. An INVALID hint appends it without loading
	// its own point (packet CORRECTED clause): the previous valid hint's groundpoint, or, before any,
	// uninitialised stack — seeded with NaN, which compares equal to nothing (the garbage retail
	// holds is unrecoverable). Reproduced.
	FVector Candidate(std::numeric_limits<double>::quiet_NaN());
	for (const int32 Node : Hints)                                        // 0x103cfe81 / 0x103d0062 +0x5d8
	{
		FHintWords Hint;
		if (!HintWords(Node, Hint))
		{
			continue;   // crash guard: the port's hint list can name an unresolvable node
		}
		if (CheckAllMoveHintsImperativeSeam(Hint))                        // 0x103cfe8a / 0x103cfe93
		{
			ClearMoveHint();                                              // 0x103d00c8
			SetMoveHint(Node, false);                                     // 0x103d00d2
			return ReportSearchTimer(true);                               // 0x103d00e0
		}
		if (IsValidMoveHint(Hint, Now))                                   // 0x103cfe9a / 0x103cfea1
		{
			Candidate = GetHintTargetGroundpoint(Hint);                   // 0x103cfeaf .. 0x103cfeca
			// The seen scan: exact equality per component, NaN never equal; a match skips the
			// tests and APPENDS the candidate again (`0x103cff0f`).
			if (!Seen.Contains(Candidate))                                // 0x103cfed0 .. 0x103cff0f
			{
				const FVector HintOriginUnits = Hint.OriginCm / ElysiumMove::U; // 0x103cff19 slot 217
				if (static_cast<float>(FVector::Dist(OriginUnits, HintOriginUnits)) < LimitB // 0x103cff4d / 0x103cff5f
					&& static_cast<float>(FVector::Dist(Candidate, EnemyPoint)) < LimitA     // 0x103cff97 / 0x103cffa9
					&& WerewolfHasPath(Candidate, EnemyPoint)                                // 0x103cffe3 / 0x103cffea
					&& WerewolfHasPath(OriginUnits, GetHintGroundpoint(Hint)))               // 0x103cfff4 / 0x103d0025
				{
					ClearMoveHint();                                      // 0x103d0105
					SetMoveHint(Node, false);                             // 0x103d010f
					return ReportSearchTimer(true);                       // 0x103d011d
				}
			}
		}
		Seen.Add(Candidate);                                              // 0x103d0036 .. 0x103d005d
	}
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("CNPC_VWerewolf::CheckAllMoveHints FAILED."));  // 0x103d0078 DevWarning
	return ReportSearchTimer(false);                                      // 0x103d008a
}
