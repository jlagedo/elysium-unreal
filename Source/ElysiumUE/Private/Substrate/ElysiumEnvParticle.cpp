// env_particle's retail spawn and attachment (`walks/L0-r010.md`, `walks/L0-r011.md`; stories
// L0.effects_world.particle-emitter-spawn, particle-attachment, particle-rate-ramp): `CEnvParticle::Spawn`
// 0x100fb3d0, `Precache` 0x100fb540, `UTIL_Extract_FileBase` 0x101cf7c0, `SetRampTime` 0x100fba40,
// `SetRateScale` 0x100fb980, `AttachToEntity` 0x100fb110 and the attachment-index resolver `FUN_100fafa0`
// 0x100fafa0. The class body and its I/O stay in `ElysiumEnvParticle.h`.

#include "Substrate/ElysiumEnvParticle.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEnvParticle, Log, All);

FString FElysiumEnvParticle::ExtractFileBase(const FString& In)
{
	// `UTIL_Extract_FileBase(in, out, 0x104)` 0x101cf7c0 (`walks/L0-r011.md`): from the end, the
	// first `.`, `/` or `\` is found; a `.` marks the extension's start; then the last `/` or `\`
	// before it is the base's start. Case is not changed; an empty input gives an empty output.
	if (In.IsEmpty())
	{
		return FString();
	}
	const int32 Last = In.Len() - 1;
	int32 Dot = Last;
	while (Dot != 0 && In[Dot] != TEXT('.') && In[Dot] != TEXT('/') && In[Dot] != TEXT('\\'))
	{
		--Dot;
	}
	const int32 End = In[Dot] == TEXT('.') ? Dot - 1 : Last;
	int32 Start = Last;
	while (Start >= 0 && In[Start] != TEXT('/') && In[Start] != TEXT('\\'))
	{
		--Start;
	}
	++Start;   // past the separator, or 0
	return End >= Start ? In.Mid(Start, End - Start + 1) : FString();
}

void FElysiumEnvParticle::Precache()
{
	// `CEnvParticle::Precache` 0x100fb540 (slot 104): runs its body only while `m_nParticle` (+0x454)
	// `< 0`; the constructor wrote -1, so it runs on the first Spawn.
	if (ParticleIndex >= 0)
	{
		return;
	}
	// A NULL `m_sParticleDefinition` (no `particle_definition` key) writes -1 silently. The port's
	// word is a string; the empty string stands for retail's NULL (a retail `""` key is interned
	// non-NULL and reaches the engine's `*name < '!'` Host_Error -- a crash -- see `PrecacheParticle`).
	if (ParticleDefinition.IsEmpty())
	{
		ParticleIndex = -1;
	}
	else
	{
		// `UTIL_Extract_FileBase(def, buffer, 0x104)` 0x101cf7c0, then `VEngineServer014` slot 17
		// `PrecacheParticle(buffer, 1)` -> `m_nParticle`. Slot 17 never returns a negative (a failed
		// lookup is Host_Error), so the `Warning("Can't find particle %s")` (`0x105675e0`) arm below is
		// dead in practice; kept as retail wrote it.
		const FString Base = ExtractFileBase(ParticleDefinition);
		ParticleIndex = World ? World->PrecacheParticle(Base) : 1;
		if (ParticleIndex < 0)
		{
			UE_LOG(LogElysiumEnvParticle, Warning, TEXT("Can't find particle %s"), *ParticleDefinition);
		}
	}
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("particle_precache"), TEXT("CEnvParticle::Precache"), 0x100fb540u, TEXT("write"),
			FString::Printf(TEXT("def=%s base=%s m_nParticle=%d"), ParticleDefinition.IsEmpty() ? TEXT("null") : *ParticleDefinition,
				*ExtractFileBase(ParticleDefinition), ParticleIndex));
	}
}

void FElysiumEnvParticle::Spawn()
{
	// `CEnvParticle::Spawn` 0x100fb3d0 (`walks/L0-r011.md`), arms in retail order.
	const double Now = World ? World->NowSeconds() : 0.0;
	// The constructor `FUN_100fad50` wrote `+0x488 = curtime` at construction; the port constructs
	// without a world, so that stamp lands here, the same frame, before Spawn's own conditional
	// restamp (arm 4c) -- observably the constructor's value.
	ActivationTime = static_cast<float>(Now);
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("particle_spawn"), TEXT("CEnvParticle::Spawn"), 0x100fb3d0u, TEXT("entry"),
			FString::Printf(TEXT("def=%s m_nAttachType=%d m_fSpawnBounds=%g m_bActive=%d m_pParent=%s"),
				ParticleDefinition.IsEmpty() ? TEXT("null") : *ParticleDefinition, AttachType, SpawnBounds, bActive ? 1 : 0,
				ParentHandle.IsSet() ? *ParentHandle.ToString() : TEXT("-1")));
	}
	// 1. slot 104 `Precache` (`CALL [EAX+0x1a0]`) = `CEnvParticle::Precache` 0x100fb540.
	Precache();
	// 2. `CPointEntity::Spawn` 0x101c0820: `FUN_100dc480(&m_Collision, 0)` (`SetSolid(0)`, a no-op on a
	//    fresh point entity whose solid word is already 0) and `CBaseEntity::Relink` (`0x1001514a`,
	//    the partition re-insertion; the port's bodiless point entity has no partition entry).
	// 3. `m_nAttachType < 0 || > 0x12` (`TEST EAX,EAX; JL` / `CMP EAX,0x13; JL`) ->
	//    `Warning(0x105675b4 "entity %s has invalid attach type!", GetDebugName)` and 0. Before the
	//    definition test, so it fires on an entity arm 5 then removes.
	const int32 AttachIn = AttachType;
	if (AttachType < 0 || AttachType > 0x12)
	{
		UE_LOG(LogElysiumEnvParticle, Warning, TEXT("entity %s has invalid attach type!"), *DebugString());
		AttachType = 0;
	}
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("particle_spawn"), TEXT("CEnvParticle::Spawn"), 0x100fb3d0u, TEXT("attach"),
			FString::Printf(TEXT("attach_type=%d -> %d warned=%d"), AttachIn, AttachType, AttachIn != AttachType ? 1 : 0));
	}
	// The port's own ramp state (the client-side ramp retail networks `m_nRampFrame` to is
	// unrecovered); initialised from the authored `ramp_scale`, never clamped here (retail's Spawn
	// touches neither `ramp_time` nor `ramp_scale`).
	RampStartScale = RampScale;
	RampTargetScale = RampScale;
	RampStartTime = Now;
	RampDuration = 0.0f;
	// 4. `if (+0x454 >= 0)` (`TEST EAX,EAX; JGE`).
	if (ParticleIndex >= 0)
	{
		// 4a. `m_fSpawnBounds` clamp: `> 4096.0` (`_DAT_104563b0`) gives 4096.0; `< 0.0`
		//     (`_DAT_104454c4`) gives 0.0; else kept. Both compares fall to the keep arm on a NaN, so a
		//     NaN passes through (retail's own behaviour).
		const float BoundsIn = SpawnBounds;
		float Clamped = 4096.0f;
		if (SpawnBounds <= 4096.0f)
		{
			Clamped = 0.0f;
			if (0.0f <= SpawnBounds)
			{
				Clamped = SpawnBounds;
			}
		}
		if (FMath::IsNaN(SpawnBounds))
		{
			Clamped = SpawnBounds;
		}
		SpawnBounds = Clamped;
		if (World)
		{
			World->EmitRetailSite(*this, TEXT("particle_spawn"), TEXT("CEnvParticle::Spawn"), 0x100fb3d0u, TEXT("bounds"),
				FString::Printf(TEXT("bounds=%g -> %g m_nParticle=%d"), BoundsIn, SpawnBounds, ParticleIndex));
		}
		// 4b. `m_pParent` (+0x254): a set handle whose table entry (`PTR_DAT_10566458`, stride 0xc)
		//     carries its serial and a non-null pointer -> `FUN_100faf60(this, entity)` -> slot 243
		//     `AttachToEntity(parent, m_nAttachType, m_sAttachName or "")`. Retail re-reads the handle
		//     and passes NULL when the ENTITY's own serial (+4) disagrees with the table's; this port's
		//     handle table and entity serial are one word, so that arm cannot arise, and a handle that
		//     does not resolve makes no call (retail's table-check failure).
		FElysiumEntity* Parent = World != nullptr ? World->Resolve(ParentHandle) : nullptr;
		if (World)
		{
			World->EmitRetailSite(*this, TEXT("particle_spawn"), TEXT("CEnvParticle::Spawn"), 0x100fb3d0u, TEXT("parent"),
				FString::Printf(TEXT("m_pParent=%s parent=%s"), ParentHandle.IsSet() ? *ParentHandle.ToString() : TEXT("-1"),
					Parent ? TEXT("resolved") : TEXT("none")));
		}
		if (Parent != nullptr)
		{
			AttachToEntity(Parent, AttachType, AttachBone);
		}
		// 4c. `m_bActive != 0` -> `+0x488 = gpGlobals->curtime` (`m_flActivationTime`; the client
		//     rebuilds the emitter whenever it changes -- `TurnOnSerial` is this port's restart word).
		if (bActive)
		{
			ActivationTime = static_cast<float>(Now);
		}
		if (World)
		{
			World->EmitRetailSite(*this, TEXT("particle_spawn"), TEXT("CEnvParticle::Spawn"), 0x100fb3d0u, TEXT("active"),
				FString::Printf(TEXT("m_bActive=%d m_flActivationTime=%g"), bActive ? 1 : 0, ActivationTime));
		}
		// Not a retail write: the embodiment pull (the bake-placed actor reads the published state).
		Publish();
		return;
	}
	// 5. `else` (`+0x454 < 0`): `FUN_101cd940(this)` = `UTIL_Remove` -- the kill-me bit, slot 180
	//    `UpdateOnRemove`, `SetName(NULL)`, the deferred-deletion queue (`0x100f6bb0`; audit rows 788 /
	//    706 file the port's `Kill` partial against it). No SUB_Remove warning on this path. Arms 4a-4c
	//    do not run.
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("particle_spawn"), TEXT("CEnvParticle::Spawn"), 0x100fb3d0u, TEXT("remove"),
			FString::Printf(TEXT("m_nParticle=%d fn=FUN_101cd940 va=0x101cd940"), ParticleIndex));
	}
	Kill();
}

void FElysiumEnvParticle::SetRampTime(float Duration)
{
	// `FUN_100fba40` 0x100fba40 (`walks/L0-r011.md`), `__thiscall (float)`.
	// 1. `duration < 0.0` (`_DAT_104454c4`; `FCOM; TEST AH,5; JP skip`: a strict less-than, a NaN
	//    skips) -> `Warning(0x1056765c "%s ramp time set to %.2f, must be >=0", GetDebugName, duration)`
	//    and 0.0.
	if (Duration < 0.0f)
	{
		UE_LOG(LogElysiumEnvParticle, Warning, TEXT("%s ramp time set to %.2f, must be >=0"), *DebugString(), Duration);
		Duration = 0.0f;
	}
	// 2. `+0x490 = duration` (`m_fRampTime`).
	RampTime = Duration;
	// 3. `+0x494 = VEngineServer014 slot 120()` (`MOV [ESI+0x494],EAX`): the host frame counter, an
	//    integer, unconditional. `m_nRampFrame`.
	const int32 Prev = RampFrame;
	RampFrame = World ? static_cast<int32>(World->HostFrame()) : 0;
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("particle_ramp"), TEXT("FUN_100fba40"), 0x100fba40u, TEXT("write"),
			FString::Printf(TEXT("m_fRampTime=%g prev=%d m_nRampFrame=%d curtime=%.3f"), RampTime, Prev, RampFrame,
				World->NowSeconds()));
	}
}

void FElysiumEnvParticle::SetRateScale(float Rate)
{
	// `FUN_100fb980` 0x100fb980 (`walks/L0-r011.md`), `__thiscall (float)`, `RET 4`.
	// 1. `rate < 0.0` (strict) -> `Warning(0x1056762c "%s rate scale set to %.2f, must be >=0")`, 0.0.
	if (Rate < 0.0f)
	{
		UE_LOG(LogElysiumEnvParticle, Warning, TEXT("%s rate scale set to %.2f, must be >=0"), *DebugString(), Rate);
		Rate = 0.0f;
	}
	// 2. `+0x48c = rate` (`m_fRateScaleTarget`).
	RampTargetScale = Rate;
	// 3. `+0x4a1 != 0` -> `mult = DAT_107083dc->vfunc1() ? 0.0 : *(float*)(DAT_107083dc + 0x28)` and
	//    `+0x48c *= mult`. HOOK (L4, `hooks.tsv:402`; the byte's only writer is the Ghoul croucher's
	//    `FUN_1038e9c0`, and the ConVar's identity is unrecovered): no map emitter has the byte set;
	//    the arm is stated and reported, the multiplier is left at the ConVar's unread value 1.
	if (bRateScaleByConVar && World)
	{
		World->EmitRetailSite(*this, TEXT("particle_ramp"), TEXT("FUN_100fb980"), 0x100fb980u, TEXT("hook"),
			TEXT("arm=+0x4a1 convar=DAT_107083dc layer=L4"));
	}
	// 4. `+0x494 = VEngineServer014 slot 120()` -- the same frame stamp SetRampTime writes.
	const int32 Prev = RampFrame;
	RampFrame = World ? static_cast<int32>(World->HostFrame()) : 0;
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("particle_ramp"), TEXT("FUN_100fb980"), 0x100fb980u, TEXT("write"),
			FString::Printf(TEXT("m_fRateScaleTarget=%g prev=%d m_nRampFrame=%d curtime=%.3f"), RampTargetScale, Prev, RampFrame,
				World->NowSeconds()));
	}
	// The port's own linear ramp toward the target over `m_fRampTime` (the client consumer of
	// `m_nRampFrame` is unrecovered; this is the visual-only stand-in that drives the emitter rate).
	const double Now = World ? World->NowSeconds() : 0.0;
	RampScale = RateAt(Now);
	RampStartScale = RampScale;
	RampStartTime = Now;
	RampDuration = FMath::Max(0.0f, RampTime);
	if (RampDuration <= 0.0f)
	{
		RampScale = RampTargetScale;
	}
	else
	{
		NextThink = static_cast<float>(Now);
	}
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
