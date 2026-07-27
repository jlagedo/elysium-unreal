// 11.4 — the player entity and the two chain nodes above it (S3).
//
// `runtime-architecture.md` sections 5-6 is the design; `script_api.md` is the input inventory. The
// three classes here are ordinary registry nodes: nothing about the player is special-cased, which
// is the whole point — `pc.MoneyAdd(50)` from a level script, `MoneyAdd` on a Hammer wire and
// `elysium.ent_fire !player MoneyAdd 50` from the console are one input, reached by one R2 walk.
//
// What is deliberately NOT here: the economy (9.10), the vdata sheet and its meters (9.4),
// inventory and barter (9.8), disposition reactions (9.9), disciplines/frenzy (P13) and the
// look-at rig (P12). Their inputs register and log; the field they will write is already in place.

#include "ElysiumPlayer.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumRulebook.h"

#include "Components/SkeletalMeshComponent.h"
#include "Misc/Paths.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumPlayer, Log, All);

namespace
{
	// Indexed by the level-script encoding pc.clan uses: 2 = Brujah ... 8 = Ventrue.
	const TCHAR* GClanNames[] = { TEXT("?"), TEXT("?"), TEXT("Brujah"), TEXT("Gangrel"),
		TEXT("Malkavian"), TEXT("Nosferatu"), TEXT("Toreador"), TEXT("Tremere"), TEXT("Ventrue") };
	constexpr int32 GClanMin = 2;
	constexpr int32 GClanMax = 8;

	// Register a field backed by a subclass member (FElysiumClassDesc::Field only reaches
	// FElysiumEntity members). Mirrors AddSubclassField / AddNpcField / AddLogicField — file-unique
	// name so all of them can land in one unity blob.
	template <typename TClass, typename TMember>
	void AddCharField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, EElysiumField Flags = ElysiumFieldDefault)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(Flags);
		if constexpr (std::is_same_v<TMember, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<TMember, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddCharField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

	// One trait slot on FElysiumCombatCharacter::Sheet, as VtMB's datamap exposes it: the current
	// value under the bare name, the base under a `base_` prefix. Both halves are keyable and both
	// are saved, which is what the recovered datamap flags say.
	void AddSlotField(FElysiumClassDesc& D, const TCHAR* Name,
		EElysiumTraitContainer Container, int32 Slot, bool bBase)
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(ElysiumFieldDefault);
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [Container, Slot, bBase](const FElysiumEntity& E)
		{
			const FElysiumSheet& S = static_cast<const FElysiumCombatCharacter&>(E).Sheet;
			return FElysiumVariant::Int(bBase ? S.GetBase(Container, Slot) : S.GetCurrent(Container, Slot));
		};
		Acc.Set = [Container, Slot](FElysiumEntity& E, const FElysiumVariant& V)
		{
			// A write lands on the base either way: a keyvalue and a script assignment both set the
			// character sheet, and the current value is derived from it.
			static_cast<FElysiumCombatCharacter&>(E).Sheet.SetBase(Container, Slot, V.ToInt());
		};
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

	// Every compiled slot in every container, twice over. 74 slots -> 148 fields on
	// CBaseCombatCharacter, which is the whole sheet reachable through one R2 walk.
	void AddSheetFields(FElysiumClassDesc& D)
	{
		for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
		{
			const EElysiumTraitContainer Container = (EElysiumTraitContainer)i;
			for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
			{
				AddSlotField(D, Slot.Datamap, Container, Slot.Index, /*bBase=*/false);
				AddSlotField(D, *FString::Printf(TEXT("base_%s"), Slot.Datamap),
					Container, Slot.Index, /*bBase=*/true);
				if (Slot.Alias)
				{
					AddSlotField(D, Slot.Alias, Container, Slot.Index, /*bBase=*/false);
					AddSlotField(D, *FString::Printf(TEXT("base_%s"), Slot.Alias),
						Container, Slot.Index, /*bBase=*/true);
				}
			}
		}
	}

	// The same shape for FElysiumPlayer::Law, read-only (the SetCriminalLevel family writes it).
	void AddLawField(FElysiumClassDesc& D, const TCHAR* Name, int32 FElysiumLawState::* Member)
	{
		FElysiumFieldAccessor Acc;
		// Neither keyable nor saved: the setter is a deliberate no-op, and the counters' durable home
		// is FElysiumPlayerRecord::Law in the Player block, not the entity field walk.
		Acc.ApplyFlags(EElysiumField::None);
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [Member](const FElysiumEntity& E)
		{
			return FElysiumVariant::Int(static_cast<const FElysiumPlayer&>(E).Law.*Member);
		};
		Acc.Set = [](FElysiumEntity&, const FElysiumVariant&) {};
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}
}

// --- FElysiumSheet ---------------------------------------------------------------------------

bool FElysiumSheet::IsValidClan(int32 Clan)
{
	return Clan >= GClanMin && Clan <= GClanMax;
}

const TCHAR* FElysiumSheet::ClanName(int32 Clan)
{
	return IsValidClan(Clan) ? GClanNames[Clan] : TEXT("(unset)");
}

int32 FElysiumSheet::ClanFromName(const FString& Name)
{
	// A bare number is taken as the 2..8 encoding directly, so both forms work.
	if (Name.IsNumeric())
	{
		const int32 N = FCString::Atoi(*Name);
		return (N >= GClanMin && N <= GClanMax) ? N : 0;
	}
	for (int32 i = GClanMin; i <= GClanMax; ++i)
	{
		if (Name.Equals(GClanNames[i], ESearchCase::IgnoreCase))
		{
			return i;
		}
	}
	return 0;
}

// ============================================================================================
// FElysiumAnimating — CBaseAnimating
// ============================================================================================

FString FElysiumAnimating::ModelStem() const
{
	return FPaths::GetBaseFilename(Model).ToLower();
}

void FElysiumAnimating::BuildBody()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Def || Model.IsEmpty())
	{
		return;   // bare test world, no embodiment, or a bodiless character (npc_VCamera has no model)
	}

	// Source `angles` is [pitch yaw roll]; a standing character needs yaw only. The Source->Unreal Y
	// reflection negates yaw (`rebuild-strategy.md`).
	const FRotator Rot(0.0f, -Angles.Y, 0.0f);
	Visual = Embodiment->BuildNpcVisual(ModelStem(), Origin, Rot, Embodiment->BodyScaleFor(*Def),
		Disposition, IdleVariant());
	if (Visual)
	{
		World->RegisterNpcBody(Visual);
		if (IsInert())
		{
			GateVisual();   // born hidden (start_hidden / a Spawn()-time Kill)
		}
	}
}

bool FElysiumAnimating::PlayAnimClip(const FString& ClipName, bool bLoop, float* OutSeconds)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual || ClipName.IsEmpty())
	{
		return false;
	}
	return Embodiment->PlayNpcClip(Visual, ModelStem(), ClipName, bLoop, OutSeconds);
}

bool FElysiumAnimating::ResetAnimToIdle()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return false;
	}
	return Embodiment->RefreshNpcIdle(Visual, ModelStem(), Disposition, IdleVariant());
}

bool FElysiumAnimating::SetDispositionName(const FString& NewDisposition)
{
	if (NewDisposition.IsEmpty() || Disposition.Equals(NewDisposition, ESearchCase::IgnoreCase))
	{
		return true;   // already there — the write is answered, it just changes nothing
	}
	Disposition = NewDisposition;
	// 9.9 owns the emotional-state half; this is its animation half, and it is what makes the 2,467
	// `.dlg` column-4 SetDisposition actions visible on screen.
	ResetAnimToIdle();
	return true;
}

void FElysiumAnimating::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (Visual)
	{
		Visual->SetRelativeLocation(Origin);
		Visual->SetRelativeRotation(FRotator(0.0f, -Angles.Y, 0.0f));
	}
}

void FElysiumAnimating::OnRuntimeModelChanged()
{
	if (!World)
	{
		return;
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (!Embodiment)
	{
		return;   // bare test world — the logical Model field is still updated
	}
	if (Visual)
	{
		Visual->DestroyComponent();
		Visual = nullptr;
	}
	BuildBody();
}

void FElysiumAnimating::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	GateVisual();
}

void FElysiumAnimating::GateVisual()
{
	if (Visual)
	{
		const bool bShown = !IsInert();
		Visual->SetVisibility(bShown);
		Visual->SetComponentTickEnabled(bShown);   // pause the idle clip while hidden
	}
}

// ============================================================================================
// FElysiumCombatCharacter — CBaseCombatCharacter
// ============================================================================================

void FElysiumCombatCharacter::PendingInput(const TCHAR* Input, const TCHAR* Owner,
	const FElysiumInputArgs& Args) const
{
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s.%s(%s) — no backing system yet (%s)"),
		*DebugString(), Input, *Args.Param.Describe(), Owner);
}

void FElysiumCombatCharacter::InputMoneyAdd(const FElysiumInputArgs& Args)
{
	const int32 N = Args.Param.ToInt();
	if (N != 0) { Money += N; }
}

void FElysiumCombatCharacter::InputMoneyRemove(const FElysiumInputArgs& Args)
{
	const int32 N = Args.Param.ToInt();
	// The floor is the runtime's, not a recovered rule: VtMB's MoneyRemove is reached through the
	// barter/quest paths that check affordability first (9.10 owns those checks).
	if (N != 0) { Money = FMath::Max(0, Money - N); }
}

const FElysiumStatTable* FElysiumCombatCharacter::SheetRules() const
{
	UElysiumGameStateSubsystem* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->Stats() : nullptr;
}

void FElysiumCombatCharacter::AddTrait(EElysiumTraitContainer Container, int32 Slot, int32 Delta)
{
	Sheet.AddBase(Container, Slot, Delta);
	// The bounds are `stats.txt`'s own — Humanity 0..10, Masquerade 0..5, BloodPool 0..15 — applied
	// to the *current* value, which is what every reader goes through. The write-time gate VtMB's
	// `IncBase` carries (and `BumpStat`'s hardcoded `< 5` ceiling) is 9.4c's.
	Sheet.RecomputeCurrent(SheetRules());
}

void FElysiumCombatCharacter::InputHumanityAdd(const FElysiumInputArgs& Args)
{
	const int32 N = Args.Param.ToInt();
	if (N != 0) { AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::Humanity, N); }
}

void FElysiumCombatCharacter::InputChangeMasqueradeLevel(const FElysiumInputArgs& Args)
{
	const int32 N = Args.Param.ToInt();
	if (N != 0) { AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::Masquerade, N); }
	// The meter reaching 5 is the second loss condition (`game_runtime.md` section 3); 9.4c owns
	// the check and the HUD readout, so nothing watches this number yet.
}

void FElysiumCombatCharacter::InputBloodloss(const FElysiumInputArgs& Args)
{
	const int32 N = Args.Param.ToInt();
	if (N != 0) { AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool, -N); }
}

void FElysiumCombatCharacter::InputBloodgain(const FElysiumInputArgs& Args)
{
	// The ceiling is `BloodPool`'s authored Max, applied by the recompute; the per-generation
	// variant is a `Generation` lookup 9.4c owns.
	const int32 N = Args.Param.ToInt();
	if (N != 0) { AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool, N); }
}

void FElysiumCombatCharacter::InputBloodHeal(const FElysiumInputArgs& Args)
{
	// VtMB's internal name is BloodHealIn. Spending blood to heal is a `VampHeal_Info` conversion
	// (9.4c), so this only spends the blood it is told to; the health half does not land here.
	const int32 N = Args.Param.ToInt();
	if (N != 0) { AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool, -N); }
	PendingInput(TEXT("BloodHeal"), TEXT("9.4c — the health/blood conversion"), Args);
}

void FElysiumCombatCharacter::InputWillTalk(const FElysiumInputArgs& Args)
{
	bWillTalk = Args.Param.ToInt() != 0;
}

void FElysiumCombatCharacter::SyncHealthFromSheet()
{
	// `CBaseCombatCharacter::HealthToPercent` projects the sheet pair onto Source's engine-space
	// health (`game_runtime.md` section 3). Our `health` / `max_health` keyfields ARE that engine
	// space — what the save walk enumerates, what a `.ents` `health` key writes, and what the body
	// reads — so they are derived, never the truth.
	MaxHealth = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth);
	const int32 Damage = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Health);
	Health = FMath::Max(0, MaxHealth - Damage);
}

void FElysiumCombatCharacter::TakeDamage(float Amount)
{
	if (Amount <= 0.f || IsInert())
	{
		return;
	}
	if (MaxHealth <= 0)
	{
		// No health track: the rulebook did not load, or the character was built without a sheet.
		// Damage is recorded rather than applied — a character with no health model must not die of
		// arithmetic.
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s took %.1f damage with no health track"),
			*DebugString(), Amount);
		return;
	}
	const int32 Points = FMath::Max(1, FMath::RoundToInt(Amount));
	// Unkillable is the damage system's gate (`events_player`'s MakePlayerUnkillable), so it takes
	// the hit down to 1 hp rather than refusing it — retail keeps the flinch, only not the death.
	const int32 MaxDamage = bUnkillable ? MaxHealth - 1 : MaxHealth;
	const int32 Damage = FMath::Clamp(
		Sheet.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health) + Points, 0, MaxDamage);
	Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, Damage);
	Sheet.RecomputeCurrent(SheetRules());
	SyncHealthFromSheet();

	if (Health <= 0 && !bUnkillable)
	{
		OnKilled();
	}
}

void FElysiumCombatCharacter::OnKilled()
{
	if (bDeathReported)
	{
		return;
	}
	bDeathReported = true;
	static const FName OnDeath(TEXT("OnDeath"));
	FireOutput(OnDeath, Handle);   // one of CAI_BaseNPC's 16 outputs; the player wires none
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s died"), *DebugString());
}

bool FElysiumCombatCharacter::GetDynamicField(FName Name, FElysiumVariant& Out) const
{
	// Every compiled slot is a registered field, so the R2 walk has already answered by the time
	// this runs. What is left is a `base_*` name the shipped `stats.txt` does not own — which still
	// reads 0 rather than raising, the same default-on-miss `G` has, because the gates that ask
	// (`pc.base_Celerity > 0`) are written against a sheet where every name resolves.
	if (const int32* V = Sheet.Extra.Find(Name))
	{
		Out = FElysiumVariant::Int(*V);
		return true;
	}
	if (Name.ToString().StartsWith(TEXT("base_"), ESearchCase::CaseSensitive))
	{
		Out = FElysiumVariant::Int(0);
		return true;
	}
	return false;
}

bool FElysiumCombatCharacter::SetDynamicField(FName Name, const FElysiumVariant& Value)
{
	if (Sheet.Extra.Contains(Name) || Name.ToString().StartsWith(TEXT("base_"), ESearchCase::CaseSensitive))
	{
		Sheet.Extra.Add(Name, Value.ToInt());
		return true;
	}
	return false;
}

void FElysiumCombatCharacter::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	using EC = EElysiumTraitContainer;
	Out.Emplace(TEXT("Clan"), FString::Printf(TEXT("%d (%s), %s"), Sheet.Clan(),
		FElysiumSheet::ClanName(Sheet.Clan()), Sheet.IsMale() ? TEXT("male") : TEXT("female")));
	Out.Emplace(TEXT("Health"), FString::Printf(TEXT("%d / %d%s"), Health, MaxHealth,
		bUnkillable ? TEXT("  (unkillable)") : TEXT("")));
	Out.Emplace(TEXT("Money"), FString::FromInt(Money));
	Out.Emplace(TEXT("Blood"), FString::FromInt(Sheet.GetCurrent(EC::Attributes, ElysiumSlot::BloodPool)));
	Out.Emplace(TEXT("Humanity"), FString::FromInt(Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Humanity)));
	Out.Emplace(TEXT("Masquerade"), FString::FromInt(Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Masquerade)));
	Out.Emplace(TEXT("Physical"), FString::Printf(TEXT("str %d  dex %d  sta %d"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Strength),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Dexterity),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Stamina)));
	Out.Emplace(TEXT("WillTalk"), bWillTalk ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Disposition"), Disposition.IsEmpty() ? TEXT("(none)") : Disposition);
	Out.Emplace(TEXT("Unnamed stats"), FString::FromInt(Sheet.Extra.Num()));
}

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
	SyncHealthFromSheet();
	SyncFromBody();
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
	bUnkillable = Record.bUnkillable;
	bDeathReported = false;
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
	Record.bUnkillable = bUnkillable;
}

void FElysiumPlayer::SyncFromBody()
{
	const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	FVector Loc; float Yaw = 0.f;
	if (!Embodiment || !Embodiment->GetPlayerOrigin(Loc, Yaw))
	{
		return;   // no pawn (a menu backdrop, a headless world): the entity keeps its last position
	}
	// Written straight into the fields: SetRuntimeOrigin would call OnRuntimeTransformChanged, which
	// teleports the pawn — every frame, to where it already is.
	Origin = Loc;
	// The stored `angles` is Source-space [pitch yaw roll] like every other entity's, so the Unreal
	// yaw is negated on the way in and back out again (the Source->Unreal Y reflection).
	Angles.Y = -Yaw;
}

void FElysiumPlayer::OnRuntimeTransformChanged()
{
	// No FElysiumAnimating::OnRuntimeTransformChanged — the player has no spawned skeletal body; the
	// pawn is the body, and the embodiment owns the capsule compensation (Source places feet).
	FElysiumEntity::OnRuntimeTransformChanged();
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		Embodiment->TeleportPlayer(Origin, -Angles.Y);
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
	// STRING — the item's `vdata/items` key, 126 wires game-wide. 9.8 owns the item model; until it
	// lands the name is recorded so a beat that hands the player a weapon is visible in the log.
	PendingInput(TEXT("GiveItem"), TEXT("9.8 — inventory"), Args);
}

void FElysiumPlayer::InputAwardExperience(const FElysiumInputArgs& Args)
{
	// STRING, not an amount: it names an entry the engine looks up in the experience table
	// (`script_api.md`), so the ledger keeps the key and 9.4 resolves its value.
	FElysiumXpEntry Entry;
	Entry.Entry = Args.Param.ToString();
	if (!Entry.Entry.IsEmpty())
	{
		ExperienceLog.Add(MoveTemp(Entry));
	}
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s AwardExperience(\"%s\") — logged; the table lookup is 9.4"),
		*DebugString(), *Args.Param.ToString());
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
	Out.Emplace(TEXT("XP log"), FString::FromInt(ExperienceLog.Num()));
	Out.Emplace(TEXT("Body"), (World && World->Embodiment()) ? TEXT("pawn") : TEXT("(none)"));
}

// ============================================================================================
// Registration
// ============================================================================================

// One registered input that names itself in the log and does nothing else. The thunk is a
// captureless function pointer (FElysiumInputThunk), so the name and the owning task have to be
// baked into the lambda's body — hence the macro rather than a table.
#define ELYSIUM_PENDING_INPUT(Class, Name, OwnerText)                                  \
	D.Input(TEXT(#Name), [](FElysiumEntity& E, const FElysiumInputArgs& A)             \
		{ static_cast<Class&>(E).PendingInput(TEXT(#Name), TEXT(OwnerText), A); })

static TUniquePtr<FElysiumEntity> MakePlayer() { return MakeUnique<FElysiumPlayer>(); }

// CBaseAnimating — a chain node, never a `.ents` classname, so it needs no factory.
static FElysiumClassRegistrar GRegAnimating(
	ElysiumAnimatingClassName(), ElysiumBaseClassName(), nullptr,
	[](FElysiumClassDesc& D)
	{
		// `skin` is KEY and INPUT with a null inputFunc — the keyvalue, the wire and `.skin =` are
		// the same direct write, so the field alone serves all three (entity_io.md).
		AddCharField(D, TEXT("skin"), &FElysiumAnimating::Skin);
		AddCharField(D, TEXT("default_disposition"), &FElysiumAnimating::Disposition);

		D.Input(TEXT("SetAnimation"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{
				// VtMB's SetAnimation sets the model's *current* sequence rather than firing a
				// one-shot: the arguments the corpus passes are resting poses (`cower_idle`,
				// `dance0N`) that have to persist, so it loops.
				E.PlayAnimClip(A.Param.ToString(), /*bLoop=*/true);
			});
	});

// CBaseCombatCharacter — datamap 0x1061664c, 25 inputs (`script_api.md`).
static FElysiumClassRegistrar GRegCombatCharacter(
	ElysiumCombatCharacterClassName(), ElysiumAnimatingClassName(), nullptr,
	[](FElysiumClassDesc& D)
	{
		using FC = FElysiumCombatCharacter;

		// The eight with a field behind them.
		D.Input(TEXT("MoneyAdd"),              [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputMoneyAdd(A); });
		D.Input(TEXT("MoneyRemove"),           [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputMoneyRemove(A); });
		D.Input(TEXT("HumanityAdd"),           [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputHumanityAdd(A); });
		D.Input(TEXT("ChangeMasqueradeLevel"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputChangeMasqueradeLevel(A); });
		D.Input(TEXT("Bloodloss"),             [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputBloodloss(A); });
		D.Input(TEXT("Bloodgain"),             [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputBloodgain(A); });
		D.Input(TEXT("BloodHeal"),             [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputBloodHeal(A); });
		D.Input(TEXT("WillTalk"),              [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputWillTalk(A); });

		// The seventeen whose system has not landed. They register so the name resolves through the
		// R2 walk and reaches a defined place — fail-closed, not missing (roadmap 11.4). An input
		// thunk is a captureless function pointer, so each row states its own name and owner.
		ELYSIUM_PENDING_INPUT(FC, FrenzyTrigger,          "P13 — disciplines and frenzy");
		ELYSIUM_PENDING_INPUT(FC, FrenzyCheck,            "P13 — disciplines and frenzy");
		// HungerCheck shares FrenzyCheck's handler in VtMB — two external names, one behaviour.
		ELYSIUM_PENDING_INPUT(FC, HungerCheck,            "P13 — disciplines and frenzy");
		ELYSIUM_PENDING_INPUT(FC, FrenzyUpdate,           "P13 — disciplines and frenzy");
		ELYSIUM_PENDING_INPUT(FC, ClearActiveDisciplines, "P13 — disciplines");
		ELYSIUM_PENDING_INPUT(FC, Inventory_Remove,       "9.8 — inventory");
		ELYSIUM_PENDING_INPUT(FC, BarterBegin,            "9.8 — barter");
		ELYSIUM_PENDING_INPUT(FC, BarterEnd,              "9.8 — barter");
		ELYSIUM_PENDING_INPUT(FC, PlayFloat,              "8.9 — the floating HUD readout");
		ELYSIUM_PENDING_INPUT(FC, SetHeadAsCameraTarget,  "11.7 — the scripted-shot channel");
		ELYSIUM_PENDING_INPUT(FC, SetBodyAsCameraTarget,  "11.7 — the scripted-shot channel");
		ELYSIUM_PENDING_INPUT(FC, FadeHeadAsCameraTarget, "11.7 — the scripted-shot channel");
		ELYSIUM_PENDING_INPUT(FC, FadeBodyAsCameraTarget, "11.7 — the scripted-shot channel");
		ELYSIUM_PENDING_INPUT(FC, LookAtEntityEye,        "12.4 — the look-at rig");
		ELYSIUM_PENDING_INPUT(FC, LookAtEntityCenter,     "12.4 — the look-at rig");
		ELYSIUM_PENDING_INPUT(FC, LookAtEntityOrigin,     "12.4 — the look-at rig");
		ELYSIUM_PENDING_INPUT(FC, LookAtEntityDefault,    "12.4 — the look-at rig");

		// `money` is `m_iMoney`, the one counter `stats.txt` does not carry as a Stat. Humanity,
		// blood, masquerade, clan and sex are all trait slots, and arrive with the rest of the sheet.
		AddCharField(D, TEXT("money"), &FC::Money);
		AddSheetFields(D);
	});

// The player. Its classname is VtMB's own (`player`); nothing in a `.ents` file carries it, because
// the player is created by the engine at map build, not authored into the map.
static FElysiumClassRegistrar GRegPlayer(
	ElysiumPlayerClassName(), ElysiumCombatCharacterClassName(), &MakePlayer,
	[](FElysiumClassDesc& D)
	{
		using FP = FElysiumPlayer;

		D.Input(TEXT("GiveItem"),             [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FP&>(E).InputGiveItem(A); });
		D.Input(TEXT("AwardExperience"),      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FP&>(E).InputAwardExperience(A); });
		D.Input(TEXT("SetCriminalLevel"),     [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FP&>(E).InputSetCriminalLevel(A); });
		D.Input(TEXT("SetInvestigateLevel"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FP&>(E).InputSetInvestigateLevel(A); });
		D.Input(TEXT("SetSupernaturalLevel"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FP&>(E).InputSetSupernaturalLevel(A); });

		// The rest of the recovered ten: no system yet. (The datamap header states 11 inputs and
		// only 10 were recovered from the builder dump — the eleventh is still unidentified,
		// `script_api.md`.)
		D.Input(TEXT("Whisper"),         [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FP&>(E).PendingInput(TEXT("Whisper"), TEXT("9.2 — dialogue line audio"), A); });
		// RemoveCamera — the other half of `SetCamera`: hand the view back to the player. It clears the
		// map's one scripted camera whether a script, a wire or the theatre put it up (11.7).
		D.Input(TEXT("RemoveCamera"),    [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ if (E.World) { E.World->ClearScriptedCamera(); } });
		D.Input(TEXT("PlayHUDParticle"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FP&>(E).PendingInput(TEXT("PlayHUDParticle"), TEXT("8.9 — the HUD"), A); });
		D.Input(TEXT("StopHUDParticle"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FP&>(E).PendingInput(TEXT("StopHUDParticle"), TEXT("8.9 — the HUD"), A); });
		D.Input(TEXT("Holster"),         [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FP&>(E).PendingInput(TEXT("Holster"), TEXT("9.8 — equipped weapons"), A); });

		// The three law counters as read-only fields, so `pc.criminal_level` reads a number. They
		// are engine-written (the inputs above are the only writers), which is what !bKeyable says.
		AddLawField(D, TEXT("criminal_level"),     &FElysiumLawState::Criminal);
		AddLawField(D, TEXT("supernatural_level"), &FElysiumLawState::Supernatural);
		AddLawField(D, TEXT("investigate_level"),  &FElysiumLawState::Investigate);
	});

#undef ELYSIUM_PENDING_INPUT
