#include "ElysiumChannels.h"

namespace ElysiumChannels
{

namespace
{
	// Positions and velocities are emitted in **Source units**, not centimetres, so a row reads
	// directly against `docs/vtmb/source_movement.md`'s own numbers and against the decompile. The
	// tolerances are the ones the movement comparator has always used: positions are held tighter
	// than velocities because a position error is cumulative and a velocity one is not.
	const FChannelDef GDefs[] =
	{
	// --- The movement producer, per frame -------------------------------------------------------
	{ TEXT("frame"),    TEXT("move"), EScope::Frame, EKind::Exact,   0.0f,    0, TEXT(""),
	  true,  TEXT("sample index within the course") },
	{ TEXT("seq"),      TEXT("move"), EScope::Frame, EKind::Exact,   0.0f,    0, TEXT(""),
	  true,  TEXT("the replayed user command's sequence number") },
	{ TEXT("dt"),       TEXT("move"), EScope::Frame, EKind::Numeric, 1e-6f,   6, TEXT("s"),
	  true,  TEXT("the step the frame was integrated at") },
	{ TEXT("px"),       TEXT("move"), EScope::Frame, EKind::Numeric, 0.5f,    4, TEXT("u"),
	  true,  TEXT("origin X") },
	{ TEXT("py"),       TEXT("move"), EScope::Frame, EKind::Numeric, 0.5f,    4, TEXT("u"),
	  true,  TEXT("origin Y") },
	{ TEXT("pz"),       TEXT("move"), EScope::Frame, EKind::Numeric, 0.25f,   4, TEXT("u"),
	  true,  TEXT("origin Z") },
	{ TEXT("vx"),       TEXT("move"), EScope::Frame, EKind::Numeric, 2.0f,    4, TEXT("u/s"),
	  true,  TEXT("velocity X") },
	{ TEXT("vy"),       TEXT("move"), EScope::Frame, EKind::Numeric, 2.0f,    4, TEXT("u/s"),
	  true,  TEXT("velocity Y") },
	{ TEXT("vz"),       TEXT("move"), EScope::Frame, EKind::Numeric, 2.0f,    4, TEXT("u/s"),
	  true,  TEXT("velocity Z") },
	{ TEXT("speed2d"),  TEXT("move"), EScope::Frame, EKind::Numeric, 2.0f,    4, TEXT("u/s"),
	  true,  TEXT("horizontal speed") },
	{ TEXT("onground"), TEXT("move"), EScope::Frame, EKind::Exact,   0.0f,    0, TEXT(""),
	  true,  TEXT("CategorizePosition's answer") },
	{ TEXT("ducked"),   TEXT("move"), EScope::Frame, EKind::Exact,   0.0f,    0, TEXT(""),
	  true,  TEXT("the hull is the small one") },
	{ TEXT("ducking"),  TEXT("move"), EScope::Frame, EKind::Exact,   0.0f,    0, TEXT(""),
	  true,  TEXT("a duck transition is in flight") },
	{ TEXT("canunduck"),TEXT("move"), EScope::Frame, EKind::Exact,   0.0f,    0, TEXT(""),
	  true,  TEXT("the standing hull would fit here") },
	{ TEXT("water"),    TEXT("move"), EScope::Frame, EKind::Exact,   0.0f,    0, TEXT(""),
	  true,  TEXT("EElysiumWaterLevel") },
	{ TEXT("surffric"), TEXT("move"), EScope::Frame, EKind::Numeric, 0.01f,   3, TEXT(""),
	  true,  TEXT("the ground surface's friction scale") },

	// --- The animation producer, per frame (CCC1) -----------------------------------------------
	// The body sample's two candidate movement yaws, both facing-relative. **Both are recorded from
	// the first day on purpose**: `CCC7` recovers the sign of `move_yaw` by comparing the retail
	// selector's own input against them, and that comparison should run against recordings rather
	// than against fresh instrumentation. Which one the graph steers on is that rung's call, not a
	// property of the recording.
	{ TEXT("move_yaw_wish"), TEXT("anim"), EScope::Frame, EKind::Angle, 1.0f, 3, TEXT("deg"),
	  true,  TEXT("the commanded direction, relative to facing") },
	{ TEXT("move_yaw_vel"),  TEXT("anim"), EScope::Frame, EKind::Angle, 1.0f, 3, TEXT("deg"),
	  true,  TEXT("the realized velocity's direction, relative to facing") },
	// And the pose parameter itself, which is neither of them: the realized yaw after the 720 deg/s
	// slew, the 0.3 s re-arm and the hold at a standstill. Recorded beside its own input so a stride
	// that lags the body reads as the filter doing its job rather than as the resolver picking wrong.
	{ TEXT("move_yaw"),      TEXT("anim"), EScope::Frame, EKind::Angle, 1.0f, 3, TEXT("deg"),
	  true,  TEXT("the pose parameter the grid is steered by: move_yaw_vel, slewed and held") },

	// --- The animation producer, per frame (CCC4) -----------------------------------------------
	// Every discrete value rides as an enum ordinal under `Exact`, so a state flip is a behaviour
	// change and never a rounding one. **The string identities are deliberately not here**: there is
	// no string channel by design, because a value that reaches disk with no comparison rule is what
	// this registry exists to refuse. The label, animation and owning bank ride the run metadata and
	// are asserted by the Content tier, which can compare an identity against the real corpus.
	{ TEXT("act_code"),    TEXT("anim"), EScope::Frame, EKind::Exact,   0.0f,   0, TEXT(""),
	  true,  TEXT("EElysiumAnimActivityCode the classifier chose; 0 is outside the locomotion slice") },
	{ TEXT("act_route"),   TEXT("anim"), EScope::Frame, EKind::Exact,   0.0f,   0, TEXT(""),
	  true,  TEXT("EElysiumAnimRoute the record came from") },
	{ TEXT("act_outcome"), TEXT("anim"), EScope::Frame, EKind::Exact,   0.0f,   0, TEXT(""),
	  true,  TEXT("EElysiumAnimOutcome; 0 is a clean resolve and anything else names the fallback") },
	{ TEXT("act_asset"),   TEXT("anim"), EScope::Frame, EKind::Exact,   0.0f,   0, TEXT(""),
	  true,  TEXT("EElysiumAnimAssetKind the label resolved to") },
	{ TEXT("air_phase"),   TEXT("anim"), EScope::Frame, EKind::Exact,   0.0f,   0, TEXT(""),
	  true,  TEXT("EElysiumAirPhase the jump latch holds; what the sample alone cannot answer") },
	{ TEXT("act_gen"),     TEXT("anim"), EScope::Frame, EKind::Exact,   0.0f,   0, TEXT(""),
	  true,  TEXT("request generation; it advances only when the discrete request changes") },
	{ TEXT("act_stride"),  TEXT("anim"), EScope::Frame, EKind::Numeric, 2.0f,   4, TEXT("u/s"),
	  true,  TEXT("the selected cell's authored ground speed") },
	{ TEXT("act_fade"),    TEXT("anim"), EScope::Frame, EKind::Numeric, 0.001f, 3, TEXT("s"),
	  true,  TEXT("the authored transition duration the selection carries; 0 is a snap") },

	// --- The camera producer, per frame (CCC2) --------------------------------------------------
	// The faithful evaluator's solve, read off the settled sample the camera manager publishes
	// rather than re-derived, so a recording cannot disagree with the view that was rendered.
	//
	// Every one of these is speed-dependent by the registry's own invariant, which is not a
	// formality here: the damper's position is a function of how fast the body moved, so `CCC7`
	// really can move all of them. Their comparison therefore lives in the sited runs — whose
	// baselines are full traces — and in the cross-rate `--hz` run, which is what actually asserts
	// that the damper settles the same way at 60 and 120 Hz.
	{ TEXT("cam_boom"),  TEXT("camera"), EScope::Frame, EKind::Numeric, 0.5f,  3, TEXT("u"),
	  true,  TEXT("boom length, third-person weight applied") },
	{ TEXT("cam_damp"),  TEXT("camera"), EScope::Frame, EKind::Numeric, 0.5f,  3, TEXT("u"),
	  true,  TEXT("the damper's own distance from the eye, before the weight") },
	{ TEXT("cam_pitch"), TEXT("camera"), EScope::Frame, EKind::Angle,   1.0f,  3, TEXT("deg"),
	  true,  TEXT("the solved boom pitch") },
	{ TEXT("cam_yaw"),   TEXT("camera"), EScope::Frame, EKind::Angle,   1.0f,  3, TEXT("deg"),
	  true,  TEXT("the solved boom yaw") },
	{ TEXT("cam_clip"),  TEXT("camera"), EScope::Frame, EKind::Exact,   0.0f,  0, TEXT(""),
	  true,  TEXT("the collision sweep hit this frame") },
	{ TEXT("cam_third"), TEXT("camera"), EScope::Frame, EKind::Numeric, 0.01f, 3, TEXT(""),
	  true,  TEXT("the third-person blend weight") },

	// The modern rig, recorded beside the faithful one **whether or not `elysium.ModernCamera` has
	// it supplying the base**. That is the whole instrument: one deterministic run carries both
	// booms, so the co-tune diffs them against each other rather than against a recollection, and
	// a rig regression is the same kind of diff as a movement one.
	{ TEXT("mcam_boom"),  TEXT("camera"), EScope::Frame, EKind::Numeric, 0.5f,  3, TEXT("u"),
	  true,  TEXT("modern boom length, third-person weight applied") },
	{ TEXT("mcam_damp"),  TEXT("camera"), EScope::Frame, EKind::Numeric, 0.5f,  3, TEXT("u"),
	  true,  TEXT("the modern damper's own distance from the eye, before the weight") },
	{ TEXT("mcam_pitch"), TEXT("camera"), EScope::Frame, EKind::Angle,   1.0f,  3, TEXT("deg"),
	  true,  TEXT("the modern boom pitch") },
	{ TEXT("mcam_yaw"),   TEXT("camera"), EScope::Frame, EKind::Angle,   1.0f,  3, TEXT("deg"),
	  true,  TEXT("the modern boom yaw") },
	{ TEXT("mcam_clip"),  TEXT("camera"), EScope::Frame, EKind::Exact,   0.0f,  0, TEXT(""),
	  true,  TEXT("the modern collision sweep hit this frame") },

	// --- The movement producer, per run ---------------------------------------------------------
	// These are the gym's actual assertions, and they are written to **saturate**: a body either
	// climbs a riser or is stopped by it, and either answer is reached at any gait given a long
	// enough course. That is what makes them survive `CCC7` when no per-frame trace can.
	{ TEXT("advance_max"),  TEXT("move"), EScope::Run, EKind::Numeric, 0.5f,  3, TEXT("u"),
	  false, TEXT("furthest distance reached along the start heading") },
	{ TEXT("top_stand"),    TEXT("move"), EScope::Run, EKind::Numeric, 0.25f, 3, TEXT("u"),
	  false, TEXT("highest the feet stood, above the start") },
	{ TEXT("reach_max"),    TEXT("move"), EScope::Run, EKind::Numeric, 0.25f, 3, TEXT("u"),
	  false, TEXT("highest the feet reached at all, above the start") },
	{ TEXT("peak_rise"),    TEXT("move"), EScope::Run, EKind::Numeric, 0.25f, 3, TEXT("u"),
	  false, TEXT("highest rise above a takeoff, per airborne span") },
	{ TEXT("ground_transitions"), TEXT("move"), EScope::Run, EKind::Exact, 0.0f, 0, TEXT(""),
	  false, TEXT("how many times the ground state flipped") },
	{ TEXT("ended_ducked"), TEXT("move"), EScope::Run, EKind::Exact,   0.0f,  0, TEXT(""),
	  false, TEXT("the body was still ducked on the last frame") },
	{ TEXT("frames"),       TEXT("move"), EScope::Run, EKind::Exact,   0.0f,  0, TEXT(""),
	  false, TEXT("how many samples the course produced") },
	{ TEXT("peak_speed2d"), TEXT("move"), EScope::Run, EKind::Numeric, 2.0f,  3, TEXT("u/s"),
	  true,  TEXT("fastest horizontal speed reached") },

	// --- The leniency measurement, per run (CCC3) -----------------------------------------------
	// Written only by the courses that place a jump against a body event, because a course that
	// merely *holds* jump re-fires on landing at a gait-dependent moment and its count would not
	// survive `CCC7`. On a leniency course the count is 0 or 1 at any gait, which is what makes it
	// the committed boolean: `ledge_p1` going from 0 to 1 is coyote time being added, and nothing
	// else. `event_frame` records what the probe pass resolved the press against — *when* the body
	// reaches the lip does move with the gait, so it is recorded and deferred rather than compared.
	{ TEXT("jumps_taken"),  TEXT("move"), EScope::Run, EKind::Exact,   0.0f,  0, TEXT(""),
	  false, TEXT("press-edge jumps the body took") },
	{ TEXT("event_frame"),  TEXT("move"), EScope::Run, EKind::Exact,   0.0f,  0, TEXT(""),
	  true,  TEXT("probe-resolved body-event frame the jump was placed against; -1 if not found") },

	// --- The camera producer, per run (CCC2) ----------------------------------------------------
	// The one camera assertion a **committed** gym baseline can carry, and it saturates the way the
	// movement run channels do: the harness puts the body in third person, so the weight reaches 1
	// on every course at any gait. It is worth a row because it is the cheap catch for the camera
	// never engaging at all — which would otherwise leave every frame channel a plausible zero.
	{ TEXT("cam_third_max"), TEXT("camera"), EScope::Run, EKind::Numeric, 0.01f, 3, TEXT(""),
	  false, TEXT("highest third-person weight the course reached") },

	// --- The animation producer, per run (CCC4) -------------------------------------------------
	// The first two saturate the way the movement run channels do: "did every frame resolve" has the
	// same answer at any gait, which is what makes them the assertion a **committed** gym baseline
	// can actually carry — and the cheap catch for the resolver going dark, which would otherwise
	// leave every frame column a plausible zero. `act_codes` is the opposite and is marked so: halve
	// the walk speed and a course that ran now walks, so *which* activities it reaches genuinely
	// moves with `CCC7`.
	{ TEXT("act_resolved"),  TEXT("anim"), EScope::Run, EKind::Exact, 0.0f, 0, TEXT(""),
	  false, TEXT("frames whose selection resolved to an asset") },
	{ TEXT("act_fallbacks"), TEXT("anim"), EScope::Run, EKind::Exact, 0.0f, 0, TEXT(""),
	  false, TEXT("frames whose outcome was not a clean resolve") },
	{ TEXT("act_codes"),     TEXT("anim"), EScope::Run, EKind::Exact, 0.0f, 0, TEXT(""),
	  true,  TEXT("bitmask of the activity codes the course reached") },
	};
}

TArrayView<const FChannelDef> Defs()
{
	return MakeArrayView(GDefs);
}

const FChannelDef* Find(const TCHAR* Name)
{
	if (!Name)
	{
		return nullptr;
	}
	for (const FChannelDef& Def : GDefs)
	{
		if (FCString::Strcmp(Def.Name, Name) == 0)
		{
			return &Def;
		}
	}
	return nullptr;
}

const TCHAR* KindName(EKind Kind)
{
	switch (Kind)
	{
	case EKind::Numeric: return TEXT("numeric");
	case EKind::Angle:   return TEXT("angle");
	default:             return TEXT("exact");
	}
}

const TCHAR* ScopeName(EScope Scope)
{
	return Scope == EScope::Frame ? TEXT("frame") : TEXT("run");
}

} // namespace ElysiumChannels
