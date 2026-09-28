// NPC presence and the first native-Unreal locomotion slice: the `npc_*` character leaf.
//
// The leaf here is the **dialogue half only**. Its place in VtMB's chain is CAI_BaseNPC under
// CBaseCombatCharacter under CBaseAnimating (`ElysiumPlayer.h`), and everything those two own
// — the sheet and its 25 inputs, the WillTalk latch, `default_disposition`, the skeletal body,
// playing a clip on it, following SetOrigin/SetModel, gating it on dormancy — arrives through the
// chain, shared with the player. `elysium.NpcBodies` is the one thing that stays here: it is this
// class's A/B, not the animating node's.

#include "Substrate/ElysiumNpc.h"
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
	// published surface IS that field — written per move (`CAI_Navigator::MoveEnact 0x102ef870`) and
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
	// A corpse is out of the world in retail; nothing reaches it to kill it twice.
	if (bDeathCommitted)
	{
		return;
	}
	FElysiumTakeDamageInfo Info;
	Info.Attacker = Attacker;
	Event_Killed(&Info);                                                      // slot 144
}

void FElysiumNpc::BecomeClientRagdoll()
{
	if (bDeathCommitted)
	{
		return;
	}
	bDeathCommitted = true;
	// Every animation-channel claim goes back ahead of the physics handoff: a claim outliving its
	// producer parks a channel. (The body-owner tokens and the mind are vacated at the end of the
	// Troika `Event_Killed`, after retail's own state change.)
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment != nullptr && Visual != nullptr && !bStealthDeathCommitted)
	{
		Embodiment->ReleaseBodyAnimClaims(Visual);
	}
	// `FSOLID_NOT_SOLID` / `MOVETYPE_NONE`: frozen rather than hidden — a corpse stays on screen.
	// `SetIgnoreCharacterCollision` is stated beside it because the two are separate switches with
	// separate lifetimes: whatever later un-freezes a body must not make a corpse block the player.
	SetBodyFrozen(true);
	SetIgnoreCharacterCollision(true);
	SetBodyHeld(false);
	SetBodyAnimationHeld(false);
	// The pose goes to physics (`ACT_DIERAGDOLL`'s seed in retail; this runtime's current pose).
	CompleteDeathHandoff();
	// The think stops: `BecomeClientRagdoll` leaves the entity no think function.
	ThinkSet(nullptr, 0.0);
	NextThink = ELYSIUM_NEVER_THINK;
}

void FElysiumNpc::InputUseInteresting(const FElysiumInputArgs& Args)
{
	bUseInteresting = Args.Param.ToInt() != 0;
	if (!bUseInteresting)
	{
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	}
	// No clock reset: the input is not a slot-614 site. The visit starts on the next cadence
	// think, as an install from outside a think does in retail.
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

void FElysiumNpc::InputSetRelationship(const FElysiumInputArgs& Args)
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
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment && Visual)
	{
		// The one activity seam: the gait is chosen through this body's whole translation chain, so a
		// class body's alert walk and a weapon's own gait reach a patrol leg exactly as they reach the
		// frame publish. A travel cycle always loops, whatever the selected row's own flag says.
		FElysiumActivityClipRequest Request;
		FillActivityClipRequest(Request);
		Request.Activity = bRunning ? TEXT("ACT_RUN") : TEXT("ACT_WALK");
		Request.Variant = FMath::Max(0, Handle.Index);
		Request.BodyKind = EElysiumAnimBodyKind::Cast;

		FElysiumActivityClip Clip;
		if (Embodiment->ResolveNpcActivityClip(Request, Clip)
			&& PlayAnimClip(Clip.Label, /*bLoop=*/true))
		{
			return true;
		}
	}
	// A few early manifests only carry the retail label. Keep them mobile while the animation
	// catalog remains strict for every model that does expose ACT_WALK.
	return PlayAnimClip(bRunning ? TEXT("run") : TEXT("walk"), /*bLoop=*/true);
}

bool FElysiumNpc::AcquireSequenceBody(const TCHAR* Reason)
{
	// A pushed scripted order is DROPPED rather than parked, for the same reason dialogue drops it:
	// the arbiter's one parked slot belongs to the patrol route, and `aiscripted_schedule` is a
	// one-shot push with no resume. A beat that takes this body ends whatever a director had asked
	// for, which is also the recovered precedence — a sequence claims the body outright.
	EndScriptedSchedule(Reason);
	// The combat claim leaves the same way, and for a load-bearing reason rather than tidiness: the
	// arbiter has ONE parked slot, and a schedule that took the body off a patrol route is already
	// holding the route in it. Parking the schedule on top would discard the route and strand the
	// mind owning a claim whose token this leaf has retired. Releasing first restores the route, and
	// the claim below then parks the thing that is actually resumable.
	ReleaseScheduleBody(Reason);
	return Mind.Acquire(EElysiumBodyOwner::Sequence, /*bSuspendCurrent=*/false,
		SequenceOwner, Reason);
}

void FElysiumNpc::ReleaseSequenceBody(const TCHAR* Reason)
{
	if (!SequenceOwner.IsSet())
	{
		return;
	}
	Mind.Release(SequenceOwner, Reason);
	SequenceOwner.Reset();
}

bool FElysiumNpc::ClaimScriptMove()
{
	FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	if (!AcquireSequenceBody(TEXT("BeginScriptMove")))
	{
		return false;
	}
	bMoveIssued = false;
	bWalkingAnimation = false;
	return true;
}

void FElysiumNpc::ReleaseScriptMove(const TCHAR* Reason)
{
	bMoveIssued = false;
	bWalkingAnimation = false;
	if (bScriptBodyHeld)
	{
		// The beat outlives its travel: `m_iszPlay` and the post-idle still play on this body,
		// so arrival hands the motor back without giving up the claim under them.
		return;
	}
	ReleaseSequenceBody(Reason);
}

bool FElysiumNpc::ClaimScriptBody(const TCHAR* Reason)
{
	bScriptBodyRequested = true;
	if (bScriptBodyHeld)
	{
		return true;
	}
	if (AmbientOwner.IsSet() || AmbientPhase != EAmbientPhase::None)
	{
		// An interesting-place visit is this NPC's own executor; the beat takes the body off it.
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	}
	bScriptBodyHeld = AcquireSequenceBody(Reason);
	// This leaf owns the arbiter, so it owns the one report of a claim it could not grant. The
	// two outcomes are not the same event: an unadmitted mind is an ordinary race this NPC
	// resolves itself on its next think, while an admitted mind that still refuses means another
	// owner holds a body a beat is entitled to.
	if (!bScriptBodyHeld && !Mind.IsAdmitted())
	{
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("%s defers a scripted beat's body claim until admission has run"),
			*DebugString());
	}
	else if (!bScriptBodyHeld)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s refused a scripted beat the body: %s owns it. The beat runs on its queue "
				 "lock and the claim is retried when the arbitration allows it"),
			*DebugString(), LexToString(Mind.Owner()));
	}
	return bScriptBodyHeld;
}

void FElysiumNpc::ReleaseScriptBody(const TCHAR* Reason)
{
	bScriptBodyRequested = false;
	if (!bScriptBodyHeld)
	{
		return;
	}
	bScriptBodyHeld = false;
	if (bScriptMoveClaimed)
	{
		// Cancelled mid-travel: the motor is still on the same token and EndScriptMove, which
		// the beat runs next, is what gives it back.
		return;
	}
	ReleaseSequenceBody(Reason);
}

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

void FElysiumNpc::Think()
{
	// The entity think IS slot 431 `NPCThink`: `CAI_BaseNPCTroika::NPCThink` (`0x10292de0`), or the
	// species override where the class has one (story 8 wave 2, L13). What stands around the call is
	// the port's own, each piece named:
	//
	//  - `IsInert`: an entity the world has not activated or has removed (port lifecycle).
	//  - The corpse: `CreateCorpse` (`BecomeClientRagdoll`) leaves the entity no think in retail and
	//    takes it out of the world; this runtime keeps it, so a think that still arrives (a feed
	//    pair's release re-arms one) re-freezes the body and turns the think off again.
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
	if (bDeathCommitted)
	{
		SetBodyFrozen(true);
		SetIgnoreCharacterCollision(true);
		NextThink = ELYSIUM_NEVER_THINK;
		return;
	}
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

bool FElysiumNpc::TickScriptWatchdog()
{
	if (ScriptPhase == EScriptPhase::None)
	{
		return false;
	}
	// The owning beat advances the move; this think only watches for a beat that stopped
	// doing so (killed or hidden mid-travel) and releases the body rather than freezing it.
	const double Now = World ? World->NowSeconds() : 0.0;
	if (Now < ScriptWatchdogAt)
	{
		// No deadline is pushed: a script-driven body is a `ShouldThinkFrequently` body, so the
		// cadence laws already hold it at their 0.01 s floor.
		return true;
	}
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s released an abandoned scripted move"),
		*DebugString());
	// Everything the beat holds leaves together: a beat that stopped advancing its own move will not
	// run its teardown either, and half a claim would leave this body suppressed and unowned for the
	// rest of the map. While the owning director still resolves, that teardown IS `CineCleanup`
	// (`0x1027d170`): the saved movetype/flags, the oblivious count and the squad come back with it.
	if (ResolveCine() != nullptr)
	{
		FElysiumScriptedSequence::CineCleanup(*this);
	}
	ReleaseScriptBody(TEXT("abandoned scripted move"));
	EndScriptMove();
	ScriptOwner = FElysiumEntityHandle::Invalid();
	// The beat's montage-slot run claim goes with the rest of what it held. Nothing else can give it
	// back — the beat that took it is the thing that stopped answering — and it has no duration, so a
	// claim left standing here would refuse the idle below at the ambient band and park this body's
	// base channel in its travel cycle for the rest of the map.
	ReleaseAnimSegment();
	ResetAnimToIdle();
	return true;
}

bool FElysiumNpc::RouteScheduleMaintenance(double Now, bool bReduced)
{
	// `MaintainSchedule` (`0x102817c0`) as `RunAI` (`0x1026f302`) reaches it on a Troika body. Retail
	// runs the schedule interpreter alone: a scripted beat is `SCHED_AISCRIPT`, a patrol is the patrol
	// path's program (story 8 wave 2: it is, here too), an interesting place is its program. This
	// runtime still drives three owners outside the interpreter, routed here ahead of it — each a
	// named survivor of the story-8 rewire, with the reason it stays:
	//
	// STORY8-TWIN (survivor): the scripted-beat owner (`TickScriptWatchdog`, `ThinkScriptOwned`) is
	// replaced by `SCHED_AISCRIPT` and the director's own schedule arms — spec 0003 stories 1-2 build
	// them; until then the beat drives the body and the interpreter must not fight it.
	// STORY8-TWIN (survivor): the dialogue clip hold (`ThinkInDialog`) is replaced by the dialogue
	// family's `m_hDialogPartner` path (RunAI's gather skip is already retail's, `0x1026f1f0`); the
	// per-line VCD body clip it protects has no retail schedule stand yet.
	// STORY8-TWIN (survivor): the interesting-place executor (`ThinkAutonomous`'s ambient arm, and
	// the hand-over in `ThinkSchedulePolicy` that also serves the pushed `aiscripted_schedule` order)
	// is replaced by the interesting-place programs (`SelectSchedule` case 1's `0xff` walk-to-place
	// setup and its `FIND/GET_PATH_TO_INTERESTING_PLACE` arms, ported). It stays because the visit's
	// claim and its into/dwell/out activity phases are the executor's own state, which the retail
	// arms read (`ClaimAmbientSpot`, `CurrentSpotIndex`) but do not yet drive end to end.
	if (TickScriptWatchdog())
	{
		return Schedule.IsRunning();
	}
	if (ThinkInDialog(Now, bReduced) || ThinkScriptOwned(Now) || ThinkSchedulePolicy(Now, bReduced))
	{
		return Schedule.IsRunning();
	}
	ThinkAutonomous(Now, bReduced);
	return Schedule.IsRunning();
}

bool FElysiumNpc::ThinkInDialog(double Now, bool bReduced)
{
	if (!Dialogue.bInDialog)
	{
		return false;
	}
	// A per-line VCD owns the body while its sequence/gesture event is live. The ordinary
	// dialogue stance think must not replace that one-shot with a disposition idle. It needs no
	// cadence of its own: an NPC in dialogue answers `ShouldThinkFrequently` (`0x102c2430`), which
	// pins the normal law to 0.01 s and the update law to 0.03 s.
	if (World && World->HasActiveDialogueBodyClip(Handle))
	{
		return true;
	}
	// A character in conversation still runs its stance machine -- retail's Talking
	// threshold/chance pair exists precisely for this case. The selector settles it onto its
	// current idle rather than fidgeting through a line, so the reschedule is what keeps it
	// posed rather than what makes it move.
	ThinkStanceOrIdle(Now, bReduced);
	return true;
}

bool FElysiumNpc::ThinkScriptOwned(double Now)
{
	if (!ScriptOwner.IsSet())
	{
		return false;
	}
	// A scripted owner — a `scripted_sequence` beat or a choreographed scene's cast — is
	// driving this body's pose. Script ownership suppresses the ordinary condition-gathering
	// path (`docs/vtmb/npc-ai/README.md`), so nothing after this phase may select a
	// schedule whose idle would replace the clip the owner put on the body.
	if (bScriptBodyRequested && !bScriptBodyHeld
		&& Mind.CanAcquire(EElysiumBodyOwner::Sequence))
	{
		// The beat asked before this NPC's admission think had run. Admission has happened
		// by now, so the claim it is entitled to lands here.
		bScriptBodyHeld = AcquireSequenceBody(TEXT("scripted beat claim after admission"));
	}
	// A script-driven body is a `ShouldThinkFrequently` body, so the laws already hold it at their
	// floor. Nothing to write here.
	return true;
}

bool FElysiumNpc::ThinkSchedulePolicy(double Now, bool bReduced)
{
	// Schedule selection pre-empts an autonomous executor.
	// A committed enemy or an authored director outranks this NPC's own patrol route and
	// interesting-place visit: without this routing a patrolling guard that acquired an enemy
	// would keep walking its route and never reach combat selection.
	//
	// The hand-over lives in the ROUTING and not in either executor: the arbiter already carries
	// both shapes (patrol suspends and resumes, ambient owns a claimed place that has to be given
	// back), and the combat programs are the registered ones.
	const bool bScriptedPolicy = ScriptedScheduleOrder.IsSet() || ScriptedScheduleOwner.IsSet();
	// A program that is ALREADY RUNNING pre-empts the executors too. Retail has no split to
	// bridge here -- patrol is itself a schedule (`SelectSchedule` case 1 returns the patrol path's
	// program at `+0x6590+4`), so a forced `SetSchedule` from a script's `ChangeSchedule`, a
	// discipline's `AI_Schedule` or the feed's mesmerize install simply replaces it and
	// `MaintainSchedule` runs the new program on the next think. This runtime keeps patrol and the
	// ambient visit as executors outside the kernel, and only `ThinkStanceOrIdle` ticks a program:
	// without this term a program installed by name on a patrolling NPC sat at task 0 forever while
	// the route kept walking. A running program is the policy; which think installed it is not.
	const bool bForcedProgram = Schedule.IsRunning();
	if (!bScriptedPolicy && !bForcedProgram && Mind.State() != EElysiumNpcState::Combat)
	{
		return false;
	}
	if (AmbientOwner.IsSet() || AmbientPhase != EAmbientPhase::None)
	{
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	}
	ThinkStanceOrIdle(Now, bReduced);
	return true;
}

void FElysiumNpc::ThinkAutonomous(double Now, bool bReduced)
{
	if (bUseInteresting)
	{
		ThinkAmbient(Now);
	}
	else
	{
		ThinkStanceOrIdle(Now, bReduced);
	}
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
	// An authored director outranks the state change it may itself have caused. `forcestate 3` puts
	// an NPC in combat with no enemy, whose recovered fallback is a drop back to alert on the very
	// next pass — and discarding the program here would cancel the walk the same director pushed
	// half a think earlier. The pushed program ends where every other program ends: on its own
	// completion or failure, in `ThinkStanceOrIdle`.
	if (ScriptedScheduleOwner.IsSet() || ScriptedScheduleOrder.IsSet())
	{
		return;
	}
	// A state change reselects. The running program was chosen by the state that has just been left
	// — an idle stance under an NPC that just acquired an enemy — so it ends here rather than
	// finishing on behalf of a state that no longer holds.
	ClearSchedule();
	// The discarded program's movement claim goes with it. Releasing after `RequestState` is
	// deliberate: the arbiter's own state refresh runs on the release, and it must see the state
	// this pass decided rather than the one the program was chosen under.
	ReleaseScheduleBody(TEXT("ideal state changed"));
}

void FElysiumNpc::ThinkStanceOrIdle(double Now, bool bReduced)
{
	if (MaintainScheduleRetail(Now, bReduced))
	{
		return;
	}

	// The program ended — completed, interrupted, or cleared by `ClearSchedule`; a failure never
	// ends one, it routes into the next program on the following pass. Whatever movement it claimed
	// goes back BEFORE the next selection runs: an idle program picked while this NPC still held the
	// body would decline its own first task against itself.
	ReleaseScheduleBody(TEXT("schedule ended"));

	// A pushed scripted order lives exactly as long as the program it started. Arrival, a refused
	// route and a failed leg all land here, and this is what returns the NPC to ordinary selection
	// and hands a suspended patrol route back. The forced state deliberately does NOT come back with
	// it: `forcestate` was a state push, not a hold.
	if (ScriptedScheduleOrder.IsSet() || ScriptedScheduleOwner.IsSet())
	{
		EndScriptedSchedule(TEXT("scripted schedule ended"));
		// Hand back to `Think`'s routing rather than selecting from inside the branch the director
		// sent this NPC down: a suspended patrol route has just been restored, and resuming it is
		// that executor's turn, not schedule selection's. It resumes on the next normal think.
		return;
	}

	// `MaintainSchedule` already selected, installed and ran the replacement inside its one loop.
	// A false answer here is the retail missing-schedule exit or a selector that deliberately
	// returned none; the ordinary cadence asks again.
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
	if (!World || !Def || !ElysiumInterestingPlaces::Types())
	{
		return nullptr;
	}

	// PickRandomInterestingPlace admits nodes within 10,000 Source units, but distance does not
	// rank them. It walks rating 5 -> 0, stops at the first populated tier, and chooses uniformly
	// within that tier. The NPC schedule stream makes the choice replayable across save/load.
	constexpr double FindRadiusCm = 10000.0 * ElysiumMove::U;
	constexpr double FindRadiusSqCm = FindRadiusCm * FindRadiusCm;
	TArray<FElysiumInterestingPlace*> Candidates;
	TArray<int32> CandidateRatings;
	for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
	{
		if (!Candidate || !Candidate->Def
			|| !Candidate->Def->Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		FElysiumInterestingPlace* Spot = static_cast<FElysiumInterestingPlace*>(Candidate.Get());
		const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
		if (!Spot->IsAvailable() || FailedSpotIndices.Contains(Spot->Handle.Index)
			|| !AcceptsAmbientGroup(Spot->GroupMask)
			|| !TypeRow || TypeRow->Activities.IsEmpty()
			|| !TypeRow->Accepts(Def->Classname, StatTemplate))
		{
			continue;
		}
		if (FVector::DistSquared(Origin, Spot->Origin) > FindRadiusSqCm)
		{
			continue;
		}
		Candidates.Add(Spot);
		CandidateRatings.Add(Spot->Rating);
	}

	const int32 PickedIndex = ElysiumInterestingPlaces::PickHighestRatedCandidate(
		CandidateRatings, ElysiumRng::Stream(EElysiumRngStream::NpcSchedule));
	FElysiumInterestingPlace* Picked = Candidates.IsValidIndex(PickedIndex)
		? Candidates[PickedIndex] : nullptr;
	if (Picked && Picked->Claim(Handle))
	{
		if (!Mind.Acquire(EElysiumBodyOwner::Ambient, /*bSuspendCurrent=*/false,
			AmbientOwner, TEXT("interesting-place claim")))
		{
			Picked->Release(Handle);
			return nullptr;
		}
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

bool FElysiumNpc::PlayAmbientActivity(const TArray<FElysiumWeightedName>& Choices, bool bLoop,
	double Now, double& OutEnd)
{
	FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
	const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!TypeRow || !Embodiment || !Visual)
	{
		return false;
	}
	const uint32 Seed = HashCombineFast(static_cast<uint32>(FMath::Max(0, Handle.Index)),
		static_cast<uint32>(AmbientActivityCycle++));
	const FString Activity = TypeRow->PickActivity(Choices, Seed);
	if (Activity.IsEmpty())
	{
		return false;
	}
	FElysiumActivityClipRequest Request;
	FillActivityClipRequest(Request);
	Request.Activity = Activity;
	Request.Variant = AmbientActivityCycle;
	Request.BodyKind = EElysiumAnimBodyKind::Cast;

	FElysiumActivityClip Clip;
	if (!Embodiment->ResolveNpcActivityClip(Request, Clip))
	{
		return false;
	}
	// The CLIP decides whether it loops, not the caller. VtMB reads `m_bSequenceLoops` off the
	// sequence's own flags (RE35), so an authored loop keeps looping however it was asked for — and
	// the ambient callers ask for every activity with bLoop false. Without this a body-language idle
	// authored as a loop plays once and then stands on its last frame, which is what a held one-shot
	// means: frozen, not resting.
	//
	// **One segment of the spot's montage-slot run** — enter, hold, leave — and the same mechanism a
	// `scripted_sequence`'s idle/play/post-idle takes, differing only in band. It stays `Ambient`
	// deliberately: an ambient stance holds against a standing body's every-tick publish and yields
	// the moment the body travels, which is the one recovered relationship in the priority table. The
	// claim is HELD so the gap between two of the spot's segments is not a frame the channel goes
	// back; `FinishAmbientUse` is the one place it is given back.
	FElysiumClipSegment Segment;
	Segment.ClipName = Clip.Label;
	Segment.bLoop = bLoop || Clip.bLooping;
	Segment.Source = EElysiumAnimSource::Npc;
	Segment.Priority = EElysiumAnimPriority::Ambient;
	Segment.bHoldUntilReleased = true;
	float Seconds = 0.0f;
	if (!PlayAnimSegment(Segment, &Seconds))
	{
		return false;
	}
	OutEnd = Now + FMath::Max(0.25f, Seconds);
	return true;
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
	FElysiumDisposition OldRow;
	FElysiumDisposition NewRow;
	bool bChanged = false;
	if (!CommitDisposition(NewDisposition, NewLevel, bChanged, &OldRow, &NewRow))
	{
		return false;
	}
	if (!bChanged)
	{
		return true;
	}

	StanceResolvedFor.Reset();
	bool bPlayedTransition = false;
	const EElysiumBodyOwner Owner = Mind.Owner();
	if (Visual && !FElysiumCombatCharacter::IsFeedBusy()
		&& (Owner == EElysiumBodyOwner::None || Owner == EElysiumBodyOwner::Dialogue))
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (Embodiment)
		{
			const FString OldAnim = !OldRow.AnimName.IsEmpty() ? OldRow.AnimName : OldRow.Name;
			const FString NewAnim = !NewRow.AnimName.IsEmpty() ? NewRow.AnimName : NewRow.Name;
			const int32 StanceNumber = FMath::Clamp(
				Stance.Current, 0, ElysiumStance::Count - 1) + 1;
			auto TryTransition = [&](int32 Number)
			{
				if (OldAnim.IsEmpty() || NewAnim.IsEmpty())
				{
					return false;
				}
				const FString Clip = FString::Printf(TEXT("Stance_Trans_%s_%d_%s_%d"),
					*OldAnim, Number, *NewAnim, Number);
				// This cross-disposition transition is authored per model, and most bodies carry
				// none: probe the vocabulary first, so an absent clip is a quiet negative query
				// result rather than PlayNpcClip's logged miss.
				if (!Embodiment->HasNpcClip(ModelStem(), Clip))
				{
					UE_LOG(LogElysiumNpcEnt, Verbose,
						TEXT("%s disposition transition '%s' not authored; not taken"),
						*DebugString(), *Clip);
					return false;
				}
				float Seconds = 0.f;
				if (!Embodiment->PlayNpcClip(Visual, ModelStem(),
					FElysiumClipSegment(Clip, /*bLoop=*/false), &Seconds))
				{
					return false;
				}
				UE_LOG(LogElysiumNpcEnt, Verbose,
					TEXT("%s disposition transition '%s' taken"), *DebugString(), *Clip);
				Mind.RecordExternal(FString::Printf(TEXT("disposition %s L%d -> %s L%d via %s"),
					*OldRow.Name, OldRow.Level, *NewRow.Name, NewRow.Level, *Clip));
				return true;
			};
			bPlayedTransition = TryTransition(StanceNumber)
				|| (StanceNumber != 1 && TryTransition(1));
		}
	}
	if (!bPlayedTransition)
	{
		ResetAnimToIdle();
		// No clock reset: a disposition change is not a slot-614 site; the next stance selection
		// happens on the cadence.
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
	ScheduleIdealActivity = FElysiumClipIdentity();
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr)
	{
		return -1.f;
	}
	FElysiumActivityClipRequest Request;
	FillActivityClipRequest(Request);
	Request.Activity = Activity;
	Request.Variant = ScheduleActivityCycle++;
	Request.BodyKind = EElysiumAnimBodyKind::Cast;

	FElysiumActivityClip Clip;
	float Seconds = 0.f;
	// The task asks for a one-shot; the selected row's own loop bit still wins. Completion is the
	// base-channel phase identity reaching the resolved ideal, with the kernel's retail watchdog;
	// this clip length remains the body's presentation result only.
	if (!Embodiment->ResolveNpcActivityClip(Request, Clip))
	{
		return -1.f;
	}
	// `FElysiumActivityClip::OwnerStem` and Label are the resolver's canonical bank identity -- the
	// exact pair the clip player later publishes in the base-channel phase. Do not keep the requested
	// ACT_* name here: several activities can resolve to one sequence, while equal labels in different
	// banks are different sequences.
	ScheduleIdealActivity = FElysiumClipIdentity(Clip.OwnerStem, Clip.Label);
	if (!PlayAnimClip(Clip.Label, Clip.bLooping, &Seconds))
	{
		return -1.f;
	}
	return Seconds;
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
	ElysiumNpcDebugLogging::ScheduleInstalled(*this, InstalledSchedule);
}

// --- Combat task bodies ---

bool FElysiumNpc::AcquireProgramBody(EElysiumBodyOwner Owner, FElysiumBodyOwnerToken& Token,
	const TCHAR* Reason)
{
	if (Token.IsSet() && Mind.Owner() == Owner && Mind.Generation() == Token.Generation)
	{
		return true;
	}
	// A token from a claim that was displaced (a scripted beat took the body mid-chase) is retired
	// here rather than carried: the arbiter would refuse a release against it anyway.
	Token.Reset();
	// Nothing is parked: an interesting-place visit owns a claimed place, so `Think`'s hand-over
	// finishes it first, and the patrol claims no body since it became the path object's program.
	return Mind.Acquire(Owner, /*bSuspendCurrent=*/false, Token, Reason);
}

void FElysiumNpc::ReleaseProgramBody(EElysiumBodyOwner Owner, FElysiumBodyOwnerToken& Token,
	const TCHAR* Reason)
{
	if (!Token.IsSet())
	{
		return;
	}
	const bool bLive = Mind.Owner() == Owner && Mind.Generation() == Token.Generation;
	if (bLive)
	{
		// Whatever the program had the body doing stops with the claim. A schedule that ended
		// mid-path must not leave an outstanding move running under whatever selects next.
		const FElysiumNpcNavigationSample Nav = Motor ? Motor->SampleNavigation() : FElysiumNpcNavigationSample();
		if (Motor && !NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH)
			&& Nav.Type != EElysiumNpcNavType::Jump && Nav.Type != EElysiumNpcNavType::Climb)
		{
			Motor->Stop();
		}
		bMoveIssued = false;
		bWalkingAnimation = false;
		Mind.Release(Token, Reason);
	}
	Token.Reset();
}

bool FElysiumNpc::AcquireScheduleBody(const TCHAR* Reason)
{
	return AcquireProgramBody(EElysiumBodyOwner::Schedule, ScheduleOwner, Reason);
}

void FElysiumNpc::ReleaseScheduleBody(const TCHAR* Reason)
{
	ReleaseProgramBody(EElysiumBodyOwner::Schedule, ScheduleOwner, Reason);
}

bool FElysiumNpc::AcquireScriptedScheduleBody(const TCHAR* Reason)
{
	return AcquireProgramBody(EElysiumBodyOwner::ScriptedSchedule, ScriptedScheduleOwner, Reason);
}

void FElysiumNpc::ReleaseScriptedScheduleBody(const TCHAR* Reason)
{
	ReleaseProgramBody(EElysiumBodyOwner::ScriptedSchedule, ScriptedScheduleOwner, Reason);
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
		// has run, the same shape a scripted beat's deferred body claim already takes.
		ScriptedScheduleOrder = Order;
		ScriptedScheduleOrder.bHasForcedState = bHasForcedState;
		ScriptedScheduleOrder.ForcedState = ForcedState;
		ScriptedScheduleOrder.bPending = true;
		return true;
	}
	// No clock reset anywhere in this function. `CCineAISchedule::vfunc586` (`0x101a98c0`) goes
	// through `SetIdealState`, `ScheduledMoveToGoalEntity` / `ScheduledFollowPath` and
	// `SetSchedule` (`0x10280e50`), none of which writes a think stamp: an `aiscripted_schedule`
	// takes effect on the NPC's next cadence think, up to the normal law's out-of-PVS ceiling.

	// The policy, first and unconditionally. It is a STATE push and not a hold: nothing here parks
	// the state to restore later, because the recovered entity has no end and no release — it fires
	// once and the NPC carries what it was given.
	if (bHasForcedState)
	{
		Mind.RequestState(ForcedState, TEXT("aiscripted_schedule forcestate"));
	}

	if (static_cast<EMode>(Order.Mode) == EMode::AssignEnemy)
	{
		// Mode 3, recovered: "assigns the goal entity as enemy, copies its target position, and
		// injects native condition 0x54". The assignment goes through the ordinary `SetEnemy`
		// transaction rather than writing the handle, so the last-enemy transfer and everything else
		// `SetEnemy` (`0x10279a50`) does all happen exactly once and in one place.
		FElysiumEntity* Goal = World ? World->Resolve(Order.Goal) : nullptr;
		ElysiumNpcEnemy::SetEnemy(*this, Order.Goal);
		if (Goal != nullptr)
		{
			UpdateEnemyMemory(Goal, Goal->Origin, nullptr); // 0x101a99d0, slot 544
		}
		// `(*DAT_10924a6c)->vfunc1()` (`0x101a9a86`): a global object's slot 1, no argument, answer
		// discarded -- the same unrecovered call `Event_Killed` `0x10265d18` names; nothing answers.
		// The injected condition, by its recovered number: `NEW_ENEMY` is 0x54.
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

	// An interesting-place visit gives its claimed place back before the director takes the body —
	// ambient owns a place rather than a resumable route, so parking it would strand the claim.
	if (AmbientOwner.IsSet() || AmbientPhase != EAmbientPhase::None)
	{
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	}
	// An ordinary combat claim gives way to the director. Releasing before the new claim keeps the
	// arbiter's parked-owner slot holding the PATROL route rather than the schedule that displaced it.
	ClearSchedule();
	ReleaseScheduleBody(TEXT("aiscripted_schedule took the body"));

	ScriptedScheduleOrder = Order;
	ScriptedScheduleOrder.bPending = false;   // this IS the replay; nothing is waiting any more
	if (!AcquireScriptedScheduleBody(TEXT("aiscripted_schedule")))
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s refused a scripted schedule the body: %s owns it"),
			*DebugString(), LexToString(Mind.Owner()));
		ScriptedScheduleOrder.Reset();
		return false;
	}
	if (ElysiumAiScriptedSchedule::IsMoveToGoal(Order.Mode))
	{
		// Modes 1/2 are retail's own mover (`0x101a9960..0x101a99cc`): the activity is
		// `(-(mode != 1) & 10) + 9` -- ACT_WALK 9 for mode 1, ACT_RUN 0x13 for mode 2 -- replaced by
		// ACT_FLY 0x22 when slot 94 `GetMoveType` (+0x178, asked twice) answers 5 or 6; then
		// `ScheduledMoveToGoalEntity(npc, 2, goal, activity)` (`0x102800c0`): program 2 (IDLE_WALK,
		// translated by slot 440), `m_pGoalEnt`, and a type-4 `SetGoal` at 128 units.
		int32 Activity = static_cast<EMode>(Order.Mode) == EMode::MoveToGoalA ? 9 : 0x13;   // 0x101a9960 / 0x101a9963
		if (GetMoveType() == 5 || GetMoveType() == 6)                                        // 0x101a996a / 0x101a9979
		{
			Activity = 0x22;
		}
		FElysiumEntity* const Goal = World != nullptr ? World->Resolve(Order.Goal) : nullptr;
		const bool bGoalSet = ScheduledMoveToGoalEntity(ElysiumSched::IDLE_WALK, Goal, Activity);  // 0x101a998f
		ScriptedScheduleOrder.Program = Schedule.Current;
		if (!bGoalSet && !Order.bSuppressRouteWarning)                                        // 0x101a9996 / 0x101a99a5 spawnflags & 0x800
		{
			// `DevMsg(1, ...)` in retail; a Warning here, as the goal-miss line is, because it fires on
			// shipped content whose route this runtime's mover refuses.
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("ScheduledMoveToGoalEntity to goal entity %s failed\nCan't execute script %s"),
				Goal != nullptr ? *Goal->TargetName : TEXT(""),
				World != nullptr ? *World->DescribeHandle(Order.Source) : TEXT("(no world)"));   // 0x101a99c5 0x10595230
		}
		RecordScheduleEvent(FString::Printf(TEXT("aiscripted_schedule mode %d (%s, activity 0x%x) goal %s"),
			Order.Mode, ElysiumAiScriptedSchedule::ModeName(Order.Mode), Activity,
			World ? *World->DescribeHandle(Order.Goal) : TEXT("(no world)")));
		return bGoalSet;
	}

	BaseScheduleHost.IdealScheduleRetail = ResolveScheduleId(Program); // 0x10280de0, before slot 440
	if (!ElysiumSchedule::Start(Schedule, TranslateSchedule(Program), *this))
	{
		EndScriptedSchedule(TEXT("scripted program would not start"));
		return false;
	}
	ScriptedScheduleOrder.Program = Schedule.Current;
	BaseScheduleHost.GoalEnt = Order.Goal; // 0x102800c0 / 0x102801e0, after the schedule install
	// The director builds the goal immediately. The text only selects a movement activity and
	// waits; neither a synthetic GET_PATH_TO_GOAL nor a SET_SCHEDULE loop belongs to it.
	if (!GetPathToScriptedGoal())
	{
		// Retail's own failure line for the follow-path modes (`0x101a9a42`, spawnflag 0x800 silences it).
		if (ElysiumAiScriptedSchedule::IsFollowPath(Order.Mode) && !Order.bSuppressRouteWarning)
		{
			FElysiumEntity* const Goal = World != nullptr ? World->Resolve(Order.Goal) : nullptr;
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("ScheduledFollowPath to goal entity %s failed\nCan't execute script %s"),
				Goal != nullptr ? *Goal->TargetName : TEXT(""),
				World != nullptr ? *World->DescribeHandle(Order.Source) : TEXT("(no world)"));   // 0x105951d8
		}
		TaskFail(0x0c);
		return false;
	}
	RecordScheduleEvent(FString::Printf(TEXT("aiscripted_schedule mode %d (%s, %s) goal %s"),
		Order.Mode, ElysiumAiScriptedSchedule::ModeName(Order.Mode),
		Order.bRun ? TEXT("run") : TEXT("walk"),
		World ? *World->DescribeHandle(Order.Goal) : TEXT("(no world)")));
	return true;
}

void FElysiumNpc::EndScriptedSchedule(const TCHAR* Reason)
{
	// `bPending` is tested beside the other two because a state-only push carries mode 0, which is
	// not `IsSet()` — a deferred one still has to be droppable by death, dormancy and a beat.
	if (!ScriptedScheduleOrder.IsSet() && !ScriptedScheduleOrder.bPending
		&& !ScriptedScheduleOwner.IsSet())
	{
		return;
	}
	const int32 PushedProgram = ScriptedScheduleOrder.Program;
	ScriptedScheduleOrder.Reset();
	if (PushedProgram != 0 && Schedule.Current == PushedProgram)
	{
		// The program and the order are one thing. A scripted program left running with no order
		// behind it would fail its next leg by name for a reason no reader could act on.
		ClearSchedule();
	}
	ReleaseScriptedScheduleBody(Reason);
}

// STORY8-TWIN (survivor, modes 4/5 only): retail's `CCineAISchedule::vfunc586` (`0x101a98c0`) hands
// modes 4/5 to `ScheduledFollowPath` (`0x102801e0`), whose type-3 `SetGoal` makes the navigator walk
// the `path_corner` chain itself. This runtime's navigator builds no path-corner goal, so the order
// executor below walks the chain leg by leg instead, and writes `m_flGoalTolerance` (+0x6320) where
// retail's SetGoal writes the path's +0x28. Modes 1/2 run retail's `ScheduledMoveToGoalEntity`
// (`BeginScriptedSchedule`) and mode 3 retail's enemy arm; neither reaches this executor.
bool FElysiumNpc::GetPathToScriptedGoal()
{
	if (!ScriptedScheduleOrder.IsSet())
	{
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_GOAL refused: no scripted order is in force"));
		return false;
	}
	if (!ScriptedScheduleOrder.Route.IsValidIndex(ScriptedScheduleOrder.Leg))
	{
		// The route ran out, which is how a follow-path program ends: the transfer back to itself
		// re-enters here, this fails, and the NPC returns to ordinary selection. It is not an error.
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_GOAL: the scripted route is complete"));
		return false;
	}
	if (Motor == nullptr)
	{
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_GOAL refused: this NPC has no motor"));
		return false;
	}
	if (!AcquireScriptedScheduleBody(TEXT("TASK_GET_PATH_TO_GOAL")))
	{
		Mind.RecordExternal(FString::Printf(TEXT("TASK_GET_PATH_TO_GOAL refused: %s owns the body"),
			LexToString(Mind.Owner())));
		return false;
	}
	FVector Destination = ScriptedScheduleOrder.Route[ScriptedScheduleOrder.Leg++];
	const EElysiumNpcGaitKind RouteGait = ScriptedScheduleOrder.bRun
		? EElysiumNpcGaitKind::Run : EElysiumNpcGaitKind::Walk;
	// 0x102800c0 authors 128; 0x102801e0 authors -1 (keep the current tolerance, or hull
	// width if unset). Slot 563 gets the goal and tolerance before the navigator receives them.
	float ToleranceCm = 128.f * ElysiumMove::U;
	if (ElysiumAiScriptedSchedule::IsFollowPath(ScriptedScheduleOrder.Mode))
	{
		FVector Mins, Maxs;
		RetailHullExtents(HullKind, EElysiumHullExtents::Full, Mins, Maxs);
		ToleranceCm = ScheduleHost.GoalToleranceCm > 0.f ? ScheduleHost.GoalToleranceCm
			: static_cast<float>(Maxs.Y - Mins.Y) * ElysiumMove::U;
	}
	float UnusedTolerance = ToleranceCm;
	TranslateEnemyChasePosition(World ? World->Resolve(ScriptedScheduleOrder.Goal) : nullptr,
		Destination, &ToleranceCm, &UnusedTolerance);
	ScheduleHost.GoalToleranceCm = ToleranceCm;
	ScheduleHost.NavigationActivity = ScriptedScheduleOrder.bRun ? 0x13 : 9;
	if (GetMoveType() == 5 || GetMoveType() == 6)
	{
		ScheduleHost.NavigationActivity = 0x22; // authored ACT_FLY; 0018/12 owns the flying mover
		RecordScheduleEvent(TEXT("aiscripted_schedule ACT_FLY: the flying mover is not built"));
		return false;
	}
	MoveGoal = Destination;
	bMoveIssued = Motor->MoveTo(Destination, ToleranceCm,
		ElysiumNpcGait::TravelSpeed(Motor, RouteGait), /*bAllowPartialPath=*/false, RouteGait);
	if (!bMoveIssued)
	{
		// The recovered route-failure report, and the recovered switch that silences it: "spawn flag
		// 0x800 suppresses the route-failure warning". Latched per pushed order either way — a body
		// that could not take this leg will not take the next one.
		if (!ScriptedScheduleOrder.bSuppressRouteWarning && !ScriptedScheduleOrder.bWarnedRoute)
		{
			ScriptedScheduleOrder.bWarnedRoute = true;
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s could not take the route an aiscripted_schedule pushed (goal %s): the "
					 "program fails and the NPC returns to ordinary selection"),
				*DebugString(),
				World ? *World->DescribeHandle(ScriptedScheduleOrder.Goal) : TEXT("(no world)"));
		}
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_GOAL refused: the body would not take the route"));
		return false;
	}
	bWalkingAnimation = StartWalkingAnimation(ScriptedScheduleOrder.bRun);
	return true;
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
	ReleaseScheduleBody(*Surface);
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
	if (CurrentAmbientSpot()) FinishAmbientUse(bAmbientArrived, false);
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
	// Patrol, ambient use and pushed `aiscripted_schedule` orders are the three retail schedule
	// families this runtime currently represents as executors outside the program registry. Their
	// old handoff lived after Tick returned false; latch it at retail's actual completion edge so
	// the single MaintainSchedule loop does not replace them with an idle program first.
	bReturnToExternalExecutorAfterSchedule = bUseInteresting
		|| ScriptedScheduleOrder.IsSet() || ScriptedScheduleOwner.IsSet();
}

void FElysiumNpc::ClearScheduleHint(float ReuseDelay)
{
	// 0x10295ab0: a missing hint performs no writes, and another owner's hint is
	// forgotten locally without imposing our cooldown on that owner.
	if (BaseScheduleHost.HintNode == INDEX_NONE) return;
	if (BaseScheduleHost.bOwnsHint)
	{
		BaseScheduleHost.bOwnsHint = false;
		BaseScheduleHost.HintReusableAt = (World ? World->NowSeconds() : 0.0) + ReuseDelay;
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
		Schedule.TaskStatus = EElysiumTaskStatus::Complete;
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
	if (!bMoveIssued)
	{
		return false;
	}
	OutPoint = MoveGoal;
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
	// `CacheInterruptConditions` (`0x1026a0f0`) adds this one unconditionally after the virtual.
	InOutMask.Set(EElysiumNpcCond::NpcFreeze);
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

void FElysiumNpc::BeginAmbientUse(FElysiumInterestingPlace& Spot, double Now)
{
	bAmbientArrived = true;
	bMoveIssued = false;
	bWalkingAnimation = false;
	if (Motor)
	{
		Motor->Stop();
		if (Spot.bMatchOrientation)
		{
			Motor->Teleport(Spot.Origin, -Spot.Angles.Y);
			Origin = Spot.Origin;
			Angles.Y = Spot.Angles.Y;
		}
	}
	Spot.Arrived(Handle);

	const uint32 StaySeed = HashCombineFast(static_cast<uint32>(FMath::Max(0, Handle.Index)),
		static_cast<uint32>(FMath::Max(0, Spot.Handle.Index)));
	const float Unit = static_cast<float>(StaySeed) / static_cast<float>(MAX_uint32);
	const float Lo = FMath::Max(0.0f, FMath::Min(Spot.MinTime, Spot.MaxTime));
	const float Hi = FMath::Max(Lo, FMath::Max(Spot.MinTime, Spot.MaxTime));
	AmbientLeaveAt = Now + FMath::Lerp(Lo, Hi, Unit);

	const FElysiumInterestingPlaceType* TypeRow = AmbientType(&Spot);
	if (TypeRow && PlayAmbientActivity(TypeRow->IntoActivities, /*bLoop=*/false,
		Now, AmbientNextActivityAt))
	{
		AmbientPhase = EAmbientPhase::Into;
	}
	else
	{
		AmbientPhase = EAmbientPhase::Dwelling;
		AmbientNextActivityAt = Now;
	}
}

void FElysiumNpc::BeginAmbientLeave(double Now)
{
	FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
	const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
	if (TypeRow && PlayAmbientActivity(TypeRow->OutOfActivities, /*bLoop=*/false,
		Now, AmbientNextActivityAt))
	{
		AmbientPhase = EAmbientPhase::Out;
		return;
	}
	FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
}

void FElysiumNpc::FinishAmbientUse(bool bFireLeft, bool bStopMovement)
{
	// This is the single exit for an ambient claim, including UseInteresting(0), a disabled spot,
	// dialogue, dormancy and a patrol taking ownership. Cancel a request before forgetting it so
	// the native controller cannot keep walking an entity the substrate now considers idle.
	if (bStopMovement && AmbientPhase == EAmbientPhase::Moving && bMoveIssued && Motor)
	{
		Motor->Stop();
	}
	if (FElysiumInterestingPlace* Spot = CurrentAmbientSpot())
	{
		if (bFireLeft)
		{
			Spot->Left(Handle);
		}
		Spot->Release(Handle);
	}
	CurrentSpotIndex = INDEX_NONE;
	NpcFlags.Clear(EElysiumNpcFlag::INTERESTING_INTO);
	NpcFlags.Clear(EElysiumNpcFlag2::INTERESTING_LOST);
	AmbientPhase = EAmbientPhase::None;
	bAmbientArrived = false;
	bMoveIssued = false;
	bWalkingAnimation = false;
	if (AmbientOwner.IsSet())
	{
		Mind.Release(AmbientOwner, TEXT("interesting-place release"));
		AmbientOwner.Reset();
	}
	// The spot's run ends here, whichever way it ended — a natural leave, `UseInteresting(0)`, a
	// disabled spot, dialogue, dormancy or a patrol taking ownership all funnel through this one
	// exit. The claim goes back BEFORE the idle below, because the resting pose comes in on the same
	// ambient band and a standing held claim would refuse it.
	ReleaseAnimSegment();
	if (ScriptPhase == EScriptPhase::None)
	{
		ResetAnimToIdle();   // a script that owns the body owns its pose too
	}
}

void FElysiumNpc::ThinkAmbient(double Now)
{
	// No arm of this executor writes a cadence. Its old 0.05-1.0 s literals were the port's own
	// polling rates; a visit is a schedule in retail (`SelectSchedule` case 1 re-selects it) and so
	// runs on the AI clock like every other program. 11 retires the executor into that shape.
	if (!Motor)
	{
		return;
	}

	FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
	if (AmbientPhase == EAmbientPhase::None || !Spot)
	{
		Spot = ClaimAmbientSpot();
		if (!Spot)
		{
			if (!FailedSpotIndices.IsEmpty())
			{
				FailedSpotIndices.Reset();
			}
			return;
		}
		AmbientPhase = EAmbientPhase::Moving;
		MoveGoal = Spot->Origin;
		bMoveIssued = Motor->MoveTo(Spot->Origin, 24.0f,
			ElysiumNpcGait::TravelSpeed(Motor, EElysiumNpcGaitKind::Walk),
			/*bAllowPartialPath=*/false, EElysiumNpcGaitKind::Walk);
		if (bMoveIssued)
		{
			bWalkingAnimation = StartWalkingAnimation();
		}
		else
		{
			FailedSpotIndices.Add(Spot->Handle.Index);
			FinishAmbientUse(/*bFireLeft=*/false);
		}
		return;
	}

	if (!Spot->IsEnabledFor(Handle))
	{
		if (!bAmbientArrived)
		{
			FinishAmbientUse(/*bFireLeft=*/false);
			return;
		}
		if (AmbientPhase != EAmbientPhase::Out)
		{
			BeginAmbientLeave(Now);
			return;
		}
		// An already-started out activity is allowed to finish below even though Disable made
		// the place unavailable to new claimants.
	}

	if (AmbientPhase == EAmbientPhase::Moving)
	{
		const EElysiumNpcMoveStatus Status = SampleMotorIntoEntity();
		if (Status == EElysiumNpcMoveStatus::Reached)
		{
			BeginAmbientUse(*Spot, Now);
		}
		else if (Status == EElysiumNpcMoveStatus::Failed
			|| Status == EElysiumNpcMoveStatus::Unavailable)
		{
			FailedSpotIndices.Add(Spot->Handle.Index);
			FinishAmbientUse(/*bFireLeft=*/false);
		}
		return;
	}

	if (AmbientPhase == EAmbientPhase::Out && Now >= AmbientNextActivityAt)
	{
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		return;
	}
	if (AmbientPhase == EAmbientPhase::Into && Now >= AmbientNextActivityAt)
	{
		AmbientPhase = EAmbientPhase::Dwelling;
		AmbientNextActivityAt = Now;
	}
	if (AmbientPhase == EAmbientPhase::Dwelling)
	{
		if (Now >= AmbientLeaveAt)
		{
			BeginAmbientLeave(Now);
		}
		else if (Now >= AmbientNextActivityAt)
		{
			const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
			if (!TypeRow || !PlayAmbientActivity(TypeRow->Activities, /*bLoop=*/false,
				Now, AmbientNextActivityAt))
			{
				ResetAnimToIdle();
				AmbientNextActivityAt = FMath::Min(AmbientLeaveAt, Now + 2.0);
			}
		}
	}
}

FElysiumBodyOwnerToken FElysiumNpc::BeginDialogueBodySession()
{
	if (IsInert())
	{
		// Every other refusal below says why; this one did not, and a silent refusal here reads at
		// the call site as "the body arbiter said no" when the truth is that the entity is dead or
		// hidden. Story 29d spent a bisection on exactly that.
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s refused a dialogue body session: the entity is inert (dead=%d hidden=%d)"),
			*DebugString(), bDead ? 1 : 0, bHidden ? 1 : 0);
		return FElysiumBodyOwnerToken();
	}
	if (DialogueBodyOwner.IsSet())
	{
		return DialogueBodyOwner;
	}
	// Slot 614 at dialogue START. Every retail entry into a player dialogue re-bases the NPC's
	// clock before installing its program: the three `StartPlayerDialog*` inputs (`0x1029ef80`,
	// `0x1029f060`, `0x1029f120`: `FinishTalking`, reset, `m_bForceDialogStart`, `SetSchedule
	// (0x6d)`) and the `+use` path (`CBasePlayer::PlayerUse` `0x10167850`: `CanTalk()`, reset,
	// `SetSchedule(0x6a)`). This session is the port's one door for all four.
	ResetThinkTimers(World ? World->NowSeconds() : 0.0);
	// Dialogue is the authored interruption of CCineNPC ownership. Cancel through the sequence
	// itself before asking the mind for Dialogue so travel, action, collision flags and the
	// sequence's delayed completion all leave through the one CancelSequence teardown. This is
	// what prevents an older action deadline from resetting a newer dialogue line to idle.
	if (ScriptOwner.IsSet() && World)
	{
		const FElysiumEntityHandle PreviousOwner = ScriptOwner;
		if (FElysiumEntity* Owner = World->Resolve(PreviousOwner))
		{
			if (Owner->CancelScriptedSequenceForDialogue(Handle) && ScriptOwner.IsSet())
			{
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("%s dialogue cancelled scripted owner %s but the body claim remained"),
					*DebugString(), *PreviousOwner.ToString());
				return FElysiumBodyOwnerToken();
			}
		}
		else
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s cleared stale scripted owner %s while opening dialogue"),
				*DebugString(), *PreviousOwner.ToString());
			// Nothing is left to run the beat's own teardown, so its body claim is dropped here
			// too -- otherwise the arbiter would still read Sequence and refuse the dialogue.
			ReleaseScriptBody(TEXT("stale scripted owner cleared for dialogue"));
			EndScriptMove();
			ScriptOwner = FElysiumEntityHandle::Invalid();
		}
	}
	// A pushed scripted order is DROPPED rather than parked. The arbiter's one parked slot is spoken
	// for by the patrol route, and `aiscripted_schedule` has no resume: it is a one-shot push with no
	// end and no release, so a conversation ends the order it interrupted. The combat claim leaves
	// for the same one-slot reason it does at `AcquireSequenceBody`.
	EndScriptedSchedule(TEXT("dialogue opened"));
	ReleaseScheduleBody(TEXT("dialogue opened"));
	if (!Mind.Acquire(EElysiumBodyOwner::Dialogue, /*bSuspendCurrent=*/false,
		DialogueBodyOwner, TEXT("dialogue open")))
	{
		return FElysiumBodyOwnerToken();
	}
	Dialogue.bInDialog = true;
	return DialogueBodyOwner;
}

void FElysiumNpc::EndDialogueBodySession(const FElysiumBodyOwnerToken& Token, bool bSilent)
{
	if (DialogueBodyOwner.IsSet() && Token.Owner == DialogueBodyOwner.Owner
		&& Token.Generation == DialogueBodyOwner.Generation)
	{
		Mind.Release(DialogueBodyOwner, bSilent ? TEXT("dialogue silent close")
			: TEXT("dialogue normal close"));
		DialogueBodyOwner.Reset();
	}
	if (bSilent)
	{
		Dialogue.bInDialog = false;
		Dialogue.bForceDialogStart = false;
		// A silent close (the owner died, the world tore down, a second conversation replaced this
		// one) never routes `EndDialog`, so the holster's other door is here. Retail restores the
		// weapon from `CDialog::Release` (`FUN_10178400`), which runs on every teardown path.
		// `RestoreDialogHolster` is one-shot, so the ordinary close is unaffected.
		if (FElysiumPlayer* DialoguePlayer = World ? World->FindPlayer() : nullptr)
		{
			DialoguePlayer->RestoreDialogHolster();
		}
	}
	// No clock reset on the way OUT: retail re-bases at dialogue START (`InputStartPlayerDialog`
	// `0x1029ef80` and its two siblings, `CBasePlayer::PlayerUse` `0x10167850`), never at the end.
	// A patrol or a visit resumes on the next cadence think.
}

bool FElysiumNpc::PrepareBodyForDialogue()
{
	FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	return BeginDialogueBodySession().IsSet();
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
	// (`ElysiumNpcSpawn19.cpp`): the stat template, `Precache`, `SetModel`, the Troika words, the
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

void FElysiumNpc::ReleaseAllBodyOwnership(const TCHAR* Reason, bool bDeadMind)
{
	if (DialogueBodyOwner.IsSet())
	{
		if (World && World->GetOpenDialogOwner() == Handle)
		{
			World->CloseDialog(/*bSilent=*/true);
		}
		else
		{
			EndDialogueBodySession(DialogueBodyOwner, /*bSilent=*/true);
		}
	}
	EndScriptMove();
	FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	EndScriptedSchedule(Reason);
	ReleaseScheduleBody(Reason);
	ClearSchedule();
	Mind.Invalidate(Reason, bDeadMind);
	AmbientOwner.Reset();
	SequenceOwner.Reset();
	ScheduleOwner.Reset();
	ScriptedScheduleOwner.Reset();
	ScriptedScheduleOrder.Reset();
	DialogueBodyOwner.Reset();
	// The beat's own ReleaseNpc still runs; it must find nothing left to give back rather
	// than releasing a token this invalidation already retired.
	bScriptBodyRequested = false;
	bScriptBodyHeld = false;
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
	if (IsInert())
	{
		ReleaseAllBodyOwnership(bDead ? TEXT("death") : TEXT("dormancy"), bDead);
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
	// Troika `Save` `0x102993c0` calls the base's `0x1027bc60` first, and `Restore` runs the base's
	// `0x1027c160` first: the base half of the record leads (story 5 step 5).
	FElysiumNpcBase::Serialize(Ar);
	SerializePatrolBlock(Ar);
	SerializeMakerBlock(Ar);
	SerializeMindBlock(Ar);
	Senses.Serialize(Ar);
	Ar << bLoadoutResolved;
	Disciplines.Serialize(Ar);
	SerializeDisciplineFlags(Ar);
	ScheduleHost.Serialize(Ar);
	// `m_flNextComfortCheckTime`, a retail `SAVE` row and a recorded gap in the generated walk
	// (`ElysiumNpcKernelBindings.cpp`: "the port member exists but its owner keeps it private"), so
	// the record carries it until that owner opens a path.
	Ar << NextComfortCheckTime;
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
	OnRestore(/*bFromLoad=*/true);
	RestartRestoredSchedule();

	// --- The port's re-derivations, which retail has no counterpart for. ---
	// Each of these is a component saying what its own restored words mean; the order is the order
	// the components depend on one another in, and no component reads an archive.
	ScheduleHost.OnPostRestore();
	// The base layer's memory first: the senses' own rebase reads its enemy.
	if (World)
	{
		BaseMemory.Rebase(*World);
	}
	else
	{
		BaseMemory.Reset();
	}
	Senses.OnPostRestore(*this);
	EnemyMemory.Rebase(InWorld);
	Relationships.Rebase(InWorld);
	Witness.Rebase(InWorld);
	RestoreDisciplineState(InWorld);

	// `m_hTargetEnt` and the maker relationship ride the record rather than the walk (both are
	// recorded gaps), so their epochs are this hook's to re-stamp.
	TargetEnt = InWorld.RebaseSavedHandle(TargetEnt);
	OwnerEntity = InWorld.RebaseSavedHandle(OwnerEntity);

	RestorePatrolAndAmbient();
	RestoreMindState();

	// Conditions are not saved (`ElysiumNpcConditions.h`): slot 433 rebuilds them on the first pass.
	// `m_bConditionsGathered` (+0x5ca4) is not saved either: a restored body has not gathered. (The
	// old stamp-the-load-time edge served the port's re-deriving gather, deleted at story 8 wave 2.)
	Cognition.Conditions.Reset();
	Cognition.GatheredAt = -1.0;
	RestoreDeathBodyState();
	// The restored `m_fEffects` decides whether the body is transmitted (`ShouldTransmit` `0x100ab020`).
	RefreshVisualGate();
}

// **Divergence, stated.** Retail's `OnRestore` installs the re-found program by writing
// `m_pSchedule` and nothing else, because its datamap has already restored the whole
// `m_ScheduleState` block underneath it -- the task index at `+0x5c50` (which is what the ceiling
// clamp just above exists to sanitise), `timeStarted +0x5c48` and `timeCurTaskStarted +0x5c4c`. So
// a retail NPC genuinely RESUMES mid-program.
//
// This port does not save those three words, deliberately: a task holds a playing clip, a pending
// motor move or a wall-clock deadline, and none of those survive a load, so resuming at task 3
// would hold a pose nothing is playing. Restarting the same program keeps the intent (an NPC
// mid-lookaround resumes looking around rather than dropping to its stance) without pretending the
// state under it survived. That choice is older than this hook; what changes with pass C is only
// WHICH program comes back -- retail's re-find by name and task-array checksum instead of a saved
// enum -- so the restart is applied here, over retail's own answer, rather than inside it.
void FElysiumNpc::RestartRestoredSchedule()
{
	if (!Schedule.IsRunning())
	{
		return;
	}
	const int32 Restored = Schedule.Current;
	// `Clear` and not `ClearSchedule`: nothing is running here, the record is being replaced by the
	// one the header named. Dispatching slot 435 would release the NPC flag word the record has
	// just restored, and the program would come back conversable and un-oblivious.
	Schedule.Clear();
	ElysiumSchedule::Start(Schedule, Restored, *this);
}

// A restored body stands where the record puts it, so no in-flight travel survives the load; a beat
// cannot be in flight across a save at all (`FElysiumScriptedSequence::SaveBlockReason`, a named
// modernization). The patrol path objects come back with the record (`SerializePatrolBlock`) and
// `TroikaOnRestore` validates them (`0x1029f610`).
void FElysiumNpc::RestorePatrolAndAmbient()
{
	EndScriptMove();
	bMoveIssued = false;
	bWalkingAnimation = false;
	if (AmbientPhase != EAmbientPhase::None)
	{
		FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
		if (!Spot || !Spot->Claim(Handle))
		{
			CurrentSpotIndex = INDEX_NONE;
			AmbientPhase = EAmbientPhase::None;
			bAmbientArrived = false;
		}
	}
	// No clock is touched on any branch: the saved cadence is the authoritative one, and
	// `ApplyEntityRecord` restamps the saved `NextThink` after this hook returns. Retail restores
	// its stamps the same way and resets nothing on a load.
}

// The mind's state and its body owner, validated against the patrol and ambient state the step
// above has just settled -- which is why it runs after it rather than inside the decode.
void FElysiumNpc::RestoreMindState()
{
	const EElysiumNpcState State = static_cast<EElysiumNpcState>(RestoredMindState);
	EElysiumBodyOwner Owner = static_cast<EElysiumBodyOwner>(RestoredMindOwner);
	if (!FElysiumNpcMind::IsSupportedState(State) || !FElysiumNpcMind::IsResumableOwner(Owner))
	{
		Owner = EElysiumBodyOwner::None;
	}
	if (Owner == EElysiumBodyOwner::Ambient && AmbientPhase == EAmbientPhase::None)
	{
		Owner = EElysiumBodyOwner::None;
	}
	Mind.Restore(State, Owner);
	AmbientOwner = Owner == EElysiumBodyOwner::Ambient
		? Mind.CurrentToken() : FElysiumBodyOwnerToken();
	// A restore never resumes `Schedule` ownership either: `FElysiumNpcBase::OnRestore` re-found the program by
	// name and restarted it, and its first movement task takes the claim again.
	ScheduleOwner.Reset();
	// Nor `ScriptedSchedule`. The order behind it is a live goal handle and a route resolved out of
	// the previous map epoch, so it is session state (the reasoning is on
	// `FElysiumScriptedScheduleOrder`); what the push durably changed is the mind state restored
	// just above and, for mode 3, the committed enemy the senses record carries.
	ScriptedScheduleOwner.Reset();
	ScriptedScheduleOrder.Reset();
	// A restore never resumes `Sequence` ownership, so any token from before the load is retired
	// with it. The request survives: whichever order the two entities restore in, a beat that
	// re-stamps its queue lock has its claim taken again on the next think.
	SequenceOwner.Reset();
	bScriptBodyHeld = false;
}

void FElysiumNpc::RestoreDeathBodyState()
{
	if (Mind.State() != EElysiumNpcState::Dead)
	{
		return;
	}
	// A corpse's BODY state is not save state and cannot be: the motor is rebuilt at load and a held
	// pose is a pose, not a fact about the character. So the death transaction's body half -- frozen,
	// non-solid to characters, handed to physics or held on its final frame -- is re-applied rather
	// than restored. Without it a loaded corpse stands up solid, animating its spawn idle.
	//
	// Re-applied HERE and not from an armed think, because a corpse's saved cadence is `never` and
	// there is no think to arm: the snapshot applier restamps the saved `NextThink` after this
	// returns and is the authoritative one there. The body already exists -- `Spawn` builds it, and a
	// snapshot is applied over a fully-spawned world.
	// A dead mind is a corpse: `CreateCorpse` ran inside the death transaction (story 8 wave 2), so
	// the handoff is the load's own work and the corpse mark comes back with it.
	bDeathHandoffDone = false;
	bDeathCommitted = true;
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
	uint8 SavedAmbientPhase = static_cast<uint8>(AmbientPhase);
	Ar << SavedAmbientPhase;
	Ar << CurrentSpotIndex;
	Ar << AmbientLeaveAt;
	Ar << AmbientNextActivityAt;
	Ar << AmbientActivityCycle;
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

// `m_NPCState` and the body owner, as raw words. The shape map records the state pair as `PRIVATE`,
// so no compiled path reaches it and the walk cannot carry it. Validation is `RestoreMindState`.
void FElysiumNpc::SerializeMindBlock(FElysiumSaveArchive& Ar)
{
	uint8 SavedState = static_cast<uint8>(Mind.State());
	EElysiumBodyOwner ResumableOwner = Mind.Owner();
	if (!FElysiumNpcMind::IsResumableOwner(ResumableOwner))
	{
		ResumableOwner = EElysiumBodyOwner::None;
	}
	uint8 SavedOwner = static_cast<uint8>(ResumableOwner);
	Ar << SavedState;
	Ar << SavedOwner;
	if (Ar.IsLoading())
	{
		RestoredMindState = SavedState;
		RestoredMindOwner = SavedOwner;
	}
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
	// HitInfo end/interrupt callbacks (0x101dfe80) retain the original caster. Handle archives strip
	// the map epoch; restore it before expiry resolves the source.
	for (FElysiumActiveDisciplineEffect& Effect : Disciplines.TargetEffects)
	{
		Effect.Source = InWorld.RebaseSavedHandle(Effect.Source);
	}

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

const TCHAR* FElysiumNpc::SaveBlockReason() const
{
	switch (Mind.Owner())
	{
	case EElysiumBodyOwner::Dialogue:          return TEXT("a conversation is open");
	case EElysiumBodyOwner::Sequence:          return TEXT("a scripted sequence is active");
	case EElysiumBodyOwner::ScriptedSchedule:  return TEXT("a scripted schedule is active");
	case EElysiumBodyOwner::Follower:          return TEXT("a follower session is active");
	default:                                   return nullptr;
	}
}

void FElysiumNpc::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumCombatCharacter::GetDebugState(Out);
	Out.Emplace(TEXT("UseInteresting"), bUseInteresting ? TEXT("yes") : TEXT("no"));
	// An empty list is retail's ZERO mask, which matches no place at all — not "every group".
	Out.Emplace(TEXT("Interesting groups"), InterestingPlaceGroups.IsEmpty()
		? TEXT("(none)") : InterestingPlaceGroups);
	Out.Emplace(TEXT("In dialog"), Dialogue.bInDialog
		? FString::Printf(TEXT("YES (%s raw=%d decoded=%d)"),
			ElysiumDialogueCamera::LexToString(Dialogue.DialogOpener), Dialogue.DialogFlags,
			Dialogue.DecodedDialogFlags)
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
	Out.Emplace(TEXT("Body owner"), FString::Printf(TEXT("%s gen=%u parked=%s"),
		LexToString(Mind.Owner()), Mind.Generation(), LexToString(Mind.SuspendedOwner())));
	Out.Emplace(TEXT("Mind transition"), Mind.LastTransition().IsEmpty()
		? TEXT("(none)") : Mind.LastTransition());
	Out.Emplace(TEXT("Scripted schedule"), ScriptedScheduleOrder.IsSet()
		? FString::Printf(TEXT("mode %d (%s, %s) leg %d/%d goal %s"), ScriptedScheduleOrder.Mode,
			ElysiumAiScriptedSchedule::ModeName(ScriptedScheduleOrder.Mode),
			ScriptedScheduleOrder.bRun ? TEXT("run") : TEXT("walk"),
			ScriptedScheduleOrder.Leg, ScriptedScheduleOrder.Route.Num(),
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
	Out.Emplace(TEXT("Scripted move"), ScriptPhase == EScriptPhase::None
		? TEXT("(free)")
		: FString::Printf(TEXT("%s to %s, %.0fcm out"),
			ScriptPhase == EScriptPhase::Travel ? TEXT("travelling") : TEXT("facing"),
			*ScriptMark.ToString(), FVector::Dist2D(Origin, ScriptMark)));

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
