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
// (`0x10293ec0` and slot 506's) are layer 14 and belong to story **29d**, family **Sounds10**.
// Story 29d ported them: each definition below is the SPECIES PROLOGUE 29c-1 put here, and the arm
// it falls through to is `TroikaDeathSound()` (slot 506's `TroikaSlot506` is dead, 0019/6) in
// `Substrate/ElysiumNpcSounds10.cpp`, beside the other fifteen hooks of the same band.

// slot 488 0x10293ec0 `void DeathSound()`
void FElysiumNpc::DeathSound()
{
	// `CNPC_VTzimisce` overrides this slot (`FElysiumNpcTzimisce`, `0x103b92a0`): it fires
	// `SPI_DIES` at the script host and tail-calls slot 487, so a Tzimisce never reaches the base
	// death sound; that tail call is the one part of its body still unported.
	TroikaDeathSound();
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

