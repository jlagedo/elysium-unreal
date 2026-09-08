#include "ElysiumEntityWorld.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumEntityWorldShared.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// --- Persistence ---
// The map snapshot. Both halves run against the *same* world the game runs against, which is what
// `docs/architecture/save-architecture.md` §5 means by "a snapshot is produced by exactly the same code path a save
// uses": a travel boundary and a Save Game call reach Freeze identically.

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
	S.NextThink = E.NextThink;
	S.SavedNextThink = E.GetSavedNextThink();
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
					S.Fields.Emplace(FieldName, Acc->Get(E));
				}
			}
		}
	}

	// The leaf's derived state (§4). A leaf that does not override Serialize writes nothing.
	{
		FMemoryWriter Writer(S.LeafState, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
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
		// `ObjectCaps() & FCAP_ACROSS_TRANSITION` — the port's transition carry **is** this
		// snapshot, so an entity that clears the bit is simply not written into it. It is not
		// recorded absent either: a map-placed entity must come back from its own def on the next
		// load, fresh, and a runtime one must not come back at all. `CBaseCineCam::ObjectCaps()`
		// returns 0 (RC2.4), so a live scripted shot never rides a `trigger_changelevel` or a save
		// — the map teardown `FUN_10071970` has already ended it on the way out, and this is the
		// same statement from the persistence side.
		if ((E.ObjectCaps() & ElysiumEntityCaps::AcrossTransition) == 0)
		{
			continue;
		}

		FElysiumEntityState S = CaptureState(E);
		const int32 Index = E.Handle.Index;
		const FElysiumEntityState* Base =
			(Baseline.IsValidIndex(Index) && Baseline[Index].bCaptured) ? &Baseline[Index] : nullptr;

		// A runtime entity has no def on disk, so it is always written and its synthesized def rides
		// along — that is the whole of how it restores as itself.
		if (S.bRuntime && E.Def)
		{
			S.Def = *E.Def;
		}

		bool bDiffers = S.bRuntime || Base == nullptr;
		if (Base)
		{
			bDiffers = bDiffers
				|| S.TargetName != Base->TargetName
				|| S.bDead != Base->bDead
				|| S.bHidden != Base->bHidden
				|| S.bSpawnCalled != Base->bSpawnCalled
				|| S.bActivateCalled != Base->bActivateCalled
				|| S.NextThink != Base->NextThink
				|| S.SavedNextThink != Base->SavedNextThink
				|| S.OutputTimesRemaining != Base->OutputTimesRemaining
				|| !S.Origin.Equals(Base->Origin, 0.0f)
				|| S.LeafState != Base->LeafState;

			// Prune the field list to what actually moved. The baseline is sorted the same way, so
			// this is a merge, not a search.
			TArray<TPair<FName, FElysiumVariant>> Changed;
			int32 b = 0;
			for (const TPair<FName, FElysiumVariant>& F : S.Fields)
			{
				while (b < Base->Fields.Num() && Base->Fields[b].Key != F.Key
					&& Base->Fields[b].Key.LexicalLess(F.Key))
				{
					++b;
				}
				const bool bSame = (b < Base->Fields.Num() && Base->Fields[b].Key == F.Key
					&& Base->Fields[b].Value == F.Value);
				if (!bSame)
				{
					Changed.Add(F);
				}
			}
			if (Changed.Num() > 0)
			{
				bDiffers = true;
			}
			S.Fields = MoveTemp(Changed);

			if (S.LeafState == Base->LeafState)
			{
				S.LeafState.Reset();   // a leaf whose derived state has not moved costs 0 bytes
			}
		}

		if (bDiffers)
		{
			Out.Entities.Add(MoveTemp(S));
		}
	}

	Out.Queue = EventQueue.Pending();
	Out.QueueNextSerial = EventQueue.NextSerialValue();
	Out.QueueLastEnqueue = EventQueue.LastEnqueueValue();

	Out.Fade = ScreenFade.ToSaved();
	Out.Weather = WeatherState;

	UE_LOG(LogElysiumWorld, Log,
		TEXT("froze '%s' at %.3f: %d/%d entity records, %d absent, %d queued"),
		*Out.MapName, Out.FrozenAt, Out.Entities.Num(), EntityList.Num(),
		Out.AbsentEntities.Num(), Out.Queue.Num());
}

int32 FElysiumEntityWorld::ApplySnapshot(const FElysiumMapSnapshot& Snapshot)
{
	if (!Snapshot.IsValid())
	{
		return 0;
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
		return 0;
	}
	bSnapshotApplied = true;
	SnapshotEntityIndices.Reset();

	// Pass 1 — re-create the runtime-spawned entities (npc_maker.Spawn, CreateEntityNoSpawn) from the
	// defs that ride along, in index order, so every one lands back on its saved index. They do land
	// there: the def array fills 0..DefCount-1, the player takes DefCount (SpawnPlayer runs
	// immediately after Load, on every load), and the rest were appended in exactly this order.
	// Creating and spawning them here, before anything is restored, gives them the same
	// "built, then edited by the snapshot" shape the def-array entities already have.
	for (const FElysiumEntityState& S : Snapshot.Entities)
	{
		if (!S.bRuntime || EntityList.IsValidIndex(S.Index))
		{
			continue;
		}
		if (S.Index != EntityList.Num())
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("snapshot '%s': runtime entity #%d cannot be placed (world holds %d) — skipped"),
				*Snapshot.MapName, S.Index, EntityList.Num());
			continue;
		}
		const FElysiumEntityHandle H = CreateRuntimeEntityNoSpawn(S.Def);
		if (FElysiumEntity* Fresh = Resolve(H))
		{
			CallEntitySpawn(*Fresh);
		}
	}

	int32 Applied = 0;
	for (const FElysiumEntityState& S : Snapshot.Entities)
	{
		if (!ApplyEntityRecord(S, Snapshot.MapName, Snapshot.SchemaVersion))
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
			&& Candidate->Def->Classname.Equals(TEXT("npc_VPlayerController"), ESearchCase::IgnoreCase))
		{
			PlayerControllerEntity = Candidate->Handle;
			break;
		}
	}

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
		EventQueue.AddRestored(MoveTemp(E));
	}
	EventQueue.SetNextSerial(Snapshot.QueueNextSerial);
	// AddRestored deliberately skips the backward-clock guard — a restored deadline is already
	// absolute against the restored clock. The guard's own state comes back with it, so the first
	// live enqueue after the load compares against the time the save was written at.
	EventQueue.SetLastEnqueue(Snapshot.QueueLastEnqueue);

	ScreenFade = FScreenFade::FromSaved(Snapshot.Fade);
	WeatherState = Snapshot.Weather;
	WeatherState.Tick(NowSeconds());
	PublishWetness();

	UE_LOG(LogElysiumWorld, Log,
		TEXT("applied snapshot '%s': %d/%d entity records, %d absent, %d queued"),
		*Snapshot.MapName, Applied, Snapshot.Entities.Num(), Snapshot.AbsentEntities.Num(),
		Snapshot.Queue.Num());
	return Applied;
}

bool FElysiumEntityWorld::ApplyEntityRecord(const FElysiumEntityState& S,
	const FString& SnapshotMapName, int32 LeafSchemaVersion)
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
			if (F.Value.IsHandle())
			{
				// §6 — a saved handle carries a dead epoch, and re-stamping is this applier's
				// job (the archive drops the epoch by design). An index that no longer exists
				// reads Invalid, the same falsy value a killed entity produces.
				Acc->Set(*E, FElysiumVariant::Handle(RebaseHandle(F.Value.AsHandle)));
				continue;
			}
			Acc->Set(*E, F.Value);
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
		E->SetRuntimeOrigin(S.Origin);
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
	E->NextThink = S.NextThink;
	E->SetSavedNextThink(S.SavedNextThink);
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

	if (S.LeafState.Num() > 0)
	{
		FMemoryReader Reader(S.LeafState, /*bIsPersistent*/ true);
		// The schema the blob was WRITTEN at, not the one this build writes. A leaf that gates a
		// field on its own version can only be right if the archive it reads through reports the
		// writer's version; handing it `Latest` makes every such gate read true and consumes bytes
		// the writer never emitted.
		FElysiumSaveArchive Ar(Reader, LeafSchemaVersion);
		E->Serialize(Ar);
		// A blob that ran short, or one whose fields no longer line up, sets the archive's error
		// flag. It must never restore quietly: what comes back is a half-read entity whose remaining
		// fields hold whatever the overrun produced.
		if (Ar.IsError())
		{
			UE_LOG(LogElysiumWorld, Error,
				TEXT("snapshot '%s': the leaf state of #%d %s(%s) failed to read (%d bytes at schema "
					"v%d) — the entity is restored only as far as the read got"),
				*SnapshotMapName, S.Index, *S.TargetName, *S.ClassName.ToString(),
				S.LeafState.Num(), LeafSchemaVersion);
		}
	}

	// Dormancy last, and written directly rather than through ScriptHide/ScriptUnhide: those are
	// inputs with side effects (they stash and restore the think we have just restored ourselves).
	E->bHidden = S.bHidden;
	E->bDead = S.bDead;
	// Registered fields can alter a class-specific physical gate (notably trigger StartDisabled)
	// without changing hidden/dead. Re-apply unconditionally after all restored state is present.
	E->OnDormancyChanged();
	// Leaf deserializers and dormancy hooks may arm an entity while rebuilding transient state
	// (NPC patrol/interesting-place recovery does both). The generic saved schedule is the later,
	// authoritative statement: restore it last so freeze -> apply -> freeze remains identical and
	// a loaded entity cannot think earlier than the snapshot said.
	E->NextThink = S.NextThink;
	E->SetSavedNextThink(S.SavedNextThink);
	NotifyVisualChanged(*E);
	return true;
}

FElysiumEntityHandle FElysiumEntityWorld::RebaseHandle(const FElysiumEntityHandle& Saved) const
{
	if (!Saved.IsSet() || !EntityList.IsValidIndex(Saved.Index))
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
