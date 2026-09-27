// `CBaseEntity`'s hand-written slot bodies and the helpers they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Declarations are generated in
// `ElysiumEntitySlots.inl` (a slot body) or in `ElysiumEntitySlotBodies.inl`.

#include "ElysiumEntity.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStanceTypes.h"
#include "ElysiumStub.h"
#include "ElysiumVariant.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumCameraOverride.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcBaseEntityChainShared.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "Substrate/ElysiumNpcDebugShared.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcEntityChainShared.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcGeometryShared.h"
#include "Substrate/ElysiumNpcKernelBaseHelpersShared.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLifecycleShared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSensesBodiesShared.h"
#include "Substrate/ElysiumNpcSounds10Shared.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Substrate/ElysiumNpcTroikaHelpersShared.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Visual/ElysiumActionTables.h"
#include "Visual/ElysiumEyeRig.h"

// --- File-scope helpers moved with the bodies (story 5 step 6) ---

namespace
{
	constexpr float BleedNoiseLowDamage = 10.0f;  // _DAT_1044e664
	constexpr float BleedNoiseHighDamage = 25.0f; // _DAT_10462994
	constexpr float BleedDirFlip = -1.0f;         // _DAT_104492dc — `vecDir * -1`
	constexpr float BleedTraceReach = -172.0f;    // _DAT_10499514 — SDK 2013's own -172
	// `_DAT_104454c8` = **80.0f** (`docs/vtmb/computer-terminals.md` line 475, `docs/vtmb/npc-ai/
	// conditions-and-states.md` line 460: "farther than 80 units (`_DAT_104454c8`)"). This is slot
	// 37's whole answer, which the ledger's `checklist-0-9.md` records as unrecovered; recovered
	// here from the same word's other readers, exactly as that row predicted it would be.
	constexpr float GChainSlot37 = 80.0f;
	constexpr int32 GChainSolidBbox = 2;      // SOLID_BBOX
	// `FSOLID_NOT_SOLID`, the `GetSolidFlags()` bit slot 164 refuses on.
	constexpr int32 GChainSolidNotSolid = 0x10;
	// `MoveType_t` as slot 226 switches on it. 7 takes the read-back-from-physics arm; 1 and 8 take
	// `VPhysicsUpdatePusher`. Troika's enum is NOT stock Source's here, so the numbers are carried
	// as numbers and the names are not claimed.
	constexpr int32 GChainMoveTypePhysicsRead = 7;
	constexpr int32 GChainMoveTypePusherA = 1;
	constexpr int32 GChainMoveTypePusherB = 8;
	// `m_iEFlags` bit 0, `EFL_KILLME` — slot 116 `IsMarkedForDeletion` (`0x10027490`).
	constexpr int32 GEntityFlagKillMe = 1 << 0;
	// Slot 91 `ShouldCollide` (`0x100b4de0`): the one collision group it refuses for, and the
	// contents mask bit that overrides the refusal. `1` is Source's `COLLISION_GROUP_DEBRIS`.
	constexpr int32 GCollisionGroupDebris = 1;
	constexpr int32 GContentsDebrisOverride = 0x4000000;
	// `10268900` @ `10268a3a`: the filter word is `ammoFlags | 0x1000`.
	constexpr int32 GSounds10FireBulletsFilterBit = 0x1000;
	// The ammo-def flag that SUPPRESSES spread for everyone but a player with a zeroed `+0x1e78`.
	constexpr int32 GSounds10AmmoFlagNoNpcSpread = 0x2000000;
	// `info+0xa0` bit 0, which latches the tracer arm on its own.
	constexpr int32 GSounds10TracerFlagAlways = 0x1;
	// The skill above which an NPC shooter latches the tracer arm (`2 < stat`).
	constexpr int32 GSounds10TracerSkillThreshold = 2;
	// `DAT_104994c8`, six floats read out of the pinned image: the divisor `info+0xa4` is divided
	// by, indexed by the ranged skill clamped to 0..5.
	constexpr float GSounds10SkillDivisor[6] = { 1.0f, 1.0f, 1.0f, 1.1f, 1.3f, 1.5f };
	// `_DAT_104454c0`, the numerator of the tracer's per-shot fraction AND the rejection sampler's
	// radius test. 1.0 in the image.
	constexpr float GSounds10One = 1.0f;
	// `0x10268170`'s four draws are `RandomFloat(0xbf000000, 0x3f000000)`.
	constexpr float GSounds10SpreadDrawMin = -0.5f;
	constexpr float GSounds10SpreadDrawMax = 0.5f;
	// NAMED DECISION: `FireBullets`' spread draws come off `EElysiumRngStream::Reaction`.
	// Retail draws from the one engine stream (`DAT_1070b244`); this runtime splits streams so two
	// systems cannot walk each other's position, and `Reaction` is the combat-side stream (the
	// damage flinch's coin and jitter). The alternative — `NpcSchedule` — would move the idle
	// branch's position by four draws per bullet. Nothing fires a weapon in this runtime yet, so
	// the choice costs nothing today and is recorded here rather than rediscovered.
	constexpr EElysiumRngStream GSounds10SpreadStream = EElysiumRngStream::Reaction;
}

// --- Moved from `ElysiumNpcBaseClosure.cpp` (story 5 step 6) ---

float FElysiumEntity::Slot48()
{
	// `0x10026810` -> `ElysiumCameraOverride::DefaultRollDegrees`. `IElysiumCameraOverrideSource`'s
	// own `GetCameraRoll()` default is the same constant, written FROM this body.
	return ElysiumCameraOverride::DefaultRollDegrees;
}

float FElysiumEntity::Slot49()
{
	// `0x10026830` -> `ElysiumCameraOverride::DefaultFieldOfView`.
	return ElysiumCameraOverride::DefaultFieldOfView;
}

void FElysiumEntity::SetOrigin(float X, float Y, float Z)
{
	// `0x10026a10`, and the whole of it: build a `Vector` on the stack from the three floats and
	// dispatch `vtable +0xf8` — slot 62, `SetOrigin(const Vector&)`. A compiler-generated forwarding
	// thunk shared unmodified by about 82 classes, not retail logic, which is why 29c's verdict is
	// `mechanism` and names `RTTI:VirtualThunk`.
	//
	// It forwards VIRTUALLY here too, exactly as retail does: slot 62 (`0x100b2be0`) is another
	// story's row and is still a generated stub, so this reaches whatever that slot eventually
	// answers rather than a copy of it. Reproducing the dispatch is the point — a species that
	// overrides slot 62 must be reached through slot 63 as well.
	SetOrigin(FVector(X, Y, Z));
}

bool FElysiumEntity::Slot88()
{
	// `0x10026b50` is a bare tail jump into `0x10146700` on the eight-byte tracker at `this+0x1b0`
	// (interval word `+0x4`, countdown `+0x6`, flag bytes `+0x0`/`+0x1`/`+0x2`). `0x10146700` runs
	// the countdown down by the frame delta, re-arms it and raises `+0x2` when it lapses, and
	// answers "a change is pending". Slot 89 (`0x10026b70`) zeroes `+0x1`/`+0x2`, and
	// `SetOrigin` (`0x100b2be0`) sets `+0x1b1`. The tracker's retail NAME is **unrecovered** — no
	// datamap names it and no corpus body declares it.
	//
	// **REFUSAL.** Roughly 80 unrelated non-NPC classes (props, triggers, items) fill this slot with
	// the same body, which is what says it is engine plumbing rather than an NPC rule; it is the
	// networked-edict dirty flag, and this substrate has no replication to dirty. No port member
	// stands `+0x1b0` — the shape map has no row for it, and no other family declared one.
	//
	// `false` is not a placeholder: it is what `0x10146700` itself answers for a ZERO-INITIALISED
	// tracker. With `+0x4` zero the countdown arm never runs, `+0x0` is clear, and the function
	// takes its `(sVar1 == 0)` exit and returns 0. An NPC that never dirtied has no change pending.
	++ClosureRefusals.ChangeTracker;
	return false;
}

void FElysiumEntity::Physics_TraceEntity(FElysiumEntity* Entity, const FVector& StartCm,
	const FVector& EndCm, uint32 Mask, void* OutTrace)
{
	// `0x100ab450`, 121 bytes of which 100 are the crash-report breadcrumb push and pop: the body
	// writes `"Physics_TraceEntity"` into the scope-trace stack, calls `thunk_FUN_101cd110` with all
	// five arguments unchanged, and pops. `0x101cd110` issues the trace through the engine trace
	// service's own vtable (`DAT_1070b254 + 0x14`). There is nothing else in the body, which is 29c's
	// `mechanism` verdict and why it names `UWorld::LineTraceSingleByChannel`.
	//
	// **REFUSAL, and it is the out parameter that forces it.** The port HAS a trace seam — the
	// embodiment's `QueryLineOfSight` — but retail's fifth argument is a `trace_t*`, a Source
	// structure (fraction, endpos, plane, surface, hit entity, hitbox, physics bone) that this
	// substrate stands no counterpart for, so the generator could only type it `void*`. Filling a
	// buffer whose layout is not the caller's would be worse than filling none, and answering only
	// the boolean half would silently drop the fraction and the endpos that every retail consumer
	// of this slot reads. So the trace is NOT issued and `OutTrace` is left exactly as the caller
	// handed it in — which the case asserts by passing a sentinel-filled buffer.
	//
	// What it takes to close: a port `trace_t` and the world trace behind it. Then this becomes a
	// forward, and the `Mask` (Source's `MASK_*` content flags) becomes a channel choice.
	(void)Entity;
	(void)OutTrace;
	++ClosureRefusals.PhysicsTraceEntity;
	ClosureRefusals.TraceStartCm = StartCm;
	ClosureRefusals.TraceEndCm = EndCm;
	ClosureRefusals.TraceMask = Mask;
}

void FElysiumEntity::MakeTracer(const FVector& StartCm, void* Trace, int32 TracerType)
{
	// `0x10267260`, 186 bytes and 497 classes deep — the stock SDK body. It builds a `CPASFilter`
	// around `param_1` (the tracer's start), and when `param_3 == 1` (`TRACER_LINE`) fires the
	// bullet-tracer temp entity from that point to the trace's `endpos` (`param_2 + 0xc`) with this
	// entity's index as the attachment owner, then unwinds the filter's heap. Every other tracer
	// type builds the filter and fires nothing. A pure visual effect: no condition, no state, no
	// stamp, and nothing downstream reads anything it writes.
	//
	// **REFUSAL, for the same two reasons as slot 102.** `param_2` is a `trace_t&` this substrate
	// has no type for, so the endpos the tracer would be drawn TO cannot be read; and the PAS filter
	// is Source's potentially-audible-set broadcast, whose counterpart here is Niagara plus the
	// engine's own relevance, not a recipient list. 29c's `mechanism` target
	// (`UGameplayStatics::SpawnEmitterAtLocation`) is the right eventual home and the effect is
	// visual-only, so adopting it changes no event order — but it needs the endpos first.
	(void)Trace;
	++ClosureRefusals.MakeTracer;
	ClosureRefusals.TracerStartCm = StartCm;
	ClosureRefusals.TracerType = TracerType;
}

FVector FElysiumEntity::WorldSpaceCenter()
{
	// `0x10027160` -> `ElysiumCameraShots::SurroundingBounds`, the port's one bounds accessor.
	return ElysiumCameraShots::SurroundingBounds(*this).GetCenter();
}

void* FElysiumEntity::WorldSpaceCenter() const
{
	// `0x100b4c30`, the `const Vector&` overload of the SAME body. The value is slot 192's; what is
	// different is that retail hands out an ADDRESS, into its rotating temp-vector ring. The port
	// caches into `WorldSpaceCentreCacheCm` instead — a named modernization stated in full at the
	// member's declaration in `Substrate/ElysiumNpcClosure.inl`.
	WorldSpaceCentreCacheCm = ElysiumCameraShots::SurroundingBounds(*this).GetCenter();
	return &WorldSpaceCentreCacheCm;
}

void FElysiumEntity::VPhysicsDestroyObject()
{
	// `0x100b5040`, 47 bytes, 497 classes: when `m_pPhysicsObject` (`+0x36c`) is set, unregister it
	// from the physics-object list (`thunk_FUN_1002ee60`), destroy it (`thunk_FUN_10158520`) and
	// null the pointer. Stock SDK teardown with no VtMB-specific rule.
	//
	// **REFUSAL.** There is no `m_pPhysicsObject` here. This substrate stands no rigid body: the
	// shape map has no row for `+0x36c`, the collision surface it would tear down is Unreal's own
	// (`UPrimitiveComponent::DestroyPhysicsState`, which the engine calls on component destruction
	// without being asked), and no port system holds a physics handle to release. With the pointer
	// null retail's body is `return;` — so answering nothing is not merely the port's answer, it is
	// retail's for an NPC that never got a physics object, which is every NPC that was never
	// ragdolled.
	++ClosureRefusals.VPhysicsDestroyObject;
}

// --- Moved from `ElysiumNpcBaseDamage.cpp` (story 5 step 6) ---

FVector FElysiumEntity::GetAttackExtents()
{
	// The whole body: copy `m_vecAttackExtents` (+0x50/+0x54/+0x58) into the out-parameter. The
	// setter's other half — `CBaseEntity::SetAttackExtents` (`0x1009af40`), which also pushes the
	// margin at the collision partition — is already `FElysiumEntity::SetAttackExtents`, so this reads
	// the word that one writes. This runtime carries the margin in CENTIMETRES.
	return AttackExtentsCm;
}

int32 FElysiumEntity::DamageDecal(int32 DamageBits, int32 GameMaterial)
{
	// Arm 1: `m_nRenderMode == kRenderTransAlpha (4)` answers -1 — no decal at all.
	if (RenderMode == 4)
	{
		return INDEX_NONE;
	}
	// Arm 2: any other non-normal render mode, on glass (`0x47` = `'G'`), answers the fixed decal
	// index `0x34`.
	if (RenderMode != 0 && GameMaterial == 0x47)
	{
		return 0x34;
	}
	// Arm 3 is a TAIL CALL that rewrites its own two stack arguments to `(0, 4)` before jumping —
	//     100b4eca  MOV dword ptr [ESP + 0x8],0x4
	//     100b4ed2  MOV dword ptr [ESP + 0x4],0x0
	//     100b4edc  JMP dword ptr [EAX + 0x8]
	// through `*DAT_1070b244` slot 2, which is `IUniformRandomStream::RandomInt`: the same object
	// whose slot 1 (`+0x4`) `TraceBleed` draws its float spread from. So the ordinary answer is a
	// uniform decal index in `[0, 4]` and neither argument reaches it.
	(void)DamageBits;
	return ElysiumRng::Stream(EElysiumRngStream::Effects).RandRange(0, 4);
}

void FElysiumEntity::TraceBleed(void* InDmg, const FVector& DirUnits, void* InTrace)
{
	const FElysiumDmg* Dmg = static_cast<const FElysiumDmg*>(InDmg);
	const FElysiumTraceHit* Trace = static_cast<const FElysiumTraceHit*>(InTrace);
	if (Dmg == nullptr || Trace == nullptr)
	{
		return;
	}

	// 1. `BloodColor()` (slot 145) gates the whole body: `DONT_BLEED` (-1) and `BLOOD_COLOR_MECH`
	//    (0x14) both refuse. Retail asks it TWICE — once per comparison — and again at the decal.
	const int32 Color = BloodColor();
	if (Color == INDEX_NONE || Color == 0x14)
	{
		return;
	}

	// 2. The descriptor's word 1 — its AUTHORED base damage, not the applied result — is what the
	//    noise table is keyed on (`(float)*(int *)(param_1 + 4)`), and a zero refuses.
	const float BaseDamage = static_cast<float>(Dmg->BaseDamage);
	if (BaseDamage == NpcKernelDamageShared::DamageZero)
	{
		return;
	}

	// 3. `(m_bdmgTypes & 0xc7) != 0` — DMG_CRUSH|DMG_BULLET|DMG_SLASH|DMG_BLAST|DMG_CLUB. Only the
	//    LOW BYTE is tested (`*(byte *)(param_1 + 0x10) & 199`), so `DMG_AIRBOAT` and every bit
	//    above 8 is excluded, exactly as SDK 2013's mask is.
	if ((Dmg->DmgMask & 0xc7u) == 0)
	{
		return;
	}

	// 4. This fork's own addition to the SDK body: a per-entity decal BUDGET. Slot 158 answering
	//    false makes the body spend one unit of `+0x208` (`m_iMaxHealth`'s offset in Ghidra's
	//    struct) and refuse outright once it is exhausted. No word of this substrate stands for that
	//    counter and slot 158's meaning is UNRECOVERED; the seam is the slot answering TRUE, which
	//    is the arm that spends nothing.
	// (nothing to do on the true arm — recorded here so the budget is not rediscovered)

	// 5. The noise/count table, read off `_DAT_1044e664` = 10.0 and `_DAT_10462994` = 25.0 and the
	//    three denormal float bit patterns the decompiler printed for the integer counts (1, 2, 4):
	//        damage <  10 -> noise 0.1, 1 trace
	//        damage <  25 -> noise 0.2, 2 traces
	//        else         -> noise 0.3, 4 traces
	float Noise = 0.1f;
	int32 Count = 1;
	if (BaseDamage >= BleedNoiseLowDamage)
	{
		if (BaseDamage >= BleedNoiseHighDamage)
		{
			Noise = 0.3f;
			Count = 4;
		}
		else
		{
			Noise = 0.2f;
			Count = 2;
		}
	}

	FTraceBleedPass Pass;
	Pass.Noise = Noise;
	Pass.TraceCount = Count;

	FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Effects);
	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	for (int32 i = 0; i < Count; ++i)
	{
		// 6. `vecTraceDir = vecDir * -1` (`_DAT_104492dc`) jittered by three independent
		//    `RandomFloat(-noise, +noise)` draws — three draws per trace, in X, Y, Z order.
		FVector TraceDir = DirUnits * BleedDirFlip;
		TraceDir.X += Rng.FRandRange(-Noise, Noise);
		TraceDir.Y += Rng.FRandRange(-Noise, Noise);
		TraceDir.Z += Rng.FRandRange(-Noise, Noise);

		// 7. The trace runs from `ptr->endpos` to `endpos + traceDir * -172` (`_DAT_10499514`), mask
		//    `0x400b` — `MASK_SOLID_BRUSHONLY` with `CONTENTS_GRATE` cleared, which is what keeps
		//    blood off a grate. The NEGATIVE reach with the already-flipped direction is Valve's own
		//    double negation and lands the trace behind the victim.
		const FVector StartUnits = Trace->EndPosUnits;
		const FVector EndUnits = StartUnits + TraceDir * BleedTraceReach;
		Pass.LastStartUnits = StartUnits;
		Pass.LastEndUnits = EndUnits;

		// 8. A trace whose fraction is not 1.0 paints `UTIL_BloodDecalTrace(&tr, BloodColor())`.
		//    The port's decal seam takes a direction and a range rather than a hit, so the same
		//    segment is handed to it; headless it answers false, which is a miss.
		if (Embodiment != nullptr)
		{
			const FVector Direction = (EndUnits - StartUnits).GetSafeNormal();
			const float RangeCm = static_cast<float>((EndUnits - StartUnits).Size()) * ElysiumMove::U;
			Embodiment->LayShotImpactDecal(StartUnits * ElysiumMove::U, Direction, RangeCm,
				ElysiumRng::Stream(EElysiumRngStream::Effects).RandRange(
					1, ElysiumImpactDecals::PoolSize));
		}
	}
	TraceBleedPasses.Add(Pass);
}

// --- Moved from `ElysiumNpcBaseDebug.cpp` (story 5 step 6) ---

const TCHAR* FElysiumEntity::DebugGetClassName()
{
	// slot 14, `CAISound::FUN_1009af00` — four bytes, `return (int)&this->field_0x24;`, the ADDRESS
	// of the embedded classname buffer at `CBaseEntity+0x0024`. The shape map binds that word as
	// `FElysiumEntity::Class`, "retail's debug copy of the classname; the registry descriptor
	// carries it", so the answer is the classname the registry resolved.
	//
	// Note this is the address of a fixed-size char array, not a pointer read out of it: retail
	// answers a non-null string even for an entity whose classname was never written, because the
	// buffer is always there. Reproduced by answering the empty string rather than null when the
	// def carries no classname.
	return Def != nullptr ? *Def->Classname : TEXT("");
}

// --- Moved from `ElysiumNpcBaseDialogue.cpp` (story 5 step 6) ---

void FElysiumEntity::OnUseBegin(FElysiumEntity* Activator)
{
	// slot 39, 0x100a4fe0 — `CBaseEntity`'s use-begin hook, 59 bytes, shared by 82 classes in the
	// kernel family (`CAISound` is simply the class the corpus attributes the address to). Two
	// statements:
	//
	//     FireOutput(m_OnUseBegin (+0x5c), activator = param_1, caller = this, delay = 0);
	//     m_hUseActivator (+0x8c) = param_1 ? param_1->GetRefEHandle() : -1;
	//
	// The output fires FIRST and UNCONDITIONALLY — a null activator still fires it, with a null
	// activator — and the handle is written after. `OnUseBegin` is a `CBaseEntity` datamap keyfield
	// (`vtmb_fields CAISound` names `+0x5c` `m_OnUseBegin`, key `OnUseBegin`), so every `npc_*` row
	// in a shipped map may wire it.
	static const FName GOnUseBegin(TEXT("OnUseBegin"));
	FireOutput(GOnUseBegin, Activator != nullptr ? Activator->Handle : FElysiumEntityHandle());
	UseActivator = Activator != nullptr ? Activator->Handle : FElysiumEntityHandle::Invalid();
}

void FElysiumEntity::OnUseEnd(FElysiumEntity* Activator)
{
	// slot 42, 0x100a5030 — the other half, 33 bytes:
	//
	//     FireOutput(m_OnUseEnd (+0x74), activator = param_1, caller = this, delay = 0);
	//     m_hUseActivator (+0x8c) = -1;
	//
	// The clear is UNCONDITIONAL — it does not consult the activator at all, so ending a use someone
	// else began still clears the cache. That asymmetry with slot 39 is retail's.
	static const FName GOnUseEnd(TEXT("OnUseEnd"));
	FireOutput(GOnUseEnd, Activator != nullptr ? Activator->Handle : FElysiumEntityHandle());
	UseActivator = FElysiumEntityHandle::Invalid();
}

// --- Moved from `ElysiumNpcBaseEntityChain.cpp` (story 5 step 6) ---

bool FElysiumEntity::PhysicsObjectPosition(const void* PhysicsObject, FVector& OutOrigin,
	FRotator& OutAngles) const
{
	// SEAM for `IPhysicsObject::GetPosition(&origin, &angles)` (the `+0x94` dispatch inside
	// `0x100b4f30`). No `IPhysicsObject` in this substrate; the generated slot signature hands the
	// pointer in as `void*` and nothing can be read off it.
	(void)PhysicsObject;
	OutOrigin = FVector::ZeroVector;
	OutAngles = FRotator::ZeroRotator;
	return false;
}

float FElysiumEntity::Slot37()
{
	// 0x10026710 — the whole body is `return (float10)_DAT_104454c8;`.
	//
	// The ledger's `checklist-0-9.md` records the value as unrecovered and names where it would be
	// recovered ("UpdateEnemyPos 0x10271900 and UpdateTargetPos 0x10271b10 read the same constant").
	// It is **80.0f**: `docs/vtmb/npc-ai/conditions-and-states.md` line 460 reads the same word as
	// "farther than `_DAT_104454c8 = 80.0` units from the goal point", and
	// `docs/vtmb/computer-terminals.md` line 475 as `CPropDoorknob`'s 80-unit break-off.
	return GChainSlot37;
}

FElysiumEntity* FElysiumEntity::Slot38(FElysiumEntity* Other)
{
	// 0x10026730 — `return this;`, ignoring the argument. The declared signature really is
	// `CBaseEntity* vfunc38(CBaseEntity*)`, so this is a genuine always-answers-itself default and
	// not a decompiler artefact: what the caller passes never reaches anything.
	(void)Other;
	return this;
}

void FElysiumEntity::SetAngles(float Pitch, float Yaw, float Roll)
{
	// 0x10026a50, slot 65 — the three-scalar overload. The BODY is the packing: it lays the three
	// scalars into one stack record and dispatches slot 64 (`+0x100`) with its address. The vtable
	// hop is retail's, so a species that replaced slot 64 is reached through it, and this is the
	// same body on 501 classes.
	SetAngles(FRotator(Pitch, Yaw, Roll));
}

void FElysiumEntity::Slot89()
{
	// 0x10026b70 — tail-jumps into `0x10146790`, which clears bytes +1 and +2 of the
	// `m_NetworkChangeState` record at `+0x01b0` (`docs/vtmb/npc-kernel/layout.md`): `m_bChanged`
	// and the second flag. The interval and the countdown at +4/+6 are NOT touched, and neither is
	// byte +0, which the static prop/brush Spawns own.
	NetworkChangeState.bChanged = false;
	NetworkChangeState.bByte2 = false;
}

bool FElysiumEntity::ReflectGauss()
{
	// 0x10026f20, slot 159 — `IsStandableSolid() && m_takedamage == 0`. Both terms, in retail's
	// order; `m_takedamage` is `+0x01fc`, family Damage's `TakeDamageMode`, and `0` is `DAMAGE_NO`.
	// So the answer is "a solid I could stand on that takes no damage" — world brush, not a body.
	return IsStandableSolid() && TakeDamageMode == 0;
}

bool FElysiumEntity::IsStandable()
{
	// 0x100b50a0, slot 164.
	//
	//   GetSolidFlags() & FSOLID_NOT_SOLID -> false, immediately
	//   GetSolid() is SOLID_BSP, SOLID_VPHYSICS or SOLID_BBOX -> true
	//   otherwise -> the shared helper
	//
	// The three solid tests are three SEPARATE dispatches of slot 92 in retail, in the order
	// 1, 6, 2, and the port keeps them that way. Note the asymmetry with the helper: slot 164 takes
	// `SOLID_VPHYSICS` as standable OUTRIGHT, while the helper asks the physics object — so a moving
	// physics prop is standable to slot 164 and not to `ReflectGauss`.
	if ((GetSolidFlags() & GChainSolidNotSolid) != 0)
	{
		return false;
	}
	if (GetSolid() == NpcKernelEntityChainShared::GChainSolidBsp)
	{
		return true;
	}
	if (GetSolid() == NpcKernelEntityChainShared::GChainSolidVPhysics)
	{
		return true;
	}
	if (GetSolid() == GChainSolidBbox)
	{
		return true;
	}
	return IsStandableSolid();
}

bool FElysiumEntity::CanStandOn(void* Edict)
{
	// 0x10026fb0, slot 165 — the `edict_t*` overload. It selects an ARGUMENT and dispatches slot 166
	// (`+0x298`), which is the `CBaseEntity*` overload and family Motor's body: the networkable at
	// `edict+0x40` if the edict and the networkable are both non-null, else literal 0. Retail
	// dispatches slot 166 on BOTH arms — a null edict is not a refusal, it is `CanStandOn(nullptr)`.
	if (Edict != nullptr)
	{
		return CanStandOn(EntityOfEdict(Edict));
	}
	return CanStandOn(static_cast<FElysiumEntity*>(nullptr));
}

void FElysiumEntity::VPhysicsUpdate(void* PhysicsObject)
{
	// 0x100b4f30, slot 226 — retail's per-movetype physics-tick ordering, on slot 94's answer.
	//
	//   movetype 7 : read the object's transform back (`IPhysicsObject +0x94`), warn on any
	//                component whose exponent field is all-ones (`& 0x7f800000 == 0x7f800000`,
	//                i.e. inf or NaN), then SetAbsOrigin (slot 216), SetAbsAngles (slot 218),
	//                PhysicsTouchTriggers(0) and PhysicsRelinkChildren — IN THAT ORDER.
	//   movetype 1 or 8 : `CBaseEntity::VPhysicsUpdatePusher(object)`.
	//   anything else   : nothing at all.
	//
	// The warn is `Msg("Infinite values from vphysics!...")` and it does NOT abort the arm: retail
	// prints and then writes the bad transform anyway, which is a fact a program can observe.
	const int32 MoveType = GetMoveType();
	if (MoveType == GChainMoveTypePhysicsRead)
	{
		FVector PhysOrigin = FVector::ZeroVector;
		FRotator PhysAngles = FRotator::ZeroRotator;
		if (PhysicsObjectPosition(PhysicsObject, PhysOrigin, PhysAngles))
		{
			if (!FMath::IsFinite(PhysOrigin.X) || !FMath::IsFinite(PhysOrigin.Y)
				|| !FMath::IsFinite(PhysOrigin.Z))
			{
				// `Msg(s_Infinite_values_from_vphysics__105591dc)` — and then the write anyway.
				UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Infinite values from vphysics! (%s)"),
					*DebugString());
			}
			SetAbsOrigin(PhysOrigin);
			SetAbsAngles(PhysAngles);
		}
		// The two calls run on this arm whether or not the transform read answered; retail's read
		// cannot fail, and the seam's refusal must not remove them from the sequence.
		PhysicsUpdateCalls.Add(TEXT("PhysicsTouchTriggers"));
		PhysicsUpdateCalls.Add(TEXT("PhysicsRelinkChildren"));
		return;
	}
	if (MoveType == GChainMoveTypePusherA || MoveType == GChainMoveTypePusherB)
	{
		VPhysicsUpdatePusher(PhysicsObject);
	}
}

void FElysiumEntity::VPhysicsUpdatePusher(const void* PhysicsObject)
{
	// SEAM for `CBaseEntity::VPhysicsUpdatePusher(physicsObject)`, the arm movetypes 1 and 8 take.
	(void)PhysicsObject;
	PhysicsUpdateCalls.Add(TEXT("VPhysicsUpdatePusher"));
}

FElysiumEntity* FElysiumEntity::EntityOfEdict(const void* Edict) const
{
	// SEAM for `edict->m_pNetworkable (+0x40)->GetBaseEntity() (+0x10)`. There are no edicts here.
	// Answering null is not a refusal of slot 165: retail's own null arm dispatches slot 166 with
	// 0, and so does this.
	(void)Edict;
	return nullptr;
}

bool FElysiumEntity::PhysicsObjectIsStandable(const FElysiumEntity& Entity) const
{
	// SEAM for `(*DAT_1070b250 + 0x18)(entityIndex)`, the physics-environment query `0x100b5110`
	// makes for a `SOLID_VPHYSICS` entity. False is retail's answer for an object that is awake and
	// moving, which is what an unmodelled physics world stands for.
	(void)Entity;
	return false;
}

bool FElysiumEntity::IsStandableSolid() const
{
	// 0x100b5110, the helper slots 159 and 164 both end in.
	//
	//   GetSolid() == SOLID_BSP                          -> true
	//   GetSolid() == SOLID_VPHYSICS and the physics
	//     environment says the object is standable       -> true
	//   anything else                                    -> false  (retail returns the movetype
	//                                                       word with its low byte zeroed, which
	//                                                       IS false and nothing else)
	//
	// Retail re-dispatches slot 92 for the second test rather than caching the first answer; the
	// port keeps the two calls so a species that answered differently on the second would be read
	// differently here too.
	if (GetSolid() == NpcKernelEntityChainShared::GChainSolidBsp)
	{
		return true;
	}
	if (GetSolid() == NpcKernelEntityChainShared::GChainSolidVPhysics)
	{
		return PhysicsObjectIsStandable(*this);
	}
	return false;
}

// --- Moved from `ElysiumNpcBaseGeometry.cpp` (story 5 step 6) ---

const FVector& FElysiumEntity::EyeAngles()
{
	// `0x100b4bc0`, EIGHT bytes and no frame:
	//
	//     100b4bc0  MOV EAX,dword ptr [ECX]
	//     100b4bc2  JMP dword ptr [EAX + 0x36c]
	//
	// `+0x36c` is slot 219, `GetAbsAngles()`. An NPC's eye angles ARE its body angles in this
	// engine — there is no separate head orientation at this slot — and 82 classes fill 194 with
	// this exact tail call.
	return GetAbsAngles();
}

const FVector& FElysiumEntity::LocalEyeAngles()
{
	// `0x100b4be0`, the same eight bytes with `+0x374` — slot 221, `GetAngles()`, the LOCAL angles.
	// 80 classes fill 195 with it.
	return GetAngles();
}

void FElysiumEntity::SetSize(const FVector& InSizeCm)
{
	// `0x100b1890`, 146 bytes, of which 132 are the scope-trace push and pop: the body reads
	// `m_iName` (`+0x026c`) purely to label a crash-report breadcrumb (`"CBaseEntity::SetSize"`,
	// with `"NULL ENTITY"` for a null `this` and the empty string for an unnamed entity), pushes the
	// row, writes the three words, and pops. The breadcrumb stack has no observable effect on any
	// program and is not reproduced.
	//
	// The three writes ARE the body: `m_vecSize` (`+0x038c`) and the two words after it. Nothing in
	// layers 0–9 reads them but slot 214 `GetSize`. Unreal's collision component is the eventual
	// host for an actor's bounds; until then this member is what the kernel sees.
	SizeCm = InSizeCm;
}

// --- Moved from `ElysiumNpcBaseHelpers.cpp` (story 5 step 6) ---

// slot 25 0x100265b0 `void vfunc25(CBaseEntity*)`
void FElysiumEntity::Slot25(FElysiumEntity* Victim)
{
	// `CNPC_VZombie` replaces slots 25 and 26 (`FElysiumNpcZombie`, story 5 step 3) with one
	// `m_OnAttackedVictim` fire and NO base forward.
	(void)Victim;
	// `0x100265b0`, the Troika line's own body: ONE byte, `ret`. The overlay's reading was
	// `default:void` and the body is still exactly that — no member is written and nothing is
	// tallied, because retail writes nothing either.
}

// slot 26 0x100265d0 `void vfunc26(CBaseEntity*)`
void FElysiumEntity::Slot26(FElysiumEntity* Victim)
{
	(void)Victim;
	// `0x100265d0`, empty on the Troika line exactly as slot 25 is.
}

// --- Moved from `ElysiumNpcBaseLifecycle.cpp` (story 5 step 6) ---

// slot 0 `void SetRefEHandle(const CBaseHandle&)` — 0x10027450
void FElysiumEntity::SetRefEHandle(const FElysiumEntityHandle& InHandle)
{
	// Retail's whole body is `*(undefined4*)&this->field_0x448 = *param_1;` — the four-byte entity
	// handle/serial word at `+0x0448`. This runtime's counterpart is `FElysiumEntity::Handle`, which
	// the world's registry binds at `Construct`; writing it is exactly what retail does, and the
	// registry stays the authority over what the handle RESOLVES to.
	Handle = InHandle;
}

// slot 3 `IServerNetworkable* GetNetworkable()` — 0x10027630
void* FElysiumEntity::GetNetworkable()
{
	// Retail's whole body is `return &this->field_0x2d4;`, the address of the embedded
	// `CServerNetworkProperty` sub-object. SEAM: this runtime has no network property and no
	// networkable interface — entities are plain C++ objects with no edict behind them — so there is
	// no sub-object whose address could be answered. Answers null and names the retail word.
	return nullptr;
}

// slot 4 `CBaseEntity* GetBaseEntity()` — 0x10027650
FElysiumEntity* FElysiumEntity::GetBaseEntity()
{
	// `return this;`
	return this;
}

// slot 91 `bool ShouldCollide(int, int) const` — 0x100b4de0
bool FElysiumEntity::ShouldCollide(int32 CollisionGroupArg, int32 ContentsMask) const
{
	// Verbatim: the answer is true unless `m_CollisionGroup == 1` (`+0x0368`) AND bit `0x4000000` of
	// the contents mask is CLEAR. Retail ignores its first argument entirely, and so does this.
	(void)CollisionGroupArg;
	if (CollisionGroup == GCollisionGroupDebris && (ContentsMask & GContentsDebrisOverride) == 0)
	{
		return false;
	}
	return true;
}

// slot 116 `bool IsMarkedForDeletion()` — 0x10027490
bool FElysiumEntity::IsMarkedForDeletion()
{
	// `return this->m_iEFlags & 1;` — bit 0 of the entity-flags word at `+0x0268`, `EFL_KILLME`.
	// This runtime spells "killed, and the world will reap the slot" as `FElysiumEntity::bDead`,
	// which `Kill()` is the sole writer of; that IS the killme bit.
	return (IsDead() ? GEntityFlagKillMe : 0) != 0;
}

// slot 158 `bool IsAlive()` — 0x100b4dc0
bool FElysiumEntity::IsAlive()
{
	// `return this->m_lifeState == 0;` — `LIFE_ALIVE`, the int at `+0x0200`.
	//
	// This runtime has no `m_lifeState` word: it spells the same fact as two latches, and a body is
	// alive when NEITHER stands. `bDeathReported` is the death transaction's own one-shot
	// (`OnKilled` ran; the mind is dead and the death schedule is running), and `bDead` is `Kill`'s
	// terminal flag. `UpdateEnemyDistances` already reads `m_lifeState` through `IsDead()` for its
	// own arm, which is why the second term is spelled the same way here. The death transaction's
	// latch lives on the combat character (story 5 step 6 moved this body to the entity): an entity
	// that is not one has only `bDead`.
	const FElysiumCombatCharacter* const Character = AsCombatCharacter();
	return !(Character != nullptr && Character->HasReportedDeath()) && !IsDead();
}

float FElysiumEntity::ScaleField_0x1ddc() const
{
	// `FUN_10160680` — `(float10)_DAT_10725c9c * (float10)*(float *)(param_1 + 0x1ddc)`.
	// **Unrecovered:** `_DAT_10725c9c`'s value, the field's retail name and the class that owns the
	// offset. 1.0 keeps the read observable without claiming a scale.
	constexpr float Scale = 1.0f;   // _DAT_10725c9c — unrecovered
	return Field_0x1ddc * Scale;
}

// --- Moved from `ElysiumNpcBaseMotor.cpp` (story 5 step 6) ---

bool FElysiumEntity::RetailIsStandable(const FElysiumEntity& Entity)
{
	// `CBaseEntity::IsStandable()` slot 164 `0x100b50a0`:
	//     if (GetSolidFlags() & 0x10) return false;
	//     int mt = GetMoveType();
	//     if (mt == 1 || mt == 6 || mt == 2) return true;
	//     return thunk_FUN_100b5110(this);
	// **SEAM**: no solid flags and no move type here. It answers FALSE, which is retail's own answer
	// for the first arm, and `CanStandOn` therefore refuses every non-null candidate.
	(void)Entity;
	return false;
}

bool FElysiumEntity::CanStandOn(FElysiumEntity* Other)
{
	// slot 166, `CAISound::FUN_10026f80` `0x10026f80`, the body slot 166 carries for the whole family
	// (`CNPC_VMingXiaoTentacle::vfunc166` `0x1039ebd0` overrides it on its C++ class, story 5 step 3):
	//     if (other && !other->IsStandable()) return false;
	//     return true;
	if (Other != nullptr && !RetailIsStandable(*Other))
	{
		return false;
	}
	return true;
}

void FElysiumEntity::GetGroundVelocityToApply(FVector& OutVelocity)
{
	// slot 210. `CAISound::FUN_10027370` `0x10027370` copies the three shared statics
	// `DAT_1070d1b0/b4/b8` into the out-parameter. All three sit in `.data`'s zero-initialised tail
	// (the section's raw data ends at `0x106b9000`) and no corpus function writes them, so the
	// contribution is exactly `vec3_origin`.
	OutVelocity = FVector::ZeroVector;
}

// --- Moved from `ElysiumNpcBaseSenses.cpp` (story 5 step 6) ---

FVector FElysiumEntity::EarPosition()
{
	// `0x100b4c00`, 20 bytes: `(**(code**)(*this + 0x304))(out); return out;` — a tail call to slot
	// 193, `EyePosition`, for the side effect of filling the out-vector, and the same pointer back.
	// The ear IS the eye on every class in the family: 82 classes fill slot 196 and every one of
	// them with this body.
	return EyePosition();
}

// --- Moved from `ElysiumNpcBaseSounds10.cpp` (story 5 step 6) ---

bool FElysiumEntity::FireBulletsShooterIsPlayer() const
{
	// `+0x00a8 m_pPlayer`. Never an NPC. `FElysiumPlayer` is a different leaf entirely and does not
	// dispatch this slot.
	return false;
}

FVector FElysiumEntity::BulletSpreadOffset(const FVector& SpreadUnits, const FVector& RightAxis,
	const FVector& UpAxis)
{
	// `0x10268170`. The rejection sampler first, exactly as retail writes it: FOUR draws per
	// attempt, summed in pairs, redrawn while the pair is outside the unit disc.
	FRandomStream& Stream = ElysiumRng::Stream(GSounds10SpreadStream);
	float X = 0.f;
	float Y = 0.f;
	do
	{
		X = Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax)
			+ Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax);
		Y = Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax)
			+ Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax);
	}
	while (X * X + Y * Y > GSounds10One);

	// `this+0xa8` (`m_pPlayer`): a PLAYER shooter replaces BOTH spread components with
	// `ScaleField_0x1ddc()` (`0x10160680`, family **Lifecycle**). This leaf is never the player, so
	// the info's own pair stands and the arm is named rather than run.
	const float SpreadX = FireBulletsShooterIsPlayer()
		? static_cast<float>(ScaleField_0x1ddc()) : static_cast<float>(SpreadUnits.X);
	const float SpreadY = FireBulletsShooterIsPlayer()
		? static_cast<float>(ScaleField_0x1ddc()) : static_cast<float>(SpreadUnits.Y);

	return RightAxis * (SpreadX * X) + UpAxis * (SpreadY * Y);
}

// `CAISound::FUN_10268900` (`0x10268900`), slot 185, 1212 bytes, shared by `CAI_BaseNPC#185` and
// `CAI_BaseNPCTroika#185`. Ten steps, in retail's order:
//
//   1. `m_pBaseNPCTroika` (`+0x98`, `AsNpc()`) set → `--m_iFakeReloadCount` (`+0x65f0`) on it.
//   2. `info.m_iFlags (+0x58) = GetAmmoDef()->Flags(info.m_iAmmoType (+0x8c))`.
//   3. `info.m_pAttacker (+0x94)` defaults to the shooter when unset.
//   4. the two trace filters (`0x101c2c60`, `0x101c2c30`) and `DAT_1072cb48 = flags | 0x1000`.
//   5. `VectorVectors(info+0x14, right, up)` (`0x10138a90`).
//   6. open the per-victim tally (`0x1027f940`).
//   7. `for (repeat : info[0]) for (bullet : info[1])` — the pass below.
//   8. slot 186 (`vtable +0x2e8`) false → the tracer arm OR the trace-and-damage pass.
//   9. after EACH repeat, `RangedDamagePerVictim(victim, info, hits / info[1])` for every victim in
//      the tally — which is NOT cleared between repeats, so repeat 2 re-pays repeat 1's victims
//      with their accumulated counts. Retail's own behaviour, reproduced.
//  10. `0x10160560(m_pPlayer)` and free the tally.
//
// Step 8's gate is slot 186, `FElysiumNpcBase::Slot186` (family **Closure**), whose whole retail body
// (`0x100270c0`) is `return false;` — so the pass below always runs.
void FElysiumEntity::FireBullets(void* InInfo)
{
	FElysiumFireBulletsInfo* Info = static_cast<FElysiumFireBulletsInfo*>(InInfo);
	if (Info == nullptr)
	{
		// CRASH GUARD: retail dereferences the packet unconditionally. No caller in this runtime
		// passes null yet; refusing is the only answer that is not a fault.
		return;
	}

	// 1. The outstanding-shot counter, on the shooter's own NPC self-pointer.
	if (FElysiumNpc* const Troika = AsNpc())
	{
		--Troika->FakeReloadCount;
	}

	// 2. The ammo flags, WRITTEN back into the packet — every later arm reads them from there.
	Info->AmmoFlags = AmmoDefFlags(Info->AmmoType);

	// 3. The attacker default.
	if (!Info->Attacker.IsSet())
	{
		Info->Attacker = Handle;
	}

	// 4. The filter word. `DAT_1072cb48` is a file static in retail with this as its only writer in
	// the closure.
	FireBulletsFilterWord = Info->AmmoFlags | GSounds10FireBulletsFilterBit;

	// 5. The basis, from the SHOOTING DIRECTION and not from a set of angles: `0x10138a90` is
	// `VectorVectors`, not `AngleVectors`. (The checklist's walk named it `AngleVectors`; the body
	// at `0x10138a90` is the cross-product basis builder, and `info+0x14` is the same word the
	// per-shot direction is copied from two lines later, which a set of Euler angles could not be.)
	FVector RightAxis = FVector::ZeroVector;
	FVector UpAxis = FVector::ZeroVector;
	VectorVectors(Info->DirShooting, RightAxis, UpAxis);

	// 6. The tally: one `{ victim, hits }` pair per distinct victim, appended in first-hit order.
	TArray<FRangedDamagePerVictimCall> Tally;

	for (int32 Repeat = 0; Repeat < Info->Repeats; ++Repeat)
	{
		for (int32 Bullet = 0; Bullet < Info->Bullets; ++Bullet)
		{
			// 7a. The working direction starts as the forward, unchanged.
			Info->DirCurrent = Info->DirShooting;

			// 7b. The spread gate. `(flags & 0x2000000) == 0 || (m_pPlayer && m_pPlayer->+0x1e78
			// == 0)` — for an NPC shooter the second disjunct is dead, so the bit alone decides.
			// The offset is added UNSCALED: retail does not multiply it by the distance.
			const bool bNoSpreadBit = (Info->AmmoFlags & GSounds10AmmoFlagNoNpcSpread) != 0;
			if (!bNoSpreadBit || FireBulletsShooterIsPlayer())
			{
				Info->DirCurrent += BulletSpreadOffset(Info->Spread, RightAxis, UpAxis);
			}

			// 7c. The endpoint. `end = src + dir * distance`, component by component, in retail's
			// own x/z/y store order (which is unobservable and is written x/y/z here).
			Info->EndUnits = Info->SrcUnits + Info->DirCurrent * Info->DistanceUnits;

			// 7d. The ranged skill and the tracer latch.
			const int32 Skill = ShooterRangedSkill();
			bool bTracer = false;
			if ((Info->TracerFlags & GSounds10TracerFlagAlways) != 0
				|| (Skill > GSounds10TracerSkillThreshold && !FireBulletsShooterIsPlayer()))
			{
				bTracer = true;
				// `uVar11 & ((int)uVar11 < 0) - 1` below 6, `5` at or above it: the clamp to 0..5.
				const int32 Index = Skill < 6 ? (Skill < 0 ? 0 : Skill) : 5;
				Info->TracerScale = Info->TracerScale / GSounds10SkillDivisor[Index];
			}

			// 8. Slot 186 gates the whole emission half.
			if (Slot186())
			{
				continue;
			}
			if (bTracer && !Info->TracerName.IsEmpty())
			{
				// The tracer arm REPLACES the trace-and-damage pass: a bullet that draws a tracer
				// does no damage in this body at all.
				EmitBulletTracer(*Info, GSounds10One / static_cast<float>(Info->Bullets));
			}
			else
			{
				FireBulletsTracePass(*Info);
				if (Info->LastVictim.IsSet())
				{
					FRangedDamagePerVictimCall* Row = Tally.FindByPredicate(
						[Info](const FRangedDamagePerVictimCall& Candidate)
						{
							return Candidate.Victim.Index == Info->LastVictim.Index;
						});
					if (Row == nullptr)
					{
						FRangedDamagePerVictimCall New;
						New.Victim = Info->LastVictim;
						New.Fraction = 0.f;
						Row = &Tally.Add_GetRef(New);
					}
					// `*(int*)(entry + 4) += 1` — the hit count, carried in `Fraction` until the
					// division below turns it into one.
					Row->Fraction += 1.f;
				}
			}
		}

		// 9. The per-victim payout, once per REPEAT and guarded on a non-zero bullet count (the
		// guard is retail's own divide-by-zero check).
		if (Info->Bullets != 0)
		{
			for (const FRangedDamagePerVictimCall& Row : Tally)
			{
				RangedDamagePerVictim(Row.Victim, *Info,
					Row.Fraction / static_cast<float>(Info->Bullets));
			}
		}
	}

	// 10. `0x10160560(m_pPlayer)` — unreachable for an NPC shooter, and named so.
	if (FireBulletsShooterIsPlayer())
	{
		// SEAM: `0x10160560`, the player-side post-fire bookkeeping. No NPC reaches it.
	}
}

int32 FElysiumEntity::AmmoDefFlags(int32 AmmoTypeIndex) const
{
	// `0x104276a0`: `(0 < i && i < m_nAmmoIndex) ? m_AmmoType[i].nFlags : 0`. No `CAmmoDef` here,
	// so `m_nAmmoIndex` is 0 and every index is out of range.
	(void)AmmoTypeIndex;
	return 0;
}

int32 FElysiumEntity::ShooterRangedSkill() const
{
	// `0x101cda50` → the local player, then its type-3 stat list's stat 3, else the empty static
	// list's `0`. Unjoined here; `0` is retail's own no-list answer.
	return 0;
}

void FElysiumEntity::FireBulletsTracePass(FElysiumFireBulletsInfo& Info)
{
	FFireBulletsTrace Trace;
	Trace.SrcUnits = Info.SrcUnits;
	Trace.EndUnits = Info.EndUnits;
	Trace.DirUnits = Info.DirCurrent;
	Trace.TracerScale = Info.TracerScale;
	Trace.FilterWord = FireBulletsFilterWord;
	FireBulletsTraces.Add(Trace);

	// `0x10267b60` writes `info+0xbc` with whatever it hit. Nothing traces here, so it stays unset —
	// the same word a bullet that hit world geometry leaves.
	Info.LastVictim = FElysiumEntityHandle();
}

void FElysiumEntity::EmitBulletTracer(const FElysiumFireBulletsInfo& Info, float Fraction)
{
	FBulletTracerCall Call;
	Call.Name = Info.TracerName;
	Call.EndUnits = Info.EndUnits;
	Call.Fraction = Fraction;
	Call.TracerScale = Info.TracerScale;
	BulletTracerCalls.Add(MoveTemp(Call));
}

void FElysiumEntity::RangedDamagePerVictim(const FElysiumEntityHandle& Victim,
	const FElysiumFireBulletsInfo& Info, float Fraction)
{
	(void)Info;
	FRangedDamagePerVictimCall Call;
	Call.Victim = Victim;
	Call.Fraction = Fraction;
	RangedDamagePerVictimCalls.Add(Call);
}

// --- Moved from `ElysiumNpcSounds10.cpp` (story 5 step 5) ---

void FElysiumEntity::VectorVectors(const FVector& Forward, FVector& OutRight, FVector& OutUp)
{
	// `0x10138a90`, Source's `VectorVectors`, with `_DAT_104454c4` (0.0) folded into the cross
	// products as the constant `up` axis's x and y.
	if (Forward.X == 0.0 && Forward.Y == 0.0)
	{
		// Retail's degenerate arm, verbatim: `right = (1,0,0)`, `up = (0, -forward.z, 0)`. Neither
		// is normalized and `up` is not perpendicular to anything; it is what the body writes.
		OutRight = FVector(1.0, 0.0, 0.0);
		OutUp = FVector(0.0, -Forward.Z, 0.0);
		return;
	}
	OutRight = FVector(Forward.Y, -Forward.X, 0.0);
	OutRight.Normalize();
	// `up = cross(right, forward)`, then normalized.
	OutUp = FVector(
		Forward.Z * OutRight.Y - Forward.Y * OutRight.Z,
		Forward.X * OutRight.Z - Forward.Z * OutRight.X,
		OutRight.X * Forward.Y - OutRight.Y * Forward.X);
	OutUp.Normalize();
}

// --- The chain integrations (story 5 step 6, `decisions-step6.json` `integrations`) ---

void FElysiumEntity::SetOrigin(const FVector& NewOrigin)
{
	// `CBaseEntity::SetOrigin` `0x100b2be0`, slot 62: past its scope-trace push it acts only when the
	// vector differs from `m_vecOrigin` (+0x41c) -- then it invalidates (`0x100b5340(this, 0x10800,
	// 0)`, `0x100b52a0`), copies, and sets the change-tracker byte `+0x1b1`. The port has one origin,
	// so the write is `SetRuntimeOrigin`, which moves the body with it. The change-tracker byte stays
	// the refusal `Slot88`/`Slot89` record (no entity is networked here).
	if (NewOrigin != Origin)
	{
		SetRuntimeOrigin(NewOrigin);
	}
}

void FElysiumEntity::SetMoveType(int32 MoveType, int32 MoveCollide)
{
	// `CBaseEntity::SetMoveType` `0x100aad70`, slot 93: `m_MoveType` and `m_MoveCollide` are written
	// only when the type differs, and then the physics object is notified (its slot 25). **SEAM**:
	// this runtime stands no `IPhysicsObject`, so the notify is a refusal.
	if (RetailMoveType != MoveType)
	{
		RetailMoveType = MoveType;
		RetailMoveCollide = MoveCollide;
	}
}

const FVector& FElysiumEntity::GetAbsOrigin() const
{
	// `0x100b31b0`, slot 217: the absolute origin. The port's `Origin`, in port units (cm, Unreal axes).
	return Origin;
}

const FVector& FElysiumEntity::GetAbsAngles() const
{
	// `0x100b3280`, slot 219: the absolute angles. The port's `Angles`, Source degrees (pitch, yaw, roll).
	return Angles;
}

const FVector& FElysiumEntity::GetOrigin()
{
	// `0x100b3070`, slot 220, returns `&m_vecOrigin`, the LOCAL origin. **Modernization (named)**: the
	// port keeps one origin and no parent-relative one, so for a parented entity this answers the
	// absolute origin 217 answers.
	return Origin;
}

const FVector& FElysiumEntity::GetAngles()
{
	// `0x100b3110`, slot 221, returns `&m_angRotation`, the LOCAL angles; the port has no local/abs
	// split (as 220).
	return Angles;
}
