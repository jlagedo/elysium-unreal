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
#include "ElysiumMapSubsystem.h"
#include "ElysiumSaveArchive.h"
#include "Engine/GameInstance.h"
#include "ElysiumMapEntities.h"             // from_map: the baked DA_<map>_Entities reader
#include "ElysiumMapPlaces.h"               // FElysiumPlaceRow, the network handed to the rebuild
#include "ElysiumMoveSolve.h"               // ElysiumMove::U, for a moved row's Source `origin`
#include "ElysiumMovementComponent.h"
#include "ElysiumPlayer.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumPlayerController.h"
#include "ElysiumRng.h"
#include "Engine/Engine.h"
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


// Harness envelope maps carry provenance through the NORMAL payload codec, never gameplay Load.
// Retail save/restore consumer: 0x1027bc60/0x1027c160; post-restore fence: 0x1011a620.
namespace ElysiumArenaStageTransport
{
	TWeakPtr<ElysiumArenaStage::FTransport> Active;
	ElysiumArenaStage::FHostAdapter Adapter;
	uint64 NextOperation = uint64(1) << 63;
	const TCHAR* const SnapshotName = TEXT("__arena_checkpoint_v6");
	const TCHAR* const EnvelopeName = TEXT("__arena_provenance_v6");
	FString Number(double Value) { return FString::Printf(TEXT("%.17g"), Value); }

	void EncodeEnvelope(const ElysiumArenaStage::FTransport& Transport, FElysiumSavePayload& Payload)
	{
		FElysiumMapSnapshot Envelope;
		Envelope.MapName = EnvelopeName;
		for (const FElysiumEntityDef& Def : Transport.Defs.Defs)
		{
			FElysiumEntityState& Row = Envelope.Entities.AddDefaulted_GetRef();
			Row.bRuntime = true;
			Row.Index = Envelope.Entities.Num() - 1;
			Row.Def = Def;
			// The ordinary runtime-def codec omits these annotations; envelope preserves them.
			// Harness provenance only, removed before Load (0x101a2e40).
			Row.Def.Keys.Add(TEXT("__arena_source_class"), Def.SourceClassname);
			Row.Def.Keys.Add(TEXT("__arena_brush_mesh"), Def.BrushMesh);
			Row.Def.Keys.Add(TEXT("__arena_cull"), Number(Def.CullMaxCm));
			Row.Def.Keys.Add(TEXT("__arena_floor_count"), LexToString(Def.ElevatorFloors.Num()));
			for (int32 FloorIndex = 0; FloorIndex < Def.ElevatorFloors.Num(); ++FloorIndex)
				Row.Def.Keys.Add(FString::Printf(TEXT("__arena_floor_%d"), FloorIndex), Number(Def.ElevatorFloors[FloorIndex]));
		}
		Envelope.DefCount = Transport.Defs.Num();
		for (const FElysiumPlaceRow& Node : Transport.Network)
		{
			FElysiumEntityState& Row = Envelope.Entities.AddDefaulted_GetRef();
			Row.bRuntime = true;
			Row.Index = Envelope.Entities.Num() - 1;
			Row.Def.Classname = TEXT("__arena_network_v6");
			Row.Def.Origin = Node.OriginCm;
			Row.Def.Keys.Add(TEXT("index"), LexToString(Node.NetworkIndex));
			Row.Def.Keys.Add(TEXT("type"), LexToString(Node.Type));
			Row.Def.Keys.Add(TEXT("flags"), LexToString(Node.Flags));
			Row.Def.Keys.Add(TEXT("yaw"), Number(Node.YawDeg));
			Row.Def.Keys.Add(TEXT("wc"), LexToString(Node.WcId));
			Row.Def.Keys.Add(TEXT("hint"), LexToString(Node.HintBspIndex));
			for (int32 HullIndex = 0; HullIndex < 22; ++HullIndex)
				Row.Def.Keys.Add(FString::Printf(TEXT("z%d"), HullIndex), Number(Node.ZOffsetCm[HullIndex]));
		}
		FElysiumEntityState& Seat = Envelope.Entities.AddDefaulted_GetRef();
		Seat.bRuntime = true;
		Seat.Index = Envelope.Entities.Num() - 1;
		Seat.Def.Classname = TEXT("__arena_seat_v6");
		Seat.Def.Origin = Transport.SeatFeet;
		Seat.Def.Keys.Add(TEXT("yaw"), Number(Transport.SeatYaw));
		Seat.Def.Keys.Add(TEXT("sky_scale"), Number(Transport.Defs.SkyScale));
		Seat.Def.Keys.Add(TEXT("sky_origin"), Transport.Defs.SkyOrigin.ToString());
		Payload.Maps.Add(EnvelopeName, MoveTemp(Envelope));
	}

	bool DecodeEnvelope(const FElysiumSavePayload& Payload, ElysiumArenaStage::FTransport& Transport, FString& Error)
	{
		const FElysiumMapSnapshot* Envelope = Payload.Maps.Find(EnvelopeName);
		if (!Envelope || Envelope->DefCount < 0 || Envelope->Entities.Num() <= Envelope->DefCount)
		{ Error = TEXT("missing or malformed arena provenance"); return false; }
		Transport.Defs = FElysiumEntityDefs();
		Transport.Network.Reset();
		for (int32 DefIndex = 0; DefIndex < Envelope->DefCount; ++DefIndex)
		{
			const FElysiumEntityState& Row = Envelope->Entities[DefIndex];
			if (!Row.bRuntime || Row.Index != DefIndex || Row.Def.Classname.IsEmpty())
			{ Error = TEXT("invalid arena def provenance"); return false; }
			FElysiumEntityDef Def = Row.Def;
			Def.SourceClassname = Def.Keys.FindRef(TEXT("__arena_source_class"));
			Def.BrushMesh = Def.Keys.FindRef(TEXT("__arena_brush_mesh"));
			Def.CullMaxCm = FCString::Atof(*Def.Keys.FindRef(TEXT("__arena_cull")));
			const int32 FloorCount = FCString::Atoi(*Def.Keys.FindRef(TEXT("__arena_floor_count")));
			if (FloorCount < 0 || FloorCount > 8) { Error = TEXT("invalid arena floor provenance"); return false; }
			for (int32 FloorIndex = 0; FloorIndex < FloorCount; ++FloorIndex)
			{
				const FString Key = FString::Printf(TEXT("__arena_floor_%d"), FloorIndex);
				Def.ElevatorFloors.Add(FCString::Atof(*Def.Keys.FindRef(Key)));
				Def.Keys.Remove(Key);
			}
			for (const TCHAR* Key : { TEXT("__arena_source_class"), TEXT("__arena_brush_mesh"), TEXT("__arena_cull"), TEXT("__arena_floor_count") }) Def.Keys.Remove(Key);
			Transport.Defs.Defs.Add(MoveTemp(Def));
		}
		for (int32 NodeIndex = Envelope->DefCount; NodeIndex < Envelope->Entities.Num() - 1; ++NodeIndex)
		{
			const FElysiumEntityDef& Def = Envelope->Entities[NodeIndex].Def;
			if (Def.Classname != TEXT("__arena_network_v6") || Def.Keys.Num() != 28)
			{ Error = TEXT("invalid arena network provenance"); return false; }
			FElysiumPlaceRow Node;
			Node.OriginCm = Def.Origin;
			Node.NetworkIndex = FCString::Atoi(*Def.Keys.FindRef(TEXT("index")));
			Node.Type = FCString::Atoi(*Def.Keys.FindRef(TEXT("type")));
			Node.Flags = FCString::Atoi(*Def.Keys.FindRef(TEXT("flags")));
			Node.YawDeg = FCString::Atof(*Def.Keys.FindRef(TEXT("yaw")));
			Node.WcId = FCString::Atoi(*Def.Keys.FindRef(TEXT("wc")));
			Node.HintBspIndex = FCString::Atoi(*Def.Keys.FindRef(TEXT("hint")));
			for (int32 HullIndex = 0; HullIndex < 22; ++HullIndex)
				Node.ZOffsetCm[HullIndex] = FCString::Atof(*Def.Keys.FindRef(FString::Printf(TEXT("z%d"), HullIndex)));
			Transport.Network.Add(Node);
		}
		const FElysiumEntityDef& Seat = Envelope->Entities.Last().Def;
		if (Seat.Classname != TEXT("__arena_seat_v6")) { Error = TEXT("invalid arena seat provenance"); return false; }
		Transport.SeatFeet = Seat.Origin;
		Transport.SeatYaw = FCString::Atof(*Seat.Keys.FindRef(TEXT("yaw")));
		Transport.Defs.SkyScale = FCString::Atof(*Seat.Keys.FindRef(TEXT("sky_scale")));
		Transport.Defs.SkyOrigin.InitFromString(Seat.Keys.FindRef(TEXT("sky_origin")));
		return true;
	}
}

namespace ElysiumArenaStage
{
void SetHostAdapter(FHostAdapter InAdapter) { ElysiumArenaStageTransport::Adapter = MoveTemp(InAdapter); }
void ConfigureHost(FHost& Host)
{
	if (!Host.Transport) Host.Transport = MakeShared<FTransport>();
	if (ElysiumArenaStageTransport::Adapter) ElysiumArenaStageTransport::Adapter(Host);
}
FTransport::~FTransport() { Cancel(); }
void FTransport::Cancel()
{
	if (UElysiumSessionSubsystem* State = Session.Get())
		if (ResultHandle.IsValid()) State->OnSaveResult().Remove(ResultHandle);
	ResultHandle.Reset();
	Observer = nullptr;
	bPending = false;
}
void FTransport::Emit(EFence Phase, AElysiumMapActor* Map, const FString& Reason)
{
	FTransactionFence Fence;
	Fence.OperationId = OperationId;
	Fence.Phase = Phase;
	Fence.Map = Map;
	Fence.World = Map ? Map->GetWorld() : nullptr;
	Fence.MapIdentity = bArena ? TEXT("arena") : Map ? Map->MapName : ExpectedMap;
	Fence.Reason = Reason;
	if (Phase == EFence::Applied) bAppliedSent = true;
	if (Phase == EFence::Written || Phase == EFence::Ready || Phase == EFence::Failed) bPending = false;
	// Observer may cancel/destroy bindings; invoke a private copy (0x1011a620 synchronous fence).
	const auto Notify = Observer;
	if (Notify) Notify(Fence);
}
void NotifyWorldConstructed(AElysiumMapActor* Map)
{
	const TSharedPtr<FTransport> Transport = ElysiumArenaStageTransport::Active.Pin();
	if (!Transport || !Transport->bPending || !Transport->bLoading || !Map) return;
	if (!Transport->bArena && !Transport->ExpectedMap.IsEmpty() && Transport->ExpectedMap != Map->MapName)
	{ Transport->Emit(EFence::Failed, Map, TEXT("wrong map at world construction")); return; }
	Transport->PendingMap = Map;
	Transport->Emit(EFence::Rebinding, Map);
	if (FElysiumEntityWorld* World = Map->GetEntityWorld())
	{
		const TWeakPtr<FTransport> Weak = Transport;
		const auto PreviousApplied = World->OnSnapshotApplied;
		World->OnSnapshotApplied = [Weak, Map, PreviousApplied]()
		{
			if (PreviousApplied) PreviousApplied();
			if (const TSharedPtr<FTransport> Live = Weak.Pin())
				if (Live->bPending && !Live->bAppliedSent) Live->Emit(EFence::Applied, Map);
		};
	}
}
void NotifyWorldApplied(AElysiumMapActor* Map)
{
	if (const TSharedPtr<FTransport> Transport = ElysiumArenaStageTransport::Active.Pin())
		if (Transport->bPending && Transport->bLoading && !Transport->bArena && Transport->PendingMap.Get() == Map && !Transport->bAppliedSent)
			Transport->Emit(EFence::Applied, Map);
}
void FTransport::Poll()
{
	if (!bPending || !bLoading) return;
	AElysiumMapActor* Map = PendingMap.Get();
	if (!Map) return;
	if (Map->GetRuntimePhase() == EElysiumMapRuntimePhase::Failed)
	{ Emit(EFence::Failed, Map, Map->GetRuntimeFailureReason()); return; }
	if (Map->GetRuntimePhase() == EElysiumMapRuntimePhase::Active)
	{
		if (!bAppliedSent) { Emit(EFence::Failed, Map, TEXT("ready without pre-think applied fence")); return; }
		Emit(EFence::Ready, Map);
	}
}

bool FTransport::Begin(const FHost& Host, const FElysiumArenaAction& Action,
	TFunction<void(const FTransactionFence&)> InObserver, FString& OutError)
{
	if (bPending || Storage.IsWriting()) { OutError = TEXT("arena persistence operation in flight"); return false; }
	UGameInstance* Instance = Host.GetWorld() ? Host.GetWorld()->GetGameInstance() : nullptr;
	UElysiumSessionSubsystem* State = Instance ? Instance->GetSubsystem<UElysiumSessionSubsystem>() : nullptr;
	UElysiumMapSubsystem* Maps = Instance ? Instance->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!State || !Maps || !Host.GetEntityWorld()) { OutError = TEXT("no real harness session/world"); return false; }
	Cancel();
	Session = State;
	Observer = MoveTemp(InObserver);
	bPending = true;
	bLoading = Action.Do != EElysiumArenaAction::Save;
	bArena = Host.bArena;
	bAppliedSent = false;
	OperationId = ++ElysiumArenaStageTransport::NextOperation;
	PendingMap = Host.Map;
	if (bLoading) PendingMap.Reset(); // no stale source map during replacement, 0x101a2e40
	ElysiumArenaStageTransport::Active = AsShared();
	const FString Slot = TEXT("__arena_v6_") + Action.Slot;
	ExpectedMap = Action.Map;
	if (!Host.bArena)
	{
		if (Action.Do == EElysiumArenaAction::FreshMap || Action.Do == EElysiumArenaAction::Travel)
		{
			const bool bAccepted = Action.Do == EElysiumArenaAction::FreshMap ? Maps->FreshLoad(Action.Map) : Maps->Travel(Action.Map, Action.Landmark);
			if (!bAccepted) { OutError = TEXT("map travel refused"); Emit(EFence::Failed, Host.GetMap(), OutError); }
			return bAccepted;
		}
		if (bLoading)
		{
			FElysiumSaveHeaderData Header;
			if (State->ReadSlotHeader(Slot, Header)) ExpectedMap = Header.Map;
		}
		OperationId = 0; // latch actual session id at its first synchronous result
		const TWeakPtr<FTransport> Weak = AsShared();
		ResultHandle = State->OnSaveResult().AddLambda([Weak, Slot](const FElysiumSaveResult& Result)
		{
			const TSharedPtr<FTransport> Live = Weak.Pin();
			if (!Live || !Live->bPending || Result.Slot != Slot) return;
			if (Live->OperationId == 0) Live->OperationId = Result.OperationId;
			if (Live->OperationId != Result.OperationId) return;
			UElysiumSessionSubsystem* Current = Live->Session.Get();
			if (!Current) return;
			if (Result.State == EElysiumSaveOperationState::Failed) { Live->Emit(EFence::Failed, Live->PendingMap.Get(), Result.Error); return; }
			if (!Live->bLoading && Result.State == EElysiumSaveOperationState::Written) { Live->Emit(EFence::Written, Live->PendingMap.Get()); return; }
			const EElysiumPersistencePhase Phase = Current->LastPersistencePhase();
			if (!Live->bLoading && Phase == EElysiumPersistencePhase::Captured && Result.State == EElysiumSaveOperationState::Capturing)
				Live->Emit(EFence::Captured, Live->PendingMap.Get());
			if (Live->bLoading && Phase == EElysiumPersistencePhase::Applied && !Live->bAppliedSent)
				Live->Emit(EFence::Applied, Live->PendingMap.Get());
			if (Live->bLoading && Phase == EElysiumPersistencePhase::Ready) Live->Poll();
		});
		FString WrittenSlot;
		// The observer is armed above before the direct public alias. This proves the real
		// console route, including inline refusal, rather than counting command enqueue as load.
		const bool bAccepted = bLoading ? (GEngine && GEngine->Exec(Host.GetWorld(), *FString::Printf(TEXT("elysium.load %s"), *Slot)))
			: State->RequestSave({ EElysiumSaveKind::Manual, Slot }, WrittenSlot, OutError);
		if (!bAccepted && bPending) Emit(EFence::Failed, Host.GetMap(), OutError);
		return bAccepted;
	}
	// Green Room codec/storage is explicit harness provenance; production baked-map gate untouched.
	if (Action.Do == EElysiumArenaAction::Save)
	{
		if (!State->CanSave(OutError)) { Emit(EFence::Failed, Host.GetMap(), OutError); return false; }
		FElysiumSavePayload Payload;
		// BuildPayload fills every ordinary block before its final empty-map refusal.
		if (!State->BuildPayload(Payload, OutError) && OutError != TEXT("no current map to save"))
		{ Emit(EFence::Failed, Host.GetMap(), OutError); return false; }
		OutError.Reset();
		FElysiumMapSnapshot Snapshot;
		Host.GetEntityWorld()->Freeze(Snapshot);
		Snapshot.MapName = ElysiumArenaStageTransport::SnapshotName;
		Payload.World.CurrentMap = Snapshot.MapName;
		Payload.Maps.Add(Snapshot.MapName, MoveTemp(Snapshot));
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			if (Def.InternalFactory) { OutError = TEXT("native internal factory cannot be checkpoint provenance"); Emit(EFence::Failed, Host.GetMap(), OutError); return false; }
			for (const TPair<FString, FString>& Key : Def.Keys)
				if (Key.Key.StartsWith(TEXT("__arena_"))) { OutError = TEXT("reserved arena provenance key collision"); Emit(EFence::Failed, Host.GetMap(), OutError); return false; }
		}
		ElysiumArenaStageTransport::EncodeEnvelope(*this, Payload);
		Emit(EFence::Captured, Host.GetMap());
		if (!bPending) { OutError = TEXT("capture witness refused"); return false; }
		const TWeakPtr<FTransport> Weak = AsShared();
		if (!Storage.Write(Slot, EElysiumSaveKind::Manual, Payload, [Weak](bool bSuccess)
		{
			if (const TSharedPtr<FTransport> Live = Weak.Pin())
				if (Live->bPending) Live->Emit(bSuccess ? EFence::Written : EFence::Failed,
					Live->PendingMap.Get(), bSuccess ? FString() : FString(TEXT("native arena slot write failed")));
		}, OutError)) { Emit(EFence::Failed, Host.GetMap(), OutError); return false; }
		return true;
	}
	FElysiumSavePayload Payload;
	if (!Storage.ReadSlotPayload(Slot, Payload, OutError)
		|| Payload.World.CurrentMap != ElysiumArenaStageTransport::SnapshotName
		|| !ElysiumArenaStageTransport::DecodeEnvelope(Payload, *this, OutError))
	{
		if (OutError.IsEmpty()) OutError = TEXT("slot has wrong arena provenance/map");
		Emit(EFence::Failed, Host.GetMap(), OutError); return false;
	}
	FElysiumMapSnapshot* Snapshot = Payload.Maps.Find(ElysiumArenaStageTransport::SnapshotName);
	if (const FString* Corruption = Corruptions.Find(Action.Slot))
	{
		if (!Snapshot || !MutateCheckpoint || !MutateCheckpoint(*Snapshot, *Corruption, OutError))
		{ if (OutError.IsEmpty()) OutError = TEXT("unavailable retail checkpoint-header fixture adapter"); Emit(EFence::Failed, Host.GetMap(), OutError); return false; }
		Snapshot->BlockStream.Reset(); // the rows were edited after their freeze: the block stream is re-encoded from them (L0-r029)
	}
	if (!Snapshot || Snapshot->DefCount != Defs.Num()) { OutError = TEXT("arena snapshot/defs mismatch"); Emit(EFence::Failed, Host.GetMap(), OutError); return false; }
	// Tear down session ownership before applying blocks; reconstruct actual stage admissions.
	State->ApplyPayload(Payload);
	State->TimeControl().ResetClock(bHasRestoreBase && RestoreBaseSlot == Action.Slot ? RestoreBase : Payload.Session.ClockNow);
	FElysiumStageSeat Seat;
	Seat.FeetCm = SeatFeet; Seat.YawDeg = SeatYaw; Seat.bReleaseMovement = true;
	if (Payload.World.bHasPlacement)
	{
		const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(Host.GetWorld());
		if (!Refs) { OutError = TEXT("saved player placement has no admitted body"); Emit(EFence::Failed, Host.GetMap(), OutError); return false; }
		Seat.FeetCm = Payload.World.PlayerOrigin - FVector(0.0, 0.0, Refs.Body->GetBodyHalfHeight());
		Seat.YawDeg = Payload.World.PlayerYaw; // barrier seats at saved pose, never original zero pose (0x200975f0)
	}
	FElysiumEntityDefs RestoredDefs = Defs;
	TArray<FElysiumPlaceRow> RestoredNetwork = Network;
	AElysiumMapActor* Map = Host.GetMap();
	if (!Map->RebuildStageWorld(MoveTemp(RestoredDefs), MoveTemp(RestoredNetwork), Seat, OutError))
	{ Emit(EFence::Failed, Map, OutError); return false; }
	// The integrator hook in RebuildStageWorld installs the sink BEFORE its Load pass.
	// Fallback refuses if absent; late installation cannot claim restoration trace parity.
	if (PendingMap.Get() != Map) { OutError = TEXT("stage rebuild lacks pre-Load world fence"); Emit(EFence::Failed, Map, OutError); return false; }
	FElysiumEntityWorld* World = Map->GetEntityWorld();
	if (!World) { OutError = TEXT("rebuilt stage has no entity world"); Emit(EFence::Failed, Map, OutError); return false; }
	const TWeakPtr<FTransport> RestoreTransport = AsShared();
	const TWeakObjectPtr<AElysiumMapActor> RestoreMap = Map;
	Map->RestoreBeforeActivation = [RestoreTransport, RestoreMap, PayloadCopy = MoveTemp(Payload)]() mutable
	{
		const auto Live = RestoreTransport.Pin(); AElysiumMapActor* PreparedMap = RestoreMap.Get();
		if (!Live || !Live->bPending || !PreparedMap) return false;
		FElysiumEntityWorld* PreparedWorld = PreparedMap->GetEntityWorld();
		const FElysiumMapSnapshot* PreparedSnapshot = PayloadCopy.Maps.Find(ElysiumArenaStageTransport::SnapshotName);
		if (!PreparedWorld || !PreparedSnapshot) { Live->Emit(EFence::Failed, PreparedMap, TEXT("prepared restore lost snapshot/world")); return false; }
		// Native0x101a2e40 has prepared model/collision/navigation inputs before0x1011a620.
		// Body admission draws finish first; then restore the saved stream before OnRestore's reroll.
		ElysiumRng::Restore(PayloadCopy.Session.Rng);
		if (PreparedWorld->ApplySnapshot(*PreparedSnapshot) == INDEX_NONE)
		{ Live->Emit(EFence::Failed, PreparedMap, TEXT("common prepared snapshot apply refused")); return false; }
		return true;
	};
	return bPending;
}


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
	Out.Outputs = Row.Outputs; // stage fixture wires use normal FireOutput/queue/count machinery
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
			if (!Record.Script.IsEmpty() && Record.Script[0].Do == EElysiumArenaAction::FreshMap
				&& Record.Script[0].bAtTime && Record.Script[0].Time == 0.0) break; // validate after its own fresh boundary, not against the predecessor's dead cast
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
			StageSession->TimeControl().ResetClock(1.0); // engine 0x200f5bc4: before entity Load, never restore reseeding
			if (Host.Transport)
			{
				Host.Transport->StageInitialTime = StageSession->GameClock().GetNow();
				Host.Transport->StageInitialDraw = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).GetCurrentSeed();
			}
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
	// Harness envelope retains exact defs/network/seat, not a copy of NPC members (0x1027bc60).
	if (Host.Transport)
	{
		Host.Transport->Defs = Defs;
		Host.Transport->Network = Network;
		Host.Transport->SeatFeet = StageSeat.FeetCm;
		Host.Transport->SeatYaw = StageSeat.YawDeg;
	}
	UE_LOG(LogElysiumArenaStage, Log, TEXT("%s: pre-entity clock=1 epoch=fresh npc_draw=%d"),
		*Record.Name, ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).GetCurrentSeed());
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
