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
		Def.CullMaxCm = Row.CullMaxCm;
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
			// Verbatim. Retail rewrites an authored `times` of 0 to -1, but it does so in the row
			// parser `0x100ccf90`, and the port's twin of that parser is
			// `UE_map_sidecars.split_output` -- which owns the rewrite since 0018 story 21-7. By
			// the time a row reaches this asset it has been parsed, so re-applying the rule here
			// would be a second owner disagreeing with the direct-construction path.
			OutDef.Times = OutputRow.Times;
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
		default:                               return TEXT("none");
		}
	}

	EElysiumEntityDefSource Load(const FString& MapName, FElysiumEntityDefs& Out,
		float SkyScale, const FVector& SkyOrigin)
	{
		const FString AssetPath = FElysiumContentPaths::BakedMapEntities(MapName);
		// The asset is used and released inside this call; nothing retains it, so the defs it
		// produced outlive it by value.
		if (const UElysiumMapEntities* Asset = LoadObject<UElysiumMapEntities>(nullptr, *AssetPath))
		{
			Asset->Deserialize(Out, SkyScale, SkyOrigin);
			UE_LOG(LogElysiumMapEntities, Log, TEXT("%s: %d entity def(s) from %s"),
				*MapName, Out.Num(), *AssetPath);
			return EElysiumEntityDefSource::Asset;
		}

		// 0018 story 21-1: there is no sidecar arm behind this any more, so a map whose asset is
		// missing or unreadable has no entities at all -- which is a failure, not a quiet empty
		// world. The error names the command that authors the asset.
		UE_LOG(LogElysiumMapEntities, Error,
			TEXT("'%s': no entity asset at %s; run: uv run elysium bake map --maps %s"),
			*MapName, *AssetPath, *MapName);
		return EElysiumEntityDefSource::None;
	}
}
