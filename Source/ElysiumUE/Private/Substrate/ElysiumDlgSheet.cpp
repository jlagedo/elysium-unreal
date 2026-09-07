#include "Substrate/ElysiumDlgSheet.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumDlgSheet, Log, All);

namespace ElysiumDlgSheetImpl
{
	EElysiumDlgTraitClass ClassFromContainer(EElysiumTraitContainer Container, bool& bOutSupported)
	{
		bOutSupported = true;
		switch (Container)
		{
		case EElysiumTraitContainer::Attributes:  return EElysiumDlgTraitClass::Attribute;
		case EElysiumTraitContainer::Abilities:   return EElysiumDlgTraitClass::Ability;
		case EElysiumTraitContainer::Disciplines: return EElysiumDlgTraitClass::Discipline;
		default:
			// `CVStatRef` class 3 (Active_Disciplines). `TestSimple` has no arm for it and falls to
			// its `Unhandled dialog dependency` DevMsg, so the port refuses the name here and lets
			// the half be Python — the same false. No shipped condition names one.
			bOutSupported = false;
			return EElysiumDlgTraitClass::Unknown;
		}
	}

	class FRulebookTraitResolver final : public IElysiumDlgTraitResolver
	{
	public:
		explicit FRulebookTraitResolver(UElysiumRulebookSubsystem* InRules) : Rules(InRules) {}

		virtual bool ResolveTrait(const FString& Name, EElysiumDlgTraitClass& OutClass,
			int32& OutId) const override
		{
			EElysiumTraitContainer Container = EElysiumTraitContainer::Attributes;
			int32 Slot = INDEX_NONE;
			if (ElysiumFindSheetSlot(*Name, Container, Slot))
			{
				bool bSupported = false;
				const EElysiumDlgTraitClass Class = ClassFromContainer(Container, bSupported);
				if (!bSupported)
				{
					UE_LOG(LogElysiumDlgSheet, Warning,
						TEXT("dialogue dependency names '%s', an %s slot; retail's TestSimple has no arm "
							 "for that class and the check reads false"), *Name,
						ElysiumTraitContainerName(Container));
					return false;
				}
				OutClass = Class;
				OutId = Slot;
				return true;
			}

			// Feats come last, which is why `Persuasion` (an ability AND a feat name) resolves as
			// the ABILITY in a dialogue check and only `Intimidate`/`Seduction`/`Haggle`/`Research`
			// reach the feat table.
			const FElysiumFeatTable* Feats =
				Rules ? &Rules->Feats() : ElysiumSheetRules::BoundTables().Feats;
			if (Feats != nullptr)
			{
				if (const FElysiumFeat* Feat = Feats->Find(Name))
				{
					OutClass = EElysiumDlgTraitClass::Feat;
					OutId = Feat->Index;
					return true;
				}
			}
			return false;
		}

		virtual FString DisciplineName(int32 Id) const override
		{
			const FElysiumStatTable* Stats =
				Rules ? &Rules->Stats() : ElysiumSheetRules::BoundTables().Stats;
			if (Stats == nullptr)
			{
				return FString();
			}
			const FElysiumStat* Stat =
				Stats->Container(EElysiumTraitContainer::Disciplines).At(Id);
			return Stat ? Stat->InternalName : FString();
		}

	private:
		UElysiumRulebookSubsystem* Rules = nullptr;
	};

	class FWorldPlayerSheet final : public IElysiumDlgSheet
	{
	public:
		FWorldPlayerSheet(FElysiumEntityWorld& InWorld, const FElysiumEntityHandle& InPlayer,
			const FElysiumEntityHandle& InNpc)
			: World(&InWorld), PlayerHandle(InPlayer), NpcHandle(InNpc)
		{
		}

		virtual int32 CalcFeat(const FString& FeatName) const override
		{
			const FElysiumCombatCharacter* C = Char();
			return C ? C->CalcFeat(FeatName) : 0;
		}

		virtual int32 Stat(EElysiumDlgTraitClass Class, int32 Id) const override
		{
			const EElysiumTraitContainer Container = Class == EElysiumDlgTraitClass::Ability
				? EElysiumTraitContainer::Abilities : EElysiumTraitContainer::Attributes;
			const FElysiumCombatCharacter* C = Char();
			return C ? C->Sheet.GetCurrent(Container, Id) : 0;
		}

		virtual int32 Discipline(int32 Id) const override
		{
			const FElysiumCombatCharacter* C = Char();
			return C ? C->Sheet.GetCurrent(EElysiumTraitContainer::Disciplines, Id) : 0;
		}

		virtual int32 BloodPool() const override
		{
			const FElysiumCombatCharacter* C = Char();
			return C ? C->BloodPoolValue() : 0;
		}

		virtual bool IsMale() const override
		{
			const FElysiumCombatCharacter* C = Char();
			return C ? C->Sheet.IsMale() : true;
		}

		virtual int32 ClanOffset() const override
		{
			const FElysiumCombatCharacter* C = Char();
			return C ? ElysiumDlgClan::OffsetFromSheetClan(C->Sheet.Clan()) : ElysiumDlgClan::None;
		}

		virtual void SpendBlood(int32 Points) override
		{
			if (FElysiumCombatCharacter* C = MutableChar())
			{
				C->AddBlood(-Points);
			}
		}

		virtual void AddFakedDisciplineEffect(const FString& Trait, int32 DisciplineId,
			int32 Level) override
		{
			FElysiumEntity* NpcEntity = World ? World->Resolve(NpcHandle) : nullptr;
			FElysiumCombatCharacter* Target = NpcEntity ? NpcEntity->AsCombatCharacter() : nullptr;
			if (Target == nullptr)
			{
				return;   // the partner went away; retail's null receiver does nothing either
			}
			ElysiumDlgCharge::AddFakedDisciplineEffect(*Target, Trait, DisciplineId, Level);
		}

	private:
		const FElysiumCombatCharacter* Char() const
		{
			FElysiumEntity* Entity = World ? World->Resolve(PlayerHandle) : nullptr;
			return Entity ? Entity->AsCombatCharacter() : nullptr;
		}
		FElysiumCombatCharacter* MutableChar()
		{
			FElysiumEntity* Entity = World ? World->Resolve(PlayerHandle) : nullptr;
			return Entity ? Entity->AsCombatCharacter() : nullptr;
		}

		FElysiumEntityWorld* World = nullptr;
		FElysiumEntityHandle PlayerHandle;
		FElysiumEntityHandle NpcHandle;
	};
}

TSharedRef<const IElysiumDlgTraitResolver> ElysiumDlgSheet::MakeTraitResolver(
	UElysiumRulebookSubsystem* Rules)
{
	return MakeShared<ElysiumDlgSheetImpl::FRulebookTraitResolver>(Rules);
}

TSharedRef<IElysiumDlgSheet> ElysiumDlgSheet::MakePlayerSheet(FElysiumEntityWorld& World,
	const FElysiumEntityHandle& Player, const FElysiumEntityHandle& Npc)
{
	return MakeShared<ElysiumDlgSheetImpl::FWorldPlayerSheet>(World, Player, Npc);
}

void ElysiumDlgCharge::AddFakedDisciplineEffect(FElysiumCombatCharacter& Target,
	const FString& Trait, int32 DisciplineId, int32 Level)
{
	// The transaction is recorded even though nothing plays it, so a live run shows the charge and
	// its target rather than a silent blood loss.
	UE_LOG(LogElysiumDlgSheet, Log,
		TEXT("%s takes a faked %s (id %d) effect at level %d from a dialogue line "
			 "(TODO(dialogue-plan): dialog_domination_emitter / dialog_presence_emitter)"),
		*Target.DebugString(), *Trait, DisciplineId, Level);
}
