// The weapon controller over the item entity and the damage spine.
//
// `docs/vtmb/combat-and-damage.md` is the specification and owns every fact below; the seam layout
// is `docs/architecture/gameplay-systems-architecture.md` §5.4. Nothing here re-implements the
// descriptor, the soak resolver or the health commit: a weapon decides lethality, defense and the
// opposed margin, and hands the result to `FElysiumCombatCharacter::TakeDamage`, which is the one
// typed route into `ElysiumDamage::Apply` and `CommitDamage`.

#include "Substrate/ElysiumWeaponClasses.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumComboChain.h"             // the authored combo block and its busy/window rules
#include "ElysiumDecalSubsystem.h"         // ElysiumImpactDecals::PoolSize — the shot's variation roll
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumSkeletalBasis.h"          // FromSourceAngles — the defender's facing as an Unreal yaw
#include "ElysiumVariant.h"
#include "ElysiumWieldTable.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumDiceTables.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"             // the NPC leaf the incoming-swing notice is sent to
#include "Substrate/ElysiumNpcConditions.h"   // ElysiumNpcCond::WeaponCapability — the `0x18000` mask
#include "Substrate/ElysiumReactions.h"       // the block and knockback families' pure rules
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumStealth.h"
#include "Substrate/ElysiumSwingContact.h"    // the contact walk's pure window/sub-step rules
#include "Visual/ElysiumMeleeTrail.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Visual/ElysiumPreparedWieldModels.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumWeapon, Log, All);

// The melee attack TIMELINE, and nothing else: one line per press, whatever the press did. It is a
// separate category from `LogElysiumWeapon` on purpose — that one carries the whole weapon's
// diagnostics, and raising it to Verbose to watch a combo buries the combo in per-swing and
// per-batch chatter. This one is readable at its DEFAULT level, so nothing has to be turned on and
// a log tail filtered to this category reads the attack sequence back as a sequence:
//
//   atk #12 swing  ACT_MELEE_ATTACK 'katana_combo_D1' 0.87s x0.76 roll 47/10 ordinary target (none)
//   atk #12 link 2 ACT_MELEE_ATTACK 'katana_combo_D2' press @0.63 window [0.50,0.90]
//   atk #12 link 3 ACT_MELEE_ATTACK 'katana_combo_D3' press @0.71 window [0.50,0.90]
//   atk #12 ignored 'katana_combo_D3' names no successor — the chain ends
//
// Every line names the clip, because "which animation is it playing" is the question this exists to
// answer. Nothing here decides anything; a line removed changes no behaviour.
DEFINE_LOG_CATEGORY_STATIC(LogElysiumMelee, Log, All);

// The logical activities the controller requests.
//
// `CWeaponMelee::PrimaryAttack` requests `ACT_MELEE_ATTACK` and substitutes
// `ACT_MELEE_AIR_ATTACK` for a player whose ideal activity is one of the recovered airborne
// phases, its secondary requests `ACT_MELEE_ATTACK_HEAVY`,
// and `RequestActivity`'s one live substitution OFFERS an exactly-ordinary `ACT_MELEE_ATTACK` the
// promotion to `ACT_MELEE_ATTACK_2COMBO` — an offer the re-entered request can refuse, in which
// case the ordinary attack is performed. The two substitutions are ordered, and that order is what keeps them
// exclusive: the air form is chosen first and is not the ordinary activity, so it never reaches the
// combo test. The ordinary ranged selector realizes `ACT_RANGE_ATTACK1_LAYER`, which
// the weapon activity table then translates per family. The translation is the embodiment's — the
// substrate names the LOGICAL activity and nothing else.

namespace
{
	const TCHAR* const GActMeleeAttack       = TEXT("ACT_MELEE_ATTACK");
	// The air form the melee primary substitutes, keyed on the player's ideal activity
	// (`ElysiumWeapons::IsAirborneMeleeActivity`). It is the player's alone — the cast's activity
	// chain has no air fork. The clip that answers it on the baseball bat is `BaseballBat_air`;
	// `baseballbat_attack_jump` is the grounded back-key attack and declares
	// `ACT_MELEE_ATTACK_BASEBALLBAT`, so the two names read the opposite way round.
	const TCHAR* const GActMeleeAirAttack    = TEXT("ACT_MELEE_AIR_ATTACK");
	const TCHAR* const GActMeleeAttack2Combo = TEXT("ACT_MELEE_ATTACK_2COMBO");
	const TCHAR* const GActMeleeAttackHeavy  = TEXT("ACT_MELEE_ATTACK_HEAVY");
	const TCHAR* const GActRangeAttackLayer  = TEXT("ACT_RANGE_ATTACK1_LAYER");
	const TCHAR* const GActReloadLayer       = TEXT("ACT_RELOAD_LAYER");
	// The cast's reload, and it is a different activity from the player's rather than the same one on
	// another channel. `CAI_BaseNPC::StartTask`'s `TASK_RELOAD` restarts the body's IDEAL ACTIVITY at
	// `ACT_RELOAD` — a full-body base pose — and no NPC path anywhere requests `ACT_RELOAD_LAYER`.
	// The generic spelling is what travels: `ACT_RELOAD` carries no rename rule
	// (`ElysiumWeaponActivityTables.cpp` -> `GRenames`), so the ladder appends the weapon's own family
	// to it and every armed cast body reaches its own `ACT_RELOAD_<FAMILY>`.
	const TCHAR* const GActReload            = TEXT("ACT_RELOAD");

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

	// `WasMeleeBlocked` (`0x10345AB0`, `docs/vtmb/combat-and-damage.md` § "Block and stagger
	// reactions"): whether this contact was blocked, and therefore whether the two block-reaction
	// callbacks fire at all.
	//
	// Three terms, in retail's own order and ours from cheapest to most expensive:
	//
	//  1. **A block-capable defender holding a melee weapon.** Retail's test is the active weapon's
	//     capability mask against `0x18000`, which is exactly the question the AI's own capability
	//     join answers — so it is asked THROUGH that join rather than re-derived here. A firearm
	//     fails it (ranged is `0x2000`), which is why there is no blocked-reaction analogue on the
	//     ranged impact path.
	//  2. **The frontal test**, whose constant is not decoded — `ElysiumReactions::IsFrontalContact`
	//     states the hemisphere that stands in for it, and why.
	//  3. **The defender's own answer**, and this is where the fork is. A PLAYER counts as blocking
	//     while its ideal activity is `ACT_PREBLOCK`/`ACT_BLOCK` — a live input state, not a roll.
	//     A NON-PLAYER uses the stored roll classifier: the four blocked classes count, and
	//     hit/knockback does not.
	bool WasMeleeBlocked(const FElysiumCombatCharacter& Attacker,
		const FElysiumCombatCharacter& Defender, EElysiumMeleeDefenderReaction DefenderReaction)
	{
		if (ElysiumNpcCond::WeaponCapability(Defender) != ElysiumNpcCond::ECapability::Melee)
		{
			return false;
		}
		if (!ElysiumReactions::IsFrontalContact(Attacker.Origin, Defender.Origin,
			ElysiumSkeletalBasis::FromSourceAngles(Defender.Angles).Yaw))
		{
			return false;
		}
		if (IsPlayerSide(Defender))
		{
			return Defender.IsActivelyBlocking();
		}
		return DefenderReaction == EElysiumMeleeDefenderReaction::DodgeAttack
			|| DefenderReaction == EElysiumMeleeDefenderReaction::Dodge
			|| DefenderReaction == EElysiumMeleeDefenderReaction::Block
			|| DefenderReaction == EElysiumMeleeDefenderReaction::BlockStagger;
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

// The `rules.txt` margins.

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

	// **The subsystem always wins; the bound tables are the fallback** — `ElysiumSheetRules`'
	// standing rule, and the same one the sheet's own evaluator follows. A world with no rulebook in
	// reach is not a world with no rules: the bundle is what a headless run and a Substrate-tier case
	// bind, and a transaction that ignored it would defend with zero and classify nothing while the
	// tables it needed were sitting bound beside it. Every member may still be absent, and each
	// consumer below fails safe and says so when it is.
	const ElysiumSheetRules::FBoundTables& Bound = ElysiumSheetRules::BoundTables();
	Context.Feats = Rulebook ? &Rulebook->Feats() : Bound.Feats;
	Context.DiceTables = Rulebook ? &Rulebook->Dice() : nullptr;

	const FElysiumRules* RulesPtr = Rulebook ? &Rulebook->Rules() : Bound.Rules;
	if (RulesPtr == nullptr)
	{
		return Context;
	}
	const FElysiumRules& Rules = *RulesPtr;
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

// The pure rules.

namespace ElysiumWeapons
{
	int32 ComboChancePercent(int32 BaseRank)
	{
		// Recovered from `vampire.dll` (`combat-and-damage.md` § "`2COMBO` is an activity
		// substitution, not the combo chain"), and read out of the shipped `.rdata` as literal
		// PERCENTAGES. Ranks outside the table clamp to its ends; rank 0 is a hard zero, so the
		// substitution is never offered at all to a character who never raised the Ability.
		static constexpr int32 Chances[] = { 0, 10, 25, 45, 70, 100 };
		const int32 Clamped = FMath::Clamp(BaseRank, 0, (int32)UE_ARRAY_COUNT(Chances) - 1);
		return Chances[Clamped];
	}

	bool IsAirborneMeleeActivity(const FString& IdealActivity)
	{
		if (IdealActivity.IsEmpty())
		{
			return false;
		}
		// The recovered switch's cases, in its own order. The five entry activities are exactly the
		// player selector's jump arm (`ElysiumActionTables`' `PLAYER_JUMP` rows), minus the four
		// phases that arm names and this switch does not: the two landings, `ACT_LAND_HARD`, and
		// the two `ACT_LEAP_*` halves. Their absence is the rule, not an omission — a body mid-land
		// is off the ground and swings the GROUNDED form.
		//
		// The sixth is the self-latch: once the air attack is the ideal, a follow-up press stays on
		// the air form until the activity moves off it.
		static const TCHAR* const Entries[] =
		{
			TEXT("ACT_HOP"),
			TEXT("ACT_HOP_UP"),
			TEXT("ACT_HOP_DOWN"),
			TEXT("ACT_LEAP"),
			TEXT("ACT_FALLING"),
			TEXT("ACT_MELEE_AIR_ATTACK"),
		};
		for (const TCHAR* const Entry : Entries)
		{
			if (IdealActivity.Equals(Entry, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
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
		// Ranged alone. A melee contact is the swept walk over the clip's own authored swing records,
		// so no id in the band names its instant — 3047 is the NPC swing trigger, not a commit.
		return Body == EOperatorBody::Ranged
			&& Event >= RangedShotEventFirst && Event <= RangedShotEventLast;
	}

	bool IsMeleeSwingTrigger(int32 Event)
	{
		return Event == MeleeSwingTriggerEvent;
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
	case EVerdict::Chained:     return TEXT("chained");
	case EVerdict::Busy:        return TEXT("busy");
	case EVerdict::DryFire:     return TEXT("dry fire");
	case EVerdict::NotReady:    return TEXT("not ready");
	case EVerdict::Reloading:   return TEXT("reloading");
	case EVerdict::ModeToggled: return TEXT("mode toggled");
	case EVerdict::ZoomCycled:  return TEXT("zoom cycled");
	case EVerdict::NoMode:      return TEXT("no mode");
	case EVerdict::NoOwner:     return TEXT("no owner");
	case EVerdict::Idle:        return TEXT("idle");
	default:                    return TEXT("unsupported");
	}
}



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

		const auto Ready = FElysiumPreparedWieldModels::ForOwner(Body->GetOwner());
		const FElysiumCatalogueWieldModel* Model = nullptr;
		FString Error;
		const auto Result = Ready ? Ready->Resolve(Weapon.ClassName(), !Wearer.Sheet.IsMale(), Model, Error)
			: EElysiumCatalogueWieldResult::InvalidCatalogue;

		switch (Result)
		{
		case EElysiumCatalogueWieldResult::Found:
			if (!ElysiumNpcVisual::InstallWieldModel(Body, FElysiumPreparedWieldModels::AttachmentRef(*Model), Weapon.ClassName()))
				ElysiumMeleeTrail::ClearTrail(Body);
			break;
		case EElysiumCatalogueWieldResult::NoGeometry:
		case EElysiumCatalogueWieldResult::WorldModel:
			// The corpus's ordinary answer (`w_null.mdl` / no wield model) or the `shows_view_model`
			// gate: either way the hand draws nothing, which is an authored answer, not a missing asset.
			ElysiumNpcVisual::ClearWieldModel(Body);
			ElysiumMeleeTrail::ClearTrail(Body);
			break;
		default:
			UE_LOG(LogElysiumWeapon, Warning,
				TEXT("%s equipped by %s: wield presentation failed: %s"),
				*Weapon.DebugString(), *Wearer.DebugString(), Error.IsEmpty() ? TEXT("native wield preparation unavailable") : *Error);
			ElysiumNpcVisual::ClearWieldModel(Body);
			ElysiumMeleeTrail::ClearTrail(Body);
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
	// Additive, behind its own version, at the END of the swing block. A payload written before the
	// field existed carries no owner; the blocked reaction still resolves off `ClipLabel` above,
	// which every supported version writes, so what a pre-27 swing loses is the bank name on the
	// contact's diagnostic line and nothing the player can see.
	if (Ar.Version() >= FElysiumSaveVersion::WeaponSwingClipOwner)
	{
		Ar << Swing.ClipOwnerStem;
	}

	Ar << bReloading;
	Ar << ReloadSerial;
	Ar << ReloadEndTime;
	Ar << bFireIntentDuringReload;

	// `m_fEffects & EF_NODRAW`. Additive behind its own version; the default is DRAWN, which is what
	// every payload written before the bit existed describes.
	if (Ar.Version() >= FElysiumSaveVersion::WeaponHidden)
	{
		Ar << bHidden;
	}

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

void FElysiumWeapon::Hide(FElysiumCombatCharacter* Wearer)
{
	// `CBaseEntity::Hide` (0x1009d2a0), vtable `+0x108`: set `m_fEffects |= EF_NODRAW`. The weapon
	// stays the owner's ACTIVE weapon — nothing about the transaction, the mode or the magazine
	// changes — it stops being drawn, and with it stops translating the body's activities
	// (0x10327ec0 / 0x103854f0 both gate on the bit).
	bHidden = true;
	// The hand follows the bit. Retail's NODRAW is what stops the model rendering at all; here the
	// wield model is a separate attached component, so it is taken off explicitly. The trail goes
	// with it — a trail on a weapon nothing is drawing is a swing arc from an empty hand.
	if (Wearer != nullptr)
	{
		if (USkeletalMeshComponent* const WearerBody = Wearer->GetSkeletalBody())
		{
			ElysiumNpcVisual::ClearWieldModel(WearerBody);
			ElysiumMeleeTrail::ClearTrail(WearerBody);
		}
	}
	UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s hidden (EF_NODRAW set)"), *DebugString());
}

void FElysiumWeapon::Unhide(FElysiumCombatCharacter* Wearer)
{
	// `CBaseEntity::Unhide` (0x1009d380), vtable `+0x10c`: clear `m_fEffects &= ~EF_NODRAW`.
	bHidden = false;
	// The wield model is re-installed only for the wielder that is actually holding this weapon: an
	// unhide on a weapon some other equip has since replaced must not put its model back in the hand.
	if (Wearer != nullptr && IsActiveWeapon())
	{
		ApplyWieldVisual(*this, *Wearer);
	}
	UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s unhidden (EF_NODRAW cleared)"), *DebugString());
}

void FElysiumWeapon::OnEquipped(FElysiumCombatCharacter& Wearer)
{
	// **The deploy commit clears EF_NODRAW** (the shared deploy body at 0x10253b70), which is what
	// makes a weapon that was hidden by a state change draw again when it is re-equipped. Ordered
	// before `ApplyWieldVisual` below so the hand and the bit are written by one path.
	bHidden = false;
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
	// **`Holster` (0x10253ca0) ends with `Hide()`.** The holstered weapon carries EF_NODRAW away with
	// it, so a weapon put back is one that must be DEPLOYED to draw again rather than one that
	// silently reappears the next time some other path makes it active.
	bHidden = true;
	if (USkeletalMeshComponent* const WearerBody = Wearer.GetSkeletalBody())
	{
		ElysiumNpcVisual::ClearWieldModel(WearerBody);
		ElysiumMeleeTrail::ClearTrail(WearerBody);
	}
	UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s holstered by %s"), *DebugString(),
		*Wearer.DebugString());
}

void FElysiumWeapon::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumItem::GetDebugState(Out);

	const FElysiumItemDef* Record = Data();
	Out.Emplace(TEXT("Modes"), Record ? FString::FromInt(Record->Modes.Num()) : TEXT("(no record)"));
	// `m_fEffects & EF_NODRAW`. A hidden ACTIVE weapon reads as "no weapon" to the whole activity
	// translation, which is a state a readout has to be able to see.
	Out.Emplace(TEXT("Drawn"), bHidden ? TEXT("no (EF_NODRAW)") : TEXT("yes"));
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
			Swing.bMelee
				? *FString::Printf(TEXT("swept walk (cycle %.3f, roll %s)"), Swing.PrevCycle,
					Swing.bContactStaged ? TEXT("staged") : TEXT("pending"))
				: Swing.bAwaitingAnimEvent
					? TEXT("awaiting the clip's own event")
					: *FString::Printf(TEXT("%.3f (estimated)"), Swing.CommitTime),
			Swing.RecoveryDeadline)
		: FString(TEXT("(idle)")));
	Out.Emplace(TEXT("Reload"), bReloading
		? FString::Printf(TEXT("#%d ends %.3f%s"), ReloadSerial, ReloadEndTime,
			bFireIntentDuringReload ? TEXT(" (fire intent latched)") : TEXT(""))
		: FString(TEXT("(idle)")));
}



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

// Clip resolution — the one door to the animation half.

bool FElysiumWeapon::BuildActivityClipRequest(FElysiumCombatCharacter& Char,
	const FString& Activity, FElysiumActivityClipRequest& Out) const
{
	// The chain is the OWNER's, and this one entity has both owners: a player weapon and an NPC's are
	// the same `FElysiumWeapon`, so the attack activity walks `CBasePlayer`'s one pass on the player
	// and the cast's alternation and probe on everyone else. The rest of the context — the owner's
	// classname, its equipped weapon and its state — is the owner's own, which is why it is filled
	// from the character rather than from this weapon.
	Char.FillActivityClipRequest(Out);
	Out.Activity = Activity;
	Out.Variant = FMath::Max(Handle.Index, 0);
	// The producer and the chain are one answer, taken from the same owner: a player-owned weapon's
	// attack is the player's own request walking `CBasePlayer`'s one pass, and everyone else's is the
	// cast's. Stating one without the other would record a producer whose translation belongs to a
	// different body.
	const bool bPlayerOwned = IsPlayerSide(Char);
	Out.Source = bPlayerOwned ? EElysiumAnimSource::Player : EElysiumAnimSource::Npc;
	Out.BodyKind = bPlayerOwned ? EElysiumAnimBodyKind::Player : EElysiumAnimBodyKind::Cast;
	return Out.IsValid();
}

float FElysiumWeapon::ResolveAndPlay(const FString& Activity, EElysiumAnimPriority Band,
	EElysiumAnimChannel Channel, const FElysiumWeaponMode& Mode, FString& OutClipLabel,
	FString* OutOwnerStem, float* OutMaxReachCm, float PlaybackRate, bool bRequirePlayerStateMask)
{
	OutClipLabel.Reset();
	if (OutOwnerStem)
	{
		OutOwnerStem->Reset();
	}
	if (OutMaxReachCm)
	{
		*OutMaxReachCm = 0.0f;
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
		BuildActivityClipRequest(*Char, Activity, Request);
		// The player arm of the melee sequence selector, and only where the caller says so.
		Request.bRequireStateMask = bRequirePlayerStateMask;

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
			// The acquisition distance the translated activity asks for. Taken here, off the same
			// resolution the swing is committing, because the seam is the only thing that knows which
			// activity the vocabulary was finally searched for.
			if (OutMaxReachCm)
			{
				*OutMaxReachCm = Clip.MaxReachCm;
			}
			// The claim is STATED, never left to the band-less door — which means `Ambient`, and an
			// ambient claim is consumed by a travelling body's own `LocomotionTravel` publish, so a
			// swing armed through it would lose the base channel the instant the attacker walked
			// (`ElysiumAnimIntent::LocomotionPriority`, `FElysiumAnimationDriver::ArbitrateBase`).
			//
			// The band is the caller's because the two attack families disagree about the base pose:
			// a melee swing replaces it, a ranged or reload layer leaves it to the gait ladder.
			//
			// The source is the OWNER's, the same answer the request above already took: a claim
			// naming a producer the resolution did not walk describes a body that is not the one
			// swinging. It is stated rather than read out of
			// `ElysiumAnimIntent::DefaultPriority(Source)`, which answers `Ambient` for `Npc` and
			// would leave a travelling NPC's swing with exactly the defect this states away.
			//
			// The claim is not held: a swing is one clip, and `PlayNpcClip` gives a non-looping,
			// non-held segment a hold of exactly its own play length, so an armer that never comes
			// back cannot park the channel.
			FElysiumClipSegment Segment;
			Segment.ClipName = Clip.Label;
			Segment.bLoop = false;
			Segment.Source = Request.Source;
			Segment.Priority = Band;
			// **The forced ideal activity, carried with the clip.** This is the half retail's
			// `ForcePreTranslatedSequenceAndActivity` performs beside the sequence commit: the body's
			// ideal activity BECOMES the attack, and its own locomotion classifier stops answering
			// for it. The LOGICAL request travels, never the translated name — a
			// `ACT_MELEE_ATTACK_BASEBALLBAT` says which sequence set answered, not what the body is
			// doing, and every consumer of the ideal activity is asking the second question.
			Segment.Activity = Activity;
			// **`m_flPlaybackRate`, written onto the play rather than kept beside it.**
			// `ResetSequenceInfo` resets the rate to 1.0 and `RequestActivity` then writes the
			// weapon's, so the weapon's write is the authority for the whole play: the clip's drawn
			// speed, its cycle advance and therefore the authored lunge all scale off this one
			// number. A caller that names no rate (every ranged and reload layer, whose schedules are
			// authored rather than clip-timed) leaves the authored 1.0 in place.
			Segment.PlaybackRate = PlaybackRate;
			// **Which pose this clip IS, and it is the caller's answer because the two attack
			// families disagree.** A melee swing REPLACES the base pose; a ranged fire and the
			// player's reload are retail's `CBaseAnimatingOverlay` slot 0 — a masked partial-body
			// layer accumulated over whatever owns the base, whose unowned bones decode to a zero
			// quaternion and a zero position. Posed as the base those bytes collapse the character,
			// so the channel travels with the clip rather than being inferred from the activity name
			// downstream: only the producer knows which of retail's two mechanisms it just asked for.
			Segment.Channel = Channel;
			// **The clip's own hard-cut bit, and it is the only input to the layer's blend envelope.**
			// Retail's `SetLayer` writes `0.2` at each end and zeroes both for a SNAP sequence, so an
			// attack layer is at full weight on the frame it is armed while a reload ramps over a
			// fifth of its cycle. It travels from the resolution that answered it because nothing
			// downstream can ask the clip again for the sequence THIS pick chose.
			Segment.bSnap = Clip.bSnap;

			float Played = 0.0f;
			if (Char->PlayAnimSegment(Segment, &Played) && Played > 0.0f)
			{
				Seconds = Played;
			}
		}
	}

	if (Seconds > 0.0f)
	{
		// The clip's AUTHORED length, never the wall-clock duration of this play: the caller's own
		// schedule divides it by the same rate, so scaling it here would apply the rate twice.
		return Seconds;
	}

	// The stated fallback. A headless run, a bodiless character and a bank with no sequence for the
	// activity all land here, and the transaction still has to have a duration or the commit could
	// never be scheduled — which is what keeps the two halves running with no renderer at all.
	//
	// **A refused player-arm selection is not one of them, and is not reported here.** Under
	// `bRequirePlayerStateMask` the selector answering nothing is the recovered rule doing its job —
	// retail's `0x10160F90` seeds `-1` and returns `answer >= 0`, so an activity none of whose
	// candidates authors a button mask is refused by design and the caller falls through to the
	// ordinary attack. Every `ACT_MELEE_ATTACK_2COMBO` clip is exactly that, so warning here would
	// report the single most common outcome the player arm has as a bank miss, and it names the wrong
	// cause: the bank holds those clips. The melee line reports the refusal where it means something.
	if (!bReportedClipFallback && !bRequirePlayerStateMask)
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

// Which route names the commit instant.

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

	// No commit id on this attack clip. For MELEE that is the corpus as authored — no shipped
	// sequence emits 3047, retail commits melee through its traced-contact path — so the estimate
	// is the interim stand-in and the absence reports at Verbose, not as a failure. A RANGED clip
	// without its 3030-3044 id is a genuine content gap (the move_and_ranged banks author 3031)
	// and keeps the warning.
	//
	// Keyed by CLIP and process-wide, like the unclaimed-event census and for the same reason: the
	// gap belongs to an authored sequence, not to a weapon entity, and a crowd of twenty combatants
	// holding the same record would otherwise report one authored gap twenty times.
	if (ShouldReportOnce(FString::Printf(TEXT("estimate:%s@%s"), *ClipLabel, *OwnerStem)))
	{
		if (OpBody == ElysiumWeapons::EOperatorBody::Melee)
		{
			UE_LOG(LogElysiumWeapon, Verbose,
				TEXT("%s: attack clip '%s'@'%s' declares %s, so the %s commit takes the "
					"ContactEventCycle stand-in at %.2f of the clip"),
				*DebugString(), *ClipLabel, *OwnerStem,
				Timeline == nullptr ? TEXT("no event timeline at all")
					: TEXT("an event timeline with no commit id"),
				ElysiumWeapons::OperatorBodyName(OpBody), ElysiumWeapons::ContactEventCycle);
		}
		else
		{
			UE_LOG(LogElysiumWeapon, Warning,
				TEXT("%s: attack clip '%s'@'%s' declares %s, so the %s commit falls back to the "
					"ContactEventCycle estimate at %.2f of the clip"),
				*DebugString(), *ClipLabel, *OwnerStem,
				Timeline == nullptr ? TEXT("no event timeline at all")
					: TEXT("an event timeline with no commit id"),
				ElysiumWeapons::OperatorBodyName(OpBody), ElysiumWeapons::ContactEventCycle);
		}
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
	//
	// The transaction is a SHOT: the only ids that reach here are the ranged body's 3030..3044,
	// because `IsCommitEvent` answers for that body alone.
	QueueSelfInput(ElysiumWeaponCommitInput(), Swing.Serial, 0.0);
	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s anim event %d commits shot #%d from clip '%s' at cycle %.3f"), *DebugString(),
		Event.Event, Swing.Serial, *Swing.ClipLabel, Event.Cycle);
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
	if (OpBody == ElysiumWeapons::EOperatorBody::Melee)
	{
		if (ElysiumWeapons::IsMeleeSwingTrigger(Event.Event))
		{
			// Retail's NPC swing trigger: a clip asking the character's weapon for a swing from
			// inside its own timeline. It is CLAIMED — leaving it to the census would put a recovered
			// id on the unclaimed work list — and it commits nothing, because the melee contact is the
			// swept walk over this clip's authored records. Its own consumer is not built; reported
			// once per process rather than per occurrence, like every other authored-gap report here.
			if (ShouldReportOnce(TEXT("melee_swing_trigger")))
			{
				UE_LOG(LogElysiumWeapon, Verbose,
					TEXT("%s took the melee swing trigger %d: it is retail's NPC swing request and has "
						"no consumer here — the contact is the swept walk over the clip's own records, "
						"which this id does not commit"),
					*DebugString(), Event.Event);
			}
			return true;
		}
		if (ElysiumWeapons::IsSwallowedMeleeEvent(Event.Event))
		{
			return true;   // `0x103ea5b0`'s swallow set — accepted, and acted on by nothing
		}
	}

	// Everything else in the band, including every id a `None` body was offered. The 4001/4002
	// bodygroup routes the accepting bodies carry are presentation and are not claimed; the census
	// names whatever else the corpus actually fires.
	return false;
}

// Melee target acquisition.

FElysiumEntityHandle FElysiumWeapon::AcquireMeleeOpponent(const FElysiumCombatCharacter& Attacker,
	float ReachCm, float ConeDot) const
{
	if (!World)
	{
		return FElysiumEntityHandle::Invalid();
	}
	// The authored reach is the query distance; the constant covers only the cases that have no
	// authored answer to give — a headless run, a body with no clip vocabulary, or a row whose
	// descriptor states no reach. Swinging at a stand-in distance changes who is reserved, so it is
	// a degraded path rather than a tolerance and reports once — keyed by classname through
	// `ShouldReportOnce`, so it is one line per weapon RECORD for the whole process rather than one
	// per entity or one per swing.
	//
	// `ElysiumNpcConditions`' own melee band keeps using the constant on purpose: that layer decides
	// whether an NPC should *ask* for a swing, from a distance measured before any activity has been
	// translated or any sequence resolved. It has no clip to read a reach off, so it is a different
	// owner answering a different question, not a second copy of this one.
	float Reach = ReachCm;
	if (Reach <= 0.0f)
	{
		Reach = ElysiumWeapons::MeleeReachSourceUnits * ElysiumMove::U;
		if (ShouldReportOnce(FString::Printf(TEXT("reach:%s"), *ClassName())))
		{
			UE_LOG(LogElysiumWeapon, Warning,
				TEXT("%s: the swing's activity states no authored reach — acquisition queries at the "
					"stated %.0f-unit stand-in instead of the sequence maximum"),
				*DebugString(), ElysiumWeapons::MeleeReachSourceUnits);
		}
	}
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

// The attack intent — mode dispatch and the accepted swing.

bool FElysiumWeapon::WantsPrimaryPress(EElysiumWeaponButton Held,
	EElysiumWeaponButton Pressed) const
{
	// **The primary is a press EDGE, melee included.** `CWeaponMelee::ItemPostFrame` `0x103EAEC0`
	// reads `m_afButtonPressed` — `(last ^ current) & current` — and never the held field, so one
	// press is one swing and holding the button produces nothing further
	// (`combat-and-damage.md` § "Weapon and input surface"). What a held button reaches instead is the
	// busy path below, where the press either continues the combo or is spent.
	//
	// A firearm is the same edge, with one authored exception: `allow_autofire` is the key's whole
	// meaning, and it is what makes the held bit the poll for that mode (`Substrate/ElysiumItemTable.h`).
	const FElysiumWeaponMode* PrimaryMode = ModeAt(PrimaryModeIndex);
	const bool bAutofire = PrimaryMode && PrimaryMode->bAllowAutofire;
	return bAutofire
		? EnumHasAnyFlags(Held, EElysiumWeaponButton::Primary)
		: EnumHasAnyFlags(Pressed, EElysiumWeaponButton::Primary);
}

bool FElysiumWeapon::WantsSecondaryPress(EElysiumWeaponButton Pressed) const
{
	// CHOSEN press-edge (RE-A1): nothing recovered states whether the secondary route polls a held
	// bit the way the melee primary does, and a held answer would make `+wpn_secondaryatk` a
	// heavy-attack autofire — which no shipped weapon's recovery deadline is shaped for. The
	// composite's OTHER half, the block bit, is a separate standing classification on the player and
	// is not this frame's business.
	return EnumHasAnyFlags(Pressed, EElysiumWeaponButton::Secondary);
}

float FElysiumWeapon::AimQueryRangeCm(EIntent Intent) const
{
	const FElysiumWeaponMode* Mode = ModeFor(Intent);
	if (Mode == nullptr)
	{
		// No mode is no press: `AttackIntent` refuses it and reports the primary case for itself, so
		// answering 0 here would be a second report of one fact.
		return 0.0f;
	}
	if (Mode->Type != EElysiumWeaponModeType::Attack
		&& Mode->Type != EElysiumWeaponModeType::SecondaryAttack)
	{
		// The press names a mode that does not fire — `Toggle_Primary_Mode`, `Zoom_Out_Loop`, an
		// unrecovered spelling. `ModeDispatch` never reaches a shot for it, so there is no aim to
		// query and no missing `Range` to report: a firearm whose secondary swaps barrels is exactly
		// the record that authors none, and warning about it would name a gap that does not exist.
		return 0.0f;
	}
	if (Mode->Range > 0.0f)
	{
		return Mode->Range * ElysiumMove::U;
	}
	// A firing record that authors no `Range` leaves the aim query with no distance to trace, so the
	// stated stand-in covers it — and a shot acquired at an invented distance is degraded rather than
	// faithful, which is why it says so.
	if (ShouldReportOnce(FString::Printf(TEXT("range:%s:%s"), *ClassName(), *Mode->Tag)))
	{
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s: mode '%s' authors no Range — the aim query uses the stated %.0f-unit stand-in"),
			*DebugString(), *Mode->Tag, ElysiumWeapons::RangedRangeSourceUnits);
	}
	return ElysiumWeapons::RangedRangeSourceUnits * ElysiumMove::U;
}

FElysiumWeapon::EVerdict FElysiumWeapon::ItemPostFrame(EElysiumWeaponButton Held,
	EElysiumWeaponButton Pressed, const FElysiumEntityHandle& AimTarget)
{
	// 1. The empty record. `item_w_unarmed` is always carried and is NOT the melee implementation:
	// `CWeaponUnarmed` has no ordinary attack table at all, and `item_w_fists` is the class that
	// punches (`docs/vtmb/combat-and-damage.md` § "Weapon and input surface"). A holstered player's
	// first click therefore lands on a record that authors nothing, which is ordinary rather than a
	// fault — so it short-circuits here instead of falling through to `AttackIntent`, whose `NoMode`
	// arm warns and would turn every such click into a false alarm.
	if (PrimaryModeIndex == INDEX_NONE && SecondaryModeIndex == INDEX_NONE)
	{
		if (ShouldReportOnce(FString::Printf(TEXT("noattackmodes:%s"), *ClassName())))
		{
			UE_LOG(LogElysiumWeapon, Verbose,
				TEXT("%s authors no attack mode — its weapon frame is idle by design"),
				*ClassName());
		}
		return EVerdict::Idle;
	}

	EVerdict First = EVerdict::Idle;
	const auto Note = [&First](EVerdict V)
	{
		if (First == EVerdict::Idle)
		{
			First = V;
		}
	};

	// 2. Primary — the press edge `WantsPrimaryPress` states.
	if (WantsPrimaryPress(Held, Pressed))
	{
		// **The busy path belongs to the PRESS, and this is the only door a press comes through.**
		// That is what makes the combo chain the player's without a player test anywhere in it: an AI
		// producer reaches the weapon through `AttackIntent` from a schedule task — already a decision
		// rather than a button — and never produces the edge this branch is reached from.
		//
		// A player press advances the Dice stream through `BeginMeleeSwing`'s 2COMBO draw. That is
		// correct rather than a leak: retail's combo substitution is the same draw off the same
		// stream, and an NPC swing already spends it. A press taken by the busy path spends nothing —
		// a chain hand-off draws no combo.
		Note(IsMeleePressBusy() ? MeleeBusyPress() : AttackIntent(EIntent::Primary, AimTarget));
	}

	// 3. Secondary — the chosen press edge `WantsSecondaryPress` states.
	if (WantsSecondaryPress(Pressed))
	{
		Note(AttackIntent(EIntent::Secondary, AimTarget));
	}

	// 4. Reload — the press edge, and the only button here that cannot produce a swing. A refused
	// reload (a full magazine, no reserve, one already running) is an ordinary negative that
	// `BeginReload` reports for itself, so it leaves the frame's verdict where it was.
	if (EnumHasAnyFlags(Pressed, EElysiumWeaponButton::Reload) && BeginReload())
	{
		Note(EVerdict::Accepted);
	}

	return First;
}

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
		// The mode-change activity plays and the weapon timers refresh. It is the attack LAYER, so it
		// composes on the overlay slot exactly as a shot does — the same activity through the same
		// mechanism, on the player and on the cast alike.
		FString Label;
		ResolveAndPlay(GActRangeAttackLayer, EElysiumAnimPriority::Ambient,
			EElysiumAnimChannel::UpperBody, *Mode, Label);
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

	// **The air fork, and it is the PLAYER's alone.** `CWeaponMelee::PrimaryAttack` substitutes the
	// air form for a body whose IDEAL ACTIVITY is one of five airborne phases, or already the air
	// attack — not for a body that merely lacks ground contact
	// (`docs/vtmb/animation_and_movers.md` § "Player action selection is code around the model
	// table"). `ElysiumWeapons::IsAirborneMeleeActivity` is that set. The fork is the player's: the
	// cast's activity chain carries no air fork, and a cast body reports itself airborne for reasons
	// that are never a jump — the same reason `ElysiumAnimIntent::AdvanceJumpLatch` holds a producer
	// with no jump command on the ground.
	//
	// The ideal activity is the driver's own published fact, asked of the embodiment rather than
	// derived here, exactly as the block predicate asks for ground contact. A world with no
	// embodiment publishes no activity, so the grounded form stands and nothing is missing.
	//
	// **Only the primary forks.** The melee `SecondaryAttack` body requests `ACT_MELEE_ATTACK_HEAVY`
	// and substitutes nothing; no recovered weapon ladder declares an airborne heavy base and no
	// authored clip answers one, so the grounded heavy IS the heavy answer in the air. An authored
	// absence, not a gap.
	if (Activity == GActMeleeAttack && IsPlayerSide(Char))
	{
		const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (Embodiment != nullptr
			&& ElysiumWeapons::IsAirborneMeleeActivity(Embodiment->GetPlayerBaseActivity()))
		{
			Activity = GActMeleeAirAttack;
		}
	}

	// What the combo draw did, for the timeline line at the end. Diagnostics only.
	int32 ComboDraw = INDEX_NONE;
	int32 ComboChance = 0;
	const TCHAR* ComboOutcome = TEXT("not rolled");

	// The one live combo substitution: only for an exactly-ordinary `ACT_MELEE_ATTACK` — so the air
	// form above, which is a different activity, never reaches it and spends no draw. That is the
	// recovered order rather than a convenience: `PrimaryAttack` chooses the air form and only then
	// calls `RequestActivity`, whose substitution tests the requested activity for equality with the
	// ordinary one. The controlling Ability is read as a BASE value — a temporary or effect-adjusted
	// current value does not enter this test.
	if (Activity == GActMeleeAttack)
	{
		const bool bBrawl = ModeDmg.AttackFeat.Equals(GFeatCloseCombatBrawl, ESearchCase::IgnoreCase);
		const int32 Slot = bBrawl ? 1 /*Brawl*/ : 6 /*Melee*/;
		const int32 BaseRank = Char.Sheet.GetBase(EElysiumTraitContainer::Abilities, Slot);
		const int32 Chance = ElysiumWeapons::ComboChancePercent(BaseRank);
		const int32 Draw = ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 99);
		ComboDraw = Draw;
		ComboChance = Chance;
		ComboOutcome = TEXT("ordinary");
		if (Draw < Chance)
		{
			// **The promotion is a TRY, not a switch** (`0x103E9EEB`-`0x103E9F03`). On a winning draw
			// retail re-enters `RequestActivity` with `ACT_MELEE_ATTACK_2COMBO`, and takes the answer
			// only if that inner call returns true; a false answer falls through and performs the
			// ORDINARY attack. So the roll decides whether the combo is OFFERED, not whether it
			// happens — and on the PLAYER it is refused almost every time, by the selector below
			// rather than by anything here.
			//
			// **What refuses it is the owner's own sequence selector**, and that is a live capture
			// rather than a reading: 43 of 43 player presses at Melee 5 requested the combo and lost
			// it, because `CBasePlayer`'s selector answers only sequences carrying an authored button
			// mask and no `..._2COMBO_<FAMILY>` clip carries one. The request is made here and denied
			// there, exactly as retail does it; nothing at this site tests a target, an ability or a
			// translated name. `ElysiumWeapons::RequiresPlayerStateMask` is where the denial lives.
			ComboOutcome = TEXT("2COMBO offered");
			Activity = GActMeleeAttack2Combo;
		}
	}

	// The sequence is RESOLVED AND PLAYED first, because the acquisition distance is the
	// resolution's answer: retail reads the maximum reach over every sequence the translated
	// activity returns and only then runs `FindEntityFOV`
	// (`docs/vtmb/combat-and-damage.md` § "Target acquisition, sequence commit and recovery").
	//
	// **A stated order divergence, and it survives only where acquisition cannot refuse.** Retail
	// computes that maximum from the whole answering set WITHOUT picking one, acquires, and only then
	// sets the concrete sequence; this seam answers the reach and the pick together, so on the path
	// below the clip is already playing by the time acquisition runs. That is unobservable rather
	// than merely tolerated: nothing acquisition reads is written by the play — `PlayAnimSegment`
	// reaches the embodiment's visual channel and never the substrate `Origin`/`Angles` the cone is
	// measured from, or any candidate's life state — and neither half draws from an RNG stream, so
	// the Dice draw above stays the only one this swing spends. An ordinary swing animates with no
	// candidate found, so the reservation is the only thing the order could change.
	//
	//
	// **`Scripted` is the swing's band**, and it is the whole difference between a swing that plays
	// and one that is swallowed. It is the first row above the `LocomotionTravel` publish a moving
	// body makes every anim tick, which is what makes a melee attack replace the base pose rather
	// than yield to a walk — and the smallest band that does.
	//
	// It also loses to `Reaction`, and the asymmetry is the point: a hit reaction takes the body out
	// of a swing, while a swing cannot take the channel back from a standing reaction. The
	// combat-action band would make that mutual — a claim replaces on `>=` — so a press would cut
	// short the flinch that answered it, and the player's held-block `Predicate` claim, which is
	// already arbitrated against `ACT_BLOCK` at its own band, would gain a second contender.
	// **The playback rate is decided BEFORE the play, because it is part of the play.** Retail's
	// `CWeaponMelee::RequestActivity` writes `max(m_flSpeedScale, floor) * (0.7 + 0.03 * rank)` onto
	// the animating object as it commits the sequence, over the 1.0 `ResetSequenceInfo` had just put
	// there — so one number governs the drawn swing, its cycle, the authored lunge that cycle
	// samples, and the recovery deadline below.
	const int32 FeatRank = FeatRating(Char, ModeDmg.AttackFeat, Context);
	const float Rate = FMath::Max(
		ElysiumWeapons::MeleePlaybackRate(FeatRank) * ElysiumWeapons::AttackSpeedScale(Char),
		KINDA_SMALL_NUMBER);

	FString ClipLabel;
	FString ClipOwnerStem;
	float MaxReachCm = 0.0f;
	const bool bPlayerArm = IsPlayerSide(Char);
	// The base channel, because a melee swing REPLACES the pose: retail's own attack is the body's
	// selected base sequence and nothing composes over it (no `ACT_MELEE_ATTACK_<FAMILY>` clip is a
	// masked layer, and none declares an autolayer either).
	float Seconds = ResolveAndPlay(Activity, EElysiumAnimPriority::Scripted,
		EElysiumAnimChannel::Base, Mode, ClipLabel, &ClipOwnerStem, &MaxReachCm, Rate, bPlayerArm);

	// The combat-stance clock, stamped where retail stamps it: `CWeaponMelee::RequestActivity`
	// dispatches the field's writer as it commits the sequence, so the swing holds the stance
	// whether or not it goes on to touch anything.
	Char.StampCombatAnim(Now);

	// **The offered combo, refused: fall through to the ordinary attack.** This is the outer half of
	// retail's `TEST AL,AL / JNZ` at `0x103E9F03` — the recursion answered false, so the call it
	// returned to performs `ACT_MELEE_ATTACK` instead. The refusal is the player selector's
	// (`ElysiumWeapons::RequiresPlayerStateMask`), and it resolves NO clip at all, which is what this
	// reads. Without the retry a promoted press would swing nothing, which is neither retail's
	// behaviour nor a swing.
	if (ClipLabel.IsEmpty() && Activity == GActMeleeAttack2Combo)
	{
		ComboOutcome = TEXT("2COMBO offered, refused by the selector");
		Activity = GActMeleeAttack;
		Seconds = ResolveAndPlay(Activity, EElysiumAnimPriority::Scripted,
			EElysiumAnimChannel::Base, Mode, ClipLabel, &ClipOwnerStem, &MaxReachCm, Rate,
			bPlayerArm);
	}

	// Target acquisition happens before the sequence is committed. It is aim assistance and
	// opponent reservation, not a damage verdict: an ordinary swing still animates with no
	// candidate found. It is NOT the opposed roll's own query — that one runs on the swing's first
	// live frame at its own 60-unit reach and 0.7 cone (`StageSwingOpposedRoll`).
	//
	// It runs once, against whatever activity finally answered — the promotion above resolves before
	// this line, so a refused combo has already fallen back and acquires at the ORDINARY attack's
	// reach rather than the combo's.
	const FElysiumEntityHandle Opponent = AcquireMeleeOpponent(Char, MaxReachCm,
		FMath::Cos(FMath::DegreesToRadians(ElysiumWeapons::MeleeConeHalfAngleDegrees)));

	// Melee recovery is selected-clip timing over the playback rate — never the mode's authored
	// `Attack_Rate` and never one global cooldown. `Seconds` is the clip's authored length and the
	// play is already running at `Rate`, so this division is the play's wall-clock duration rather
	// than a second application of the rate.
	const double Recovery = Now + static_cast<double>(Seconds) / Rate;

	ClearSwing();
	Swing.bActive = true;
	Swing.Serial = ++SwingSerialCounter;
	// A press opens a new attack, so it is its own chain root and its first link. A hand-off keeps
	// the root and counts on from here.
	Swing.ChainRoot = Swing.Serial;
	Swing.ChainLink = 1;
	Swing.ModeIndex = ModeIndex;
	Swing.bMelee = true;
	Swing.Activity = Activity;
	Swing.ClipLabel = ClipLabel;
	Swing.ClipOwnerStem = ClipOwnerStem;
	Swing.Opponent = Opponent;
	Swing.PlaybackRate = Rate;
	Swing.ClipSeconds = Seconds;
	// Nothing is scheduled and nothing is estimated: the contact is the per-frame swept walk over
	// this clip's own authored swing records, which `AdvanceSwingContact` picks up from the first
	// frame the pose layer reports the clip playing.
	Swing.CommitTime = 0.0;
	Swing.RecoveryDeadline = Recovery;
	Swing.bAwaitingAnimEvent = false;

	HoldAttacksUntil(Recovery);

	// The timeline line: one per press that opened a swing, naming the clip it is playing.
	const FString RollNote = ComboDraw == INDEX_NONE
		? FString(ComboOutcome)
		: FString::Printf(TEXT("%d/%d %s"), ComboDraw, ComboChance, ComboOutcome);
	// The held direction rides on the same line, because it is the input half of the selection this
	// line already reports the output of. Only the player arm reads a button field at all; a cast
	// body selects geometrically and has none to name.
	const FString StateNote = bPlayerArm
		? ElysiumCombo::DescribeStateMask(World->PlayerSelectionStateMask())
		: FString(TEXT("(npc)"));
	UE_LOG(LogElysiumMelee, Log,
		TEXT("atk #%d swing  %s '%s' %.2fs x%.2f  held %s  roll %s  target %s"),
		Swing.ChainRoot, *Activity,
		ClipLabel.IsEmpty() ? TEXT("(no clip)") : *ClipLabel, Seconds, Rate, *StateNote, *RollNote,
		Opponent.IsSet() ? *World->DescribeHandle(Opponent) : TEXT("(none)"));
	return EVerdict::Accepted;
}

// The busy path — what a press does while an attack is already running.

bool FElysiumWeapon::IsMeleePressBusy() const
{
	// A press with no live melee transaction behind it is not busy at all — it is either an ordinary
	// swing or the plain deadline refusal `AttackIntent` answers, and both are that function's.
	if (!Swing.bActive || !Swing.bMelee || World == nullptr)
	{
		return false;
	}
	// **Retail's own OR**, and the two arms really are different: `w_hold` can sit BELOW `w_close`
	// (`katana_running_attack` authors 0.25/1.0/0.9), which releases the predicate while the hand-off
	// window is still open, and a chain hand-off does not push the deadline, which is what leaves the
	// predicate holding the later links of a chain open after the clock has run out.
	const double Now = World->NowSeconds();
	return Now < NextPrimaryAttackTime || IsMeleeBusy(Now);
}

bool FElysiumWeapon::IsMeleeBusy(double Now) const
{
	if (!Swing.bActive || !Swing.bMelee)
	{
		return false;
	}
	// The busy question is asked of the PRIMARY schedule, because that is the press that reaches it.
	const double NextAttack = NextPrimaryAttackTime;

	// The block family's arm reads the clock alone and needs no clip at all, which is the whole
	// reason the arms are separate: a held block publishes no attack cycle to compare.
	if (ElysiumCombo::BusyArmFor(Swing.Activity) == ElysiumCombo::EBusyArm::Clock)
	{
		return Now < NextAttack;
	}

	FElysiumCombatCharacter* Char = OwnerCharacter();
	FElysiumClipPhase Phase;
	if (Char == nullptr || Swing.ClipLabel.IsEmpty() || Swing.ClipOwnerStem.IsEmpty()
		|| !Char->GetLiveClipPhase(Swing.ClipOwnerStem, Swing.ClipLabel, Phase))
	{
		// No clip on a polled channel is no cycle to compare, and that is NOT busy: a swing whose clip
		// was cut short, and a headless run with no pose layer at all, must not hold the weapon shut
		// forever. The caller's own OR still covers the recovery deadline.
		return false;
	}

	// **The hold is the playing sequence's own, and there is no substitution behind it.** The retail
	// predicate compares the cycle against `seqdesc+0x2F8` verbatim — no default path and no clamp —
	// so a sequence that authors no combo block authors no `w_hold` either and the field reads 1.0,
	// which is what 10,597 of the 14,012 shipped descriptors state outright. A sequence that DOES
	// author a block is read as written, including the four whose `w_hold` sits below their
	// `w_close`.
	float HoldCycle = 1.0f;
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		if (const FElysiumComboChain* Combo =
			Embodiment->NpcClipCombo(Char->ModelStem(), Swing.ClipLabel))
		{
			HoldCycle = Combo->HoldCycle;
		}
	}
	return ElysiumCombo::IsBusy(Swing.Activity, Phase.Cycle, HoldCycle, Now, NextAttack);
}

FElysiumWeapon::EVerdict FElysiumWeapon::MeleeBusyPress()
{
	FElysiumCombatCharacter* Char = OwnerCharacter();
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	FElysiumClipPhase Phase;
	if (Char == nullptr || Embodiment == nullptr || Swing.ClipLabel.IsEmpty()
		|| Swing.ClipOwnerStem.IsEmpty()
		|| !Char->GetLiveClipPhase(Swing.ClipOwnerStem, Swing.ClipLabel, Phase))
	{
		// Nothing to ask the hand-off of. The press is spent either way — retail's busy frame consumes
		// it — and a headless swing with no pose layer is the ordinary shape of this, not a failure.
		UE_LOG(LogElysiumMelee, Log,
			TEXT("atk #%d ignored no live cycle for '%s' — the hand-off cannot be asked"),
			Swing.ChainRoot, Swing.ClipLabel.IsEmpty() ? TEXT("(no clip)") : *Swing.ClipLabel);
		return EVerdict::Busy;
	}

	const FElysiumComboChain* Combo = Embodiment->NpcClipCombo(Char->ModelStem(), Swing.ClipLabel);
	if (Combo == nullptr || !Combo->HasChain())
	{
		// A terminal attack, and every `2COMBO` clip: the press is IGNORED. Not queued for the moment
		// the attack ends, and not a restart — the combo is a chain of authored links and an attack
		// that names no successor simply ends.
		UE_LOG(LogElysiumMelee, Log,
			TEXT("atk #%d ignored '%s' names no successor — the chain ends"),
			Swing.ChainRoot, *Swing.ClipLabel);
		return EVerdict::Busy;
	}
	if (!Combo->IsWindowOpen(Phase.Cycle))
	{
		// Too early or too late, on a window closed at both ends. This is the line that explains a
		// press the player felt they made and the body ignored, so it names which side it missed.
		UE_LOG(LogElysiumMelee, Log,
			TEXT("atk #%d ignored press @%.2f is %s '%s' window [%.2f,%.2f]"),
			Swing.ChainRoot, Phase.Cycle,
			Phase.Cycle < Combo->WindowOpen ? TEXT("before") : TEXT("past"),
			*Swing.ClipLabel, Combo->WindowOpen, Combo->WindowClose);
		return EVerdict::Busy;
	}

	// The successor is a SEQUENCE LABEL resolved against the body's own vocabulary, case-insensitively
	// — which is what the ten shipped links whose case disagrees with their target rely on.
	const FString ChainOwnerStem = Embodiment->NpcClipOwner(Char->ModelStem(), Combo->Chain);
	if (ChainOwnerStem.IsEmpty())
	{
		// **The dangling link.** `LookupSequence` answers -1 and the chain is silently dead. The four
		// shipped ones — both sexes' `fists` and `katana` banks — are authoring bugs in retail's own
		// content, not decode failures, so the press is ignored rather than repaired. It is named on
		// the timeline like every other refused press: the fact a player needs is which press did
		// nothing, and once-per-(clip, body) would hide every repeat of it.
		UE_LOG(LogElysiumMelee, Log,
			TEXT("atk #%d ignored '%s' chains to '%s', which body '%s' does not name (dead link)"),
			Swing.ChainRoot, *Swing.ClipLabel, *Combo->Chain, *Char->ModelStem());
		return EVerdict::Busy;
	}

	// Read off the transaction BEFORE the hand-off tears it down: the line below reports the press
	// that was accepted, and the window it was accepted in belongs to the clip being left.
	const float PressCycle = Phase.Cycle;
	const float WindowOpen = Combo->WindowOpen;
	const float WindowClose = Combo->WindowClose;

	CommitMeleeChain(*Char, Combo->Chain, ChainOwnerStem);

	UE_LOG(LogElysiumMelee, Log,
		TEXT("atk #%d link %d %s '%s' %.2fs  press @%.2f window [%.2f,%.2f]"),
		Swing.ChainRoot, Swing.ChainLink, *Swing.Activity, *Swing.ClipLabel, Swing.ClipSeconds,
		PressCycle, WindowOpen, WindowClose);
	return EVerdict::Chained;
}

void FElysiumWeapon::CommitMeleeChain(FElysiumCombatCharacter& Char, const FString& ChainLabel,
	const FString& ChainOwnerStem)
{
	// Everything the hand-off carries forward, read off the transaction before it is torn down.
	const int32 ModeIndex = Swing.ModeIndex;
	// The attack's identity survives the hand-off; only the link count moves. `ClearSwing` below
	// resets both, so they are read here like every other carried value.
	const int32 ChainRoot = Swing.ChainRoot;
	const int32 NextLink = Swing.ChainLink + 1;
	// **The same playback rate.** The chain does not re-derive it from the attack feat: it continues
	// one attack, and re-deriving would be a second `RequestActivity` this path never performs.
	const float Rate = Swing.PlaybackRate;
	// **The deadline is NOT pushed again.** It is what makes a combo faster than the same number of
	// separate swings, and it is why the busy predicate rather than the clock is what holds the later
	// links of a chain open.
	const double Recovery = Swing.RecoveryDeadline;
	// The aimed opponent is a reservation made by acquisition, and the chain runs no acquisition — so
	// the reservation the swing it continues made still stands. The opposed roll is a different thing
	// and IS restaged below, with its own query, because it is per swing.
	const FElysiumEntityHandle Opponent = Swing.Opponent;
	const float PreviousSeconds = Swing.ClipSeconds;

	// **The hand-off claims the base channel at the same band the swing it continues did.** A chain
	// link is one more frame of the same melee attack, so it replaces the base pose for the same
	// reason: an `Ambient` claim — what the band-less door means — is consumed by a travelling
	// body's own locomotion publish, and a combo landed while moving would animate nothing at all.
	// The band is stated rather than taken from `DefaultPriority(Source)`, which answers `Ambient`
	// for an NPC and would leave a travelling combatant's chain with exactly that defect.
	FElysiumClipSegment Segment;
	Segment.ClipName = ChainLabel;
	Segment.bLoop = false;
	Segment.Source = IsPlayerSide(Char) ? EElysiumAnimSource::Player : EElysiumAnimSource::Npc;
	Segment.Priority = EElysiumAnimPriority::Scripted;
	// The forced ideal activity is the ordinary attack for every link of the chain, exactly as
	// `Swing.Activity` below records — a hand-off is one more frame of the same melee attack, so the
	// body's ideal activity does not move and each link's own `w_hold` decides its lock.
	Segment.Activity = GActMeleeAttack;
	// The same `m_flPlaybackRate` the swing this link continues is running at. The chain performs no
	// second `RequestActivity`, so it re-writes the rate it inherited rather than re-deriving one.
	Segment.PlaybackRate = Rate;

	float Seconds = 0.0f;
	if (!Char.PlayAnimSegment(Segment, &Seconds) || Seconds <= 0.0f)
	{
		// The vocabulary named the label, so a body that cannot play it is a real gap rather than an
		// authored absence — unlike the dangling link above, which is the file's own bug.
		if (ShouldReportOnce(FString::Printf(TEXT("chainplay:%s@%s"), *ChainLabel, *Char.ModelStem())))
		{
			UE_LOG(LogElysiumWeapon, Warning,
				TEXT("%s: chain successor '%s' is in body '%s' vocabulary but would not play — the "
					"hand-off is timed off the clip it continues"),
				*DebugString(), *ChainLabel, *Char.ModelStem());
		}
		Seconds = PreviousSeconds;
	}

	// The per-swing state is reset EXACTLY as a swing start resets it: a fresh serial, a cleared walk
	// and a cleared `bContactStaged`, so the sweep meets the new clip with no inherited hit list and
	// the opposed roll is staged again on its first batched frame. `ClearSwing` is the same door
	// `BeginMeleeSwing` uses, which is what keeps the two from drifting apart.
	ClearSwing();
	Swing.bActive = true;
	Swing.Serial = ++SwingSerialCounter;
	Swing.ChainRoot = ChainRoot;
	Swing.ChainLink = NextLink;
	Swing.ModeIndex = ModeIndex;
	Swing.bMelee = true;
	// The LOGICAL activity stays the ordinary attack across the whole chain, whatever link it is on.
	Swing.Activity = GActMeleeAttack;
	Swing.ClipLabel = ChainLabel;
	Swing.ClipOwnerStem = ChainOwnerStem;
	Swing.Opponent = Opponent;
	Swing.PlaybackRate = Rate;
	Swing.ClipSeconds = Seconds;
	Swing.CommitTime = 0.0;
	Swing.RecoveryDeadline = Recovery;
	Swing.bAwaitingAnimEvent = false;

}

FElysiumWeapon::EVerdict FElysiumWeapon::BeginRangedShot(EIntent Intent, int32 ModeIndex,
	const FElysiumWeaponMode& Mode, const FElysiumEntityHandle& Victim)
{
	FElysiumCombatCharacter& Char = *OwnerCharacter();
	const double Now = World->NowSeconds();

	FString ClipLabel;
	FString ClipOwnerStem;
	// **Fire is an overlay for BOTH bodies, so there is no fork here.** The player reaches it as
	// `CWeaponRanged::Attack` -> `SetAnimation(PLAYER_ATTACK1)` -> an overlay-slot-0 layer with the
	// base activity left at -1, and the cast reaches it as `CAI_BaseNPC::RunAI` ->
	// `AddGesture(ACT_RANGE_ATTACK1_LAYER)` into `m_AnimOverlay` — two producers, one mechanism.
	// Retail's own "the attack layer for the weapon '%s' lasts longer than the fire rate" warning is
	// about that NPC layer, so the cast arm is a layer in retail's own words.
	const float Seconds = ResolveAndPlay(GActRangeAttackLayer, EElysiumAnimPriority::Ambient,
		EElysiumAnimChannel::UpperBody, Mode, ClipLabel, &ClipOwnerStem);

	// The combat-stance clock. A shot reaches it through `CBasePlayer::SetAnimation`'s
	// `PLAYER_ATTACK1` arm, which stamps before it selects anything — so the five-second ready hold
	// starts on the trigger pull rather than on a hit.
	Char.StampCombatAnim(Now);

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
	Swing.ClipOwnerStem = ClipOwnerStem;
	// The victim is the caller's: the player frame gets it from the embodiment's aim query, an AI
	// cycle from its own enemy selection, and a test states it. One handle rather than a per-ray
	// hit set is what the transaction carries, and that is the whole of the divergence below.
	Swing.Opponent = Victim;
	// The shot leaves with no dispersion at all — the degenerate zero-spread member of retail's cone
	// family rather than the cone itself. `SpreadAngle`/`SpreadAngleMax` are authored, but the live
	// ranged-accuracy value that interpolates between them is unrecovered (RE-A3), so
	// there is no honest dispersion to apply yet. RE40 settled what does NOT enter it: the Presence
	// bonus and the Shaky Hands penalty only print diagnostics in the shot body and leave the
	// physical dispersion alone, and the crosshair is a HUD mirror of `CrosshairMinSize` /
	// `CrosshairWalkSizeMax` rather than an input. Reported once per weapon entity so a run that
	// fired perfectly straight says why.
	//
	// Two further gaps in this transaction stay open and are NOT this report's: `Ammo_Fired` pellet
	// volleys are committed as one victim's whole share rather than grouped per victim, and Kick
	// (RE-A5) has no recovered producer at all.
	if (!bReportedNoSpread)
	{
		bReportedNoSpread = true;
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s fires with no spread applied — the authored SpreadAngle/SpreadAngleMax pair's "
				"interpolation input is unrecovered (RE-A3), so the shot takes the cone's "
				"zero-spread case"), *DebugString());
	}
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

	// **No third-person body clip is requested here, by anybody, and the absence is the recovered
	// behaviour rather than a gap.** The player's dry fire is VIEWMODEL-ONLY:
	// `CWeaponRanged::FireOnEmpty` reaches `SendWeaponAnim(ACT_VM_DRYFIRE)`, and
	// `CBasePlayer::SetAnimation` carries no dry-fire case at all, so nothing ever reaches the body's
	// own animation seam. NPCs never dry-fire — an empty magazine ends the burst and the AI picks a
	// reload schedule instead. The `ACT_DRYFIRE_LAYER` body clips do exist in the banks, but the only
	// thing that reaches them is the `debug_test_switch2 4` ConVar, which is not a game path.
	//
	// Retail's own arm also plays a click; that sound is not reproduced here, and no path in this
	// runtime emits one for an empty magazine.
	//
	// The hold below is the mode's authored `Attack_Rate` over the owner's attack-speed scale, which
	// is what both attack timers advance on. No clip length enters it: this arm resolves none.
	const float Scale = Char
		? FMath::Max(ElysiumWeapons::AttackSpeedScale(*Char), KINDA_SMALL_NUMBER) : 1.0f;
	HoldAttacksUntil(Now + static_cast<double>(Mode.AttackRate) / Scale);

	// The combat-stance clock, and this arm reaches it WITHOUT `SetAnimation`: retail's own
	// `CWeaponRanged::FireOnEmpty` ends by dispatching the field's writer on its owner directly. So
	// an empty trigger pull holds the ready stance even though it poses no body clip.
	if (Char != nullptr)
	{
		Char->StampCombatAnim(Now);
	}

	UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s dry fire (mode %d, magazine %d/%d)"),
		*DebugString(), ModeIndex, MagazineCount, Mode.AmmoCost);
}

// The commit half — it can miss.

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

	if (Swing.bMelee)
	{
		// **Melee has no queued commit.** Its contact is the swept walk over the swing clip's own
		// authored records, so nothing in this runtime enqueues one. A payload written by a build
		// that did can still carry one in the event queue's own save block, and it arrives here on
		// restore; the walk owns the transaction, so the record is dropped. It is reported because a
		// commit reaching a family that accepts none is exactly what a quiet return would hide.
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s dropped a queued melee commit #%d — a melee contact is the swept walk over the "
				"swing clip's own records, and no queued input commits one"),
			*DebugString(), Serial);
		return;
	}

	const int32 ModeIndex = Swing.ModeIndex;
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
	if (Mode->AmmoCost > 0)
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
	if (World != nullptr)
	{
		// `AdjustSoundDistForStealth`: the SOURCE's own hearing reduction, subtracted at insertion.
		// The weapon knows its owner, so the reduction is read off that character's committed
		// surface here rather than inside the bus — the parameter is producer-side by design.
		World->EmitGameSound(Attacker->Origin, ElysiumGameSounds::Gunshot(),
			/*RadiusCm, table-resolved*/ -1.f, Attacker->Handle,
			ElysiumStealth::HearingReductionCmFor(Attacker));
	}

	// SEAM (R7.2, closed) — retail traces forward first and accepts a valid obstruction hit.
	// The paid-for shot leaves a mark on whatever the ray actually met, resolved a hit or a miss:
	// `C_TEGunshotDecal` is emitted by the shot itself, not by the damage. The substrate owns the
	// instant and the variation roll (a decal variation is an effects-side pick, so it draws on
	// that stream); the trace, the hit's surface character and the decal are engine questions and
	// live behind `IElysiumEmbodiment::LayShotImpactDecal` (`docs/vtmb/effects.md` §3.5).
	TraceShotImpact(*Attacker, *Mode);

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

	RangedImpact(*Attacker, *Victim, ModeIndex);
}

void FElysiumWeapon::TraceShotImpact(const FElysiumCombatCharacter& Attacker,
	const FElysiumWeaponMode& Mode)
{
	// The variation is rolled for every committed shot, before anything can decline to draw it: the
	// Effects stream's position is a function of what the run DID, not of whether an engine was
	// attached to watch it (`ElysiumRng.h` — the stream state is in the save).
	const int32 Variation =
		ElysiumRng::Stream(EElysiumRngStream::Effects).RandRange(1, ElysiumImpactDecals::PoolSize);
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		return;
	}
	// The shot's own origin and direction, off the attacker's committed surface. The reach is the
	// mode's `Range`, or the stated stand-in the aim query already reports on for a record that
	// authors none — this call adds no second report.
	//
	// `Origin` (and so `EyePosition`) is an Unreal world position in cm, but `Angles` is Source
	// QAngles: the handedness reflection reverses pitch and yaw, which is why the player's own
	// `SyncFromBody` writes `Angles = ElysiumPlayerView::ToSource(View)` and every motor call
	// passes `-Angles.Y`. A ray built from the raw components would be mirrored about the world X
	// axis and pitched the wrong way, so a shot fired at Unreal yaw 90 would stain the wall at
	// −90. `ToUnreal` is that conversion's one home, and it is the same frame
	// `AElysiumMapActor::QueryAimTarget` traces the victim along (`GetViewRotation()`), so the
	// mark and the target come off one aim rather than two.
	const FVector EyeOrigin = Attacker.EyePosition();
	const FVector Forward = ElysiumPlayerView::ToUnreal(Attacker.Angles).Vector().GetSafeNormal();
	const float RangeCm = (Mode.Range > 0.0f ? Mode.Range : ElysiumWeapons::RangedRangeSourceUnits)
		* ElysiumMove::U;
	// One hull half-width forward of the eye, so the shooter's own body is never the obstruction
	// the trace accepts. Retail excluded the firer from its own trace mask; this is that exclusion
	// expressed as a start point, which needs no handle on an engine body.
	const FVector From = EyeOrigin + Forward * ElysiumMove::HullHalfWidth;
	Embodiment->LayShotImpactDecal(From, Forward, RangeCm, Variation);
}

// The melee contact — a per-frame swept walk over the clip's own authored windows.

namespace
{
	// The attacker's frame partway through a walk sub-step. The sub-steps exist to place the limb
	// where it was between two rendered frames, and the root it hangs off moved over the same
	// interval — a segment carried on this frame's origin alone would sweep from a position the
	// attacker had already left.
	FTransform ElysiumSwingFrameAt(const FTransform& From, const FTransform& To, float Alpha)
	{
		FTransform Out;
		Out.Blend(From, To, Alpha);
		return Out;
	}
}

void FElysiumWeapon::StageSwingOpposedRoll(FElysiumCombatCharacter& Attacker,
	const FSwingContact& Contact)
{
	const int32 ModeIndex = Contact.ModeIndex;

	// `MeleeRollAndSendNoticeCallback`'s own opponent query, and it is NOT the acquisition query the
	// accepted swing already ran: a fixed 60 Source units and a half-cone dot of 0.7, against the
	// authored per-sequence reach and 30-degree cone that reserved `Swing.Opponent`. Two retail
	// calls, two shapes.
	const FElysiumEntityHandle OpponentHandle = AcquireMeleeOpponent(Attacker,
		ElysiumWeapons::SwingRollReachSourceUnits * ElysiumMove::U,
		ElysiumWeapons::SwingRollConeDot);
	FElysiumEntity* OpponentEnt = OpponentHandle.IsSet() && World
		? World->Resolve(OpponentHandle) : nullptr;
	FElysiumCombatCharacter* Victim = OpponentEnt ? OpponentEnt->AsCombatCharacter() : nullptr;
	if (!Victim || !IsAliveForCombat(*Victim))
	{
		// A swing opened on empty air. Ordinary, and deliberately not retried on a later frame:
		// retail rolls once per swing and a body that walks into the arc afterwards is opposed by
		// nothing. It is the reason a sweep can reach a victim with no record to consume.
		// VeryVerbose: this fires on EVERY swing that opened on empty air, which is most of them
		// outside a fight, and at Verbose it buries the attack timeline it sits next to. The swing
		// line already reports the acquired target, so the absence is readable without this.
		UE_LOG(LogElysiumWeapon, VeryVerbose,
			TEXT("%s swing #%d opened with no opponent inside the %.0f-unit roll cone — no opposed "
				"record staged"),
			*DebugString(), Contact.Serial, ElysiumWeapons::SwingRollReachSourceUnits);
		return;
	}

	const FElysiumWeaponContext Context = FElysiumWeaponContext::FromCharacter(*Victim);
	const FElysiumDmg& ModeDmg = DamageForMode(ModeIndex);

	// The opposed record, staged on the DEFENDER.
	// Stamped with this swing's serial, which is what stands in for retail's `ForceMeleeReset`:
	// every record an earlier swing of this attacker's staged stops answering the moment a new one
	// is accepted, so a later sweep cannot consume a margin nobody rolled for it. `StageMeleeRoll`
	// replaces this attacker's row rather than appending, so the previous swing's record is also
	// physically gone from any body this one opposes.
	FElysiumMeleeRoll Roll;
	Roll.Attacker = Attacker.Handle;
	Roll.SwingSerial = Contact.Serial;
	Roll.Lethality = TotalLethality(ModeIndex, Attacker, Context);

	if (Context.HasDefenseDifficulties())
	{
		const int32 Difficulty = IsPlayerSide(*Victim)
			? Context.DefenseDifficultyPc : Context.DefenseDifficultyNpc;
		// Word 2 is the defender's net successes plus any bounded defense bonus. The bonus is the
		// blocked-contact `Dexterity` re-roll, which belongs with the block reactions at contact.
		Roll.Defense = RollFeatNet(*Victim, GDefensiveManeuvers, Difficulty, Context);
	}
	else
	{
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s: rules.txt Damage_Info defense difficulties are unavailable — '%s' is not "
				"rolled and the defender defends with 0"),
			*Victim->DebugString(), GDefensiveManeuvers);
	}

	// Word 3 is the defender's soak, selected from the weapon's active descriptor family.
	if (const TCHAR* SoakFeat = ElysiumDamage::SoakFeatName(ModeDmg.Family, Victim->IsKindred(),
		ModeDmg.IsFalling()))
	{
		if (Context.HasSoakDifficulties())
		{
			const int32 Difficulty = IsPlayerSide(*Victim)
				? Context.SoakDifficultyPc : Context.SoakDifficultyNpc;
			Roll.Soak = RollFeatNet(*Victim, SoakFeat, Difficulty, Context);
		}
	}

	Victim->StageMeleeRoll(Roll);

	// The incoming-swing notice.
	// The other half of the same callback, sent from the same staging instant. A player victim has
	// no such memory — its reaction is its own input — so a swing at the player stages the record
	// and notices nobody, which is an ordinary negative rather than a miss.
	if (FElysiumNpc* NpcVictim = OpponentEnt->AsNpc())
	{
		ElysiumNpcCond::NoticeMeleeAttack(*NpcVictim, Attacker.Handle, Attacker.Origin,
			World->NowSeconds());
	}

	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s swing #%d staged its opposed record on %s (lethality %d, defense %d, soak %d)"),
		*DebugString(), Contact.Serial, *Victim->DebugString(), Roll.Lethality, Roll.Defense,
		Roll.Soak);
}

void FElysiumWeapon::AdvanceSwingContact(float DeltaSeconds)
{
	if (!Swing.bActive || !Swing.bMelee)
	{
		return;
	}

	// **What retires a melee transaction.** Melee has no commit event and no queued half, so the
	// walk is the only thing that can close it: once the clip it was walking has stopped playing and
	// the recovery deadline has passed, the transaction is over and is cleared. Leaving it standing
	// would let a LATER play of the same clip label — a scene, a schedule, anything that is not an
	// accepted swing — re-open contact for a swing that ended, against a record its own serial still
	// matches. The two conditions are both required: before the deadline the clip may simply not have
	// been armed by the pose layer yet, which is the ordinary state of a swing's first frames.
	const auto RetireIfRecovered = [this]()
	{
		if (World != nullptr && World->NowSeconds() >= Swing.RecoveryDeadline)
		{
			ClearSwing();
		}
	};

	FElysiumCombatCharacter* Attacker = OwnerCharacter();
	if (Attacker == nullptr || !IsAliveForCombat(*Attacker) || !IsActiveWeapon())
	{
		// The swing outlived its own attacker or was holstered away. The walk stops finding anything
		// to sweep, and the transaction goes when its recovery does.
		Swing.ResetWalk();
		RetireIfRecovered();
		return;
	}

	// A swing is LIVE for exactly as long as its own clip is playing on a channel the pose layer
	// publishes. There is no start event and no stop event: the clip's presence is the window.
	FElysiumClipPhase Phase;
	if (Swing.ClipLabel.IsEmpty() || Swing.ClipOwnerStem.IsEmpty()
		|| !Attacker->GetLiveClipPhase(Swing.ClipOwnerStem, Swing.ClipLabel, Phase))
	{
		Swing.ResetWalk();
		RetireIfRecovered();
		return;
	}

	// **The swinging body clears its OWN hit-buildup counter** once its clip reaches
	// `melee_swing_completion_percent`. Retail's site is on the swinging body's own NPC self-pointer
	// rather than on anything it hit, and that asymmetry is the whole mechanism: a body is knocked
	// around until it fights back, and no relationship bookkeeping is involved.
	//
	// **It sits here — above the record lookup and above the batch decision — deliberately.** Retail
	// zeroes inside the live-swing branch of `MeleeSwingUpdate`, which knows nothing about swing
	// records, and a melee clip declaring none is an ordinary shipped shape rather than a defect.
	// Placed below the record bail-out, a body whose attack clip authors no contact record could
	// raise its counter by being hit and never clear it by swinging — permanently knockback-immune,
	// which inverts the very loop this reproduces. The batch decision is the same trap: a frame too
	// short to walk still advanced the clip.
	//
	// Unguarded, like retail's: it asks on every frame past the cycle, and zeroing an already-zero
	// counter is not an event.
	if (Phase.Cycle >= FElysiumCombatCharacter::MeleeSwingCompletionPercent)
	{
		Attacker->ClearHitBuildup();
	}

	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		Swing.ResetWalk();
		return;   // a phase without an embodiment cannot happen; the guard costs nothing
	}

	// The records are filed under the ATTACKING BODY's own stem, the same key the blocked reaction
	// is addressed by: the exporter writes one row per label into the body's own clip slice,
	// carrying whatever the owning bank's sequence declared.
	const TArray<FElysiumSwingRecord>* Records =
		Embodiment->NpcClipSwings(Attacker->ModelStem(), Swing.ClipLabel);
	if (Records == nullptr || Records->IsEmpty())
	{
		// **No records is no contact, and it is retail's own shape** — the swing animates and
		// touches nothing. It is also what an export predating the `swings` column gives every clip,
		// so it is reported once per clip at Verbose: an authored absence is not a failure, and a
		// warning here would fire for every swing of every body on an older corpus.
		if (ShouldReportOnce(FString::Printf(TEXT("noswings:%s@%s"), *Swing.ClipLabel,
			*Attacker->ModelStem())))
		{
			UE_LOG(LogElysiumWeapon, Verbose,
				TEXT("%s: attack clip '%s' on '%s' declares no swing-contact records, so this swing "
					"opens no contact window"),
				*DebugString(), *Swing.ClipLabel, *Attacker->ModelStem());
		}
		Swing.ResetWalk();
		return;
	}

	// A re-armed clip is a new play, and the walk starts over on it: a hit list or a previous limb
	// position carried across would belong to a different pass through the same timeline.
	if (Swing.WalkPlayId != Phase.PlayId)
	{
		Swing.ResetWalk();
		Swing.WalkPlayId = Phase.PlayId;
	}

	// The batch decision, and nothing before it is recorded.
	//
	// Retail's update walks on a server tick, and a tick is never shorter than a sub-step: its
	// `dt <= 0` exit is the only path that does not write the stored timestamp, so a call either
	// records nothing or walks a whole tick. Under a render clock the same rule is accumulation —
	// a frame too short to walk records NOTHING (no stamp, no cursor advance, no segment cache), so
	// its span survives into the batch that does run and the cycle interval below still covers
	// everything the clip passed through. Reproducing retail's own sub-step branch literally would
	// discard the span instead, and melee would stop landing above 100 fps on a clock retail never
	// had. `ElysiumSwing`'s header states the reconciliation in full.
	Swing.PendingSeconds += FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.0f
		? DeltaSeconds : 0.0f;

	bool bSpanClamped = false;
	const float BatchSeconds = ElysiumSwing::ClampBatchSpan(Swing.PendingSeconds, bSpanClamped);
	const int32 SubSteps = ElysiumSwing::SubStepCount(BatchSeconds);
	if (SubSteps < 1)
	{
		return;   // not a batch yet: record nothing, and let the span accumulate
	}
	// The batch consumes the whole accumulated span. There is no remainder to carry: the sub-step
	// COUNT is what the span buys, and the ground the sub-steps cover is the cycle interval below,
	// which already reaches back to the last batch.
	Swing.PendingSeconds = 0.0f;
	if (bSpanClamped && ShouldReportOnce(FString::Printf(TEXT("swingbatch:%s"), *ClassName())))
	{
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s: a frame longer than the stated %.0f ms cap reached the contact walk — the "
				"batch covers the cycle it reached at the cap's sub-step count rather than at its "
				"own, because retail's tick clock could not hand its own update that span either"),
			*DebugString(), ElysiumSwing::MaxBatchSeconds * 1000.0f);
	}

	// The swing's own identity, copied out of the transaction before the first commit: a contact
	// can retire the transaction, and the walk must not read a cleared one back through it.
	FSwingContact Contact;
	Contact.ModeIndex = Swing.ModeIndex;
	Contact.Serial = Swing.Serial;
	Contact.ClipLabel = Swing.ClipLabel;
	Contact.ClipOwnerStem = Swing.ClipOwnerStem;

	// **The roll and the notice are staged here, on the first BATCHED frame, before any contact
	// test.** Retail's order, and the whole reason the contact below consumes a record instead of
	// rolling one: the dice are spent when the swing opens, not when it lands. A frame that ran no
	// batch is retail's `dt <= 0` call, which reaches neither.
	if (!Swing.bContactStaged)
	{
		Swing.bContactStaged = true;
		StageSwingOpposedRoll(*Attacker, Contact);
	}

	// Where every record's segment is RIGHT NOW, in the attacker's own frame. Held per record rather
	// than per bone so the interpolation below is a straight index walk; the bone queries themselves
	// are cached by name, because a 17-record swing names two or three bones.
	const FTransform NowFrame(FRotator(Attacker->Angles.X, Attacker->Angles.Y, Attacker->Angles.Z),
		Attacker->Origin);
	TMap<FString, FTransform> BoneCache;
	TArray<TPair<FVector, FVector>> NowSegments;
	NowSegments.SetNum(Records->Num());
	TBitArray<> HasSegment(false, Records->Num());
	for (int32 Index = 0; Index < Records->Num(); ++Index)
	{
		const FElysiumSwingRecord& Record = (*Records)[Index];
		if (!Record.HasSegment())
		{
			// A record naming no bone, or whose two endpoints coincide, has no limb to sweep. That is
			// an authored defect rather than an absence — every shipped record states both — so it is
			// named once per (clip, record) and produces no contact.
			if (ShouldReportOnce(FString::Printf(TEXT("swingseg:%s@%s#%d"), *Swing.ClipLabel,
				*Attacker->ModelStem(), Index)))
			{
				UE_LOG(LogElysiumWeapon, Warning,
					TEXT("%s: swing record %d of '%s' on '%s' states no contact segment (bone '%s') — "
						"it can never contact anything"),
					*DebugString(), Index, *Swing.ClipLabel, *Attacker->ModelStem(), *Record.Bone);
			}
			continue;
		}
		const FTransform* BoneWorld = BoneCache.Find(Record.Bone);
		if (BoneWorld == nullptr)
		{
			FTransform Resolved;
			if (!Embodiment->GetBodyBoneTransform(Attacker->GetSkeletalBody(), Record.Bone, Resolved))
			{
				// The pose layer has no such bone on this body. Same severity and same reason: the
				// record names a bone of its own model, so a body that cannot answer is a defect in
				// the pair rather than a clip that simply declares nothing.
				if (ShouldReportOnce(FString::Printf(TEXT("swingbone:%s@%s"), *Record.Bone,
					*Attacker->ModelStem())))
				{
					UE_LOG(LogElysiumWeapon, Warning,
						TEXT("%s: swing clip '%s' sweeps bone '%s', which body '%s' does not carry — "
							"that record produces no contact"),
						*DebugString(), *Swing.ClipLabel, *Record.Bone, *Attacker->ModelStem());
				}
				continue;
			}
			BoneWorld = &BoneCache.Add(Record.Bone, Resolved);
		}
		// The endpoints are bone-local Unreal centimetres and the bone frame is the live one, so the
		// placement is one transform and no conversion (the `UE_` exporter already stated them in
		// this frame). They are then expressed against the attacker's own frame, which is what lets
		// the sub-steps carry the limb on an interpolated root.
		NowSegments[Index] = TPair<FVector, FVector>(
			NowFrame.InverseTransformPosition(BoneWorld->TransformPosition(Record.ACm)),
			NowFrame.InverseTransformPosition(BoneWorld->TransformPosition(Record.BCm)));
		HasSegment[Index] = true;
	}

	// The first batch has no previous position: it covers the instant the clip stands on and sweeps
	// nothing, which is what an unprimed cursor means everywhere else in this runtime.
	bool bPrimed = Swing.PrevCycle >= 0.0f && Swing.PrevSegmentsLocal.Num() == Records->Num();

	// The discontinuity guard (OURS).
	// Between two batches an engine event can move the attacker or its pose by a distance no swing
	// produces: a teleport, a map travel, a scene handing the body back, a pose-layer hitch. Swept
	// as motion, that reads as a limb crossing the whole intervening space — it would land on every
	// bystander standing on the line, and the sweep's own patch would be metres wide. Re-priming
	// instead throws away only the crossing: the batch sweeps nothing and the next one starts from
	// where the limb actually is. The hit lists survive, so nothing re-lands because of it.
	if (bPrimed)
	{
		bool bJumped = ElysiumSwing::ExceedsBatchTravel(Swing.PrevOrigin, Attacker->Origin);
		for (int32 Index = 0; !bJumped && Index < Records->Num(); ++Index)
		{
			bJumped = HasSegment[Index]
				&& (ElysiumSwing::ExceedsBatchTravel(
						Swing.PrevSegmentsLocal[Index].Key, NowSegments[Index].Key)
					|| ElysiumSwing::ExceedsBatchTravel(
						Swing.PrevSegmentsLocal[Index].Value, NowSegments[Index].Value));
		}
		if (bJumped)
		{
			if (ShouldReportOnce(FString::Printf(TEXT("swingjump:%s"), *ClassName())))
			{
				UE_LOG(LogElysiumWeapon, Verbose,
					TEXT("%s: the swinging body moved further than the stated %.0f cm between two "
						"contact batches — the walk re-primes rather than sweeping through the gap"),
					*DebugString(), ElysiumSwing::MaxBatchTravelCm);
			}
			Swing.RePrimePosition();
			bPrimed = false;
		}
	}

	const float PrevCycle = bPrimed ? Swing.PrevCycle : Phase.Cycle;
	const FTransform PrevFrame = bPrimed
		? FTransform(Swing.PrevAngles, Swing.PrevOrigin)
		: NowFrame;

	// A record whose window is closed over everything this batch covered forgets whom it has hit, so
	// a later group of the same swing — a `2COMBO`'s second half — lands again.
	if (Swing.RecordHits.Num() != Records->Num())
	{
		Swing.RecordHits.SetNum(Records->Num());
	}
	ElysiumSwing::ClearClosedRecords(*Records, PrevCycle, Phase.Cycle, Swing.RecordHits);

	TArray<ElysiumSwing::FInterval> Intervals;
	ElysiumSwing::SubStepIntervals(PrevCycle, Phase.Cycle, SubSteps, Intervals);

	TArray<FElysiumEntityHandle> Contacts;
	for (int32 Step = 0; Step < Intervals.Num() && Swing.bActive; ++Step)
	{
		const float U0 = static_cast<float>(Step) / static_cast<float>(SubSteps);
		const float U1 = static_cast<float>(Step + 1) / static_cast<float>(SubSteps);
		const FTransform FrameAt0 = ElysiumSwingFrameAt(PrevFrame, NowFrame, U0);
		const FTransform FrameAt1 = ElysiumSwingFrameAt(PrevFrame, NowFrame, U1);

		for (int32 Index = 0; Index < Records->Num() && Swing.bActive; ++Index)
		{
			if (!HasSegment[Index])
			{
				continue;
			}
			const FElysiumSwingRecord& Record = (*Records)[Index];
			if (!ElysiumSwing::WindowOverlaps(Record.Start, Record.End,
				Intervals[Step].Start, Intervals[Step].End))
			{
				continue;
			}

			const TPair<FVector, FVector>& From = bPrimed
				? Swing.PrevSegmentsLocal[Index] : NowSegments[Index];
			const TPair<FVector, FVector>& To = NowSegments[Index];

			FElysiumSwingSweep Sweep;
			Sweep.Attacker = Attacker->Handle;
			Sweep.PrevA = FrameAt0.TransformPosition(FMath::Lerp(From.Key, To.Key, U0));
			Sweep.PrevB = FrameAt0.TransformPosition(FMath::Lerp(From.Value, To.Value, U0));
			Sweep.CurA = FrameAt1.TransformPosition(FMath::Lerp(From.Key, To.Key, U1));
			Sweep.CurB = FrameAt1.TransformPosition(FMath::Lerp(From.Value, To.Value, U1));

			Embodiment->QuerySwingContacts(Sweep, Contacts);
			for (const FElysiumEntityHandle& Hit : Contacts)
			{
				if (!Swing.bActive)
				{
					// The same guard the two loops above carry, and this is where it has to hold: the
					// hit list is the transaction's own storage, so a commit that retired the swing
					// leaves nothing to index.
					break;
				}
				if (ElysiumSwing::IsMarked(Swing.RecordHits[Index], Hit))
				{
					continue;   // hit-once: this record, or one sharing its window, already landed
				}
				FElysiumEntity* VictimEnt = World ? World->Resolve(Hit) : nullptr;
				FElysiumCombatCharacter* Victim = VictimEnt ? VictimEnt->AsCombatCharacter() : nullptr;
				if (!Victim || !IsAliveForCombat(*Victim))
				{
					continue;   // an ordinary negative: the query answers geometry, not eligibility
				}
				// The mark goes down BEFORE the commit, and across every record sharing this one's
				// window: a contact that kills its victim must still count as this swing's one hit.
				ElysiumSwing::MarkHit(*Records, Index, Hit, Swing.RecordHits);
				// The victim's hit-buildup counter, raised by ANY attacker's landed hit — this is the
				// site retail calls on the victim with the attacker passed and never read. It sits
				// here rather than inside the contact because retail raises it on the hit landing,
				// not on the reaction it selects: a hit that is blocked still counts.
				Victim->RaiseHitBuildup();
				// WHICH record reached this body, stamped per contact rather than per swing: the
				// knockback the victim answers with is authored on the record, so a contact that
				// could not name its own would have no candidate table to read. The mark above is
				// spread across every record sharing the window, but the one that LANDED is this
				// one, and it is the one whose table applies.
				Contact.RecordIndex = Index;
				MeleeContact(*Attacker, *Victim, Contact);
			}
		}
	}

	Swing.PrevCycle = Phase.Cycle;
	Swing.PrevSegmentsLocal = MoveTemp(NowSegments);
	Swing.PrevOrigin = Attacker->Origin;
	Swing.PrevAngles = FRotator(Attacker->Angles.X, Attacker->Angles.Y, Attacker->Angles.Z);
}

void FElysiumWeapon::MeleeContact(FElysiumCombatCharacter& Attacker, FElysiumCombatCharacter& Victim,
	const FSwingContact& Contact)
{
	const int32 ModeIndex = Contact.ModeIndex;
	const FElysiumWeaponContext Context = FElysiumWeaponContext::FromCharacter(Victim);
	const FElysiumDmg& ModeDmg = DamageForMode(ModeIndex);

	// The opposed record, CONSUMED from the defender.
	// It was staged on this victim on the swing's first batched frame, before any contact test, by
	// `StageSwingOpposedRoll` — which is where retail rolls it. Nothing is rolled here.
	//
	// The lookup is scoped to THIS SWING's serial, which is what makes the sentence above true. A
	// sweep can reach a body the swing's own 60-unit roll query never selected; without the scope it
	// would be judged on whatever margin an earlier swing rolled against it, which is a hit nobody
	// rolled for. Retail closes the same hole from the attacker's end, by clearing its record array
	// at every swing start. Either way the answer here is: no record of this swing, no contact —
	// and inventing one would put the dice back on the path they were taken off.
	const FElysiumMeleeRoll* Staged = Victim.FindMeleeRoll(Attacker.Handle, Contact.Serial);
	if (Staged == nullptr)
	{
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s -> %s: swing #%d swept this body and staged no opposed record against it — "
				"no contact"),
			*Attacker.DebugString(), *Victim.DebugString(), Contact.Serial);
		return;
	}
	const FElysiumMeleeRoll Roll = *Staged;

	// The `rules.txt` margin classifier.
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
	// SEAM — the blocked-contact `Dexterity` bonus soak re-roll at `0x10160BC0` and the knockback
	// impulse still belong to a later cycle. The re-roll is a SOAK rule, not a reaction one: it adds
	// bonus soak dice from attribute slot 2 and re-classifies, so it changes the NUMBER the block
	// family then reacts to. It lands with the soak work rather than here, which owns the pose and
	// not the number (`docs/vtmb/combat-and-damage.md:485-488`, `docs/project/plans/animation.md:69-70`).
	//
	// The attacker's own classification stays on this line rather than branching the reaction below:
	// retail gives the attacker ONE blocked-reaction source — the activity its swing sequence stores
	// — and no blocked/blocked-major split over it. The band is diagnosis, not a fork.
	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s -> %s melee margin %d (lethality %d - defense %d - soak %d): attacker %s, defender %s"),
		*Attacker.DebugString(), *Victim.DebugString(), Margin, Roll.Lethality, Roll.Defense,
		Roll.Soak, ElysiumWeapons::AttackerReactionName(AttackerReaction),
		ElysiumWeapons::DefenderReactionName(DefenderReaction));

	// The two block reactions.
	// Both callbacks fire on a blocked contact, and both run BEFORE the damage test below: retail's
	// blocked path plays its reactions and only then asks whether positive damage remains
	// (`docs/vtmb/combat-and-damage.md` § "Block and stagger reactions").
	// Armed by the knockback branch below and spent past the health commit — see there for why
	// the two halves of this contact sit on opposite sides of it.
	bool bKnockbackPending = false;

	if (WasMeleeBlocked(Attacker, Victim, DefenderReaction))
	{
		const double Now = World->NowSeconds();

		// The defender (`0x10160BC0`). Class 3 is the block-stagger band and plays `ACT_BLOCK_HEAVY`;
		// the other blocked classes play `ACT_BLOCK`. A class that names none is an ordinary answer,
		// not a miss — a player blocking through a hit/knockback margin is exactly that case.
		if (const TCHAR* BlockActivity = ElysiumReactions::BlockActivityFor(DefenderReaction))
		{
			FElysiumReactionPlayRequest Block;
			Block.Activity = BlockActivity;
			// The ideal-activity request walks the cast chain's weapon ladder, and it has to: the
			// corpus carries bare `ACT_BLOCK` on one stem against `ACT_BLOCK_fists` and its siblings
			// on 155, so a request refused the ladder would resolve nothing on almost every body.
			Block.bAllowFallbackLadder = true;
			// No stated blend — the block takes its resolved clip's own authored fade, which is the
			// ordinary sequence-blend rule. Only retail's flinch gesture hard-codes a pair.
			float Held = 0.0f;
			if (Victim.PlayReactionActivity(Block, &Held))
			{
				// OURS: the reaction owns the base channel for as long as it plays, so the flinch the
				// damage below may commit yields to it rather than replacing it mid-pose. The reason
				// this hold exists at all is on `FElysiumCombatCharacter::MeleeReactionHoldsBaseUntil`.
				Victim.HoldBaseForMeleeReaction(Now + static_cast<double>(Held));
			}
		}

		// The attacker (`0x10160D00`). The activity is the one the ATTACKER's own swing sequence
		// descriptor stores — authored per swing, never chosen from a movement direction — falling
		// back to `ACT_BLOCKED_REACTION_RIGHT` when that sequence names none.
		//
		// The key is the attacking BODY's stem plus the swing's label, which is where the column is
		// filed: the exporter writes one row per label into the body's OWN clip slice, carrying the
		// blocked reaction of whichever bank owns the sequence. `Contact.ClipOwnerStem` names that
		// bank and rides the log line alone — a shared-bank body and its bank are what a missing
		// column has to be diagnosed across, but neither addresses the row.
		FString Blocked;
		if (IElysiumEmbodiment* Embodiment = World->Embodiment();
			Embodiment && !Contact.ClipLabel.IsEmpty())
		{
			Blocked = Embodiment->NpcClipBlockedReaction(Attacker.ModelStem(), Contact.ClipLabel);
		}
		const bool bAuthored = !Blocked.IsEmpty();
		if (!bAuthored)
		{
			Blocked = ElysiumReactions::DefaultBlockedReaction;
		}
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s blocked by %s -> attacker plays %s (%s; swing clip '%s' off '%s')"),
			*Attacker.DebugString(), *Victim.DebugString(), *Blocked,
			bAuthored ? TEXT("authored") : TEXT("fallback"),
			Contact.ClipLabel.IsEmpty() ? TEXT("(none)") : *Contact.ClipLabel,
			Contact.ClipOwnerStem.IsEmpty() ? TEXT("(none)") : *Contact.ClipOwnerStem);

		FElysiumReactionPlayRequest Reaction;
		Reaction.Activity = Blocked;
		Reaction.bAllowFallbackLadder = true;
		float AttackerHeld = 0.0f;
		if (Attacker.PlayReactionActivity(Reaction, &AttackerHeld))
		{
			// The same hold the defender takes above, for the same reason and on the same channel:
			// the blocked reaction is a base-channel pose, so a blow landing on the attacker while
			// it recoils yields rather than flinching over a pose already on screen. The two sides
			// of one exchange cannot own the channel on different terms.
			Attacker.HoldBaseForMeleeReaction(Now + static_cast<double>(AttackerHeld));
		}
	}
	else if (DefenderReaction == EElysiumMeleeDefenderReaction::HitKnockback)
	{
		// The grounded knockback.
		// "A stronger unblocked result takes the separate normal-hit or knockback callbacks"
		// (`docs/vtmb/combat-and-damage.md` § "Block and stagger reactions"), so this is the sibling
		// branch of the blocked callbacks above — but it does NOT sit at the same point in the
		// contact order, and the difference is recovered rather than chosen.
		//
		// **The blocked callbacks run before the health commit; the knockback runs after it.**
		// `CBaseCombatWeapon::FUN_102579F0` puts its two blocked slots ahead of
		// `DispatchTraceAttack` and its knockback slots behind it. That ordering is load-bearing:
		// it is the whole of why a killing blow is never knocked back — the health commit has
		// already run and the victim reads dead by the time eligibility is asked. So the branch is
		// only ARMED here, and the call is made past the commit below.
		//
		// **SEAM — grounded cells only.** The nine-activity flying chain is the other half of this
		// outcome. Its contract is recovered whole — a two-stage velocity assignment with a
		// one-think delay, and a land/wall terminator — and reproducing it needs a motor verb that
		// carries a ballistic body, which this seam does not have yet. Nothing here moves the
		// victim; the pose and the facing snap it needs are what is reproduced.
		//
		// `Unclassified` is deliberately NOT a knockback: a margin table that never loaded names no
		// band, and the classifier's own warning above already reported it.
		//
		bKnockbackPending = true;
	}

	// A record that is not damaging commits no damage. Blocked does NOT mean zero damage: what
	// decides is the margin, and a positive one carries on even when a block reaction played.
	//
	// It is a BRANCH rather than an early return, because the knockback below is not gated on the
	// damage: retail's knockback block runs on any unblocked contact, and only the health commit
	// stands between them.
	if (Margin > 0)
	{
	// The damage commit.
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

	// The knockback, AFTER the health commit.
	// Retail's own order, and the reason a killing blow is not thrown: the commit above may have
	// killed this victim, and `IsKnockbackAllowed`'s alive term then refuses. A body killed by a
	// swing dies where it stands and hands off to the corpse path.
	//
	// The consequence for the reaction channel is retail's too. The commit's own `DamageFlinch` has
	// already played by now, so the knockback TAKES the base channel from it rather than the flinch
	// yielding to a knockback already on screen — which is what `TASK_MELEE_KNOCKBACK` does when it
	// clears the three flinch slots before restarting the ideal activity.
	//
	// The authored record that landed this contact rides in, because the cell the victim answers
	// with is stated ON it rather than derived from the direction alone.
	if (bKnockbackPending)
	{
		KnockbackContact(Attacker, Victim, ResolveSwingRecord(Attacker, Contact));
	}
}

const FElysiumSwingRecord* FElysiumWeapon::ResolveSwingRecord(
	const FElysiumCombatCharacter& Attacker, const FSwingContact& Contact) const
{
	if (Contact.RecordIndex == INDEX_NONE)
	{
		return nullptr;
	}
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		return nullptr;
	}
	// The same key the walk read the array under: the records are filed against the ATTACKING
	// body's own stem, carrying whatever the owning bank's sequence declared.
	const TArray<FElysiumSwingRecord>* Records =
		Embodiment->NpcClipSwings(Attacker.ModelStem(), Contact.ClipLabel);
	return Records && Records->IsValidIndex(Contact.RecordIndex)
		? &(*Records)[Contact.RecordIndex] : nullptr;
}

void FElysiumWeapon::KnockbackContact(FElysiumCombatCharacter& Attacker,
	FElysiumCombatCharacter& Victim, const FElysiumSwingRecord* Record)
{
	// The player's knockback is a VIEW KICK — a separate reaction reading `KnockbackPreventTime` as
	// its refractory — and that system is not built. Refusing it here rather than playing an NPC
	// grounded cell on the player body keeps the two apart; the omission is named once.
	if (IsPlayerSide(Victim))
	{
		if (ShouldReportOnce(TEXT("knockback_player_view_kick")))
		{
			UE_LOG(LogElysiumWeapon, Verbose,
				TEXT("a hit/knockback landed on the player: retail answers it with the player view "
					"kick, which is not built, so no knockback reaction is produced"));
		}
		return;
	}

	// **The hit-buildup gate.** Retail admits the knockback on `counter <= npc_hit_buildup_amount`
	// OR the landing swing record's `+0xBA == 2` unconditional marker
	// (`docs/vtmb/combat-and-damage.md` → "Who may be knocked back"). The two halves live in
	// different places on purpose: the counter is a property of the VICTIM and the marker one of the
	// ATTACK, so they are resolved here and handed to the rule as one answer.
	//
	// **RECOVERED — the raise happens before this read.** `CBaseCombatWeapon::FUN_102579F0` is the
	// one body that both commits a melee blow and tests this counter, and its order is settled:
	// `DispatchTraceAttack` — the damage commit, and the chain the only increment site
	// (`0x1029F800`) hangs off — runs well before the slot-326 buildup test near its tail, and no
	// increment appears anywhere in that function. So the counter a blow tests already carries that
	// blow. With the default of `2` a victim is thrown on two hits and stands its ground on the
	// third, which is the widely reported "about two hits and then the NPC stops flying".
	const bool bUnconditional = Record != nullptr
		&& Record->Ba == ElysiumReactions::KnockbackUnconditionalMarker;
	const bool bBuildupAdmits = bUnconditional
		|| Victim.HitBuildupCount <= FElysiumCombatCharacter::HitBuildupAdmitAtOrBelow;

	// Eligibility, whole. `IsAliveForCombat` is retail's own dead-victim refusal, which it spells as
	// `Health == Max_Health` over damage-taken; the template half is the authored
	// `General/Disallow_Knockbacks` that zombies, cabbies and the tutorial cast wear; and the class
	// bypass is the `CNPC_VTzimisceRunner` virtual, which skips both of the others.
	if (!ElysiumReactions::IsKnockbackAllowed(IsAliveForCombat(Victim),
		Victim.DisallowsKnockbacks(), bBuildupAdmits, Victim.BypassesKnockbackEligibility()))
	{
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s -> %s hit/knockback: %s is not eligible to be knocked back"),
			*Attacker.DebugString(), *Victim.DebugString(), *Victim.DebugString());
		return;
	}

	const float VictimYaw = ElysiumSkeletalBasis::FromSourceAngles(Victim.Angles).Yaw;
	ElysiumReactions::FElysiumKnockback Knockback;
	ElysiumReactions::BuildKnockback(
		ElysiumReactions::KnockbackAwayFrom(Attacker.Origin, Victim.Origin), VictimYaw, Knockback);

	// The cell, off the attack's own authored candidate table.
	// Retail selects out of the swing record's four direction buckets — up to four candidates each,
	// rotated by the record's `+0xB8` byte, one drawn with `RandomInt`
	// (`docs/vtmb/combat-and-damage.md` → "The authored table lives in the swing record"). The
	// candidate NAME is the answer: it already states its own size, height and direction, which is
	// how the `SMALL` family and the two `LOW_BACK` cells are reached at all.
	//
	// The draw is the session's own `Reaction` stream — the same one the flinch's coin and jitter
	// and the shared reaction path's weighted-variant pick take, because they are one family.
	FString Activity;
	const bool bAuthored = !Knockback.bFallbackCell && Record != nullptr
		&& ElysiumReactions::SelectKnockbackActivity(*Record, Knockback.Direction,
			ElysiumRng::Stream(EElysiumRngStream::Reaction), Activity);
	if (!bAuthored)
	{
		// Retail's own no-list fallback, and three things reach it: a degenerate classification, a
		// contact carrying no record, and a record whose rotation byte or selected bucket states
		// nothing (639 records fill fewer than four buckets). None is a defect — each is a shape the
		// shipped data has — so this is Verbose and keyed by the attacking clip rather than warned.
		Activity = ElysiumReactions::FallbackGroundedKnockbackActivity;
		// The key names all THREE routes, because the message does. Keying only on
		// record-versus-no-record folded the degenerate-classification case in with the
		// no-rotation one, so the first coincident-origin contact on a body silenced every later
		// contact whose record genuinely states no rotation — 639 shipped records.
		const TCHAR* const Route = Knockback.bFallbackCell ? TEXT("degenerate")
			: Record != nullptr ? TEXT("norotation") : TEXT("norecord");
		if (ShouldReportOnce(FString::Printf(TEXT("kbtable:%s@%s"), *Attacker.ModelStem(), Route)))
		{
			UE_LOG(LogElysiumWeapon, Verbose,
				TEXT("%s: a hit/knockback resolved no authored candidate (%s) and took the no-list "
					"fallback cell '%s'"),
				*DebugString(),
				Knockback.bFallbackCell ? TEXT("degenerate classification")
					: Record != nullptr ? TEXT("the record states no rotation or an empty bucket")
					: TEXT("the contact carries no swing record"),
				ElysiumReactions::FallbackGroundedKnockbackActivity);
		}
	}

	// The yaw snap, BEFORE the clip is asked for, which is retail's own order: the authored cell is a
	// model-space direction, so the body has to be facing the way that direction means before it
	// plays. The door is the ordinary substrate facing writer — the same one the feed transaction
	// aligns its pair with — so the authoritative `Angles` field moves and the body follows it.
	// `Angles.Y` is Source yaw, which this runtime carries negated relative to Unreal's, so the write
	// is negated once, here.
	FVector Facing = Victim.Angles;
	Facing.Y = -Knockback.SnapYawDegrees;
	Victim.SetRuntimeAngles(Facing);

	FElysiumReactionPlayRequest Reaction;
	Reaction.Activity = Activity;
	// The ideal-activity request walks the weapon ladder's availability probe, and it HAS to.
	// `ElysiumWeaponActivityTables.cpp` declares all ten grounded cells as bases, and thirteen weapon
	// ladders carry an explicit block-1 EXCEPTION row translating each of them to its
	// `..._MELEESHARED_ONEHAND` spelling — a stated literal, not an inferred append. The corpus
	// authors that spelling for the OLDER twelve knockback labels only; the ten bare cells exist bare
	// on 155 bodies and suffixed on none (`docs/vtmb/animation_and_movers.md` § "The knockback and
	// death corpus"). So for a bare cell an armed body's translated answer resolves nothing, and the
	// probe's fourth rung — the original request — is the only thing that reaches the clip.
	//
	// **Both halves of that now matter, because the authored table reaches both vocabularies.** A
	// candidate naming one of the older twelve labels DOES have a suffixed spelling to translate
	// into, so the probe's earlier rungs answer it; one naming a bare cell falls to the fourth rung
	// as before. The flag serves both, which is why it is unconditional rather than keyed on which
	// vocabulary the candidate came from.
	Reaction.bAllowFallbackLadder = true;
	// No steering angle. A knockback is four authored cells, not a fan: the direction is IN the cell
	// name and the yaw snap above is what aims it, so `hit_yaw` stays at its resting value the way
	// the block family's does.
	//
	// No stated blend either — all 1,550 grounded knockback rows set `flags & 0x2`, so the whole
	// family is a hard cut and the resolved clip's own fade is already zero.
	float Held = 0.0f;
	if (!Victim.PlayReactionActivity(Reaction, &Held))
	{
		// The named miss is already on the resolver's own selection record.
		return;
	}

	// The base channel is held for as long as the cell runs — the same claim the block family takes,
	// on the same channel and for the same reason: the flinch the damage below commits yields to a
	// knockback already on screen rather than replacing it.
	Victim.HoldBaseForMeleeReaction(World->NowSeconds() + static_cast<double>(Held));

	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s -> %s knocked back: %s (%s, away yaw %.1f, relative %.1f, snapped to %.1f%s, "
			"holds %.3f s)"),
		*Attacker.DebugString(), *Victim.DebugString(), *Activity,
		bAuthored ? TEXT("authored candidate") : TEXT("no-list fallback"),
		Knockback.AwayWorldYawDegrees, Knockback.RelativeYawDegrees, Knockback.SnapYawDegrees,
		Knockback.bFallbackCell ? TEXT(", degenerate classification") : TEXT(""), Held);
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
	// **The one place the player and the cast genuinely diverge, and they diverge in the ACTIVITY as
	// well as the channel.** The player's reload is `CWeaponRanged`'s overlay: `SetAnimation` hands
	// `apply_player_activity_and_sequence` a base of -1 and `ACT_RELOAD_LAYER` as the layer, so slot 0
	// composes the reload over whatever gait owns the base and the legs keep walking. The cast has no
	// such path — `ACT_RELOAD_LAYER` is requested nowhere in NPC code — and reloads through
	// `CAI_BaseNPC::StartTask`'s `TASK_RELOAD`, which calls `RestartIdealActivity(ACT_RELOAD)`: a
	// full-body base pose that the body's own locomotion classifier stops answering under.
	//
	// Asking one activity on two channels would be wrong in both directions: `ACT_RELOAD_LAYER` on the
	// cast's base channel collapses the body on the layer clip's masked bones, and `ACT_RELOAD` on the
	// player's slot composes a full-body pose through a partial-body mechanism.
	//
	// **The two arms take different BANDS, because only one of them is arbitrated.** The cast's claim
	// is `Scripted` — the same band `BeginMeleeSwing` states, for the identical reason: it is the
	// first row above the `LocomotionTravel` publish a moving body makes every anim tick, and the
	// smallest band that is. `ArbitrateBase` does not merely yield a lower claim to that publish, it
	// CONSUMES it, and the claim is what carries `Activity="ACT_RELOAD"` into `ForcedIdealActivity` —
	// so an `Ambient` reload would hand a gunman who steps sideways mid-magazine both his pose and his
	// forced ideal activity back to the gait ladder, while retail's `RestartIdealActivity(ACT_RELOAD)`
	// writes `m_IdealActivity` outright and a walking NPC keeps reloading. It stops BELOW `Reaction`
	// on purpose: a reload must not displace a standing flinch.
	//
	// The player's stays `Ambient` because nothing arbitrates it. `ArbitrateSlot` ranks nothing — the
	// overlay channel has exactly one producer family — so the band on that claim decides no contest.
	const bool bPlayerReload = IsPlayerSide(*Char);
	const float Seconds = bPlayerReload
		? ResolveAndPlay(GActReloadLayer, EElysiumAnimPriority::Ambient,
			EElysiumAnimChannel::UpperBody, Mode ? *Mode : EmptyMode, Label)
		: ResolveAndPlay(GActReload, EElysiumAnimPriority::Scripted,
			EElysiumAnimChannel::Base, Mode ? *Mode : EmptyMode, Label);
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

// Registration — `CWeapon`, the chain node the weapon families register under.

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
