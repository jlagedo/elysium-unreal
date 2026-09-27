// Story 29c-1, family **Sounds** — the NPC's sound and speech surface, layers 0–9.
//
// Nine Troika-line slots (474 `GetBestSound`, 475 `GetBestScent`, 484 `PlaySentence`,
// 485 `PlayScriptedSentence`, 486 `FOkToMakeSound`, 487 `JustMadeSound`, 509 `ShouldPlayIdleSound`,
// 510 `ShouldPlayFloatSound`, 511 `StopLoopingSounds`), the five base-class halves the Troika line
// replaces or delegates to, and the per-species vocalization table behind the 488–508 / 620 / 621
// sound hooks.
//
// The walked prose is `docs/vtmb/npc-ai/conditions-and-states.md` (the gates),
// `docs/vtmb/npc-ai/senses.md` (the sound/scent memory readers) and
// `docs/vtmb/npc-ai/shape.md` (the species vocalization table).
//
// **What this family emits through.** Every retail body here ends in one of three mechanisms, and
// each goes through a seam this runtime already owns rather than a second audio path:
//
//   * `IEngineSound::EmitSound(filter, entindex, channel, wav, volume, attenuation, 0, 100)` —
//     `IElysiumAudio::PlayBodySound`, the same seam `FElysiumNpc::NpcStep` emits a footfall
//     through (`ElysiumFootsteps.h` → "Species overrides").
//   * `SENTENCEG_PlayRndSz(edict, group, volume, soundlevel, 0, pitch)` and
//     `IEngineSound::EmitSentenceByIndex(...)` — this runtime has NO sentence system (retail's
//     `sentences.txt` is unported), so `SoundsPlaySentenceGroup` (this file) is the seam and it answers -1.
//   * `IEngineSound` slot 5, "stop what this entity is playing" —
//     `IElysiumAudio::StopEntitySounds`, added by this story and answering nothing.

#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSoundsShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"      // ElysiumMove::U — the Source-unit/centimetre conversion
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"

namespace
{
	// ---------------------------------------------------------------------------------------
	// The recovered constants, each beside the object it was read from.
	// ---------------------------------------------------------------------------------------

	// `CAI_BaseNPCTroika::JustMadeSound` `0x102b4c40` draws `RandomFloat(0x3e800000, 0x3f400000)`.
	constexpr float GSoundsTroikaSoundWaitMin = 0.25f;
	constexpr float GSoundsTroikaSoundWaitMax = 0.75f;

	// `CAI_BaseNPCTroika::ShouldPlayFloatSound` `0x10294070`: `m_bfAINPCFlags & 0x20000`, which
	// `ElysiumNpcFlags.h` names `SLEEPING`.
	constexpr EElysiumNpcFlag GSoundsFloatSoundBlockingFlag = EElysiumNpcFlag::SLEEPING;

	// The `Float_Sound_Info` rule table (`vdata/system/rules_tables.txt`) and the row `0x10294070`
	// lazily loads into `_DAT_1092483c`: index 0, "FloatSoundDistance -- Maximum distance to player
	// to play float sounds", 50.0 SOURCE UNITS. The guard word `DAT_10924d1c` makes retail's read
	// a once-per-process magic static; this port re-reads the authored table, which is the same
	// answer with no process-lifetime cache to invalidate.
	const TCHAR* const GSoundsFloatSoundTable = TEXT("Float_Sound_Info");
	constexpr int32 GSoundsFloatSoundDistanceRow = 0;
	constexpr float GSoundsFloatSoundDistanceFallbackUnits = 50.0f;

	// `soundlevel_t` for every species vocalization. Retail passes `EmitSound` the pair (volume,
	// attenuation 0.8); 0.8 is Source's `ATTN_NORM`, and the level behind it is the exact inverse of
	// `0x1026d5e1`'s own `20/(L-50)`: `L = 50 + 20/0.8 = 75` (the derivation is spelled once in
	// `ElysiumFootsteps.h` → `SpeciesSoundLevelDb`, which reads the same pair off the footstep
	// vfuncs). A species vocalization is `SNDLVL_NORM`; not one of these rows reads a distance.
	constexpr int32 GSoundsSpeciesSoundLevelDb = 75;
	// `pitch 100` on every row, which is this seam's 1.0 (`FElysiumBodySound::Pitch` is a
	// multiplier, not Source's percentage).
	constexpr float GSoundsSpeciesPitch = 1.0f;

	// Source `CHAN_*`. The vocalizations emit on `CHAN_VOICE`; the Sabbat leader's footstep and
	// attack hooks emit on `CHAN_BODY`.
	constexpr int32 GSoundsChanVoice = 2;
	constexpr int32 GSoundsChanBody = 4;

	// ---------------------------------------------------------------------------------------
	// `CAI_BaseNPCTroika::IsInDialog` (`0x102c1170`) — the Troika gate four of this family's
	// bodies open with.
	//
	// Four terms: `m_bIsTalking` (`+0x64c0`), a queued dialogue string (`+0x64ec`), the dialogue
	// partner handle (`+0xfe8`) and the bound speech scene (`+0x6554`). This runtime carries one
	// session bit for the last three and a talk-end stamp for the first, which is the reading
	// `ElysiumNpcThinkCadence.cpp`'s `ShouldThinkFrequently` already made; it is repeated here as a
	// function rather than a second reading so the two cannot drift.
	// ---------------------------------------------------------------------------------------
	bool SoundsIsInDialog(const FElysiumNpc& Npc)
	{
		const double Now = Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
		return Npc.Dialogue.bInDialog || Npc.IsTalking(Now);
	}

	// `CBaseCombatCharacter::IsUnconscious` (`0x10341aa0`), whose whole body past its VPROF scope is
	// `return (m_iMiscFlags & 1) != 0`. Bit 0 of `m_iMiscFlags` is the name table's `Unconscious`
	// (`Substrate/ElysiumMiscFlags.h`).
	bool SoundsIsUnconscious(const FElysiumNpc& Npc)
	{
		return ElysiumMiscFlags::Has(Npc.MiscFlags, 0x1u);
	}

}

// ==================================================================================================
// The species vocalization table
// ==================================================================================================

namespace
{
	// --- CNPC_VSabbatLeader (`0x103a6ab0` precaches them) ---------------------------------------
	//
	// `0x1064c480`, seven entries, `RandomInt(0, 6)`; and `0x1064c49c`, three entries,
	// `RandomInt(0, 2)`. The two tables are contiguous in `.rdata`, which is why the footstep
	// draw's upper bound and the attack table's base agree.
	const TCHAR* const GSoundsAndreiSteps[] = {
		TEXT("character/monster/andrei_transformed/step1.wav"),
		TEXT("character/monster/andrei_transformed/step2.wav"),
		TEXT("character/monster/andrei_transformed/step3.wav"),
		TEXT("character/monster/andrei_transformed/step4.wav"),
		TEXT("character/monster/andrei_transformed/step5.wav"),
		TEXT("character/monster/andrei_transformed/step6.wav"),
		TEXT("character/monster/andrei_transformed/step7.wav"),
	};
	const TCHAR* const GSoundsAndreiExertHeavy[] = {
		TEXT("character/monster/andrei_transformed/exert_heavy_1.wav"),
		TEXT("character/monster/andrei_transformed/exert_heavy_2.wav"),
		TEXT("character/monster/andrei_transformed/exert_heavy_3.wav"),
	};

	// One `Mute` row: `CNPC_VCamera`'s nineteen empty overrides, each one byte of `RET`.
	// `CNPC_VCameraSecurity` derives from `CNPC_VCamera` and inherits every one of them, which is
	// why there are nineteen rows here and not thirty-eight.
	constexpr FElysiumNpc::FVocalization SoundsMuteRow(int32 Slot, const TCHAR* Address)
	{
		return FElysiumNpc::FVocalization{ TEXT("CNPC_VCamera"), Slot, Address,
			FElysiumNpc::EVocalization::Mute, nullptr, 0, nullptr, 1.0f, 0.8f, GSoundsChanVoice, false,
			false };
	}

	// One wav-pool row.
	constexpr FElysiumNpc::FVocalization SoundsWavRow(const TCHAR* Class, int32 Slot, const TCHAR* Address,
		const TCHAR* const* Wavs, int32 Count, float Volume, int32 Channel, bool bGated)
	{
		return FElysiumNpc::FVocalization{ Class, Slot, Address,
			FElysiumNpc::EVocalization::WavPool, Wavs, Count, nullptr, Volume, 0.8f, Channel,
			bGated, false };
	}

	const FElysiumNpc::FVocalization GSoundsVocalizations[] =
	{
		// --- CNPC_VCamera: nineteen empty overrides ---------------------------------------------
		SoundsMuteRow(488, TEXT("0x103680b0")),   // DeathSound
		SoundsMuteRow(489, TEXT("0x103680d0")),   // AlertSound
		SoundsMuteRow(490, TEXT("0x103680f0")),   // IdleSound
		SoundsMuteRow(491, TEXT("0x10368110")),   // PainSound
		SoundsMuteRow(492, TEXT("0x10368130")),   // FearSound
		SoundsMuteRow(493, TEXT("0x10368150")),   // LostEnemySound
		SoundsMuteRow(494, TEXT("0x10368170")),   // FoundEnemySound
		SoundsMuteRow(495, TEXT("0x10368190")),   // SurprisedSound
		SoundsMuteRow(496, TEXT("0x103681b0")),   // TargetAcquiredSound
		SoundsMuteRow(498, TEXT("0x103681f0")),   // FleeSound
		SoundsMuteRow(499, TEXT("0x10368210")),   // IdleAgitatedSound
		SoundsMuteRow(500, TEXT("0x10368230")),   // ExertHvySound
		SoundsMuteRow(501, TEXT("0x10368250")),   // ExertLightSound
		SoundsMuteRow(502, TEXT("0x10368270")),   // RiledSound
		SoundsMuteRow(503, TEXT("0x10368290")),   // ComfortSound
		SoundsMuteRow(504, TEXT("0x103682b0")),   // UpsetSound
		SoundsMuteRow(505, TEXT("0x103682d0")),   // TargetGiveUpSound
		SoundsMuteRow(507, TEXT("0x10368310")),   // FloatSound
		// `0x10368330` is three bytes rather than one: slot 508 `SpeakSentence(int)` still has to
		// pop its argument. Same answer — the camera never speaks a scripted sentence.
		SoundsMuteRow(508, TEXT("0x10368330")),   // SpeakSentence(int)

		// --- CNPC_VSabbatLeader: two hooks on CHAN_BODY, both inside a VPROF scope ---------------
		// `0x103aa5e0` slot 620 FootstepSound: `RandomInt(0, 6)`, volume 1.0, CHAN_BODY.
		// NOT an animation event — `ElysiumFootsteps.cpp` records why this class is deliberately
		// absent from the footstep species table: retail drives it from the schedule tasks
		// `TASK_VSABBATLEADER_PLAY_FOOTSTEP_SOUND` / `..._STOP_FOOTSTEP_SOUND`, which are unbuilt.
		// This row is the SOUND that task will play; the task remains the Schedule family's.
		SoundsWavRow(TEXT("CNPC_VSabbatLeader"), 620, TEXT("0x103aa5e0"), GSoundsAndreiSteps,
			UE_ARRAY_COUNT(GSoundsAndreiSteps), 1.0f, GSoundsChanBody, false),
		// `0x103aa7a0` slot 621 AttackSound: `RandomInt(0, 2)`, volume 1.0, CHAN_BODY.
		SoundsWavRow(TEXT("CNPC_VSabbatLeader"), 621, TEXT("0x103aa7a0"), GSoundsAndreiExertHeavy,
			UE_ARRAY_COUNT(GSoundsAndreiExertHeavy), 1.0f, GSoundsChanBody, false),

		// --- CNPC_VTzimisce: two SENTENCE hooks -------------------------------------------------
		//
		// Both take their volume, soundlevel and pitch from three ConVars shared by the whole
		// Tzimisce sound family: `tzimisce_voice_volume` "1" (`0x1093cebc`), `tzimisce_voice_attn`
		// "65" dB (`0x1093cfdc`) and `tzimisce_voice_pitch` "100" (`0x1093cf94`). The row carries
		// the ordinary pair because the sentence seam refuses before the numbers matter; the day it
		// emits, it reads `ElysiumNpcTunables::ConVar*` for all three.
		//
		// `0x103b9380` slot 490 IdleSound: gated on `FOkToMakeSound()`, then `"SPI_IDLE"`.
		FElysiumNpc::FVocalization{ TEXT("CNPC_VTzimisce"), 490, TEXT("0x103b9380"),
			FElysiumNpc::EVocalization::Sentence, nullptr, 0, TEXT("SPI_IDLE"), 1.0f, 0.8f, GSoundsChanVoice,
			/*bGated*/ true, /*bCallsJustMadeSound*/ false },
		// `0x103b9500` slot 491 PainSound: gated on `FOkToMakeSound()`, then `"SPI_TAKE_DAMAGE"`
		// AND `JustMadeSound()` (vtable `+0x79c`) — the only vocalization in the family that
		// re-arms the sound clock itself.
		//
		// UNRECOVERED TAIL: outside the gate the body always calls `0x103b9f90(this, 1, 1.0)`,
		// `CBaseCombatCharacter::SetExpression(ExpressionTable[1], 0.0, 0.15, max(1.0 - k, floor),
		// 0.15, 1.0)` — the pain FACIAL expression. This runtime has no `SetExpression` (the script
		// API lists it as a stub), so the expression half is not ported; it belongs to the facial /
		// anim surface, not to the sound one.
		FElysiumNpc::FVocalization{ TEXT("CNPC_VTzimisce"), 491, TEXT("0x103b9500"),
			FElysiumNpc::EVocalization::Sentence, nullptr, 0, TEXT("SPI_TAKE_DAMAGE"), 1.0f, 0.8f, GSoundsChanVoice,
			/*bGated*/ true, /*bCallsJustMadeSound*/ true },
	};
}

const FElysiumNpc::FVocalization* FElysiumNpc::Vocalizations(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GSoundsVocalizations);
	return GSoundsVocalizations;
}

const FElysiumNpc::FVocalization* FElysiumNpc::VocalizationFor(const TCHAR* RetailClassName,
	int32 Slot)
{
	// The class's OWN row. A subclass inherits its base's rows through the C++ override the row
	// belongs to (story 5 step 3: `CNPC_VCameraSecurity` inherits `FElysiumNpcCamera`'s nineteen), so
	// no base-chain walk is needed here.
	if (RetailClassName == nullptr)
	{
		return nullptr;
	}
	for (const FVocalization& Row : GSoundsVocalizations)
	{
		if (Row.Slot == Slot && FCString::Strcmp(Row.RetailClass, RetailClassName) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

bool FElysiumNpc::SpeciesVocalize(const TCHAR* SpeciesClass, int32 Slot)
{
	// The body of a species class's vocalization override (story 5 step 3): the class's own row of
	// `GSoundsVocalizations`. Before step 3 no production path reached this table.
	const FVocalization* Row = VocalizationFor(SpeciesClass, Slot);
	if (Row == nullptr)
	{
		// No row: nothing this class's override can say.
		return false;
	}

	// `if (!FOkToMakeSound()) return;` — the one arm the gated overrides open with. Claimed
	// either way: retail's body returned, it did not fall through to the base.
	if (Row->bGatedByFOkToMakeSound && !FOkToMakeSound())
	{
		return true;
	}

	switch (Row->Kind)
	{
	case EVocalization::Mute:
		// `RET`. The species is silent at this hook and the base body must not run.
		break;

	case EVocalization::WavPool:
	{
		if (Row->WavCount <= 0 || Row->Wavs == nullptr)
		{
			break;
		}
		// `RandomInt(0, N)`, inclusive at both ends, exactly as `DAT_1070b244` slot 2 is. The
		// Sabbat leader's 0..6 draw is the one this stream's own comment already names
		// (`EElysiumRngStream::Footsteps`); every other row is an NPC-think decision and draws
		// from the schedule stream beside the rest of the idle branch.
		const EElysiumRngStream Which = Row->Channel == GSoundsChanBody
			? EElysiumRngStream::Footsteps : EElysiumRngStream::NpcSchedule;
		const int32 Index = ElysiumRng::Stream(Which).RandRange(0, Row->WavCount - 1);

		IElysiumAudio* Audio = World != nullptr ? World->Audio() : nullptr;
		if (Audio != nullptr)
		{
			// `EmitSound(CPASAttenuationFilter(GetSoundEmissionOrigin(), 0.8), entindex(), channel,
			// wav, volume, 0.8, 0, 100, NULL, NULL, true, 0)`. The PAS filter itself is a recipient
			// cull that single-player never runs (`docs/vtmb/footsteps.md` §3.5), so the port emits
			// the sound and does not build a filter.
			FElysiumBodySound Sound;
			Sound.Rel = Row->Wavs[Index];
			Sound.Volume = Row->Volume;
			Sound.SoundLevelDb = GSoundsSpeciesSoundLevelDb;
			Sound.Pitch = GSoundsSpeciesPitch;
			Sound.Channel = static_cast<EElysiumSoundChannel>(Row->Channel);
			Audio->PlayBodySound(Handle, Sound);
		}
		break;
	}

	case EVocalization::Sentence:
		NpcKernelSoundsShared::SoundsPlaySentenceGroup(*this, Row->Sentence, Row->Volume, GSoundsSpeciesSoundLevelDb, 0, 100);
		break;
	}

	// `CNPC_VTzimisce::vfunc491`'s tail inside the gate: `JustMadeSound()` through vtable `+0x79c`.
	if (Row->bCallsJustMadeSound)
	{
		JustMadeSound();
	}
	return true;
}

// ==================================================================================================
// Slot 474 `GetBestSound` / slot 475 `GetBestScent` — the sound and scent memory readers
// ==================================================================================================

// `CAI_BaseNPCTroika::FUN_102b4520` (`0x102b4520`) — slot 474 on the Troika line, and the whole
// body: `return &this->m_BestSound;` (`+0x60b0`). Not the senses object's live answer; the
// COMMITTED record, which `CommitBestSound` writes one sweep earlier.
void* FElysiumNpc::GetBestSound()
{
	return &Senses.Memory.BestSound;
}

// ==================================================================================================
// Slot 484 `PlaySentence` / slot 485 `PlayScriptedSentence`
// ==================================================================================================

// ==================================================================================================
// Slot 486 `FOkToMakeSound` / slot 487 `JustMadeSound` — the "may I make a sound now" clock
// ==================================================================================================

// `CAI_BaseNPCTroika::FUN_102b4c10` (`0x102b4c10`), slot 486 on the Troika line. Twenty-four bytes,
// and it does NOT call the base: `return !IsInDialog();`. The whole sound-wait clock, the squad
// partner's copy of it and `SF_NPC_GAG` are dropped by this override — a Troika NPC's only reason to
// stay quiet is that it is talking.
bool FElysiumNpc::FOkToMakeSound()
{
	return !SoundsIsInDialog(*this);
}

void FElysiumNpc::JustMadeSound()
{
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	BaseMemory.SoundWaitTime = NpcKernelSoundsShared::SoundsCurTime(*this)
		+ Stream.FRandRange(GSoundsTroikaSoundWaitMin, GSoundsTroikaSoundWaitMax);

	if (BaseScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)
	{
		// SEAM: the squad object's own `m_flSoundWaitTime` (`+0x60`). Retail draws a second
		// `RandomFloat(0.25, 0.75)` here; with no squad layer there is nothing to write it to, and
		// consuming the draw anyway would walk the stream off retail's position for every NPC.
	}
}

// ==================================================================================================
// Slot 488 `DeathSound` / slot 506 `vfunc506` — two sound hooks with a species arm in front
// ==================================================================================================
//
// Both were generated stub bodies until story 29c-1 needed a species prologue on them. They live in
// THIS family's file because 497 and 506 are the same sound-hook band and 488 is the death
// vocalization — the concern is sound, not dispatch — but the Troika-line bodies behind them
// (`0x10293ec0` and `0x10294e70`) are layer 14 and belong to story **29d**, family **Sounds10**.
// Story 29d ported them: each definition below is the SPECIES PROLOGUE 29c-1 put here, and the arm
// it falls through to is `TroikaDeathSound()` / `TroikaSlot506()` in
// `Substrate/ElysiumNpcSounds10.cpp`, beside the other fifteen hooks of the same band.

// slot 488 0x10293ec0 `void DeathSound()`
void FElysiumNpc::DeathSound()
{
	// `CNPC_VTzimisce` overrides this slot (`FElysiumNpcTzimisce`, `0x103b92a0`): it fires
	// `SPI_DIES` at the script host and tail-calls slot 487, so a Tzimisce never reaches the base
	// death sound; that tail call is the one part of its body still unported.
	TroikaDeathSound();
}

// slot 506 0x10294e70 `void vfunc506()`
void FElysiumNpc::Slot506()
{
	// `CNPC_VCamera` (and `CNPC_VCameraSecurity` under it) overrides this slot with an EMPTY body
	// (`FElysiumNpcCamera`, `0x103682f0`), so a camera never reaches the Troika body below.
	TroikaSlot506();
}

// ==================================================================================================
// Slot 509 `ShouldPlayIdleSound` / slot 510 `ShouldPlayFloatSound` — the two idle vocalization gates
// ==================================================================================================

// `CAI_BaseNPCTroika::FUN_10294040` (`0x10294040`), slot 509 on the Troika line. Twenty-four bytes:
// `return IsInDialog() ? false : CAI_BaseNPC::ShouldPlayIdleSound();` — a talking body never rolls,
// and every other body takes the base decision unchanged.
bool FElysiumNpc::ShouldPlayIdleSound()
{
	// Story 29d, family **SpeciesAnim10**: `CNPC_VZombie#509` is `0x103e0fa0`, which REPLACES this
	// body wholesale — no dialog refusal, no state test, no `SF_NPC_GAG` — and adds a 1-in-21 arm on
	// `SCHED_TROIKA_COMFORT` that skips the float-sound gate. It is `FElysiumNpcZombie`'s override
	// (story 5 step 3), whose body is in `ElysiumNpcZombie.cpp` (story 5 step 4).
	if (SoundsIsInDialog(*this))
	{
		return false;
	}
	return FElysiumNpcBase::ShouldPlayIdleSound();
}

// `CAI_BaseNPCTroika::FUN_10294070` (`0x10294070`), slot 510 on the Troika line. Eight gates in the
// listing's order, then the base body:
//
//   1. `IsInDialog()`                                                 → false   (`10294075`)
//   2. a LIVE grapple: `+0x1538` resolves AND `+0x153c != -1`    → false   (`10294082`)
//   3. `IsUnconscious()`                                              → false   (`102940be`)
//   4. `m_bfAINPCFlags & 0x20000` (`SLEEPING`)                   → false   (`102940cb`)
//   5. `m_IdealNPCState != NPC_STATE_IDLE(1)`                    → false   (`102940db`)
//   6. `m_NPCState != NPC_STATE_IDLE(1)`                         → false   (`102940ee`)
//   7. `m_hClosestPlayer` (`+0x628c`) not live                   → false   (`102940fa`)
//   8. that PLAYER's own `+0xfe8` dialogue partner is live       → false   (`10294161`)
//
// then the `Float_Sound_Info` row-0 distance (50.0 Source units), lazily cached into
// `_DAT_1092483c` behind the magic-static guard `DAT_10924d1c`, and
// `m_flPlayerDist (+0x6264) <= threshold` (`FCOMP` + `AND 0x4100`, so equality passes) before the
// tail call into the base body.
//
// NOTE the two IDLE tests: both the ideal and the current state must be IDLE. The one-line walk
// recorded ALERT for both, which is the wrong ordinal — `0x1027e660`'s name table orders retail's
// enum None, Idle, Combat, Alert.
//
// SEAM: gate 2 reads `+0x1538` / `+0x153c`, the grapple partner and role
// (`Public/ElysiumMoveSolve.h` spells the identical predicate for the player). An NPC in this
// runtime carries the pair through `EnterGrappleState`; the test is the same one the move solver
// makes.
bool FElysiumNpc::ShouldPlayFloatSound()
{
	// `CNPC_VZombie` overrides this slot (`FElysiumNpcZombie`, `0x103e1080`): it writes
	// `m_iFloatSoundFrequency = 9` before any gate, runs its own gates with no IDLE test and tails
	// DIRECTLY into the CAI_BaseNPC body `FElysiumNpcBase::ShouldPlayFloatSound`, never this one.
	if (SoundsIsInDialog(*this))
	{
		return false;
	}
	if (IsGrappling())
	{
		return false;
	}
	if (SoundsIsUnconscious(*this))
	{
		return false;
	}
	if (NpcFlags.Has(GSoundsFloatSoundBlockingFlag))
	{
		return false;
	}
	if (NpcKernelSoundsShared::SoundsRetailNpcState(Mind.IdealState()) != NpcKernelSoundsShared::GSoundsNpcStateIdle)
	{
		return false;
	}
	if (NpcKernelSoundsShared::SoundsRetailNpcState(Mind.State()) != NpcKernelSoundsShared::GSoundsNpcStateIdle)
	{
		return false;
	}
	const FElysiumNpcMemory& Memory = Senses.Memory;
	if (!Memory.ClosestPlayer.IsSet())
	{
		return false;
	}
	// Gate 8, the PLAYER's own dialogue partner: an NPC does not float while the player it is
	// nearest to is in a conversation with anyone. The port's stand-in is the open session itself.
	if (World != nullptr && World->GetOpenDialogOwner().IsSet())
	{
		return false;
	}

	float ThresholdUnits = GSoundsFloatSoundDistanceFallbackUnits;
	if (World != nullptr)
	{
		if (UElysiumSessionSubsystem* GameState = World->GetGameState())
		{
			if (UElysiumRulebookSubsystem* Rules = GameState->Rulebook())
			{
				if (const FElysiumRuleTable* Table = Rules->Rules().Table(GSoundsFloatSoundTable))
				{
					ThresholdUnits = Table->Lookup(GSoundsFloatSoundDistanceRow,
						GSoundsFloatSoundDistanceFallbackUnits);
				}
			}
		}
	}
	// `m_flPlayerDist` is Source units; the port's cache is centimetres, and the one conversion
	// happens here rather than on the authored number.
	const float PlayerDistUnits = Memory.ClosestPlayerDistanceCm / ElysiumMove::U;
	if (PlayerDistUnits > ThresholdUnits)
	{
		return false;
	}
	return FElysiumNpcBase::ShouldPlayFloatSound();
}

// ==================================================================================================
// Slot 511 `StopLoopingSounds`
// ==================================================================================================

