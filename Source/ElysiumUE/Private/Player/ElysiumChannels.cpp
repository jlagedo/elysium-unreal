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
