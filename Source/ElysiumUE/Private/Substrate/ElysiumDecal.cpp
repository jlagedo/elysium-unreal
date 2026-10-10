// infodecal -- `CDecal` (`walks/L0-r011.md`, story L0.effects_world.runtime-decal): `CDecal::Spawn`
// 0x1023a750, the unnamed projector `FUN_1023aa30` 0x1023aa30 with its filter `0x1023acc0`, the
// `texture` key `CDecal::vfunc110` 0x1023adb0, the named-decal `CDecal::Use` 0x1023a7d0 and the
// `SUB_Remove` 0x101c0b10 think it arms. The engine half of every submission (`VEngineServer014` slot
// 63 `StaticDecal`, the `CTEBSPDecal` temp entity) is `IElysiumEmbodiment::SubmitStaticDecal`, the
// world's decal subsystem drawing the texture on the surface -- a visual peripheral.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumDecal, Log, All);

namespace
{
	// `_DAT_10454110`, 5.0f: the half-extent of the box round the decal origin, Source inches.
	constexpr float GDecalBoxHalfInches = 5.0f;
	constexpr double GCmPerInch = 2.54;
	// The two trace masks, verbatim.
	constexpr int32 GProjectorMask = 0x200400b;
	constexpr int32 GUseMask = 0x400b;
	// The function words the named path writes: `m_pfnThink` <- `0x1000572c` (a jump to `FUN_101c0b60`,
	// a bare `ret`), `m_pfnUse` <- `0x1000c888` (a jump to `CDecal::Use`), and Use's own
	// `m_pfnThink` <- `0x10015b68` (a jump to `SUB_Remove`).
	constexpr int32 GThinkNoop = 0x1000572c;
	constexpr int32 GUseFn = 0x1000c888;
	constexpr int32 GThinkSubRemove = 0x10015b68;
	// `_DAT_104493d0`, the double 0.1: `m_flNextThink = curtime + 0.1`, added on the x87 stack and
	// rounded once by the `FSTP dword` store.
	constexpr double GUseRemoveDelay = 0.1;

	// Source inches on Source axes <-> this port's cm on the Unreal axes (Y reflected).
	FVector ToInches(const FVector& Cm)
	{
		return FVector(Cm.X / GCmPerInch, -Cm.Y / GCmPerInch, Cm.Z / GCmPerInch);
	}
	FVector FromInches(const FVector& In)
	{
		return FVector(In.X * GCmPerInch, -In.Y * GCmPerInch, In.Z * GCmPerInch);
	}

	FString Vec(const FVector& V)
	{
		// `%g` of a negative zero prints `-0`; the word is 0.
		return FString::Printf(TEXT("%g,%g,%g"), V.X == 0.0 ? 0.0 : V.X, V.Y == 0.0 ? 0.0 : V.Y, V.Z == 0.0 ? 0.0 : V.Z);
	}

	// `VectorITransform(p, m)` `0x10138130` over `m_rgflCoordinateFrame` (+0x130, the 3x4 row-major
	// frame `CalcAbsolutePosition` 0x100b1ac0 builds from the absolute pose with `AngleMatrix`
	// 0x1024ddf0's products): `d = p - (m[3], m[7], m[11])`, `local.x = d.x*m[0] + d.y*m[4] + d.z*m[8]`,
	// `local.y` with `m[1], m[5], m[9]`, `local.z` with `m[2], m[6], m[10]`. Inches in, inches out.
	FVector VectorITransform(const FVector& PointInches, const FVector& OriginInches, const FVector& AnglesDeg)
	{
		constexpr float DegToRad = 0.017453292f;   // _DAT_1044eb08
		const float YawRad = static_cast<float>(AnglesDeg.Y) * DegToRad;
		const float Cy = static_cast<float>(FMath::Cos(static_cast<double>(YawRad)));
		const float Sy = static_cast<float>(FMath::Sin(static_cast<double>(YawRad)));
		const float PitchRad = static_cast<float>(AnglesDeg.X) * DegToRad;
		const float Cp = static_cast<float>(FMath::Cos(static_cast<double>(PitchRad)));
		const float Sp = static_cast<float>(FMath::Sin(static_cast<double>(PitchRad)));
		const float RollRad = static_cast<float>(AnglesDeg.Z) * DegToRad;
		const float Cr = static_cast<float>(FMath::Cos(static_cast<double>(RollRad)));
		const float Sr = static_cast<float>(FMath::Sin(static_cast<double>(RollRad)));
		float M[12];
		M[0] = Cp * Cy;                  M[4] = Cp * Sy;                  M[8] = -Sp;
		M[1] = Cy * Sr * Sp - Cr * Sy;   M[5] = Cr * Cy + Sr * Sp * Sy;   M[9] = Sr * Cp;
		M[2] = Sr * Sy + Cy * Cr * Sp;   M[6] = Cr * Sp * Sy - Sr * Cy;   M[10] = Cr * Cp;
		M[3] = static_cast<float>(OriginInches.X); M[7] = static_cast<float>(OriginInches.Y); M[11] = static_cast<float>(OriginInches.Z);
		const float Dx = static_cast<float>(PointInches.X) - M[3];
		const float Dy = static_cast<float>(PointInches.Y) - M[7];
		const float Dz = static_cast<float>(PointInches.Z) - M[11];
		return FVector(Dx * M[0] + Dy * M[4] + Dz * M[8], Dx * M[1] + Dy * M[5] + Dz * M[9], Dx * M[2] + Dy * M[6] + Dz * M[10]);
	}
}

class FElysiumDecal final : public FElysiumEntity
{
public:
	// The `texture` key (the material stem, `decals/stains/rusta`). Retail keeps no string: the key
	// handler writes the index below and nothing else; this port keeps the name the submission draws.
	FString Texture;
	// `m_nTexture` (+0x450, datamap fieldType 4): `VEngineServer014` slot 16 `PrecacheDecal`'s index,
	// written by the `texture` key (`CDecal::vfunc110` 0x1023adb0). Not written by the constructor
	// (`FUN_1023a6e0`); entity memory is `calloc`, so a decal with no key keeps 0 -- a VALID index.
	int32 TextureIndex = 0;
	// `m_pfnThink` (+0x118) / `m_pfnUse` (+0x1f0) as the function words retail stores (the port's
	// think identity is `ThinkCallback`; these are the retail values a record reads).
	int32 ThinkFn = 0;
	int32 UseFn = 0;

	virtual void Spawn() override;
	virtual void Think() override;
	virtual void Use(const FElysiumEntityHandle& Activator) override;

private:
	// `CDecal::vfunc110` 0x1023adb0, the `texture` key: `__strcmpi(key, "texture")` -> slot 16
	// `PrecacheDecal(value, 1)` -> `m_nTexture`; a negative answer warns "Can't find decal %s"
	// (`0x105c3fe8`) -- unreachable in practice, slot 16 Host_Errors instead. Any other key goes to the
	// base `KeyValue` 0x1009e430. The port applies keys before Spawn, so the write lands at Spawn's
	// entry; `m_nTexture` is read for the first time in Spawn, so the order is observably the same.
	void KeyValueTexture();
	// `FUN_1023aa30` 0x1023aa30, the unnamed projector.
	void Project();
	// `SUB_Remove` 0x101c0b10 then `UTIL_Remove` 0x101cd940.
	void SubRemove();
	// `UTIL_Remove` 0x101cd940 alone (`FUN_101cd940`).
	void UtilRemove();
	// Which entity a retail trace with this mask hit: the world answer, or the nearest character the
	// same query met in front of it (`CTraceFilterSimple` passes NPCs and the player; the engine folds
	// them into `m_pEnt`, this seam lists them beside the world answer).
	static FElysiumEntityHandle FoldHit(const FElysiumRetailTraceResult& Result, float& OutFraction, bool& bOutStartSolid);
	// `IndexOfEdict(m_pEnt->+0x2e0)` (`VEngineServer014` slot 35): 0 for the world (edict 0), else the
	// entity's index. The port's handle index is its edict number (a baked map's def 0 is `worldspawn`).
	int32 EdictIndex(const FElysiumEntity* Entity) const;
};

static TUniquePtr<FElysiumEntity> MakeDecal() { return MakeUnique<FElysiumDecal>(); }

static FElysiumClassRegistrar GRegInfoDecal(
	TEXT("infodecal"), ElysiumBaseClassName(), &MakeDecal,
	[](FElysiumClassDesc& D)
	{
		// `CBaseEntity::Use` (slot 173, 0x100a4e70) dispatches `m_pfnUse`; `CDecal::Spawn` sets it on the
		// named path only, and `Use` below answers nothing while the word is 0.
		D.Input(TEXT("Use"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumDecal&>(E).Use(A.Activator); });
		ElysiumAddClassField(D, TEXT("texture"), &FElysiumDecal::Texture, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_nTexture"), &FElysiumDecal::TextureIndex, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_pfnThink"), &FElysiumDecal::ThinkFn, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_pfnUse"), &FElysiumDecal::UseFn, EElysiumField::None);
	});

void FElysiumDecal::KeyValueTexture()
{
	if (Texture.IsEmpty())
	{
		return;   // no `texture` key: the handler never ran, `m_nTexture` keeps its calloc 0
	}
	TextureIndex = World ? World->PrecacheDecal(Texture) : 1;
	if (TextureIndex < 0)
	{
		UE_LOG(LogElysiumDecal, Warning, TEXT("Can't find decal %s"), *Texture);
	}
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("decal_key"), TEXT("CDecal::vfunc110"), 0x1023adb0u, TEXT("write"),
			FString::Printf(TEXT("key=texture value=%s m_nTexture=%d"), *Texture, TextureIndex));
	}
}

void FElysiumDecal::Spawn()
{
	// `CDecal::Spawn` 0x1023a750 (`walks/L0-r011.md`). It calls neither `CBaseEntity::Spawn`,
	// `CPointEntity::Spawn` nor `Precache`.
	KeyValueTexture();
	// `gpGlobals->+0x30`, the multiplayer byte (unpinned, `docs/vtmb/multiplayer.md`): this port is the
	// single-player game, so the byte is 0 and arm 2 never fires; carried as the word it reads.
	constexpr int32 MultiplayerByte = 0;
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("decal_spawn"), TEXT("CDecal::Spawn"), 0x1023a750u, TEXT("entry"),
			FString::Printf(TEXT("texture=%d spawnflags=0x%x mp_byte=%d path=%s"), TextureIndex, SpawnFlags, MultiplayerByte,
				TargetName.IsEmpty() ? TEXT("unnamed") : TEXT("named")));
	}
	// 1. `m_nTexture < 0` -> `FUN_101cd940` (UTIL_Remove, no SUB_Remove) and return.
	if (TextureIndex < 0)
	{
		if (World)
		{
			World->EmitRetailSite(*this, TEXT("decal_spawn"), TEXT("CDecal::Spawn"), 0x1023a750u, TEXT("remove"),
				FString::Printf(TEXT("arm=texture m_nTexture=%d"), TextureIndex));
		}
		UtilRemove();
		return;
	}
	// 2. `gpGlobals->+0x30 != 0 && (m_spawnflags & 0x800)` (`TEST AH,0x8` on +0x204) -> remove.
	if (MultiplayerByte != 0 && (SpawnFlags & 0x800) != 0)
	{
		if (World)
		{
			World->EmitRetailSite(*this, TEXT("decal_spawn"), TEXT("CDecal::Spawn"), 0x1023a750u, TEXT("remove"),
				FString::Printf(TEXT("arm=multiplayer spawnflags=0x%x"), SpawnFlags));
		}
		UtilRemove();
		return;
	}
	// 3. `m_iName == 0` -> tail jump to `FUN_1023aa30` (the projector, which then self-removes).
	if (TargetName.IsEmpty())
	{
		Project();
		return;
	}
	// 4. Named: `ThinkSet(this, 0x1000572c, 0.0, NULL)` -- with a NULL context `ThinkSet` 0x100ac4e0
	//    writes `m_pfnThink` only, the time is ignored, nothing is scheduled; then `m_pfnUse` <-
	//    `0x1000c888` (`CDecal::Use`). The decal does nothing until used.
	ThinkFn = GThinkNoop;
	UseFn = GUseFn;
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("decal_spawn"), TEXT("CDecal::Spawn"), 0x1023a750u, TEXT("arm"),
			FString::Printf(TEXT("m_pfnThink=0x%x m_pfnUse=0x%x m_flNextThink=untouched"), ThinkFn, UseFn));
	}
}

FElysiumEntityHandle FElysiumDecal::FoldHit(const FElysiumRetailTraceResult& Result, float& OutFraction, bool& bOutStartSolid)
{
	FElysiumEntityHandle Hit = Result.HitEntity;
	OutFraction = Result.Fraction;
	bOutStartSolid = Result.bStartSolid;
	for (const FElysiumRetailTraceCharacter& Character : Result.Characters)
	{
		if (Character.Entity.IsSet() && (Character.bStartSolid || Character.Fraction < OutFraction))
		{
			Hit = Character.Entity;
			OutFraction = Character.bStartSolid ? 0.0f : Character.Fraction;
			bOutStartSolid = Character.bStartSolid;
			break;   // nearest first
		}
	}
	// A static-world hit has no entity handle; retail's `m_pEnt` is then the world entity (edict 0).
	return Hit;
}

int32 FElysiumDecal::EdictIndex(const FElysiumEntity* Entity) const
{
	if (Entity == nullptr || (Entity->Def != nullptr && Entity->Def->Classname.Equals(TEXT("worldspawn"), ESearchCase::IgnoreCase)))
	{
		return 0;
	}
	return Entity->Handle.Index;
}

void FElysiumDecal::Project()
{
	// `FUN_1023aa30` 0x1023aa30 (`walks/L0-r011.md`), arms in retail order.
	// 1. `CDecal::CTraceFilterValidForDecal(this, 0)` (`0x1023ac90`; `ShouldHitEntity` 0x1023acc0): the
	//    six-entry classname table, then `CTraceFilterSimple::ShouldHitEntity` 0x101d31c0.
	FElysiumRetailTrace Trace;
	Trace.Filter = EElysiumRetailTraceFilter::ValidForDecal;
	Trace.Ignore.Add(Handle);
	// 2. `o = GetAbsOrigin()` (slot 217).
	const FVector OriginCm = GetAbsOrigin();
	// 3. `max = o + 5.0`, `min = o - 5.0` on each SOURCE axis (`_DAT_10454110`): the corners are Source
	//    words, so they are formed in inches and the line is traced between their Unreal positions.
	const FVector OriginIn = ToInches(OriginCm);
	const FVector MinIn = OriginIn - FVector(GDecalBoxHalfInches);
	const FVector MaxIn = OriginIn + FVector(GDecalBoxHalfInches);
	// 4. `Ray_t::Init(min, max)` `0x1004f7a0`: `start = min`, `delta = max - min`, extents and offset
	//    zero, `IsRay = 1`, `IsSwept = |delta|^2 != 0`: a LINE along the box diagonal, not a box.
	Trace.StartCm = FromInches(MinIn);
	Trace.EndCm = FromInches(MaxIn);
	// 5. `EngineTraceServer003::TraceRay(ray, 0x200400b, filter, trace)`.
	Trace.RetailMask = GProjectorMask;
	FElysiumRetailTraceResult Result;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment)
	{
		Embodiment->TraceRetail(Trace, Result);
	}
	else
	{
		Result.StartPosCm = Trace.StartCm;
		Result.EndPosCm = Trace.EndCm;
	}
	// 6. The debug line: `svc->vfunc1() == 0 && svc->+0x2c != 0` (`DAT_10738964`, a ConVar whose
	//    identity is unrecovered) -> `0x10146570` (both endpoints lifted 0.1f) -> `0x10142e90` (sent to
	//    the host player within sqrt(9.0e7) units, lifetime -1). Developer output with no state write;
	//    the ConVar is not modelled here (`ElysiumNpcBaseConditions2.cpp` lists the same gate as
	//    uncarried). UNRECOVERED: which ConVar.
	// 7. `m_pEnt` (trace+0x4c): `TEST AX,AX` -- a 16-bit test on the pointer; a pointer whose low word
	//    is zero reads as no hit (the port has no raw pointers: a set handle is a hit).
	float HitFraction = 1.0f;
	bool bHitStartSolid = false;
	const FElysiumEntityHandle HitHandle = FoldHit(Result, HitFraction, bHitStartSolid);
	const FElysiumEntity* Hit = World && HitHandle.IsSet() ? World->Resolve(HitHandle) : nullptr;
	int32 Eidx = 0;
	FString HitModel = TEXT("0");
	FVector SubmitInches = OriginIn;
	if (Hit != nullptr)
	{
		// 7a. `eidx = IndexOfEdict(m_pEnt->+0x2e0)` (slot 35); 0 (the world) ends the path with
		//     `eidx = 0, model = 0` and the world origin.
		Eidx = EdictIndex(Hit);
		if (Eidx != 0)
		{
			// 7b. `model = m_pEnt->GetModelIndex()` (slot 8). The engine's precache index is replaced by
			//     the model name in this port (`CBaseEntity::GetModelIndex` is engine-replaced).
			HitModel = Hit->Model.IsEmpty() ? TEXT("0") : Hit->Model;
			// 7c. `m_iEFlags & 0x800` -> `CalcAbsolutePosition()` (slot 98): the pose getters below do it.
			// 7d. `local = VectorITransform(GetAbsOrigin(this), m_pEnt->m_rgflCoordinateFrame)`.
			SubmitInches = VectorITransform(OriginIn, ToInches(Hit->GetAbsOrigin()), Hit->GetAbsAngles());
		}
	}
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("decal_project"), TEXT("FUN_1023aa30"), 0x1023aa30u, TEXT("trace"),
			FString::Printf(TEXT("min=%s max=%s mask=0x%x fraction=%g startsolid=%d hit=%s eidx=%d model=%s local=%s"),
				*Vec(MinIn), *Vec(MaxIn), GProjectorMask, HitFraction, bHitStartSolid ? 1 : 0,
				Hit ? *HitHandle.ToString() : TEXT("none"), Eidx, *HitModel, *Vec(SubmitInches)));
	}
	// 8. `VEngineServer014` slot 63 `StaticDecal(&point, m_nTexture, eidx, model)` (`engine.dll
	//    0x20109740`: the signon buffer gets `0x15 0x0d`, the point, the decal index, the entity index
	//    and -- entity != 0 -- the model index). Submitted on EVERY path: a miss and a world hit carry
	//    the world origin with `(0, 0)`.
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("decal_project"), TEXT("FUN_1023aa30"), 0x1023aa30u, TEXT("submit"),
			FString::Printf(TEXT("fn=CVEngineServer::StaticDecal slot=63 point=%s texture=%d eidx=%d model=%s"),
				*Vec(SubmitInches), TextureIndex, Eidx, *HitModel));
	}
	if (Embodiment)
	{
		FElysiumStaticDecalSubmit Submit;
		Submit.WorldOriginCm = OriginCm;
		Submit.Texture = Texture;
		Submit.Entity = Eidx != 0 ? HitHandle : FElysiumEntityHandle::Invalid();
		Submit.NormalHint = Result.HitEntity.IsSet() || Result.Fraction < 1.0f ? Result.Normal : FVector::ZeroVector;
		Embodiment->SubmitStaticDecal(Submit);
	}
	// 9. `SUB_Remove(this)` 0x101c0b10, then `FUN_101cd940`.
	SubRemove();
}

void FElysiumDecal::Use(const FElysiumEntityHandle& Activator)
{
	// `CBaseEntity::Use` (slot 173) runs `m_pfnUse` when set; `CDecal::Spawn` sets it on the named path.
	if (UseFn != GUseFn)
	{
		return;
	}
	// `CDecal::Use` `FUN_1023a7d0` 0x1023a7d0 (`walks/L0-r011.md`). It does NOT call StaticDecal.
	// 1. `o = GetOrigin()` (slot 220, the LOCAL origin), `max = o + 5`, `min = o - 5`, a line (`0x1004f7a0`).
	const FVector OriginCm = GetOrigin();
	const FVector OriginIn = ToInches(OriginCm);
	FElysiumRetailTrace Trace;
	Trace.StartCm = FromInches(OriginIn - FVector(GDecalBoxHalfInches));
	Trace.EndCm = FromInches(OriginIn + FVector(GDecalBoxHalfInches));
	// 2. `CTraceFilterSimple(this, 0)` (`0x101d3190`), mask `0x400b`.
	Trace.Filter = EElysiumRetailTraceFilter::Simple;
	Trace.Ignore.Add(Handle);
	Trace.RetailMask = GUseMask;
	FElysiumRetailTraceResult Result;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment)
	{
		Embodiment->TraceRetail(Trace, Result);
	}
	// 3. The debug line, gated as in the projector (not modelled; see `Project`).
	// 4. `eidx = IndexOfEdict(m_pEnt->+0x2e0)` with NO test on `m_pEnt`: a trace that hits nothing
	//    dereferences NULL in retail -- a crash. This port answers the world (0) and says so.
	float HitFraction = 1.0f;
	bool bHitStartSolid = false;
	const FElysiumEntityHandle HitHandle = FoldHit(Result, HitFraction, bHitStartSolid);
	const FElysiumEntity* Hit = World && HitHandle.IsSet() ? World->Resolve(HitHandle) : nullptr;
	const bool bMiss = Hit == nullptr && HitFraction >= 1.0f && !bHitStartSolid;
	if (bMiss)
	{
		UE_LOG(LogElysiumDecal, Warning, TEXT("%s: CDecal::Use trace hit nothing; retail dereferences a NULL m_pEnt here (a crash). Answering edict 0."),
			*DebugString());
	}
	const int32 Eidx = EdictIndex(Hit);
	// 5. `CBroadcastRecipientFilter` (`0x1019ce00`, `FUN_1019cff0`: players 1..maxClients).
	// 6. `te->BSPDecal(filter, 0.0, &GetOrigin(), eidx, m_nTexture)` (`CTempEntsSystem` slot 16,
	//    0x10058ce0 -> `0x1005f550`, the `CTEBSPDecal` singleton `0x106c144c`): the entity-LOCAL origin,
	//    no model index, no local transform.
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("decal_use"), TEXT("CDecal::Use"), 0x1023a7d0u, TEXT("broadcast"),
			FString::Printf(TEXT("origin=%s mask=0x%x hit=%s eidx=%d fn=CTempEntsSystem::BSPDecal va=0x10058ce0 delay=0 texture=%d recipients=all miss_fault=%d activator=%s"),
				*Vec(OriginIn), GUseMask, Hit ? *HitHandle.ToString() : TEXT("none"), Eidx, TextureIndex, bMiss ? 1 : 0,
				Activator.IsSet() ? *Activator.ToString() : TEXT("none")));
	}
	if (Embodiment)
	{
		FElysiumStaticDecalSubmit Submit;
		Submit.WorldOriginCm = OriginCm;
		Submit.Texture = Texture;
		Submit.Entity = Eidx != 0 ? HitHandle : FElysiumEntityHandle::Invalid();
		Submit.NormalHint = Result.HitEntity.IsSet() || Result.Fraction < 1.0f ? Result.Normal : FVector::ZeroVector;
		Embodiment->SubmitStaticDecal(Submit);
	}
	// 7. `ThinkSet(this, 0x10015b68 (SUB_Remove), 0.0, NULL)` writes `m_pfnThink` only; then
	//    `m_flNextThink = curtime + 0.1` (the double `_DAT_104493d0`, one rounding at the store).
	//    Nothing guards a second Use before the think fires.
	ThinkFn = GThinkSubRemove;
	const double Now = World ? World->NowSeconds() : 0.0;
	NextThink = static_cast<float>(Now + GUseRemoveDelay);
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("decal_use"), TEXT("CDecal::Use"), 0x1023a7d0u, TEXT("arm"),
			FString::Printf(TEXT("m_pfnThink=0x%x m_flNextThink=%.4f curtime=%.4f"), ThinkFn, NextThink, Now));
	}
	// 8. The filter's CUtlVector cleanup; return.
}

void FElysiumDecal::Think()
{
	// `m_pfnThink`: `0x1000572c` is `FUN_101c0b60`, a bare `ret`; `0x10015b68` is `SUB_Remove`.
	if (ThinkFn == GThinkSubRemove)
	{
		SubRemove();
	}
}

void FElysiumDecal::SubRemove()
{
	// `CBaseEntity::SUB_Remove` 0x101c0b10 (`__fastcall`): `m_iHealth > 0` -> `m_iHealth = 0` and
	// `DevWarning(2, "SUB_Remove called on entity with health > 0\n")` (`0x1059c3a4`); then `FUN_101cd940`.
	if (Health > 0)
	{
		Health = 0;
		UE_LOG(LogElysiumDecal, Log, TEXT("SUB_Remove called on entity with health > 0"));
	}
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("decal_remove"), TEXT("CBaseEntity::SUB_Remove"), 0x101c0b10u, TEXT("entry"),
			FString::Printf(TEXT("m_iHealth=%d then fn=FUN_101cd940 va=0x101cd940"), Health));
	}
	UtilRemove();
}

void FElysiumDecal::UtilRemove()
{
	// `FUN_101cd940` 0x101cd940 -> `FUN_101cd8c0` 0x101cd8c0 (idempotent: the node's bit 0, slot 180
	// `UpdateOnRemove`, `SetName(NULL)`, then `0x100f6bb0` queues the deferred deletion). The port's
	// `Kill` is this removal (audit rows 788 / 706 file its partial words).
	UpdateOnRemove();
	Kill();
}
