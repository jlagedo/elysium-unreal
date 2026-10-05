#include "Debug/ElysiumArenaStage.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaBuilder.h"
#include "Debug/ElysiumArenaScenario.h"
#include "Debug/ElysiumGreenRoomShared.h"   // ResolveDriveBody: the seat's four steps need the drive refs
#include "ElysiumCameraComponent.h"
#include "ElysiumCastData.h"                // the body name's canonical model id
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGymSpec.h"                 // SeatOrigin, the one feet-to-centre conversion
#include "ElysiumInputRouter.h"             // ResetPlayerState: the router's held-button latches
#include "ElysiumMapActor.h"
#include "ElysiumMapEntities.h"             // from_map: the baked DA_<map>_Entities reader
#include "ElysiumMapPlaces.h"               // FElysiumPlaceRow, the network handed to the rebuild
#include "ElysiumMoveSolve.h"               // ElysiumMove::U, for a moved row's Source `origin`
#include "ElysiumMovementComponent.h"
#include "ElysiumPlayer.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumPlayerController.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumUserCmd.h"
#include "Substrate/ElysiumDisciplines.h"   // ClearAll: the one discipline teardown
#include "HAL/IConsoleManager.h"            // ResetPlayerState: the `debug_stealth_light` pin
#include "Substrate/ElysiumNodeEntity.h"    // which record rows take an AI-network node
#include "Substrate/ElysiumPlaceSet.h"
#include "Visual/ElysiumCharacterModel.h"   // IdFromSource, the model key's canonical reading

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumArenaStage, Log, All);

// Qualified rather than anonymous: a unity build concatenates translation units.
namespace ElysiumArenaStageDetail
{
	// Unreal-native yaw, degrees, from one world point toward another (the arena's own convention).
	float YawToward(const FVector& From, const FVector& To)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(To.Y - From.Y, To.X - From.X));
	}

	// The seat names a record may use, with the spec values they stand for.
	bool ResolveSeat(const ElysiumArena::FSpec& Spec, const FString& Name, ElysiumArenaStage::FPlace& Out)
	{
		if (Name.Equals(TEXT("start"), ESearchCase::IgnoreCase))
		{
			Out.FeetCm = Spec.PlayerFeet;
			Out.YawDeg = Spec.PlayerYaw;
			return true;
		}
		if (Name.Equals(TEXT("cover_seat"), ESearchCase::IgnoreCase))
		{
			Out.FeetCm = Spec.CoverSeatFeet;
			Out.YawDeg = Spec.CoverSeatYaw;
			return true;
		}
		if (Name.Equals(TEXT("cover_behind"), ESearchCase::IgnoreCase))
		{
			Out.FeetCm = Spec.CoverBehindFeet;
			Out.YawDeg = Spec.CoverBehindYaw;
			return true;
		}
		return false;
	}

	FString ArenaPlaceNames(const ElysiumArena::FSpec& Spec)
	{
		TArray<FString> Names = { TEXT("start"), TEXT("cover_seat"), TEXT("cover_behind") };
		for (const ElysiumArena::FPad& Pad : Spec.Pads)
		{
			Names.Add(Pad.Name.ToString());
		}
		for (const ElysiumArena::FAnchor& Anchor : Spec.Anchors)
		{
			Names.Add(Anchor.Name.ToString());
		}
		for (const ElysiumArena::FNode& Node : Spec.Nodes)
		{
			Names.Add(Node.Name);
		}
		return FString::Join(Names, TEXT(", "));
	}

	// The Source `origin` keyvalue a moved row carries, as `ElysiumArena::AuthoredRow` writes one:
	// inches, Y negated back.
	FString SourceOrigin(const FVector& OriginCm)
	{
		const double Inches = 1.0 / ElysiumMove::U;
		return FString::Printf(TEXT("%.9g %.9g %.9g"), OriginCm.X * Inches, -OriginCm.Y * Inches,
			OriginCm.Z * Inches);
	}

	// The Unreal-native yaw a row's `angles` key authors (`"p y r"`, Source degrees), 0 with none.
	float AuthoredYaw(const FElysiumEntityDef& Def)
	{
		const FString* Angles = Def.Keys.Find(TEXT("angles"));
		if (Angles == nullptr)
		{
			return 0.0f;
		}
		TArray<FString> Parts;
		Angles->ParseIntoArrayWS(Parts);
		return Parts.Num() >= 2 ? -FCString::Atof(*Parts[1]) : 0.0f;
	}

	// The node type a row's classname makes (`FElysiumPlaceRow::Type`: 2 ground, 3 air, 4 climb --
	// `CNodeEnt::Spawn` `0x102d7b50..0x102d7b62` decides it by class).
	int32 NodeType(const FString& Classname)
	{
		if (Classname.StartsWith(TEXT("info_node_air"), ESearchCase::IgnoreCase))
		{
			return 3;
		}
		if (Classname.Equals(TEXT("info_node_climb"), ESearchCase::IgnoreCase))
		{
			return 4;
		}
		return 2;
	}

	// The record's own node rows join the network after the room's. `Load`'s counter
	// (`ElysiumNodeEntity::SpawnNodeRow`, `CNodeEnt::Spawn`) hands node `i` to the `i`th
	// non-standalone node row in def order, so one network row per such def, in that order, is what
	// keeps every record node bound to a node of its own rather than counted out of range.
	int32 AppendRecordNodes(const FElysiumEntityDefs& Defs, int32 FirstRecordDef, TArray<FElysiumPlaceRow>& Rows)
	{
		int32 Added = 0;
		for (int32 Index = FirstRecordDef; Index < Defs.Num(); ++Index)
		{
			const FElysiumEntityDef& Def = Defs.Defs[Index];
			if (!ElysiumNodeEntity::IsNodeClassname(Def.Classname)
				|| ElysiumNodeEntity::IsStandaloneClassname(Def.Classname))
			{
				continue;
			}
			FElysiumPlaceRow Row;
			Row.NetworkIndex = Rows.Num();
			Row.Type = NodeType(Def.Classname);
			Row.OriginCm = Def.Origin;
			Row.YawDeg = AuthoredYaw(Def);
			Row.HintBspIndex = INDEX_NONE;
			Rows.Add(Row);
			++Added;
		}
		return Added;
	}

	// `from_map`: the named rows of `DA_<map>_Entities`, verbatim, moved so the anchor row stands at
	// the record's place and every other row keeps its offset from it, Z on the arena floor.
	bool AddFromMapRows(const ElysiumArenaStage::FHost& Host, const FElysiumArenaFromMap& FromMap,
		FElysiumEntityDefs& Defs, int32& OutAdded, FString& OutError)
	{
		OutAdded = 0;
		FElysiumEntityDefs MapDefs;
		if (ElysiumEntityDefSource::Load(FromMap.Map, MapDefs) == EElysiumEntityDefSource::None)
		{
			OutError = FString::Printf(TEXT("from_map: '%s' has no baked DA_%s_Entities to read"),
				*FromMap.Map, *FromMap.Map);
			return false;
		}
		for (const FString& Name : FromMap.Names)
		{
			if (!MapDefs.Defs.ContainsByPredicate([&Name](const FElysiumEntityDef& Def)
				{ return Def.TargetName.Equals(Name, ESearchCase::IgnoreCase); }))
			{
				OutError = FString::Printf(TEXT("from_map: %s carries no row named '%s'"), *FromMap.Map, *Name);
				return false;
			}
		}
		const FElysiumEntityDef* Anchor = MapDefs.Defs.FindByPredicate([&FromMap](const FElysiumEntityDef& Def)
			{ return Def.TargetName.Equals(FromMap.Anchor, ESearchCase::IgnoreCase); });
		ElysiumArenaStage::FPlace Target;
		if (Anchor == nullptr || !ElysiumArenaStage::ResolveAt(Host, FromMap.At, Target, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(TEXT("from_map: %s carries no anchor row '%s'"), *FromMap.Map,
					*FromMap.Anchor);
			}
			return false;
		}
		const FVector Delta = Target.FeetCm - Anchor->Origin;
		const double FloorZ = Host.Origin.Z + Host.Spec.FloorZ();
		// In the map's own (lump) order, so the record's node rows reach the counter in the order the
		// map authored them.
		for (const FElysiumEntityDef& Source : MapDefs.Defs)
		{
			const bool bNamed = FromMap.Names.ContainsByPredicate([&Source](const FString& Name)
				{ return Source.TargetName.Equals(Name, ESearchCase::IgnoreCase); });
			if (!bNamed)
			{
				continue;
			}
			if (Source.bSky)
			{
				OutError = FString::Printf(TEXT("from_map: '%s' lives in %s's 3D-skybox miniature"),
					*Source.TargetName, *FromMap.Map);
				return false;
			}
			if (Source.IsBrush())
			{
				// A brush row's mesh and collision belong to its map's bake; a map record runs it there.
				OutError = FString::Printf(TEXT("from_map: '%s' is a brush entity (*%d) and does not travel; ")
					TEXT("run it on its map host (`\"stage\": \"map:%s\"`)"), *Source.TargetName, Source.Model,
					*FromMap.Map);
				return false;
			}
			FElysiumEntityDef Def = Source;
			Def.Origin.X += Delta.X;
			Def.Origin.Y += Delta.Y;
			Def.Origin.Z = FloorZ;
			if (Def.Keys.Contains(TEXT("origin")))
			{
				Def.Keys.Add(TEXT("origin"), SourceOrigin(Def.Origin));
			}
			Defs.Defs.Add(MoveTemp(Def));
			++OutAdded;
		}
		return true;
	}
}

namespace ElysiumArenaStage
{

FElysiumEntityWorld* FHost::GetEntityWorld() const
{
	const AElysiumMapActor* MapActor = Map.Get();
	return MapActor != nullptr ? MapActor->GetEntityWorld() : nullptr;
}

bool ResolveAt(const FHost& Host, const FElysiumArenaAt& At, FPlace& Out, FString& OutError)
{
	Out = FPlace();
	if (!At.bSet)
	{
		OutError = TEXT("no place given");
		return false;
	}
	if (At.bCoordinates)
	{
		Out.FeetCm = Host.bArena ? Host.Origin + At.Coordinates : At.Coordinates;
		return true;
	}
	if (!Host.bArena)
	{
		FElysiumEntityWorld* World = Host.GetEntityWorld();
		FElysiumEntity* Entity = World == nullptr ? nullptr
			: At.Name.Equals(TEXT("player"), ESearchCase::IgnoreCase) ? static_cast<FElysiumEntity*>(World->FindPlayer())
			: World->FindByName(At.Name);
		if (Entity == nullptr)
		{
			OutError = FString::Printf(TEXT("the map has no live entity named '%s'"), *At.Name);
			return false;
		}
		Out.FeetCm = Entity->Origin;
		return true;
	}
	const ElysiumArena::FSpec& Spec = Host.Spec;
	if (ElysiumArenaStageDetail::ResolveSeat(Spec, At.Name, Out))
	{
		Out.FeetCm += Host.Origin;
		return true;
	}
	const FName AsName(*At.Name);
	if (const ElysiumArena::FPad* Pad = Spec.FindPad(AsName))
	{
		Out.FeetCm = Host.Origin + Pad->FeetOrigin;
		Out.YawDeg = Pad->Yaw;
		return true;
	}
	if (const ElysiumArena::FAnchor* Anchor = Spec.FindAnchor(AsName))
	{
		Out.FeetCm = Host.Origin + Anchor->FeetOrigin;
		Out.YawDeg = Anchor->Yaw;
		return true;
	}
	if (const ElysiumArena::FNode* Node = Spec.FindNode(At.Name))
	{
		Out.FeetCm = Host.Origin + Node->FeetCm;
		Out.YawDeg = Node->YawDeg;
		return true;
	}
	OutError = FString::Printf(TEXT("'%s' is no place of the arena (%s)"), *At.Name,
		*ElysiumArenaStageDetail::ArenaPlaceNames(Spec));
	return false;
}

bool ResolveFace(const FHost& Host, const FElysiumArenaFace& Face, const FPlace& From,
	const FVector& PlayerFeet, float& OutYaw, FString& OutError)
{
	switch (Face.Kind)
	{
	case FElysiumArenaFace::EKind::Yaw:
		OutYaw = Face.Yaw;
		return true;
	case FElysiumArenaFace::EKind::Player:
		OutYaw = ElysiumArenaStageDetail::YawToward(From.FeetCm, PlayerFeet);
		return true;
	case FElysiumArenaFace::EKind::Name:
	{
		FElysiumArenaAt Toward;
		Toward.bSet = true;
		Toward.Name = Face.Name;
		FPlace Target;
		if (!ResolveAt(Host, Toward, Target, OutError))
		{
			OutError = FString::Printf(TEXT("face: %s"), *OutError);
			return false;
		}
		OutYaw = ElysiumArenaStageDetail::YawToward(From.FeetCm, Target.FeetCm);
		return true;
	}
	default:
		OutYaw = From.YawDeg;
		return true;
	}
}

bool PlayerPlace(const FHost& Host, const FElysiumArenaScenario& Record, FPlace& Out, FString& OutError)
{
	OutError.Reset();
	FElysiumArenaAt At = Record.Player.At;
	if (!At.bSet)
	{
		if (!Host.bArena)
		{
			return false;   // a map host leaves the player where the map put it
		}
		At.bSet = true;
		At.Name = TEXT("start");
	}
	if (!ResolveAt(Host, At, Out, OutError))
	{
		OutError = FString::Printf(TEXT("player.at: %s"), *OutError);
		return false;
	}
	float Yaw = Out.YawDeg;
	if (!ResolveFace(Host, Record.Player.Face, Out, Out.FeetCm, Yaw, OutError))
	{
		OutError = FString::Printf(TEXT("player.%s"), *OutError);
		return false;
	}
	Out.YawDeg = Yaw;
	return true;
}

bool BodyModelPath(const FString& Body, FString& OutModelPath, FString& OutError)
{
	const FString ModelId = UElysiumCastData::ModelIdForPreparation(Body, OutError);
	const FString Prefix(TEXT("vtmb:model:"));
	if (!ElysiumCharacterModel::IsCanonicalId(ModelId))
	{
		OutError = FString::Printf(TEXT("'%s' names no cast model: %s"), *Body,
			OutError.IsEmpty() ? TEXT("no canonical id") : *OutError);
		return false;
	}
	OutModelPath = FString::Printf(TEXT("models/%s.mdl"), *ModelId.Mid(Prefix.Len()));
	if (ElysiumCharacterModel::IdFromSource(OutModelPath) != ModelId)
	{
		OutError = FString::Printf(TEXT("'%s' (%s) has no source-path spelling"), *Body, *ModelId);
		return false;
	}
	OutError.Reset();
	return true;
}

bool BuildRow(const FHost& Host, const FElysiumArenaRow& Row, const FVector& PlayerFeet,
	FElysiumEntityDef& Out, FString& OutError)
{
	if (Row.Classname.IsEmpty())
	{
		OutError = FString::Printf(TEXT("row '%s' names no classname"), *Row.Name);
		return false;
	}
	FPlace Place;
	if (!ResolveAt(Host, Row.At, Place, OutError))
	{
		OutError = FString::Printf(TEXT("row '%s' at: %s"), *Row.Name, *OutError);
		return false;
	}
	float Yaw = Place.YawDeg;
	if (!ResolveFace(Host, Row.Face, Place, PlayerFeet, Yaw, OutError))
	{
		OutError = FString::Printf(TEXT("row '%s' %s"), *Row.Name, *OutError);
		return false;
	}
	Out = ElysiumArena::AuthoredRow(*Row.Classname, Row.Name, Place.FeetCm, Yaw);
	if (!Row.Body.IsEmpty())
	{
		FString ModelPath;
		if (!BodyModelPath(Row.Body, ModelPath, OutError))
		{
			OutError = FString::Printf(TEXT("row '%s' body: %s"), *Row.Name, *OutError);
			return false;
		}
		Out.Keys.Add(TEXT("model"), ModelPath);
	}
	for (const TPair<FString, FString>& Key : Row.Keys)
	{
		Out.Keys.Add(Key.Key, Key.Value);
	}
	// The bake hoists `StartHidden == "1"` out of the keys into the def; a staged row does the same
	// rather than leaving the two to disagree.
	Out.bStartHidden = Out.Keys.FindRef(TEXT("StartHidden")) == TEXT("1");
	return true;
}

bool SeatPlayerAt(UWorld* World, const FVector& FeetWorld, float YawDeg, FString& OutError)
{
	const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(World);
	if (!Refs)
	{
		OutError = TEXT("no player body to seat");
		return false;
	}
	Refs.Move->ResetState();
	Refs.Pawn->SetActorLocation(ElysiumGym::SeatOrigin(FeetWorld, Refs.Body->GetBodyHalfHeight()),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	Refs.PC->SetControlRotation(FRotator(0.0f, YawDeg, 0.0f));
	if (UElysiumCameraComponent* Camera = Refs.Body->GetCameraComponent())
	{
		Camera->RequestReseed();
	}
	return true;
}

FString ResetPlayerState(UWorld* World, FElysiumEntityWorld* EntityWorld)
{
	TArray<FString> Undone;

	// The pawn half: what one record's input leaves in the router and the mover.
	const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(World);
	if (Refs)
	{
		const AElysiumPlayerController* Controller = Cast<AElysiumPlayerController>(Refs.PC);
		if (UElysiumInputRouter* Router = Controller != nullptr ? Controller->GetInputRouter() : nullptr)
		{
			if (Router->IsReplaying())
			{
				Router->StopReplay();
				Undone.Add(TEXT("input replay"));
			}
			// A typed `+duck` latches until a typed `-duck` (`docs/vtmb/controls.md`), and nothing types
			// the `-`: the latch outlives the record and re-raises the duck on the next frame no matter
			// what the mover was reset to. `ClearButtons` is the router's own "input was taken away".
			const uint64 Held = Router->Builder().ButtonBits();
			if (Held != 0)
			{
				Undone.Add(FString::Printf(TEXT("held buttons 0x%llx"), Held));
			}
			Router->Builder().ClearButtons();
		}
		if (Refs.Move->IsDucked() || Refs.Move->IsDucking())
		{
			Undone.Add(TEXT("ducked"));
		}
		// Drops the duck and its retained request, the carried motion and the anim movement lock.
		Refs.Move->ResetState();
	}

	// The entity half: the player's own transactions, through the doors world teardown and the
	// respawn paths use.
	FElysiumPlayer* Player = EntityWorld != nullptr ? EntityWorld->FindPlayer() : nullptr;
	if (Player != nullptr)
	{
		if (Player->IsFeedPaired())
		{
			Player->BreakFeed();   // either role; the attacker's `FeedInterrupt`, idempotent
			Undone.Add(TEXT("feed"));
		}
		if (Player->Grapple.IsPaired() || Player->Grapple.bOwnsStealthAction)
		{
			// A stealth kill is a type-3 grapple pair (`FElysiumPlayer::StartStealthKill`); the one
			// leave also ends its action ownership, as `FElysiumEntityWorld::Teardown` does.
			Undone.Add(Player->Grapple.bOwnsStealthAction ? TEXT("stealth kill") : TEXT("grapple"));
			Player->LeaveGrapplePair();
		}
		if (Player->Inventory.Active(*Player) != nullptr)
		{
			Undone.Add(TEXT("wielded item"));
		}
		// `Weapon_Switch(NULL)`: nothing in hand. A record that is `armed` is armed by the runner at
		// zero, after Activate, through `GivePlayerItem` / `ArmPlayerWithArsenal`.
		Player->Inventory.Holster(*Player);

		for (int32 Slot = 0; Slot < FElysiumDisciplineState::SlotCount; ++Slot)
		{
			if (Player->Disciplines.IsActive(Slot))
			{
				Undone.Add(TEXT("discipline active"));
				break;
			}
		}
		ElysiumDisciplines::ClearAll(*Player);   // idempotent; the one teardown `vdiscipline_endall` shares

		// The unkillable latch (`events_player`'s `MakePlayerUnkillable`, the `god` command) rides the
		// session record across the rebuild too: a record that staged it would leave the next
		// record's player unkillable, and `god` -- a toggle -- would then turn it OFF.
		if (Player->IsUnkillable())
		{
			Player->SetUnkillable(false);
			Undone.Add(TEXT("unkillable latch"));
		}

		// `Health` on the sheet counts damage TAKEN and rides the session record across the rebuild.
		const int32 Healed = Player->HealDamage(MAX_int32);
		if (Healed > 0)
		{
			Undone.Add(FString::Printf(TEXT("%d damage"), Healed));
		}
	}
	// The light pin: retail's `debug_stealth_light` (`0x109384d8`) back to its default `-1`, off. A
	// record's `light_pin` releases its own at the run's end; this also covers a `console` action's.
	if (IConsoleVariable* LightPin = IConsoleManager::Get().FindConsoleVariable(TEXT("debug_stealth_light")))
	{
		if (LightPin->GetFloat() != -1.0f)
		{
			Undone.Add(FString::Printf(TEXT("debug_stealth_light %g"), LightPin->GetFloat()));
			LightPin->Set(-1.0f, ECVF_SetByCode);
		}
	}
	// NOT RESET, no door: the law/police block (`FElysiumPlayerRecord::Police`) crosses a rebuild
	// unscoped by design (session clock) and has no door that clears it short of a new game.
	return FString::Join(Undone, TEXT(", "));
}

bool Stage(const FElysiumArenaScenario& Record, const FHost& Host, FString& OutSummary, FString& OutError)
{
	OutSummary.Reset();
	AElysiumMapActor* Map = Host.GetMap();
	UWorld* World = Host.GetWorld();
	if (Map == nullptr || World == nullptr)
	{
		OutError = TEXT("no map actor or world to stage into");
		return false;
	}

	if (!Host.bArena)
	{
		// A map host: the cast names the map's own entities, and every one must be there.
		FElysiumEntityWorld* EntityWorld = Map->GetEntityWorld();
		if (EntityWorld == nullptr)
		{
			OutError = TEXT("the map has no entity world");
			return false;
		}
		for (const FElysiumArenaRow& Row : Record.Cast)
		{
			if (EntityWorld->FindByName(Row.Name) == nullptr)
			{
				OutError = FString::Printf(TEXT("cast: %s has no live entity named '%s'"),
					*Map->MapName, *Row.Name);
				return false;
			}
		}
		// The map's own Activate ran long before, from the boot's seed: the launcher passes the boot's
		// first record's `seed` as `-ArenaSeed`, which New Game took in place of the clock (H17,
		// `FElysiumArenaRun::LaunchSeed`). Re-seeding here puts every record's run at a known random
		// position at its zero; a later record of a shared boot still inherits the map the earlier
		// records left, so it replays only as part of that same boot.
		ElysiumRng::SeedAll(Record.Seed);
		FMath::RandInit(Record.Seed);
		FMath::SRandInit(Record.Seed);
		// The map is not rebuilt, so the previous record's player is still the player: stand it at rest.
		const FString Released = ResetPlayerState(World, EntityWorld);
		if (!Released.IsEmpty())
		{
			UE_LOG(LogElysiumArenaStage, Log, TEXT("%s: player reset, undid: %s"), *Record.Name, *Released);
		}
		FPlace Seat;
		FString SeatError;
		if (PlayerPlace(Host, Record, Seat, SeatError))
		{
			if (!SeatPlayerAt(World, Seat.FeetCm, Seat.YawDeg, SeatError))
			{
				OutError = FString::Printf(TEXT("player: %s"), *SeatError);
				return false;
			}
		}
		else if (!SeatError.IsEmpty())
		{
			OutError = SeatError;
			return false;
		}
		OutSummary = FString::Printf(TEXT("%s on %s: %d cast entit%s present, seed %d"), *Record.Name,
			*Map->MapName, Record.Cast.Num(), Record.Cast.Num() == 1 ? TEXT("y") : TEXT("ies"), Record.Seed);
		return true;
	}

	if (!Map->IsStageOnly())
	{
		OutError = TEXT("the arena host needs a stage world");
		return false;
	}
	const ElysiumArena::FSpec& Spec = Host.Spec;
	if (Spec.Solids.IsEmpty())
	{
		OutError = TEXT("no arena is standing to stage into");
		return false;
	}
	FPlace Seat;
	if (!PlayerPlace(Host, Record, Seat, OutError))
	{
		return false;
	}

	// Every row, as one def list in the order a map would carry it: the room's anchors and cover-node
	// rows, the record's rows, its from_map rows, its cast. The player entity is not a def:
	// `RebuildStageWorld` creates it after `Load`, as `LoadMap` does.
	FElysiumEntityDefs Defs;   // MapName empty: a stage is nobody's map (no snapshot at teardown)
	for (const ElysiumArena::FAnchor& Anchor : Spec.Anchors)
	{
		Defs.Defs.Add(ElysiumArena::AnchorRow(Anchor, Host.Origin));
	}
	for (int32 Index = 0; Index < Spec.Nodes.Num(); ++Index)
	{
		Defs.Defs.Add(ElysiumArena::NodeRow(Spec.Nodes[Index], Index, Host.Origin));
	}
	const int32 FirstRecordDef = Defs.Num();
	for (const FElysiumArenaRow& Row : Record.Rows)
	{
		if (!BuildRow(Host, Row, Seat.FeetCm, Defs.Defs.AddDefaulted_GetRef(), OutError))
		{
			OutError = FString::Printf(TEXT("rows: %s"), *OutError);
			return false;
		}
	}
	int32 FromMapRows = 0;
	if (Record.FromMap.bSet
		&& !ElysiumArenaStageDetail::AddFromMapRows(Host, Record.FromMap, Defs, FromMapRows, OutError))
	{
		return false;
	}
	for (const FElysiumArenaRow& Row : Record.Cast)
	{
		if (!BuildRow(Host, Row, Seat.FeetCm, Defs.Defs.AddDefaulted_GetRef(), OutError))
		{
			OutError = FString::Printf(TEXT("cast: %s"), *OutError);
			return false;
		}
	}
	TArray<FElysiumPlaceRow> Network = ElysiumArena::NodePlaceRows(Spec, Host.Origin);
	const int32 RecordNodes = ElysiumArenaStageDetail::AppendRecordNodes(Defs, FirstRecordDef, Network);
	const int32 DefCount = Defs.Num();

	// The record's random position, before `Load` and `Activate` draw anything. `SeedAll` re-seeds
	// every `ElysiumRng` stream (`NpcSchedule` among them) from the one seed; the engine's own global
	// generator (`FMath::Rand`/`SRand`) is seeded too, for anything Unreal-side that draws from it.
	ElysiumRng::SeedAll(Record.Seed);
	FMath::RandInit(Record.Seed);
	FMath::SRandInit(Record.Seed);

	// Each rebuilt arena is a fresh map clock as well as a fresh random position. Otherwise
	// NPCInit/StartNPC (0x10273390 / 0x10273ad0) cross their curtime <= 1 startup branches
	// in later records, changing both first-think timing and the shared NpcSchedule draw order.
	if (UGameInstance* StageInstance = World->GetGameInstance())
	{
		if (UElysiumSessionSubsystem* StageSession = StageInstance->GetSubsystem<UElysiumSessionSubsystem>())
		{
			StageSession->TimeControl().ResetClock();
		}
	}

	// A stage the last record left Failed (the barrier's wait ended) is rebuilt over, not refused: its
	// pending character admissions are released here, and the rebuild replaces the failed flag, the
	// half-built entity world and the model preparations (`RebuildStageWorld` -> `TeardownEntityWorld`).
	if (Map->GetRuntimePhase() == EElysiumMapRuntimePhase::Failed)
	{
		UE_LOG(LogElysiumArenaStage, Warning, TEXT("%s: rebuilding over a Failed stage (%s)"), *Record.Name,
			*Map->GetRuntimeFailureReason());
		Map->CancelCharacterModelAdmissions();
	}

	FElysiumStageSeat StageSeat;
	StageSeat.FeetCm = Seat.FeetCm;
	StageSeat.YawDeg = Seat.YawDeg;
	StageSeat.bReleaseMovement = true;   // the arena's floor is under the seat
	if (!Map->RebuildStageWorld(MoveTemp(Defs), MoveTemp(Network), StageSeat, OutError))
	{
		return false;
	}
	// The new world's player starts at rest; what the pawn and the session record carried over from the
	// last record is undone before the seat, whose mover reset and camera reseed come next. The barrier
	// re-places the pawn on the same feet and freezes it until `Activate`.
	const FString Released = ResetPlayerState(World, Map->GetEntityWorld());
	if (!Released.IsEmpty())
	{
		UE_LOG(LogElysiumArenaStage, Log, TEXT("%s: player reset, undid: %s"), *Record.Name, *Released);
	}
	FString SeatError;
	if (!SeatPlayerAt(World, Seat.FeetCm, Seat.YawDeg, SeatError))
	{
		UE_LOG(LogElysiumArenaStage, Warning, TEXT("%s: player not seated: %s"), *Record.Name, *SeatError);
	}

	const FElysiumEntityWorld* Rebuilt = Map->GetEntityWorld();
	OutSummary = FString::Printf(
		TEXT("%s: stage world rebuilt through Load -- %d def(s) (%d anchor(s), %d node row(s), %d row(s), ")
		TEXT("%d from_map row(s), %d cast), AI network %d node(s) (%d the record's), %d hint(s) listed; ")
		TEXT("player seat %s yaw %.0f; seed %d; Activate runs at the barrier"),
		*Record.Name, DefCount, Spec.Anchors.Num(), Spec.Nodes.Num(), Record.Rows.Num(), FromMapRows,
		Record.Cast.Num(), Rebuilt != nullptr ? Rebuilt->Places().NumNodes() : 0, RecordNodes,
		Rebuilt != nullptr ? Rebuilt->HintList().Num() : 0, *Seat.FeetCm.ToCompactString(), Seat.YawDeg,
		Record.Seed);
	return true;
}

} // namespace ElysiumArenaStage

#endif // !UE_BUILD_SHIPPING
