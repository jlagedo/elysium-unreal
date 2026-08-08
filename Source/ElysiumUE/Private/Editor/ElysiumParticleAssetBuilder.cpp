#include "ElysiumParticleAssetBuilder.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInterface.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
void ReportParticleErrors(const TCHAR* Operation, const FNiagaraExternalEditContext& Context)
{
	for (const FText& Error : Context.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("[particle-assets] %s: %s"), Operation, *Error.ToString());
	}
}

template<typename TValue>
FNiagaraExt_SetParameterEntry ParticleParameter(
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

void SetExpression(
	UNiagaraSystem* System,
	const FName Emitter,
	const TCHAR* Script,
	const TCHAR* Module,
	const TCHAR* Input,
	const FString& Expression,
	FNiagaraExternalEditContext& Context)
{
	FNiagaraExt_StackItemReference Reference(System, Emitter, FName(Script), FName(Module));
	Reference.InputNameStack.Add(FName(Input));
	FNiagaraExt_StackInputValue Value;
	FNiagaraExt_StackInputData_HlslExpression& Data =
		Value.InitializeAs<FNiagaraExt_StackInputData_HlslExpression>();
	Data.HlslExpression = Expression;
	UNiagaraExternalEditUtilities::SetStackInputData(Reference, Value, Context);
}

// Emitter names come from VtMB definition names, which are already lowercase and
// `[a-z0-9_./-]`-constrained by the offline contract. Niagara wants a plain FName, so the two
// characters that are legal in a definition name but read badly in a stack become underscores.
FName LayerName(const FString& Raw, int32 Ordinal)
{
	FString Clean = Raw.IsEmpty() ? FString::Printf(TEXT("layer%d"), Ordinal) : Raw;
	Clean.ReplaceInline(TEXT("/"), TEXT("_"));
	Clean.ReplaceInline(TEXT("."), TEXT("_"));
	return FName(*Clean);
}

void ConfigureLayer(
	UNiagaraSystem* System,
	const FName Emitter,
	const FElysiumParticleLayer& Layer,
	FNiagaraExternalEditContext& Context)
{
	// A VtMB spawn block carries either a rate or a burst. Niagara's template drives SpawnRate from
	// the emitter update script; a burst-only layer sets it to zero and leans on the spawn count.
	SetExpression(System, Emitter, TEXT("EmitterUpdateScript"), TEXT("SpawnRate"),
		TEXT("SpawnRate"), FString::Printf(TEXT("%f"), Layer.SpawnRate), Context);
	if (Layer.Burst > 0)
	{
		// The stock Fountain template carries SpawnRate but no burst module, so add one before
		// driving it.
		if (UNiagaraScript* BurstModule = LoadObject<UNiagaraScript>(nullptr,
			TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous")))
		{
			FNiagaraExt_StackItemReference Location(
				System, Emitter, FName(TEXT("EmitterUpdateScript")));
			FNiagaraExt_ModuleTopology OutTopology;
			UNiagaraExternalEditUtilities::AddModule(Location, BurstModule, OutTopology, Context);
			SetExpression(System, Emitter, TEXT("EmitterUpdateScript"),
				TEXT("SpawnBurst_Instantaneous"), TEXT("Spawn Count"),
				FString::Printf(TEXT("%d"), Layer.Burst), Context);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[particle-assets] burst module missing; '%s' emits by rate only"),
				*Emitter.ToString());
		}
	}

	FNiagaraFloat LifetimeValue;
	LifetimeValue.Value = FMath::Max(Layer.LifetimeSeconds, KINDA_SMALL_NUMBER);

	TArray<FNiagaraExt_SetParameterEntry> Parameters;
	Parameters.Add(ParticleParameter(TEXT("Particles.Lifetime"),
		FNiagaraTypeDefinition::GetFloatDef(), LifetimeValue));
	Parameters.Add(ParticleParameter(TEXT("Particles.SpriteSize"),
		FNiagaraTypeDefinition::GetVec2Def(),
		FVector2f(static_cast<float>(Layer.SpriteSize.X), static_cast<float>(Layer.SpriteSize.Y))));
	Parameters.Add(ParticleParameter(TEXT("Particles.Velocity"),
		FNiagaraTypeDefinition::GetVec3Def(),
		FVector3f(static_cast<float>(Layer.Velocity.X), static_cast<float>(Layer.Velocity.Y),
			static_cast<float>(Layer.Velocity.Z))));
	Parameters.Add(ParticleParameter(TEXT("Particles.Position"),
		FNiagaraTypeDefinition::GetVec3Def(),
		FVector3f(static_cast<float>(Layer.Offset.X), static_cast<float>(Layer.Offset.Y),
			static_cast<float>(Layer.Offset.Z))));
	Parameters.Add(ParticleParameter(TEXT("Particles.Color"),
		FNiagaraTypeDefinition::GetColorDef(), Layer.Color));
	Parameters.Add(ParticleParameter(TEXT("Particles.SpriteFacing"),
		FNiagaraTypeDefinition::GetVec3Def(), FVector3f(0.0f, 0.0f, 1.0f)));

	FNiagaraExt_StackItemReference Location(System, Emitter, FName(TEXT("ParticleSpawnScript")));
	FNiagaraExt_ModuleTopology OutTopology;
	UNiagaraExternalEditUtilities::AddSetParametersModule(
		Location, Parameters, OutTopology, Context);

	// The stock template's forces are authored for a fountain. VtMB resolves its own motion into
	// the layer's velocity offline, so anything that would add to it is turned off.
	for (const TCHAR* Module : {TEXT("GravityForce"), TEXT("Drag"), TEXT("ScaleColor")})
	{
		FNiagaraExt_StackItemReference ModuleRef(
			System, Emitter, FName(TEXT("ParticleUpdateScript")), FName(Module));
		UNiagaraExternalEditUtilities::SetModuleEnabled(ModuleRef, false, Context);
	}
}
}
#endif

void UElysiumParticleAssetBuilder::FinishAssetCompilation()
{
#if WITH_EDITOR
	FAssetCompilingManager::Get().FinishAllCompilation();
#endif
}

UNiagaraSystem* UElysiumParticleAssetBuilder::BuildParticleSystem(
	const FString& AssetName,
	const FString& PackagePath,
	UNiagaraEmitter* TemplateEmitter,
	const TArray<FElysiumParticleLayer>& Layers)
{
#if WITH_EDITOR
	if (!TemplateEmitter)
	{
		UE_LOG(LogTemp, Error, TEXT("[particle-assets] %s: no Niagara emitter template"), *AssetName);
		return nullptr;
	}
	if (Layers.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[particle-assets] %s: closure flattened to no layers"), *AssetName);
		return nullptr;
	}

	FNiagaraExternalEditContext Context;
	UNiagaraSystem* System = UNiagaraExternalEditUtilities::CreateNiagaraSystem(
		AssetName, PackagePath, nullptr, Context);
	if (!System || Context.HasErrors())
	{
		ReportParticleErrors(TEXT("create system"), Context);
		return nullptr;
	}

	Context = FNiagaraExternalEditContext(System);
	TArray<FName> Names;
	Names.Reserve(Layers.Num());
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		const FName Wanted = LayerName(Layers[Index].Name, Index);
		const int32 Before = System->GetEmitterHandles().Num();
		FNiagaraExt_EmitterTopology Topology;
		UNiagaraExternalEditUtilities::AddEmitter(TemplateEmitter, Wanted, Topology, Context);

		// The name asked for is not necessarily the name given. `FNiagaraEmitterHandle::SetName`
		// sanitizes it and then runs it through `FNiagaraUtilities::GetUniqueName`, which appends
		// an index on collision -- and a VtMB spawn graph CAN name one particle twice, because the
		// flatten walk's cycle guard is an ancestor path and a diamond legitimately yields two
		// layers with one name. Configuring by the requested name then wrote both layers onto the
		// first emitter and left the second holding the raw Fountain template, sprite material and
		// all, with nothing reporting a difference.
		const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
		if (Handles.Num() != Before + 1)
		{
			UE_LOG(LogTemp, Error, TEXT("[particle-assets] %s: emitter '%s' was not added"),
				*System->GetName(), *Wanted.ToString());
			Names.Add(Wanted);
			continue;
		}
		const FName Actual = Handles[Before].GetName();
		if (Actual != Wanted)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[particle-assets] %s: emitter '%s' was renamed to '%s' -- ")
				TEXT("the definition spawns it twice"),
				*System->GetName(), *Wanted.ToString(), *Actual.ToString());
		}
		Names.Add(Actual);
	}
	// A VtMB emitter is placed at an entity and its offsets are authored around that origin, so the
	// whole system rides its component rather than sitting in world space. This is what lets a
	// bone-attached emitter follow the bone with its layers intact.
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
		{
			Data->bLocalSpace = true;
		}
	}

	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		ConfigureLayer(System, Names[Index], Layers[Index], Context);
	}
	ReportParticleErrors(TEXT("author system"), Context);
	if (Context.HasErrors())
	{
		return nullptr;
	}

	FAssetCompilingManager::Get().FinishAllCompilation();
	return System;
#else
	return nullptr;
#endif
}

bool UElysiumParticleAssetBuilder::BindLayerMaterial(
	UNiagaraSystem* System, int32 LayerIndex, UMaterialInterface* Material)
{
#if WITH_EDITOR
	if (!System || !Material)
	{
		return false;
	}
	FNiagaraExternalEditContext Context(System);
	FNiagaraExt_SystemSummary Summary;
	UNiagaraExternalEditUtilities::GetSystemSummary(System, Summary, Context);
	// By ORDINAL, not by name. Emitters are added in layer order, but the name one ends up with is
	// the engine's -- a definition a spawn graph reaches twice gets its second emitter uniquified,
	// and a caller that knows only the requested name would bind the first emitter twice and leave
	// the second on the template's default material.
	if (!Summary.Emitters.IsValidIndex(LayerIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("[particle-assets] %s: no emitter %d of %d"),
			*System->GetName(), LayerIndex, Summary.Emitters.Num());
		return false;
	}
	const FName Target = Summary.Emitters[LayerIndex].EmitterName;
	const FString ObjectReference = FString::Printf(
		TEXT("%s'%s'"), *Material->GetClass()->GetPathName(), *Material->GetPathName());
	bool bFound = false;
	for (const FNiagaraExt_EmitterSummary& Emitter : Summary.Emitters)
	{
		if (Emitter.EmitterName != Target)
		{
			continue;
		}
		bFound = true;
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
				UE_LOG(LogTemp, Error, TEXT("[particle-assets] invalid renderer JSON for %s"),
					*Emitter.EmitterName.ToString());
				return false;
			}
			Root->SetStringField(TEXT("Material"), ObjectReference);
			// VtMB's `movealign` is per-definition; the offline flatten does not carry it yet, so
			// every layer draws camera-facing, which is what an unaligned sprite does anyway.
			Root->SetStringField(TEXT("Alignment"), TEXT("Unaligned"));
			Root->SetStringField(TEXT("FacingMode"), TEXT("FaceCamera"));
			Data.PropertyValues.Reset();
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Data.PropertyValues);
			FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
			UNiagaraExternalEditUtilities::SetRendererData(RendererRef, Data, Context);
		}
	}
	if (!bFound)
	{
		UE_LOG(LogTemp, Error, TEXT("[particle-assets] %s: emitter %d ('%s') has no renderer"),
			*System->GetName(), LayerIndex, *Target.ToString());
		return false;
	}
	ReportParticleErrors(TEXT("bind layer material"), Context);
	System->RequestCompile(false);
	FAssetCompilingManager::Get().FinishAllCompilation();
	System->MarkPackageDirty();
	return !Context.HasErrors();
#else
	return false;
#endif
}

FString UElysiumParticleAssetBuilder::ValidateParticleSystem(
	UNiagaraSystem* System, int32 ExpectedLayers)
{
#if WITH_EDITOR
	TArray<FString> Errors;
	if (!System)
	{
		return TEXT("particle system is null");
	}
	const int32 Actual = System->GetEmitterHandles().Num();
	if (Actual != ExpectedLayers)
	{
		Errors.Add(FString::Printf(
			TEXT("expected %d emitter(s), found %d"), ExpectedLayers, Actual));
	}
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (!Handle.GetIsEnabled())
		{
			Errors.Add(FString::Printf(TEXT("emitter '%s' is disabled"),
				*Handle.GetName().ToString()));
		}
		const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data)
		{
			Errors.Add(FString::Printf(TEXT("emitter '%s' has no data"),
				*Handle.GetName().ToString()));
		}
		else if (!Data->bLocalSpace)
		{
			Errors.Add(FString::Printf(TEXT("emitter '%s' is not local-space"),
				*Handle.GetName().ToString()));
		}
	}
	return FString::Join(Errors, TEXT("; "));
#else
	return TEXT("particle validation is editor-only");
#endif
}
