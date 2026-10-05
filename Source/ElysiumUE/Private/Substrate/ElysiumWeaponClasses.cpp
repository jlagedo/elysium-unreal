// The weapon controller over the item entity and the damage spine.
//
// `docs/vtmb/combat-and-damage.md` is the specification and owns every fact below. Nothing here
// re-implements the descriptor, the soak resolver or the health commit: a weapon decides lethality, defense and the
// opposed margin, and hands the result to `FElysiumCombatCharacter::TakeDamage`, which is the one
// typed route into `ElysiumDamage::Apply` and `CommitDamage`.

#include "Substrate/ElysiumWeaponClasses.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumAnimatingOverlay.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumComboChain.h"             // the authored combo block and its busy/window rules
#include "ElysiumDecalSubsystem.h"         // ElysiumImpactDecals::PoolSize — the shot's variation roll
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSessionSubsystem.h"
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
#include "Substrate/ElysiumMiscFlags.h"       // `AddMiscFlag 0x1033c6b0`, `Shot 0x102387b0` step 2
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
#include "Visual/ElysiumAnimationPick.h"
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
	UElysiumSessionSubsystem* GameState = Char.World ? Char.World->GetGameState() : nullptr;
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

	float AttackSpeedScale(const FElysiumCombatCharacter& SpeedOwner)
	{
		if (const FElysiumNpc* const SpeedNpc = SpeedOwner.AsNpc())
			return SpeedNpc->NpcSpeedScale; // 0x103ea28b m_flSpeedScale +0x1488
		return 1.f; // 0015 player speed-scale input seam; default +0x1488

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
		return Event == 3001 || (Event >= 3030 && Event <= 3037)
			|| (Event >= 3039 && Event <= 3044) || Event == 3047; // 0x103ea5b0
	}

	bool IsSwallowedMeleeEvent(int32 Event)
	{
		// `0x103ea5b0`'s own swallow set, exactly: the two body/swish ids and the ranged shot ids a
		// melee clip may still carry. Nothing acts on them, and reaching the census with them would
		// put a recovered no-op on the work list.
		return Event == 3003; // 0x103ea5b0; every trigger above calls NPC PrimaryAttack
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

	// --- The constructor range words (0018 story 8, findings R3) ---------------------------------

	namespace
	{
		// The words each retail constructor leaves, in `+0x8b8 / +0x8bc / +0x8c0 / +0x8c4` order.
		constexpr FRangeWords GRangeWordsBase    { 65.f, 65.f, 1024.f, 1024.f };  // 0x10250ac0
		constexpr FRangeWords GRangeWordsRanged  { 150.f, 65.f, 1024.f, 300.f };  // 0x10238070
		constexpr FRangeWords GRangeWordsMelee   { 0.f, 0.f, 50.f, 50.f };        // 0x103e9ac0
		constexpr FRangeWords GRangeWordsMelee108{ 0.f, 0.f, 108.f, 108.f };      // 0x103e8a30 / 0x103ec870
		constexpr FRangeWords GRangeWordsMelee500{ 0.f, 0.f, 500.f, 500.f };      // 0x103ec2b0
		constexpr FRangeWords GRangeWordsNone    { 0.f, 0.f, 0.f, 0.f };          // IArmor / IGeneric / IWritten

		struct FClassRangeRow
		{
			const TCHAR* Classname;
			FRangeWords Words;
		};

		// The factories whose class the record's `item_type` would name wrongly. Every other
		// `item_w_*` factory builds the class its type implies (`weapon_firearm` -> `CWeaponRanged`,
		// `weapon_melee` -> `CWeaponMelee`, `weapon_thrown` -> `CWeaponIThrown`).
		const FClassRangeRow GClassRangeRows[] = {
			{ TEXT("item_w_unarmed"),             GRangeWordsBase },      // 0x1000373d CWeaponUnarmed (hidden)
			{ TEXT("item_w_tzimisce2_head"),      GRangeWordsRanged },    // 0x100047c3 CWeaponRanged (hidden)
			{ TEXT("item_w_tzimisce_melee"),      GRangeWordsMelee108 },  // 0x1000f2bd (powerup)
			{ TEXT("item_w_mingxiao_tentacle"),   GRangeWordsMelee108 },  // 0x10011ce3 (generic)
			{ TEXT("item_w_mingxiao_melee"),      GRangeWordsMelee500 },  // 0x1000d035
			{ TEXT("item_w_claws_ghoul"),         GRangeWordsMelee },     // 0x10015726 CWeaponMelee (generic)
			{ TEXT("item_w_claws_protean4"),      GRangeWordsMelee },     // 0x100060b4 (generic)
			{ TEXT("item_w_claws_protean5"),      GRangeWordsMelee },     // 0x100060b9 (generic)
			{ TEXT("item_w_zombie_fists"),        GRangeWordsMelee },     // 0x10001f5f (generic)
			{ TEXT("item_w_gargoyle_fist"),       GRangeWordsMelee },     // 0x1000d7ab (powerup)
			{ TEXT("item_w_hengeyokai_fist"),     GRangeWordsMelee },     // 0x1001103b (powerup)
			{ TEXT("item_w_manbat_claw"),         GRangeWordsMelee },     // 0x1000d850 (powerup)
			{ TEXT("item_w_sabbatleader_attack"), GRangeWordsMelee },     // 0x10015393 (powerup)
			{ TEXT("item_w_tzimisce3_claw"),      GRangeWordsMelee },     // 0x1000fa79 (powerup)
			{ TEXT("item_w_werewolf_attacks"),    GRangeWordsMelee },     // 0x1000ccde (powerup)
			{ TEXT("item_w_tzimisce2_claw"),      GRangeWordsMelee },     // 0x1000482c (hidden)
		};
	}

	FRangeWords ConstructorRangeWords(const FString& Classname, const FElysiumItemDef* Record)
	{
		for (const FClassRangeRow& Row : GClassRangeRows)
		{
			if (Classname.Equals(Row.Classname, ESearchCase::IgnoreCase))
			{
				return Row.Words;
			}
		}
		if (Record == nullptr)
		{
			return GRangeWordsBase;
		}
		switch (Record->Type)
		{
		case EElysiumItemType::WeaponFirearm: return GRangeWordsRanged;
		case EElysiumItemType::WeaponMelee:   return GRangeWordsMelee;
		case EElysiumItemType::WeaponThrown:  return GRangeWordsBase;
		default:
			// Every non-weapon item class recovered (`CWeaponIArmor`, `CWeaponIGeneric`,
			// `CWeaponIWritten`) zeroes all four words.
			return GRangeWordsNone;
		}
	}

	bool ItemRangeWords(const FElysiumEntity& Item, FRangeWords& Out)
	{
		const FElysiumItem* Carried = Item.AsItem();
		if (Carried == nullptr)
		{
			Out = FRangeWords();
			return false;
		}
		if (const FElysiumWeapon* Weapon = Carried->AsWeapon())
		{
			Out = Weapon->RangeWords;
			return true;
		}
		// A carried item the port does not run as a weapon controller (an NPC natural weapon whose
		// record is `generic`/`powerup`/`hidden`): its class's constructor words. It has nowhere to
		// hold `Weapon_Equip`'s 1e9 arm.
		Out = ConstructorRangeWords(Carried->ClassName(), Carried->Data());
		return true;
	}
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
	// (`docs/vtmb/wielded_weapons.md` §2, "Player and NPC"). One path serves both: sex is read off
	// `Wearer.Sheet.IsMale()`, which every
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
	// The class constructor's four range words (`+0x8b8..+0x8c4`), by the classname's factory.
	RangeWords = ElysiumWeapons::ConstructorRangeWords(ClassName(), Record);
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
	Ar << bInReload << bInterruptReload << bIsJammed; // +0x898/+0x899/+0x89a BOOL SAVE, 0x1025506f..77/0x1028918d

	// The two queued halves ride the event queue's own save block, so the transaction they complete
	// has to restore with them — exactly the pairing `CLogicRelay`'s refire latch keeps.
	Ar << PrimaryModeIndex;
	Ar << SecondaryModeIndex;
	Ar << NextPrimaryAttackTime;
	Ar << NextSecondaryAttackTime;
	Ar.Time(WeaponIdleTime); // +0x894 TIME SAVE, 0x101a0a80

	Ar << Swing.bActive;
	Ar << Swing.Serial;
	Ar << Swing.ModeIndex;
	Ar << Swing.bMelee;
	Ar << Swing.Activity;
	Ar << Swing.ClipLabel;
	Ar << Swing.Opponent;
	Ar << Swing.PlaybackRate;
	Ar << Swing.ClipSeconds;
	Ar.Time(Swing.CommitTime, EElysiumTimePolicy::Zero); // 0x101a0a80 port transaction deadline, map-clock domain
	Ar.Time(Swing.RecoveryDeadline, EElysiumTimePolicy::Zero); // 0x101a0a80 port transaction deadline, map-clock domain
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
	Ar.Time(ReloadEndTime, EElysiumTimePolicy::Zero); // 0x101a0a80 port transaction deadline, map-clock domain
	Ar << bFireIntentDuringReload;

	// `m_fEffects & EF_NODRAW`. Additive behind its own version; the default is DRAWN, which is what
	// every payload written before the bit existed describes.
	if (Ar.Version() >= FElysiumSaveVersion::WeaponHidden)
	{
		Ar << bHidden;
	}

	if (Ar.IsLoading())
	{
		SwingSerialCounter = FMath::Max(SwingSerialCounter, Swing.Serial);
		ReloadSerialCounter = FMath::Max(ReloadSerialCounter, ReloadSerial);
	}
}

void FElysiumWeapon::RebaseSavedReferences(FElysiumEntityWorld& InWorld)
{
	FElysiumItem::RebaseSavedReferences(InWorld);
	Swing.Opponent = InWorld.RestoreHandle(Swing.Opponent); // 0x101a2e40, before consumers
}

void FElysiumWeapon::Hide()
{
	// `CBaseEntity::Hide` (0x1009d2a0), vtable `+0x108`: set `m_fEffects |= EF_NODRAW`. The weapon
	// stays the owner's ACTIVE weapon — nothing about the transaction, the mode or the magazine
	// changes — it stops being drawn, and with it stops translating the body's activities
	// (0x10327ec0 / 0x103854f0 both gate on the bit).
	bHidden = true;
	// The hand follows the bit. Retail's NODRAW is what stops the model rendering at all; here the
	// wield model is a separate attached component, so it is taken off explicitly. The trail goes
	// with it — a trail on a weapon nothing is drawing is a swing arc from an empty hand.
	if (FElysiumCombatCharacter* const Wearer = OwnerCharacter())
	{
		if (USkeletalMeshComponent* const WearerBody = Wearer->GetSkeletalBody())
		{
			ElysiumNpcVisual::ClearWieldModel(WearerBody);
			ElysiumMeleeTrail::ClearTrail(WearerBody);
		}
	}
	UE_LOG(LogElysiumWeapon, Verbose, TEXT("%s hidden (EF_NODRAW set)"), *DebugString());
}

void FElysiumWeapon::Unhide()
{
	// `CBaseEntity::Unhide` (0x1009d380), vtable `+0x10c`: clear `m_fEffects &= ~EF_NODRAW`.
	bHidden = false;
	// The wield model is re-installed only for the wielder that is actually holding this weapon: an
	// unhide on a weapon some other equip has since replaced must not put its model back in the hand.
	FElysiumCombatCharacter* const Wearer = OwnerCharacter();
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
	// `Weapon_Equip 0x1032d380`, after `Inventory_Wield`: a wielder carrying spawnflag `0x100` pins
	// BOTH max words of its new active weapon to 1e9 (`0x4e6e6b28` into `+0x8c0` and `+0x8c4`).
	// Retail runs it once per equip that reaches the active-weapon switch; here it runs on every
	// draw, which only re-writes the same 1e9 (nothing lowers a max word), so no state differs.
	if ((Wearer.SpawnFlags & ElysiumWeapons::LongRangeSpawnflag) != 0)
	{
		RangeWords.MaxRange1 = ElysiumWeapons::LongRangeWordUnits;
		RangeWords.MaxRange2 = ElysiumWeapons::LongRangeWordUnits;
	}
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
	Out.Emplace(TEXT("Range words"), FString::Printf(TEXT("min1 %.0f min2 %.0f max1 %.0f max2 %.0f"),
		RangeWords.MinRange1, RangeWords.MinRange2, RangeWords.MaxRange1, RangeWords.MaxRange2));
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



int32 FElysiumWeapon::RangeAttack1Conditions(const FElysiumEntity* Enemy, float Dot,
	float DistanceUnits, double Now) const
{
	// Slot 365, `0x1024f670` (`RET 0xc`), arm for arm off the listing. Retail's first argument, the
	// enemy, is never read.
	(void)Enemy;
	// `_DAT_10450564` (100.0f, `FCOMP float`) and `_DAT_10449270` (0.5, `FCOMP double`).
	constexpr float TooCloseForRangedUnits = 100.0f;
	constexpr double FacingDot = 0.5;

	if (!(MagazineCount > 0))                                            // 1024f670 [+0x74c] / JG
	{
		return static_cast<int32>(EElysiumNpcCond::NoPrimaryAmmo);       // 0x40
	}
	if (DistanceUnits < TooCloseForRangedUnits)                          // 1024f682 TEST AH,5 / JP
	{
		return static_cast<int32>(EElysiumNpcCond::TooCloseForRanged);   // 0x08
	}
	if (DistanceUnits < RangeWords.MinRange1)                            // 1024f69b +0x8b8
	{
		return static_cast<int32>(EElysiumNpcCond::TooCloseToAttack);    // 0x5f
	}
	if (DistanceUnits > RangeWords.MaxRange1)                            // 1024f6b4 +0x8c0, AND 0x4100 / JNZ
	{
		return static_cast<int32>(EElysiumNpcCond::TooFarToAttack);      // 0x60
	}
	if (static_cast<double>(Dot) < FacingDot)                            // 1024f6cf
	{
		return static_cast<int32>(EElysiumNpcCond::NotFacingAttack);     // 0x61
	}
	// `0x10252410(this, 0)`: ready unless `curtime < +0x730` (NaN is ready); `NEG / SBB / AND 0x4f`.
	return !(Now < NextPrimaryAttackTime)
		? static_cast<int32>(EElysiumNpcCond::CanRangeAttack1) : 0;     // 1024f6e8
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

bool FElysiumWeapon::HeldSourcePosition(FVector& OutPosition) const
{
	const FElysiumCombatCharacter* Char = OwnerCharacter();
	if (Owner.IsSet())
	{
		if (Char == nullptr)
		{
			return false; // stale owner handle: not a loose world weapon
		}
		OutPosition = Char->Origin;
		return true;
	}
	if (IsInert())
	{
		return false;
	}
	OutPosition = Origin;
	return true;
}

bool FElysiumWeapon::IsActiveWeapon() const
{
	const FElysiumCombatCharacter* Char = OwnerCharacter();
	return Char != nullptr && Char->Inventory.ActiveWeapon == Handle;
}

bool FElysiumWeapon::CanStealthKill() const
{
	const FElysiumItemDef* Record = Data();
	return Record != nullptr && Record->Type == EElysiumItemType::WeaponMelee;
}

void FElysiumWeapon::PlayStealthKillSound()
{
	// StartGrappleAttack calls weapon +0x534(0x17, 1, 0, 0, 0, 0): WeaponSound,
	// not an activity request. All 22 shipped success blocks author one sound1 and no pitch override.
	const FElysiumItemDef* Item = Data();
	FElysiumCombatCharacter* Wielder = OwnerCharacter();
	IElysiumAudio* Audio = World ? World->Audio() : nullptr;
	if (!Item || Item->StealthKillSounds.IsEmpty()) return;
	if (!Audio || !Wielder)
	{
		UE_LOG(LogElysiumWeapon, Warning, TEXT("%s cannot play stealth kill sound without owner/audio"), *DebugString());
		return;
	}
	FElysiumAudioRequest Request;
	Request.Source = FElysiumAudioSource::Path(Item->StealthKillSounds[
		ElysiumRng::Stream(EElysiumRngStream::Ambient).RandRange(0, Item->StealthKillSounds.Num() - 1)]);
	Request.Owner.Kind = EElysiumAudioOwnerKind::GameplaySystem;
	Request.Owner.StableId = FString::Printf(TEXT("stealthkill.%d"), Wielder->Handle.Index);
	Request.Category = EElysiumAudioCategory::Sfx;
	Request.Placement.bSpatialized = true;
	Request.Placement.AttachTo = Wielder->GetAttachBody();
	Request.Placement.Location = Wielder->Origin;
	Request.Gain = 1.f;
	Request.Pitch = 1.f;
	Request.AttenuationRadiusCm = (1000.f / 0.27f) * ElysiumMove::U;
	if (!Audio->Submit(MoveTemp(Request)).IsValid())
		UE_LOG(LogElysiumWeapon, Warning, TEXT("%s failed to submit stealth kill sound"), *DebugString());
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
	Out.Variant = 0; // K4: C2's shared animation picker owns the draw; no handle-seeded choice.
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

void FElysiumWeapon::WarnNpcRangedClipWithoutShotEvent(const FElysiumNpc& Npc, int32 Sequence)
{
	if (OperatorBody() != ElysiumWeapons::EOperatorBody::Ranged
		|| !Npc.SequenceRows.IsValidIndex(Sequence)) return; // 0x10238160 ranged body only
	for (const FElysiumAnimEvent& ClipEvent : Npc.SequenceEvents(Sequence))
		if (ClipEvent.Event >= 3030 && ClipEvent.Event <= 3044) return; // 0x10238160
	const FElysiumNpc::FSequenceRow& ClipRow = Npc.SequenceRows[Sequence]; // 0x10238160
	if (ShouldReportOnce(FString::Printf(TEXT("silent-npc-ranged:%s@%s@%s"),
		*Npc.ModelStem(), *ClipRow.OwnerStem, *ClipRow.Label))) // 0x10238160 model/sequence, not weapon entity
		UE_LOG(LogElysiumWeapon, Warning, TEXT("NPC ranged clip '%s' on '%s' authors no 3030..3044; no shot"),
			*ClipRow.Label, *Npc.ModelStem()); // 0x10238160: no timer fallback
}

int32 FElysiumWeapon::PickWeaponModelSequence(int32 /*Activity*/)
{
	FWeaponModelClock* const Clock = WeaponModelClock(); // 0x1024efa0 held-model seqdesc source
	if (Clock == nullptr) return INDEX_NONE; // 0x1024efa0
	TArray<ElysiumAnimationPick::FCandidate> WeightedRows; // 0x1024efa0
	for (const TPair<int32, int32>& ModelCandidate : Clock->Candidates)
		WeightedRows.Add({ModelCandidate.Key, ModelCandidate.Value}); // 0x1008dc40 raw weights
	return ElysiumAnimationPick::Weighted(WeightedRows); // 0x1024efa0 -> 0x10427fc0 shared stream
}

void FElysiumWeapon::ResetWeaponModelSequence(int32 NewSequence)
{
	FWeaponModelClock* const Clock = WeaponModelClock(); // 0x10090950 descriptor input seam
	if (Clock == nullptr) return; // 0x10090950
	Clock->Words.Sequence = NewSequence; // 0x1024efa0 commit before reset
	// 0x10090950 ResetSequenceInfo does not zero cycle; the just-wrapped cycle survives.
	Clock->Words.bSequenceFinished = false; // 0x10090950
	Clock->Words.LastEventCheck = 0.f; // 0x10090950
	Clock->Words.bHasDescriptor = false; // 0015 new held-model seqdesc seam answers none // 0x10090950
	Clock->Words.CycleRate = 0.f; // 0x10090950
	Clock->Events.Reset(); // no borrowed owner events; 0015 overrides the descriptor reset // 0x10090950
}

void FElysiumWeapon::WeaponFrameUpdate(FElysiumCombatCharacter& Wielder)
{
	if (const FElysiumNpc* const NpcWielder = Wielder.AsNpc())
	{
		if (NpcWielder->ActivityNumber == 25 || NpcWielder->ActivityNumber == 27
			|| NpcWielder->ActivityNumber == 0x1114 || NpcWielder->ActivityNumber == 0x110f
			|| NpcWielder->ActivityNumber == 0x1120 || NpcWielder->ActivityNumber == 0x111f) // 0x102aaa60 cover-shot arms
			WarnNpcRangedClipWithoutShotEvent(*NpcWielder, NpcWielder->SequenceNumber); // 0x10238160
		for (const FElysiumAnimatingOverlay::FAnimOverlayLayer& ModelLayer : NpcWielder->AnimOverlay)
			if (ModelLayer.Weight != 0.f && (ModelLayer.Activity == 26 || ModelLayer.Activity == 28))
				WarnNpcRangedClipWithoutShotEvent(*NpcWielder, ModelLayer.Sequence); // 0x10238160 layer
	}
	FWeaponModelClock* const Clock = WeaponModelClock(); // 0x1032aa40 -> 0x1024efa0
	if (Clock == nullptr) return; // 0015 held model +0x170/174/65c/65d/658/6f0/6f4/6f8 seam
	FElysiumSequenceWords& ClockWords = Clock->Words; // 0x1024efa0
	const float ClockNow = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.f; // 0x1024efa0
	if (Clock->PrevAnimTime == 0.f)
		ClockWords.AnimTime = Clock->PrevAnimTime = ClockNow; // 0x1008f120 +0x170 seed
	float ClockInterval = 0.1f + ClockNow - ClockWords.AnimTime; // 0x1024efa0 slot 250(0)
	if (static_cast<double>(ClockInterval) > 0.001) // 0x1008f120 double 0x1044f020
	{
		Clock->PrevAnimTime = ClockWords.AnimTime; // 0x1008f120
		ClockWords.AnimTime += ClockInterval; // 0x1008f120
		ClockWords.Cycle += ClockWords.CycleRate * ClockInterval; // 0x1008f120 rate includes playback
		if (ClockWords.Cycle < 0.f || ClockWords.Cycle >= 1.f)
		{
			ClockWords.Cycle = ClockWords.bLoops
				? ClockWords.Cycle - static_cast<float>(static_cast<int32>(ClockWords.Cycle))
				: FMath::Clamp(ClockWords.Cycle, 0.f, 1.f); // 0x1008f120
			ClockWords.bSequenceFinished = true; // 0x1008f120
		}
		else ClockWords.bSequencePastHalf = ClockWords.Cycle >= 0.5f; // 0x1008f120
	}
	else ClockInterval = 0.f; // 0x1008f120 early out
	if (ClockWords.bSequenceFinished && ClockWords.bLoops)
	{
		const int32 NewSequence = PickWeaponModelSequence(Clock->Activity); // 0x1024efa0 weighted
		if (NewSequence >= 0) ResetWeaponModelSequence(NewSequence); // 0x1024efa0
	}
	(void)ClockInterval; // 0x1024efa0 slot 258 consumes the just-advanced words
	ElysiumAnimEvents::DispatchBase(ClockWords, Clock->Events, *this, Wielder); // 0x10091880 weapon events -> wielder
}

bool FElysiumWeapon::CommitArrivesFromAnimEvent(FElysiumCombatCharacter& Char,
	const FString& OwnerStem, const FString& ClipLabel)
{
	if (!IsPlayerSide(Char)) // 0x10238160: no NPC timer exception, even without a model/timeline
	{
		if (const FElysiumNpc* const NpcOperator = Char.AsNpc())
			WarnNpcRangedClipWithoutShotEvent(*NpcOperator, NpcOperator->SequenceNumber); // 0x10238160
		return true; // 0x10238160 -> 0x102383b0: only the event can commit an NPC shot
	}
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

	// The player's slot 258 (`FElysiumPlayer::PostThinkAnimation`, `CBasePlayer::PostThink
	// 0x1016be10`) walks the timeline of whatever clip the base channel and overlay slot 0 are
	// standing on, so a body playing a different clip there never reaches this one's events — and
	// the estimate standing down for it would swallow the whole transaction.
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
	const FElysiumCombatCharacter* const EventOperator = OwnerCharacter(); // 0x10238160
	if (EventOperator == nullptr || !IsPlayerSide(*EventOperator))
		return ShotFromAnimEvent(Event); // 0x10238160 -> 0x102383b0 -> 0x102387b0, never staged estimate

	if (!Swing.bActive)
	{
		// The id fired with nothing staged. Retail stages nothing before the event at all:
		// `0x10238160` -> `0x10238320` -> `ModeDispatch(1) 0x102383b0` -> `Shot 0x102387b0`, so for
		// an NPC operator the event IS the shot (spec 0002 V4o). The only ids that reach here are the
		// ranged body's 3030..3044, because `IsCommitEvent` answers for that body alone.
		const FElysiumCombatCharacter* const Operator = OwnerCharacter();
		if (Operator == nullptr || !IsPlayerSide(*Operator))
		{
			return ShotFromAnimEvent(Event);
		}
		// The player's `Shot` is the press transaction `AttackIntent` stages, so its unstaged event --
		// an aim or idle clip carrying a shot id -- finds no attack to commit. Claimed, and an
		// ordinary negative rather than a fault.
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

bool FElysiumWeapon::PresenceDoublesAttackRate(const FElysiumCombatCharacter* /*Owner*/)
{
	// 0x1033d940 / 0x101e3f50: Presence id 10 reads m_iDisciplineFlags2 +0xeb4.
	// Status apply 0x101e3560 writes via AddDiscFlag 0x1033cfb0; activation callers are
	// 0x101e2f50 / 0x101e3380 / 0x101e33c0 / 0x101f8620. Spec 0006 owns that producer
	// and the runtime level-bit table; the absent cast/status input answers false.
	return false; // 0x101e3f50
}

float FElysiumWeapon::ShotAttackRate(const FElysiumWeaponMode& Mode) const
{
	// Slot 332 `0x10254410`: the mode record's `Attack_Rate (+0x260)` handed to the owner's
	// `0x1033d940`, which answers `rate * 2` under the Presence test and `rate` otherwise.
	const float Rate = Mode.AttackRate;                                          // 0x10254410 +0x260
	return PresenceDoublesAttackRate(OwnerCharacter()) ? Rate * 2.0f : Rate;     // 0x1033d940
}

int32 FElysiumWeapon::CapBulletSetsByClip(int32 Sets, const FElysiumWeaponMode& Mode) const
{
	// `Shot 0x102387b0` step 7: `n = sets * Ammo_Cost (+0x110)`; `n > 0` and
	// `m_iMagazineCurAmts[ammo index] (+0x74c) < n` -> `sets = clip / Ammo_Cost`. An empty clip gives
	// zero sets; a mode that costs nothing is never capped.
	const int32 Needed = Sets * Mode.AmmoCost;
	if (Needed > 0 && MagazineCount < Needed)
	{
		return MagazineCount / Mode.AmmoCost;
	}
	return Sets;
}

bool FElysiumWeapon::ShotFromAnimEvent(const FElysiumAnimEvent& Event)
{
	// `CWeaponRanged::Operator_HandleAnimEvent 0x10238160` (events 3030..3044) -> `0x10238320`
	// (`DAT_1088aee4 = 0`: an NPC operator never takes the secondary wrapper) -> `ModeDispatch(1)
	// 0x102383b0` -> slot 373 `Shot 0x102387b0`. No attack is staged before the event on this path.
	if (World == nullptr)
	{
		return true;   // a worldless probe entity: no clock to read (`curtime`), nothing to fire
	}
	FElysiumCombatCharacter* const Char = OwnerCharacter();                      // 0x10252240
	if (Char != nullptr && IsPlayerSide(*Char))
	{
		// The player's `Shot` is the press transaction (`AttackIntent` -> `BeginRangedShot`); its
		// unstaged event commits nothing here, as before this entry existed.
		return true;
	}
	const double Now = World->NowSeconds();                                      // curtime

	// --- `ModeDispatch 0x102383b0` ---------------------------------------------------------------
	// `weapon +0x848 (m_iItemCurActivateMode) = m_iItemActivationModes[0] (+0x84c)`, then the mode
	// record `0x102517e0(weapon)` whose id `+0x104` equals it: the PRIMARY mode in force, for an NPC
	// always (the toggle `0x10239270` is reached by no NPC path). No secondary NPC shot exists.
	const int32 ModeIndex = PrimaryModeIndex;                                    // +0x848
	const FElysiumWeaponMode* const Mode = ModeAt(ModeIndex);                    // 0x102517e0
	if (Mode == nullptr)
	{
		// Retail answers a static default record of type 0 here and takes the "any other type" arm
		// with that record's rate. The default record's `Attack_Rate` is unread, so nothing is
		// written: a weapon with no mode in force advances no clock.
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s took anim event %d with no primary mode in force — nothing fired"),
			*DebugString(), Event.Event);
		return true;
	}
	// On the record's type `+0x108`.
	switch (Mode->Type)
	{
	case EElysiumWeaponModeType::Attack:              // type 1
	case EElysiumWeaponModeType::SecondaryAttack:     // type 2
		break;                                        // -> slot 373 `Shot`
	case EElysiumWeaponModeType::ZoomLoop:
	{
		// Type 3: the zoom step is the player's alone; both next-attack times `= curtime + rate`.
		const double ZoomRate = static_cast<double>(ShotAttackRate(*Mode));
		NextPrimaryAttackTime = Now + ZoomRate;                                  // +0x730
		NextSecondaryAttackTime = Now + ZoomRate;                                // +0x734
		return true;
	}
	case EElysiumWeaponModeType::TogglePrimaryMode:
		// Type 4: the fire-mode toggle `0x10239270`. SEAM: its body is unread on the NPC path and no
		// NPC's mode in force is a toggle record (`weapon +0x848` is row 0), so nothing is written.
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s took anim event %d on a toggle mode in force — the toggle 0x10239270 is not "
				"ported on the NPC path"), *DebugString(), Event.Event);
		return true;
	default:
	{
		// Any other type (6, the throw `0x10239e70`, is not carried apart from the rest by the item
		// table): `+0x730 += rate`, `+0x734 =` the same word; nothing fired.
		NextPrimaryAttackTime += static_cast<double>(ShotAttackRate(*Mode));     // +0x730
		NextSecondaryAttackTime = NextPrimaryAttackTime;                         // +0x734
		return true;
	}
	}

	// --- `Shot 0x102387b0`, for an NPC, in its order -----------------------------------------------
	// 1. Zoomed (weapon data `+0x4fe84 > 0 && +0x914 > 0`): the mode record is re-read as tag 2.
	//    SEAM: no scope state stands in this runtime (`AttackIntent`'s `ZoomLoop` arm), so `+0x914`
	//    answers 0 and the record stays the one read above.

	// 2. Owner null -> return. `AddMiscFlag(0x200000)` on the owner.
	if (Char == nullptr)
	{
		return true;
	}
	ElysiumMiscFlags::Set(Char->MiscFlags, 0x200000u);                           // 0x1033c6b0

	// 3. Not a player and the owner's NPC pointer (`+0x94`) null -> return.
	FElysiumNpc* const Npc = Char->AsNpc();
	if (Npc == nullptr)
	{
		return true;
	}
	//    The weapon activity: the weapon's slot 333 `(6, 1, 0, 0, 0, 0)`. Played before the count,
	//    so an event inside the cooldown still plays it.
	++ShotSeams.WeaponActivityCalls;
	ShotSeams.LastWeaponActivity = 6;

	// 4. Weapon data `+0x50170` -> the owner's `m_fEffects |= 2`. SEAM: the item table carries no
	//    field for the weapon-data word at `+0x50170`, so it answers 0 and the bit is not written.

	// 5. The shoot position: owner slot 389; the direction: the NPC's slot 574 `(&out, &shootPos,
	//    1, 0)`.
	const FVector ShootPositionCm = Char->Weapon_ShootPosition(Char->Origin);    // slot 389 0x103338c0
	ShotSeams.LastShootPositionCm = ShootPositionCm;
	ShotSeams.LastShootDirection = Npc->GetShootEnemyDir(ShootPositionCm, 1, 0); // slot 574 0x10278900

	// 6. The cooldown is a COUNT, not a refusal (`0x1023891b..0x1023895d`; the slot index is
	//    `DAT_1088aee4`, 0 for an NPC, so the word is `m_flNextPrimaryAttack +0x730` and `+0x734` is
	//    never touched). Written also when the count ends 0: the `max` is then a no-op and the loop
	//    does not run, so the stamp is unchanged and never moved back. `m_iAtkMode +0x86c != 0`
	//    re-reads slot 332 each step; the rate cannot change inside the loop here, so one read stands.
	int32 Sets = 0;                                                              // +0x918 bulletSetsToFire
	const double Rate = static_cast<double>(ShotAttackRate(*Mode));              // slot 332 0x10254410
	double Next = FMath::Max(NextPrimaryAttackTime, Now - World->FrameSeconds()); // curtime - frametime
	if (Rate > 0.0)
	{
		while (Next <= Now)                                                      // 0x1023891b
		{
			Next += Rate;
			++Sets;
		}
	}
	else if (Next <= Now)
	{
		// CRASH GUARD: retail's loop does not end for a rate of 0 or below. No shipped record states
		// one (`0x10259230` defaults `Attack_Rate` to 1.0); a fixture that does fires nothing.
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("%s: mode '%s' states Attack_Rate %.3f — the event shot's cooldown loop cannot "
				"advance, nothing fired"), *DebugString(), *Mode->Tag, Mode->AttackRate);
	}
	NextPrimaryAttackTime = Next;                                                // +0x730[0]
	ShotSeams.LastCooldownSets = Sets;

	// 7. The clip caps, never refuses outright, and an NPC's clip is NOT decremented (the
	//    subtraction is inside the player-only block `0x10238a15`..`0x10238a4b`): the clip is
	//    `max(Default_Size, 1)` from `Inventory_Insert 0x10334e70` to death.
	Sets = CapBulletSetsByClip(Sets, *Mode);
	ShotSeams.LastSets = Sets;

	// 8. No line-of-fire gate: the one trace (2 048 units, mask `0x46004003`) feeds a debug overlay
	//    and a discarded `GetFlags`.

	// 9. Per set, owner slot 185 `FireBullets`; `CSoundEnt::InsertSound(1, owner origin,
	//    [0x1072bc40], 0.2)`; slot 339 `Kick` when the owner's slot 220 answered non-null (SEAM: Kick
	//    has no recovered producer here, RE-A5 -- not called); `+0x730[slot]` advanced (above).
	++ShotSeams.CombatSoundInserts;
	if (Sets <= 0)
	{
		// Zero sets: an event inside the cooldown, or a clip that covers no set. Nothing is staged;
		// the sound is still inserted (the staged path's is the commit's own, below in
		// `CommitQueuedAttack`).
		World->EmitGameSound(Char->Origin, ElysiumGameSounds::Gunshot(),
			/*RadiusCm, table-resolved*/ -1.f, Char->Handle,
			ElysiumStealth::HearingReductionCmFor(Char), ElysiumGameSounds::Combat,
			/*InsertSound's duration*/ 0.2);
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s anim event %d fires zero sets (next attack %.3f, now %.3f, clip %d)"),
			*DebugString(), Event.Event, NextPrimaryAttackTime, Now, MagazineCount);
		return true;
	}

	// The sets are one transaction: the mode, the victim, the damage spine and the queue discipline
	// are `BeginRangedShot`'s, with no clip play (the kernel pushed the layer or the sequence that
	// raised this event), no estimate and no `HasLiveAnimEventDispatch` question. The victim is the
	// owner's enemy (`m_hEnemy +0x5ce0`), the handle that stands for `FireBullets`' ray here.
	FRangedStage Stage;
	Stage.ModeIndex = ModeIndex;
	Stage.Victim = Npc->BaseMemory.Enemy;
	Stage.CommitTime = Now;
	Stage.RecoveryDeadline = NextPrimaryAttackTime;
	Stage.BulletSets = Sets;
	StageRangedShot(Stage);
	QueueSelfInput(ElysiumWeaponCommitInput(), Swing.Serial, 0.0);

	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s anim event %d is shot #%d: %d set(s) (mode '%s'), next attack %.3f, victim %s"),
		*DebugString(), Event.Event, Swing.Serial, Sets, *Mode->Tag, NextPrimaryAttackTime,
		Stage.Victim.IsSet() ? *World->DescribeHandle(Stage.Victim) : TEXT("(none)"));
	return true;
}

bool FElysiumWeapon::TzimisceMeleeActivityFromAnimEvent(FElysiumCombatCharacter& /*TzimisceOperator*/,
	int32 /*TzimisceEvent*/)
{
	// 0x103e8be0 -> 0x103e8c50/0x103e8c90: absent class, so no +0x910 storage/provider.
	// Its NPC-only temporary 1/2, RequestActivity(0x4b,1,0), reset 0 remains a named seam.
	return false; // 0x103e8be0: common melee falls through to base on 3045/3046
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

	if (OpBody != ElysiumWeapons::EOperatorBody::None && (Event.Event == 4001 || Event.Event == 4002))
	{
		SetWeaponModelBodygroup(FCString::Atoi(*Event.Options), Event.Event == 4001 ? 1 : 0); // 0x103ea5b0 / 0x10238160
		return true;
	}
	if (OpBody == ElysiumWeapons::EOperatorBody::Melee && (Event.Event == 3045 || Event.Event == 3046))
		return TzimisceMeleeActivityFromAnimEvent(Operator, Event.Event); // 0x103e8be0 named absent-class seam

	if (ElysiumWeapons::IsCommitEvent(Event.Event, OpBody))
	{
		return CommitFromAnimEvent(Event);
	}
	if (OpBody == ElysiumWeapons::EOperatorBody::Melee)
	{
		if (ElysiumWeapons::IsMeleeSwingTrigger(Event.Event))
		{
			if (!IsPlayerSide(Operator))
			{
				(void)NpcMeleeAttack(EIntent::Primary); // 0x103ea5b0 direct PrimaryAttack
			}

			return true;
		}
		if (ElysiumWeapons::IsSwallowedMeleeEvent(Event.Event))
		{
			return true;   // `0x103ea5b0`'s swallow set — accepted, and acted on by nothing
		}
	}

	// 0x1024f030: remaining/base ids go to the unclaimed-event census. Bodygroups above are claimed.
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
		// Retail enumerates candidates through `FindEntityFOV` (`0x10341c30`), whose sphere includes
		// non-solid entities; `EntityUnselectable` (`0x100a52a0`) rejects `m_bIsBCCTargetable`
		// (`+0x1480`) clear — a script director (`CCineNPC::Spawn` `0x101a6f10` stores 0).
		if (!Other || Other->Handle == Attacker.Handle || Other->AsItemContainer() != nullptr
			|| !IsAliveForCombat(*Other) || !FElysiumNpcBase::IsBccTargetable(*Other))
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
	// Melee PrimaryAttack 0x103ead0b tries the paired kill before the normal swing.
	// SecondaryAttack 0x103eae00 never calls this; both refuse while already paired.
	if (CanStealthKill())
	{
		if (Char->IsGrappling()) return EVerdict::Busy;
		if (Intent == EIntent::Primary)
		{
			FElysiumPlayer* Player = Char->AsGrapplingPlayer();
			if (Player && Player->TryStealthKill()) return EVerdict::Accepted;
		}
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

FElysiumWeapon::EVerdict FElysiumWeapon::NpcMeleeAttack(EIntent Intent)
{
	if (OwnerCharacter() == nullptr || World == nullptr) return EVerdict::NoOwner; // 0x103eaca0
	const FElysiumWeaponMode* const AttackMode = ModeFor(Intent); // 0x103e9e00 active mode
	if (AttackMode == nullptr) return EVerdict::NoMode; // 0x103eaca0
	const int32 AttackModeIndex = Intent == EIntent::Primary ? PrimaryModeIndex : SecondaryModeIndex; // 0x103eaca0
	const FElysiumItemDef* const AttackRecord = Data(); // 0x103eaca0
	if (AttackRecord != nullptr && AttackRecord->Type == EElysiumItemType::WeaponMelee)
		return BeginMeleeSwing(Intent, AttackModeIndex, *AttackMode, true); // 0x103ead7a RequestActivity force=1
	return BeginRangedShot(Intent, AttackModeIndex, *AttackMode, FElysiumEntityHandle::Invalid()); // 0x102382f0
}

FElysiumWeapon::EVerdict FElysiumWeapon::BeginMeleeSwing(EIntent Intent, int32 ModeIndex,
	const FElysiumWeaponMode& Mode, bool bForceSequence)
{
	FElysiumCombatCharacter& Char = *OwnerCharacter();
	if (Char.IsGrappling()) return EVerdict::Busy; // 0x103eaca0 owner +0x1538/+0x153c
	const double Now = World->NowSeconds();
	const FElysiumWeaponContext Context = FElysiumWeaponContext::FromCharacter(Char);
	const FElysiumDmg& ModeDmg = DamageForMode(ModeIndex);

	if (!IsPlayerSide(Char))
	{
		FElysiumNpc* const NpcOwner = Char.AsNpc(); // 0x103e9e00 owner +0x98
		if (NpcOwner == nullptr) return EVerdict::Unsupported; // no studio row source // 0x103e9e00
		const int32 RequestedActivity = Intent == EIntent::Secondary ? 0x4e : 0x4b; // 0x103eae00
		const int32 WeaponActivity = NpcOwner->WeaponActivityOverride(RequestedActivity); // 0x103e9e00 slot 361
		const int32 OwnerActivity = NpcOwner->NPC_TranslateActivity(WeaponActivity); // 0x103e9e00 slot 376
		FElysiumEntity* const EnemyEntity = NpcOwner->GetEnemy(); // 0x103e9e00 NPC GetEnemy
		FElysiumCombatCharacter* const EnemyCharacter = EnemyEntity != nullptr
			? EnemyEntity->AsCombatCharacter() : nullptr; // 0x103e9e00 enemy +0x9c
		const int32 NpcFeatRank = FeatRating(Char, ModeDmg.AttackFeat, Context); // 0x103ea0d3 before slot 331
		int32 ChosenSequence = INDEX_NONE; // 0x103ea193
		const bool bChosen = Char.ChooseMeleeAttackSequence(this, EnemyCharacter,
			OwnerActivity, &ChosenSequence); // 0x103ea1a3 slot 331
		if ((!bChosen || ChosenSequence < 0) && !bForceSequence)
			return EVerdict::NotReady; // 0x103ea1b5..0x103ea1cc unforced refusal
		NpcOwner->ForcePreTranslatedSequenceAndActivity(RequestedActivity, OwnerActivity,
			ChosenSequence); // 0x103ea214 slot 311 resets cycle/rate
		const float NpcRate = ElysiumWeapons::AttackSpeedScale(Char)
			* (0.7f + 0.03f * static_cast<float>(NpcFeatRank)); // 0x103ea28b..0x103ea291 AFTER slot 311
		NpcOwner->SequencePlaybackRate = NpcRate; // 0x103ea297 AFTER slot 311
		if (IElysiumEmbodiment* RatePresentation = World->Embodiment())
			RatePresentation->SetBodySequencePlaybackRate(NpcOwner->Visual, NpcRate); // rate-only, no NPC PlayAnimSegment
		const float NpcSeconds = ChosenSequence >= 0
			? NpcOwner->SequenceDurationSeconds(ChosenSequence) : 0.1f; // 0x103ea29d -> 0x10091196 invalid fallback
		const double NpcRecovery = Now + NpcSeconds / NpcRate; // 0x103ea2d6
		ClearSwing(); // 0x103e9e00 ForceMeleeReset
		Swing.bActive = true; // 0x103e9e00 live swing
		Swing.bMelee = true; // 0x103e9e00
		Swing.Serial = ++SwingSerialCounter; // opposed-roll identity // 0x103e9e00
		Swing.ChainRoot = Swing.Serial; // 0x103e9e00
		Swing.ChainLink = 1; // 0x103e9e00
		Swing.ModeIndex = ModeIndex; // 0x103e9e00 mode record
		Swing.Activity = Intent == EIntent::Secondary ? GActMeleeAttackHeavy : GActMeleeAttack; // 0x103e9e00
		const int32 SwingSequence = NpcOwner->SequenceNumber; // 0x10272400 negative force leaves old sequence
		if (NpcOwner->SequenceRows.IsValidIndex(SwingSequence))
		{
			Swing.ClipLabel = NpcOwner->SequenceRows[SwingSequence].Label; // 0x103e9e00 current seqdesc
			Swing.ClipOwnerStem = NpcOwner->SequenceRows[SwingSequence].OwnerStem; // 0x103e9e00
		}
		Swing.PlaybackRate = NpcRate; // 0x103ea297
		Swing.ClipSeconds = NpcSeconds; // 0x103ea29d
		Swing.RecoveryDeadline = NpcRecovery; // 0x103ea2d6
		Swing.Opponent = EnemyCharacter != nullptr ? EnemyCharacter->Handle : FElysiumEntityHandle::Invalid(); // 0x103e9e00
		Char.bMeleeSwingIsLive = true; // 0x10346760 +0xaa1
		Char.bDidSendIncomingSwingNotice = false; // 0x10346760 +0xaa2
		Char.bDidSendSwingActiveEvent = false; // 0x10346760 +0xaa3
		HoldAttacksUntil(NpcRecovery); // 0x103ea2eb..0x103ea327
		NpcOwner->NextAttackTime = FMath::Max(NpcOwner->NextAttackTime, NpcRecovery); // 0x103ea2eb
		return EVerdict::Accepted; // 0x103ea357
	}

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
	Swing.bMelee = true; // 0x10346760 live character swing
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
	Char.bMeleeSwingIsLive = true; // 0x103e9e00 ForceMeleeReset
	Char.bDidSendIncomingSwingNotice = false; // 0x103e9e00 +0xaa2
	Char.bDidSendSwingActiveEvent = false; // 0x103e9e00 +0xaa3

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
	if (!IsPlayerSide(Char))
	{
		if (const FElysiumNpc* const Npc = Char.AsNpc())
			WarnNpcRangedClipWithoutShotEvent(*Npc, Npc->SequenceNumber); // 0x10238160
		// 0x10238580 NPC Attack: no timed shot and no UpperBody presentation stand-in.
		// 0x10238580 m_flSoonestPrimaryAttack has no reader here; named seam, +0x730 unchanged.
		return EVerdict::Accepted; // 0x10238580
	}

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

	FRangedStage Stage;
	Stage.ModeIndex = ModeIndex;
	Stage.Victim = Victim;
	Stage.ClipLabel = ClipLabel;
	Stage.ClipOwnerStem = ClipOwnerStem;
	Stage.PlaybackRate = Scale;
	Stage.ClipSeconds = Seconds;
	Stage.CommitTime = Commit;
	Stage.RecoveryDeadline = Recovery;
	Stage.bAwaitingAnimEvent = bEventCommit;
	StageRangedShot(Stage);

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

void FElysiumWeapon::StageRangedShot(const FRangedStage& Stage)
{
	// The transaction a ranged commit completes, as `BeginRangedShot` has always written it; shared
	// with `ShotFromAnimEvent` (`Shot 0x102387b0` on an NPC's event), which stages the same record
	// with no clip and the cooldown loop's own set count.
	ClearSwing();
	Swing.bActive = true;
	Swing.Serial = ++SwingSerialCounter;
	Swing.ModeIndex = Stage.ModeIndex;
	Swing.bMelee = false;
	Swing.Activity = GActRangeAttackLayer;
	Swing.ClipLabel = Stage.ClipLabel;
	Swing.ClipOwnerStem = Stage.ClipOwnerStem;
	// The victim is the caller's: the player frame gets it from the embodiment's aim query, an AI
	// cycle from its own enemy selection, and a test states it. One handle rather than a per-ray
	// hit set is what the transaction carries, and that is the whole of the divergence below.
	Swing.Opponent = Stage.Victim;
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
	Swing.PlaybackRate = Stage.PlaybackRate;
	Swing.ClipSeconds = Stage.ClipSeconds;
	Swing.CommitTime = Stage.CommitTime;
	Swing.RecoveryDeadline = Stage.RecoveryDeadline;
	Swing.bAwaitingAnimEvent = Stage.bAwaitingAnimEvent;
	Swing.BulletSets = Stage.BulletSets;
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

	// 0x10238160 -> 0x10238320 -> 0x102387b0: the NPC event shot is a real
	// transaction at curtime. BeginRangedShot no longer stages an NPC timer estimate;
	// CommitTime cannot distinguish that deleted producer from the event producer.

	const int32 ModeIndex = Swing.ModeIndex;
	const FElysiumEntityHandle OpponentHandle = Swing.Opponent;
	const int32 StagedSets = Swing.BulletSets;
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
	//
	// **The spend and the refusal are the PLAYER's alone** (`Shot 0x102387b0`: the subtraction sits
	// inside the player block, `0x10238a15 TEST EAX,EAX / JZ` on the player pointer, then
	// `0x10238a4b SUB`). A non-player wielder's clip is `max(Default_Size, 1)` from
	// `Inventory_Insert 0x10334e70` to death: it CAPS the sets (step 7) and is never written, so
	// `NO_PRIMARY_AMMO 0x40` cannot rise from firing (spec 0002 J12).
	int32 Sets = 1;
	if (IsPlayerSide(*Attacker))
	{
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
	}
	else
	{
		Sets = CapBulletSetsByClip(StagedSets, *Mode);                           // 0x102387b0 step 7
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
			ElysiumStealth::HearingReductionCmFor(Attacker), ElysiumGameSounds::Combat,
			/*GetSoundDuration is unavailable for the weapon row; one second is its recovered floor*/ 1.0);
	}

	if (Sets <= 0)
	{
		// A non-player wielder whose clip covers no set: the cap, not a refusal. The sound above is
		// `Shot`'s own (`InsertSound`, step 9, runs at zero sets); no bullets leave.
		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s commit #%d fires zero sets: the clip (%d) covers no Ammo_Cost %d"),
			*DebugString(), Serial, MagazineCount, Mode->AmmoCost);
		return;
	}

	// SEAM (R7.2, closed) — retail traces forward first and accepts a valid obstruction hit.
	// The paid-for shot leaves a mark on whatever the ray actually met, resolved a hit or a miss:
	// `C_TEGunshotDecal` is emitted by the shot itself, not by the damage. The substrate owns the
	// instant and the variation roll (a decal variation is an effects-side pick, so it draws on
	// that stream); the trace, the hit's surface character and the decal are engine questions and
	// live behind `IElysiumEmbodiment::LayShotImpactDecal` (`docs/vtmb/effects.md` §3.5).
	//
	// One trace per set: `Shot` calls owner slot 185 `FireBullets` once per set (step 9). A press or a
	// task stages one set, so this is one trace for every transaction but an event shot's catch-up.
	for (int32 Set = 0; Set < Sets; ++Set)
	{
		if (FElysiumNpc* const ShotNpc = Attacker->AsNpc()) // 0x10268919 owner NPC +0x98
		{
			--ShotNpc->FakeReloadCount; // 0x10268919 DEC +0x65f0, once per FireBullets set
		}
		TraceShotImpact(*Attacker, *Mode);
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

	for (int32 Set = 0; Set < Sets; ++Set)
	{
		// A victim the earlier set killed takes no further set (the alive-path prefilter above).
		if (Set > 0 && !IsAliveForCombat(*Victim))
		{
			break;
		}
		RangedImpact(*Attacker, *Victim, ModeIndex);
	}
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

	// **Where one record's two endpoints are at a sub-step, world centimetres -- the ONE function
	// the walk asks.** Retail poses the sequence at the sub-step's own cycle (`CalcPose` at `cycle`,
	// `MeleeSwingStep 0x10343020` step 1) and transforms the record's bone-local points by that
	// pose; this body is today's stand-in, the live bone's segment lerped linearly between two
	// batches in the attacker's interpolated frame (difference D9, spec 0002 `packets-S5.md` item 3).
	// 0x10343020: Cycle reaches the named CalcPose-at-cycle seam; it returns nothing until 0015.
	struct FElysiumSwingEndpointQuery
	{
		const FTransform& PrevFrame;
		const FTransform& NowFrame;
		const TPair<FVector, FVector>& FromLocal;
		const TPair<FVector, FVector>& ToLocal;
		float Alpha = 0.0f;   // 0 = the last batch, 1 = this one
		float Cycle = 0.0f;   // the sub-step's cycle (D9's operand)
		const FElysiumCombatCharacter* Character = nullptr; // 0x10343020 pose parameters/model
		const FElysiumSwingRecord* Record = nullptr; // 0x10343020 bone-local endpoints
		FElysiumWeapon* SourceWeapon = nullptr; // 0x10343020 seam observation, never read by gameplay
	};
	bool ElysiumCalcPoseAtCycle(const FElysiumSwingEndpointQuery& PoseQuery, FTransform& /*OutBone*/)
	{
		(void)PoseQuery.Cycle; // 0x10343020 CalcPose / 0x100c3600: no sequence-cycle bone accessor yet.
		(void)PoseQuery.Character;
		(void)PoseQuery.Record;
		if (PoseQuery.SourceWeapon != nullptr)
		{
			FElysiumWeapon::FCalcPoseSeams& PoseObservation = PoseQuery.SourceWeapon->CalcPoseSeams;
			++PoseObservation.Queries; // 0x10343020 CalcPose input query
			PoseObservation.PreviousQueryCycle = PoseObservation.LastQueryCycle;
			PoseObservation.LastQueryCycle = PoseQuery.Cycle; // 0x10343020 each query's own cycle
			PoseObservation.bAvailable = false; // 0x100c3600 matrix input has no provider
		}
		return false; // named D9 seam; no synthetic pose. Integrator/judge owes embodiment accessor.
	}
	void ElysiumSwingEndpointsAt(const FElysiumSwingEndpointQuery& Query, FVector& OutA, FVector& OutB)
	{
		FTransform CycleBone;
		if (ElysiumCalcPoseAtCycle(Query, CycleBone)) // 0x10343020 each query's own cycle
		{
			OutA = CycleBone.TransformPosition(Query.Record->ACm); // 0x100c3600 bone-local, not actor-local
			OutB = CycleBone.TransformPosition(Query.Record->BCm); // 0x100c3600
			return;
		}
		const FTransform Frame = ElysiumSwingFrameAt(Query.PrevFrame, Query.NowFrame, Query.Alpha);
		OutA = Frame.TransformPosition(FMath::Lerp(Query.FromLocal.Key, Query.ToLocal.Key, Query.Alpha));
		OutB = Frame.TransformPosition(FMath::Lerp(Query.FromLocal.Value, Query.ToLocal.Value, Query.Alpha));
	}

	// `debug_allow_melee_ff` (ConVar object `0x10936ee0`), read by `MeleeSwingStep 0x10343020` at
	// `0x1034394d..`: non-zero lets an NPC's swing hit characters it does not hate. Shipped 0; this
	// runtime keeps no console variable per recovered ConVar, so the shipped value is the answer.
	bool ElysiumSwingSameTeam(const FElysiumCombatCharacter& Attacker,
		const FElysiumCombatCharacter& Victim)
	{
		return Attacker.IsSameTeam(&Victim); // 0x10323930; valid equal uint16 symbols only
	}

	// `0x1012c9c0(e)`: the root of `e`'s owner chain (`+0x25c`, the owner handle slot 97
	// `GetOwnerEntity` answers), `e` itself when it has no owner. Bounded: a chain that loops back
	// is a defect retail would spin on; here it ends after 16 links (named crash guard).
	const FElysiumEntity* ElysiumSwingOwnerRoot(const FElysiumEntityWorld& World,
		const FElysiumEntity& Entity)
	{
		const FElysiumEntity* Root = &Entity;
		for (int32 Link = 0; Link < 16; ++Link)
		{
			const FElysiumEntityHandle Owner = Root->GetOwnerEntity();
			const FElysiumEntity* Next = Owner.IsSet() ? World.Resolve(Owner) : nullptr;
			if (Next == nullptr)
			{
				break;
			}
			Root = Next;
		}
		return Root;
	}

	// `e+0x9c`, the combat-character self-pointer. A loot container registers on the
	// combat-character base in this runtime (it owns the same inventory) and is a prop in retail,
	// so it answers null here and is hit as the non-character it is.
	FElysiumCombatCharacter* ElysiumSwingVictimCharacter(FElysiumEntity& Entity)
	{
		return Entity.AsItemContainer() != nullptr ? nullptr : Entity.AsCombatCharacter();
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

void FElysiumWeapon::PrepareSwingContact()
{
	if (Swing.bContactStaged) return; // 0x10346cd0 once-only +0xaa2
	Swing.bContactStaged = true; // 0x10346cd0
	FElysiumCombatCharacter* const ContactOwner = OwnerCharacter(); // 0x10346ac0
	if (ContactOwner == nullptr) return; // 0x10346ac0
	FSwingContact Prepared; // 0x10346ac0
	Prepared.ModeIndex = Swing.ModeIndex; // 0x10346ac0
	Prepared.Serial = Swing.Serial; // 0x10346ac0
	Prepared.ClipLabel = Swing.ClipLabel; // 0x10346ac0
	Prepared.ClipOwnerStem = Swing.ClipOwnerStem; // 0x10346ac0
	StageSwingOpposedRoll(*ContactOwner, Prepared); // 0x10346ac0
}

void FElysiumWeapon::ResetSwingContact()
{
	// 0x10346760 ForceMeleeReset preserves endpoints seeded by the preceding Step.
	Swing.bContactStaged = false; // 0x10346760 +0xaa2
	Swing.bWallEffectSpent = false; // 0x10346760 +0xaa0
	Swing.bActive = true; // 0x10346760
	Swing.bMelee = true; // 0x10346760 character's live swing
	Swing.Serial = ++SwingSerialCounter; // 0x10346760 invalidates previous opposed rows
	Swing.ModeIndex = PrimaryModeIndex; // 0x10343020 current active weapon mode
}

void FElysiumWeapon::SwingActiveWeaponEvent()
{
	++SwingActiveWeaponEvents; // 0x10346cd0 slot 333(0x15,1,0,0,0,0)
	// 0015 weapon-model SetActivity input seam: no held-model activity table, answers nothing.
}

bool FElysiumWeapon::SwingHasRecordedHit(const FElysiumEntityHandle& Victim) const
{
	for (const TArray<FElysiumEntityHandle>& RecordHitList : Swing.RecordHits)
		if (RecordHitList.Contains(Victim)) return true; // 0x10343e37 read-only hit-once probe
	return false; // 0x10343020
}

void FElysiumWeapon::AdvanceSwingContact(float /*DeltaSeconds*/)
{
	if (FElysiumCombatCharacter* const SwingOwner = OwnerCharacter())
		SwingOwner->MeleeSwingUpdate(); // 0x10346cd0 ignores caller time; +0xaa4 belongs to character
}

void FElysiumWeapon::BeginSwingPoseBatch()
{
	Swing.bPoseBatchActive = true; // D9: named CalcPose input fallback only // 0x10343020
	Swing.PoseBatchFromSegmentsLocal = Swing.PrevSegmentsLocal; // 0x10343020
	Swing.PoseBatchToSegmentsLocal.Reset(); // 0x10343020
	Swing.PoseBatchHasSegments.Empty(); // 0x10343020
}

void FElysiumWeapon::EndSwingPoseBatch()
{
	Swing.bPoseBatchActive = false; // D9: the next update supplies a fresh endpoint // 0x10343020
	Swing.PoseBatchFromSegmentsLocal.Reset(); // 0x10343020
	Swing.PoseBatchToSegmentsLocal.Reset(); // 0x10343020
	Swing.PoseBatchHasSegments.Empty(); // 0x10343020
}

void FElysiumWeapon::MeleeSwingStep(const FVector& StepOrigin, const FVector& StepAngles,
	float PreviousCycle, float CurrentCycle, float PoseFraction)
{
	FElysiumCombatCharacter* const Attacker = OwnerCharacter(); // 0x10343020
	if (Attacker == nullptr) return; // 0x10343020

	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		return; // 0x10343020 no model input, nothing stored or cleared
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
		return;
	}

	const int32 SubSteps = 1; // 0x10343020: this is one Step, slot 315 owns ceil(dt*100)

	// The swing's own identity, copied out of the transaction before the first commit: a contact
	// can retire the transaction, and the walk must not read a cleared one back through it.
	FSwingContact Contact;
	Contact.ModeIndex = Swing.ModeIndex;
	Contact.Serial = Swing.Serial;
	Contact.ClipLabel = Swing.ClipLabel;
	Contact.ClipOwnerStem = Swing.ClipOwnerStem;

	// Where every record's segment is RIGHT NOW, in the attacker's own frame. Held per record rather
	// than per bone so the interpolation below is a straight index walk; the bone queries themselves
	// are cached by name, because a 17-record swing names two or three bones.
	const FTransform NowFrame(ElysiumPlayerView::ToUnreal(StepAngles),
		StepOrigin); // 0x100c3600 supplied substep origin/angles
	const bool bBatchPose = Swing.bPoseBatchActive && PoseFraction >= 0.f; // D9 fallback // 0x10343020
	const FTransform BoneInputFrame = bBatchPose
		? FTransform(ElysiumPlayerView::ToUnreal(Attacker->Angles), Attacker->Origin) : NowFrame; // 0x10343020
	TMap<FString, FTransform> BoneCache;
	TArray<TPair<FVector, FVector>> NowSegments;
	NowSegments.SetNum(Records->Num());
	TBitArray<> HasSegment(false, Records->Num());
	const bool bCachedBatchPose = bBatchPose && Swing.PoseBatchToSegmentsLocal.Num() == Records->Num(); // 0x10343020
	if (bCachedBatchPose)
	{
		NowSegments = Swing.PoseBatchToSegmentsLocal; // 0x10343020
		HasSegment = Swing.PoseBatchHasSegments; // 0x10343020
	}
	else
	{
		for (int32 Index = 0; Index < Records->Num(); ++Index)
		{
			const FElysiumSwingRecord& Record = (*Records)[Index]; // 0x10343020
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
			const FTransform* BoneWorld = BoneCache.Find(Record.Bone); // 0x10343020
			if (BoneWorld == nullptr)
			{
				FTransform Resolved; // 0x10343020
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
					continue; // 0x10343020
				}
				BoneWorld = &BoneCache.Add(Record.Bone, Resolved); // 0x10343020
			}
			// The endpoints are bone-local Unreal centimetres and the bone frame is the live one, so the
			// placement is one transform and no conversion (the `UE_` exporter already stated them in
			// this frame). They are then expressed against the attacker's own frame, which is what lets
			// the sub-steps carry the limb on an interpolated root.
			NowSegments[Index] = TPair<FVector, FVector>(
				BoneInputFrame.InverseTransformPosition(BoneWorld->TransformPosition(Record.ACm)),
				BoneInputFrame.InverseTransformPosition(BoneWorld->TransformPosition(Record.BCm))); // 0x10343020
			HasSegment[Index] = true; // 0x10343020
		}
		if (bBatchPose)
		{
			Swing.PoseBatchToSegmentsLocal = NowSegments; // D9 previous/current update endpoints // 0x10343020
			Swing.PoseBatchHasSegments = HasSegment; // 0x10343020
		}
	}
	if (bBatchPose)
	{
		if (Swing.PoseBatchFromSegmentsLocal.Num() != Records->Num())
			Swing.PoseBatchFromSegmentsLocal = NowSegments; // D9 unprimed endpoint // 0x10343020
		for (int32 PoseRecordIndex = 0; PoseRecordIndex < Records->Num(); ++PoseRecordIndex)
		{
			if (!HasSegment[PoseRecordIndex]) continue; // 0x10343020 missing bone: skip whole record
			NowSegments[PoseRecordIndex].Key = FMath::Lerp(Swing.PoseBatchFromSegmentsLocal[PoseRecordIndex].Key,
				NowSegments[PoseRecordIndex].Key, PoseFraction); // D9 retained live-pose lerp // 0x10343020
			NowSegments[PoseRecordIndex].Value = FMath::Lerp(Swing.PoseBatchFromSegmentsLocal[PoseRecordIndex].Value,
				NowSegments[PoseRecordIndex].Value, PoseFraction); // 0x10343020
		}
	}

	// The first batch has no previous position: it covers the instant the clip stands on and sweeps
	// nothing, which is what an unprimed cursor means everywhere else in this runtime.
	bool bPrimed = Swing.PrevCycle >= 0.0f && Swing.PrevSegmentsLocal.Num() == Records->Num(); // 0x10343020

	const float PrevCycle = PreviousCycle; // 0x10343020 caller previous cycle
	const FTransform PrevFrame = bPrimed
		? FTransform(Swing.PrevAngles, Swing.PrevOrigin)
		: NowFrame;

	// `MeleeSwingStep 0x10343020`, step 1: `count = min(seqdesc+0x2c4, 20)`.
	const int32 Count = FMath::Min(Records->Num(), ElysiumSwing::MaxRecords);
	if (Swing.RecordHits.Num() != Records->Num())
	{
		Swing.RecordHits.SetNum(Records->Num());
	}

	TArray<ElysiumSwing::FInterval> Intervals;
	Intervals.Add({PrevCycle, CurrentCycle}); // 0x10343020 one inclusive window

	// `this+0x94`, the attacker's NPC self-pointer: the relation filter below is the NPC's alone.
	const bool bAttackerIsNpc = Attacker->AsNpcBase() != nullptr; // 0x1034394d NPC +0x94

	// One pass of `MeleeSwingStep(pos, ang, prevCycle, cycle)` per sub-step, as `MeleeSwingUpdate
	// 0x10346cd0` calls it (who calls the sweep and over which cycles is lane C1's).
	TArray<FElysiumEntityHandle> BoxEntities;
	for (int32 Step = 0; Step < Intervals.Num() && Swing.bActive; ++Step)
	{
		const float U0 = static_cast<float>(Step) / static_cast<float>(SubSteps);
		const float U1 = static_cast<float>(Step + 1) / static_cast<float>(SubSteps);

		// Step 2, per record.
		for (int32 Index = 0; Index < Count && Swing.bActive; ++Index)
		{
			if (!HasSegment[Index])
			{
				// The bone index is negative (`0x100c77d0(model, sequence, rec+8) < 0`): the record is
				// skipped whole, nothing stored and nothing cleared.
				continue;
			}
			const FElysiumSwingRecord& Record = (*Records)[Index];
			const TPair<FVector, FVector>& From = bPrimed
				? Swing.PrevSegmentsLocal[Index] : NowSegments[Index];
			const TPair<FVector, FVector>& To = NowSegments[Index];

			// `A`, `B`: the record's two points at this pass. `lastA`, `lastB` (`+0xac0 + r * 0x18`):
			// the same two points as the previous pass stored them at its end, window open or closed
			// -- here the same function asked at the previous sub-step, which is that store.
			FVector A, B, LastA, LastB;
			ElysiumSwingEndpointsAt(FElysiumSwingEndpointQuery{ PrevFrame, NowFrame, From, To, U1,
				Intervals[Step].End, Attacker, &Record, this }, A, B); // 0x10343020
			ElysiumSwingEndpointsAt(FElysiumSwingEndpointQuery{ PrevFrame, NowFrame, From, To, U0,
				Intervals[Step].Start, Attacker, &Record, this }, LastA, LastB); // 0x10343020

			// `n = ceil(|B - A| * 0.1666667)` (f32 `0x10488874`); `n < 2` -> 1 and `A = B =` the
			// midpoint. The stored last endpoints are the collapsed ones too (the store at
			// `0x103443ed` writes the `A` / `B` the samples used), and the segment's length is the
			// record's own, so the previous pass collapsed on the same `n`.
			const int32 Samples = ElysiumSwing::SampleCount(A, B);
			if (Samples < 2)
			{
				A = B = (A + B) * 0.5;
				LastA = LastB = (LastA + LastB) * 0.5;
			}

			// The window: open iff `m_bMeleeSwingIsLive (+0xaa1)` (true here: the clip is live and
			// declares records) and `start <= cycle && end >= prevCycle`, inclusive at both ends.
			// Closed -> this record's hit-list count (`+0xcac + r * 0x14`) = 0, on THIS pass.
			if (!Attacker->bMeleeSwingIsLive
				|| !ElysiumSwing::StepWindowOpen(Record, Intervals[Step].Start, Intervals[Step].End))
			{
				Swing.RecordHits[Index].Reset();
				continue;
			}

			// The entities in the AABB over `{A, B, lastA, lastB}`: `0x101cca80(list, 100, &mins,
			// &maxs, 0x22102080, 1)` -- every entity, not combat characters only (D5).
			FElysiumSwingSweep Sweep;
			Sweep.Attacker = Attacker->Handle;
			Sweep.PrevA = LastA;
			Sweep.PrevB = LastB;
			Sweep.CurA = A;
			Sweep.CurB = B;
			Embodiment->QuerySwingStepEntities(Sweep, BoxEntities);
			if (BoxEntities.Num() > ElysiumSwing::MaxBoxEntities)
			{
				BoxEntities.SetNum(ElysiumSwing::MaxBoxEntities);   // the list's capacity, `PUSH 100`
			}

			// Step 3, per entity `e` in the box, in retail's order.
			for (const FElysiumEntityHandle& Hit : BoxEntities)
			{
				if (!Swing.bActive)
				{
					// A commit can retire the transaction, and the hit lists are its own storage.
					break;
				}
				FElysiumEntity* Ent = World ? World->Resolve(Hit) : nullptr;
				if (Ent == nullptr)
				{
					continue;   // a handle the world no longer holds is no entity in the box
				}
				FElysiumCombatCharacter* VictimCC = ElysiumSwingVictimCharacter(*Ent);   // `e+0x9c`

				// 3.1 -- the NPC attacker's relation filter, `0x1034394d..0x103439a7` (D1): an NPC
				// (`this+0x94`), `debug_allow_melee_ff` (`0x10936ee0`) 0, a combat character other
				// than the attacker -> slot 404 `IRelationType` must be 1 or 2 (hate, fear) AND
				// `0x10323930(npc, victim)` (same team) false; else skipped. A player attacker has no
				// relation filter.
				if (bAttackerIsNpc && !MeleeFriendlyFireAllowed() && VictimCC != nullptr
					&& VictimCC != Attacker)
				{
					const int32 Relation = Attacker->IRelationType(Ent);
					if ((Relation != 1 && Relation != 2) || ElysiumSwingSameTeam(*Attacker, *VictimCC))
					{
						continue;
					}
				}
				// 3.2 -- `e` solid: `m_nSolidType (+0x2b0) != 0` and `!(+0x2b4 & 4)` (D6). The port's
				// record of those two words is `IsRetailNotSolid` (an entity that never recorded a
				// `SetSolid` is not read as `SOLID_NONE`, its stated rule). This is the gate the
				// removed "reported dead" exclusion stood for: a ragdoll carries `FSOLID_NOT_SOLID`.
				if (Ent->IsRetailNotSolid())
				{
					continue;
				}
				// 3.3 -- the ATTACKER not `FSOLID_NOT_SOLID` (`this+0x2b4 & 4`), else nothing is hit.
				if ((Attacker->RetailSolidFlags & 0x4u) != 0)
				{
					continue;
				}
				// 3.4 -- the candidate's byte `+0xf4` (`0x100b5190`, `m_bScriptHidden`) zero. This
				// port carries the word as `FElysiumEntity::bHidden`.
				if (Ent->IsHidden())
				{
					continue;
				}
				// 3.5 -- `victimCC == 0` or `m_bIsBCCTargetable (+0x1480) != 0`.
				if (VictimCC != nullptr && !FElysiumNpcBase::IsBccTargetable(*Ent))
				{
					continue;
				}
				// 3.6 -- already in THIS record's hit list.
				if (ElysiumSwing::IsMarked(Swing.RecordHits[Index], Hit))
				{
					continue;
				}
				// 3.7 -- the root of `e`'s owner chain (`0x1012c9c0`) is the attacker: `e` is added to
				// this record's list only, no hit (the attacker itself, its weapon, its attachments).
				if (ElysiumSwingOwnerRoot(*World, *Ent) == Attacker)
				{
					Swing.RecordHits[Index].Add(Hit);
					continue;
				}

				// WHICH record reached this entity, stamped per contact: the knockback the victim
				// answers with is authored on the record (slot 270's second argument).
				Contact.RecordIndex = Index;

				// 3.8 -- `victimCC == 0`, or the victim's slot 329 (`+0x524`) true
				// (`0x10343b00..0x10343b16` -> `0x10343eb0`; base `0x1014f850` false,
				// `CNPC_VTzimisceRunner 0x103c30c0` true): the box overlap IS the hit, no ray (D5, D7).
				// The trace is the attacker's origin to `e`'s origin; `e` is added to the hit list of
				// EVERY record of the sequence, no window test; then slot 270.
				if (VictimCC == nullptr || VictimCC->Slot329())
				{
					for (int32 Other = 0; Other < Count; ++Other)
					{
						Swing.RecordHits[Other].AddUnique(Hit);
					}
     if (World->HasAiTraceSink()) World->EmitAiTrace(*Ent, TEXT("swinghit"), FString::Printf(TEXT("from=%s"), *Attacker->TargetName)); // 0x10343b16 read-only insertion tap
					Contact.TraceStartCm = Attacker->Origin;
					Contact.TraceEndCm = Ent->Origin;
					MeleeContact(*Attacker, *Ent, Contact);
					continue;
				}

				// 3.9 -- a character, slot 329 false: the ray (D4). For `i = 0 .. n-1`: `f = n > 1 ?
				// 1 - i/(n-1) : 0`; `P = A + (B - A) * f`; `Q = lastA + (lastB - lastA) * f`; a
				// zero-extent ray from `Q` to `P` clipped to `e` ALONE (`0x101d2530(&ray, 0x200400b,
				// e, &tr)`). No world or occlusion trace stands between the limb and the entity.
				for (int32 Sample = 0; Sample < Samples; ++Sample)
				{
					const float F = ElysiumSwing::SampleFraction(Sample, Samples);
					const FVector P = A + (B - A) * F;
					const FVector Q = LastA + (LastB - LastA) * F;
					FElysiumSwingRayHit Trace;
					Embodiment->ClipSwingRayToEntity(Q, P, Hit, Trace);
					if (!Trace.DidHit())
					{
						continue;   // `fraction >= 1`, not `allsolid`, not `startsolid`: next `i`
					}
					// The first sample that hits is the hit: `e` is added to the list of every record
					// whose window overlaps this one's (`start_r <= end_k && start_k <= end_r`,
					// `0x10343e37..0x10343e84`), BEFORE the commit; then slot 270 `(&tr, record)`.
					ElysiumSwing::MarkHit(*Records, Index, Hit, Swing.RecordHits);
     if (World->HasAiTraceSink()) World->EmitAiTrace(*Ent, TEXT("swinghit"), FString::Printf(TEXT("from=%s"), *Attacker->TargetName)); // 0x10343e37 read-only insertion tap
					Contact.TraceStartCm = Q;
					Contact.TraceEndCm = Trace.EndPosCm;
					MeleeContact(*Attacker, *Ent, Contact);
					break;
				}
			}

			// Step 4 -- the wall contact (`0x10343f96`), only when the ATTACKER's slot 328 (`+0x520`)
			// is true: `CBasePlayer 0x1015dca0` and `CNPC_VWerewolf 0x103ca730` answer 1, the base
			// `0x1014f830` 0, so no other NPC ever takes it (D8).
			if (Swing.bActive && Attacker->Slot328())
			{
				FSwingWallStep Wall;
				Wall.A = A;
				Wall.B = B;
				Wall.LastA = LastA;
				Wall.LastB = LastB;
				Wall.Samples = Samples;
				SwingWallContact(*Attacker, Wall);
			}
		}
	}

	// Step 5 -- after the record loop, records `count..19` have their hit-list counts zeroed.
	if (Swing.bActive)
	{
		for (int32 Index = Count; Index < Swing.RecordHits.Num(); ++Index)
		{
			Swing.RecordHits[Index].Reset();
		}
	}

	Swing.PrevCycle = CurrentCycle; // 0x103443ed store endpoints even when closed
	Swing.PrevSegmentsLocal = MoveTemp(NowSegments);
	Swing.PrevOrigin = StepOrigin; // 0x103443ed
	Swing.PrevAngles = ElysiumPlayerView::ToUnreal(StepAngles); // 0x100c3600 full pitch/yaw/roll
}

void FElysiumWeapon::MeleeContact(FElysiumCombatCharacter& Attacker, FElysiumEntity& HitEntity,
	const FSwingContact& Contact)
{
	// The weapon's slot 270 (`+0x438`), `0x102579f0(this weapon, trace*, record)`, step for step
	// (spec 0002 `stories/v4/packets-S5.md` item 3). Step 1: `owner = 0x10252240(this)` is the
	// caller's `Attacker`; `ent = trace+0x4c` is `HitEntity`; `victimCC = ent ? ent+0x9c : 0`.
 if (World && World->HasAiTraceSink()) World->EmitAiTrace(HitEntity, TEXT("meleeimpact"), FString::Printf(TEXT("from=%s"), *Attacker.TargetName)); // 0x102579f0 read-only entry tap
	FElysiumCombatCharacter* const VictimCC = ElysiumSwingVictimCharacter(HitEntity);

	// Step 2 -- weapon slot 339 (`+0x54c`) with no argument, then `CSoundEnt::InsertSound(0x10,
	// &trace.endpos, [0x1072bc5c], 0.2, [0x1072bcb7], owner)`, both BEFORE the null-entity test
	// (D10). Seams: recorded (`FMeleeImpactSeams`). The null-entity return has no reader here: the
	// walk hands this body an entity on every path.
	++MeleeImpactSeams.WeaponSlot339Calls;
	++MeleeImpactSeams.CombatSoundInserts;
	MeleeImpactSeams.LastCombatSoundCm = Contact.TraceEndCm;

	// Step 3 -- `dir = normalize(trace.endpos - trace.startpos)`. The player's HUD line
	// (`"MeleeHit %s with %s"`, the mode record's `+0x3c4 > 0`) is a debug print and is not carried.

	const int32 ModeIndex = Contact.ModeIndex;
	const FElysiumWeaponContext Context =
		FElysiumWeaponContext::FromCharacter(VictimCC != nullptr ? *VictimCC : Attacker);
	const FElysiumDmg& ModeDmg = DamageForMode(ModeIndex);

	// Step 5 -- `blocked = WasMeleeBlocked(owner, victimCC)`; `rolls = GetMeleeDiceRolls(owner,
	// victimCC)`.
	//
	// The opposed record is CONSUMED from the defender: it was staged on the swing's first batched
	// frame, before any contact test, by `StageSwingOpposedRoll` -- which is where retail rolls it.
	// Nothing is rolled here. The lookup is scoped to THIS swing's serial (retail clears the
	// attacker's record array at every swing start, `ForceMeleeReset`), so a body the swing's own
	// 60-unit roll query never selected has no record: that is retail's `rolls == 0`.
	const FElysiumMeleeRoll* Staged =
		VictimCC != nullptr ? VictimCC->FindMeleeRoll(Attacker.Handle, Contact.Serial) : nullptr;
	const bool bRolled = Staged != nullptr;
	const FElysiumMeleeRoll Roll = bRolled ? *Staged : FElysiumMeleeRoll();

	// The `rules.txt` margin classifier (the defender's own answer inside `WasMeleeBlocked`).
	const int32 Margin = Roll.Margin();
	const EElysiumMeleeAttackerReaction AttackerReaction =
		ElysiumWeapons::ClassifyAttacker(Context.Margins, Margin);
	const EElysiumMeleeDefenderReaction DefenderReaction =
		ElysiumWeapons::ClassifyDefender(Context.Margins, Margin);
	if (bRolled && !Context.Margins.bValid && ShouldReportOnce(TEXT("margins")))
	{
		UE_LOG(LogElysiumWeapon, Warning,
			TEXT("rules.txt Melee_Reactions is unavailable — the melee reaction margin cannot be "
				"classified; damage still resolves and no blocked reaction is selected"));
	}

	// **`rolls == 0` -> `blocked = 0`, successes 1, and the damage goes on** (`0x10257b92..
	// 0x10257c1c`; D2). Else `through = DamageWentThrough(rolls)` (`0x10349650`: `0 < rolls[1] -
	// rolls[3] - rolls[2]`, lethality less soak less defense) and **`successes = (int)(rolls[1] -
	// rolls[2] - rolls[3])`** -- `0x10257ba1..0x10257bbf`: `MOV EDX,[ESI+4]; SUB EDX,[ESI+8]; SUB
	// EDX,[ESI+0xc]; FILD; CALL __ftol`. The operand the packet S5 had as lost is the same signed
	// margin, read from the listing 2026-10-04 (spec 0002 V11-2). `dmg+0x38 = rolls[3]` (the soak
	// word copied into the packet) has no field on `FElysiumDmg`: not carried, named here.
	const bool bBlocked = bRolled && VictimCC != nullptr
		&& WasMeleeBlocked(Attacker, *VictimCC, DefenderReaction);
	const bool bThrough = Margin > 0;
	int32 Successes = bRolled ? Margin : 1;

	// SEAM — the blocked-contact `Dexterity` bonus soak re-roll at `0x10160BC0` and the knockback
	// impulse still belong to a later cycle. The re-roll is a SOAK rule, not a reaction one: it adds
	// bonus soak dice from attribute slot 2 and re-classifies, so it changes the NUMBER the block
	// family then reacts to (`docs/vtmb/combat-and-damage.md:485-488`).
	//
	// The attacker's own classification stays on this line rather than branching the reaction below:
	// retail gives the attacker ONE blocked-reaction source — the activity its swing sequence stores
	// — and no blocked/blocked-major split over it. The band is diagnosis, not a fork.
	UE_LOG(LogElysiumWeapon, Verbose,
		TEXT("%s -> %s melee %s margin %d (lethality %d - defense %d - soak %d): attacker %s, "
			"defender %s, %s"),
		*Attacker.DebugString(), *HitEntity.DebugString(),
		bRolled ? TEXT("rolled") : TEXT("unrolled (rolls == 0)"), Margin, Roll.Lethality,
		Roll.Defense, Roll.Soak, ElysiumWeapons::AttackerReactionName(AttackerReaction),
		ElysiumWeapons::DefenderReactionName(DefenderReaction),
		bBlocked ? TEXT("blocked") : TEXT("unblocked"));

	// The two blocked reactions -- the victim's slot 318 (`+0x4f8`) and the owner's slot 319
	// (`+0x4fc`, `(victim, rolls, &dir, ...)`) -- BEFORE the damage (`0x10257bcb..`).
	if (bBlocked)
	{
		FElysiumCombatCharacter& Victim = *VictimCC;
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

	// **The dispatch condition** (`0x10257cb3..`; D3): `!through && blocked` -> the tail, no damage
	// and no reaction. Every other contact -- unrolled, unblocked with a non-positive margin, or
	// blocked with the damage through -- dispatches, with `successes < 1 -> 1`.
	const bool bDispatch = !(bRolled && !bThrough && bBlocked);
	if (bDispatch)
	{
		if (Successes < 1)
		{
			Successes = 1;
		}

		// Step 6 -- `m_iToHitSuccesses = successes`; `AddFlags(8)`; `DispatchTraceAttack(ent, info,
		// dir, trace)`; `inflicted` = the accumulator's damage. **Kept, and named**: this port takes
		// `inflicted` as the success count itself (the accumulator's read-back after `TraceAttack`
		// is not walked), now floored at 1 with it.
		int32 DamageInflicted = Successes;
		// Potence: the owner's type-3 stat list `GetValue(9) > inflicted` -> `inflicted =` it.
		DamageInflicted = FMath::Max(DamageInflicted, ElysiumWeapons::ActivePotenceRank(Attacker));

		// `total = (modifier + m_iDiceAmt) x mult x inflicted`. `modifier` is `0x10204900(
		// dmg.m_vtModifierDependency, owner)`: the ATTACKER's rating for the descriptor's close-combat
		// attack feat — `Close_Combat_Brawl` for fists, `Close_Combat_Melee` for an armed weapon — a
		// rating rather than a roll (K5). `mult` is `0x101c2a70(info)`, the trace envelope's, the one
		// half RE40 does not decompose for melee. NOT carried: the owner's misc flag `0x100000` arm
		// (`total *= 0x101e8f20(&0x10739d08, owner) * 0.01`, flag removed) and the force
		// `0x103455a0(owner, victimCC, record, &force)`.
		const int32 DamageModifier = FeatRating(Attacker, ModeDmg.AttackFeat, Context);
		const int32 Total = ElysiumWeapons::MeleeDamageTotal(DamageInflicted, ModeDmg.BaseDamage,
			DamageModifier, ElysiumWeapons::DefaultMeleeMultiplier);

		FElysiumDmg Dmg = ModeDmg;
		Dmg.Source = Attacker.Handle;
		// CBaseCombatWeapon::102579f0 writes packet+0x28 from weapon `this` after 101c2770 seeds
		// the attacker. Identity is valid even while the held-weapon spatial seam has not supplied a
		// packet position; `0x10265ed0` reads the inflictor's origin (the held weapon's is its owner's).
		Dmg.Inflictor = Handle;
		// The direct-damage route: the value above IS the damage-success count, so the resolver's damage
		// roll is bypassed. Its soak test still runs.
		Dmg.Flags |= ElysiumDamage::FlagDirectInput;
		Dmg.ExtraInput = Total;

		UE_LOG(LogElysiumWeapon, Verbose,
			TEXT("%s -> %s melee inflicted %d x (base %d + modifier %d) x multiplier %.3f = %d"),
			*Attacker.DebugString(), *HitEntity.DebugString(), DamageInflicted, ModeDmg.BaseDamage,
			DamageModifier, ElysiumWeapons::DefaultMeleeMultiplier, Total);

		const FElysiumItemDef* Record = Data();
		const bool bDisallowFirearmsToBashing = Record && Record->bDisallowFirearmsToBashing;
		if (VictimCC != nullptr)
		{
			VictimCC->TakeDamage(Dmg, &Attacker, bDisallowFirearmsToBashing);
		}
		else
		{
			// A non-character (a prop, a breakable, a loot container): `DispatchTraceAttack(ent, ...)`
			// and the apply `0x101c2a10(&DAT_1072cb10, -1)` end in the entity's own slot 142
			// `OnTakeDamage` (D5). The packet is the kernel's (`FElysiumTakeDamageInfo`).
			FElysiumNpcBase::FElysiumTakeDamageInfo Info;
			Info.Dmg = &Dmg;
			Info.Attacker = Attacker.Handle;
			Info.Damage = static_cast<float>(Total);
			Info.DamageBits = Dmg.DmgMask;
			Info.bDisallowFirearmsToBashing = bDisallowFirearmsToBashing;
			HitEntity.OnTakeDamage(&Info);
		}
		// Weapon slot 269 (`+0x434`) `(ent, dir.x, dir.y)` closes step 6: unread, not carried.

		// Step 7 -- `!blocked && victimCC` (D11), asked AFTER the damage (the commit above may have
		// killed this victim, which is why a killing blow is never knocked back: slot 326's base
		// `0x103482e0` then refuses): `(record == 0 || record == -0x28 || !victim slot 326
		// (+0x518)(record)) && !victim slot 400 (+0x640)()` -> victim slot 321 (`+0x504`), the plain
		// hit reaction; else `GetKnockbackActivity(victim, trace, owner, record)` (-1 -> a Warning)
		// and victim slot 320 (`+0x500`) `(owner)` -- `KnockbackContact`, which selects the cell off
		// the record's own table and plays it.
		//
		// **SEAM — grounded cells only.** The nine-activity flying chain is the other half of the
		// knockback: a two-stage velocity assignment with a one-think delay and a land/wall
		// terminator, which needs a motor verb this seam does not have yet.
		if (!bBlocked && VictimCC != nullptr)
		{
			const FElysiumSwingRecord* SwingRecord = ResolveSwingRecord(Attacker, Contact);
			const bool bRecordAdmits = SwingRecord != nullptr
				&& VictimCC->Slot326(const_cast<FElysiumSwingRecord*>(SwingRecord), &Attacker);
			if (!bRecordAdmits && !VictimCC->AllowsKnockbackBypass())
			{
				VictimCC->Slot321(&Attacker);
			}
			else
			{
				KnockbackContact(Attacker, *VictimCC, SwingRecord);
			}
		}
	}

	// Step 8 -- the tail, on EVERY path past the null-entity test (`0x10258019..0x1025804b`; D10):
	// `0x101cfef0(trace, 0x80, 1, this)` the impact effect (seam: recorded), then `ent` slot 21
	// (`+0x54`) with the OWNER (`PUSH EBP` at `0x1025803e`), then the owner's slot 24 (`+0x60`)
	// `OnVictimHitByMe(ent)` (`PUSH EDI` at `0x10258045`).
	//
	// Slot 21 on the Troika line (`0x1029f800`) is the hit-buildup counter's one increment, so the
	// raise runs HERE -- after step 7's slot-326 test read the counter, and on a blocked contact
	// too. The walk no longer raises it itself.
	++MeleeImpactSeams.ImpactEffects;
	MeleeImpactSeams.LastImpactEffectCm = Contact.TraceEndCm;
	HitEntity.Slot21(&Attacker);
	Attacker.OnVictimHitByMe(&HitEntity);
}

void FElysiumWeapon::SwingWallContact(FElysiumCombatCharacter& Attacker, const FSwingWallStep& Step)
{
	// `MeleeSwingStep 0x10343020`, `0x10343f96..0x103443d6`: per sample the same `Q -> P` ray
	// through the engine trace (`[0x1070b254]` slot 4, mask `PUSH 0x400b`) with a simple filter on
	// the attacker (`0x101d3190(this, 0)`).
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		return;
	}
	// `AngleVectors(slot 221 (+0x374)(), &forward, 0, 0)` at `0x10344290..0x103442a7`.
	const FVector Forward = ElysiumSkeletalBasis::FromSourceAngles(Attacker.Angles).Vector();

	for (int32 Sample = 0; Sample < Step.Samples; ++Sample)
	{
		const float F = ElysiumSwing::SampleFraction(Sample, Step.Samples);   // `0x10343fc4`
		const FVector Q = Step.LastA + (Step.LastB - Step.LastA) * F;
		const FVector P = Step.A + (Step.B - Step.A) * F;

		FElysiumRetailTrace Trace;
		Trace.StartCm = Q;
		Trace.EndCm = P;
		Trace.RetailMask = 0x400b;
		Trace.Ignore.Add(Attacker.Handle);
		FElysiumRetailTraceResult Result;
		Embodiment->TraceRetail(Trace, Result);

		// `0x103441f3..0x1034421b`: `fraction < 1.0 || allsolid || startsolid`, else the next sample.
		if (!(Result.Fraction < 1.f || Result.bAllSolid || Result.bStartSolid))
		{
			continue;
		}
		// `0x10344221..0x1034428a`: the plane normal non-zero with `|normal.z| < 0.3` (f32
		// `0x10451ab8`), else the next sample -- no reaction and no effect.
		if (!ElysiumSwing::WallPlaneQualifies(Result.Normal))
		{
			continue;
		}
		// `0x103441bf..0x103441ec`: the hit point, `Q + (P - Q) * fraction` (x and y are all the
		// distance test reads).
		const FVector HitPoint = Q + (P - Q) * Result.Fraction;
		if (ElysiumSwing::WallBlocksSwing(Result.Normal, Forward, HitPoint, Attacker.Origin))
		{
			// `0x103443c6`: the attacker's slot 319 (`+0x4fc`) `(0, 0, 0)`, the blocked reaction; the
			// sample loop ends.
			++MeleeImpactSeams.WallBlockedReactions;
			Attacker.PlayerAttackerBlockedReaction(nullptr, nullptr, nullptr);
			return;
		}
		// `0x10344384..0x103443ac`: otherwise, once per swing (`+0xaa0` clear), the impact effect
		// `0x101cfef0(&tr, 0x80, 1, weapon)` when there is a weapon (there is: this one), and
		// `+0xaa0 = 1` either way.
		if (!Swing.bWallEffectSpent)
		{
			++MeleeImpactSeams.ImpactEffects;
			MeleeImpactSeams.LastImpactEffectCm = HitPoint;
			Swing.bWallEffectSpent = true;
		}
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
	// **The raise happens AFTER this read** (re-read from the listing 2026-10-04, spec 0002 V11-2;
	// an earlier note here had it before). The one increment site `0x1029F800` is the Troika body of
	// entity slot 21, and `CBaseCombatWeapon::FUN_102579F0` dispatches slot 21 on the hit entity in
	// its TAIL (`0x1025803e..0x1025803f`: `PUSH owner; CALL [vtable + 0x54]`), past the slot-326
	// test at step 7; no other slot-21 dispatch on an entity pointer exists in that body. So the
	// counter a blow tests does not yet carry that blow: with the default of `2` a victim is
	// admitted at counts 0, 1 and 2. UNVERIFIED: that nothing inside the `DispatchTraceAttack` chain
	// dispatches slot 21 as well (the chain is not walked).
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
	// Shot102387b0 writes its weapon `this` to the ranged packet's +0x98 inflictor field; the
	// downstream CAI-sound path maps that to the same `CVDmg_t` +0x28 field.
	Dmg.Inflictor = Handle;
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



bool FElysiumWeapon::CanReloadMagazine(int32 MagazineIndex) const
{
	const FElysiumItemDef* const ReloadRecord = Data(); // 0x10253ab0
	// Slot 277 (+0x454): magazine 1 has no clip or ammo type in the item surface;
	// retail m_iMagazineCurAmts[1] +0x750 is an absent input, never magazine 0's count.
	const bool bHasAmmoType = MagazineIndex == 0 && ReloadRecord != nullptr
		&& !ReloadRecord->AmmoType.IsEmpty(); // 0x10253b40
	if (!bHasAmmoType) return true; // 0x10253ab0 first arm
	if (ReloadRecord->MagazineSize > 0 && MagazineCount > 0) return true; // 0x10253ab0 slot 277
	const FElysiumCombatCharacter* const ReloadOwner = OwnerCharacter(); // 0x102521f0
	return ReloadOwner != nullptr
		&& ReloadOwner->Inventory.Reserve(ReloadRecord->AmmoType) > 0; // 0x103346c0 / 0x10253ab0
}

#if !UE_BUILD_SHIPPING
namespace
{
void ObserveNativeReload(FElysiumWeapon& Weapon, const FString& Phase, const FString& Detail = FString())
{
	if (!Weapon.World || !Weapon.World->HasAiTraceSink()) return;
	FElysiumEntity* OwnerEntity = Weapon.World->Resolve(Weapon.Owner);
	FElysiumNpc* OwnerNpc = OwnerEntity ? OwnerEntity->AsNpc() : nullptr;
	if (!OwnerNpc) return;
	const FElysiumItemDef* Record = Weapon.Data();
	Weapon.World->EmitAiTrace(*OwnerNpc, FName(TEXT("reload")), FString::Printf(
		TEXT("%s item=%s %s handle=%s clip=%d reserve=%d in_reload=%d interrupt=%d jam=%d next_attack=%.6f now=%.6f primary=%.6f secondary=%.6f"),
		*Phase, *Weapon.ClassName(), *Detail, *Weapon.Handle.ToString(), Weapon.MagazineCount,
		Record ? OwnerNpc->Inventory.Reserve(Record->AmmoType) : 0, Weapon.bInReload ? 1 : 0, Weapon.bInterruptReload ? 1 : 0,
		Weapon.bIsJammed ? 1 : 0, OwnerNpc->NextAttackTime, Weapon.World->NowSeconds(), Weapon.NextPrimaryAttackTime, Weapon.NextSecondaryAttackTime));
}
}
#endif

void FElysiumWeapon::FinishReloadBulk()
{
	FElysiumCombatCharacter* const ReloadOwner = OwnerCharacter(); // 0x102552c0 / 0x102521f0
#if !UE_BUILD_SHIPPING
	ObserveNativeReload(*this, TEXT("bulk_enter"));
	const int32 ObservedClip = MagazineCount;
	const int32 ObservedReserve = ReloadOwner && Data() ? ReloadOwner->Inventory.Reserve(Data()->AmmoType) : 0;
#endif
	if (ReloadOwner == nullptr) return; // 0x102552c0 no owner, no writes
	const FElysiumItemDef* const ReloadRecord = Data(); // 0x102552c0 +0x8c8
	if (ReloadRecord != nullptr && ReloadRecord->bReloadSingle) // 0x102552c0
	{
		// 0x10254cd0(this, 0xc3): weapon activity-send hook is absent. No output;
		// player single-round continuation remains on BeginReload / CommitQueuedReload.
	}
	else
	{
		for (int32 ReloadMagazine = 0; ReloadMagazine < 2; ++ReloadMagazine) // 0x102552c0
		{
			// Slot 277: magazine 1's m_iMagazineCurAmts[1] +0x750 and ammo type are absent.
			if (ReloadMagazine != 0 || ReloadRecord == nullptr
				|| ReloadRecord->MagazineSize <= 0) continue; // 0x102552c0 slot 277
			const int32 ReloadReserve = ReloadOwner->Inventory.Reserve(ReloadRecord->AmmoType); // 0x103346c0
			const int32 ReloadMissing = ReloadRecord->MagazineSize - MagazineCount; // 0x102552c0 slot 275
			const int32 ReloadAdded = ReloadMissing < ReloadReserve ? ReloadMissing : ReloadReserve; // 0x102552c0
			MagazineCount += ReloadAdded; // 0x102552c0, deliberately no negative-capacity clamp
			if (IsPlayerSide(*ReloadOwner)) // 0x102552c0 owner player +0xa8
			{
				// DAT_1088aef4 (IsCommand OR int<1) is absent: retain the existing player-debit stand-in.
				ReloadOwner->Inventory.AddReserve(ReloadRecord->AmmoType, -ReloadAdded); // 0x102552c0 RemoveAmmo
			}
		}
	}
	bInReload = false; // 0x102552c0 +0x898
	bIsJammed = false; // 0x102552c0 +0x89a
	bInterruptReload = false; // 0x102552c0 +0x899
#if !UE_BUILD_SHIPPING
	ObserveNativeReload(*this, TEXT("bulk_commit"), FString::Printf(TEXT("clip_before=%d clip_after=%d reserve_before=%d reserve_after=%d flags=%d"),
		ObservedClip, MagazineCount, ObservedReserve, Data() ? ReloadOwner->Inventory.Reserve(Data()->AmmoType) : 0,
		(bInReload ? 1 : 0) | (bInterruptReload ? 2 : 0) | (bIsJammed ? 4 : 0)));
#endif
}

void FElysiumWeapon::FinishReload()
{
	FElysiumCombatCharacter* const ReloadOwner = OwnerCharacter(); // 0x10255062 / 0x102551e2
#if !UE_BUILD_SHIPPING
	ObserveNativeReload(*this, TEXT("finish_enter"), FString::Printf(TEXT("clip=%d reserve=%d in_reload=%d"), MagazineCount,
		ReloadOwner && Data() ? ReloadOwner->Inventory.Reserve(Data()->AmmoType) : 0, bInReload ? 1 : 0));
#endif
	if (ReloadOwner == nullptr) return; // 0x10255069 / 0x102551e9
	const FElysiumItemDef* const ReloadRecord = Data(); // 0x10255054 +0x8c8
	if (ReloadRecord != nullptr && ReloadRecord->bReloadSingle) // 0x1025505c
	{
		// 0x1025506f..77 requires PLAYER +0xa8: an NPC retains bInReload.
		// Player continuation (0x102550b4 / 0x102550d2 / 0x102551cb..dc) belongs
		// to the player weapon story; existing queued player reloads use their own route.
		return; // 0x1025523c
	}
	const FElysiumNpc* const ReloadNpc = ReloadOwner->AsNpc(); // 0x102551eb owner combat +0x9c
	// Player m_flNextAttack +0x1564 has no shared character accessor yet; no finish output here.
	if (ReloadNpc == nullptr || !bInReload || World == nullptr)
	{
#if !UE_BUILD_SHIPPING
		ObserveNativeReload(*this, TEXT("finish_refused"), TEXT("reason=owner_or_flag"));
#endif
		return; // 0x102551f3..fd
	}
	const double ReloadNow = World->NowSeconds(); // 0x10255205
	if (!(ReloadNpc->NextAttackTime <= ReloadNow))
	{
#if !UE_BUILD_SHIPPING
		ObserveNativeReload(*this, TEXT("finish_refused"), TEXT("reason=live_deadline"));
#endif
		return; // 0x102551ff..13 LIVE +0x1564, equality admitted; NaN refuses
	}
#if !UE_BUILD_SHIPPING
	ObserveNativeReload(*this, TEXT("finish_admitted"));
#endif
	FinishReloadBulk(); // 0x10255219 slot 323
	NextPrimaryAttackTime = ReloadNow; // 0x1025521f..3a +0x730
	NextSecondaryAttackTime = ReloadNow; // 0x1025521f..3a +0x734
#if !UE_BUILD_SHIPPING
	ObserveNativeReload(*this, TEXT("finish_stamps"), NextPrimaryAttackTime == ReloadNow && NextSecondaryAttackTime == ReloadNow ? TEXT("stamps=now") : TEXT("stamps=mismatch"));
#endif
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
