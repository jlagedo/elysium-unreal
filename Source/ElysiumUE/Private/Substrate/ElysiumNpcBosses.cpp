#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBossesShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Bosses** — the unnamed bodies of the boss species between `0x10381000` and
// `0x1039a000`. This file carries the seams, `CNPC_VHengeyokai`'s pickup chain, `CNPC_VManBat`'s
// (the Sheriff's bat form) flight and carry chain and the melee-slot bodies of the
// `CNPC_VAndreiBlood` human line; `CNPC_VMingXiao`'s tentacle rules and pedestal pick were a second
// file. Story 5 step 4 moved every species body to its class's file. The declarations and the
// family's three standing facts are `Substrate/ElysiumNpcBosses.inl`; the walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// Every threshold below was read out of the pinned retail `vampire.dll`'s `.rdata` at its cited
// address (image base `0x10000000`), so the numbers are recovered facts and not estimates. Where an
// address is quoted with no number beside it, the datum lives past `.data`'s raw size and is filled
// at runtime — those are named as seams, never guessed. Where the decompiled C had lost the body
// (`0x103983d0`'s jump table and ST0 return) the LISTING was read instead and the section says so.

namespace
{

	// `CTraceFilterManBatNoIBeamEntity`'s one name, `DAT_10642d28`.
	constexpr const TCHAR* ManBatNoHitName = TEXT("lbeam*");

	// This image's `NPC_STATE` ordinals, which are NOT SDK 2013's: `0x1027e660`'s name table and
	// `0x1026e3e0`'s switch give **1 IDLE, 2 COMBAT, 3 ALERT, 7 DEAD**
	// (`docs/vtmb/npc-ai/conditions-and-states.md`). Families Anim, Conditions and Sounds each keep
	// an identical private copy; this is a fourth, because no family owns another's file.
	int32 BossesRetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}

}

// -------------------------------------------------------------------------------------------------
// The seams. Each answers nothing and names the retail call it stands for.
// -------------------------------------------------------------------------------------------------

FElysiumEntityHandle FElysiumNpc::CreatePhysAnimlink()
{
	// SEAM for `CBaseEntity::CreateNoSpawn(this, "phys_animlink", &vec3_origin)`. This runtime
	// registers no `phys_animlink` class, so there is nothing to create; the invalid handle is
	// retail's own failed-create answer.
	return FElysiumEntityHandle::Invalid();
}

int32 FElysiumNpc::LookupBoneByName(const TCHAR* BoneName) const
{
	// SEAM for `CBaseAnimating::GetModelPtr(-1)` and the `studiohdr_t` bone-table scan. No bone table
	// reaches the kernel here.
	(void)BoneName;
	return INDEX_NONE;
}

bool FElysiumNpc::RagdollElementForBone(const FElysiumEntity* Carried, int32 BoneIndex) const
{
	// SEAM for the `__RTDynamicCast` to `CRagdollProp` (`0x1057fff0`) and `+0x424`'s element lookup.
	(void)Carried;
	(void)BoneIndex;
	return false;
}

bool FElysiumNpc::RagdollBonePosition(const FElysiumEntity* InTarget, const TCHAR* BoneName,
	FVector& OutPositionUnits) const
{
	// SEAM for `0x10381e90`'s cast (`0x1057c684`), `+0x424`'s element lookup by NAME and the
	// element's `+0x94 GetPosition`.
	(void)InTarget;
	(void)BoneName;
	(void)OutPositionUnits;
	return false;
}

void FElysiumNpc::WirePhysAnimlink(const FElysiumEntityHandle& Link, int32 CarrierBone,
	int32 CarriedElement)
{
	// SEAM for `thunk_FUN_1014f210(link, this, bone, element, &vec3_origin, &vec3_origin)`.
	(void)Link;
	(void)CarrierBone;
	(void)CarriedElement;
}

void FElysiumNpc::RemovePhysAnimlink(const FElysiumEntityHandle& Link)
{
	// `thunk_FUN_101cd970(link)` -- `UTIL_RemoveImmediate` (`+0x268 |= 1`, slot `0x2d0`, then the
	// deleting destructor, slot 5) on the resolved link. Every caller resolves the handle first
	// (`0x1038291f`); this runtime's removal is `Kill()`. (Was a no-op seam; story 8 L08 integration.)
	if (World != nullptr && Link.IsSet())
	{
		if (FElysiumEntity* const Entity = World->Resolve(Link))
		{
			Entity->Kill();
		}
	}
}

void FElysiumNpc::SolveThrowImpulse(const FVector& FromUnits, const FVector& ToUnits,
	FVector& InOutImpulse) const
{
	// SEAM for `0x102c4cc0`. The recovered algebra, for whoever stands the solver:
	//     g   = sv_gravity * m_flGravity (+0x3ec) * _DAT_1044f030;
	//     d   = impulse.z * impulse.z - -(to.z - from.z) * g * _DAT_10450aa0;
	//     if (d < 0) d = 0;
	//     t   = (-impulse.z - sqrt(d)) / (g + g);
	//     impulse.x = t <= _DAT_1049a1c8 ? 0 : (to.x - from.x) / t;
	//     impulse.y = t <= _DAT_1049a1c8 ? 0 : (to.y - from.y) / t;
	// Note that the Z the caller supplied is NOT rewritten and that the negated Z term makes the
	// discriminant clamp reachable for an aim point above the launch point.
	(void)FromUnits;
	(void)ToUnits;
	(void)InOutImpulse;
}

void FElysiumNpc::ApplyThrowImpulse(const FElysiumEntityHandle& Carried, const FVector& ImpulseUnits)
{
	// SEAM for the `CRagdollProp` `+0x428` arm and the `IPhysicsObject` `+0xa0`/`+0x9c` arm.
	(void)Carried;
	(void)ImpulseUnits;
}

void FElysiumNpc::StartIgnoringCollision(const FElysiumEntityHandle& Other)
{
	// NO LONGER A SEAM. `0x102c4380` is family **TroikaHelpers**' row and it landed the body as
	// `FUN_102c4380` in `ElysiumNpcTroikaHelpers2.cpp`: it writes `IgnoreCollisionEntity`
	// (`+0x055c`) and pins `m_flIgnoreCollisionTimer` (`+0x6458`) to `FLT_MAX`. This name stays
	// because the boss call sites read better with it, and it now forwards rather than recording.
	FUN_102c4380(World != nullptr ? World->Resolve(Other) : nullptr);
}

void FElysiumNpc::ArmIgnoreCollisionExpiry(float Seconds)
{
	// Likewise `0x102c43b0`, landed as `FUN_102c43b0`: with an ignore live it renews
	// `m_flIgnoreCollisionExpire` to `curtime + Seconds` and re-checks through `0x102c43f0`, so a
	// zero or negative duration expires on the same call.
	FUN_102c43b0(Seconds);
}

void FElysiumNpc::CallFormBit(bool bSet)
{
	// `0x10381c00` — family **Misc** landed the body as `FElysiumNpc::FormBit`, so this forwards
	// rather than recording: the `CARRYING_BODY` bit, the `m_flFishTimer` (+0x666c) stamp and the
	// `m_bDidFakeThrow` (+0x667d) clear are all real now. The two counters stay because the two
	// Hengeyokai call sites are still asserted through them.
	++FormBitCalls;
	bLastFormBitArm = bSet;
	FormBit(bSet);
}

// -------------------------------------------------------------------------------------------------
// The pickup species table and the attach/release pair — `0x10382670`, `0x1038f430`, `0x10382400`,
// `0x1038f790`.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// Slot 482 `CanPlaySequence`, the species half — `0x103850a0`, `0x10396e90`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::CanPlaySequenceStateArm(int32 Result, bool bDisregardState, int32 InterruptLevel,
	EElysiumNpcState State, EElysiumNpcState IdealState)
{
	// The tail of `0x103850a0` / `0x10396e90` / `0x1035fd40` / `0x103bd270`, read from the LISTING
	// (`vtmb_asm 0x103850a0`, `0x1038512d`..`0x1038516f`) because the one instruction that matters
	// is a branchless mask the decompiler renders as arithmetic:
	//
	//     1038512d  MOV AL, [ESP+0xc]      ; param_1, fDisregardState
	//     10385135  MOV EAX, [ESI+0x5cc0]  ; m_NPCState      -> !=0, !=1
	//     10385144  CMP [ESI+0x5cc4], 1    ; m_IdealNPCState -> !=1
	//     1038514d  CMP EAX, 3 / JNZ ; CMP [ESP+0x10], 1 / JGE   ; ALERT with level >= 1 escapes
	//     10385159  XOR ECX,ECX / CMP EAX,4 / SETNZ CL / DEC ECX / AND ECX,EDI   ; state == 4 keeps
	//     1038516b  MOV EAX, EDI           ; the escape: the result stands
	//
	// **THIS IS THE ONE THING THE FOUR SPECIES BODIES DO THAT THE BASE DOES NOT.** The Troika line's
	// `0x10278090` (family **Anim**'s slot 482) reaches the same gate and answers a flat
	// `XOR EAX, EAX`; these four answer `((m_NPCState != 4) - 1) & result`, so a body in retail
	// state **4** keeps its 1-or-2 where the base would have refused. 29c's walk reads the four as
	// "byte-identical to `0x1035fd40`", which is true of the four but NOT of the base — that claim
	// is corrected here and in `docs/vtmb/npc-ai/shape.md`.
	//
	// Retail state 4 is `NPC_STATE_SCRIPT`. This image's `NPC_STATE` ordinals are NOT SDK 2013's:
	// `docs/vtmb/npc-ai/conditions-and-states.md` § "The species `SelectIdealState` overrides —
	// `0x10369060`, `0x103945a0`, `0x1039e310` (2026-09-13)" pins them off `0x1027e660`'s name table
	// and `0x1026e3e0`'s switch as **1 IDLE, 2 COMBAT, 3 ALERT, 7 DEAD**, which is why the `== 3`
	// escape is ALERT and the `== 4` survivor is SCRIPT. Families
	// Anim, Conditions and Sounds each keep a private copy of that mapping; this is a fourth, for
	// the same reason they are separate — no family owns another's file.
	//
	// This runtime's `EElysiumNpcState` has no NONE, so retail's `!= NONE && != IDLE` collapses to
	// `!= Idle`: both retail states take the same arm, so no answer changes.
	const int32 RetailState = BossesRetailNpcState(State);
	const int32 RetailIdealState = BossesRetailNpcState(IdealState);
	if (!bDisregardState && RetailState != 0 && RetailState != 1 && RetailIdealState != 1
		&& (RetailState != 3 || InterruptLevel < 1))
	{
		return RetailState == 4 ? Result : 0;
	}
	return Result;
}

int32 FElysiumNpc::CanPlaySequenceSpecies(bool bDisregardState, int32 InterruptLevel) const
{
	// The head, in retail's order, and identical to the Troika line's:
	//     result = 1;
	//     if (m_hCine resolves to a live entity) {
	//         if (!CineAllowsInterrupt(m_hCine)) return 0;       // 0x101a8ac0
	//         result = 2;
	//     }
	//     if (!IsAlive()) return 0;                              // slot 158 (+0x278)
	//     <the state arm, which is where these four diverge from the base>
	//
	// `ScriptOwnerIsLive` and `CineAllowsDynamicInteraction` are family **Anim**'s ports of the same
	// two reads (`ElysiumNpcAnim.cpp`), and `IsAlive` is family **Lifecycle**'s slot 158; all
	// three are called rather than re-stated, so the head has ONE answer across both bodies.
	int32 Result = 1;
	if (ScriptOwnerIsLive())
	{
		if (!CineAllowsDynamicInteraction())
		{
			return 0;
		}
		Result = 2;
	}
	if (!const_cast<FElysiumNpc*>(this)->IsAlive())
	{
		return 0;
	}
	return CanPlaySequenceStateArm(Result, bDisregardState, InterruptLevel, Mind.State(),
		Mind.IdealState());
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VManBat`'s velocity producer — `0x1038b370`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::ResetManBatStationaryWatch()
{
	NpcKernelBossesShared::GManBatWatch = FManBatStationaryWatch();
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VManBat`'s slot 102 trace filter.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::RetailNameMatches(const FString& EntityName, const TCHAR* NameOrWildcard)
{
	// The `NameMatches` idiom `0x10006c4e` inlines, as the listing spells it. `uVar4` is
	// `strlen(pattern) + 1`:
	//     if (uVar4 == 1)                      -> the answer is `m_iName == NULL_STRING`
	//     else if (pattern[strlen - 1] == '*') -> strnicmp(name, pattern, strlen - 1) == 0
	//     else                                 -> stricmp(name, pattern) == 0
	// So an EMPTY pattern matches only an unnamed entity, and the bare `"*"` matches everything
	// (its `strnicmp` length is zero). A null name is compared as the empty string, which is
	// retail's `DAT_106b8540` substitution.
	if (NameOrWildcard == nullptr)
	{
		return false;
	}
	const int32 PatternLength = FCString::Strlen(NameOrWildcard);
	if (PatternLength == 0)
	{
		return EntityName.IsEmpty();
	}
	if (NameOrWildcard[PatternLength - 1] == TEXT('*'))
	{
		return FCString::Strnicmp(*EntityName, NameOrWildcard, PatternLength - 1) == 0;
	}
	return FCString::Stricmp(*EntityName, NameOrWildcard) == 0;
}

bool FElysiumNpc::ManBatTraceFilterShouldHit(const FString& EntityName)
{
	// `CTraceFilterManBatNoIBeamEntity::ShouldHitEntity` `0x10006c4e`: an entity whose `m_iName`
	// (+0x26c) matches `"lbeam*"` (`DAT_10642d28`, read from the image) is NOT hit; anything else
	// falls through to `CTraceFilterSimple::ShouldHitEntity`, whose own answer is the ordinary
	// collision-group test and is true for the candidates this filter is asked about.
	//
	// The sweep that carried this filter (slot 102) is the collision service's since
	// story 0019/6 and this is the rule it keeps. No move path consults it yet: the movement seam's
	// ignore is a list (`IElysiumNpcMotor::SetMoveIgnore`), so the ManBat's move would register the
	// `lbeam*` entities ahead of the sweep — a wire into the motor families, not built here.
	return !RetailNameMatches(EntityName, ManBatNoHitName);
}

