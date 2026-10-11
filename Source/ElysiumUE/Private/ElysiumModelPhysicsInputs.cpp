#include "ElysiumModelPhysicsInputs.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UElysiumModelPhysicsInputs::ApplyJson(UStaticMesh* Mesh, const FString& Json, bool& bOutOk, FString& OutError)
{
	bOutOk = false;
	OutError.Reset();
	TSharedPtr<FJsonObject> Document;
	if (!Mesh || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Document) || !Document.IsValid())
	{
		OutError = TEXT("model body inputs require a mesh and JSON object");
		return;
	}
	bool bPresent = false;
	const TArray<TSharedPtr<FJsonValue>>* Pairs = nullptr;
	if (!Document->TryGetBoolField(TEXT("sourceBody"), bPresent) || !Document->TryGetArrayField(TEXT("solidPairs"), Pairs))
	{
		OutError = TEXT("missing sourceBody/solidPairs model input contract");
		return;
	}
	auto* Inputs = NewObject<UElysiumModelPhysicsInputs>(Mesh);
	Inputs->bSourceBody = bPresent;
	if (Document->HasField(TEXT("studioMinsCm")) || Document->HasField(TEXT("studioMaxsCm")))
	{
		const auto ReadBounds = [&Document](const TCHAR* Name, FVector& Out)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Document->TryGetArrayField(Name, Values) || Values->Num() != 3) return false;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				double Number = 0;
				if (!(*Values)[Axis]->TryGetNumber(Number) || !FMath::IsFinite(Number)) return false;
				Out[Axis] = Number;
			}
			return true;
		};
		if (!ReadBounds(TEXT("studioMinsCm"), Inputs->StudioMinsCm)
			|| !ReadBounds(TEXT("studioMaxsCm"), Inputs->StudioMaxsCm)
			|| !Document->TryGetStringField(TEXT("studioSurfaceProperty"), Inputs->StudioSurfaceProperty))
		{ OutError = TEXT("studio bbox inputs require finite bounds and raw MDL surface"); return; }
		Inputs->bStudioBounds = true;
	}
	// ParseSolid 260238f0 processes ordered pairs; numeric duplicates overwrite, no inheritance.
	for (const auto& Value : *Pairs)
	{
		const TSharedPtr<FJsonObject>* Pair = nullptr;
		FString Key, Text;
		if (!Value->TryGetObject(Pair) || !(*Pair)->TryGetStringField(TEXT("key"), Key)
			|| !(*Pair)->TryGetStringField(TEXT("value"), Text))
		{
			OutError = TEXT("invalid ordered solid pair");
			return;
		}
		Key.ToLowerInline();
		if (Key == TEXT("mass")) Inputs->MassKg = FCString::Atof(*Text);
		else if (Key == TEXT("inertia")) Inputs->InertiaScale = FCString::Atof(*Text);
		else if (Key == TEXT("damping")) Inputs->LinearDamping = FCString::Atof(*Text);
		else if (Key == TEXT("rotdamping")) Inputs->AngularDamping = FCString::Atof(*Text);
		else if (Key == TEXT("volume")) Inputs->Volume = FCString::Atof(*Text);
		else if (Key == TEXT("drag")) Inputs->Drag = FCString::Atof(*Text);
		else if (Key == TEXT("rollingdrag")) Inputs->RollingDrag = FCString::Atof(*Text);
		else if (Key == TEXT("surfaceprop")) Inputs->SurfaceProperty = Text.ToLower();
	}
	const TArray<TSharedPtr<FJsonValue>>* Center = nullptr;
	if (Document->HasField(TEXT("massCenterCm")))
	{
		if (!Document->TryGetArrayField(TEXT("massCenterCm"), Center) || Center->Num() != 3)
		{
			OutError = TEXT("massCenterCm requires three numeric coordinates");
			return;
		}
		for (const auto& Coordinate : *Center)
		{
			double Number = 0;
			if (!Coordinate->TryGetNumber(Number) || !FMath::IsFinite(Number))
			{
				OutError = TEXT("massCenterCm requires finite numeric coordinates");
				return;
			}
		}
		Inputs->bMassCenter = true;
		Inputs->MassCenterCm = FVector((*Center)[0]->AsNumber(), (*Center)[1]->AsNumber(), (*Center)[2]->AsNumber());
	}
	FJsonSerializer::Serialize(*Pairs, TJsonWriterFactory<>::Create(&Inputs->SolidPairsJson));
	if (bPresent)
	{
		double HullCount = 0;
		UBodySetup* SourceBody = Mesh->GetBodySetup();
		if (!Document->TryGetNumberField(TEXT("firstSolidHullCount"), HullCount) || HullCount < 1
			|| HullCount != FMath::FloorToDouble(HullCount) || !SourceBody || HullCount > SourceBody->AggGeom.ConvexElems.Num())
		{
			OutError = TEXT("selected first solid requires its cooked native convex shapes");
			return;
		}
		Inputs->SelectedBody = DuplicateObject<UBodySetup>(SourceBody, Inputs);
		Inputs->SelectedBody->AggGeom.ConvexElems.SetNum(static_cast<int32>(HullCount));
		Inputs->SelectedBody->InvalidatePhysicsData();
		Inputs->SelectedBody->CreatePhysicsMeshes();
	}
	Mesh->RemoveUserDataOfClass(StaticClass());
	Mesh->AddAssetUserData(Inputs);
	bOutOk = true;
}

const UElysiumModelPhysicsInputs* UElysiumModelPhysicsInputs::Find(const UStaticMesh* Mesh)
{
	return Mesh ? Cast<UElysiumModelPhysicsInputs>(const_cast<UStaticMesh*>(Mesh)->GetAssetUserDataOfClass(StaticClass())) : nullptr;
}
