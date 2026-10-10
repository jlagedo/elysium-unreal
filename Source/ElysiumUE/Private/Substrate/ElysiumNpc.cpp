// NPC presence and the first native-Unreal locomotion slice: the `npc_*` character leaf.
//
// The leaf here is the **dialogue half only**. Its place in VtMB's chain is CAI_BaseNPC under
// CBaseCombatCharacter under CBaseAnimating (`ElysiumPlayer.h`), and everything those two own
// — the sheet and its 25 inputs, the WillTalk latch, `default_disposition`, the skeletal body,
// playing a clip on it, following SetOrigin/SetModel, gating it on dormancy — arrives through the
// chain, shared with the player. `elysium.NpcBodies` is the one thing that stays here: it is this
// class's A/B, not the animating node's.

#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumRetailActivities.h"
#include <cmath>

#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSoundLevel.h"
#include "ElysiumStub.h"
#include "ElysiumSurfaceSounds.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumScriptedScheduleOrder.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumPhysProp.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumFootsteps.h"
#include "Substrate/ElysiumHint.h"
#include "ElysiumClassRegistry.h"   // FElysiumClassDesc — the registered descriptor's own name
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcCombatSchedules.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcConditionsBodiesShared.h"   // CondRetailStateId, the typed -> raw NPC_STATE table
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcLoadout.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Debug/ElysiumNpcDebugLogging.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumWeaponClasses.h"

#include "HAL/IConsoleManager.h"

// A/B toggle for NPC skeletal bodies (mirrors elysium.BrushBodies). Read in the leaf's Spawn,
// so it takes effect on the next map load: 1 stands the models, 0 leaves the NPCs bodiless records
// (their I/O still resolves — this only gates the visual).
static TAutoConsoleVariable<int32> CVarNpcBodies(
	TEXT("elysium.NpcBodies"),
	1,
	TEXT("Stand NPC glTF skeletal bodies at their origins at map load (1, default) or skip them (0)."),
	ECVF_Default);

bool FElysiumNpc::ResistsFeeding() const
{
	// `FastFood` is inherited through the resolved NPC template. It is the authored data switch
	// used by tutorial and ambient victims that must accept without the Brawl/Hacking check.
	return ElysiumFeed::ResistsByAuthoredPolicy(
		bFastFood, !FElysiumCombatCharacter::ResistsFeeding());
}

void FElysiumNpc::ApplyResolvedTemplate(const FElysiumClanTemplate& Resolved,
	const FElysiumStatTable* Table, const FElysiumExcludedEquipTable* EquipRules)
{
	bFastFood = Resolved.GeneralInt(TEXT("FastFood")) != 0;
	bHasKindredTemplate = true;
	bKindredTemplate = Resolved.GeneralInt(TEXT("Kindred")) != 0;
	bDisallowKnockbacks = Resolved.GeneralInt(TEXT("Disallow_Knockbacks")) != 0;
	// The authored spawn pool, kept beside the sheet write below. `Sheet.ApplyTemplate` puts the
	// same number on slot 12's BASE, but feeding moves that; the feed meter needs the pool the
	// victim was full at, which is what retail divides its bar by.
	const int32* AuthoredBloodPool = Resolved.Trait(TEXT("BloodPool"));
	TemplateBloodPoolValue = AuthoredBloodPool ? *AuthoredBloodPool : 0;

	// The authored damage filters, kept as the template states them. Nothing multiplies them
	// yet — the resolver only accumulates them onto the descriptor (`ElysiumDamage::Apply`
	// step 9 is an open join) — so an absent key stays absent rather than defaulting to 1.
	static const TCHAR* const FilterKeys[] =
	{
		TEXT("DamageFilterBashing"), TEXT("DamageFilterLethal"),
		TEXT("DamageFilterAggravated"), TEXT("DamageFilterFlame"),
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(FilterKeys); ++i)
	{
		const FString Authored = Resolved.GeneralStr(FilterKeys[i]);
		bHasDamageFilter[i] = !Authored.IsEmpty();
		DamageFilters[i] = bHasDamageFilter[i] ? FCString::Atof(*Authored) : 0.f;
	}

	// The footfall source (`0x1026d460` step 2, `1026d4a0`). Retail re-resolves the stat template on
	// EVERY step; this latches the already-resolved record here, at the one site that resolves it,
	// because `FElysiumClanTable::Resolve` merges seven maps down a parent chain and a walking body
	// asks two or three times a second. The keys themselves are read at the step, so a cvar flipped
	// mid-run still switches source.
	FootstepTemplate = MakeShared<FElysiumClanTemplate>(Resolved);

	Sheet.ApplyTemplate(Resolved, Table, /*Effects*/ nullptr, EquipRules);
}

bool FElysiumNpc::SpeciesFootstepAnimEvent(const TCHAR* SpeciesClassname,
	const FElysiumAnimEvent& Event)
{
	// The body of a footstep species class's `HandleAnimEvent` override (`docs/vtmb/footsteps.md`
	// §1.7): the class's row of `ElysiumFootsteps::SpeciesFor`, keyed by its own spawn classname.
	const int32 EventId = Event.Event;
	const FElysiumFootstepSpecies* Row = ElysiumFootsteps::SpeciesFor(SpeciesClassname);
	if (Row == nullptr || !ElysiumFootsteps::SpeciesClaims(*Row, EventId))
	{
		// An id this species' switch leaves to the base handler — which is `CNPC_VTzimisceRunner`'s
		// `JMP 0x100146e1` for 2052/2053 — is a direct call into the base body.
		return FElysiumNpc::HandleAnimEvent(Event);
	}

	// The shake, which retail raises BEFORE the wav vfunc on both `Shake` rows. Reported, not
	// performed: the `UTIL_ScreenShake` seam belongs to the 2100/2101 group.
	ElysiumFootsteps::ReportUnimplementedShake(*Row);

	// The species vfuncs draw from the shared RNG exactly as `0x1026d460` does, and a `Silent` row
	// draws nothing at all — its override is `return`, with no call behind it.
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::Footsteps);
	const TCHAR* const Wav = ElysiumFootsteps::PickSpeciesWav(*Row, EventId, Stream);
	const TCHAR* const Extra = ElysiumFootsteps::PickSpeciesExtraWav(*Row, Stream);

	IElysiumAudio* Audio = World != nullptr ? World->Audio() : nullptr;
	if (Audio != nullptr)
	{
		// Both sounds go out on `CHAN_BODY` because retail's vfunc emits both there
		// (`0x103c4160`), so the breath REPLACES the footfall on this owner's body channel — which
		// is what a single Source channel does and not a defect of the seam.
		auto Emit = [&](const TCHAR* Rel)
		{
			if (Rel == nullptr)
			{
				return;
			}
			FElysiumBodySound Sound;
			Sound.Rel = Rel;
			Sound.Volume = ElysiumFootsteps::SpeciesVolume;
			Sound.SoundLevelDb = ElysiumFootsteps::SpeciesSoundLevelDb;
			Sound.Pitch = ElysiumFootsteps::SpeciesPitch;
			Sound.Channel = EElysiumSoundChannel::Body;
			Audio->PlayBodySound(Handle, Sound);
		};
		Emit(Wav);
		Emit(Extra);
	}
	// Claimed either way. Every species override returns without reaching `0x1026d460`, so even the
	// `Silent` row is a handler and never a census row.
	return true;
}

bool FElysiumNpc::NpcStep(int32 EventId, bool bHeavy)
{
	// (a) The species overrides replace the whole chain before it is reached: they are their classes'
	// own `HandleAnimEvent` overrides (story 5 step 3, `SpeciesFootstepAnimEvent`).
	if (World == nullptr)
	{
		return true;
	}
	// (b) Step 1 (`1026d467`): the global player gate — a scripted camera, a camera track or an open
	// conversation silences every NPC in the level.
	if (ElysiumFootsteps::NpcStepsMuted(*World))
	{
		return true;
	}

	// (c) Step 3 (`1026d4aa`): the template's footfall pair, or the cvars.
	const ElysiumFootsteps::FStepSource Source = ElysiumFootsteps::NpcSource(
		FootstepTemplate.Get(), bHeavy, World->FootstepTuning());

	// (d) Step 5 (`1026d597`): `if (!this->m_pSurfaceData /* +0x5b90 */) return;`. The motor's last
	// published surface IS that field — written per move (`CAI_Navigator::MoveEnact`) and
	// `NAME_None` until the body has travelled. This reads the cache the motor already publishes
	// rather than tracing again.
	const FName Surface = Motor != nullptr ? Motor->SampleLocomotion().GroundSurface : FName();
	if (Surface.IsNone())
	{
		// Counted once per body, the anim-event census's rule: a body standing on nothing fires 2050
		// every half second and a line per occurrence would bury every real defect.
		if (!bReportedNoStepSurface)
		{
			bReportedNoStepSurface = true;
			UE_LOG(LogElysiumFootsteps, Verbose,
				TEXT("%s footfall %d is silent: no ground surface under the body (retail's null "
					"surfacedata_t at +0x5b90 — the motor has published none)"),
				*DebugString(), EventId);
		}
		return true;
	}

	FElysiumSurfaceSounds Sounds;
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (Embodiment == nullptr || !Embodiment->ResolveSurfaceSounds(Surface, Sounds)
		|| !Sounds.HasSteps())
	{
		// A surface the table cannot answer, or one whose baked record carries no step pool at all.
		// Retail's equivalent is a `surfacedata_t` whose `stepleft`/`stepright` resolve to empty
		// strings (`1026d666`). Once per (body, surface).
		if (!ReportedStepSurfacesWithoutPool.Contains(Surface))
		{
			ReportedStepSurfacesWithoutPool.Add(Surface);
			UE_LOG(LogElysiumFootsteps, Verbose,
				TEXT("%s footfall %d is silent: surface '%s' %s"),
				*DebugString(), EventId, *Surface.ToString(),
				Embodiment == nullptr ? TEXT("has no surface table to resolve against")
									  : TEXT("resolves with no step pool"));
		}
		return true;
	}

	// (f) Step 6 (`1026d5ab` -> `0x10228350`): the authored distance becomes the soundlevel. The
	// attenuation `0x1026d5e1` computes beside it is a PAS recipient cull that single-player never
	// runs, so it is not part of what the port emits (`docs/vtmb/footsteps.md` §3.5).
	const int32 LevelDb = ElysiumSoundLevel::FromDistanceUnits(Source.DistanceUnits);

	// (g) Step 9 (`1026d626`): the coin flip.
	const FString* Wav = ElysiumFootsteps::PickNpcWav(Sounds,
		ElysiumRng::Stream(EElysiumRngStream::Footsteps));
	if (Wav == nullptr || Wav->IsEmpty())
	{
		// The chosen side names nothing — retail's own silent step on a half-authored surface. Not
		// reported: it is a per-draw outcome, not a missing record.
		return true;
	}

	// (h) Step 11 (`1026d668`): `EmitSound(CHAN_BODY, name, volume, soundlevel, 0, pitch 100, ...)`.
	// The volume is the template's or the cvar's, unscaled; the pitch is a hard 100, which is this
	// seam's 1.0. No `CSoundEnt::InsertSound` anywhere on this path — an NPC footfall raises no AI
	// hearing stimulus, so one NPC never hears another walk (§1.6).
	if (IElysiumAudio* Audio = World->Audio())
	{
		FElysiumBodySound Sound;
		Sound.Rel = *Wav;
		Sound.Volume = Source.Volume;
		Sound.SoundLevelDb = LevelDb;
		Sound.Pitch = 1.0f;
		Sound.Channel = EElysiumSoundChannel::Body;
		Audio->PlayBodySound(Handle, Sound);
	}
	return true;
}

const FElysiumClanTemplate* FElysiumNpc::CharTemplateRecord() const
{
	// `FUN_101d5f10(&DAT_10738d10, this)`: `GetCharTemplate(this)` -> `table[idx]` when in range, else
	// the default record. `ApplyResolvedTemplate` keeps the resolved block (`FootstepTemplate`); an
	// unresolved or empty `stattemplate` leaves it null, which is the default record's every-flag-0,
	// every-string-NULL answer.
	return FootstepTemplate.Get();
}

bool FElysiumNpc::IsKindred() const
{
	return bHasKindredTemplate ? bKindredTemplate : FElysiumCombatCharacter::IsKindred();
}

bool FElysiumNpc::GetTemplateDamageFilter(EElysiumDmgFamily Family, bool bFlame,
	float& OutFilter) const
{
	const int32 Index = bFlame ? 3 : static_cast<int32>(Family);
	if (Index < 0 || Index >= UE_ARRAY_COUNT(bHasDamageFilter) || !bHasDamageFilter[Index])
	{
		return false;
	}
	OutFilter = DamageFilters[Index];
	return true;
}

void FElysiumNpc::OnKilled()
{
	KilledBy(FElysiumEntityHandle::Invalid());
}

void FElysiumNpc::KilledBy(const FElysiumEntityHandle& Attacker)
{
	// The port's packet-less kill entry (the feed's kill, a scripted kill). Retail's only route to
	// slot 144 on a living body is `0x1032ef60`'s alive arm, and a body whose `m_lifeState` is not
	// LIFE_ALIVE takes the dying/dead arms there instead -- so a body already dying is not killed
	// again (the life-state split, not a corpse refusal).
	if (LifeState != 0)
	{
		return;
	}
	FElysiumTakeDamageInfo Info;
	Info.Attacker = Attacker;
	Event_Killed(&Info);                                                      // slot 144
}

void FElysiumNpc::BecomeClientRagdoll()
{
	(void)BecomeClientRagdoll(FVector::ZeroVector, INDEX_NONE, false); // 0x1028a8f7 bone -1 fork
}

bool FElysiumNpc::HasClientRagdollRig() const
{
	return FElysiumNpcBase::HasClientRagdollRig(); // 0x10090180 shared source predicate
}

int32 FElysiumNpc::CorpseForceBone(const void* InInfo) const
{
	return FElysiumNpcBase::CorpseForceBone(InInfo); // 0x1032c1e4..0x1032c226
}

bool FElysiumNpc::BecomeClientRagdoll(const FVector& Force, int32 Bone, bool bRetainEntity)
{
	return FElysiumNpcBase::BecomeClientRagdoll(Force, Bone, bRetainEntity); // 0x10090180
}

void FElysiumNpc::InputUseInteresting(const FElysiumInputArgs& Args)
{
	// `InputUseInteresting 0x102c2a70`, nine instructions: `m_bUseInteresting +0x63d9` := the
	// variant's byte. Nothing else -- no release: a held place goes back only through `0x102b53d0`'s
	// callers (the program that holds it ends, fails or is removed).
	bUseInteresting = Args.Param.ToInt() != 0;
	// No clock reset: the input is not a slot-614 site. `SelectSchedule` case 1 reads the byte on
	// the next selection.
}

void FElysiumNpc::InputTeleportToEntity(const FElysiumInputArgs& Args)
{
	// CAI_BaseNPCTroika's FIELD_EHANDLE input performs its string-to-handle conversion when
	// the input is delivered: first live name match in entity-list order, including the retail
	// trailing-'*' prefix rule. It does not cache or retry a destination.
	if (!World)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s cannot resolve TeleportToEntity without an entity world"), *DebugString());
		return;
	}
	const FElysiumEntity* Destination = Args.Param.Type == EElysiumVariantType::Handle
		? World->Resolve(Args.Param.AsHandle)
		: World->FindByName(Args.Param.ToString());
	if (!Destination)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s TeleportToEntity destination '%s' resolved to no live entity"),
			*DebugString(), *Args.Param.ToString());
		return; // retail consumes the input as a no-op; there is no retry
	}

	// SetAbsOrigin, then SetAbsAngles, followed by the concrete NPC's due-think hook. The
	// authoritative writer crosses the existing embodiment seam without adding placement,
	// velocity, route, schedule, enemy, animation, or safe-location policy.
	SetRuntimeTransform(Destination->Origin, Destination->Angles);
	ResetThinkTimers(World->NowSeconds());
}

void FElysiumNpc::SeedPlayerRelationship()
{
	const FElysiumEntityHandle Player = World ? World->PlayerHandle()
		: FElysiumEntityHandle::Invalid();
	if (!Player.IsSet() || PlayerReaction.IsEmpty() || Relationships.HasEntity(Player))
	{
		return;
	}
	TArray<FString> Tokens;
	PlayerReaction.ParseIntoArrayWS(Tokens);
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	if (Tokens.Num() != 2 || !ElysiumRelationships::Parse(Tokens[0], Value))
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s invalid player_reaction '%s'"),
			*DebugString(), *PlayerReaction);
		return;
	}
	Relationships.SetEntity(Player, Value, FCString::Atoi(*Tokens[1]));
}

void FElysiumNpcBase::InputSetRelationship(const FElysiumInputArgs& Args)
{
	TArray<FString> Tokens;
	Args.Param.ToString().ParseIntoArrayWS(Tokens);
	if (Tokens.IsEmpty() || Tokens.Num() % 3 != 0)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s SetRelationship expects target D_* priority triples, got '%s'"),
			*DebugString(), *Args.Param.ToString());
		return;
	}

	for (int32 Index = 0; Index < Tokens.Num(); Index += 3)
	{
		const FString& TargetSpec = Tokens[Index];
		EElysiumRelationship Value = EElysiumRelationship::Neutral;
		if (!ElysiumRelationships::Parse(Tokens[Index + 1], Value))
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s SetRelationship unknown value '%s'"),
				*DebugString(), *Tokens[Index + 1]);
			continue;
		}
		const int32 Priority = FCString::Atoi(*Tokens[Index + 2]);
		bool bMatchedEntity = false;
		if (World && TargetSpec.Equals(TEXT("player"), ESearchCase::IgnoreCase))
		{
			bMatchedEntity = Relationships.SetEntity(World->PlayerHandle(), Value, Priority);
		}
		else if (World)
		{
			const bool bWildcard = TargetSpec.Contains(TEXT("*")) || TargetSpec.Contains(TEXT("?"));
			for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
			{
				if (!Candidate || Candidate->IsDead() || Candidate->TargetName.IsEmpty())
				{
					continue;
				}
				const bool bMatches = bWildcard
					? Candidate->TargetName.MatchesWildcard(TargetSpec, ESearchCase::IgnoreCase)
					: Candidate->TargetName.Equals(TargetSpec, ESearchCase::IgnoreCase);
				if (bMatches)
				{
					Relationships.SetEntity(Candidate->Handle, Value, Priority);
					bMatchedEntity = true;
				}
			}
		}
		if (!bMatchedEntity && !TargetSpec.Contains(TEXT("*")) && !TargetSpec.Contains(TEXT("?")))
		{
			Relationships.SetClass(TargetSpec, Value, Priority);
		}
	}
	Mind.RecordExternal(FString::Printf(TEXT("relationship table %d entity / %d class; combat consumer pending"),
		Relationships.NumEntityRules(), Relationships.NumClassRules()));
}

void FElysiumNpc::InputSetupPatrolType(const FElysiumInputArgs& Args)
{
	// `0x1029eb30`. A dead NPC takes no patrol (`m_NPCState != 7`, `0x1029eb3a`).
	if (NpcStateRetail() == 7)
	{
		return;
	}
	// `strtok` on the delimiter set `DAT_105c7ed4`: repeat, type, schedule. Named crash guards (both
	// patrol inputs): the whitespace split stands for `strtok` over `Q_strncpy(buf, param, 0x100)`
	// (no 256-byte truncation here), a missing third token answers schedule 0 where retail hands
	// `0x1029f370` a NULL, and `InputFollowPatrolPath` stops at 64 ids where retail overruns
	// `aiStack_204[65]` (`0x1029eb73..0x1029ebb6`, `0x1029ee5a`).
	const FString Param = Args.Param.ToString();
	TArray<FString> Tokens;
	Param.ParseIntoArrayWS(Tokens);
	if (Tokens.Num() < 2)                                                     // 0x1029ebb8 / 0x1029ebc0
	{
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("ERROR: %s - Argument to InputSetupPatrolType(%s) is in the "
			"wrong format.\nShould be 'RepeatCount PathType Schedule'"), *DebugString(), *Param);   // 0x1029ece8 0x105d9df0
		return;
	}
	const int32 Repeat = FCString::Atoi(*Tokens[0]);                          // 0x1029ebc9 _atoi
	if (Repeat < 0)                                                           // 0x1029ebd3 / 0x1029ebd5
	{
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("ERROR: %s - Could not get repeat type %s (in "
			"InputSetupPatrolType (%s))"), *DebugString(), *Tokens[1], *Param);   // 0x1029ec31 0x105d9ed8
		return;
	}
	// `0x103079c0` never answers -1: its miss is `Error()` then 0 (the crash guard `PatrolTypeForName`
	// names), so the `-1` arm at `0x1029ec02` (the same 0x105d9ed8 message) is unreachable here too.
	const int32 Type = PatrolTypeForName(Tokens[1]);                          // 0x1029ebf8 0x103079c0
	const int32 ScheduleId = Tokens.IsValidIndex(2) ? PatrolScheduleForName(Tokens[2]) : 0;  // 0x1029ec53 0x1029f370
	if (ScheduleId == 0)                                                      // 0x1029ec58
	{
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("ERROR: %s - Could not find schedule '%s' (in "
			"InputSetupPatrolType (%s))"), *DebugString(),
			Tokens.IsValidIndex(2) ? *Tokens[2] : TEXT("(null)"), *Param);    // 0x1029ec82 0x105d9e80
		return;
	}
	BuildPatrolPath(&PatrolPathCell, Repeat, Type, ScheduleId, nullptr,
		EPatrolPathBuild::Replace);                                           // 0x1029ecb1 0x1029f460(..., 1)
}

void FElysiumNpc::InputFollowPatrolPath(const FElysiumInputArgs& Args)
{
	// `0x1029ed90`. A dead NPC takes no patrol.
	if (NpcStateRetail() == 7)
	{
		return;
	}
	const FString Param = Args.Param.ToString();
	TArray<FString> Tokens;
	Param.ParseIntoArrayWS(Tokens);
	if (Tokens.IsEmpty())                                                     // 0x1029edfe
	{
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("ERROR: %s - Argument to InputFollowPatrolPath(%s) is in the "
			"wrong format.\nShould be 'NodeID1 [NodeID2 [NodeID3 [...]]]'"), *DebugString(), *Param);   // 0x1029ee24 0x105d9f90
		return;
	}
	// Up to 64 ids and the -1 terminator (`aiStack_204[65]`).
	int32 Ids[PatrolPathNodeCapacity + 1];
	int32 Count = 0;
	for (const FString& Token : Tokens)
	{
		if (Count >= PatrolPathNodeCapacity)
		{
			break;   // retail's stack array overruns here; crash guard
		}
		const int32 Id = PatrolNodeIdFor(Token);                              // 0x1029ee4f 0x102d2900
		if (Id == INDEX_NONE)                                                 // 0x1029ee57 / 0x1029ee61
		{
			// A miss logs and returns BEFORE the builder: no partial path is installed.
			UE_LOG(LogElysiumNpcEnt, Log, TEXT("ERROR: %s - Could not find node with id '%s' (in "
				"InputSetPatrolPath (%s))"), *DebugString(), *Token, *Param);   // 0x1029eed4 0x105d9f30
			return;
		}
		Ids[Count++] = Id;
	}
	Ids[Count] = -1;                                                          // 0x1029ee8f
	BuildPatrolPath(&PatrolPathCell, 0, -1, 0, Ids, EPatrolPathBuild::Extend); // 0x1029ee9a 0x1029f460(..., 0)
}

void FElysiumNpc::InputClearPatrolPath(const FElysiumInputArgs&)
{
	ReleasePatrolPath(&PatrolPathCell);                                       // 0x1029ef66 0x1029f5d0
}

bool FElysiumNpc::StartWalkingAnimation(bool bRunning)
{
	const int32 WalkActivity = bRunning ? 0x13 : 9; // 0x10264680 -> 0x10272130
	SetActivity(WalkActivity); // 0x10272440 -> 0x10272130, one lookup, row's own flags
	return SequenceNumber >= 0; // no Handle.Index variant
}

// V3c: the `Sequence` claim (`AcquireSequenceBody`, `ClaimScriptMove`, `ClaimScriptBody` and their
// releases, with their pre-claim `FinishAmbientUse` calls) is deleted. Retail's cine claims no body:
// `PossessEntity 0x101a7880` writes `m_hCine`, `m_scriptState` and `m_IdealNPCState = 4`, and the
// NPC's own `MaintainSchedule 0x102817c0` changes state and reselects (`SCHED_AISCRIPT 0x2e`). A
// visited place is released by `0x102b53d0`'s own callers only.

bool FElysiumNpc::IsFeedBusy() const
{
	return Dialogue.bInDialog || FElysiumCombatCharacter::IsFeedBusy();
}

void FElysiumNpc::LeaveGrappleState()
{
	// `CAI_BaseNPCTroika::LeaveGrappleState` `0x102b5d90` (slot 380) is exactly two statements: the
	// base body `0x1026ce30`, then a tail jump into slot 614. Story 29d ported the base under its own
	// name (`ElysiumNpcSpeciesMisc10.cpp`) because it is a DISTINCT retail function, and its
	// four steps are UNCONDITIONAL — this body used to gate the output fire and the
	// oblivious-decrement pair on `Grapple.Type == StealthKill`, which retail does not, and omitted
	// slot 416 `SetForceFrequentThink(false)` entirely.
	FElysiumNpcBase::LeaveGrappleState();
	// A body released from a grapple thinks on the same frame.
	ResetThinkTimers(World ? World->NowSeconds() : 0.0);
}

namespace
{
	// The two removal thinks by the name `ThinkSet` records (the function's retail address, as the
	// species tasks that install `SUB_Remove` spell it).
	const TCHAR* NpcSubRemoveThinkName() { return TEXT("0x101c0b10"); }
	const TCHAR* NpcSubPvsRemoveThinkName() { return TEXT("0x102696f0"); }
	// `_DAT_1044e664`, a float 10.0: `SUB_PVSRemove`'s re-arm and `CreateCorpse`'s corpse delay.
	constexpr float GNpcCorpseThinkDelaySeconds = ElysiumNpcTunables::Ten;

	// `SUB_Remove` `0x101c0b10`: `if (m_iHealth > 0) { m_iHealth = 0; DevWarning(2, ...); }` then
	// `UTIL_Remove(this)` (`0x101cd940`), whose first act is slot 180 `UpdateOnRemove`.
	void NpcSubRemove(FElysiumNpc& Npc)
	{
		if (Npc.Health > 0)
		{
			Npc.Health = 0;
			UE_LOG(LogElysiumNpcEnt, Log, TEXT("SUB_Remove called on entity with health > 0"));   // 0x1059c3a4
		}
		Npc.UpdateOnRemove();
		Npc.Kill();
	}

	// `SUB_PVSRemove` `0x102696f0` (the datamap's `CBaseEntitySUB_PVSRemove`, thunk `0x10009c9b`):
	// for each player, its slot 363 `FInViewCone(this)`, the engine PVS test `0x101d1a90(this,
	// player)` and its slot 201 `FVisible(this, 0x2804091)`; a player that passes all three re-arms
	// the think at `curtime + 10.0` (`_DAT_1044e664`) and keeps the corpse. Nobody seeing it:
	// `UTIL_Remove(this)`. The player's view cone and line of sight are the embodiment's
	// player-visibility queries (the ones the NPC maker's spawn guard asks); a headless world has no
	// view, so nothing sees the corpse there.
	void NpcSubPvsRemove(FElysiumNpc& Npc, double Now)
	{
		FElysiumPlayer* const Player = Npc.World != nullptr ? Npc.World->FindPlayer() : nullptr;
		const IElysiumEmbodiment* const Embodiment = Npc.World != nullptr ? Npc.World->Embodiment() : nullptr;
		const bool bCone = Player && Player->FInViewCone(&Npc); // slot3630x10326750: centre/scalar/native FOV
		const bool bPvs = bCone && Embodiment && Embodiment->ArePointsInSamePvs(Player->Origin, Npc.Origin); // no PVS source in a headless world
		const bool bVisible = bPvs && FElysiumNpc::BaseEntityFVisibleFrom(*Player, Npc, 0x2804091); // slot2010x100a6fa0 probe0
		if (Npc.World && Npc.World->HasAiTraceSink()) Npc.World->EmitAiTrace(Npc, FName(TEXT("script")), FString::Printf(TEXT("pvsremove cone=%d pvs=%s visible=%s origin=%s player=%s"),
			bCone ? 1 : 0, bCone ? (bPvs ? TEXT("1") : TEXT("0")) : TEXT("skipped"), bPvs ? (bVisible ? TEXT("1") : TEXT("0")) : TEXT("skipped"),
			*Npc.Origin.ToString(), Player ? *Player->Origin.ToString() : TEXT("absent")));
		if (bVisible) // same short-circuit cone -> PVS -> visibility order, 0x102696f0
		{
			Npc.NextThink = static_cast<float>(Now + static_cast<double>(GNpcCorpseThinkDelaySeconds));   // param_1[0x5f] = curtime + 10.0
			return;
		}
		Npc.UpdateOnRemove();                                                    // 0x101cd940 UTIL_Remove
		Npc.Kill();
	}
}

void FElysiumNpc::Think()
{
	// The entity think IS slot 431 `NPCThink`: `CAI_BaseNPCTroika::NPCThink` (`0x10292de0`), or the
	// species override where the class has one (story 8 wave 2, L13). What stands around the call is
	// the port's own, each piece named:
	//
	//  - `IsInert`: an entity the world has not activated or has removed (port lifecycle).
	//  - The removal/fade thinks retail installs by function pointer, dispatched by the name
	//    `ThinkSet` recorded: `SUB_Remove` (`0x101c0b10`, the static-corpse arm of `CreateCorpse`, a
	//    burning corpse, and the species tasks that ThinkSet it) and `SUB_PVSRemove` (`0x102696f0`,
	//    the ragdoll corpse's think `CreateCorpse`'s tail arms at +10 s). Explicit NULL remains NULL;
	//    a missing restored corpse function is V6's named save seam, never inferred PVS removal.
	//  - The three lifecycle one-shots retail runs in `NPCInit` and this runtime cannot run at spawn
	//    (an item entity created inside the world's spawn pass invalidates the array being iterated;
	//    admission establishes idle and would wipe a director's forced state applied ahead of it).
	//    Retail's first `NPCThink` sees their results, so they run before slot 431 on the first
	//    normal-due think of an enabled body. Admission no longer suppresses that think's AI pass:
	//    `0x10292de0` has no such arm (the old `bJustAdmitted` gate is deleted).
	//  - After the think, the body hold: this runtime's body is integrated by the movement component
	//    on the actor tick, not by `PerformMovement` inside the think, so the two think-level
	//    freezes retail gets for free are stated (see below).
	if (IsInert())
	{
		return;
	}
	if (ThinkFunctionName == NpcSubRemoveThinkName())
	{
		NpcSubRemove(*this);                                                  // 0x101c0b10
		return;
	}
	if (ThinkFunctionName == TEXT("0x100152b2") || ThinkFunctionName == TEXT("0x10269960"))
	{
		FadeOutThink(); // 0x10269960: later installed fade wins over corpse removal or clear
		return;
	}
	if (ThinkFunctionName == NpcSubPvsRemoveThinkName())
	{
		NpcSubPvsRemove(*this, World != nullptr ? World->NowSeconds() : 0.0); // 0x102696f0
		return;
	}
	if (ThinkFunctionName.IsEmpty() && (ThinkSetCalls > 0 || bDeathCommitted)) // 0x103a391c ThinkSet(NULL)
	{
		return; // explicit clear stays clear; a missing restored name has no inferred corpse clock
	}
	if (bDeathCommitted) { return; } // 0x1032c0e0: missing restored corpse function never runs NPCThink
	if (!bDisableAi && IsNormalThinkDue())
	{
		RunAdmissionBarrier();
		ResolveLoadout();
		ReplayDeferredScriptedOrder();
	}

	// The installed think function. `NPCInit` (`0x10273390`, `10273a5x`) installs `NPCInitThink`
	// (`0x10273aa0`) for the map's first second; that think runs `StartNPC`, which re-installs the
	// ordinary one (`CallNPCThink` -> slot 431). So a map-start NPC's first think is NOT an AI pass
	// -- the retail home of the port's old admission-think suppression.
	if (ThinkFunctionName == NpcInitThinkFunction())
	{
		NpcInitThink();                                                       // 0x10273aa0
		return;
	}
	NPCThink();                                                               // slot 431

	// The body hold (named modernization, the motor): retail's body moves and animates only from
	// inside `NPCThink`, so the `m_bDisableAI` return (`0x10292e6c`) freezes it in place and pose, and
	// the refused AI console gate (`0x1026c3d0`, `g_AIDisabled`) stops `PerformMovement` while
	// slot 310 `SetActivity(ACT_IDLE)` stands it idle. The animation clock is NOT stopped on the
	// gate arm: a stopped clock would park the blend into idle mid-stride and freeze a feed
	// victim's synced clip (the port's idle loops where retail holds its first frame -- visual only).
	const bool bAiConsoleRefused = World != nullptr && !World->IsAiEnabled();
	SetBodyHeld(bDisableAi || bAiConsoleRefused);
	SetBodyAnimationHeld(bDisableAi);
	// (The refused gate's slot 310 `SetActivity(ACT_IDLE)` puts the idle on the body through the
	// sequence bridge; the by-name play that stood in for it is gone.)
}

bool FElysiumNpc::RunAdmissionBarrier()
{
	// The activation barrier admits the mind on its first frozen-time think. Admission is a
	// no-op for body, motor and animation; executors may run only on a later think.
	if (!Mind.Admit())
	{
		return false;
	}
	// No cadence write: the tail arms every think, and an admitted NPC's first normal law is the
	// 0.1 s LOS pin (`Activate` seeds `m_bInPlayerLOS` true, as `NPCInit` does), which is the same
	// interval this arm used to spell as a literal.
	return true;
}

void FElysiumNpc::ResolveLoadout()
{
	// --- Combat loadout ---
	// The first ordinary think after admission, not `Spawn`: creating an item entity inside the
	// world's range-based spawn pass would invalidate the array being iterated, which is why
	// `FElysiumItemContainer` materialises its equip seeds from a think too.
	if (!bLoadoutResolved)
	{
		bLoadoutResolved = true;
		const ElysiumNpcLoadout::EResult Result = ElysiumNpcLoadout::Resolve(*this);
		Mind.RecordExternal(FString::Printf(TEXT("loadout: %s"),
			ElysiumNpcLoadout::ResultName(Result)));
	}
}

void FElysiumNpc::ReplayDeferredScriptedOrder()
{
	// --- A director that fired before this NPC's first think ---
	// The push was deferred whole (`BeginScriptedSchedule`), because admission establishes idle and
	// would have wiped a forced state applied ahead of it. Replaying it here is the first thing an
	// admitted NPC does, so the order is in force before any condition is gathered against it.
	if (ScriptedScheduleOrder.bPending && Mind.IsAdmitted())
	{
		const FElysiumScriptedScheduleOrder Pending = ScriptedScheduleOrder;
		ScriptedScheduleOrder.Reset();
		BeginScriptedSchedule(Pending, Pending.bHasForcedState, Pending.ForcedState);
	}
}

bool FElysiumNpc::OnBumped(double Now)
{
	// The NPC half of the player's touch handler `0x10147690`. Its whole body, in order: the
	// toucher takes `MiscFlag 0x100` (`Obf_Bumped_Object`); then, only when the toucher is a player
	// (`+0xa8`) and the touched thing is a Troika NPC (`+0x98`), `ConditionInterruptsCurrent
	// Schedule(WAS_BUMPED)` (`0x10269c70`) is consulted and the bit set only if it passes. The
	// player half runs in `FElysiumPlayer::PollTouchContacts`, which calls this once per frame of
	// contact, off the bodies' own `NotifyHit` records.
	//
	// That guard is the recovered fact worth having: retail does NOT raise `WAS_BUMPED`
	// unconditionally. The bit never stands on an NPC whose running program does not list it.
	// When it passes, the handler sets the bit itself (`SetCondition(0x38)`); its one-pass life is
	// `RunAI`'s end-of-pass clear (`0x1026f323`). The port's old `LastBumpTime` stamp, which a
	// gather re-derived the bit from, went with that gather (story 8 wave 2).
	(void)Now;
	if (!ElysiumSchedule::MaskHasCondition(Schedule, *this, EElysiumNpcCond::WasBumped))
	{
		return false;
	}
	Cognition.Conditions.Set(EElysiumNpcCond::WasBumped);
	return true;
}

int32 FElysiumNpc::SelectSchedule()
{
	// Story 8 Select19: retail's selector pair `0x1028a260` (`SelectNewScheduleRetail`): `+0x1b2c = 0`,
	// slot 437 `PreSelectSchedule`, then slot 438 `SelectSchedule` when 437 answered 0. It replaces
	// the port's guessed selector (the dead-state guard, the species-first composition and the law
	// branch at the outermost entry, all deleted): a dead NPC answers base case 7
	// (`0x1028a8ec`), and the incident consumers are reached from retail's own arms (`0x102ae920`
	// state 2 / crim-suspicion, `0x102af660` case 8).
	return SelectNewScheduleRetail();
}

void FElysiumNpc::UpdateIdealState(double Now)
{
	if (!Mind.IsAdmitted())
	{
		return;
	}
	const EElysiumNpcState Current = Mind.State();
	if (Current == EElysiumNpcState::Scripted || Current == EElysiumNpcState::Dead)
	{
		// A scripted owner or a dead body owns the state outright; the ideal-state pass does not
		// compete with either.
		return;
	}

	// Story 29e, family State19: slot 461 is the retail body. `HasInterruptCondition` answers 0
	// with no program installed, so a committed enemy no longer promotes idle → combat here.
	SelectIdealState();
	const int32 IdealRetail = LastSelectIdealStateRetail;
	if (IdealRetail == NpcStateRetail())
	{
		return;
	}
	SetState(IdealRetail);
	// "entering NPC state 14 opens the criminal window".
	// The CHOSEN mapping of retail state 14 onto this runtime's Alert, and why Alert rather than
	// Combat or Idle, is stated in full at `ElysiumNpcWitness::OnEnteredAlertState`. This is its one
	// call site: the promotion edge, after the transition is committed and not on every pass spent in
	// the state.
	if (IdealRetail == 3 && Mind.State() == EElysiumNpcState::Alert)
	{
		ElysiumNpcWitness::OnEnteredAlertState(*this, Now);
	}
	// A state change reselects. The running program was chosen by the state that has just been left
	// — an idle stance under an NPC that just acquired an enemy — so it ends here rather than
	// finishing on behalf of a state that no longer holds.
	ClearSchedule();
}

FElysiumInterestingPlace* FElysiumNpc::CurrentAmbientSpot() const
{
	if (!World || CurrentSpotIndex == INDEX_NONE)
	{
		return nullptr;
	}
	FElysiumEntity* Entity = World->Resolve(
		FElysiumEntityHandle(CurrentSpotIndex, World->GetEpoch()));
	return Entity && Entity->Def
		&& Entity->Def->Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase)
		? static_cast<FElysiumInterestingPlace*>(Entity) : nullptr;
}

const FElysiumInterestingPlaceType* FElysiumNpc::AmbientType(const FElysiumInterestingPlace* Spot) const
{
	const FElysiumInterestingPlaceTable* Table = ElysiumInterestingPlaces::Types();
	return Table && Spot ? Table->Find(Spot->Type) : nullptr;
}

FElysiumInterestingPlace* FElysiumNpc::ClaimAmbientSpot()
{
	// `PickRandomInterestingPlace 0x102db590` -> `BuildCandidates 0x102db470` -> the per-place test
	// `0x102dad60`. `0x102db590`'s own guards are `npc != null` and `0 < DAT_10927198` (the live
	// place count, raised by the constructor `0x102d99d0`): an empty list finds nothing below. No
	// term anywhere in the chain reads the place's type or the visitor's class (N15: the port's
	// `TypeRow` / `Activities` / `AcceptedClasses` terms are deleted -- the type parser `0x102dd0f0`
	// stores `AcceptedClasses`, but its one lookup `0x102dd630` has no caller in retail). A type
	// with no INTO or idle activity is `0x102a9f40`'s "Can not find interest" arm at arrival, not a
	// refusal here. `0x102db470` opens with two calls on the NPC's collision property (`+0x270`
	// slots 1 and 2, its mins/maxs) whose answers it discards: no test.
	if (!World)
	{
		return nullptr;
	}

	// `0x102dad60`, the per-place test, in retail's order. `npc != null` (`0x102dad68`) is `this`.
	const auto IsEligible = [this](const FElysiumInterestingPlace& Spot)
	{
		constexpr double FindRadiusCm = 10000.0 * ElysiumMove::U;
		constexpr double FindRadiusSqCm = FindRadiusCm * FindRadiusCm;
		// `0x102dad70` `m_bEnabled +0x57c`, `0x102dad7e` `+0x57d == 0`, `0x102dad8c..0x102dada4`
		// `m_iMarkersAllocated +0x584 (max_npcs) - m_iMarkersFailedAttempts +0x58c - in-use +0x588
		// > 0`: `IsAvailable` answers the three in that order. `+0x57d` has no writer but the
		// constructor (`0x102d99d0`, 0); the port's `!IsInert()` there stands for the place's
		// removal from the list (the destructor `0x102d9b10` unlinks it). The port carries no
		// `+0x58c` word: only `PickSpotFor 0x102da0d0` raises it, on a failed spot sample, and only
		// the constructor zeroes it; the port's claim takes the place's own origin and never fails a
		// sample, so the word would answer 0.
		if (!Spot.IsAvailable())
		{
			return false;
		}
		// `0x102dada6..0x102dadb4`: `place->m_iGroupID +0x574 & npc->m_iInterestingPlaceGroups +0x62dc`.
		if (!AcceptsAmbientGroup(Spot.GroupMask))
		{
			return false;
		}
		// `0x102dadb6..0x102dae13`: slot 220 `CBaseEntity::GetOrigin 0x100b3070` on both, the
		// straight-line `|place - npc|^2 <= [0x1049d28c]` (1.0e8: 10,000 units). Distance does not rank.
		return FVector::DistSquared(Origin, Spot.Origin) <= FindRadiusSqCm;
	};

	// `0x102db470`'s walk of the global list (head `DAT_10927194`, next `+0x540`). Retail walks rating
	// 5 -> 0 and stops at the first rating that yields any candidate; the port gathers every rating
	// in one pass and `PickHighestRatedCandidate` makes the same choice. Per place, in retail's order:
	// `m_iRating +0x578 == rating` (`0x102db4aa`; `Spawn 0x102d9c20` clamps it to 0..5, and a rating
	// outside that range matches no pass), `place != npc->m_pLastInterestingPlace +0x62fc`
	// (`0x102db4b4`), fewer than 0x100 candidates at that rating (`0x102db4bc`), then `0x102dad60`.
	// The last place (written by the release `0x102da600`) is tested by `0x102dad60` alone, with no
	// rating and no cap, only once no other place is eligible at any rating (`0x102db4fb..0x102db520`).
	// The NPC schedule stream makes the choice replayable across save/load. No blacklist and no body
	// claim (V3b deleted both).
	constexpr int32 MaxRating = 5;
	constexpr int32 MaxCandidatesPerRating = 0x100;   // 0x102db4bc CMP EDI,0x100
	int32 CandidatesAtRating[MaxRating + 1] = {};
	TArray<FElysiumInterestingPlace*> Candidates;
	TArray<int32> CandidateRatings;
	FElysiumInterestingPlace* LastPlace = nullptr;
	for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
	{
		if (!Candidate || !Candidate->Def
			|| !Candidate->Def->Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		FElysiumInterestingPlace* Spot = static_cast<FElysiumInterestingPlace*>(Candidate.Get());
		if (LastSpotIndex != 0 && Spot->Handle.Index == LastSpotIndex)
		{
			LastPlace = Spot;   // `+0x62fc`, which the fallback `0x102db4fb` reads directly
		}
		if (Spot->Rating < 0 || Spot->Rating > MaxRating)                // 0x102db4aa
		{
			continue;
		}
		if (Spot == LastPlace)                                           // 0x102db4b4
		{
			continue;
		}
		if (CandidatesAtRating[Spot->Rating] >= MaxCandidatesPerRating)  // 0x102db4bc
		{
			continue;
		}
		if (!IsEligible(*Spot))                                          // 0x102db4d1
		{
			continue;
		}
		++CandidatesAtRating[Spot->Rating];
		Candidates.Add(Spot);
		CandidateRatings.Add(Spot->Rating);
	}
	if (Candidates.IsEmpty() && LastPlace != nullptr && !IsEligible(*LastPlace))   // 0x102db510
	{
		LastPlace = nullptr;
	}

	FRandomStream& PickStream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	const int32 PickedIndex = ElysiumInterestingPlaces::PickHighestRatedCandidate(
		CandidateRatings, PickStream);
	FElysiumInterestingPlace* Picked = nullptr;
	if (Candidates.IsValidIndex(PickedIndex))
	{
		Picked = Candidates[PickedIndex];
	}
	else if (LastPlace != nullptr)
	{
		// `0x102db590` draws `RandomInt(0, count - 1)` over the one-entry list too.
		(void)PickStream.RandRange(0, 0);
		Picked = LastPlace;
	}
	// `PickSpotFor 0x102da0d0`'s claim; `TASK_FIND_INTERESTING_PLACE` stores `+0x62ec`.
	if (Picked != nullptr) // 0x102db590 selects; PickSpotFor is the caller's separate transaction
	{
		CurrentSpotIndex = Picked->Handle.Index;
		return Picked;
	}
	return nullptr;
}

uint32 FElysiumNpc::ParseGroupMask(const FString& Groups, bool bEmptyIsEveryGroup)
{
	// `0x102989e0` (hint groups) and `0x10298910` (interesting-place groups), which are the same
	// body but for the empty answer. Retail copies the string into a 256-byte stack buffer and
	// walks it: skip runs of `' '`, `atoi` the token, and if `id - 1` is in `[0, 0x20)` set that
	// bit. A token that is not a number is `atoi`'s 0, so it sets no bit; an id past 32 is
	// dropped in silence. The 256-byte copy is unbounded in retail and a longer key overruns it;
	// the port does not reproduce the overrun, and no map authors one.
	if (Groups.IsEmpty())
	{
		return bEmptyIsEveryGroup ? 0xffffffffu : 0u;
	}
	uint32 Mask = 0;
	TArray<FString> Tokens;
	// Retail splits on `' '` only, so a tab is part of the token and `atoi` stops at it — which
	// lands on the same bit. Whitespace splitting is the same answer with one fewer special case.
	Groups.ParseIntoArrayWS(Tokens);
	for (const FString& Token : Tokens)
	{
		const int32 Bit = FCString::Atoi(*Token) - 1;
		if (Bit >= 0 && Bit < 32)
		{
			Mask |= 1u << static_cast<uint32>(Bit);
		}
	}
	return Mask;
}

void FElysiumNpc::SetHintGroups(const FString& Groups)
{
	ScheduleHost.HintGroups = Groups;
	ScheduleHost.HintGroupMask = ParseGroupMask(Groups, /*bEmptyIsEveryGroup=*/true);
}

void FElysiumNpc::SetInterestingPlaceGroups(const FString& Groups)
{
	InterestingPlaceGroups = Groups;
	InterestingPlaceGroupMask = ParseGroupMask(Groups, /*bEmptyIsEveryGroup=*/false);
	// Retail's writer is `0x10298910`, and `0x102dad60` is the reader: see `AcceptsAmbientGroup`.
}

bool FElysiumNpc::AcceptsAmbientGroup(int32 PlaceGroupMask) const
{
	// `0x102dad60`, the group half of the eligibility test, verbatim:
	//
	//     if ((*(uint *)(place + 0x574) & npc[0x18b7]) != 0) { ... }
	//
	// `+0x0574` is the place's own `m_iGroupID` AFTER `CAI_InterestingPlace::Spawn` (`0x102d9c20`)
	// folded it — `1 << (id - 1)` for an id in 1..32, the literal `1` otherwise — and `npc[0x18b7]`
	// is `m_iInterestingPlaceGroups +0x62dc`, which `0x10298910` parsed with an empty or `"0"` list
	// leaving it ZERO. So an NPC that authors nothing, or authors `"0"` as 1005 shipped NPCs do,
	// matches NO place, and a place whose fold is zero is matched by nobody.
	//
	// This replaces the port's own "an empty list admits every group" rule, which 29c left standing
	// as a named divergence. It is closed: the walked retail rule is what runs.
	return (static_cast<uint32>(PlaceGroupMask) & InterestingPlaceGroupMask) != 0;
}

bool FElysiumNpc::EnsureStanceResolved()
{
	const FString Key = FString::Printf(TEXT("%s|%s|%d"),
		*ModelStem(), *Disposition, DispositionLevel);
	if (StanceResolvedFor == Key)
	{
		return !bStanceUnavailable;
	}
	StanceResolvedFor = Key;
	bStanceUnavailable = true;
	StanceClips = FElysiumStanceClips();
	StanceTuning = FElysiumDisposition();

	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr
		|| !Embodiment->ResolveDisposition(Disposition, DispositionLevel, StanceTuning)
		|| !Embodiment->ResolveStanceClips(ModelStem(), StanceTuning.AnimName, StanceClips))
	{
		return false;
	}
	bStanceUnavailable = false;
	return true;
}

bool FElysiumNpc::SetDisposition(const FString& NewDisposition, int32 NewLevel)
{
	IElysiumEmbodiment* const DispositionSource = World != nullptr ? World->Embodiment() : nullptr;
	FElysiumDisposition RequestedRow; // 0x102c0f70 Find(name, level)
	const bool bLookupMiss = DispositionSource == nullptr
		|| !DispositionSource->ResolveDisposition(NewDisposition, NewLevel, RequestedRow)
		|| !RequestedRow.Name.Equals(NewDisposition, ESearchCase::IgnoreCase)
		|| RequestedRow.Level != NewLevel; // 0x102c0f70: miss takes Neutral,1 and old=-1
	const bool bOldMissing = !bHasDispositionIndex || bLookupMiss; // 0x102c0f70
	FElysiumDisposition OldRow; FElysiumDisposition NewRow; bool bChanged = false;
	if (!CommitDisposition(bLookupMiss ? FString(TEXT("Neutral")) : NewDisposition,
		bLookupMiss ? 1 : NewLevel, bChanged, &OldRow, &NewRow)) // 0x102c0f70
	{
		return false; // named missing disposition-table seam
	}
	bHasDispositionIndex = DispositionSource != nullptr && NewRow.IsValid(); // 0x102c0f70 +0x64d4
	StanceTuning = NewRow; // 0x102c0f70: tuning always written, even unchanged
	DispositionBlinkWord = NewRow.MinBlinkInterval; // 0x100ecd30 +0x64d8
	DispositionBlinkWord = NewRow.MaxBlinkInterval; // 0x100ecd30 same output address, second wins
	DispositionMinEyeFidget = NewRow.EyeTarget.MinInterval; // 0x100ecd30 +0x6584
	DispositionMaxEyeFidget = NewRow.EyeTarget.MaxInterval; // 0x100ecd30 +0x6588
	RelativeEyeTarget = NewRow.EyeTarget.DefaultDirection; // 0x100eccf0 +0x5b94
	EyeIntegRate = NewRow.EyeTurnRate; // 0x100ecdf0(index0) +0xe3c
	DefExpression = NewRow.DefaultExpression; // 0x100ec360 +0x10b4, name-index bridge
	NoDeformExpression = NewRow.TalkingExpression; // 0x100ec2e0 +0x64d0, name-index bridge
	ExpressionBlendWeight = NewRow.ExpressionIntensity; // 0x100ec3d0 +0x10b8
	if (!bChanged && !bOldMissing) { return true; } // 0x102c0f70: only index change picks
	StanceResolvedFor.Reset(); // 0x102c0f70 new disposition's stance row
	int32 DispositionSequence = INDEX_NONE; // 0x102c0f70
	if (bOldMissing)
	{
		DispositionSequence = Slot611(); // 0x102c0f70 old=-1, slot611 0x102c12a0
	}
	else
	{
		const FString OldAnim = OldRow.AnimName.IsEmpty() ? OldRow.Name : OldRow.AnimName; // 0x100ed150
		const FString NewAnim = NewRow.AnimName.IsEmpty() ? NewRow.Name : NewRow.AnimName; // 0x100ed150
		const int32 StanceNumber = Stance.Current + 1; // 0x100ed150
		DispositionSequence = LookupSequenceByName(*FString::Printf(TEXT("Stance_Trans_%s_%d_%s_%d"),
			*OldAnim, StanceNumber, *NewAnim, StanceNumber)); // 0x100ed150 named transition
		if (DispositionSequence == INDEX_NONE)
		{
			DispositionSequence = LookupSequenceByName(*FString::Printf(TEXT("Stance_Trans_%s_1_%s_1"),
				*OldAnim, *NewAnim)); // 0x100ed150 second lookup, even stance1 repeats it
		}
		if (DispositionSequence == INDEX_NONE && EnsureStanceResolved())
		{
			DispositionSequence = LookupSequenceByName(*StanceClips.Idle[Stance.Current]); // 0x100ed150 new idle
		}
	}
	if (DispositionSequence >= 0) // 0x102c0f70
	{
		IdealActivityNumber = 0xf1; IdealSequence = DispositionSequence; // 0x102c0f70 +0xff0/+0x5ccc
		if (!bDisableAi) // 0x102c0f70: commit alone is gated, no feed/body-owner gate
		{
			SequenceNumber = DispositionSequence; SequenceCycle = 0.f; // 0x102c0f70
			ActivityNumber = 0xf1; // 0x102c0f70 +0xfec, translated activity untouched
			AnimTime = static_cast<float>(World != nullptr ? World->NowSeconds() : 0.0); // 0x102c0f70
			ResetSequenceInfo(); // 0x10090950
		}
	}
	return true;
}

void FElysiumNpc::InputDisableThink(const FElysiumInputArgs& Args)
{
	// `0x1029f2a0`: a bool variant is passed through; any other type disables nothing.
	SetDisableAi(Args.Param.Type == EElysiumVariantType::Bool && Args.Param.AsBool);
}

void FElysiumNpc::OnDialogFilePlayed(double DurationSeconds)
{
	// The spoken-line player `0x102c0520`: `m_bIsTalking = 1`, the end time `+0x64cc = curtime +
	// duration`, then slot 614 -- the talking body is on the 0.01 s floor from this frame.
	//
	// `102c0923 MOV byte ptr [ESI + 0x64c0],1` and `102c092a FSTP float ptr [ESI + 0x64cc]` are the
	// two writes, and story 29d (family Social10) split them: `FinishTalking` (`0x102c0ca0`) clears
	// the FLAG and STAMPS the time, so the two words cannot be one member any more.
	const double Now = World ? World->NowSeconds() : 0.0;
	bIsTalking = true;
	TalkingUntil = Now + FMath::Max(0.0, DurationSeconds);
	ResetThinkTimers(Now);
}

float FElysiumNpc::PlayActivity(const FString& Activity)
{
	ScheduleIdealActivity = FElysiumClipIdentity(); // 0x10272130 bridge identity
	const int32 RequestedActivity = ElysiumRetailActivities::ValueOf(Activity); // 0x10412520
	if (RequestedActivity == INDEX_NONE) { return -1.f; } // 0x10272130 missing activity
	SetActivity(RequestedActivity); // 0x10272440 -> 0x10272130: one actual kernel selection
	return SequenceNumber >= 0 ? SequenceDurationSeconds(SequenceNumber) : -1.f; // 0x10091080
}

bool FElysiumNpc::TakeClearScheduleRequest()
{
	const bool bRequested = bClearScheduleRequested;
	bClearScheduleRequested = false;
	return bRequested;
}

void FElysiumNpc::ClearPreservePath()
{
	NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH);
}

void FElysiumNpc::DebugScheduleInstalled(int32 InstalledSchedule)
{
	FElysiumNpcBase::DebugScheduleInstalled(InstalledSchedule);   // `0x10280e50`'s "Schedule: %s"
	ElysiumNpcDebugLogging::ScheduleInstalled(*this, InstalledSchedule);
}

// --- The authored director ---

bool FElysiumNpc::BeginScriptedSchedule(const FElysiumScriptedScheduleOrder& Order,
	bool bHasForcedState, EElysiumNpcState ForcedState)
{
	using EMode = ElysiumAiScriptedSchedule::EMode;

	if (IsInert())
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s refused a scripted schedule: the NPC is dead or hidden"), *DebugString());
		return false;
	}
	if (!Mind.IsAdmitted())
	{
		// DEFERRED, not refused, and the whole push waits rather than half of it. `sm_medical_1`
		// wires `guard_to_nurse` off an `npc_maker`'s `OnSpawnNPC`, so a director genuinely reaches
		// an NPC that has never thought — and admission establishes idle on that first think, so a
		// forced state applied ahead of it would be wiped by the barrier it was racing. The order
		// carries its own forced state for exactly this window and `Think` replays it once admission
		// has run (the admission barrier, V6's).
		ScriptedScheduleOrder = Order;
		ScriptedScheduleOrder.bHasForcedState = bHasForcedState;
		ScriptedScheduleOrder.ForcedState = ForcedState;
		ScriptedScheduleOrder.bPending = true;
		return true;
	}
	// No clock reset anywhere in this function. `CCineAISchedule::vfunc586` (`0x101a98c0`) goes
	// through `SetState`, `ScheduledMoveToGoalEntity` / `ScheduledFollowPath` and
	// `SetSchedule` (`0x10280e50`), none of which writes a think stamp: an `aiscripted_schedule`
	// takes effect on the NPC's next cadence think, up to the normal law's out-of-PVS ceiling.

	// The executor `0x101a98c0`'s NPC half, in its order: the goal was resolved by the director
	// (`ElysiumAiScriptedSchedule.cpp`, the "Can't find goal entity" return ahead of everything
	// here); then `forcestate` through `SetState 0x1026e340`, which traces the edge and fires slot
	// 463 itself; then the mode's call. It is a STATE push and not a hold: the recovered entity
	// keeps no order, has no end and no release -- it fires once and the NPC carries what it was
	// given. The authored->native table (0/1->1, 2->3, 3->2) is the director's
	// (`ElysiumAiScriptedSchedule::ForcedState`); the typed state maps back to the raw id here.
	if (bHasForcedState)
	{
		SetState(NpcKernelConditionsShared::CondRetailStateId(ForcedState));   // 0x101a98c0 -> 0x1026e340
	}

	if (static_cast<EMode>(Order.Mode) == EMode::AssignEnemy)
	{
		// Mode 3, recovered: "assigns the goal entity as enemy, copies its target position, and
		// injects native condition 0x54". The assignment goes through the ordinary `SetEnemy`
		// transaction rather than writing the handle, so the last-enemy transfer and everything else
		// `SetEnemy` (`0x10279a50`) does all happen exactly once and in one place.
		FElysiumEntity* Goal = World ? World->Resolve(Order.Goal) : nullptr;
		ElysiumNpcEnemy::SetEnemy(*this, Order.Goal);                // 0x101a9a5c 0x1000b974
		if (Goal != nullptr)
		{
			// `0x101a9a6e` the goal's slot 217 (its position, from goal+0x3d4), then `0x101a9a78` slot 544.
			UpdateEnemyMemory(Goal, Goal->Origin, nullptr);
		}
		// `(*DAT_10924a6c)->vfunc1()` (`0x101a9a86`): a global object's slot 1, no argument, answer
		// discarded -- the same unrecovered call `Event_Killed` `0x10265d18` names; nothing answers.
		// The injected condition, by its recovered number: `NEW_ENEMY` is 0x54 (`0x101a9a8d` SetCondition).
		Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
		// No schedule replacement: the standing program decides whether NEW_ENEMY interrupts it.
		RecordScheduleEvent(FString::Printf(TEXT("aiscripted_schedule mode 3: enemy := %s"),
			World ? *World->DescribeHandle(Order.Goal) : TEXT("(no world)")));
		return true;
	}

	const int32 Program = ElysiumAiScriptedSchedule::ProgramFor(Order.Mode);
	if (Program == ElysiumScheduleId::None)
	{
		// A forced state with no movement mode is an ordinary authored row: two corpus rows push a
		// state alone. The push above already happened, so there is nothing left to refuse.
		RecordScheduleEvent(TEXT("aiscripted_schedule: forced state only, no movement mode"));
		return bHasForcedState;
	}

	// No `ClearSchedule`, no place release and no claim ahead of the mover (`schedule-kernel.md:663`):
	// the movers install base 2 through `0x10280de0` -> `SetSchedule 0x10280e50`, whose slot 435
	// `OnScheduleChange 0x102a0940` releases a visited place (`0x102a09a0`) under `!PRESERVE_PATH`,
	// and the NPC keeps no order (V3d deleted the port's `ScriptedSchedule` claim and its end).
	// Modes 1/2 and 4/5 are retail's own movers (`0x101a9960..0x101a99cc`, `0x101a99dc..0x101a9a14`): the
	// activity is `(-(mode != 1 | 4) & 10) + 9` -- ACT_WALK 9 for the lower mode of each pair, ACT_RUN 0x13
	// for the upper -- replaced by ACT_FLY 0x22 when slot 94 `GetMoveType` (+0x178, asked twice) answers 5
	// or 6; then modes 1/2 call `ScheduledMoveToGoalEntity(npc, 2, goal, activity)` (`0x102800c0`: program 2,
	// IDLE_WALK translated by slot 440, `m_pGoalEnt`, a type-4 `SetGoal` at 128 units) and modes 4/5
	// `ScheduledFollowPath(npc, 2, goal, activity)` (`0x102801e0`: the same program and `m_pGoalEnt`, a
	// type-3 `SetGoal` whose `DoFindPath` lays the whole path_corner chain, `NavFindPathCorners`). The
	// navigator walks the chain itself and `AdvancePath` passes each corner once, in order, the last
	// included; no port-side leg list stands between the order and the navigator.
	const bool bFollowPath = ElysiumAiScriptedSchedule::IsFollowPath(Order.Mode);
	int32 Activity = Order.bRun ? 0x13 : 9;                                                 // 0x101a9960 / 0x101a99dc
	if (GetMoveType() == 5 || GetMoveType() == 6)                                           // 0x101a996a / 0x101a99ef
	{                                                                                       // 0x101a9973 JZ / 0x101a9982 JNZ
		Activity = 0x22;
	}
	FElysiumEntity* const Goal = World != nullptr ? World->Resolve(Order.Goal) : nullptr;
	const bool bGoalSet = bFollowPath
		? ScheduledFollowPath(ElysiumSched::IDLE_WALK, Goal, Activity)                       // 0x101a9a0c
		: ScheduledMoveToGoalEntity(ElysiumSched::IDLE_WALK, Goal, Activity);                // 0x101a998f
	if (!bGoalSet && !Order.bSuppressRouteWarning)                                          // 0x101a9996 / 0x101a99a5 spawnflags & 0x800
	{
		// `DevMsg(1, ...)` in retail; a Warning here, as the goal-miss line is, because it fires on
		// shipped content whose route this runtime's mover refuses. `0x101a99b5` JNZ ("" for a
		// null goal name), `0x101a99be` GetDebugName. The follow-path modes' own line is
		// `0x101a9a42` (`0x105951d8`), guarded the same way (`0x101a9a26`).
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s to goal entity %s failed\nCan't execute script %s"),
			bFollowPath ? TEXT("ScheduledFollowPath") : TEXT("ScheduledMoveToGoalEntity"),
			Goal != nullptr ? *Goal->TargetName : TEXT(""),
			World != nullptr ? *World->DescribeHandle(Order.Source) : TEXT("(no world)"));   // 0x101a99c5 0x10595230
	}
	RecordScheduleEvent(FString::Printf(TEXT("aiscripted_schedule mode %d (%s, activity 0x%x) goal %s"),
		Order.Mode, ElysiumAiScriptedSchedule::ModeName(Order.Mode), Activity,
		World ? *World->DescribeHandle(Order.Goal) : TEXT("(no world)")));
	return bGoalSet;
}

void FElysiumNpc::InputNamedSchedule(const FElysiumInputArgs& Args)
{
	const FString Requested = Args.Param.ToString().TrimStartAndEnd();
	const FString InputName = Args.Input.IsNone()
		? FString(TEXT("ChangeSchedule")) : Args.Input.ToString();
	if (IsInert())
	{
		return;
	}
	if (Requested.IsEmpty())
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s %s was fired with no schedule name — refused"), *DebugString(), *InputName);
		return;
	}
	// The input keeps the arg-shaped half (the empty-name refusal and the surface key) that only
	// an input has; `StartNamedSchedule` is the shared door.
	StartNamedSchedule(Requested,
		FString::Printf(TEXT("CAI_BaseNPC.%s(%s)"), *InputName, *Requested),
		ElysiumStub::DescribeInput(Args));
}

bool FElysiumNpc::StartNamedSchedule(const FString& Requested, const FString& Surface,
	const FString& Detail)
{
	if (IsInert() || Requested.IsEmpty())
	{
		return false;
	}
	if (Mind.State() == EElysiumNpcState::Dead)
	{
		// The other door into schedule selection. A corpse runs the death program and nothing else,
		// so a script's `ChangeSchedule` or a discipline's `AI_Schedule` channel arriving after the
		// death commit is refused here rather than replacing it.
		RecordScheduleEvent(FString::Printf(TEXT("refused '%s': this NPC is dead"), *Requested));
		return false;
	}
	// `CAI_ScheduleManager::FindByName` (`0x1030f350`) over the whole loaded corpus, which is one
	// namespace of names across every class. Before 0019/3 this resolved against 28 hand-typed
	// programs and the shipped content's own requests -- `SCHED_VDOG_SNARL`, `SCHED_VDOG_MADEFRIEND`
	// and the Berserk/Possession families the `disciplinetgt` records name -- all missed. They
	// resolve now.
	const FElysiumScheduleProgram* Program =
		FElysiumScheduleCorpus::Get().Manager().FindByName(Requested);
	if (Program == nullptr)
	{
		// Still keyed on the caller AND the name, so `elysium.stubs` reads back exactly which native
		// schedules the shipped content wants that the corpus does not carry -- which, after the
		// switchover, should be only the literal `-` and anything genuinely misspelled.
		ElysiumStub::Fired(TEXT("schedule"), Surface, DebugString(), Detail,
			TEXT("no loaded program carries that name"));
		RecordScheduleEvent(FString::Printf(TEXT("refused: '%s' is not a loaded program"),
			*Requested));
		return false;
	}
	return StartScheduleId(Program->GlobalId, Surface, Detail);
}

bool FElysiumNpc::StartScheduleId(int32 Id, const FString& Surface, const FString& Detail)
{
	if (IsInert())
	{
		return false;
	}
	if (Mind.State() == EElysiumNpcState::Dead)
	{
		RecordScheduleEvent(FString::Printf(TEXT("refused schedule 0x%x: this NPC is dead"), Id));
		return false;
	}
	// A named schedule starts through the ordinary kernel: interrupts, fail schedules, motor work and
	// activity translation are all whatever the named program declares. Nothing about being named by
	// a script — or by a Discipline record — changes how it runs, which is the whole recovered point
	// of these commands.
	(void)Surface;
	(void)Detail;
	// No clock reset HERE: the `ChangeSchedule` / `StartSchedule` inputs are not slot-614 sites.
	// The two callers that are -- the discipline applier `0x101de660` and `FeedInterrupt`
	// `0x1033a9e0`, each "slot 614, then `SetSchedule`" -- re-base the clock themselves before
	// they come through this door.
	return ElysiumSchedule::Start(Schedule, Id, *this);
}

// The `+0x6320 m_flGoalTolerance` store the species arms make directly (`FSTP +0x6320`: the frenzy
// shadow `0x10375fe5`, the werewolf `0x103ccec1`); the Troika arm `0x102a4289` writes the same word.
void FElysiumNpc::SetGoalTolerance(float Units)
{
	ScheduleHost.GoalToleranceCm = Units * ElysiumMove::U;
}

void FElysiumNpc::TaskFail(int32 Reason)
{
	// Troika slot 448 (0x1029adb0), then its direct call to CAI_BaseNPC 0x10273fc0
	// (`FElysiumNpcBase::TaskFail`). In particular,
	// OnScheduleChange is not a substitute: its masks and oblivious refcount writes differ.
	//
	// Nine species classes override slot 448 on their C++ classes (story 5 step 3): eight run their
	// own arm and then call this body directly (`0x1029adb0`); SabbatLeader's returns first on its
	// route-flip arm.
	// Step 1: `0x102b53d0(this, 1, "Leaving interesting place (TaskFail)")` (`0x1029adb4..0x1029adbd`),
	// unconditional -- the held-place test is inside the release.
	FinishAmbientUse(/*bFireLeft=*/true);
	const FElysiumNpcNavigationSample Nav = Motor ? Motor->SampleNavigation() : FElysiumNpcNavigationSample();
	if (Nav.Type != EElysiumNpcNavType::Jump && Nav.Type != EElysiumNpcNavType::Climb)
		NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH);
	if (Motor) Motor->ResetSteering();
	const double Now = World ? World->NowSeconds() : 0.0;
	ScheduleHost.DesiredMoveYaw = 0.f;
	// The four stamps and NOT `m_flNextThink`: `0x1029adb0` writes `+0x6244..+0x6250` only, and
	// slot 614 (`ResetThinkTimers`) is the one that also writes the entity think. The difference is
	// observable -- a fail from INSIDE a think has its reset consumed by the tail's `Calc*` on the
	// way out, so only a fail raised from outside one forces the next think full.
	ScheduleHost.ResetThinkTimers(Now);
	ScheduleHost.GoalToleranceCm = 0.f;
	Schedule.ToleranceUnits = 0.f;
	ScheduleHost.InsideInterruptDistanceSqr = ScheduleHost.OutsideInterruptDistanceSqr = 0.f;
	ScheduleHost.InterruptTime = 0.0;
	ScheduleHost.MoveTarget = FElysiumEntityHandle::Invalid();
	if (FElysiumEntity* Prop = World ? World->Resolve(ScheduleHost.KickProp) : nullptr)
	{
		if (Prop->Class && Prop->Class->ClassName == FName(TEXT("prop_physics")))
			static_cast<FElysiumPhysProp*>(Prop)->bNpcKickable = false;
		ScheduleHost.KickProp = FElysiumEntityHandle::Invalid();
	}
	BaseScheduleHost.MemoryBits &= ~0x2000u;
	BaseScheduleHost.MemoryBits &= 0x0fffffffu;
	const bool RestoreSleep = NpcFlags.Has(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX);
	if (NpcFlags.OnTaskFail())
		RecordScheduleEvent(TEXT("TaskFail retail leak: MADE_OBLIVIOUS cleared, refcount retained (0x1029adb0)"));
	if (RestoreSleep)
	{
		SetAttackExtents(ScheduleHost.SavedSleepExtents);
		ScheduleHost.SavedSleepExtents = FVector(-1.0);
		NpcFlags.Clear(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX);
	}
	ScheduleHost.bSavePositionWalk = false;
	ClearScheduleHint(5.f);
	BaseScheduleHost.bMotorAnimationMovement = false;
	ScheduleHost.Unknown6300 = ScheduleHost.Unknown659c = 0;
	ScheduleHost.bPatrolPathUseHint = false;
	bMoveIssued = false;
	ScheduleHost.PendingFailureReason = 0;
	// `0x1029adb0` ends in the direct call to the base half.
	FElysiumNpcBase::TaskFail(Reason);
	RecordScheduleEvent(FString::Printf(TEXT("TaskFail 0x%x: %s"), Reason, ElysiumTaskFailureName(Reason)));
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s TaskFail 0x%x: %s"), *DebugString(), Reason, ElysiumTaskFailureName(Reason));
}

void FElysiumNpc::ScheduleDone()
{
	Cognition.Conditions.Set(EElysiumNpcCond::ScheduleDone);
	// No external-executor return (V3b): `MaintainSchedule 0x102817c0` reselects in the same pass
	// (`0x10281be5`, `0x10281c46`); a place and a patrol are programs `SelectSchedule` case 1 returns.
}

void FElysiumNpc::ClearScheduleHint(float ReuseDelay)
{
	// 0x10295ab0 `CAI_BaseNPCTroika::ClearHintNode`: a missing hint performs no writes. The release
	// `0x102d1420(hint, delay)` (the live hint's `m_hHintOwner +0x5e0 = -1`, `m_flNextUseTime +0x5ec =
	// curtime + delay`) runs only when `0x102d1450` says this NPC owns the hint, so another owner's
	// hint is forgotten locally without imposing our cooldown on it (`shape.md` "The claim
	// primitives"). The four clears below run either way.
	if (BaseScheduleHost.HintNode == INDEX_NONE) return;
	if (OwnsHint(BaseScheduleHost.HintNode))                      // 0x102d1450
	{
		ReleaseHintNode(BaseScheduleHost.HintNode, ReuseDelay);   // 0x102d1420
	}
	BaseScheduleHost.HintNode = INDEX_NONE;
	ScheduleHost.FailedCoverLosChecks = 0;
	NpcFlags.Clear(EElysiumNpcFlag::AT_COVER_HINT);
	ScheduleHost.SavedSleepExtents = FVector(-1.0);
}

void FElysiumNpc::ClearOwnedActivityCopyProps()
{
	// 0x1018e910 enumerates activity_copy_prop and deletes rows whose owner +0x730
	// resolves to this NPC. Its class and owner producer are not implemented yet;
	// this named seam currently finds no owned copies.
}

void FElysiumNpc::EndDisciplineSchedule()
{
	// HitInfo expiry 0x101def10 reconnects first, then TaskComplete(false) for only
	// the two interruptible temporary programs. It neither clears nor replaces a schedule.
	if (NpcFlags.Has(EElysiumNpcFlag2::D_DISCONNECT_SQUAD)) ReconnectToSquad();
	const int32 Number = GetLocalScheduleId(Schedule.Current);
	if ((Number == 0xe1 || Number == 0xe3) && !Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed))
	{
		Schedule.TaskStatus = EElysiumTaskStatus::Complete;
		TraceTaskDone();   // the AI trace's `taskdone` (debug output only, behind its sink)
	}
}

// --- The gaze cascade's NPC arms ---

const FElysiumEntity* FElysiumNpc::GazeTargetEntity() const
{
	// `m_hTargetEnt`, resolved; a stale handle answers nothing, as retail's handle test does.
	return World != nullptr ? World->Resolve(TargetEnt) : nullptr;
}

const FElysiumEntity* FElysiumNpc::GazeEnemy() const
{
	// `GetEnemy()`: the committed enemy, dead or alive — the cascade itself refuses an inert one,
	// which is the `IsInert()` half of retail's handle test, not an extra rule.
	if (World == nullptr || !BaseMemory.Enemy.IsSet())
	{
		return nullptr;
	}
	return ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy);
}

bool FElysiumNpc::GazeNavigationGoal(FVector& OutPoint) const
{
	// `m_pNavigator->IsGoalActive()` then `GetGoalPos()`, lifted to eye height when the goal is a
	// position rather than an entity (retail's `+0x18 == 0` branch reads its own EyePosition().z):
	// a character walking somewhere glances at where it is going, not at the floor there.
	if (!NavIsGoalActive())                       // 0x102ee6a0 IsGoalActive
	{
		return false;
	}
	OutPoint = Navigator.GetGoalPos();            // 0x102ee140 GetGoalPos (path+0x4c - path+0x34)
	OutPoint.Z = EyePosition().Z;
	return true;
}

bool FElysiumNpc::GazeHeardSound(FVector& OutPoint) const
{
	// Retail pairs condition 0x6d with `GetBestSound()` type 1 and 0x6a with type 8, and in the
	// SDK of the era those bits are SOUND_COMBAT and SOUND_DANGER. The hearing gather promotes the
	// last stimulus to exactly these conditions from its category, so the condition standing IS
	// the best-sound test; the stimulus it stood on is the point. `HEAR_DANGER` is never raised
	// here (no recovered category maps to it — see `GatherHearing`), which leaves the combat half
	// live and the danger half waiting on that recovery rather than on this arm.
	const FElysiumNpcConditions& Conds = Cognition.Conditions;
	if (!Conds.Has(EElysiumNpcCond::HearCombat) && !Conds.Has(EElysiumNpcCond::HearDanger))
	{
		return false;
	}
	if (Senses.Memory.LastHeardTime < 0.0)
	{
		return false;
	}
	OutPoint = Senses.Memory.LastHeardPosition;
	return true;
}

namespace
{
	// The active weapon controller, or null. Shared by the two attack tasks so a missing weapon
	// fails both the same way.
	FElysiumWeapon* ElysiumNpcActiveWeapon(FElysiumNpc& Npc)
	{
		FElysiumItem* Item = Npc.Inventory.Active(Npc);
		return Item != nullptr ? Item->AsWeapon() : nullptr;
	}
}

void FElysiumNpc::ClearConditions()
{
	// Retail's `SetSchedule` zeroes all 192 condition bits. See
	// `IElysiumScheduleRunner::ClearConditions` for why this is half of `DELAY_INTERRUPTS`.
	Cognition.Conditions.Reset();
}

void FElysiumNpc::BuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
	// Slot 453's species bodies are overrides on their C++ classes (story 5 step 3): `CNPC_VGuard1`
	// (`0x1037cdf0`) calls the EMPTY base `CAI_BaseNPC::BuildScheduleTestBits` (`0x10280fb0`) and so
	// REPLACES this body; `CNPC_VHumanCombatant` (`0x10387520`), `CNPC_VPedestrian` (`0x103a2980`)
	// and `CNPC_VTzimisceHeadClaw` (`0x103c16f0`) call this body first and add.

	// `CAI_BaseNPCTroika::BuildScheduleTestBits` (`0x102ad140`), transcribed. The base
	// (`0x10280fb0`) is empty.
	if (!NpcFlags.Has(EElysiumNpcFlag::DONT_INVESTIGATE) && !NpcFlags.Has(EElysiumNpcFlag::IN_FLEE_SCHED))
	{
		if (!IsBusyWithDiscipline() && !NpcFlags.Has(EElysiumNpcFlag2::D_POSSESSED))
		{
			// `if (!m_pHintNode || m_pHintNode->type != 0x2774)`. Hint nodes of that type are not
			// modelled by this substrate, so the guard is always open here; it is named so the day
			// a hint of that type lands it has its consumer.
			InOutMask.Set(EElysiumNpcCond::InvestigateLevel);
			InOutMask.Set(EElysiumNpcCond::CriminalFleeLevel);
			InOutMask.Set(EElysiumNpcCond::SupernaturalFleeLevel);
			if (!BaseMemory.Enemy.IsSet())
			{
				// `m_bfNPCStateFlags` bits 4 and 5 -- the per-state capability byte `0x1026e3e0`
				// writes on every state change: set in idle (0x31) and alert (0x39), clear in
				// combat (0x8f), scripted (0x8), dead and the flee states. Both bits travel
				// together in every value the table writes, so one state test answers both.
				const EElysiumNpcState State = Mind.State();
				if (State == EElysiumNpcState::Idle || State == EElysiumNpcState::Alert)
				{
					InOutMask.Set(EElysiumNpcCond::HearFlinch);
					InOutMask.Set(EElysiumNpcCond::CriminalAttackLevel);
					InOutMask.Set(EElysiumNpcCond::SupernaturalAttackLevel);
				}
			}
			if (!NpcFlags.Has(EElysiumNpcFlag::COWERING))
			{
				InOutMask.Set(EElysiumNpcCond::Comfort);
			}
		}
	}
	if (NpcFlags.Has(EElysiumNpcFlag2::IGNORE_SQUAD_SEE_ENEMY))
	{
		InOutMask.Clear(EElysiumNpcCond::SquadSeeEnemy);
	}
}

int32 FElysiumNpc::SelectDoorObstructionSchedule()
{
	const double Now = World ? World->NowSeconds() : 0.0;

	// The source order is retail's own. A blocked-door handle outlives its usefulness, so an
	// expired one is cleared rather than reused.
	const FElysiumEntity* Source = nullptr;
	if (BlockedDoor.IsSet() && World)
	{
		if (Now < BlockedDoorExpiresAt)
		{
			Source = World->Resolve(BlockedDoor);
		}
		else
		{
			BlockedDoor = FElysiumEntityHandle::Invalid();
		}
	}
	if (Source == nullptr && bCondHitByDoor && CondHitByDoor.IsSet() && World)
	{
		Source = World->Resolve(CondHitByDoor);
	}
	if (Source == nullptr)
	{
		return ElysiumScheduleId::None;
	}

	// With a source chosen, retail asks the hint machinery for cover and takes it when the claim
	// is medium, low or corner cover. We carry no hint-node reader yet, so this falls through to
	// the distance test -- the authored `info_node_cover_med/low/corner` entities the branch
	// needs are exported but unread.
	SavePosition = Source->Origin;

	// 256 units, compared squared, in Source units -- the same 2.54 cm/inch the whole runtime
	// reads verbatim.
	constexpr double ThresholdCm = 256.0 * 2.54;
	const bool bNear = FVector::DistSquared(Origin, SavePosition) <= ThresholdCm * ThresholdCm;
	// The recovered table branches on `GetEnemy`: the enemy rows are `SCHED_TROIKA_BACK_AWAY_FROM_DOOR`
	// (0x90) and `_WAIT` (0x94), the no-enemy rows their `_NE` variants (0x91 / 0x96). Only the two
	// `_NE` programs are registered here, so an NPC that HAS an enemy is a divergence rather than a
	// branch -- and it is recorded by name instead of taken quietly.
	if (BaseMemory.Enemy.IsSet())
	{
		RecordScheduleEvent(TEXT("door obstruction with an enemy: SCHED_TROIKA_BACK_AWAY_FROM_DOOR "
			"(0x90) / _WAIT (0x94) are not registered — taking the _NE variant"));
	}
	return bNear ? ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE
		: ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE;
}

void FElysiumNpc::FinishAmbientUse(bool bFireLeft)
{
	// `0x102b53d0` (`docs/vtmb/npc-ai/lifecycle.md` § `UpdateOnRemove`; the flag arm at
	// `schedule-kernel.md` § `RunTask` integration notes), read off the listing. `bFireLeft` is its
	// flag argument (`[ESP+0x4c]`, tested at `0x102b54e3`). No motor stop, no pose: the body is the
	// running program's.
	if (CurrentSpotIndex != INDEX_NONE)                                     // +0x62ec != 0
	{
		// `0x10299a80`, the re-check, then `if (+0x62ec == 0) return` -- an early return that SKIPS
		// the `+0x62e8` clear below. Its first refusal (the place is no longer a live entity) is
		// ported here. **Unrecovered in the substrate:** its second refusal (the place's marker table
		// `+0x580` does not name this NPC) -- the port keeps the visitor record as the place's claimant
		// set and `FMarker::Occupant` has no writer, so that test would refuse every visit.
		ValidateRestoredInterestingPlace(); // 0x102b53d0 -> 0x10299a80 both consistency refusals
		if (CurrentSpotIndex == INDEX_NONE) return; // preserve arrived latch on invalid release
		FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
		// **Unrecovered:** `0x102b5451..0x102b54e0`, two `EmitSound`s through a
		// `CPASAttenuationFilter` (channel 4 then 2, pitch `0x24`, volume 100); the sample names come
		// from the sound-emitter interface (`DAT_1070b22c +0x8c`) and are not read. Not emitted.
		// (`0x1028a150` `GetCurTask` is called ahead of them; its answer is unused.)

		// `0x102b54e3..0x102b5511`: with the flag set and `+0x62e8` arrived, fire this NPC's
		// `m_OnInterestingPlaceLeft` (`+0x5f8c`, activator the place, caller this, `0x10010794`); the
		// detach's own flag is "still arrived after that output".
		bool bFired = false;
		if (bFireLeft && bAmbientArrived)
		{
			FireOutput(FName(TEXT("OnInterestingPlaceLeft")), Spot->Handle);      // 0x102b5500
			bFired = bAmbientArrived;                                              // 0x102b5505
		}
		// `0x102da600(place, this, 1, fired)` (`0x102b551d`): `npc +0x62fc := place` before its
		// marker scan, then on the visitor match the place's `OnNPCLeft` (`+0x468`) when `fired`, and
		// the visitor record removed.
		Spot->Release(Handle, bFired); // 0x102da600 clears occupant before conditional place output, then swap-removes row
		CurrentSpotIndex = INDEX_NONE;                                              // 0x102b553c +0x62ec
		AmbientPhase = EAmbientPhase::None;                                         // 0x102b5542 +0x6304
		NpcFlags.Clear(EElysiumNpcFlag::INTERESTING_INTO);                          // +0x14b8 &= 0xdfffffff
		// `+0x14bc &= 0x7ffffff7` (`0x102b5534`): `INTERESTING_LOST` (bit 3) and the unnamed bit 31.
		NpcFlags.ClearRawWord2Bits(static_cast<uint32>(EElysiumNpcFlag2::INTERESTING_LOST)
			| FElysiumNpcFlags::Word2UnnamedBit31);
		// **Unported:** `0x102ae310` (`0x102b5554`), the Troika weapon holster policy -- no port body
		// yet (`ElysiumNpcStartTask_2.cpp` counts its other call site).
	}
	bAmbientArrived = false;                                                    // +0x62e8, always
}

bool FElysiumNpc::IsValidStealthKillTarget(const FElysiumPlayer& /*Attacker*/) const
{
	// `CAI_BaseNPCTroika` `0x102c2300`. `CNPC_VGhoulCroucher` `0x1037bbc0` short-circuits when
	// `!IsDisturbed()` (`+0x6666`); that producer is not in this runtime, so every classname
	// including a croucher takes the Troika body. The debug ConVar at `DAT_10924afc` that
	// relaxes the state test to "not DEAD" is a developer arm and is not ported.
	if (bHidden)
	{
		return false;
	}
	if (!DialogName.IsEmpty())
	{
		return false;
	}
	if (bInvincible)
	{
		return false;
	}
	const EElysiumNpcState State = GetMind().State();
	if (State != EElysiumNpcState::Idle && State != EElysiumNpcState::Alert)
	{
		return false;
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::HearPlayer)
		|| Cognition.Conditions.Has(EElysiumNpcCond::SeePlayer))
	{
		return false;
	}
	return !bDead && !HasReportedDeath();
}

void FElysiumNpc::SeedSheet()
{
	UElysiumSessionSubsystem* GameState = World ? World->GetGameState() : nullptr;
	const FElysiumStatTable* Table = GameState ? GameState->Stats() : nullptr;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	FElysiumClanTemplate Resolved;
	const bool bResolvedTemplate = !StatTemplate.IsEmpty()
		&& Rules && Rules->Clans().Resolve(StatTemplate, Resolved);
	bFastFood = false;
	bHasKindredTemplate = false;
	bKindredTemplate = false;
	bDisallowKnockbacks = false;
	TemplateBloodPoolValue = 0;
	for (bool& bHas : bHasDamageFilter) { bHas = false; }
	if (!Table)
	{
		return;   // no rulebook: the sheet stays zeroed and the damage path stays fail-closed
	}
	Sheet.SeedFrom(*Table);

	if (!StatTemplate.IsEmpty())
	{
		if (bResolvedTemplate)
		{
			ApplyResolvedTemplate(Resolved, Table, Rules ? &Rules->ExcludedEquip() : nullptr);
			// A template names its clan's `TraitEffectGroup`; that group is where the clan's
			// gifts and banes live, for an NPC exactly as for the player.
			const FString ClanEffect = Resolved.GeneralStr(TEXT("ClanEffect"));
			if (!ClanEffect.IsEmpty())
			{
				Effects.AddUnique(ClanEffect);
			}
			RebuildEffects();
		}
		else
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s stattemplate '%s' resolves to nothing"),
				*DebugString(), *StatTemplate);
		}
	}
	SyncHealthFromSheet();
}

void FElysiumNpc::Spawn()
{
	// Slot 103 is `CAI_BaseNPCTroika::Spawn` (`0x10298d30`), every arm in `TroikaSpawnBody`
	// (`ElysiumNpcSpawn.cpp`): the stat template, `Precache`, `SetModel`, the Troika words, the
	// collision, `CAI_BaseNPC::Spawn` (`0x10299006`), the initial position, `NPCInit` (`0x10299057`),
	// the police-level repair, the occluded ladder and `AddFlag2(4)`. Every species body that retail
	// chains to `0x10298d30` calls this qualified.
	//
	// The skeletal body and its motor are stood by the body's own slot-105 `SetModel` (`0x10298dd4`
	// -> `TroikaSetModel` `0x10298ce0` -> `SetRuntimeModel`, whose body-follow hook builds them, gated
	// by `elysium.NpcBodies`): the presentation follows retail's model write, after the sheet seed and
	// `Precache` and before `NPCInit`, and `CNPC_VCamera::Spawn` (`0x10368b70`), which never reaches
	// this body, stands its own through the same slot (`0x10368bab`).
	TroikaSpawnBody();
}

void FElysiumNpc::Activate()
{
	// `CAI_BaseNPCTroika::Activate` (`0x1028e310`), slot 113: after `CBaseEntity::Activate`, an NPC
	// whose `Classify()` is non-zero applies `m_sDefaultDisposition` through `SetDisposition(name, 1)`.
	// FIRST, as retail does, so everything below sees the stance the disposition selected. Story
	// 29c-1, family Lifecycle; the body is `ApplyDefaultDispositionOnActivate`.
	ApplyDefaultDispositionOnActivate();
	SeedPlayerRelationship();
	// The hearing cursor starts at the live head so an NPC never hears the map's own load.
	Senses.StartSoundCursorAtHead(*this);
	Mind.ArmAdmission();
	if (bHidden) // 0x100a8710: lawful activation/body admission does not unpark NPCThink
	{
		if (!bSavedPhysicalWordsAvailable)
		{
			bSavedPhysicalWordsAvailable = ReadScriptPhysicalWords(ScriptSavedSolid, ScriptSavedMoveType,
				ScriptSavedMoveCollide, ScriptSavedSolidFlags, ScriptSavedEffects);
			if (bSavedPhysicalWordsAvailable) WriteScriptPhysicalWords(0, 0, 0, 4, 0xe0);
		}
		ThinkCallback = NAME_None;
		NextThink = ELYSIUM_NEVER_THINK;
		RestoreThinkCallback();
	}
	// Slot 420 `NPCInit` (`0x1029a0b0`) -- the writer of the eight think stamps, the PVS/LOS seeds,
	// `m_flNextThink` and `InitPerceptionDistances` (`0x1029a6a4`) -- is NOT called here: retail runs
	// it inside `Spawn` (`CAI_BaseNPCTroika::Spawn` `0x10299057`; `CNPC_VCamera::Spawn` `0x10368cf1`),
	// so its first-second arm (`ThinkSet(NPCInitThink, 0)`, `m_flNextThink = curtime + 0.1`,
	// `10273a5x`, `_DAT_104493d0`) stamps from the map's spawn clock, and a restored NPC (which is
	// not re-spawned in retail) is not re-initialised over its snapshot.
}

void FElysiumNpc::SetDisableAi(bool bDisable)
{
	if (bDisableAi == bDisable)
	{
		return;
	}
	bDisableAi = bDisable;
	// Coming back on, the body is due on every clock again -- the same state `NPCInit` leaves a
	// fresh NPC in. Going off, `Think`'s own gate parks `NextThink`; doing it here as well would
	// silence a body that is mid-think.
	if (!bDisableAi)
	{
		ResetThinkTimers(World ? World->NowSeconds() : 0.0);
	}
}

void FElysiumNpc::SetBodyHeld(bool bHeld)
{
	if (Motor)
	{
		Motor->SetHeld(bHeld);
	}
}

void FElysiumNpc::SetBodyAnimationHeld(bool bHeld)
{
	if (Motor)
	{
		Motor->SetAnimationHeld(bHeld);
	}
}

void FElysiumNpc::ResetThinkTimers(double Now)
{
	// Slot 614 `0x102c23f0`: the four `Next` stamps and `m_flNextThink`, all to curtime. The
	// `Last` mirrors are deliberately untouched -- retail leaves them, so the interval the next
	// `Calc*` reports is measured from the stamp the reset overwrote.
	ScheduleHost.ResetThinkTimers(Now);
	ArmThinkNow(Now);
}

void FElysiumNpc::ArmThinkNow(double Now)
{
	// `m_flNextThink := curtime`, alone.
	ArmThinkAt(Now);
}

bool FElysiumNpc::BypassesKnockbackEligibility() const
{
	// Slot 400 `AllowsKnockbackBypass`, through the vtable: `CNPC_VTzimisceRunner` (`0x103c3060`) is
	// the one class whose body returns 1, on its C++ class (story 5 step 3); every other class keeps
	// `CAI_BaseNPC`'s `0x1014fa50`.
	return const_cast<FElysiumNpc*>(this)->AllowsKnockbackBypass();
}

void FElysiumNpc::OnRuntimeModelChanged()
{
	// The rebuild is FElysiumScriptedCharacter's; the A/B gate is this leaf's.
	RebuildForModelChange(CVarNpcBodies.GetValueOnGameThread() != 0);
}

void FElysiumNpc::ReleaseOnDeathOrDormancy(const TCHAR* Reason, bool bDeadMind)
{
	// The open conversation, closed when this NPC owns it -- keyed on the world's one dialogue
	// (`CDialog`'s owner), not on a token (V3d deleted the dialogue body claim). The close runs
	// `CDialog::Release 0x100e5240` -> `0x102c0360` (D1's door). UNRECOVERED: whether retail runs
	// `CDialog::Release` on the NPC's death or removal at all, or leaves it to `0x102c1400`'s
	// partner-gone arm (D1's open item); the port closes here, as it did before V3d.
	if (World && World->GetOpenDialogOwner() == Handle)
	{
		World->CloseDialog(/*bSilent=*/true);
	}
	// Death and dormancy: retail's removal-side callers (`Event_Killed`, `UpdateOnRemove
	// 0x1028d6e0`) pass 0 to `0x102b53d0`.
	FinishAmbientUse(/*bFireLeft=*/false);
	ClearSchedule();
	const int32 TracedState = NpcStateRetail();   // read for the AI trace's `state` event only
	// Death's `SetState(7)` (`Event_Killed 0x10265ad0`). The dormancy arm's write (state Idle) has
	// no retail writer named in `docs/vtmb/` (seam: `m_NPCState +0x5cc0`'s writer on `ScriptHide` /
	// `Kill` is unrecovered); it is kept as the port wrote it.
	Mind.Invalidate(Reason, bDeadMind);
	TraceStateChange(TracedState, NpcStateRetail());
}

bool FElysiumNpc::IsTransmitted() const
{
	// `0x100ab020`: `if (*(float*)(this+0x90) <= curtime) { if (m_fEffects & 0x40) return false; ...
	// }` — a future stamp at `+0x90` (name unrecovered, no port word) transmits regardless, and the
	// combat-character override `0x103407b0` transmits while `m_clientAuraCount` is non-zero (no port
	// word either). Both stand at the arm that honours the bit. The one writer of the bit on this
	// line is `GetControllerNPC` `0x10161a70`'s `|= 0x60`.
	return (EffectsWord & 0x40u) == 0;
}

void FElysiumNpc::OnDormancyChanged()
{
	FElysiumCombatCharacter::OnDormancyChanged();
	RestoreThinkCallback(); // 0x100a8710/0x100a8990 public FUNCTION identity follows hide/unhide
	if (IsInert())
	{
		if (!bHidden && (World == nullptr || !World->IsApplyingSnapshot()))
			ReleaseOnDeathOrDormancy(bDead ? TEXT("death") : TEXT("dormancy"), bDead); // 0x100a8710 hidden/restore does not clear schedule or release markers
		// A hidden body is off screen and off its route anyway; the hold must not outlive the
		// silence that placed it, or the unhide would wake a body that cannot move.
		SetBodyHeld(false);
		SetBodyAnimationHeld(false);
	}
	else
	{
		// Waking, not going dormant: `ScriptUnhide`'s Troika tail `0x102c1ec0` dispatches slot 614
		// right after `CBaseEntity::ScriptUnhide`, so an unhidden body thinks on this frame.
		//
		// A RESTORE is not an unhide, and must not take this arm. The four think clocks
		// (`m_flNextUpdateThink` .. `m_flNextAIThink`, `+0x6244`..`+0x6250`) are retail `SAVE` rows
		// that the generated datamap walk restores, and `ApplyEntityRecord` calls this hook after
		// that walk -- so re-arming here would throw the saved cadence away and make every restored
		// NPC think immediately, which is a thing retail's own `OnRestore 0x1027bf50` never does.
		// (Found by `Elysium.Substrate.NpcKernelBindings.SaveRoundTrip`, 0019/2 pass C.)
		if (World == nullptr || !World->IsApplyingSnapshot())
		{
			ResetThinkTimers(World ? World->NowSeconds() : 0.0);
		}
	}
	if (Motor)
	{
		Motor->SetEnabled(!IsInert());
	}
}

// The NPC's leaf record. Retail's `CAI_BaseNPC::Save 0x1027bc60` writes exactly one block by hand
// -- `AIExtendedSaveHeader_t` -- and defers every other word to the datamap walk; this port's
// generated SAVE walk (`AddNpcSaveFields`, 199 rows) IS that walk, and `ApplyEntityRecord` restores
// it by name before this blob is read. So what is left here is retail's one hand block plus the two
// things a datamap cannot reach: port state that has no retail row at all (the patrol and ambient
// bookkeeping 0018's places need), and the words the shape map records as `PRIVATE`, whose owning
// struct keeps them behind an accessor no compiled path reaches.
//
// Nothing in this record fixes anything up. A restored handle's epoch, a re-found program, a value
// another build wrote, a cursor into session state -- all of that is `OnPostRestore` below, which
// is retail's slot 130. Keeping the two apart is what stops a decode from depending on the order
// the blocks happen to be written in.
void FElysiumNpc::Serialize(FElysiumSaveArchive& Ar)
{
	// Troika `Save` calls the base's `0x1027bc60` first, and `Restore` runs the base's `Restore`
	// first: the base half of the record leads (story 5 step 5).
	FElysiumNpcBase::Serialize(Ar);
	SerializePedestrianLink(Ar);  // Troika Save's tail: `+0x630c != 0`, then `link+4` / `link+8` (the archive is the mechanism)
	SerializePatrolBlock(Ar);
	SerializeMakerBlock(Ar);
	Ar.Time(NextAttackTime); // combat +0x1564 TIME SAVE, unlike weapon +0x730/+0x734 FLOAT; 0x102551ff
	Ar << SequencePlaybackRate << RenderAlphaByte << RenderFxWord; // +0x6f4 FLOAT / +0x1a3 BYTE / +0x168, 0x1008df10/0x10269960
	Ar << NavHeadCorner; // wp+0x20 retained semantic corner, 0x102f2330
	int32 SavedSequenceRows = SequenceRows.Num(); // sequence bridge identities, 0x1008df10
	Ar << SavedSequenceRows;
	if (Ar.IsLoading())
	{
		if (SavedSequenceRows < 0 || SavedSequenceRows > 65536) { Ar.SetError(); return; }
		SequenceRows.SetNum(SavedSequenceRows);
		SequenceDescriptorRows.Reset(); // model/cache rebind without ResetSequenceInfo or RNG
	}
	for (FSequenceRow& SavedSequenceRow : SequenceRows)
		Ar << SavedSequenceRow.OwnerStem << SavedSequenceRow.Label << SavedSequenceRow.RawIndex
			<< SavedSequenceRow.bLoops << SavedSequenceRow.bSnap << SavedSequenceRow.Seconds;
	Senses.Serialize(Ar);
	Ar << bLoadoutResolved;
	Disciplines.Serialize(Ar);
	SerializeDisciplineFlags(Ar);
	ScheduleHost.Serialize(Ar);
	// `m_flNextComfortCheckTime`, a retail `SAVE` row and a recorded gap in the generated walk
	// (`ElysiumNpcKernelBindings.cpp`: "the port member exists but its owner keeps it private"), so
	// the record carries it until that owner opens a path.
	Ar.Time(NextComfortCheckTime); // +0x62f8 TIME SAVE, 0x101a0a80
	// `m_fEffects` (`CBaseEntity +0x19c`, datamap offset 412, flags 6 = `SAVE|KEY`, external
	// `effects`): a retail save row the generated walk does not bind, so the record carries it. It is
	// what keeps a restored `!playercontroller` undrawn (`EF_NODRAW`, `GetControllerNPC` `0x10161a70`);
	// `OnPostRestore` re-applies the draw gate over it.
	Ar << EffectsWord;
}

// Retail's slot 130, `CAI_BaseNPCTroika::OnRestore 0x102998c0` over `CAI_BaseNPC::OnRestore
// 0x1027bf50`, reached for the first time from the port's own persistence. Everything the archive
// deliberately did not do lives here: the schedule re-find by name and checksum, the give-up arm,
// the handle re-stamping the walk cannot reach, the clamps on values another build wrote, and the
// caches that are session state rather than simulation state.
void FElysiumNpc::OnPostRestore(FElysiumEntityWorld& InWorld)
{
	// --- Retail's own chain, arm for arm (the seams inside it are named at their sites). ---
	// `FElysiumNpcBase::OnRestore` re-finds the program the record named, compares the checksum of its task
	// array against the one the record carries, and gives up to `RestoreGiveUp` when either fails.
	PrepareRestoredBody(); // 0x100aa140 synchronize motor before 0x102ee1e0 re-find
	OnRestore(InWorld.IsLevelTransitionRestore()); // 0x1011a710/0x1011a620; true only with an old level
	RestoreThinkCallback(); // 0x100aa140; valid OnRestore does not install/start the program
	RestoreNativeAnimation(); // 0x1008df10 seek adapter; no replay or ResetSequenceInfo

	// --- The port's re-derivations, which retail has no counterpart for. ---
	// Each of these is a component saying what its own restored words mean; the order is the order
	// the components depend on one another in, and no component reads an archive.
	ScheduleHost.OnPostRestore();
	// 0x101a2e40: owned EHANDLE fixup already completed before OnRestore.
	Senses.OnPostRestore(*this); // transient sight cache/tuning only
	RestoreDisciplineState(InWorld); // 0x10323b60 owned effect/cache reconstruction after the native hook
	RestorePatrolAndAmbient();
	// 0x1027bf50: raw mind words were decoded before prerequisite validation.

	// 0x1027bc60: condition masks are transient; BOOL gathered was saved separately.
	Cognition.Conditions.Reset();
	RestoreDeathBodyState();
	// The restored `m_fEffects` decides whether the body is transmitted (`ShouldTransmit` `0x100ab020`).
	RefreshVisualGate();
}

// 0x102998c0: marker rows are restored before this consumer; no post-load claimant replay.
void FElysiumNpc::RestorePatrolAndAmbient()
{
	ValidateRestoredInterestingPlace(); // 0x10299a80, no Claim/arrival output or route reset
}

// 0x1032c0e0/0x10090180: restore visual corpse handoff without logical death replay.
void FElysiumNpc::RestoreDeathBodyState()
{
	if (Mind.State() != EElysiumNpcState::Dead)
	{
		return;
	}
	// Presentation restoration only (0x10090180): never replay OnDeath, picks or corpse clocks.
	// A rebuilt visual gets one handoff; calling restore again on the same visual does nothing.
	bDeathCommitted = true;
	if (!HasClientRagdollRig() || (MiscFlags & 0x80000u) != 0
		|| CorpseForceBone(nullptr) == INDEX_NONE) { return; } // missing rig fallback is still a data failure
	SetBodyFrozen(true);
	SetIgnoreCharacterCollision(true);
	CompleteDeathHandoff();
}

// The patrol route and the ambient spot, as words. Every decision they feed is in
// `RestorePatrolAndAmbient`; nothing here is port state retail has a row for.
void FElysiumNpc::SerializePatrolBlock(FElysiumSaveArchive& Ar)
{
	// The two patrol path objects (`m_sppPatrolPath` / `m_sppPatrolPathHunt`, retail's custom save
	// ops): whether the cell holds one, then the record's words. A loaded record takes a pool slot.
	for (FPatrolPathCell* Cell : { &PatrolPathCell, &PatrolPathHuntCell })
	{
		bool bHasPath = Cell->Path != nullptr;
		Ar << bHasPath;
		if (!bHasPath)
		{
			if (Ar.IsLoading())
			{
				ReleasePatrolPath(Cell);
			}
			continue;
		}
		FPatrolPathRecord Record = Cell->Path != nullptr ? *Cell->Path : FPatrolPathRecord();
		Ar << Record.Type;
		Ar << Record.Schedule;
		Ar << Record.Repeat;
		Ar << Record.Count;
		Ar << Record.Current;
		for (int32& Node : Record.Nodes)
		{
			Ar << Node;
		}
		if (Ar.IsLoading())
		{
			if (Cell->Path == nullptr)
			{
				Cell->Path = AllocPatrolPath();
				Cell->bOwned = Cell->Path != nullptr;
			}
			if (Cell->Path != nullptr)
			{
				*Cell->Path = Record;
			}
		}
	}
	// The visit's retail words: `+0x6304` mode, `+0x62ec` place, `+0x63d4` refresh stamp, `+0x62e8`
	// arrived (schema `NpcAmbientExecutorRetired`: the executor's two timers are gone).
	uint8 SavedAmbientPhase = static_cast<uint8>(AmbientPhase);
	Ar << SavedAmbientPhase;
	Ar << CurrentSpotIndex; // 0x102db5e0 restores current place from rows; last place +0x62fc has no SAVE row
	Ar.Time(AmbientNextActivityAt, EElysiumTimePolicy::MinusOne); // +0x63d4 TIME SAVE mode2, 0x102993c0/0x101a0a80
	Ar << bAmbientArrived;
	if (Ar.IsLoading())
	{
		AmbientPhase = static_cast<EAmbientPhase>(SavedAmbientPhase);
	}
}

// The maker relationship. `npc_maker` ownership is a runtime association with no retail datamap
// row on either side, so it rides the record; the handle is re-stamped by the hook.
void FElysiumNpc::SerializeMakerBlock(FElysiumSaveArchive& Ar)
{
	Ar << OwnerEntity;
	Ar << bOwnerTerminationNotified;
}

// The NPC's own tracked discipline effects, restored. A targeted `disciplinetgt` cast lands its
// trait-effect groups on whichever character it hit, and that is usually an NPC; without the pair
// of this and `Disciplines.Serialize` a Dominate group on a guard evaporates across a save while
// its owned expiry event rides the map snapshot's queue block and comes back looking for it.
//
// The owned expiry events themselves are not in the record -- they are queue records the snapshot's
// queue block already carries, serial and all. That is what makes the restore coherent: the event
// comes back pointing at the serial the record restores. An event whose serial no longer matches
// anything (a renewal minted a newer one before the save, or teardown ran) is dropped by
// `ElysiumDisciplines::CommitExpiry`'s own guard with a Verbose line, which is the guard working
// rather than a loss.
void FElysiumNpc::RestoreDisciplineState(FElysiumEntityWorld& InWorld)
{
	// Session state, never simulation state: a restored character starts from the live sound bus
	// rather than replaying a retention window that no longer exists.
	Disciplines.SoundCursor = 0;
	(void)InWorld; // 0x101a2e40 caster references were fixed before the native hook, not rederived here.

	// The tracked rows say which authored groups this NPC is carrying; `Effects` is the list they
	// were installed into, and an NPC's `Effects` is rebuilt at spawn from its `stattemplate` alone
	// (`SeedSheet`), so the discipline groups are missing from it after a restore. Re-append exactly
	// one copy per tracked row -- the same one-per-live-effect invariant `RemoveTargetEffect`
	// removes against, which is why this adds rather than `AddUnique`s: two records may legitimately
	// name the same group.
	bool bAdded = false;
	for (const FElysiumActiveDisciplineEffect& Effect : Disciplines.TargetEffects)
	{
		for (const FString& Group : Effect.Effects)
		{
			Effects.Add(Group);
			bAdded = true;
		}
	}
	if (bAdded)
	{
		// `RebuildEffects` re-derives the `health`/`max_health` pair off the sheet, and an NPC's
		// SHEET is not save state -- it is re-seeded from the stat template at spawn, so its damage
		// slot reads zero here while the field walk has already restored the real `health`
		// keyfield. Re-deriving would therefore hand a wounded NPC its whole track back, so the
		// restored pair is put back over the derived one. (The sheet/keyfield split on a restored
		// NPC is older than this hook and is not changed by it; this only refuses to make it worse.)
		const int32 RestoredHealth = Health;
		const int32 RestoredMaxHealth = MaxHealth;
		RebuildEffects();
		Health = RestoredHealth;
		MaxHealth = RestoredMaxHealth;
	}
	// A restored row keeps its `bRemoveOnHearCombat` listener and the think that services it keeps
	// its SAVED cadence: no clock is armed here. The listener's poll runs on the next saved think,
	// as it would have in retail.
}

void FElysiumNpc::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumCombatCharacter::GetDebugState(Out);
	Out.Emplace(TEXT("UseInteresting"), bUseInteresting ? TEXT("yes") : TEXT("no"));
	// An empty list is retail's ZERO mask, which matches no place at all — not "every group".
	Out.Emplace(TEXT("Interesting groups"), InterestingPlaceGroups.IsEmpty()
		? TEXT("(none)") : InterestingPlaceGroups);
	Out.Emplace(TEXT("In dialog"), Dialogue.bInDialog
		? FString::Printf(TEXT("YES (%s)"), ElysiumDialogueCamera::LexToString(Dialogue.DialogOpener))
		: TEXT("no"));
	Out.Emplace(TEXT("default_camera"), DefaultCamera.IsEmpty() ? TEXT("(none)") : DefaultCamera);
	Out.Emplace(TEXT("Times talked"), FString::FromInt(TimesTalked));
	const FElysiumEntityHandle Player = World ? World->PlayerHandle()
		: FElysiumEntityHandle::Invalid();
	Out.Emplace(TEXT("Disposition"), FString::Printf(TEXT("%s L%d%s"), *Disposition,
		DispositionLevel, IsDispositionTalking() ? TEXT(" talking") : TEXT("")));
	// The derived row is called out by its remaining seconds rather than by its presence: a hostility
	// that is about to run out and one that was just renewed read identically otherwise.
	const FElysiumDerivedRelationship* PlayerDamageMemory = Relationships.FindDerived(Player);
	Out.Emplace(TEXT("Relationship to player"), FString::Printf(
		TEXT("%s priority %d (table %d entity / %d class / %d derived)%s"),
		ElysiumRelationships::LexToString(Relationships.Resolve(Player, TEXT("player"))),
		Relationships.ResolvePriority(Player, TEXT("player")),
		Relationships.NumEntityRules(), Relationships.NumClassRules(),
		Relationships.NumDerivedRules(),
		PlayerDamageMemory == nullptr ? TEXT("")
			: *FString::Printf(TEXT(" damage memory %.1fs left"),
				PlayerDamageMemory->ExpiresAt - (World ? World->NowSeconds() : 0.0))));
	if (!StatTemplate.IsEmpty())
	{
		Out.Emplace(TEXT("Stat template"), StatTemplate);
	}
	Out.Emplace(TEXT("Fast food"), bFastFood ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
	Out.Emplace(TEXT("Body"), Visual ? TEXT("skeletal (standing)") : TEXT("(none)"));
	Out.Emplace(TEXT("Motor"), Motor ? TEXT("Unreal character + Detour crowd") : TEXT("(none)"));
	if (const FString LastMove = Motor ? Motor->DescribeLastMoveResult() : FString();
		!LastMove.IsEmpty())
	{
		Out.Emplace(TEXT("Last move result"), LastMove);
	}
	const TCHAR* Admission = Mind.Admission() == FElysiumNpcMind::EAdmission::Spawned
		? TEXT("spawned") : (Mind.Admission() == FElysiumNpcMind::EAdmission::Armed
			? TEXT("armed") : TEXT("admitted"));
	Out.Emplace(TEXT("Mind"), FString::Printf(TEXT("%s current=%s ideal=%s"), Admission,
		LexToString(Mind.State()), LexToString(Mind.IdealState())));
	// Retail's words where the arbiter's owner row stood (V3d): `m_hCine +0x5d74` and
	// `m_hDialogPartner +0xfe8`, each resolved live.
	const bool bCineLive = World != nullptr && ScriptOwner.IsSet() && World->Resolve(ScriptOwner) != nullptr;
	Out.Emplace(TEXT("Cine"), bCineLive ? World->DescribeHandle(ScriptOwner) : FString(TEXT("(none)")));
	const FElysiumEntityHandle& Partner = GetDialogPartner();
	const bool bPartnerLive = World != nullptr && Partner.IsSet() && World->Resolve(Partner) != nullptr;
	Out.Emplace(TEXT("Dialog partner"), bPartnerLive ? World->DescribeHandle(Partner) : FString(TEXT("(none)")));
	Out.Emplace(TEXT("Mind transition"), Mind.LastTransition().IsEmpty()
		? TEXT("(none)") : Mind.LastTransition());
	// An `aiscripted_schedule` keeps nothing on the NPC (`0x101a98c0`); the only order held here is
	// one the admission barrier deferred (V6's), waiting for this NPC's first think.
	Out.Emplace(TEXT("Scripted schedule"), ScriptedScheduleOrder.bPending
		? FString::Printf(TEXT("deferred mode %d (%s, %s) route %d goal %s"), ScriptedScheduleOrder.Mode,
			ElysiumAiScriptedSchedule::ModeName(ScriptedScheduleOrder.Mode),
			ScriptedScheduleOrder.bRun ? TEXT("run") : TEXT("walk"), ScriptedScheduleOrder.Route.Num(),
			World ? *World->DescribeHandle(ScriptedScheduleOrder.Goal) : TEXT("(no world)"))
		: TEXT("(none)"));
	Out.Emplace(TEXT("Patrol"), PatrolPathCell.Path != nullptr
		? FString::Printf(TEXT("type %d node %d/%d (id %d) repeat %d, schedule 0x%x"),
			PatrolPathCell.Path->Type, PatrolPathCell.Path->Current + 1, PatrolPathCell.Path->Count,
			PatrolCurrentNode(PatrolPathCell), PatrolPathCell.Path->Repeat, PatrolPathCell.Path->Schedule)
		: TEXT("(no path)"));
	Out.Emplace(TEXT("Ambient place"), CurrentSpotIndex == INDEX_NONE
		? TEXT("searching")
		: FString::Printf(TEXT("#%d phase=%d"), CurrentSpotIndex,
			static_cast<int32>(AmbientPhase)));
	// `m_scriptState +0x5d70` (V3c: the NPC's word; the scripted-move row went with the seam).
	Out.Emplace(TEXT("Script state"), FString::Printf(TEXT("%d"), GetScriptState()));

	// --- Sensory transaction ---
	Out.Emplace(TEXT("Perception"), FString::Printf(
		TEXT("npc_perception %d, vision %.0fcm, hearing %.2fx%s"), AuthoredPerception,
		Senses.Perception.VisionDistanceCm, Senses.Perception.HearingScalar,
		Senses.Perception.bUsedFallback ? TEXT(" (table fallback)") : TEXT("")));
	const FElysiumNpcMemory& Mem = Senses.Memory;
	Out.Emplace(TEXT("Closest player"), Mem.ClosestPlayer.IsSet()
		? FString::Printf(TEXT("%.0fcm, %s%s%s"), Mem.ClosestPlayerDistanceCm,
			Mem.bPlayerVisible ? TEXT("SEEN") : TEXT("unseen"),
			Mem.bPlayerInCone ? TEXT(", in cone") : TEXT(", out of cone"),
			Mem.bPlayerInOuterBand ? TEXT(", outer band") : TEXT(""))
		: TEXT("(none)"));
	Out.Emplace(TEXT("Enemy"), BaseMemory.Enemy.IsSet()
		? FString::Printf(TEXT("%s (%s, %d failed LOS checks%s)"), *BaseMemory.Enemy.ToString(),
			BaseMemory.EnemyOccludedCheck >= ElysiumNpcSense::EnemyLosFailureLimit ? TEXT("OCCLUDED")
				: TEXT("has LOS"), BaseMemory.EnemyOccludedCheck,
			EnemyMemory.IsEluded(BaseMemory.Enemy) ? TEXT(", ELUDED") : TEXT(""))
		: TEXT("(none)"));
	Out.Emplace(TEXT("Last enemy"), BaseMemory.LastEnemy.IsSet()
		? BaseMemory.LastEnemy.ToString() : FString(TEXT("(none)")));
	Out.Emplace(TEXT("Enemy sightings"), FString::FromInt(EnemySightings));
	Out.Emplace(TEXT("Last heard"), Mem.LastHeardTime < 0.0
		? TEXT("(nothing)")
		: FString::Printf(TEXT("%s at %s, t=%.2f"), *Mem.LastHeardCategory,
			*Mem.LastHeardPosition.ToString(), Mem.LastHeardTime));
	Out.Emplace(TEXT("Last damage"), !BaseMemory.LastDamageAttacker.IsSet()
		? TEXT("(none)")
		: FString::Printf(TEXT("from %s, m_flLastDamageTime t=%.2f (m_flSumDamage %.1f, packet %.1f)"),
			*BaseMemory.LastDamageAttacker.ToString(), BaseMemory.RepeatedDamageWindowStart,
			BaseMemory.RepeatedDamageAccumulated, LastTakeDamageInfo.Damage));

	// --- Decision pass ---
	Out.Emplace(TEXT("Conditions"), FString::Printf(TEXT("%s (gathered t=%.2f)"),
		*Cognition.Conditions.Describe(), Cognition.GatheredAt));
	Out.Emplace(TEXT("no_alert_state"), bNoAlertState ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Schedule"), Schedule.IsRunning()
		? FString::Printf(TEXT("%s (0x%x) task %d"), ElysiumScheduleName(Schedule.Current),
			IdSpace(EElysiumIdCategory::Schedule)->GlobalToLocal(Schedule.Current),
			Schedule.TaskIndex)
		: TEXT("(none)"));

	// --- Loadout and the combat policy it selects ---
	const FElysiumItem* Active = Inventory.Active(*this);
	const ElysiumNpcCond::ECapability Capability = ElysiumNpcCond::WeaponCapability(*this);
	Out.Emplace(TEXT("Weapon"), FString::Printf(TEXT("%s — capability %s (0x%x)"),
		Active != nullptr ? *Active->ClassName() : TEXT("(none)"),
		ElysiumNpcCond::CapabilityName(Capability),
		ElysiumNpcCond::CapabilityBits(Capability)));
	Out.Emplace(TEXT("Loadout"), FString::Printf(TEXT("%s authored '%s'%s%s"),
		bLoadoutResolved ? TEXT("resolved,") : TEXT("PENDING,"),
		AdditionalEquipment.IsEmpty() ? TEXT("(none)") : *AdditionalEquipment,
		AlternateEquipment.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", alternate '%s' (unread)"),
			*AlternateEquipment),
		bCantDropWeapons ? TEXT(", cantdropweapons") : TEXT("")));
	Out.Emplace(TEXT("Detected attack"), Mem.DetectedAttackTime < 0.0
		? TEXT("(none)")
		: FString::Printf(TEXT("%s at t=%.2f"), *Mem.DetectedAttackAttacker.ToString(),
			Mem.DetectedAttackTime));

	// --- Witness block ---
	const double LawNow = World ? World->NowSeconds() : 0.0;
	Out.Emplace(TEXT("Law thresholds"), FString::Printf(
		TEXT("crim flee %d / attack %d, super flee %d / attack %d, investigate %d"),
		PlCriminalFlee, PlCriminalAttack, PlSupernaturalFlee, PlSupernaturalAttack, PlInvestigate));
	for (int32 i = 0; i < 2; ++i)
	{
		const ElysiumNpcWitness::EChannel Channel = static_cast<ElysiumNpcWitness::EChannel>(i);
		const FElysiumNpcWitnessChannel& Chan = Witness.Channel(Channel);
		Out.Emplace(FString::Printf(TEXT("Law (%s)"), ElysiumNpcWitness::ChannelName(Channel)),
			FString::Printf(TEXT("processed %d, window %s, witnessed %d from %s"),
				Chan.Processed,
				ElysiumNpcWitness::IsWindowOpen(Chan.IgnoreUntil, LawNow) ? TEXT("OPEN") : TEXT("closed"),
				Chan.Level, Chan.Offender.IsSet() ? *Chan.Offender.ToString() : TEXT("(nobody)")));
	}
	Out.Emplace(TEXT("Law (nosferatu)"), FString::Printf(TEXT("window %s%s"),
		ElysiumNpcWitness::IsWindowOpen(Witness.NosferatuIgnoreUntil, LawNow)
			? TEXT("OPEN") : TEXT("closed"),
		Witness.bSupernaturalFleeOnly ? TEXT(", flee only") : TEXT("")));
}

void FElysiumNpc::RebaseSavedReferences(FElysiumEntityWorld& InWorld)
{
	FElysiumNpcBase::RebaseSavedReferences(InWorld); // 0x101a2e40 before OnRestore
	OwnerEntity = InWorld.RestoreHandle(OwnerEntity); // maker/cine owner, 0x102998c0
	NavHeadCorner = InWorld.RestoreHandle(NavHeadCorner); // wp+0x20, 0x102f2330
	Senses.Memory.Rebase(InWorld, BaseMemory); // 0x10310710 owned sound handles
	Witness.Rebase(InWorld); // 0x102998c0 owned witnesses
	for (FElysiumActiveDisciplineEffect& SavedEffect : Disciplines.TargetEffects)
		SavedEffect.Source = InWorld.RestoreHandle(SavedEffect.Source); // 0x101a2e40 retained caster before consumers
}

bool FElysiumNpc::ReadScriptPhysicalWords(int32& OutSolid, int32& OutMoveType, int32& OutMoveCollide,
	int32& OutSolidFlags, int32& OutEffects) const
{
	FElysiumEntity::ReadScriptPhysicalWords(OutSolid, OutMoveType, OutMoveCollide, OutSolidFlags, OutEffects); // 0x100a8710
	OutEffects = static_cast<int32>(EffectsWord); // the kernel's m_fEffects word (+0x19c)
	return true;
}

void FElysiumNpc::WriteScriptPhysicalWords(int32 InSolid, int32 InMoveType, int32 InMoveCollide,
	int32 InSolidFlags, int32 InEffects)
{
	// `ScriptHide` 0x100a8710 / `ScriptUnhide` 0x100a8990: the base's writes (`SetSolid` `FUN_100dc480`,
	// the two move words, `SetSolidFlags` `FUN_100dc580`, each with its change tail), then the kernel's
	// `m_fEffects` and the think restore the base has no word for.
	FElysiumEntity::WriteScriptPhysicalWords(InSolid, InMoveType, InMoveCollide, InSolidFlags, InEffects);
	EffectsWord = static_cast<uint32>(InEffects);
	RestoreThinkCallback(); // saved FUNCTION restored by base ScriptUnhide before this hook
}

FElysiumEntity* FElysiumNpc::RestoreOwnerTargetSource() const
{
	// 0x102f2543: navigator owner NPC's +0x98, slot586 GetBestSeeUnknown.
	// A resolved target is an unguarded native precondition; no follower is involved.
	return World != nullptr ? World->Resolve(const_cast<FElysiumNpc*>(this)->GetBestSeeUnknown()) : nullptr;

}
