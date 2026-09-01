#include "ElysiumMapEntities.h"

#include "ElysiumContentPaths.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMapEntities, Log, All);

void UElysiumMapEntities::Deserialize(FElysiumEntityDefs& Out, float SkyScale,
	const FVector& SkyOrigin) const
{
	Out.MapName = MapName;
	Out.Defs.Reset();
	Out.SkyScale = SkyScale;
	Out.SkyOrigin = SkyOrigin;
	Out.Defs.Reserve(Entities.Num());

	for (const FElysiumMapEntityRow& Row : Entities)
	{
		FElysiumEntityDef Def;
		Def.Classname = Row.Classname;
		Def.TargetName = Row.TargetName;
		Def.Origin = Row.Origin;
		Def.Keys = Row.Keys;
		Def.Model = Row.Model;
		Def.Contents = Row.Contents;
		Def.bBlocksPlayer = Row.bBlocksPlayer;
		Def.BrushMesh = Row.BrushMesh;
		Def.ElevatorFloors = Row.ElevatorFloors;
		Def.bStartHidden = Row.bStartHidden;
		Def.bSky = Row.bSky;
		Def.ModelMesh = Row.ModelMesh;
		Def.ModelQuat = Row.ModelRotation();
		Def.HingeAxis = Row.HingeAxis;

		Def.Hulls.Reserve(Row.Hulls.Num());
		for (const FElysiumMapEntityHullRow& HullRow : Row.Hulls)
		{
			FElysiumConvexHull Hull;
			Hull.Vertices = HullRow.Vertices;
			Def.Hulls.Add(MoveTemp(Hull));
		}

		Def.Outputs.Reserve(Row.Outputs.Num());
		for (const FElysiumMapEntityOutputRow& OutputRow : Row.Outputs)
		{
			FElysiumOutputDef OutDef;
			OutDef.Name = OutputRow.Name;
			OutDef.Target = OutputRow.Target;
			OutDef.Input = OutputRow.Input;
			OutDef.Param = OutputRow.Param;
			OutDef.Delay = OutputRow.Delay;
			OutDef.Times = OutputRow.Times;
			// Retail's parser seeds `times` to -1 and rewrites an authored 0 back to -1, so both
			// spell unlimited and only a positive value is a real countdown. Normalised here, the
			// same one owner the `.ents` reader is (R3.4), not in the exporter or the stage.
			if (OutDef.Times == 0)
			{
				OutDef.Times = -1;
			}
			OutDef.Python = OutputRow.Python;
			Def.Outputs.Add(MoveTemp(OutDef));
		}

		// A miniature entity's placement is the 3D-skybox transform of its raw one:
		// `world(v) = scale * (v - skyOrigin)`. Applied here, once, exactly as
		// `FElysiumEntityDefs::Parse` applies it, so every consumer downstream is placed right
		// without knowing which transport the defs came off. Hulls are entity-LOCAL, so they take
		// the scale but not the translation.
		if (Def.bSky && SkyScale != 1.f)
		{
			Def.Origin = (Def.Origin - SkyOrigin) * SkyScale;
			for (FElysiumConvexHull& Hull : Def.Hulls)
			{
				for (FVector& Vertex : Hull.Vertices)
				{
					Vertex *= SkyScale;
				}
			}
		}

		Out.Defs.Add(MoveTemp(Def));
	}
}

namespace ElysiumEntityDefSource
{
	const TCHAR* ToString(EElysiumEntityDefSource Source)
	{
		switch (Source)
		{
		case EElysiumEntityDefSource::Asset:   return TEXT("asset");
		case EElysiumEntityDefSource::Sidecar: return TEXT("sidecar");
		default:                               return TEXT("none");
		}
	}

	EElysiumEntityDefSource Load(const FString& MapName, FElysiumEntityDefs& Out,
		float SkyScale, const FVector& SkyOrigin)
	{
		const FString AssetPath = FElysiumContentPaths::BakedMapEntities(MapName);
		// The asset is used and released inside this call; nothing retains it, so the defs it
		// produced outlive it by value the same way the parsed sidecar's do.
		// Quiet: a map with no asset is the normal case until R4.6 converts them all, and the
		// fallback below is the answer, not a warning.
		if (const UElysiumMapEntities* Asset = LoadObject<UElysiumMapEntities>(
			nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Asset->Deserialize(Out, SkyScale, SkyOrigin);
			UE_LOG(LogElysiumMapEntities, Log, TEXT("%s: %d entity def(s) from %s"),
				*MapName, Out.Num(), *AssetPath);
			return EElysiumEntityDefSource::Asset;
		}

		if (FElysiumEntityDefs::Parse(FElysiumContentPaths::MapEnts(MapName), Out,
			SkyScale, SkyOrigin))
		{
			UE_LOG(LogElysiumMapEntities, Log, TEXT("%s: %d entity def(s) from %s.ents (no %s)"),
				*MapName, Out.Num(), *MapName, *AssetPath);
			return EElysiumEntityDefSource::Sidecar;
		}

		return EElysiumEntityDefSource::None;
	}
}
