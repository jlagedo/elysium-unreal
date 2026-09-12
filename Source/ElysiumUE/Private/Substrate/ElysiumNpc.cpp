// NPC presence and the first native-Unreal locomotion slice: the `npc_*` character leaf.
//
// The leaf here is the **dialogue half only**. Its place in VtMB's chain is CAI_BaseNPC under
// CBaseCombatCharacter under CBaseAnimating (`ElysiumPlayer.h`), and everything those two own
// — the sheet and its 25 inputs, the WillTalk latch, `default_disposition`, the skeletal body,
// playing a clip on it, following SetOrigin/SetModel, gating it on dormancy — arrives through the
// chain, shared with the player. `elysium.NpcBodies` is the one thing that stays here: it is this
// class's A/B, not the animating node's.

#include "Substrate/ElysiumNpc.h"

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
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumPhysProp.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumFootsteps.h"
#include "ElysiumClassRegistry.h"   // FElysiumClassDesc — the registered descriptor's own name
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcCombatSchedules.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcLoadout.h"
#include "Substrate/ElysiumNpcLog.h"
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

// --- `CAI_BaseNPC::HandleAnimEvent` (`0x10274e30`) — the footstep arm --------------------------
//
// The four ids and nothing else yet. `docs/vtmb/footsteps.md` §1 is the recovery; the rules are
// `Substrate/ElysiumFootsteps.h` and the sequencing is here, because the chain reads this NPC's
// template, this NPC's motor and the world's gate.
//
// **The rest of retail's switch is not claimed here.** `0x10274e30` also handles 1003, 2021/2022,
// 2040, 2070/2071 and 4150-4155; those still fall to `FElysiumCombatCharacter::HandleAnimEvent` and
// then to the anim-event census, which is what keeps them on the work list.

bool FElysiumNpc::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	if (ElysiumFootsteps::IsFootstepEvent(Event.Event))
	{
		// 2050/2051 -> `0x1026d460(this, 0)` "normal"; 2052/2053 -> mode 1 "heavy". The left/right in
		// the id is carried past this point only for the species overrides — the shared chain
		// re-chooses the foot with a coin flip.
		return NpcStep(Event.Event, ElysiumFootsteps::IsHeavyFootstep(Event.Event));
	}
	return FElysiumScriptedCharacter::HandleAnimEvent(Event);
}

const FElysiumFootstepSpecies* FElysiumNpc::ResolveFootstepSpecies()
{
	if (!bFootstepSpeciesResolved)
	{
		bFootstepSpeciesResolved = true;
		FootstepSpecies = Def != nullptr ? ElysiumFootsteps::SpeciesFor(Def->Classname) : nullptr;
	}
	return FootstepSpecies;
}

bool FElysiumNpc::OverrideFootstep(int32 EventId, bool bHeavy)
{
	const FElysiumFootstepSpecies* Row = ResolveFootstepSpecies();
	if (Row == nullptr || !ElysiumFootsteps::SpeciesClaims(*Row, EventId))
	{
		// No override for this classname, or an id this species leaves to the base handler — which
		// is `CNPC_VTzimisceRunner`'s `JMP 0x100146e1` for 2052/2053.
		return false;
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
	// (a) The species overrides replace the whole chain, so they run first and short-circuit it.
	if (OverrideFootstep(EventId, bHeavy))
	{
		return true;
	}
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

void FElysiumNpc::OnDamageCommitted(const FElysiumDmg& Dmg)
{
	const double Now = World ? World->NowSeconds() : 0.0;
	Senses.Memory.LastDamageAttacker = Dmg.Source;
	Senses.Memory.LastDamageTime = Now;
	Senses.Memory.LastDamageAmount = Dmg.CommittedDamage();
	// The other half of step 3 — "records the attack position and attacker, updates enemy memory".
	// The record above is the transient damage notice; `RememberDamage` selects the recovered
	// persistent CAI_Memory-style record. Its observed-actor lifetime is not a five-second
	// relationship window (0x10265ed0 calls into the memory component; 0x102df320 removes only
	// invalid/dead handles).
	// Troika's separate surviving-damage tail (0x102beda0 -> 0x1028e8b0 -> 0x1028e940) max-writes
	// the live FVisible range-override deadline. It neither creates a relation nor expires memory.
	if (Dmg.CommittedDamage() > 0 && World != nullptr && World->Resolve(Dmg.Source) != nullptr)
	{
		Senses.ExtendVisionOverride(*this, Dmg.Source, Now, 5.0);
	}
	ElysiumNpcEnemy::RememberDamage(*this, Dmg, Now);
	// Step 5 of the recovered damage-to-AI transaction: the one-second accumulation window
	// `REPEATED_DAMAGE` is derived from. The window arithmetic is the conditions layer's rule.
	ElysiumNpcCond::AccumulateDamage(Senses.Memory, Dmg.CommittedDamage(), Now);
}

void FElysiumNpc::OnKilled()
{
	// Retail's own first clause: "an NPC already in the death schedule ignores a duplicate kill". The
	// base's one-shot latch is the same guard, so it is read here rather than counted twice.
	if (HasReportedDeath())
	{
		return;
	}
	// The shared body first — the `OnDeath` output, the owner/maker notification and the log. Its
	// producer order is already settled there and death does not reorder it.
	FElysiumCombatCharacter::OnKilled();

	// 1. Every animation-channel claim this character holds goes back. Ahead of the mind and the
	//    body, because a claim outliving its producer is what parks a channel: the executors below
	//    are about to stop existing, and none of them will come back with its handle.
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment != nullptr && Visual != nullptr && !bStealthDeathCommitted)
	{
		Embodiment->ReleaseBodyAnimClaims(Visual);
	}

	// 2. Every body-owner token, the running program, the pushed order and an open conversation, all
	//    vacated together — "strategy and squad claims are vacated" plus the Troika override's hint
	//    and feed/claim releases. `Mind.Invalidate(..., bDead=true)` is what makes current and ideal
	//    state 7 (dead), and a dead mind refuses every later acquisition.
	ReleaseAllBodyOwnership(TEXT("killed"), /*bDeadMind=*/true);

	// 3. The solid-body policy. Frozen rather than hidden: a corpse stays on screen and stops
	//    moving, which is VtMB's own SOLID_NONE + FSOLID_NOT_SOLID and NOT ScriptHide.
	//    `SetIgnoreCharacterCollision` is stated beside it even though freezing already makes the
	//    body non-solid, because the two are separate switches with separate lifetimes: whatever
	//    later un-freezes a body must not also make a corpse start blocking the player again.
	SetBodyFrozen(true);
	SetIgnoreCharacterCollision(true);

	// 4. The death schedule, selected from the death commit itself — "death sound/solid-body policy
	//    leads to the death schedule". It is started directly rather than through `SelectSchedule`,
	//    which is right and is also the only way: the mind is already dead, and a dead mind selects
	//    nothing.
	bDeathHandoffDone = false;
	if (bStealthDeathCommitted)
	{
		// The paired death already supplied its terminal pose. The existing corpse handoff
		// consumes that pose now; a second generic death clip would overwrite the action.
		Schedule.Clear();
		CompleteDeathHandoff();
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		return;
	}
	if (!ElysiumSchedule::Start(Schedule, EElysiumScheduleId::Die, *this))
	{
		// `Start` already reported the refusal by name. The handoff still has to happen, and the
		// dead think below is what runs it.
		Schedule.Clear();
	}
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
}

void FElysiumNpc::InputUseInteresting(const FElysiumInputArgs& Args)
{
	bUseInteresting = Args.Param.ToInt() != 0;
	if (!bUseInteresting)
	{
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	}
	else if (!bPatrolActive)
	{
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}
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
	NextThink = static_cast<float>(World->NowSeconds());
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
	PatrolType = Args.Param.ToString();
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s SetupPatrolType(%s)"),
		*DebugString(), *PatrolType);
}

void FElysiumNpc::InputFollowPatrolPath(const FElysiumInputArgs& Args)
{
	FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	PatrolPath = Args.Param.ToString();
	PatrolIndex = 0;
	bPatrolActive = ResolvePatrolPoints();
	bMoveIssued = false;
	if (bPatrolActive && Mind.IsAdmitted() && Mind.Owner() == EElysiumBodyOwner::None
		&& !PatrolOwner.IsSet())
	{
		bPatrolActive = Mind.Acquire(EElysiumBodyOwner::Patrol, /*bSuspendCurrent=*/false,
			PatrolOwner, TEXT("FollowPatrolPath"));
	}
	if (bPatrolActive)
	{
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}
	UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s FollowPatrolPath: %d/%d points (%s)"),
		*DebugString(), PatrolPoints.Num(), PatrolNames.Num(),
		bPatrolActive ? TEXT("armed") : TEXT("not armed"));
}

void FElysiumNpc::InputClearPatrolPath(const FElysiumInputArgs&)
{
	bPatrolActive = false;
	bMoveIssued = false;
	PatrolIndex = 0;
	PatrolPath.Reset();
	PatrolNames.Reset();
	PatrolPoints.Reset();
	if (PatrolOwner.IsSet())
	{
		if (Mind.Owner() == EElysiumBodyOwner::Patrol)
		{
			Mind.Release(PatrolOwner, TEXT("ClearPatrolPath"));
		}
		else
		{
			Mind.ForgetSuspended(EElysiumBodyOwner::Patrol, TEXT("ClearPatrolPath"));
		}
		PatrolOwner.Reset();
	}
	// A cutscene beat outranks the route inputs: clearing the route while a script owns the
	// body drops the route only, and leaves the beat's travel and its cycle running.
	if (ScriptPhase == EScriptPhase::None)
	{
		if (Motor)
		{
			Motor->Stop();
		}
		ResetAnimToIdle();
	}
	if (bUseInteresting)
	{
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}
}

const FElysiumEntity* FElysiumNpc::FindPatrolPoint(const FString& Name) const
{
	if (const FElysiumEntity* Named = World->FindByName(Name))
	{
		return Named;
	}
	for (const TUniquePtr<FElysiumEntity>& Ent : World->Entities())
	{
		if (!Ent.IsValid() || Ent->Def == nullptr
			|| !Ent->Def->Classname.Equals(TEXT("info_node_patrol_point"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (Ent->Def->Keys.FindRef(TEXT("Group")).Equals(Name, ESearchCase::IgnoreCase))
		{
			return Ent.Get();
		}
	}
	return nullptr;
}

bool FElysiumNpc::ResolvePatrolPoints()
{
	PatrolNames.Reset();
	PatrolPoints.Reset();
	PatrolPath.ParseIntoArrayWS(PatrolNames);
	if (!World)
	{
		return false;
	}
	for (const FString& Name : PatrolNames)
	{
		if (const FElysiumEntity* Point = FindPatrolPoint(Name))
		{
			PatrolPoints.Add(Point->Origin);
		}
		else
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s patrol point '%s' does not resolve"),
				*DebugString(), *Name);
		}
	}
	PatrolIndex = PatrolPoints.IsEmpty() ? 0 : PatrolIndex % PatrolPoints.Num();
	return !PatrolPoints.IsEmpty();
}

bool FElysiumNpc::IssuePatrolMove()
{
	if (!Motor || !bPatrolActive || PatrolPoints.IsEmpty())
	{
		return false;
	}
	PatrolIndex = FMath::Clamp(PatrolIndex, 0, PatrolPoints.Num() - 1);
	MoveGoal = PatrolPoints[PatrolIndex];
	bMoveIssued = Motor->MoveTo(PatrolPoints[PatrolIndex], /*AcceptanceRadiusCm=*/20.0f,
		ElysiumNpcGait::TravelSpeed(Motor, EElysiumNpcGaitKind::Walk),
		/*bAllowPartialPath=*/false, EElysiumNpcGaitKind::Walk);
	if (bMoveIssued && !bWalkingAnimation)
	{
		bWalkingAnimation = StartWalkingAnimation();
	}
	return bMoveIssued;
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

void FElysiumNpc::RestampPatrolToken()
{
	// A suspended route came back with a fresh generation. The leaf's own token has to be
	// re-stamped or the patrol executor would hold one the arbiter no longer honours.
	if (Mind.Owner() == EElysiumBodyOwner::Patrol)
	{
		PatrolOwner = Mind.CurrentToken();
	}
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
	return Mind.Acquire(EElysiumBodyOwner::Sequence, /*bSuspendCurrent=*/PatrolOwner.IsSet(),
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
	RestampPatrolToken();
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

bool FElysiumNpc::EnterGrappleState(const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role,
	EElysiumGrappleType Type, int32 Position, bool bHolster)
{
	if (Type == EElysiumGrappleType::StealthKill)
	{
		// CAI_BaseNPC 0x1026cdc0 -> 0x1026d130. Unlike TASK_MAKE_OBLIVIOUS this
		// increments the raw count without MADE_OBLIVIOUS or OnIncapacitatedStart.
		ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());
		DisconnectFromSquad();
		NpcFlags.AddGrappleOblivious();
		FireOutput(TEXT("OnGrappleBegin"), Partner);
	}
	return FElysiumCombatCharacter::EnterGrappleState(Partner, Role, Type, Position, bHolster);
}

void FElysiumNpc::LeaveGrappleState()
{
	const bool bStealth = Grapple.Type == EElysiumGrappleType::StealthKill;
	if (bStealth) FireOutput(TEXT("OnGrappleEnd"), Grapple.Partner);
	FElysiumCombatCharacter::LeaveGrappleState();
	if (bStealth)
	{
		// 0x1026ce30 -> 0x10007ea0: saturating decrement, then reconnect.
		NpcFlags.RemoveGrappleOblivious();
		ReconnectToSquad();
	}
}

void FElysiumNpc::Think()
{
	// The phase order is the recovered pass's own, and every bool phase keeps the power to end
	// the think: true means it consumed this one, and nothing after it may run.
	if (IsInert())
	{
		return;
	}
	// Death owns the pass outright and is tested first: a corpse admits nothing, resolves no
	// loadout, gathers no conditions and selects no schedule. Its own program is the only thing
	// still running on it, and when that ends the body stops thinking altogether.
	if (ThinkDead())
	{
		return;
	}
	// RunAlternateAI: the attacker owns a mode-3 pair; do not sense or replace its animation.
	if (Grapple.bOwnsStealthAction)
	{
		if (ResolveGrapplePartner()) return;
		LeaveGrappleState();
	}
	if (RunAdmissionBarrier())
	{
		return;
	}
	ResolveLoadout();
	ReplayDeferredScriptedOrder();
	// `SetClosestPlayer` (`0x10293a80`) then `SetPlayerLOS` (`0x10291610`), in `NPCThink`'s own
	// order and OUTSIDE the sense pass, which is where retail runs them: they answer on the normal
	// think while `CAI_Senses::Look` answers on the AI think. Neither is gated by obliviousness --
	// `m_iIsOblivious` gates `PerformSensing`, not these -- and neither is a sighting.
	Senses.SetClosestPlayer(*this, World ? World->NowSeconds() : 0.0);
	Senses.SetPlayerLos(*this, World ? World->NowSeconds() : 0.0);
	RunConditionPass();
	// Retail's `OnStateChange` edge (vtable slot 463), after the pass that can move the state and
	// after `ResolveLoadout` above — the first fire has to see the weapon the loadout equipped, or a
	// guard would spawn idle with nothing to put away and draw it on the first alert only.
	PumpStateChange();
	// A pair this NPC is part of owns the body outright: it advances the transaction from
	// the feeder's think and nothing else moves either actor while it runs.
	if (TickFeed(World ? World->NowSeconds() : 0.0))
	{
		return;
	}
	if (TickScriptWatchdog())
	{
		return;
	}
	if (ThinkInDialog())
	{
		return;
	}
	if (ThinkScriptOwned())
	{
		return;
	}
	if (ThinkSchedulePolicy())
	{
		return;
	}
	ThinkAutonomous();
}

bool FElysiumNpc::ThinkDead()
{
	if (Mind.State() != EElysiumNpcState::Dead)
	{
		return false;
	}
	const double Now = World ? World->NowSeconds() : 0.0;
	double Delay = 0.25;
	// No conditions are passed, and that is not an omission: gathering is suppressed for a corpse, so
	// there is no gathered set to test, and the death program declares no interrupts for it to fire.
	if (Schedule.IsRunning() && ElysiumSchedule::Tick(Schedule, *this, Now, Delay))
	{
		NextThink = static_cast<float>(Now + Delay);
		return true;
	}
	Schedule.Clear();
	// The solid-body policy, re-asserted on EVERY terminal pass rather than once with the handoff.
	// Anything that hands a corpse's body back un-freezes it and re-arms this think — the feed
	// pair's release is the live one, and it runs `EndFeedVictimRole` on a victim the same
	// transaction has just killed — so the pass that turns the think off again is also the pass
	// that takes the body back. Both setters early-return on a state that has not moved, so the
	// ordinary single pass pays nothing.
	SetBodyFrozen(true);
	SetIgnoreCharacterCollision(true);
	CompleteDeathHandoff();
	// Nothing on a dead NPC schedules work — no selection, no executor, no stance machine — so the
	// think is not rescheduled at all rather than being parked on a slow cadence.
	NextThink = ELYSIUM_NEVER_THINK;
	return true;
}

void FElysiumNpc::CompleteDeathHandoff()
{
	if (bDeathHandoffDone)
	{
		return;
	}
	bDeathHandoffDone = true;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr)
	{
		return;   // headless, or a bodiless record — an ordinary absence, not a failure
	}
	// **The named divergence.** Retail creates its ragdoll inside the shared `Event_Killed` body,
	// from the model's own `ACT_DIERAGDOLL` seed pose and with a force envelope composed from the
	// killing blow (`docs/vtmb/combat-and-damage.md`). Ours hands over at the END of the death
	// program, seeded from whatever pose that program left on the body, and with no impulse: the
	// force envelope is unrecovered (the launch slice cannot start before the impulse is), so an
	// invented one would be a behaviour rather than a reproduction.
	if (Embodiment->StartBodyRagdoll(Visual))
	{
		// Physics owns the pose now, so the animation claims mean nothing and go back.
		Embodiment->ReleaseBodyAnimClaims(Visual);
		Mind.RecordExternal(TEXT("death: the body handed to physics from its current pose"));
		return;
	}
	// The stated fallback, and the shipped one. The claims deliberately STAY: the pose stops being
	// evaluated at all, and releasing them would hand the base channel back to a locomotion publish
	// on a body that no longer answers it, leaving the verdict surface naming no holder for a pose it
	// is holding.
	Embodiment->HoldBodyFinalPose(Visual);
	Mind.RecordExternal(TEXT("death: no physics behind this body — holding its final frame"));
}

bool FElysiumNpc::RunAdmissionBarrier()
{
	// The activation barrier admits the mind on its first frozen-time think. Admission is a
	// no-op for body, motor and animation; executors may run only on a later think.
	if (!Mind.Admit())
	{
		return false;
	}
	// Every admitted NPC gets a next think, not just the ones with an executor: a standing
	// character's stance machine is an executor too, and without this it would never run.
	NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + 0.1);
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

void FElysiumNpc::RunConditionPass()
{
	// --- Condition gathering ---
	// Senses run before any executor picks work, which is where the recovered pass puts them, and
	// are suppressed exactly where retail suppresses condition gathering: a scripted owner or an
	// in-flight scripted move is driving this body (`docs/vtmb/npc-ai-reverse-engineering.md`).
	// The inert/dead gate is `Think`'s early return.
	if (!ScriptOwner.IsSet() && ScriptPhase == EScriptPhase::None)
	{
		const double SenseNow = World ? World->NowSeconds() : 0.0;
		// `CAI_BaseNPC::PerformSensing` (`0x1026e4f0`) runs the sense pass only when
		// `m_iIsOblivious < 1`. This is the first and largest of the refcount's four consumers: an
		// oblivious body takes in NO sight, sound or scent at all — it is not merely uninterested in
		// what it senses, it senses nothing. That is what makes a mesmerized victim stand through a
		// gunfight, and it is a strictly stronger statement than `DONT_INVESTIGATE` below.
		if (!IsOblivious())
		{
			Senses.Tick(*this, SenseNow);
		}
		// The rest of the recovered decision pass, in `RunAI`'s own order — condition
		// gathering (which contains the enemy transaction), then ideal-state selection, then the
		// schedule work every executor below performs.
		ElysiumNpcEnemy::GatherConditions(*this, SenseNow);
		UpdateIdealState(SenseNow);
	}
	else
	{
		// Gathering is suppressed, so the previous pass's conditions are stale. Retail clears
		// transient conditions when a pass ends; a pass that never runs must not leave a schedule
		// interruptible by a stimulus nobody re-observed.
		Cognition.Conditions.Reset();
	}
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
		NextThink = static_cast<float>(ScriptWatchdogAt);
		return true;
	}
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s released an abandoned scripted move"),
		*DebugString());
	// Everything the beat holds leaves together, the queue lock included: a beat that stopped
	// advancing its own move will not run its teardown either, and half a claim would leave
	// this body suppressed and unowned for the rest of the map.
	ReleaseScriptBody(TEXT("abandoned scripted move"));
	EndScriptMove();
	ScriptOwner = FElysiumEntityHandle::Invalid();
	bScriptOwnerLocked = false;
	// The beat's montage-slot run claim goes with the rest of what it held. Nothing else can give it
	// back — the beat that took it is the thing that stopped answering — and it has no duration, so a
	// claim left standing here would refuse the idle below at the ambient band and park this body's
	// base channel in its travel cycle for the rest of the map.
	ReleaseAnimSegment();
	ResetAnimToIdle();
	return true;
}

bool FElysiumNpc::ThinkInDialog()
{
	if (!Dialogue.bInDialog)
	{
		return false;
	}
	// A per-line VCD owns the body while its sequence/gesture event is live. The ordinary
	// dialogue stance think must not replace that one-shot with a disposition idle.
	if (World && World->HasActiveDialogueBodyClip(Handle))
	{
		NextThink = static_cast<float>(World->NowSeconds() + 0.1);
		return true;
	}
	// A character in conversation still runs its stance machine -- retail's Talking
	// threshold/chance pair exists precisely for this case. The selector settles it onto its
	// current idle rather than fidgeting through a line, so the reschedule is what keeps it
	// posed rather than what makes it move.
	ThinkStanceOrIdle(World ? World->NowSeconds() : 0.0);
	return true;
}

bool FElysiumNpc::ThinkScriptOwned()
{
	if (!ScriptOwner.IsSet())
	{
		return false;
	}
	// A scripted owner — a `scripted_sequence` beat or a choreographed scene's cast — is
	// driving this body's pose. Script ownership suppresses the ordinary condition-gathering
	// path (`docs/vtmb/npc-ai-reverse-engineering.md`), so nothing after this phase may select a
	// schedule whose idle would replace the clip the owner put on the body.
	const double Now = World ? World->NowSeconds() : 0.0;
	if (bScriptBodyRequested && !bScriptBodyHeld
		&& Mind.CanAcquire(EElysiumBodyOwner::Sequence))
	{
		// The beat asked before this NPC's admission think had run. Admission has happened
		// by now, so the claim it is entitled to lands here.
		bScriptBodyHeld = AcquireSequenceBody(TEXT("scripted beat claim after admission"));
	}
	NextThink = static_cast<float>(Now + 0.25);
	return true;
}

bool FElysiumNpc::ThinkSchedulePolicy()
{
	// Schedule selection pre-empts an autonomous executor.
	// A committed enemy or an authored director outranks this NPC's own patrol route and
	// interesting-place visit: without this routing a patrolling guard that acquired an enemy
	// would keep walking its route and never reach `SelectCombatSchedule`.
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
	if (bPatrolActive && bMoveIssued && !ScheduleOwner.IsSet() && !ScriptedScheduleOwner.IsSet())
	{
		// The route's outstanding request stops once, on the hand-over. The TOKEN is not released
		// here: the claim that takes the body suspends it through the arbiter, which is what lets
		// the route resume at the same point when the program is done.
		if (Motor != nullptr)
		{
			Motor->Stop();
		}
		bMoveIssued = false;
		bWalkingAnimation = false;
	}
	ThinkStanceOrIdle(World ? World->NowSeconds() : 0.0);
	return true;
}

void FElysiumNpc::ThinkAutonomous()
{
	if (bPatrolActive && !PatrolOwner.IsSet())
	{
		if (!Mind.Acquire(EElysiumBodyOwner::Patrol, /*bSuspendCurrent=*/false,
			PatrolOwner, TEXT("patrol executor admission")))
		{
			NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + 0.25);
			return;
		}
	}
	if (bPatrolActive && !PatrolPoints.IsEmpty())
	{
		ThinkPatrol();
	}
	else if (bUseInteresting)
	{
		ThinkAmbient();
	}
	else
	{
		// A standing NPC keeps a think: without this arm it would fall off the end of the pass
		// without touching NextThink, never be asked again, and hold whatever pose it spawned in.
		ThinkStanceOrIdle(World ? World->NowSeconds() : 0.0);
	}
}

EElysiumScheduleId FElysiumNpc::SelectIdleSchedule()
{
	// 1. Choreo scene or busy with a discipline -- `CAI_BaseNPCTroika::SelectSchedule`
	//    (`0x102af660`) case 1 opens with `if (IsBusyWithDiscipline() || m_bInChoreoScene) return
	//    0x6b`. `m_bInChoreoScene` maps onto the scripted body owner we already issue; the busy
	//    half is the `D_IS_BUSY` bit, which every discipline-victim schedule and the post-feed
	//    trance set with `TASK_SET_NPC_FLAG`.
	//
	//    This step is HOW AN INCAPACITATING SCHEDULE ENDS. Those programs carry no teardown tasks:
	//    when one completes, the NPC is still busy, so it selects the disposition idle here, and
	//    that install's schedule-change virtual is what releases the bit (`FElysiumNpcFlags::
	//    OnScheduleChange`). One hop through 0x6b, then ordinary selection -- retail's exact exit.
	if (Mind.Owner() == EElysiumBodyOwner::Sequence || IsBusyWithDiscipline())
	{
		return EElysiumScheduleId::IdleDisposition;
	}

	// 2. The follower controller at virtual `+0x97c`. Refused: `EElysiumBodyOwner::Follower` is
	//    already rejected by the mind, and the three `follower_type` radii it compares against
	//    are unrecovered.
	//
	// 3. Patrol, and 4. `use_interesting`. Both keep their existing executors rather than being
	//    re-expressed as task programs -- they own the body through the mind's own token, which
	//    is the arbitration this selection order would otherwise duplicate.
	if (bPatrolActive || bUseInteresting)
	{
		return EElysiumScheduleId::None;
	}

	// 5. Alert lookaround. `m_iEnemySightings` counts committed-enemy acquisition episodes against
	//    the player, so an NPC that has never had one reads the flat 10% floor and a veteran of
	//    four or more reads the 30% cap. `no_alert_state` does NOT suppress this route: the
	//    recovered base `SelectIdealState` carries no such test.
	if (bAllowAlertLookaround)
	{
		const int32 Chance = ElysiumNpcCond::AlertLookaroundChance(EnemySightings);
		if (ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99) < Chance)
		{
			return EElysiumScheduleId::AlertLookAroundNi;
		}
	}

	// 6. Door obstruction (`CAI_BaseNPCTroika::SelectDoorObstructionSchedule`).
	if (const EElysiumScheduleId Door = SelectDoorObstructionSchedule();
		Door != EElysiumScheduleId::None)
	{
		return Door;
	}

	// 7. Return-to-initial, else the disposition stance. Retail's producer is `OnStateChange`
	//    (`0x102ae140`), which arms `m_bReturnToInitialPos` (`+0x6494`) on entry to ALERT or COMBAT
	//    only -- SCRIPT falls into the case that does not -- and the selector consumes it one-shot.
	//    This runtime has no producer, so the flag is permanently clear and this always resolves to
	//    the stance. That is the correct answer for an NPC that has only ever idled or been script-
	//    driven; what is missing is the walk-home a real alert or combat episode should arm, which
	//    needs `SCHED_TROIKA_IDLE_RETURN_TO_INITIAL` and a captured initial position to exist first.
	return EElysiumScheduleId::IdleDisposition;
}

EElysiumScheduleId FElysiumNpc::SelectSchedule()
{
	// A dead NPC selects nothing, ever. `ThinkDead` consumes the whole pass before anything can
	// reach here, so this is unreachable through the think — it is stated anyway because selection
	// has a second door (`StartNamedSchedule`, which a script's `ChangeSchedule` and a discipline's
	// `AI_Schedule` channel both reach), and "the corpse stopped choosing" has to be a property of
	// the selector rather than of one caller's ordering.
	if (Mind.State() == EElysiumNpcState::Dead)
	{
		return EElysiumScheduleId::None;
	}
	// The law branch of schedule selection.
	// "Schedule branches, not condition gathering, call the two player incident consumers." This is
	// the only place in the runtime that reaches them from an NPC.
	//
	// CHOSEN, NOT RECOVERED — the POSITION. The recovered idle branch (`0x102af660` case 1) is
	// decoded step by step and contains no law step, so the branch cannot be inserted into that
	// order without contradicting a decoded body; the recovered material says only "schedule
	// selection/translation". It therefore sits at the outermost selection entry, ahead of the state
	// switch, which is the one point every state passes through and which displaces no decoded
	// order. It declines by returning `None` on all but the flee arm, so an NPC that witnessed
	// nothing takes the ordinary selection.
	if (const EElysiumScheduleId Law =
			ElysiumNpcWitness::SelectLawSchedule(*this, World ? World->NowSeconds() : 0.0);
		Law != EElysiumScheduleId::None)
	{
		return Law;
	}
	switch (Mind.State())
	{
	case EElysiumNpcState::Combat:  return SelectCombatSchedule();
	case EElysiumNpcState::Alert:   return SelectAlertSchedule();
	default:                        return SelectIdleSchedule();
	}
}

EElysiumScheduleId FElysiumNpc::SelectAlertSchedule()
{
	// The executors keep their bodies in alert exactly as they do in idle: a patrol or an ambient
	// place is owned through the mind's token, and re-expressing it as a task program here would
	// duplicate the arbitration.
	if (bPatrolActive || bUseInteresting)
	{
		return EElysiumScheduleId::None;
	}
	if (!Cognition.bReportedAlertRefusal)
	{
		Cognition.bReportedAlertRefusal = true;
		// The recovered alert branch's damage reactions, refused BY NAME rather than approximated.
		// Two of the three programs are registered (`TAKE_COVER_FROM_ORIGIN` 0x19 and
		// `ALERT_SMALL_FLINCH` 0x07, both minimal and marked in
		// `Substrate/ElysiumNpcCombatSchedules.cpp`) and `ALERT_FACE` is not — but what is missing
		// here is the SELECTION, not the programs: the branch turns on "when the attack origin lies
		// within its recovered facing test", and the survey names that test without stating its
		// threshold (`docs/vtmb/combat-and-damage.md` -> "Incapacitation, feeding, grapple, and
		// death"). Choosing between cover and a flinch on an invented angle would be a behaviour.
		RecordScheduleEvent(TEXT("alert: the damage branch's recovered facing test has no decoded "
			"threshold — holding on the lookaround"));
	}
	// CHOSEN, NOT RECOVERED: the alert state's ordinary (undamaged) selection. Retail's case 3 body
	// is not decoded past the three damage reactions above, so the alert idle is taken to be the
	// lookaround — the one alert-named program the survey does decode, and the one the idle branch
	// already reaches on a chance roll. `m_bAllowAlertLookaround` deliberately does NOT gate it: the
	// recovered keyfield gates step 5 of the IDLE selector, not the alert state itself.
	return EElysiumScheduleId::AlertLookAroundNi;
}

EElysiumScheduleId FElysiumNpc::SelectCombatSchedule()
{
	// An NPC running the patrol or interesting-place executor DOES reach this function: `Think`
	// routes a Combat state to schedule selection ahead of either executor, the patrol route is
	// suspended through the arbiter and resumes when the program ends, and an interesting-place visit
	// gives its claimed place back. The hand-over lives in the routing, not here.
	const double Now = World ? World->NowSeconds() : 0.0;

	// The recovered split: a weapon reporting `0x18000` enters the melee selector at `0x10385e40`,
	// every other weapon the ranged one at `0x10386560`. An NPC the item catalogue could not arm
	// takes the melee branch with bare-hands defaults, and its attack tasks then fail by name — the
	// marked unarmed path, which is a visible refusal rather than an NPC that mimes a fight.
	const ElysiumNpcCond::ECapability Capability = ElysiumNpcCond::WeaponCapability(*this);
	const EElysiumScheduleId Chosen = Capability == ElysiumNpcCond::ECapability::Ranged
		? ElysiumNpcCombat::SelectRangedSchedule(*this, Now)
		: ElysiumNpcCombat::SelectMeleeSchedule(*this, Now);
	if (Chosen != EElysiumScheduleId::None)
	{
		return Chosen;
	}

	// --- The composition rule -------------------------------------------------------------------
	// "A selector returning zero falls through to `CAI_BaseNPCTroika::SelectSchedule`, so the weapon
	// policy composes with damage, door, fear and base state reactions rather than replacing them."
	// What follows is that base branch, in the same order the idle selector runs it.
	if (const EElysiumScheduleId Door = SelectDoorObstructionSchedule();
		Door != EElysiumScheduleId::None)
	{
		return Door;
	}
	// "The base idle/combat selectors may choose `SMALL_FLINCH` (0x14)."
	if (Cognition.Conditions.Has(EElysiumNpcCond::HeavyDamage)
		|| Cognition.Conditions.Has(EElysiumNpcCond::LightDamage))
	{
		return EElysiumScheduleId::SmallFlinch;
	}
	// SEAM (comment only): the base branch's fear reaction. The `COWER`/`FLEE` families are 24
	// schedules whose contents the survey does not decode, and `SEE_FEAR` alone does not say which.
	return EElysiumScheduleId::IdleDisposition;
}

bool FElysiumNpc::ClassHolstersOnState() const
{
	// The seven classes that fill vtable slot 463 with the holster/draw body, mapped from the retail
	// class names onto the classnames a map AUTHORS. Four of them have no registered leaf in this
	// runtime yet (`ElysiumNpcClasses.cpp` registers fourteen npc_* names); they are listed anyway,
	// because the rule belongs to the classname and a map that spawns one must not have to wait for
	// this table to be remembered.
	//
	// **Four more overrides are UNREAD and are deliberately absent**: `CNPC_VCop` (0x10371c20),
	// `CNPC_VBach` (0x103639b0), `CNPC_VTzimisce` (0x103ba2c0) and `CNPC_VSabbatLeader`
	// (0x103a6f70) each fill slot 463 with a body this recovery did not decompile. Two of them —
	// npc_VCop and npc_VSabbatLeader — ARE registered classes here, so they take the Troika base's
	// answer (no weapon write) until their bodies are read. That is a stated gap, not a decision.
	static const TCHAR* const Holsterers[] = {
		TEXT("npc_VGuard1"),            // CNPC_VGuard1            0x1037d020
		TEXT("npc_VHunter"),            // CNPC_VHunter            0x10388880
		TEXT("npc_VHumanCombatant"),    // CNPC_VHumanCombatant    0x103871c0
		TEXT("npc_VHumanCombatPatrol"), // CNPC_VHumanCombatPatrol 0x103871c0
		TEXT("npc_VSabbatGunman"),      // CNPC_VSabbatGunman      0x103871c0
		TEXT("npc_VStalker"),           // CNPC_VStalker           0x103871c0
		TEXT("npc_VYukie"),             // CNPC_VYukie             0x103871c0
		TEXT("npc_ProneDialog"),        // CNPC_ProneDialog        0x103871c0
		TEXT("npc_VGhoulCroucher"),     // CNPC_VGhoulCroucher     0x103871c0
	};

	// The classname a map authored, which is the key the recovered class bodies are joined to — the
	// same read `FillActivityClipRequest` makes, and for the same reason.
	// `Def` is the authored definition; a runtime-created NPC has none, and its registered
	// descriptor's name is the classname it answers to. Same read `FillActivityClipRequest` makes.
	const FString Authored = Def != nullptr ? Def->Classname
		: (Class != nullptr ? Class->ClassName.ToString() : FString());
	if (Authored.IsEmpty())
	{
		return false;
	}
	for (const TCHAR* Name : Holsterers)
	{
		if (Authored.Equals(Name, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

void FElysiumNpc::ApplyStateWeaponVisibility(EElysiumNpcState NewState)
{
	if (!ClassHolstersOnState())
	{
		// `CAI_BaseNPCTroika::OnStateChange` (0x102ae140): the base body does not touch the weapon.
		return;
	}
	FElysiumItem* Active = Inventory.Active(*this);
	FElysiumWeapon* Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
	if (Weapon == nullptr)
	{
		// `GetActiveWeapon()` answered null, and both arms of the retail switch are guarded by it.
		return;
	}

	switch (NewState)
	{
	case EElysiumNpcState::Idle:
		// State 1. `GetActiveWeapon()->Hide()`.
		Weapon->Hide(this);
		break;
	case EElysiumNpcState::Alert:
	case EElysiumNpcState::Combat:
		// States 2 and 3. `GetActiveWeapon()->Unhide()`.
		//
		// SEAM: retail's third unhide arm is state 11, which this runtime's `EElysiumNpcState` has
		// no equivalent for — the enum's other members (Scripted, Prone, Dead) are this port's own
		// and none of them is retail's 11. Nothing is known about what 11 means beyond the fact that
		// it draws the weapon, so no port state is mapped onto it rather than guessing one.
		Weapon->Unhide(this);
		break;
	default:
		// Every other state falls straight through to the Troika base, which writes nothing. That
		// includes this runtime's Scripted, Prone and Dead.
		break;
	}
}

void FElysiumNpc::PumpStateChange()
{
	const EElysiumNpcState Now = Mind.State();
	if (bStateChangeSeen && Now == LastStateChange)
	{
		return;
	}
	bStateChangeSeen = true;
	LastStateChange = Now;
	ApplyStateWeaponVisibility(Now);
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

	ElysiumNpcCond::FIdealStateInput In;
	In.Current = Current;
	In.bNoAlertState = bNoAlertState;
	In.bHasEnemy = Senses.Memory.Enemy.IsSet();
	bool bCombatWithoutEnemy = false;
	const EElysiumNpcState Ideal =
		ElysiumNpcCond::SelectIdealState(In, Cognition.Conditions, bCombatWithoutEnemy);

	if (bCombatWithoutEnemy && !Cognition.bWarnedCombatWithoutEnemy)
	{
		// The recovered emission, verbatim, and once: it is a state-machine invariant failing, not
		// a per-think event.
		Cognition.bWarnedCombatWithoutEnemy = true;
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Combat state with no enemy"), *DebugString());
	}
	if (Ideal == Current)
	{
		return;
	}
	Mind.RequestState(Ideal, TEXT("SelectIdealState"));
	// "entering NPC state 14 opens the criminal window".
	// The CHOSEN mapping of retail state 14 onto this runtime's Alert, and why Alert rather than
	// Combat or Idle, is stated in full at `ElysiumNpcWitness::OnEnteredAlertState`. This is its one
	// call site: the promotion edge, after the transition is committed and not on every pass spent in
	// the state.
	if (Ideal == EElysiumNpcState::Alert && Mind.State() == EElysiumNpcState::Alert)
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
	Schedule.Clear();
	// The discarded program's movement claim goes with it. Releasing after `RequestState` is
	// deliberate: the arbiter's own state refresh runs on the release, and it must see the state
	// this pass decided rather than the one the program was chosen under.
	ReleaseScheduleBody(TEXT("ideal state changed"));
}

void FElysiumNpc::ThinkStanceOrIdle(double Now)
{
	double Delay = 0.25;
	if (Schedule.IsRunning()
		&& ElysiumSchedule::Tick(Schedule, *this, Now, Delay, &Cognition.Conditions))
	{
		NextThink = static_cast<float>(Now + Delay);
		return;
	}

	// The program ended — completed, failed through to nothing, or was interrupted. Whatever
	// movement it claimed goes back BEFORE the next selection runs: an idle program picked while
	// this NPC still held the body would decline its own first task against itself.
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
		// that executor's turn, not schedule selection's.
		NextThink = static_cast<float>(Now + 0.05);
		return;
	}

	const EElysiumScheduleId Next = SelectSchedule();
	if (Next == EElysiumScheduleId::None || !ElysiumSchedule::Start(Schedule, Next, *this))
	{
		// No idle schedule applies -- an executor owns this body, or this model carries no
		// stance set at all. Either way it is re-asked on a slow cadence rather than dropped.
		NextThink = static_cast<float>(Now + 1.0);
		return;
	}
	if (!ElysiumSchedule::Tick(Schedule, *this, Now, Delay))
	{
		// The schedule ended inside its first think -- a body with no stance machine takes this
		// path, because `TASK_SPECIAL_IDLE_ACTIVITY` fails for it.
		NextThink = static_cast<float>(Now + 1.0);
		return;
	}
	NextThink = static_cast<float>(Now + Delay);
}

void FElysiumNpc::ThinkPatrol()
{
	if (!bPatrolActive || PatrolPoints.IsEmpty())
	{
		return;
	}
	const double Now = World ? World->NowSeconds() : 0.0;
	if (!Motor)
	{
		NextThink = static_cast<float>(Now + 0.25);
		return;
	}

	const EElysiumNpcMoveStatus Status = SampleMotorIntoEntity();

	if (Status == EElysiumNpcMoveStatus::Reached)
	{
		PatrolIndex = (PatrolIndex + 1) % PatrolPoints.Num();
		bMoveIssued = false;
	}
	else if (Status == EElysiumNpcMoveStatus::Failed
		|| Status == EElysiumNpcMoveStatus::Unavailable)
	{
		bMoveIssued = false; // preserve the point and retry; never silently skip authored route data
	}

	if (!bMoveIssued)
	{
		IssuePatrolMove();
	}
	NextThink = static_cast<float>(Now + (bMoveIssued ? 0.05 : 0.25));
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
			|| !AcceptsAmbientGroup(Spot->GroupId)
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

bool FElysiumNpc::AcceptsAmbientGroup(int32 GroupId)
{
	if (!bAmbientGroupsParsed)
	{
		bAmbientGroupsParsed = true;
		TArray<FString> Tokens;
		InterestingPlaceGroups.ParseIntoArrayWS(Tokens);
		for (const FString& Token : Tokens)
		{
			AmbientGroups.Add(FCString::Atoi(*Token));
		}
	}
	return AmbientGroups.IsEmpty() || AmbientGroups.Contains(GroupId);
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
				if (World)
				{
					NextThink = static_cast<float>(World->NowSeconds() + FMath::Max(0.05f, Seconds));
				}
				return true;
			};
			bPlayedTransition = TryTransition(StanceNumber)
				|| (StanceNumber != 1 && TryTransition(1));
		}
	}
	if (!bPlayedTransition)
	{
		ResetAnimToIdle();
		if (World)
		{
			NextThink = static_cast<float>(World->NowSeconds());
		}
	}
	return true;
}

float FElysiumNpc::RunSpecialIdleActivity(double Now)
{
	// The task writes a clip onto the body, so it runs only while this NPC's own idle owns it.
	// `SCHED_TROIKA_IDLE_DISPOSITION` is still the faithful selection for a choreo-scene NPC --
	// the guard belongs here, at the one step that would overwrite what the owner is playing.
	// The admitted set matches the disposition-stance transition's: nobody, or a conversation.
	const EElysiumBodyOwner BodyOwner = Mind.Owner();
	if (BodyOwner != EElysiumBodyOwner::None && BodyOwner != EElysiumBodyOwner::Dialogue)
	{
		// Failing the task is how a runner declines: the schedule ends through its (absent) fail
		// schedule and the caller re-asks on the slow cadence rather than spinning at zero delay.
		Mind.RecordExternal(FString::Printf(
			TEXT("TASK_SPECIAL_IDLE_ACTIVITY declined: %s owns the body"), LexToString(BodyOwner)));
		return -1.f;
	}
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr || !EnsureStanceResolved())
	{
		return -1.f;
	}
	// `bTalking` is the file's own distinction: a line playing on this character, not a dialogue
	// being open. We have only the session latch until the per-line driver lands, and the two
	// agree on the branch that matters -- a character in dialogue holds its stance either way.
	const int32 Before = Stance.Current;
	const FElysiumStanceChoice Choice = ElysiumStance::Select(StanceClips, StanceTuning, Stance,
		/*bTalking=*/IsDispositionTalking(), Now,
		ElysiumRng::Stream(EElysiumRngStream::NpcSchedule));
	float Seconds = 0.f;
	if (!Choice.IsSet()
		|| !Embodiment->PlayNpcClip(Visual, ModelStem(),
			FElysiumClipSegment(Choice.Clip, Choice.bLoop), &Seconds))
	{
		return -1.f;
	}
	// Only a change is traced. An idle re-settling on the same stance is the common case by far,
	// and recording it would push everything else out of a 16-row window.
	if (Choice.bChangedStance)
	{
		Mind.RecordExternal(FString::Printf(TEXT("stance %d -> %d via %s"),
			Before, Stance.Current, *Choice.Clip));
	}
	return Seconds;
}

bool FElysiumNpc::IsBodyVisible() const
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	// No embodiment is a headless run, where the question has no renderer to answer it. The
	// service's own default says visible for the same reason.
	return Embodiment == nullptr || Embodiment->IsNpcBodyVisible(Visual);
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

bool FElysiumNpc::IsIdealActivityCurrent() const
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr || !ScheduleIdealActivity.IsValid())
	{
		return false;
	}

	FElysiumClipPhase Current;
	if (!Embodiment->GetBodyClipPhase(Visual, EElysiumAnimChannel::Base, Current)
		|| !Current.IsValid())
	{
		return false;
	}
	// Both sides are canonicalized before they cross their seams: the activity resolver supplies the
	// owning include-DAG bank, and the body publishes that same committed identity. Case-insensitive
	// comparison is the convention at every other identity join; no source-model alias participates.
	return Current.OwnerStem.Equals(ScheduleIdealActivity.OwnerStem, ESearchCase::IgnoreCase)
		&& Current.OwnerRoot.Equals(ScheduleIdealActivity.OwnerRoot, ESearchCase::IgnoreCase)
		&& Current.Label.Equals(ScheduleIdealActivity.Label, ESearchCase::IgnoreCase);
}

float FElysiumNpc::PlayDeathActivity(const FString& Activity)
{
	FElysiumReactionPlayRequest Request;
	Request.Activity = Activity;
	// The ladder IS the fallback. Letting the availability probe, the disposition retry or sequence
	// zero substitute something else would resolve a rung the body does not author and hide the
	// ladder's own answer — which on every shipped body is that there is no death performance at all.
	Request.bAllowFallbackLadder = false;
	float Seconds = 0.f;
	// A refusal is an ordinary negative the resolver's own record already names: no body, no
	// embodiment, a vocabulary carrying no such activity, or a choreographed scene that outranks the
	// Reaction band and keeps the body. The ladder simply tries its next rung.
	return PlayReactionActivity(Request, &Seconds) ? FMath::Max(0.f, Seconds) : -1.f;
}

float FElysiumNpc::RandomSeconds(float Max)
{
	return Max > 0.f
		? ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(0.f, Max) : 0.f;
}

void FElysiumNpc::RecordScheduleEvent(const FString& Row)
{
	Mind.RecordExternal(Row);
}

void FElysiumNpc::DebugScheduleInstalled(EElysiumScheduleId InstalledSchedule)
{
	ElysiumNpcDebugLogging::ScheduleInstalled(*this, InstalledSchedule);
}

bool FElysiumNpc::FaceSavePosition()
{
	if (Motor == nullptr)
	{
		return false;
	}
	const FVector ToSource = SavePosition - Origin;
	if (ToSource.IsNearlyZero())
	{
		return false;
	}
	Motor->Face(static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToSource.Y, ToSource.X))));
	return true;
}

bool FElysiumNpc::StepAwayFromSavePosition(float DistanceCm)
{
	// A retreat is body movement, so it claims the body first. The near-door family reaches this
	// from the idle branch and the combat family from the fight; both are schedules moving an NPC,
	// which is exactly what the `Schedule` owner names.
	if (Motor != nullptr && !AcquireScheduleBody(TEXT("TASK_MOVE_AWAY_PATH")))
	{
		Mind.RecordExternal(FString::Printf(TEXT("TASK_MOVE_AWAY_PATH refused: %s owns the body"),
			LexToString(Mind.Owner())));
		return false;
	}
	// The rule itself is `ElysiumSchedule::StepAwayFromSavePosition` — extrapolate, project
	// through the motor, re-test the projection against the retreat rule. This leaf supplies the
	// two positions and turns the outcome into the task's pass/fail, so a refusal reaches the
	// schedule's fail path already named rather than as a bare false.
	FVector Destination = FVector::ZeroVector;
	const ElysiumSchedule::ERetreat Result = ElysiumSchedule::StepAwayFromSavePosition(
		Motor, Origin, SavePosition, DistanceCm, Destination);
	if (Result != ElysiumSchedule::ERetreat::Moving)
	{
		Mind.RecordExternal(FString::Printf(TEXT("TASK_MOVE_AWAY_PATH refused: %s"),
			ElysiumSchedule::RetreatResultName(Result)));
		return false;
	}
	MoveGoal = Destination;
	bMoveIssued = true;
	return true;
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
	// A patrol route is SUSPENDED rather than taken: the arbiter parks it, the release below restores
	// it, and the route continues from the point it reached. An interesting-place visit is not
	// parkable in that sense — it owns a claimed place — so `Think`'s hand-over finishes it first.
	const bool bParkPatrol = Mind.Owner() == EElysiumBodyOwner::Patrol;
	return Mind.Acquire(Owner, bParkPatrol, Token, Reason);
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
	RestampPatrolToken();
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
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		return true;
	}

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
		// transaction rather than writing the handle, so the last-enemy transfer, the LOS-episode
		// reset and everything else an acquisition means all happen exactly once and in one place.
		const FElysiumEntity* Goal = World ? World->Resolve(Order.Goal) : nullptr;
		ElysiumNpcEnemy::SetEnemy(*this, Order.Goal);
		if (Goal != nullptr)
		{
			// "copies its target position". CHOSEN, NOT RECOVERED — WHICH slot. The recovered
			// sentence names a target position and this chain carries exactly one recovered position
			// member, `m_vSavePosition` (+0x5dd0), which is also what the retreat and cover tasks
			// read and what the combat selector stamps with an enemy origin for the same purpose.
			SavePosition = Goal->Origin;
		}
		// The injected condition, by its recovered number: `NEW_ENEMY` is 0x54.
		Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
		// The running program was chosen by an NPC that did not have this enemy. It ends here rather
		// than finishing on behalf of a decision the director has just overruled.
		Schedule.Clear();
		ReleaseScheduleBody(TEXT("aiscripted_schedule assigned an enemy"));
		RecordScheduleEvent(FString::Printf(TEXT("aiscripted_schedule mode 3: enemy := %s"),
			World ? *World->DescribeHandle(Order.Goal) : TEXT("(no world)")));
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		return true;
	}

	const EElysiumScheduleId Program = ElysiumAiScriptedSchedule::ProgramFor(Order.Mode);
	if (Program == EElysiumScheduleId::None)
	{
		// A forced state with no movement mode is an ordinary authored row: two corpus rows push a
		// state alone. The push above already happened, so there is nothing left to refuse.
		RecordScheduleEvent(TEXT("aiscripted_schedule: forced state only, no movement mode"));
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
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
	Schedule.Clear();
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
	if (!ElysiumSchedule::Start(Schedule, Program, *this))
	{
		EndScriptedSchedule(TEXT("scripted program would not start"));
		return false;
	}
	RecordScheduleEvent(FString::Printf(TEXT("aiscripted_schedule mode %d (%s, %s) goal %s"),
		Order.Mode, ElysiumAiScriptedSchedule::ModeName(Order.Mode),
		Order.bRun ? TEXT("run") : TEXT("walk"),
		World ? *World->DescribeHandle(Order.Goal) : TEXT("(no world)")));
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
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
	ScriptedScheduleOrder.Reset();
	if (ElysiumAiScriptedSchedule::IsScriptedProgram(Schedule.Current))
	{
		// The program and the order are one thing. A scripted program left running with no order
		// behind it would fail its next leg by name for a reason no reader could act on.
		Schedule.Clear();
	}
	ReleaseScriptedScheduleBody(Reason);
}

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
	const FVector Destination = ScriptedScheduleOrder.Route[ScriptedScheduleOrder.Leg++];
	const EElysiumNpcGaitKind RouteGait = ScriptedScheduleOrder.bRun
		? EElysiumNpcGaitKind::Run : EElysiumNpcGaitKind::Walk;
	MoveGoal = Destination;
	bMoveIssued = Motor->MoveTo(Destination, ElysiumNpcGait::ScriptAcceptanceCm,
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
	EElysiumScheduleId Id = EElysiumScheduleId::None;
	if (!ElysiumScheduleIdFromName(Requested, Id))
	{
		// The gap is the NAMED program, not the producer. The corpus's five `ChangeSchedule` sites
		// ask for `SCHED_VDOG_SNARL`, `SCHED_VDOG_MADEFRIEND` and the literal `-`, and the shipped
		// `disciplinetgt` records name the Berserk/Possession families; this runtime registers none
		// of them. The reported surface is keyed on the caller AND the name, so `elysium.stubs`
		// reads back exactly which native schedules the shipped content wants, one row each.
		ElysiumStub::Fired(TEXT("schedule"), Surface, DebugString(), Detail,
			TEXT("no registered program carries that name"));
		RecordScheduleEvent(FString::Printf(TEXT("refused: '%s' is not a registered program"),
			*Requested));
		return false;
	}
	// A named schedule starts through the ordinary kernel: interrupts, fail schedules, motor work and
	// activity translation are all whatever the named program declares. Nothing about being named by
	// a script — or by a Discipline record — changes how it runs, which is the whole recovered point
	// of these commands.
	ReleaseScheduleBody(*Surface);
	if (!ElysiumSchedule::Start(Schedule, Id, *this))
	{
		return false;   // `Start` reports the refusal by name through the runner's own trace
	}
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	return true;
}

void FElysiumNpc::StopMoving()
{
	if (Motor != nullptr)
	{
		Motor->Stop();
	}
	bMoveIssued = false;
	bWalkingAnimation = false;
}

EElysiumTaskResult FElysiumNpc::StopMovingTask()
{
	ScheduleHost.PendingFailureReason = 0;
	const FElysiumNpcNavigationSample Nav = Motor ? Motor->SampleNavigation() : FElysiumNpcNavigationSample();
	if (Nav.Type == EElysiumNpcNavType::Jump)
	{
		// CAI_BaseNPC::RunTask 0x102888d4..0x10288963: keep a moving jump alive;
		// at <= 0.01 Source units/s fail instead of holding TASK_STOP_MOVING forever.
		if (!Nav.bGrounded && Nav.VelocityCmPerSecond.Size() > 0.01 * ElysiumMove::U)
			return EElysiumTaskResult::Running;
		// RunTask switches to NAV_GROUND before invoking the failure virtual. That changes
		// TaskFail's PRESERVE_PATH test; a direct navigator failure can still retain NAV_JUMP.
		if (Motor) Motor->SetNavigationType(EElysiumNpcNavType::Ground);
		if (!Nav.bGrounded)
		{
			ScheduleHost.PendingFailureReason = 0x1c;
			return EElysiumTaskResult::Failed;
		}
	}
	if (Nav.Type == EElysiumNpcNavType::Climb) return EElysiumTaskResult::Running;
	bMoveIssued = false;
	bWalkingAnimation = false;
	return EElysiumTaskResult::Complete;
}

EElysiumTaskResult FElysiumNpc::BeginStopMovingTask()
{
	// StartTask 0x10282d71: only an active goal enters RunTask. Clearing the goal must
	// retain nav type and flight velocity, which RunTask still reads in this same think.
	const FElysiumNpcNavigationSample Nav = Motor ? Motor->SampleNavigation() : FElysiumNpcNavigationSample();
	if (!Nav.bActiveGoal)
	{
		bMoveIssued = false;
		return EElysiumTaskResult::Complete;
	}
	Motor->ClearNavigationGoal();
	ScheduleHost.DesiredMoveYaw = 0.f;
	return StopMovingTask();
}

void FElysiumNpc::SetGoalTolerance(float Units)
{
	ScheduleHost.GoalToleranceCm = Units * ElysiumMove::U;
}

void FElysiumNpc::TaskFail(int32 Reason)
{
	// Troika slot 448 (0x1029adb0), then CAI_BaseNPC 0x10273fc0. In particular,
	// OnScheduleChange is not a substitute: its masks and oblivious refcount writes differ.
	if (CurrentAmbientSpot()) FinishAmbientUse(bAmbientArrived, false);
	const FElysiumNpcNavigationSample Nav = Motor ? Motor->SampleNavigation() : FElysiumNpcNavigationSample();
	if (Nav.Type != EElysiumNpcNavType::Jump && Nav.Type != EElysiumNpcNavType::Climb)
		NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH);
	if (Motor) Motor->ResetSteering();
	const double Now = World ? World->NowSeconds() : 0.0;
	ScheduleHost.DesiredMoveYaw = 0.f;
	ScheduleHost.ResetThinkTimers(Now);
	NextThink = static_cast<float>(Now);
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
	ScheduleHost.MemoryBits &= ~0x2000u;
	ScheduleHost.MemoryBits &= 0x0fffffffu;
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
	ScheduleHost.bMotorAnimationMovement = false;
	ScheduleHost.Unknown6300 = ScheduleHost.Unknown659c = 0;
	ScheduleHost.bPatrolPathUseHint = false;
	bMoveIssued = false;
	ScheduleHost.FailureReason = Reason;
	ScheduleHost.PendingFailureReason = 0;
	Cognition.Conditions.Set(EElysiumNpcCond::TaskFailed);
	RecordScheduleEvent(FString::Printf(TEXT("TaskFail 0x%x: %s"), Reason, ElysiumTaskFailureName(Reason)));
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s TaskFail 0x%x: %s"), *DebugString(), Reason, ElysiumTaskFailureName(Reason));
}

void FElysiumNpc::ScheduleDone()
{
	Cognition.Conditions.Set(EElysiumNpcCond::ScheduleDone);
}

void FElysiumNpc::ClearScheduleHint(float ReuseDelay)
{
	// 0x10295ab0: a missing hint performs no writes, and another owner's hint is
	// forgotten locally without imposing our cooldown on that owner.
	if (ScheduleHost.HintNode == INDEX_NONE) return;
	if (ScheduleHost.bOwnsHint)
	{
		ScheduleHost.bOwnsHint = false;
		ScheduleHost.HintReusableAt = (World ? World->NowSeconds() : 0.0) + ReuseDelay;
	}
	ScheduleHost.HintNode = INDEX_NONE;
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

void FElysiumNpc::DisconnectFromSquad()
{
	// 0x1026d050: the refcount is real even while no named squad exists. R17 supplies
	// the shared/global enemy-memory redirection; this host must not invent a local squad.
	++ScheduleHost.SquadDisconnected;
	NpcFlags.Set(EElysiumNpcFlag2::D_DISCONNECT_SQUAD);
}

void FElysiumNpc::ReconnectToSquad()
{
	// 0x1026d0c0. At zero R17 rejoins the squad's shared CAI_Memory.
	ScheduleHost.SquadDisconnected = FMath::Max(0, ScheduleHost.SquadDisconnected - 1);
	NpcFlags.Clear(EElysiumNpcFlag2::D_DISCONNECT_SQUAD);
}

void FElysiumNpc::EndDisciplineSchedule()
{
	// HitInfo expiry 0x101def10 reconnects first, then TaskComplete(false) for only
	// the two interruptible temporary programs. It neither clears nor replaces a schedule.
	if (NpcFlags.Has(EElysiumNpcFlag2::D_DISCONNECT_SQUAD)) ReconnectToSquad();
	const int32 Number = ElysiumScheduleNumber(Schedule.Current);
	if ((Number == 0xe1 || Number == 0xe3) && !Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed))
		Schedule.bTaskCompletedExternally = true;
}

bool FElysiumNpc::GetPathToEnemy(float ToleranceUnits)
{
	ScheduleHost.PendingFailureReason = 0;
	const FElysiumEntity* Enemy = World
		? ElysiumNpcCond::ResolveEnemyHandle(*World, Senses.Memory.Enemy) : nullptr;
	if (Enemy == nullptr || Enemy->IsInert())
	{
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_ENEMY refused: no live committed enemy"));
		ScheduleHost.PendingFailureReason = 0x06;
		return false;
	}
	if (Motor == nullptr)
	{
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_ENEMY refused: this NPC has no motor"));
		ScheduleHost.PendingFailureReason = 0x0c;
		return false;
	}
	if (!AcquireScheduleBody(TEXT("TASK_GET_PATH_TO_ENEMY")))
	{
		Mind.RecordExternal(FString::Printf(
			TEXT("TASK_GET_PATH_TO_ENEMY refused: %s owns the body"), LexToString(Mind.Owner())));
		ScheduleHost.PendingFailureReason = 0x0c;
		return false;
	}
	// The operand is the schedule's own tolerance, in Source units. A program that never ran
	// `TASK_SET_TOLERANCE_DISTANCE` leaves it negative, and the motor's own arrival radius decides.
	const float ToleranceCm = ToleranceUnits > 0.f
		? static_cast<float>(ToleranceUnits * ElysiumMove::U)
		: ElysiumNpcGait::ScriptAcceptanceCm;
	// The enemy's FEET: an entity's origin is its feet in this runtime, which is what the patrol
	// executor already hands the same verb.
	MoveGoal = Enemy->Origin;
	bMoveIssued = Motor->MoveTo(Enemy->Origin, ToleranceCm,
		ElysiumNpcGait::TravelSpeed(Motor, EElysiumNpcGaitKind::Run),
		/*bAllowPartialPath=*/false, EElysiumNpcGaitKind::Run);
	if (!bMoveIssued)
	{
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_ENEMY refused: the body would not take the path"));
		ScheduleHost.PendingFailureReason = 0x0c;
	}
	return bMoveIssued;
}

void FElysiumNpc::RunPath()
{
	// Locomotion, not permission: a bank with no run clip still travels. The miss is recorded so a
	// body walking a chase in its idle pose is diagnosable rather than invisible.
	bWalkingAnimation = StartWalkingAnimation(/*bRunning=*/true);
	if (!bWalkingAnimation)
	{
		Mind.RecordExternal(TEXT("TASK_RUN_PATH: no run locomotion resolved for this body"));
	}
}

// --- The gaze cascade's NPC arms ---

const FElysiumEntity* FElysiumNpc::GazeEnemy() const
{
	// `GetEnemy()`: the committed enemy, dead or alive — the cascade itself refuses an inert one,
	// which is the `IsInert()` half of retail's handle test, not an extra rule.
	if (World == nullptr || !Senses.Memory.Enemy.IsSet())
	{
		return nullptr;
	}
	return ElysiumNpcCond::ResolveEnemyHandle(*World, Senses.Memory.Enemy);
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

EElysiumMoveWatch FElysiumNpc::WaitForMovement()
{
	if (Motor == nullptr)
	{
		return EElysiumMoveWatch::Failed;
	}
	const EElysiumNpcMoveStatus Status = SampleMotorIntoEntity();
	switch (Status)
	{
	case EElysiumNpcMoveStatus::Reached:
		bMoveIssued = false;
		return EElysiumMoveWatch::Arrived;
	case EElysiumNpcMoveStatus::Failed:
	case EElysiumNpcMoveStatus::Unavailable:
		bMoveIssued = false;
		Mind.RecordExternal(TEXT("TASK_WAIT_FOR_MOVEMENT: the body gave up its path"));
		return EElysiumMoveWatch::Failed;
	case EElysiumNpcMoveStatus::Idle:
		// No outstanding request. The step that should have issued one already failed its own task,
		// so arriving here means the body is standing where it was told to be.
		return EElysiumMoveWatch::Arrived;
	default:
		return EElysiumMoveWatch::Moving;
	}
}

bool FElysiumNpc::FaceEnemy()
{
	const FElysiumEntity* Enemy = World
		? ElysiumNpcCond::ResolveEnemyHandle(*World, Senses.Memory.Enemy) : nullptr;
	if (Enemy == nullptr || Enemy->IsInert())
	{
		Mind.RecordExternal(TEXT("TASK_FACE_ENEMY refused: no live committed enemy"));
		return false;
	}
	if (Motor == nullptr)
	{
		// A bodiless NPC turns by writing its own yaw. The task is about where the character is
		// pointed, and the character exists whether or not a capsule was built for it.
		const FVector ToEnemy = Enemy->Origin - Origin;
		if (ToEnemy.IsNearlyZero())
		{
			return false;
		}
		Angles.Y = -static_cast<float>(
			FMath::RadiansToDegrees(FMath::Atan2(ToEnemy.Y, ToEnemy.X)));
		if (World)
		{
			World->NotifyVisualChanged(*this);
		}
		return true;
	}
	if (!AcquireScheduleBody(TEXT("TASK_FACE_ENEMY")))
	{
		Mind.RecordExternal(FString::Printf(TEXT("TASK_FACE_ENEMY refused: %s owns the body"),
			LexToString(Mind.Owner())));
		return false;
	}
	const FVector ToEnemy = Enemy->Origin - Origin;
	if (ToEnemy.IsNearlyZero())
	{
		return false;
	}
	Motor->Face(static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToEnemy.Y, ToEnemy.X))));
	return true;
}

bool FElysiumNpc::AnnounceAttack(float Param)
{
	FElysiumEntity* Enemy = World ? World->Resolve(Senses.Memory.Enemy) : nullptr;
	if (Enemy == nullptr)
	{
		Mind.RecordExternal(TEXT("TASK_ANNOUNCE_ATTACK refused: no live committed enemy"));
		return false;
	}
	const double Now = World->NowSeconds();
	// The notice is an NPC-side record. A player victim has no such memory — its reaction is the
	// player's own input — so announcing at the player is an ordinary negative, not a failure.
	if (FElysiumNpc* Victim = Enemy->AsNpc())
	{
		const bool bAccepted = ElysiumNpcCond::NoticeMeleeAttack(*Victim, Handle, Origin, Now);
		Mind.RecordExternal(FString::Printf(TEXT("TASK_ANNOUNCE_ATTACK %g -> %s %s"),
			Param, *World->DescribeHandle(Victim->Handle),
			bAccepted ? TEXT("accepted") : TEXT("out of notice range")));
	}
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

bool FElysiumNpc::MeleeAttack1()
{
	FElysiumWeapon* Weapon = ElysiumNpcActiveWeapon(*this);
	if (Weapon == nullptr)
	{
		// The marked unarmed path: an NPC the catalogue could not arm fails its terminal attack task
		// by name rather than dealing damage out of nothing.
		Mind.RecordExternal(TEXT("TASK_MELEE_ATTACK1 failed: no active weapon"));
		return false;
	}
	// The transaction is the weapon's (`docs/vtmb/combat-and-damage.md`): the task presses, and the
	// controller acquires its own opponent, stages the swing and schedules the commit.
	const FElysiumWeapon::EVerdict Verdict =
		Weapon->AttackIntent(FElysiumWeapon::EIntent::Primary);
	Mind.RecordExternal(FString::Printf(TEXT("TASK_MELEE_ATTACK1 -> %s"),
		FElysiumWeapon::VerdictName(Verdict)));
	return Verdict == FElysiumWeapon::EVerdict::Accepted;
}

bool FElysiumNpc::RangeAttack1()
{
	FElysiumWeapon* Weapon = ElysiumNpcActiveWeapon(*this);
	if (Weapon == nullptr)
	{
		Mind.RecordExternal(TEXT("TASK_RANGE_ATTACK1 failed: no active weapon"));
		return false;
	}
	// The ranged transaction takes an explicit victim rather than tracing for one: the shot's world
	// trace and spread cone are a producer that joins with the perception cycle, and the committed
	// enemy IS this NPC's answer to it.
	const FElysiumWeapon::EVerdict Verdict =
		Weapon->AttackIntent(FElysiumWeapon::EIntent::Primary, Senses.Memory.Enemy);
	Mind.RecordExternal(FString::Printf(TEXT("TASK_RANGE_ATTACK1 -> %s"),
		FElysiumWeapon::VerdictName(Verdict)));
	return Verdict == FElysiumWeapon::EVerdict::Accepted;
}

void FElysiumNpc::RememberFact(float What)
{
	ScheduleHost.MemoryBits |= static_cast<uint32>(What);
	Mind.RecordExternal(FString::Printf(TEXT("TASK_REMEMBER 0x%x"), static_cast<uint32>(What)));
}

void FElysiumNpc::MakeOblivious(bool bOblivious)
{
	// `CAI_BaseNPC` `0x1026d130` (set) and `0x1026d160` (clear), in their recovered order.
	if (bOblivious)
	{
		// 1. `SetEnemy(NULL)`. Through the ordinary transaction, so the last-enemy transfer, the
		//    slot and the lost-output effects all happen — an incapacitated NPC forgetting its enemy
		//    is the same operation as any other forgetting, not a field poke.
		ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());
		// 2. Squad disconnect (`0x1026d050`: leave the squad, `++m_iSquadDisconnected`, and set
		//    `D_DISCONNECT_SQUAD`).
		//
		// SEAM (named, no substrate): this runtime has no squad object for an NPC to leave, so there
		// is nothing to disconnect from. The flag is set because it is what the schedule-change clear
		// and the scripted-scene teardown both look for, and because a squad layer that lands later
		// must find the bit already correct rather than have to backfill it.
		DisconnectFromSquad();
		// 3. The refcount and its bookkeeping bit.
		NpcFlags.AddOblivious();
		// 4. `OnIncapacitatedStart`. An authored output with 10 wires across the exported maps
		//    (`docs/vtmb/npc-ai-reverse-engineering.md`), so this is a real content surface and not a
		//    diagnostic. The NPC is both caller and activator: nothing else is in scope at the arm.
		FireOutput(FName(TEXT("OnIncapacitatedStart")), Handle);
	}
	else
	{
		NpcFlags.RemoveOblivious();
		ReconnectToSquad();
		FireOutput(FName(TEXT("OnIncapacitatedEnd")), Handle);
	}
	RecordScheduleEvent(FString::Printf(TEXT("TASK_MAKE_OBLIVIOUS %s -> %s"),
		bOblivious ? TEXT("TRUE") : TEXT("FALSE"), *NpcFlags.Describe()));
}

void FElysiumNpc::SetNpcFlag(EElysiumNpcFlag Flag)
{
	NpcFlags.Set(Flag);
	RecordScheduleEvent(FString::Printf(TEXT("TASK_SET_NPC_FLAG NPCFlag:%s"),
		FElysiumNpcFlags::LexToString(Flag)));
}

void FElysiumNpc::ClearConditions()
{
	// Retail's `SetSchedule` zeroes all 192 condition bits. See
	// `IElysiumScheduleRunner::ClearConditions` for why this is half of `DELAY_INTERRUPTS`.
	Cognition.Conditions.Reset();
}

void FElysiumNpc::OnScheduleChange()
{
	// Base slot 435 (0x1027a700) precedes the Troika override. Navigator's notification
	// and the dead strategy-slot namespace have no additional substrate state to release.
	ScheduleHost.MoveWaitFinished = 0.0;
	NpcFlags.BeginScheduleChange();
	if (!NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH))
	{
		const FElysiumNpcNavigationSample Nav = Motor ? Motor->SampleNavigation() : FElysiumNpcNavigationSample();
		if (Motor && Nav.Type != EElysiumNpcNavType::Jump && Nav.Type != EElysiumNpcNavType::Climb)
			Motor->ClearNavigationGoal();
		if (CurrentAmbientSpot()) FinishAmbientUse(bAmbientArrived, false);
		ScheduleHost.Unknown6300 = ScheduleHost.Unknown659c = 0;
		if (Motor) Motor->ResetSteering();
		bMoveIssued = false;
		bWalkingAnimation = false;
		ScheduleHost.GoalToleranceCm = 0.f;
		ScheduleHost.InsideInterruptDistanceSqr = ScheduleHost.OutsideInterruptDistanceSqr = 0.f;
		ScheduleHost.InterruptTime = 0.0;
		ScheduleHost.MoveTarget = FElysiumEntityHandle::Invalid();
		// m_hOpeningDoor/slot532's close operation is supplied with the door obstruction lane.
		if (NpcFlags.ApplyScheduleChangeMasks())
		{
			// UnOblivious 0x1026d160 always calls Reconnect, even if its bookkeeping bit
			// was already cleared by a separate discipline-expiry owner.
			ReconnectToSquad();
			RecordScheduleEvent(TEXT("OnScheduleChange: obliviousness released"));
		}
		if (NpcFlags.Has(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX))
		{
			SetAttackExtents(ScheduleHost.SavedSleepExtents);
			ScheduleHost.SavedSleepExtents = FVector(-1.0);
			NpcFlags.Clear(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX);
		}
		ScheduleHost.bMotorAnimationMovement = false;
		ScheduleHost.DesiredMoveYaw = 0.f;
		ScheduleHost.bWaitFinishedSet = false;
	}
	if (NpcFlags.Has(EElysiumNpcFlag2::ACTIVITY_COPY_PROP_CLEAN))
	{
		ElysiumDisciplines::NotifyScheduleChanged(*this);
		ClearOwnedActivityCopyProps();
		bInvincible = false;
	}
	NpcFlags.FinishScheduleChange();
	ScheduleHost.MemoryBits &= ~0x2000u;
}

void FElysiumNpc::BuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
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
			if (!Senses.Memory.Enemy.IsSet())
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

EElysiumScheduleId FElysiumNpc::SelectDoorObstructionSchedule()
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
		return EElysiumScheduleId::None;
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
	if (Senses.Memory.Enemy.IsSet())
	{
		RecordScheduleEvent(TEXT("door obstruction with an enemy: SCHED_TROIKA_BACK_AWAY_FROM_DOOR "
			"(0x90) / _WAIT (0x94) are not registered — taking the _NE variant"));
	}
	return bNear ? EElysiumScheduleId::BackAwayFromDoorNe
		: EElysiumScheduleId::BackAwayFromDoorWaitNe;
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

void FElysiumNpc::ThinkAmbient()
{
	const double Now = World ? World->NowSeconds() : 0.0;
	if (!Motor)
	{
		NextThink = static_cast<float>(Now + 0.5);
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
			NextThink = static_cast<float>(Now + 1.0);
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
		NextThink = static_cast<float>(Now + 0.25);
		return;
	}

	if (!Spot->IsEnabledFor(Handle))
	{
		if (!bAmbientArrived)
		{
			FinishAmbientUse(/*bFireLeft=*/false);
			NextThink = static_cast<float>(Now + 0.5);
			return;
		}
		if (AmbientPhase != EAmbientPhase::Out)
		{
			BeginAmbientLeave(Now);
			NextThink = static_cast<float>(Now + 0.1);
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
		NextThink = static_cast<float>(Now + 0.05);
		return;
	}

	if (AmbientPhase == EAmbientPhase::Out && Now >= AmbientNextActivityAt)
	{
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		NextThink = static_cast<float>(Now + 0.5);
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
	NextThink = static_cast<float>(Now + 0.1);
}

FElysiumBodyOwnerToken FElysiumNpc::BeginDialogueBodySession()
{
	if (IsInert())
	{
		return FElysiumBodyOwnerToken();
	}
	if (DialogueBodyOwner.IsSet())
	{
		return DialogueBodyOwner;
	}
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
			bScriptOwnerLocked = false;
		}
	}
	// A pushed scripted order is DROPPED rather than parked. The arbiter's one parked slot is spoken
	// for by the patrol route, and `aiscripted_schedule` has no resume: it is a one-shot push with no
	// end and no release, so a conversation ends the order it interrupted. The combat claim leaves
	// for the same one-slot reason it does at `AcquireSequenceBody`.
	EndScriptedSchedule(TEXT("dialogue opened"));
	ReleaseScheduleBody(TEXT("dialogue opened"));
	if (!Mind.Acquire(EElysiumBodyOwner::Dialogue, /*bSuspendCurrent=*/PatrolOwner.IsSet(),
		DialogueBodyOwner, TEXT("dialogue open")))
	{
		return FElysiumBodyOwnerToken();
	}
	if (Motor && bPatrolActive)
	{
		Motor->Stop();
		bMoveIssued = false;
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
		RestampPatrolToken();
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
	if (!Dialogue.bInDialog && (bPatrolActive || bUseInteresting))
	{
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}
}

bool FElysiumNpc::PrepareBodyForDialogue()
{
	FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	if (Motor && bPatrolActive)
	{
		Motor->Stop();
		bMoveIssued = false;
	}
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
	if (!DialogName().IsEmpty())
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
	SeedSheet();
	// Keyfields (model/angles/use_interesting/stattemplate) are already applied. Stand the body:
	// out/npc/<stem>.glb, playing the standing idle `default_disposition` selects, spread across
	// the three VtMB authors per disposition and seeded from this entity's own index — a cop that
	// stood with its arms crossed must still be doing so after a reload. All of that is
	// FElysiumAnimating's; the cvar is this leaf's A/B.
	if (CVarNpcBodies.GetValueOnGameThread() == 0)
	{
		return;   // gated off: a bodiless record whose I/O still resolves
	}
	BuildBody();
	BuildMotor();
	// Spawn constructs presentation only. Activate arms the first deterministic admission think;
	// no autonomous decision, controller wake or activity write occurs in this phase.
}

void FElysiumNpc::Activate()
{
	SeedPlayerRelationship();
	// `InitPerceptionDistances` runs once, on authored data that is already applied, and
	// the hearing cursor starts at the live head so an NPC never hears the map's own load.
	Senses.ResolveTuning(*this);
	Senses.StartSoundCursorAtHead(*this);
	Mind.ArmAdmission();
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
}

bool FElysiumNpc::BypassesKnockbackEligibility() const
{
	// `CNPC_VTzimisceRunner` is the one class whose slot-400 virtual returns 1; every other class in
	// the image keeps the stub. The classname is the whole test.
	// Case-folded, like every other classname test in this runtime: `.ents` content spells a
	// classname however it likes and the registry folds on the way in.
	return Def != nullptr
		&& Def->Classname.Equals(TEXT("npc_VTzimisceRunner"), ESearchCase::IgnoreCase);
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
	Schedule.Clear();
	CombatSelector.Reset();
	Mind.Invalidate(Reason, bDeadMind);
	PatrolOwner.Reset();
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

void FElysiumNpc::OnDormancyChanged()
{
	FElysiumCombatCharacter::OnDormancyChanged();
	if (IsInert())
	{
		ReleaseAllBodyOwnership(bDead ? TEXT("death") : TEXT("dormancy"), bDead);
	}
	else
	{
		// Waking, not going dormant: every NPC gets a think back, standing ones included.
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}
	if (Motor)
	{
		Motor->SetEnabled(!IsInert());
	}
}

void FElysiumNpc::Serialize(FElysiumSaveArchive& Ar)
{
	// One helper per version block, in exact archive order. Only the patrol block may end the
	// record: a payload written before ambient-place state existed carries nothing after it.
	if (!SerializePatrolBlock(Ar))
	{
		return;
	}
	SerializeMakerBlock(Ar);
	SerializeMindBlock(Ar);
	SerializeScheduleBlock(Ar);
	SerializeSocialBlock(Ar);
	SerializeSensesBlock(Ar);
	if (Ar.Version() >= FElysiumSaveVersion::NpcEnemyMemory)
	{
		EnemyMemory.Serialize(Ar);
		if (Ar.IsLoading() && World)
		{
			EnemyMemory.Rebase(*World);
		}
	}
	SerializeLoadoutBlock(Ar);
	SerializeWitnessBlock(Ar);
	SerializeDisciplineBlock(Ar);
	SerializeDisciplineFlags(Ar);
	ScheduleHost.Serialize(Ar, World);

	if (Ar.IsLoading())
	{
		// Conditions are not saved (`ElysiumNpcConditions.h`): they are rebuilt from the memory
		// above on the first think after the load. What has to be stamped is the pass CLOCK — the
		// edge every stimulus producer measures against. Leaving it at -1 would make an hour-old
		// remembered gunshot look new and promote a restored NPC to alert on the strength of it.
		Cognition.Conditions.Reset();
		Cognition.GatheredAt = World ? World->NowSeconds() : 0.0;
		RestoreDeathBodyState();
	}
}

void FElysiumNpc::RestoreDeathBodyState()
{
	if (Mind.State() != EElysiumNpcState::Dead)
	{
		return;
	}
	// A corpse's BODY state is not save state and cannot be: the motor is rebuilt at load and a held
	// pose is a pose, not a fact about the character. So the death transaction's body half — frozen,
	// non-solid to characters, handed to physics or held on its final frame — is re-applied rather
	// than restored. Without it a loaded corpse stands up solid, animating its spawn idle.
	//
	// Re-applied HERE and not from an armed think, because a corpse's saved cadence is `never` and
	// there is no think to arm: the snapshot applier restamps the saved `NextThink` after this
	// returns and is the authoritative one there (the same caveat the patrol and discipline blocks
	// state). The body already exists — `Spawn` builds it, and a snapshot is applied over a
	// fully-spawned world.
	bDeathHandoffDone = false;
	SetBodyFrozen(true);
	SetIgnoreCharacterCollision(true);
	if (!Schedule.IsRunning())
	{
		// The death program had already finished when the save was taken, so nothing is coming to
		// end it: the handoff is the load's own work.
		CompleteDeathHandoff();
	}
	// Otherwise `SCHED_DIE` restarted with the schedule block, the saved think that was carrying it
	// comes back with the record, and `ThinkDead` ends it exactly as it would have.
}

bool FElysiumNpc::SerializePatrolBlock(FElysiumSaveArchive& Ar)
{
	Ar << PatrolType;
	Ar << PatrolPath;
	Ar << PatrolIndex;
	uint8 Active = bPatrolActive ? 1 : 0;
	Ar << Active;
	if (Ar.IsLoading())
	{
		// A restored body stands where the payload puts it, so no in-flight travel survives
		// the load. The beat that owned it re-issues its own move (FElysiumScriptedSequence).
		EndScriptMove();
	}
	if (Ar.IsLoading() && Ar.AtEnd())
	{
		bPatrolActive = Active != 0 && ResolvePatrolPoints();
		bMoveIssued = false;
		bWalkingAnimation = false;
		if (bPatrolActive)
		{
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
		return false; // compatibility with snapshots written before ambient-place state existed
	}
	uint8 SavedAmbientPhase = static_cast<uint8>(AmbientPhase);
	Ar << SavedAmbientPhase;
	Ar << CurrentSpotIndex;
	Ar << AmbientLeaveAt;
	Ar << AmbientNextActivityAt;
	Ar << AmbientActivityCycle;
	uint8 SavedAmbientArrived = bAmbientArrived ? 1 : 0;
	Ar << SavedAmbientArrived;
	if (Ar.IsLoading())
	{
		bPatrolActive = Active != 0 && ResolvePatrolPoints();
		bMoveIssued = false;
		bWalkingAnimation = false;
		AmbientPhase = static_cast<EAmbientPhase>(SavedAmbientPhase);
		bAmbientArrived = SavedAmbientArrived != 0;
		if (bPatrolActive)
		{
			CurrentSpotIndex = INDEX_NONE;
			AmbientPhase = EAmbientPhase::None;
			bAmbientArrived = false;
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
		if (!bPatrolActive && AmbientPhase != EAmbientPhase::None)
		{
			FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
			if (!Spot || !Spot->Claim(Handle))
			{
				CurrentSpotIndex = INDEX_NONE;
				AmbientPhase = EAmbientPhase::None;
				bAmbientArrived = false;
			}
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
	}
	return true;
}

void FElysiumNpc::SerializeMakerBlock(FElysiumSaveArchive& Ar)
{
	// Version 13 appends the maker relationship after the pre-existing NPC leaf. Older saves
	// deliberately restore legacy runtime NPCs unowned rather than guessing a maker association.
	if (Ar.Version() >= FElysiumSaveVersion::NpcMaker)
	{
		Ar << OwnerEntity;
		uint8 OwnerNotified = bOwnerTerminationNotified ? 1 : 0;
		Ar << OwnerNotified;
		if (Ar.IsLoading())
		{
			OwnerEntity = World
				? World->RebaseSavedHandle(OwnerEntity) : FElysiumEntityHandle::Invalid();
			bOwnerTerminationNotified = OwnerNotified != 0;
		}
	}
}

void FElysiumNpc::SerializeMindBlock(FElysiumSaveArchive& Ar)
{
	if (Ar.Version() >= FElysiumSaveVersion::NpcMind)
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
			const EElysiumNpcState State = static_cast<EElysiumNpcState>(SavedState);
			EElysiumBodyOwner Owner = static_cast<EElysiumBodyOwner>(SavedOwner);
			if (!FElysiumNpcMind::IsSupportedState(State)
				|| !FElysiumNpcMind::IsResumableOwner(Owner))
			{
				Owner = EElysiumBodyOwner::None;
			}
			if (Owner == EElysiumBodyOwner::Patrol && !bPatrolActive)
			{
				Owner = EElysiumBodyOwner::None;
			}
			if (Owner == EElysiumBodyOwner::Ambient && AmbientPhase == EAmbientPhase::None)
			{
				Owner = EElysiumBodyOwner::None;
			}
			Mind.Restore(State, Owner);
			PatrolOwner = Owner == EElysiumBodyOwner::Patrol
				? Mind.CurrentToken() : FElysiumBodyOwnerToken();
			AmbientOwner = Owner == EElysiumBodyOwner::Ambient
				? Mind.CurrentToken() : FElysiumBodyOwnerToken();
			// A restore never resumes `Schedule` ownership either: the program restarts from its
			// first task below, and its first movement task takes the claim again.
			ScheduleOwner.Reset();
			// Nor `ScriptedSchedule`. The order behind it is a live goal handle and a route resolved
			// out of the previous map epoch, so it is session state (the reasoning is on
			// `FElysiumScriptedScheduleOrder`); what the push durably changed is the mind state
			// restored just above and, for mode 3, the committed enemy the senses block carries.
			ScriptedScheduleOwner.Reset();
			ScriptedScheduleOrder.Reset();
			CombatSelector.Reset();
			// A restore never resumes `Sequence` ownership, so any token from before the load is
			// retired with it. The request survives: whichever order the two entities restore in,
			// a beat that re-stamps its queue lock has its claim taken again on the next think.
			SequenceOwner.Reset();
			bScriptBodyHeld = false;
			// A restored Dead mind still owes its body the death transaction's body half. It is not
			// re-applied here: the schedule block below decides whether the death program comes back
			// with the record, and `RestoreDeathBodyState` runs once every block has landed.
		}
	}
}

void FElysiumNpc::SerializeScheduleBlock(FElysiumSaveArchive& Ar)
{
	// The schedule's IDENTITY is saved; its task position is not, and that is deliberate. A task
	// holds a playing clip, a pending motor move or a wall-clock deadline, and none of those
	// survive a load -- so resuming at task 3 would hold a pose nothing is playing. Restarting
	// the same program preserves the intent (an NPC mid-lookaround resumes looking around rather
	// than dropping to its stance) without pretending the state under it survived.
	if (Ar.Version() >= FElysiumSaveVersion::NpcSchedule)
	{
		// The flag word travels with the schedule because it IS schedule state: every bit in it was
		// written by a task and is released by the next schedule change.
		//
		// The restore below re-Starts the saved program, and `ElysiumSchedule::Start` runs
		// `OnScheduleChange` -- which releases exactly these bits before the restarted program's own
		// first tasks set them again. That is the same one-think round trip a live schedule change
		// performs, so the word is not redundant: it is what keeps a mesmerized NPC from being
		// conversable, sensing and un-oblivious in the window before its first think after a load,
		// and what carries bits belonging to any program that does not restart at all.
		NpcFlags.Serialize(Ar);
		uint8 SavedSchedule = static_cast<uint8>(Schedule.Current);
		Ar << SavedSchedule;
		if (Ar.IsLoading())
		{
			Schedule.Clear();
			const EElysiumScheduleId Restored = static_cast<EElysiumScheduleId>(SavedSchedule);
			if (ElysiumAiScriptedSchedule::IsScriptedProgram(Restored))
			{
				// A scripted director's program is not restartable without the order that pushed it,
				// and that order is not save state. `SaveBlockReason` refuses a save while the
				// `ScriptedSchedule` owner holds the body, so a payload can only carry this program
				// from the narrow window between the push and its first movement claim. Restarting it
				// would fail its first task by name on the next think; refusing it here says so once,
				// and the NPC selects normally instead.
				RecordScheduleEvent(FString::Printf(
					TEXT("restore refused %s: the pushed order it needs is not save state"),
					ElysiumScheduleName(Restored)));
			}
			else if (Restored != EElysiumScheduleId::None && ElysiumScheduleFor(Restored) != nullptr)
			{
				ElysiumSchedule::Start(Schedule, Restored, *this);
			}
			// NextThink is deliberately NOT touched here. The base record serializes it, so a
			// restored NPC already carries the cadence it was saved on -- rewriting it to "now"
			// would discard saved state and make the payload fail its own round trip.
		}
	}
}

void FElysiumNpc::SerializeSocialBlock(FElysiumSaveArchive& Ar)
{
	if (Ar.Version() >= FElysiumSaveVersion::NpcSocial)
	{
		Relationships.Serialize(Ar);
		if (Ar.IsLoading() && World)
		{
			Relationships.Rebase(*World);
		}
	}
}

void FElysiumNpc::SerializeSensesBlock(FElysiumSaveArchive& Ar)
{
	// The memory is what survives losing sight, so it is what a save has to carry; the
	// resolved perception pair is not saved because it is derived from the keyfields the field
	// walk already restored.
	if (Ar.Version() >= FElysiumSaveVersion::NpcSenses)
	{
		Senses.Serialize(Ar, *this);
	}
}

void FElysiumNpc::SerializeLoadoutBlock(FElysiumSaveArchive& Ar)
{
	// The loadout latch, and only the latch: the weapon it granted is a real runtime entity
	// the snapshot already carries with its own owner field, so re-running the resolution on a
	// restore would hand a restored NPC a second gun. A payload that predates this restores the
	// latch CLEAR, which is correct for it — an older payload was written by a build that granted
	// nothing, so the loadout has genuinely not run for that NPC.
	if (Ar.Version() >= FElysiumSaveVersion::NpcCombat)
	{
		uint8 LoadoutResolved = bLoadoutResolved ? 1 : 0;
		Ar << LoadoutResolved;
		if (Ar.IsLoading())
		{
			bLoadoutResolved = LoadoutResolved != 0;
		}
	}
	else if (Ar.IsLoading())
	{
		bLoadoutResolved = false;
	}
}

void FElysiumNpc::SerializeWitnessBlock(FElysiumSaveArchive& Ar)
{
	// The retained witness block.
	// Appended at the very end of the NPC leaf behind its own version, so it is additive: an
	// `NpcCombat` payload restores an NPC that has witnessed nothing and whose three windows are at
	// the spawn-zero default, which is exactly what the pre-witness build wrote. The processed counts
	// restore at zero there, which means a legacy NPC will observe the first act after the load —
	// the conservative direction, since the alternative would silently forgive a crime.
	if (Ar.Version() >= FElysiumSaveVersion::NpcWitness)
	{
		Witness.Serialize(Ar);
		if (Ar.IsLoading())
		{
			if (World)
			{
				Witness.Rebase(*World);
			}
			else
			{
				Witness.Reset();
			}
		}
	}
	else if (Ar.IsLoading())
	{
		Witness.Reset();
	}
}

void FElysiumNpc::SerializeDisciplineBlock(FElysiumSaveArchive& Ar)
{
	// The NPC's own tracked discipline effects.
	// A targeted `disciplinetgt` cast lands its trait-effect groups on whichever character it hit —
	// active targeted effects are tracked on the affected character — and the affected character
	// is usually an NPC. Both
	// halves must persist: without this block a Dominate group on a guard evaporates across a save
	// while its owned expiry event rides the map snapshot's queue block and comes back looking for
	// it.
	//
	// Appended at the very END of the NPC leaf behind its own version, so it is additive: an
	// `NpcWitness` payload restores an NPC carrying no discipline state, which is what a payload
	// written before this block carries.
	//
	// The owned expiry events themselves are NOT written here — they are queue records and the map
	// snapshot's queue block already carries them, serial and all. That is what makes the restore
	// coherent: the event comes back pointing at the serial this block restores. An event whose
	// serial no longer matches anything (a renewal minted a newer one before the save, or teardown
	// ran) is dropped by `ElysiumDisciplines::CommitExpiry`'s own guard with a Verbose line, which
	// is the guard working rather than a loss.
	if (Ar.Version() >= FElysiumSaveVersion::NpcDisciplines)
	{
		Disciplines.Serialize(Ar);
		if (Ar.IsLoading())
		{
			// Session state, never simulation state: a restored character starts from the live
			// sound bus rather than replaying a retention window that no longer exists.
			Disciplines.SoundCursor = 0;
			// HitInfo end/interrupt callbacks (0x101dfe80) retain the original caster.
			// Handle archives strip the map epoch; restore it before expiry resolves the source.
			if (World)
			{
				for (FElysiumActiveDisciplineEffect& Effect : Disciplines.TargetEffects)
				{
					Effect.Source = World->RebaseSavedHandle(Effect.Source);
				}
			}

			// The tracked rows say which authored groups this NPC is carrying; `Effects` is the
			// list they were installed into, and an NPC's `Effects` is rebuilt at spawn from its
			// `stattemplate` alone (`SeedSheet`), so the discipline groups are missing from it
			// after a restore. Re-append exactly one copy per tracked row — the same one-per-live-
			// effect invariant `RemoveTargetEffect` removes against, which is why this adds rather
			// than `AddUnique`s: two records may legitimately name the same group.
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
				// `RebuildEffects` re-derives the `health`/`max_health` pair off the sheet, and an
				// NPC's SHEET is not save state — it is re-seeded from the stat template at spawn,
				// so its damage slot reads zero here while the field walk has already restored the
				// real `health` keyfield. Re-deriving would therefore hand a wounded NPC its whole
				// track back, so the restored pair is put back over the derived one. (The sheet/
				// keyfield split on a restored NPC is older than this block and is not changed by
				// it; this only refuses to make it worse.)
				const int32 RestoredHealth = Health;
				const int32 RestoredMaxHealth = MaxHealth;
				RebuildEffects();
				Health = RestoredHealth;
				MaxHealth = RestoredMaxHealth;
			}
			// A restored row keeps its `bRemoveOnHearCombat` listener, and the poll that services it
			// runs from this character's own think, so the think is armed here — the same statement
			// the patrol and ambient branches above make, and with the same caveat: the snapshot
			// applier restamps the SAVED think after this returns and is the authoritative one
			// there. This arms the direct-leaf path and can only ever move the deadline earlier.
			if (Disciplines.TargetEffects.ContainsByPredicate(
				[](const FElysiumActiveDisciplineEffect& Effect)
				{ return Effect.bRemoveOnHearCombat; }))
			{
				NextThink = FMath::Min(NextThink,
					static_cast<float>(World ? World->NowSeconds() : 0.0));
			}
		}
	}
	else if (Ar.IsLoading())
	{
		Disciplines.Reset();
	}
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
	Out.Emplace(TEXT("Interesting groups"), InterestingPlaceGroups.IsEmpty()
		? TEXT("(all)") : InterestingPlaceGroups);
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
	Out.Emplace(TEXT("Patrol"), bPatrolActive
		? FString::Printf(TEXT("point %d/%d: %s"), PatrolIndex + 1, PatrolPoints.Num(), *PatrolPath)
		: TEXT("inactive"));
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
	Out.Emplace(TEXT("Enemy"), Mem.Enemy.IsSet()
		? FString::Printf(TEXT("%s (%s, %d failed LOS checks%s)"), *Mem.Enemy.ToString(),
			Mem.bEnemyOccluded ? TEXT("OCCLUDED") : TEXT("has LOS"), Mem.EnemyLosFailures,
			EnemyMemory.IsEluded(Mem.Enemy) ? TEXT(", ELUDED") : TEXT(""))
		: TEXT("(none)"));
	Out.Emplace(TEXT("Last enemy"), Mem.LastEnemy.IsSet()
		? Mem.LastEnemy.ToString() : FString(TEXT("(none)")));
	Out.Emplace(TEXT("Enemy sightings"), FString::FromInt(EnemySightings));
	Out.Emplace(TEXT("Last heard"), Mem.LastHeardTime < 0.0
		? TEXT("(nothing)")
		: FString::Printf(TEXT("%s at %s, t=%.2f"), *Mem.LastHeardCategory,
			*Mem.LastHeardPosition.ToString(), Mem.LastHeardTime));
	Out.Emplace(TEXT("Last damage"), Mem.LastDamageTime < 0.0
		? TEXT("(none)")
		: FString::Printf(TEXT("%d from %s at t=%.2f (window sum %d)"), Mem.LastDamageAmount,
			*Mem.LastDamageAttacker.ToString(), Mem.LastDamageTime,
			Mem.RepeatedDamageAccumulated));

	// --- Decision pass ---
	Out.Emplace(TEXT("Conditions"), FString::Printf(TEXT("%s (gathered t=%.2f)"),
		*Cognition.Conditions.Describe(), Cognition.GatheredAt));
	Out.Emplace(TEXT("no_alert_state"), bNoAlertState ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Schedule"), Schedule.IsRunning()
		? FString::Printf(TEXT("%s (0x%x) task %d"), ElysiumScheduleName(Schedule.Current),
			ElysiumScheduleNumber(Schedule.Current), Schedule.TaskIndex)
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

// --- npc_VPlayerController ---

void FElysiumPlayerControllerNpc::Spawn()
{
	if (CVarNpcBodies.GetValueOnGameThread() != 0)
	{
		BuildBody();
		BuildOwnMotor();
	}
}

void FElysiumPlayerControllerNpc::OnRuntimeModelChanged()
{
	RebuildForModelChange(CVarNpcBodies.GetValueOnGameThread() != 0);
}

void FElysiumPlayerControllerNpc::SetIgnoreCharacterCollision(bool)
{
	// The duplicate navigates against the world but never becomes a second solid character.
	if (Motor)
	{
		Motor->SetIgnoreCharacterCollision(true);
	}
}

void FElysiumPlayerControllerNpc::OnDormancyChanged()
{
	FElysiumCombatCharacter::OnDormancyChanged();
	if (IsInert())
	{
		EndScriptMove();
	}
	if (bDead)
	{
		DestroyMotor();
	}
	else if (Motor)
	{
		Motor->SetEnabled(!IsInert());
	}
}

void FElysiumPlayerControllerNpc::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumCombatCharacter::GetDebugState(Out);
	Out.Emplace(TEXT("Role"), TEXT("player controller (non-AI, non-solid)"));
	Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
	Out.Emplace(TEXT("Body"), Visual ? TEXT("skeletal") : TEXT("(none)"));
	Out.Emplace(TEXT("Motor"), Motor ? TEXT("Unreal character + Detour crowd") : TEXT("(none)"));
}

void FElysiumPlayerControllerNpc::BuildOwnMotor()
{
	BuildMotor();
	SetIgnoreCharacterCollision(true);
}
