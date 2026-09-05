// AElysiumMapActor's effects domain (R7.3, `docs/architecture/effects-architecture.md` §5): on a
// `MapsOnV2Models` map the env_particle / func_particle publish drives the bake-placed
// AElysiumEffectActor by entity index; the three Valve classes drive their own actors the same
// way; and `SpawnParticleRoot` stands a transient root for a code producer. The legacy per-map
// component path stays in `ElysiumMapActorWeather.cpp`, byte for byte, for every unlisted map.

#include "ElysiumMapActor.h"

#include "ElysiumEffectActor.h"
#include "ElysiumEffectFamilies.h"
#include "ElysiumEffectValveActors.h"
#include "ElysiumEntity.h"         // the parent's attach body and skeletal body
#include "ElysiumEntityDefs.h"     // the parent's authored origin -- a tree attach's offset
#include "ElysiumEntityWorld.h"    // FindByName / Resolve
#include "Map/ElysiumMapLog.h"
#include "Visual/ElysiumMapVisuals.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"

namespace
{
	bool ModeWantsParent(int32 AttachType)
	{
		switch (AttachType)
		{
		case 1: case 2: case 3: case 6: case 9: case 17:
			return true;
		default:
			return false;
		}
	}
}

bool AElysiumMapActor::ResolveEffectAttachment(FElysiumEntity* ParentEntity, int32 AttachType,
	const FString& Bone, const FVector& AuthoredLocationCm, int32 EntityIndex,
	const FString& Label, FElysiumEffectAttachment& Out)
{
	Out = FElysiumEffectAttachment();
	if (!ModeWantsParent(AttachType))
	{
		return false;
	}
	USceneComponent* ParentBody = ParentEntity ? ParentEntity->GetAttachBody() : nullptr;
	if (!ParentBody)
	{
		WarnEmitterOnce(FString::Printf(TEXT("effect-parent|%d"), EntityIndex),
			[&]
			{
				return FString::Printf(
					TEXT("effect %d ('%s') attach mode %d cannot find parent '%s' body on map '%s'; using its origin"),
					EntityIndex, *Label, AttachType,
					ParentEntity ? *ParentEntity->ParentName : TEXT("<none>"), *MapName);
			});
		return false;
	}
	Out.ParentBody = ParentBody;
	Out.Skeletal = ParentEntity->GetSkeletalBody();
	if (AttachType == 1 || AttachType == 3)
	{
		// Retail parents these at map setup, before a cinematic moves the actor; the placed actor
		// reconstructs the authored offset against the parent's live position.
		Out.bHasWorldLocation = true;
		Out.WorldLocationCm = AuthoredLocationCm;
		if (ParentEntity->Def)
		{
			Out.WorldLocationCm += ParentEntity->Origin - ParentEntity->Def->Origin;
		}
		return true;
	}
	// VtMB bone names carry spaces (`Bip01 Neck`) and survive the glTF export unchanged, so the
	// authored name is used verbatim. A body that has not got the bone falls back to its root;
	// the numbered `attach_point` (authored on 0 rows) has no socket mapping yet.
	if (!Bone.IsEmpty())
	{
		const FName Socket(*Bone);
		if (ParentBody->DoesSocketExist(Socket))
		{
			Out.Socket = Socket;
		}
		else
		{
			WarnEmitterOnce(FString::Printf(TEXT("effect-bone|%d|%s"), EntityIndex, *Bone.ToLower()),
				[&]
				{
					return FString::Printf(
						TEXT("effect %d ('%s') cannot find bone '%s' on its parent; using the body root"),
						EntityIndex, *Label, *Bone);
				});
		}
	}
	return true;
}

void AElysiumMapActor::ApplyEmitterToPlacedActor(const FElysiumWeatherEmitterState& Emitter)
{
	AElysiumEffectActor* Actor = Visuals ? Visuals->FindEffectActor(Emitter.Entity.Index) : nullptr;
	if (!Actor)
	{
		// An unresolved root places no actor (VtMB removes the entity); the input is still
		// accepted, said once.
		WarnEmitterOnce(FString::Printf(TEXT("effect-actor|%d"), Emitter.Entity.Index),
			[&]
			{
				return FString::Printf(
					TEXT("effect %d ('%s') has no placed actor on map '%s' (unresolved root, or the level predates the effects bake)"),
					Emitter.Entity.Index, *Emitter.ParticleDefinition, *MapName);
			});
		return;
	}
	if (Emitter.bDead)
	{
		Actor->Drive(Emitter, nullptr);
		EffectEmitterStates.Remove(Emitter.Entity.Index);
		return;
	}
	// The parent is resolved on every publish that may re-attach (the actor decides which); it
	// is looked up here rather than at spawn because the parent may only now exist.
	FElysiumEffectAttachment Attachment;
	FElysiumEntity* Parent = (EntityWorld && !Emitter.ParentName.IsEmpty())
		? EntityWorld->FindByName(Emitter.ParentName) : nullptr;
	const bool bAttached = ResolveEffectAttachment(Parent, Emitter.AttachType, Emitter.AttachBone,
		Emitter.LocationCm, Emitter.Entity.Index, Emitter.ParticleDefinition, Attachment);
	// The substrate's box and size scalar outrank the bake's copy: both are the same derivation,
	// and the live one is what `SetRateScale` multiplies.
	if (Emitter.BrushBoundsCm.IsValid)
	{
		Actor->BoundsCm = Emitter.BrushBoundsCm;
	}
	Actor->VolumeScale = Emitter.VolumeScale;
	Actor->Drive(Emitter, bAttached ? &Attachment : nullptr);
	EffectEmitterStates.Add(Emitter.Entity.Index, Emitter);
}

void AElysiumMapActor::ApplyDust(const FElysiumDustState& Dust)
{
	AElysiumDustActor* Actor = Visuals
		? Cast<AElysiumDustActor>(Visuals->FindEffectActor(Dust.Entity.Index)) : nullptr;
	if (!Actor)
	{
		WarnEmitterOnce(FString::Printf(TEXT("dust-actor|%d"), Dust.Entity.Index),
			[&]
			{
				return FString::Printf(TEXT("func_dustmotes %d has no placed actor on map '%s'"),
					Dust.Entity.Index, *MapName);
			});
		return;
	}
	Actor->Drive(Dust);
}

void AElysiumMapActor::ApplySteam(const FElysiumSteamState& Steam)
{
	AElysiumSteamActor* Actor = Visuals
		? Cast<AElysiumSteamActor>(Visuals->FindEffectActor(Steam.Entity.Index)) : nullptr;
	if (!Actor)
	{
		WarnEmitterOnce(FString::Printf(TEXT("steam-actor|%d"), Steam.Entity.Index),
			[&]
			{
				return FString::Printf(TEXT("env_steam %d has no placed actor on map '%s'"),
					Steam.Entity.Index, *MapName);
			});
		return;
	}
	Actor->Drive(Steam);
}

void AElysiumMapActor::ApplyBeam(const FElysiumBeamState& Beam)
{
	AElysiumBeamActor* Actor = Visuals
		? Cast<AElysiumBeamActor>(Visuals->FindEffectActor(Beam.Entity.Index)) : nullptr;
	if (!Actor)
	{
		WarnEmitterOnce(FString::Printf(TEXT("beam-actor|%d"), Beam.Entity.Index),
			[&]
			{
				return FString::Printf(TEXT("env_beam %d has no placed actor on map '%s'"),
					Beam.Entity.Index, *MapName);
			});
		return;
	}
	Actor->Drive(Beam);
}

FElysiumEffectHandle AElysiumMapActor::SpawnParticleRoot(const FString& Root,
	const FElysiumEntityHandle* Parent, int32 AttachMode, FName AttachName, int32 AttachPoint,
	const FVector& OriginCm, const FRotator& Angles)
{
	UWorld* World = GetWorld();
	if (!World || Root.IsEmpty())
	{
		return FElysiumEffectHandle();
	}
	if (!ParticleTrees)
	{
		ParticleTrees = LoadObject<UElysiumParticleTrees>(nullptr, ElysiumEffectAssets::Trees);
	}
	// The folded key, or the bare root name a producer's data spells.
	const FString Key = Root.StartsWith(TEXT("vtmb:particle:"))
		? Root : TEXT("vtmb:particle:") + Root.Replace(TEXT("\\"), TEXT("/")).ToLower();
	const FElysiumParticleTree* Tree = ParticleTrees ? ParticleTrees->Trees.Find(Key) : nullptr;
	// R7.4 (G7): a by-root spawn whose root the effects lane generated needs no staged tree.
	// `DA_ElysiumParticleTrees` is a code-producer table no bake writes today, and a generated
	// `NS_<root>` carries its leaves as emitters -- `AElysiumEffectActor::WriteTree` skips the slot
	// layout entirely for one (`bGeneratedSystem`). So the missing tree is only fatal when there is
	// no generated system either; then the actor would stand the empty floor and draw nothing.
	const bool bGenerated = AElysiumEffectActor::HasGeneratedSystem(Root);
	if (!Tree && !bGenerated)
	{
		WarnEmitterOnce(TEXT("root|") + Root.ToLower(),
			[&]
			{
				return FString::Printf(
					TEXT("SpawnParticleRoot '%s': no tree in %s and no generated NS_<root> "
						"on map '%s'"),
					*Root, ElysiumEffectAssets::Trees, *MapName);
			});
		return FElysiumEffectHandle();
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform Placement(Angles, OriginCm);
	AElysiumEffectActor* Actor = World->SpawnActorDeferred<AElysiumEffectActor>(
		AElysiumEffectActor::StaticClass(), Placement, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Actor)
	{
		return FElysiumEffectHandle();
	}
	Actor->Classname = FName(TEXT("env_particle"));
	Actor->Root = Tree ? Tree->Root : Key;
	Actor->RootName = (Tree && !Tree->Name.IsEmpty()) ? Tree->Name : Root;
	Actor->Tree = Tree ? *Tree : FElysiumParticleTree();
	Actor->AttachType = AttachMode;
	Actor->AttachBone = AttachName.IsNone() ? FString() : AttachName.ToString();
	Actor->AttachPoint = AttachPoint;
	Actor->FinishSpawning(Placement);
	Actor->ResolveFamily(Visuals ? Visuals->GetEffectFamilies() : nullptr);

	FElysiumEntity* ParentEntity = (Parent && EntityWorld) ? EntityWorld->Resolve(*Parent) : nullptr;
	FElysiumEffectAttachment Attachment;
	const bool bAttached = ResolveEffectAttachment(ParentEntity, AttachMode, Actor->AttachBone,
		OriginCm, INDEX_NONE, Actor->RootName, Attachment);
	Actor->SetAttachType(AttachMode, bAttached ? Attachment : FElysiumEffectAttachment());
	Actor->SetRate(1.f);
	Actor->TurnOn();

	const int32 Id = ++NextEffectId;
	TransientEffects.Add(Id, Actor);
	return FElysiumEffectHandle{ Id };
}

void AElysiumMapActor::StopParticleRoot(const FElysiumEffectHandle& Handle)
{
	if (const TWeakObjectPtr<AElysiumEffectActor>* Found = TransientEffects.Find(Handle.Id))
	{
		if (AElysiumEffectActor* Actor = Found->Get())
		{
			Actor->TurnOff();
		}
	}
}

void AElysiumMapActor::KillParticleRoot(const FElysiumEffectHandle& Handle)
{
	TWeakObjectPtr<AElysiumEffectActor> Found;
	if (TransientEffects.RemoveAndCopyValue(Handle.Id, Found))
	{
		if (AElysiumEffectActor* Actor = Found.Get())
		{
			Actor->Kill();
			Actor->Destroy();
		}
	}
}
