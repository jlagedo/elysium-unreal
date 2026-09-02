#include "ElysiumEffectValveActors.h"

#include "ElysiumEffectFamilies.h"   // ElysiumEffectAssets -- the family system and material paths
#include "ElysiumWorldServices.h"    // the three published states

#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"

namespace
{
	// The on/off half every Valve actor shares: `User.Active` follows the state, a rising edge
	// restarts (a striker's window re-fires), a falling edge lets the live particles finish.
	void DriveActive(AElysiumEffectActor& Actor, UNiagaraComponent* Niagara, bool bActive)
	{
		if (Niagara)
		{
			Niagara->SetVariableBool(TEXT("User.Active"), bActive);
		}
		if (bActive)
		{
			Actor.TurnOn();
		}
		else if (Actor.IsOn())
		{
			Actor.TurnOff();
		}
	}
}

// --- Dust ------------------------------------------------------------------------------------

const TCHAR* AElysiumDustActor::DefaultSystemPath() const
{
	return ElysiumEffectAssets::DustSystem;
}

void AElysiumDustActor::WriteParameters()
{
	if (!Niagara)
	{
		return;
	}
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Niagara,
		TEXT("User.SpawnPoints"), SpawnPointsLocal);
	Niagara->SetVariableFloat(TEXT("User.SpawnRate"), SpawnRate);
	Niagara->SetVariableLinearColor(TEXT("User.Color"), Color);
	Niagara->SetVariableFloat(TEXT("User.SpeedMax"), SpeedMaxCmS);
	Niagara->SetVariableFloat(TEXT("User.SizeMin"), SizeMinCm);
	Niagara->SetVariableFloat(TEXT("User.SizeMax"), SizeMaxCm);
	Niagara->SetVariableFloat(TEXT("User.LifetimeMin"), LifetimeMinS);
	Niagara->SetVariableFloat(TEXT("User.LifetimeMax"), LifetimeMaxS);
	Niagara->SetVariableFloat(TEXT("User.DistMax"), DistMaxCm);
	// Zero until the weather plan has a wind; the term is wired so a future wind drives it.
	Niagara->SetVariableVec3(TEXT("User.Wind"), FVector::ZeroVector);
	Niagara->SetVariableBool(TEXT("User.Frozen"), bFrozen);
	Niagara->SetVariableBool(TEXT("User.Active"), bOn);
}

void AElysiumDustActor::Drive(const FElysiumDustState& Dust)
{
	if (IsKilled())
	{
		return;
	}
	SpawnRate = Dust.SpawnRate;
	Color = Dust.Color;
	SpeedMaxCmS = Dust.SpeedMaxCm;
	SizeMinCm = Dust.SizeMinCm;
	SizeMaxCm = Dust.SizeMaxCm;
	LifetimeMinS = Dust.LifetimeMin;
	LifetimeMaxS = Dust.LifetimeMax;
	DistMaxCm = Dust.DistMaxCm;
	bFrozen = Dust.bFrozen;
	if (Dust.BoundsCm.IsValid)
	{
		BoundsCm = Dust.BoundsCm;
	}
	// The leaf samples in world cm; the system reads component-local.
	const FTransform Frame = Niagara ? Niagara->GetComponentTransform() : GetActorTransform();
	SpawnPointsLocal.Reset(Dust.SpawnPointsCm.Num());
	for (const FVector& Point : Dust.SpawnPointsCm)
	{
		SpawnPointsLocal.Add(Frame.InverseTransformPosition(Point));
	}
	EnsureSystem();
	WriteParameters();
	DriveActive(*this, Niagara, Dust.bActive);
}

// --- Steam -----------------------------------------------------------------------------------

const TCHAR* AElysiumSteamActor::DefaultSystemPath() const
{
	return ElysiumEffectAssets::SteamSystem;
}

void AElysiumSteamActor::WriteParameters()
{
	if (!Niagara)
	{
		return;
	}
	Niagara->SetVariableFloat(TEXT("User.SpreadSpeed"), SpreadSpeedCmS);
	Niagara->SetVariableFloat(TEXT("User.Speed"), SpeedCmS);
	Niagara->SetVariableFloat(TEXT("User.StartSize"), StartSizeCm);
	Niagara->SetVariableFloat(TEXT("User.EndSize"), EndSizeCm);
	Niagara->SetVariableFloat(TEXT("User.Rate"), Rate);
	Niagara->SetVariableFloat(TEXT("User.JetLength"), JetLengthCm);
	Niagara->SetVariableFloat(TEXT("User.Lifetime"), LifetimeS);
	Niagara->SetVariableLinearColor(TEXT("User.Color"), Color);
	// The heatwave type (0 rows) takes the DUDV child; the floor child otherwise.
	if (UMaterialInterface* Material = LoadMaterial(Type == 1
		? ElysiumEffectAssets::MaterialRefract : ElysiumEffectAssets::MaterialFloor))
	{
		Niagara->SetVariableMaterial(TEXT("User.Material"), Material);
	}
	Niagara->SetVariableBool(TEXT("User.Active"), bOn);
}

void AElysiumSteamActor::Drive(const FElysiumSteamState& Steam)
{
	if (IsKilled())
	{
		return;
	}
	Type = Steam.Type;
	SpreadSpeedCmS = Steam.SpreadSpeedCm;
	SpeedCmS = Steam.SpeedCm;
	StartSizeCm = Steam.StartSizeCm;
	EndSizeCm = Steam.EndSizeCm;
	Rate = Steam.Rate;
	JetLengthCm = Steam.JetLengthCm;
	LifetimeS = Steam.Lifetime;
	Color = Steam.Color;
	EnsureSystem();
	WriteParameters();
	DriveActive(*this, Niagara, Steam.bActive);
}

// --- Beam ------------------------------------------------------------------------------------

const TCHAR* AElysiumBeamActor::DefaultSystemPath() const
{
	return ElysiumEffectAssets::BeamSystem;
}

void AElysiumBeamActor::WriteParameters()
{
	if (!Niagara)
	{
		return;
	}
	if (bHaveEndpoints)
	{
		Niagara->SetVariableVec3(TEXT("User.Start"), StartCm);
		Niagara->SetVariableVec3(TEXT("User.End"), EndCm);
	}
	Niagara->SetVariableFloat(TEXT("User.Width"), WidthCm);
	Niagara->SetVariableFloat(TEXT("User.EndWidth"), EndWidthCm);
	// VtMB scales the authored amplitude by the beam's length / 100 over its 128 divisions.
	const float LengthCm = bHaveEndpoints ? static_cast<float>(FVector::Dist(StartCm, EndCm)) : 0.f;
	Niagara->SetVariableFloat(TEXT("User.NoiseAmplitude"), NoiseAmplitudeCm * LengthCm / 100.f);
	Niagara->SetVariableFloat(TEXT("User.TextureScroll"), TextureScroll);
	if (UMaterialInterface* Material = LoadMaterial(ElysiumEffectAssets::BeamMaterial))
	{
		Niagara->SetVariableMaterial(TEXT("User.Material"), Material);
	}
	Niagara->SetVariableLinearColor(TEXT("User.Color"), Color);
	Niagara->SetVariableBool(TEXT("User.Active"), bOn);
}

void AElysiumBeamActor::Drive(const FElysiumBeamState& Beam)
{
	if (IsKilled())
	{
		return;
	}
	StartCm = Beam.StartCm;
	EndCm = Beam.EndCm;
	bHaveEndpoints = true;
	WidthCm = Beam.WidthCm;
	EndWidthCm = Beam.WidthCm * 0.1f;
	NoiseAmplitudeCm = Beam.NoiseAmplitudeCm;
	TextureScroll = Beam.TextureScroll;
	Texture = Beam.Texture;
	Color = Beam.Color;
	EnsureSystem();
	WriteParameters();
	DriveActive(*this, Niagara, Beam.bActive);
}
