#include "ElysiumStanceTypes.h"

#include "Substrate/ElysiumDisposition.h"

namespace
{
	// Retail rolls `RandomInt(1,100)` and compares it against an authored percentage. The fidget arm
	// spells it `chance > roll` and the stance-change arm `roll < chance`, which is the same
	// relation written twice, so one helper serves both. The bound is worth stating: the roll is
	// 1-based, so a chance of 100 fires on 99 of 100 draws and a chance of 0 never fires.
	bool RollPercent(int32 Chance, FRandomStream& Rng)
	{
		return Rng.RandRange(1, 100) < FMath::Clamp(Chance, 0, 100);
	}
}

void ElysiumStance::ApplyPrecacheFallbacks(FElysiumStanceClips& Clips)
{
	// Idle 1 is the anchor: it is the pose a zero-initialised `m_CurrStance` shows, and every other
	// miss resolves through it. A table whose first idle is empty has no stance set at all, and the
	// caller tests `IsValid()` rather than being handed a table of empty strings.
	if (Clips.Idle[0].IsEmpty())
	{
		return;
	}
	for (int32 Index = 1; Index < ElysiumStance::Count; ++Index)
	{
		if (Clips.Idle[Index].IsEmpty())
		{
			Clips.Idle[Index] = Clips.Idle[0];
		}
	}
	for (int32 Index = 0; Index < ElysiumStance::Count; ++Index)
	{
		// A fidget that falls back to its own idle is what makes the fidget branch unreachable for
		// that stance -- the selector's availability test is exactly this equality, so the fallback
		// and the test are one fact rather than two that have to agree.
		if (Clips.Fidget[Index].IsEmpty())
		{
			Clips.Fidget[Index] = Clips.Idle[Index];
		}
	}
	for (int32 From = 0; From < ElysiumStance::Count; ++From)
	{
		for (int32 To = 0; To < ElysiumStance::Count; ++To)
		{
			if (Clips.Trans[From][To].IsEmpty())
			{
				Clips.Trans[From][To] = Clips.Idle[To];
			}
		}
	}
}

FElysiumStanceChoice ElysiumStance::ChangeStance(const FElysiumStanceClips& Clips,
	FElysiumStanceState& State, double Now, FRandomStream& Rng)
{
	FElysiumStanceChoice Choice;
	if (!Clips.IsValid())
	{
		return Choice;
	}

	// `do { new = RandomInt(0,2) } while (new == m_CurrStance)` -- uniform over the two stances that
	// are not current. Drawn once from the offset instead of looping: the retry loop and a single
	// draw over the other two have the same distribution, and this one cannot spin.
	const int32 From = FMath::Clamp(State.Current, 0, ElysiumStance::Count - 1);
	const int32 To = (From + 1 + Rng.RandRange(0, ElysiumStance::Count - 2)) % ElysiumStance::Count;

	Choice.Clip = Clips.Trans[From][To];
	// The transition is a one-shot: the next selection sees `bInChange` and settles onto the
	// destination idle. A transition that fell back to the destination idle is still played as a
	// one-shot, which costs nothing -- the settle plays the same clip looping straight after.
	Choice.bLoop = false;
	Choice.bChangedStance = true;

	State.Current = To;
	State.LastChangeTime = static_cast<float>(Now);
	State.bInChange = true;
	State.bInFidget = false;
	return Choice;
}

FElysiumStanceChoice ElysiumStance::Select(const FElysiumStanceClips& Clips,
	const FElysiumDisposition& Tuning, FElysiumStanceState& State, bool bTalking, double Now,
	FRandomStream& Rng)
{
	FElysiumStanceChoice Choice;
	if (!Clips.IsValid())
	{
		return Choice;
	}

	const int32 Stance = FMath::Clamp(State.Current, 0, ElysiumStance::Count - 1);
	State.Current = Stance;

	auto SettleOnIdle = [&Clips, &State, Stance]() -> FElysiumStanceChoice
	{
		FElysiumStanceChoice Idle;
		Idle.Clip = Clips.Idle[Stance];
		Idle.bLoop = true;
		State.bInFidget = false;
		State.bInChange = false;
		return Idle;
	};

	// A character speaking a line holds its stance for the length of the line; the talking pair's
	// threshold and chance are consumed by the dialogue-pause driver instead, not here.
	if (bTalking)
	{
		return SettleOnIdle();
	}

	// The clip that just finished was a fidget or a transition, so this selection is the settle
	// that follows it. Consuming the latch before the rolls is what stops a fidget chaining
	// straight into another fidget.
	if (State.bInFidget || State.bInChange)
	{
		return SettleOnIdle();
	}

	// A fidget is available only where the model authored one: the precache fallback made an
	// unauthored fidget equal to its idle, so inequality *is* the availability test.
	if (Clips.Fidget[Stance] != Clips.Idle[Stance]
		&& RollPercent(Tuning.StandingFidgetChance, Rng))
	{
		Choice.Clip = Clips.Fidget[Stance];
		Choice.bLoop = false;
		State.bInFidget = true;
		State.bInChange = false;
		return Choice;
	}

	// The stance change, floored by the disposition's own threshold since the last one. The floor is
	// measured rather than counted, so however many idles complete inside it, none of them can move
	// the stance.
	if (Now - static_cast<double>(State.LastChangeTime) > static_cast<double>(
			Tuning.StandingStanceChangeThreshold)
		&& RollPercent(Tuning.StandingStanceChangeChance, Rng))
	{
		return ChangeStance(Clips, State, Now, Rng);
	}

	return SettleOnIdle();
}
