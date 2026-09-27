// `CAI_BaseNPC`'s bodies of the `Sounds` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSounds.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSoundsShared.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `SF_NPC_GAG`, `m_spawnflags & 2` (`0x1027a5c0` at `1027a5ea`). A gagged NPC is silent outside
	// combat and vocal inside it.
	constexpr int32 GSoundsSpawnFlagGag = 0x2;
	// `CAI_BaseNPC::JustMadeSound` `0x1027a640` draws `RandomFloat(0x3fc00000, 0x40000000)`.
	constexpr float GSoundsBaseSoundWaitMin = 1.5f;
	constexpr float GSoundsBaseSoundWaitMax = 2.0f;
	// `CAI_BaseNPC::ShouldPlayIdleSound` `0x1027a420`: `RandomInt(0, 999)` ordinarily, `RandomInt(
	// 0, 20)` while `SCHED_TROIKA_COMFORT` runs (`1027a4a8` loads `0x14` into the weight).
	constexpr int32 GSoundsIdleSoundWeight = 999;
	constexpr int32 GSoundsIdleSoundWeightComforting = 0x14;
	// `SCHED_TROIKA_COMFORT`, the schedule the weight is special-cased for
	// (`docs/vtmb/npc-ai/conditions-and-states.md` → "The comfort sweep").
	constexpr int32 GSoundsScheduleComfort = 0x12f;
	// `CAI_BaseNPC::ShouldPlayFloatSound` `0x1027a530`: the two frequencies that disable the hook
	// outright, tested as two separate equalities rather than a range.
	constexpr int32 GSoundsFloatFrequencyOff = 0;
	constexpr int32 GSoundsFloatFrequencyAlsoOff = 8;
	// `CAI_BaseNPC::PlaySentence`'s raw-wave arm (`10278e60`..`10278f1f`): `SENTENCEG_Lookup(name)`
	// then `EmitSentenceByIndex(CPASAttenuationFilter, entindex, CHAN_VOICE, index, volume,
	// soundlevel, 0, 100)`. Same seam, same refusal, and the return value IS the looked-up index, so
	// a refusal is -1 here too.
	int32 SoundsEmitSentenceByName(const FElysiumNpcBase& Npc, const TCHAR* Name, float Volume,
		int32 SoundLevel)
	{
		return NpcKernelSoundsShared::SoundsPlaySentenceGroup(Npc, Name, Volume, SoundLevel, 0, 100);
	}
}

// --- Moved from `ElysiumNpcSounds.cpp` (story 5 step 5) ---

// `CAI_BaseNPC::FUN_1026aef0` (`0x1026aef0`) — slot 474's BASE body, replaced on the Troika line
// and therefore unreachable from any class this port stands. `m_pSenses (+0x5cdc)->GetClosestSound(
// /*bScent*/ 0)` (`PUSH 0x0` at `1026aef7`), with a dev warning on null.
//
// SEAM: `CAI_Senses::GetClosestSound` (`0x103104f0`) walks the senses object's retained sound list
// and answers the NEAREST audible member. `FElysiumNpcSenses` retains one record per CSound family
// and commits a winner into `Memory.BestSound`; it has no "closest of the live list" accessor,
// because nothing in this runtime dispatches the base body. The seam answers null and warns exactly
// as retail does.
void* FElysiumNpcBase::GetBestSound()
{
	void* const Sound = nullptr;
	if (Sound == nullptr)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s Warning: NULL Return from GetBestSound"),
			*DebugString());
	}
	return Sound;
}

// `CAI_BaseNPC::FUN_1026af30` (`0x1026af30`) — slot 475, and the Troika line does NOT override it,
// so this IS the body every `npc_V*` dispatches. Byte-for-byte `GetBestSound`'s base body with
// `PUSH 0x1` instead of `PUSH 0x0` (`1026af37`) and its own warning string.
//
// SEAM: scent. `CAI_Senses::GetClosestSound(bScent = true)` reads the same retained list filtered to
// scent stimuli; this runtime's game-sound bus carries no scent channel at all — nothing emits one —
// so the reader answers null and warns, which is retail's own answer on a map with no scents.
void* FElysiumNpcBase::GetBestScent()
{
	void* const Scent = nullptr;
	if (Scent == nullptr)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s Warning: NULL Return from GetBestScent"),
			*DebugString());
	}
	return Scent;
}

// `CAI_BaseNPC::FUN_10278e30` (`0x10278e30`), slot 484. Four arms, in retail's order:
//
//   1. `if (!pszSentence) return -1;`                                       (`10278e3f`)
//   2. `if (!IsAlive()) return -1;`   — slot 158, vtable `+0x278`           (`10278e49`)
//   3. a leading `'!'` names a RAW SENTENCE: `SENTENCEG_Lookup(name)`, then
//      `EmitSentenceByIndex(CPASAttenuationFilter(GetSoundEmissionOrigin(), attn), entindex(),
//      CHAN_VOICE, index, volume, soundlevel, 0, pitch 100, NULL, NULL, true, 0)`, returning the
//      looked-up index.                                                     (`10278e57`)
//   4. anything else is a sentence GROUP: `SENTENCEG_PlayRndSz(edict(), name, volume, soundlevel,
//      0, 100)`, returned unchanged.                                        (`10278f5f`)
//
// `delay` (arg 1) and `pListener` (arg 4) are READ BY NOTHING — the listing never touches
// `[ESP+0x4c]` or `[ESP+0x58]`. They are retail's own dead parameters, kept because slot 485 and
// every caller pass them.
//
// The attenuation the '!' arm hands its filter is Source's `SNDLVL_TO_ATTN` with a retail quirk:
// `soundlevel > 50 ? (int)(20 / (soundlevel - 50)) : <const>` is an INTEGER divide (`IDIV` at
// `10278eb1`, `FILD` at `10278eb7`), not the SDK's float one, so level 51 gives 20 and level 71
// gives 1 with everything between truncated. It feeds only the PAS recipient cull, which
// single-player never runs.
int32 FElysiumNpcBase::PlaySentence(const TCHAR* Sentence, float /*Delay*/, float Volume,
	int32 SoundLevel, FElysiumEntity* /*Listener*/)
{
	if (Sentence == nullptr || *Sentence == TEXT('\0'))
	{
		return -1;
	}
	// Dispatched, not inlined: retail calls slot 158 through the vtable, and the Lifecycle family
	// owns that body.
	if (!IsAlive())
	{
		return -1;
	}
	if (*Sentence == TEXT('!'))
	{
		return SoundsEmitSentenceByName(*this, Sentence, Volume, SoundLevel);
	}
	return NpcKernelSoundsShared::SoundsPlaySentenceGroup(*this, Sentence, Volume, SoundLevel, 0, 100);
}

// `CAI_BaseNPC::FUN_10279000` (`0x10279000`), slot 485, and the whole body is the forward: it
// tail-calls slot 484 through vtable `+0x790` with `(name, delay, volume, soundlevel, NULL)`,
// DROPPING both of its own extra arguments — the bool and the `CBaseEntity*` listener — and
// hardcoding a null listener rather than passing the one it was given.
int32 FElysiumNpcBase::PlayScriptedSentence(const TCHAR* Sentence, float Delay, float Volume,
	int32 SoundLevel, bool /*bConcurrent*/, FElysiumEntity* /*Listener*/)
{
	return PlaySentence(Sentence, Delay, Volume, SoundLevel, nullptr);
}

// `CAI_BaseNPC::FUN_1027a5c0` (`0x1027a5c0`), slot 486's base body. Three gates, in order:
//
//   1. `curtime <= m_flSoundWaitTime` → false. (`FCOMP`/`FNSTSW` with mask `0x4100` at `1027a5d8`:
//      the less-or-EQUAL sense is retail's, so a sound at exactly the deadline is refused.)
//   2. connected squad (`m_iSquadDisconnected < 1 && m_pSquad`) and `curtime <= squad->+0x60`
//      → false. The squad keeps its OWN copy of the clock and either one can gag this NPC.
//   3. `SF_NPC_GAG` set and `m_NPCState != NPC_STATE_COMBAT` → false.
//
// SEAM: `m_pSquad` (`+0x5da4`). This substrate has no squad object (`ConnectedSquad()` answers null
// on every NPC), so gate 2 never refuses; the squad layer replaces the accessor, not this body.
bool FElysiumNpcBase::FOkToMakeSound()
{
	const double Now = NpcKernelSoundsShared::SoundsCurTime(*this);
	if (Now <= BaseMemory.SoundWaitTime)
	{
		return false;
	}
	if (BaseScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)
	{
		// The squad's own `m_flSoundWaitTime` at `+0x60`. Unreachable while `ConnectedSquad()`
		// answers null; the field it stands for is named so the squad layer knows what to hand back.
		return false;
	}
	if ((SpawnFlags & GSoundsSpawnFlagGag) != 0 && NpcKernelSoundsShared::SoundsRetailNpcState(Mind.State()) != NpcKernelSoundsShared::GSoundsNpcStateCombat)
	{
		return false;
	}
	return true;
}

// `CAI_BaseNPC::FUN_1027a640` (`0x1027a640`), slot 487's base body. Identical shape to the Troika
// one with a 1.5–2.0 draw. Not reached on the Troika line.
void FElysiumNpcBase::JustMadeSound()
{
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	BaseMemory.SoundWaitTime = NpcKernelSoundsShared::SoundsCurTime(*this) + Stream.FRandRange(GSoundsBaseSoundWaitMin,
		GSoundsBaseSoundWaitMax);
	if (BaseScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)
	{
		// SEAM: the squad's own copy, as above.
	}
}

// `CAI_BaseNPC::FUN_1027a420` (`0x1027a420`), slot 509's base body — REACHED through the override
// above. Read off the listing, because the decompiled C hides the order of the last two arms.
//
// Five refusals, then a weighted roll:
//
//   1. `m_NPCState` not in { IDLE(1), ALERT(3) } → false.
//   2. `SF_NPC_GAG` (`m_spawnflags & 2`) → false.
//   3. `!m_bIsBCCTargetable` → false.
//   4. a LIVE `m_hDialogPartner` (`+0xfe8`) → false.
//   5. `IsBusyWithDiscipline()` → false.
//
// then: weight 999, unless a schedule is installed AND `GetLocalScheduleId(m_pSchedule->id)` is
// `0x12f SCHED_TROIKA_COMFORT`, in which case the weight is 20 and the float-sound arm is SKIPPED
// ENTIRELY. Otherwise — and this is the arm the one-line walk missed — the body first asks
// `ShouldPlayFloatSound()` (slot 510, vtable `+0x7f8`) and, when that says yes, plays `FloatSound()`
// (slot 507, `+0x7ec`) and returns FALSE. The float sound is played INSTEAD of an idle sound, not
// beside it. Only when the float gate refuses does `RandomInt(0, weight) == 0` decide.
//
// SEAMS, each named where it is asked:
//   * `m_bIsBCCTargetable` (`+0x7ec` on `CBaseCombatCharacter`) has no port member; the port has no
//     targetability latch and the arm is not tested.
//   * `m_hDialogPartner` is stood for by "this character owns the open dialogue session", the same
//     reading `FElysiumCombatCharacter`'s gaze cascade makes.
//   * `GetLocalScheduleId` is slot 447 (`0x101a6620` → `0x102ea280`), landed by family
//     EntityChain; this body dispatches it exactly as retail does. It answers -1 for every id
//     today, because this runtime parses no schedule text and every `CAI_ClassScheduleIdSpace`
//     sub-space is still the empty `9999` sentinel. -1 is neither 0 nor `0x12f`, so an
//     untranslatable running schedule cannot be read as schedule 0 and cannot take the comfort
//     branch; the weight stays 999 until story 10i registers `SCHED_TROIKA_COMFORT`.
bool FElysiumNpcBase::ShouldPlayIdleSound()
{
	const int32 State = NpcKernelSoundsShared::SoundsRetailNpcState(Mind.State());
	if (State != NpcKernelSoundsShared::GSoundsNpcStateIdle && State != NpcKernelSoundsShared::GSoundsNpcStateAlert)
	{
		return false;
	}
	if ((SpawnFlags & GSoundsSpawnFlagGag) != 0)
	{
		return false;
	}
	// `m_bIsBCCTargetable` — SEAM: no port member. Retail refuses here when the byte is clear.
	if (World != nullptr)
	{
		const FElysiumEntityHandle DialogOwner = World->GetOpenDialogOwner();
		if (DialogOwner.IsSet() && DialogOwner.Index == Handle.Index)
		{
			return false;
		}
	}
	if (IsBusyWithDiscipline())
	{
		return false;
	}

	int32 Weight = GSoundsIdleSoundWeight;
	bool bComforting = false;
	if (Schedule.IsRunning())
	{
		const int32 Local = GetLocalScheduleId(Schedule.Current);
		if (Local == GSoundsScheduleComfort)
		{
			Weight = GSoundsIdleSoundWeightComforting;
			bComforting = true;
		}
	}
	if (!bComforting)
	{
		// `1027a4cc`: the float-sound arm, and it RETURNS — a body that floated does not also
		// vocalise on this pass.
		if (ShouldPlayFloatSound())
		{
			FloatSound();
			return false;
		}
	}
	return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, Weight) == 0;
}

// `CAI_BaseNPC::FUN_1027a530` (`0x1027a530`), slot 510's base body — REACHED as the Troika
// override's tail call. Three refusals and a roll:
//
//   1. `m_iFloatSoundFrequency == 0`  → false   (two SEPARATE equalities in retail, not a range)
//   2. `m_iFloatSoundFrequency == 8`  → false
//   3. `engine->Time() < m_flNextFloatSoundTime` → false
//   4. `RandomInt(0, m_iFloatSoundFrequency) == 0` — the authored "1 time in X" roll.
//
// The clock this arm reads is the ENGINE's `Time()` (`DAT_1070b22c` vtable `+0x1dc`), not
// `gpGlobals->curtime`; in this substrate there is one clock and it is `World->NowSeconds()`.
bool FElysiumNpcBase::ShouldPlayFloatSound()
{
	if (FloatSoundFrequency == GSoundsFloatFrequencyOff)
	{
		return false;
	}
	if (FloatSoundFrequency == GSoundsFloatFrequencyAlsoOff)
	{
		return false;
	}
	if (NpcKernelSoundsShared::SoundsCurTime(*this) < NextFloatSoundTime)
	{
		return false;
	}
	return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, FloatSoundFrequency) == 0;
}

// `CAI_BaseNPC::FUN_1027caa0` (`0x1027caa0`). Forty-four bytes and one call:
// `IEngineSound::vfunc5(engine->IndexOfEdict(this->edict() /*+0x2e0*/), 1)`.
//
// SEAM: `IElysiumAudio::StopEntitySounds`, added by this story and answering nothing. `PlayBodySound`
// hands the caller a voice handle and the map actor's `(Owner, Channel)` pool is what holds a live
// voice, so there is nothing in this substrate addressable by entity index; the verb is declared so
// this body asks for what retail asks for, and the day a pool reachable by owner exists it answers.
//
// UNRECOVERED: what the literal `1` is. `IEngineSound`'s slot-5 signature is not pinned by this call
// site — the only other use of the interface in this family is slot 3 (`EmitSound`) and slot 4
// (`EmitSentenceByIndex`) — so the second argument is passed through as the recovered literal.
//
// The census's only species override, `CNPC_Crow::vfunc511` (`0x10357800`), is on a class no map
// stands and carries no port arm.
void FElysiumNpcBase::StopLoopingSounds()
{
	if (IElysiumAudio* Audio = World != nullptr ? World->Audio() : nullptr)
	{
		Audio->StopEntitySounds(Handle, /*retail's literal second argument*/ 1);
	}
}
