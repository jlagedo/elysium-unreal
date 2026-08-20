#include "Debug/ElysiumCogWindow_Npc.h"

#if ENABLE_COG

#include "Debug/ElysiumArenaCast.h"
#include "Debug/ElysiumCogLocomotionRow.h"
#include "Debug/ElysiumCogStyle.h"
#include "Debug/ElysiumGreenRoomRun.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumCameraSolve.h"   // CameraClass — what a drawn weapon is doing to the view
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumNpcSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumPlayerBody.h"
#include "Substrate/ElysiumAnimEvents.h"    // the unclaimed sequence-event census
#include "Substrate/ElysiumItemClasses.h"   // Inventory.Active() is read for its classname
#include "Substrate/ElysiumItemTable.h"   // FElysiumItemDef — the drawn weapon's own policy record
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumSchedule.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumFacialRig.h"
#include "Visual/ElysiumNpcBody.h"

#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"
#include "imgui.h"

namespace
{
	// The relationship values a spawn form offers, in the order a combat test wants them. Hate is
	// first because it is the one that makes a character select a combat schedule at all.
	struct FReactionOption
	{
		const TCHAR* Label;
		EElysiumRelationship Value;
	};
	const FReactionOption ReactionOptions[] = {
		{ TEXT("D_HT  hate"),    EElysiumRelationship::Hate },
		{ TEXT("D_NU  neutral"), EElysiumRelationship::Neutral },
		{ TEXT("D_FR  fear"),    EElysiumRelationship::Fear },
		{ TEXT("D_LI  like"),    EElysiumRelationship::Like },
	};
	constexpr int32 NumReactionOptions = UE_ARRAY_COUNT(ReactionOptions);

	// The registered `npc_*` leaves worth standing in a fight, in the order a picker should offer
	// them. Not the whole registered set: `npc_VCamera` has no model and `npc_VPlayerController` is
	// the scene-owned duplicate with no AI at all, so offering either would be offering a character
	// that cannot participate.
	const TCHAR* const CombatClasses[] = {
		TEXT("npc_VHumanCombatant"),
		TEXT("npc_VHuman"),
		TEXT("npc_VCop"),
		TEXT("npc_VVampire"),
		TEXT("npc_VHunter"),
		TEXT("npc_VSabbatLeader"),
		TEXT("npc_VPedestrian"),
		TEXT("npc_VAnimal"),
		TEXT("npc_VRat"),
	};
	constexpr int32 NumCombatClasses = UE_ARRAY_COUNT(CombatClasses);

	// Every condition identity the registry names, in the order the enum declares them. Written out
	// rather than iterated, because the values are retail's own registered numbers with deliberate
	// gaps — there is no range to walk, and inventing one would print identities that do not exist.
	const EElysiumNpcCond AllConditions[] = {
		EElysiumNpcCond::ShouldDodge, EElysiumNpcCond::ShouldBlock,
		EElysiumNpcCond::ShouldStepback, EElysiumNpcCond::ShouldKick,
		EElysiumNpcCond::CriminalFleeLevel, EElysiumNpcCond::CriminalAttackLevel,
		EElysiumNpcCond::SupernaturalFleeLevel, EElysiumNpcCond::SupernaturalAttackLevel,
		EElysiumNpcCond::Knockback, EElysiumNpcCond::WaitingAttackTime,
		EElysiumNpcCond::HitByDoor, EElysiumNpcCond::WeaponThroughWall,
		EElysiumNpcCond::NoPrimaryAmmo, EElysiumNpcCond::SeeHate, EElysiumNpcCond::SeeDislike,
		EElysiumNpcCond::LostEnemy, EElysiumNpcCond::EnemyOccluded, EElysiumNpcCond::HaveEnemyLos,
		EElysiumNpcCond::LightDamage, EElysiumNpcCond::HeavyDamage, EElysiumNpcCond::RepeatedDamage,
		EElysiumNpcCond::CanRangeAttack1, EElysiumNpcCond::CanRangeAttack2,
		EElysiumNpcCond::CanMeleeAttack1, EElysiumNpcCond::CanMeleeAttack2,
		EElysiumNpcCond::NewEnemy, EElysiumNpcCond::EnemyDead, EElysiumNpcCond::EnemyUnreachable,
		EElysiumNpcCond::SeeNemesis, EElysiumNpcCond::TooCloseToAttack,
		EElysiumNpcCond::TooFarToAttack, EElysiumNpcCond::WeaponBlockedByFriend,
		EElysiumNpcCond::WeaponSightOccluded,
		EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::SeeFear, EElysiumNpcCond::HearCombat,
		EElysiumNpcCond::HearPlayer, EElysiumNpcCond::HearWorld, EElysiumNpcCond::HearDanger,
		EElysiumNpcCond::InvestigateLevel,
	};
	constexpr int32 NumConditions = UE_ARRAY_COUNT(AllConditions);

	// A time held as an absolute substrate stamp, rendered as an age. `-1` is the "never" sentinel
	// every memory slot uses, and printing it as a huge negative age would read as a bug.
	FString AgeOf(double Stamp, double Now)
	{
		return Stamp < 0.0 ? FString(TEXT("never"))
			: FString::Printf(TEXT("%.1fs ago"), FMath::Max(0.0, Now - Stamp));
	}

	// A handle rendered as the thing it points at. It deliberately reports a DEAD entity by name
	// rather than as nothing: `ENEMY_DEAD` and `LOST_ENEMY` are different conditions with different
	// consequences, and a panel that collapsed the two would hide the distinction the cognition
	// layer exists to keep.
	FString NameOf(const FElysiumEntityWorld& World, const FElysiumEntityHandle& Handle)
	{
		if (!Handle.IsSet())
		{
			return TEXT("(none)");
		}
		const FElysiumEntity* Entity = ElysiumNpcCond::ResolveEnemyHandle(World, Handle);
		if (Entity == nullptr)
		{
			return TEXT("(gone)");
		}
		FString Name = Entity->TargetName.IsEmpty()
			? FString::Printf(TEXT("#%d"), Handle.Index) : Entity->TargetName;
		if (Entity->IsInert())
		{
			Name += TEXT(" (dead)");
		}
		return Name;
	}

	void Row(const TCHAR* Label, const FString& Value, const ImVec4* Colour = nullptr)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextColored(ElysiumCogStyle::ColDim, "%s", COG_TCHAR_TO_CHAR(Label));
		ImGui::TableNextColumn();
		if (Colour != nullptr)
		{
			ImGui::TextColored(*Colour, "%s", COG_TCHAR_TO_CHAR(*Value));
		}
		else
		{
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Value));
		}
	}

	bool BeginFacts(const char* Id)
	{
		return ImGui::BeginTable(Id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg);
	}

	const ImVec4& StateColour(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Combat: return ElysiumCogStyle::ColError;
		case EElysiumNpcState::Alert:  return ElysiumCogStyle::ColWarn;
		case EElysiumNpcState::Dead:   return ElysiumCogStyle::ColInert;
		default:                       return ElysiumCogStyle::ColDim;
		}
	}
}

void FElysiumCogWindow_Npc::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_Npc::RenderHelp()
{
	ImGui::Text(
		"The cast: who is standing, what they are thinking, and how to put more of them there.\n\n"
		"Cast stands characters up through the same runtime-spawn door npc_maker uses -- a body "
		"stem off the /ElysiumBaked mount, an npctemplate stat block, a weapon resolved through "
		"additionalequipment, and a player_reaction that decides whether they fight. It also arms "
		"the player with every controllable weapon in vdata/items and a reserve of every ammo type "
		"any of them names.\n\n"
		"The other tabs read one link each of the decision chain, live: Mind (admission, state vs "
		"ideal state, the body-owner arbiter, the transition trace), Senses (perception tuning and "
		"everything remembered), Conditions (this pass's gathered bits beside the running "
		"schedule's interrupt mask -- the pairing that decides whether a program is re-selected), "
		"Schedule (the task program and where in it the NPC is), Combat (health, the weapon "
		"capability split, relationships, the last damage packet).\n\n"
		"Nothing outside Cast writes. A character that will not fight is a finding; a panel that "
		"could make it fight would have destroyed the finding.\n\n"
		"It is not gated on the green room -- it reads the entity world, so it works in the arena "
		"and in a loaded map alike. The arena's spawn pads only appear when an arena is standing.");
}

FElysiumNpc* FElysiumCogWindow_Npc::SelectedNpc() const
{
	FElysiumEntityWorld* World = GetEntityWorld();
	FElysiumEntity* Entity = World ? World->Resolve(GetSelection()) : nullptr;
	return Entity ? Entity->AsNpc() : nullptr;
}

void FElysiumCogWindow_Npc::GatherNpcs(TArray<FElysiumNpc*>& Out) const
{
	Out.Reset();
	FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		return;
	}
	for (const TUniquePtr<FElysiumEntity>& Entity : World->Entities())
	{
		if (Entity)
		{
			if (FElysiumNpc* Npc = Entity->AsNpc())
			{
				Out.Add(Npc);
			}
		}
	}
}

// ================================================================================================
// The cast — the one tab that writes
// ================================================================================================

void FElysiumCogWindow_Npc::RenderSpawnControls(FElysiumEntityWorld& World)
{
	UElysiumGameStateSubsystem* GameState = GetGameState();
	if (bCatalogsDirty)
	{
		Stems = ElysiumArenaCast::BodyStems();
		Templates = ElysiumArenaCast::StatTemplates(GameState);
		Weapons.Reset();
		WeaponLabels.Reset();
		for (const ElysiumArenaCast::FItemOption& Option : ElysiumArenaCast::WeaponCatalog(GameState))
		{
			if (Option.bAmmo)
			{
				continue;   // stocked as a reserve, never carried as a spawn's weapon
			}
			Weapons.Add(Option.Classname);
			WeaponLabels.Add(FString::Printf(TEXT("%s  [%s]"),
				Option.PrintName.IsEmpty() ? *Option.Classname : *Option.PrintName,
				*Option.TypeName));
		}
		bCatalogsDirty = false;
		if (PendingStem.IsEmpty() && !Stems.IsEmpty())
		{
			PendingStem = Stems[0];
		}
		if (PendingTemplate.IsEmpty() && !Templates.IsEmpty())
		{
			PendingTemplate = Templates[0];
		}
	}

	ImGui::SeparatorText("Stand a character up");
	if (Stems.IsEmpty())
	{
		ImGui::TextColored(ElysiumCogStyle::ColError,
			"No baked body on the /ElysiumBaked mount. There is no second build of a character:");
		ImGui::TextDisabled("  uv run elysium export characters <stem>");
		return;
	}

	const float Third = FMath::Max(GetDpiScale() * 120.0f,
		(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f);

	// --- body ---------------------------------------------------------------------------------
	ImGui::TextDisabled("body");
	ImGui::SetNextItemWidth(Third);
	FCogWidgets::InputTextWithHint("##StemFilter", "(filter bodies)", StemFilter);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(Third);
	if (ImGui::BeginCombo("##Stem", COG_TCHAR_TO_CHAR(*PendingStem)))
	{
		for (const FString& Stem : Stems)
		{
			if (!StemFilter.IsEmpty() && !Stem.Contains(StemFilter))
			{
				continue;
			}
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Stem), Stem == PendingStem))
			{
				PendingStem = Stem;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Rescan"))
	{
		bCatalogsDirty = true;
	}

	// --- stat template ------------------------------------------------------------------------
	// Named, not optional-by-omission: a character with no `stattemplate` seeds a zeroed sheet, and
	// the damage path is fail-closed against one. That is the difference between an opponent that
	// can be killed and one that silently absorbs everything.
	ImGui::TextDisabled("stat template  (the health ceiling, soak and Kindred classification)");
	ImGui::SetNextItemWidth(Third);
	FCogWidgets::InputTextWithHint("##TemplateFilter", "(filter templates)", TemplateFilter);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(Third);
	if (ImGui::BeginCombo("##Template", COG_TCHAR_TO_CHAR(
		PendingTemplate.IsEmpty() ? TEXT("(none — zeroed sheet)") : *PendingTemplate)))
	{
		if (ImGui::Selectable("(none — zeroed sheet)", PendingTemplate.IsEmpty()))
		{
			PendingTemplate.Reset();
		}
		for (const FString& Template : Templates)
		{
			if (!TemplateFilter.IsEmpty() && !Template.Contains(TemplateFilter))
			{
				continue;
			}
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Template), Template == PendingTemplate))
			{
				PendingTemplate = Template;
			}
		}
		ImGui::EndCombo();
	}
	if (Templates.IsEmpty())
	{
		ImGui::SameLine();
		ImGui::TextColored(ElysiumCogStyle::ColError, "no rulebook");
	}

	// --- class and weapon -----------------------------------------------------------------------
	ImGui::TextDisabled("class and weapon");
	ImGui::SetNextItemWidth(Third);
	if (ImGui::BeginCombo("##Class", COG_TCHAR_TO_CHAR(*PendingClass)))
	{
		for (int32 Index = 0; Index < NumCombatClasses; ++Index)
		{
			const FString Candidate = CombatClasses[Index];
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Candidate), Candidate == PendingClass))
			{
				PendingClass = Candidate;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(Third);
	if (ImGui::BeginCombo("##Weapon", COG_TCHAR_TO_CHAR(
		PendingWeapon.IsEmpty() ? TEXT("(unarmed)") : *PendingWeapon)))
	{
		// Unarmed is a real, distinct case rather than a missing choice: it takes the MELEE selector
		// with bare-hands reach, and its attack tasks then fail by name because there is no weapon
		// controller to press. Worth being able to pick deliberately.
		if (ImGui::Selectable("(unarmed)", PendingWeapon.IsEmpty()))
		{
			PendingWeapon.Reset();
		}
		for (int32 Index = 0; Index < Weapons.Num(); ++Index)
		{
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*WeaponLabels[Index]),
				Weapons[Index] == PendingWeapon))
			{
				PendingWeapon = Weapons[Index];
			}
		}
		ImGui::EndCombo();
	}

	// --- reaction and placement -------------------------------------------------------------------
	ImGui::TextDisabled("reaction to the player, and where");
	ImGui::SetNextItemWidth(Third);
	if (ImGui::BeginCombo("##Reaction",
		COG_TCHAR_TO_CHAR(ReactionOptions[FMath::Clamp(PendingReaction, 0, NumReactionOptions - 1)].Label)))
	{
		for (int32 Index = 0; Index < NumReactionOptions; ++Index)
		{
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(ReactionOptions[Index].Label),
				Index == PendingReaction))
			{
				PendingReaction = Index;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(GetDpiScale() * 70.0f);
	ImGui::DragInt("prio", &PendingPriority, 1.0f, 0, 100);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(GetDpiScale() * 60.0f);
	ImGui::DragInt("count", &PendingCount, 0.2f, 1, 8);

	// The pads are the arena's, when one is standing. Outside it there is exactly one sensible
	// destination and it is where the player is looking — a spawn that lands somewhere unreachable
	// in a real map is worse than no pad list at all.
	FElysiumGreenRoomRun* Lab = nullptr;
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		Lab = Maps->GetGreenRoom();
	}
	const bool bArena = Lab != nullptr && Lab->IsArena();
	TArray<FString> PadNames;
	PadNames.Add(TEXT("in front of me (6 m)"));
	if (bArena)
	{
		for (const ElysiumArena::FPad& Pad : Lab->ArenaSpec().Pads)
		{
			PadNames.Add(Pad.Name.ToString());
		}
	}
	PendingPad = FMath::Clamp(PendingPad, 0, PadNames.Num() - 1);
	ImGui::SetNextItemWidth(Third);
	if (ImGui::BeginCombo("##Pad", COG_TCHAR_TO_CHAR(*PadNames[PendingPad])))
	{
		for (int32 Index = 0; Index < PadNames.Num(); ++Index)
		{
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*PadNames[Index]), Index == PendingPad))
			{
				PendingPad = Index;
			}
		}
		ImGui::EndCombo();
	}

	// --- the navigation gate ----------------------------------------------------------------------
	// Stated before the button rather than after the spawn. A character standing on a room with no
	// Recast graph looks exactly like a character with broken AI: it acquires an enemy, selects a
	// chase, and every `TASK_GET_PATH_TO_ENEMY` fails by name. Saying so up front is the difference
	// between a two-minute puzzle and a two-hour one.
	if (bArena && !Lab->IsArenaNavigationReady())
	{
		ImGui::TextColored(ElysiumCogStyle::ColWarn,
			"Recast is still building over the arena — a character spawned now cannot path yet.");
	}

	ImGui::BeginDisabled(PendingStem.IsEmpty());
	if (ImGui::Button("Spawn"))
	{
		LastError.Reset();
		LastNotice.Reset();
		ElysiumArenaCast::FSpawnRequest Request;
		Request.Classname = PendingClass;
		Request.Model = PendingStem;
		Request.StatTemplate = PendingTemplate;
		Request.Weapon = PendingWeapon;
		Request.PlayerReaction = ReactionOptions[PendingReaction].Value;
		Request.PlayerReactionPriority = PendingPriority;

		FVector Feet = FVector::ZeroVector;
		float Yaw = 0.0f;
		bool bPlaced = false;
		if (PendingPad > 0 && bArena)
		{
			bPlaced = Lab->ArenaPadOrigin(
				FName(*PadNames[PendingPad]), Feet, Yaw);
		}
		if (!bPlaced)
		{
			// In front of the camera, on the ground plane the player is standing on. Deliberately
			// flat rather than along the view ray: a look aimed at the floor would otherwise bury a
			// character in it, and a look at the ceiling would drop one from the sky.
			const UWorld* World3D = GetWorld();
			const APlayerController* PC = World3D ? World3D->GetFirstPlayerController() : nullptr;
			const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (Pawn == nullptr)
			{
				LastError = TEXT("no player pawn to spawn in front of");
			}
			else
			{
				const IElysiumPlayerBody* Body = Cast<const IElysiumPlayerBody>(Pawn);
				const float HalfHeight = Body ? Body->GetBodyHalfHeight() : 0.0f;
				const FRotator Look(0.0f, PC->GetControlRotation().Yaw, 0.0f);
				Feet = Pawn->GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight)
					+ Look.RotateVector(FVector(600.0f, 0.0f, 0.0f));
				Yaw = Look.Yaw + 180.0f;   // facing back at the player
				bPlaced = true;
			}
		}

		if (bPlaced)
		{
			int32 Made = 0;
			for (int32 Index = 0; Index < FMath::Clamp(PendingCount, 1, 8); ++Index)
			{
				// Spread a group along the pad's own right, so a count of four is a line abreast
				// rather than four characters interpenetrating at one point.
				const float Offset = (Index - (FMath::Clamp(PendingCount, 1, 8) - 1) * 0.5f) * 90.0f;
				Request.Origin = Feet + FRotator(0.0f, Yaw, 0.0f).RotateVector(
					FVector(0.0f, Offset, 0.0f));
				Request.Yaw = Yaw;
				FString Error;
				if (ElysiumArenaCast::Spawn(World, Request, Error).IsSet())
				{
					++Made;
				}
				else if (LastError.IsEmpty())
				{
					LastError = Error;
				}
			}
			if (Made > 0)
			{
				LastNotice = FString::Printf(TEXT("stood %d × %s"), Made, *PendingStem);
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Clear spawned"))
	{
		const int32 Cleared = ElysiumArenaCast::ClearSpawned(World);
		LastNotice = FString::Printf(TEXT("killed %d spawned character(s)"), Cleared);
		LastError.Reset();
	}
}

void FElysiumCogWindow_Npc::RenderPlayerLoadout(FElysiumEntityWorld& World)
{
	ImGui::SeparatorText("The player");
	FElysiumPlayer* Player = World.FindPlayer();
	if (Player == nullptr)
	{
		ImGui::TextColored(ElysiumCogStyle::ColError, "No player entity in this world.");
		return;
	}

	// --- the character preset ---------------------------------------------------------------------
	// A stage world's player record is whatever the game instance happened to be carrying, which for
	// a cold `gr --arena` is a zeroed sheet: no clan, no soak, no derived health block, and a damage
	// path that is fail-closed against it. Seeding one is therefore not a convenience — without it
	// the player is not a combatant and nothing in a fight resolves.
	if (Clans.IsEmpty())
	{
		Clans = ElysiumArenaCast::PlayableClans(GetGameState());
	}
	if (!Clans.IsEmpty())
	{
		const int32 Index = FMath::Clamp(PendingClan, 0, Clans.Num() - 1);
		ImGui::SetNextItemWidth(GetDpiScale() * 140.0f);
		if (ImGui::BeginCombo("##Clan", COG_TCHAR_TO_CHAR(*Clans[Index].Name)))
		{
			for (int32 i = 0; i < Clans.Num(); ++i)
			{
				if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Clans[i].Name), i == PendingClan))
				{
					PendingClan = i;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::Checkbox("male", &bPendingMale);
		ImGui::SameLine();
		if (ImGui::Button("Seed character"))
		{
			FString Stem;
			LastError.Reset();
			if (ElysiumArenaCast::SeedPlayerCharacter(GetGameState(), Clans[Index].Id,
				bPendingMale, Stem, LastError))
			{
				LastNotice = FString::Printf(TEXT("seeded a baseline %s"), *Clans[Index].Name);
				// The sheet and the body are two halves of one identity: a Nosferatu sheet on a
				// Toreador body is a character nobody asked for. Stand the clan's own body when a
				// lab is driving one — outside the lab the map owns the visual and this is not ours.
				if (!Stem.IsEmpty())
				{
					if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
					{
						if (FElysiumGreenRoomRun* Lab = Maps->GetGreenRoom())
						{
							if (Lab->IsDriving())
							{
								FString BodyError;
								if (!Lab->LabSetBody(Stem, FString(), BodyError))
								{
									LastError = BodyError;
								}
							}
						}
					}
				}
			}
		}
		ImGui::SameLine();
		const FString Stem = bPendingMale ? Clans[Index].MaleStem : Clans[Index].FemaleStem;
		ImGui::TextDisabled("body: %s", COG_TCHAR_TO_CHAR(
			Stem.IsEmpty() ? TEXT("(the export has not covered it)") : *Stem));
		ImGui::TextDisabled("The chargen point pools are left unspent — the character screen spends them.");
	}

	ImGui::SetNextItemWidth(GetDpiScale() * 90.0f);
	ImGui::DragInt("reserve per ammo type", &ReservePerAmmoType, 5.0f, 0, 999);
	if (ImGui::SmallButton("Arm player with arsenal"))
	{
		ElysiumArenaCast::ArmPlayerWithArsenal(World, GetGameState(), ReservePerAmmoType);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("slot1..slot8 select · +attack · +reload · discipline");

	// --- what is actually in the player's hands ---------------------------------------------------
	// Its own section rather than a row in the facts table, because it answers three questions at
	// once and two of them are not obvious from the model: what is drawn, whether it is a weapon the
	// combat path can drive, and — the one that costs an afternoon when it is missing — what its
	// `camera_class` is doing to the view. A melee class is the `0x10` force: third person is not a
	// preference while one is drawn, and `togglecamera` correctly will not move it.
	ImGui::SeparatorText("In hand");
	FElysiumItem* Active = Player->Inventory.Active(*Player);
	const FElysiumItemDef* ActiveData = Active ? Active->Data() : nullptr;
	if (Active == nullptr)
	{
		ImGui::TextColored(ElysiumCogStyle::ColDim, "Nothing drawn — bare hands.");
	}
	else
	{
		ImGui::TextColored(ElysiumCogStyle::ColOk, "%s", COG_TCHAR_TO_CHAR(
			ActiveData && !ActiveData->PrintName.IsEmpty()
				? *ActiveData->PrintName : *Active->ClassName()));
		ImGui::SameLine();
		ImGui::TextDisabled("(%s)", COG_TCHAR_TO_CHAR(*Active->ClassName()));
		if (ActiveData != nullptr)
		{
			const bool bForcesThird = ActiveData->CameraClass == ElysiumCam::CameraClass::ForceThird;
			const bool bForcesFirst = ActiveData->CameraClass == ElysiumCam::CameraClass::ForceFirst;
			ImGui::TextDisabled("%s · camera_class 0x%02x",
				COG_TCHAR_TO_CHAR(ElysiumItemTypeName(ActiveData->Type)), ActiveData->CameraClass);
			if (bForcesThird)
			{
				ImGui::TextColored(ElysiumCogStyle::ColWarn,
					"melee forces THIRD person — togglecamera will not leave it while this is drawn");
			}
			else if (bForcesFirst)
			{
				ImGui::TextColored(ElysiumCogStyle::ColWarn,
					"force_1st: this item forces FIRST person");
			}
		}
	}

	// Draw any carried weapon. `SetActiveWeapon` is the ordinary switch — the same one
	// `Weapon_Equip` and the NPC loadout use — so this is the slot keys' operation, not a shortcut
	// around them.
	{
		TArray<FElysiumItem*> Carried;
		TArray<FString> Labels;
		for (int32 Position = 0; Position < Player->Inventory.Num(); ++Position)
		{
			FElysiumItem* Item = Player->Inventory.At(*Player, Position);
			const FElysiumItemDef* Data = Item ? Item->Data() : nullptr;
			if (Data == nullptr || !Data->IsControllableWeapon())
			{
				continue;
			}
			Carried.Add(Item);
			Labels.Add(FString::Printf(TEXT("%s  [%s]"),
				Data->PrintName.IsEmpty() ? *Item->ClassName() : *Data->PrintName,
				ElysiumItemTypeName(Data->Type)));
		}
		if (Carried.IsEmpty())
		{
			ImGui::TextDisabled("No controllable weapon carried. Arm the player below.");
		}
		else
		{
			ImGui::SetNextItemWidth(GetDpiScale() * 240.0f);
			if (ImGui::BeginCombo("##Draw", "Draw a weapon..."))
			{
				for (int32 Index = 0; Index < Carried.Num(); ++Index)
				{
					if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Labels[Index]), Carried[Index] == Active))
					{
						Player->Inventory.SetActiveWeapon(*Player, *Carried[Index]);
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%d carried", Carried.Num());
		}
	}

	if (BeginFacts("##PlayerFacts"))
	{
		Row(TEXT("health"), FString::Printf(TEXT("%d / %d"), Player->Health, Player->MaxHealth));
		Row(TEXT("carried items"), FString::FromInt(Player->Inventory.Num()));
		ImGui::EndTable();
	}
}

void FElysiumCogWindow_Npc::RenderRoster(FElysiumEntityWorld& World,
	const TArray<FElysiumNpc*>& Npcs)
{
	ImGui::SeparatorText("Standing");
	ImGui::Text("%d character(s)", Npcs.Num());
	if (Npcs.IsEmpty())
	{
		ImGui::TextDisabled("Nothing is standing. Spawn one above, or load a map that authors some.");
		return;
	}

	const ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders
		| ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
	if (!ImGui::BeginTable("##Roster", 7, Flags, ImVec2(0, GetDpiScale() * 200.0f)))
	{
		return;
	}
	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Name");
	ImGui::TableSetupColumn("Body");
	ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 62.0f);
	ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 90.0f);
	ImGui::TableSetupColumn("Owner", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 96.0f);
	ImGui::TableSetupColumn("Schedule");
	ImGui::TableSetupColumn("Enemy");
	ImGui::TableHeadersRow();

	for (FElysiumNpc* Npc : Npcs)
	{
		const FElysiumNpcMind& Mind = Npc->GetMind();
		const bool bSelected = GetSelection() == Npc->Handle;

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		// `RowName`, not `Name`: `FCogWindow` carries its own `Name` member and the window's own
		// title being shadowed by a table cell is a warning this codebase treats as an error.
		const FString RowName = Npc->TargetName.IsEmpty()
			? FString::Printf(TEXT("#%d"), Npc->Handle.Index) : Npc->TargetName;
		ImGui::PushStyleColor(ImGuiCol_Text, Npc->IsInert()
			? ElysiumCogStyle::ColInert
			: (bSelected ? ElysiumCogStyle::ColSelected : ElysiumCogStyle::ColName));
		if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*RowName), bSelected, ImGuiSelectableFlags_SpanAllColumns))
		{
			SetSelection(Npc->Handle);
		}
		ImGui::PopStyleColor();

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Npc->ModelStem()));
		ImGui::TableNextColumn();
		// Zero over zero is the seeded-nothing case, not a corpse: a character with no resolved stat
		// template has no ceiling, and the damage path refuses rather than killing it.
		const bool bNoCeiling = Npc->MaxHealth <= 0;
		ImGui::TextColored(bNoCeiling ? ElysiumCogStyle::ColError : ElysiumCogStyle::ColDim,
			"%d/%d", Npc->Health, Npc->MaxHealth);
		ImGui::TableNextColumn();
		const EElysiumNpcState State = Mind.State();
		ImGui::TextColored(StateColour(State), "%s",
			COG_TCHAR_TO_CHAR(LexToString(State)));
		if (Mind.IdealState() != State)
		{
			ImGui::SameLine();
			ImGui::TextColored(ElysiumCogStyle::ColWarn, "→%s",
				COG_TCHAR_TO_CHAR(LexToString(Mind.IdealState())));
		}
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(LexToString(Mind.Owner())));
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(Npc->Schedule.IsRunning()
			? COG_TCHAR_TO_CHAR(ElysiumScheduleName(Npc->Schedule.Current)) : "—");
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*NameOf(World, Npc->Senses.Memory.Enemy)));
	}
	ImGui::EndTable();
}

void FElysiumCogWindow_Npc::RenderCast(FElysiumEntityWorld& World)
{
	RenderSpawnControls(World);
	if (!LastError.IsEmpty())
	{
		ImGui::TextColored(ElysiumCogStyle::ColError, "%s", COG_TCHAR_TO_CHAR(*LastError));
	}
	if (!LastNotice.IsEmpty())
	{
		ImGui::TextColored(ElysiumCogStyle::ColOk, "%s", COG_TCHAR_TO_CHAR(*LastNotice));
	}
	RenderPlayerLoadout(World);

	ImGui::SeparatorText("Overlay");
	ImGui::Checkbox("Draw over each head", &bDrawOverlay);
	ImGui::SameLine();
	ImGui::BeginDisabled(!bDrawOverlay);
	ImGui::Checkbox("Enemy lines", &bOverlayEnemyLines);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(GetDpiScale() * 110.0f);
	ImGui::DragFloat("range (cm)", &OverlayRangeCm, 25.0f, 200.0f, 20000.0f, "%.0f");
	ImGui::EndDisabled();

	// Gathered AFTER the spawn controls, so a character stood up this frame is in the roster on the
	// frame it appeared rather than the one after.
	TArray<FElysiumNpc*> Npcs;
	GatherNpcs(Npcs);
	RenderRoster(World, Npcs);
}

// ================================================================================================
// The decision chain
// ================================================================================================

void FElysiumCogWindow_Npc::RenderMind(FElysiumNpc& Npc)
{
	const FElysiumNpcMind& Mind = Npc.GetMind();

	if (BeginFacts("##MindFacts"))
	{
		// Admission is first because it gates everything below it. A character stuck at `Armed` has
		// been constructed and has never taken an autonomous decision — which looks identical, from
		// the outside, to one that decided to stand still.
		const TCHAR* Admission =
			Mind.Admission() == FElysiumNpcMind::EAdmission::Admitted ? TEXT("Admitted")
			: Mind.Admission() == FElysiumNpcMind::EAdmission::Armed ? TEXT("Armed (awaiting first think)")
			: TEXT("Spawned (not armed)");
		Row(TEXT("admission"), Admission, Mind.IsAdmitted()
			? &ElysiumCogStyle::ColOk : &ElysiumCogStyle::ColWarn);
		Row(TEXT("state"), LexToString(Mind.State()), &StateColour(Mind.State()));
		// The two disagree while a requested transition has not been committed, which is a real and
		// readable intermediate rather than a glitch.
		Row(TEXT("ideal state"), LexToString(Mind.IdealState()),
			Mind.IdealState() == Mind.State() ? &ElysiumCogStyle::ColDim : &ElysiumCogStyle::ColWarn);
		Row(TEXT("body owner"), FString::Printf(TEXT("%s  (generation %u)"),
			LexToString(Mind.Owner()), Mind.Generation()));
		Row(TEXT("suspended owner"), LexToString(Mind.SuspendedOwner()));
		Row(TEXT("last transition"), Mind.LastTransition().IsEmpty()
			? FString(TEXT("(none)")) : Mind.LastTransition());
		ImGui::EndTable();
	}

	// The trace carries the schedule runner's rows as well as the mind's own, which is the point:
	// one read shows stimulus, state change, body claim and program selection in the order they
	// happened, rather than three logs that have to be interleaved by hand.
	ImGui::SeparatorText("Transition trace (newest last, 16 rows)");
	const TArray<FString>& Trace = Mind.Trace();
	if (Trace.IsEmpty())
	{
		ImGui::TextDisabled("Nothing recorded yet.");
		return;
	}
	if (ImGui::BeginChild("##Trace", ImVec2(0, GetDpiScale() * 190.0f), ImGuiChildFlags_Borders))
	{
		for (int32 Index = 0; Index < Trace.Num(); ++Index)
		{
			ImGui::TextColored(Index == Trace.Num() - 1
				? ElysiumCogStyle::ColOk : ElysiumCogStyle::ColDim,
				"%2d  %s", Index, COG_TCHAR_TO_CHAR(*Trace[Index]));
		}
	}
	ImGui::EndChild();
}

void FElysiumCogWindow_Npc::RenderSenses(FElysiumEntityWorld& World, FElysiumNpc& Npc)
{
	const double Now = World.NowSeconds();
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	const FElysiumNpcPerception& Tuning = Npc.Senses.Perception;

	ImGui::SeparatorText("Resolved tuning");
	if (BeginFacts("##Tuning"))
	{
		Row(TEXT("npc_perception"), FString::FromInt(Npc.AuthoredPerception));
		Row(TEXT("vision"), Tuning.bResolved
			? FString::Printf(TEXT("%.0f cm"), Tuning.VisionDistanceCm)
			: FString(TEXT("(unresolved)")),
			Tuning.bResolved ? nullptr : &ElysiumCogStyle::ColError);
		Row(TEXT("hearing scalar"), FString::Printf(TEXT("%.2f"), Tuning.HearingScalar));
		// A fallback is not a failure, but it means the numbers came from the average-human row
		// rather than from this character's own — worth knowing before reading a detection range as
		// authored behaviour.
		Row(TEXT("used fallback"), Tuning.bUsedFallback ? TEXT("yes") : TEXT("no"),
			Tuning.bUsedFallback ? &ElysiumCogStyle::ColWarn : nullptr);
		Row(TEXT("enemy sightings"), FString::Printf(TEXT("%d  (lookaround chance %d%%)"),
			Npc.EnemySightings, ElysiumNpcCond::AlertLookaroundChance(Npc.EnemySightings)));
		ImGui::EndTable();
	}

	ImGui::SeparatorText("The committed enemy");
	if (BeginFacts("##Enemy"))
	{
		Row(TEXT("enemy"), NameOf(World, Memory.Enemy));
		Row(TEXT("last enemy"), NameOf(World, Memory.LastEnemy));
		Row(TEXT("last had LOS"), AgeOf(Memory.EnemyLastLosTime, Now));
		// The debounce, spelled as a fraction of its own limit. Ten consecutive failures is what
		// flips `HAVE_ENEMY_LOS` to `ENEMY_OCCLUDED`, and a counter at 7 is a character about to
		// lose one it currently believes it can see.
		Row(TEXT("LOS failures"), FString::Printf(TEXT("%d / %d"),
			Memory.EnemyLosFailures, ElysiumNpcSense::EnemyLosFailureLimit),
			Memory.EnemyLosFailures > 0 ? &ElysiumCogStyle::ColWarn : nullptr);
		Row(TEXT("occluded"), Memory.bEnemyOccluded ? TEXT("yes") : TEXT("no"),
			Memory.bEnemyOccluded ? &ElysiumCogStyle::ColWarn : nullptr);
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Last seen, by relation");
	if (BeginFacts("##Seen"))
	{
		static const TCHAR* const Categories[] = {
			TEXT("hate"), TEXT("fear"), TEXT("dislike"), TEXT("nemesis") };
		for (int32 Index = 0; Index < static_cast<int32>(FElysiumNpcMemory::ESeen::Count); ++Index)
		{
			Row(Categories[Index], FString::Printf(TEXT("%s   %s"),
				*NameOf(World, Memory.LastSeen[Index]), *AgeOf(Memory.LastSeenTime[Index], Now)));
		}
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Heard, hit, and warned");
	if (BeginFacts("##Heard"))
	{
		Row(TEXT("last heard"), Memory.LastHeardCategory.IsEmpty()
			? FString(TEXT("(nothing)"))
			: FString::Printf(TEXT("%s from %s   %s"), *Memory.LastHeardCategory,
				*NameOf(World, Memory.LastHeardSource), *AgeOf(Memory.LastHeardTime, Now)));
		Row(TEXT("heard at"), Memory.LastHeardTime < 0.0
			? FString(TEXT("—")) : Memory.LastHeardPosition.ToCompactString());
		Row(TEXT("last damage"), Memory.LastDamageTime < 0.0
			? FString(TEXT("(none)"))
			: FString::Printf(TEXT("%d from %s   %s"), Memory.LastDamageAmount,
				*NameOf(World, Memory.LastDamageAttacker), *AgeOf(Memory.LastDamageTime, Now)));
		Row(TEXT("repeated-damage window"), Memory.RepeatedDamageWindowStart < 0.0
			? FString(TEXT("closed"))
			: FString::Printf(TEXT("%d accumulated, opened %s"), Memory.RepeatedDamageAccumulated,
				*AgeOf(Memory.RepeatedDamageWindowStart, Now)));
		// The incoming-attack notice: written by `TASK_ANNOUNCE_ATTACK`, and read by the diagnostics
		// only — its four `SHOULD_*` consumers are a policy the survey does not decode. Shown so the
		// producer is observable even though nothing acts on it.
		Row(TEXT("detected attack"), Memory.DetectedAttackTime < 0.0
			? FString(TEXT("(none)"))
			: FString::Printf(TEXT("%s   %s%s"), *NameOf(World, Memory.DetectedAttackAttacker),
				*AgeOf(Memory.DetectedAttackTime, Now),
				ElysiumNpcCond::HasDetectedAttack(Npc, Now) ? TEXT(" (live)") : TEXT(" (expired)")));
		ImGui::EndTable();
	}
}

void FElysiumCogWindow_Npc::RenderConditions(FElysiumNpc& Npc)
{
	const FElysiumNpcConditions& Gathered = Npc.Cognition.Conditions;
	const FElysiumSchedule* Program = Npc.Schedule.IsRunning()
		? ElysiumScheduleFor(Npc.Schedule.Current) : nullptr;
	const FElysiumNpcConditions Mask = Program ? Program->Interrupts : FElysiumNpcConditions();

	ImGui::Text("%d gathered", Gathered.Num());
	ImGui::SameLine();
	if (Program == nullptr)
	{
		ImGui::TextDisabled("· no schedule running, so nothing is being interrupted");
	}
	else if (Mask.IsEmpty())
	{
		// Empty is a recovered posture, not an unfilled default, and saying so is the difference
		// between "we did not fill this in" and "retail's own program declares no interrupts".
		ImGui::TextColored(ElysiumCogStyle::ColWarn,
			"· %s declares NO interrupts — it runs to completion or failure",
			COG_TCHAR_TO_CHAR(ElysiumScheduleName(Npc.Schedule.Current)));
	}
	else
	{
		const FElysiumNpcConditions Hit = Gathered.Intersection(Mask);
		if (Hit.IsEmpty())
		{
			ImGui::TextColored(ElysiumCogStyle::ColDim, "· %d in %s's interrupt mask, none set",
				Mask.Num(), COG_TCHAR_TO_CHAR(ElysiumScheduleName(Npc.Schedule.Current)));
		}
		else
		{
			ImGui::TextColored(ElysiumCogStyle::ColError, "· INTERRUPTING: %s",
				COG_TCHAR_TO_CHAR(*Hit.Describe()));
		}
	}
	ImGui::Checkbox("Set only", &bConditionsSetOnly);
	ImGui::SameLine();
	ImGui::TextDisabled("(a condition that failed to gather is half of every AI diagnosis)");

	const ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders
		| ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
	if (!ImGui::BeginTable("##Conditions", 3, Flags, ImVec2(0, GetDpiScale() * 300.0f)))
	{
		return;
	}
	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Condition");
	ImGui::TableSetupColumn("Gathered", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 72.0f);
	ImGui::TableSetupColumn("Interrupts", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 78.0f);
	ImGui::TableHeadersRow();
	for (int32 Index = 0; Index < NumConditions; ++Index)
	{
		const EElysiumNpcCond Cond = AllConditions[Index];
		const bool bSet = Gathered.Has(Cond);
		const bool bMasked = Mask.Has(Cond);
		if (bConditionsSetOnly && !bSet)
		{
			continue;
		}
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextColored(bSet && bMasked ? ElysiumCogStyle::ColError
			: bSet ? ElysiumCogStyle::ColOk : ElysiumCogStyle::ColDim,
			"%s", COG_TCHAR_TO_CHAR(ElysiumNpcCondName(Cond)));
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(bSet ? "set" : "—");
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(bMasked ? "yes" : "—");
	}
	ImGui::EndTable();
}

void FElysiumCogWindow_Npc::RenderSchedule(FElysiumNpc& Npc)
{
	const FElysiumScheduleState& State = Npc.Schedule;
	if (!State.IsRunning())
	{
		ImGui::TextDisabled("No program running. The next think re-selects.");
		// The scripted director's pushed order outlives no program, so there is nothing else to show
		// — but the fact that selection is what happens next is itself the answer to "why is it
		// standing there".
		return;
	}

	const FElysiumSchedule* Program = ElysiumScheduleFor(State.Current);
	if (BeginFacts("##ScheduleFacts"))
	{
		Row(TEXT("schedule"), FString::Printf(TEXT("%s  (retail #%d / 0x%02x)"),
			ElysiumScheduleName(State.Current), ElysiumScheduleNumber(State.Current),
			ElysiumScheduleNumber(State.Current)), &ElysiumCogStyle::ColName);
		Row(TEXT("task"), Program
			? FString::Printf(TEXT("%d of %d"), State.TaskIndex + 1, Program->Tasks.Num())
			: FString(TEXT("(unregistered program)")));
		Row(TEXT("started"), State.bTaskStarted ? TEXT("yes") : TEXT("no"));
		// Both are PER-RUN: `TASK_SET_FAIL_SCHEDULE` and `TASK_SET_TOLERANCE_DISTANCE` write them for
		// this run of the program, and `Start` resets them, so a previous program's tolerance can
		// never leak into the next one's path request.
		Row(TEXT("fail route"), State.FailScheduleOverride != EElysiumScheduleId::None
			? ElysiumScheduleName(State.FailScheduleOverride)
			: (Program && Program->FailSchedule != EElysiumScheduleId::None
				? ElysiumScheduleName(Program->FailSchedule) : TEXT("(ends the program)")));
		Row(TEXT("tolerance"), State.ToleranceUnits < 0.0f
			? FString(TEXT("(the motor's own acceptance)"))
			: FString::Printf(TEXT("%.0f Source units"), State.ToleranceUnits));
		ImGui::EndTable();
	}

	if (Program == nullptr)
	{
		ImGui::TextColored(ElysiumCogStyle::ColError,
			"This id has no registered program — the runner will fail it by name.");
		return;
	}

	ImGui::SeparatorText("Task program");
	const ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders
		| ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("##Tasks", 3, Flags))
	{
		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 26.0f);
		ImGui::TableSetupColumn("Task");
		ImGui::TableSetupColumn("Operand");
		ImGui::TableHeadersRow();
		for (int32 Index = 0; Index < Program->Tasks.Num(); ++Index)
		{
			const FElysiumTaskStep& Step = Program->Tasks[Index];
			const bool bCurrent = Index == State.TaskIndex;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (bCurrent)
			{
				ImGui::TextColored(ElysiumCogStyle::ColOk, "%s", "▶");
			}
			else
			{
				ImGui::TextColored(ElysiumCogStyle::ColDim, "%d", Index);
			}
			ImGui::TableNextColumn();
			ImGui::TextColored(bCurrent ? ElysiumCogStyle::ColOk : ElysiumCogStyle::ColDim,
				"%s", COG_TCHAR_TO_CHAR(ElysiumTaskName(Step.Task)));
			ImGui::TableNextColumn();
			FString Operand;
			if (!Step.Activity.IsEmpty())
			{
				Operand = Step.Activity;
			}
			else if (Step.Target != EElysiumScheduleId::None)
			{
				Operand = ElysiumScheduleName(Step.Target);
			}
			else if (!FMath::IsNearlyZero(Step.Param))
			{
				Operand = FString::Printf(TEXT("%.2f"), Step.Param);
			}
			ImGui::TextUnformatted(Operand.IsEmpty() ? "—" : COG_TCHAR_TO_CHAR(*Operand));
		}
		ImGui::EndTable();
	}
}

void FElysiumCogWindow_Npc::RenderCombat(FElysiumEntityWorld& World, FElysiumNpc& Npc)
{
	if (BeginFacts("##CombatFacts"))
	{
		const bool bNoCeiling = Npc.MaxHealth <= 0;
		Row(TEXT("health"), FString::Printf(TEXT("%d / %d"), Npc.Health, Npc.MaxHealth),
			bNoCeiling ? &ElysiumCogStyle::ColError : nullptr);
		if (bNoCeiling)
		{
			Row(TEXT(""), TEXT("no ceiling — the damage path is fail-closed. Name a stattemplate."),
				&ElysiumCogStyle::ColError);
		}
		Row(TEXT("stat template"), Npc.StatTemplate.IsEmpty()
			? FString(TEXT("(none)")) : Npc.StatTemplate);
		Row(TEXT("Kindred"), Npc.IsKindred() ? TEXT("yes") : TEXT("no"));

		// The capability split is the branch `CNPC_VHuman::SelectSchedule` takes: a melee-class
		// weapon enters the melee selector, everything else the ranged one, and no weapon at all
		// takes melee with bare-hands defaults whose attack tasks then fail by name. It is the single
		// most useful fact about why a character picked the family it did.
		const ElysiumNpcCond::ECapability Capability = ElysiumNpcCond::WeaponCapability(Npc);
		Row(TEXT("weapon capability"), FString::Printf(TEXT("%s  (0x%05x)"),
			ElysiumNpcCond::CapabilityName(Capability),
			ElysiumNpcCond::CapabilityBits(Capability)),
			Capability == ElysiumNpcCond::ECapability::Unarmed
				? &ElysiumCogStyle::ColWarn : &ElysiumCogStyle::ColOk);
		const FElysiumItem* Active = Npc.Inventory.Active(Npc);
		Row(TEXT("active weapon"), Active && Active->Def
			? Active->Def->Classname : FString(TEXT("(none)")));
		Row(TEXT("authored loadout"), Npc.AdditionalEquipment.IsEmpty()
			? FString(TEXT("(none)"))
			: FString::Printf(TEXT("%s%s"), *Npc.AdditionalEquipment,
				Npc.bLoadoutResolved ? TEXT("  (resolved)") : TEXT("  (pending)")));
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Relationship to the player");
	if (BeginFacts("##Relationship"))
	{
		const FElysiumEntityHandle Player = World.PlayerHandle();
		EElysiumRelationship Value = EElysiumRelationship::Neutral;
		int32 Priority = 0;
		const bool bHasRow = Npc.Relationships.ResolveRow(Player, TEXT("player"), Value, Priority);
		Row(TEXT("authored player_reaction"), Npc.PlayerReaction.IsEmpty()
			? FString(TEXT("(none)")) : Npc.PlayerReaction);
		Row(TEXT("resolved"), bHasRow
			? FString::Printf(TEXT("%s at priority %d"),
				ElysiumRelationships::LexToString(Value), Priority)
			: FString(TEXT("no row — neutral at the default priority 5")),
			Value == EElysiumRelationship::Hate ? &ElysiumCogStyle::ColError : nullptr);
		Row(TEXT("rules"), FString::Printf(TEXT("%d entity, %d class, %d derived"),
			Npc.Relationships.NumEntityRules(), Npc.Relationships.NumClassRules(),
			Npc.Relationships.NumDerivedRules()));
		// The damage memory is the one row with a clock on it, so it is shown as time left rather
		// than as a value: "hostile" and "hostile for another half second" are different facts.
		const FElysiumDerivedRelationship* DamageMemory = Npc.Relationships.FindDerived(Player);
		Row(TEXT("damage memory"), DamageMemory == nullptr
			? FString(TEXT("(none)"))
			: FString::Printf(TEXT("%s, %.1fs left"),
				ElysiumRelationships::LexToString(DamageMemory->Value),
				DamageMemory->ExpiresAt - World.NowSeconds()));
		ImGui::EndTable();
	}

	// Retargeting is the one write outside the Cast tab, and it exists because the alternative is
	// killing a character and standing a new one — which throws away the memory, the sightings
	// counter and the schedule state that made the moment worth looking at.
	ImGui::SeparatorText("Retarget");
	ImGui::TextDisabled("Writes the same relationship store player_reaction seeds, at priority 100.");
	for (int32 Index = 0; Index < NumReactionOptions; ++Index)
	{
		if (Index > 0)
		{
			ImGui::SameLine();
		}
		ImGui::PushID(Index);
		if (ImGui::SmallButton(COG_TCHAR_TO_CHAR(ReactionOptions[Index].Label)))
		{
			ElysiumArenaCast::SetPlayerReaction(World, Npc, ReactionOptions[Index].Value, 100);
		}
		ImGui::PopID();
	}
}

// ================================================================================================
// The world overlay
// ================================================================================================

void FElysiumCogWindow_Npc::GameTick(float DeltaTime)
{
	Super::GameTick(DeltaTime);
	if (bDrawOverlay)
	{
		DrawWorldOverlay();
	}
}

void FElysiumCogWindow_Npc::DrawWorldOverlay() const
{
	UWorld* World3D = GetWorld();
	FElysiumEntityWorld* World = GetEntityWorld();
	if (!World3D || World == nullptr)
	{
		return;
	}
	const APlayerController* PC = World3D->GetFirstPlayerController();
	const FVector Eye = PC && PC->PlayerCameraManager
		? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const float RangeSq = OverlayRangeCm * OverlayRangeCm;

	TArray<FElysiumNpc*> Npcs;
	GatherNpcs(Npcs);
	for (const FElysiumNpc* Npc : Npcs)
	{
		if (Npc->IsInert() || FVector::DistSquared(Eye, Npc->Origin) > RangeSq)
		{
			continue;
		}
		const FElysiumNpcMind& Mind = Npc->GetMind();
		const EElysiumNpcState State = Mind.State();
		const FColor Colour = State == EElysiumNpcState::Combat ? FColor(230, 90, 90)
			: State == EElysiumNpcState::Alert ? FColor(230, 200, 90)
			: FColor(170, 180, 190);

		// Head height rather than the entity origin, which is at the feet. One line, three facts:
		// what state it is in, who owns its body, and what program it is running — the three that
		// together answer "why is it doing that".
		const FVector Head = Npc->Origin + FVector(0.0f, 0.0f, 195.0f);
		FString Line = FString::Printf(TEXT("%s  %s"),
			Npc->TargetName.IsEmpty() ? TEXT("(noname)") : *Npc->TargetName, LexToString(State));
		if (Mind.Owner() != EElysiumBodyOwner::None)
		{
			Line += FString::Printf(TEXT("  [%s]"), LexToString(Mind.Owner()));
		}
		if (Npc->Schedule.IsRunning())
		{
			Line += FString::Printf(TEXT("\n%s"), ElysiumScheduleName(Npc->Schedule.Current));
		}
		DrawDebugString(World3D, Head, Line, nullptr, Colour, 0.0f, /*bDrawShadow=*/true, 1.0f);

		if (bOverlayEnemyLines)
		{
			// The line is drawn to the committed enemy whether or not the NPC can currently see it,
			// and it is dashed when it cannot. That distinction is the occlusion debounce made
			// visible: a solid line means `HAVE_ENEMY_LOS`, a dashed one means the character is
			// acting on memory.
			if (const FElysiumEntity* Enemy =
				ElysiumNpcCond::ResolveEnemyHandle(*World, Npc->Senses.Memory.Enemy))
			{
				const FVector To = Enemy->Origin + FVector(0.0f, 0.0f, 100.0f);
				const FVector From = Npc->Origin + FVector(0.0f, 0.0f, 140.0f);
				// Amber for a character acting on MEMORY, red for one that can currently see its
				// enemy. That is the occlusion debounce made visible, and it is the difference
				// between "the AI is chasing nothing" and "the AI remembers where you went".
				const bool bOccluded = Npc->Senses.Memory.bEnemyOccluded;
				DrawDebugLine(World3D, From, To,
					bOccluded ? FColor(200, 140, 60) : FColor(230, 90, 90),
					false, -1.0f, 0, bOccluded ? 1.0f : 2.5f);
			}
		}
	}
}

// ================================================================================================
// The body-facing half, unchanged
// ================================================================================================

// The facial flex rig (12.3). Everything below a flex controller is arithmetic, so this tab is the
// whole system in one view: the 44 controllers as sliders, and beside them the 65 flexdesc weights
// the RPN rules derive and the morph-target weights the per-flex ramps derive from those. Sliding
// `blink` moves four flexdescs and four morph targets and closes both pairs of lids.
void FElysiumCogWindow_Npc::RenderFacial()
{
	UElysiumNpcSubsystem* Npc = GetNpcSubsystem();
	if (Npc == nullptr)
	{
		ImGui::TextDisabled("NPC subsystem unavailable.");
		return;
	}

	ImGui::SetNextItemWidth(GetDpiScale() * 160.f);
	FCogWidgets::InputTextWithHint("##FacialFilter", "(every rigged body)", FacialFilter);
	ImGui::SameLine();
	ImGui::Checkbox("Non-zero only", &bFacialNonZeroOnly);

	const TArray<UElysiumBipedAnimInstance*> Bodies = Npc->FacialBodies(FacialFilter);
	if (Bodies.IsEmpty())
	{
		ImGui::TextDisabled("No live body carries a facial flex rig.");
		ImGui::TextDisabled("Stand a speaking character up in the Cast tab, or load a map with rigged NPCs.");
		ImGui::TextDisabled("Animals, dancers, crowd bodies and every player body carry no flex data at all.");
		return;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Reset all"))
	{
		for (UElysiumBipedAnimInstance* Inst : Bodies)
		{
			Inst->ResetFlexControllers();
		}
	}

	for (int32 BodyIndex = 0; BodyIndex < Bodies.Num(); ++BodyIndex)
	{
		UElysiumBipedAnimInstance* Inst = Bodies[BodyIndex];
		const FElysiumFacialRig& Rig = *Inst->GetFacialRig();
		const TArray<float>& Values = Inst->GetFlexControllerValues();
		const TArray<float>& Flexes = Inst->GetFlexWeights();
		const TArray<float>& Morphs = Inst->GetMorphWeights();

		ImGui::PushID(BodyIndex);
		const FString Header = FString::Printf(TEXT("%s   %d controllers · %d rules · %d morphs · %d lid(s)"),
			*Rig.Stem, Rig.Controllers.Num(), Rig.Rules.Num(), Rig.Morphs.Num(), Rig.Lids.Num());
		// Only the first body opens by default: a crowded map stands dozens, and 44 sliders each
		// would bury the one being driven.
		if (ImGui::CollapsingHeader(COG_TCHAR_TO_CHAR(*Header),
			BodyIndex == 0 ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
		{
			// The controllers arrive grouped by family — eyelid, brow, nose, mouth, phoneme — so a
			// family break is just a change of the type string.
			FString CurrentType;
			for (int32 i = 0; i < Rig.Controllers.Num() && i < Values.Num(); ++i)
			{
				const FElysiumFlexController& Controller = Rig.Controllers[i];
				if (!Controller.Type.Equals(CurrentType))
				{
					CurrentType = Controller.Type;
					ImGui::SeparatorText(COG_TCHAR_TO_CHAR(*CurrentType));
				}
				float Value = Values[i];
				ImGui::PushID(i);
				if (ImGui::SliderFloat(COG_TCHAR_TO_CHAR(*Controller.Name), &Value,
					Controller.Min, Controller.Max))
				{
					Inst->SetFlexControllerByIndex(i, Value);
				}
				ImGui::PopID();
			}

			ImGui::SeparatorText("Derived");
			const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
				ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
			const ImVec2 TableSize(0, GetDpiScale() * 120.f);
			if (ImGui::BeginTable("##Derived", 2, ImGuiTableFlags_SizingStretchSame))
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextDisabled("flexdesc weights (the rules)");
				if (ImGui::BeginTable("##Flexes", 2, TableFlags, TableSize))
				{
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableSetupColumn("Flexdesc");
					ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 56.f);
					ImGui::TableHeadersRow();
					for (int32 i = 0; i < Flexes.Num() && i < Rig.FlexDescs.Num(); ++i)
					{
						if (bFacialNonZeroOnly && FMath::IsNearlyZero(Flexes[i]))
						{
							continue;
						}
						ImGui::TableNextRow();
						ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Rig.FlexDescs[i]));
						ImGui::TableNextColumn(); ImGui::Text("%.3f", Flexes[i]);
					}
					ImGui::EndTable();
				}

				ImGui::TableNextColumn();
				ImGui::TextDisabled("morph weights (the target ramps)");
				if (ImGui::BeginTable("##Morphs", 2, TableFlags, TableSize))
				{
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableSetupColumn("Morph target");
					ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 56.f);
					ImGui::TableHeadersRow();
					for (int32 i = 0; i < Morphs.Num() && i < Rig.Morphs.Num(); ++i)
					{
						if (bFacialNonZeroOnly && FMath::IsNearlyZero(Morphs[i]))
						{
							continue;
						}
						ImGui::TableNextRow();
						ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Rig.Morphs[i].Name));
						ImGui::TableNextColumn(); ImGui::Text("%.3f", Morphs[i]);
					}
					ImGui::EndTable();
				}
				ImGui::EndTable();
			}
		}
		ImGui::PopID();
	}
}

// The body sample, from both producers at once (CCC1). The player's mover published its row at its
// own tick tail; each NPC row is pulled from its motor here. What the view is for is the claim the
// contract makes — that these are the same record — so they are drawn by one function over one
// struct rather than by two panels that happen to look alike.
void FElysiumCogWindow_Npc::RenderLocomotion()
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		ImGui::TextDisabled("No world.");
		return;
	}

	const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable |
		ImGuiTableFlags_SizingStretchProp;
	if (!ImGui::BeginTable("##Locomotion", ElysiumCogLocomotion::NumColumns, TableFlags,
		ImVec2(0, GetDpiScale() * 200.f)))
	{
		return;
	}

	ImGui::TableSetupScrollFreeze(0, 1);
	ElysiumCogLocomotion::SetupColumns();
	ImGui::TableHeadersRow();

	const AElysiumMapActor* Map = GetMapActor();
	const APlayerController* PC = World->GetFirstPlayerController();
	if (const IElysiumPlayerBody* Body = PC ? Cast<IElysiumPlayerBody>(PC->GetPawn()) : nullptr)
	{
		// The driver's published pair where there is a driver, for the same reason the cast rows
		// below take theirs: the mover's getter recomputes from live component state, so pairing it
		// with the record beside it shows a frame that record never classified. Without a map actor
		// there is no driver, and the mover's own sample is the only sample there is.
		ElysiumCogLocomotion::Row("player", COG_TCHAR_TO_CHAR(*PC->GetPawn()->GetName()),
			Map ? Map->GetPlayerAnimSample() : Body->GetLocomotionSample(),
			Map ? &Map->GetPlayerAnimSelection() : nullptr);
	}

	for (TActorIterator<AElysiumNpcBody> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const AElysiumNpcBody* Npc = *It;
		if (Npc)
		{
			// The driver's published pair, not a fresh sample: a readout that re-samples shows a
			// frame the record beside it never classified.
			ElysiumCogLocomotion::Row("npc", COG_TCHAR_TO_CHAR(*Npc->GetName()),
				Npc->GetAnimSample(), &Npc->GetAnimSelection());
		}
	}

	ImGui::EndTable();

	RenderAnimEventCensus();
}

// The unclaimed-id work list, drawn under the locomotion table because it is the same question from
// the other side: what the clips a body is playing are announcing, and how much of it nothing is
// listening to yet. Global rather than per-character — it is a work list across the session, which
// is why it is not one of the selected-character tabs.
void FElysiumCogWindow_Npc::RenderAnimEventCensus()
{
	if (!ImGui::CollapsingHeader("Sequence events (unclaimed)"))
	{
		return;
	}

	TArray<ElysiumAnimEventCensus::FRow> Rows;
	ElysiumAnimEventCensus::Collect(Rows);
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear##AnimEventCensus"))
	{
		ElysiumAnimEventCensus::Clear();
		Rows.Reset();
	}
	if (Rows.Num() == 0)
	{
		ImGui::TextDisabled("No unclaimed sequence event has fired since load.");
		return;
	}

	const ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
	if (!ImGui::BeginTable("##AnimEvents", 5, Flags, ImVec2(0, GetDpiScale() * 160.f)))
	{
		return;
	}
	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Fires", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.f);
	ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.f);
	ImGui::TableSetupColumn("Label");
	ImGui::TableSetupColumn("Owner");
	ImGui::TableSetupColumn("Options");
	ImGui::TableHeadersRow();

	for (const ElysiumAnimEventCensus::FRow& Row : Rows)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Text("%d", Row.Count);
		ImGui::TableNextColumn();
		// An id above the server band never reached a handler at all, which is a different repair
		// from one a handler saw and refused — so the two never read the same on the list.
		if (Row.bAboveServerBand)
		{
			ImGui::TextDisabled("%d", Row.Event);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("At or above the 5000 server dispatch ceiling: never offered to a "
					"handler.");
			}
		}
		else
		{
			ImGui::Text("%d", Row.Event);
		}
		ImGui::TableNextColumn();
		ImGui::TextColored(ElysiumCogStyle::ColName, "%s", COG_TCHAR_TO_CHAR(*Row.Label));
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%s", COG_TCHAR_TO_CHAR(*Row.OwnerStem));
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%s", COG_TCHAR_TO_CHAR(*Row.Options));
	}
	ImGui::EndTable();
}

// ================================================================================================

void FElysiumCogWindow_Npc::RenderContent()
{
	Super::RenderContent();

	FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		ImGui::TextDisabled("No entity world. Load a map, or open the arena: uv run elysium gr --arena");
		return;
	}

	if (!ImGui::BeginTabBar("##NpcViews"))
	{
		return;
	}

	if (ImGui::BeginTabItem("Cast"))
	{
		RenderCast(*World);
		ImGui::EndTabItem();
	}

	// Every tab below reads ONE character. Drawing them without a selection would mean either
	// picking one arbitrarily or showing forty at once, and both make a decision chain unreadable.
	FElysiumNpc* Selected = SelectedNpc();
	auto BeginSelectedTab = [this, &Selected](const char* Label) -> bool
	{
		if (!ImGui::BeginTabItem(Label))
		{
			return false;
		}
		if (Selected == nullptr)
		{
			ImGui::TextDisabled("Pick a character in the Cast tab.");
			ImGui::EndTabItem();
			return false;
		}
		ImGui::TextColored(ElysiumCogStyle::ColName, "%s", COG_TCHAR_TO_CHAR(
			Selected->TargetName.IsEmpty() ? TEXT("(noname)") : *Selected->TargetName));
		ImGui::SameLine();
		ImGui::TextDisabled("%s · %s",
			COG_TCHAR_TO_CHAR(Selected->Def ? *Selected->Def->Classname : TEXT("(no def)")),
			COG_TCHAR_TO_CHAR(*Selected->ModelStem()));
		ImGui::Separator();
		return true;
	};

	if (BeginSelectedTab("Mind"))
	{
		RenderMind(*Selected);
		ImGui::EndTabItem();
	}
	if (BeginSelectedTab("Senses"))
	{
		RenderSenses(*World, *Selected);
		ImGui::EndTabItem();
	}
	if (BeginSelectedTab("Conditions"))
	{
		RenderConditions(*Selected);
		ImGui::EndTabItem();
	}
	if (BeginSelectedTab("Schedule"))
	{
		RenderSchedule(*Selected);
		ImGui::EndTabItem();
	}
	if (BeginSelectedTab("Combat"))
	{
		RenderCombat(*World, *Selected);
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Locomotion"))
	{
		RenderLocomotion();
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Facial"))
	{
		RenderFacial();
		ImGui::EndTabItem();
	}

	ImGui::EndTabBar();
}

#endif // ENABLE_COG
