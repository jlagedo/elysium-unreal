// 13.3 — the weapon controller over the 9.8 item entity and the 13.3 damage spine.
//
// `docs/vtmb/combat-and-damage.md` is the specification and owns every fact below; the seam layout
// is `docs/architecture/gameplay-systems-architecture.md` §5.4. Nothing here re-implements the
// descriptor, the soak resolver or the health commit: a weapon decides lethality, defense and the
// opposed margin, and hands the result to `FElysiumCombatCharacter::TakeDamage`, which is the one
// typed route into `ElysiumDamage::Apply` and `CommitDamage`.

#include "Substrate/ElysiumWeaponClasses.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumVariant.h"
#include "ElysiumWieldTable.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumDiceTables.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumStealth.h"
#include "Visual/ElysiumNpcVisual.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumWeapon, Log, All);

// ================================================================================================
// The logical activities the controller requests
// ================================================================================================
//
// `CWeaponMelee::PrimaryAttack` requests `ACT_MELEE_ATTACK`, its secondary requests
// `ACT_MELEE_ATTACK_HEAVY`, and `RequestActivity`'s one live substitution promotes the first to
// `ACT_MELEE_ATTACK_2COMBO`. The ordinary ranged selector realizes `ACT_RANGE_ATTACK1_LAYER`, which
// the weapon activity table then translates per family. The translation is the embodiment's — the
// substrate names the LOGICAL activity and nothing else.

namespace
{
	const TCHAR* const GActMeleeAttack       = TEXT("ACT_MELEE_ATTACK");
	const TCHAR* const GActMeleeAttack2Combo = TEXT("ACT_MELEE_ATTACK_2COMBO");
	const TCHAR* const GActMeleeAttackHeavy  = TEXT("ACT_MELEE_ATTACK_HEAVY");
	const TCHAR* const GActRangeAttackLayer  = TEXT("ACT_RANGE_ATTACK1_LAYER");
	const TCHAR* const GActRangeDryFire      = TEXT("ACT_RANGE_DRYFIRE_LAYER");
	const TCHAR* const GActReloadLayer       = TEXT("ACT_RELOAD_LAYER");

	// The two `rules.txt` blocks the weapon transactions read.
	const TCHAR* const GDamageInfoBlock    = TEXT("Damage_Info");
	const TCHAR* const GMeleeReactionBlock = TEXT("Melee_Reactions");

	// The feat every defender rolls to reduce an incoming attack.
	const TCHAR* const GDefensiveManeuvers = TEXT("Defensive_Maneuvers");

	// The `Dmg` grammar's two close-combat attack feats. Retail selects the controlling combo
	// Ability through a virtual that only `CWeaponMelee_Fists` overrides; the record-side signal for
	// the same distinction is which close-combat feat the mode's own `Dmg` names, which is the read
	// this runtime uses so the choice stays data-driven (K-rule: never the classname prefix).
	const TCHAR* const GFeatCloseCombatBrawl = TEXT("Close_Combat_Brawl");

	// A one-shot report keyed by an arbitrary string, so an unrecovered authored case reports once
	// per distinct case rather than once per press.
	bool ShouldReportOnce(const FString& Key)
	{
		static TSet<FString> Reported;
		if (Reported.Contains(Key))
		{
			return false;
		}
		Reported.Add(Key);
		return true;
	}

	// A live opponent for combat purposes. Death is the RPG comparison, not entity destruction: a
	// killed character stays a real entity until the world reaps it, so `HasReportedDeath` is what
	// separates it from a valid target. It is the weapon-side half of the alive-path prefilter —
	// retail's own commit refuses a victim whose life state is no longer alive (`combat-and-damage.md`
	// § RE40 -> Alive-Path Filtering and Rounding).
	bool IsAliveForCombat(const FElysiumCombatCharacter& Char)
	{
		return !Char.IsInert() && !Char.HasReportedDeath();
	}

	bool IsPlayerSide(const FElysiumCombatCharacter& Char)
	{
		return Char.World != nullptr && Char.World->PlayerHandle() == Char.Handle;
	}

	// One feat roll, returning `max(successes - botches, 0)`. Every absence fails safe at zero and
	// says so — a defence that cannot be rolled must not silently read as a perfect defence or as
	// none at all without a line saying which.
	int32 RollFeatNet(FElysiumCombatCharacter& Roller, const TCHAR* FeatName, int32 Difficulty,
		const FElysiumWeaponContext& Context)
	{
		const FElysiumFeat* Feat = Context.Feats ? Context.Feats->Find(FeatName) : nullptr;
		if (!Feat)
		{
			if (ShouldReportOnce(FString::Printf(TEXT("feat:%s"), FeatName)))
			{
				UE_LOG(LogElysiumWeapon, Warning,
					TEXT("%s: feat '%s' does not resolve (rulebook %s) — it rolls nothing"),
					*Roller.DebugString(), FeatName, Context.Feats ? TEXT("loaded") : TEXT("absent"));
			}
			return 0;
		}
		const int32 Pool = ElysiumFeats::FeatValue(*Feat, Roller.Sheet, Roller.SheetEffects(), &Roller);
		const FElysiumDiceTable& Weighting = Context.DiceTables
			? Context.DiceTables->ForFeat(*Feat, !IsPlayerSide(Roller))
			: FElysiumDiceTable::Uniform();
		const FElysiumRollResult Roll = ElysiumDice::Roll(Pool, Difficulty, Weighting);
		return FMath::Max(Roll.Successes - Roll.Botches, 0);
	}

	// The rating a feat name evaluates to on a character — a rating, never a roll (K5).
	int32 FeatRating(const FElysiumCombatCharacter& Char, const FString& FeatName,
		const FElysiumWeaponContext& Context)
	{
		if (FeatName.IsEmpty() || Context.Feats == nullptr)
		{
			return 0;
		}
		const FElysiumFeat* Feat = Context.Feats->Find(FeatName);
		return Feat ? ElysiumFeats::FeatValue(*Feat, Char.Sheet, Char.SheetEffects(), &Char) : 0;
	}
}

// ================================================================================================
// The `rules.txt` margins
// ================================================================================================

FElysiumMeleeMargins FElysiumMeleeMargins::FromRules(const FElysiumRules& Rules)
{
	FElysiumMeleeMargins Out;
	static const TCHAR* const Keys[] =
	{
		TEXT("SuccessesForAttackerBlockedMajor"),
		TEXT("SuccessesForAttackerBlocked"),
		TEXT("SuccessesForDefenderDodgeAttack"),
		TEXT("SuccessesForDefenderDodge"),
		TEXT("SuccessesForDefenderBlock"),
		TEXT("SuccessesForDefenderBlockStagger"),
	};
	for (const TCHAR* Key : Keys)
	{
		if (!Rules.Has(GMeleeReactionBlock, Key))
		{
			return Out;   // invalid; the classifier reports rather than inventing bands
		}
	}
	Out.AttackerBlockedMajor  = Rules.Int(GMeleeReactionBlock, Keys[0]);
	Out.AttackerBlocked       = Rules.Int(GMeleeReactionBlock, Keys[1]);
	Out.DefenderDodgeAttack   = Rules.Int(GMeleeReactionBlock, Keys[2]);
	Out.DefenderDodge         = Rules.Int(GMeleeReactionBlock, Keys[3]);
	Out.DefenderBlock         = Rules.Int(GMeleeReactionBlock, Keys[4]);
	Out.DefenderBlockStagger  = Rules.Int(GMeleeReactionBlock, Keys[5]);
	Out.bValid = true;
	return Out;
}

FElysiumWeaponContext FElysiumWeaponContext::FromCharacter(const FElysiumCombatCharacter& Char)
{
	FElysiumWeaponContext Context;
	UElysiumGameStateSubsystem* GameState = Char.World ? Char.World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rulebook = GameState ? GameState->Rulebook() : nullptr;
	if (!Rulebook)
	{
		return Context;   // headless / bare world: every consumer below fails safe and says so
	}
	Context.Feats = &Rulebook->Feats();
	Context.DiceTables = &Rulebook->Dice();

	const FElysiumRules& Rules = Rulebook->Rules();
	if (Rules.Has(GDamageInfoBlock, TEXT("Defense_Difficulty_PC"))
		&& Rules.Has(GDamageInfoBlock, TEXT("Defense_Difficulty_NPC")))
	{
		Context.DefenseDifficultyPc = Rules.Int(GDamageInfoBlock, TEXT("Defense_Difficulty_PC"));
		Context.DefenseDifficultyNpc = Rules.Int(GDamageInfoBlock, TEXT("Defense_Difficulty_NPC"));
	}
	if (Rules.Has(GDamageInfoBlock, TEXT("Soak_Difficulty_PC"))
		&& Rules.Has(GDamageInfoBlock, TEXT("Soak_Difficulty_NPC")))
	{
		Context.SoakDifficultyPc = Rules.Int(GDamageInfoBlock, TEXT("Soak_Difficulty_PC"));
		Context.SoakDifficultyNpc = Rules.Int(GDamageInfoBlock, TEXT("Soak_Difficulty_NPC"));
	}
	Context.Margins = FElysiumMeleeMargins::FromRules(Rules);
	return Context;
}

// ================================================================================================
// The pure rules
// ================================================================================================

namespace ElysiumWeapons
{
	int32 ComboChancePercent(int32 BaseRank)
	{
		// Recovered from `vampire.dll` (`combat-and-damage.md` § "The combo is automatic, not
		// directional"). Ranks outside the table clamp to its ends.
		static constexpr int32 Chances[] = { 0, 10, 25, 45, 70, 100 };
		const int32 Clamped = FMath::Clamp(BaseRank, 0, (int32)UE_ARRAY_COUNT(Chances) - 1);
		return Chances[Clamped];
	}

	float MeleePlaybackRate(int32 AttackFeatRank)
	{
		return 0.70f + 0.03f * static_cast<float>(FMath::Max(AttackFeatRank, 0));
	}

	float AttackSpeedScale(const FElysiumCombatCharacter& /*Owner*/)
	{
		// SEAM — retail scales both the ranged `Attack_Rate` schedule and the melee playback rate by
		// the character's attack-speed value. No shipped `stats.txt` trait, `feats.txt` feat or
		// `traiteffect.txt` flag names one, so there is nothing to read and the scale is 1.0. A
		// discipline or template that changes attack speed joins here.
		return 1.0f;
	}

	int32 ActivePotenceRank(const FElysiumCombatCharacter& Attacker)
	{
		// The melee commit floors `DamageInflicted` up to the ACTIVE Potence rank. The rank is the
		// `Active_Potence` sheet slot, which the discipline runtime writes and clears; a character
		// with the power inactive reads 0 and the floor changes no number.
		return ElysiumDisciplines::ActiveRank(Attacker, ElysiumDisciplines::Potence);
	}

	int32 RangedRemainingLethality(int32 InTotalLethality, bool bKindredVictim, int32 DefenseNet)
	{
		const int32 Clamped = FMath::Max(InTotalLethality, 1);
		return bKindredVictim ? FMath::Max(Clamped - FMath::Max(DefenseNet, 0), 0) : Clamped;
	}

	int32 MeleeDamageTotal(int32 DamageInflicted, int32 BaseDamage, int32 DamageModifier,
		float Multiplier)
	{
		// Retail's own diagnostic string and the body agree on the shape; RE40 names `DmgModifier` as
		// the attacker's close-combat feat rating. The product is float before it is spent, and the
		// conversion is `__ftol` — truncation toward zero, never a round.
		const float Total = static_cast<float>(DamageInflicted)
			* static_cast<float>(BaseDamage + DamageModifier) * Multiplier;
		return FMath::TruncToInt(Total);
	}

	int32 RangedDamageTotal(int32 RemainingLethality, int32 BaseDamage, float Multiplier)
	{
		const float Total =
			static_cast<float>(RemainingLethality) * static_cast<float>(BaseDamage) * Multiplier;
		return FMath::TruncToInt(Total);
	}

	float VolleyFraction(int32 RaysOnVictim, int32 RaysFired)
	{
		if (RaysFired <= 0)
		{
			// A mode that places no rays produces no volley to share out. This is arithmetic safety,
			// not a recovered case: `Ammo_Fired` defaults to one ray at load.
			return 0.0f;
		}
		return static_cast<float>(FMath::Clamp(RaysOnVictim, 0, RaysFired))
			/ static_cast<float>(RaysFired);
	}

	EElysiumMeleeAttackerReaction ClassifyAttacker(const FElysiumMeleeMargins& Margins, int32 Margin)
	{
		if (!Margins.bValid)
		{
			return EElysiumMeleeAttackerReaction::Unclassified;
		}
		if (Margin <= Margins.AttackerBlockedMajor) { return EElysiumMeleeAttackerReaction::BlockedMajor; }
		if (Margin <= Margins.AttackerBlocked)      { return EElysiumMeleeAttackerReaction::Blocked; }
		return EElysiumMeleeAttackerReaction::Hit;
	}

	EElysiumMeleeDefenderReaction ClassifyDefender(const FElysiumMeleeMargins& Margins, int32 Margin)
	{
		if (!Margins.bValid)
		{
			return EElysiumMeleeDefenderReaction::Unclassified;
		}
		if (Margin <= Margins.DefenderDodgeAttack)  { return EElysiumMeleeDefenderReaction::DodgeAttack; }
		if (Margin <= Margins.DefenderDodge)        { return EElysiumMeleeDefenderReaction::Dodge; }
		if (Margin <= Margins.DefenderBlock)        { return EElysiumMeleeDefenderReaction::Block; }
		if (Margin <= Margins.DefenderBlockStagger) { return EElysiumMeleeDefenderReaction::BlockStagger; }
		return EElysiumMeleeDefenderReaction::HitKnockback;
	}

	const TCHAR* AttackerReactionName(EElysiumMeleeAttackerReaction Reaction)
	{
		switch (Reaction)
		{
		case EElysiumMeleeAttackerReaction::Hit:          return TEXT("hit");
		case EElysiumMeleeAttackerReaction::Blocked:      return TEXT("blocked");
		case EElysiumMeleeAttackerReaction::BlockedMajor: return TEXT("blocked major");
		default:                                          return TEXT("unclassified");
		}
	}

	const TCHAR* DefenderReactionName(EElysiumMeleeDefenderReaction Reaction)
	{
		switch (Reaction)
		{
		case EElysiumMeleeDefenderReaction::DodgeAttack:  return TEXT("dodge attack");
		case EElysiumMeleeDefenderReaction::Dodge:        return TEXT("dodge");
		case EElysiumMeleeDefenderReaction::Block:        return TEXT("block");
		case EElysiumMeleeDefenderReaction::BlockStagger: return TEXT("block stagger");
		case EElysiumMeleeDefenderReaction::HitKnockback: return TEXT("hit / knockback");
		default:                                          return TEXT("unclassified");
		}
	}

	EOperatorBody OperatorBodyFor(EElysiumItemType Type)
	{
		switch (Type)
		{
		case EElysiumItemType::WeaponMelee:   return EOperatorBody::Melee;
		case EElysiumItemType::WeaponFirearm: return EOperatorBody::Ranged;
		// A thrown weapon takes the base body with the 17 discipline/armor/unarmed classes, which
		// accepts nothing. Its commit stays on the `ContactEventCycle` estimate, which is not a
		// degraded path for it — retail names no event that would commit one.
		default:                              return EOperatorBody::None;
		}
	}

	const TCHAR* OperatorBodyName(EOperatorBody Body)
	{
		switch (Body)
		{
		case EOperatorBody::Ranged: return TEXT("ranged");
		case EOperatorBody::Melee:  return TEXT("melee");
		default:                    return TEXT("no accepted route");
		}
	}

	bool IsCommitEvent(int32 Event, EOperatorBody Body)
	{
		switch (Body)
		{
		case EOperatorBody::Ranged:
			return Event >= RangedShotEventFirst && Event <= RangedShotEventLast;
		case EOperatorBody::Melee:
			return Event == MeleeContactEvent;
		default:
			return false;
		}
	}

	bool IsSwallowedMeleeEvent(int32 Event)
	{
		// `0x103ea5b0`'s own swallow set, exactly: the two body/swish ids and the ranged shot ids a
		// melee clip may still carry. Nothing acts on them, and reaching the census with them would
		// put a recovered no-op on the work list.
		return Event == 3001 || Event == 3003 || (Event >= 3030 && Event <= 3037);
	}

	bool TimelineHasCommitEvent(const TArray<FElysiumAnimEvent>& Timeline, EOperatorBody Body)
	{
		for (const FElysiumAnimEvent& Record : Timeline)
		{
			if (IsCommitEvent(Record.Event, Body))
			{
				return true;
			}
		}
		return false;
	}

	TUniquePtr<FElysiumEntity> MakeWeapon() { return MakeUnique<FElysiumWeapon>(); }
}

const TCHAR* FElysiumWeapon::VerdictName(EVerdict Verdict)
{
	switch (Verdict)
	{
	case EVerdict::Accepted:    return TEXT("accepted");
	case EVerdict::DryFire:     return TEXT("dry fire");
	case EVerdict::NotReady:    return TEXT("not ready");
	case EVerdict::Reloading:   return TEXT("reloading");
	case EVerdict::ModeToggled: return TEXT("mode toggled");
	case EVerdict::ZoomCycled:  return TEXT("zoom cycled");
	case EVerdict::NoMode:      return TEXT("no mode");
	case EVerdict::NoOwner:     return TEXT("no owner");
	default:                    return TEXT("unsupported");
	}
}

// ================================================================================================
// Lifecycle
// ================================================================================================

namespace
{
	// The equip funnel: put the wield table's answer for (classname, wielder sex) in the wielder's
	// hand, or take away whatever it was holding. This is the one door a real equip/holster
	// transaction reaches the attachment the green room's `gr_wield` lane proves
	// (`docs/vtmb/wielded_weapons.md` §2, `docs/architecture/wielded-weapon-integration.md` —
	// "Player and NPC"). One path serves both: sex is read off `Wearer.Sheet.IsMale()`, which every
	// combat character carries, so there is no player-only branch.
	void ApplyWieldVisual(const FElysiumWeapon& Weapon, FElysiumCombatCharacter& Wearer)
	{
		USkeletalMeshComponent* const Body = Wearer.GetSkeletalBody();
		if (Body == nullptr)
		{
			// A bodiless wearer — a headless substrate fixture, or `elysium.NpcBodies 0`. Nothing to
			// attach geometry to; the equip transaction still completes. Named at Verbose rather than
			// silently, because a body built OUTSIDE the wearer's own embodiment call (a debug lab
			// attaching straight to a pawn) reads identically to this ordinary case from here, and the
			// two are easy to conflate without a line naming which one happened.
			UE_LOG(LogElysiumWeapon, Verbose,
				TEXT("%s equipped by %s: no skeletal body to attach the wield model to — deferred"),
				*Weapon.DebugString(), *Wearer.DebugString());
			return;
		}

		const FElysiumWieldModelRef* Ref = nullptr;
		const EElysiumWieldResult Result = UElysiumWieldTable::FindRow(
			FName(*Weapon.ClassName()), /*bFemale*/ !Wearer.Sheet.IsMale(), Ref);

		switch (Result)
		{
		case EElysiumWieldResult::Found:
			// A load miss here is a bake that did not produce a package its own table references;
			// InstallWieldModel names it.
			ElysiumNpcVisual::InstallWieldModel(Body, *Ref, Weapon.ClassName());
			break;
		case EElysiumWieldResult::NoGeometry:
		case EElysiumWieldResult::WorldModel:
			// The corpus's ordinary answer (`w_null.mdl` / no wield model) or the `shows_view_model`
			// gate: either way the hand draws nothing, which is an authored answer, not a missing asset.
			ElysiumNpcVisual::ClearWieldModel(Body);
			break;
		case EElysiumWieldResult::UnknownItem:
			UE_LOG(LogElysiumWeapon, Warning,
				TEXT("%s equipped by %s names no row in the wield table for '%s'"),
				*Weapon.DebugString(), *Wearer.DebugString(), *Weapon.ClassName());
			ElysiumNpcVisual::ClearWieldModel(Body);
			break;
		case EElysiumWieldResult::NoTable:
			// UElysiumWieldTable::Load already warned once that the wield bake has not run.
			break;
		case EElysiumWieldResult::MeshMissing:
		case EElysiumWieldResult::NoWearer:
			// Not FindRow's vocabulary (`ElysiumWieldTable.h`) — it never answers either from this call.
			break;
		}
	}
}

void FElysiumWeapon::Spawn()
{
	FElysiumItem::Spawn();

	ModeDamage.Reset();
	PrimaryModeSlots.Reset();
	PrimaryModeIndex = INDEX_NONE;
	SecondaryModeIndex = INDEX_NONE;

	const FElysiumItemDef* Record = Data();
	if (!Record)
	{
		if (!IsRecordOnly())
		{
			UE_LOG(LogElysiumWeapon, Warning,
				TEXT("%s registered as a weapon but has no vdata/items record — it has no modes"),
				*DebugString());
		}
		return;
	}

	// Parse each mode's authored `Dmg` ONCE, here, rather than on every swing: the grammar is data
	// and the descriptor it produces is immutable for the entity's life.
	ModeDamage.Reserve(Record->Modes.Num());
	for (int32 i = 0; i < Record->Modes.Num(); ++i)
	{
		const FElysiumWeaponMode& Mode = Record->Modes[i];
		ModeDamage.Add(Mode.Dmg.IsEmpty() ? FElysiumDmg() : ElysiumDamage::ParseDmg(Mode.Dmg));

		if (Mode.Tag.StartsWith(TEXT("Primary"), ESearchCase::IgnoreCase) && Mode.IsAttack())
		{
			PrimaryModeSlots.Add(i);
		}
		else if (Mode.Tag.Equals(TEXT("Secondary"), ESearchCase::IgnoreCase)
			&& SecondaryModeIndex == INDEX_NONE)
		{
			SecondaryModeIndex = i;
		}
	}
	// A record that tags nothing `Primary` still has a primary press: the first attack mode is it.
	if (PrimaryModeSlots.IsEmpty())
	{
		for (int32 i = 0; i < Record->Modes.Num(); ++i)
		{
			if (Record->Modes[i].IsAttack())
			{
				PrimaryModeSlots.Add(i);
				break;
			}
		}
	}
	PrimaryModeIndex = PrimaryModeSlots.IsEmpty() ? INDEX_NONE : PrimaryModeSlots[0];

	if (Record->Modes.IsEmpty())
	{
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s ('%s') is a weapon-family item whose record authors no Activation block — it "
				"cannot attack"),
			*DebugString(), *Record->Classname);
	}
}

void FElysiumWeapon::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumItem::Serialize(Ar);

	// The two queued halves ride the event queue's own save block, so the transaction they complete
	// has to restore with them — exactly the pairing `CLogicRelay`'s refire latch keeps.
	Ar << PrimaryModeIndex;
	Ar << SecondaryModeIndex;
	Ar << NextPrimaryAttackTime;
	Ar << NextSecondaryAttackTime;

	Ar << Swing.bActive;
	Ar << Swing.Serial;
	Ar << Swing.ModeIndex;
	Ar << Swing.bMelee;
	Ar << Swing.Activity;
	Ar << Swing.ClipLabel;
	Ar << Swing.Opponent;
	Ar << Swing.PlaybackRate;
	Ar << Swing.ClipSeconds;
	Ar << Swing.CommitTime;
	Ar << Swing.RecoveryDeadline;
	// Additive, behind its own version: a payload written before the event route existed carries a
	// transaction whose estimate IS queued, which is exactly what the default false describes.
	if (Ar.Version() >= FElysiumSaveVersion::WeaponAnimEvent)
	{
		Ar << Swing.bAwaitingAnimEvent;
	}

	Ar << bReloading;
	Ar << ReloadSerial;
	Ar << ReloadEndTime;
	Ar << bFireIntentDuringReload;

	if (Ar.IsLoading())
	{
		if (World)
		{
			Swing.Opponent = World->RebaseSavedHandle(Swing.Opponent);
		}
		SwingSerialCounter = FMath::Max(SwingSerialCounter, Swing.Serial);
		ReloadSerialCounter = FMath::Max(ReloadSerialCounter, ReloadSerial);
	}
}

void FElysiumWeapon::OnEquipped(FElysiumCombatCharacter& Wearer)
{
	// Drawing a weapon restores its authored primary mode and holds both presses until now — a
	// weapon holstered mid-cooldown must not draw with a deadline in the past that it never had.
	PrimaryModeIndex = PrimaryModeSlots.IsEmpty() ? INDEX_NONE : PrimaryModeSlots[0];
	if (Wearer.World)
	{
		HoldAttacksUntil(Wearer.World->NowSeconds());
	}
	ApplyWieldVisual(*this, Wearer);
	UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s equipped by %s"), *DebugString(),
		*Wearer.DebugString());
}

void FElysiumWeapon::OnHolstered(FElysiumCombatCharacter& Wearer)
{
	// A swing or reload in flight cannot survive the holster: its queued commit will still arrive,
	// and the serial check is what turns it into a no-op.
	ClearSwing();
	bReloading = false;
	bFireIntentDuringReload = false;
	if (USkeletalMeshComponent* const WearerBody = Wearer.GetSkeletalBody())
	{
		ElysiumNpcVisual::ClearWieldModel(WearerBody);
	}
	UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s holstered by %s"), *DebugString(),
		*Wearer.DebugString());
}

void FElysiumWeapon::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumItem::GetDebugState(Out);

	const FElysiumItemDef* Record = Data();
	Out.Emplace(TEXT("Modes"), Record ? FString::FromInt(Record->Modes.Num()) : TEXT("(no record)"));
	if (const FElysiumWeaponMode* Mode = ModeAt(PrimaryModeIndex))
	{
		Out.Emplace(TEXT("Primary mode"), FString::Printf(TEXT("%s (%s) lethality %d rate %.2f"),
			*Mode->Tag, ElysiumWeaponModeTypeName(Mode->Type), Mode->BaseLethality, Mode->AttackRate));
	}
	Out.Emplace(TEXT("Next attack"), FString::Printf(TEXT("primary %.3f  secondary %.3f"),
		NextPrimaryAttackTime, NextSecondaryAttackTime));
	Out.Emplace(TEXT("Swing"), Swing.bActive
		? FString::Printf(TEXT("#%d %s rate %.2f commit %s recover %.3f"), Swing.Serial,
			*Swing.Activity, Swing.PlaybackRate,
			Swing.bAwaitingAnimEvent
				? TEXT("awaiting the clip's own event")
				: *FString::Printf(TEXT("%.3f (estimated)"), Swing.CommitTime),
			Swing.RecoveryDeadline)
		: FString(TEXT("(idle)")));
	Out.Emplace(TEXT("Reload"), bReloading
		? FString::Printf(TEXT("#%d ends %.3f%s"), ReloadSerial, ReloadEndTime,
			bFireIntentDuringReload ? TEXT(" (fire intent latched)") : TEXT(""))
		: FString(TEXT("(idle)")));
}

// ================================================================================================
// Accessors
// ================================================================================================

const FElysiumWeaponMode* FElysiumWeapon::ModeAt(int32 Index) const
{
	const FElysiumItemDef* Record = Data();
	return (Record && Record->Modes.IsValidIndex(Index)) ? &Record->Modes[Index] : nullptr;
}

const FElysiumWeaponMode* FElysiumWeapon::ModeFor(EIntent Intent) const
{
	return ModeAt(Intent == EIntent::Primary ? PrimaryModeIndex : SecondaryModeIndex);
}

const FElysiumDmg& FElysiumWeapon::DamageForMode(int32 Index) const
{
	static const FElysiumDmg Empty;
	return ModeDamage.IsValidIndex(Index) ? ModeDamage[Index] : Empty;
}

FElysiumCombatCharacter* FElysiumWeapon::OwnerCharacter() const
{
	if (!World || !Owner.IsSet())
	{
		return nullptr;
	}
	FElysiumEntity* Ent = World->Resolve(Owner);
	return Ent ? Ent->AsCombatCharacter() : nullptr;
}

bool FElysiumWeapon::IsActiveWeapon() const
{
	const FElysiumCombatCharacter* Char = OwnerCharacter();
	return Char != nullptr && Char->Inventory.ActiveWeapon == Handle;
}

int32 FElysiumWeapon::TotalLethality(int32 ModeIndex, const FElysiumCombatCharacter& Attacker,
	const FElysiumWeaponContext& Context) const
{
	const FElysiumWeaponMode* Mode = ModeAt(ModeIndex);
	if (!Mode)
	{
		return 0;
	}
	// The adjustment is the descriptor's own attack feat/reference applied to the attacker — a
	// RATING, not a roll (K5). A developer override replaces it in retail; there is none here.
	const int32 Adjustment = FeatRating(Attacker, DamageForMode(ModeIndex).AttackFeat, Context);
	return FMath::Max(Mode->BaseLethality + Adjustment, 0);
}

// ================================================================================================
// Scheduling
// ================================================================================================

void FElysiumWeapon::HoldAttacksUntil(double Deadline)
{
	// MAXIMUM operations, both of them: a pre-existing later deadline is never shortened.
	NextPrimaryAttackTime = FMath::Max(NextPrimaryAttackTime, Deadline);
	NextSecondaryAttackTime = FMath::Max(NextSecondaryAttackTime, Deadline);
}

void FElysiumWeapon::QueueSelfInput(FName Input, int32 Serial, double Delay)
{
	if (!World)
	{
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s cannot queue '%s': the weapon has no world"), *DebugString(), *Input.ToString());
		return;
	}
	// `!self` with this entity as the caller is the single-target form: one delivery, to this
	// weapon, through the one event queue. No private timer and no second scheduler (K11).
	World->EnqueueInput(TEXT("!self"), Input, FElysiumVariant::Int(Serial), FMath::Max(Delay, 0.0),
		Owner, Handle);
}

void FElysiumWeapon::ClearSwing()
{
	Swing = FSwing();
}

// ================================================================================================
// Clip resolution — the one door to the animation half
// ================================================================================================

float FElysiumWeapon::ResolveAndPlay(const FString& Activity, const FElysiumWeaponMode& Mode,
	FString& OutClipLabel, FString* OutOwnerStem)
{
	OutClipLabel.Reset();
	if (OutOwnerStem)
	{
		OutOwnerStem->Reset();
	}

	FElysiumCombatCharacter* Char = OwnerCharacter();
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;

	float Seconds = 0.0f;
	if (Char && Embodiment)
	{
		// The activity route: the embodiment weighs the manifest's candidates and answers with the
		// vocabulary key that has to go back through the clip player, which is what preserves the
		// shared-bank owner.
		//
		// The chain is the OWNER's, and this one entity has both owners: a player weapon and an
		// NPC's are the same `FElysiumWeapon`, so the attack activity walks `CBasePlayer`'s one pass
		// on the player and the cast's alternation and probe on everyone else. The rest of the
		// context — the owner's classname, its equipped weapon and its state — is the owner's own,
		// which is why it is filled from the character rather than from this weapon.
		FElysiumActivityClipRequest Request;
		Char->FillActivityClipRequest(Request);
		Request.Activity = Activity;
		Request.Variant = FMath::Max(Handle.Index, 0);
		// The producer and the chain are one answer, taken from the same owner: a player-owned
		// weapon's attack is the player's own request walking `CBasePlayer`'s one pass, and everyone
		// else's is the cast's. Stating one without the other would record a producer whose translation
		// belongs to a different body.
		const bool bPlayerOwned = IsPlayerSide(*Char);
		Request.Source = bPlayerOwned ? EElysiumAnimSource::Player : EElysiumAnimSource::Npc;
		Request.BodyKind = bPlayerOwned
			? EElysiumAnimBodyKind::Player : EElysiumAnimBodyKind::Cast;

		FElysiumActivityClip Clip;
		if (Embodiment->ResolveNpcActivityClip(Request, Clip) && !Clip.Label.IsEmpty())
		{
			OutClipLabel = Clip.Label;
			// The bank the include DAG named. A timeline is keyed by (owner, label) the same way a
			// grid cell is, so handing back only the label would look the sequence up on whichever
			// bank happened to answer last.
			if (OutOwnerStem)
			{
				*OutOwnerStem = Clip.OwnerStem;
			}
			float Played = 0.0f;
			if (Char->PlayAnimClip(Clip.Label, /*bLoop*/ false, &Played) && Played > 0.0f)
			{
				Seconds = Played;
			}
		}
	}

	if (Seconds > 0.0f)
	{
		return Seconds;
	}

	// The stated fallback. A headless run, a bodiless character and a bank with no sequence for the
	// activity all land here, and the transaction still has to have a duration or the commit could
	// never be scheduled — which is what keeps the two halves running with no renderer at all.
	if (!bReportedClipFallback)
	{
		bReportedClipFallback = true;
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s: no clip resolved for '%s' (%s) — timing falls back to %s"),
			*DebugString(), *Activity,
			World && World->Embodiment() ? TEXT("body/bank miss") : TEXT("no embodiment"),
			Mode.AttackRate > 0.0f ? TEXT("the mode's Attack_Rate") : TEXT("the stated constant"));
	}
	return Mode.AttackRate > 0.0f ? Mode.AttackRate : ElysiumWeapons::FallbackClipSeconds;
}

// ================================================================================================
// Which route names the commit instant
// ================================================================================================

ElysiumWeapons::EOperatorBody FElysiumWeapon::OperatorBody() const
{
	const FElysiumItemDef* Record = Data();
	return Record ? ElysiumWeapons::OperatorBodyFor(Record->Type)
		: ElysiumWeapons::EOperatorBody::None;
}

bool FElysiumWeapon::CommitArrivesFromAnimEvent(FElysiumCombatCharacter& Char,
	const FString& OwnerStem, const FString& ClipLabel)
{
	const ElysiumWeapons::EOperatorBody OpBody = OperatorBody();
	if (OpBody == ElysiumWeapons::EOperatorBody::None)
	{
		// This weapon's operator body accepts nothing in the band, so no clip can name its commit
		// instant. The estimate is the only route there is and taking it is not degraded — reporting
		// it as a gap would name a clip for a fact about the weapon.
		return false;
	}

	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || ClipLabel.IsEmpty() || OwnerStem.IsEmpty())
	{
		// Nothing resolved a clip at all — a headless run, a bodiless character, or a bank with no
		// sequence for the activity. `ResolveAndPlay` already named it once; there is no clip to
		// carry a timeline, so this is the ordinary estimate rather than a degraded one.
		return false;
	}

	// The dispatcher's own precondition, asked about THIS clip. `FElysiumAnimating::AdvanceAnimEvents`
	// walks the timeline of whatever clip a polled channel is standing on, so a body playing a
	// different clip there never reaches this one's events — and the estimate standing down for it
	// would swallow the whole transaction.
	if (!Char.HasLiveAnimEventDispatch(OwnerStem, ClipLabel))
	{
		// **Verbose, because this is an ordinary refusal rather than a gap.** A scene owns the base
		// channel of the body it is posing and refuses the clip the attack asked for, so no polled
		// channel stands on it and no timeline of its can be walked; the estimate is then the right
		// answer, not a degraded one. Keyed apart from `estimate:` — that key names an authored
		// sequence with no commit id, and reading the two as one work list would fold a body-state
		// answer into a content gap.
		if (ShouldReportOnce(FString::Printf(TEXT("wrongclip:%s@%s"), *ClipLabel, *OwnerStem)))
		{
			UE_LOG(LogElysiumWeapon, Verbose,
				TEXT("%s: '%s'@'%s' was armed but no polled channel stands on it, so the %s commit "
					"keeps the ContactEventCycle estimate"),
				*DebugString(), *ClipLabel, *OwnerStem,
				ElysiumWeapons::OperatorBodyName(OpBody));
		}
		return false;
	}

	const TArray<FElysiumAnimEvent>* Timeline = Embodiment->GetNpcEventTimeline(OwnerStem, ClipLabel);
	if (Timeline != nullptr && ElysiumWeapons::TimelineHasCommitEvent(*Timeline, OpBody))
	{
		return true;
	}

	// Degraded: the body IS being walked and this attack clip declares no commit id, so the instant
	// is estimated where retail reads it off the sequence.
	//
	// Keyed by CLIP and process-wide, like the unclaimed-event census and for the same reason: the
	// gap belongs to an authored sequence, not to a weapon entity, and a crowd of twenty combatants
	// holding the same record would otherwise report one authored gap twenty times.
	if (ShouldReportOnce(FString::Printf(TEXT("estimate:%s@%s"), *ClipLabel, *OwnerStem)))
	{
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s: attack clip '%s'@'%s' declares %s, so the %s commit falls back to the "
				"ContactEventCycle estimate at %.2f of the clip"),
			*DebugString(), *ClipLabel, *OwnerStem,
			Timeline == nullptr ? TEXT("no event timeline at all")
				: TEXT("an event timeline with no commit id"),
			ElysiumWeapons::OperatorBodyName(OpBody), ElysiumWeapons::ContactEventCycle);
	}
	return false;
}

bool FElysiumWeapon::CommitFromAnimEvent(const FElysiumAnimEvent& Event)
{
	if (!Swing.bActive)
	{
		// The id fired with nothing staged. Retail's ranged body still accepts it and re-enters mode
		// dispatch, which finds no attack in flight; an aim or idle clip carrying a shot id lands
		// here. Claimed, and an ordinary negative rather than a fault.
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s took anim event %d with no transaction staged — nothing to commit"),
			*DebugString(), Event.Event);
		return true;
	}

	// Delay 0.0, not a synchronous call: a producer enqueues and only queue service delivers (K11),
	// so the commit serializes with everything else the frame raised instead of re-entering the
	// damage spine from inside the animation pass.
	QueueSelfInput(ElysiumWeaponCommitInput(), Swing.Serial, 0.0);
	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s anim event %d commits %s #%d from clip '%s' at cycle %.3f"), *DebugString(),
		Event.Event, Swing.bMelee ? TEXT("swing") : TEXT("shot"), Swing.Serial, *Swing.ClipLabel,
		Event.Cycle);
	return true;
}

bool FElysiumWeapon::OperatorHandleAnimEvent(FElysiumCombatCharacter& Operator,
	const FElysiumAnimEvent& Event)
{
	// The character hands the band to its OWN active weapon, so the operator is this weapon's owner.
	// Anything else means a record crossed characters on its way here, which cannot pass quietly: the
	// commit it would queue belongs to a transaction a different body staged.
	if (Operator.Handle != Owner)
	{
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s was handed anim event %d by %s, which does not own it — refused"),
			*DebugString(), Event.Event, *Operator.DebugString());
		return false;
	}

	// WHICH body answers is the record's, never the classname's. A thrown weapon takes `0x1024f030`
	// and accepts nothing here, which is why this is not a two-way melee/ranged test.
	const ElysiumWeapons::EOperatorBody OpBody = OperatorBody();

	if (ElysiumWeapons::IsCommitEvent(Event.Event, OpBody))
	{
		return CommitFromAnimEvent(Event);
	}
	if (OpBody == ElysiumWeapons::EOperatorBody::Melee
		&& ElysiumWeapons::IsSwallowedMeleeEvent(Event.Event))
	{
		return true;   // `0x103ea5b0`'s swallow set — accepted, and acted on by nothing
	}

	// Everything else in the band, including every id a `None` body was offered. The 4001/4002
	// bodygroup routes the accepting bodies carry are presentation and are not claimed; the census
	// names whatever else the corpus actually fires.
	return false;
}

// ================================================================================================
// Melee target acquisition
// ================================================================================================

FElysiumEntityHandle FElysiumWeapon::AcquireMeleeOpponent(const FElysiumCombatCharacter& Attacker) const
{
	if (!World)
	{
		return FElysiumEntityHandle::Invalid();
	}
	const float Reach = ElysiumWeapons::MeleeReachSourceUnits * ElysiumMove::U;
	const float ConeDot = FMath::Cos(FMath::DegreesToRadians(ElysiumWeapons::MeleeConeHalfAngleDegrees));
	const FVector EyeOrigin = Attacker.EyePosition();
	const FVector Forward =
		FRotator(Attacker.Angles.X, Attacker.Angles.Y, Attacker.Angles.Z).Vector().GetSafeNormal();

	FElysiumEntityHandle Best = FElysiumEntityHandle::Invalid();
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
	{
		FElysiumCombatCharacter* Other = Candidate ? Candidate->AsCombatCharacter() : nullptr;
		// The melee predicate rejects self, a missing entity and anything not `LIFE_ALIVE`. A loot
		// container re-registers on the combat-character base (it owns the same inventory), so it is
		// a combat character in this runtime without being a combatant — it is not a swing target.
		if (!Other || Other->Handle == Attacker.Handle || Other->AsItemContainer() != nullptr
			|| !IsAliveForCombat(*Other))
		{
			continue;
		}
		const FVector To = Other->EyePosition() - EyeOrigin;
		const float DistanceSq = static_cast<float>(To.SizeSquared());
		if (DistanceSq > Reach * Reach || DistanceSq <= 0.0f)
		{
			continue;
		}
		if (FVector::DotProduct(Forward, To.GetSafeNormal()) < ConeDot)
		{
			continue;
		}
		// SEAM — retail traces forward with mask `0x46004003` and accepts a valid obstruction hit
		// first, and the shared query rejects non-targetable / `ScriptHidden` candidates through the
		// engine's own visibility test. That trace is an engine question (K13) and lands with the
		// perception cycle; distance plus facing is the whole selection until then.
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Other->Handle;
		}
	}
	return Best;
}

// ================================================================================================
// The attack intent — mode dispatch and the accepted swing
// ================================================================================================

FElysiumWeapon::EVerdict FElysiumWeapon::AttackIntent(EIntent Intent,
	const FElysiumEntityHandle& Victim)
{
	FElysiumCombatCharacter* Char = OwnerCharacter();
	if (!Char || !World)
	{
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s took an attack intent with no owning character — refused"), *DebugString());
		return EVerdict::NoOwner;
	}
	if (!IsAliveForCombat(*Char))
	{
		return EVerdict::NoOwner;
	}

	// Fire intent while a reload is live sets the interruption latch; the reload frame finishes the
	// transaction before normal firing resumes.
	if (bReloading)
	{
		bFireIntentDuringReload = true;
		return EVerdict::Reloading;
	}

	const int32 ModeIndex = (Intent == EIntent::Primary) ? PrimaryModeIndex : SecondaryModeIndex;
	const FElysiumWeaponMode* Mode = ModeAt(ModeIndex);
	if (!Mode)
	{
		// A record legitimately authors no secondary; the primary press finding nothing is the
		// reportable case.
		if (Intent == EIntent::Primary && ShouldReportOnce(
			FString::Printf(TEXT("nomode:%s"), *ClassName())))
		{
			UE_LOG(LogElysiumWeapon, Warning,
				TEXT("%s: the record authors no primary attack mode — the press does nothing"),
				*DebugString());
		}
		return EVerdict::NoMode;
	}

	const double Now = World->NowSeconds();
	const double Deadline = (Intent == EIntent::Primary)
		? NextPrimaryAttackTime : NextSecondaryAttackTime;
	if (Now < Deadline)
	{
		return EVerdict::NotReady;
	}

	// `CWeaponRanged::ModeDispatch` — the authored `Type` decides what a press does.
	switch (Mode->Type)
	{
	case EElysiumWeaponModeType::TogglePrimaryMode:
	{
		if (PrimaryModeSlots.Num() < 2)
		{
			return EVerdict::NoMode;
		}
		const int32 Current = PrimaryModeSlots.IndexOfByKey(PrimaryModeIndex);
		const int32 Next = (Current == INDEX_NONE) ? 0 : (Current + 1) % PrimaryModeSlots.Num();
		PrimaryModeIndex = PrimaryModeSlots[Next];
		// The mode-change activity plays and the weapon timers refresh.
		FString Label;
		ResolveAndPlay(GActRangeAttackLayer, *Mode, Label);
		HoldAttacksUntil(Now + Mode->AttackRate / FMath::Max(ElysiumWeapons::AttackSpeedScale(*Char), KINDA_SMALL_NUMBER));
		return EVerdict::ModeToggled;
	}
	case EElysiumWeaponModeType::ZoomLoop:
		// SEAM — the scope range/state cycle and its player notification belong to the weapon
		// presentation half. The mode is recognised and reports rather than silently doing nothing.
		if (ShouldReportOnce(FString::Printf(TEXT("zoom:%s"), *ClassName())))
		{
			UE_LOG(LogElysiumWeapon, Warning,
				TEXT("%s: '%s' is a zoom-loop mode and this runtime has no scope state — the press "
					"is recognised and does nothing"), *DebugString(), *Mode->Tag);
		}
		return EVerdict::ZoomCycled;
	case EElysiumWeaponModeType::Attack:
	case EElysiumWeaponModeType::SecondaryAttack:
		break;
	default:
		if (ShouldReportOnce(FString::Printf(TEXT("modetype:%s:%s"), *ClassName(), *Mode->TypeName)))
		{
			UE_LOG(LogElysiumWeapon, Warning,
				TEXT("%s: mode '%s' has authored type '%s', which has no recovered consumer"),
				*DebugString(), *Mode->Tag, *Mode->TypeName);
		}
		return EVerdict::Unsupported;
	}

	// The magazine has to be able to pay before the swing is accepted. Empty fire is a distinct
	// action, not a refusal.
	if (Mode->AmmoCost > 0 && MagazineCount < Mode->AmmoCost)
	{
		FireOnEmpty(ModeIndex, *Mode);
		return EVerdict::DryFire;
	}

	const FElysiumItemDef* Record = Data();
	const bool bMelee = Record && Record->Type == EElysiumItemType::WeaponMelee;
	return bMelee
		? BeginMeleeSwing(Intent, ModeIndex, *Mode)
		: BeginRangedShot(Intent, ModeIndex, *Mode, Victim);
}

FElysiumWeapon::EVerdict FElysiumWeapon::BeginMeleeSwing(EIntent Intent, int32 ModeIndex,
	const FElysiumWeaponMode& Mode)
{
	FElysiumCombatCharacter& Char = *OwnerCharacter();
	const double Now = World->NowSeconds();
	const FElysiumWeaponContext Context = FElysiumWeaponContext::FromCharacter(Char);
	const FElysiumDmg& ModeDmg = DamageForMode(ModeIndex);

	FString Activity = (Intent == EIntent::Secondary) ? GActMeleeAttackHeavy : GActMeleeAttack;

	// The one live combo substitution: only for an exactly-ordinary `ACT_MELEE_ATTACK`. The
	// controlling Ability is read as a BASE value — a temporary or effect-adjusted current value
	// does not enter this test.
	if (Activity == GActMeleeAttack)
	{
		const bool bBrawl = ModeDmg.AttackFeat.Equals(GFeatCloseCombatBrawl, ESearchCase::IgnoreCase);
		const int32 Slot = bBrawl ? 1 /*Brawl*/ : 6 /*Melee*/;
		const int32 BaseRank = Char.Sheet.GetBase(EElysiumTraitContainer::Abilities, Slot);
		const int32 Chance = ElysiumWeapons::ComboChancePercent(BaseRank);
		const int32 Draw = ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 99);
		if (Draw < Chance)
		{
			Activity = GActMeleeAttack2Combo;
		}
	}

	// Target acquisition happens before the sequence is committed. It is aim assistance and
	// opponent reservation, not a damage verdict: an ordinary swing still animates with no
	// candidate found.
	const FElysiumEntityHandle Opponent = AcquireMeleeOpponent(Char);

	FString ClipLabel;
	FString ClipOwnerStem;
	const float Seconds = ResolveAndPlay(Activity, Mode, ClipLabel, &ClipOwnerStem);
	const bool bEventCommit =
		CommitArrivesFromAnimEvent(Char, ClipOwnerStem, ClipLabel);
	const int32 FeatRank = FeatRating(Char, ModeDmg.AttackFeat, Context);
	const float Rate = FMath::Max(
		ElysiumWeapons::MeleePlaybackRate(FeatRank) * ElysiumWeapons::AttackSpeedScale(Char),
		KINDA_SMALL_NUMBER);

	// Melee recovery is selected-clip timing over the playback rate — never the mode's authored
	// `Attack_Rate` and never one global cooldown.
	const double Recovery = Now + static_cast<double>(Seconds) / Rate;
	const double Commit = Now
		+ static_cast<double>(Seconds * ElysiumWeapons::ContactEventCycle) / Rate;

	ClearSwing();
	Swing.bActive = true;
	Swing.Serial = ++SwingSerialCounter;
	Swing.ModeIndex = ModeIndex;
	Swing.bMelee = true;
	Swing.Activity = Activity;
	Swing.ClipLabel = ClipLabel;
	Swing.Opponent = Opponent;
	Swing.PlaybackRate = Rate;
	Swing.ClipSeconds = Seconds;
	Swing.CommitTime = Commit;
	Swing.RecoveryDeadline = Recovery;
	Swing.bAwaitingAnimEvent = bEventCommit;

	HoldAttacksUntil(Recovery);
	if (!bEventCommit)
	{
		QueueSelfInput(ElysiumWeaponCommitInput(), Swing.Serial, Commit - Now);
	}

	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s swing #%d %s rate %.2f clip %.3fs -> commit %s recover %.3f opponent %s"),
		*DebugString(), Swing.Serial, *Activity, Rate, Seconds,
		bEventCommit ? TEXT("on the clip's own 3047")
			: *FString::Printf(TEXT("%.3f (estimated)"), Commit),
		Recovery, Opponent.IsSet() ? *World->DescribeHandle(Opponent) : TEXT("(none)"));
	return EVerdict::Accepted;
}

FElysiumWeapon::EVerdict FElysiumWeapon::BeginRangedShot(EIntent Intent, int32 ModeIndex,
	const FElysiumWeaponMode& Mode, const FElysiumEntityHandle& Victim)
{
	FElysiumCombatCharacter& Char = *OwnerCharacter();
	const double Now = World->NowSeconds();

	FString ClipLabel;
	FString ClipOwnerStem;
	const float Seconds = ResolveAndPlay(GActRangeAttackLayer, Mode, ClipLabel, &ClipOwnerStem);
	const bool bEventCommit =
		CommitArrivesFromAnimEvent(Char, ClipOwnerStem, ClipLabel);
	const float Scale = FMath::Max(ElysiumWeapons::AttackSpeedScale(Char), KINDA_SMALL_NUMBER);

	// The ranged schedule advances by the authored `Attack_Rate`, scaled by the owner's attack-speed
	// value — not by the clip.
	const double Recovery = Now + static_cast<double>(Mode.AttackRate) / Scale;
	const double Commit = Now + static_cast<double>(Seconds * ElysiumWeapons::ContactEventCycle) / Scale;

	ClearSwing();
	Swing.bActive = true;
	Swing.Serial = ++SwingSerialCounter;
	Swing.ModeIndex = ModeIndex;
	Swing.bMelee = false;
	Swing.Activity = GActRangeAttackLayer;
	Swing.ClipLabel = ClipLabel;
	// SEAM — the shot's world trace and its spread cone are a producer that joins with the
	// perception and player-crosshair cycles. RE40 settled what does NOT enter the cone: the Presence
	// bonus and the Shaky Hands penalty only print diagnostics in the shot body and leave the
	// physical dispersion alone, and the crosshair is a HUD mirror of `CrosshairMinSize` /
	// `CrosshairWalkSizeMax` rather than an input. The cone itself is the authored `SpreadAngle` /
	// `SpreadAngleMax` pair selected by the live ranged-accuracy value, whose interpolation input is
	// still unrecovered — so the transaction takes an explicit victim handle: tests and the AI cycles
	// supply it, and the trace producer joins here.
	Swing.Opponent = Victim;
	Swing.PlaybackRate = Scale;
	Swing.ClipSeconds = Seconds;
	Swing.CommitTime = Commit;
	Swing.RecoveryDeadline = Recovery;
	Swing.bAwaitingAnimEvent = bEventCommit;

	HoldAttacksUntil(Recovery);
	if (!bEventCommit)
	{
		QueueSelfInput(ElysiumWeaponCommitInput(), Swing.Serial, Commit - Now);
	}

	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s shot #%d %s (mode '%s' intent %d) -> commit %s recover %.3f victim %s"),
		*DebugString(), Swing.Serial, *Swing.Activity, *Mode.Tag, (int32)Intent,
		bEventCommit ? TEXT("on the clip's own 3030..3044")
			: *FString::Printf(TEXT("%.3f (estimated)"), Commit),
		Recovery, Victim.IsSet() ? *World->DescribeHandle(Victim) : TEXT("(none)"));
	return EVerdict::Accepted;
}

void FElysiumWeapon::FireOnEmpty(int32 ModeIndex, const FElysiumWeaponMode& Mode)
{
	FElysiumCombatCharacter* Char = OwnerCharacter();
	const double Now = World ? World->NowSeconds() : 0.0;

	// `CWeaponRanged::FireOnEmpty` plays the mode-specific dry-fire activity through the same
	// resolution seam an attack uses and advances BOTH attack timers.
	FString Label;
	ResolveAndPlay(GActRangeDryFire, Mode, Label);

	const float Scale = Char
		? FMath::Max(ElysiumWeapons::AttackSpeedScale(*Char), KINDA_SMALL_NUMBER) : 1.0f;
	HoldAttacksUntil(Now + static_cast<double>(Mode.AttackRate) / Scale);

	UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s dry fire (mode %d, magazine %d/%d)"),
		*DebugString(), ModeIndex, MagazineCount, Mode.AmmoCost);
}

// ================================================================================================
// The commit half — it can miss
// ================================================================================================

void FElysiumWeapon::CommitQueuedAttack(int32 Serial)
{
	if (!Swing.bActive || Swing.Serial != Serial)
	{
		// The transaction was replaced, holstered away or restored without its swing. A stale
		// commit is dropped rather than fired against whatever is staged now.
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s dropped a stale attack commit #%d (staged #%d, %s)"), *DebugString(), Serial,
			Swing.Serial, Swing.bActive ? TEXT("active") : TEXT("idle"));
		return;
	}

	const int32 ModeIndex = Swing.ModeIndex;
	const bool bMelee = Swing.bMelee;
	const FElysiumEntityHandle OpponentHandle = Swing.Opponent;
	ClearSwing();

	FElysiumCombatCharacter* Attacker = OwnerCharacter();
	if (!Attacker || !IsAliveForCombat(*Attacker))
	{
		UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s commit #%d missed: the attacker is gone"),
			*DebugString(), Serial);
		return;
	}
	if (!IsActiveWeapon())
	{
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s commit #%d missed: the weapon is no longer active"), *DebugString(), Serial);
		return;
	}

	const FElysiumWeaponMode* Mode = ModeAt(ModeIndex);
	if (!Mode)
	{
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s commit #%d found no mode %d on its record — no damage"), *DebugString(),
			Serial, ModeIndex);
		return;
	}

	// The magazine is spent HERE, at the authoritative boundary — the animation event chooses the
	// instant, the weapon logic does the spending. `Ammo_Cost` is rounds; `Ammo_Fired` is rays, and
	// the two never substitute for one another.
	if (!bMelee && Mode->AmmoCost > 0)
	{
		if (MagazineCount < Mode->AmmoCost)
		{
			UE_LOG(LogElysiumWeapon, Verbose,
				TEXT("%s commit #%d missed: the magazine no longer covers Ammo_Cost %d"),
				*DebugString(), Serial, Mode->AmmoCost);
			return;
		}
		MagazineCount -= Mode->AmmoCost;
	}

	// The gunshot stimulus, from its real producer: the shot has been paid for, so it is heard
	// whether or not it hits. Emitted before the victim is resolved for exactly that reason — a
	// miss is the loudest thing in the room too.
	if (!bMelee && World != nullptr)
	{
		// `AdjustSoundDistForStealth`: the SOURCE's own hearing reduction, subtracted at insertion.
		// The weapon knows its owner, so the reduction is read off that character's committed
		// surface here rather than inside the bus — the parameter is producer-side by design.
		World->EmitGameSound(Attacker->Origin, ElysiumGameSounds::Gunshot(),
			/*RadiusCm, table-resolved*/ -1.f, Attacker->Handle,
			ElysiumStealth::HearingReductionCmFor(Attacker));
	}

	FElysiumEntity* VictimEnt = OpponentHandle.IsSet() && World ? World->Resolve(OpponentHandle) : nullptr;
	FElysiumCombatCharacter* Victim = VictimEnt ? VictimEnt->AsCombatCharacter() : nullptr;
	if (!Victim || !IsAliveForCombat(*Victim))
	{
		// A miss is an ordinary outcome, not a failure: an unaimed swing, a target that stepped
		// away, a target that died between the swing and the contact.
		UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s commit #%d found no live target — miss"),
			*DebugString(), Serial);
		return;
	}

	if (bMelee)
	{
		MeleeContact(*Attacker, *Victim, ModeIndex);
	}
	else
	{
		RangedImpact(*Attacker, *Victim, ModeIndex);
	}
}

void FElysiumWeapon::MeleeContact(FElysiumCombatCharacter& Attacker, FElysiumCombatCharacter& Victim,
	int32 ModeIndex)
{
	const FElysiumWeaponContext Context = FElysiumWeaponContext::FromCharacter(Victim);
	const FElysiumDmg& ModeDmg = DamageForMode(ModeIndex);

	// --- The combat-stance clock (`m_flLastCombatAnimTime`) ----------------------------------
	// A melee-opponent contact holds BOTH bodies in stance for the next five seconds
	// (`FElysiumCombatCharacter::IsInCombatStance`, the gait ladder's `CombatReady` gate).
	// Stamped ahead of the margin classifier below, because a blocked or fully soaked contact
	// is still contact. `World` is non-null here — the caller resolved the victim through it.
	{
		const double Now = World->NowSeconds();
		Attacker.StampMeleeContact(Now);
		Victim.StampMeleeContact(Now);
	}

	// --- The opposed record, staged on the DEFENDER ------------------------------------------
	FElysiumMeleeRoll Roll;
	Roll.Attacker = Attacker.Handle;
	Roll.Lethality = TotalLethality(ModeIndex, Attacker, Context);

	if (Context.HasDefenseDifficulties())
	{
		const int32 Difficulty = IsPlayerSide(Victim)
			? Context.DefenseDifficultyPc : Context.DefenseDifficultyNpc;
		// Word 2 is the defender's net successes plus any bounded defense bonus. The bonus is the
		// blocked-contact `Dexterity` re-roll, which belongs with the block reactions below.
		Roll.Defense = RollFeatNet(Victim, GDefensiveManeuvers, Difficulty, Context);
	}
	else
	{
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s: rules.txt Damage_Info defense difficulties are unavailable — '%s' is not "
				"rolled and the defender defends with 0"),
			*Victim.DebugString(), GDefensiveManeuvers);
	}

	// Word 3 is the defender's soak, selected from the weapon's active descriptor family.
	if (const TCHAR* SoakFeat = ElysiumDamage::SoakFeatName(ModeDmg.Family, Victim.IsKindred(),
		ModeDmg.IsFalling()))
	{
		if (Context.HasSoakDifficulties())
		{
			const int32 Difficulty = IsPlayerSide(Victim)
				? Context.SoakDifficultyPc : Context.SoakDifficultyNpc;
			Roll.Soak = RollFeatNet(Victim, SoakFeat, Difficulty, Context);
		}
	}

	Victim.StageMeleeRoll(Roll);

	// --- The `rules.txt` margin classifier ----------------------------------------------------
	const int32 Margin = Roll.Margin();
	const EElysiumMeleeAttackerReaction AttackerReaction =
		ElysiumWeapons::ClassifyAttacker(Context.Margins, Margin);
	const EElysiumMeleeDefenderReaction DefenderReaction =
		ElysiumWeapons::ClassifyDefender(Context.Margins, Margin);
	if (!Context.Margins.bValid && ShouldReportOnce(TEXT("margins")))
	{
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("rules.txt Melee_Reactions is unavailable — the melee reaction margin cannot be "
				"classified; damage still resolves and no reaction is selected"));
	}
	// SEAM — consuming the classification is a later animation cycle: `ACT_PREBLOCK`/`ACT_BLOCK`/
	// `ACT_BLOCK_HEAVY` on the defender, the authored left/right blocked reaction on the attacker,
	// the blocked-contact `Dexterity` bonus soak re-roll, and the knockback impulse. The record and
	// its classification are stored now so that cycle consumes a decided value rather than re-rolling.
	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s -> %s melee margin %d (lethality %d - defense %d - soak %d): attacker %s, defender %s"),
		*Attacker.DebugString(), *Victim.DebugString(), Margin, Roll.Lethality, Roll.Defense,
		Roll.Soak, ElysiumWeapons::AttackerReactionName(AttackerReaction),
		ElysiumWeapons::DefenderReactionName(DefenderReaction));

	// A record that is not damaging exits without damage. Blocked does NOT mean zero damage: what
	// decides is the margin, and a positive one carries on even when a block reaction played.
	if (Margin <= 0)
	{
		return;
	}

	// --- The damage commit --------------------------------------------------------------------
	int32 DamageInflicted = Margin;
	// Potence guarantees a minimum on what the formula multiplies: the remaining lethality is floored
	// up to the active rank when Potence is higher.
	DamageInflicted = FMath::Max(DamageInflicted, ElysiumWeapons::ActivePotenceRank(Attacker));

	// `Total = DamageInflicted * (BaseDamage + DamageModifier) * Multiplier`, retail's own diagnostic
	// string. `DamageModifier` is the ATTACKER's rating for the descriptor's close-combat attack feat
	// — `Close_Combat_Brawl` for fists, `Close_Combat_Melee` for an armed weapon — which is the same
	// rating the swing's playback rate reads, and a rating rather than a roll (K5). The multiplier is
	// the trace envelope's and is the one half RE40 does not decompose for melee.
	const int32 DamageModifier = FeatRating(Attacker, ModeDmg.AttackFeat, Context);
	const int32 Total = ElysiumWeapons::MeleeDamageTotal(DamageInflicted, ModeDmg.BaseDamage,
		DamageModifier, ElysiumWeapons::DefaultMeleeMultiplier);

	FElysiumDmg Dmg = ModeDmg;
	Dmg.Source = Attacker.Handle;
	// The direct-damage route: the value above IS the damage-success count, so the resolver's damage
	// roll is bypassed. Its soak test still runs.
	Dmg.Flags |= ElysiumDamage::FlagDirectInput;
	Dmg.ExtraInput = Total;

	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s -> %s melee inflicted %d x (base %d + modifier %d) x multiplier %.3f = %d"),
		*Attacker.DebugString(), *Victim.DebugString(), DamageInflicted, ModeDmg.BaseDamage,
		DamageModifier, ElysiumWeapons::DefaultMeleeMultiplier, Total);

	const FElysiumItemDef* Record = Data();
	Victim.TakeDamage(Dmg, &Attacker, Record && Record->bDisallowFirearmsToBashing);
}

void FElysiumWeapon::RangedImpact(FElysiumCombatCharacter& Attacker, FElysiumCombatCharacter& Victim,
	int32 ModeIndex)
{
	const FElysiumWeaponContext Context = FElysiumWeaponContext::FromCharacter(Victim);
	const FElysiumWeaponMode* Mode = ModeAt(ModeIndex);
	const FElysiumDmg& ModeDmg = DamageForMode(ModeIndex);

	// 1/2. Copy the mode's descriptor (its optional source trait came off the same authored string)
	//      and compute total lethality.
	// 4.   ONLY a Kindred victim rolls `Defensive_Maneuvers`; a mortal one rolls nothing at all.
	const bool bKindredVictim = Victim.IsKindred();
	int32 DefenseNet = 0;
	if (bKindredVictim)
	{
		if (Context.HasDefenseDifficulties())
		{
			const int32 Difficulty = IsPlayerSide(Victim)
				? Context.DefenseDifficultyPc : Context.DefenseDifficultyNpc;
			DefenseNet = RollFeatNet(Victim, GDefensiveManeuvers, Difficulty, Context);
		}
		else
		{
			UE_LOG(LogElysiumWeapon, Warning,
				TEXT("%s: rules.txt Damage_Info defense difficulties are unavailable — the Kindred "
					"'%s' defence is not rolled"), *Victim.DebugString(), GDefensiveManeuvers);
		}
	}
	// 3. Round and clamp the ranged path to at least one, then subtract the defence.
	const int32 Lethality = ElysiumWeapons::RangedRemainingLethality(
		TotalLethality(ModeIndex, Attacker, Context), bKindredVictim, DefenseNet);

	// 5/6. Mark the direct-damage route and compute `remaining lethality * BaseDamage * Multiplier`,
	//      the multiplier being `Volley_Fraction * Hitgroup_Scale`. `Ammo_Fired` is the volley's ray
	//      count and enters only through that fraction — it is never a damage multiplier.
	//      SEAM — the trace path is what counts how many of a shot's rays reached THIS victim and
	//      which hitgroup each one struck; both are engine queries (K13). The transaction names one
	//      explicit victim and casts no rays, so the whole volley is attributed to it and the
	//      hitgroup is the unmodified body scale.
	const int32 RaysFired = Mode ? FMath::Max(Mode->AmmoFired, 1) : 1;
	const float Fraction = ElysiumWeapons::VolleyFraction(/*RaysOnVictim*/ RaysFired, RaysFired);
	const float Multiplier = Fraction * ElysiumWeapons::DefaultHitgroupScale;
	const int32 Value = ElysiumWeapons::RangedDamageTotal(Lethality, ModeDmg.BaseDamage, Multiplier);

	FElysiumDmg Dmg = ModeDmg;
	Dmg.Source = Attacker.Handle;
	Dmg.Flags |= ElysiumDamage::FlagDirectInput;
	Dmg.ExtraInput = Value;

	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s -> %s ranged lethality %d x base %d x multiplier %.3f (%d rays) = %d"),
		*Attacker.DebugString(), *Victim.DebugString(), Lethality, ModeDmg.BaseDamage, Multiplier,
		RaysFired, Value);

	// There is no firearm analogue of the melee block bands and no firearm stagger threshold. The
	// recovered firearm chain is `RangedDamagePerVictim -> DispatchTraceAttack -> TraceAttack ->
	// DispatchTakeDamage -> OnTakeDamage_Alive -> DamageFlinch`: the weapon side ends at the typed
	// damage entry below, and the flinch is the shared alive commit's own reaction, not a second
	// weapon-side call (`combat-and-damage.md` § RE40 -> Burst Fields and DamageFlinch Pipeline).
	const FElysiumItemDef* Record = Data();
	Victim.TakeDamage(Dmg, &Attacker, Record && Record->bDisallowFirearmsToBashing);
}

// ================================================================================================
// Reload
// ================================================================================================

bool FElysiumWeapon::BeginReload()
{
	FElysiumCombatCharacter* Char = OwnerCharacter();
	const FElysiumItemDef* Record = Data();
	if (!Char || !World || !Record)
	{
		UE_LOG(LogElysiumWeapon, Warning, TEXT("%s cannot reload: no owner or no item record"),
			*DebugString());
		return false;
	}
	if (bReloading)
	{
		return false;
	}
	if (Record->MagazineSize <= 0 || Record->AmmoType.IsEmpty())
	{
		return false;   // the record carries no magazine — an ordinary absence
	}

	const int32 Missing = Record->MagazineSize - MagazineCount;
	if (Missing <= 0)
	{
		return false;
	}
	const int32 Reserve = Char->Inventory.Reserve(Record->AmmoType);
	if (Reserve <= 0)
	{
		return false;
	}

	const double Now = World->NowSeconds();
	// The end time comes from the SELECTED SEQUENCE'S duration over the active playback rate.
	// `ReloadTime` is loaded from the magazine record and is explicitly NOT this clock.
	FString Label;
	const FElysiumWeaponMode* Mode = ModeAt(PrimaryModeIndex);
	static const FElysiumWeaponMode EmptyMode;
	const float Seconds = ResolveAndPlay(GActReloadLayer, Mode ? *Mode : EmptyMode, Label);
	const float Rate = FMath::Max(ElysiumWeapons::AttackSpeedScale(*Char), KINDA_SMALL_NUMBER);

	bReloading = true;
	bFireIntentDuringReload = false;
	ReloadSerial = ++ReloadSerialCounter;
	ReloadEndTime = Now + static_cast<double>(Seconds) / Rate;

	// The whole weapon is held for the reload, both presses.
	HoldAttacksUntil(ReloadEndTime);
	QueueSelfInput(ElysiumWeaponReloadInput(), ReloadSerial, ReloadEndTime - Now);

	UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s reload #%d (%s) ends %.3f, %d missing, %d reserve"),
		*DebugString(), ReloadSerial, Record->bReloadSingle ? TEXT("single") : TEXT("bulk"),
		ReloadEndTime, Missing, Reserve);
	return true;
}

void FElysiumWeapon::CommitQueuedReload(int32 Serial)
{
	if (!bReloading || ReloadSerial != Serial)
	{
		UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s dropped a stale reload commit #%d"),
			*DebugString(), Serial);
		return;
	}
	bReloading = false;

	FElysiumCombatCharacter* Char = OwnerCharacter();
	const FElysiumItemDef* Record = Data();
	if (!Char || !Record || !IsAliveForCombat(*Char))
	{
		UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s reload #%d completed with no owner — no rounds"),
			*DebugString(), Serial);
		return;
	}
	if (!IsActiveWeapon())
	{
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s reload #%d completed while holstered — no rounds"), *DebugString(), Serial);
		return;
	}

	const int32 Missing = FMath::Max(Record->MagazineSize - MagazineCount, 0);
	const int32 Reserve = Char->Inventory.Reserve(Record->AmmoType);
	if (Missing <= 0 || Reserve <= 0)
	{
		return;
	}

	// Ordinary reload fills by `min(missing capacity, reserve)` and removes the same amount from
	// reserve. `reload_single` adds ONE round and re-enters until interrupted, full or out.
	const int32 Moved = Record->bReloadSingle ? 1 : FMath::Min(Missing, Reserve);
	MagazineCount += Moved;
	Char->Inventory.AddReserve(Record->AmmoType, -Moved);

	if (!Record->bReloadSingle)
	{
		return;
	}

	const bool bFull = MagazineCount >= Record->MagazineSize;
	const bool bOut = Char->Inventory.Reserve(Record->AmmoType) <= 0;
	if (bFireIntentDuringReload || bFull || bOut)
	{
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s single reload stopped (%s): magazine %d/%d"), *DebugString(),
			bFireIntentDuringReload ? TEXT("interrupted") : (bFull ? TEXT("full") : TEXT("out")),
			MagazineCount, Record->MagazineSize);
		bFireIntentDuringReload = false;
		return;
	}
	BeginReload();
}

// ================================================================================================
// Registration — `CWeapon`, the chain node the weapon families register under
// ================================================================================================

namespace
{
	FElysiumWeapon* AsWeaponFor(FElysiumEntity& Entity)
	{
		FElysiumItem* Item = Entity.AsItem();
		return Item ? Item->AsWeapon() : nullptr;
	}

	void BuildWeaponClass(FElysiumClassDesc& D)
	{
		D.Input(ElysiumWeaponCommitInput(), [](FElysiumEntity& E, const FElysiumInputArgs& A)
		{
			if (FElysiumWeapon* Weapon = AsWeaponFor(E))
			{
				Weapon->CommitQueuedAttack(A.Param.ToInt());
			}
		});
		D.Input(ElysiumWeaponReloadInput(), [](FElysiumEntity& E, const FElysiumInputArgs& A)
		{
			if (FElysiumWeapon* Weapon = AsWeaponFor(E))
			{
				Weapon->CommitQueuedReload(A.Param.ToInt());
			}
		});
	}

	struct FElysiumWeaponChainRegistrar
	{
		FElysiumWeaponChainRegistrar()
		{
			BuildWeaponClass(FElysiumClassRegistry::Get().Register(
				ElysiumWeaponClassName(), ElysiumItemClassName(), &ElysiumWeapons::MakeWeapon));
		}
	};

	const FElysiumWeaponChainRegistrar GWeaponChainRegistrar;
}
