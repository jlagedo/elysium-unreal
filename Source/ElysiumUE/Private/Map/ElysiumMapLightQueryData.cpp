#include "ElysiumMapLightQueryData.h"
#include "ElysiumMapCollisionPayload.h"
#include "ElysiumMoveSolve.h"
#if WITH_EDITOR
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#endif

int32 UElysiumMapLightQueryData::ClusterAt(const FVector& PointCm) const
{
	int32 Node = Records.HeadNode;
	for (int32 Depth = 0; Node >= 0 && Depth <= Records.Nodes.Num(); ++Depth)
	{
		if (!Records.Nodes.IsValidIndex(Node)) return -1;
		const FElysiumLightQueryNode& N = Records.Nodes[Node];
		Node = FVector::DotProduct(N.Normal, PointCm) - N.Distance >= 0 ? N.Front : N.Back;
	}
	const int32 Leaf = -Node - 1;
	return Node < 0 && Records.LeafClusters.IsValidIndex(Leaf) ? Records.LeafClusters[Leaf] : -1;
}

bool UElysiumMapLightQueryData::ClusterVisible(int32 From, int32 To) const
{
	if (From < 0 || To < 0 || From >= Records.NumClusters || To >= Records.NumClusters) return false;
	const int64 Offset = int64(From) * ((Records.NumClusters + 7) / 8) + To / 8;
	return Offset >= 0 && Offset < Records.Pvs.Num()
		&& (Records.Pvs[static_cast<int32>(Offset)] & (1 << (To & 7))) != 0;
}

bool UElysiumMapLightQueryData::IsValidQuery() const
{
	return Occluders && Sky && !Records.Nodes.IsEmpty() && !Records.LeafClusters.IsEmpty()
		&& Records.NumClusters > 0 && int64(Records.Pvs.Num())
			== int64(Records.NumClusters) * ((Records.NumClusters + 7) / 8);
}

float ElysiumWorldLight::DistanceFalloff(const FElysiumWorldLight& L, const FVector& DeltaCm)
{
	const double D2 = DeltaCm.SizeSquared();
	const double D = FMath::Sqrt(D2);
	if ((L.Type == 0 || L.Type == 1 || L.Type == 2) && L.Radius != 0 && D > L.Radius) return 0;
	switch (L.Type)
	{
	case 0:
		// InvRSquared dispatch 0x201a65a8 -> scalar 0x200ad040 / SSE 0x200ad3f0.
		return float((ElysiumMove::U * ElysiumMove::U)
			/ FMath::Max(D2, double(ElysiumMove::U * ElysiumMove::U)));
	case 1: case 2: return float(1.0 / (L.Quadratic * D2 + L.Linear * D + L.Constant));
	case 4: return FMath::Max(0.f, float((L.QuakeDistance - D) / ElysiumMove::U));
	default: return 1.f;
	}
}

float ElysiumWorldLight::Angle(const FElysiumWorldLight& L, const FVector& DirectionToLight)
{
	const float Surface = float(DirectionToLight.SizeSquared());
	const float Facing = float(-FVector::DotProduct(L.Normal, DirectionToLight));
	switch (L.Type)
	{
	case 0: return Facing > 0.01f ? Facing * Surface : 0.f;
	case 1: case 4: return Surface;
	case 2:
		if (Facing <= L.StopDot2) return 0.f;
		if (Facing >= L.StopDot) return Surface;
		{
			const float Ramp = (Facing - L.StopDot2) / (L.StopDot - L.StopDot2);
			return Surface * ((L.Exponent == 0.f || L.Exponent == 1.f) ? Ramp : FMath::Pow(Ramp, L.Exponent));
		}
	case 3: return FMath::Max(0.f, Facing);
	case 5: return 1.f;
	default: return 0.f;
	}
}

float ElysiumWorldLight::Luminance(const FVector& Rgb)
{
	return float(Rgb.X * 0.30 + Rgb.Y * 0.59 + Rgb.Z * 0.11);
}

float ElysiumWorldLight::StyleValue(const FString& Pattern, double Time)
{
	if (Pattern.IsEmpty()) return 256.f / 264.f;
	const int32 Frame = FMath::TruncToInt(Time * 10.0);
	const int32 Index = (Frame % Pattern.Len() + Pattern.Len()) % Pattern.Len();
	return float((Pattern[Index] - TEXT('a')) * 22) / 264.f;
}

float ElysiumWorldLight::Query(const UElysiumMapLightQueryData& Data, const FVector& PointCm,
	TFunctionRef<float(int32)> Style, TFunctionRef<bool(const FVector&, bool)> Trace)
{
	const int32 Cluster = Data.ClusterAt(PointCm);
	if (Cluster < 0) return 0.f;
	FVector Sum = FVector::ZeroVector;
	bool bSunTested = false;
	for (const FElysiumWorldLight& L : Data.Records.Lights)
	{
		if (L.Cluster < 0 || L.Type == 5) continue;
		if (L.Type == 3)
		{
			if (!bSunTested)
			{
				bSunTested = true;
				if (Trace(PointCm - L.Normal * (56755.84 * ElysiumMove::U), true)) Sum += L.Intensity;
			}
			continue;
		}
		if (!Data.ClusterVisible(Cluster, L.Cluster)) continue;
		const FVector Delta = L.Position - PointCm;
		const float Scale = Style(L.Style) * DistanceFalloff(L, Delta);
		if (Scale <= 0.f || !Trace(L.Position, false)) continue;
		Sum += L.Intensity * (Scale * Angle(L, Delta.GetSafeNormal()));
	}
	return Luminance(Sum);
}

#if WITH_EDITOR
FString UElysiumMapLightQueryData::AuthorJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root)
		return TEXT("invalid light query JSON");
	const TSharedPtr<FJsonObject>* Rows;
	if (!Root->TryGetObjectField(TEXT("records"), Rows)
		|| !FJsonObjectConverter::JsonObjectToUStruct(Rows->ToSharedRef(), &Records))
		return TEXT("invalid light query records");
	MapName = Root->GetStringField(TEXT("map"));
	if (Occluders) Occluders->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	if (Sky) Sky->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	Occluders = NewObject<UElysiumMapCollisionPayload>(this);
	Sky = NewObject<UElysiumMapCollisionPayload>(this);
	Occluders->MapName = Sky->MapName = MapName;
	TArray<FElysiumCollisionHull> Hulls;
	for (const TSharedPtr<FJsonValue>& Row : Root->GetArrayField(TEXT("hulls")))
	{
		FElysiumCollisionHull& Hull = Hulls.AddDefaulted_GetRef();
		const auto& Values = Row->AsArray();
		if (Values.Num() % 3 != 0) return TEXT("invalid light query hull");
		for (int32 I = 0; I < Values.Num(); I += 3)
			Hull.Vertices.Emplace(Values[I]->AsNumber(), Values[I+1]->AsNumber(), Values[I+2]->AsNumber());
	}
	Occluders->AuthorWorldHulls(Hulls);
	auto AuthorSoup = [&Root](const TCHAR* Key, UElysiumMapCollisionPayload* Asset)
	{
		TArray<FVector> Vertices;
		TArray<int32> Indices;
		for (const auto& Row : Root->GetArrayField(Key))
		{
			const auto& V = Row->AsArray();
			if (V.Num() != 9) return false;
			for (int32 I = 0; I < 9; I += 3)
			{
				Indices.Add(Vertices.Num());
				Vertices.Emplace(V[I]->AsNumber(), V[I+1]->AsNumber(), V[I+2]->AsNumber());
			}
		}
		Asset->AuthorDisplacement(Vertices, Indices);
		return true;
	};
	if (!AuthorSoup(TEXT("displacements"), Occluders) || !AuthorSoup(TEXT("sky"), Sky))
		return TEXT("invalid light query triangle soup");
	if (!IsValidQuery()) return TEXT("invalid light query partition/PVS");
	const FString Errors = Occluders->CookAuthored() + Sky->CookAuthored();
	return Errors;
}
#endif
