// func_dustmotes -- Valve's C_Func_Dust (`docs/vtmb/effects.md` §3.1, `effects-architecture.md`
// §5.6): a brush volume of motes that spawn INSIDE the brush solid. The leaf rejection-samples
// the entity's own convex set (ten retries per point, VtMB's count) and publishes the candidates
// with the keys through `IElysiumWeather::ApplyDust`; the actor on `NS_ElysiumDust` draws them
// with no gravity, a zero wind term, the `sin(pi t)` alpha law, `DistMax` and `Frozen`.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumDustmotes, Log, All);

namespace
{
	constexpr int32 SamplePoints = 256;
	constexpr int32 RetriesPerPoint = 10;

	// The face planes of a convex vertex cloud: every plane through three vertices that keeps
	// the whole cloud on one side. Cubic in the vertex count, run once per brush at spawn --
	// a dust brush has a handful of hulls of a dozen vertices each.
	void ConvexPlanes(const TArray<FVector>& Vertices, TArray<FPlane>& Out)
	{
		constexpr double Eps = 0.05;   // cm
		const int32 N = Vertices.Num();
		for (int32 I = 0; I < N; ++I)
		{
			for (int32 J = I + 1; J < N; ++J)
			{
				for (int32 K = J + 1; K < N; ++K)
				{
					FVector Normal = FVector::CrossProduct(Vertices[J] - Vertices[I], Vertices[K] - Vertices[I]);
					if (!Normal.Normalize(KINDA_SMALL_NUMBER))
					{
						continue;   // collinear
					}
					const double D = FVector::DotProduct(Normal, Vertices[I]);
					bool bAllBelow = true;
					bool bAllAbove = true;
					for (const FVector& V : Vertices)
					{
						const double S = FVector::DotProduct(Normal, V) - D;
						bAllBelow &= S <= Eps;
						bAllAbove &= S >= -Eps;
					}
					if (bAllBelow)
					{
						Out.Add(FPlane(Normal, D));         // outward normal
					}
					else if (bAllAbove)
					{
						Out.Add(FPlane(-Normal, -D));
					}
				}
			}
		}
	}

	bool Inside(const TArray<FPlane>& Planes, const FVector& P)
	{
		for (const FPlane& Plane : Planes)
		{
			if (Plane.PlaneDot(P) > 0.0)
			{
				return false;
			}
		}
		return Planes.Num() > 0;
	}

	FLinearColor ParseColor(const FString& Text, float Alpha)
	{
		TArray<FString> Parts;
		Text.ParseIntoArrayWS(Parts);
		FLinearColor C(1.f, 1.f, 1.f, Alpha);
		if (Parts.Num() >= 3)
		{
			C.R = FCString::Atof(*Parts[0]) / 255.f;
			C.G = FCString::Atof(*Parts[1]) / 255.f;
			C.B = FCString::Atof(*Parts[2]) / 255.f;
		}
		return C;
	}
}

class FElysiumFuncDustmotes final : public FElysiumEntity
{
public:
	float SpawnRate = 10.f;
	FString ColorText = TEXT("255 255 255");
	float Alpha = 255.f;
	float SpeedMax = 2.f;      // Source inches / s
	float SizeMin = 7.f;       // inches
	float SizeMax = 12.f;
	float LifetimeMin = 3.f;
	float LifetimeMax = 5.f;
	float DistMax = 1024.f;    // inches
	bool bFrozen = false;
	bool bStartDisabled = false;
	FString SpriteName;

	virtual void Spawn() override
	{
		bOn = !bStartDisabled;
		Sample();
		Publish();
	}

	void TurnOn()  { bOn = true;  Publish(); }
	void TurnOff() { bOn = false; Publish(); }

	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		Publish();
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar << bOn;
		if (Ar.IsLoading())
		{
			Publish();
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Dust"), bOn ? TEXT("on") : TEXT("off"));
		Out.Emplace(TEXT("Points"), FString::Printf(TEXT("%d in solid"), PointsCm.Num()));
	}

private:
	// Ten retries per point for a point inside any of the brush's hulls, as C_Func_Dust does.
	void Sample()
	{
		PointsCm.Reset();
		BoundsCm = FBox(ForceInit);
		if (!Def || Def->Hulls.Num() == 0)
		{
			UE_LOG(LogElysiumDustmotes, Warning, TEXT("%s carries no brush hulls; no motes"), *DebugString());
			return;
		}
		TArray<TArray<FPlane>> Hulls;
		Hulls.Reserve(Def->Hulls.Num());
		for (const FElysiumConvexHull& Hull : Def->Hulls)
		{
			TArray<FPlane>& Planes = Hulls.AddDefaulted_GetRef();
			ConvexPlanes(Hull.Vertices, Planes);
			for (const FVector& V : Hull.Vertices)
			{
				BoundsCm += V;
			}
		}
		if (!BoundsCm.IsValid)
		{
			return;
		}
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Effects);
		PointsCm.Reserve(SamplePoints);
		for (int32 I = 0; I < SamplePoints; ++I)
		{
			for (int32 Try = 0; Try < RetriesPerPoint; ++Try)
			{
				const FVector Local(
					Rng.FRandRange(BoundsCm.Min.X, BoundsCm.Max.X),
					Rng.FRandRange(BoundsCm.Min.Y, BoundsCm.Max.Y),
					Rng.FRandRange(BoundsCm.Min.Z, BoundsCm.Max.Z));
				bool bInside = false;
				for (const TArray<FPlane>& Planes : Hulls)
				{
					if (Inside(Planes, Local))
					{
						bInside = true;
						break;
					}
				}
				if (bInside)
				{
					PointsCm.Add(Origin + Local);
					break;
				}
			}
		}
		BoundsCm = BoundsCm.ShiftBy(Origin);
	}

	void Publish() const
	{
		IElysiumWeather* Service = World ? World->Weather() : nullptr;
		if (!Service)
		{
			return;
		}
		FElysiumDustState State;
		State.Entity = Handle;
		State.bActive = bOn && !IsInert();
		State.bFrozen = bFrozen;
		State.SpawnPointsCm = PointsCm;
		State.BoundsCm = BoundsCm;
		State.SpawnRate = FMath::Max(0.f, SpawnRate);
		State.Color = ParseColor(ColorText, FMath::Clamp(Alpha / 255.f, 0.f, 1.f));
		State.SpeedMaxCm = SpeedMax * 2.54f;
		State.SizeMinCm = SizeMin * 2.54f;
		State.SizeMaxCm = SizeMax * 2.54f;
		State.LifetimeMin = LifetimeMin;
		State.LifetimeMax = LifetimeMax;
		State.DistMaxCm = DistMax * 2.54f;
		Service->ApplyDust(State);
	}

	bool bOn = true;
	TArray<FVector> PointsCm;   // world cm, inside the solid
	FBox BoundsCm = FBox(ForceInit);
};

static TUniquePtr<FElysiumEntity> MakeFuncDustmotes() { return MakeUnique<FElysiumFuncDustmotes>(); }

static FElysiumClassRegistrar GRegFuncDustmotes(
	TEXT("func_dustmotes"), ElysiumBaseClassName(), &MakeFuncDustmotes,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumFuncDustmotes&>(E).TurnOn(); });
		D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumFuncDustmotes&>(E).TurnOff(); });
		ElysiumAddClassField(D, TEXT("SpawnRate"), &FElysiumFuncDustmotes::SpawnRate, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("Color"), &FElysiumFuncDustmotes::ColorText, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("Alpha"), &FElysiumFuncDustmotes::Alpha, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("SpeedMax"), &FElysiumFuncDustmotes::SpeedMax, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("SizeMin"), &FElysiumFuncDustmotes::SizeMin, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("SizeMax"), &FElysiumFuncDustmotes::SizeMax, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("LifetimeMin"), &FElysiumFuncDustmotes::LifetimeMin, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("LifetimeMax"), &FElysiumFuncDustmotes::LifetimeMax, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("DistMax"), &FElysiumFuncDustmotes::DistMax, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("Frozen"), &FElysiumFuncDustmotes::bFrozen, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("StartDisabled"), &FElysiumFuncDustmotes::bStartDisabled, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("SpriteName"), &FElysiumFuncDustmotes::SpriteName, EElysiumField::None);
	});
