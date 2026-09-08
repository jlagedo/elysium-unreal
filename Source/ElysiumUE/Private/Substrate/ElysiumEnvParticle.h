#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"

// env_particle -- CEnvParticle (`docs/vtmb/effects.md` §3.1). The leaf owns the I/O and the linear rate ramp and publishes one
// `FElysiumWeatherEmitterState` through `IElysiumWeather::ApplyEmitter` on every change; the
// embodiment decides what draws it -- the bake-placed `AElysiumEffectActor` by entity index on a
// `MapsOnV2Models` map, the legacy per-map `NS_<root>` component elsewhere.
//
// The keyfield is `spawnbounds` (m_fSpawnBounds +0x49c, default 512): the FGD's `bounds` is not a
// keyfield on this class and no engine code reads it. `JetLength` is accepted as VtMB's own no-op.
// The five m_f*Scale fields have no key name and are actor pins (code producers only).
class FElysiumEnvParticle : public FElysiumEntity
{
public:
	bool bActive = true;       // `active`, the constructor's default 1
	FString ParticleDefinition;
	int32 AttachType = 0;
	FString AttachBone;        // `bone` -> m_sAttachName; the point on the parent's model
	int32 AttachPoint = 0;     // `attach_point` -> m_nAttachPoint; the numbered attachment
	float SpawnBounds = 512.0f;   // `spawnbounds`, Source inches; converted at the seam
	float RampScale = 1.0f;
	float RampTime = 0.0f;

	// An emitter is placed long before the thing it rides exists: `plus_impact` parents to `Sire2`,
	// whose model the level script swaps at scene start, and the Embrace emitters parent to
	// `player_understudy`, which a trigger creates. PostSpawn's parentname pass is one-shot and
	// never retried, so resolution is deferred to the first TurnOn instead, by which point every
	// one of these parents exists. Every TurnOn bumps the serial: VtMB's client rebuilds the emitter
	// whenever the activation timestamp changes, so a TurnOn on a live emitter restarts it.
	void InputTurnOn()  { bActive = true; ++TurnOnSerial; Publish(); }
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

	// The two `JetLength` wires in the corpus land on this class, which has no such input in VtMB:
	// the dispatch falls through and nothing happens. Accepted here for the same nothing, said once.
	void InputJetLength()
	{
		static bool bSaid = false;
		if (!bSaid)
		{
			bSaid = true;
			UE_LOG(LogTemp, Log, TEXT("%s: JetLength is not an env_particle input in VtMB; accepted as its no-op"),
				*DebugString());
		}
	}

	virtual void Spawn() override
	{
		ParticleDefinition = ParticleDefinition.Replace(TEXT("\\"), TEXT("/")).ToLower();
		SpawnBounds = FMath::Max(0.0f, SpawnBounds);
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
		Out.Emplace(TEXT("Attach / spawnbounds"), FString::Printf(TEXT("%d / %.1f cm"),
			AttachType, SpawnBounds * 2.54f));
		if (VolumeScale != 1.0f)
		{
			Out.Emplace(TEXT("Volume scale"), FString::Printf(TEXT("%.3f"), VolumeScale));
		}
	}

protected:
	// `func_particle` fills these at Spawn (`ElysiumFuncParticle.cpp`); an env_particle leaves
	// them at "no box, unity".
	FBox BrushBoundsCm = FBox(ForceInit);
	float VolumeScale = 1.0f;

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
		State.bDead = IsDead();
		State.TurnOnSerial = TurnOnSerial;
		State.AttachType = AttachType;
		State.ParentName = ParentName;
		State.AttachBone = AttachBone;
		State.AttachPoint = AttachPoint;
		State.BoundsCm = SpawnBounds * 2.54f;
		State.RateScale = RampScale;
		State.RampStartScale = RampStartScale;
		State.RampTargetScale = RampTargetScale;
		State.RampStartTime = RampStartTime;
		State.RampDuration = RampDuration;
		State.BrushBoundsCm = BrushBoundsCm;
		State.VolumeScale = VolumeScale;
		Service->ApplyEmitter(State);
	}

	uint32 TurnOnSerial = 0;
	float RampStartScale = 1.0f;
	float RampTargetScale = 1.0f;
	double RampStartTime = 0.0;
	float RampDuration = 0.0f;
};
