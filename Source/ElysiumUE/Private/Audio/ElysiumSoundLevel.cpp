#include "ElysiumSoundLevel.h"

#include "ElysiumMoveSolve.h"        // ElysiumMove::U — the one Source-unit -> cm conversion
#include "Curves/CurveFloat.h"       // FRuntimeFloatCurve::GetRichCurve
#include "Curves/RichCurve.h"

namespace
{
	// ---------------------------------------------------------------------------------------
	// A3: the shape of the sampled curve. Every VALUE the model uses is a recovered constant in
	// A3: `ElysiumSoundLevel.h`'s block (`docs/vtmb/footsteps.md` §3.1); the two numbers here are
	// A3: how finely `SND_GetGain` (`engine.dll 0x2011a0b0`) is sampled onto an Unreal curve and
	// A3: where that curve is cut. Nothing else in this file is a choice.
	// ---------------------------------------------------------------------------------------

	// Keys between the reference distance and the audible range, placed GEOMETRICALLY so each
	// segment spans the same number of dB. 64 keys over the 40 dB from `D_ref` to `D_ref * 100` is
	// 0.63 dB per segment, and the linear interpolation between two keys is an equal-error chord of
	// the law everywhere.
	constexpr int32 FalloffCurveKeys = 64;

	// Where the law stops and the terminating zero key begins, as a fraction of the falloff range.
	// The zero key is not a choice — `FBaseAttenuationSettings::GetMaxFalloffDistance` reports
	// `WorldMax`, i.e. NEVER CULLED, for a custom curve whose last key is non-zero, and a footstep
	// audible across the whole map is the one thing a sound level exists to prevent. Retail's own
	// sub-floor knee (`gain = 0.01 * (2 - 0.01 * relative)`, then a pin at 0.001) carries the tail
	// out to twice the audible range at under -40 dB; the port cuts at the floor instead, one curve
	// segment wide, which is the only place this file departs from the listing.
	constexpr float FalloffCurveTail = 0.9999f;

	// A level at or below zero is Source's `SNDLVL_NONE` — `ch->dist_mult == 0`, on which
	// `SND_GetGain` returns `snd_gain` unchanged and applies no distance falloff at all.
	// `FromDistanceUnits(0)` answers 0 for exactly that reason, so the settings have to be able to
	// say it. The falloff distance is still finite because the audio device culls on
	// `GetMaxDimension()` regardless of `bAttenuate`; 10 km is past any VtMB map's diagonal.
	constexpr float UnattenuatedFalloffCm = 1000000.0f;
}

namespace ElysiumSoundLevel
{

int32 FromDistanceUnits(float DistUnits)
{
	// `vampire.dll 0x10228350`:
	//   FLD [dist]; FCOMP [0x104454c4]        -- dist > 0 ?
	//   FMUL [0x1047aa18]                     -- * 1/36
	//   FLDLG2; FXCH; FYL2X                   -- log10(dist/36)
	//   FMUL [0x104704a8]                     -- * 20
	//   FADD [0x10462950]                     -- + 40   <- the GAME dll's own reference, not snd_refdb
	//   JMP __ftol                            -- truncate toward zero
	// A non-finite distance is not a case retail can reach (its input is an authored float), but
	// `log10` of one would poison the whole audio path, so it takes the same arm as `dist <= 0`.
	if (!(DistUnits > 0.0f) || !FMath::IsFinite(DistUnits))
	{
		return 0;
	}
	const double Level = DbPerDecade * FMath::LogX(10.0, static_cast<double>(DistUnits) / RefDistUnits)
		+ AuthoredFloorDb;
	// `__ftol` truncates toward zero, which is `(int)` in C and NOT `FMath::RoundToInt`.
	return static_cast<int32>(Level);
}

float SourceAttenuation(int32 LevelDb)
{
	// `vampire.dll 0x1026d460`:
	//   CMP EDI,0x32 ; JLE -> FLD double ptr [0x10449148]        -- level <= 50 -> 4.0
	//   MOV EAX,0x14 ; LEA ECX,[EDI-0x32] ; CDQ ; IDIV ECX ; FILD
	// The division is INTEGER: 57 and 58 both answer 2, 63 and 64 both answer 1, and every level
	// from 70 up answers 0. Reproduced with integer arithmetic rather than a float divide plus a
	// truncation, so there is no rounding to disagree about.
	if (LevelDb <= AttenuationPivotDb)
	{
		return AttenuationFloor;
	}
	return static_cast<float>(AttenuationNumerator / (LevelDb - AttenuationPivotDb));
}

float ReferenceDistanceUnits(int32 LevelDb)
{
	// The inverse of `DIST_MULT_TO_SNDLVL` (`engine.dll 0x20119fe0`):
	// `dist_mult(L) = 10^((snd_refdb - L)/20) / snd_refdist`, and `D_ref` is where
	// `dist * dist_mult == 1`.
	return RefDistUnits * FMath::Pow(10.0f, (static_cast<float>(LevelDb) - RefDb) / DbPerDecade);
}

float ReferenceDistanceCm(int32 LevelDb)
{
	return ReferenceDistanceUnits(LevelDb) * ElysiumMove::U;
}

float AuthoredDistanceUnits(int32 LevelDb)
{
	// `d_authored / D_ref(L) == 10` exactly, because `0x10228350` uses 40 dB where the engine uses
	// `snd_refdb` 60: the two twenties cancel and leave one decade. The authored footfall distance
	// is the -20 dB point of the step, not where it dies.
	return ReferenceDistanceUnits(LevelDb) * 10.0f;
}

float AudibleRangeUnits(int32 LevelDb)
{
	// `gain = D_ref / d` reaches `snd_gain_min` (0.01) at `D_ref / snd_gain_min`.
	return ReferenceDistanceUnits(LevelDb) / SndGainMin;
}

float AudibleRangeCm(int32 LevelDb)
{
	return AudibleRangeUnits(LevelDb) * ElysiumMove::U;
}

float Gain(int32 LevelDb, float DistUnits)
{
	// `SND_GetGain`, `engine.dll 0x2011a0b0`, with the foliage term omitted
	// (`FoliageDbLossPer1200Units` states why).
	const float RefUnits = ReferenceDistanceUnits(LevelDb);
	if (!(RefUnits > 0.0f))
	{
		// `ch->dist_mult == 0` — SNDLVL_NONE, no distance falloff.
		return SndGain;
	}
	const float Relative = FMath::Max(DistUnits, 0.0f) / RefUnits;

	// `gain = (relative > 0.1) ? snd_gain / relative : snd_gain * 10` — one expression, and the
	// near-field cap is what keeps a sound the listener stands on from being infinitely loud.
	float Result = SndGain / FMath::Max(Relative, NearFieldRelativeFloor);

	if (Result > CompressionKnee)
	{
		// The soft knee towards `snd_gain_max`. Continuous at 0.5 (`0.5^e * 2^(e+1) == 2` for any
		// e) and asymptotic to 1, so the whole near field lands in (0.5, 1).
		const float Exponent = LevelDb > CompressionLevelKnee
			? CompressionExponent - static_cast<float>(LevelDb - CompressionLevelKnee)
				* CompressionExponentSlope / CompressionLevelSpan
			: CompressionExponent;
		Result = SndGainMax * (1.0f - 1.0f
			/ (FMath::Pow(Result, Exponent) * 2.0f * FMath::Pow(2.0f, Exponent)));
	}

	if (Result < SndGainMin)
	{
		// The sub-floor knee: linear to zero at twice the audible range, then pinned at 0.001.
		Result = SndGainMin * (2.0f - SndGainMin * Relative);
		if (Result <= 0.0f)
		{
			Result = SilentGain;
		}
	}
	return Result;
}

FSoundAttenuationSettings MakeAttenuation(int32 LevelDb)
{
	FSoundAttenuationSettings Att;
	Att.bAttenuate = true;
	Att.bSpatialize = true;
	Att.AttenuationShape = EAttenuationShape::Sphere;

	if (LevelDb <= 0)
	{
		// `SNDLVL_NONE`. Audible everywhere, still placed in the world.
		Att.bAttenuate = false;
		Att.AttenuationShapeExtents = FVector::ZeroVector;
		Att.FalloffDistance = UnattenuatedFalloffCm;
		Att.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
		Att.dBAttenuationAtMax = 0.0f;
		return Att;
	}

	// The sphere's extent is the REFERENCE DISTANCE: `FBaseAttenuationSettings::Evaluate` measures
	// `max(dist - extent, 0)` for a sphere, so the curve below is indexed from `D_ref` outward and
	// everything inside it evaluates at the curve's first key.
	//
	// **One bounded simplification, stated.** Retail keeps shaping the field INSIDE `D_ref`: the
	// raw gain rises to 10 (the `relative` floor) and the knee compresses it from 0.912 at `D_ref`
	// to 0.99972 at `D_ref/10`. Holding the extent at `D_ref` flattens that at 0.912, a monotone
	// error of at most 0.8 dB within 28 units of a walking NPC's foot. Everything from `D_ref`
	// outward — the knee at `2*D_ref`, the -6 dB per doubling beyond it, the -20 dB point at the
	// authored distance and the -40 dB floor — is the listing.
	const float RefCm = ReferenceDistanceCm(LevelDb);
	const float AudibleCm = AudibleRangeCm(LevelDb);
	const float SpanCm = AudibleCm - RefCm;
	Att.AttenuationShapeExtents = FVector(RefCm, 0.0f, 0.0f);
	Att.FalloffDistance = SpanCm / FalloffCurveTail;
	Att.DistanceAlgorithm = EAttenuationDistanceModel::Custom;
	// Unused by the `Custom` branch of `AttenuationEval`, and set anyway because it IS the model's
	// statement of itself: `snd_gain_min` 0.01 is -40 dB below the reference gain, and that is
	// where the sound stops.
	Att.dBAttenuationAtMax = -(DbPerDecade * 2.0f);

	// `AttenuationEval`'s `Custom` arm evaluates the curve at `alpha = (dist - extent) / falloff`,
	// so the curve carries `SND_GetGain` itself, sampled at equal-dB steps from `D_ref` out to the
	// audible range.
	FRichCurve* Curve = Att.CustomAttenuationCurve.GetRichCurve();
	check(Curve != nullptr);
	Curve->Reset();
	const float Ratio = AudibleCm / RefCm;                 // == 1 / snd_gain_min
	for (int32 Index = 0; Index <= FalloffCurveKeys; ++Index)
	{
		const float Step = static_cast<float>(Index) / static_cast<float>(FalloffCurveKeys);
		const float DistCm = RefCm * FMath::Pow(Ratio, Step);
		Curve->AddKey(FalloffCurveTail * (DistCm - RefCm) / SpanCm,
			Gain(LevelDb, DistCm / ElysiumMove::U));
	}
	// The compressor's boundary — raw gain exactly 0.5, which is exactly `2 * D_ref` — is the one
	// corner the law has: continuous in value, not in slope. A chord across it overshoots by about
	// 1 dB, so it gets a key of its own rather than more keys everywhere.
	const float KneeCm = RefCm * 2.0f;
	Curve->AddKey(FalloffCurveTail * (KneeCm - RefCm) / SpanCm,
		Gain(LevelDb, KneeCm / ElysiumMove::U));

	// The cut at the floor. With it, `GetMaxDimension()` is `FalloffDistance + extent`, which is
	// `AudibleRangeCm` to four figures; without it the sound is never culled at all.
	Curve->AddKey(1.0f, 0.0f);

	return Att;
}

} // namespace ElysiumSoundLevel
