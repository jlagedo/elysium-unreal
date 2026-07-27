#include "ElysiumEntityDefs.h"

#include "ElysiumContentPaths.h"
#include "ElysiumMapSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumDefs, Log, All);

namespace
{
	// Read a JSON [x, y, z] number array into an FVector (zero on any other shape).
	FVector ParseVec3(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		FVector V = FVector::ZeroVector;
		if (Arr.Num() == 3)
		{
			V.X = Arr[0]->AsNumber();
			V.Y = Arr[1]->AsNumber();
			V.Z = Arr[2]->AsNumber();
		}
		return V;
	}

	// Read a JSON [x, y, z, w] number array into an FQuat (identity on any other shape). The
	// exporter emits it via source_angles_to_unreal_quat, already Unreal-space (8.3).
	FQuat ParseQuat(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		if (Arr.Num() == 4)
		{
			return FQuat(Arr[0]->AsNumber(), Arr[1]->AsNumber(), Arr[2]->AsNumber(), Arr[3]->AsNumber());
		}
		return FQuat::Identity;
	}
}

FString FElysiumEntityDefs::LevelScriptModule() const
{
	for (const FElysiumEntityDef& Def : Defs)
	{
		if (Def.Classname.Equals(TEXT("worldspawn"), ESearchCase::IgnoreCase))
		{
			return Def.Keys.FindRef(TEXT("levelscript")).TrimStartAndEnd();
		}
	}
	return FString();
}

bool FElysiumEntityDefs::Parse(const FString& EntsPath, FElysiumEntityDefs& Out,
	float SkyScale, const FVector& SkyOrigin)
{
	Out.MapName.Reset();
	Out.Defs.Reset();
	Out.SkyScale = SkyScale;
	Out.SkyOrigin = SkyOrigin;

	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *EntsPath))
	{
		return false;   // no .ents sidecar for this map
	}

	// The whole file is one JSON object ({"map":..., "entities":[...]}), so it is parsed in
	// one pass — not line-oriented like the other sidecars.
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogElysiumDefs, Warning, TEXT(".ents parse failed (bad JSON): %s"), *EntsPath);
		return false;
	}

	Root->TryGetStringField(TEXT("map"), Out.MapName);

	const TArray<TSharedPtr<FJsonValue>>* Entities = nullptr;
	if (!Root->TryGetArrayField(TEXT("entities"), Entities))
	{
		return false;   // no entities array: nothing to spawn
	}

	Out.Defs.Reserve(Entities->Num());
	for (const TSharedPtr<FJsonValue>& EntVal : *Entities)
	{
		const TSharedPtr<FJsonObject>* EntObjPtr = nullptr;
		if (!EntVal.IsValid() || !EntVal->TryGetObject(EntObjPtr))
		{
			continue;
		}
		const TSharedPtr<FJsonObject>& E = *EntObjPtr;

		FElysiumEntityDef Def;
		E->TryGetStringField(TEXT("classname"), Def.Classname);
		E->TryGetStringField(TEXT("targetname"), Def.TargetName);
		E->TryGetBoolField(TEXT("start_hidden"), Def.bStartHidden);
		E->TryGetBoolField(TEXT("sky"), Def.bSky);

		const TArray<TSharedPtr<FJsonValue>>* OriginArr = nullptr;
		if (E->TryGetArrayField(TEXT("origin"), OriginArr))
		{
			Def.Origin = ParseVec3(*OriginArr);
		}

		// keys{}: the raw keyvalues, all string-valued in the export.
		const TSharedPtr<FJsonObject>* KeysObjPtr = nullptr;
		if (E->TryGetObjectField(TEXT("keys"), KeysObjPtr))
		{
			const TSharedPtr<FJsonObject>& K = *KeysObjPtr;
			Def.Keys.Reserve(K->Values.Num());
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : K->Values)
			{
				FString Value;
				if (Pair.Value.IsValid() && Pair.Value->TryGetString(Value))
				{
					Def.Keys.Add(Pair.Key, Value);
				}
			}
		}

		// Brush-entity fields — present only when the entity had a "*N" model.
		E->TryGetNumberField(TEXT("model"), Def.Model);
		E->TryGetNumberField(TEXT("contents"), Def.Contents);
		E->TryGetBoolField(TEXT("blocks_player"), Def.bBlocksPlayer);

		const TArray<TSharedPtr<FJsonValue>>* HullsArr = nullptr;
		if (E->TryGetArrayField(TEXT("hulls"), HullsArr))
		{
			Def.Hulls.Reserve(HullsArr->Num());
			for (const TSharedPtr<FJsonValue>& HullVal : *HullsArr)
			{
				const TArray<TSharedPtr<FJsonValue>>* Flat = nullptr;
				if (!HullVal.IsValid() || !HullVal->TryGetArray(Flat))
				{
					continue;
				}
				// One flat x y z x y z ... list per hull — whole (x,y,z) triples only.
				FElysiumConvexHull Hull;
				Hull.Vertices.Reserve(Flat->Num() / 3);
				for (int32 I = 0; I + 2 < Flat->Num(); I += 3)
				{
					Hull.Vertices.Emplace(
						(*Flat)[I]->AsNumber(),
						(*Flat)[I + 1]->AsNumber(),
						(*Flat)[I + 2]->AsNumber());
				}
				Def.Hulls.Add(MoveTemp(Hull));
			}
		}

		// outputs[]: the 7-field rows (name + target/input/param/delay/times/python).
		const TArray<TSharedPtr<FJsonValue>>* OutputsArr = nullptr;
		if (E->TryGetArrayField(TEXT("outputs"), OutputsArr))
		{
			Def.Outputs.Reserve(OutputsArr->Num());
			for (const TSharedPtr<FJsonValue>& OutVal : *OutputsArr)
			{
				const TSharedPtr<FJsonObject>* OutObjPtr = nullptr;
				if (!OutVal.IsValid() || !OutVal->TryGetObject(OutObjPtr))
				{
					continue;
				}
				const TSharedPtr<FJsonObject>& O = *OutObjPtr;

				FElysiumOutputDef OutDef;
				O->TryGetStringField(TEXT("name"), OutDef.Name);
				O->TryGetStringField(TEXT("target"), OutDef.Target);
				O->TryGetStringField(TEXT("input"), OutDef.Input);
				O->TryGetStringField(TEXT("param"), OutDef.Param);
				double Delay = 0.0;
				if (O->TryGetNumberField(TEXT("delay"), Delay))
				{
					OutDef.Delay = static_cast<float>(Delay);
				}
				O->TryGetNumberField(TEXT("times"), OutDef.Times);
				O->TryGetStringField(TEXT("python"), OutDef.Python);
				Def.Outputs.Add(MoveTemp(OutDef));
			}
		}

		// Static-mesh render annotation (8.1 → 8.3): the decoded OBJ stem + its Unreal-space
		// placement rotation. Present only for point entities whose `.mdl` decoded.
		E->TryGetStringField(TEXT("model_mesh"), Def.ModelMesh);
		const TArray<TSharedPtr<FJsonValue>>* QuatArr = nullptr;
		if (E->TryGetArrayField(TEXT("model_quat"), QuatArr))
		{
			Def.ModelQuat = ParseQuat(*QuatArr);
		}

		// Constraint axis (8.4): the pre-converted, normalized Unreal-space hinge axis, read
		// verbatim like origin. Present only on phys_* constraints carrying `hingeaxis`.
		const TArray<TSharedPtr<FJsonValue>>* AxisArr = nullptr;
		if (E->TryGetArrayField(TEXT("hinge_axis"), AxisArr))
		{
			Def.HingeAxis = ParseVec3(*AxisArr);
		}

		// B7 — a miniature entity's placement is the 3D-skybox transform of its raw one:
		// `world(v) = scale * (v - skyOrigin)`. Applied here, once, so every consumer
		// downstream — brush bodies, prop bodies, gizmos, the click-pick — is placed right
		// without knowing the miniature exists. Hulls are entity-LOCAL (world = origin +
		// vertex), so they take the scale but not the translation.
		if (Def.bSky && SkyScale != 1.f)
		{
			Def.Origin = (Def.Origin - SkyOrigin) * SkyScale;
			for (FElysiumConvexHull& Hull : Def.Hulls)
			{
				for (FVector& V : Hull.Vertices)
				{
					V *= SkyScale;
				}
			}
		}

		Out.Defs.Add(MoveTemp(Def));
	}

	return true;
}

// --- Verification command ---------------------------------------------------------------
// `elysium.ents [map]` parses a map's `.ents` off disk and logs a summary. It is a
// standalone check that the parser round-trips the export before the entity world (P1.4)
// consumes the defs; it spawns nothing. No arg uses the currently-loaded map.
static FString ElysiumResolveEntsMap(UWorld* World, const TArray<FString>& Args)
{
	if (Args.Num() >= 1)
	{
		return Args[0];
	}
	if (World)
	{
		if (const UGameInstance* GI = World->GetGameInstance())
		{
			if (const UElysiumMapSubsystem* Maps = GI->GetSubsystem<UElysiumMapSubsystem>())
			{
				return Maps->GetCurrentMapName();
			}
		}
	}
	return FString();
}

static FAutoConsoleCommandWithWorldAndArgs GElysiumEntsCmd(
	TEXT("elysium.ents"),
	TEXT("elysium.ents [map] — parse <map>.ents and log a summary (defaults to the current map)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const FString Map = ElysiumResolveEntsMap(World, Args);
		if (Map.IsEmpty())
		{
			UE_LOG(LogElysiumDefs, Warning, TEXT("elysium.ents: no map (pass a name or load one first)"));
			return;
		}

		FElysiumEntityDefs Defs;
		if (!FElysiumEntityDefs::Parse(FElysiumContentPaths::MapEnts(Map), Defs))
		{
			UE_LOG(LogElysiumDefs, Warning, TEXT("elysium.ents: no parseable %s.ents"), *Map);
			return;
		}

		int32 Brush = 0, Hulls = 0, Outputs = 0, PyOnly = 0, Hidden = 0;
		TSet<FString> Classes;
		for (const FElysiumEntityDef& D : Defs.Defs)
		{
			if (D.IsBrush())
			{
				++Brush;
				Hulls += D.Hulls.Num();
			}
			Outputs += D.Outputs.Num();
			for (const FElysiumOutputDef& O : D.Outputs)
			{
				if (O.IsPythonOnly())
				{
					++PyOnly;
				}
			}
			if (D.bStartHidden)
			{
				++Hidden;
			}
			Classes.Add(D.Classname);
		}

		UE_LOG(LogElysiumDefs, Display,
			TEXT("%s.ents: %d entities (%d brush, %d hulls, %d outputs [%d py-only], %d start_hidden, %d classnames)"),
			*Map, Defs.Num(), Brush, Hulls, Outputs, PyOnly, Hidden, Classes.Num());
	}));
