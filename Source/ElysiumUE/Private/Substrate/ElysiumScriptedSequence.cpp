#include "Substrate/ElysiumScriptedSequence.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcKernelTunables.h"

#include "Misc/Paths.h"

// `scripted_sequence` — `CCineNPC`, the script director (story 5 fold A3). Every body below is the
// retail body at the address its comment names, read off the decompilation and, where a branch
// mattered, the listing (`vtmb_asm`). The walked prose is `docs/vtmb/npc-ai/authored-control.md`
// § "Scripted control and authority",
// `docs/vtmb/npc-ai/authored-control.md` § "Story 8, family Script19, the script directors"
// and
// `docs/specs/0003-scripted-sequence/spec.md`.
//
// The outputs are the load-bearing half: across the exported maps 88 wires leave these entities,
// 48 of them `OnEndSequence`, and they unlock doors, restore cameras and start conversations.
// `OnBeginSequence` fires from `StartScript` (`0x101a81a0`), which the possessed NPC's
// `TASK_WAIT_FOR_SCRIPT` reaches once the NPC stands on its mark and `IsTimeToStart` passes — NOT at
// the input. `OnEndSequence` fires from `SequenceDone` (`0x101a8460`) unconditionally, before any
// post-idle, spawnflag `0x100` included.

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSeq, Log, All);

namespace
{
	// `CCineNPC` spawnflags, by their retail readers.
	constexpr int32 GCineSfRepeatable = 0x4;            // `Finish` 0x101a8640: no SUB_Remove, self-chain allowed
	constexpr int32 GCineSfStartOnSpawn = 0x10;         // `Spawn` 0x101a6f10: arm `CineThink` on a named cine
	constexpr int32 GCineSfNoInterrupt = 0x20;          // `Spawn`: `m_interruptable = 0`; `AllowInterrupt` gate
	constexpr int32 GCineSfOverrideState = 0x40;        // slot 585 `0x101a7210`
	constexpr int32 GCineSfLeaveCorpsePose = 0x80;      // `CineCleanup` 0x1027d170: skip the bone-0 placement
	constexpr int32 GCineSfHoldPostIdle = 0x100;        // `Finish`: replay the post-idle forever
	constexpr int32 GCineSfPriority = 0x200;            // `CanOverride` 0x101a8ac0, read on the QUEUED cine
	constexpr int32 GCineSfCyclicSearch = 0x400;        // `0x101a7760`: `m_pLastFoundEntity` latch
	constexpr int32 GCineSfQuietSearch = 0x800;         // `FindEntity` 0x101a7600: no "can't play" line
	constexpr int32 GCineSfIgnoreNpcCollision = 0x1000; // `PossessEntity`: `m_bfAINPCFlags |= 0x40`

	// `m_scriptState` values (`+0x5d70`).
	constexpr int32 GScriptPlaying = 0;
	constexpr int32 GScriptWait = 1;
	constexpr int32 GScriptPostIdle = 2;
	constexpr int32 GScriptCleanup = 3;
	constexpr int32 GScriptWalkToMark = 4;
	constexpr int32 GScriptRunToMark = 5;
	constexpr int32 GScriptCustomMoveToMark = 6;

	// Retail `NPC_STATE` ids.
	constexpr int32 GNpcStateIdle = 1;
	constexpr int32 GNpcStateScript = 4;
	constexpr int32 GNpcStateDead = 7;

	// `_DAT_10445e08` — the 0.05 s start delay `BeginSequence` and `DelayStart` add to curtime.
	constexpr double GCineStartDelaySeconds = 0.05;
	// `_DAT_10449280` — `CineThink`'s retry and `Spawn`'s first think, 1.0 s.
	constexpr double GCineThinkDelaySeconds = ElysiumNpcTunables::OneDouble;
	// `_DAT_10449e10` — the "wait for BeginSequence" start time, curtime + 1e6.
	constexpr double GCineStartTimeFar = ElysiumNpcTunables::CineStartTimeOffset;
	// `_DAT_104493d0` — `Finish`'s `SUB_Remove`, curtime + 0.1.
	constexpr double GCineRemoveDelaySeconds = ElysiumNpcTunables::TenthDouble;

	// The selector-trace source lines retail stamps beside each `m_IdealNPCState` write
	// (`+0x1b40`), the mind's transition trace carries them.
	constexpr int32 GCinePossessLine = 0x2c8;        // `CCineNPC::PossessEntity` `0x101a7880`
	constexpr int32 GCineFixScheduleLine = 0x3ca;    // `CCineNPC::FixScriptNPCSchedule` `0x101a8840`
	constexpr int32 GCineCleanupIdleLine = 0x2b1d;   // `CineCleanup` `0x1027d170`, alive
	constexpr int32 GCineCleanupDeadLine = 0x2b22;   // `CineCleanup`, health below 1

	// `m_bfAINPCFlags` bit `0x40`, `NAV_IGNORE_NPC`: what spawnflag `0x1000` ORs in at possession.
	constexpr uint32 GNavIgnoreNpc = 0x40u;
	// `EF_NOINTERP`, the `m_fEffects` bit the case-4 teleport ORs in (`0x101a7e2f`).
	constexpr uint32 GEffectNoInterp = 0x10u;
	// `AddFlag2(0x10)` at the end of `Spawn`.
	constexpr uint32 GCineFlags2 = 0x10u;
	// `FSOLID_NOT_SOLID`, `SOLID_NONE`, `SOLID_BBOX`, `MOVETYPE_FLY`.
	constexpr uint32 GSolidFlagNotSolid = 0x4u;
	constexpr int32 GSolidNone = 0;
	constexpr int32 GSolidBbox = 2;
	constexpr int32 GMoveTypeNone = 0;
	constexpr int32 GMoveTypeFly = 4;
	// `CineCleanup`'s dead-cine arm: `SetSolidFlags(0x10)`.
	constexpr uint32 GSolidFlagsCleanupDeadCine = 0x10u;
	// `m_spawnflags` bit the NPC loses in `CineCleanup` (`& 0xffffff7f`).
	constexpr int32 GNpcSfWaitForScript = 0x80;

	FString CineDebugName(const FElysiumEntity& Entity)
	{
		if (!Entity.TargetName.IsEmpty())
		{
			return Entity.TargetName;
		}
		return Entity.Class != nullptr ? Entity.Class->ClassName.ToString() : FString();
	}

	double CineNow(const FElysiumEntity& Entity)
	{
		return Entity.World != nullptr ? Entity.World->NowSeconds() : 0.0;
	}
}

// --- Own slots ------------------------------------------------------------------------------------

// Slot 72: `0x101a6e20`.
bool FElysiumScriptedSequence::Slot72(int32 Discipline)
{
	(void)Discipline;
	return false;
}

// Slot 82: `0x101a5db0` returns `&datamap_CCineNPC`.
void* FElysiumScriptedSequence::GetDataDescMap()
{
	return const_cast<FElysiumClassDesc*>(FElysiumClassRegistry::Get().Find(FName(RetailClassName)));
}

// Slot 103: `0x101a6f10`, in retail's order. No base `Spawn`, so no `NPCInit`: a director never
// thinks as AI.
void FElysiumScriptedSequence::Spawn()
{
	// 1. `SetSolid(SOLID_NONE)` on `m_Collision` under its scope-trace frame.
	RetailSolidType = GSolidNone;
	++RetailSolidSets;
	// 2. `AddSolidFlags` inlined as `SetSolidFlags(word[+0x2b4] | FSOLID_NOT_SOLID)`.
	RetailSolidFlags = (RetailSolidFlags & 0xffffu) | GSolidFlagNotSolid;
	// 3. Slot 93 `SetMoveType(MOVETYPE_NONE, MOVECOLLIDE_DEFAULT)`, virtually.
	SetMoveType(GMoveTypeNone, 0);
	// 4. `m_bIsBCCTargetable = 0`, `m_bIsAlive = 0`.
	bIsBccTargetable = false;
	bNpcIsAlive = false;
	// 5. An unnamed cine, or spawnflag `0x10`: `ThinkSet(CineThink)`, `m_flNextThink = now + 1.0`,
	//    and a NAMED one waits for `BeginSequence` (`m_startTime = now + 1e6`).
	const double Now = CineNow(*this);
	if (TargetName.IsEmpty() || (SpawnFlags & GCineSfStartOnSpawn) != 0)
	{
		ArmCineThink(EThinkFunction::CineThink, Now + GCineThinkDelaySeconds);
		if (!TargetName.IsEmpty())
		{
			StartTime = Now + GCineStartTimeFar;
		}
	}
	// 6. `m_interruptable = !(spawnflags & 0x20)`.
	bInterruptable = (SpawnFlags & GCineSfNoInterrupt) == 0;
	// 7. `CBaseEntity::Relink` — no port counterpart (the port links no entity tree).
	// 8. `m_sequenceStarted = 0`, `m_hNextCine = -1`, `m_pLastFoundEntity = 0`, `AddFlag2(0x10)`.
	bSequenceStarted = false;
	NextCine = FElysiumEntityHandle::Invalid();
	LastFoundEntity = FElysiumEntityHandle::Invalid();
	AddFlag2(GCineFlags2);                                  // 0x101a70bb m_fFlags2 (+0x438)
}

// Slot 113: `0x101a8de0`. Plays NOTHING: retail's pre-idle belongs to `TASK_WAIT_FOR_SCRIPT`.
void FElysiumScriptedSequence::Activate()
{
	// `CBaseEntity::Activate(this)` — direct (`0x100a0bc0`).
	FElysiumEntity::Activate();

	// `while (a = FindEntityByName(a, m_iszEntity)) if (a->+0x94) break;` — the first entity of the
	// name that is an NPC base (`m_pBaseNPC`). A named marker or trigger sharing the actor's name is
	// skipped. A director is itself an NPC base, which is retail's own answer.
	FElysiumEntity* Actor = nullptr;
	if (World != nullptr && !TargetEntity.IsEmpty())
	{
		World->ForEachNamed(TargetEntity, [&Actor](FElysiumEntity& Candidate)
			{
				if (Actor == nullptr && Candidate.AsNpcBase() != nullptr)
				{
					Actor = &Candidate;
				}
			});
	}

	const TCHAR* const Divider = TEXT("--------------------");
	if (Actor == nullptr)
	{
		// `"Could not find NPC %s in CCineNPC::Activate (%s)"` — skips the precache.
		ActivateDiagnostics.Add(Divider);
		ActivateDiagnostics.Add(FString::Printf(TEXT("Could not find NPC %s in CCineNPC::Activate (%s)"),
			*TargetEntity, *DebugString()));
		ActivateDiagnostics.Add(Divider);
	}
	else if (Actor->Model.IsEmpty())
	{
		// SEAM for `CBaseAnimating::GetModelPtr`: this port resolves a model by name, so a non-empty
		// `Model` is the same question. Retail formats THIS cine's debug name first and discards it.
		ActivateDiagnostics.Add(Divider);
		ActivateDiagnostics.Add(FString::Printf(TEXT("NPC %s has no model in CCineNPC::Activate"),
			*Actor->DebugString()));
		ActivateDiagnostics.Add(Divider);
	}
	else
	{
		// `0x10428880(model, name)` on `m_iszPreIdle`, `m_iszPostIdle`, `m_iszPlay`, in that order.
		// SEAM: recorded, nothing acquired (no studio header at kernel level).
		for (const FString* Name : { &PreIdle, &PostIdle, &Play })
		{
			ActivatePrecacheLog.Add({ Actor->DebugString(), *Name });
		}
	}

	// `m_hNextCine = FindEntityByName(0, m_iszNextScript) ?: -1`, and a handle that does not
	// resolve clears `m_iszNextScript`.
	NextCine = FElysiumEntityHandle::Invalid();
	if (World != nullptr && !NextScript.IsEmpty())
	{
		if (FElysiumEntity* Next = World->FindByName(NextScript))
		{
			NextCine = Next->Handle;
		}
	}
	if (World == nullptr || World->Resolve(NextCine) == nullptr)
	{
		NextScript.Reset();
	}
}

// Slot 117: `0x101a6d20`.
int32 FElysiumScriptedSequence::ObjectCaps() const
{
	return FElysiumEntity::ObjectCaps() & ~ElysiumEntityCaps::AcrossTransition;
}

// Slot 180: `0x101a7140` (`CCineNPC::UpdateOnRemove`, filled by all three director classes). 19
// bytes, no branch.
void FElysiumScriptedSequence::UpdateOnRemove()
{
	FElysiumNpcBase::UpdateOnRemove();   // 0x101a7143 CAI_BaseNPC::UpdateOnRemove 0x1027ca30, DIRECT
	ScriptEntityCancel(*this);           // 0x101a7149 ScriptEntityCancel 0x101a7170(this)
}                                        // 0x101a7152 RET

// Slots 362–365.
bool FElysiumScriptedSequence::FInViewCone(const FVector& PointCm)
{
	(void)PointCm;
	return false;
}

bool FElysiumScriptedSequence::FInViewCone(FElysiumEntity* Candidate)
{
	(void)Candidate;
	return false;
}

bool FElysiumScriptedSequence::FInAimCone(const FVector& TargetCm)
{
	(void)TargetCm;
	return false;
}

bool FElysiumScriptedSequence::FInAimCone(FElysiumEntity* AimTarget)
{
	(void)AimTarget;
	return false;
}

// Slot 370: `0x101a6d40`, a tail call through slot 368.
FVector FElysiumScriptedSequence::HeadDirection2D()
{
	return BodyDirection2D();
}

// Slot 371: `0x101a6d70`, a tail call through slot 369.
FVector FElysiumScriptedSequence::HeadDirection3D()
{
	return BodyDirection3D();
}

// Slot 459: `0x101a89a0`. Reached from the NPC's own slot 459 (`0x1026d7f0`) while it is in SCRIPT.
void FElysiumScriptedSequence::RemoveIgnoredConditions()
{
	if (CanInterrupt())
	{
		return;
	}
	if (FElysiumNpcBase* Npc = TargetNpc())
	{
		ClearCineIgnoredConditions(*Npc);
	}
}

// --- Slots 583–586 --------------------------------------------------------------------------------

// Slot 583: `0x101a7880` (`CCineNPC::vfunc583`, `PossessEntity`), 1577 bytes. Reached from
// `CineThink` `0x101a8070`, the two inputs and `Finish`'s hand-off to a chained cine. Arms in the
// listing's order; the two `Msg` blocks under the debug ConVar `DAT_1072bb84`
// (`0x101a798d..0x101a7a34` "posession delayed", `0x101a7b77..0x101a7c1d` "is posessing entity")
// are dead in shipped play (the ConVar defaults off) and stay absent. Block one: the ConVar gate
// `0x101a7995` / `0x101a799a` / `0x101a79ab`, the `m_hTargetEnt` resolve `0x101a79ba` / `0x101a79da` /
// `0x101a79df` / `0x101a79ea` / `0x101a7a01`, the `GetDebugName`s `0x101a7a05` / `0x101a7a0f` /
// `0x101a7a1e`, the `Msg` `0x101a7a29`. Block two: the gate `0x101a7b7f` / `0x101a7b84` / `0x101a7b94`,
// the resolve `0x101a7ba3` / `0x101a7bc3` / `0x101a7bc8` / `0x101a7bd3` / `0x101a7bea`, the
// `GetDebugName`s `0x101a7bee` / `0x101a7bf8` / `0x101a7c07`, the `Msg` `0x101a7c12`.
void FElysiumScriptedSequence::PossessEntity()
{
	// `m_hTargetEnt` (`+0x5ce4`) -> entity -> `MyNPCPointer` (`+0x94`); any miss does NOTHING.
	FElysiumNpcBase* Npc = TargetNpc();   // 0x101a7888 / 0x101a78c2
	if (Npc == nullptr)
	{
		return;                           // 0x101a7891 / 0x101a78b2 / 0x101a78bc / 0x101a78ca -> 0x101a7ea2
	}
	if (!Npc->BaseScheduleHost.bRanAi)    // 0x101a78d0 m_bRanAI +0x1b4c, 0x101a78d8 JNZ
	{
		NotRunAiWarning(*Npc, TEXT("   that has not run it's AI yet.....")); // 0x101a78da..0x101a794f
	}

	// The QUEUE arm: the NPC's `m_hCine` (`+0x5d74`) resolves live. A busy NPC is queued behind its
	// owner, never stolen.
	if (World != nullptr && Npc->ScriptOwner.IsSet() && World->Resolve(Npc->ScriptOwner) != nullptr) // 0x101a7954 / 0x101a795d / 0x101a797e / 0x101a7987
	{
		FElysiumScriptedSequence* Old = Npc->ResolveCine();   // 0x101a7a39..0x101a7a62 (-1 0x101a7a48)
		if (Old == nullptr)
		{
			// **Named divergence (port ownership rule).** The port's choreographed scene also stands
			// in `ScriptOwner` for its cast; a retail scene never writes `m_hCine`, so retail would
			// write THIS cine into that entity's `+0x5f94` and return. The port refuses instead: the
			// owner is not a director and has no `m_hNextCine` to queue on.
			Diagnostic(FString::Printf(TEXT("%s: %s is held by a non-director owner; nothing queued"),
				*CineDebugName(*this), *CineDebugName(*Npc)));
			return;
		}
		// The kicked cine is re-read through a second `m_hCine` resolve (`0x101a7aa9` / `0x101a7ac3`) and
		// its `m_hNextCine` (`0x101a7ad4` / `0x101a7aeb`): the same entity as the test resolved.
		if (FElysiumEntity* Kicked = World->Resolve(Old->NextCine)) // 0x101a7a6a / 0x101a7a73 / 0x101a7a91 / 0x101a7a9a
		{
			if (FElysiumNpcBase* KickedBase = Kicked->AsNpcBase())
			{
				KickedBase->SetTarget(FElysiumEntityHandle::Invalid()); // 0x101a7af7 SetTarget(kicked, NULL)
			}
			Diagnostic(FString::Printf(TEXT("script \"%s\" kicking script \"%s\" out of the queue"),
				*CineDebugName(*this), *CineDebugName(*Kicked)));        // 0x101a7b06 / 0x101a7afe GetDebugName, 0x101a7b13 DevMsg(2, ...)
		}
		// `m_hCine` resolved a third time (`0x101a7b2b` / `0x101a7b44`); `this` is never null, so the
		// `-1` store of `0x101a7b4e JZ 0x101a7b66` is not reached.
		Old->NextCine = Handle;   // 0x101a7b54..0x101a7b5a current cine's m_hNextCine := our handle
		return;                   // 0x101a7b65
	}

	if (Npc->bHidden)             // 0x101a7c24 0x100b5190 (+0xf4 m_bScriptHidden), 0x101a7c2b
	{
		ScriptHiddenWarning(*Npc);   // 0x101a7c30 0x101a77a0
	}
	if (!bInterruptable)          // 0x101a7c35 m_interruptable +0x5f90, 0x101a7c3d
	{
		MakeNpcOblivious(*Npc);   // 0x101a7c41 0x1026d130
	}
	if (NextScript.IsEmpty())     // 0x101a7c46 m_iszNextScript +0x5f58, 0x101a7c4e
	{
		NextCine = FElysiumEntityHandle::Invalid();   // 0x101a7c50 m_hNextCine := -1
	}
	Npc->BaseScheduleHost.GoalEnt = Handle;   // 0x101a7c5a npc m_pGoalEnt +0x5de8 := this
	Npc->ScriptOwner = Handle;                // 0x101a7c64..0x101a7c6c npc m_hCine +0x5d74 := our handle
	Npc->SetTarget(Handle);                   // 0x101a7c72 SetTarget 0x10279cc0(npc, this)
	SavedMoveType = Npc->RetailMoveType;      // 0x101a7c7b slot 94 -> 0x101a7c81 +0x5f78
	SavedMoveCollide = Npc->RetailMoveCollide; // 0x101a7c8b slot 95 -> 0x101a7c91 +0x5f7c
	SavedSolid = Npc->RetailSolidType;        // 0x101a7c9b slot 92 -> 0x101a7ca1 +0x5f80
	SavedSolidFlags = static_cast<int32>(Npc->RetailSolidFlags); // 0x101a7cab slot 211 -> 0x101a7cb1 +0x5f84
	// `+0x5f88 := npc m_fEffects (+0x19c)` (`0x101a7cb7..0x101a7cbd`). The NPC's word is the Troika
	// tier's `EffectsWord` (`ElysiumNpcPositions.inl`); a base-only NPC carries none and saves 0. The
	// OR of the DIRECTOR's own `m_fEffects` onto the NPC at `0x101a7cfc..0x101a7d0a` below stays a
	// SEAM: the director carries no effects word (spec 0003's), so the OR adds nothing.
	const FElysiumNpc* EffectsNpc = Npc->AsNpc();
	SavedEffects = EffectsNpc != nullptr ? static_cast<int32>(EffectsNpc->EffectsWord) : 0;
	if (FElysiumNpc* Troika = Npc->AsNpc())   // 0x101a7cc3 npc +0x98, 0x101a7ccb
	{
		Troika->ResetThinkTimers(CineNow(*this));                          // 0x101a7cd1 slot 614
		SavedTroikaFlags = static_cast<int32>(Troika->NpcFlags.RawWord1()); // 0x101a7cd7..0x101a7cdd +0x5f8c
		if ((SpawnFlags & GCineSfIgnoreNpcCollision) != 0)                   // 0x101a7ce3 / 0x101a7cec
		{
			Troika->NpcFlags.AssignAiFlagsWord(Troika->NpcFlags.RawWord1() | GNavIgnoreNpc); // 0x101a7cf6
			// The collision view of `NAV_IGNORE_NPC` (`CBaseAnimating::IsIgnoreCollisionEntity` reads the
			// bit): the Unreal motor ignores characters. `CineCleanup`'s restore of `+0x5f8c` undoes it.
			Npc->SetIgnoreCharacterCollision(true);
		}
	}
	// `m_fMoveTo` (`+0x5f60`) through the six-entry table at `0x101a7eac`; above 5 skips the switch
	// (`0x101a7d19 JA 0x101a7e84`). Every state write is preceded by the empty `0x1027f270(state)`.
	switch (MoveTo)                                  // 0x101a7d1f JMP [table]
	{
	case 1:
		Npc->SetScriptState(GScriptWalkToMark);      // 0x101a7d26..0x101a7d33 npc +0x5d70 := 4 (0x1027f270 0x101a7d2a)
		DelayStart(true);                            // 0x101a7d3d DelayStart(1)
		break;
	case 2:
		Npc->SetScriptState(GScriptRunToMark);       // 0x101a7d47..0x101a7d54 npc +0x5d70 := 5 (0x1027f270 0x101a7d4b)
		DelayStart(true);                            // 0x101a7d5e
		break;
	case 3:
		Npc->SetScriptState(GScriptCustomMoveToMark); // 0x101a7d68..0x101a7d75 npc +0x5d70 := 6 (0x1027f270 0x101a7d6c)
		DelayStart(true);                            // 0x101a7d7f
		break;
	case 4:
		TeleportToMark(*Npc);                        // 0x101a7d89..0x101a7e6b
		[[fallthrough]];                             // falls into 0x101a7e71
	case 0:
	case 5:
		Npc->SetScriptState(GScriptWait);            // 0x101a7e71..0x101a7e7a npc +0x5d70 := 1 (0x1027f270 0x101a7e75)
		break;
	default:
		break;                                       // 0x101a7d19 JA 0x101a7e84
	}
	// The last statement: the NPC's own `MaintainSchedule` `0x102817c0` sees ideal != state, enters
	// SCRIPT and selects `SCHED_AISCRIPT` (`SelectSchedule` `0x1028a380` case 4).
	Npc->RequestIdealStateRetail(GNpcStateScript, GCinePossessLine); // 0x101a7e84..0x101a7e98 line 0x2c8, m_IdealNPCState := 4
}

void FElysiumScriptedSequence::NotRunAiWarning(const FElysiumNpcBase& Npc, const TCHAR* HasNotRunLine)
{
	// `DevMsg` through the import at `0x109f3630`, one line per call, verbatim. The trailing addresses
	// are `CCineNPC`'s (`0x101a7880`); `CCineAI` / `CCineAISchedule` cite theirs at the call site.
	Diagnostic(TEXT("************************************************"));                          // 0x101a78e5
	Diagnostic(TEXT("***  WARNING  **********************************"));                          // 0x101a78ec
	Diagnostic(FString::Printf(TEXT("   scripted sequence(%s)"), *CineDebugName(*this)));      // 0x101a78f3 / 0x101a78fe
	Diagnostic(FString::Printf(TEXT("   is targeting an entity(%s)"), *CineDebugName(Npc)));   // 0x101a7905 / 0x101a7910
	if (HasNotRunLine != nullptr)
	{
		Diagnostic(HasNotRunLine);                                                             // 0x101a7917
	}
	Diagnostic(TEXT("   This can happen if it starts hidden and is"));                              // 0x101a791e
	Diagnostic(TEXT("   immediately put into a script without a delay."));                          // 0x101a7925
	Diagnostic(TEXT("   If this is the case.  Put at least a .1 second"));                          // 0x101a792c
	Diagnostic(TEXT("   delay between ScriptUnhide and starting the"));                             // 0x101a7933
	Diagnostic(TEXT("   scripted sequence."));                                                      // 0x101a793a
	Diagnostic(TEXT("   Otherwise, talk to a programmer."));                                        // 0x101a7941
	Diagnostic(TEXT("***  WARNING  **********************************"));                          // 0x101a7948
	Diagnostic(TEXT("************************************************"));                          // 0x101a794f
}

void FElysiumScriptedSequence::TeleportToMark(FElysiumNpcBase& Npc) const
{
	// Slot 181 `Teleport(GetOrigin(), NULL, &vec3_origin)`: the origin moves, the angles do not (a
	// NULL angles pointer), the velocity is zeroed.
	Npc.SetRuntimeOrigin(Origin);                               // 0x101a7d96 slot 220 / 0x101a7d9f slot 181
	Npc.Velocity = FVector::ZeroVector;                          // 0x101a7d9f third argument vec3_origin
	// `0x102e0b40(m_pMotor)` — `motor+0x2c = -1.0`, the yaw-speed hold. SEAM, as `NPCInit` states it:
	// no motor word carries it.                                // 0x101a7dab
	float Yaw = static_cast<float>(Angles.Y);                    // 0x101a7dba slot 221, yaw at +4
	if (Npc.BaseScheduleHost.bMotorAnimationMovement)            // 0x101a7dc3 motor+0x28, 0x101a7dcc
	{
		Yaw = Yaw < MotorYawHalfTurn ? Yaw + MotorYawHalfTurn    // 0x101a7de3 JNZ -> 0x101a7ded FADD 180
			: Yaw - MotorYawHalfTurn;                            // 0x101a7de5 FSUB 180
	}
	// `motor+0x1c == 180.0f` writes `motor+0x34` directly, anything else goes through `0x102e0a80`.
	// SEAM: no motor `+0x1c`; `MotorIdealYaw` takes the direct write, as `NPCInit` does.
	Npc.MotorIdealYaw = Yaw;                                     // 0x101a7dfe / 0x101a7e04 (0x101a7e10 / 0x101a7e15)
	Npc.AngularVelocity = FVector::ZeroVector;                   // 0x101a7e1f SetLocalAngularVelocity(vec3_angle)
	// `npc->m_fEffects |= 0x10` (`EF_NOINTERP`, `0x101a7e24..0x101a7e2f`; the CCineAI twin
	// `0x101a933f..0x101a9344`), on the Troika tier's `EffectsWord`. No reader of bit 0x10 on an NPC
	// yet; `CineCleanup`'s restore of `+0x5f88` clears it again, as retail's does.
	if (FElysiumNpc* EffectsNpc = Npc.AsNpc())
	{
		EffectsNpc->EffectsWord |= GEffectNoInterp;
	}
	FVector NpcAngles = Npc.Angles;                              // 0x101a7e37 slot 221 on the NPC
	NpcAngles.Y = Angles.Y;                                      // 0x101a7e55..0x101a7e60 our yaw
	Npc.SetRuntimeAngles(NpcAngles);                             // 0x101a7e6b slot 64 SetAngles
}

// Slot 584: `0x101a82d0` (`StartSequence`), `CCineNPC` and `CCineAISchedule`. The `Msg` tail under the
// debug ConVar `DAT_1072bb84` (`0x101a8358..0x101a83fe`) is dead in shipped play and stays absent: the
// ConVar gate `0x101a8360` / `0x101a8365` / `0x101a8375`, the `m_hTargetEnt` resolve `0x101a8384` /
// `0x101a83a4` / `0x101a83a9` / `0x101a83b4` / `0x101a83cb`, the `GetDebugName`s `0x101a83cf` /
// `0x101a83d9` / `0x101a83e8` and the `Msg` `0x101a83f3`.
bool FElysiumScriptedSequence::StartSequence(FElysiumNpcBase& Npc, const FString& SequenceName,
	bool bCompleteOnEmpty)
{
	bSequenceStarted = true;                          // 0x101a82da m_sequenceStarted := 1, FIRST
	if (SequenceName.IsEmpty() && bCompleteOnEmpty)   // 0x101a82e1 null name / 0x101a82e9 low byte
	{
		SequenceDone(Npc);                            // 0x101a82f0 SequenceDone 0x101a8460
		return false;                                 // 0x101a82f9 RET, AL = 0
	}
	// A null name with `bCompleteOnEmpty` clear is looked up as "" (`0x101a8300`).
	Npc.SequenceNumber = Npc.LookupSequenceByName(*SequenceName); // 0x101a830d LookupSequence -> 0x101a8315 npc m_nSequence (+0x6f0)
	if (Npc.SequenceNumber == INDEX_NONE)             // 0x101a8312 / 0x101a831b
	{
		// `Warning("%s: unknown scripted sequence \"%s\"\n")` (`0x101a8334`); `GetDebugName(npc)`
		// `0x101a8329`; a null name prints "" (`0x101a831f`).
		UE_LOG(LogElysiumSeq, Log, TEXT("%s: unknown scripted sequence \"%s\""), *CineDebugName(Npc),
			*SequenceName);
		Npc.SequenceNumber = 0;                       // 0x101a833d m_nSequence := 0
	}
	Npc.SequenceCycle = 0.f;                          // 0x101a8349 m_flCycle (+0x6f8) := 0
	// `ResetSequenceInfo` `0x10090950` (thunk `0x100117b1`), where the sequence bridge starts the
	// clip; `TASK_PLAY_SCRIPT`'s run arm then waits the kernel's own `m_bSequenceFinished`.
	Npc.ResetSequenceInfo();                          // 0x101a8353
	return true;                                      // 0x101a8403..0x101a8408 AL = 1
}

// Slot 585: `0x101a7210`.
bool FElysiumScriptedSequence::FCanOverrideState() const
{
	return (SpawnFlags & GCineSfOverrideState) != 0;
}

// Slot 586: `0x101a8840`. `m_iFinishSchedule` is NOT read here.
void FElysiumScriptedSequence::FixScriptNPCSchedule(FElysiumNpcBase& Npc)
{
	if (Npc.IdealStateRetail() != GNpcStateDead)
	{
		Npc.RequestIdealStateRetail(GNpcStateIdle, GCineFixScheduleLine);
	}
	Npc.ClearSchedule();
}

// --- The think ------------------------------------------------------------------------------------

void FElysiumScriptedSequence::ArmCineThink(EThinkFunction Function, double At)
{
	ThinkFunction = Function;
	CineThinkAt = At;
	RescheduleThink();
}

void FElysiumScriptedSequence::RescheduleThink()
{
	const double Next = ThinkFunction != EThinkFunction::None ? CineThinkAt : ELYSIUM_NEVER_THINK;
	NextThink = Next >= ELYSIUM_NEVER_THINK ? ELYSIUM_NEVER_THINK : static_cast<float>(Next);
}

void FElysiumScriptedSequence::ThinkAt(double Now)
{
	// Only the installed `m_pfnThink` (`CineThink` `0x101a8070` or `SUB_Remove` `0x101c0b10`): Source
	// clears `m_flNextThink` before the call, so a think that does not re-arm itself stops.
	if (ThinkFunction != EThinkFunction::None && CineThinkAt < ELYSIUM_NEVER_THINK && Now >= CineThinkAt)
	{
		const EThinkFunction Function = ThinkFunction;
		CineThinkAt = ELYSIUM_NEVER_THINK;
		if (Function == EThinkFunction::CineThink)
		{
			CineThink(Now);
		}
		else if (Function == EThinkFunction::SubRemove)
		{
			RemoveSelf();
			return;
		}
	}
	RescheduleThink();
}

// `CineThink` `0x101a8070`.
void FElysiumScriptedSequence::CineThink(double Now)
{
	if (FindEntityWrap())
	{
		bSequenceStarted = false;
		PossessEntity();   // slot 583, virtual
		Diagnostic(FString::Printf(TEXT("script \"%s\" using NPC \"%s\""), *CineDebugName(*this),
			*TargetEntity));
		return;
	}
	CancelScript();
	Diagnostic(FString::Printf(TEXT("script \"%s\" can't find NPC \"%s\""), *CineDebugName(*this),
		*TargetEntity));
	// `m_flNextThink = now + 1.0` — the function stays `CineThink`: retry forever.
	ArmCineThink(EThinkFunction::CineThink, Now + GCineThinkDelaySeconds);
}

void FElysiumScriptedSequence::RemoveSelf()
{
	// `SUB_Remove` `0x101c0b10` -> `UTIL_Remove`, whose first act is slot 180.
	if (IsDead())
	{
		return;
	}
	UpdateOnRemove();
	Kill();
}

// --- Target acquisition ---------------------------------------------------------------------------

FElysiumEntity* FElysiumScriptedSequence::FindGenericWithin(FElysiumEntity* Start, const FString& Name,
	const FVector& CenterCm, float RadiusUnits)
{
	if (World == nullptr || Name.IsEmpty())
	{
		return nullptr;
	}
	// `0x100f7c30` / `0x100f7e30`: a zero radius is unbounded; otherwise the candidate's origin must lie
	// STRICTLY inside it (`d² < r²`). The port's origins are centimetres, `m_flRadius` is authored in
	// Source units. Retail also requires the candidate's `m_Network.m_pPev` (`+0x2e0`, the edict) to be
	// non-null; SEAM: the port has no edicts, and `!IsDead()` (a removed entity has none) stands in.
	const double RadiusCm = static_cast<double>(RadiusUnits) * ElysiumMove::U;
	const bool bBounded = RadiusUnits * RadiusUnits != 0.f;
	auto Within = [&CenterCm, RadiusCm, bBounded](const FElysiumEntity& Candidate)
	{
		return !Candidate.IsDead()
			&& (!bBounded || FVector::DistSquared(Candidate.Origin, CenterCm) < RadiusCm * RadiusCm);
	};
	// `FindEntityByName`'s procedural names answer one entity, and only to a search from the start;
	// the radius gate applies to that entity like any other (`0x100f7c30` tests whatever
	// `0x100f7770` returns). No classname fallback follows a procedural name: `0x100f7e30` would
	// compare the literal `!name` against classnames and match nothing.
	if (Name.StartsWith(TEXT("!")))
	{
		if (Start != nullptr)
		{
			return nullptr;
		}
		FElysiumEntity* Hit = ResolveProceduralName(Name);
		return Hit != nullptr && Within(*Hit) ? Hit : nullptr;
	}
	const TArray<TUniquePtr<FElysiumEntity>>& List = World->Entities();
	const int32 First = Start != nullptr ? Start->Handle.Index + 1 : 0;
	// `0x100f7f70`: the next name match after `Start`, else the next CLASSNAME match after it.
	for (int32 Index = First; Index < List.Num(); ++Index)
	{
		FElysiumEntity* Candidate = List[Index].Get();
		if (Candidate != nullptr && !Candidate->IsDead() && !Candidate->TargetName.IsEmpty()
			&& FElysiumEntityWorld::NameMatches(Candidate->TargetName, Name) && Within(*Candidate))
		{
			return Candidate;
		}
	}
	for (int32 Index = First; Index < List.Num(); ++Index)
	{
		FElysiumEntity* Candidate = List[Index].Get();
		const FString Classname = Candidate != nullptr && Candidate->Def != nullptr
			? Candidate->Def->Classname : FString();
		if (Candidate != nullptr && !Candidate->IsDead() && !Classname.IsEmpty()
			&& FElysiumEntityWorld::NameMatches(Classname, Name) && Within(*Candidate))
		{
			return Candidate;
		}
	}
	return nullptr;
}

FElysiumEntity* FElysiumScriptedSequence::ResolveProceduralName(const FString& Name)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	if (Name.Equals(TEXT("!player"), ESearchCase::IgnoreCase))
	{
		return World->FindPlayer();
	}
	if (Name.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
	{
		return World->FindPlayerController();   // the player's `m_hControllerNPC`
	}
	if (Name.Equals(TEXT("!pvsplayer"), ESearchCase::IgnoreCase))
	{
		// SEAM: retail answers the player in the searching entity's PVS (`0x101d1800`); the port
		// has no PVS query here, so the player stands in.
		return World->FindPlayer();
	}
	if (Name.Equals(TEXT("!activator"), ESearchCase::IgnoreCase))
	{
		return World->Resolve(LastInputActivator);   // activator arg 0 -> searching `+0x10c`
	}
	if (Name.Equals(TEXT("!caller"), ESearchCase::IgnoreCase))
	{
		return World->Resolve(LastInputCaller);      // searching `+0x110`
	}
	if (Name.Equals(TEXT("!picker"), ESearchCase::IgnoreCase))
	{
		// SEAM: the entity under the host player's crosshair (`0x10172710`), a debug verb.
		return nullptr;
	}
	UE_LOG(LogElysiumSeq, Log, TEXT("Invalid entity search name %s"), *Name);
	return nullptr;
}

void FElysiumScriptedSequence::RecordInput(const FElysiumInputArgs& Args)
{
	LastInputActivator = Args.Activator;
	LastInputCaller = Args.Caller;
}

// `FindEntity` `0x101a7600`.
FElysiumNpcBase* FElysiumScriptedSequence::FindEntity()
{
	FElysiumNpcBase* Fallback = nullptr;
	// The walk starts at `m_pLastFoundEntity` and stops at the list's end; the centre is this cine's
	// `WorldSpaceCenter` (slot 220), its origin for a point entity.
	FElysiumEntity* Cursor = World != nullptr ? World->Resolve(LastFoundEntity) : nullptr;
	for (FElysiumEntity* Hit = FindGenericWithin(Cursor, TargetEntity, Origin, Radius); Hit != nullptr;
		Hit = FindGenericWithin(Hit, TargetEntity, Origin, Radius))
	{
		FElysiumNpcBase* Npc = Hit->AsNpcBase();
		if (Npc == nullptr)
		{
			continue;
		}
		// Slot 482 `CanPlaySequence(FCanOverrideState(), 0)`: 1 wins at once, the LAST 2 is kept.
		const int32 Answer = Npc->CanPlaySequence(FCanOverrideState(), 0);
		if (Answer == 1)
		{
			return Npc;
		}
		if (Answer == 2)
		{
			Fallback = Npc;
		}
		else if ((SpawnFlags & GCineSfQuietSearch) == 0)
		{
			UE_LOG(LogElysiumSeq, Log, TEXT("Found %s, but can't play!"), *TargetEntity);
			Diagnostic(FString::Printf(TEXT("Found %s, but can't play!"), *TargetEntity));
		}
	}
	return Fallback;
}

// `0x101a7760`.
bool FElysiumScriptedSequence::FindEntityWrap()
{
	FElysiumNpcBase* Found = FindEntity();
	if ((SpawnFlags & GCineSfCyclicSearch) != 0)
	{
		LastFoundEntity = Found != nullptr ? Found->Handle : FElysiumEntityHandle::Invalid();
	}
	SetTarget(Found != nullptr ? Found->Handle : FElysiumEntityHandle::Invalid());
	return Found != nullptr;
}

FElysiumNpcBase* FElysiumScriptedSequence::TargetNpc() const
{
	FElysiumEntity* Resolved = World != nullptr ? World->Resolve(GetTarget()) : nullptr;
	return Resolved != nullptr ? Resolved->AsNpcBase() : nullptr;
}

// --- Interruption and cleanup ---------------------------------------------------------------------

// `ScriptEntityCancel` `0x101a7170`.
void FElysiumScriptedSequence::ScriptEntityCancel(FElysiumEntity& Entity)
{
	// The `+0x4c & 0x1000` gate is "this entity is a cine" (the constructor sets it).
	FElysiumNpcBase* AsBase = Entity.AsNpcBase();
	FElysiumScriptedSequence* Cine = AsBase != nullptr ? AsBase->AsSpecies<FElysiumScriptedSequence>() : nullptr;
	if (Cine == nullptr)
	{
		return;
	}
	// The TARGET's state, not whether this cine owns it: retail does not compare `m_hCine`.
	if (FElysiumNpcBase* Npc = Cine->TargetNpc())          // 0x101a717f..0x101a71b6 m_hTargetEnt, +0x94
	{
		if (Npc->NpcStateRetail() == GNpcStateScript)      // 0x101a71b8 npc +0x5cc0 == 4
		{
			Npc->SetScriptState(GScriptCleanup);           // 0x101a71c5 0x1027f270(3) / 0x101a71cc npc +0x5d70 := 3
			CineCleanup(*Npc);                             // 0x101a71d6
		}
	}
	Cine->Delay = 0;                                       // 0x101a71db m_iDelay := 0
}

// `CancelScript` `0x101a8c30`, 130 bytes. The name it sweeps is `m_iName` (`+0x26c`, this cine's
// own targetname), not `m_target` (`+0x20c`) as the checklist row spells it.
void FElysiumScriptedSequence::CancelScript()
{
	// `DevMsg(2, "Cancelling script: %s\n", m_iszPlay ?: "")` through `0x109f3650`.
	Diagnostic(FString::Printf(TEXT("Cancelling script: %s"), *Play));   // 0x101a8c34 / 0x101a8c3c / 0x101a8c4b
	if (World == nullptr || TargetName.IsEmpty())                          // 0x101a8c51 / 0x101a8c5c
	{
		ScriptEntityCancel(*this);                                         // 0x101a8c5f ScriptEntityCancel(this)
		return;                                                            // 0x101a8c69
	}
	// `for (e = FindEntityByName(NULL, m_iName); e; e = FindEntityByName(e, m_iName ?: ""))`
	// `ScriptEntityCancel(e)`. The names are collected first; nothing a cancel does renames an
	// entity, so the order and the set are retail's.
	TArray<FElysiumEntityHandle> Named;
	// The loop re-reads `m_iName` for each next search (`0x101a8c87`), "" when null (`0x101a8c92`).
	World->ForEachNamed(TargetName, [&Named](FElysiumEntity& Candidate) { Named.Add(Candidate.Handle); }); // 0x101a8c76 / 0x101a8ca4
	for (const FElysiumEntityHandle& Each : Named)                         // 0x101a8c7f / 0x101a8cad
	{
		if (FElysiumEntity* Entity = World->Resolve(Each))
		{
			ScriptEntityCancel(*Entity);                                   // 0x101a8c82
		}
	}
}                                                                          // 0x101a8cb1

// `CineCleanup` `0x1027d170`, on the NPC.
void FElysiumScriptedSequence::CineCleanup(FElysiumNpcBase& Npc)
{
	Npc.SetScriptState(GScriptPlaying);                // 0x1027d17b 0x1027f270(0) / 0x1027d180 npc +0x5d70 := 0
	FElysiumScriptedSequence* Cine = Npc.ResolveCine(); // 0x1027d199 m_hCine (+0x5d74), held in EBX
	if (Cine == nullptr)
	{
		// A dead `m_hCine`: `SetMoveType(MOVETYPE_FLY, 0)`, `SetSolid(SOLID_BBOX)`, `SetSolidFlags(0x10)`.
		Npc.SetMoveType(GMoveTypeFly, 0);
		Npc.RetailSolidType = GSolidBbox;
		++Npc.RetailSolidSets;
		Npc.RetailSolidFlags = GSolidFlagsCleanupDeadCine;
	}
	else
	{
		if (!Cine->bInterruptable)
		{
			ReleaseNpcOblivious(Npc);   // 0x10007ea0
		}
		Cine->SetTarget(FElysiumEntityHandle::Invalid());
		Npc.SetMoveType(Cine->SavedMoveType, Cine->SavedMoveCollide);
		Npc.RetailSolidFlags = static_cast<uint32>(Cine->SavedSolidFlags) & 0xffffu;
		if (FElysiumNpc* Troika = Npc.AsNpc())
		{
			Troika->EffectsWord = static_cast<uint32>(Cine->SavedEffects);   // 0x1027d271 / 0x1027d279 m_fEffects := +0x5f88
			Troika->NpcFlags.AssignAiFlagsWord(static_cast<uint32>(Cine->SavedTroikaFlags));
		}
		// The collision view of the restored `NAV_IGNORE_NPC` bit, which a `0x1000` cine's
		// `PossessEntity` turned on (`IsIgnoreCollisionEntity` reads the bit).
		if ((Cine->SpawnFlags & GCineSfIgnoreNpcCollision) != 0)
		{
			if (const FElysiumNpc* Troika = Npc.AsNpc())
			{
				Npc.SetIgnoreCharacterCollision(Troika->NpcFlags.Has(EElysiumNpcFlag::NAV_IGNORE_NPC));
			}
		}
	}

	// `m_hCine = -1`, `SetTarget(npc, NULL)`, `m_pGoalEnt = 0`.
	Npc.ScriptOwner = FElysiumEntityHandle::Invalid();
	Npc.SetTarget(FElysiumEntityHandle::Invalid());
	Npc.BaseScheduleHost.GoalEnt = FElysiumEntityHandle::Invalid();   // 0x1027d2b4

	// `m_lifeState != LIFE_DYING` (`0x1027d2ba` `+0x200 == 1` -> `0x1027d493`; the entity's
	// `LifeState`). NAMED GAP: the dying arm -- health 0, not-solid, `SetState(DEAD)`, the corpse
	// bounds -- is not ported in this body yet, so it is not reached from here.
	// The placement block runs on the cine resolved at entry (`0x1027d493` EBX), its `m_iszPlay`
	// (`0x1027d49b`) and `m_sequenceStarted` (`0x1027d4a7`).
	if (Cine != nullptr && !Cine->Play.IsEmpty() && Cine->bSequenceStarted)
	{
		// SEAM, both placement arms: the kernel reads no bone. Spawnflag `0x2000` (`0x1027d4c3`) ->
		// `MoveToBoneOriginAngles("Bip01", 1, 1)` (`0x1027d4d0`) and the origin's Z lowered by
		// `0x10006b13(+0x1568) * _DAT_104454d0` (`0x1027d4fa..0x1027d519`); else, unless spawnflag
		// `0x80` (`0x1027d529`), the bone-0 position (`0x1027d53c GetBonePosition`, kept at the origin
		// within `_DAT_1049a148`), `+1` Z, `FL_ONGROUND`, the floor probe `0x101cd250`, the angles and
		// `EF_NOINTERP` (`0x1027d6c6..0x1027d6ce`). No played-clip root reaches the kernel, so the body
		// stays where the scene left it and neither arm moves it.
		(void)GCineSfLeaveCorpsePose;
		// `m_Activity := 0` (`0x1027d6d4`), after either arm and with `0x80` set too: the next
		// `SetActivity` re-resolves even when it asks for the activity the scene started from.
		Npc.ActivityNumber = 0;
	}
	if (Npc.Health < 1)
	{
		Npc.RequestIdealStateRetail(GNpcStateDead, GCineCleanupDeadLine);
		Npc.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);   // SetCondition(0x4c)
	}
	else
	{
		Npc.RequestIdealStateRetail(GNpcStateIdle, GCineCleanupIdleLine);
	}
	Npc.SpawnFlags &= ~GNpcSfWaitForScript;
}

void FElysiumScriptedSequence::MakeNpcOblivious(FElysiumNpcBase& Npc)
{
	// `0x1026d130`, whole.
	ElysiumNpcEnemy::SetEnemy(Npc, FElysiumEntityHandle::Invalid());
	Npc.DisconnectFromSquad();
	++Npc.ObliviousCount;
}

void FElysiumScriptedSequence::ReleaseNpcOblivious(FElysiumNpcBase& Npc)
{
	// `0x10007ea0`, whole.
	Npc.ObliviousCount = FMath::Max(0, Npc.ObliviousCount - 1);
	Npc.ReconnectToSquad();
}

void FElysiumScriptedSequence::ScriptHiddenWarning(FElysiumNpcBase& Npc) const
{
	// `0x101a77a0`: `m_bCineScriptHidden = 1`, then the block of `DevWarning`s.
	Npc.bCineScriptHidden = true;
	UE_LOG(LogElysiumSeq, Warning, TEXT("Attempting to play a scripted sequence (%s) on a hidden NPC (%s). "
		"Performing voodoo flag magic."), *CineDebugName(*this), *CineDebugName(Npc));
}

// `CanInterrupt` `0x101a8930`.
bool FElysiumScriptedSequence::CanInterrupt() const
{
	if (!bInterruptable)
	{
		return false;
	}
	FElysiumNpcBase* Npc = TargetNpc();
	return Npc != nullptr && Npc->IsAlive();   // slot 158
}

// `CanOverride` `0x101a8ac0`, asked of the cine an NPC already holds.
bool FElysiumScriptedSequence::CanOverride() const
{
	FElysiumEntity* Next = World != nullptr ? World->Resolve(NextCine) : nullptr;
	if (Next == nullptr)
	{
		return true;
	}
	if (!NextScript.IsEmpty())
	{
		UE_LOG(LogElysiumSeq, Log, TEXT("%s is specified as the 'Next Script' and cannot be kicked out "
			"of the queue"), *CineDebugName(*Next));
		return false;
	}
	// The QUEUED cine's spawnflag `0x200`, not this one's.
	if ((Next->SpawnFlags & GCineSfPriority) == 0)
	{
		return true;
	}
	UE_LOG(LogElysiumSeq, Log, TEXT("%s is a priority script and cannot be kicked out of the queue"),
		*CineDebugName(*Next));
	return false;
}

// `AllowInterrupt` `0x101a8890`, 123 bytes; one caller, the NPC's `HandleAnimEvent` `0x10274e30`
// script-event arm (spec 0003's, unbuilt). RAISING the latch resumes the NPC's AI; clearing it
// suspends it.
void FElysiumScriptedSequence::AllowInterrupt(bool bAllow)
{
	if ((SpawnFlags & GCineSfNoInterrupt) != 0)   // 0x101a8893 TEST +0x204, 0x20 / 0x101a889a JNZ
	{
		return;                                   // 0x101a8907 -> 0x101a8908
	}
	if (FElysiumNpcBase* Npc = TargetNpc())       // 0x101a889c..0x101a88d7 m_hTargetEnt (-1 0x101a88aa, serial 0x101a88c7, null 0x101a88cd), +0x94
	{
		if (!bInterruptable)                      // 0x101a88d9 +0x5f90 / 0x101a88e1 JZ 0x101a88f7
		{
			if (bAllow)                           // 0x101a88f9
			{
				ReleaseNpcOblivious(*Npc);        // 0x101a88fb 0x10007ea0
			}
		}
		else if (!bAllow)                         // 0x101a88e3 / 0x101a88e5
		{
			MakeNpcOblivious(*Npc);               // 0x101a88e7 0x1026d130
			bInterruptable = false;               // 0x101a88ec
			return;                               // 0x101a88f4
		}
	}
	bInterruptable = bAllow;                      // 0x101a8900 +0x5f90 := argument
}

void FElysiumScriptedSequence::FireScriptEvent(int32 Index)
{
	static const FName Names[] = {
		TEXT("OnScriptEvent01"), TEXT("OnScriptEvent02"), TEXT("OnScriptEvent03"), TEXT("OnScriptEvent04"),
		TEXT("OnScriptEvent05"), TEXT("OnScriptEvent06"), TEXT("OnScriptEvent07"), TEXT("OnScriptEvent08"),
	};
	if (Index >= 0 && Index < static_cast<int32>(UE_ARRAY_COUNT(Names)))
	{
		FireOutput(Names[Index], LastInputActivator);
	}
}

// --- The start gate -------------------------------------------------------------------------------

// `DelayStart` `0x101a8cf0`.
void FElysiumScriptedSequence::DelayStart(bool bIncrement)
{
	if (World == nullptr)
	{
		return;
	}
	TArray<FElysiumEntityHandle> Named;
	World->ForEachNamed(TargetName, [&Named](FElysiumEntity& Candidate) { Named.Add(Candidate.Handle); });
	const double Now = CineNow(*this);
	for (const FElysiumEntityHandle& Each : Named)
	{
		FElysiumEntity* Entity = World->Resolve(Each);
		// The literal classname `scripted_sequence` — an `aiscripted_sequence` sharing the name is
		// skipped, and so is any other entity.
		const FString Classname = Entity != nullptr && Entity->Def != nullptr ? Entity->Def->Classname
			: FString();
		FElysiumNpcBase* AsBase = Entity != nullptr ? Entity->AsNpcBase() : nullptr;
		FElysiumScriptedSequence* Cine = AsBase != nullptr ? AsBase->AsSpecies<FElysiumScriptedSequence>() : nullptr;
		if (Cine == nullptr || !Classname.Equals(TEXT("scripted_sequence"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (!bIncrement)
		{
			--Cine->Delay;
			if (Cine->Delay < 1)
			{
				Cine->Delay = 0;
				Cine->StartTime = Now + GCineStartDelaySeconds;
			}
		}
		else
		{
			++Cine->Delay;
		}
	}
}

// `IsTimeToStart` `0x101a7540`.
bool FElysiumScriptedSequence::IsTimeToStart() const
{
	return Delay < 1 && StartTime <= CineNow(*this);
}

// `0x101a8130`.
FElysiumScriptedSequence* FElysiumScriptedSequence::LinkedSequence() const
{
	if (World == nullptr || LinkedSequenceName.IsEmpty())
	{
		return nullptr;
	}
	FElysiumEntity* Hit = World->FindByName(LinkedSequenceName);
	FElysiumNpcBase* AsBase = Hit != nullptr ? Hit->AsNpcBase() : nullptr;   // `+0x94` non-null
	return AsBase != nullptr ? AsBase->AsSpecies<FElysiumScriptedSequence>() : nullptr;   // RTDynamicCast
}

// `StartScript` `0x101a81a0`.
void FElysiumScriptedSequence::StartScript()
{
	if (FElysiumScriptedSequence* Linked = LinkedSequence())   // 0x101a8130
	{
		if (FElysiumNpcBase* LinkedNpc = Linked->TargetNpc())  // linked m_hTargetEnt (+0x5ce4), +0x94
		{
			LinkedNpc->TaskComplete(false);                    // TaskComplete(npc, 0): its TASK_WAIT_FOR_SCRIPT
			if (Linked->LinkedSequence() != this)
			{
				Linked->StartScript();                         // 0x101a81a0, recursive
			}
			Linked->StartSequence(*LinkedNpc, Linked->Play, true); // linked slot 584 (npc, m_iszPlay +0x5f48, 1)
			if (LinkedNpc->bSequenceFinished)                  // npc +0x65c
			{
				LinkedNpc->ClearSchedule();
			}
			if (FElysiumNpc* LinkedTroika = LinkedNpc->AsNpc())
			{
				LinkedTroika->SequencePlaybackRate = 1.f;      // npc m_flPlaybackRate (+0x6f4) := 1.0
			}
		}
	}
	FireOutput(FName(TEXT("OnBeginSequence")), LastInputActivator); // m_OnBeginSequence (+0x5f9c), 0x100cd660
}

// `SequenceDone` `0x101a8460`, 371 bytes, three direct callers and no slot. The `Msg` block under the
// debug ConVar `DAT_1072bb84` (`0x101a8463..0x101a850b`, "Sequence %s targeting %s is done" and the
// `0x101a6ec0` stack dump) is dead in shipped play and stays absent: the ConVar gate `0x101a846c` /
// `0x101a8471` / `0x101a8482`, the `m_hTargetEnt` resolve `0x101a8491` / `0x101a84b1` / `0x101a84b6` /
// `0x101a84c1` / `0x101a84d8`, the three `GetDebugName`s `0x101a84dc` / `0x101a84e6` / `0x101a84f5` and
// the `Msg` `0x101a8500`.
void FElysiumScriptedSequence::SequenceDone(FElysiumNpcBase& Npc)
{
	FElysiumEntity* Next = World != nullptr ? World->Resolve(NextCine) : nullptr; // 0x101a851a..0x101a8545 (-1 0x101a8523, serial 0x101a8540)
	if (PostIdle.IsEmpty() || Next != nullptr)    // 0x101a8510 / 0x101a8518 JZ, 0x101a8545 JNZ
	{
		Finish(Npc);                              // 0x101a857b Finish 0x101a8640
	}
	else
	{
		// `0x1027f270(2)` (empty), then the NPC's `m_scriptState := 2` and OUR slot 584 with
		// `(npc, m_iszPostIdle, 0)`: the NPC stays possessed.
		Npc.SetScriptState(GScriptPostIdle);      // 0x101a854f / 0x101a8554 npc +0x5d70 := 2
		StartSequence(Npc, PostIdle, false);      // 0x101a856c slot 584
	}
	// `m_OnEndSequence` (`+0x5fb4`) through `0x100cd660`, LAST and UNCONDITIONALLY, with
	// `m_hLastInputActivator` (`+0x10c`, `0x101a8580`) when it resolves and NULL when it does not
	// (`0x101a85be`; -1 `0x101a8589`, serial `0x101a85a6`), caller `this`.
	FireOutput(FName(TEXT("OnEndSequence")), LastInputActivator); // 0x101a8580..0x101a85c9
}

// `Finish` `0x101a8640` (`PostIdleDone`), 396 bytes, reached from `SequenceDone` and from the
// NPC's `TASK_PLAY_SCRIPT_POST_IDLE` / `0x62` run arms.
void FElysiumScriptedSequence::Finish(FElysiumNpcBase& Npc)
{
	FElysiumEntity* Next = World != nullptr ? World->Resolve(NextCine) : nullptr; // 0x101a8662..0x101a8690
	if (!PostIdle.IsEmpty()                                   // 0x101a8645 / 0x101a864d
		&& (SpawnFlags & GCineSfHoldPostIdle) != 0            // 0x101a8653 / 0x101a865c TEST AH,1
		&& Next == nullptr)                                   // 0x101a8671 / 0x101a868b / 0x101a8690
	{
		// `DevMsg(2, "Post Idle %s finished\n", npc->m_hCine->m_iszPostIdle ?: "")`: the string is the
		// NPC's OWNING cine's, re-resolved from `npc +0x5d74`. A stale `m_hCine` makes retail read
		// `[NULL + 0x5f4c]` (`0x101a86bc` / `0x101a86be`) and fault; the port prints the empty
		// string instead (crash guard).
		const FElysiumScriptedSequence* Owner = Npc.ResolveCine();   // 0x101a8696..0x101a86b8 (-1 0x101a869f, serial 0x101a86b6)
		Diagnostic(FString::Printf(TEXT("Post Idle %s finished"),
			Owner != nullptr ? *Owner->PostIdle : TEXT("")));        // 0x101a86be..0x101a86d5 (null string 0x101a86c6)
		Npc.SetScriptState(GScriptPostIdle);                         // 0x101a86e2 0x1027f270(2) / 0x101a86e7 npc +0x5d70 := 2
		StartSequence(Npc, PostIdle, false);                         // 0x101a86ff slot 584 (npc, m_iszPostIdle, 0)
		return;                                                     // 0x101a8708, BEFORE any cleanup
	}
	if ((SpawnFlags & GCineSfRepeatable) == 0)                       // 0x101a870b / 0x101a8712
	{
		// `ThinkSet(SUB_Remove)` (`0x10015b68` -> `0x101c0b10`), `m_flNextThink = curtime + 0.1`.
		ArmCineThink(EThinkFunction::SubRemove, CineNow(*this) + GCineRemoveDelaySeconds); // 0x101a871f / 0x101a8733
	}
	CineCleanup(Npc);                                                // 0x101a873f 0x1027d170
	FixScriptNPCSchedule(Npc);                                       // 0x101a8749 slot 586, virtual
	Next = World != nullptr ? World->Resolve(NextCine) : nullptr;    // 0x101a874f..0x101a877d, re-resolved 0x101a877f..0x101a87a5 (-1 0x101a8788, serial 0x101a879f)
	FElysiumNpcBase* NextBase = Next != nullptr ? Next->AsNpcBase() : nullptr;
	FElysiumScriptedSequence* NextCineEntity = NextBase != nullptr
		? NextBase->AsSpecies<FElysiumScriptedSequence>() : nullptr;
	if (NextCineEntity != nullptr                                    // 0x101a8758 / 0x101a8778 / 0x101a877d
		&& (NextCineEntity != this || (SpawnFlags & GCineSfRepeatable) != 0)) // 0x101a87a9 / 0x101a87b2
	{
		NextCineEntity->SetTarget(Npc.Handle);                       // 0x101a87b7 SetTarget(next, npc)
		NextCineEntity->PossessEntity();                             // 0x101a87c0 next's slot 583
	}
}                                                                    // 0x101a87c9

// --- Inputs ---------------------------------------------------------------------------------------

// `InputBeginSequence` `0x101a7390`.
void FElysiumScriptedSequence::InputBeginSequence(const FElysiumInputArgs& Args)
{
	RecordInput(Args);
	const double Now = CineNow(*this);
	const double PendingThink = ThinkFunction != EThinkFunction::None ? CineThinkAt : ELYSIUM_NEVER_THINK;
	if (PendingThink < ELYSIUM_NEVER_THINK && PendingThink > Now)
	{
		UE_LOG(LogElysiumSeq, Log, TEXT("***WARNING*** Called BeginSequence for '%s' before it had a chance "
			"to think. Still another %f seconds before we think. Try delaying your BeginSequence call."),
			*CineDebugName(*this), PendingThink - Now);
		StartTime = PendingThink + GCineStartDelaySeconds;
		return;
	}
	StartTime = Now + GCineStartDelaySeconds;
	if (!FindEntityWrap())
	{
		// Not found: nothing — no think, no output (`0x101a7412 JZ 0x101a74a9`).
		return;
	}
	FElysiumNpcBase* Npc = TargetNpc();
	if (Npc == nullptr)
	{
		ArmCineThink(EThinkFunction::CineThink, Now);
		return;
	}
	const int32 State = Npc->GetScriptState();   // npc m_scriptState (+0x5d70)
	if (State != GScriptPlaying && State != GScriptPostIdle)
	{
		return;
	}
	if (Npc->CanPlaySequence(FCanOverrideState(), 1) != 0)
	{
		PossessEntity();
	}
}

// `InputMoveToPosition` `0x101a72b0`: the standing `m_hTargetEnt`, no search.
void FElysiumScriptedSequence::InputMoveToPosition(const FElysiumInputArgs& Args)
{
	RecordInput(Args);
	const double Now = CineNow(*this);
	FElysiumNpcBase* Npc = TargetNpc();
	if (Npc == nullptr)
	{
		ArmCineThink(EThinkFunction::CineThink, Now);
	}
	else
	{
		const int32 State = Npc->GetScriptState();   // npc m_scriptState (+0x5d70)
		if (State != GScriptPlaying && State != GScriptPostIdle)
		{
			return;
		}
		if (Npc->CanPlaySequence(FCanOverrideState(), 1) == 0)
		{
			return;
		}
		PossessEntity();
	}
	StartTime = Now + GCineStartTimeFar;
}

// `InputCancelSequence` `0x101a7500`: THIS cine only, no output.
void FElysiumScriptedSequence::InputCancelSequence(const FElysiumInputArgs& Args)
{
	RecordInput(Args);
	UE_LOG(LogElysiumSeq, Verbose, TEXT("InputCancelScript: Cancelling script '%s'"), *Play);
	ScriptEntityCancel(*this);
}

void FElysiumScriptedSequence::InputKill(const FElysiumInputArgs& Args)
{
	RecordInput(Args);
	RemoveSelf();
}

bool FElysiumScriptedSequence::CancelScriptedSequenceForDialogue(const FElysiumEntityHandle& NpcHandle)
{
	// The NPC-side callers (`EnterGrappleState` `0x102b5c00`, `ForceScheduleChange` `0x102ae490`, the
	// dialogue opener) run `CancelScript` (`0x101a8c30`) on their `m_hCine`.
	FElysiumEntity* NpcEntity = World != nullptr ? World->Resolve(NpcHandle) : nullptr;
	const bool bOwner = NpcEntity != nullptr && NpcEntity->ScriptOwner == Handle;
	CancelScript();
	return bOwner;
}

void FElysiumScriptedSequence::AddInputs(FElysiumClassDesc& D, const TCHAR* RetailClass)
{
	if (FCString::Strcmp(RetailClass, TEXT("CCineNPC")) != 0)
	{
		return;
	}
	using FS = FElysiumScriptedSequence;
	D.Input(TEXT("BeginSequence"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FS&>(*E.AsNpcBase()).InputBeginSequence(Args); });
	D.Input(TEXT("MoveToPosition"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FS&>(*E.AsNpcBase()).InputMoveToPosition(Args); });
	D.Input(TEXT("CancelSequence"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FS&>(*E.AsNpcBase()).InputCancelSequence(Args); });
	// The port's `Kill` runs no slot 180; a director's states the `UTIL_Remove` pair itself.
	D.Input(TEXT("Kill"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FS&>(*E.AsNpcBase()).InputKill(Args); });
}

// --- Persistence and presentation -----------------------------------------------------------------

const TCHAR* FElysiumScriptedSequence::SaveBlockReason() const
{
	// 0x1027bf50/0x1008df10: logical cursor, phase and possession now resume.
	// K1 remains until the integrator installs event-free native seek and passes the possession witness.
	const FElysiumNpcBase* Npc = TargetNpc();
	return Npc != nullptr && Npc->ScriptOwner == Handle ? TEXT("a scripted sequence is active") : nullptr;
}

void FElysiumScriptedSequence::Serialize(FElysiumSaveArchive& Ar)
{
	// The `CAI_BaseNPC` record first (retail's datamap chain), then the installed think, the input
	// activator and the NPC this director possesses (`m_hCine` on the NPC, which the base snapshot
	// does not carry). The six saved words, `m_iDelay`, `m_startTime` and `m_hNextCine` ride the
	// registry's SAVE walk; `m_scriptState` is the NPC's own word. A map snapshot is taken at every
	// level teardown, possession or not — only an explicit save refuses (K1).
	FElysiumNpcBase::Serialize(Ar);
	uint8 Function = static_cast<uint8>(ThinkFunction);
	Ar << Function;
	Ar.Time(CineThinkAt, EElysiumTimePolicy::MaxFloat); // 0x101a0a80 port director deadline, map-clock domain
	int32 ActivatorIndex = LastInputActivator.IsSet() ? LastInputActivator.Index : INDEX_NONE;
	Ar << ActivatorIndex;
	Ar << LastInputCaller; // +0x110 EHANDLE, 0x101a7880 input identity
	const FElysiumNpcBase* Owned = Ar.IsLoading() ? nullptr : TargetNpc();
	int32 OwnedIndex = Owned != nullptr && Owned->ScriptOwner == Handle ? Owned->Handle.Index : INDEX_NONE;
	Ar << OwnedIndex;
	if (Ar.IsLoading())
	{
		ThinkFunction = static_cast<EThinkFunction>(Function);
		LastInputActivator = (ActivatorIndex == INDEX_NONE || World == nullptr)
			? FElysiumEntityHandle::Invalid()
			: FElysiumEntityHandle(ActivatorIndex, World->GetEpoch());
		RestoredNpcIndex = OwnedIndex;
		RescheduleThink();
	}
}

void FElysiumScriptedSequence::OnPostRestore(FElysiumEntityWorld& InWorld)
{
	FElysiumNpcBase::OnPostRestore(InWorld);
	// 0x101a2e40/0x101a7880: possession was rebound before ANY NPC prerequisite consumer.
	FElysiumNpcBase* Npc = TargetNpc();
	if (Npc == nullptr || Npc->IsDead() || Npc->ScriptOwner != Handle) return;
	// The collision view follows the restored `NAV_IGNORE_NPC` bit, as at possession and cleanup
	// (`IsIgnoreCollisionEntity` reads the bit live; the view does not ride the snapshot).
	if ((SpawnFlags & GCineSfIgnoreNpcCollision) != 0)
	{
		if (const FElysiumNpc* Troika = Npc->AsNpc())
		{
			Npc->SetIgnoreCharacterCollision(Troika->NpcFlags.Has(EElysiumNpcFlag::NAV_IGNORE_NPC));
		}
	}
}

void FElysiumScriptedSequence::PreloadForActivation()
{
	FElysiumEntity* Npc = World != nullptr && !TargetEntity.StartsWith(TEXT("!")) && !TargetEntity.IsEmpty()
		? World->FindByName(TargetEntity) : nullptr;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	FString ProxyStem;
	if (Npc == nullptr && Embodiment != nullptr
		&& TargetEntity.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
	{
		if (const FElysiumPlayer* Player = World->FindPlayer())
		{
			ProxyStem = FPaths::GetBaseFilename(Player->Model).ToLower();
		}
	}
	auto Preload = [Npc, Embodiment, &ProxyStem](const FString& Clip)
	{
		if (Clip.IsEmpty())
		{
			return;
		}
		if (Npc != nullptr)
		{
			Npc->PreloadAnimClip(Clip);
		}
		else if (Embodiment != nullptr && !ProxyStem.IsEmpty())
		{
			Embodiment->PreloadNpcClipForModel(ProxyStem, /*bPlayerMaterial=*/false, Clip);
		}
	};
	Preload(PreIdle);
	Preload(Play);
	Preload(PostIdle);
	Preload(CustomMove);
	if (MoveTo == 1)
	{
		Preload(TEXT("walk"));
	}
	else if (MoveTo == 2)
	{
		Preload(TEXT("run"));
	}
}

void FElysiumScriptedSequence::Diagnostic(const FString& Line)
{
	UE_LOG(LogElysiumSeq, Verbose, TEXT("%s"), *Line);
	Diagnostics.Add(Line);
	if (Diagnostics.Num() > 32)
	{
		Diagnostics.RemoveAt(0);
	}
}

void FElysiumScriptedSequence::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Target NPC"), TargetEntity.IsEmpty() ? TEXT("(none)") : TargetEntity);
	const FElysiumNpcBase* Npc = TargetNpc();
	const bool bPossessing = Npc != nullptr && Npc->ScriptOwner == Handle;
	Out.Emplace(TEXT("Resolved"), Npc != nullptr ? Npc->DebugString() : TEXT("no"));
	Out.Emplace(TEXT("Possessing"), bPossessing ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Script state"), bPossessing ? FString::FromInt(Npc->GetScriptState()) : TEXT("-"));
	Out.Emplace(TEXT("Move to"), FString::FromInt(MoveTo));
	Out.Emplace(TEXT("Radius"), FString::Printf(TEXT("%.0f"), Radius));
	Out.Emplace(TEXT("Delay"), FString::FromInt(Delay));
	Out.Emplace(TEXT("Start time"), FString::Printf(TEXT("%.3f"), StartTime));
	Out.Emplace(TEXT("Think"), ThinkFunction == EThinkFunction::CineThink ? TEXT("CineThink")
		: ThinkFunction == EThinkFunction::SubRemove ? TEXT("SUB_Remove") : TEXT("none"));
	Out.Emplace(TEXT("Interruptable"), bInterruptable ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Next cine"), World != nullptr && World->Resolve(NextCine) != nullptr
		? World->DescribeHandle(NextCine) : TEXT("(none)"));
	for (const TPair<const TCHAR*, const FString*> F : {
			TPair<const TCHAR*, const FString*>(TEXT("Pre-idle"), &PreIdle),
			TPair<const TCHAR*, const FString*>(TEXT("Play"), &Play),
			TPair<const TCHAR*, const FString*>(TEXT("Post-idle"), &PostIdle),
			TPair<const TCHAR*, const FString*>(TEXT("Custom move"), &CustomMove),
			TPair<const TCHAR*, const FString*>(TEXT("Next script"), &NextScript) })
	{
		if (!F.Value->IsEmpty())
		{
			Out.Emplace(F.Key, *F.Value);
		}
	}
	Out.Emplace(TEXT("Activate precaches"), FString::JoinBy(ActivatePrecacheLog, TEXT(","),
		[](const FSequenceSoundPrecache& Row) { return Row.SequenceName; }));
	Out.Emplace(TEXT("Activate diagnostics"), FString::FromInt(ActivateDiagnostics.Num()));
	Out.Emplace(TEXT("Diagnostics"), FString::Join(Diagnostics, TEXT(" | ")));
}

float FElysiumScriptedSequence::PlayActivity(const FString& Activity)
{
	(void)Activity;
	return -1.0f;
}
