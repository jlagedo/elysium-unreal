// 11.4 — CBasePlayer / CHL2_Player: the player leaf, its recovered datamap inputs, the XP ledger
// and the session record's hydrate/dehydrate pair (S3).
//
// Moved verbatim out of `ElysiumPlayerClasses.cpp` when the discipline runtime landed; the file
// granularity rule in `Source/ElysiumUE/CLAUDE.md` puts one primary class per `.cpp`. The public
// declaration stays the chain header `Public/ElysiumPlayer.h`, and the class registration stays at
// the one registration site, `ElysiumPlayerClasses.cpp`.

// Cycle 10b (the player law channels) touches this file in five places, all banner-marked:
//   1. `Think` — the law expiry pass, beside cycle 8's stealth block on the same 0.1 s heartbeat;
//   2. `Hydrate` — the unscoped law/police block, with the response witness rebased-or-dropped;
//   3. `GetDebugState` — the deadlines, act counts, response/pursuit state and the world area;
//   4. the three activity-level inputs, with their whole recovered variant/clamp/raise semantics;
//   5. `Dehydrate` — the police block out to the record (one line, marked inline).

#include "ElysiumPlayer.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDisciplines.h"   // Cycle 9
#include "Substrate/ElysiumLaw.h"          // Cycle 10b
#include "Substrate/ElysiumPlayerLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumStealth.h"      // Cycle 8 — 13.1

#include "Components/SkeletalMeshComponent.h"
#include "Misc/Paths.h"

// ============================================================================================
// FElysiumPlayer — CBasePlayer / CHL2_Player
// ============================================================================================

void FElysiumPlayer::Spawn()
{
	// The body is the pawn, already standing: nothing to build, and the first SyncFromBody puts the
	// entity where the pawn is.
	//
	// The health ceiling is read, not derived: `Max_Health` is an ordinary stat with `Default 100`
	// and no formula anywhere in `vdata` — nothing derives health from Stamina (RE24). A run that
	// has been through New Game arrives with the record's seeded sheet; one that has not (a map
	// loaded straight from the console) seeds here so the damage path has a track.
	if (Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth) <= 0)
	{
		if (const FElysiumStatTable* Table = SheetRules())
		{
			Sheet.SeedFrom(*Table);
		}
	}
	// The clan is a sheet slot, so the effect layer it names can only be resolved once the sheet is
	// in place — which is here, whether it arrived from the record or was just seeded.
	RefreshClanEffects();
	SyncHealthFromSheet();
	SyncFromBody();

	if (!Model.IsEmpty())
	{
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Visual = Embodiment->BuildPlayerVisual(ModelStem(), Disposition, IdleVariant());
			if (Visual)
			{
				World->RegisterNpcBody(Visual);
				GateVisual();
			}
		}
	}

	// Cycle 8 — arm the think for the first stealth recompute. The surface's deadline is negative
	// ("due now") on a fresh entity, and `Think` re-arms itself from it after every pass; without
	// this first arm the deadline-driven think would never start.
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
}

void FElysiumPlayer::Think()
{
	const double Now = World ? World->NowSeconds() : 0.0;

	// The player's only autonomous work today is the feed transaction. It runs here rather than off
	// a timer because the pulse deadline is simulation state (R4/S8): the same think that advances
	// it is the one the save's clock restores, so a load cannot duplicate or skip a pulse.
	TickFeed(Now);

	// Cycle 9 — `ShouldRemove_OnHearCombat`. The sound-event bus delivers nothing (§2.5.3), so its
	// consumers poll it during their own think; this is the discipline domain's poll.
	ElysiumDisciplines::PollHeardCombat(*this, Now);

	// ------------------------------------------------------------------------------------------
	// Cycle 8 (13.1) — the stealth target surface (`docs/vtmb/stealth.md` -> "Player target-surface
	// update"). It hangs off THIS think and nowhere else: retail's own recompute is a player-think
	// virtual gated on `m_flNextStealthUpdate`, and this think is reached only through
	// `FElysiumEntityWorld::RunPlayerThink` — never `Tick` — so the 0.1 s cadence is measured on
	// the substrate clock the save restores. One body point is sampled per due pass.
	//
	// The observer snapshot is committed straight after, in the same pass, so the HUD's view is
	// always downstream of the gameplay state it describes (§5.9).
	// ------------------------------------------------------------------------------------------
	ElysiumStealth::TickPlayerSurface(*this, Now);
	ElysiumStealth::CommitObserverSnapshot(*this, Now);

	// ------------------------------------------------------------------------------------------
	// Cycle 10b hunk 1/5 — the law expiry pass (`docs/vtmb/player-entity.md` § "Law, Masquerade and
	// world response"). It rides the same 0.1 s heartbeat cycle 8 established above, for the same
	// reason: retail's `PlayerRuleUpdate` is the first call of `CHL2_Player::PreThink`, this think
	// is reached only through `FElysiumEntityWorld::RunPlayerThink`, and every deadline it reads is
	// therefore measured on the substrate clock the save restores rather than on a timer.
	//
	// Its four steps are retail's own order: supernatural expiry, the delayed response-cop
	// deadline, heightened-alert expiry — and the criminal expiry, which retail reaches from
	// `SetAnimation`'s law helper instead. `ElysiumLaw.cpp` states that divergence beside the code.
	// ------------------------------------------------------------------------------------------
	ElysiumLaw::TickPlayerLaw(*this, Now);

	// SEAM: the footstep hearing stimulus belongs here, and there is nothing to hang it on yet.
	// `sound_volume_table.txt` names `PLAYER_FOOTSTEP_SNEAK`/`_WALK`/`_RUN` plus `PLAYER_JUMP` and
	// the two landings, and separates walking from running by its own `PLAYER_RUN_SPEED` (128
	// units/s) — but this runtime raises no step EVENT at all: no animation notify, no movement
	// callback, nothing on the locomotion sample that says "a foot just landed". Inventing a cadence
	// here would be substrate arithmetic standing in for a producer, so the step stays unemitted
	// until the locomotion/stealth work supplies one. Not warned: nothing failed.

	// Cycle 8 — the re-arm. `RunPlayerThink` clears `NextThink` before entering here, so a think
	// that schedules nothing never runs again. Retail's player think runs every frame and gates the
	// recompute internally; ours is deadline-driven, so the surface's own 0.1 s deadline IS the
	// heartbeat. `Min` keeps whatever the feed transaction scheduled ahead of it.
	NextThink = FMath::Min(NextThink, static_cast<float>(Stealth.NextUpdateTime));
}

void FElysiumPlayer::RefreshClanEffects()
{
	UElysiumGameStateSubsystem* GameState = World ? World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	if (!Rules)
	{
		RebuildEffects();   // no rulebook: the layer still has to match the names, which is nothing
		return;
	}
	// `clandoc000.txt` names the player templates `Player_<Clan>`, and each one names the
	// `TraitEffectGroup` carrying that clan's gift and bane — which is where every bane lives:
	// nothing about a clan is special-cased in code (`docs/vtmb/game_runtime.md` section 3).
	FString Group;
	FElysiumClanTemplate Resolved;
	if (Rules->Clans().Resolve(FString::Printf(TEXT("Player_%s"), FElysiumSheet::ClanName(Sheet.Clan())), Resolved))
	{
		Group = Resolved.GeneralStr(TEXT("ClanEffect"));
	}

	// One clan group at a time: re-running this after a clan change must replace, not accumulate.
	Effects.RemoveAll([](const FString& Name) { return Name.StartsWith(TEXT("Clan (")); });
	if (!Group.IsEmpty())
	{
		Effects.Add(Group);
	}
	RebuildEffects();
}

bool FElysiumPlayer::HasAwarded(const FString& Key) const
{
	// `Q_strnicmp` over the STORED key's length — a prefix compare, which is latent breakage VtMB
	// gets away with because no shipped key prefixes another (`Elysium.Content.Rulebook` asserts
	// the premise still holds). Reproduced as authored, not "fixed".
	for (const FElysiumXpEntry& Entry : ExperienceLog)
	{
		if (!Entry.Entry.IsEmpty() && Key.StartsWith(Entry.Entry, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

int32 FElysiumPlayer::AwardExperience(const FString& Key)
{
	if (Key.IsEmpty())
	{
		return 0;
	}
	// 1. Give-once is the LEDGER, not an encoding: every key is give-once, unconditionally — the
	//    trailing `01` every real row carries has nothing to do with it.
	if (HasAwarded(Key))
	{
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s AwardExperience(\"%s\") — already given"),
			*DebugString(), *Key);
		return 0;
	}

	UElysiumGameStateSubsystem* GameState = World ? World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	const FElysiumExperienceEntry* Row = Rules ? Rules->Experience().Find(Key) : nullptr;
	if (!Row)
	{
		// 2. A key the table does not hold awards nothing AND APPENDS nothing, so it retries on
		//    every fire. That is the engine's own behaviour, not a leniency.
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s AwardExperience(\"%s\") — no such experience_table row"),
			*DebugString(), *Key);
		return 0;
	}

	// 3. The `Experience_Modifier` bonus, above 2 XP — the file's own "the value without extra
	//    experience points". The threshold is on the RAW value, which is in hundredths.
	const int32 Value = ElysiumXp::WithModifier(Row->Value,
		Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::ExpModifier));

	// 4. The key joins the ledger with the amount it was worth.
	FElysiumXpEntry Entry;
	Entry.Entry = Key;
	Entry.Amount = Value;
	ExperienceLog.Add(MoveTemp(Entry));

	// `AddExperience`: the raw value accumulates untouched, the /100 keeps its remainder, and only
	// whole points reach the sheet. Every real row being `N01`, each award banks 0.01 XP of residue
	// and one bonus point falls out per 100 awards.
	const int32 Whole = ElysiumXp::Bank(Value, ExperienceRemainder, LifetimeExperience);
	if (Whole > 0)
	{
		AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience, Whole);
	}
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s AwardExperience(\"%s\") — %d raw, +%d XP (%.0f left over)"),
		*DebugString(), *Key, Value, Whole, ExperienceRemainder);
	return Whole;
}

// ================ Cycle 11b hunk 5/9 — the level-5 loss transaction's map half ==================
// `docs/vtmb/player-entity.md` § "Law, Masquerade and world response": "An increment whose
// resulting value is greater than four loads `sp_masquerade_1`. The retail loss boundary is
// therefore a native level-5 transaction, not only a UI convention." The increment-past-4 rule is
// applied by `FElysiumCombatCharacter::ChangeMasqueradeLevel`, which is what calls this; the map
// load is here because only the PLAYER's counter ends a run.
//
// SEAM — three things about the transition's exact form are not recovered and are not invented:
//   * whether retail's load is a plain map change or a landmark transition. No landmark is named
//     anywhere in the recovered record and `sp_masquerade_1` is an ending map with no return leg,
//     so the plain `ChangeMap` door is taken;
//   * whether it fades, holds the player, or runs any pre-travel presentation first. Nothing is
//     played here — the request goes straight to the travel seam;
//   * what the destination map's own script then does. `sp_masquerade_1` is not in the exported
//     corpus (`vamputil.py` carries only its ordinary `sp_masquerade_1_patch` hook), so nothing is
//     known about the ending it plays beyond the fact that loading it IS the loss.
// This runtime additionally keeps its own session GameOver state, which retail has no equivalent
// of — the map is retail's ending. It is a declared divergence rather than a reproduction, and it
// stays until the ending map is exported and its script owns the transition.
const FString& FElysiumPlayer::MasqueradeLossMap()
{
	static const FString Name(TEXT("sp_masquerade_1"));
	return Name;
}

void FElysiumPlayer::OnMasqueradeBreached()
{
	FElysiumCombatCharacter::OnMasqueradeBreached();

	if (IElysiumTravel* Maps = World ? World->Travel() : nullptr)
	{
		UE_LOG(LogElysiumPlayer, Log,
			TEXT("%s broke the masquerade at %d — loading %s"),
			*DebugString(), GetMasqueradeLevel(), *MasqueradeLossMap());
		Maps->ChangeMap(MasqueradeLossMap());
	}
	else
	{
		// Not a failure: a headless substrate world and the menu backdrop both carry no travel
		// seam, and the session half below still reports the loss. Stated rather than silent,
		// because the recovered transaction did not happen.
		UE_LOG(LogElysiumPlayer, Log,
			TEXT("%s broke the masquerade at %d — no travel seam, so %s is not loaded"),
			*DebugString(), GetMasqueradeLevel(), *MasqueradeLossMap());
	}

	// The second loss condition. Same shape as death: the substrate reports, and the session owns
	// what it means to the application (11.3's GameOver state, with its own reason).
	if (UElysiumGameStateSubsystem* State = World ? World->GetGameState() : nullptr)
	{
		State->NotifyMasqueradeBreach();
	}
}
// ================================================================================================

void FElysiumPlayer::Hydrate(const FElysiumPlayerRecord& Record)
{
	Sheet      = Record.Sheet;
	Money      = Record.Money;
	Health     = Record.Health;
	MaxHealth  = Record.MaxHealth;
	Law        = Record.Law;
	// ------------------------------------------------------------------------------------------
	// Cycle 10b hunk 2/5 — the law/police block crosses UNSCOPED, unlike the feed, discipline and
	// stealth blocks below. Every deadline in it is on the session clock
	// (`UElysiumGameStateSubsystem`'s, which outlives the map), and a wanted level with four
	// seconds left, a Masquerade window, a pursuit count and a heightened alert all mean exactly
	// what they meant in the previous map — none of them describes THIS map's geometry, light or
	// cast. The single exception is the pending response's witness, which is a map entity: that one
	// handle is rebased, and a response whose witness does not survive the boundary is dropped
	// rather than left pointing at whatever now holds its index.
	// ------------------------------------------------------------------------------------------
	Police     = Record.Police;
	if (Police.bResponsePending)
	{
		Police.ResponseWitness = (World && Police.ResponseWitness.IsSet())
			? World->RebaseSavedHandle(Police.ResponseWitness)
			: FElysiumEntityHandle::Invalid();
		if (!Police.ResponseWitness.IsSet())
		{
			UE_LOG(LogElysiumPlayer, Log,
				TEXT("%s pending police response (severity %d) dropped: its witness did not survive "
					"the map boundary"), *DebugString(), Police.ResponseSeverity);
			Police.bResponsePending = false;
			Police.ResponseSeverity = 0;
		}
	}
	else
	{
		Police.ResponseWitness = FElysiumEntityHandle::Invalid();
	}
	ExperienceLog = Record.ExperienceLog;
	Effects    = Record.Effects;
	EmailFlags = Record.EmailFlags;
	ExperienceRemainder = Record.ExperienceRemainder;
	LifetimeExperience  = Record.LifetimeExperience;
	bUnkillable = Record.bUnkillable;
	bDeathReported = false;
	// B6 — a feed only resumes into the map it was taken in, and its handles have to be re-stamped
	// against this world's epoch (the record's copy carries a dead one, exactly as a saved handle in
	// the map snapshot does). Anything else drops the pair rather than pointing it at a stranger.
	FeedState = FElysiumFeedState();
	if (World && !Record.FeedMap.IsEmpty() && Record.FeedMap == World->MapName())
	{
		FeedState = Record.Feed;
		auto Rebase = [this](FElysiumEntityHandle& H)
		{
			H = (H.IsSet() && World->Resolve(FElysiumEntityHandle(H.Index, World->GetEpoch())))
				? FElysiumEntityHandle(H.Index, World->GetEpoch())
				: FElysiumEntityHandle::Invalid();
		};
		Rebase(FeedState.Target);
		Rebase(FeedState.Peer);
		if (!FeedState.IsPaired())
		{
			FeedState = FElysiumFeedState();
		}
		else if (!FeedState.bVictim)
		{
			// The player record owns the feeder half, while the map snapshot owns its victim. Preserve
			// the absolute phase/pulse deadlines and re-arm the player think at their earlier boundary;
			// otherwise a correctly restored pair can remain inert behind ELYSIUM_NEVER_THINK.
			NextThink = FeedState.PhaseDeadline;
			if (FeedState.IsTransacting())
			{
				NextThink = FMath::Min(NextThink, FeedState.NextPulse);
			}
		}
	}
	// Cycle 9 — the discipline block. Its owned expiry events ride the map's own queue and its
	// tracked effects name entities in that map, so it resumes only into the map it was taken in.
	// Any other map takes the recovered world-transition teardown instead: the sheet arrived with
	// the `Active_*` slots set, and `ClearAll` is what zeroes them and removes the groups they
	// installed. Selection and the cast counter are map-independent and always cross.
	Disciplines = FElysiumDisciplineState();
	SelectedDiscipline = Record.SelectedDiscipline;
	SelectedTier = Record.SelectedTier;
	DisciplineCastCount = Record.DisciplineCastCount;
	const bool bSameDisciplineMap =
		World && !Record.DisciplineMap.IsEmpty() && Record.DisciplineMap == World->MapName();
	if (bSameDisciplineMap)
	{
		Disciplines = Record.Disciplines;
		for (FElysiumActiveDisciplineEffect& Effect : Disciplines.TargetEffects)
		{
			// A saved handle carries a dead epoch, exactly as one in the map snapshot does.
			Effect.Source = (Effect.Source.IsSet()
				&& World->Resolve(FElysiumEntityHandle(Effect.Source.Index, World->GetEpoch())))
				? FElysiumEntityHandle(Effect.Source.Index, World->GetEpoch())
				: FElysiumEntityHandle::Invalid();
		}
	}

	// Cycle 8 — the stealth block. It is ONE generation (`docs/vtmb/stealth.md`): the three light
	// samples, the rotation index and the derived scalars either all cross or none do, and a
	// restored triplet is never combined with newly defaulted derived values. It resumes only into
	// the map it was taken in, and for a stronger reason than the two blocks above — the samples
	// measure THAT map's light at THAT position, and the raw aggregate is the sum of the
	// `trigger_stealth_mod` volumes of that map the player was standing inside.
	Stealth.Reset();
	StealthModRaw = 0;
	Observer.Reset();
	PendingObserver.Reset();
	if (World && !Record.StealthMap.IsEmpty() && Record.StealthMap == World->MapName())
	{
		Stealth = Record.Stealth;
		StealthModRaw = Record.StealthModRaw;
	}

	// The names crossed the boundary; their resolution did not — the rulebook is re-read at load,
	// which is what lets a patched rulebook re-apply to a run that started before it. Going through
	// RefreshClanEffects rather than RebuildEffects reconciles the clan group with the clan slot
	// that just arrived, so a New Game into a different clan cannot keep the old one's bane.
	RefreshClanEffects();

	// The teardown runs AFTER the effect layer is rebuilt, so removing a group leaves a layer that
	// still matches the names on the character.
	if (!bSameDisciplineMap)
	{
		ElysiumDisciplines::ClearAll(*this);
	}
}

void FElysiumPlayer::Dehydrate(FElysiumPlayerRecord& Record) const
{
	Record.Sheet      = Sheet;
	Record.Money      = Money;
	Record.Health     = Health;
	Record.MaxHealth  = MaxHealth;
	Record.Law        = Law;
	Record.Police     = Police;   // Cycle 10b — unscoped; see Hydrate for why
	Record.ExperienceLog = ExperienceLog;
	Record.Effects    = Effects;
	Record.EmailFlags = EmailFlags;
	Record.ExperienceRemainder = ExperienceRemainder;
	Record.LifetimeExperience  = LifetimeExperience;
	Record.bUnkillable = bUnkillable;
	Record.Feed = FeedState;
	Record.FeedMap = (World && FeedState.IsPaired()) ? World->MapName() : FString();
	// Cycle 9 — the discipline block travels with the record, scoped to the map its owned expiry
	// events and tracked effects belong to.
	Record.Disciplines = Disciplines;
	Record.DisciplineMap = World ? World->MapName() : FString();
	Record.SelectedDiscipline = SelectedDiscipline;
	Record.SelectedTier = SelectedTier;
	Record.DisciplineCastCount = DisciplineCastCount;
	// Cycle 8 — the stealth group, whole and map-scoped. The observer snapshot is deliberately not
	// carried: it is published presentation state that the observers' own sight passes rebuild
	// within one cadence, and a restored one would name an entity of the previous world.
	Record.Stealth = Stealth;
	Record.StealthModRaw = StealthModRaw;
	Record.StealthMap = World ? World->MapName() : FString();
}

void FElysiumPlayer::SyncFromBody()
{
	const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	FVector Feet; FRotator View = FRotator::ZeroRotator;
	if (!Embodiment || !Embodiment->GetPlayerFeetTransform(Feet, View))
	{
		return;   // no pawn (a menu backdrop, a headless world): the entity keeps its last position
	}
	// Written straight into the fields: SetRuntimeOrigin would call OnRuntimeTransformChanged, which
	// teleports the pawn — every frame, to where it already is.
	Origin = Feet;
	Angles = ElysiumPlayerView::ToSource(View);
}

void FElysiumPlayer::OnRuntimeTransformChanged()
{
	// The skeletal surface is attached to the pawn, so moving the pawn carries it. Do not run
	// FElysiumAnimating::OnRuntimeTransformChanged, which would treat its relative transform as a
	// map-root world transform and double-apply the placement.
	FElysiumEntity::OnRuntimeTransformChanged();
	// CreateControllerNPC snapshots a scene-owned duplicate that RemoveControllerNPC later uses as
	// the player's final pose anchor. An explicit player transform (point_teleport, console teleport,
	// or script SetOrigin/SetAngles) is authoritative while that relationship exists; carry it onto
	// the duplicate so delayed teardown cannot restore the pre-teleport mark. Ordinary pawn movement
	// reaches SyncFromBody instead and deliberately leaves a scene-staged controller independent.
	if (FElysiumEntity* Controller = World ? World->FindPlayerController() : nullptr)
	{
		Controller->SetRuntimeTransform(Origin, Angles);
	}
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		Embodiment->TeleportPlayer(Origin, ElysiumPlayerView::ToUnreal(Angles));
	}
}

void FElysiumPlayer::OnRuntimeModelChanged()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment)
	{
		return; // a headless world still keeps the logical model string
	}
	Embodiment->ClearPlayerVisual();
	Visual = nullptr;
	if (!Model.IsEmpty())
	{
		Visual = Embodiment->BuildPlayerVisual(ModelStem(), Disposition, IdleVariant());
		if (Visual)
		{
			World->RegisterNpcBody(Visual);
			GateVisual();
		}
	}
}

void FElysiumPlayer::SetHiddenByController(bool bInHidden)
{
	bHiddenByController = bInHidden;
	GateVisual();
}

void FElysiumPlayer::GateVisual()
{
	// The pawn owns the surface. Publishing the entity's hide state and letting the pawn combine it
	// with the camera's eligibility is what keeps one flag from having two writers — the defect that
	// let a scene clip un-hide a body the camera had just put away.
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		Embodiment->SetPlayerBodyEntityHidden(IsInert() || bHiddenByController);
	}
}

void FElysiumPlayer::OnKilled()
{
	FElysiumCombatCharacter::OnKilled();
	// The run is lost. The session owns what that means to the application (11.3's GameOver state
	// holds the world and raises its screen); the substrate only reports it, and only through the
	// game-state subsystem it was already handed.
	if (UElysiumGameStateSubsystem* State = World ? World->GetGameState() : nullptr)
	{
		State->NotifyPlayerKilled();
	}
}

void FElysiumPlayer::InputGiveItem(const FElysiumInputArgs& Args)
{
	// STRING — the item's `vdata/items` key, 126 wires game-wide. `GiveItem` exists twice, as this
	// input and as a Character method, and the two need not share an implementation; both reach the
	// one player-only service, `GiveNamedItem`.
	const FString Classname = Args.Param.ToString();
	if (!Inventory.GiveNamedItem(*this, Classname).IsSet())
	{
		// Retail's own line for a grant that did not land.
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s Could not give item (\"%s\")"), *DebugString(), *Classname);
	}
}

void FElysiumPlayer::InputAwardExperience(const FElysiumInputArgs& Args)
{
	// STRING, not an amount: it names an entry the engine looks up in the experience table. The
	// handler takes the variant's string when the field type is STRING and otherwise stringifies
	// it, which a Hammer wire's string parameter satisfies either way.
	AwardExperience(Args.Param.ToString());
}

// ================================================================================================
// Cycle 10b hunk 4/5 — the three activity-level inputs, with their whole recovered semantics
// (`docs/vtmb/player-entity.md` § "Law, Masquerade and world response").
//
// Each takes an integer entity-input variant, clamps it to 0..5 and treats a wrong type or a
// negative value as zero. No authored wire in the corpus carries a second parameter and the one
// script call is `pc.SetCriminalLevel(1)`, so the input form never names a duration: it always
// passes `DeriveDuration`, and the finite `max(previously retained level, pl_min_act_timer)`
// deadline falls out of the rule. A native producer that DOES know its own duration (the feed
// pulse's two seconds) calls `ElysiumLaw::Set*Level` directly with it.
// ================================================================================================

void FElysiumPlayer::InputSetCriminalLevel(const FElysiumInputArgs& Args)
{
	ElysiumLaw::SetCriminalLevel(*this, ElysiumLaw::SanitizeLevel(Args.Param));
}

void FElysiumPlayer::InputSetInvestigateLevel(const FElysiumInputArgs& Args)
{
	ElysiumLaw::SetInvestigateLevel(*this, ElysiumLaw::SanitizeLevel(Args.Param));
}

void FElysiumPlayer::InputSetSupernaturalLevel(const FElysiumInputArgs& Args)
{
	ElysiumLaw::SetSupernaturalLevel(*this, ElysiumLaw::SanitizeLevel(Args.Param));
}

void FElysiumPlayer::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumCombatCharacter::GetDebugState(Out);
	Out.Emplace(TEXT("Origin"), Origin.ToString());
	Out.Emplace(TEXT("Facing"), FString::Printf(TEXT("yaw %.0f"), -Angles.Y));
	Out.Emplace(TEXT("Law"), FString::Printf(TEXT("criminal %d / supernatural %d / investigate %d"),
		Law.Criminal, Law.Supernatural, Law.Investigate));
	// Cycle 10b hunk 3/5 — the deadlines, the act counts and the response/pursuit state beside them.
	Out.Emplace(TEXT("Law deadlines"), FString::Printf(
		TEXT("criminal %.2f / supernatural %.2f (act counts %d / %d)"),
		Law.CriminalExpiry, Law.SupernaturalExpiry, Law.CriminalCount, Law.SupernaturalCount));
	Out.Emplace(TEXT("Police"), FString::Printf(
		TEXT("%s, cops %d, hunters %d, %s, masquerade window %.2f"),
		Police.bResponsePending
			? *FString::Printf(TEXT("response severity %d due %.2f"),
				Police.ResponseSeverity, Police.ResponseDeadline)
			: TEXT("no pending response"),
		Police.CopsInPursuit, Police.HuntersInPursuit,
		Police.bHeightenedAlert
			? *FString::Printf(TEXT("heightened alert until %.2f"), Police.HeightenedAlertExpiry)
			: TEXT("no alert"),
		Police.MasqueradeTimerNext));
	Out.Emplace(TEXT("World area"), World
		? FString::FromInt(ElysiumLaw::WorldAreaType(*World))
		: FString(TEXT("(no world)")));
	Out.Emplace(TEXT("XP"), FString::Printf(TEXT("%d spent-able, %d awards, %.0f raw (%.0f pending)"),
		Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience),
		ExperienceLog.Num(), LifetimeExperience, ExperienceRemainder));
	// Cycle 9 — the selection state the two cast verbs read.
	Out.Emplace(TEXT("Discipline selection"), SelectedDiscipline == INDEX_NONE
		? FString(TEXT("(none)"))
		: FString::Printf(TEXT("%s tier %d, %d cast(s)"),
			ElysiumDisciplines::InternalName(SelectedDiscipline), SelectedTier, DisciplineCastCount));
	Out.Emplace(TEXT("Body"), (World && World->Embodiment()) ? TEXT("pawn") : TEXT("(none)"));
}
