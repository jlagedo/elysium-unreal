// 11.4 — CBasePlayer / CHL2_Player: the player leaf, its recovered datamap inputs, the XP ledger
// and the session record's hydrate/dehydrate pair (S3).
//
// Moved verbatim out of `ElysiumPlayerClasses.cpp` when the discipline runtime landed; the file
// granularity rule in `Source/ElysiumUE/CLAUDE.md` puts one primary class per `.cpp`. The public
// declaration stays the chain header `Public/ElysiumPlayer.h`, and the class registration stays at
// the one registration site, `ElysiumPlayerClasses.cpp`.

#include "ElysiumPlayer.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumPlayerLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"

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
}

void FElysiumPlayer::Think()
{
	// The player's only autonomous work today is the feed transaction. It runs here rather than off
	// a timer because the pulse deadline is simulation state (R4/S8): the same think that advances
	// it is the one the save's clock restores, so a load cannot duplicate or skip a pulse.
	TickFeed(World ? World->NowSeconds() : 0.0);

	// SEAM: the footstep hearing stimulus belongs here, and there is nothing to hang it on yet.
	// `sound_volume_table.txt` names `PLAYER_FOOTSTEP_SNEAK`/`_WALK`/`_RUN` plus `PLAYER_JUMP` and
	// the two landings, and separates walking from running by its own `PLAYER_RUN_SPEED` (128
	// units/s) — but this runtime raises no step EVENT at all: no animation notify, no movement
	// callback, nothing on the locomotion sample that says "a foot just landed". Inventing a cadence
	// here would be substrate arithmetic standing in for a producer, so the step stays unemitted
	// until the locomotion/stealth work supplies one. Not warned: nothing failed.
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

void FElysiumPlayer::OnMasqueradeBreached()
{
	FElysiumCombatCharacter::OnMasqueradeBreached();
	// The second loss condition. Same shape as death: the substrate reports, and the session owns
	// what it means to the application (11.3's GameOver state, with its own reason).
	if (UElysiumGameStateSubsystem* State = World ? World->GetGameState() : nullptr)
	{
		State->NotifyMasqueradeBreach();
	}
}

void FElysiumPlayer::Hydrate(const FElysiumPlayerRecord& Record)
{
	Sheet      = Record.Sheet;
	Money      = Record.Money;
	Health     = Record.Health;
	MaxHealth  = Record.MaxHealth;
	Law        = Record.Law;
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
	// The names crossed the boundary; their resolution did not — the rulebook is re-read at load,
	// which is what lets a patched rulebook re-apply to a run that started before it. Going through
	// RefreshClanEffects rather than RebuildEffects reconciles the clan group with the clan slot
	// that just arrived, so a New Game into a different clan cannot keep the old one's bane.
	RefreshClanEffects();
}

void FElysiumPlayer::Dehydrate(FElysiumPlayerRecord& Record) const
{
	Record.Sheet      = Sheet;
	Record.Money      = Money;
	Record.Health     = Health;
	Record.MaxHealth  = MaxHealth;
	Record.Law        = Law;
	Record.ExperienceLog = ExperienceLog;
	Record.Effects    = Effects;
	Record.EmailFlags = EmailFlags;
	Record.ExperienceRemainder = ExperienceRemainder;
	Record.LifetimeExperience  = LifetimeExperience;
	Record.bUnkillable = bUnkillable;
	Record.Feed = FeedState;
	Record.FeedMap = (World && FeedState.IsPaired()) ? World->MapName() : FString();
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

void FElysiumPlayer::GateVisual()
{
	// The pawn owns the surface. Publishing the entity's hide state and letting the pawn combine it
	// with the camera's eligibility is what keeps one flag from having two writers — the defect that
	// let a scene clip un-hide a body the camera had just put away.
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		Embodiment->SetPlayerBodyEntityHidden(IsInert());
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

void FElysiumPlayer::InputSetCriminalLevel(const FElysiumInputArgs& Args)
{
	Law.Criminal = Args.Param.ToInt();
}

void FElysiumPlayer::InputSetInvestigateLevel(const FElysiumInputArgs& Args)
{
	Law.Investigate = Args.Param.ToInt();
}

void FElysiumPlayer::InputSetSupernaturalLevel(const FElysiumInputArgs& Args)
{
	Law.Supernatural = Args.Param.ToInt();
}

void FElysiumPlayer::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumCombatCharacter::GetDebugState(Out);
	Out.Emplace(TEXT("Origin"), Origin.ToString());
	Out.Emplace(TEXT("Facing"), FString::Printf(TEXT("yaw %.0f"), -Angles.Y));
	Out.Emplace(TEXT("Law"), FString::Printf(TEXT("criminal %d / supernatural %d / investigate %d"),
		Law.Criminal, Law.Supernatural, Law.Investigate));
	Out.Emplace(TEXT("XP"), FString::Printf(TEXT("%d spent-able, %d awards, %.0f raw (%.0f pending)"),
		Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience),
		ExperienceLog.Num(), LifetimeExperience, ExperienceRemainder));
	Out.Emplace(TEXT("Body"), (World && World->Embodiment()) ? TEXT("pawn") : TEXT("(none)"));
}
