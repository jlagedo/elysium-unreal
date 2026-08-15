#include "Substrate/ElysiumDamage.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumDamage, Log, All);

const TCHAR* ElysiumDmgFamilyName(EElysiumDmgFamily Family)
{
	switch (Family)
	{
	case EElysiumDmgFamily::Bashing:    return TEXT("Bashing");
	case EElysiumDmgFamily::Lethal:     return TEXT("Lethal");
	case EElysiumDmgFamily::Aggravated: return TEXT("Aggravated");
	default:                            return TEXT("None");
	}
}

int32 FElysiumDmg::GetDmg() const
{
	if (AppliedDamage > 0) { return AppliedDamage; }
	return AppliedDamage == 0 ? BaseDamage : 0;
}

FString FElysiumDmg::Describe() const
{
	return FString::Printf(
		TEXT("%s base %d extra %d mask 0x%08x flags 0x%x src '%s' feat '%s' -> %d dmg - %d soak = %d"),
		ElysiumDmgFamilyName(Family), BaseDamage, ExtraInput, DmgMask, Flags,
		SourceTrait.IsEmpty() ? TEXT("-") : *SourceTrait,
		AttackFeat.IsEmpty() ? TEXT("-") : *AttackFeat,
		RolledSuccesses, SoakSuccesses, Remainder);
}

// ================================================================================================
// The rulebook halves
// ================================================================================================

FElysiumDamageContext FElysiumDamageContext::FromCharacter(const FElysiumCombatCharacter& Char)
{
	FElysiumDamageContext Context;
	UElysiumGameStateSubsystem* GameState = Char.World ? Char.World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rulebook = GameState ? GameState->Rulebook() : nullptr;
	if (!Rulebook)
	{
		return Context;   // headless / bare world: every consumer below fails safe and says so
	}
	Context.Feats = &Rulebook->Feats();
	Context.DiceTables = &Rulebook->Dice();

	// K9 — the two difficulties are `rules.txt` values, so they are read, never retyped. An absent
	// block leaves them at INDEX_NONE and the soak roll is skipped with a warning.
	const FElysiumRules& Rules = Rulebook->Rules();
	static const TCHAR* DamageBlock = TEXT("Damage_Info");
	if (Rules.Has(DamageBlock, TEXT("Soak_Difficulty_PC"))
		&& Rules.Has(DamageBlock, TEXT("Soak_Difficulty_NPC")))
	{
		Context.SoakDifficultyPc = Rules.Int(DamageBlock, TEXT("Soak_Difficulty_PC"));
		Context.SoakDifficultyNpc = Rules.Int(DamageBlock, TEXT("Soak_Difficulty_NPC"));
	}
	return Context;
}

namespace
{
	// A trait read by its `stats.txt` InternalName. The compiled slot table is the resolver, so a
	// name that stops resolving is a code/data disagreement and says so once rather than reading 0
	// forever in silence.
	int32 CurrentTrait(const FElysiumCombatCharacter& Char, const TCHAR* InternalName)
	{
		EElysiumTraitContainer Container = EElysiumTraitContainer::Attributes;
		int32 Slot = INDEX_NONE;
		if (!ElysiumFindSheetSlot(InternalName, Container, Slot))
		{
			static TSet<FString> Reported;
			const FString Key(InternalName);
			if (!Reported.Contains(Key))
			{
				Reported.Add(Key);
				UE_LOG(LogElysiumDamage, Warning,
					TEXT("no compiled sheet slot owns '%s' — the damage path reads it as 0"),
					InternalName);
			}
			return 0;
		}
		return Char.Sheet.GetCurrent(Container, Slot);
	}

	// Retail rolls soak at the player target number for a player and the NPC one for everything
	// else. The player is the entity the world created, not a classname test.
	bool IsPlayerSide(const FElysiumCombatCharacter& Char)
	{
		return Char.World != nullptr && Char.World->PlayerHandle() == Char.Handle;
	}

	const TCHAR* VictimClassname(const FElysiumCombatCharacter& Char)
	{
		return Char.Def ? *Char.Def->Classname : TEXT("(no def)");
	}

	// Step 9's populate-but-do-not-apply half. The accumulator is the product of the family filter
	// and, for a flame-family mask, the flame filter — both authored on the victim's NPC template.
	float AccumulateTemplateFilters(const FElysiumDmg& Dmg, const FElysiumCombatCharacter& Victim)
	{
		float Accumulated = 0.0f;
		float Family = 1.0f;
		const bool bHasFamily = Victim.GetTemplateDamageFilter(Dmg.Family, /*bFlame=*/false, Family);
		float Flame = 1.0f;
		const bool bFlameMask = (Dmg.DmgMask & ElysiumDamage::DmgBurn) != 0;
		const bool bHasFlame = bFlameMask
			&& Victim.GetTemplateDamageFilter(Dmg.Family, /*bFlame=*/true, Flame);
		if (bHasFamily || bHasFlame)
		{
			Accumulated = (bHasFamily ? Family : 1.0f) * (bHasFlame ? Flame : 1.0f);
		}
		return Accumulated;
	}
}

// ================================================================================================
// The authored `Dmg` grammar
// ================================================================================================

namespace ElysiumDamage
{
	namespace
	{
		// The recovered string-to-bit table, in the file's own order. A token that starts with
		// `DMG_` and is absent from this table is authored data (`DMG_FIST`), not a parse failure.
		struct FDmgToken { const TCHAR* Token; uint32 Bit; };
		const FDmgToken GDmgTokens[] =
		{
			{ TEXT("DMG_BULLET"),         DmgBullet },
			{ TEXT("DMG_SLASH"),          DmgSlash },
			{ TEXT("DMG_BURN"),           DmgBurn },
			{ TEXT("DMG_BLAST"),          DmgBlast },
			{ TEXT("DMG_CLUB"),           DmgClub },
			{ TEXT("DMG_BUCKSHOT"),       DmgBuckshot },
			{ TEXT("DMG_SUPERCLAWBITE"),  DmgSuperClawBite },
			{ TEXT("DMG_CLAWBITE"),       DmgClawBite },
			{ TEXT("DMG_SUNLIGHT"),       DmgSunlight },
			{ TEXT("DMG_FAITH"),          DmgFaith },
		};

		bool ParseFamilyWord(const FString& Token, EElysiumDmgFamily& OutFamily)
		{
			if (Token.Equals(TEXT("Bashing"), ESearchCase::IgnoreCase))
			{
				OutFamily = EElysiumDmgFamily::Bashing;
				return true;
			}
			if (Token.Equals(TEXT("Lethal"), ESearchCase::IgnoreCase))
			{
				OutFamily = EElysiumDmgFamily::Lethal;
				return true;
			}
			if (Token.Equals(TEXT("Aggravated"), ESearchCase::IgnoreCase))
			{
				OutFamily = EElysiumDmgFamily::Aggravated;
				return true;
			}
			return false;
		}

		bool IsIntegerToken(const FString& Token)
		{
			if (Token.IsEmpty())
			{
				return false;
			}
			int32 Start = (Token[0] == TEXT('-') || Token[0] == TEXT('+')) ? 1 : 0;
			if (Start >= Token.Len())
			{
				return false;
			}
			for (int32 i = Start; i < Token.Len(); ++i)
			{
				if (!FChar::IsDigit(Token[i]))
				{
					return false;
				}
			}
			return true;
		}
	}

	FElysiumDmg ParseDmg(const FString& Authored)
	{
		FElysiumDmg Out;
		TArray<FString> Tokens;
		Authored.ParseIntoArrayWS(Tokens);
		if (Tokens.IsEmpty())
		{
			// Nothing authored is an ordinary absence (an item mode with no `Dmg` line), not a
			// failure. The family stays None, which `Apply` rejects.
			return Out;
		}

		bool bHaveBase = false;
		for (const FString& Token : Tokens)
		{
			if (Token.StartsWith(TEXT("DMG_"), ESearchCase::IgnoreCase))
			{
				bool bKnown = false;
				for (const FDmgToken& Row : GDmgTokens)
				{
					if (Token.Equals(Row.Token, ESearchCase::IgnoreCase))
					{
						Out.DmgMask |= Row.Bit;
						bKnown = true;
						break;
					}
				}
				if (!bKnown)
				{
					// `DMG_FIST` is the shipped example: authored by the fists record and absent
					// from the recovered table, so it contributes no bit. Its fallback/alias
					// behaviour is an open research question, not a data defect to repair.
					UE_LOG(LogElysiumDamage, Verbose,
						TEXT("Dmg '%s': '%s' names no recovered damage bit — carried as no bit"),
						*Authored, *Token);
				}
				continue;
			}
			if (!bHaveBase && IsIntegerToken(Token))
			{
				Out.BaseDamage = FCString::Atoi(*Token);
				bHaveBase = true;
				continue;
			}
			EElysiumDmgFamily Family = EElysiumDmgFamily::None;
			if (Out.Family == EElysiumDmgFamily::None && ParseFamilyWord(Token, Family))
			{
				Out.Family = Family;
				continue;
			}
			// A word before the base integer is the optional leading `CVStatRef`; a word after the
			// family is the attack feat/reference. Both keep the FIRST spelling seen, because the
			// grammar authors at most one of each.
			if (!bHaveBase)
			{
				if (Out.SourceTrait.IsEmpty())
				{
					Out.SourceTrait = Token;
					continue;
				}
			}
			else if (Out.AttackFeat.IsEmpty())
			{
				Out.AttackFeat = Token;
				continue;
			}
			UE_LOG(LogElysiumDamage, Warning,
				TEXT("Dmg '%s': token '%s' does not fit the authored grammar — ignored"),
				*Authored, *Token);
		}

		if (Out.Family == EElysiumDmgFamily::None || !bHaveBase)
		{
			UE_LOG(LogElysiumDamage, Warning,
				TEXT("Dmg '%s' is incomplete (base %s, family %s) — it will apply no damage"),
				*Authored, bHaveBase ? TEXT("yes") : TEXT("missing"),
				ElysiumDmgFamilyName(Out.Family));
		}
		return Out;
	}

	const TCHAR* SoakFeatName(EElysiumDmgFamily Family, bool bKindred, bool bFalling)
	{
		switch (Family)
		{
		case EElysiumDmgFamily::Bashing:
			return bKindred ? TEXT("Soak_vs_Bashing_Kindred") : TEXT("Soak_vs_Bashing");
		case EElysiumDmgFamily::Lethal:
			// The raw selector adds one for flag 0x20; the falling rows are the observed intended
			// use of that, and no other family/flag pair is claimed to be meaningful.
			if (bFalling)
			{
				return bKindred ? TEXT("Soak_vs_Lethal_Falling_Kindred") : TEXT("Soak_vs_Lethal_Falling");
			}
			return bKindred ? TEXT("Soak_vs_Lethal_Kindred") : TEXT("Soak_vs_Lethal");
		case EElysiumDmgFamily::Aggravated:
			return bKindred ? TEXT("Soak_vs_Aggravated_Kindred") : TEXT("Soak_vs_Aggravated");
		default:
			return nullptr;
		}
	}

	bool IsKindred(const FElysiumCombatCharacter& Char)
	{
		return Char.IsKindred();
	}

	// The soak half of step 7: the forced value, or the selected feat rolled at the PC/NPC target
	// number. Returns `max(successes - botches, 0)`; automatic soak is added by the caller.
	static int32 ResolveSoakRoll(const FElysiumDmg& Dmg, FElysiumCombatCharacter& Victim,
		const FElysiumDamageContext& Context, bool bKindred)
	{
		if (Dmg.ForcedSoak >= 0)
		{
			return Dmg.ForcedSoak;
		}
		const TCHAR* FeatName = SoakFeatName(Dmg.Family, bKindred, Dmg.IsFalling());
		if (FeatName == nullptr)
		{
			return 0;   // family None never reaches here — Apply rejected it
		}
		const FElysiumFeat* Feat = Context.Feats ? Context.Feats->Find(FeatName) : nullptr;
		if (!Feat)
		{
			UE_LOG(LogElysiumDamage, Warning,
				TEXT("%s soak: feat '%s' does not resolve (rulebook %s) — soaking nothing"),
				*Victim.DebugString(), FeatName,
				Context.Feats ? TEXT("loaded") : TEXT("absent"));
			return 0;
		}
		if (!Context.HasSoakDifficulties())
		{
			UE_LOG(LogElysiumDamage, Warning,
				TEXT("%s soak: rules.txt Damage_Info soak difficulties are unavailable — "
					"'%s' is not rolled"),
				*Victim.DebugString(), FeatName);
			return 0;
		}
		const bool bPlayer = IsPlayerSide(Victim);
		const int32 Difficulty = bPlayer ? Context.SoakDifficultyPc : Context.SoakDifficultyNpc;
		const int32 Pool = ElysiumFeats::FeatValue(*Feat, Victim.Sheet, Victim.SheetEffects());
		const FElysiumDiceTable& Weighting = Context.DiceTables
			? Context.DiceTables->ForFeat(*Feat, !bPlayer)
			: FElysiumDiceTable::Uniform();
		const FElysiumRollResult Roll = ElysiumDice::Roll(Pool, Difficulty, Weighting);
		return FMath::Max(Roll.Successes - Roll.Botches, 0);
	}

	bool Apply(FElysiumDmg& Dmg, FElysiumCombatCharacter* Attacker, FElysiumCombatCharacter& Victim,
		const FElysiumDamageContext& Context, bool bDisallowFirearmsToBashing)
	{
		// 1. A descriptor with no family is rejected outright.
		if (Dmg.Family == EElysiumDmgFamily::None)
		{
			UE_LOG(LogElysiumDamage, Warning,
				TEXT("%s received a damage descriptor with no family — rejected (%s)"),
				*Victim.DebugString(), *Dmg.Describe());
			Dmg.AppliedDamage = -1;
			Dmg.bResolved = false;
			return false;
		}

		const bool bKindred = IsKindred(Victim);

		// 2. A Kindred victim takes firearm LETHAL as bashing, unless the attacker's active weapon
		//    record forbids the conversion. That record read arrives with the weapon classes; until
		//    then every caller passes the authored default (false).
		if (bKindred && Dmg.Family == EElysiumDmgFamily::Lethal && Dmg.IsFirearm()
			&& !bDisallowFirearmsToBashing)
		{
			Dmg.Family = EElysiumDmgFamily::Bashing;
		}

		// 3/4. The damage pool, or the direct-damage input.
		int32 Damage = 0;
		EElysiumRollTier Tier = EElysiumRollTier::Failure;
		if (Dmg.IsDirectInput())
		{
			// Flag 0x8 bypasses the damage ROLL and nothing else: the soak test below still runs.
			Damage = Dmg.ExtraInput;
		}
		else
		{
			const int32 Pool = Dmg.BaseDamage + FMath::Max(Dmg.ExtraInput - 1, 0);
			const FElysiumDiceTable& Weighting = Context.DiceTables
				? Context.DiceTables->Find(DamageRollWeighting)
				: FElysiumDiceTable::Uniform();
			const FElysiumRollResult Roll = ElysiumDice::Roll(Pool, DamageRollDifficulty, Weighting);
			Tier = Roll.Tier;
			Damage = Roll.Net;
			// The descriptor's source trait being Strength adds the attacker's automatic Strength
			// successes AFTER the roll rather than seeding the roll with them.
			if (Attacker && Dmg.SourceTrait.Equals(TEXT("Strength"), ESearchCase::IgnoreCase))
			{
				Damage += CurrentTrait(*Attacker, TEXT("Automatic_Str_Successes"));
			}
		}

		// 5. The non-botch floor: a nonnegative net of zero still lands one point of damage as long
		//    as the roll's own result discriminator is nonzero — which is every tier but Botched,
		//    whose enum value is 0. A direct input has no roll and therefore no floor.
		Damage = FMath::Max(Damage, 0);
		if (!Dmg.IsDirectInput() && Damage == 0 && Tier != EElysiumRollTier::Botched)
		{
			Damage = 1;
		}
		Dmg.RolledSuccesses = Damage;

		// 6. The defender's automatic soak.
		const int32 AutomaticSoak = CurrentTrait(Victim, TEXT("Automatic_Soak_Successes"));

		// 7. Faith, sunlight, superclaw and burn take no soak at all — neither the roll nor the
		//    automatic successes.
		int32 Soak = 0;
		if (!Dmg.TakesNoSoak())
		{
			Soak = ResolveSoakRoll(Dmg, Victim, Context, bKindred) + AutomaticSoak;
			// Soak never turns a hit into healing: it is capped to the damage it is resisting.
			Soak = FMath::Clamp(Soak, 0, Damage);
		}
		Dmg.SoakSuccesses = Soak;

		// 8. What is left.
		Dmg.Remainder = FMath::Max(Damage - Soak, 0);

		// 9. OPEN JOIN — the template damage filters and descriptor word 15.
		//    The victim's authored `DamageFilter*` values are accumulated here because the data
		//    supports it, and are deliberately NOT multiplied into the result: retail's point of
		//    commit for word 15 is unrecovered (`combat-and-damage.md` § open research gaps), and a
		//    guessed multiply would silently rescale every hit in the game. Closing the RE lands as
		//    one operation on `Remainder` right here, not as a redesign.
		Dmg.FilterAccumulator = AccumulateTemplateFilters(Dmg, Victim);
		if (Dmg.FilterAccumulator != 0.0f)
		{
			static TSet<FString> ReportedClasses;
			const FString Classname(VictimClassname(Victim));
			if (!ReportedClasses.Contains(Classname))
			{
				ReportedClasses.Add(Classname);
				UE_LOG(LogElysiumDamage, Warning,
					TEXT("'%s' authors damage filters (%s accumulator %.3f) that are populated but "
						"not applied — the word-15 commit point is an open RE join"),
					*Classname, ElysiumDmgFamilyName(Dmg.Family), Dmg.FilterAccumulator);
			}
		}

		// 10. The result.
		Dmg.AppliedDamage = Dmg.Remainder;
		Dmg.bResolved = true;
		UE_LOG(LogElysiumDamage, Verbose, TEXT("%s <- %s"), *Victim.DebugString(), *Dmg.Describe());
		return true;
	}
}
