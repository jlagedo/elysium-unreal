#include "ElysiumParticleAssetBuilder.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "Editor.h"
#include "ElysiumEffectFamilies.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
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
		// layers with one name. Configuring by the requested name writes both layers onto the
		// first emitter and leaves the second holding the raw Fountain template, sprite material and
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
			// VtMB's `movealign` is per-definition; the offline flatten does not carry it, so
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

// ============================================================ the generated lane (R7.3)
//
// One `NS_<root>` per VtMB root, one inherited emitter per drawing node of the staged tree
// (`docs/project/niagara_authoring_strategy.md` 4.1; the design note is
// `E:/elysium-work/scratch/effects/generator_design.md`). Everything above this line belongs to
// the legacy per-map closure lane and is untouched.

#if WITH_EDITOR
namespace ElysiumRootSystem
{
using namespace ElysiumEffectAssets;

// ------------------------------------------------------------------ the staged tree, parsed

// One ramp keyframe in normalized age. `Lo != Hi` is a range VtMB rolls per particle at spawn.
struct FKey
{
	float T = 0.f;
	float Lo = 0.f;
	float Hi = 0.f;
};

struct FNode
{
	int32 Index = 0;
	FString Name;
	bool bDraws = false;
	bool bResolved = true;
	bool bLoop = false;
	int32 Parent = INDEX_NONE;
	FString Via;

	float LifetimeS = 1.f;
	float LifetimeMinS = 1.f;
	float LifetimeMaxS = 1.f;

	bool bMoveAlign = false;
	bool bNoZTest = false;
	bool bLighting = false;

	// The node's own ramps and its reaching spawn block's ramps, keyed by the staged field name.
	TMap<FString, TArray<FKey>> Ramps;
	TMap<FString, TArray<FKey>> Spawn;
	bool bHasSpawn = false;
	float SpawnTimescale = 1.f;

	FString SpriteTexture;
	FString NormalTexture;
	FVector2D SpriteAspect = FVector2D(0.5, 0.5);

	bool bHasCollide = false;
	float Bounce = 1.f;
	float Friction = 1.f;
	float Gravity = 0.f;
	float Drag = 1.f;
	bool bSelfCollide = false;
};

TArray<FKey> ReadRamp(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	TArray<FKey> Keys;
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Object.IsValid() || !Object->TryGetArrayField(Field, Rows) || Rows == nullptr)
	{
		return Keys;
	}
	for (const TSharedPtr<FJsonValue>& Row : *Rows)
	{
		const TArray<TSharedPtr<FJsonValue>>* Triple = nullptr;
		if (!Row.IsValid() || !Row->TryGetArray(Triple) || Triple == nullptr || Triple->Num() < 3)
		{
			continue;
		}
		FKey Key;
		Key.T = static_cast<float>((*Triple)[0]->AsNumber());
		Key.Lo = static_cast<float>((*Triple)[1]->AsNumber());
		Key.Hi = static_cast<float>((*Triple)[2]->AsNumber());
		Keys.Add(Key);
	}
	return Keys;
}

// The value a scalar write takes off a ramp: the midpoint of the first keyframe, which is what the
// runtime's family pins already read (`ElysiumEffectActor.cpp` RampScalar). The whole curve is the
// ramp modules' business; a scalar module input can only hold one number.
float RampScalar(const TMap<FString, TArray<FKey>>& Ramps, const TCHAR* Field, float Default)
{
	const TArray<FKey>* Keys = Ramps.Find(Field);
	return (Keys && Keys->Num() > 0) ? 0.5f * ((*Keys)[0].Lo + (*Keys)[0].Hi) : Default;
}

FVector2f RampRange(const TMap<FString, TArray<FKey>>& Ramps, const TCHAR* Field, float Default)
{
	const TArray<FKey>* Keys = Ramps.Find(Field);
	return (Keys && Keys->Num() > 0) ? FVector2f((*Keys)[0].Lo, (*Keys)[0].Hi)
	                                 : FVector2f(Default, Default);
}

// A sprite block's `texture` (the texture lane's `T_` install path) and half-extent aspect.
void ReadSprite(const TSharedPtr<FJsonObject>& Node, const TCHAR* Field, FString& OutTexture,
	FVector2D& OutAspect)
{
	const TSharedPtr<FJsonObject>* Image = nullptr;
	if (!Node->TryGetObjectField(Field, Image) || Image == nullptr || !Image->IsValid())
	{
		return;
	}
	(*Image)->TryGetStringField(TEXT("texture"), OutTexture);
	const TArray<TSharedPtr<FJsonValue>>* Aspect = nullptr;
	if ((*Image)->TryGetArrayField(TEXT("aspect"), Aspect) && Aspect && Aspect->Num() >= 2)
	{
		OutAspect = FVector2D((*Aspect)[0]->AsNumber(), (*Aspect)[1]->AsNumber());
	}
}

// The node ramps the generator reads. The full 37-slot table is the runtime's; the generator writes
// the scalars a module input can hold and leaves the curves to the ramp modules.
const TCHAR* const GNodeRamps[] =
{
	TEXT("size_cm"), TEXT("width"), TEXT("height"), TEXT("rotation_deg"),
	TEXT("red"), TEXT("green"), TEXT("blue"), TEXT("color"), TEXT("mask"), TEXT("refract"),
	TEXT("radius_speed_cm_s"), TEXT("theta_speed_deg_s"), TEXT("phi_speed_deg_s"),
	TEXT("x_speed_cm_s"), TEXT("y_speed_cm_s"), TEXT("z_speed_cm_s"),
	TEXT("elevation_speed_cm_s"), TEXT("parent_speed"),
};

const TCHAR* const GSpawnRamps[] =
{
	TEXT("rate"), TEXT("burst"), TEXT("radius_cm"), TEXT("theta_deg"), TEXT("phi_deg"),
	TEXT("x_cm"), TEXT("y_cm"), TEXT("z_cm"), TEXT("elevation_cm"), TEXT("rotation_deg"),
};

bool ParseTree(const FString& TreeJson, TArray<FNode>& OutNodes, FString& OutName, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TreeJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("the staged tree is not a JSON object");
		return false;
	}
	Root->TryGetStringField(TEXT("name"), OutName);
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!Root->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr)
	{
		OutError = TEXT("the staged tree carries no nodes[]");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Nodes)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr)
		{
			continue;
		}
		const TSharedPtr<FJsonObject>& Json = *Object;
		FNode Node;
		Node.Index = OutNodes.Num();
		Json->TryGetNumberField(TEXT("index"), Node.Index);
		Json->TryGetStringField(TEXT("name"), Node.Name);
		Json->TryGetBoolField(TEXT("draws"), Node.bDraws);
		Json->TryGetBoolField(TEXT("resolved"), Node.bResolved);
		Json->TryGetBoolField(TEXT("loop"), Node.bLoop);
		Json->TryGetStringField(TEXT("via"), Node.Via);
		Json->TryGetBoolField(TEXT("movealign"), Node.bMoveAlign);
		Json->TryGetBoolField(TEXT("no_z_test"), Node.bNoZTest);
		Json->TryGetBoolField(TEXT("lighting"), Node.bLighting);
		// `parent` is null on the root, which TryGetNumberField leaves alone -- INDEX_NONE stands.
		Json->TryGetNumberField(TEXT("parent"), Node.Parent);
		double Number = 0.0;
		if (Json->TryGetNumberField(TEXT("lifetime_s"), Number)) { Node.LifetimeS = static_cast<float>(Number); }
		if (Json->TryGetNumberField(TEXT("lifetime_min_s"), Number)) { Node.LifetimeMinS = static_cast<float>(Number); }
		if (Json->TryGetNumberField(TEXT("lifetime_max_s"), Number)) { Node.LifetimeMaxS = static_cast<float>(Number); }

		for (const TCHAR* Field : GNodeRamps)
		{
			TArray<FKey> Keys = ReadRamp(Json, Field);
			if (Keys.Num() > 0)
			{
				Node.Ramps.Add(Field, MoveTemp(Keys));
			}
		}
		const TSharedPtr<FJsonObject>* Spawn = nullptr;
		if (Json->TryGetObjectField(TEXT("spawn"), Spawn) && Spawn && Spawn->IsValid())
		{
			Node.bHasSpawn = true;
			for (const TCHAR* Field : GSpawnRamps)
			{
				TArray<FKey> Keys = ReadRamp(*Spawn, Field);
				if (Keys.Num() > 0)
				{
					Node.Spawn.Add(Field, MoveTemp(Keys));
				}
			}
			if ((*Spawn)->TryGetNumberField(TEXT("timescale"), Number))
			{
				Node.SpawnTimescale = static_cast<float>(Number);
			}
		}
		ReadSprite(Json, TEXT("sprite"), Node.SpriteTexture, Node.SpriteAspect);
		FVector2D UnusedAspect(0.5, 0.5);
		ReadSprite(Json, TEXT("normal"), Node.NormalTexture, UnusedAspect);

		const TSharedPtr<FJsonObject>* Collide = nullptr;
		if (Json->TryGetObjectField(TEXT("collide"), Collide) && Collide && Collide->IsValid())
		{
			Node.bHasCollide = true;
			if ((*Collide)->TryGetNumberField(TEXT("bounce"), Number)) { Node.Bounce = static_cast<float>(Number); }
			if ((*Collide)->TryGetNumberField(TEXT("friction"), Number)) { Node.Friction = static_cast<float>(Number); }
			if ((*Collide)->TryGetNumberField(TEXT("gravity"), Number)) { Node.Gravity = static_cast<float>(Number); }
			if ((*Collide)->TryGetNumberField(TEXT("drag"), Number)) { Node.Drag = static_cast<float>(Number); }
			(*Collide)->TryGetBoolField(TEXT("self"), Node.bSelfCollide);
		}
		OutNodes.Add(MoveTemp(Node));
	}
	return OutNodes.Num() > 0;
}

// The nearest *drawing* ancestor, or INDEX_NONE when the leaf hangs off non-drawing wrappers all
// the way to the root. This is the edge that becomes an attribute reader inside the asset -- the
// 80 drawing-parent edges the whole corpus has.
int32 DrawingParentOf(const TArray<FNode>& Nodes, const FNode& Node)
{
	int32 Cursor = Node.Parent;
	int32 Guard = 0;
	while (Nodes.IsValidIndex(Cursor) && Guard++ < 64)
	{
		if (Nodes[Cursor].bDraws)
		{
			return Cursor;
		}
		Cursor = Nodes[Cursor].Parent;
	}
	return INDEX_NONE;
}

// `via: collide` anywhere from the leaf up to its drawing parent: VtMB's collide-spawned wrappers
// pass the trigger down to the leaf that draws. The same rule as the runtime's SpawnsOnCollision.
bool SpawnsOnCollision(const TArray<FNode>& Nodes, const FNode& Node)
{
	if (Node.Via == TEXT("collide"))
	{
		return true;
	}
	int32 Cursor = Node.Parent;
	int32 Guard = 0;
	while (Nodes.IsValidIndex(Cursor) && Cursor > 0 && Guard++ < 64)
	{
		if (Nodes[Cursor].bDraws)
		{
			return false;
		}
		if (Nodes[Cursor].Via == TEXT("collide"))
		{
			return true;
		}
		Cursor = Nodes[Cursor].Parent;
	}
	return false;
}

// The non-drawing wrapper (or node 0) whose lifetime and loop flag are the clock a leaf runs on.
const FNode* WrapperOf(const TArray<FNode>& Nodes, const FNode& Node)
{
	return Nodes.IsValidIndex(Node.Parent) ? &Nodes[Node.Parent] : nullptr;
}

FName EmitterNameFor(const FString& Raw, int32 Ordinal)
{
	FString Clean = Raw.IsEmpty() ? FString::Printf(TEXT("leaf%d"), Ordinal) : Raw;
	Clean.ReplaceInline(TEXT("/"), TEXT("_"));
	Clean.ReplaceInline(TEXT("."), TEXT("_"));
	Clean.ReplaceInline(TEXT(" "), TEXT("_"));
	return FName(*Clean);
}

// ------------------------------------------------------------------ writing one module input

// Every write is attempted and never fatal. The context accumulates errors in one flat array, so a
// write's own failures are exactly the entries it appended: snapshot the count, write, and move
// anything new into the result's `Skipped` list. That is what lets one generator run against both
// the stock Fountain template (where the Elysium modules do not exist) and the authored base
// emitter, and report the difference instead of dying on it.
void WriteInput(
	FNiagaraExternalEditContext& Context,
	FElysiumRootSystemResult& Result,
	UNiagaraSystem* System,
	const FName Emitter,
	const TCHAR* Script,
	const TCHAR* Module,
	const TCHAR* Input,
	const FNiagaraExt_StackInputValue& Value)
{
	const int32 Before = Context.Errors.Num();
	FNiagaraExt_StackItemReference Reference(System, Emitter, FName(Script), FName(Module));
	Reference.InputNameStack.Add(FName(Input));
	UNiagaraExternalEditUtilities::SetStackInputData(Reference, Value, Context);
	for (int32 Index = Context.Errors.Num() - 1; Index >= Before; --Index)
	{
		Result.Skipped.Add(FString::Printf(TEXT("%s/%s/%s/%s: %s"),
			*Emitter.ToString(), Script, Module, Input, *Context.Errors[Index].ToString()));
		Context.Errors.RemoveAt(Index);
	}
}

template<typename TValue>
FNiagaraExt_StackInputValue LocalValue(const FNiagaraTypeDefinition& Type, const TValue& Value)
{
	FNiagaraExt_StackInputValue Out;
	// The local-literal branch of InitStackInputFromValue memcpys the instanced struct's memory into
	// the input, gated on the script struct matching the input's own Niagara type exactly.
	Out.InitializeAs(Type.GetScriptStruct(), reinterpret_cast<const uint8*>(&Value));
	return Out;
}

FNiagaraExt_StackInputValue FloatValue(float Value)
{
	FNiagaraFloat Wrapped;
	Wrapped.Value = Value;
	return LocalValue(FNiagaraTypeDefinition::GetFloatDef(), Wrapped);
}

FNiagaraExt_StackInputValue IntValue(int32 Value)
{
	FNiagaraInt32 Wrapped;
	Wrapped.Value = Value;
	return LocalValue(FNiagaraTypeDefinition::GetIntDef(), Wrapped);
}

FNiagaraExt_StackInputValue BoolValue(bool Value)
{
	// Niagara's `true` is INDEX_NONE, not 1 (`NiagaraTypes.h` BoolValues); SetValue is the only
	// spelling that gets that right.
	FNiagaraBool Wrapped;
	Wrapped.SetValue(Value);
	return LocalValue(FNiagaraTypeDefinition::GetBoolDef(), Wrapped);
}

FNiagaraExt_StackInputValue Vec2Value(const FVector2f& Value)
{
	return LocalValue(FNiagaraTypeDefinition::GetVec2Def(), Value);
}

FNiagaraExt_StackInputValue Vec3Value(const FVector3f& Value)
{
	return LocalValue(FNiagaraTypeDefinition::GetVec3Def(), Value);
}

FNiagaraExt_StackInputValue ColorValue(const FLinearColor& Value)
{
	return LocalValue(FNiagaraTypeDefinition::GetColorDef(), Value);
}

// A data-interface input, set from a JSON property blob. The default property provider is
// FJsonObjectConverter::JsonObjectToUStruct, so nested structs and enum-by-name both work with no
// toolset plugin loaded; the DI's class comes from the input's own type, not from here.
FNiagaraExt_StackInputValue DataInterfaceValue(const FString& PropertyValues)
{
	FNiagaraExt_StackInputValue Out;
	FNiagaraExt_StackInputData_DataInterface& Data =
		Out.InitializeAs<FNiagaraExt_StackInputData_DataInterface>();
	Data.PropertyValues = PropertyValues;
	return Out;
}

// ------------------------------------------------------------------ one drawing node's emitter

// The base emitter's named module inputs (`generator_design.md` -> "The base-emitter input
// contract"), plus the stock fallbacks so a Fountain-based spike is not inert. Anything the base
// does not expose lands in `Result.Skipped`.
void ConfigureLeaf(
	UNiagaraSystem* System,
	const FName Emitter,
	const TArray<FNode>& Nodes,
	const FNode& Node,
	FNiagaraExternalEditContext& Context,
	FElysiumRootSystemResult& Result)
{
	const float Rate = RampScalar(Node.Spawn, TEXT("rate"), 0.f);
	const int32 Burst = FMath::RoundToInt(RampScalar(Node.Spawn, TEXT("burst"), 0.f));
	const FNode* Wrapper = WrapperOf(Nodes, Node);

	// --- Emitter Update: the clock and the spawn block.
	WriteInput(Context, Result, System, Emitter, TEXT("EmitterUpdateScript"),
		TEXT("NM_VtMBRootClock"), TEXT("Root Lifetime"),
		FloatValue(Wrapper && Wrapper->LifetimeS > 0.f ? Wrapper->LifetimeS : 1.f));
	WriteInput(Context, Result, System, Emitter, TEXT("EmitterUpdateScript"),
		TEXT("NM_VtMBRootClock"), TEXT("Root Loop"),
		BoolValue(Wrapper ? Wrapper->bLoop : true));
	WriteInput(Context, Result, System, Emitter, TEXT("EmitterUpdateScript"),
		TEXT("NM_VtMBSpawn"), TEXT("Rate"), FloatValue(Rate));
	WriteInput(Context, Result, System, Emitter, TEXT("EmitterUpdateScript"),
		TEXT("NM_VtMBSpawn"), TEXT("Burst"), IntValue(Burst));
	WriteInput(Context, Result, System, Emitter, TEXT("EmitterUpdateScript"),
		TEXT("NM_VtMBSpawn"), TEXT("Timescale"), FloatValue(Node.SpawnTimescale));

	// The stock fallback: a Fountain-shaped emitter drives its rate from this module.
	WriteInput(Context, Result, System, Emitter, TEXT("EmitterUpdateScript"),
		TEXT("SpawnRate"), TEXT("SpawnRate"), FloatValue(Rate));
	if (Burst > 0)
	{
		if (UNiagaraScript* BurstModule = LoadObject<UNiagaraScript>(nullptr,
			TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous")))
		{
			FNiagaraExt_StackItemReference Location(
				System, Emitter, FName(TEXT("EmitterUpdateScript")));
			FNiagaraExt_ModuleTopology Topology;
			const int32 Before = Context.Errors.Num();
			UNiagaraExternalEditUtilities::AddModule(Location, BurstModule, Topology, Context);
			for (int32 Index = Context.Errors.Num() - 1; Index >= Before; --Index)
			{
				Result.Skipped.Add(FString::Printf(TEXT("%s/add SpawnBurst_Instantaneous: %s"),
					*Emitter.ToString(), *Context.Errors[Index].ToString()));
				Context.Errors.RemoveAt(Index);
			}
			WriteInput(Context, Result, System, Emitter, TEXT("EmitterUpdateScript"),
				TEXT("SpawnBurst_Instantaneous"), TEXT("Spawn Count"), IntValue(Burst));
		}
	}

	// --- Particle Spawn: the leaf's own shape.
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBLeaf"), TEXT("Lifetime Min"), FloatValue(Node.LifetimeMinS));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBLeaf"), TEXT("Lifetime Max"), FloatValue(Node.LifetimeMaxS));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBLeaf"), TEXT("Size Cm"),
		FloatValue(RampScalar(Node.Ramps, TEXT("size_cm"), 1.f)));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBLeaf"), TEXT("Sprite Aspect"),
		Vec2Value(FVector2f(static_cast<float>(Node.SpriteAspect.X),
			static_cast<float>(Node.SpriteAspect.Y))));
	// `red`/`green`/`blue` are already 0..1 on the staged row; `mask` is the alpha ramp.
	const FLinearColor Tint(
		RampScalar(Node.Ramps, TEXT("red"), 1.f),
		RampScalar(Node.Ramps, TEXT("green"), 1.f),
		RampScalar(Node.Ramps, TEXT("blue"), 1.f),
		RampScalar(Node.Ramps, TEXT("mask"), 1.f));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBLeaf"), TEXT("Color"), ColorValue(Tint));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBLeaf"), TEXT("Rotation Deg"),
		Vec2Value(RampRange(Node.Ramps, TEXT("rotation_deg"), 0.f)));

	// The spherical spawn frame VtMB places a particle in.
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBSpawnPlace"), TEXT("Radius Cm"),
		Vec2Value(RampRange(Node.Spawn, TEXT("radius_cm"), 0.f)));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBSpawnPlace"), TEXT("Theta Deg"),
		Vec2Value(RampRange(Node.Spawn, TEXT("theta_deg"), 0.f)));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBSpawnPlace"), TEXT("Phi Deg"),
		Vec2Value(RampRange(Node.Spawn, TEXT("phi_deg"), 0.f)));
	const FVector3f Offset(
		RampScalar(Node.Spawn, TEXT("x_cm"), 0.f),
		RampScalar(Node.Spawn, TEXT("y_cm"), 0.f),
		RampScalar(Node.Spawn, TEXT("z_cm"), 0.f) + RampScalar(Node.Spawn, TEXT("elevation_cm"), 0.f));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBSpawnPlace"), TEXT("Offset Cm"), Vec3Value(Offset));

	// Motion. VtMB's `elevation` is the axial member of the spherical frame and maps onto +Z.
	const FVector3f Linear(
		RampScalar(Node.Ramps, TEXT("x_speed_cm_s"), 0.f),
		RampScalar(Node.Ramps, TEXT("y_speed_cm_s"), 0.f),
		RampScalar(Node.Ramps, TEXT("z_speed_cm_s"), 0.f));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBMotion"), TEXT("Linear Speed Cm S"), Vec3Value(Linear));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBMotion"), TEXT("Elevation Speed Cm S"),
		Vec2Value(RampRange(Node.Ramps, TEXT("elevation_speed_cm_s"), 0.f)));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("NM_VtMBMotion"), TEXT("Radial Speed Cm S"),
		Vec2Value(RampRange(Node.Ramps, TEXT("radius_speed_cm_s"), 0.f)));

	// The stock fallback, so a Fountain-shaped emitter still takes the leaf's lifetime and size.
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("InitializeParticle"), TEXT("Lifetime"),
		FloatValue(FMath::Max(Node.LifetimeS, KINDA_SMALL_NUMBER)));
	const float Size = RampScalar(Node.Ramps, TEXT("size_cm"), 1.f);
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("InitializeParticle"), TEXT("Sprite Size"),
		Vec2Value(FVector2f(Size * 2.f * static_cast<float>(Node.SpriteAspect.X),
			Size * 2.f * static_cast<float>(Node.SpriteAspect.Y))));
	WriteInput(Context, Result, System, Emitter, TEXT("ParticleSpawnScript"),
		TEXT("InitializeParticle"), TEXT("Color"), ColorValue(Tint));

	// --- Particle Update: collision.
	if (Node.bHasCollide)
	{
		WriteInput(Context, Result, System, Emitter, TEXT("ParticleUpdateScript"),
			TEXT("NM_VtMBCollide"), TEXT("Bounce"), FloatValue(Node.Bounce));
		WriteInput(Context, Result, System, Emitter, TEXT("ParticleUpdateScript"),
			TEXT("NM_VtMBCollide"), TEXT("Friction"), FloatValue(Node.Friction));
		WriteInput(Context, Result, System, Emitter, TEXT("ParticleUpdateScript"),
			TEXT("NM_VtMBCollide"), TEXT("Gravity"), FloatValue(Node.Gravity));
		WriteInput(Context, Result, System, Emitter, TEXT("ParticleUpdateScript"),
			TEXT("NM_VtMBCollide"), TEXT("Drag"), FloatValue(Node.Drag));
		WriteInput(Context, Result, System, Emitter, TEXT("ParticleUpdateScript"),
			TEXT("NM_VtMBCollide"), TEXT("Self"), BoolValue(Node.bSelfCollide));
	}

	// TODO(ramps): `Elysium.RampSize` / `Elysium.RampColor` / `Elysium.RampMask` /
	// `Elysium.RampRefract` on `NM_VtMBRamps`, ParticleUpdateScript. Each is a Vector-array data
	// interface fed the node's keyframes, written as a DataInterfaceValue() whose PropertyValues
	// carries UNiagaraDataInterfaceArrayFloat3's own array field. Held until `E_VtMBLeaf` declares
	// the module, because the array field name is the module author's choice, not the engine's.
}

// One emitter's sprite renderer: the material child the leaf's flags pick, the sprite as a renderer
// material-parameter binding, and the alignment `movealign` asks for.
void ConfigureRenderer(
	UNiagaraSystem* System,
	const FName Emitter,
	const FNiagaraExt_EmitterTopology& Topology,
	const FNode& Node,
	FNiagaraExternalEditContext& Context,
	FElysiumRootSystemResult& Result)
{
	if (Topology.Renderers.Num() == 0)
	{
		Result.Skipped.Add(FString::Printf(TEXT("%s: the base emitter has no renderer"),
			*Emitter.ToString()));
		return;
	}
	// 5.4: the material child by the flags -- the same chain the runtime walks.
	const TCHAR* MaterialPath = Node.bNoZTest ? MaterialNoZ
		: !Node.NormalTexture.IsEmpty() ? MaterialRefract
		: Node.bLighting ? MaterialLit
		: MaterialFloor;
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
	if (!Material)
	{
		Result.Skipped.Add(FString::Printf(TEXT("%s: material '%s' does not load"),
			*Emitter.ToString(), MaterialPath));
	}

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	if (Material)
	{
		Properties->SetStringField(TEXT("Material"), FString::Printf(TEXT("%s'%s'"),
			*Material->GetClass()->GetPathName(), *Material->GetPathName()));
	}
	// `movealign` is VtMB's "point the card along the motion"; everything else draws camera-facing.
	Properties->SetStringField(TEXT("Alignment"),
		Node.bMoveAlign ? TEXT("VelocityAligned") : TEXT("Unaligned"));
	Properties->SetStringField(TEXT("FacingMode"), TEXT("FaceCamera"));

	// The sprite rides the renderer's own material-parameter binding rather than a per-slot user
	// texture: FNiagaraRendererMaterialTextureParameter holds a UTexture directly, so the four
	// MI_Particle* children stay shared and no per-sprite material instance has to be authored.
	if (!Node.SpriteTexture.IsEmpty())
	{
		if (UTexture* Sprite = LoadObject<UTexture>(nullptr, *Node.SpriteTexture))
		{
			TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
			Binding->SetStringField(TEXT("MaterialParameterName"), TEXT("BaseTexture"));
			Binding->SetStringField(TEXT("Texture"), FString::Printf(TEXT("%s'%s'"),
				*Sprite->GetClass()->GetPathName(), *Sprite->GetPathName()));
			TArray<TSharedPtr<FJsonValue>> Textures;
			Textures.Add(MakeShared<FJsonValueObject>(Binding));
			TSharedPtr<FJsonObject> Parameters = MakeShared<FJsonObject>();
			Parameters->SetArrayField(TEXT("TextureParameters"), Textures);
			Properties->SetObjectField(TEXT("MaterialParameters"), Parameters);
		}
		else
		{
			Result.Skipped.Add(FString::Printf(TEXT("%s: sprite '%s' does not load"),
				*Emitter.ToString(), *Node.SpriteTexture));
		}
	}

	FNiagaraExt_RendererData Data;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Data.PropertyValues);
	FJsonSerializer::Serialize(Properties.ToSharedRef(), Writer);

	FNiagaraExt_StackItemReference RendererRef(System, Emitter);
	RendererRef.RendererIndex = Topology.Renderers[0].RendererIndex;
	const int32 Before = Context.Errors.Num();
	UNiagaraExternalEditUtilities::SetRendererData(RendererRef, Data, Context);
	for (int32 Index = Context.Errors.Num() - 1; Index >= Before; --Index)
	{
		Result.Skipped.Add(FString::Printf(TEXT("%s/renderer: %s"),
			*Emitter.ToString(), *Context.Errors[Index].ToString()));
		Context.Errors.RemoveAt(Index);
	}
}
}   // namespace ElysiumRootSystem
#endif

UNiagaraSystem* UElysiumParticleAssetBuilder::BuildRootSystem(
	const FString& AssetName,
	const FString& PackagePath,
	UNiagaraEmitter* BaseEmitter,
	const FString& TreeJson,
	FElysiumRootSystemResult& OutResult)
{
	OutResult = FElysiumRootSystemResult();
#if WITH_EDITOR
	using namespace ElysiumRootSystem;

	if (!BaseEmitter)
	{
		OutResult.Errors.Add(TEXT("no base emitter"));
		return nullptr;
	}
	TArray<FNode> Nodes;
	FString TreeName;
	FString ParseError;
	if (!ParseTree(TreeJson, Nodes, TreeName, ParseError))
	{
		OutResult.Errors.Add(ParseError);
		return nullptr;
	}
	TArray<const FNode*> Leaves;
	for (const FNode& Node : Nodes)
	{
		if (Node.bDraws && Node.bResolved)
		{
			Leaves.Add(&Node);
		}
	}
	if (Leaves.Num() == 0)
	{
		OutResult.Errors.Add(FString::Printf(
			TEXT("'%s' has no resolved drawing node"), *TreeName));
		return nullptr;
	}

	UNiagaraSystem* System = nullptr;
	{
		FNiagaraExternalEditContext Create;
		System = UNiagaraExternalEditUtilities::CreateNiagaraSystem(
			AssetName, PackagePath, nullptr, Create);
		for (const FText& Error : Create.Errors)
		{
			OutResult.Errors.Add(Error.ToString());
		}
		if (!System || Create.HasErrors())
		{
			return nullptr;
		}
	}

	// One context, one view model, for the whole system. Forty-two call sites each building their
	// own is what put the editor at 13 GB in R7.3. The context must also die before the compile
	// below: its view model is built with bCompileForEdit = false, and compiling under a live VM is
	// how the floor ended up reading a stale compile state.
	{
		FNiagaraExternalEditContext Context(System);
		TArray<FName> Names;
		Names.Reserve(Leaves.Num());
		TArray<FNiagaraExt_EmitterTopology> Topologies;
		Topologies.Reserve(Leaves.Num());
		for (int32 Index = 0; Index < Leaves.Num(); ++Index)
		{
			FNiagaraExt_EmitterTopology Topology;
			UNiagaraExternalEditUtilities::AddEmitter(
				BaseEmitter, EmitterNameFor(Leaves[Index]->Name, Index), Topology, Context);
			// The name asked for is not necessarily the name given: FNiagaraEmitterHandle::SetName
			// uniquifies, and a VtMB spawn graph can legitimately name one particle twice.
			Names.Add(Topology.EmitterName);
			Topologies.Add(MoveTemp(Topology));
		}
		for (const FText& Error : Context.Errors)
		{
			OutResult.Errors.Add(Error.ToString());
		}
		Context.Errors.Reset();
		if (OutResult.Errors.Num() > 0)
		{
			return nullptr;
		}

		// A VtMB emitter is placed at an entity and its offsets are authored around that origin, so
		// the whole system rides its component rather than sitting in world space. This is what
		// lets a bone-attached effect follow the bone with its leaves intact.
		for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
			{
				Data->bLocalSpace = true;
			}
		}

		for (int32 Index = 0; Index < Leaves.Num(); ++Index)
		{
			ConfigureLeaf(System, Names[Index], Nodes, *Leaves[Index], Context, OutResult);
			ConfigureRenderer(System, Names[Index], Topologies[Index], *Leaves[Index],
				Context, OutResult);
		}

		// The drawing-parent edges. Inside one asset the parent is a compile-time emitter name, so
		// the per-instance reader problem the slotted floor could not solve does not arise:
		// FNiagaraDataInterfaceEmitterBinding::ResolveHandle needs the DI's outer to be the system,
		// which a module input satisfies and a user parameter never does.
		if (UNiagaraScript* ReaderModule = LoadObject<UNiagaraScript>(nullptr,
			TEXT("/Niagara/Modules/AttributeReader/SpawnParticlesFromOtherEmitter")
			TEXT(".SpawnParticlesFromOtherEmitter")))
		{
			for (int32 Index = 0; Index < Leaves.Num(); ++Index)
			{
				const int32 ParentNode = DrawingParentOf(Nodes, *Leaves[Index]);
				if (ParentNode == INDEX_NONE)
				{
					continue;
				}
				if (SpawnsOnCollision(Nodes, *Leaves[Index]))
				{
					// TODO(collide-spawn): the trigger is a collision event, not an attribute
					// reader -- /Niagara/Modules/Collision/Collision on the parent's Particle
					// Update stack plus a collision-event writer, and this emitter's Emitter Update
					// taking /Niagara/Modules/Emitter/SpawnByTrigger. Named here so the A2
					// (WaterDrops_Timer) pass has exactly one place to fill in.
					OutResult.Skipped.Add(FString::Printf(
						TEXT("%s: collide->spawn under '%s' is not wired yet"),
						*Names[Index].ToString(), *Nodes[ParentNode].Name));
					continue;
				}
				int32 ParentEmitter = INDEX_NONE;
				for (int32 Other = 0; Other < Leaves.Num(); ++Other)
				{
					if (Leaves[Other]->Index == Nodes[ParentNode].Index)
					{
						ParentEmitter = Other;
						break;
					}
				}
				if (ParentEmitter == INDEX_NONE)
				{
					continue;
				}
				FNiagaraExt_StackItemReference Location(
					System, Names[Index], FName(TEXT("EmitterUpdateScript")));
				FNiagaraExt_ModuleTopology Added;
				const int32 Before = Context.Errors.Num();
				UNiagaraExternalEditUtilities::AddModule(Location, ReaderModule, Added, Context);
				for (int32 Error = Context.Errors.Num() - 1; Error >= Before; --Error)
				{
					OutResult.Skipped.Add(FString::Printf(TEXT("%s/add reader: %s"),
						*Names[Index].ToString(), *Context.Errors[Error].ToString()));
					Context.Errors.RemoveAt(Error);
				}
				const FString Binding = FString::Printf(
					TEXT("{\"EmitterBinding\":{\"BindingMode\":\"Other\",\"EmitterName\":\"%s\"}}"),
					*Names[ParentEmitter].ToString());
				WriteInput(Context, OutResult, System, Names[Index], TEXT("EmitterUpdateScript"),
					TEXT("SpawnParticlesFromOtherEmitter"), TEXT("Attribute Reader"),
					DataInterfaceValue(Binding));
			}
		}
		else
		{
			OutResult.Skipped.Add(
				TEXT("SpawnParticlesFromOtherEmitter does not load; no child edge is wired"));
		}

		for (const FText& Error : Context.Errors)
		{
			OutResult.Errors.Add(Error.ToString());
		}
		if (OutResult.Errors.Num() > 0)
		{
			return nullptr;
		}
		for (const FName Name : Names)
		{
			OutResult.EmitterNames.Add(Name.ToString());
		}
	}

	const FElysiumRootSystemResult Verified = FinishAndVerify(System);
	OutResult.bReady = Verified.bReady;
	OutResult.bInherited = Verified.bInherited;
	OutResult.EmitterCount = Verified.EmitterCount;
	OutResult.ParticleCounts = Verified.ParticleCounts;
	OutResult.Errors.Append(Verified.Errors);
	System->MarkPackageDirty();
	return System;
#else
	OutResult.Errors.Add(TEXT("the generated particle lane is editor-only"));
	return nullptr;
#endif
}

FElysiumRootSystemResult UElysiumParticleAssetBuilder::FinishAndVerify(UNiagaraSystem* System)
{
	FElysiumRootSystemResult Result;
#if WITH_EDITOR
	if (!System)
	{
		Result.Errors.Add(TEXT("particle system is null"));
		return Result;
	}
	System->RequestCompile(false);
	System->WaitForCompilationComplete(false, false);
	FAssetCompilingManager::Get().FinishAllCompilation();

	Result.EmitterCount = System->GetEmitterHandles().Num();
	Result.bInherited = Result.EmitterCount > 0;
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		Result.EmitterNames.Add(Handle.GetName().ToString());
		Result.ParticleCounts.Add(INDEX_NONE);
		if (!Handle.GetIsEnabled())
		{
			Result.Errors.Add(FString::Printf(TEXT("emitter '%s' is disabled"),
				*Handle.GetName().ToString()));
		}
		const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data)
		{
			Result.Errors.Add(FString::Printf(TEXT("emitter '%s' has no data"),
				*Handle.GetName().ToString()));
			Result.bInherited = false;
			continue;
		}
		if (!Data->bLocalSpace)
		{
			Result.Errors.Add(FString::Printf(TEXT("emitter '%s' is not local-space"),
				*Handle.GetName().ToString()));
		}
		// UNiagaraSystem::AddEmitterHandle always asks for a parent; an emitter asset marked
		// bIsInheritable = false -- which every stock template is -- has it stripped on the way in,
		// yielding a copy. Only an inherited child follows later edits to the base.
		if (Data->GetParent().Emitter == nullptr)
		{
			Result.bInherited = false;
		}
	}
	// IsReadyToRun is live in an uncooked build, but IsReadyToRunInternal short-circuits false when
	// FApp::CanEverRender() is false, so the host commandlet needs -AllowCommandletRendering.
	Result.bReady = System->IsReadyToRun();
	if (!Result.bReady)
	{
		Result.Errors.Add(FApp::CanEverRender()
			? TEXT("IsReadyToRun() is false after an explicit compile")
			: TEXT("IsReadyToRun() is false because the process cannot render "
				"(-AllowCommandletRendering is missing)"));
	}
#else
	Result.Errors.Add(TEXT("the generated particle lane is editor-only"));
#endif
	return Result;
}

FElysiumRootSystemResult UElysiumParticleAssetBuilder::ProbeSystem(
	UNiagaraSystem* System, int32 TickCount, float DeltaSeconds)
{
	FElysiumRootSystemResult Result;
#if WITH_EDITOR
	if (!System)
	{
		Result.Errors.Add(TEXT("particle system is null"));
		return Result;
	}
	Result.EmitterCount = System->GetEmitterHandles().Num();
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		Result.EmitterNames.Add(Handle.GetName().ToString());
		Result.ParticleCounts.Add(INDEX_NONE);
	}
	Result.bReady = System->IsReadyToRun();
	if (!Result.bReady)
	{
		Result.Errors.Add(TEXT("not ready to run; not probed"));
		return Result;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result.Errors.Add(TEXT("no editor world to probe in"));
		return Result;
	}

	UNiagaraComponent* Component = NewObject<UNiagaraComponent>(World->GetWorldSettings());
	if (!Component)
	{
		Result.Errors.Add(TEXT("could not create a Niagara component"));
		return Result;
	}
	Component->bAutoActivate = false;
	Component->bWaitForCompilationOnActivate = true;
	Component->SetAsset(System);
	Component->RegisterComponentWithWorld(World);
	Component->Activate(true);

	// A headless editor runs no tick loop, so the world is ticked by hand. Niagara's own tick hangs
	// off FWorldDelegates::OnWorldTickStart / OnWorldPostActorTick, both of which UWorld::Tick
	// broadcasts, so this is the path a running game takes -- but it is not a path the bake has
	// exercised before, and anything it refuses is reported rather than fataled.
	for (int32 Frame = 0; Frame < FMath::Max(TickCount, 1); ++Frame)
	{
		World->Tick(LEVELTICK_All, FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER));
	}

	if (FNiagaraSystemInstanceControllerPtr Controller = Component->GetSystemInstanceController())
	{
		if (FNiagaraSystemInstance* Instance = Controller->GetSystemInstance_Unsafe())
		{
			int32 Index = 0;
			for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
			{
				if (Result.ParticleCounts.IsValidIndex(Index))
				{
					Result.ParticleCounts[Index] = Emitter->IsDisabled()
						? INDEX_NONE : Emitter->GetNumParticles();
				}
				++Index;
			}
		}
		else
		{
			Result.Errors.Add(TEXT("the component has no system instance after ticking"));
		}
	}
	else
	{
		Result.Errors.Add(TEXT("the component never built a system instance controller"));
	}

	Component->Deactivate();
	Component->DestroyComponent();
#else
	Result.Errors.Add(TEXT("the generated particle lane is editor-only"));
#endif
	return Result;
}
