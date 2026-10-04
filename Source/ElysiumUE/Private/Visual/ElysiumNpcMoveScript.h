#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "Misc/Optional.h"

// Retail's ground-move script (`CAI_HumanoidMotor`, spec 0002 V4b): the velocity script
// `0x102630b0` and the turn script `0x102627e0`, rebuilt on every `MoveGroundExecute 0x10264680`
// (through `0x10262590`) from where the NPC stands and how fast it is going, and read at the
// tick's interval. Plain functions over waypoints: no Unreal object, no world, no body.
//
// **Units are retail's: Source units, units per second, degrees, seconds.** The caller converts at
// its boundary. Yaw is `atan2(y, x)` in degrees of whatever frame the locations are stated in.
namespace ElysiumNpcMoveScript
{
// The constants, each with the cell it was read from (`packets-S1.md` item 6, `packets-S4.md` f.1).
inline constexpr float DefaultIdealSpeed = 50.0f;      // `0x102630b0`: slot 248 answered 0
inline constexpr float AccelOverIdeal = 50.0f;         // f64 `0x104493c0`: accel = ideal + 50
inline constexpr float CornerBias = 0.2f;              // f64 `0x10449198`: dot(in, out) + 0.2
inline constexpr float MinPiece = 0.01f;               // f64 `0x1044e658`: distance, time, turn time
inline constexpr float TrapezoidHalf = 0.5f;           // f64 `0x10449270`
inline constexpr float TurnMinDegrees = 0.1f;          // f64 `0x104493d0`
inline constexpr float TurnEase = 0.8f;                // f64 `0x104491a8`
inline constexpr float TurnDegreesPerSecond = 150.0f;  // f32 `0x10457f60`; `0x10457f58` is 1/150
// Navigator slot 16 `0x102ef510`: a waypoint is reached inside this distance, 2-D on a ground move
// (f64 `0x10451f78`). A constant, not the goal tolerance and not a hull.
inline constexpr float ArrivalUnits = 0.0625f;

// One script entry (retail's is `0x38` bytes): `+0x00` flTime, `+0x04` flElapsed, `+0x08` flDist,
// `+0x0c` flMaxVelocity, `+0x10` flYaw, `+0x20` the waypoint pointer, `+0x2c` vecLocation.
struct FEntry
{
	float Time = 0.0f;         // the segment's duration, to the next entry
	float Elapsed = 0.0f;      // when the segment starts
	float Dist = 0.0f;         // to the next entry, 3-D
	float MaxVelocity = 0.0f;  // the speed at this entry
	float Yaw = 0.0f;          // the turn script's word
	// `+0x20`: which of the input's waypoints this entry stands on; none for entry 0 (the body) and
	// for the points pass 4 inserts.
	int32 Waypoint = INDEX_NONE;
	FVector Location = FVector::ZeroVector;
};

struct FInput
{
	// Entry 0: slot 217 `GetAbsOrigin`, `GetLocalAngles().y`, `|m_vecVelocity|` (motor `+0x3c`, 3-D).
	FVector Origin = FVector::ZeroVector;
	float Yaw = 0.0f;
	float Speed = 0.0f;
	// Owner slot 248 `GetIdealSpeed 0x10091740` (`m_flGroundSpeed +0x654`), as it answers: 0 becomes
	// `DefaultIdealSpeed` inside the build.
	float IdealSpeed = 0.0f;
	// The path from its current waypoint on (`nav+0x30 -> +0x24`, next at `waypoint+0x30`).
	TConstArrayView<FVector> Waypoints;
	// The navigator's arrival direction (`0x102ee5b0` -> the path's `+0x64`, `0x1030b6b0`), which the
	// turn script takes as the last waypoint's outbound direction. SEAM: no source reaches the body
	// yet; unset, the last waypoint keeps its inbound direction (it turns nothing).
	TOptional<FVector> ArrivalDirection;
};

// `0x1013a6f0`: `r1 = (sqrt(b^2 - 4ac) - b) / 2a`. False when it has no real root.
bool SolveQuadratic(float A, float B, float C, float& OutRoot1);
// `0x102e1470`: `(v2^2 - v1^2) * 0.5 / d`.
float DeltaV(float V1, float V2, float Dist);
// `0x101d2c70` (`UTIL_VecToYaw`, inferred from its use): 0 for a vector with no x and no y.
float VecToYaw(const FVector& Dir);
// `0x1013d580` (`UTIL_AngleDiff(dest, src)`, inferred from its use): `dest - src` wrapped to +-180.
float AngleDiff(float Dest, float Src);
// `0x1013d450` = `UTIL_ApproachAngle(target, value, speed)`: starts from `value`, moves toward
// `target` by at most `|speed|`.
float ApproachAngle(float Target, float Value, float Speed);

// `0x102630b0`, the five passes. `Out` is emptied first; it always holds entry 0.
void BuildVelocityScript(const FInput& Input, TArray<FEntry>& Out);
// `0x102627e0` over a built velocity script. `Out` is emptied first; it always holds entry 0.
void BuildTurnScript(const FInput& Input, TConstArrayView<FEntry> Velocity, TArray<FEntry>& Out);
// `0x10262ea0(i, t)`: the turn script's insert. Returns the new entry's index, 0 when `t` runs off
// the end.
int32 InsertTurnEntry(TArray<FEntry>& Turn, int32 From, float T);
// `0x10262c20(i, j)`: the 0.01 s / 0.8 insert rule. Returns how many entries it inserted (0..2).
int32 InsertTurnPair(TArray<FEntry>& Turn, int32 I, int32 J);

// The read at `0x102646c0`: the speed at TIME `Interval` into the script (not at the body's place
// on the path). `CurrentSpeed` stands when the script has one entry or the interval outruns it.
float SampleSpeed(TConstArrayView<FEntry> Velocity, float Interval, float CurrentSpeed);
// The turn script read "the same way", quantised as `AngleMod`. `CurrentYaw` stands likewise.
float SampleYaw(TConstArrayView<FEntry> Turn, float Interval, float CurrentYaw);

struct FStep
{
	float Distance = 0.0f;           // what the body covers this tick
	float RemainingInterval = 0.0f;  // `m_flMoveInterval` after the step (motor `+0x30`)
	bool bClamped = false;           // the step was cut to `MaxDist`: the body lands on the waypoint
};
// `0x10264680`'s step and its clamp at `0x10264916`: `dist = (|v_now| + speed) * interval * 0.5`,
// cut to `move.maxDist (+0x28)`, the navigator's distance to the waypoint.
FStep Step(float CurrentSpeed, float NewSpeed, float Interval, float MaxDist);
}
