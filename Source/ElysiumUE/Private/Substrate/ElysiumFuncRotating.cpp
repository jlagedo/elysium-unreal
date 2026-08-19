// func_rotating — VtMB CFuncRotating (vampire.dll CFuncRotating::Spawn FUN_100bfaa0)
//
// The continuous spinner (fans, gears, clock hands, the sky's cloud/lightning rotators). Not a
// FElysiumMoverBase leaf: that base drives finite constant-velocity moves to a target transform,
// while this turns forever about one axis. It drives the same brush Body, so the adopted baked mesh
// (UElysiumBrushComponent::SetVisual) and every `parentname` child ride the rotation.

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumClassFields.h"

namespace
{
	// func_rotating spawnflag bits (B.5 / animation_and_movers.md, read off CFuncRotating::Spawn).
	// The two axis bits carry Source's own misleading names: the flag Hammer labels "Z axis" spins
	// the brush about X, and the one labelled "X axis" spins it about Y. Spawn stores each as a
	// QAngle *component selector* (pitch, yaw, roll) in m_vecMoveAng, not as a direction — so the
	// Z_AXIS flag's Vector(0,0,1) picks roll (a turn about X), and the unflagged default's
	// Vector(0,1,0) picks yaw (a turn about Z). The names below are the flags'; SourceSpinAxis
	// resolves what each one actually turns.
	constexpr int32 SF_ROT_START_ON  = 0x1;
	constexpr int32 SF_ROT_REVERSE   = 0x2;
	constexpr int32 SF_ROT_Z_AXIS    = 0x4;
	constexpr int32 SF_ROT_X_AXIS    = 0x8;
	constexpr int32 SF_ROT_ACCDEC    = 0x10;   // inferred; fanfriction ramps are not implemented
	constexpr int32 SF_ROT_HURT      = 0x20;   // crush touch; `dmg` is 0 on every exported instance
	constexpr int32 SF_ROT_NOT_SOLID = 0x40;   // solidity is applied at body build, not here

	// The spin axis, as a raw-Source direction, after resolving the QAngle component each flag
	// selects. Every exported instance agrees: the ceiling fans, the junkyard fan, the la_hub blade
	// and the sky's cloud/lightning rotators are all unflagged and turn about Z, while the wall
	// clocks' three hands carry Z_AXIS and sweep about the wall normal, X.
	FVector SourceSpinAxis(int32 SpawnFlags)
	{
		if (SpawnFlags & SF_ROT_Z_AXIS) { return FVector(1.f, 0.f, 0.f); }   // roll
		if (SpawnFlags & SF_ROT_X_AXIS) { return FVector(0.f, 1.f, 0.f); }   // pitch
		return FVector(0.f, 0.f, 1.f);                                       // yaw — the default
	}

	FString DescribeRotatingSpawnFlags(int32 SF)
	{
		if (SF == 0)
		{
			return TEXT("(none)");
		}
		TArray<FString> On;
		auto Add = [&](int32 Bit, const TCHAR* Name) { if (SF & Bit) { On.Add(Name); } };
		Add(SF_ROT_START_ON, TEXT("START_ON"));
		Add(SF_ROT_REVERSE, TEXT("REVERSE"));
		Add(SF_ROT_Z_AXIS, TEXT("Z_AXIS"));
		Add(SF_ROT_X_AXIS, TEXT("X_AXIS"));
		Add(SF_ROT_ACCDEC, TEXT("ACCDEC"));
		Add(SF_ROT_HURT, TEXT("HURT"));
		Add(SF_ROT_NOT_SOLID, TEXT("NOT_SOLID"));
		// The remaining 0x80/0x100/0x200 are the small/medium/large sound radii.
		if (SF & 0x80)  { On.Add(TEXT("SND_SMALL")); }
		if (SF & 0x100) { On.Add(TEXT("SND_MEDIUM")); }
		if (SF & 0x200) { On.Add(TEXT("SND_LARGE")); }
		return FString::Join(On, TEXT(" | "));
	}
}

class FElysiumFuncRotating final : public FElysiumEntity
{
public:
	float MaxSpeed = 0.0f;      // `maxspeed`, degrees/second (the authored rate)
	float FanFriction = 0.0f;   // `fanfriction`, spin-down percent — stored, no ramp implemented
	float Volume = 0.0f;        // `volume`  — the running-sound loudness (no rotating sound yet)
	float Dmg = 0.0f;           // `dmg`     — crush damage for the HURT flag (0 on every instance)
	int32 Sounds = 0;           // `sounds`  — the running-sound index (0 on every instance)

	virtual void Spawn() override
	{
		Speed = MaxSpeed;
		Direction = (SpawnFlags & SF_ROT_REVERSE) ? -1.0f : 1.0f;
		AngleDeg = 0.0f;
		bRunning = (SpawnFlags & SF_ROT_START_ON) != 0;
		DeferRebase();   // Spawn runs before the world is activated: there is no clock to read yet
	}

	void InputStart()
	{
		if (!bRunning)
		{
			bRunning = true;
			Rebase();
		}
	}

	void InputStop()
	{
		if (bRunning)
		{
			AngleDeg = AngleAt(World ? World->NowSeconds() : RunStartTime);
			bRunning = false;
			NextThink = ELYSIUM_NEVER_THINK;
		}
	}

	void InputReverse()
	{
		Settle();
		Direction = -Direction;
		Rebase();
	}

	void InputSetSpeed(const FElysiumVariant& Value)
	{
		Settle();
		Speed = FMath::Max(0.0f, Value.ToFloat());
		Rebase();
	}

	virtual void Think() override
	{
		if (!bRunning)
		{
			NextThink = ELYSIUM_NEVER_THINK;
			return;
		}
		const double Now = World ? World->NowSeconds() : 0.0;
		if (RunStartTime < 0.0)
		{
			RunStartTime = Now;   // first think after Spawn or a save load: start the clock here
		}
		ApplyAngle(AngleAt(Now));
		NextThink = static_cast<float>(Now);   // keep turning every frame
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		float Frozen = bRunning ? AngleAt(World ? World->NowSeconds() : RunStartTime) : AngleDeg;
		Ar << Frozen << bRunning << Speed << Direction;
		if (Ar.IsLoading())
		{
			AngleDeg = Frozen;
			DeferRebase();   // a load lands before Activate too; re-seat on the next think
			NextThink = 0.0f;
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		const FVector Axis = SourceSpinAxis(SpawnFlags);
		Out.Emplace(TEXT("Running"), bRunning ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Speed"), FString::Printf(TEXT("%g deg/s %s"),
			Speed, Direction < 0.0f ? TEXT("(reversed)") : TEXT("")));
		Out.Emplace(TEXT("Angle"), FString::Printf(TEXT("%.2f deg"),
			bRunning ? AngleAt(World ? World->NowSeconds() : RunStartTime) : AngleDeg));
		Out.Emplace(TEXT("Source axis"), Axis.ToString());
		Out.Emplace(TEXT("Spawnflags"), FString::Printf(TEXT("%d = %s"),
			SpawnFlags, *DescribeRotatingSpawnFlags(SpawnFlags)));
	}

	// The angle the body would be at, for a test or a debug read, without moving anything.
	float CurrentAngleDeg() const
	{
		return bRunning ? AngleAt(World ? World->NowSeconds() : RunStartTime) : AngleDeg;
	}

private:
	// Angle from elapsed time rather than accumulated per-frame deltas: the hour hand turns at
	// 0.0083 deg/s for twelve hours, where float accumulation would visibly drift.
	float AngleAt(double Now) const
	{
		if (!bRunning || RunStartTime < 0.0)
		{
			return AngleDeg;
		}
		const double Turned = Speed * Direction * (Now - RunStartTime);
		const double Wrapped = FMath::Fmod(AngleDeg + Turned, 360.0);
		return static_cast<float>(Wrapped < 0.0 ? Wrapped + 360.0 : Wrapped);   // Fmod keeps the sign
	}

	// Freeze the angle reached so far so a speed/direction change starts from where the body is.
	void Settle()
	{
		AngleDeg = AngleAt(World ? World->NowSeconds() : RunStartTime);
	}

	// An input arrives with a live clock, so the new rate takes effect at the moment it lands
	// rather than at whatever time the next think happens to run.
	void Rebase()
	{
		RunStartTime = World ? World->NowSeconds() : 0.0;
		Arm();
	}

	// Spawn and a save load both run before Activate, where the clock means nothing yet: -1 defers
	// the reference time to the first think.
	void DeferRebase()
	{
		RunStartTime = -1.0;
		Arm();
	}

	void Arm()
	{
		if (bRunning)
		{
			NextThink = 0.0f;
		}
	}

	void ApplyAngle(float NewAngleDeg)
	{
		if (!Body)
		{
			return;   // a bodiless rotator turns nothing; keep the angle for when a body exists
		}
		if (!bSpawnRotCached)
		{
			bSpawnRotCached = true;
			SpawnRot = Body->GetRelativeRotation().Quaternion();
		}
		// One uniform rule, the same reflection every other coordinate read uses: the Source spin
		// axis maps to Unreal by negating Y, and the reflection reverses the turn, so the angle
		// negates with it. Pre-multiplied — the axis is a world axis, not a body-local one.
		const FVector Src = SourceSpinAxis(SpawnFlags);
		const FVector UnrealAxis(Src.X, -Src.Y, Src.Z);
		const FQuat Delta(UnrealAxis, FMath::DegreesToRadians(-NewAngleDeg));
		Body->SetRelativeRotation(Delta * SpawnRot);
	}

	float  Speed = 0.0f;         // the live rate; SetSpeed writes it, Spawn seeds it from maxspeed
	float  Direction = 1.0f;     // +1, or -1 from REVERSE / the Reverse input
	float  AngleDeg = 0.0f;      // the angle at RunStartTime (the whole angle while stopped)
	double RunStartTime = -1.0;  // < 0 = rebase on the next think
	bool   bRunning = false;
	bool   bSpawnRotCached = false;
	FQuat  SpawnRot = FQuat::Identity;
};

static TUniquePtr<FElysiumEntity> MakeFuncRotating() { return MakeUnique<FElysiumFuncRotating>(); }

// func_rotating sits directly on the base chain — it shares no state with the CBaseDoor lineage.
// Inputs are the B.3 surface; ScriptHide/ScriptUnhide/Kill come from the base.
static FElysiumClassRegistrar GRegFuncRotating(
	TEXT("func_rotating"), ElysiumBaseClassName(), &MakeFuncRotating,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Start"),    [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumFuncRotating&>(E).InputStart(); });
		D.Input(TEXT("Stop"),     [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumFuncRotating&>(E).InputStop(); });
		D.Input(TEXT("Reverse"),  [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumFuncRotating&>(E).InputReverse(); });
		D.Input(TEXT("SetSpeed"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumFuncRotating&>(E).InputSetSpeed(A.Param); });
		ElysiumAddClassField(D, TEXT("maxspeed"),    &FElysiumFuncRotating::MaxSpeed);
		ElysiumAddClassField(D, TEXT("fanfriction"), &FElysiumFuncRotating::FanFriction);
		ElysiumAddClassField(D, TEXT("volume"),      &FElysiumFuncRotating::Volume);
		ElysiumAddClassField(D, TEXT("dmg"),         &FElysiumFuncRotating::Dmg);
		ElysiumAddClassField(D, TEXT("sounds"),      &FElysiumFuncRotating::Sounds);
	});
