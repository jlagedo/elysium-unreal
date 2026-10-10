#include "Misc/ScopeExit.h"
#include "ElysiumEntityWorld.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumPlayer.h"
#include "ElysiumRetailSite.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumEntityWorldShared.h"
#include "Substrate/ElysiumSaveRestoreGame.h" // L0-r029: the block set's chain over this map's rows

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// --- Persistence ---
// The map snapshot. Both halves run against the *same* world the game runs against: "a snapshot
// is produced by exactly the same code path a save uses", so a travel boundary and a Save Game
// call reach Freeze identically.

FElysiumEntityState FElysiumEntityWorld::CaptureState(const FElysiumEntity& E) const
{
	FElysiumEntityState S;
	S.Index = E.Handle.Index;
	S.ClassName = E.Def ? FName(*E.Def->Classname) : NAME_None;
	S.TargetName = E.TargetName;
	S.bRuntime = E.Handle.Index >= Defs.Defs.Num();
	S.bDead = E.bDead;
	S.bHidden = E.bHidden;
	S.bSpawnCalled = E.bSpawnCalled;
	S.bActivateCalled = E.bActivateCalled;
	S.NextThinkSR = E.NextThink <= 0.0f || E.NextThink == MAX_flt ? E.NextThink : 1.0f; // 0x100a9f70 +0x180 FLOAT SAVE
	S.NextThink = static_cast<float>(FElysiumSaveArchive::EncodeTime(E.NextThink, NowSeconds(), EElysiumTimePolicy::Ordinary)); // 0x100a9f70 +0x17c TIME/NextThinkSR FLOAT
	S.SavedNextThink = E.GetSavedNextThink(); // port diagnostic FLOAT, retail saves callback instead
	S.ThinkCallback = E.ThinkCallback; S.SavedThinkCallback = E.SavedThinkCallback; // FUNCTION SAVE
	S.bSavedPhysicalWordsAvailable = E.bSavedPhysicalWordsAvailable; // 0x100a8710
	S.ScriptSavedSolid = E.ScriptSavedSolid; S.ScriptSavedMoveType = E.ScriptSavedMoveType;
	S.ScriptSavedMoveCollide = E.ScriptSavedMoveCollide; S.ScriptSavedSolidFlags = E.ScriptSavedSolidFlags;
	S.ScriptSavedEffects = E.ScriptSavedEffects; // 0x100a8990
	S.OutputTimesRemaining = E.OutputTimesRemaining;
	// The live origin is not a registered field and cannot become one: the def's `origin` key is
	// still the raw Source-space string, and Construct applies every key that has a field, so a
	// registered `origin` would overwrite the converted placement at every spawn. It rides here
	// instead — `point_teleport` and `Entity.SetOrigin` move entities for real.
	S.Origin = E.Origin;

	// The field walk, in the registry's sorted order so two captures of one state agree byte
	// for byte (§8).
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	if (E.Class)
	{
		for (const FName& FieldName : Reg.SaveFields(*E.Class))
		{
			if (const FElysiumFieldAccessor* Acc = Reg.FindField(*E.Class, FieldName))
			{
				if (Acc->Get)
				{
					FElysiumVariant Value = Acc->Get(E); // 0x101a0a80, same type-aware path for every carried row
					if (Acc->PersistenceType == EElysiumPersistenceType::Time)
					{
						Value = FElysiumVariant::Float(static_cast<float>(FElysiumSaveArchive::EncodeTime(Value.ToFloat(), NowSeconds(), Acc->TimePolicy))); // TIME SAVE
					}
					S.Fields.Emplace(FieldName, MoveTemp(Value));
				}
			}
		}
	}

	// The leaf's derived state (§4). A leaf that does not override Serialize writes nothing.
	{
		FMemoryWriter Writer(S.LeafState, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest, NowSeconds(), NowSeconds()); // 0x101a0a80
		const_cast<FElysiumEntity&>(E).Serialize(Ar);
	}
	return S;
}

void FElysiumEntityWorld::CaptureBaseline(int32 Index)
{
	// **The omission baseline is the post-Load state, not the post-Construct state.** The question
	// the rule asks is "would rebuilding this map produce this value anyway?", and a rebuild is
	// Construct *plus* Spawn plus PostSpawn — so the answer has to be taken after all three. Taking
	// it after Construct alone silently drops anything Spawn writes and gameplay later returns to
	// its constructed value: `logic_auto`'s first think is exactly that (Spawn arms it at 0, the
	// think disarms it to never, and never is also the constructed value), and omitting it would
	// re-run every map's ignition on load.
	if (!EntityList.IsValidIndex(Index) || !EntityList[Index])
	{
		return;
	}
	if (Baseline.Num() < EntityList.Num())
	{
		Baseline.SetNum(EntityList.Num());
	}
	Baseline[Index] = CaptureState(*EntityList[Index]);
	Baseline[Index].bCaptured = true;
}

void FElysiumEntityWorld::Freeze(FElysiumMapSnapshot& Out) const
{
	Out = FElysiumMapSnapshot();
	Out.MapName = Defs.MapName;
	Out.DefCount = Defs.Defs.Num();
	Out.FrozenAt = NowSeconds();
	Out.SaveBase = Out.FrozenAt; // 0x101a0a80/engine 0x20097d00
	Out.ComfortTargets = ComfortTargetList;

	for (const TUniquePtr<FElysiumEntity>& EntPtr : EntityList)
	{
		if (!EntPtr)
		{
			continue;
		}
		const FElysiumEntity& E = *EntPtr;

		// The player is the Player block's, not this map's — it travels, and its record already
		// carries everything the entity holds (the hydrate/dehydrate pair).
		if (Player.IsSet() && E.Handle.Index == Player.Index)
		{
			continue;
		}
		// Anything carried out of the map is recorded absent rather than saved here (§5). Nothing
		// answers true for an inventory item that travels with the player; the rule is the mechanism, not a stub.
		if (E.TravelsWithPlayer())
		{
			Out.AbsentEntities.Add(E.Handle.Index);
			continue;
		}
		// **Port rule: a live scripted shot is not written.** Retail's level save loop (`0x101a37c0`)
		// skips only `FCAP_DONT_SAVE`; `FCAP_ACROSS_TRANSITION` (bit 2) picks the changelevel carry
		// list (`0x101c7ff0`), what travels with the player INTO a new level, and this snapshot is the
		// LEVEL's own save and revisit record. The port keys its one exclusion on the camera
		// (`CBaseCineCam::ObjectCaps()` returns 0, RC2.4; the map teardown `FUN_10071970` has already
		// ended the shot on the way out) and records it neither saved nor absent: a map-placed camera
		// comes back from its own def, fresh. Every other entity clearing bit 2 — the script
		// directors (`CCineNPC::ObjectCaps` `0x101a6d20`, story 5 fold A3) — is saved with its level,
		// so a removed director stays gone and a director mid-beat keeps its NPC.
		if ((E.ObjectCaps() & ElysiumEntityCaps::AcrossTransition) == 0
			&& E.AsCameraCinematic() != nullptr)
		{
			continue;
		}

		FElysiumEntityState S = CaptureState(E);
		if (S.bRuntime && E.Def) S.Def = *E.Def; // 0x101a2e40 factory reconstruction
		// 0x101a0a80: retain every row; an omitted TIME/leaf baseline belongs to a different epoch.
		Out.Entities.Add(MoveTemp(S));
	}

	Out.Queue = EventQueue.Pending();
	Out.QueueNextSerial = EventQueue.NextSerialValue();
	Out.QueueLastEnqueue = EventQueue.LastEnqueueValue() - Out.SaveBase; // queue TIME
	for (FElysiumIOEvent& Pending : Out.Queue) Pending.FireTime -= Out.SaveBase; // 0x101a0a80

	Out.Fade = ScreenFade.ToSaved();
	Out.Fade.StartTime -= Out.SaveBase; // TIME, preserve elapsed fade

	Out.Weather = WeatherState;
	Out.Weather.TransitionStart -= Out.SaveBase; // TIME; durations stay FLOAT

	// L0-r029: the engine's save (CSaveRestore::vfunc13 0x20096470) hands the buffer to the registered
	// block set -- CServerGameDLL slot 17 PreSave (0x1011b890), slot 18 Save (0x1011b080), slot 19
	// WriteSaveHeaders + PostSave (0x1011b8b0) -- whose Entities and EventQueue handlers write the rows
	// captured above into the section's stream. Retail runs it at exactly this point of a save and of
	// a departing map's freeze; the tap names the set ("Game", set slot 0) in the entity column.
	{
		FElysiumEntityWorld& TraceWorld = const_cast<FElysiumEntityWorld&>(*this); // the trace sink is debug output, never state
		FElysiumNamedRetailSites Sites(TraceWorld, TEXT("Game"));
		ElysiumSaveRestore::EncodeMapBlocks(Out, HasAiTraceSink() ? &Sites : nullptr);
	}

	UE_LOG(LogElysiumWorld, Log,
		TEXT("froze '%s' at %.3f: %d/%d entity records, %d absent, %d queued, %d block bytes"),
		*Out.MapName, Out.FrozenAt, Out.Entities.Num(), EntityList.Num(),
		Out.AbsentEntities.Num(), Out.Queue.Num(), Out.BlockStream.Num());
}

int32 FElysiumEntityWorld::ApplySnapshot(const FElysiumMapSnapshot& Snapshot)
{
	return ApplySnapshot(Snapshot, NowSeconds()); // engine 0x200975f0 selected map clock
}

int32 FElysiumEntityWorld::ApplySnapshot(const FElysiumMapSnapshot& Snapshot, double RestoreBase, bool bLevelTransition)
{
	if (bActive || bApplyingSnapshot) // 0x20096010 -> 0x2008f2e0 requires a reconstructed world
	{
		UE_LOG(LogElysiumWorld, Warning, TEXT("restore requires a fresh inactive world"));
		return INDEX_NONE;
	}
	LastTickNow = RestoreBase; // headless clock matches explicit destination, engine 0x20097d00
	// Scoped, so an early return below cannot leave the world thinking it is still restoring.
	bApplyingSnapshot = true;
	bLevelTransitionRestore = bLevelTransition; // 0x1011a710 receives old-level presence, not 'is loading'
	ON_SCOPE_EXIT { bApplyingSnapshot = false; bLevelTransitionRestore = false; };
	if (Snapshot.SchemaVersion < FElysiumSaveVersion::MinSupported) // common applier also accepts nameless stage snapshots
	{
		return INDEX_NONE; // refused, distinguish an empty successful snapshot
	}
	if (Snapshot.DefCount != Defs.Defs.Num())
	{
		// The map's `.ents` changed under the save, so record #N is no longer entity #N: every
		// record would land by index on a different entity, and the classname guard below only
		// catches the ones whose class also changed. An `env_sprite` record with `bOn` set lighting
		// the corona next door is the shape of it. Save files are disposable here (CLAUDE.md — we
		// have not released), so the snapshot is refused whole and the map keeps the state its
		// fresh spawn just built, which is the `.ents` this build actually parsed. `bSnapshotApplied`
		// stays false, so Activate takes the ordinary post-Activate omission baseline.
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("snapshot '%s' was frozen against %d defs, this build parsed %d — refused, "
				"the map keeps its fresh spawn state"),
			*Snapshot.MapName, Snapshot.DefCount, Defs.Defs.Num());
		return INDEX_NONE; // refused, distinguish an empty successful snapshot
	}
	bSnapshotApplied = false; // true only after complete decode/post-restore
	SnapshotEntityIndices.Reset();
	// A restore re-runs `CWorld::Precache` (`0x101a2e40`), so the network manager's 0.8 s first
	// think (`0x102f6690`) is armed afresh: the gate re-stamps here (0018 story 5, lane D).
	BuildStamp = NowSeconds();
	bNetworkManagerFirstThinkRun = false;

	// L0-r029: the engine's restore runs the registered block set over the section's stream --
	// CServerGameDLL slot 20 (0x1011b950: set PreRestore, then ReadRestoreHeaders reads the two lengths,
	// the directory and each found block's headers) and, on an ordinary load (CSaveRestore::vfunc9
	// 0x200975f0), slot 21 (0x1011b300: set Restore seeks each found block's body and calls its handler,
	// then PostRestore). A level transition (vfunc10 0x20097d00) calls slot 20 only and the engine
	// reads the entity data itself. The Entities and EventQueue handlers' Restore bodies decode the
	// rows into `Decoded` and apply them here (`ApplyRestoredEntities` / `ApplyRestoredQueue`).
	FElysiumMapSnapshot Decoded;
	int32 Applied = INDEX_NONE;
	{
		FElysiumNamedRetailSites Sites(*this, TEXT("Game"));
		Applied = ElysiumSaveRestore::RestoreMapBlocks(Snapshot, *this, Decoded, RestoreBase, bLevelTransition,
			HasAiTraceSink() ? &Sites : nullptr);
	}
	if (Applied == INDEX_NONE) return INDEX_NONE; // a refused row or a stream that did not read back: no partial decode

	ComfortTargetList.Reset();
	for (const FElysiumEntityHandle& Saved : Decoded.ComfortTargets)
	{
		const FElysiumEntityHandle Target = RebaseHandle(Saved);
		if (Target.IsSet()) { ComfortTargetList.Add(Target); }
	}
	ScreenFade = FScreenFade::FromSaved(Decoded.Fade);
	ScreenFade.StartTime += RestoreBase; // 0x101a2a30
	WeatherState = Decoded.Weather;
	WeatherState.TransitionStart += RestoreBase; // 0x101a2a30
	WeatherState.Tick(NowSeconds());
	PublishWetness();

	// 0x1011a620: reversed restored list, only after all decode/fixup/queue work.
	for (int32 Row = Decoded.Entities.Num() - 1; Row >= 0; --Row)
	{
		const int32 RestoredIndex = Decoded.Entities[Row].Index;
		if (!SnapshotEntityIndices.Contains(RestoredIndex) || Decoded.AbsentEntities.Contains(RestoredIndex)) continue;
		FElysiumEntity& Restored = *EntityList[RestoredIndex];
		if (FElysiumCombatCharacter* Character = Restored.AsCombatCharacter()) // 0x10348890 V4c team registration
		{
			if (!Character->TeamName.IsEmpty()) Character->AddToTeam(Character->TeamName);
			if (&Restored == FindPlayer()) Character->AddToTeam(TEXT("player")); // 0x1016ebd0
		}
		Restored.OnPostRestore(*this); // 0x1027bf50/0x102998c0, later callback/deadline writes WIN
		Restored.OnRuntimeTransformChanged(); // 0x100aa140 POSITION physical reconnect after coherent words
		Restored.OnDormancyChanged(); // reconnect presentation after coherent restore
		NotifyVisualChanged(Restored);
	}
	bSnapshotApplied = true; // 0x1011a620 complete restoration
	if (OnSnapshotApplied) OnSnapshotApplied(); // exact applied-before-think witness

	UE_LOG(LogElysiumWorld, Log,
		TEXT("applied snapshot '%s': %d/%d entity records, %d absent, %d queued"),
		*Decoded.MapName, Applied, Decoded.Entities.Num(), Decoded.AbsentEntities.Num(),
		Decoded.Queue.Num());
	return Applied;
}

int32 FElysiumEntityWorld::ApplyRestoredEntities(const FElysiumMapSnapshot& Snapshot, double RestoreBase)
{
	// The Entities block's restore half (L0-r029): called from the block set's slot 7 dispatch of the
	// `Entities` handler, or from the engine-side transition path. Everything below is the port's
	// existing apply, unchanged in order.
	SnapshotEntityIndices.Reset();

	// Pass 1 — re-create the runtime-spawned entities (npc_maker.Spawn, CreateEntityNoSpawn) from the
	// defs that ride along, in index order, so every one lands back on its saved index. They do land
	// there: the def array fills 0..DefCount-1, the player takes DefCount (SpawnPlayer runs
	// immediately after Load, on every load), and the rest were appended in exactly this order.
	// Creating and spawning them here, before anything is restored, gives them the same
	// "built, then edited by the snapshot" shape the def-array entities already have.
	TArray<const FElysiumEntityState*> RuntimeRecords; // 0x101a2e40 identity table before fields
	for (const FElysiumEntityState& Record : Snapshot.Entities)
		if (Record.bRuntime) RuntimeRecords.Add(&Record);
	RuntimeRecords.Sort([](const FElysiumEntityState& Left, const FElysiumEntityState& Right) { return Left.Index < Right.Index; });
	TArray<FElysiumEntityHandle> NewRuntimeEntities;
	for (const FElysiumEntityState* RuntimeRecord : RuntimeRecords)
	{
		if (EntityList.IsValidIndex(RuntimeRecord->Index) && EntityList[RuntimeRecord->Index]) continue;
		if (RuntimeRecord->Index < EntityList.Num()) return INDEX_NONE; // invalid overlapping identity
		// 0x101a2e40: skipped saved identities remain missing; never slide later handles into their slots.
		EntityList.SetNum(RuntimeRecord->Index); // camera/transition exclusions can leave stable-index holes
		const FElysiumEntityHandle RuntimeHandle = CreateRuntimeEntityNoSpawn(RuntimeRecord->Def);
		if (!RuntimeHandle.IsSet()) return INDEX_NONE;
		NewRuntimeEntities.Add(RuntimeHandle);
	}
	for (const FElysiumEntityHandle& RuntimeHandle : NewRuntimeEntities)
	{
		if (FElysiumEntity* Fresh = Resolve(RuntimeHandle)) CallEntitySpawn(*Fresh); // 0x101a2e40 second pass
	}

	int32 Applied = 0;
	for (const FElysiumEntityState& S : Snapshot.Entities)
	{
		if (!ApplyEntityRecord(S, Snapshot.MapName, Snapshot.SchemaVersion, Snapshot.SaveBase, RestoreBase))
		{
			continue;
		}
		SnapshotEntityIndices.Add(S.Index);
		++Applied;
	}

	// Entities carried out with the player are not here any more (§5): kill them, which is exactly
	// what a resolve against them already reports and what gates their body. Last, so a stale record
	// for the same index cannot resurrect one — the absent set is the later statement about it.
	for (int32 Absent : Snapshot.AbsentEntities)
	{
		SnapshotEntityIndices.Add(Absent);
		if (FElysiumEntity* E = Resolve(FElysiumEntityHandle(Absent, Epoch)))
		{
			E->Kill();
		}
	}

	if (Applied != Snapshot.Entities.Num()) return INDEX_NONE; // no partial decode success fence

	// 0x101a2e40: every identity and word exists before reference rebasing or virtual OnRestore.
	if (OnSnapshotDecoded) OnSnapshotDecoded();
	const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
	for (const FElysiumEntityState& Record : Snapshot.Entities)
	{
		if (!SnapshotEntityIndices.Contains(Record.Index)) continue;
		FElysiumEntity* Restored = EntityList[Record.Index].Get();
		if (Restored->Class)
		{
			for (const auto& Field : Record.Fields)
			{
				const FElysiumFieldAccessor* Accessor = Registry.FindField(*Restored->Class, Field.Key);
				if (Field.Value.IsHandle() && Accessor && Accessor->bSave && Accessor->Set)
					Accessor->Set(*Restored, FElysiumVariant::Handle(RebaseHandle(Field.Value.AsHandle))); // EHANDLE SAVE
			}
		}
		Restored->RebaseSavedReferences(*this); // leaf EHANDLE fixup, no pointer archive
	}

	// An inventory's handle list is a CACHE of what the items' own Save-flagged fields say, so
	// it is re-derived rather than serialized twice. It runs here, after every record has landed and
	// every dead flag is final, because an item may restore either side of the character carrying it.
	for (const TUniquePtr<FElysiumEntity>& Candidate : EntityList)
	{
		if (FElysiumCombatCharacter* Char = Candidate ? Candidate->AsCombatCharacter() : nullptr)
		{
			Char->Inventory.RebuildFrom(*Char);
		}
	}

	// Rebind the map-epoch relationship after runtime entities and their dead flags are final. A
	// live saved controller keeps its stable runtime index; a removed/dead one resolves to nothing.
	PlayerControllerEntity = FElysiumEntityHandle::Invalid();
	for (const TUniquePtr<FElysiumEntity>& Candidate : EntityList)
	{
		if (Candidate && !Candidate->IsDead() && Candidate->Def
			&& (Candidate->Def->Classname.Equals(TEXT("npc_VPlayerController"), ESearchCase::IgnoreCase)
				|| Candidate->Def->Classname.Equals(TEXT("npc_VFrenzyShadow"), ESearchCase::IgnoreCase)
				|| Candidate->Def->Classname.Equals(TEXT("npc_VWolfMorph"), ESearchCase::IgnoreCase)))
		{
			PlayerControllerEntity = Candidate->Handle;
			break;
		}
	}
	return Applied;
}

void FElysiumEntityWorld::ApplyRestoredQueue(const FElysiumMapSnapshot& Snapshot, double RestoreBase)
{
	// The EventQueue block's restore half (L0-r029): the `EventQueue` handler's slot 7 body, after the
	// Entities handler's (retail's registration order), or the engine-side transition path.
	// The queue is REPLACED: this load's Spawn pass has already queued its own openers, and the
	// saved queue is the one that was actually pending.
	EventQueue.Reset();
	for (const FElysiumIOEvent& Saved : Snapshot.Queue)
	{
		FElysiumIOEvent E = Saved;
		// §6 — the handles inside an event carry a dead epoch. Re-stamp them against this world; an
		// index that no longer resolves reads Invalid, the same falsy value a killed entity produces.
		E.Activator = RebaseHandle(Saved.Activator);
		E.Caller    = RebaseHandle(Saved.Caller);
		E.FireTime += RestoreBase; // 0x101a2a30 delayed I/O fires once at destination delay
		EventQueue.AddRestored(MoveTemp(E));
	}
	EventQueue.SetNextSerial(Snapshot.QueueNextSerial);
	// AddRestored deliberately skips the backward-clock guard — a restored deadline is already
	// absolute against the restored clock. The guard's own state comes back with it, so the first
	// live enqueue after the load compares against the time the save was written at.
	EventQueue.SetLastEnqueue(Snapshot.QueueLastEnqueue + RestoreBase); // TIME guard
}

bool FElysiumEntityWorld::ApplyEntityRecord(const FElysiumEntityState& S,
	const FString& SnapshotMapName, int32 LeafSchemaVersion, double SaveBase, double RestoreBase)
{
	FElysiumEntity* E = EntityList.IsValidIndex(S.Index) ? EntityList[S.Index].Get() : nullptr;
	if (!E)
	{
		return false;
	}
	if (!S.ClassName.IsNone() && E->Def && FName(*E->Def->Classname) != S.ClassName)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("snapshot '%s': #%d is %s here but was %s when saved — skipped"),
			*SnapshotMapName, S.Index, *E->Def->Classname, *S.ClassName.ToString());
		return false;
	}

	// Fields first, so a leaf's Serialize sees the restored keyfields. Matched by name, never by
	// position: a field added to a base class does not invalidate an existing payload, and a name
	// this build no longer registers is skipped with a warning rather than failing the load.
	if (E->Class)
	{
		const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		for (const TPair<FName, FElysiumVariant>& F : S.Fields)
		{
			const FElysiumFieldAccessor* Acc = Reg.FindField(*E->Class, F.Key);
			if (!Acc || !Acc->bSave || !Acc->Set)
			{
				UE_LOG(LogElysiumWorld, Warning,
					TEXT("snapshot '%s': #%d has no saved field '%s' in this build — skipped"),
					*SnapshotMapName, S.Index, *F.Key.ToString());
				continue;
			}
			if (F.Key == FName(TEXT("targetname")))
			{
				continue;   // the name index has to be re-keyed; handled below, once
			}
			if (F.Value.IsHandle()) continue; // EHANDLE fixup waits for final dead/missing flags, 0x101a2e40
			const FElysiumVariant Value = Acc->PersistenceType == EElysiumPersistenceType::Time
				? FElysiumVariant::Float(static_cast<float>(FElysiumSaveArchive::DecodeTime(F.Value.ToFloat(), RestoreBase, Acc->TimePolicy)))
				: F.Value; // 0x101a2a30; FLOAT stays raw
			Acc->Set(*E, Value);
		}
	}

	if (E->TargetName != S.TargetName)
	{
		RenameEntity(*E, S.TargetName);
	}

	// Through the runtime writer, so a leaf's body follows the restored position exactly as it
	// does for a live `point_teleport` — the body was built at the def origin by Spawn().
	if (!E->Origin.Equals(S.Origin, 0.0f))
	{
		E->Origin = S.Origin; // POSITION SAVE, 0x100aa140; physical reconnect follows post-restore
	}

	E->bSpawnCalled = S.bSpawnCalled;
	// Activate is part of entity lifecycle, not presentation reconstruction. Replaying it after
	// restoring a live record re-arms NPC admission and replaces the saved schedule/leaf state.
	// Records which omit this field came from an older active-map payload and default to true.
	// point_teleport is the one class whose activation-derived cache must persist; before that
	// cache is absent from the leaf blob, Activate rebuilds it from the restored live transform.
	// Skipping that rebuild on an empty leaf would drop the cache.
	E->bActivateCalled = S.bActivateCalled
		&& !(E->ActivationStateMustPersist() && S.LeafState.IsEmpty());
	E->NextThink = S.NextThinkSR <= 0.0f || S.NextThinkSR == MAX_flt ? S.NextThinkSR
		: static_cast<float>(FElysiumSaveArchive::DecodeTime(S.NextThink, RestoreBase, EElysiumTimePolicy::Ordinary)); // +0x17c TIME
	E->SetSavedNextThink(S.SavedNextThink); // diagnostic FLOAT
	E->ThinkCallback = S.ThinkCallback; E->SavedThinkCallback = S.SavedThinkCallback; // FUNCTION SAVE
	E->bSavedPhysicalWordsAvailable = S.bSavedPhysicalWordsAvailable;
	E->ScriptSavedSolid = S.ScriptSavedSolid; E->ScriptSavedMoveType = S.ScriptSavedMoveType;
	E->ScriptSavedMoveCollide = S.ScriptSavedMoveCollide; E->ScriptSavedSolidFlags = S.ScriptSavedSolidFlags;
	E->ScriptSavedEffects = S.ScriptSavedEffects; // 0x100a8990
	if (S.OutputTimesRemaining.Num() == E->OutputTimesRemaining.Num())
	{
		E->OutputTimesRemaining = S.OutputTimesRemaining;
	}
	else
	{
		// A cardinality change only happens when this build's def carries a different output row
		// count than the one the snapshot was taken against (R3.4/MP-2.4: a def edit, or the
		// datamap-output-typing divergence changing which keyvalues became rows). Restoring
		// anyway would misalign every row after the split, so the live def's freshly-seeded
		// counters are kept instead — loud, because a silent drop here is exactly the "reads clean,
		// countdowns are wrong" bug this gate exists to catch.
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("snapshot '%s': #%d has %d saved output rows but this build's def has %d — ")
			TEXT("output countdowns were not restored"),
			*SnapshotMapName, S.Index, S.OutputTimesRemaining.Num(), E->OutputTimesRemaining.Num());
	}

	bool bLeafDecoded = true; // 0x101a2e40 coherent apply refuses partial leaves
	if (S.LeafState.Num() > 0)
	{
		FMemoryReader Reader(S.LeafState, /*bIsPersistent*/ true);
		// The schema the blob was WRITTEN at, not the one this build writes. A leaf that gates a
		// field on its own version can only be right if the archive it reads through reports the
		// writer's version; handing it `Latest` makes every such gate read true and consumes bytes
		// the writer never emitted.
		FElysiumSaveArchive Ar(Reader, LeafSchemaVersion, SaveBase, RestoreBase); // 0x101a2a30
		E->Serialize(Ar);
		// A blob that ran short, or one whose fields no longer line up, sets the archive's error
		// flag. It must never restore quietly: what comes back is a half-read entity whose remaining
		// fields hold whatever the overrun produced.
		if (Ar.IsError() || Reader.Tell() != S.LeafState.Num())
		{
			bLeafDecoded = false; // no success fence for partial decoding
			UE_LOG(LogElysiumWorld, Error,
				TEXT("snapshot '%s': the leaf state of #%d %s(%s) failed to read (%d bytes at schema "
					"v%d) — the entity is restored only as far as the read got"),
				*SnapshotMapName, S.Index, *S.TargetName, *S.ClassName.ToString(),
				S.LeafState.Num(), LeafSchemaVersion);
		}
	}

	E->bHidden = S.bHidden; E->bDead = S.bDead; // all words first, 0x101a2e40
	return bLeafDecoded; // hook dispatch belongs to the world fence

}

FElysiumEntityHandle FElysiumEntityWorld::RebaseHandle(const FElysiumEntityHandle& Saved) const
{
	if (!Saved.IsSet() || !EntityList.IsValidIndex(Saved.Index)
		|| !EntityList[Saved.Index] || EntityList[Saved.Index]->IsDead()) // 0x101a2e40 invalid before validation
	{
		return FElysiumEntityHandle::Invalid();
	}
	return FElysiumEntityHandle(Saved.Index, Epoch);
}

void FElysiumEntityWorld::Detach()
{
	Player = FElysiumEntityHandle::Invalid();
	bDetached = true;
}
