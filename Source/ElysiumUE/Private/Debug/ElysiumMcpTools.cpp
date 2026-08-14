#include "Debug/ElysiumMcpTools.h"

#if ELYSIUM_WITH_MCP

#include "ElysiumAudioSubsystem.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraService.h"
#include "ElysiumPlayerCameraManager.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "Debug/ElysiumLogTap.h"
#include "ElysiumMapActor.h"
#include "Visual/ElysiumMapVisuals.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumPlayer.h"
#include "Debug/ElysiumScreenshot.h"
#include "Debug/ElysiumWireDump.h"
#include "ElysiumWireReport.h"
#include "Audio/ElysiumSoundScheme.h"
#include "ElysiumVariant.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "IModelContextProtocolModule.h"
#include "IModelContextProtocolTool.h"
#include "ModelContextProtocolSession.h"
#include "ModelContextProtocolToolResults.h"
#include "Misc/OutputDevice.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMcpTools, Log, All);

// The engine's rolling FPS average (Engine/Private/UnrealEngine.cpp); it has no public header, so
// declare it the way engine code does when it needs it. Used by elysium_player_get.
extern ENGINE_API float GAverageFPS;

// Everything in this file is file-local; the module builds with unity on, so a bare `namespace {}`
// here would still collide with another translation unit's helpers of the same name. The named
// namespace is the project's convention for that.
namespace ElysiumMcpImpl
{
	using namespace UE::ModelContextProtocol;

	// ---------------------------------------------------------------------------------------
	// Live-state resolution. Tools resolve the world at CALL time (never at registration), so one
	// registered tool list survives map travel, PIE start/stop, and an idle editor.
	// ---------------------------------------------------------------------------------------

	// The playing world: PIE wins over a standalone Game world, so a tool called from an editor
	// session with PIE running targets the session the developer is looking at.
	UWorld* LiveWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		UWorld* GameWorld = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World)
			{
				continue;
			}
			if (Context.WorldType == EWorldType::PIE)
			{
				return World;
			}
			if (Context.WorldType == EWorldType::Game)
			{
				GameWorld = World;
			}
		}
		return GameWorld;
	}

	UGameInstance* LiveGameInstance()
	{
		UWorld* World = LiveWorld();
		return World ? World->GetGameInstance() : nullptr;
	}

	template <typename T>
	T* Sub()
	{
		UGameInstance* GameInstance = LiveGameInstance();
		return GameInstance ? GameInstance->GetSubsystem<T>() : nullptr;
	}

	AElysiumMapActor* MapActor()
	{
		UElysiumMapSubsystem* Maps = Sub<UElysiumMapSubsystem>();
		return Maps ? Maps->GetCurrentMap() : nullptr;
	}

	FElysiumEntityWorld* Entities()
	{
		AElysiumMapActor* Map = MapActor();
		return Map ? Map->GetEntityWorld() : nullptr;
	}

	// The body sample's own two enums as words (CCC1). The animation record's live in
	// `ElysiumAnimIntent` beside the types they name; these two belong to the sample, which predates
	// it.
	const TCHAR* StanceName(EElysiumStance Stance)
	{
		switch (Stance)
		{
		case EElysiumStance::Lowering: return TEXT("lowering");
		case EElysiumStance::Ducked:   return TEXT("ducked");
		case EElysiumStance::Rising:   return TEXT("rising");
		default:                       return TEXT("standing");
		}
	}

	const TCHAR* JumpPhaseName(EElysiumJumpPhase Phase)
	{
		switch (Phase)
		{
		case EElysiumJumpPhase::Ascend:  return TEXT("ascend");
		case EElysiumJumpPhase::Descend: return TEXT("descend");
		default:                         return TEXT("ground");
		}
	}

	const TCHAR* CameraRequestKindName(EElysiumCameraRequestKind Kind)
	{
		switch (Kind)
		{
		case EElysiumCameraRequestKind::Aim:      return TEXT("aim");
		case EElysiumCameraRequestKind::Inspect:  return TEXT("inspect");
		case EElysiumCameraRequestKind::Dialogue: return TEXT("dialogue");
		case EElysiumCameraRequestKind::Feed:     return TEXT("feed");
		case EElysiumCameraRequestKind::Death:    return TEXT("death");
		case EElysiumCameraRequestKind::Sequence: return TEXT("sequence");
		default:                                  return TEXT("player");
		}
	}

	const TCHAR* CameraControlName(EElysiumCameraControlPolicy Control)
	{
		switch (Control)
		{
		case EElysiumCameraControlPolicy::LookOnly: return TEXT("look-only");
		case EElysiumCameraControlPolicy::Locked:   return TEXT("locked");
		default:                                     return TEXT("preserve");
		}
	}

	const TCHAR* CameraFallbackName(EElysiumCameraFallback Fallback)
	{
		switch (Fallback)
		{
		case EElysiumCameraFallback::SourceShot:      return TEXT("source-shot");
		case EElysiumCameraFallback::AuthoredProfile: return TEXT("authored-profile");
		case EElysiumCameraFallback::PlayerView:      return TEXT("player-view");
		default:                                      return TEXT("none");
		}
	}

	APlayerController* LivePlayerController()
	{
		UWorld* World = LiveWorld();
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	APawn* LivePawn()
	{
		APlayerController* Controller = LivePlayerController();
		return Controller ? Controller->GetPawn() : nullptr;
	}

	// ---------------------------------------------------------------------------------------
	// JSON plumbing
	// ---------------------------------------------------------------------------------------

	TSharedRef<FJsonObject> Obj() { return MakeShared<FJsonObject>(); }

	TSharedRef<FJsonObject> Vec(const FVector& V)
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("x"), V.X);
		Out->SetNumberField(TEXT("y"), V.Y);
		Out->SetNumberField(TEXT("z"), V.Z);
		return Out;
	}

	FModelContextProtocolToolResult Structured(const TSharedRef<FJsonObject>& Body)
	{
		return MakeStructuredContentResult(TSharedPtr<FJsonValue>(MakeShared<FJsonValueObject>(Body)));
	}

	// Report the result of a Travel/Reload/NewGame the same way across the three map tools. Under
	// hard travel a live world's swap is an end-of-frame OpenLevel, so the new map is not built when
	// the tool returns: PendingMapLoad is set and there is no current map yet. Surface pending=true +
	// pending_travel so the caller knows to poll elysium_maps_list. Only a cold boot (no world to tear
	// down) builds in-call, in which case the map actor is already live and its counts are readable.
	void AddTravelOutcome(const TSharedRef<FJsonObject>& Body, UElysiumMapSubsystem* Maps)
	{
		if (Maps->HasPendingMapLoad())
		{
			Body->SetBoolField(TEXT("pending"), true);
			Body->SetStringField(TEXT("pending_travel"), Maps->PendingTravelDesc());
			Body->SetStringField(TEXT("note"),
				TEXT("deferred hard travel — poll elysium_maps_list until pending_travel clears and spawn_done is true"));
		}
		else if (AElysiumMapActor* Actor = Maps->GetCurrentMap())
		{
			Body->SetBoolField(TEXT("pending"), false);
			Body->SetStringField(TEXT("current"), Maps->GetCurrentMapName());
			Body->SetNumberField(TEXT("entity_count"), Actor->EntityCount);
			Body->SetStringField(TEXT("entry_landmark"), Actor->EntryLandmark);
		}
	}

	FString ParamStr(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, const FString& Default = FString())
	{
		FString Value;
		return (Params.IsValid() && Params->TryGetStringField(Key, Value)) ? Value : Default;
	}

	int32 ParamInt(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, int32 Default)
	{
		int32 Value = 0;
		return (Params.IsValid() && Params->TryGetNumberField(Key, Value)) ? Value : Default;
	}

	double ParamNum(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, double Default)
	{
		double Value = 0.0;
		return (Params.IsValid() && Params->TryGetNumberField(Key, Value)) ? Value : Default;
	}

	bool ParamBool(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, bool Default)
	{
		bool Value = false;
		return (Params.IsValid() && Params->TryGetBoolField(Key, Value)) ? Value : Default;
	}

	bool HasParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key)
	{
		return Params.IsValid() && Params->HasField(Key);
	}

	// A JSON value as the substrate's marshalling currency. Mirrors what a map-authored output
	// param would carry: numbers keep their integral-ness (a `times`/counter input reads an int),
	// everything else lands as its natural variant, and an absent param is Void.
	FElysiumVariant ParamVariant(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key)
	{
		if (!Params.IsValid())
		{
			return FElysiumVariant::Void();
		}
		const TSharedPtr<FJsonValue> Value = Params->TryGetField(Key);
		if (!Value.IsValid())
		{
			return FElysiumVariant::Void();
		}
		switch (Value->Type)
		{
		case EJson::Boolean: return FElysiumVariant::Bool(Value->AsBool());
		case EJson::Number:
		{
			const double N = Value->AsNumber();
			return (N == FMath::TruncToDouble(N) && FMath::Abs(N) < static_cast<double>(MAX_int32))
				? FElysiumVariant::Int(static_cast<int32>(N))
				: FElysiumVariant::Float(static_cast<float>(N));
		}
		case EJson::String: return FElysiumVariant::String(Value->AsString());
		default:            return FElysiumVariant::Void();
		}
	}

	// --- Input-schema builder ---------------------------------------------------------------
	// Small enough to hand-roll; the alternative (a USTRUCT per tool + FJsonSchemaGenerator) buys
	// nothing here because none of these parameter sets is reused.

	struct FSchema
	{
		TSharedRef<FJsonObject> Root = Obj();
		TSharedRef<FJsonObject> Props = Obj();
		TArray<FString> RequiredKeys;

		FSchema& Add(const TCHAR* Name, const TCHAR* Type, const TCHAR* Description, bool bRequired = false)
		{
			TSharedRef<FJsonObject> Prop = Obj();
			Prop->SetStringField(TEXT("type"), Type);
			Prop->SetStringField(TEXT("description"), Description);
			Props->SetObjectField(Name, Prop);
			if (bRequired)
			{
				RequiredKeys.Add(Name);
			}
			return *this;
		}

		TSharedPtr<FJsonObject> Build()
		{
			Root->SetStringField(TEXT("type"), TEXT("object"));
			Root->SetObjectField(TEXT("properties"), Props);
			if (RequiredKeys.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Required;
				for (const FString& Key : RequiredKeys)
				{
					Required.Add(MakeShared<FJsonValueString>(Key));
				}
				Root->SetArrayField(TEXT("required"), Required);
			}
			return Root;
		}
	};

	// --- Tool shapes -------------------------------------------------------------------------

	// Every synchronous tool. Run() is invoked on the game thread (the MCP server ticks off the
	// core ticker and serializes tool calls there), so a handler may touch the entity world,
	// the map actor, and the pawn directly with no marshalling.
	struct FTool final : IModelContextProtocolTool
	{
		FString Name;
		FString Description;
		TSharedPtr<FJsonObject> Schema;
		TFunction<FModelContextProtocolToolResult(const TSharedPtr<FJsonObject>&)> Handler;

		virtual FString GetName() const override { return Name; }
		virtual FString GetDescription() const override { return Description; }
		virtual TSharedPtr<FJsonObject> GetInputJsonSchema() const override { return Schema; }
		virtual FModelContextProtocolToolResult Run(const TSharedPtr<FJsonObject>& Params) override
		{
			return Handler ? Handler(Params) : MakeErrorResult(TEXT("tool has no handler"));
		}
	};

	// The deferred shape, for work that cannot answer within one call (a screenshot needs the next
	// rendered frame). The result callback may be invoked from any thread; the server hops.
	struct FAsyncTool final : IModelContextProtocolTool
	{
		FString Name;
		FString Description;
		TSharedPtr<FJsonObject> Schema;
		TFunction<void(const TSharedPtr<FJsonObject>&, const FResultCallback&)> Handler;

		virtual FString GetName() const override { return Name; }
		virtual FString GetDescription() const override { return Description; }
		virtual TSharedPtr<FJsonObject> GetInputJsonSchema() const override { return Schema; }
		virtual void RunAsync(const FModelContextProtocolToolRequestId&, const TSharedPtr<FJsonObject>& Params,
			const FResultCallback& OnComplete) override
		{
			if (Handler)
			{
				Handler(Params, OnComplete);
			}
			else
			{
				OnComplete(MakeErrorResult(TEXT("tool has no handler")));
			}
		}
	};

	TSharedRef<IModelContextProtocolTool> MakeTool(const TCHAR* Name, const TCHAR* Description, FSchema& Schema,
		TFunction<FModelContextProtocolToolResult(const TSharedPtr<FJsonObject>&)> Handler)
	{
		TSharedRef<FTool> Tool = MakeShared<FTool>();
		Tool->Name = Name;
		Tool->Description = Description;
		Tool->Schema = Schema.Build();
		Tool->Handler = MoveTemp(Handler);
		return Tool;
	}

	// ---------------------------------------------------------------------------------------
	// Entity marshalling
	// ---------------------------------------------------------------------------------------

	// The class chain, derived first. The registry links each descriptor to its base by name and
	// walks that chain at lookup time; enumerating (as the inspector does) means walking it here.
	void ChainDescs(const FElysiumClassDesc* Desc, TArray<const FElysiumClassDesc*>& Out)
	{
		const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
		while (Desc)
		{
			Out.Add(Desc);
			Desc = Desc->BaseName.IsNone() ? nullptr : Registry.Find(Desc->BaseName);
		}
	}

	TSharedRef<FJsonObject> EntitySummary(const FElysiumEntity& Entity)
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("index"), Entity.Handle.Index);
		Out->SetStringField(TEXT("targetname"), Entity.TargetName);
		Out->SetStringField(TEXT("classname"), Entity.Def ? Entity.Def->Classname : FString());
		Out->SetBoolField(TEXT("dead"), Entity.IsDead());
		Out->SetBoolField(TEXT("hidden"), Entity.IsHidden());
		Out->SetBoolField(TEXT("inert"), Entity.IsInert());
		// `record_only` is the load-bearing one for a QA read: it means no leaf class is registered
		// for this classname, so the record parses and indexes but does nothing.
		Out->SetBoolField(TEXT("record_only"), Entity.IsRecordOnly());
		Out->SetBoolField(TEXT("brush"), Entity.Def && Entity.Def->IsBrush());
		// The live origin — where the entity actually is. A `scripted_sequence` places its NPC on a
		// mark and scripts call SetOrigin, so the def's spawn point is a different fact; it is
		// reported alongside only when the two have diverged.
		Out->SetObjectField(TEXT("origin"), Vec(Entity.Origin));
		if (Entity.Def && !Entity.Origin.Equals(Entity.Def->Origin, 0.01))
		{
			Out->SetObjectField(TEXT("spawn_origin"), Vec(Entity.Def->Origin));
		}
		return Out;
	}

	TSharedRef<FJsonObject> EntityDetail(FElysiumEntityWorld& World, const FElysiumEntity& Entity)
	{
		TSharedRef<FJsonObject> Out = EntitySummary(Entity);
		Out->SetStringField(TEXT("debug_string"), Entity.DebugString());
		Out->SetNumberField(TEXT("epoch"), static_cast<double>(Entity.Handle.Epoch));
		Out->SetNumberField(TEXT("next_think"), Entity.NextThink == ELYSIUM_NEVER_THINK ? -1.0 : Entity.NextThink);

		// Raw `.ents` keyvalues, verbatim.
		if (Entity.Def)
		{
			TSharedRef<FJsonObject> Keys = Obj();
			for (const TPair<FString, FString>& Pair : Entity.Def->Keys)
			{
				Keys->SetStringField(Pair.Key, Pair.Value);
			}
			Out->SetObjectField(TEXT("keyvalues"), Keys);

			// Outputs, with the runtime `times` countdown alongside the authored row.
			TArray<TSharedPtr<FJsonValue>> Outputs;
			for (int32 i = 0; i < Entity.Def->Outputs.Num(); ++i)
			{
				const FElysiumOutputDef& Def = Entity.Def->Outputs[i];
				TSharedRef<FJsonObject> Row = Obj();
				Row->SetStringField(TEXT("name"), Def.Name);
				Row->SetStringField(TEXT("target"), Def.Target);
				Row->SetStringField(TEXT("input"), Def.Input);
				Row->SetStringField(TEXT("param"), Def.Param);
				Row->SetNumberField(TEXT("delay"), Def.Delay);
				Row->SetNumberField(TEXT("times"), Def.Times);
				Row->SetStringField(TEXT("python"), Def.Python);
				Row->SetBoolField(TEXT("python_only"), Def.IsPythonOnly());
				Row->SetNumberField(TEXT("times_remaining"),
					Entity.OutputTimesRemaining.IsValidIndex(i) ? Entity.OutputTimesRemaining[i] : -1);
				Outputs.Add(MakeShared<FJsonValueObject>(Row));
			}
			Out->SetArrayField(TEXT("outputs"), Outputs);
		}

		// The class chain, plus its live fields and the inputs it accepts. Derived shadows base,
		// so the first descriptor to name a field or input wins.
		TArray<const FElysiumClassDesc*> Chain;
		ChainDescs(Entity.Class, Chain);

		TArray<TSharedPtr<FJsonValue>> ChainNames;
		TSharedRef<FJsonObject> Fields = Obj();
		TArray<TSharedPtr<FJsonValue>> Inputs;
		TSet<FName> SeenFields;
		TSet<FName> SeenInputs;

		for (const FElysiumClassDesc* Desc : Chain)
		{
			ChainNames.Add(MakeShared<FJsonValueString>(Desc->ClassName.ToString()));
			for (const TPair<FName, FElysiumFieldAccessor>& Pair : Desc->Fields)
			{
				if (SeenFields.Contains(Pair.Key) || !Pair.Value.Get)
				{
					continue;
				}
				SeenFields.Add(Pair.Key);
				const FElysiumVariant Value = Pair.Value.Get(Entity);
				TSharedRef<FJsonObject> Field = Obj();
				Field->SetStringField(TEXT("value"), Value.ToString());
				Field->SetStringField(TEXT("described"), Value.Describe());
				Field->SetBoolField(TEXT("keyable"), Pair.Value.bKeyable);
				Fields->SetObjectField(Pair.Key.ToString(), Field);
			}
			for (const TPair<FName, FElysiumInputThunk>& Pair : Desc->Inputs)
			{
				if (SeenInputs.Contains(Pair.Key))
				{
					continue;
				}
				SeenInputs.Add(Pair.Key);
				Inputs.Add(MakeShared<FJsonValueString>(Pair.Key.ToString()));
			}
		}
		Out->SetArrayField(TEXT("class_chain"), ChainNames);
		Out->SetObjectField(TEXT("fields"), Fields);
		Out->SetArrayField(TEXT("inputs"), Inputs);

		// Leaf-class runtime state the field tables do not carry (mover toggle state, resolved
		// links, spawnflag decode) — the same rows the Cog inspector's "Live state" section shows.
		TArray<TPair<FString, FString>> DebugState;
		Entity.GetDebugState(DebugState);
		if (DebugState.Num() > 0)
		{
			TSharedRef<FJsonObject> State = Obj();
			for (const TPair<FString, FString>& Pair : DebugState)
			{
				State->SetStringField(Pair.Key, Pair.Value);
			}
			Out->SetObjectField(TEXT("live_state"), State);
		}

		Out->SetBoolField(TEXT("usable"), Entity.IsUsable());
		Out->SetNumberField(TEXT("use_icon"), Entity.GetUseIcon());
		Out->SetBoolField(TEXT("aimed_by_player"), World.GetAimedUsable() == Entity.Handle);
		return Out;
	}

	// Resolve a target the way `elysium.ent_fire` does: targetname first, then classname. Both are
	// non-unique, so both fan out.
	void ResolveEntities(FElysiumEntityWorld& World, const FString& Target, TArray<FElysiumEntity*>& Out)
	{
		for (const TUniquePtr<FElysiumEntity>& Entity : World.Entities())
		{
			if (Entity && !Entity->IsDead() && Entity->TargetName.Equals(Target, ESearchCase::IgnoreCase))
			{
				Out.Add(Entity.Get());
			}
		}
		if (Out.Num() > 0)
		{
			return;
		}
		for (const TUniquePtr<FElysiumEntity>& Entity : World.Entities())
		{
			if (Entity && !Entity->IsDead() && Entity->Def
				&& Entity->Def->Classname.Equals(Target, ESearchCase::IgnoreCase))
			{
				Out.Add(Entity.Get());
			}
		}
	}

	// ---------------------------------------------------------------------------------------
	// Console-exec output capture
	// ---------------------------------------------------------------------------------------

	// GEngine->Exec writes a command's direct output to the Ar it is handed, but most `elysium.*`
	// verbs report through UE_LOG instead. Capturing both means collecting Ar AND the log tap's
	// delta across the call, then dropping the duplicates that a verb doing both would produce.
	struct FExecCapture final : FOutputDevice
	{
		TArray<FString> Lines;
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			Lines.Add(V);
		}
	};

	// ---------------------------------------------------------------------------------------
	// The tools
	// ---------------------------------------------------------------------------------------

	void BuildTools(TArray<TSharedRef<IModelContextProtocolTool>>& Out)
	{
		// --- Map lifecycle ------------------------------------------------------------------

		{
			FSchema Schema;
			Out.Add(MakeTool(TEXT("elysium_maps_list"),
				TEXT("List the VtMB maps the offline pipeline has exported (folders under $ELYSIUM_EXPORT_ROOT holding a <name>.obj), and which one is loaded right now. Call this first — every other map tool takes a name from here."),
				Schema,
				[](const TSharedPtr<FJsonObject>&) -> FModelContextProtocolToolResult
				{
					UElysiumMapSubsystem* Maps = Sub<UElysiumMapSubsystem>();
					if (!Maps)
					{
						return MakeErrorResult(TEXT("no game running (no UElysiumMapSubsystem)"));
					}
					TSharedRef<FJsonObject> Body = Obj();
					TArray<TSharedPtr<FJsonValue>> Names;
					for (const FString& Name : Maps->ExportedMaps())
					{
						Names.Add(MakeShared<FJsonValueString>(Name));
					}
					Body->SetArrayField(TEXT("maps"), Names);
					Body->SetStringField(TEXT("current"), Maps->GetCurrentMapName());
					Body->SetStringField(TEXT("pending_travel"), Maps->PendingTravelDesc());
					// The app state is what says whether the loaded map is a menu backdrop, a running
					// session or a held one — the same poll answers "has the travel landed" and "what
					// kind of world am I in" (11.3).
					if (const UElysiumGameFlowSubsystem* Flow = Sub<UElysiumGameFlowSubsystem>())
					{
						Body->SetStringField(TEXT("app_state"), ElysiumAppState::Name(Flow->AppState()));
					}
					if (AElysiumMapActor* Map = Maps->GetCurrentMap())
					{
						Body->SetStringField(TEXT("entry_landmark"), Map->EntryLandmark);
						Body->SetNumberField(TEXT("entity_count"), Map->EntityCount);
						Body->SetNumberField(TEXT("brush_body_count"), Map->BrushBodyCount);
						if (const UElysiumMapVisuals* Visuals = Map->GetVisuals())
						{
							Body->SetNumberField(TEXT("world_light_count"), Visuals->WorldLightCount);
							Body->SetNumberField(TEXT("prop_instance_count"), Visuals->PropInstanceCount);
						}
						Body->SetBoolField(TEXT("spawn_done"), Map->IsSpawnDone());
					}
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("map"), TEXT("string"), TEXT("Map name as listed by elysium_maps_list, e.g. sp_tutorial_1."), true)
				.Add(TEXT("landmark"), TEXT("string"), TEXT("Optional info_landmark targetname to enter at, instead of info_player_start. This is the P4.6 landmark-transition entry."));
			Out.Add(MakeTool(TEXT("elysium_map_load"),
				TEXT("Hard-travel to a map, replacing the current one; optionally enter at a named info_landmark. Does NOT seed story state — use elysium_new_game for the story entry. Deferred: when a map is already loaded the travel is an end-of-frame UE OpenLevel, so this returns pending=true BEFORE the new world exists — poll elysium_maps_list until pending_travel clears and spawn_done is true. Only a cold boot (no map loaded) builds in-call (pending=false, with entity_count)."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					UElysiumMapSubsystem* Maps = Sub<UElysiumMapSubsystem>();
					if (!Maps)
					{
						return MakeErrorResult(TEXT("no game running"));
					}
					const FString Map = ParamStr(Params, TEXT("map"));
					if (Map.IsEmpty())
					{
						return MakeErrorResult(TEXT("`map` is required"));
					}
					const FString Landmark = ParamStr(Params, TEXT("landmark"));
					const bool bOk = Maps->Travel(Map, Landmark);

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetBoolField(TEXT("ok"), bOk);
					AddTravelOutcome(Body, Maps);
					if (!bOk)
					{
						Body->SetStringField(TEXT("error"),
							FString::Printf(TEXT("map '%s' has no exported .obj under $ELYSIUM_EXPORT_ROOT"), *Map));
					}
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Out.Add(MakeTool(TEXT("elysium_map_reload"),
				TEXT("Re-travel the current map (the export->reload hot loop: re-run the offline exporter, then call this to pick up the new intermediates without restarting). Deferred hard travel — returns pending=true before the rebuilt world exists; poll elysium_maps_list until pending_travel clears and spawn_done is true."),
				Schema,
				[](const TSharedPtr<FJsonObject>&) -> FModelContextProtocolToolResult
				{
					UElysiumMapSubsystem* Maps = Sub<UElysiumMapSubsystem>();
					if (!Maps)
					{
						return MakeErrorResult(TEXT("no game running"));
					}
					TSharedRef<FJsonObject> Body = Obj();
					Body->SetBoolField(TEXT("ok"), Maps->Reload());
					AddTravelOutcome(Body, Maps);
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("clan"), TEXT("integer"), TEXT("Clan in the level-script 2..8 encoding (2 Brujah .. 8 Ventrue). Default 4 (Malkavian)."))
				.Add(TEXT("male"), TEXT("boolean"), TEXT("Player sex. Default false (female)."));
			Out.Add(MakeTool(TEXT("elysium_new_game"),
				TEXT("Seed a fresh story context (G flags, quest map, retail-reference player sheet) and travel to sp_tutorial_1 at its `tutorial` info_landmark. Defaults to the shared female Malkavian Completely Batshit developer preset. The story state is seeded synchronously (clan/clan_name are valid immediately), but the map travel is deferred when a map is already loaded — returns pending=true; poll elysium_maps_list until pending_travel clears and spawn_done is true."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					UElysiumMapSubsystem* Maps = Sub<UElysiumMapSubsystem>();
					UElysiumGameFlowSubsystem* Flow = Sub<UElysiumGameFlowSubsystem>();
					if (!Maps || !Flow)
					{
						return MakeErrorResult(TEXT("no game running"));
					}
					const int32 Clan = ParamInt(Params, TEXT("clan"),
						FElysiumSheet::ClanFromName(TEXT("Malkavian")));
					const bool bMale = ParamBool(Params, TEXT("male"), false);

					FElysiumNewGameRequest Request =
						ElysiumStory::MakeMockCharacterRequest(TEXT("tutorial"));
					Request.Clan = Clan;
					Request.bMale = bMale;
					if (Clan != FElysiumSheet::ClanFromName(TEXT("Malkavian")))
					{
						Request.HistoryId = INDEX_NONE;
						Request.Spends.Reset();
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetBoolField(TEXT("ok"), Flow->NewGame(Request));
					Body->SetStringField(TEXT("app_state"), ElysiumAppState::Name(Flow->AppState()));
					Body->SetNumberField(TEXT("clan"), Clan);
					Body->SetStringField(TEXT("clan_name"), FElysiumSheet::ClanName(Clan));
					AddTravelOutcome(Body, Maps);
					return Structured(Body);
				}));
		}

		// --- Player -------------------------------------------------------------------------

		{
			FSchema Schema;
			Out.Add(MakeTool(TEXT("elysium_player_get"),
				TEXT("Read the player's pose and state: world position (Unreal cm) and view rotation, noclip on/off, the legacy first/third-person camera state plus the modern camera director's resolved winner, request policy, pose, weight, and FOV, the settled locomotion sample, the frame's animation selection record (classified activity, resolved label, OWNING BANK, asset kind and the outcome or fallback reason), the current map, average FPS, and what the +use look-cursor is currently aimed at."),
				Schema,
				[](const TSharedPtr<FJsonObject>&) -> FModelContextProtocolToolResult
				{
					APawn* Pawn = LivePawn();
					if (!Pawn)
					{
						return MakeErrorResult(TEXT("no player pawn (no game running, or the map has not spawned one yet)"));
					}
					TSharedRef<FJsonObject> Body = Obj();
					Body->SetObjectField(TEXT("position"), Vec(Pawn->GetActorLocation()));

					FRotator View = Pawn->GetActorRotation();
					if (APlayerController* Controller = LivePlayerController())
					{
						View = Controller->GetControlRotation();
					}
					TSharedRef<FJsonObject> Rot = Obj();
					Rot->SetNumberField(TEXT("pitch"), View.Pitch);
					Rot->SetNumberField(TEXT("yaw"), View.Yaw);
					Rot->SetNumberField(TEXT("roll"), View.Roll);
					Body->SetObjectField(TEXT("rotation"), Rot);

					Body->SetObjectField(TEXT("velocity"), Vec(Pawn->GetVelocity()));
					if (const IElysiumPlayerBody* PlayerBody = Cast<IElysiumPlayerBody>(Pawn))
					{
						Body->SetBoolField(TEXT("noclip"), PlayerBody->IsNoclip());

						// Keep the original fields at camera.* for compatibility, and report the
						// modern local-player director separately at camera.director.*. During a
						// dialogue the legacy component can correctly remain in first person while
						// the director owns the final view.
						TSharedRef<FJsonObject> Camera = Obj();
						if (const UElysiumCameraComponent* Cam = PlayerBody->GetCameraComponent())
						{
							Camera->SetStringField(TEXT("mode"),
								Cam->IsThirdPerson() ? TEXT("third") : TEXT("first"));
							Camera->SetStringField(TEXT("driver"), Cam->GetWeights().Driver());
							Camera->SetNumberField(TEXT("weight"), Cam->ThirdPersonWeight());
							Camera->SetNumberField(TEXT("scripted_weight"), Cam->GetShots().GetWeight());
							Camera->SetNumberField(TEXT("boom_length"), Cam->BoomLength());
							Camera->SetNumberField(TEXT("model_alpha"), Cam->ModelAlpha());
							const FElysiumCameraShot* Shot = Cam->GetShots().Top();
							Camera->SetStringField(TEXT("shot"), Shot ? Shot->DebugName : FString());

						}

						// The modern rig solves every frame beside the faithful one, so an agent can
						// read the A/B delta without flipping the mode and taking a second sample.
						const APlayerController* CamPC = LivePlayerController();
						if (const AElysiumPlayerCameraManager* Manager = CamPC
								? Cast<AElysiumPlayerCameraManager>(CamPC->PlayerCameraManager)
								: nullptr)
						{
							const FElysiumCameraSample& S = Manager->GetCameraSample();
							Camera->SetNumberField(TEXT("boom_length"), S.BoomLength);
							Camera->SetBoolField(TEXT("clipped"), S.bClipped);
						}

						const ULocalPlayer* LocalPlayer = CamPC ? CamPC->GetLocalPlayer() : nullptr;
						const UElysiumCameraService* CameraService = LocalPlayer
							? LocalPlayer->GetSubsystem<UElysiumCameraService>()
							: nullptr;
						if (CameraService)
						{
							const FElysiumResolvedCameraState& Resolved =
								CameraService->ResolvedCamera();
							const FElysiumCameraRequest& Request = Resolved.Request;
							TSharedRef<FJsonObject> Director = Obj();
							Director->SetBoolField(TEXT("active"), Resolved.bActive);
							Director->SetNumberField(TEXT("epoch"),
								static_cast<double>(CameraService->CurrentEpoch()));
							Director->SetStringField(TEXT("kind"), CameraRequestKindName(Request.Kind));
							Director->SetStringField(TEXT("winner"), Request.DebugName);
							Director->SetStringField(TEXT("owner"), Request.Owner);
							Director->SetNumberField(TEXT("priority"), Request.Priority);
							Director->SetNumberField(TEXT("weight"), Resolved.Weight);
							Director->SetBoolField(TEXT("override_pose"), Request.bOverridePose);
							Director->SetObjectField(TEXT("position"), Vec(Resolved.Location));
							TSharedRef<FJsonObject> DirectorRotation = Obj();
							DirectorRotation->SetNumberField(TEXT("pitch"), Resolved.Rotation.Pitch);
							DirectorRotation->SetNumberField(TEXT("yaw"), Resolved.Rotation.Yaw);
							DirectorRotation->SetNumberField(TEXT("roll"), Resolved.Rotation.Roll);
							Director->SetObjectField(TEXT("rotation"), DirectorRotation);
							Director->SetNumberField(TEXT("fov"), Resolved.FieldOfView);
							Director->SetStringField(TEXT("control"), CameraControlName(Request.Control));
							Director->SetBoolField(TEXT("show_hud"), Request.bShowHud);
							Director->SetBoolField(TEXT("draw_viewmodel"), Request.bDrawViewmodel);
							Director->SetBoolField(TEXT("show_player_body"), Request.bShowPlayerBody);
							Director->SetBoolField(TEXT("dialog_pov"), Request.bDialogPOV);
							Director->SetStringField(TEXT("source_shot"), Request.SourceShot);
							Director->SetStringField(TEXT("profile"), Request.SelectedProfile);
							Director->SetStringField(TEXT("fallback"),
								CameraFallbackName(Request.Fallback));
							Director->SetStringField(TEXT("fallback_reason"), Request.FallbackReason);
							Director->SetStringField(TEXT("candidate_rejections"),
								Resolved.CandidateRejections);

							TSharedRef<FJsonObject> Handle = Obj();
							Handle->SetNumberField(TEXT("slot"), Resolved.Handle.Slot);
							Handle->SetNumberField(TEXT("generation"), Resolved.Handle.Generation);
							Handle->SetNumberField(TEXT("epoch"),
								static_cast<double>(Resolved.Handle.Epoch));
							Director->SetObjectField(TEXT("handle"), Handle);

							TArray<FString> RequestDescriptions;
							CameraService->DescribeRequests(RequestDescriptions);
							TArray<TSharedPtr<FJsonValue>> Requests;
							Requests.Reserve(RequestDescriptions.Num());
							for (const FString& Description : RequestDescriptions)
							{
								Requests.Add(MakeShared<FJsonValueString>(Description));
							}
							Director->SetNumberField(TEXT("request_count"), Requests.Num());
							Director->SetArrayField(TEXT("requests"), Requests);
							Camera->SetObjectField(TEXT("director"), Director);
						}
						Body->SetObjectField(TEXT("camera"), Camera);
					}
					// CCC1/CCC4 — the body sample and the selection it produced. Together they are the
					// whole diagnosis of a wrong pose: what the body was doing, what activity that
					// classified to, which label and OWNING BANK it resolved through, and why if it
					// did not. An agent can drive `+forward` and check the bank without a screenshot.
					if (const IElysiumPlayerBody* PlayerBody = Cast<IElysiumPlayerBody>(Pawn))
					{
						const FElysiumLocomotionSample S = PlayerBody->GetLocomotionSample();
						TSharedRef<FJsonObject> Loco = Obj();
						Loco->SetNumberField(TEXT("speed2d"), S.Speed2D());
						Loco->SetObjectField(TEXT("local_velocity"), Vec(S.LocalVelocity));
						Loco->SetNumberField(TEXT("facing_yaw"), S.FacingYaw);
						Loco->SetNumberField(TEXT("move_yaw_wish"), S.MoveYawWish);
						Loco->SetNumberField(TEXT("move_yaw_vel"), S.MoveYawVelocity);
						// Zero means the wish yaw beside it is a placeholder, not a measurement.
						Loco->SetNumberField(TEXT("wish_scale"), S.WishScale);
						Loco->SetBoolField(TEXT("on_ground"), S.bOnGround);
						Loco->SetNumberField(TEXT("water"), static_cast<int32>(S.Water));
						Loco->SetStringField(TEXT("stance"), StanceName(S.Stance));
						Loco->SetStringField(TEXT("jump_phase"), JumpPhaseName(S.JumpPhase()));
						Loco->SetNumberField(TEXT("jump_hold"), S.JumpHoldRemaining);
						Body->SetObjectField(TEXT("locomotion"), Loco);
					}
					if (const AElysiumMapActor* Map = MapActor())
					{
						const FElysiumAnimationSelection& Sel = Map->GetPlayerAnimSelection();
						TSharedRef<FJsonObject> Anim = Obj();
						Anim->SetStringField(TEXT("stem"), Sel.Stem);
						Anim->SetStringField(TEXT("source"), ElysiumAnimIntent::SourceName(Sel.Source));
						Anim->SetStringField(TEXT("channel"), ElysiumAnimIntent::ChannelName(Sel.Channel));
						Anim->SetStringField(TEXT("route"), ElysiumAnimIntent::RouteName(Sel.Route));
						Anim->SetNumberField(TEXT("generation"), Sel.Generation);
						Anim->SetStringField(TEXT("requested_activity"), Sel.RequestedActivity);
						Anim->SetStringField(TEXT("resolved_activity"), Sel.ResolvedActivity);
						Anim->SetStringField(TEXT("first_weapon_activity"), Sel.FirstWeaponActivity);
						Anim->SetStringField(TEXT("weapon_activity"), Sel.WeaponActivity);
						Anim->SetNumberField(TEXT("translation_iterations"), Sel.TranslationIterations);
						Anim->SetStringField(TEXT("label"), Sel.SequenceLabel);
						Anim->SetStringField(TEXT("owner"), Sel.OwnerStem);
						Anim->SetNumberField(TEXT("raw_index"), Sel.RawSequenceIndex);
						Anim->SetNumberField(TEXT("variant"), Sel.Variant);
						Anim->SetNumberField(TEXT("weight"), Sel.Weight);
						Anim->SetNumberField(TEXT("candidates"), Sel.Candidates);
						Anim->SetStringField(TEXT("asset"),
							ElysiumAnimIntent::AssetKindName(Sel.AssetKind));
						Anim->SetStringField(TEXT("animation"), Sel.AnimationName);
						Anim->SetBoolField(TEXT("looping"), Sel.bLooping);
						Anim->SetBoolField(TEXT("snap"), Sel.bSnap);
						Anim->SetBoolField(TEXT("additive"), Sel.bAdditive);
						Anim->SetNumberField(TEXT("fade"), Sel.FadeSeconds);
						Anim->SetNumberField(TEXT("ground_speed"), Sel.GroundSpeedCmPerSecond);
						Anim->SetNumberField(TEXT("move_yaw"), Sel.MoveYaw);
						TArray<TSharedPtr<FJsonValue>> Axes;
						for (int32 Axis = 0; Axis < Sel.Axes; ++Axis)
						{
							TSharedRef<FJsonObject> One = Obj();
							One->SetStringField(TEXT("name"), Sel.AxisName[Axis]);
							One->SetNumberField(TEXT("value"), Sel.AxisValue[Axis]);
							Axes.Add(MakeShared<FJsonValueObject>(One));
						}
						Anim->SetArrayField(TEXT("axes"), Axes);
						TArray<TSharedPtr<FJsonValue>> Layers;
						for (const FString& Layer : Sel.LayerLabels)
						{
							Layers.Add(MakeShared<FJsonValueString>(Layer));
						}
						Anim->SetArrayField(TEXT("layers"), Layers);
						Anim->SetStringField(TEXT("outcome"),
							ElysiumAnimIntent::OutcomeName(Sel.Outcome));
						Anim->SetStringField(TEXT("detail"), Sel.Detail);
						Body->SetObjectField(TEXT("animation"), Anim);
					}

					if (UElysiumMapSubsystem* Maps = Sub<UElysiumMapSubsystem>())
					{
						Body->SetStringField(TEXT("map"), Maps->GetCurrentMapName());
					}
					Body->SetNumberField(TEXT("fps"), GAverageFPS);

					if (FElysiumEntityWorld* World = Entities())
					{
						const FElysiumEntityHandle Aimed = World->GetAimedUsable();
						const FElysiumInteractionView Interaction = World->GetInteractionView();
						Body->SetNumberField(TEXT("use_icon"), Interaction.Icon);
						Body->SetNumberField(TEXT("use_prompt_alpha"), Interaction.PromptAlpha);
						Body->SetBoolField(TEXT("use_actionable"), Interaction.bActionable);
						Body->SetStringField(TEXT("aimed_usable"),
							Aimed.IsSet() ? World->DescribeHandle(Aimed) : FString());
						Body->SetNumberField(TEXT("game_time"), World->NowSeconds());

						// 11.4 — the pawn above is the body; the state is the entity's.
						if (const FElysiumPlayer* Player = World->FindPlayer())
						{
							TSharedRef<FJsonObject> Ent = Obj();
							Ent->SetStringField(TEXT("handle"), World->DescribeHandle(Player->Handle));
							Ent->SetNumberField(TEXT("health"), Player->Health);
							Ent->SetNumberField(TEXT("max_health"), Player->MaxHealth);
							Ent->SetBoolField(TEXT("unkillable"), Player->IsUnkillable());
							Ent->SetNumberField(TEXT("money"), Player->Money);
							const FElysiumSheet& S = Player->Sheet;
							using EC = EElysiumTraitContainer;
							Ent->SetNumberField(TEXT("blood"), S.GetCurrent(EC::Attributes, ElysiumSlot::BloodPool));
							Ent->SetNumberField(TEXT("humanity"), S.GetCurrent(EC::Attributes, ElysiumSlot::Humanity));
							Ent->SetNumberField(TEXT("masquerade"), S.GetCurrent(EC::Attributes, ElysiumSlot::Masquerade));
							Body->SetObjectField(TEXT("entity"), Ent);
						}
					}
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("landmark"), TEXT("string"), TEXT("info_landmark targetname to teleport to. Takes precedence over x/y/z."))
				.Add(TEXT("entity"), TEXT("string"), TEXT("Entity targetname or classname to teleport to (its def origin). Used when `landmark` is absent."))
				.Add(TEXT("x"), TEXT("number"), TEXT("World X in Unreal centimetres."))
				.Add(TEXT("y"), TEXT("number"), TEXT("World Y in Unreal centimetres."))
				.Add(TEXT("z"), TEXT("number"), TEXT("World Z in Unreal centimetres."))
				.Add(TEXT("yaw"), TEXT("number"), TEXT("Optional view yaw to face after the move."))
				.Add(TEXT("pitch"), TEXT("number"), TEXT("Optional view pitch after the move; positive looks up, -90..90."));
			Out.Add(MakeTool(TEXT("elysium_player_teleport"),
				TEXT("Move the player. Target by info_landmark name, by entity targetname/classname, or by explicit world coordinates. Teleports through physics, so the pawn keeps its collision — enable noclip first if the destination is inside geometry."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					APawn* Pawn = LivePawn();
					if (!Pawn)
					{
						return MakeErrorResult(TEXT("no player pawn"));
					}

					FVector Destination = Pawn->GetActorLocation();
					FString Resolved;

					const FString Landmark = ParamStr(Params, TEXT("landmark"));
					const FString EntityName = ParamStr(Params, TEXT("entity"));
					FElysiumEntityWorld* World = Entities();

					if (!Landmark.IsEmpty())
					{
						if (!World)
						{
							return MakeErrorResult(TEXT("no entity world loaded"));
						}
						const FElysiumEntity* Found = World->FindLandmark(Landmark);
						if (!Found || !Found->Def)
						{
							return MakeErrorResult(FString::Printf(TEXT("no info_landmark named '%s'"), *Landmark));
						}
						Destination = Found->Def->Origin;
						Resolved = FString::Printf(TEXT("landmark '%s'"), *Landmark);
					}
					else if (!EntityName.IsEmpty())
					{
						if (!World)
						{
							return MakeErrorResult(TEXT("no entity world loaded"));
						}
						TArray<FElysiumEntity*> Matches;
						ResolveEntities(*World, EntityName, Matches);
						if (Matches.Num() == 0 || !Matches[0]->Def)
						{
							return MakeErrorResult(FString::Printf(TEXT("no entity matching '%s'"), *EntityName));
						}
						Destination = Matches[0]->Def->Origin;
						Resolved = Matches[0]->DebugString();
					}
					else if (HasParam(Params, TEXT("x")) || HasParam(Params, TEXT("y")) || HasParam(Params, TEXT("z")))
					{
						Destination = FVector(
							ParamNum(Params, TEXT("x"), Destination.X),
							ParamNum(Params, TEXT("y"), Destination.Y),
							ParamNum(Params, TEXT("z"), Destination.Z));
						Resolved = TEXT("explicit coordinates");
					}
					else
					{
						return MakeErrorResult(TEXT("one of `landmark`, `entity`, or x/y/z is required"));
					}

					const bool bMoved = Pawn->TeleportTo(Destination, Pawn->GetActorRotation(),
						/*bIsATest*/ false, /*bNoCheck*/ true);

					if (HasParam(Params, TEXT("yaw")) || HasParam(Params, TEXT("pitch")))
					{
						if (APlayerController* Controller = LivePlayerController())
						{
							FRotator View = Controller->GetControlRotation();
							View.Yaw = ParamNum(Params, TEXT("yaw"), View.Yaw);
							// Straight up/down is what a sky check needs, so clamp to the pole rather
							// than letting a wrapped pitch roll the view over.
							View.Pitch = FMath::Clamp(ParamNum(Params, TEXT("pitch"), View.Pitch), -90.0, 90.0);
							Controller->SetControlRotation(View);
						}
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetBoolField(TEXT("ok"), bMoved);
					Body->SetStringField(TEXT("resolved"), Resolved);
					Body->SetObjectField(TEXT("requested"), Vec(Destination));
					Body->SetObjectField(TEXT("position"), Vec(Pawn->GetActorLocation()));
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("enabled"), TEXT("boolean"), TEXT("True to fly through geometry, false to walk. Omit to toggle."));
			Out.Add(MakeTool(TEXT("elysium_player_noclip"),
				TEXT("Turn noclip on or off (fly through geometry with collision disabled). Needed before teleporting into a sealed room, and to reach a vantage the walkable surface does not."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					IElysiumPlayerBody* PlayerBody = Cast<IElysiumPlayerBody>(LivePawn());
					if (!PlayerBody)
					{
						return MakeErrorResult(TEXT("no Elysium player body"));
					}
					const bool bEnable = HasParam(Params, TEXT("enabled"))
						? ParamBool(Params, TEXT("enabled"), true)
						: !PlayerBody->IsNoclip();
					PlayerBody->SetNoclip(bEnable);

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetBoolField(TEXT("noclip"), PlayerBody->IsNoclip());
					return Structured(Body);
				}));
		}

		// --- Entities -----------------------------------------------------------------------

		{
			FSchema Schema;
			Schema.Add(TEXT("name"), TEXT("string"), TEXT("Substring-match on targetname (case-insensitive)."))
				.Add(TEXT("classname"), TEXT("string"), TEXT("Substring-match on classname (case-insensitive)."))
				.Add(TEXT("live_only"), TEXT("boolean"), TEXT("Drop dead and hidden records. Default false."))
				.Add(TEXT("registered_only"), TEXT("boolean"), TEXT("Drop inert records (classnames with no leaf class registered). Default false."))
				.Add(TEXT("limit"), TEXT("integer"), TEXT("Max entities to return. Default 100."))
				.Add(TEXT("offset"), TEXT("integer"), TEXT("Skip this many matches first. Default 0."));
			Out.Add(MakeTool(TEXT("elysium_entity_list"),
				TEXT("Browse the current map's entity substrate. A map holds ~1,200 records including inert ones (classnames with no leaf class registered), so filter and page rather than dumping everything. Returns a class histogram alongside the matches."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					FElysiumEntityWorld* World = Entities();
					if (!World)
					{
						return MakeErrorResult(TEXT("no entity world loaded (no map, or the map has no .ents sidecar)"));
					}

					const FString NameFilter = ParamStr(Params, TEXT("name"));
					const FString ClassFilter = ParamStr(Params, TEXT("classname"));
					const bool bLiveOnly = ParamBool(Params, TEXT("live_only"), false);
					const bool bRegisteredOnly = ParamBool(Params, TEXT("registered_only"), false);
					const int32 Limit = FMath::Clamp(ParamInt(Params, TEXT("limit"), 100), 1, 2000);
					const int32 Offset = FMath::Max(0, ParamInt(Params, TEXT("offset"), 0));

					TArray<TSharedPtr<FJsonValue>> Matches;
					TMap<FString, int32> Histogram;
					int32 Total = 0;

					for (const TUniquePtr<FElysiumEntity>& Entity : World->Entities())
					{
						if (!Entity)
						{
							continue;
						}
						if (bLiveOnly && Entity->IsInert())
						{
							continue;
						}
						if (bRegisteredOnly && Entity->IsRecordOnly())
						{
							continue;
						}
						if (!NameFilter.IsEmpty() && !Entity->TargetName.Contains(NameFilter, ESearchCase::IgnoreCase))
						{
							continue;
						}
						const FString Classname = Entity->Def ? Entity->Def->Classname : FString();
						if (!ClassFilter.IsEmpty() && !Classname.Contains(ClassFilter, ESearchCase::IgnoreCase))
						{
							continue;
						}

						Histogram.FindOrAdd(Classname)++;
						if (Total >= Offset && Matches.Num() < Limit)
						{
							Matches.Add(MakeShared<FJsonValueObject>(EntitySummary(*Entity)));
						}
						++Total;
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetNumberField(TEXT("total_matched"), Total);
					Body->SetNumberField(TEXT("world_total"), World->NumEntities());
					Body->SetNumberField(TEXT("offset"), Offset);
					Body->SetArrayField(TEXT("entities"), Matches);

					TSharedRef<FJsonObject> Classes = Obj();
					for (const TPair<FString, int32>& Pair : Histogram)
					{
						Classes->SetNumberField(Pair.Key, Pair.Value);
					}
					Body->SetObjectField(TEXT("class_histogram"), Classes);
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("name"), TEXT("string"), TEXT("Exact targetname, or a classname (fans out to every match)."))
				.Add(TEXT("index"), TEXT("integer"), TEXT("Entity index (its position in the parsed .ents array). Takes precedence over `name`."))
				.Add(TEXT("limit"), TEXT("integer"), TEXT("Max entities to detail when `name` matches several. Default 10."));
			Out.Add(MakeTool(TEXT("elysium_entity_get"),
				TEXT("Full detail for one or more entities: raw .ents keyvalues, the resolved class chain with every live field value, the inputs the class accepts, all 7-field outputs with their remaining `times` counts, and leaf-class runtime state. This is the read half of a QA loop — pair it with elysium_entity_fire."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					FElysiumEntityWorld* World = Entities();
					if (!World)
					{
						return MakeErrorResult(TEXT("no entity world loaded"));
					}

					TArray<FElysiumEntity*> Found;
					if (HasParam(Params, TEXT("index")))
					{
						const int32 Index = ParamInt(Params, TEXT("index"), INDEX_NONE);
						const TArray<TUniquePtr<FElysiumEntity>>& All = World->Entities();
						if (!All.IsValidIndex(Index) || !All[Index])
						{
							return MakeErrorResult(FString::Printf(TEXT("no entity at index %d"), Index));
						}
						Found.Add(All[Index].Get());
					}
					else
					{
						const FString Name = ParamStr(Params, TEXT("name"));
						if (Name.IsEmpty())
						{
							return MakeErrorResult(TEXT("one of `name` or `index` is required"));
						}
						ResolveEntities(*World, Name, Found);
						if (Found.Num() == 0)
						{
							return MakeErrorResult(FString::Printf(
								TEXT("no live entity with targetname or classname '%s'"), *Name));
						}
					}

					const int32 Limit = FMath::Clamp(ParamInt(Params, TEXT("limit"), 10), 1, 100);
					TArray<TSharedPtr<FJsonValue>> Details;
					for (int32 i = 0; i < Found.Num() && i < Limit; ++i)
					{
						Details.Add(MakeShared<FJsonValueObject>(EntityDetail(*World, *Found[i])));
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetNumberField(TEXT("matched"), Found.Num());
					Body->SetArrayField(TEXT("entities"), Details);
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("target"), TEXT("string"), TEXT("Targetname, or a classname to fan out over."), true)
				.Add(TEXT("input"), TEXT("string"), TEXT("Input name, e.g. Open / Toggle / Trigger / Kill. Case-folds. Call elysium_entity_get first to see what the class accepts."), true)
				.Add(TEXT("param"), TEXT("string"), TEXT("Optional input parameter. A JSON number or boolean is marshalled as Int/Float/Bool; a string stays a string."))
				.Add(TEXT("delay"), TEXT("number"), TEXT("Seconds to defer the delivery. Default 0."));
			Out.Add(MakeTool(TEXT("elysium_entity_fire"),
				TEXT("Fire an input at an entity through the real event queue — the same code path a map's own I/O takes, so it is loggable, pausable, and single-steppable. This is how you drive a QA scenario: press a button, open a door, trip a trigger."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					FElysiumEntityWorld* World = Entities();
					if (!World)
					{
						return MakeErrorResult(TEXT("no entity world loaded"));
					}
					const FString Target = ParamStr(Params, TEXT("target"));
					const FString Input = ParamStr(Params, TEXT("input"));
					if (Target.IsEmpty() || Input.IsEmpty())
					{
						return MakeErrorResult(TEXT("`target` and `input` are both required"));
					}

					TArray<FElysiumEntity*> Found;
					ResolveEntities(*World, Target, Found);
					if (Found.Num() == 0)
					{
						return MakeErrorResult(FString::Printf(
							TEXT("no live entity with targetname or classname '%s'"), *Target));
					}

					const FElysiumVariant Param = ParamVariant(Params, TEXT("param"));
					const double Delay = ParamNum(Params, TEXT("delay"), 0.0);
					const FName InputName(*Input);

					// One enqueue per resolved entity, addressed as `!self` with that entity as the
					// caller. That is exactly how ent_fire reaches a specific instance when the
					// targetname is shared (targetnames are non-unique).
					TArray<TSharedPtr<FJsonValue>> Fired;
					for (FElysiumEntity* Entity : Found)
					{
						World->EnqueueInput(TEXT("!self"), InputName, Param, Delay,
							FElysiumEntityHandle::Invalid(), Entity->Handle);
						Fired.Add(MakeShared<FJsonValueString>(Entity->DebugString()));
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetNumberField(TEXT("queued"), Fired.Num());
					Body->SetArrayField(TEXT("targets"), Fired);
					Body->SetStringField(TEXT("input"), Input);
					Body->SetStringField(TEXT("param"), Param.Describe());
					Body->SetNumberField(TEXT("delay"), Delay);
					Body->SetBoolField(TEXT("queue_paused"), World->Queue().IsPaused());
					if (World->Queue().IsPaused())
					{
						Body->SetStringField(TEXT("note"),
							TEXT("the event queue is paused — call elysium_queue_step or elysium_queue_pause{paused:false} to deliver this"));
					}
					return Structured(Body);
				}));
		}

		// --- Event queue --------------------------------------------------------------------

		{
			FSchema Schema;
			Schema.Add(TEXT("limit"), TEXT("integer"), TEXT("Max pending events to return. Default 50."));
			Out.Add(MakeTool(TEXT("elysium_queue_get"),
				TEXT("Read the entity world's one time-sorted event queue: every pending I/O delivery and deferred Python payload with its fire time, plus the pause/step state. Everything deferred in the game is here — there are no engine timers."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					FElysiumEntityWorld* World = Entities();
					if (!World)
					{
						return MakeErrorResult(TEXT("no entity world loaded"));
					}
					const int32 Limit = FMath::Clamp(ParamInt(Params, TEXT("limit"), 50), 1, 500);
					const double Now = World->NowSeconds();
					const FElysiumEventQueue& Queue = World->Queue();

					TArray<TSharedPtr<FJsonValue>> Events;
					const TArray<FElysiumIOEvent>& Pending = Queue.Pending();
					for (int32 i = 0; i < Pending.Num() && i < Limit; ++i)
					{
						const FElysiumIOEvent& Event = Pending[i];
						TSharedRef<FJsonObject> Row = Obj();
						Row->SetNumberField(TEXT("fire_time"), Event.FireTime);
						Row->SetNumberField(TEXT("in_seconds"), Event.FireTime - Now);
						Row->SetStringField(TEXT("target"), Event.Target);
						Row->SetStringField(TEXT("input"), Event.Input.ToString());
						Row->SetStringField(TEXT("param"), Event.Param.Describe());
						Row->SetStringField(TEXT("python"), Event.PythonSrc);
						Row->SetStringField(TEXT("caller"), World->DescribeHandle(Event.Caller));
						Row->SetStringField(TEXT("activator"), World->DescribeHandle(Event.Activator));
						Row->SetNumberField(TEXT("serial"), static_cast<double>(Event.Serial));
						Events.Add(MakeShared<FJsonValueObject>(Row));
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetNumberField(TEXT("now"), Now);
					Body->SetNumberField(TEXT("pending"), Queue.Num());
					Body->SetBoolField(TEXT("paused"), Queue.IsPaused());
					Body->SetNumberField(TEXT("steps_pending"), Queue.StepsPending());
					Body->SetNumberField(TEXT("unknown_targets"), World->UnknownTargets());
					Body->SetNumberField(TEXT("unknown_inputs"), World->UnknownInputs());
					Body->SetArrayField(TEXT("events"), Events);
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("paused"), TEXT("boolean"), TEXT("True to freeze the queue, false to resume. Omit to toggle."));
			Out.Add(MakeTool(TEXT("elysium_queue_pause"),
				TEXT("Freeze or resume the event queue. Frozen, no queued I/O is delivered and the debug overlays stop fading, so the evidence stays put — the single-stepper for causality bugs. The world keeps rendering."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					FElysiumEntityWorld* World = Entities();
					if (!World)
					{
						return MakeErrorResult(TEXT("no entity world loaded"));
					}
					FElysiumEventQueue& Queue = World->Queue();
					const bool bPause = HasParam(Params, TEXT("paused"))
						? ParamBool(Params, TEXT("paused"), true)
						: !Queue.IsPaused();
					if (bPause)
					{
						Queue.Pause();
					}
					else
					{
						Queue.Resume();
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetBoolField(TEXT("paused"), Queue.IsPaused());
					Body->SetNumberField(TEXT("pending"), Queue.Num());
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("count"), TEXT("integer"), TEXT("How many queued events to release. Default 1."));
			Out.Add(MakeTool(TEXT("elysium_queue_step"),
				TEXT("Release N queued events while the queue is paused, one delivery at a time. Read the result with elysium_io_history to see exactly what each step did."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					FElysiumEntityWorld* World = Entities();
					if (!World)
					{
						return MakeErrorResult(TEXT("no entity world loaded"));
					}
					FElysiumEventQueue& Queue = World->Queue();
					const int32 Count = FMath::Clamp(ParamInt(Params, TEXT("count"), 1), 1, 1000);
					if (!Queue.IsPaused())
					{
						// Stepping an unpaused queue is meaningless — it drains on its own. Pause
						// first so the step count means what the caller intended.
						Queue.Pause();
					}
					Queue.RequestSteps(Count);

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetNumberField(TEXT("requested"), Count);
					Body->SetNumberField(TEXT("steps_pending"), Queue.StepsPending());
					Body->SetNumberField(TEXT("queue_pending"), Queue.Num());
					Body->SetBoolField(TEXT("paused"), Queue.IsPaused());
					Body->SetStringField(TEXT("note"),
						TEXT("steps are consumed on the next world ticks; read elysium_io_history for what they delivered"));
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("limit"), TEXT("integer"), TEXT("How many of the most recent lines to return. Default 50, ring holds 1000."));
			Out.Add(MakeTool(TEXT("elysium_io_history"),
				TEXT("The always-on I/O history ring: every delivered input, dead wire, unknown input, and Python payload in causal order. Recorded whether or not anyone was watching — this is the postmortem when something fired and you missed it."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					FElysiumEntityWorld* World = Entities();
					if (!World)
					{
						return MakeErrorResult(TEXT("no entity world loaded"));
					}
					const int32 Limit = FMath::Clamp(ParamInt(Params, TEXT("limit"), 50), 1, 1000);
					const FElysiumRingBufferSink& Ring = World->RingBuffer();

					TArray<FString> Lines;
					Ring.CollectOrdered(Limit, Lines);

					TArray<TSharedPtr<FJsonValue>> Values;
					for (const FString& Line : Lines)
					{
						Values.Add(MakeShared<FJsonValueString>(Line));
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetNumberField(TEXT("recorded"), Ring.Num());
					Body->SetNumberField(TEXT("capacity"), Ring.Capacity());
					Body->SetArrayField(TEXT("lines"), Values);
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("outcome"), TEXT("string"), TEXT("Return only wires with this outcome: never_fired, exhausted, unknown_target, unknown_input, delivered, python_only, pending. Omit for all of them."));
			Schema.Add(TEXT("limit"), TEXT("integer"), TEXT("Maximum wire rows to return; `matched` still counts the whole set. Default 100."));
			Schema.Add(TEXT("write"), TEXT("boolean"), TEXT("Also dump the full report to _wires/<map>.json for the offline joiner. Default false."));
			Out.Add(MakeTool(TEXT("elysium_wire_report"),
				TEXT("Per-wire I/O accounting for the live map: every authored output row and what it did — fired, refused because `times` was spent, delivered, dead target, unresolved input, Python forwarded. The summary answers 'is the event surface working?' as a number; filtering by outcome gives the work list, and never_fired is the one that matters most."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					FElysiumEntityWorld* World = Entities();
					if (!World)
					{
						return MakeErrorResult(TEXT("no entity world loaded"));
					}

					TArray<FElysiumWireReportRow> Rows;
					World->BuildWireReport(Rows);
					const FElysiumWireSummary S = ElysiumWireSummarize(Rows);

					TSharedRef<FJsonObject> Summary = Obj();
					Summary->SetNumberField(TEXT("authored"), S.Authored);
					Summary->SetNumberField(TEXT("runtime_rows"), S.RuntimeRows);
					Summary->SetNumberField(TEXT("fired"), S.Fired);
					Summary->SetNumberField(TEXT("fully_delivered"), S.FullyDelivered);
					Summary->SetNumberField(TEXT("never_fired"), S.NeverFired);
					Summary->SetNumberField(TEXT("exhausted"), S.Exhausted);
					Summary->SetNumberField(TEXT("unknown_target"), S.UnknownTarget);
					Summary->SetNumberField(TEXT("unknown_input"), S.UnknownInput);
					Summary->SetNumberField(TEXT("pending"), S.Pending);
					Summary->SetNumberField(TEXT("python_rows"), S.PythonRows);
					Summary->SetNumberField(TEXT("python_forwarded"), S.PythonForwarded);

					const FString Filter = ParamStr(Params, TEXT("outcome"));
					const int32 Limit = FMath::Clamp(ParamInt(Params, TEXT("limit"), 100), 1, 5000);

					TArray<TSharedPtr<FJsonValue>> Wires;
					int32 Matched = 0;
					for (const FElysiumWireReportRow& R : Rows)
					{
						const TCHAR* Outcome = ElysiumWireOutcomeName(ElysiumWireOutcome(R));
						if (Filter.IsEmpty() || FCString::Stricmp(*Filter, Outcome) == 0)
						{
							++Matched;
							// `matched` keeps counting past the cap, so a truncated answer still
							// states how big the work list actually is.
							if (Wires.Num() >= Limit)
							{
								continue;
							}
							TSharedRef<FJsonObject> Row = Obj();
							Row->SetNumberField(TEXT("entity"), R.Wire.SourceIndex);
							Row->SetStringField(TEXT("name"), R.SourceName);
							Row->SetStringField(TEXT("classname"), R.SourceClass);
							Row->SetStringField(TEXT("output"), R.Wire.Output.ToString());
							Row->SetNumberField(TEXT("row"), R.Wire.Row);
							Row->SetNumberField(TEXT("output_row"), R.OutputRow);
							Row->SetStringField(TEXT("target"), R.Target);
							Row->SetStringField(TEXT("input"), R.Input);
							Row->SetStringField(TEXT("param"), R.Param);
							Row->SetNumberField(TEXT("delay"), R.Delay);
							Row->SetNumberField(TEXT("times"), R.AuthoredTimes);
							Row->SetBoolField(TEXT("python"), R.HasPython());
							Row->SetBoolField(TEXT("runtime_source"), R.bRuntimeSource);
							Row->SetStringField(TEXT("outcome"), Outcome);
							Row->SetNumberField(TEXT("fired"), R.Tally.Fired);
							Row->SetNumberField(TEXT("times_exhausted"), R.Tally.TimesExhausted);
							Row->SetNumberField(TEXT("delivered"), R.Tally.Delivered);
							Row->SetNumberField(TEXT("unknown_target"), R.Tally.UnknownTarget);
							Row->SetNumberField(TEXT("unknown_input"), R.Tally.UnknownInput);
							Row->SetNumberField(TEXT("python_forwarded"), R.Tally.PythonForwarded);
							Wires.Add(MakeShared<FJsonValueObject>(Row));
						}
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetStringField(TEXT("map"), World->MapName());
					Body->SetObjectField(TEXT("summary"), Summary);
					Body->SetStringField(TEXT("outcome_filter"), Filter);
					Body->SetNumberField(TEXT("matched"), Matched);
					Body->SetArrayField(TEXT("wires"), Wires);
					if (ParamBool(Params, TEXT("write"), false))
					{
						const FString Path = ElysiumWireDump::Write(*World);
						Body->SetStringField(TEXT("report_path"), Path);
						Body->SetBoolField(TEXT("written"), !Path.IsEmpty());
					}
					return Structured(Body);
				}));
		}

		// --- Scripting ----------------------------------------------------------------------

		{
			FSchema Schema;
			Schema.Add(TEXT("source"), TEXT("string"), TEXT("Python source. Expression or `;`-separated simple statements; evaluates in __main__, exactly where a field-6 payload does."), true);
			Out.Add(MakeTool(TEXT("elysium_script_eval"),
				TEXT("Evaluate a script string through the installed host (embedded CPython 2.7 by default, the expression evaluator as fallback), against the live entity world and G store. Resolves everything a field-6 payload resolves, including the loaded level script's names. Total: an error yields Void, never a throw."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					UElysiumGameStateSubsystem* State = Sub<UElysiumGameStateSubsystem>();
					if (!State)
					{
						return MakeErrorResult(TEXT("no game running"));
					}
					const FString Source = ParamStr(Params, TEXT("source"));
					if (Source.IsEmpty())
					{
						return MakeErrorResult(TEXT("`source` is required"));
					}

					FString Error;
					const FElysiumVariant Value = State->EvalScript(Source, Error);

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetStringField(TEXT("source"), Source);
					Body->SetStringField(TEXT("value"), Value.ToString());
					Body->SetStringField(TEXT("described"), Value.Describe());
					Body->SetBoolField(TEXT("truthy"), Value.ToBool());
					Body->SetBoolField(TEXT("error"), !Error.IsEmpty());
					Body->SetStringField(TEXT("error_text"), Error);
					Body->SetBoolField(TEXT("live_eval"), State->IsLiveScriptEval());
					Body->SetStringField(TEXT("level_script"), State->CurrentLevelScriptModule());
					Body->SetBoolField(TEXT("level_script_loaded"), State->IsLevelScriptLoaded());
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("prefix"), TEXT("string"), TEXT("Only return G keys starting with this (case-sensitive, like Python)."));
			Out.Add(MakeTool(TEXT("elysium_g_dump"),
				TEXT("Dump the persistent story state that survives map travel: the G global flag bag, the quest map, and the player sheet. G is the flag store VtMB's own scripts branch on — read it to assert that a scenario advanced."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					UElysiumGameStateSubsystem* State = Sub<UElysiumGameStateSubsystem>();
					if (!State)
					{
						return MakeErrorResult(TEXT("no game running"));
					}
					const FString Prefix = ParamStr(Params, TEXT("prefix"));

					TSharedRef<FJsonObject> Globals = Obj();
					for (const TPair<FString, FElysiumVariant>& Pair : State->GetGlobals())
					{
						if (!Prefix.IsEmpty() && !Pair.Key.StartsWith(Prefix, ESearchCase::CaseSensitive))
						{
							continue;
						}
						Globals->SetStringField(Pair.Key, Pair.Value.Describe());
					}

					TSharedRef<FJsonObject> Quests = Obj();
					for (const TPair<FString, int32>& Pair : State->GetQuests())
					{
						Quests->SetNumberField(Pair.Key, Pair.Value);
					}

					const FElysiumSheet& Sheet = State->PlayerSheet();
					TSharedRef<FJsonObject> Player = Obj();
					Player->SetNumberField(TEXT("clan"), Sheet.Clan());
					Player->SetStringField(TEXT("clan_name"), FElysiumSheet::ClanName(Sheet.Clan()));
					Player->SetBoolField(TEXT("male"), Sheet.IsMale());
					// The whole sheet by datamap name, current values — what a script would read.
					// Zero slots are dropped so the dump reads as a character, not as a table.
					TSharedRef<FJsonObject> Stats = Obj();
					for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
					{
						const EElysiumTraitContainer Container = (EElysiumTraitContainer)i;
						for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
						{
							const int32 Value = Sheet.GetCurrent(Container, Slot.Index);
							if (Value != 0)
							{
								Stats->SetNumberField(Slot.Datamap, Value);
							}
						}
					}
					for (const TPair<FName, int32>& Pair : Sheet.Extra)
					{
						Stats->SetNumberField(Pair.Key.ToString(), Pair.Value);
					}
					Player->SetObjectField(TEXT("stats"), Stats);

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetObjectField(TEXT("g"), Globals);
					Body->SetObjectField(TEXT("quests"), Quests);
					Body->SetObjectField(TEXT("player"), Player);
					return Structured(Body);
				}));
		}

		// --- Audio --------------------------------------------------------------------------

		{
			FSchema Schema;
			Out.Add(MakeTool(TEXT("elysium_audio_state"),
				TEXT("Read the audio request ledger, live voices, routing ownership, and the active SoundScheme."),
				Schema,
				[](const TSharedPtr<FJsonObject>&) -> FModelContextProtocolToolResult
				{
					UElysiumAudioSubsystem* Audio = Sub<UElysiumAudioSubsystem>();
					if (!Audio)
					{
						return MakeErrorResult(TEXT("no game running"));
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetBoolField(TEXT("muted"), Audio->IsMuted());
					Body->SetNumberField(TEXT("master_gain"), Audio->MasterGain());
					Body->SetNumberField(TEXT("decoded_files"), Audio->Results().Num());
					// The render clock every voice is scheduled against. `scheduled_audio_clock` says
					// when a voice was *asked for*; `render_head` below says where the mixer has
					// actually reached inside it, which is the read that survives a slow decode.
					Body->SetNumberField(TEXT("audio_clock"), Audio->AudioClock());

					// 12.2b — the lead every cue is scheduled with and the terms it is made of. The
					// device half is queried, the endpoint half modelled from those queried frame
					// counts, the submit->render half measured on the voices that have played.
					const FElysiumAudioLatency& Lat = Audio->OutputLatency();
					TSharedRef<FJsonObject> Latency = Obj();
					Latency->SetBoolField(TEXT("device_queried"), Lat.bDeviceQueried);
					Latency->SetStringField(TEXT("platform_api"), Lat.PlatformApi);
					Latency->SetStringField(TEXT("device"), Lat.DeviceName);
					Latency->SetNumberField(TEXT("sample_rate"), Lat.SampleRate);
					Latency->SetNumberField(TEXT("callback_frames"), Lat.CallbackFrames);
					Latency->SetNumberField(TEXT("output_buffers"), Lat.OutputBuffers);
					Latency->SetNumberField(TEXT("device_period_frames"), Lat.DevicePeriodFrames);
					Latency->SetNumberField(TEXT("endpoint_frames"), Lat.EndpointFrames);
					Latency->SetNumberField(TEXT("mixer_queue"), Lat.MixerQueueSeconds);
					Latency->SetNumberField(TEXT("endpoint"), Lat.EndpointSeconds);
					Latency->SetNumberField(TEXT("submit_to_render"), Lat.SubmitToRenderSeconds);
					Latency->SetNumberField(TEXT("submit_to_render_peak"), Lat.SubmitToRenderPeakSeconds);
					Latency->SetNumberField(TEXT("submit_to_render_samples"), Lat.SubmitToRenderSamples);
					Latency->SetNumberField(TEXT("decode"), Lat.DecodeSeconds);
					Latency->SetNumberField(TEXT("lead"), Lat.Lead());
					Body->SetObjectField(TEXT("latency"), Latency);

					TArray<TSharedPtr<FJsonValue>> Voices;
					for (const FElysiumAudioVoice& Voice : Audio->ActiveVoices())
					{
						TSharedRef<FJsonObject> Row = Obj();
						Row->SetNumberField(TEXT("slot"), Voice.Handle.Slot);
						Row->SetNumberField(TEXT("generation"), Voice.Handle.Generation);
						Row->SetStringField(TEXT("file"), Voice.Event.ResolvedPath);
						Row->SetStringField(TEXT("owner"), Voice.Request.Owner.StableId);
						Row->SetNumberField(TEXT("map_epoch"), static_cast<double>(Voice.Request.Owner.MapEpoch));
						Row->SetNumberField(TEXT("state"), static_cast<int32>(Voice.Event.State));
						Row->SetBoolField(TEXT("looping"), Voice.Request.bLooping);
						Row->SetBoolField(TEXT("spatialized"), Voice.Request.Placement.bSpatialized);
						Row->SetNumberField(TEXT("volume"), Voice.Request.Gain);
						Row->SetNumberField(TEXT("pitch"), Voice.Request.Pitch);
						Row->SetNumberField(TEXT("scheduled_audio_clock"), Voice.Event.ScheduledAudioClock);
						Row->SetNumberField(TEXT("media_offset"), Voice.Event.MediaOffsetSeconds);
						Row->SetNumberField(TEXT("duration"), Voice.Event.DurationSeconds);
						// Where the mixer's render head sits inside this voice's own media, and how
						// long the file read + decode + realization took before it got there.
						// Negative on both until the mixer has pulled from the voice once.
						Row->SetNumberField(TEXT("render_head"), Voice.RenderHeadSeconds());
						Row->SetNumberField(TEXT("submit_to_render"),
							Voice.Render ? Voice.Render->SubmitToRenderSeconds() : -1.0);
						Voices.Add(MakeShared<FJsonValueObject>(Row));
					}
					Body->SetArrayField(TEXT("voices"), Voices);

					if (AElysiumMapActor* Map = MapActor())
					{
						if (const FElysiumSoundSchemeManager* Schemes = Map->GetSchemeManager())
						{
							TSharedRef<FJsonObject> Scheme = Obj();
							Scheme->SetBoolField(TEXT("active"), Schemes->HasActiveScheme());
							Scheme->SetStringField(TEXT("file"), Schemes->ActiveSchemeRel());
							Scheme->SetObjectField(TEXT("anchor"), Vec(Schemes->ActiveAnchor()));
							Scheme->SetNumberField(TEXT("random_voices"), Schemes->ActiveRandomVoiceCount());
							Scheme->SetNumberField(TEXT("elapsed"), Schemes->SchemeElapsed());
							const TCHAR* MusicState = TEXT("Explore");
							switch (Schemes->MusicState())
							{
							case EElysiumMusicState::Combat: MusicState = TEXT("Combat"); break;
							case EElysiumMusicState::Alert:  MusicState = TEXT("Alert");  break;
							default: break;
							}
							Scheme->SetStringField(TEXT("music_state"), MusicState);
							Body->SetObjectField(TEXT("sound_scheme"), Scheme);
						}
					}
					return Structured(Body);
				}));
		}

		// --- Escape hatches -----------------------------------------------------------------

		{
			FSchema Schema;
			Schema.Add(TEXT("command"), TEXT("string"), TEXT("A console command line, e.g. `elysium.ent_dump elevator_door` or `stat unit`."), true);
			Out.Add(MakeTool(TEXT("elysium_console_exec"),
				TEXT("Run a console command and return everything it printed. The full elysium.* verb set (ent_*, world.*, script.*, py.*, lights, props, showtriggers, Mute, MusicState, npc.*) plus every engine command is reachable here — use it for anything the typed tools do not cover."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					const FString Command = ParamStr(Params, TEXT("command"));
					if (Command.IsEmpty())
					{
						return MakeErrorResult(TEXT("`command` is required"));
					}
					if (!GEngine)
					{
						return MakeErrorResult(TEXT("no engine"));
					}

					// Most elysium.* verbs report through UE_LOG rather than the exec Ar, so take
					// the log tap's delta across the call as well and merge the two.
					FElysiumLogTap& Tap = ElysiumLogTapGet();
					const uint64 Before = Tap.Cursor();

					FExecCapture Capture;
					const bool bHandled = GEngine->Exec(LiveWorld(), *Command, Capture);

					TArray<FElysiumLogTap::FLine> Logged;
					Tap.CollectSince(Before, Logged);

					TArray<TSharedPtr<FJsonValue>> Output;
					TSet<FString> Seen;
					for (const FString& Line : Capture.Lines)
					{
						if (!Line.IsEmpty() && !Seen.Contains(Line))
						{
							Seen.Add(Line);
							Output.Add(MakeShared<FJsonValueString>(Line));
						}
					}
					for (const FElysiumLogTap::FLine& Line : Logged)
					{
						if (!Line.Text.IsEmpty() && !Seen.Contains(Line.Text))
						{
							Seen.Add(Line.Text);
							Output.Add(MakeShared<FJsonValueString>(
								FString::Printf(TEXT("[%s] %s"), *Line.Category, *Line.Text)));
						}
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetStringField(TEXT("command"), Command);
					Body->SetBoolField(TEXT("handled"), bHandled);
					Body->SetArrayField(TEXT("output"), Output);
					if (!bHandled && Output.Num() == 0)
					{
						Body->SetStringField(TEXT("note"),
							TEXT("command not recognised, or it is a cvar assignment that prints nothing"));
					}
					return Structured(Body);
				}));
		}

		{
			FSchema Schema;
			Schema.Add(TEXT("lines"), TEXT("integer"), TEXT("How many of the most recent lines to return. Default 100, ring holds 2000."))
				.Add(TEXT("category"), TEXT("string"), TEXT("Keep only lines whose log category contains this, e.g. `Elysium` or `LogElysiumIO`."));
			Out.Add(MakeTool(TEXT("elysium_log_tail"),
				TEXT("Read recent engine log lines from an always-on ring buffer, optionally filtered by category. Covers output produced before you connected — the first place to look after something failed."),
				Schema,
				[](const TSharedPtr<FJsonObject>& Params) -> FModelContextProtocolToolResult
				{
					const int32 Limit = FMath::Clamp(ParamInt(Params, TEXT("lines"), 100), 1, 2000);
					const FString Category = ParamStr(Params, TEXT("category"));

					TArray<FElysiumLogTap::FLine> Lines;
					ElysiumLogTapGet().CollectOrdered(Limit, Category, Lines);

					TArray<TSharedPtr<FJsonValue>> Values;
					for (const FElysiumLogTap::FLine& Line : Lines)
					{
						TSharedRef<FJsonObject> Row = Obj();
						Row->SetStringField(TEXT("category"), Line.Category);
						Row->SetStringField(TEXT("verbosity"), Line.Verbosity);
						Row->SetStringField(TEXT("text"), Line.Text);
						Values.Add(MakeShared<FJsonValueObject>(Row));
					}

					TSharedRef<FJsonObject> Body = Obj();
					Body->SetNumberField(TEXT("returned"), Values.Num());
					Body->SetArrayField(TEXT("lines"), Values);
					return Structured(Body);
				}));
		}

		// --- Screenshot ---------------------------------------------------------------------

		{
			FSchema Schema;
			TSharedRef<FAsyncTool> Tool = MakeShared<FAsyncTool>();
			Tool->Name = TEXT("elysium_screenshot");
			Tool->Description = TEXT("Capture the game viewport and return it as a PNG image, including the game UI (menus, HUD). Use it to close the loop — fire an input, then look at what happened.");
			Tool->Schema = Schema.Build();
			Tool->Handler = [](const TSharedPtr<FJsonObject>&, const IModelContextProtocolTool::FResultCallback& OnComplete)
			{
				const bool bRequested = ElysiumScreenshot::Request(
					[OnComplete](int32 Width, int32 Height, const TArray<FColor>& Bitmap)
					{
						if (Width <= 0 || Height <= 0)
						{
							OnComplete(MakeErrorResult(TEXT("screenshot timed out (the viewport is not presenting frames)")));
							return;
						}
						TArray64<uint8> Png;
						if (!ElysiumScreenshot::EncodePng(Width, Height, Bitmap, Png))
						{
							OnComplete(MakeErrorResult(TEXT("PNG encode failed")));
							return;
						}
						OnComplete(MakeImageResult(TEXT("image/png"),
							TArrayView<uint8>(Png.GetData(), static_cast<int32>(Png.Num()))));
					},
					/*TimeoutFrames*/ 300,
					// Show Slate: this tool exists to show what the player sees, and since 8.6 the
					// menu and every other UMG screen are Slate. The regression harness keeps the
					// UI-free capture so its baselines stay comparable.
					/*bShowUI*/ true);

				if (!bRequested)
				{
					OnComplete(MakeErrorResult(TEXT("no game viewport to capture (headless run, or no game running)")));
				}
			};
			Out.Add(Tool);
		}
	}
}

FElysiumMcpTools::FElysiumMcpTools() = default;

FElysiumMcpTools::~FElysiumMcpTools()
{
	Unregister();
}

int32 FElysiumMcpTools::Register()
{
	IModelContextProtocolModule* Module = IModelContextProtocolModule::Get();
	if (!Module)
	{
		UE_LOG(LogElysiumMcpTools, Warning,
			TEXT("ModelContextProtocol module unavailable; no Elysium MCP tools registered."));
		return 0;
	}

	// `ModelContextProtocol.RefreshTools` releases every registered tool and broadcasts for
	// providers to re-add — so a refresh must find us listening, or our tools silently vanish.
	if (!RefreshHandle.IsValid())
	{
		RefreshHandle = Module->OnRefreshTools().AddLambda([this]()
		{
			Registered.Reset();
			Register();
		});
	}

	if (Registered.Num() > 0)
	{
		return Registered.Num();
	}

	TArray<TSharedRef<IModelContextProtocolTool>> Built;
	ElysiumMcpImpl::BuildTools(Built);

	for (const TSharedRef<IModelContextProtocolTool>& Tool : Built)
	{
		if (Module->AddTool(Tool))
		{
			Registered.Add(Tool);
		}
		else
		{
			UE_LOG(LogElysiumMcpTools, Warning, TEXT("tool name '%s' already taken; skipped."),
				*Tool->GetName());
		}
	}

	UE_LOG(LogElysiumMcpTools, Log, TEXT("registered %d Elysium MCP tools."), Registered.Num());
	return Registered.Num();
}

void FElysiumMcpTools::Unregister()
{
	if (IModelContextProtocolModule* Module = IModelContextProtocolModule::Get())
	{
		if (RefreshHandle.IsValid())
		{
			Module->OnRefreshTools().Remove(RefreshHandle);
			RefreshHandle.Reset();
		}
		for (const TSharedRef<IModelContextProtocolTool>& Tool : Registered)
		{
			Module->RemoveTool(Tool);
		}
	}
	Registered.Reset();
}

int32 FElysiumMcpTools::Num() const
{
	return Registered.Num();
}

void FElysiumMcpTools::Describe(TArray<FString>& Out) const
{
	for (const TSharedRef<IModelContextProtocolTool>& Tool : Registered)
	{
		Out.Add(FString::Printf(TEXT("%s — %s"), *Tool->GetName(), *Tool->GetDescription()));
	}
}

#else  // !ELYSIUM_WITH_MCP

// No MCP plugin on this target (anything but Editor). The subsystem still constructs this, so the
// shell must exist; it simply never has anything to register.
FElysiumMcpTools::FElysiumMcpTools() = default;
FElysiumMcpTools::~FElysiumMcpTools() = default;
int32 FElysiumMcpTools::Register() { return 0; }
void FElysiumMcpTools::Unregister() {}
int32 FElysiumMcpTools::Num() const { return 0; }
void FElysiumMcpTools::Describe(TArray<FString>&) const {}

#endif // ELYSIUM_WITH_MCP
