#include "Debug/ElysiumArenaCast.h"

#if !UE_BUILD_SHIPPING

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumNpcSubsystem.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Visual/ElysiumNpcVisual.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumArenaCast, Log, All);

namespace ElysiumArenaCast
{

namespace
{
	// Generated names climb a counter that never resets within a session, so a killed character's
	// name is never reissued to a live one — a targetname is how every diagnostic, wire report and
	// `ent_fire` in this runtime addresses a character.
	int32 GSpawnCounter = 0;

	const TCHAR* GeneratedPrefix = TEXT("arena_npc_");

	FElysiumNpc* ResolveNpc(FElysiumEntityWorld& World, const FElysiumEntityHandle& Handle)
	{
		FElysiumEntity* Entity = World.Resolve(Handle);
		return Entity ? Entity->AsNpc() : nullptr;
	}
}

TArray<FString> BodyStems()
{
	TArray<FString> Exported = UElysiumNpcSubsystem::AvailableBodyStems();
	TArray<FString> Baked;
	Baked.Reserve(Exported.Num());
	for (const FString& Stem : Exported)
	{
		if (ElysiumNpcVisual::IsStemBaked(Stem))
		{
			Baked.Add(Stem);
		}
	}
	if (Baked.Num() < Exported.Num())
	{
		// Not a failure — `uv run elysium export characters` covers the cast in whatever scope it was
		// asked for — but a picker showing 40 of 400 bodies looks like a broken list, so the gap is
		// stated once per rescan rather than left to be guessed at.
		UE_LOG(LogElysiumArenaCast, Log,
			TEXT("arena cast: %d of %d exported bodies are baked; the rest cannot stand"),
			Baked.Num(), Exported.Num());
	}
	return Baked;
}

TArray<FString> StatTemplates(UElysiumGameStateSubsystem* GameState)
{
	TArray<FString> Names;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	if (!Rules)
	{
		UE_LOG(LogElysiumArenaCast, Warning,
			TEXT("arena cast: no rulebook — every spawn would carry a zeroed sheet"));
		return Names;
	}
	const FElysiumClanTable& Clans = Rules->Clans();
	Names.Reserve(Clans.NpcTemplates.Num());
	for (const FElysiumClanTemplate& Template : Clans.NpcTemplates)
	{
		if (!Template.TemplateName.IsEmpty())
		{
			Names.Add(Template.TemplateName);
		}
	}
	Names.Sort();
	return Names;
}

TArray<FItemOption> WeaponCatalog(UElysiumGameStateSubsystem* GameState)
{
	TArray<FItemOption> Out;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	if (!Rules)
	{
		UE_LOG(LogElysiumArenaCast, Warning, TEXT("arena cast: no rulebook — no item catalog"));
		return Out;
	}
	const FElysiumItemTable& Items = Rules->Items();
	Out.Reserve(Items.Num());
	for (const FElysiumItemDef& Def : Items.Items)
	{
		if (!Def.IsControllableWeapon() && Def.Type != EElysiumItemType::Ammo)
		{
			continue;
		}
		FItemOption Option;
		Option.Classname = Def.Classname;
		Option.PrintName = Def.PrintName;
		Option.TypeName = ElysiumItemTypeName(Def.Type);
		Option.AmmoType = Def.AmmoType;
		Option.bWieldable = Def.bWieldable;
		Option.bAmmo = Def.Type == EElysiumItemType::Ammo;
		Out.Add(MoveTemp(Option));
	}
	Out.Sort([](const FItemOption& A, const FItemOption& B)
	{
		return A.TypeName == B.TypeName ? A.Classname < B.Classname : A.TypeName < B.TypeName;
	});
	return Out;
}

FElysiumEntityHandle Spawn(FElysiumEntityWorld& World, const FSpawnRequest& Request,
	FString& OutError)
{
	if (Request.Model.IsEmpty())
	{
		OutError = TEXT("no body named");
		return FElysiumEntityHandle::Invalid();
	}
	if (!ElysiumNpcVisual::IsStemBaked(Request.Model))
	{
		OutError = FString::Printf(
			TEXT("the mount carries no body for '%s' — export it before spawning it"),
			*Request.Model);
		return FElysiumEntityHandle::Invalid();
	}
	if (!World.IsActive())
	{
		// A dormant world defers `CallEntityActivate`, so the character would stand there with an
		// unarmed mind and never take a think. Refusing says so; spawning would look like broken AI.
		OutError = TEXT("the entity world is not active yet — wait for the map to finish activating");
		return FElysiumEntityHandle::Invalid();
	}

	FElysiumEntityDef Def;
	Def.Classname = Request.Classname;
	Def.TargetName = Request.TargetName.IsEmpty()
		? FString::Printf(TEXT("%s%d"), GeneratedPrefix, ++GSpawnCounter)
		: Request.TargetName;
	Def.Origin = Request.Origin;
	Def.Keys.Add(TEXT("model"), Request.Model);
	Def.Keys.Add(TEXT("angles"), FString::Printf(TEXT("0 %.1f 0"), Request.Yaw));
	if (!Request.StatTemplate.IsEmpty())
	{
		Def.Keys.Add(TEXT("stattemplate"), Request.StatTemplate);
	}
	if (!Request.Disposition.IsEmpty())
	{
		Def.Keys.Add(TEXT("default_disposition"), Request.Disposition);
	}
	if (!Request.Weapon.IsEmpty())
	{
		Def.Keys.Add(TEXT("additionalequipment"), Request.Weapon);
	}
	Def.Keys.Add(TEXT("player_reaction"), FString::Printf(TEXT("%s %d"),
		ElysiumRelationships::LexToString(Request.PlayerReaction), Request.PlayerReactionPriority));
	Def.Keys.Add(TEXT("npc_perception"), FString::FromInt(Request.Perception));
	Def.Keys.Add(TEXT("allow_alert_lookaround"), Request.bAllowAlertLookaround ? TEXT("1") : TEXT("0"));

	const FString Name = Def.TargetName;
	const FElysiumEntityHandle Handle = World.SpawnRuntimeEntity(MoveTemp(Def));
	if (!Handle.IsSet())
	{
		OutError = FString::Printf(TEXT("'%s' is not a registered classname"), *Request.Classname);
		return Handle;
	}
	if (ResolveNpc(World, Handle) == nullptr)
	{
		// A registered classname that is not on the NPC leaf — `npc_VPlayerController`, or a
		// classname the registry resolved to a bare record. It exists, but it will never think.
		OutError = FString::Printf(
			TEXT("'%s' spawned as a record with no AI leaf — pick an npc_V* combat class"),
			*Request.Classname);
		UE_LOG(LogElysiumArenaCast, Warning, TEXT("arena cast: %s"), *OutError);
		return Handle;
	}

	UE_LOG(LogElysiumArenaCast, Log,
		TEXT("arena cast: %s (%s) as %s at %s reaction=%s/%d weapon=%s"),
		*Name, *Request.Model, *Request.Classname, *Request.Origin.ToCompactString(),
		ElysiumRelationships::LexToString(Request.PlayerReaction), Request.PlayerReactionPriority,
		Request.Weapon.IsEmpty() ? TEXT("(none)") : *Request.Weapon);
	return Handle;
}

bool SetPlayerReaction(FElysiumEntityWorld& World, FElysiumNpc& Npc,
	EElysiumRelationship Value, int32 Priority)
{
	const FElysiumEntityHandle Player = World.PlayerHandle();
	if (!Player.IsSet())
	{
		UE_LOG(LogElysiumArenaCast, Warning,
			TEXT("arena cast: no player entity to set a relationship against"));
		return false;
	}
	// The same store `SeedPlayerRelationship` writes. `SetEntity` replaces an existing row only at
	// an equal-or-higher priority, which is why a retarget has to carry one at least as strong as
	// whatever the character was authored with.
	if (!Npc.Relationships.SetEntity(Player, Value, Priority))
	{
		UE_LOG(LogElysiumArenaCast, Warning,
			TEXT("%s refused a %s relationship at priority %d — an existing row outranks it"),
			*Npc.DebugString(), ElysiumRelationships::LexToString(Value), Priority);
		return false;
	}
	return true;
}

int32 Clear(FElysiumEntityWorld& World, TArray<FElysiumEntityHandle>& Handles)
{
	int32 Killed = 0;
	for (const FElysiumEntityHandle& Handle : Handles)
	{
		if (FElysiumEntity* Entity = World.Resolve(Handle))
		{
			Entity->Kill();
			++Killed;
		}
	}
	Handles.Reset();
	return Killed;
}

const TCHAR* GeneratedNamePrefix()
{
	return GeneratedPrefix;
}

int32 ClearSpawned(FElysiumEntityWorld& World)
{
	int32 Killed = 0;
	for (const TUniquePtr<FElysiumEntity>& Entity : World.Entities())
	{
		if (Entity && !Entity->IsDead()
			&& Entity->TargetName.StartsWith(GeneratedPrefix, ESearchCase::IgnoreCase))
		{
			Entity->Kill();
			++Killed;
		}
	}
	return Killed;
}

TArray<FClanOption> PlayableClans(UElysiumGameStateSubsystem* GameState)
{
	TArray<FClanOption> Out;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	if (!Rules)
	{
		UE_LOG(LogElysiumArenaCast, Warning, TEXT("arena cast: no rulebook — no clan list"));
		return Out;
	}
	const FElysiumClanTable& Clans = Rules->Clans();
	// The 2..8 encoding `FElysiumSheet::Clan()` speaks. Walked rather than read off the table,
	// because the encoding is what every consumer takes and the table's own index is not it.
	for (int32 Clan = 2; Clan <= 8; ++Clan)
	{
		if (!FElysiumSheet::IsValidClan(Clan))
		{
			continue;
		}
		FClanOption Option;
		Option.Id = Clan;
		Option.Name = FElysiumSheet::ClanName(Clan);
		Option.MaleStem = Clans.PlayerBodyStem(Clan, /*bFemale=*/false, 0);
		Option.FemaleStem = Clans.PlayerBodyStem(Clan, /*bFemale=*/true, 0);
		Out.Add(MoveTemp(Option));
	}
	return Out;
}

bool SeedPlayerCharacter(UElysiumGameStateSubsystem* GameState, int32 Clan, bool bMale,
	FString& OutBodyStem, FString& OutError)
{
	OutBodyStem.Reset();
	if (GameState == nullptr)
	{
		OutError = TEXT("no game state");
		return false;
	}
	if (!FElysiumSheet::IsValidClan(Clan))
	{
		OutError = FString::Printf(TEXT("%d is not one of the seven playable clans"), Clan);
		return false;
	}
	UElysiumRulebookSubsystem* Book = GameState->Rulebook();
	if (Book == nullptr)
	{
		OutError = TEXT("no rulebook — a seeded character would carry a zeroed sheet");
		return false;
	}

	// The same gathering the New Game path performs. Every member may be null and every chargen
	// entry point survives it, so a partial rulebook costs the rule it carries rather than the seed.
	FElysiumChargenRules Rules;
	Rules.Stats        = &Book->Stats();
	Rules.Rules        = &Book->Rules();
	Rules.Clans        = &Book->Clans();
	Rules.Histories    = &Book->Histories();
	Rules.Leveling     = &Book->Leveling();
	Rules.TraitEffects = &Book->TraitEffects();
	Rules.Feats        = &Book->Feats();
	Rules.Strings      = &Book->Strings();
	Rules.ExcludedEquip = &Book->ExcludedEquip();

	FElysiumChargenState Chargen;
	Chargen.Currency = EElysiumChargenCurrency::Pools;
	Chargen.Name = FString::Printf(TEXT("Arena %s"), FElysiumSheet::ClanName(Clan));
	Chargen.Clan = Clan;
	Chargen.bMale = bMale;
	// No History. One would add a second trait-effect group and a set of starting-equipment
	// implications, and neither is something a combat baseline should acquire by default.
	Chargen.HistoryId = INDEX_NONE;
	ElysiumChargen::ApplyBaseline(Chargen, Rules);
	GameState->CommitChargen(Chargen);

	OutBodyStem = Book->Clans().PlayerBodyStem(Clan, !bMale, 0);
	if (OutBodyStem.IsEmpty())
	{
		UE_LOG(LogElysiumArenaCast, Warning,
			TEXT("arena cast: %s resolves no player body stem for slot 0"),
			FElysiumSheet::ClanName(Clan));
	}
	UE_LOG(LogElysiumArenaCast, Log,
		TEXT("arena cast: seeded a baseline %s %s (%d point(s) unspent), body '%s'"),
		FElysiumSheet::ClanName(Clan), bMale ? TEXT("male") : TEXT("female"),
		Chargen.TotalRemaining(), OutBodyStem.IsEmpty() ? TEXT("(none)") : *OutBodyStem);
	return true;
}

bool GivePlayerItem(FElysiumEntityWorld& World, const FString& Classname, FString& OutError)
{
	FElysiumPlayer* Player = World.FindPlayer();
	if (!Player)
	{
		OutError = TEXT("no player entity");
		return false;
	}
	if (!Player->Inventory.GiveNamedItem(*Player, Classname).IsSet())
	{
		OutError = FString::Printf(
			TEXT("'%s' has no vdata/items definition, or the inventory refused it"), *Classname);
		return false;
	}
	return true;
}

int32 StockPlayerAmmo(FElysiumEntityWorld& World, int32 Amount, FString& OutError)
{
	OutError.Reset();
	FElysiumPlayer* Player = World.FindPlayer();
	if (!Player)
	{
		OutError = TEXT("no player entity");
		return 0;
	}

	// The types come off what is CARRIED, not off the catalog: seeding reserve for a caliber no
	// carried weapon chambers would stock rounds nothing can fire.
	TSet<FString> AmmoTypes;
	for (int32 i = 0; i < Player->Inventory.Num(); ++i)
	{
		const FElysiumItem* Item = Player->Inventory.At(*Player, i);
		const FElysiumItemDef* Def = Item ? Item->Data() : nullptr;
		if (Def && !Def->AmmoType.IsEmpty())
		{
			AmmoTypes.Add(Def->AmmoType);
		}
	}
	for (const FString& AmmoType : AmmoTypes)
	{
		// Same posture as the arsenal grant: `GiveAmmo`'s wrapper applies no clamp, and neither
		// does this.
		Player->Inventory.AddReserve(AmmoType, Amount);
	}
	UE_LOG(LogElysiumArenaCast, Log,
		TEXT("arena cast: stocked %d round(s) into %d carried ammo type(s)"),
		Amount, AmmoTypes.Num());
	return AmmoTypes.Num();
}

FArmResult ArmPlayerWithArsenal(FElysiumEntityWorld& World,
	UElysiumGameStateSubsystem* GameState, int32 ReservePerType)
{
	FArmResult Result;
	FElysiumPlayer* Player = World.FindPlayer();
	if (!Player)
	{
		Result.FirstError = TEXT("no player entity");
		return Result;
	}
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	if (!Rules)
	{
		Result.FirstError = TEXT("no rulebook — there is no item catalog to give from");
		return Result;
	}

	// **What is in the player's hands is not this function's decision.** `Weapon_Equip` switches the
	// active weapon for any `is_wieldable` item, and exactly ONE controllable weapon in the shipped
	// catalog carries that key — `item_w_chang_claw`, Tiger's Claws, a boss weapon. So a grant that
	// let the switch stand would end with a melee weapon equipped that nobody chose, and a melee
	// `camera_class` is the `0x10` force: the view is pinned to third and `togglecamera` cannot move
	// it. Handing someone an armoury must leave them holding whatever they were holding.
	const FElysiumEntityHandle ActiveBefore = Player->Inventory.ActiveWeapon;

	// `item_w_unarmed` is `CWeaponUnarmed`, the always-carried fallback placeholder — not one of the
	// three controllable weapon families `IsControllableWeapon()` tests for, so the loop below
	// correctly will not grant it (`docs/vtmb/combat-and-damage.md` § "Weapon and input surface").
	// `Holster` still needs one carried: it switches the active weapon to a carried `item_w_unarmed`
	// and refuses outright — logging a warning and leaving the draw unchanged — when the character
	// holds none (`ElysiumPlayerClasses.cpp`'s `Holster` input). Grant it explicitly, ahead of and
	// outside the controllable-weapon loop, so an arena player can holster with zero typing.
	{
		FString UnarmedError;
		if (GivePlayerItem(World, TEXT("item_w_unarmed"), UnarmedError))
		{
			Result.bUnarmedGranted = true;
		}
		else
		{
			++Result.Refused;
			Result.FirstError = UnarmedError;
		}
	}

	// Every ordinarily-reachable player weapon — every ranged and melee item a run gets by playing
	// the game rather than by a drop-only enemy kill or a plus-patch addition/restoration
	// (`docs/vtmb/wielded_weapons.md` § "Who carries what"). `item_w_occultblade` is the
	// Tal'Mahe'Ra Blade.
	const FString Arsenal[] = {
		// Melee — blunt
		TEXT("item_w_fists"), TEXT("item_w_baton"), TEXT("item_w_baseball_bat"),
		TEXT("item_w_tire_iron"), TEXT("item_w_severed_arm"), TEXT("item_w_sledgehammer"),
		// Melee — bladed
		TEXT("item_w_knife"), TEXT("item_w_fireaxe"), TEXT("item_w_katana"), TEXT("item_w_bush_hook"),
		// Melee — special
		TEXT("item_w_torch"), TEXT("item_w_occultblade"),
		// Ranged — handguns
		TEXT("item_w_thirtyeight"), TEXT("item_w_glock_17c"), TEXT("item_w_colt_anaconda"),
		TEXT("item_w_deserteagle"),
		// Ranged — shotguns
		TEXT("item_w_ithaca_m_37"), TEXT("item_w_supershotgun"),
		// Ranged — machine guns
		TEXT("item_w_mac_10"), TEXT("item_w_uzi"), TEXT("item_w_steyr_aug"),
		// Ranged — rifles
		TEXT("item_w_remington_m_700"),
		// Ranged — special
		TEXT("item_w_crossbow"), TEXT("item_w_flamethrower"),
	};

	TSet<FString> AmmoTypes;
	const FElysiumItemTable& Items = Rules->Items();
	for (const FString& Classname : Arsenal)
	{
		const FElysiumItemDef* Def = Items.Find(Classname);
		if (!Def || !Def->IsControllableWeapon())
		{
			continue;
		}
		FString Error;
		if (!GivePlayerItem(World, Classname, Error))
		{
			++Result.Refused;
			if (Result.FirstError.IsEmpty())
			{
				Result.FirstError = Error;
			}
			continue;
		}
		switch (Def->Type)
		{
		case EElysiumItemType::WeaponMelee:   ++Result.Melee; break;
		case EElysiumItemType::WeaponFirearm:
			++Result.Firearms;
			AmmoTypes.Add(Def->AmmoType);
			// A fresh firearm spawns loaded with `Default_Size`, which a record may author BELOW the
			// magazine's capacity. An arena player starts at max: top the loaded magazine to `Size`,
			// the same ceiling a completed reload reaches.
			if (FElysiumItem* Granted = Player->Inventory.FindOrdinary(*Player, Classname))
			{
				Granted->MagazineCount = FMath::Max(Granted->MagazineCount, Def->MagazineSize);
			}
			break;
		case EElysiumItemType::WeaponThrown:  ++Result.Thrown; break;
		default: break;
		}
	}

	for (const FString& AmmoType : AmmoTypes)
	{
		// `GiveAmmo`'s own wrapper applies no clamp, and neither does this: a reserve is a count,
		// and inventing a ceiling here would be a rule with no author.
		Player->Inventory.AddReserve(AmmoType, ReservePerType);
		++Result.AmmoTypes;
	}

	// Put the hands back. Assigned directly rather than through `SetActiveWeapon`, which takes an
	// item and therefore cannot express "nothing" — the ordinary state a fresh arena player is in.
	// `PublishEquippedCameraClass` is called because the contract on `Inventory.ActiveWeapon` is
	// that every writer calls it; skipping it would leave the camera arbitrating against the class
	// of a weapon that is no longer active.
	if (Player->Inventory.ActiveWeapon != ActiveBefore)
	{
		Player->Inventory.ActiveWeapon = ActiveBefore;
		Player->PublishEquippedCameraClass();
		UE_LOG(LogElysiumArenaCast, Log,
			TEXT("arena cast: restored the active weapon the grant displaced — "
				 "the armoury is carried, not drawn"));
	}

	UE_LOG(LogElysiumArenaCast, Log,
		TEXT("arena cast: armed the player — unarmed=%s, %d melee, %d firearm(s), %d thrown, "
			 "%d ammo type(s) at %d each, %d refused"),
		Result.bUnarmedGranted ? TEXT("granted") : TEXT("MISSING — Holster will refuse"),
		Result.Melee, Result.Firearms, Result.Thrown, Result.AmmoTypes, ReservePerType,
		Result.Refused);
	return Result;
}

} // namespace ElysiumArenaCast

#endif // !UE_BUILD_SHIPPING
