#include "ElysiumRainAssetBuilder.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "NiagaraEmitter.h"
#include "NiagaraTypes.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraSystem.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "RHIShaderPlatform.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
void ReportErrors(const TCHAR* Operation, const FNiagaraExternalEditContext& Context)
{
	for (const FText& Error : Context.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("[rain-assets] %s: %s"), Operation, *Error.ToString());
	}
}

void AddFloatUserVariable(
	UNiagaraSystem* System,
	const TCHAR* Name,
	const float Default,
	FNiagaraExternalEditContext& Context)
{
	FNiagaraExt_UserVariable Variable;
	Variable.Name = FName(Name);
	Variable.Type = FNiagaraTypeDefinition::GetFloatDef();
	FNiagaraFloat Value;
	Value.Value = Default;
	Variable.DefaultValue.InitializeAs<FNiagaraFloat>(Value);
	UNiagaraExternalEditUtilities::AddUserVariable(System, Variable, Context);
}

void AddPositionUserVariable(
	UNiagaraSystem* System,
	const TCHAR* Name,
	const FVector3f& Default,
	FNiagaraExternalEditContext& Context)
{
	FNiagaraExt_UserVariable Variable;
	Variable.Name = FName(Name);
	Variable.Type = FNiagaraTypeDefinition::GetPositionDef();
	const FNiagaraPosition Value(Default);
	Variable.DefaultValue.InitializeAs<FNiagaraPosition>(Value);
	UNiagaraExternalEditUtilities::AddUserVariable(System, Variable, Context);
}

void SetExpression(
	UNiagaraSystem* System,
	const FName Emitter,
	const TCHAR* Script,
	const TCHAR* Module,
	const TCHAR* Input,
	const TCHAR* Expression,
	FNiagaraExternalEditContext& Context)
{
	FNiagaraExt_StackItemReference Reference(
		System, Emitter, FName(Script), FName(Module));
	Reference.InputNameStack.Add(FName(Input));
	FNiagaraExt_StackInputValue Value;
	FNiagaraExt_StackInputData_HlslExpression& Data =
		Value.InitializeAs<FNiagaraExt_StackInputData_HlslExpression>();
	Data.HlslExpression = Expression;
	UNiagaraExternalEditUtilities::SetStackInputData(Reference, Value, Context);
}

template<typename TValue>
FNiagaraExt_SetParameterEntry Parameter(
	const TCHAR* Name,
	const FNiagaraTypeDefinition& Type,
	const TValue& Value)
{
	FNiagaraExt_SetParameterEntry Entry;
	Entry.Variable.Name = FName(Name);
	Entry.Variable.Type = Type;
	Entry.DefaultValue.Set(Type, FNiagaraVariant(&Value, sizeof(Value)));
	return Entry;
}

void ConfigureEmitter(
	UNiagaraSystem* System,
	const FName Emitter,
	const TCHAR* SpawnExpression,
	const float Lifetime,
	const FVector2f SpriteSize,
	const FVector3f Velocity,
	const FLinearColor Color,
	FNiagaraExternalEditContext& Context)
{
	FNiagaraFloat LifetimeValue;
	LifetimeValue.Value = Lifetime;
	SetExpression(System, Emitter, TEXT("EmitterUpdateScript"), TEXT("SpawnRate"),
		TEXT("SpawnRate"), SpawnExpression, Context);
	// World-space spawn around the component. Local-space made the whole field ride the camera.
	SetExpression(System, Emitter, TEXT("ParticleSpawnScript"), TEXT("ShapeLocation"),
		TEXT("Sphere Radius"), TEXT("User.BoundsCm"), Context);
	SetExpression(System, Emitter, TEXT("ParticleSpawnScript"), TEXT("ShapeLocation"),
		TEXT("Shape Origin"),
		TEXT("User.SpawnCenter + float3(0.0, 0.0, 300.0)"), Context);

	TArray<FNiagaraExt_SetParameterEntry> Parameters;
	Parameters.Add(Parameter(TEXT("Particles.Lifetime"),
		FNiagaraTypeDefinition::GetFloatDef(), LifetimeValue));
	Parameters.Add(Parameter(TEXT("Particles.SpriteSize"),
		FNiagaraTypeDefinition::GetVec2Def(), SpriteSize));
	Parameters.Add(Parameter(TEXT("Particles.Velocity"),
		FNiagaraTypeDefinition::GetVec3Def(), Velocity));
	Parameters.Add(Parameter(TEXT("Particles.Color"),
		FNiagaraTypeDefinition::GetColorDef(), Color));
	FNiagaraExt_StackItemReference Location(
		System, Emitter, FName(TEXT("ParticleSpawnScript")));
	FNiagaraExt_ModuleTopology OutTopology;
	UNiagaraExternalEditUtilities::AddSetParametersModule(
		Location, Parameters, OutTopology, Context);

	for (const TCHAR* Module : {TEXT("GravityForce"), TEXT("Drag"), TEXT("ScaleColor")})
	{
		FNiagaraExt_StackItemReference ModuleRef(
			System, Emitter, FName(TEXT("ParticleUpdateScript")), FName(Module));
		UNiagaraExternalEditUtilities::SetModuleEnabled(ModuleRef, false, Context);
	}
}

bool BindEmitterMaterial(
	UNiagaraSystem* System,
	const FName EmitterName,
	UMaterialInterface* Material,
	const TCHAR* Alignment,
	const TCHAR* FacingMode,
	FNiagaraExternalEditContext& Context)
{
	FNiagaraExt_StackItemReference EmitterRef(System, EmitterName);
	FNiagaraExt_EmitterTopology Topology;
	UNiagaraExternalEditUtilities::GetEmitterTopology(EmitterRef, Topology, Context);
	if (Topology.Renderers.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[rain-assets] emitter %s has %d renderers"),
			*EmitterName.ToString(), Topology.Renderers.Num());
		return false;
	}
	FNiagaraExt_StackItemReference RendererRef(System, EmitterName);
	RendererRef.RendererIndex = Topology.Renderers[0].RendererIndex;
	FNiagaraExt_RendererData Data;
	UNiagaraExternalEditUtilities::GetRendererData(RendererRef, Data, Context);
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Data.PropertyValues);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root)
	{
		UE_LOG(LogTemp, Error, TEXT("[rain-assets] invalid renderer JSON for %s"),
			*EmitterName.ToString());
		return false;
	}
	Root->SetStringField(TEXT("Material"), FString::Printf(
		TEXT("%s'%s'"), *Material->GetClass()->GetPathName(), *Material->GetPathName()));
	Root->SetStringField(TEXT("Alignment"), Alignment);
	Root->SetStringField(TEXT("FacingMode"), FacingMode);
	if (EmitterName == TEXT("Streaks"))
	{
		Root->SetBoolField(TEXT("bEnableCameraDistanceCulling"), true);
		Root->SetNumberField(TEXT("MinCameraDistance"), 180.0);
		Root->SetNumberField(TEXT("MaxCameraDistance"), 2800.0);
	}
	Data.PropertyValues.Reset();
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Data.PropertyValues);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	UNiagaraExternalEditUtilities::SetRendererData(RendererRef, Data, Context);
	return true;
}
}
#endif

UNiagaraSystem* UElysiumRainAssetBuilder::BuildRainSystem(
	const FString& AssetName,
	const FString& PackagePath,
	UNiagaraEmitter* TemplateEmitter)
{
#if WITH_EDITOR
	if (!TemplateEmitter)
	{
		UE_LOG(LogTemp, Error, TEXT("[rain-assets] no Niagara emitter template"));
		return nullptr;
	}

	FNiagaraExternalEditContext Context;
	UNiagaraSystem* System = UNiagaraExternalEditUtilities::CreateNiagaraSystem(
		AssetName, PackagePath, nullptr, Context);
	if (!System || Context.HasErrors())
	{
		ReportErrors(TEXT("create system"), Context);
		return nullptr;
	}

	Context = FNiagaraExternalEditContext(System);
	for (const FName Name : {FName(TEXT("Streaks")), FName(TEXT("Mist"))})
	{
		FNiagaraExt_EmitterTopology Topology;
		UNiagaraExternalEditUtilities::AddEmitter(TemplateEmitter, Name, Topology, Context);
	}
	// Follow-volume: the component tracks the viewer, particles stay in world space so they do
	// not glue to the camera. Spawn is offset from Engine.Owner.Position.
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
		{
			Data->bLocalSpace = false;
			Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
			Data->FixedBounds = FBox(FVector(-2000.0, -2000.0, -2000.0), FVector(2000.0, 2000.0, 2000.0));
		}
	}

	AddFloatUserVariable(System, TEXT("User.RateScale"), 1.0f, Context);
	AddFloatUserVariable(System, TEXT("User.BoundsCm"), 1200.0f, Context);
	AddFloatUserVariable(System, TEXT("User.LightResponse"), 1.0f, Context);
	AddPositionUserVariable(System, TEXT("User.SpawnCenter"), FVector3f::ZeroVector, Context);
	AddFloatUserVariable(System, TEXT("User.StreakWidth"), 1.2f, Context);
	AddFloatUserVariable(System, TEXT("User.StreakLength"), 55.0f, Context);
	AddFloatUserVariable(System, TEXT("User.StreakAlpha"), 0.18f, Context);
	ConfigureEmitter(System, TEXT("Streaks"), TEXT("2200.0 * User.RateScale"),
		0.70f, FVector2f(1.20f, 55.0f), FVector3f(40.0f, -40.0f, -1600.0f),
		FLinearColor(0.75f, 0.82f, 0.90f, 0.18f), Context);
	ConfigureEmitter(System, TEXT("Mist"), TEXT("0.0"),
		3.00f, FVector2f(80.0f, 80.0f), FVector3f(0.0f, 0.0f, -40.0f),
		FLinearColor(0.55f, 0.62f, 0.70f, 0.03f), Context);
	ReportErrors(TEXT("author system"), Context);
	UMaterialInterface* Streaks = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/VtMB/Particles/MI_ElysiumRainStreak.MI_ElysiumRainStreak"));
	UMaterialInterface* Mist = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/VtMB/Particles/MI_ElysiumRainMist.MI_ElysiumRainMist"));
	if (!Streaks)
	{
		Streaks = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/VtMB/Particles/M_ElysiumRain.M_ElysiumRain"));
	}
	if (!Mist)
	{
		Mist = Streaks;
	}
	if (!Streaks || !BindRainMaterials(System, Streaks, Mist))
	{
		UE_LOG(LogTemp, Error, TEXT("[rain-assets] generated rain material is missing or invalid"));
		return nullptr;
	}
	System->RequestCompile(false);
	FAssetCompilingManager::Get().FinishAllCompilation();
	System->MarkPackageDirty();
	return Context.HasErrors() ? nullptr : System;
#else
	return nullptr;
#endif
}

bool UElysiumRainAssetBuilder::BindRainMaterial(
	UNiagaraSystem* System,
	UMaterialInterface* Material)
{
	return BindRainMaterials(System, Material, Material);
}

bool UElysiumRainAssetBuilder::BindRainMaterials(
	UNiagaraSystem* System,
	UMaterialInterface* Streaks,
	UMaterialInterface* Mist)
{
#if WITH_EDITOR
	if (!System || !Streaks || !Mist)
	{
		return false;
	}
	FNiagaraExternalEditContext Context(System);
	if (!BindEmitterMaterial(System, TEXT("Streaks"), Streaks,
			TEXT("VelocityAligned"), TEXT("FaceCamera"), Context)
		|| !BindEmitterMaterial(System, TEXT("Mist"), Mist,
			TEXT("Unaligned"), TEXT("FaceCamera"), Context))
	{
		return false;
	}
	ReportErrors(TEXT("bind rain material"), Context);
	System->RequestCompile(false);
	FAssetCompilingManager::Get().FinishAllCompilation();
	System->MarkPackageDirty();
	return !Context.HasErrors();
#else
	return false;
#endif
}

FString UElysiumRainAssetBuilder::ValidateRainSystem(
	UNiagaraSystem* System,
	UMaterialInterface* Material)
{
#if WITH_EDITOR
	TArray<FString> Errors;
	if (!System)
	{
		Errors.Add(TEXT("Niagara system is missing"));
	}
	if (Material)
	{
		UMaterial* BaseMaterial = Material->GetMaterial();
		if (!BaseMaterial)
		{
			Errors.Add(TEXT("rain material has no base material"));
		}
		else
		{
			TArray<FMaterialResource*> Resources;
			FMaterialResource* Sm6 = FindOrCreateMaterialResource(Resources, BaseMaterial, nullptr,
				SP_PCD3D_SM6, EMaterialQualityLevel::High);
			if (!Sm6)
			{
				Errors.Add(TEXT("rain material creates no PCD3D_SM6 resource"));
			}
			else
			{
				if (!Sm6->CacheShaders(EMaterialShaderPrecompileMode::None))
				{
					Errors.Add(TEXT("rain material does not compile for PCD3D_SM6"));
				}
				for (const FString& CompileError : Sm6->GetCompileErrors())
				{
					Errors.Add(FString::Printf(TEXT("material: %s"), *CompileError));
				}
			}
			FMaterial::DeferredDeleteArray(Resources);
		}
	}
	if (!System)
	{
		return FString::Join(Errors, TEXT("; "));
	}

	FNiagaraExternalEditContext Context(System);
	FNiagaraExt_SystemSummary Summary;
	UNiagaraExternalEditUtilities::GetSystemSummary(System, Summary, Context);
	const TSet<FName> ExpectedEmitters = {FName(TEXT("Streaks")), FName(TEXT("Mist"))};
	TSet<FName> ActualEmitters;
	for (const FNiagaraExt_EmitterSummary& Emitter : Summary.Emitters)
	{
		ActualEmitters.Add(Emitter.EmitterName);
		if (!Emitter.bEnabled)
		{
			Errors.Add(FString::Printf(TEXT("emitter disabled: %s"),
				*Emitter.EmitterName.ToString()));
		}
		if (Emitter.RendererClasses.Num() != 1)
		{
			Errors.Add(FString::Printf(TEXT("emitter %s has %d renderer classes"),
				*Emitter.EmitterName.ToString(), Emitter.RendererClasses.Num()));
		}

		FNiagaraExt_StackItemReference EmitterRef(System, Emitter.EmitterName);
		FNiagaraExt_EmitterTopology Topology;
		UNiagaraExternalEditUtilities::GetEmitterTopology(EmitterRef, Topology, Context);
		if (Topology.Renderers.Num() != 1)
		{
			Errors.Add(FString::Printf(TEXT("emitter %s has %d renderers"),
				*Emitter.EmitterName.ToString(), Topology.Renderers.Num()));
			continue;
		}
		FNiagaraExt_StackItemReference RendererRef(System, Emitter.EmitterName);
		RendererRef.RendererIndex = Topology.Renderers[0].RendererIndex;
		FNiagaraExt_RendererData RendererData;
		UNiagaraExternalEditUtilities::GetRendererData(RendererRef, RendererData, Context);
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(RendererData.PropertyValues);
		FString BoundMaterial;
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root
			|| !Root->TryGetStringField(TEXT("Material"), BoundMaterial)
			|| BoundMaterial.IsEmpty())
		{
			Errors.Add(FString::Printf(TEXT("emitter %s has no material"),
				*Emitter.EmitterName.ToString()));
		}
	}
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data || Data->bLocalSpace)
		{
			Errors.Add(FString::Printf(TEXT("emitter is not world-space: %s"),
				*Handle.GetName().ToString()));
		}
	}
	if (ActualEmitters.Num() != ExpectedEmitters.Num()
		|| ActualEmitters.Difference(ExpectedEmitters).Num() != 0
		|| ExpectedEmitters.Difference(ActualEmitters).Num() != 0)
	{
		Errors.Add(TEXT("Niagara emitter set is not Streaks/Mist"));
	}

	const TSet<FName> ExpectedVariables = {
		FName(TEXT("User.RateScale")), FName(TEXT("User.BoundsCm")),
		FName(TEXT("User.LightResponse")), FName(TEXT("User.SpawnCenter")),
		FName(TEXT("User.StreakWidth")), FName(TEXT("User.StreakLength")),
		FName(TEXT("User.StreakAlpha"))};
	TSet<FName> ActualVariables;
	for (const FNiagaraExt_UserVariable& Variable : Summary.UserVariables)
	{
		ActualVariables.Add(Variable.Name);
	}
	for (const FName Expected : ExpectedVariables)
	{
		if (!ActualVariables.Contains(Expected))
		{
			Errors.Add(FString::Printf(TEXT("missing Niagara user variable: %s"),
				*Expected.ToString()));
		}
	}

	FNiagaraExt_SystemCompileState CompileState;
	UNiagaraExternalEditUtilities::GetSystemCompileState(System, CompileState, Context);
	if (CompileState.bIsCompiling || CompileState.bIsStale)
	{
		Errors.Add(TEXT("Niagara compile state is pending or stale"));
	}
	if (CompileState.bHasErrors
		|| CompileState.AggregateStatus == ENiagaraExt_ScriptCompileStatus::Error)
	{
		for (const FNiagaraExt_ScriptCompileInfo& Script : CompileState.Scripts)
		{
			if (!Script.ErrorSummary.IsEmpty())
			{
				Errors.Add(FString::Printf(TEXT("Niagara %s/%s: %s"),
					*Script.EmitterName.ToString(), *Script.ScriptName.ToString(),
					*Script.ErrorSummary));
			}
		}
		if (Errors.Num() == 0)
		{
			Errors.Add(TEXT("Niagara compile failed without an error summary"));
		}
	}
	for (const FText& ContextError : Context.Errors)
	{
		Errors.Add(FString::Printf(TEXT("Niagara inspection: %s"),
			*ContextError.ToString()));
	}
	return FString::Join(Errors, TEXT("; "));
#else
	return TEXT("rain validation is editor-only");
#endif
}
