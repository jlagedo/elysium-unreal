#include "Substrate/ElysiumFootsteps.h"

#include "ElysiumEntityWorld.h"           // the NPC gate's three world predicates
#include "ElysiumSurfaceSounds.h"         // the step pools the coin flip draws from
#include "Substrate/ElysiumGameSound.h"   // the six PLAYER_* category names
#include "Substrate/ElysiumRulebook.h"    // FElysiumClanTemplate::GeneralFloat

namespace ElysiumFootsteps
{

// --- NPC (0x1026d460) ---------------------------------------------------------------------------
//
// Owned by B1. Do not edit this region from the player lane.

FStepSource NpcSource(const FElysiumClanTemplate* Template, bool bHeavy,
	const FElysiumFootstepTuning& Tuning)
{
	// `1026d4ba`: `if (footstep_npc_use_templates == 0) goto cvars`. The cvar arm is the whole of
	// `1026d551`...`1026d58a` and reads nothing else.
	if (!Tuning.bNpcUseTemplates)
	{
		return FStepSource{ Tuning.VolumeFor(bHeavy), Tuning.DistanceUnitsFor(bHeavy) };
	}

	// The template arm. `bHeavy` selects the pair, exactly as `1026d4d6` (normal) and `1026d51c`
	// (heavy) write the same two stack slots the emit reads back at `1026d66e`.
	const TCHAR* const VolKey = bHeavy ? ElysiumFootstep::KeyHeavyVol : ElysiumFootstep::KeyNormalVol;
	const TCHAR* const DistKey = bHeavy ? ElysiumFootstep::KeyHeavyDist : ElysiumFootstep::KeyNormalDist;
	const float VolDefault = bHeavy
		? ElysiumFootstep::TemplateHeavyVolDefault : ElysiumFootstep::TemplateNormalVolDefault;
	const float DistDefault = bHeavy
		? ElysiumFootstep::TemplateHeavyDistDefault : ElysiumFootstep::TemplateNormalDistDefault;

	if (Template == nullptr)
	{
		// No record to read. `0x101d3f10` writes these four into every record it loads, so the
		// defaults ARE what a template answers; an NPC with no `stattemplate` steps like a template
		// that authored nothing rather than like the cvars.
		return FStepSource{ VolDefault, DistDefault };
	}
	return FStepSource{ Template->GeneralFloat(VolKey, VolDefault),
		Template->GeneralFloat(DistKey, DistDefault) };
}

bool NpcStepsMuted(const FElysiumEntityWorld& World)
{
	// `0x10014371` -> `0x1017d630`: the player's camera VIEW entity (`+0x19c0`), and `0x10014ad3`:
	// the camera TARGET (`+0x19cc`). Both are the `camera_track` override channel's two roles;
	// the world composes them into one value shot, and either role standing is the fact retail
	// reads off those two handles.
	if (World.HasTrackCamera())
	{
		return true;
	}
	// `0x1000306c`: the cine camera (`+0x19b4`) with `+0x1ec4 > 0` -- `CBasePlayer::GetCineCamera`
	// (`FUN_1017cf90`). Every adopter of that one slot lands here: `SetCamera`, the dialogue
	// opener, `camera_cinematic`'s `StartShot`, the terminal, keypad, hacking and lock sessions, and
	// anim event 4050 -- which is why the gate is the slot and not the open conversation.
	return World.CineCameraEntity().IsSet();
}

const FString* PickNpcWav(const FElysiumSurfaceSounds& Sounds, FRandomStream& Stream)
{
	// `1026d626`: `if (RandomInt(0,1)) sample = +0x2c /* stepleft */; else sample = +0x2e`.
	const TArray<FString>& Pool =
		Stream.RandRange(0, 1) != 0 ? Sounds.StepLeft : Sounds.StepRight;
	if (Pool.IsEmpty())
	{
		// `1026d666`: `if (!name || !*name) return;` — the chosen side names no script. A real,
		// resolved surface can answer this on one foot and not the other.
		return nullptr;
	}
	// The variation pick retail leaves to the sound engine (the soundscript's own repeats), made
	// here because the bake flattened the script into this pool. Duplicates are alternates and are
	// legal picks: a pool of `[a, a, b]` is authored to play `a` twice as often.
	return &Pool[Stream.RandRange(0, Pool.Num() - 1)];
}

// --- The species table --------------------------------------------------------------------------
//
// `docs/vtmb/footsteps.md` §1.7 and §5.1. Fixed wav pools: not one of these overrides reads a
// `surfacedata_t`, a template or a cvar.

namespace
{
	// The Tzimisce vfunc `+0x9ac` (`0x103c4160`): `foot_steps_{1,2}` or `{3,4}` by the flag the
	// caller passes, then `RandomInt(0, 1)` inside the chosen pair.
	//
	// RECOVERED, story 29e, from `0x103c4160`'s own listing (`103c41d8` / `103c420e` / `103c424b`)
	// while reading the damaged `CNPC_VTzimisceRunner::HandleAnimEvent 0x103c32c0`: the flag the
	// caller passes selects **flag 0 → `foot_steps_1/2`** and **flag 1 → `foot_steps_3/4`**. Anim
	// event 2051 passes 0 (`103c32d9`) and 2050 passes 1 (`103c32eb`), so 2051 is the `1/2` pair.
	// The port had the sense backwards and the two arrays are swapped here to match; the arrays
	// keep their `Left`/`Right` names because that is the foot each anim event stands for.
	const TCHAR* const TzimisceLeftSteps[] = {
		TEXT("character/monster/TC_Runner/foot_steps_3.wav"),
		TEXT("character/monster/TC_Runner/foot_steps_4.wav"),
	};
	const TCHAR* const TzimisceRightSteps[] = {
		TEXT("character/monster/TC_Runner/foot_steps_1.wav"),
		TEXT("character/monster/TC_Runner/foot_steps_2.wav"),
	};
	// The SECOND sound the same call emits, `RandomInt(0, 3)`.
	const TCHAR* const TzimisceBreaths[] = {
		TEXT("character/monster/TC_Runner/Breath1.wav"),
		TEXT("character/monster/TC_Runner/Breath2.wav"),
		TEXT("character/monster/TC_Runner/Breath3.wav"),
		TEXT("character/monster/TC_Runner/Breath4.wav"),
	};

	// The table. One row: `CNPC_VTzimisceRunner`'s. The three other event-driven species bodies
	// (`CNPC_VMingXiao 0x10392a70`, `CNPC_VHengeyokai 0x1037fb60`, `CNPC_VTzimisceHeadClaw
	// 0x103c1540`) are ported whole as their classes' slot-259 overrides in
	// `ElysiumNpcMisc19Species.cpp` (story 8 lane L11) and read no row here.
	//
	// `CNPC_VSabbatLeader::FootstepSound 0x103aa5e0` is not a row either: its anim-event caller is the
	// class's own slot-259 body (`0x103a7000`, 0x802/0x803), also in `ElysiumNpcMisc19Species.cpp`.
	const FElysiumFootstepSpecies GSpeciesTable[] = {
		// `CNPC_VTzimisceRunner::HandleAnimEvent 0x103c32c0` — 2050/2051 only, NO shake, the `+0x9ac`
		// pools. The run pair falls through to the base handler.
		FElysiumFootstepSpecies{
			TEXT("npc_VTzimisceRunner"), TEXT("CNPC_VTzimisceRunner"), TEXT("0x103c32c0"),
			EElysiumFootstepPolicy::CustomWav, /*bClaimsRun*/ false,
			0.0f, 0.0f, 0.0f, 0.0f,
			TArrayView<const TCHAR* const>(TzimisceLeftSteps, UE_ARRAY_COUNT(TzimisceLeftSteps)),
			TArrayView<const TCHAR* const>(TzimisceRightSteps, UE_ARRAY_COUNT(TzimisceRightSteps)),
			TArrayView<const TCHAR* const>(TzimisceBreaths, UE_ARRAY_COUNT(TzimisceBreaths)) },
	};

	// Which rows have already said their shake is unbuilt. Process-wide and unsynchronised — the
	// anim-event census's own posture, and for its reason: every writer is the world's event pass.
	TSet<FString>& ShakeReports()
	{
		static TSet<FString> Reported;
		return Reported;
	}
}

const FElysiumFootstepSpecies* SpeciesFor(const FString& Classname)
{
	if (Classname.IsEmpty())
	{
		return nullptr;
	}
	for (const FElysiumFootstepSpecies& Row : GSpeciesTable)
	{
		if (Classname.Equals(Row.Classname, ESearchCase::IgnoreCase))
		{
			return &Row;
		}
	}
	return nullptr;
}

bool SpeciesClaims(const FElysiumFootstepSpecies& Row, int32 EventId)
{
	if (!IsFootstepEvent(EventId))
	{
		return false;
	}
	return Row.bClaimsRun || !IsHeavyFootstep(EventId);
}

const TCHAR* PickSpeciesWav(const FElysiumFootstepSpecies& Row, int32 EventId,
	FRandomStream& Stream)
{
	if (Row.Policy == EElysiumFootstepPolicy::Silent)
	{
		// `CNPC_VMingXiao`'s arm is a bare `return`: no draw, no sound.
		return nullptr;
	}
	const TArrayView<const TCHAR* const>& Pool =
		IsLeftFootstep(EventId) ? Row.LeftWavs : Row.RightWavs;
	if (Pool.IsEmpty())
	{
		return nullptr;
	}
	return Pool[Stream.RandRange(0, Pool.Num() - 1)];
}

const TCHAR* PickSpeciesExtraWav(const FElysiumFootstepSpecies& Row, FRandomStream& Stream)
{
	if (Row.ExtraWavs.IsEmpty())
	{
		return nullptr;
	}
	return Row.ExtraWavs[Stream.RandRange(0, Row.ExtraWavs.Num() - 1)];
}

void ReportUnimplementedShake(const FElysiumFootstepSpecies& Row)
{
	if (Row.ShakeAmplitude <= 0.0f)
	{
		return;
	}
	bool bAlready = false;
	ShakeReports().Add(FString(Row.Classname), &bAlready);
	if (bAlready)
	{
		return;
	}
	UE_LOG(LogElysiumFootsteps, Warning,
		TEXT("UNIMPLEMENTED footstep screenshake — %s (%s %s) raises "
			"UTIL_ScreenShake(amp %.2f, freq %.2f, %.2f s, radius %.0f) on every claimed footfall; "
			"this runtime plays the wavs and does not move the camera. The shake seam belongs to "
			"the 2100/2101 werewolf group (werewolf_footstep_shakes 0x103d8ba0)."),
		Row.Classname, Row.RetailClass, Row.RetailAddress,
		Row.ShakeAmplitude, Row.ShakeFrequency, Row.ShakeDurationSeconds, Row.ShakeRadiusUnits);
}

void ResetUnimplementedShakeReports()
{
	ShakeReports().Reset();
}

// --- Player (0x1011e940 / 0x1011e430 / 0x10125db0) ----------------------------------------------

const FName& LadderSurface()
{
	// `GetSurfaceIndex("ladder")`, string at `0x10572554`.
	static const FName Name(TEXT("ladder"));
	return Name;
}

const FName& WaterSurface()
{
	// `GetSurfaceIndex("water")`, string at `0x10572544`.
	static const FName Name(TEXT("water"));
	return Name;
}

const FName& WadeSurface()
{
	// `GetSurfaceIndex("wade")`, string at `0x1057254c`.
	static const FName Name(TEXT("wade"));
	return Name;
}

float MaxSafeFallSpeedUnits(float JumpBoost, float Gravity, float BaseJumpVelocity,
	float VerticalScalar, float JumpDurationSeconds, float FallThreshold)
{
	// `0x10125e04`..`0x10125e9d`, recomputed on every `CheckFalling` call rather than cached.
	const float Speed = FMath::Sqrt(2.0f * JumpBoost * Gravity)
		+ BaseJumpVelocity * VerticalScalar
		+ 0.75f * BaseJumpVelocity * VerticalScalar * JumpDurationSeconds;   // 0.75 at `0x10462958`
	return FMath::Max(Speed, FallThreshold);
}

float PlayerDryVolume(const FString& GameMaterial, bool bWalking, bool bDucked, float PcVol)
{
	// The jump table at `0x1011ed90` is over `'D'`..`'V'` and only two letters have their own
	// entry; `'E'`..`'U'` and anything outside the range fall to the default pair. The exporter
	// writes the authored letter uppercase (`GAME_MATERIALS` in
	// `pipeline/.../importers/surface_properties.py`), so the fold below cannot change a shipped
	// answer — it is there so a hand-authored lowercase row in a test reads the same.
	const TCHAR Letter = GameMaterial.IsEmpty()
		? TEXT('\0') : FChar::ToUpper(GameMaterial[0]);

	float Volume = bWalking ? kVolDefaultWalk : kVolDefaultRun;
	if (Letter == TEXT('D'))
	{
		Volume = bWalking ? kVolDirtWalk : kVolDirtRun;
	}
	else if (Letter == TEXT('V'))
	{
		Volume = bWalking ? kVolVentWalk : kVolVentRun;
	}

	// The common tail (`0x1011ec9c` onward). It is NOT dry-only: retail runs it past the switch,
	// so a ladder step is 0.35 * 0.5 and a ducked water step is 1.0 * 0.35 * 0.5.
	if (bDucked)
	{
		Volume *= kVolDuckScale;
	}
	Volume *= PcVol;
	return FMath::Min(Volume, kVolClamp);
}

namespace
{
	// The tail alone, for the three arms whose volume is a constant rather than a material lookup.
	float ApplyVolumeTail(float Volume, bool bDucked, float PcVol)
	{
		if (bDucked)
		{
			Volume *= kVolDuckScale;
		}
		Volume *= PcVol;
		return FMath::Min(Volume, kVolClamp);
	}
}

bool AdvanceStepClock(float& StepSoundMs, float DtMs, int32& WadePhase, const FStepClockIn& In,
	FStepClockOut& Out)
{
	Out = FStepClockOut();

	// `CGameMovement::ReduceTimers` (`0x1011f520`), once per move: `m_flStepSoundTime -=
	// frametime * 1000`, **clamped at zero**. The clamp is not a tidiness detail — it is what makes
	// early return #7 (`speed3D < velwalk && m_flStepSoundTime != 0`) unreachable, and therefore
	// what makes `velwalk` gate nothing at all.
	if (StepSoundMs > 0.0f)
	{
		StepSoundMs -= DtMs;
		if (StepSoundMs < 0.0f)
		{
			StepSoundMs = 0.0f;
		}
	}

	// #1 `0x1011e94c` — the clock has not come due.
	if (StepSoundMs > 0.0f)
	{
		return false;
	}
	// #4 `0x1011e986` — `sv_footsteps` gates the WHOLE clock, unconditionally and regardless of
	// `maxClients`, unlike `PlayStepSound`'s own copy of the test.
	if (!In.bServerFootsteps)
	{
		return false;
	}
	// #5 `0x1011ea62` — not on a ladder, not standing on anything, and not in water.
	if (!In.bOnLadder && !In.bOnGround && In.Water == EElysiumWaterLevel::None)
	{
		return false;
	}
	// #6 `0x1011eaa5` — the only surviving speed gate: moving at all, horizontally.
	if (In.Speed2D <= 0.0f)
	{
		return false;
	}
	// #7 `0x1011eabb` is DEAD (see the clamp above) and is deliberately not written here.

	// The bands (`0x1011ea16`..`0x1011ea58`). Ducking, a ladder and ANY water level all take the
	// slow pair — and, crucially, the `flduck` of 100 that the tail adds to the interval.
	const bool bDuckBand = In.bDucked || In.bOnLadder || In.Water != EElysiumWaterLevel::None;
	const float VelRun = bDuckBand ? kDuckedVelRun : kNormalVelRun;
	const float FlDuck = bDuckBand ? kDuckedFlDuck : kNormalFlDuck;
	const bool bWalking = In.Speed3D < VelRun;   // `0x1011eae6`

	float Interval = 0.0f;
	float Volume = 0.0f;

	if (In.bOnLadder)
	{
		Out.Pool = EStepPool::Ladder;
		Interval = kIntervalLadderMs;
		Volume = ApplyVolumeTail(kVolLadder, In.bDucked, In.PcVol);
	}
	else if (In.Water >= EElysiumWaterLevel::Waist)
	{
		// The wade branch's four-phase counter (`0x1011eb42`, global `0x1070b898`). Phase 0 returns
		// **before the clock is re-armed**, so the next move retries immediately and the silence is
		// one step rather than one interval. The cycle is silent, sound, sound, sound.
		if (WadePhase == 0)
		{
			WadePhase = 1;
			Out.Pool = EStepPool::Wade;
			Out.bSilentPhase = true;
			return false;
		}
		WadePhase = (WadePhase == kWadePhases - 1) ? 0 : WadePhase + 1;

		Out.Pool = EStepPool::Wade;
		Interval = kIntervalWadeMs;
		Volume = ApplyVolumeTail(kVolWade, In.bDucked, In.PcVol);
	}
	else if (In.Water == EElysiumWaterLevel::Feet)
	{
		Out.Pool = EStepPool::Water;
		Interval = bWalking ? kIntervalWaterWalkMs : kIntervalWaterRunMs;
		Volume = ApplyVolumeTail(kVolWater, In.bDucked, In.PcVol);
	}
	else
	{
		Out.Pool = EStepPool::Dry;
		Interval = bWalking ? kIntervalDryWalkMs : kIntervalDryRunMs;
		if (In.Speed2D <= kSlowSpeed2DUnits)
		{
			Interval = kIntervalSlowMs;
		}
		Volume = PlayerDryVolume(In.GameMaterial, bWalking, In.bDucked, In.PcVol);
	}

	// `m_flStepSoundTime += flduck` (`0x1011ec9c`, `FLD [ESP+0x1c]`). The decompiled C at this
	// address renders `velwalk` here; the listing loads the `flduck` local, which is 0 standing dry
	// and 100 in every other band.
	Out.NextIntervalMs = Interval + FlDuck;
	StepSoundMs = Out.NextIntervalMs;
	Out.Volume = Volume;

	// **The dry arm's final test is dead and the step always plays.** Retail's tail reads a button
	// mask and then compares `speed2D` against `[ESP+0x18]` — the slot that held `velrun` until the
	// dry arm overwrote it at `0x1011ebf2` with the INTEGER interval so it could `FILD` it. Read
	// back as a float at `0x1011ed52` the slot is a denormal (~4.2e-43), so the compare can never
	// refuse, and the ladder and water arms jump straight past it anyway. The port mirrors the
	// observable behaviour and does not reproduce the clobber (`docs/vtmb/footsteps.md` §2.2).
	return true;
}

float LandingStepVolume(float& FallSpeedUnits, bool bInWater, bool bGroundIsFloating,
	float MaxSafeFallSpeed, float SafeFallSpeed)
{
	// `CheckFalling` (`0x10125db0`), the volume ladder, in retail's own order.
	if (FallSpeedUnits < MaxSafeFallSpeed)
	{
		// SOFT. `mv[0xc0] = (mv[0xc0] < 5) ? 0 : 8` beside it — the animation state the hearing
		// stimulus reads as `PLAYER_LAND_SOFT`.
		return FallSpeedUnits > 0.0f ? kLandVolSoft : 0.0f;
	}

	if (bInWater)
	{
		// Landed in water: `clamp(fallVel / SafeFallSpeed, 0, 1)` (`0x10125f25`), anim state 9.
		return FMath::Clamp(FallSpeedUnits / SafeFallSpeed, 0.0f, 1.0f);
	}

	// HARD, dry. The floating reduction is a real write to `m_flFallVelocity` (`0x104629e4`), which
	// is why it is applied to the caller's value and not to a copy: it is also the ONLY way the
	// `0.5` and `0.65` arms below are reachable with the shipped rules.
	if (bGroundIsFloating)
	{
		FallSpeedUnits -= kFloatingFallReduction;
	}

	if (FallSpeedUnits > SafeFallSpeed)
	{
		// FATAL band. `rules.SupernaturalFallSpeed` is read and DISCARDED here (`0x10125ffd`).
		return kLandVolFatal;
	}
	if (FallSpeedUnits > SafeFallSpeed * 0.5f)   // 0.5 at `0x104454d0`
	{
		return kLandVolHard;
	}
	// **Inverted, as shipped** (`0x101260ab` `FCOMP 200.0`, `JP` -> 0.5, fall-through -> 0.65): the
	// faster fall is the quieter one.
	return FallSpeedUnits >= kFallPunchThreshold ? kLandVolMid : kLandVolLow;
}

FName HearingCategory(const FHearingIn& In)
{
	// `CBasePlayer::UpdatePlayerSound` (`0x1016b480`), priority order verbatim. Note what is NOT
	// here: the water level (never read — wading emits footsteps, swimming emits nothing because it
	// has no ground contact), the stealth feat, the light level and the player's stealth surface.
	// **Ducking is the whole of "sneaking"** on this path.
	if (In.bJumpHeld)
	{
		return ElysiumGameSounds::PlayerJump();
	}
	if (!In.bOnGround)
	{
		// In the air and not jumping: the volume stays 0.
		return NAME_None;
	}
	if (In.bLandSoft)
	{
		return ElysiumGameSounds::PlayerLandSoft();
	}
	if (In.bLandHard)
	{
		return ElysiumGameSounds::PlayerLandHard();
	}
	if (In.Speed3D <= 0.0f)
	{
		return NAME_None;
	}
	if (In.bDucked)
	{
		return ElysiumGameSounds::PlayerFootstepSneak();
	}
	// Strict `>`, against `PLAYER_RUN_SPEED` from the table's `MiscData`.
	return In.Speed3D > In.RunSpeedUnits
		? ElysiumGameSounds::PlayerFootstepRun() : ElysiumGameSounds::PlayerFootstepWalk();
}

float HearingRadiusUnits(FName Category)
{
	if (Category == ElysiumGameSounds::PlayerFootstepSneak()) { return kHearingRadiusSneak; }
	if (Category == ElysiumGameSounds::PlayerFootstepWalk())  { return kHearingRadiusWalk; }
	if (Category == ElysiumGameSounds::PlayerFootstepRun())   { return kHearingRadiusRun; }
	if (Category == ElysiumGameSounds::PlayerJump())          { return kHearingRadiusJump; }
	if (Category == ElysiumGameSounds::PlayerLandSoft())      { return kHearingRadiusLandSoft; }
	if (Category == ElysiumGameSounds::PlayerLandHard())      { return kHearingRadiusLandHard; }
	return 0.0f;
}

float DecayHearingRadiusUnits(float CurrentUnits, float TargetUnits, float DeltaSeconds)
{
	// `0x1016b5xx`: `if (v < vol) v = vol; else if (v > vol) { v -= frametime * 250; if (v < vol)
	// v = vol; }`. Instantly up, 250 units per second down.
	if (CurrentUnits < TargetUnits)
	{
		return TargetUnits;
	}
	if (CurrentUnits > TargetUnits)
	{
		const float Decayed = CurrentUnits - FMath::Max(0.0f, DeltaSeconds) * kHearingDecayUnitsPerSecond;
		return Decayed < TargetUnits ? TargetUnits : Decayed;
	}
	return CurrentUnits;
}

bool PlayerSwallows(int32 EventId)
{
	// `CBasePlayer::HandleAnimEvent`: 2050 (`0x802`) .. 2053 (`0x805`) are claimed and nothing is
	// played. The player's clips carry the same footfall records the cast's do; the player steps
	// off `UpdateStepSound`'s millisecond clock instead, which is why claiming them is a behaviour
	// and not a census tidy-up.
	return EventId >= 2050 && EventId <= 2053;
}

} // namespace ElysiumFootsteps
