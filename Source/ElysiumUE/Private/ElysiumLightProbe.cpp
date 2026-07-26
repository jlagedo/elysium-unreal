#include "ElysiumLightProbe.h"

#if !UE_BUILD_SHIPPING

#include "ElysiumBakedTags.h"
#include "ElysiumContentPaths.h"
#include "ElysiumLightRig.h"
#include "ElysiumMapActor.h"
#include "ElysiumPick.h"           // ELYSIUM_PICK_CHANNEL

#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumLightProbe, Log, All);

namespace
{
	// How far a probe ray looks before giving up. Past this the answer is "nothing near", which is
	// itself the signal — a light with no geometry within 30 m is not lighting an object.
	constexpr float ProbeReach = 3000.0f;

	// A surface counts as a light source when its bound material actually emits. The world master
	// leaves EmissiveScale at 0 for everything without a $selfillum map (map_Ke), so a non-zero
	// value is exactly "this texture glows" — the same switch FElysiumMaterialFactory sets.
	bool IsEmissiveMaterial(const UMaterialInterface* Mat)
	{
		if (Mat == nullptr)
		{
			return false;
		}
		float Scale = 0.0f;
		if (Mat->GetScalarParameterValue(FName(TEXT("EmissiveScale")), Scale))
		{
			return Scale > KINDA_SMALL_NUMBER;
		}
		return false;
	}

	// Roughly uniform directions on the sphere (Fibonacci lattice) — an even fan with no pole bias,
	// so "nearest thing in any direction" is not skewed by how the samples were generated.
	void BuildDirections(int32 Count, TArray<FVector>& Out)
	{
		Out.Reset(Count);
		const double Golden = PI * (1.0 + FMath::Sqrt(5.0));
		for (int32 i = 0; i < Count; ++i)
		{
			const double K = i + 0.5;
			const double CosPhi = 1.0 - 2.0 * K / Count;
			const double SinPhi = FMath::Sqrt(FMath::Max(0.0, 1.0 - CosPhi * CosPhi));
			const double Theta = Golden * K;
			Out.Add(FVector(FMath::Cos(Theta) * SinPhi, FMath::Sin(Theta) * SinPhi, CosPhi));
		}
	}

	// What one probe ray found.
	struct FRayHit
	{
		bool bHit = false;
		float Distance = 0.0f;
		bool bEmissive = false;
		bool bProp = false;
		FString Surface;      // material slot / model name, for the nearest hit only
	};

	FRayHit CastOne(UWorld& World, const FVector& Origin, const FVector& Dir,
		const FCollisionQueryParams& Params)
	{
		FRayHit R;
		FHitResult Hit;
		if (!World.LineTraceSingleByChannel(Hit, Origin, Origin + Dir * ProbeReach,
			ELYSIUM_PICK_CHANNEL, Params))
		{
			return R;
		}
		R.bHit = true;
		R.Distance = Hit.Distance > 0.0f ? Hit.Distance
			: static_cast<float>(FVector::Dist(Origin, Hit.ImpactPoint));

		UStaticMeshComponent* Comp = Cast<UStaticMeshComponent>(Hit.GetComponent());
		if (Comp == nullptr)
		{
			return R;
		}
		if (const AActor* Actor = Hit.GetActor())
		{
			R.bProp = Actor->ActorHasTag(ElysiumBakedTags::Prop);
		}
		int32 Section = INDEX_NONE;
		UMaterialInterface* Mat = (Hit.FaceIndex != INDEX_NONE)
			? Comp->GetMaterialFromCollisionFaceIndex(Hit.FaceIndex, Section)
			: Comp->GetMaterial(0);
		R.bEmissive = IsEmissiveMaterial(Mat);
		if (Mat != nullptr)
		{
			R.Surface = Mat->GetName();
		}
		else if (const UStaticMesh* Mesh = Comp->GetStaticMesh())
		{
			R.Surface = Mesh->GetName();
		}
		return R;
	}
}

int32 ElysiumLightProbe::Run(UWorld* World, AElysiumMapActor* Map, int32 NumRays)
{
	UElysiumLightRig* Rig = Map ? Map->GetLightRig() : nullptr;
	if (World == nullptr || Rig == nullptr)
	{
		return -1;
	}
	const TArray<UElysiumLightRig::FLightSource>& Sources = Rig->Sources();
	if (Sources.Num() == 0)
	{
		return -1;
	}

	TArray<FVector> Dirs;
	BuildDirections(FMath::Max(NumRays, 8), Dirs);

	FCollisionQueryParams Params(FName(TEXT("ElysiumLightProbe")), /*bTraceComplex*/ true);
	Params.bReturnFaceIndex = true;
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		Params.AddIgnoredActor(PC->GetPawn());
	}

	// Every light's position and reach up front: the redundancy term needs to ask what the OTHER
	// lights deliver at each probed surface point, so it needs the whole set before it starts.
	TArray<FVector> Pos;
	TArray<float> Reach;
	TArray<float> Mag;
	Pos.Reserve(Sources.Num()); Reach.Reserve(Sources.Num()); Mag.Reserve(Sources.Num());
	for (const UElysiumLightRig::FLightSource& S : Sources)
	{
		const ULightComponent* L = S.Light.Get();
		Pos.Add(L ? L->GetComponentLocation() : FVector::ZeroVector);
		// Radius 0 means no authored cutoff, so the rig's fallback is the real reach — treating it
		// as zero would invert every comparison that uses it.
		Reach.Add(S.RadiusCm > 1.f ? S.RadiusCm : Rig->FallbackRadiusCm);
		Mag.Add(S.Mag);
	}

	FString Json;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Json);
	W->WriteObjectStart();
	W->WriteValue(TEXT("map"), Map->MapName);
	W->WriteValue(TEXT("probed_utc"), FDateTime::UtcNow().ToIso8601());
	W->WriteValue(TEXT("rays"), Dirs.Num());
	W->WriteValue(TEXT("reach_cm"), ProbeReach);
	W->WriteArrayStart(TEXT("lights"));

	int32 Probed = 0;
	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		const UElysiumLightRig::FLightSource& S = Sources[Index];
		const ULightComponent* Light = S.Light.Get();
		if (Light == nullptr)
		{
			continue;
		}
		const FVector O = Light->GetComponentLocation();

		// --- the ray fan ---------------------------------------------------------------------
		float Nearest = FLT_MAX;
		int32 NearestDir = INDEX_NONE;
		FRayHit NearestHit;
		int32 NumHit = 0, NumEmissive = 0, NumProp = 0;
		int32 Within256 = 0, Within512 = 0;
		double SumDist = 0.0;
		TArray<float> Dists;
		Dists.Reserve(Dirs.Num());
		// Self-vs-others illumination at the points this light actually reaches.
		double SelfLit = 0.0, OtherLit = 0.0;

		for (int32 d = 0; d < Dirs.Num(); ++d)
		{
			const FRayHit R = CastOne(*World, O, Dirs[d], Params);
			if (!R.bHit)
			{
				continue;
			}
			++NumHit;
			Dists.Add(R.Distance);
			SumDist += R.Distance;
			Within256 += (R.Distance < 256.f) ? 1 : 0;
			Within512 += (R.Distance < 512.f) ? 1 : 0;
			NumEmissive += R.bEmissive ? 1 : 0;
			NumProp += R.bProp ? 1 : 0;
			if (R.Distance < Nearest)
			{
				Nearest = R.Distance;
				NearestDir = d;
				NearestHit = R;
			}

			// Redundancy: how much of the light landing on this surface point is this light's own?
			const FVector Point = O + Dirs[d] * R.Distance;
			const float Own = (R.Distance < Reach[Index])
				? S.Mag / FMath::Max(R.Distance * R.Distance, 1024.f) : 0.f;
			SelfLit += Own;
			for (int32 j = 0; j < Pos.Num(); ++j)
			{
				if (j == Index) { continue; }
				const float Dj = static_cast<float>(FVector::Dist(Point, Pos[j]));
				if (Dj < Reach[j])
				{
					OtherLit += Mag[j] / FMath::Max(Dj * Dj, 1024.f);
				}
			}
		}

		Dists.Sort();
		const float Median = Dists.Num() ? Dists[Dists.Num() / 2] : -1.f;
		const float Share = (SelfLit + OtherLit) > 0.0 ? float(SelfLit / (SelfLit + OtherLit)) : -1.f;

		W->WriteObjectStart();
		W->WriteValue(TEXT("index"), S.SourceIndex);
		W->WriteValue(TEXT("row"), Index);
		W->WriteValue(TEXT("type"), S.Type);
		W->WriteValue(TEXT("mag"), S.Mag);
		W->WriteValue(TEXT("radius_cm"), S.RadiusCm);
		W->WriteValue(TEXT("intensity"), S.BaseIntensity);
		W->WriteValue(TEXT("style"), S.Style);
		const FLinearColor Col = Light->GetLightColor();
		W->WriteArrayStart(TEXT("color"));
		W->WriteValue(Col.R); W->WriteValue(Col.G); W->WriteValue(Col.B);
		W->WriteArrayEnd();
		W->WriteArrayStart(TEXT("pos"));
		W->WriteValue(O.X); W->WriteValue(O.Y); W->WriteValue(O.Z);
		W->WriteArrayEnd();

		// The headline: the nearest thing this light touches, and whether that thing emits.
		W->WriteValue(TEXT("near_dist"), Nearest < FLT_MAX ? Nearest : -1.f);
		W->WriteValue(TEXT("near_emissive"), NearestHit.bEmissive);
		W->WriteValue(TEXT("near_is_prop"), NearestHit.bProp);
		W->WriteValue(TEXT("near_surface"), NearestHit.Surface);
		W->WriteValue(TEXT("near_dir"), NearestDir);

		// The fan's shape: how boxed-in it is and at what scale it sits.
		W->WriteValue(TEXT("hit_frac"), float(NumHit) / Dirs.Num());
		W->WriteValue(TEXT("enc256"), float(Within256) / Dirs.Num());
		W->WriteValue(TEXT("enc512"), float(Within512) / Dirs.Num());
		W->WriteValue(TEXT("hit_med"), Median);
		W->WriteValue(TEXT("hit_mean"), NumHit ? float(SumDist / NumHit) : -1.f);
		W->WriteValue(TEXT("emissive_frac"), NumHit ? float(NumEmissive) / NumHit : 0.f);
		W->WriteValue(TEXT("prop_frac"), NumHit ? float(NumProp) / NumHit : 0.f);
		W->WriteValue(TEXT("share"), Share);
		W->WriteObjectEnd();
		++Probed;
	}

	W->WriteArrayEnd();
	W->WriteObjectEnd();
	W->Close();

	const FString Path = FElysiumContentPaths::LightEditsDir() / (Map->MapName + TEXT(".probe.json"));
	IFileManager::Get().MakeDirectory(*FElysiumContentPaths::LightEditsDir(), /*Tree*/ true);
	if (!FFileHelper::SaveStringToFile(Json, *Path))
	{
		UE_LOG(LogElysiumLightProbe, Warning, TEXT("lightprobe: could not write %s"), *Path);
		return -1;
	}
	UE_LOG(LogElysiumLightProbe, Log, TEXT("lightprobe: %d lights x %d rays -> %s"),
		Probed, Dirs.Num(), *Path);
	return Probed;
}

#endif // !UE_BUILD_SHIPPING
