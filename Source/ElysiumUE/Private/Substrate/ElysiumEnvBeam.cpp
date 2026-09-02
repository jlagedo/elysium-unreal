// env_beam -- CEnvBeam (`docs/vtmb/effects.md` §3.1, `effects-architecture.md` §5.6): the beam
// between two targetnames. The leaf resolves the endpoints (a random pick among duplicates, a
// random point within `Radius` when one is missing), runs the striker clock (`life` seconds on,
// then `StrikeTime` -- random 0..StrikeTime under flag 4 -- until the next), and applies `damage`
// as one trace along the straight axis into the combat damage path; `NS_ElysiumBeam` on the
// actor draws the taper, the noise and the scroll. `impact_particle` and `faces_player` are
// inert in VtMB and stay inert here.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"         // FElysiumCombatCharacter::TakeDamage, FElysiumPlayer
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumDamage.h"

namespace
{
	constexpr int32 SF_BEAM_START_ON = 0x1;
	constexpr int32 SF_BEAM_RANDOM_STRIKE = 0x4;
	// CEnvBeam::UpdateThink's cadence for a continuous damaging beam.
	constexpr float DamageThinkSeconds = 0.1f;
}

class FElysiumEnvBeam final : public FElysiumEntity
{
public:
	FString LightningStart;
	FString LightningEnd;
	float BoltWidth = 6.f;       // Source inches
	float NoiseAmplitude = 0.f;  // inches
	FString Texture = TEXT("materials/sprites/beama.vmt");
	float TextureScroll = 35.f;
	float Radius = 256.f;        // inches
	float Life = 0.f;            // 0 = continuous
	float StrikeTime = 0.f;
	float Damage = 0.f;          // per second along the trace
	FString RenderColor = TEXT("255 255 255");
	float RenderAmt = 255.f;
	// Provenance, inert.
	FString ImpactParticle;
	int32 FacesPlayer = 0;
	float Framerate = 0.f;
	float Framestart = 0.f;
	int32 RenderFx = 0;

	virtual void Spawn() override
	{
		bOn = (SpawnFlags & SF_BEAM_START_ON) != 0;
	}

	// The endpoints may be created after the map's spawn pass; the first strike waits for the
	// activation barrier like an env_particle's parent does.
	virtual void Activate() override
	{
		FElysiumEntity::Activate();
		if (bOn)
		{
			Start();
		}
		else
		{
			Publish(false);
		}
	}

	void TurnOn()
	{
		if (bOn)
		{
			return;
		}
		bOn = true;
		Start();
	}

	void TurnOff()
	{
		bOn = false;
		bWindowOpen = false;
		NextThink = ELYSIUM_NEVER_THINK;
		Publish(false);
	}

	void Toggle() { bOn ? TurnOff() : TurnOn(); }

	// One strike now, whatever the state; a continuous beam re-traces, a striker opens a window.
	void StrikeOnce() { Strike(Now()); }

	void InputWidth(const FElysiumVariant& V) { BoltWidth = FMath::Max(0.f, V.ToFloat()); Republish(); }
	void InputNoise(const FElysiumVariant& V) { NoiseAmplitude = FMath::Max(0.f, V.ToFloat()); Republish(); }
	void InputAlpha(const FElysiumVariant& V) { RenderAmt = FMath::Clamp(V.ToFloat(), 0.f, 255.f); Republish(); }
	void InputColor(const FElysiumVariant& V) { RenderColor = V.ToString(); Republish(); }

	virtual void Think() override
	{
		const double T = Now();
		if (Life > 0.f)
		{
			if (bWindowOpen)
			{
				// The window closes; the next strike comes `StrikeTime` after it (random under
				// flag 4), as CEnvBeam::StrikeThink schedules `life + restrike`.
				bWindowOpen = false;
				Publish(false);
				if (bOn)
				{
					const float Restrike = (SpawnFlags & SF_BEAM_RANDOM_STRIKE)
						? ElysiumRng::Stream(EElysiumRngStream::Effects).FRandRange(0.f, StrikeTime)
						: StrikeTime;
					NextThink = static_cast<float>(T + FMath::Max(0.f, Restrike));
				}
				else
				{
					NextThink = ELYSIUM_NEVER_THINK;
				}
			}
			else if (bOn)
			{
				Strike(T);
			}
			return;
		}
		if (bOn && Damage > 0.f)
		{
			ApplyDamage(T);
			NextThink = static_cast<float>(T + DamageThinkSeconds);
		}
		else
		{
			NextThink = ELYSIUM_NEVER_THINK;
		}
	}

	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		Publish(bOn && (Life <= 0.f || bWindowOpen));
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar << bOn << bWindowOpen << StartCm << EndCm;
		if (Ar.IsLoading())
		{
			Publish(bOn && (Life <= 0.f || bWindowOpen));
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Beam"), bOn ? TEXT("on") : TEXT("off"));
		Out.Emplace(TEXT("Ends"), FString::Printf(TEXT("%s -> %s"), *LightningStart, *LightningEnd));
		Out.Emplace(TEXT("Strike"), Life > 0.f
			? FString::Printf(TEXT("%.2fs every %.2fs%s"), Life, StrikeTime,
				(SpawnFlags & SF_BEAM_RANDOM_STRIKE) ? TEXT(" (random)") : TEXT(""))
			: FString(TEXT("continuous")));
	}

private:
	double Now() const { return World ? World->NowSeconds() : 0.0; }

	void Start()
	{
		DamageTime = Now();
		PendingDamage = 0.f;
		if (Life > 0.f)
		{
			Strike(DamageTime);
			return;
		}
		ResolveEndpoints();
		Publish(true);
		NextThink = Damage > 0.f ? static_cast<float>(DamageTime + DamageThinkSeconds) : ELYSIUM_NEVER_THINK;
	}

	void Strike(double T)
	{
		ResolveEndpoints();
		Publish(true);
		ApplyDamage(T);
		if (Life > 0.f)
		{
			bWindowOpen = true;
			NextThink = static_cast<float>(T + Life);
		}
	}

	// A striker's window and a continuous beam both show the current keys; an input that changes a
	// key re-sends what is showing without re-rolling the endpoints.
	void Republish()
	{
		if (bOn && (Life <= 0.f || bWindowOpen))
		{
			Publish(true);
		}
	}

	// CEnvBeam's endpoint rule: a random pick among the live entities carrying each name; with no
	// start the beam's own origin, with no end a random point within `Radius` of the start.
	void ResolveEndpoints()
	{
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Effects);
		auto Pick = [this, &Rng](const FString& Name, FVector& Out) -> bool
		{
			if (Name.IsEmpty() || !World)
			{
				return false;
			}
			TArray<FVector> Candidates;
			World->ForEachNamed(Name, [&Candidates](FElysiumEntity& E)
			{
				if (!E.IsDead())
				{
					Candidates.Add(E.Origin);
				}
			});
			if (Candidates.Num() == 0)
			{
				return false;
			}
			Out = Candidates[Rng.RandRange(0, Candidates.Num() - 1)];
			return true;
		};
		if (!Pick(LightningStart, StartCm))
		{
			StartCm = Origin;
		}
		if (!Pick(LightningEnd, EndCm))
		{
			EndCm = StartCm + Rng.VRand() * Rng.FRandRange(0.f, FMath::Max(0.f, Radius) * 2.54f);
		}
	}

	// CEnvBeam::BeamDamage: one trace along the axis, `damage x (now - last damage time)` to the
	// first character it crosses. Fractions accumulate so a `damage 1` beam lands one point per
	// second rather than one per tick; nothing crossed drops the accumulation, as retail's
	// `m_flDamageTime` advance does.
	void ApplyDamage(double T)
	{
		const float Elapsed = static_cast<float>(FMath::Max(0.0, T - DamageTime));
		DamageTime = T;
		if (Damage <= 0.f || !World)
		{
			return;
		}
		PendingDamage += Damage * Elapsed;
		if (PendingDamage < 1.f)
		{
			return;
		}
		IElysiumEmbodiment* Embodiment = World->Embodiment();
		if (!Embodiment)
		{
			return;
		}
		FElysiumSwingSweep Sweep;
		Sweep.PrevA = Sweep.CurA = StartCm;
		Sweep.PrevB = Sweep.CurB = EndCm;
		TArray<FElysiumEntityHandle> Hits;
		Embodiment->QuerySwingContacts(Sweep, Hits);
		const int32 Points = FMath::FloorToInt(PendingDamage);
		PendingDamage = Hits.Num() > 0 ? PendingDamage - Points : 0.f;
		const FElysiumPlayer* Player = World->FindPlayer();
		for (const FElysiumEntityHandle& Hit : Hits)
		{
			FElysiumEntity* Victim = World->Resolve(Hit);
			FElysiumCombatCharacter* Character = Victim ? Victim->AsCombatCharacter() : nullptr;
			if (!Character)
			{
				continue;
			}
			FElysiumDmg Dmg;
			Dmg.Family = EElysiumDmgFamily::Lethal;
			Dmg.Flags = ElysiumDamage::FlagDirectInput;
			Dmg.ExtraInput = Points;
			Dmg.ForcedSoak = 0;
			Dmg.Source = Handle;
			Character->TakeDamage(Dmg, /*Attacker=*/nullptr);
			if (Player && Player->Handle == Hit)
			{
				Embodiment->DamagePlayer(static_cast<float>(Points));
			}
		}
	}

	void Publish(bool bActive) const
	{
		IElysiumWeather* Service = World ? World->Weather() : nullptr;
		if (!Service)
		{
			return;
		}
		FElysiumBeamState State;
		State.Entity = Handle;
		State.bActive = bActive && !IsInert();
		State.StartCm = StartCm;
		State.EndCm = EndCm;
		State.WidthCm = BoltWidth * 2.54f;
		State.NoiseAmplitudeCm = NoiseAmplitude * 2.54f;
		State.TextureScroll = TextureScroll;
		State.Texture = Texture;
		TArray<FString> Parts;
		RenderColor.ParseIntoArrayWS(Parts);
		State.Color = FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(RenderAmt / 255.f, 0.f, 1.f));
		if (Parts.Num() >= 3)
		{
			State.Color.R = FCString::Atof(*Parts[0]) / 255.f;
			State.Color.G = FCString::Atof(*Parts[1]) / 255.f;
			State.Color.B = FCString::Atof(*Parts[2]) / 255.f;
		}
		Service->ApplyBeam(State);
	}

	bool bOn = false;
	bool bWindowOpen = false;
	FVector StartCm = FVector::ZeroVector;
	FVector EndCm = FVector::ZeroVector;
	double DamageTime = 0.0;
	float PendingDamage = 0.f;
};

static TUniquePtr<FElysiumEntity> MakeEnvBeam() { return MakeUnique<FElysiumEnvBeam>(); }

static FElysiumClassRegistrar GRegEnvBeam(
	TEXT("env_beam"), ElysiumBaseClassName(), &MakeEnvBeam,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvBeam&>(E).TurnOn(); });
		D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvBeam&>(E).TurnOff(); });
		D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvBeam&>(E).Toggle(); });
		D.Input(TEXT("StrikeOnce"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvBeam&>(E).StrikeOnce(); });
		D.Input(TEXT("Width"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvBeam&>(E).InputWidth(A.Param); });
		D.Input(TEXT("Noise"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvBeam&>(E).InputNoise(A.Param); });
		D.Input(TEXT("Alpha"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvBeam&>(E).InputAlpha(A.Param); });
		D.Input(TEXT("Color"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvBeam&>(E).InputColor(A.Param); });
		ElysiumAddClassField(D, TEXT("LightningStart"), &FElysiumEnvBeam::LightningStart, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("LightningEnd"), &FElysiumEnvBeam::LightningEnd, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("BoltWidth"), &FElysiumEnvBeam::BoltWidth, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("NoiseAmplitude"), &FElysiumEnvBeam::NoiseAmplitude, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("texture"), &FElysiumEnvBeam::Texture, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("TextureScroll"), &FElysiumEnvBeam::TextureScroll, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("Radius"), &FElysiumEnvBeam::Radius, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("life"), &FElysiumEnvBeam::Life, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("StrikeTime"), &FElysiumEnvBeam::StrikeTime, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("damage"), &FElysiumEnvBeam::Damage, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("rendercolor"), &FElysiumEnvBeam::RenderColor, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("renderamt"), &FElysiumEnvBeam::RenderAmt, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("impact_particle"), &FElysiumEnvBeam::ImpactParticle, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("faces_player"), &FElysiumEnvBeam::FacesPlayer, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("framerate"), &FElysiumEnvBeam::Framerate, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("framestart"), &FElysiumEnvBeam::Framestart, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("renderfx"), &FElysiumEnvBeam::RenderFx, EElysiumField::None);
	});
