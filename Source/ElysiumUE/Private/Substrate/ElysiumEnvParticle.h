#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"

// env_particle -- CEnvParticle (`docs/vtmb/effects.md` §3.1). The leaf owns the I/O and the linear rate ramp and publishes one
// `FElysiumWeatherEmitterState` through `IElysiumWeather::ApplyEmitter` on every change; the
// embodiment draws it through the bake-placed `AElysiumEffectActor`, found by entity index.
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
	// The three `DT_EnvParticle` words the datamap leaves unnamed (`walks/L0-r011.md`, builder
	// `FUN_100fa840`): `m_nParticle` (+0x454), the precached definition index, constructor
	// `FUN_100fad50` -1 (`< 0` = unresolved; `Precache` 0x100fb540 writes it); `m_flActivationTime`
	// (+0x488), a curtime the constructor, `Spawn` (when active) and the TurnOn body 0x100fb7a0 stamp;
	// `m_nRampFrame` (+0x494), the HOST FRAME COUNTER (`VEngineServer014` slot 120) `SetRampTime`
	// 0x100fba40 and `SetRateScale` 0x100fb980 both stamp (last writer wins). Entity memory is
	// `calloc` (engine `0x201092d0`), so +0x494 starts 0.
	int32 ParticleIndex = -1;
	float ActivationTime = 0.0f;
	int32 RampFrame = 0;
	// `m_fRateScaleTarget` (+0x48c): the `ramp_scale` key's word, `SetRateScale` 0x100fb980's write.
	// Public so the class table can register it by its retail name.
	float RampTargetScale = 1.0f;
	// +0x4a1, an unnamed non-networked byte: constructor 0; `FUN_1038e9c0` (the Ghoul croucher's L4
	// body, unread) sets it to 1, which makes `SetRateScale` multiply by the ConVar `DAT_107083dc`'s
	// value. No map emitter reaches it; carried as the word the arm reads.
	bool bRateScaleByConVar = false;

	// An emitter is placed long before the thing it rides exists: `plus_impact` parents to `Sire2`,
	// whose model the level script swaps at scene start, and the Embrace emitters parent to
	// `player_understudy`, which a trigger creates. PostSpawn's parentname pass is one-shot and
	// never retried, so resolution is deferred to the first TurnOn instead, by which point every
	// one of these parents exists. Every TurnOn bumps the serial: VtMB's client rebuilds the emitter
	// whenever the activation timestamp changes, so a TurnOn on a live emitter restarts it.
	// `InputTurnOn` 0x100fa690 -> slot 241 `0x100fb7a0`: `m_bActive = 1`, `+0x488 = curtime`; no
	// already-active guard. Slot 242 `0x100fb7d0` (`TurnOff`): `m_bActive = 0`, no stamp.
	void InputTurnOn()
	{
		bActive = true;
		ActivationTime = World ? static_cast<float>(World->NowSeconds()) : 0.0f;
		++TurnOnSerial;
		Publish();
	}
	void InputTurnOff() { bActive = false; Publish(); }

	// `CEnvParticle::InputSetRateScale` 0x100fb850 / `InputSetRampTime` 0x100fb8a0: `inputdata->+0x18
	// == 1` (the variant is FIELD_FLOAT) takes the float at `+8`, any other type gives `0.0`; then the
	// body (`FUN_100fb980` / `FUN_100fba40`) through a tail jump. The dispatcher `AcceptInput`
	// 0x100abc90 converts the variant to the input's declared FIELD_FLOAT before the call (Source's
	// `variant_t::Convert`), so the `0.0` arm is reached only by a value no conversion yields; this
	// port's total `ToFloat()` is that conversion (a non-numeric string converts to 0).
	void InputSetRateScale(const FElysiumVariant& Value) { SetRateScale(Value.ToFloat()); }
	void InputSetRampTime(const FElysiumVariant& Value) { SetRampTime(Value.ToFloat()); }
	// `FUN_100fb980` 0x100fb980 (SetRateScale) and `FUN_100fba40` 0x100fba40 (SetRampTime), the
	// retail bodies: the strict `< 0.0` Warning and clamp, the word, the `+0x494` frame stamp.
	void SetRateScale(float Rate);
	void SetRampTime(float Duration);

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

	// `CEnvParticle::Spawn` 0x100fb3d0 (slot 103), every arm in retail order (`walks/L0-r011.md`):
	// slot 104 `Precache`, `CPointEntity::Spawn` 0x101c0820, the attach-type range check, then under
	// `m_nParticle >= 0` the spawnbounds clamp to [0, 4096], the parent attach (`FUN_100faf60` ->
	// slot 243 when `m_pParent` resolves) and the active stamp; else `UTIL_Remove` 0x101cd940.
	virtual void Spawn() override;
	// `CEnvParticle::Precache` 0x100fb540 (slot 104): only while `m_nParticle < 0`; a NULL definition
	// writes -1, else `UTIL_Extract_FileBase` 0x101cf7c0 and `VEngineServer014` slot 17.
	virtual void Precache() override;
	// `UTIL_Extract_FileBase(in, out, 0x104)` 0x101cf7c0: the text after the last `/` or `\`, without
	// the extension that follows it; case kept; an empty input gives an empty output.
	static FString ExtractFileBase(const FString& In);
	// `CEnvParticle::AttachToEntity` 0x100fb110, CEnvParticle's slot 243 (`CAuspexAura::AttachToEntity`
	// 0x10051d50 overrides it): `SetParent(parent, 0)`, `m_nAttachType`, `m_sAttachName`
	// (`AllocPooledString` 0x1042bff0), `SetMoveType(11 MOVETYPE_FOLLOW, 0)`, `SetAimEnt(entity(m_pParent))`,
	// `SetOrigin(0, 0, 0)` (slot 63), then `m_nAttachPoint` <- `FUN_100fafa0(parent, mode, name)`.
	virtual void AttachToEntity(FElysiumEntity* Parent, int32 Mode, const FString& Name);
	// `FUN_100fafa0(parent, mode, name)` 0x100fafa0: -1 for the tree modes 1 / 3; `LookupAttachment`
	// (L1 hook, `hooks.tsv:21`) for the attachment modes 6 / 17 on a `CBaseAnimating`, else 0; for any
	// other mode > 0 the index of the first studio bone whose name matches case-insensitively, else 0;
	// 0 for mode <= 0. `Site` names the entity the `attach_index` site is emitted for.
	static int32 ResolveAttachIndex(FElysiumEntity* Parent, int32 Mode, const FString& Name, const FElysiumEntity& Site);

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
		Ar << ParticleIndex << ActivationTime << RampFrame;
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
		// The embodiment's resolution key (the bake lower-cases and forward-slashes its root names);
		// the retail word `m_sParticleDefinition` above stays as authored.
		State.ParticleDefinition = ParticleDefinition.Replace(TEXT("\\"), TEXT("/")).ToLower();
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
	double RampStartTime = 0.0;
	float RampDuration = 0.0f;
};
