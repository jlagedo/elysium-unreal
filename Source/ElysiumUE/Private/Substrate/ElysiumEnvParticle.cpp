// env_particle's retail attachment (`walks/L0-r010.md`, story L0.effects_world.particle-attachment):
// `CEnvParticle::Spawn` 0x100fb3d0, `CEnvParticle::AttachToEntity` 0x100fb110 and the attachment-index
// resolver `FUN_100fafa0` 0x100fafa0. The class body and its I/O stay in `ElysiumEnvParticle.h`.

#include "Substrate/ElysiumEnvParticle.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEnvParticle, Log, All);

void FElysiumEnvParticle::Spawn()
{
	// `CEnvParticle::Spawn` 0x100fb3d0: slot 104 `Precache` (the definition's own resolution; the
	// port's visual layer resolves the root by name), `CPointEntity::Spawn`, then:
	// `m_nAttachType < 0 || > 0x12` -> `Warning("entity %s has invalid attach type ...")` and 0.
	if (AttachType < 0 || AttachType > 0x12)
	{
		UE_LOG(LogElysiumEnvParticle, Warning, TEXT("entity %s has invalid attach type %d"), *DebugString(), AttachType);
		AttachType = 0;
	}
	// `field_0x454 >= 0` (the resolved definition; a miss `UTIL_Remove`s -- that arm is the visual
	// layer's "unresolved root places no actor" today): clamp `m_fSpawnBounds` between
	// `_DAT_104454c4` (0.0) and `_DAT_104563b0` (upper bound not decoded here; the port keeps its floor).
	ParticleDefinition = ParticleDefinition.Replace(TEXT("\\"), TEXT("/")).ToLower();
	SpawnBounds = FMath::Max(0.0f, SpawnBounds);
	RampTime = FMath::Max(0.0f, RampTime);
	RampScale = FMath::Max(0.0f, RampScale);
	RampStartScale = RampScale;
	RampTargetScale = RampScale;
	RampStartTime = World ? World->NowSeconds() : 0.0;
	RampDuration = 0.0f;
	// `m_pParent` (+0x254) resolves (serial test, non-NULL pointer) -> `FUN_100faf60(this, parent)`:
	// slot 243 `AttachToEntity(parent, m_nAttachType, m_sAttachName ? m_sAttachName : "")`.
	if (FElysiumEntity* Parent = World != nullptr ? World->Resolve(ParentHandle) : nullptr)
	{
		AttachToEntity(Parent, AttachType, AttachBone);
	}
	// `m_bActive != 0` -> `+0x488 = gpGlobals->curtime` (the activation stamp the client rebuilds on;
	// `TurnOnSerial` is this port's equivalent, bumped by `TurnOn`).
	Publish();
}

void FElysiumEnvParticle::AttachToEntity(FElysiumEntity* Parent, int32 Mode, const FString& Name)
{
	// `CEnvParticle::AttachToEntity` 0x100fb110, straight-line, in retail order.
	// 1. `SetParent(this, parent, 0)` (`0x100a0670` through thunk `0x10005650`): attach byte 0.
	SetParent(Parent, 0);
	// 2. `m_nAttachType` (+0x458) <- mode.
	AttachType = Mode;
	// 3. `m_sAttachName` (+0x45c) <- `AllocPooledString(name)` (`0x1042bff0` over the pool `0x109eed2c`):
	// 0 for an empty name, else the pooled copy. This port's word is the string itself (empty for 0);
	// the pool is a representation, not a behaviour.
	AttachBone = Name;
	// 4. slot 93 `SetMoveType(11, 0)` (`0x100aad70`): `MOVETYPE_FOLLOW` -- `PhysicsFollow` 0x10039470
	// copies the aim entity's absolute pose each tick -- and `MOVECOLLIDE_DEFAULT`.
	SetMoveType(11, 0);
	// 5. `SetAimEnt(entity(m_pParent))` (`0x1009ee80`): the handle SetParent stored, re-resolved.
	SetAimEnt(World != nullptr ? World->Resolve(ParentHandle) : nullptr);
	// 6. slot 63 `SetOrigin(0, 0, 0)` (`0x10026a10` -> slot 62): the local origin becomes the parent's.
	SetOrigin(0.0f, 0.0f, 0.0f);
	// 7. `m_nAttachPoint` (+0x460) <- `FUN_100fafa0(parent, mode, name)`: the ORIGINAL arguments.
	AttachPoint = ResolveAttachIndex(Parent, Mode, Name, *this);
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("particle_attach"), TEXT("CEnvParticle::AttachToEntity"), 0x100fb110u, TEXT("return"),
			FString::Printf(TEXT("parent=%s m_nAttachType=%d m_sAttachName=%s m_MoveType=%d m_hAimEnt=%s m_pParent=%s m_nAttachPoint=%d"),
				Parent ? *Parent->Handle.ToString() : TEXT("null"), AttachType, AttachBone.IsEmpty() ? TEXT("0") : *AttachBone,
				GetMoveType(), AimEnt.IsSet() ? *AimEnt.ToString() : TEXT("-1"),
				ParentHandle.IsSet() ? *ParentHandle.ToString() : TEXT("-1"), AttachPoint));
	}
	Publish();
}

int32 FElysiumEnvParticle::ResolveAttachIndex(FElysiumEntity* Parent, int32 Mode, const FString& Name,
	const FElysiumEntity& Site)
{
	// `FUN_100fafa0(parent, mode, name)` 0x100fafa0 (`walks/L0-r010.md`), arms in retail order.
	auto Return = [&](int32 Index, const TCHAR* Arm) -> int32
	{
		if (Site.World)
		{
			Site.World->EmitRetailSite(Site, TEXT("attach_index"), TEXT("FUN_100fafa0"), 0x100fafa0u, TEXT("return"),
				FString::Printf(TEXT("mode=%d name=%s index=%d arm=%s"), Mode, *Name, Index, Arm));
		}
		return Index;
	};
	// 1. The tree modes 1 (BoneTree) and 3 (BoneTreeWithColors): -1, the parent is not read.
	if (Mode == 1 || Mode == 3)
	{
		return Return(-1, TEXT("tree"));
	}
	// 2. The attachment modes 6 (ModelAttachment) and 17 (ModelAttachmentNoFollow):
	// `RTDynamicCast(parent, CBaseEntity -> CBaseAnimating)` -- the question slot 137 `GetBaseAnimating`
	// answers (NULL for a plain `CBaseEntity`, `this` for `CBaseAnimating` and every descendant).
	if (Mode == 6 || Mode == 17)
	{
		FElysiumEntity* Animating = Parent != nullptr ? Parent->GetBaseAnimating() : nullptr;
		if (Animating != nullptr)
		{
			// HOOK (L1 animation, `hooks.tsv:21`): `CBaseAnimating::LookupAttachment(name)` 0x10092d50 --
			// `FUN_100c6280(modelptr, name) + 1`, or 0 with no model pointer. Not recovered here; the
			// answer is retail's own no-model answer, 0, until L1 lands the body.
			if (Site.World)
			{
				Site.World->EmitRetailSite(Site, TEXT("attach_index"), TEXT("FUN_100fafa0"), 0x100fafa0u, TEXT("hook"),
					FString::Printf(TEXT("fn=CBaseAnimating::LookupAttachment va=0x10092d50 mode=%d name=%s"), Mode, *Name));
			}
			return Return(0, TEXT("lookup_attachment_hook"));
		}
		return Return(0, TEXT("not_animating"));
	}
	// 3. Any other mode > 0: `VEngineServer014` slot 21 (model type of `parent->GetModelIndex()`, slot 8)
	// must be 3 (`mod_studio`), then slot 18 `GetModelPtr`; with a header and `hdr[+0xf0] > 0` bones,
	// the bone table at `hdr + hdr[+0xf4]` (0xa0-byte records, the name at `entry + *(int*)entry`) is
	// scanned with `__strcmpi(name)`; the first match is the index, none is 0; a non-studio model or a
	// NULL header is 0. A NULL parent is dereferenced unguarded in retail (a crash); this port answers
	// 0 and says so.
	if (Mode > 0)
	{
		if (Parent == nullptr)
		{
			UE_LOG(LogElysiumEnvParticle, Warning,
				TEXT("%s: FUN_100fafa0 mode %d with a NULL parent (retail dereferences it); answering 0"), *Site.DebugString(), Mode);
			return Return(0, TEXT("null_parent"));
		}
		// The studio bone table of the parent's model: this port reaches it through the baked skeletal
		// asset's reference skeleton (the standing skeletal body, or the placed skeletal body). Its
		// bone order is the exporter's (`skeletal_stage/payload.py` `unreal_bones`: a fork-resolving
		// synthetic root can shift the indices), so the INDEX can differ from the studio one on such a
		// model; a static-reduced placed prop stands no skeletal body and answers as a non-studio
		// model would. Both are stated in the run's report.
		USkeletalMeshComponent* Skeletal = Parent->GetSkeletalBody();
		if (Skeletal == nullptr)
		{
			Skeletal = Parent->GenericModelBody;
		}
		USkeletalMesh* Mesh = Skeletal != nullptr ? Skeletal->GetSkeletalMeshAsset() : nullptr;
		if (Mesh == nullptr)
		{
			return Return(0, TEXT("not_studio"));
		}
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		if (Ref.GetRawBoneNum() <= 0)
		{
			return Return(0, TEXT("no_bones"));
		}
		for (int32 Index = 0; Index < Ref.GetRawBoneNum(); ++Index)
		{
			if (Ref.GetBoneName(Index).ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				return Return(Index, TEXT("bone"));
			}
		}
		return Return(0, TEXT("bone_missing"));
	}
	// 4. mode <= 0: `local_4` stays 0.
	return Return(0, TEXT("mode_le_0"));
}
