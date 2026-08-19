#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumWeatherEntity, Log, All);

class FElysiumEnvParticle final : public FElysiumEntity
{
public:
	bool bActive = false;
	FString ParticleDefinition;
	int32 AttachType = 0;
	FString AttachBone;        // `bone` -> m_sAttachName; the point on the parent's model
	float Bounds = 0.0f;       // Source inches; converted only at the engine service seam
	float RampScale = 1.0f;
	float RampTime = 0.0f;

	// An emitter is placed long before the thing it rides exists: `plus_impact` parents to `Sire2`,
	// whose model the level script swaps at scene start, and the Embrace emitters parent to
	// `player_understudy`, which a trigger creates. PostSpawn's parentname pass is one-shot and
	// never retried, so resolution is deferred to the first TurnOn instead, by which point every
	// one of these parents exists.
	void InputTurnOn()  { bActive = true; Publish(); }
	void InputTurnOff() { bActive = false; Publish(); }

	void InputSetRateScale(const FElysiumVariant& Value)
	{
		const double Now = World ? World->NowSeconds() : 0.0;
		RampScale = RateAt(Now);
		RampStartScale = RampScale;
		RampTargetScale = FMath::Max(0.0f, Value.ToFloat());
		RampStartTime = Now;
		RampDuration = FMath::Max(0.0f, RampTime);
		if (RampDuration <= 0.0f)
		{
			RampScale = RampTargetScale;
		}
		else
		{
			NextThink = static_cast<float>(Now);
		}
		Publish();
	}

	void InputSetRampTime(const FElysiumVariant& Value)
	{
		RampTime = FMath::Max(0.0f, Value.ToFloat());
	}

	void InputSetAttachType(const FElysiumVariant& Value)
	{
		AttachType = Value.ToInt();
		Publish();
	}

	virtual void Spawn() override
	{
		ParticleDefinition = ParticleDefinition.Replace(TEXT("\\"), TEXT("/")).ToLower();
		Bounds = FMath::Max(0.0f, Bounds);
		RampTime = FMath::Max(0.0f, RampTime);
		RampScale = FMath::Max(0.0f, RampScale);
		RampStartScale = RampScale;
		RampTargetScale = RampScale;
		RampStartTime = World ? World->NowSeconds() : 0.0;
		RampDuration = 0.0f;
		Publish();
	}

	virtual void Think() override
	{
		const double Now = World ? World->NowSeconds() : 0.0;
		RampScale = RateAt(Now);
		if (RampDuration > 0.0f && Now < RampStartTime + RampDuration)
		{
			NextThink = static_cast<float>(Now);
		}
		else
		{
			RampScale = RampTargetScale;
			RampStartScale = RampTargetScale;
			RampDuration = 0.0f;
			NextThink = ELYSIUM_NEVER_THINK;
		}
		Publish();
	}

	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		Publish();
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar << RampScale << RampStartScale << RampTargetScale << RampStartTime << RampDuration;
		if (Ar.IsLoading())
		{
			Publish();
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Definition"), ParticleDefinition);
		Out.Emplace(TEXT("Active"), bActive ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Rate"), FString::Printf(TEXT("%.3f -> %.3f in %.2fs"),
			RampScale, RampTargetScale, RampDuration));
		Out.Emplace(TEXT("Attach / bounds"), FString::Printf(TEXT("%d / %.1f cm"), AttachType, Bounds * 2.54f));
	}

private:
	float RateAt(double Now) const
	{
		if (RampDuration <= 0.0f || Now <= RampStartTime)
		{
			return RampScale;
		}
		const float Alpha = FMath::Clamp(
			static_cast<float>((Now - RampStartTime) / RampDuration), 0.0f, 1.0f);
		return FMath::Lerp(RampStartScale, RampTargetScale, Alpha);
	}

	void Publish() const
	{
		IElysiumWeather* Service = World ? World->Weather() : nullptr;
		if (!Service)
		{
			return;
		}
		FElysiumWeatherEmitterState State;
		State.Entity = Handle;
		State.LocationCm = Def ? Def->Origin : FVector::ZeroVector;
		State.ParticleDefinition = ParticleDefinition;
		State.bActive = bActive && !IsInert();
		State.AttachType = AttachType;
		State.ParentName = ParentName;
		State.AttachBone = AttachBone;
		State.BoundsCm = Bounds * 2.54f;
		State.RateScale = RampScale;
		State.RampStartScale = RampStartScale;
		State.RampTargetScale = RampTargetScale;
		State.RampStartTime = RampStartTime;
		State.RampDuration = RampDuration;
		Service->ApplyEmitter(State);
	}

	float RampStartScale = 1.0f;
	float RampTargetScale = 1.0f;
	double RampStartTime = 0.0;
	float RampDuration = 0.0f;
};

static TUniquePtr<FElysiumEntity> MakeEnvParticle() { return MakeUnique<FElysiumEnvParticle>(); }

static FElysiumClassRegistrar GRegEnvParticle(
	TEXT("env_particle"), ElysiumBaseClassName(), &MakeEnvParticle,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvParticle&>(E).InputTurnOn(); });
		D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvParticle&>(E).InputTurnOff(); });
		D.Input(TEXT("SetRateScale"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvParticle&>(E).InputSetRateScale(A.Param); });
		D.Input(TEXT("SetRampTime"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvParticle&>(E).InputSetRampTime(A.Param); });
		D.Input(TEXT("SetAttachType"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvParticle&>(E).InputSetAttachType(A.Param); });
		// None: spawn-time keyvalue application ignores bKeyable, and these fields are neither
		// runtime-writable nor save-enumerated.
		ElysiumAddClassField(D, TEXT("active"), &FElysiumEnvParticle::bActive, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("particle_definition"), &FElysiumEnvParticle::ParticleDefinition, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("attach_type"), &FElysiumEnvParticle::AttachType, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("bone"), &FElysiumEnvParticle::AttachBone, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("bounds"), &FElysiumEnvParticle::Bounds, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("ramp_scale"), &FElysiumEnvParticle::RampScale, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("ramp_time"), &FElysiumEnvParticle::RampTime, EElysiumField::None);
	});
