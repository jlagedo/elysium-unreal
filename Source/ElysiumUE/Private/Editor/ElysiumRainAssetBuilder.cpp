#include "ElysiumRainAssetBuilder.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "NiagaraEmitter.h"
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
	const float MaterialRandom,
	FNiagaraExternalEditContext& Context)
{
	FNiagaraFloat LifetimeValue;
	LifetimeValue.Value = Lifetime;
	FNiagaraFloat MaterialRandomValue;
	MaterialRandomValue.Value = MaterialRandom;
	SetExpression(System, Emitter, TEXT("EmitterUpdateScript"), TEXT("SpawnRate"),
		TEXT("SpawnRate"), SpawnExpression, Context);
	SetExpression(System, Emitter, TEXT("ParticleSpawnScript"), TEXT("ShapeLocation"),
		TEXT("Sphere Radius"), TEXT("User.BoundsCm"), Context);
	SetExpression(System, Emitter, TEXT("ParticleSpawnScript"), TEXT("ShapeLocation"),
		TEXT("Shape Origin"), TEXT("float3(0.0, 0.0, User.BoundsCm)"), Context);

	TArray<FNiagaraExt_SetParameterEntry> Parameters;
	Parameters.Add(Parameter(TEXT("Particles.Lifetime"),
		FNiagaraTypeDefinition::GetFloatDef(), LifetimeValue));
	Parameters.Add(Parameter(TEXT("Particles.SpriteSize"),
		FNiagaraTypeDefinition::GetVec2Def(), SpriteSize));
	Parameters.Add(Parameter(TEXT("Particles.Velocity"),
		FNiagaraTypeDefinition::GetVec3Def(), Velocity));
	Parameters.Add(Parameter(TEXT("Particles.Color"),
		FNiagaraTypeDefinition::GetColorDef(), Color));
	Parameters.Add(Parameter(TEXT("Particles.MaterialRandom"),
		FNiagaraTypeDefinition::GetFloatDef(), MaterialRandomValue));
	Parameters.Add(Parameter(TEXT("Particles.SpriteFacing"),
		FNiagaraTypeDefinition::GetVec3Def(), FVector3f(0.0f, 0.0f, 1.0f)));
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
	for (const FName Name : {FName(TEXT("Streaks")), FName(TEXT("ImpactsStains")), FName(TEXT("Mist"))})
	{
		FNiagaraExt_EmitterTopology Topology;
		UNiagaraExternalEditUtilities::AddEmitter(TemplateEmitter, Name, Topology, Context);
	}
	// rain_follow_emitter is authored as a volume around the viewer.  The stock Fountain
	// template is world-space, which leaves its shape coordinates around the map origin when the
	// component follows the camera.  Keep all three layers in the component's local frame so the
	// one authored volume and its fixed bounds move together.
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
		{
			Data->bLocalSpace = true;
		}
	}

	AddFloatUserVariable(System, TEXT("User.RateScale"), 1.0f, Context);
	AddFloatUserVariable(System, TEXT("User.Enhancement"), 1.0f, Context);
	AddFloatUserVariable(System, TEXT("User.MistEnhancement"), 0.2f, Context);
	AddFloatUserVariable(System, TEXT("User.LightResponse"), 0.25f, Context);
	AddFloatUserVariable(System, TEXT("User.BoundsCm"), 1000.0f, Context);
	ConfigureEmitter(System, TEXT("Streaks"), TEXT("1000.0 * User.RateScale"),
		1.25f, FVector2f(7.62f, 25.4f), FVector3f(50.8f, -50.8f, -1270.0f),
		FLinearColor(1.0f, 0.0f, 0.0f, 0.32f), 0.0f, Context);
	ConfigureEmitter(System, TEXT("ImpactsStains"), TEXT("1000.0 * User.RateScale"),
		0.8f, FVector2f(63.5f, 63.5f), FVector3f::ZeroVector,
		FLinearColor(0.0f, 1.0f, 0.0f, 0.25f), 0.5f, Context);
	ConfigureEmitter(System, TEXT("Mist"),
		TEXT("70.0 * User.RateScale * (1.0 + User.Enhancement * User.MistEnhancement)"),
		10.0f, FVector2f(1270.0f, 1270.0f), FVector3f::ZeroVector,
		FLinearColor(0.0f, 0.0f, 1.0f, 0.03f), 1.0f, Context);
	ReportErrors(TEXT("author system"), Context);
	UMaterialInterface* RainMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/VtMB/Particles/M_ElysiumRain.M_ElysiumRain"));
	if (!RainMaterial || !BindRainMaterial(System, RainMaterial))
	{
		UE_LOG(LogTemp, Error, TEXT("[rain-assets] generated rain material is missing or invalid"));
		return nullptr;
	}
	System->RequestCompile(false);
	// The policy generator runs in a short-lived commandlet.  Do not let it save/exit while the
	// newly-authored material shader is still pending: a following verifier or first game frame
	// would otherwise see no SM6 material resource and silently render no rain.
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
#if WITH_EDITOR
	if (!System || !Material)
	{
		return false;
	}
	FNiagaraExternalEditContext Context(System);
	FNiagaraExt_SystemSummary Summary;
	UNiagaraExternalEditUtilities::GetSystemSummary(System, Summary, Context);
	const FString ObjectReference = FString::Printf(
		TEXT("%s'%s'"), *Material->GetClass()->GetPathName(), *Material->GetPathName());
	for (const FNiagaraExt_EmitterSummary& Emitter : Summary.Emitters)
	{
		FNiagaraExt_StackItemReference EmitterRef(System, Emitter.EmitterName);
		FNiagaraExt_EmitterTopology Topology;
		UNiagaraExternalEditUtilities::GetEmitterTopology(EmitterRef, Topology, Context);
		for (const FNiagaraExt_RendererRef& Renderer : Topology.Renderers)
		{
			FNiagaraExt_StackItemReference RendererRef(System, Emitter.EmitterName);
			RendererRef.RendererIndex = Renderer.RendererIndex;
			FNiagaraExt_RendererData Data;
			UNiagaraExternalEditUtilities::GetRendererData(RendererRef, Data, Context);
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Data.PropertyValues);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root)
			{
				UE_LOG(LogTemp, Error, TEXT("[rain-assets] invalid renderer JSON for %s"),
					*Emitter.EmitterName.ToString());
				return false;
			}
			Root->SetStringField(TEXT("Material"), ObjectReference);
			if (Emitter.EmitterName == TEXT("Streaks"))
			{
				Root->SetStringField(TEXT("Alignment"), TEXT("VelocityAligned"));
				Root->SetStringField(TEXT("FacingMode"), TEXT("FaceCamera"));
			}
			else if (Emitter.EmitterName == TEXT("ImpactsStains"))
			{
				Root->SetStringField(TEXT("Alignment"), TEXT("Unaligned"));
				Root->SetStringField(TEXT("FacingMode"), TEXT("CustomFacingVector"));
			}
			else
			{
				Root->SetStringField(TEXT("Alignment"), TEXT("Unaligned"));
				Root->SetStringField(TEXT("FacingMode"), TEXT("FaceCamera"));
			}
			Data.PropertyValues.Reset();
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Data.PropertyValues);
			FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
			UNiagaraExternalEditUtilities::SetRendererData(RendererRef, Data, Context);
		}
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
	if (!Material)
	{
		Errors.Add(TEXT("rain material is missing"));
	}
	if (Material)
	{
		const UMaterial* BaseMaterial = Material->GetMaterial();
		const FMaterialResource* Resource = BaseMaterial
			? BaseMaterial->GetMaterialResource(GMaxRHIShaderPlatform)
			: nullptr;
		if (!BaseMaterial || !Resource)
		{
			Errors.Add(TEXT("rain material has no compiled resource"));
		}
		else
		{
			for (const FString& CompileError : Resource->GetCompileErrors())
			{
				Errors.Add(FString::Printf(TEXT("material: %s"), *CompileError));
			}
		}
	}
	if (!System)
	{
		return FString::Join(Errors, TEXT("; "));
	}

	FNiagaraExternalEditContext Context(System);
	FNiagaraExt_SystemSummary Summary;
	UNiagaraExternalEditUtilities::GetSystemSummary(System, Summary, Context);
	const TSet<FName> ExpectedEmitters = {
		FName(TEXT("Streaks")), FName(TEXT("ImpactsStains")), FName(TEXT("Mist"))};
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
			|| !Material || !BoundMaterial.Contains(Material->GetPathName()))
		{
			Errors.Add(FString::Printf(TEXT("emitter %s is not bound to %s"),
				*Emitter.EmitterName.ToString(),
				Material ? *Material->GetPathName() : TEXT("<missing>")));
		}
	}
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data || !Data->bLocalSpace)
		{
			Errors.Add(FString::Printf(TEXT("emitter is not viewer-volume local-space: %s"),
				*Handle.GetName().ToString()));
		}
	}
	if (ActualEmitters.Num() != ExpectedEmitters.Num()
		|| ActualEmitters.Difference(ExpectedEmitters).Num() != 0
		|| ExpectedEmitters.Difference(ActualEmitters).Num() != 0)
	{
		Errors.Add(TEXT("Niagara emitter set is not Streaks/ImpactsStains/Mist"));
	}

	const TSet<FName> ExpectedVariables = {
		FName(TEXT("User.RateScale")), FName(TEXT("User.Enhancement")),
		FName(TEXT("User.MistEnhancement")), FName(TEXT("User.LightResponse")),
		FName(TEXT("User.BoundsCm"))};
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
