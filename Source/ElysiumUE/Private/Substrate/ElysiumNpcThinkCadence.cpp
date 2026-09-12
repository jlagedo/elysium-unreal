#include "Substrate/ElysiumNpcThinkCadence.h"

#include "ElysiumMoveSolve.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

namespace
{
	// Every law's ceiling adds a uniform jitter so a crowd spawned on one frame does not stay in
	// lockstep for the rest of the map.
	double Jitter(double Max)
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcThinkCadence).FRandRange(0.0, Max);
	}

	// The epilogue every `Calc*` shares: `*out = Next - Last; Last = Next; Next = max(Next,
	// curtime) + i`. Retail hands the elapsed interval on to `PerformMovement`; this runtime's
	// bodies are integrated by the movement component, so the out-param is dropped -- but `Last`
	// is still written, because it is saved state and `UpdateCharacter` reads its own.
	//
	// The `max(Next, curtime)` matters more than it looks: a stamp that fell behind the clock (a
	// hitch, a `ResetThinkTimers`) does not accumulate a debt of missed intervals, it restarts
	// from now.
	void Commit(double& Next, double& Last, double Now, double Interval)
	{
		Last = Next;
		Next = FMath::Max(Next, Now) + Interval;
	}

	// `!PVS` and `!LOS` are EXCLUSIVE arms in both laws that carry them (`if (PVS == 0) ... else
	// if (LOS == 0) ...`), not a pair of successive multipliers.
	double ScaleForVisibility(double Interval, bool bInPvs, bool bInLos,
		double PvsScale, double PvsCap, double LosScale, double LosCap)
	{
		if (!bInPvs)
		{
			return FMath::Min(Interval * PvsScale, PvsCap);
		}
		if (!bInLos)
		{
			return FMath::Min(Interval * LosScale, LosCap);
		}
		return Interval;
	}
}

ElysiumNpcThink::FInputs ElysiumNpcThink::GatherInputs(const FElysiumNpc& Npc)
{
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	FInputs In;
	In.bHasClosestPlayer = Memory.ClosestPlayer.IsSet();
	In.PlayerDistUnits = Memory.ClosestPlayerDistanceCm / ElysiumMove::U;
	In.bInPlayerPvs = Memory.bPlayerInPvs;
	In.bInPlayerLos = Memory.bPlayerLos;
	In.bScheduleChanged = Npc.NpcFlags.Has(EElysiumNpcFlag2::SCHEDULE_CHANGED);
	In.bThinkFrequently = ShouldThinkFrequently(Npc);
	In.bAlwaysInPlayerView =
		Npc.NpcFlags.HasFrenzied(FElysiumNpcFlags::FrenziedAlwaysInPlayerView);
	return In;
}

bool ElysiumNpcThink::ShouldThinkFrequently(const FElysiumNpc& Npc)
{
	// `0x102c2430`, three arms:
	//
	//   IsInDialog() || m_scriptState in {4,5,6}
	//                || (curtime > m_flTeleportMoveTimer ? m_bForceFrequentThink : true)
	//
	// READ THE THIRD ONE CAREFULLY. Inside the teleport-move window it is unconditionally TRUE;
	// outside it, it falls back to a flag that defaults false. Reversed, it pins every NPC in the
	// map to the Normal law's 0.01 s floor -- 100 Hz per body.

	// `IsInDialog()` `0x102c1170` is four terms: `m_bIsTalking`, a queued dialogue string, the
	// dialogue partner handle and `+0x6554`. This runtime carries one session bit for the last
	// three and a talk-end stamp for the first: `m_bIsTalking` is set by the spoken-line player
	// `0x102c0520` with its own end time (`+0x64cc`), outside any dialogue session.
	const double Now = Npc.World ? Npc.World->NowSeconds() : 0.0;
	if (Npc.Dialogue.bInDialog || Npc.IsTalking(Now))
	{
		return true;
	}
	if (Npc.IsScriptDriven())   // `m_scriptState in {4,5,6}`
	{
		return true;
	}
	return Now <= static_cast<double>(Npc.TeleportMoveTimer) ? true : Npc.bForceFrequentThink;
}

double ElysiumNpcThink::UpdateInterval(const FInputs& In)
{
	// `CalcNextUpdateThink` `0x10290720`.
	double Interval = 0.03;
	if (In.bHasClosestPlayer)
	{
		Interval = (static_cast<double>(In.PlayerDistUnits) - 512.0) / 704.0;
		if (Interval >= 8.0)
		{
			Interval = 8.0 + Jitter(0.8);
		}
		else if (Interval < 0.03)
		{
			Interval = 0.03;
		}
	}
	// The no-player arm falls into the scaling too -- it is not an early return.
	Interval = ScaleForVisibility(Interval, In.bInPlayerPvs, In.bInPlayerLos, 10.0, 16.0, 5.0, 12.0);
	// Last, and an OVERRIDE rather than a floor: a talking or scripted body updates at 0.03 s no
	// matter how far away or how hidden it is.
	return In.bThinkFrequently ? 0.03 : Interval;
}

double ElysiumNpcThink::NormalInterval(const FInputs& In)
{
	// `CalcNextNormalThink` `0x10290b60`. Note the asymmetry the cadence turns on: being OUT of
	// LOS lengthens the update think, while being IN LOS pins this one and the AI one to 0.1 s.
	if (In.bThinkFrequently)
	{
		return 0.01;
	}
	if (In.bAlwaysInPlayerView || In.bScheduleChanged || In.bInPlayerLos)
	{
		// These three skip the visibility scaling entirely.
		return 0.1;
	}
	double Interval = 0.1;
	if (In.bHasClosestPlayer)
	{
		Interval = (static_cast<double>(In.PlayerDistUnits) - 2048.0) * 3.0 / 4096.0;
		// `v > 3` and `v == 3` are separate arms in `0x10290b60` and both jitter, so `>=`.
		if (Interval >= 3.0)
		{
			Interval = 3.0 + Jitter(0.3);
		}
		else if (Interval < 0.1)
		{
			Interval = 0.1;
		}
	}
	// On this branch LOS is necessarily false (the pin above took every other case), so the
	// `x3 cap 6` always applies and the law's real ceiling in PVS is 6 s, not 3.
	return ScaleForVisibility(Interval, In.bInPlayerPvs, In.bInPlayerLos, 10.0, 16.0, 3.0, 6.0);
}

double ElysiumNpcThink::AiInterval(const FInputs& In)
{
	// `CalcNextAIThink` `0x10291230`. No visibility scaling and no `ShouldThinkFrequently` --
	// the only law of the four that reads neither.
	if (In.bAlwaysInPlayerView || In.bScheduleChanged || In.bInPlayerLos || !In.bHasClosestPlayer)
	{
		return 0.1;
	}
	double Interval = (static_cast<double>(In.PlayerDistUnits) - 512.0) / 896.0;
	if (Interval >= 4.0)
	{
		return 4.0 + Jitter(0.4);
	}
	return Interval < 0.1 ? 0.1 : Interval;
}

void ElysiumNpcThink::CalcNextUpdateThink(FElysiumNpcScheduleHost& Host, const FInputs& In,
	double Now, double FrameSeconds)
{
	if (!IsDue(Host.NextUpdate, Now, FrameSeconds))
	{
		return;
	}
	Commit(Host.NextUpdate, Host.LastUpdate, Now, UpdateInterval(In));
}

void ElysiumNpcThink::CalcNextNormalThink(FElysiumNpcScheduleHost& Host, const FInputs& In,
	double Now, double FrameSeconds)
{
	if (!IsDue(Host.NextNormal, Now, FrameSeconds))
	{
		return;
	}
	Commit(Host.NextNormal, Host.LastNormal, Now, NormalInterval(In));
}

void ElysiumNpcThink::CalcNextAiThink(FElysiumNpcScheduleHost& Host, const FInputs& In,
	double Now, double FrameSeconds)
{
	if (!IsDue(Host.NextAI, Now, FrameSeconds))
	{
		return;
	}
	Commit(Host.NextAI, Host.LastAI, Now, AiInterval(In));
}

void ElysiumNpcThink::CalcNextMoveThink(FElysiumNpcScheduleHost& Host, double Now)
{
	// `0x10290fc0`: no due test, no inputs, and unlike the other three it does not even preserve
	// the old stamp -- `Next = curtime + 0.001` outright. Nothing reads the result: the move
	// clock's due test `0x102906e0` is a bare `return true`, so `PerformMovement` always runs on a
	// normal-due think. The stamp is kept because it is saved state and a later story may give it
	// a reader.
	Host.LastMove = Host.NextMove;
	Host.NextMove = Now + 0.001;
}
