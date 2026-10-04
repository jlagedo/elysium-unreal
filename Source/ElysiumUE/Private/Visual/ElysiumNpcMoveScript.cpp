#include "Visual/ElysiumNpcMoveScript.h"

#include <cmath>

namespace ElysiumNpcMoveScript
{
namespace
{
// `|loc[i+1] - loc[i]|`, 3-D, as passes 1 and 5 measure it.
float DistToNext(const TArray<FEntry>& Script, int32 Index)
{
	return static_cast<float>(FVector::Dist(Script[Index + 1].Location, Script[Index].Location));
}

// z zeroed, then normalised (`PTR_thunk_FUN_10137220`); a zero vector stays zero.
FVector Flat(const FVector& V)
{
	return FVector(V.X, V.Y, 0.0).GetSafeNormal();
}

// 0..360, as `UTIL_ApproachAngle` reduces both of its angles.
float Anglemod(float Degrees)
{
	float Out = std::fmod(Degrees, 360.0f);
	if (Out < 0.0f)
	{
		Out += 360.0f;
	}
	return Out;
}
}

bool SolveQuadratic(float A, float B, float C, float& OutRoot1)
{
	// `0x1013a6f0`: r1 = (sqrt(b^2 - 4ac) - b) / 2a. Every caller here passes a = k * accel with
	// accel >= 50, so the SDK's a == 0 arms are not reachable from the script and are not written.
	const float Discriminant = B * B - 4.0f * A * C;
	if (A == 0.0f || Discriminant < 0.0f)
	{
		return false;
	}
	OutRoot1 = (std::sqrt(Discriminant) - B) / (2.0f * A);
	return true;
}

float DeltaV(float V1, float V2, float Dist)
{
	// `0x102e1470`.
	return (V2 * V2 - V1 * V1) * 0.5f / Dist;
}

float VecToYaw(const FVector& Dir)
{
	// `0x101d2c70`.
	if (Dir.X == 0.0 && Dir.Y == 0.0)
	{
		return 0.0f;
	}
	float Yaw = FMath::RadiansToDegrees(static_cast<float>(std::atan2(Dir.Y, Dir.X)));
	if (Yaw < 0.0f)
	{
		Yaw += 360.0f;
	}
	return Yaw;
}

float AngleDiff(float Dest, float Src)
{
	// `0x1013d580`.
	float Delta = std::fmod(Dest - Src, 360.0f);
	if (Dest > Src)
	{
		if (Delta >= 180.0f)
		{
			Delta -= 360.0f;
		}
	}
	else if (Delta <= -180.0f)
	{
		Delta += 360.0f;
	}
	return Delta;
}

float ApproachAngle(float Target, float Value, float Speed)
{
	// `0x1013d450` (`packets-S4.md` f.1): both angles reduced to 0..360, the delta wrapped to
	// +-180, the speed made positive.
	Target = Anglemod(Target);
	Value = Anglemod(Value);
	float Delta = Target - Value;
	if (Speed < 0.0f)
	{
		Speed = -Speed;
	}
	if (Delta < -180.0f)
	{
		Delta += 360.0f;
	}
	else if (Delta > 180.0f)
	{
		Delta -= 360.0f;
	}
	if (Delta > Speed)
	{
		return Value + Speed;
	}
	if (Delta < -Speed)
	{
		return Value - Speed;
	}
	return Target;
}

void BuildVelocityScript(const FInput& Input, TArray<FEntry>& Out)
{
	Out.Reset();

	// `0x102630b0`: ideal = owner slot 248 (`GetIdealSpeed 0x10091740`), 50.0 when it answers 0;
	// accel = ideal + 50.0 (`0x104493c0`).
	const float Ideal = Input.IdealSpeed == 0.0f ? DefaultIdealSpeed : Input.IdealSpeed;
	const float Accel = Ideal + AccelOverIdeal;

	// Entry 0: the body's origin, its yaw, its speed.
	FEntry First;
	First.Location = Input.Origin;
	First.Yaw = Input.Yaw;
	First.MaxVelocity = Input.Speed;
	Out.Add(First);

	// One entry per remaining waypoint. The last one's speed is 0.0 (`GetArrivalSpeed` is not
	// asked); any other's is ideal * clamp(dot(next - this, this - previous entry) + 0.2, 0, 1),
	// both directions z-zeroed and normalised (`0x10449198`).
	const int32 WaypointCount = Input.Waypoints.Num();
	for (int32 Index = 0; Index < WaypointCount; ++Index)
	{
		FEntry Entry;
		Entry.Location = Input.Waypoints[Index];
		Entry.Waypoint = Index;
		if (Index + 1 < WaypointCount)
		{
			const FVector D1 = Flat(Input.Waypoints[Index + 1] - Entry.Location);
			const FVector D2 = Flat(Entry.Location - Out.Last().Location);
			float Scale = static_cast<float>(FVector::DotProduct(D1, D2)) + CornerBias;
			if (Scale <= 0.0f)
			{
				Scale = 0.0f;
			}
			else if (Scale > 1.0f)
			{
				Scale = 1.0f;
			}
			Entry.MaxVelocity = Scale * Ideal;
		}
		Out.Add(Entry);
	}

	// Pass 1, distances and prune (`0x1026330e`): a piece under 0.01 that is not the body's own
	// entry is removed and the same index is measured again.
	for (int32 Index = 0; Index < Out.Num() - 1;)
	{
		Out[Index].Dist = DistToNext(Out, Index);
		if (Out[Index].Dist < MinPiece && Index != 0)
		{
			Out.RemoveAt(Index);
		}
		else
		{
			++Index;
		}
	}

	// Pass 2, forward (`0x102633b8`): a speed-up that does not fit its segment at `Accel` is cut
	// to what the segment allows.
	for (int32 Index = 0; Index < Out.Num() - 1; ++Index)
	{
		const float V = Out[Index].MaxVelocity;
		const float Dv = Out[Index + 1].MaxVelocity - V;
		if (Dv > 0.0f)   // f64 `0x1044fab0` = 0.0
		{
			const float T = Dv / Accel;
			float Root = 0.0f;
			if (V * T + 0.5f * Accel * T * T > Out[Index].Dist
				&& SolveQuadratic(0.5f * Accel, V, -Out[Index].Dist, Root))
			{
				Out[Index + 1].MaxVelocity = V + Root * Accel;
			}
		}
	}

	// Pass 3, backward (`0x10263480`). RETAIL'S, REPRODUCED AS READ: the test is `dv > 0` with
	// dv = v[i] - v[i-1] -- pass 2's sign test, not the SDK's `dv < 0` -- and the distance is
	// entry i's own, not the segment's between the pair. So it never fires on a slowing pair: a
	// deceleration that does not fit its segment stays uncorrected.
	for (int32 Index = Out.Num() - 1; Index >= 1; --Index)
	{
		const float V = Out[Index].MaxVelocity;
		const float Dv = V - Out[Index - 1].MaxVelocity;
		if (Dv > 0.0f)
		{
			const float T = Dv / Accel;
			float Root = 0.0f;
			if (V * T + 0.5f * Accel * T * T > Out[Index].Dist
				&& SolveQuadratic(0.5f * Accel, V, -Out[Index].Dist, Root))
			{
				Out[Index - 1].MaxVelocity = V - Root * Accel;
			}
		}
	}

	// Pass 4, cruise points (`0x10263554`). No guards: the SDK's `d > 1.0 && t > 0.1` are absent
	// in retail, and a zero-length piece this makes is removed by pass 5.
	for (int32 Index = 0; Index < Out.Num() - 1;)
	{
		const float V0 = Out[Index].MaxVelocity;
		const float V1 = Out[Index + 1].MaxVelocity;
		const float Dist = Out[Index].Dist;
		const FVector From = Out[Index].Location;
		const FVector To = Out[Index + 1].Location;

		const float T1 = (Ideal - V0) / Accel;
		const float D1 = V0 * T1 + 0.5f * Accel * T1 * T1;
		const float T2 = (Ideal - V1) / Accel;
		const float D2 = V1 * T2 + 0.5f * Accel * T2 * T2;

		if (D1 + D2 < Dist)
		{
			// Room to reach the ideal and leave it: two points at the ideal, where the speed-up
			// ends and where the slow-down starts.
			FEntry Up;
			Up.Location = FMath::Lerp(From, To, static_cast<double>(D1 / Dist));
			Up.MaxVelocity = Ideal;
			FEntry Down;
			Down.Location = FMath::Lerp(From, To, static_cast<double>((Dist - D2) / Dist));
			Down.MaxVelocity = Ideal;
			Out.Insert(Up, Index + 1);
			Out.Insert(Down, Index + 2);
			Index += 3;
		}
		else if (FMath::Abs(DeltaV(V0, V1, Dist)) < Accel)
		{
			// No room for the ideal: one peak where the speed-up meets the slow-down, when that
			// peak is under the ideal.
			const float Ratio = (Accel + V0) / (Accel + V1);
			float T = 0.0f;
			if (SolveQuadratic((0.5f * Ratio * Ratio + 0.5f) * Accel, Ratio * V1 + V0, -Dist, T))
			{
				const float DA = V0 * T + 0.5f * Accel * T * T;
				const float DB = T * Ratio * V1 + 0.5f * Accel * (T * Ratio) * (T * Ratio);
				const float Peak = V0 + T * Accel;
				if (Peak < Ideal)
				{
					FEntry Mid;
					Mid.Location = FMath::Lerp(From, To, static_cast<double>(DA / (DA + DB)));
					Mid.MaxVelocity = Peak;
					Out.Insert(Mid, Index + 1);
					Index += 1;
				}
			}
			Index += 1;
		}
		else
		{
			Index += 1;
		}
	}

	// Pass 5, times (`0x10263991`): every distance measured again; a segment's time is its length
	// over the mean of its two speeds, 1.0 when both are 0; a piece under 0.01 in length or in time
	// loses its far entry and is measured again.
	Out[0].Elapsed = 0.0f;
	for (int32 Index = 0; Index < Out.Num() - 1;)
	{
		FEntry& Entry = Out[Index];
		const float NextVelocity = Out[Index + 1].MaxVelocity;
		Entry.Dist = DistToNext(Out, Index);
		Entry.Time = (Entry.MaxVelocity > 0.0f || NextVelocity > 0.0f)
			? Entry.Dist / (0.5f * (Entry.MaxVelocity + NextVelocity))
			: 1.0f;
		if (Entry.Dist < MinPiece || Entry.Time < MinPiece)
		{
			Out.RemoveAt(Index + 1);
		}
		else
		{
			Out[Index + 1].Elapsed = Entry.Elapsed + Entry.Time;
			++Index;
		}
	}
}

int32 InsertTurnEntry(TArray<FEntry>& Turn, int32 From, float T)
{
	// `0x10262ea0(i, t)` (`packets-S5.md` item 2). From entry i, the first entry k whose own time
	// holds t is split there.
	for (int32 K = From; K < Turn.Num(); ++K)
	{
		if (T <= Turn[K].Time)
		{
			// GUARD, not retail's: a zero-length entry asked for t <= 0 would divide 0 by 0.
			const float A = Turn[K].Time > 0.0f ? T / Turn[K].Time : 0.0f;
			// AS READ, swapped against the SDK: the old entry keeps `time - t`, the new one gets t.
			Turn[K].Time -= T;
			// GUARD, not retail's: the lerp reads entry k+1 before the shift, so on the last entry
			// retail reads one slot past the count. Here the last entry lerps against itself.
			const FEntry& Far = Turn.IsValidIndex(K + 1) ? Turn[K + 1] : Turn[K];
			FEntry Inserted;   // every other word zero
			Inserted.Time = T;
			Inserted.Elapsed = (1.0f - A) * Turn[K].Elapsed + A * Far.Elapsed;
			Inserted.Location = FMath::Lerp(Turn[K].Location, Far.Location, static_cast<double>(A));
			Turn.Insert(Inserted, K + 1);
			return K + 1;
		}
		T -= Turn[K].Time;
	}
	return 0;   // ran off the end
}

int32 InsertTurnPair(TArray<FEntry>& Turn, int32 I, int32 J)
{
	// `0x10262c20(i, j)` (`packets-S4.md` f.1): the yaw of the straight line between the two
	// entries, how long turning onto it and off it takes at 150 degrees per second, and where in
	// the segment's time those two turns end and start.
	const float Yaw = VecToYaw(Turn[J].Location - Turn[I].Location);
	const float TimeIn = FMath::Abs(AngleDiff(Yaw, Turn[I].Yaw)) / TurnDegreesPerSecond;
	const float TimeOut = FMath::Abs(AngleDiff(Turn[J].Yaw, Yaw)) / TurnDegreesPerSecond;
	const float Total = Turn[J].Elapsed - Turn[I].Elapsed;

	// AS READ: when `0x10262ea0` runs off the end it answers 0 and retail writes the yaw to entry
	// 0. `Turn[0]` always exists, so the write is reproduced.
	float Single = 0.0f;
	if (TimeIn >= MinPiece)
	{
		if (TimeOut >= MinPiece)
		{
			if (TimeIn + TimeOut <= Total && TimeIn + TimeOut < Total * TurnEase)
			{
				// Both inserts start from entry i, the second at `TimeOut` (not `Total - TimeOut`):
				// as read.
				Turn[InsertTurnEntry(Turn, I, TimeIn)].Yaw = Yaw;
				Turn[InsertTurnEntry(Turn, I, TimeOut)].Yaw = Yaw;
				return 2;
			}
			return 0;
		}
		if (TimeIn > Total * TurnEase)
		{
			return 0;
		}
		Single = TimeIn;
	}
	else
	{
		if (TimeOut > Total * TurnEase)
		{
			return 0;
		}
		Single = Total - TimeOut;
	}
	Turn[InsertTurnEntry(Turn, I, Single)].Yaw = Yaw;
	return 1;
}

void BuildTurnScript(const FInput& Input, TConstArrayView<FEntry> Velocity, TArray<FEntry>& Out)
{
	Out.Reset();

	// `0x102627e0`. Entry 0: the origin (slot 217) and the current yaw (`GetLocalAngles().y`).
	FEntry First;
	First.Location = Input.Origin;
	First.Yaw = Input.Yaw;
	Out.Add(First);

	// One candidate per velocity-script entry that carries a waypoint (`+0x20` non-null; pass 4's
	// points carry none). `Previous` is the last turn entry appended.
	int32 Previous = 0;
	for (const FEntry& Source : Velocity)
	{
		if (!Input.Waypoints.IsValidIndex(Source.Waypoint))
		{
			continue;
		}
		FEntry Entry;
		Entry.Location = Input.Waypoints[Source.Waypoint];
		Entry.Waypoint = Source.Waypoint;
		Entry.Elapsed = Source.Elapsed;
		// Written before the 0.1-degree test, so a waypoint that adds no entry still stretches the
		// previous entry's time up to itself.
		Out[Previous].Time = Entry.Elapsed - Out[Previous].Elapsed;

		const float In = VecToYaw(Flat(Entry.Location - Out[Previous].Location));
		if (Input.Waypoints.IsValidIndex(Source.Waypoint + 1))
		{
			const float OutYaw = VecToYaw(Flat(Input.Waypoints[Source.Waypoint + 1] - Entry.Location));
			const float Diff = AngleDiff(OutYaw, In);
			if (FMath::Abs(Diff) <= TurnMinDegrees)   // `0x104493d0`
			{
				continue;
			}
			// From the inbound yaw toward the outbound one, 0.8 of the way (`0x104491a8`).
			Entry.Yaw = ApproachAngle(OutYaw, In, FMath::Abs(Diff) * TurnEase);
		}
		else if (Input.ArrivalDirection.IsSet())
		{
			// The last waypoint: the navigator's arrival direction (`0x102ee5b0`), whole, with no
			// 0.1-degree test and no ease.
			Entry.Yaw = VecToYaw(Input.ArrivalDirection.GetValue());
		}
		else
		{
			// SEAM for the path's arrival direction (`path+0x64`, `0x1030b6b0`): nothing states it
			// to the body yet, so the last waypoint keeps the direction it is reached in.
			Entry.Yaw = In;
		}
		Out.Add(Entry);
		++Previous;
	}

	// Backward, i = n-1 .. 2: a turn larger than the segment before it allows at 150 degrees per
	// second of segment time (`0x10457f60`) pulls the earlier yaw toward the later one's side.
	// `0x1013d450(yaw[i-1], yaw[i], limit)` starts from yaw[i] and moves toward yaw[i-1].
	for (int32 Index = Out.Num() - 1; Index >= 2; --Index)
	{
		const float Limit = Out[Index - 1].Time * TurnDegreesPerSecond;
		if (Limit < FMath::Abs(AngleDiff(Out[Index - 1].Yaw, Out[Index].Yaw)))
		{
			Out[Index - 1].Yaw = ApproachAngle(Out[Index - 1].Yaw, Out[Index].Yaw, Limit);
		}
	}

	// Then `0x10262c20(i, i+1)` over the entries, stepping past what each call inserted.
	if (Out.Num() > 1)
	{
		int32 Index = 0;
		do
		{
			Index += 1 + InsertTurnPair(Out, Index, Index + 1);
		} while (Index < Out.Num() - 1);
	}
}

float SampleSpeed(TConstArrayView<FEntry> Velocity, float Interval, float CurrentSpeed)
{
	// `0x102646c0`: the first i >= 1 whose flElapsed is past the interval. The divisor is
	// flElapsed[i] itself, not the segment's own time (as read).
	for (int32 Index = 1; Index < Velocity.Num(); ++Index)
	{
		if (Interval < Velocity[Index].Elapsed)
		{
			const float A = Interval / Velocity[Index].Elapsed;
			return (1.0f - A) * Velocity[Index - 1].MaxVelocity + A * Velocity[Index].MaxVelocity;
		}
	}
	// One entry, or the interval outruns the script: `|m_vecVelocity|` stands.
	return CurrentSpeed;
}

float SampleYaw(TConstArrayView<FEntry> Turn, float Interval, float CurrentYaw)
{
	// `0x10264680`, the second loop: the same search over the turn script, the yaw taken along
	// `AngleDiff(yaw[i], yaw[i-1])` and quantised to 16 bits (`& 0xffff`, `* 0x1044ffdc`). The
	// decompiler drops the float arithmetic between the two; the fraction is the speed read's
	// (`packets-S1.md` item 6: "the yaw the same way").
	for (int32 Index = 1; Index < Turn.Num(); ++Index)
	{
		if (Interval < Turn[Index].Elapsed)
		{
			const float A = Interval / Turn[Index].Elapsed;
			const float Yaw = Turn[Index - 1].Yaw + A * AngleDiff(Turn[Index].Yaw, Turn[Index - 1].Yaw);
			const int32 Quantised = static_cast<int32>(Yaw * (65536.0f / 360.0f)) & 0xffff;
			return static_cast<float>(Quantised) * (360.0f / 65536.0f);
		}
	}
	return CurrentYaw;   // `GetLocalAngles().y`, unquantised
}

FStep Step(float CurrentSpeed, float NewSpeed, float Interval, float MaxDist)
{
	// `0x10264680`: dist = (|m_vecVelocity| + speed) * m_flMoveInterval * 0.5 (`0x10449270`).
	FStep Out;
	Out.Distance = (CurrentSpeed + NewSpeed) * Interval * TrapezoidHalf;
	// `0x10264916`: dist <= move.maxDist -> the interval is spent. Else the step is cut to maxDist
	// -- the body is placed on the waypoint -- and the unused interval,
	// interval * (1 - maxDist / dist), goes back to the navigator's loop. (`move.flags & 2` zeroes
	// it instead; the caller that has no such loop ignores the remainder either way.)
	if (Out.Distance > MaxDist)
	{
		Out.RemainingInterval = (1.0f - MaxDist / Out.Distance) * Interval;
		Out.Distance = MaxDist;
		Out.bClamped = true;
	}
	return Out;
}
}
