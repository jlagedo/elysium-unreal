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

	// `SF_NPC_GAG`, `m_spawnflags & 2` (`0x1027a5c0` at `1027a5ea`). A gagged NPC is silent outside
	// combat and vocal inside it.
	constexpr int32 GSoundsSpawnFlagGag = 0x2;

	// Retail's `NPC_STATE` ordinals, as the name table at `0x1027e660` orders them:
	// None(0) Idle(1) Combat(2) Alert(3) Script(4) … The port's `EElysiumNpcState` is a different
	// enum with a different order, so a body that tests a retail ordinal converts.
	constexpr int32 GSoundsNpcStateIdle = 1;
	constexpr int32 GSoundsNpcStateCombat = 2;
	constexpr int32 GSoundsNpcStateAlert = 3;

	// `CAI_BaseNPC::JustMadeSound` `0x1027a640` draws `RandomFloat(0x3fc00000, 0x40000000)`.
	constexpr float GSoundsBaseSoundWaitMin = 1.5f;
	constexpr float GSoundsBaseSoundWaitMax = 2.0f;
	// `CAI_BaseNPCTroika::JustMadeSound` `0x102b4c40` draws `RandomFloat(0x3e800000, 0x3f400000)`.
	constexpr float GSoundsTroikaSoundWaitMin = 0.25f;
	constexpr float GSoundsTroikaSoundWaitMax = 0.75f;
	// `CNPC_VTzimisce::vfunc487` `0x103b9f10` draws `RandomFloat(0x3f000000, 0x3f400000)` — and,
	// unlike both bodies above, writes NO squad copy.
	constexpr float GSoundsTzimisceSoundWaitMin = 0.5f;
	constexpr float GSoundsTzimisceSoundWaitMax = 0.75f;

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

	// `gpGlobals->curtime` (`DAT_1070b228 + 0xc`).
	double SoundsCurTime(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	// The NPC's current `m_NPCState`, in RETAIL's ordinals.
	int32 SoundsRetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return GSoundsNpcStateIdle;
		case EElysiumNpcState::Combat:   return GSoundsNpcStateCombat;
		case EElysiumNpcState::Alert:    return GSoundsNpcStateAlert;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}

	// `CBaseCombatCharacter::IsUnconscious` (`0x10341aa0`), whose whole body past its VPROF scope is
	// `return (m_iMiscFlags & 1) != 0`. Bit 0 of `m_iMiscFlags` is the name table's `Unconscious`
	// (`Substrate/ElysiumMiscFlags.h`).
	bool SoundsIsUnconscious(const FElysiumNpc& Npc)
	{
		return ElysiumMiscFlags::Has(Npc.MiscFlags, 0x1u);
	}

	// SEAM: `SENTENCEG_PlayRndSz(edict_t*, const char* group, float volume, soundlevel_t, int flags,
	// int pitch)` — retail `0x101aeb60`, reached from `PlaySentence`'s ordinary arm and from both
	// `CNPC_VTzimisce` sound hooks.
	//
	// It resolves a SENTENCE GROUP name out of the engine's loaded `sentences.txt`, picks one member
	// of the group, resolves the caption text, and emits the chosen sentence by index. This runtime
	// carries no sentence table at all — nothing loads `sentences.txt`, nothing maps a group name to
	// members, and `IEngineSound::EmitSentenceByIndex` has no counterpart on `IElysiumAudio` — so
	// the seam is asked and refuses with retail's own "no such sentence" answer, -1.
	//
	// UNRECOVERED beyond that: whether VtMB ships `sentences.txt` at all, and what the `SPI_*` group
	// names the Tzimisce hooks pass resolve to. Both belong to the speech story, not to this one.
	int32 SoundsPlaySentenceGroup(const FElysiumNpc& Npc, const TCHAR* Group, float Volume,
		int32 SoundLevel, int32 Flags, int32 Pitch)
	{
		static bool bReported = false;
		if (!bReported)
		{
			bReported = true;
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("%s SENTENCEG_PlayRndSz(\"%s\", vol=%.2f, lvl=%d, flags=%d, pitch=%d) refused: "
					"this runtime carries no sentence table (retail 0x101aeb60)"),
				*Npc.DebugString(), Group != nullptr ? Group : TEXT(""), Volume, SoundLevel, Flags,
				Pitch);
		}
		return -1;
	}

	// `CAI_BaseNPC::PlaySentence`'s raw-wave arm (`10278e60`..`10278f1f`): `SENTENCEG_Lookup(name)`
	// then `EmitSentenceByIndex(CPASAttenuationFilter, entindex, CHAN_VOICE, index, volume,
	// soundlevel, 0, 100)`. Same seam, same refusal, and the return value IS the looked-up index, so
	// a refusal is -1 here too.
	int32 SoundsEmitSentenceByName(const FElysiumNpc& Npc, const TCHAR* Name, float Volume,
		int32 SoundLevel)
	{
		return SoundsPlaySentenceGroup(Npc, Name, Volume, SoundLevel, 0, 100);
	}
}

// ==================================================================================================
// The species vocalization table
// ==================================================================================================

namespace
{
	// --- CGeneric_NPC (`0x10359f70` precaches them) and CGenericSabbat_NPC (`0x1035b5d0`) --------
	//
	// The two classes keep SEPARATE pointer tables (`0x10629a18`… and `0x1062a004`…) holding the
	// same four wav paths, so the rows below name the same strings twice on purpose: a reader
	// checking either class against `Precache` finds its own table.
	const TCHAR* const GSoundsMetropoliceAlert[] = { TEXT("npc/metropolice/alert1.wav") };
	const TCHAR* const GSoundsMetropoliceDie[] = { TEXT("npc/metropolice/die1.wav") };
	const TCHAR* const GSoundsMetropoliceSurprise[] = { TEXT("npc/metropolice/surprise1.wav") };
	// `PTR_s_npc_citizen_pain1_wav` + 0x10 bytes, i.e. four entries; `RandomInt(0, 3)`.
	const TCHAR* const GSoundsCitizenPain[] = {
		TEXT("npc/citizen/pain1.wav"),
		TEXT("npc/citizen/pain2.wav"),
		TEXT("npc/citizen/pain3.wav"),
		TEXT("npc/citizen/pain4.wav"),
	};

	// --- CNPC_VTest (`0x103b41e0` precaches them) ------------------------------------------------
	//
	// The symbol names read `character_npc_test_*`; the strings themselves are
	// `character/npc/test/*.wav` (`0x106523c4` is the death one, verbatim).
	const TCHAR* const GSoundsTestDeath[] = { TEXT("character/npc/test/death1.wav") };
	const TCHAR* const GSoundsTestAlert[] = { TEXT("character/npc/test/alert1.wav") };
	const TCHAR* const GSoundsTestIdle[] = { TEXT("character/npc/test/idle1.wav") };
	const TCHAR* const GSoundsTestPain[] = {
		TEXT("character/npc/test/pain1.wav"),
		TEXT("character/npc/test/pain2.wav"),
		TEXT("character/npc/test/pain3.wav"),
		TEXT("character/npc/test/pain4.wav"),
	};
	const TCHAR* const GSoundsTestFear[] = { TEXT("character/npc/test/fear1.wav") };
	const TCHAR* const GSoundsTestLostEnemy[] = { TEXT("character/npc/test/lostenemy1.wav") };
	const TCHAR* const GSoundsTestFoundEnemy[] = { TEXT("character/npc/test/foundenemy1.wav") };
	const TCHAR* const GSoundsTestSurprise[] = { TEXT("character/npc/test/surprise1.wav") };

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
		// --- CGeneric_NPC ---------------------------------------------------------------------
		// `0x1035a500` slot 488 DeathSound: `RandomInt(0, 0)`, volume 0.5 (`0x3f000000`).
		SoundsWavRow(TEXT("CGeneric_NPC"), 488, TEXT("0x1035a500"), GSoundsMetropoliceDie,
			UE_ARRAY_COUNT(GSoundsMetropoliceDie), 0.5f, GSoundsChanVoice, false),
		// `0x1035a390` slot 489 AlertSound: `RandomInt(0, 0)`, volume 1.0.
		SoundsWavRow(TEXT("CGeneric_NPC"), 489, TEXT("0x1035a390"), GSoundsMetropoliceAlert,
			UE_ARRAY_COUNT(GSoundsMetropoliceAlert), 1.0f, GSoundsChanVoice, false),
		// `0x1035a670` slot 491 PainSound: `RandomInt(0, 3)`, volume 1.0.
		SoundsWavRow(TEXT("CGeneric_NPC"), 491, TEXT("0x1035a670"), GSoundsCitizenPain,
			UE_ARRAY_COUNT(GSoundsCitizenPain), 1.0f, GSoundsChanVoice, false),
		// `0x1035a220` slot 495 SurprisedSound: `RandomInt(0, 0)`, volume 1.0.
		SoundsWavRow(TEXT("CGeneric_NPC"), 495, TEXT("0x1035a220"), GSoundsMetropoliceSurprise,
			UE_ARRAY_COUNT(GSoundsMetropoliceSurprise), 1.0f, GSoundsChanVoice, false),

		// --- CGenericSabbat_NPC: the same four hooks, its own copies of the same four wavs ------
		SoundsWavRow(TEXT("CGenericSabbat_NPC"), 488, TEXT("0x1035bb70"), GSoundsMetropoliceDie,
			UE_ARRAY_COUNT(GSoundsMetropoliceDie), 0.5f, GSoundsChanVoice, false),
		SoundsWavRow(TEXT("CGenericSabbat_NPC"), 489, TEXT("0x1035ba00"), GSoundsMetropoliceAlert,
			UE_ARRAY_COUNT(GSoundsMetropoliceAlert), 1.0f, GSoundsChanVoice, false),
		SoundsWavRow(TEXT("CGenericSabbat_NPC"), 491, TEXT("0x1035bce0"), GSoundsCitizenPain,
			UE_ARRAY_COUNT(GSoundsCitizenPain), 1.0f, GSoundsChanVoice, false),
		SoundsWavRow(TEXT("CGenericSabbat_NPC"), 495, TEXT("0x1035b890"), GSoundsMetropoliceSurprise,
			UE_ARRAY_COUNT(GSoundsMetropoliceSurprise), 1.0f, GSoundsChanVoice, false),

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

		// --- CNPC_VTest: eight hooks ------------------------------------------------------------
		SoundsWavRow(TEXT("CNPC_VTest"), 488, TEXT("0x103b4320"), GSoundsTestDeath,
			UE_ARRAY_COUNT(GSoundsTestDeath), 0.5f, GSoundsChanVoice, false),
		SoundsWavRow(TEXT("CNPC_VTest"), 489, TEXT("0x103b4490"), GSoundsTestAlert,
			UE_ARRAY_COUNT(GSoundsTestAlert), 1.0f, GSoundsChanVoice, false),
		// Slot 490 is the ONLY CNPC_VTest hook that opens with `if (!FOkToMakeSound()) return;`
		// (`103b4609`, vtable `+0x798`) — the idle vocalization is rate-limited, the reactive ones
		// are not.
		SoundsWavRow(TEXT("CNPC_VTest"), 490, TEXT("0x103b4600"), GSoundsTestIdle,
			UE_ARRAY_COUNT(GSoundsTestIdle), 1.0f, GSoundsChanVoice, true),
		SoundsWavRow(TEXT("CNPC_VTest"), 491, TEXT("0x103b4780"), GSoundsTestPain,
			UE_ARRAY_COUNT(GSoundsTestPain), 1.0f, GSoundsChanVoice, false),
		SoundsWavRow(TEXT("CNPC_VTest"), 492, TEXT("0x103b48f0"), GSoundsTestFear,
			UE_ARRAY_COUNT(GSoundsTestFear), 1.0f, GSoundsChanVoice, false),
		SoundsWavRow(TEXT("CNPC_VTest"), 493, TEXT("0x103b4a60"), GSoundsTestLostEnemy,
			UE_ARRAY_COUNT(GSoundsTestLostEnemy), 1.0f, GSoundsChanVoice, false),
		SoundsWavRow(TEXT("CNPC_VTest"), 494, TEXT("0x103b4bd0"), GSoundsTestFoundEnemy,
			UE_ARRAY_COUNT(GSoundsTestFoundEnemy), 1.0f, GSoundsChanVoice, false),
		SoundsWavRow(TEXT("CNPC_VTest"), 495, TEXT("0x103b4d40"), GSoundsTestSurprise,
			UE_ARRAY_COUNT(GSoundsTestSurprise), 1.0f, GSoundsChanVoice, false),

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
	// The vtable's own rule, which `ElysiumNpcKernelClass::OverrideOf` makes over the census: walk
	// the base chain upward and stop at the FIRST class that fills the slot. A name with no census
	// row still answers its own table rows, so a family class the census does not carry can be
	// exercised by name.
	if (RetailClassName == nullptr)
	{
		return nullptr;
	}
	const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(RetailClassName);
	const TCHAR* Walk = RetailClassName;
	while (Walk != nullptr && *Walk != TEXT('\0'))
	{
		for (const FVocalization& Row : GSoundsVocalizations)
		{
			if (Row.Slot == Slot && FCString::Strcmp(Row.RetailClass, Walk) == 0)
			{
				return &Row;
			}
		}
		Cls = ElysiumNpcKernelClass::Find(Walk);
		Walk = Cls != nullptr ? Cls->Base : nullptr;
	}
	return nullptr;
}

const FElysiumNpc::FVocalization* FElysiumNpc::VocalizationFor(int32 Slot) const
{
	const FElysiumNpcClass* Cls = RetailClass();
	return Cls != nullptr ? VocalizationFor(Cls->Name, Slot) : nullptr;
}

bool FElysiumNpc::EmitVocalization(int32 Slot)
{
	const FVocalization* Row = VocalizationFor(Slot);
	if (Row == nullptr)
	{
		// No species override: the Troika-line body at this slot runs (story 29d).
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
		SoundsPlaySentenceGroup(*this, Row->Sentence, Row->Volume, GSoundsSpeciesSoundLevelDb, 0, 100);
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

// `CAI_BaseNPC::FUN_1026aef0` (`0x1026aef0`) — slot 474's BASE body, replaced on the Troika line
// and therefore unreachable from any class this port stands. `m_pSenses (+0x5cdc)->GetClosestSound(
// /*bScent*/ 0)` (`PUSH 0x0` at `1026aef7`), with a dev warning on null.
//
// SEAM: `CAI_Senses::GetClosestSound` (`0x103104f0`) walks the senses object's retained sound list
// and answers the NEAREST audible member. `FElysiumNpcSenses` retains one record per CSound family
// and commits a winner into `Memory.BestSound`; it has no "closest of the live list" accessor,
// because nothing in this runtime dispatches the base body. The seam answers null and warns exactly
// as retail does.
void* FElysiumNpc::BaseGetBestSound()
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
void* FElysiumNpc::GetBestScent()
{
	void* const Scent = nullptr;
	if (Scent == nullptr)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s Warning: NULL Return from GetBestScent"),
			*DebugString());
	}
	return Scent;
}

// ==================================================================================================
// Slot 484 `PlaySentence` / slot 485 `PlayScriptedSentence`
// ==================================================================================================

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
int32 FElysiumNpc::PlaySentence(const TCHAR* Sentence, float /*Delay*/, float Volume,
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
	return SoundsPlaySentenceGroup(*this, Sentence, Volume, SoundLevel, 0, 100);
}

// `CAI_BaseNPC::FUN_10279000` (`0x10279000`), slot 485, and the whole body is the forward: it
// tail-calls slot 484 through vtable `+0x790` with `(name, delay, volume, soundlevel, NULL)`,
// DROPPING both of its own extra arguments — the bool and the `CBaseEntity*` listener — and
// hardcoding a null listener rather than passing the one it was given.
int32 FElysiumNpc::PlayScriptedSentence(const TCHAR* Sentence, float Delay, float Volume,
	int32 SoundLevel, bool /*bConcurrent*/, FElysiumEntity* /*Listener*/)
{
	return PlaySentence(Sentence, Delay, Volume, SoundLevel, nullptr);
}

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
bool FElysiumNpc::BaseFOkToMakeSound() const
{
	const double Now = SoundsCurTime(*this);
	if (Now <= Senses.Memory.SoundWaitTime)
	{
		return false;
	}
	if (ScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)
	{
		// The squad's own `m_flSoundWaitTime` at `+0x60`. Unreachable while `ConnectedSquad()`
		// answers null; the field it stands for is named so the squad layer knows what to hand back.
		return false;
	}
	if ((SpawnFlags & GSoundsSpawnFlagGag) != 0 && SoundsRetailNpcState(Mind.State()) != GSoundsNpcStateCombat)
	{
		return false;
	}
	return true;
}

// `CAI_BaseNPCTroika::FUN_102b4c40` (`0x102b4c40`), slot 487 on the Troika line: `m_flSoundWaitTime
// = curtime + RandomFloat(0.25, 0.75)`, and — when the squad is connected — a SECOND, INDEPENDENT
// draw written to the squad's own copy at `+0x60`. Retail draws twice; it does not reuse the first
// number, which is why the two clocks drift apart.
//
// `CNPC_VTzimisce::vfunc487` (`0x103b9f10`) is the one species override of this slot: the same
// formula with a 0.5–0.75 draw and NO squad half at all. It is a row here rather than a table entry
// because the species difference is two constants and one dropped arm, not a different behaviour.
void FElysiumNpc::JustMadeSound()
{
	const bool bTzimisce = IsRetailClass(TEXT("CNPC_VTzimisce"));
	const float Min = bTzimisce ? GSoundsTzimisceSoundWaitMin : GSoundsTroikaSoundWaitMin;
	const float Max = bTzimisce ? GSoundsTzimisceSoundWaitMax : GSoundsTroikaSoundWaitMax;

	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	Senses.Memory.SoundWaitTime = SoundsCurTime(*this) + Stream.FRandRange(Min, Max);

	if (bTzimisce)
	{
		// `0x103b9f10` is 41 bytes and ends at the write above. No squad copy.
		return;
	}
	if (ScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)
	{
		// SEAM: the squad object's own `m_flSoundWaitTime` (`+0x60`). Retail draws a second
		// `RandomFloat(0.25, 0.75)` here; with no squad layer there is nothing to write it to, and
		// consuming the draw anyway would walk the stream off retail's position for every NPC.
	}
}

// `CAI_BaseNPC::FUN_1027a640` (`0x1027a640`), slot 487's base body. Identical shape to the Troika
// one with a 1.5–2.0 draw. Not reached on the Troika line.
void FElysiumNpc::BaseJustMadeSound()
{
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	Senses.Memory.SoundWaitTime = SoundsCurTime(*this) + Stream.FRandRange(GSoundsBaseSoundWaitMin,
		GSoundsBaseSoundWaitMax);
	if (ScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)
	{
		// SEAM: the squad's own copy, as above.
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
// `Substrate/ElysiumNpcKernelSounds10.cpp`, beside the other fifteen hooks of the same band.

// slot 488 0x10293ec0 `void DeathSound()`
void FElysiumNpc::DeathSound()
{
	// The vtable dispatch first: `CNPC_VTzimisce` `0x103b92a0` (family **Species**) fires `SPI_DIES`
	// at the script host with three singleton-derived arguments and then TAIL-CALLS slot 487 — so a
	// Tzimisce never reaches the base death sound at all, and the tail call is the only part of that
	// body still unported (slot 487 is `JustMadeSound`, above, and is not this row).
	if (SpeciesDeathSound())
	{
		return;
	}
	TroikaDeathSound();
}

// slot 506 0x10294e70 `void vfunc506()`
void FElysiumNpc::Slot506()
{
	// The vtable dispatch first: `CNPC_VCamera` `0x103682f0` (and `CNPC_VCameraSecurity` under it) is
	// an EMPTY body — the other end of the same pair of sound hooks slot 497 carries — so a camera
	// makes none of whatever this hook plays and the Troika body below is not reached for one.
	if (SpeciesSlot506())
	{
		return;
	}
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
	// `SCHED_TROIKA_COMFORT` that skips the float-sound gate. The body is in
	// `ElysiumNpcKernelAnim10_2.cpp`; the dispatch is here so slot 509 stays one method.
	if (ShouldPlayIdleSoundZombieArm())
	{
		return ShouldPlayIdleSoundZombie();
	}
	if (SoundsIsInDialog(*this))
	{
		return false;
	}
	return BaseShouldPlayIdleSound();
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
bool FElysiumNpc::BaseShouldPlayIdleSound()
{
	const int32 State = SoundsRetailNpcState(Mind.State());
	if (State != GSoundsNpcStateIdle && State != GSoundsNpcStateAlert)
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
	// The vtable dispatch first: `CNPC_VZombie` `0x103e1080` (family **Species**) writes
	// `m_iFloatSoundFrequency = 9` before any gate, runs a different eight and tails into THIS body
	// on its accepting arm. The tail jump is direct, which `SpeciesShouldPlayFloatSound`'s dispatch
	// scope reproduces.
	bool bSpeciesAnswer = false;
	if (SpeciesShouldPlayFloatSound(bSpeciesAnswer))
	{
		return bSpeciesAnswer;
	}

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
	if (SoundsRetailNpcState(Mind.IdealState()) != GSoundsNpcStateIdle)
	{
		return false;
	}
	if (SoundsRetailNpcState(Mind.State()) != GSoundsNpcStateIdle)
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
	return BaseShouldPlayFloatSound();
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
bool FElysiumNpc::BaseShouldPlayFloatSound() const
{
	if (FloatSoundFrequency == GSoundsFloatFrequencyOff)
	{
		return false;
	}
	if (FloatSoundFrequency == GSoundsFloatFrequencyAlsoOff)
	{
		return false;
	}
	if (SoundsCurTime(*this) < NextFloatSoundTime)
	{
		return false;
	}
	return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, FloatSoundFrequency) == 0;
}

// ==================================================================================================
// Slot 511 `StopLoopingSounds`
// ==================================================================================================

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
// SPECIES ARM (story 29d, family **Sounds10**): `CNPC_Crow::vfunc511` (`0x10357800`), the whole body
// of which is ELEVEN bytes — `PUSH "NPC_Crow.Flap"; CALL thunk_FUN_101b0d80; RET`, i.e.
// `CBaseEntity::StopSound("NPC_Crow.Flap")`. It does NOT chain to the base: a crow's slot 511
// replaces the generic "stop everything this entity is playing" with a single named-script stop, so
// nothing the base body would have stopped is stopped for a crow.
void FElysiumNpc::StopLoopingSounds()
{
	if (IsRetailClass(TEXT("CNPC_Crow")))
	{
		StopNamedSound(TEXT("NPC_Crow.Flap"));
		return;
	}
	if (IElysiumAudio* Audio = World != nullptr ? World->Audio() : nullptr)
	{
		Audio->StopEntitySounds(Handle, /*retail's literal second argument*/ 1);
	}
}

// ==================================================================================================
// `CNPC_VManBat::m_bHasPlayedFlyBySound`
// ==================================================================================================

// `FUN_10390040` (`0x10390040`). Eight bytes: `*(bool*)(this + 0x66b8) = false`.
//
// UNRECOVERED: the body has NO caller anywhere in the image and nothing sets the byte either, so the
// fly-by sound this latch exists for was cut or is emitted through a path the census does not reach.
// The reset is ported verbatim; the latch has no producer and no consumer.
void FElysiumNpc::ClearHasPlayedFlyBySound()
{
	bHasPlayedFlyBySound = false;
}
