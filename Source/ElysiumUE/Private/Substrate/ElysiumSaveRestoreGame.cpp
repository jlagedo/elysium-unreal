#include "Substrate/ElysiumSaveRestoreGame.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumRetailSite.h"
#include "ElysiumSaveArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// The "Game" set (0x1072bae8) and its five handlers over the port's snapshot (L0-r029). The set and
// the slot protocol are `ElysiumSaveRestoreBlocks.cpp`; the handler bodies below are the port's own
// (retail's are unwalked: Open 10) and say so at each line.

namespace ElysiumSaveRestore
{
	namespace
	{
		FGameContext* GameContextOf(FSaveRestoreData* Data)
		{
			return Data ? static_cast<FGameContext*>(Data->Context) : nullptr;
		}

		// One row's bytes through the port's archive (named modernization: the datamap writer's
		// encoding, CSave::vfunc2 0x1019f930, is unrecovered).
		template <typename T>
		void GameRowToBytes(T& Row, int32 Version, double Base, TArray<uint8>& OutBytes)
		{
			OutBytes.Reset();
			FMemoryWriter Writer(OutBytes, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, Version, Base, Base);
			Ar << Row;
		}

		template <typename T>
		bool GameRowFromBytes(const TArray<uint8>& Bytes, int32 Version, double Base, T& OutRow)
		{
			FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Reader, Version, Base, Base);
			Ar << OutRow;
			return !Reader.IsError();
		}

		// --- "Entities" (CEntitySaveRestoreBlockHandler 0x10477130, object 0x1072bb44) -----------------
		// Retail: header = the entity table (ETABLE: id, location, size, classname...), body = every
		// entity's field stream (`docs/vtmb/savegame_format.md` § `.HL1`). Port: header = the row table
		// (index, body location, body size), body = each `FElysiumEntityState` through the archive.
		class FEntitiesBlockHandler final : public IBlockHandler
		{
		public:
			using FTableRow = FEntityTableRow;

			virtual const char* GetBlockName() const override { return "Entities"; } // 0x1059317c

			virtual void PreSave(FSaveRestoreData* Arg) override
			{
				// Slot 1 (CEntitySaveRestoreBlockHandler::vfunc1 0x101a2c30; L0-r030): `+0x1330 := the
				// entity count`, `+0x1334 := calloc(count * 0x30)`, then one row an entity in list order --
				// +0x00 := i, +0x04 := entindex, +0x0c := -1, +0x10 := the handle, and +0x14/+0x18/+0x20/
				// +0x24/+0x28 zeroed. Here the rows are the snapshot's (the player and the camera exclusions
				// are `Freeze`'s own); the table lives on the save data so slot 23's second pass
				// (FUN_101c7ff0) flags it before slot 2 runs.
				Context = GameContextOf(Arg);
				Data = Arg;
				if (Data == nullptr) return;
				Data->EntityTable.Reset();
				if (Context == nullptr || Context->Source == nullptr) return;
				for (const FElysiumEntityState& Row : Context->Source->Entities)
				{
					FTableRow& Entry = Data->EntityTable.AddDefaulted_GetRef();
					Entry.Id = Data->EntityTable.Num() - 1;
					Entry.EdictIndex = Row.Index;
					Entry.RestoredEdictIndex = -1;
					Entry.Handle = FElysiumEntityHandle(Row.Index, 0);
					Entry.Classname = Row.ClassName.IsNone() ? FString() : Row.ClassName.ToString();
				}
			}

			virtual void Save(FSave& S) override
			{
				// Slot 2 (retail 0x101a37c0, unwalked here): the rows back to back from the block's start;
				// the table remembers where each landed (+0x14 / +0x18), for the header slot 3 writes next.
				if (Context == nullptr || Context->Source == nullptr || Data == nullptr) return;
				const FElysiumMapSnapshot& Source = *Context->Source;
				const int32 BlockStart = S.Tell();
				TArray<uint8> Bytes;
				int32 RowIndex = 0;
				for (const FElysiumEntityState& Row : Source.Entities)
				{
					if (!Data->EntityTable.IsValidIndex(RowIndex)) break;
					GameRowToBytes(const_cast<FElysiumEntityState&>(Row), Source.SchemaVersion, Source.SaveBase, Bytes);
					FTableRow& Entry = Data->EntityTable[RowIndex++];
					Entry.Location = S.Tell() - BlockStart;
					Entry.Size = Bytes.Num();
					S.WriteData(Bytes.GetData(), Bytes.Num());
				}
			}

			virtual void WriteSaveHeaders(FSave& S) override
			{
				// Slot 3: the table (retail's ETABLE group) -- dword count, then per row the words a
				// transition reads back: edict index, location, size, the two flag words, and the
				// classname (dword length, bytes). Encoding the port's (decisions.md D6).
				const int32 Count = Data ? Data->EntityTable.Num() : 0;
				S.WriteInt(&Count, 1);
				for (int32 I = 0; I < Count; ++I)
				{
					const FTableRow& Entry = Data->EntityTable[I];
					const int32 Words[5] = { Entry.EdictIndex, Entry.Location, Entry.Size,
						static_cast<int32>(Entry.FlagsLo), static_cast<int32>(Entry.FlagsHi) };
					S.WriteInt(Words, 5);
					const FTCHARToUTF8 Ansi(*Entry.Classname);
					const int32 Len = Ansi.Length();
					S.WriteInt(&Len, 1);
					if (Len > 0) S.WriteData(Ansi.Get(), Len);
				}
			}

			virtual void PostSave() override { Context = nullptr; Data = nullptr; }

			virtual void ReadRestoreHeaders(FRestore& R) override
			{
				// Slot 6: the table back onto the save data, from the set's seek to locHeader.
				Context = GameContextOf(R.Data);
				Data = R.Data;
				if (Data == nullptr) return;
				Data->EntityTable.Reset();
				const int32 Count = R.ReadInt();
				if (Count < 0 || Count > R.Data->Size / 24) { if (Context) Context->bRowDecodeFailed = true; return; }
				Data->EntityTable.AddDefaulted(Count);
				for (int32 I = 0; I < Count; ++I)
				{
					FTableRow& Entry = Data->EntityTable[I];
					int32 Words[5] = { 0, 0, 0, 0, 0 };
					R.ReadInt(Words, 5, 0);
					Entry.Id = I;
					Entry.EdictIndex = Words[0]; Entry.Location = Words[1]; Entry.Size = Words[2];
					Entry.FlagsLo = static_cast<uint32>(Words[3]); Entry.FlagsHi = static_cast<uint32>(Words[4]);
					Entry.RestoredEdictIndex = -1;
					Entry.Handle = FElysiumEntityHandle(Entry.EdictIndex, 0);
					const int32 Len = R.ReadInt();
					if (Len < 0 || Len > R.Data->Size - R.Tell()) { if (Context) Context->bRowDecodeFailed = true; return; }
					if (Len > 0)
					{
						TArray<ANSICHAR> Chars;
						Chars.SetNumZeroed(Len + 1);
						R.ReadData(Chars.GetData(), Len);
						Entry.Classname = FString(UTF8_TO_TCHAR(Chars.GetData()));
					}
				}
			}

			virtual void Restore(FRestore& R, int32 /*P2*/, int32 /*P3*/) override
			{
				// Slot 7, from the set's seek to locBody: the rows through the table, then the port's
				// apply onto the world (`FElysiumEntityWorld::ApplyRestoredEntities`). p2 / p3 are
				// forwarded by the set and unread here (their meaning is unrecovered, Open 7).
				if (!ReadRows(R)) return;
				if (Context && Context->World && Context->Decoded)
				{
					Context->AppliedRows = Context->World->ApplyRestoredEntities(*Context->Decoded, Context->RestoreBase);
				}
			}

			virtual void PostRestore() override { Context = nullptr; Data = nullptr; }

			// The body read alone: the engine-side transition path (vfunc10) reads the entity data
			// itself after slot 20 (its loop is unrecovered); the port's stand-in reuses this reader.
			bool ReadRows(FRestore& R)
			{
				if (Context == nullptr || Context->Decoded == nullptr || R.Data == nullptr) return false;
				FElysiumMapSnapshot& Decoded = *Context->Decoded;
				const TArray<FTableRow>& Table = R.Data->EntityTable;
				const int32 BlockStart = R.Tell();
				Decoded.Entities.Reset();
				Decoded.Entities.Reserve(Table.Num());
				TArray<uint8> Bytes;
				for (const FTableRow& Entry : Table)
				{
					if (Entry.Size < 0 || Entry.Location < 0 || BlockStart + Entry.Location + Entry.Size > R.Data->Size)
					{
						Context->bRowDecodeFailed = true;
						return false;
					}
					R.Seek(BlockStart + Entry.Location);
					Bytes.SetNumUninitialized(Entry.Size);
					R.ReadData(Bytes.GetData(), Entry.Size);
					FElysiumEntityState& Row = Decoded.Entities.AddDefaulted_GetRef();
					if (!GameRowFromBytes(Bytes, Decoded.SchemaVersion, Decoded.SaveBase, Row) || Row.Index != Entry.EdictIndex)
					{
						Context->bRowDecodeFailed = true;
						return false;
					}
				}
				return true;
			}

			FGameContext* Context = nullptr;
			FSaveRestoreData* Data = nullptr;   // the save data whose +0x1330 table this handler fills
		};

		// --- "EventQueue" (CEQ_SaveRestoreBlockHandler 0x10454060, object 0x106e70a8) ------------------
		// Retail: the pending delayed I/O events. Port body: dword count, the queue's next serial and
		// its backward-clock guard word, then each `FElysiumIOEvent` as (dword size, bytes). No header.
		class FEventQueueBlockHandler final : public IBlockHandler
		{
		public:
			virtual const char* GetBlockName() const override { return "EventQueue"; } // 0x1055e5bc

			virtual void PreSave(FSaveRestoreData* Arg) override { Context = GameContextOf(Arg); }

			virtual void Save(FSave& S) override
			{
				if (Context == nullptr || Context->Source == nullptr) return;
				const FElysiumMapSnapshot& Source = *Context->Source;
				const int32 Count = Source.Queue.Num();
				S.WriteInt(&Count, 1);
				S.WriteData(&Source.QueueNextSerial, sizeof(uint64));
				S.WriteData(&Source.QueueLastEnqueue, sizeof(double));
				TArray<uint8> Bytes;
				for (const FElysiumIOEvent& Event : Source.Queue)
				{
					GameRowToBytes(const_cast<FElysiumIOEvent&>(Event), Source.SchemaVersion, Source.SaveBase, Bytes);
					const int32 Size = Bytes.Num();
					S.WriteInt(&Size, 1);
					S.WriteData(Bytes.GetData(), Size);
				}
			}

			virtual void PostSave() override { Context = nullptr; }

			virtual void ReadRestoreHeaders(FRestore& R) override { Context = GameContextOf(R.Data); }

			virtual void Restore(FRestore& R, int32 /*P2*/, int32 /*P3*/) override
			{
				if (Context && Context->bLevelTransition)
				{
					// The transition path (CreateEntityTransitionList 0x1011b590 calls this slot with
					// (adapter, 0, 0) when rows moved; L0-r030). Retail's body CEQ_SaveRestoreBlockHandler::
					// vfunc7 0x100cff70 -> FUN_100cfda0: clear the queue (FUN_100cdda0), read the "EventQueue"
					// header and `count` "PEvent" records (12 fields) at the adapter's cursor, AddEvent each
					// (FUN_100ce1c0). The cursor it reads at is wherever FUN_101a3c40's last row seek left it,
					// and a restored event's EHANDLEs resolve through the entity table's +0x10 handles -- the
					// cross-map handle remap the Entities block's 0x101a2e40 machinery performs is not ported.
					// PLANNING FAULT (reported): the body is L0 (r029 Open 10) and is not reproduced on this
					// path; the dispatch is, so the slot order a record observes is retail's.
					if (R.Data && R.Data->Sites)
					{
						R.Data->Sites->Site(TEXT("eq_restore"), TEXT("CEQ_SaveRestoreBlockHandler::vfunc7"), 0x100cff70u, TEXT("hook"),
							FString::Printf(TEXT("cursor=%d body=FUN_100cfda0 va=0x100cfda0 applied=0"), R.Tell()));
					}
					return;
				}
				if (!ReadRows(R)) return;
				if (Context && Context->World && Context->Decoded)
				{
					Context->World->ApplyRestoredQueue(*Context->Decoded, Context->RestoreBase);
				}
			}

			virtual void PostRestore() override { Context = nullptr; }

			bool ReadRows(FRestore& R)
			{
				if (Context == nullptr || Context->Decoded == nullptr) return false;
				FElysiumMapSnapshot& Decoded = *Context->Decoded;
				const int32 Count = R.ReadInt();
				if (Count < 0 || R.Data == nullptr || Count > R.Data->Size / 4) { Context->bRowDecodeFailed = true; return false; }
				R.ReadData(&Decoded.QueueNextSerial, sizeof(uint64));
				R.ReadData(&Decoded.QueueLastEnqueue, sizeof(double));
				Decoded.Queue.Reset();
				Decoded.Queue.Reserve(Count);
				TArray<uint8> Bytes;
				for (int32 I = 0; I < Count; ++I)
				{
					const int32 Size = R.ReadInt();
					if (Size < 0 || Size > R.Data->Size - R.Tell()) { Context->bRowDecodeFailed = true; return false; }
					Bytes.SetNumUninitialized(Size);
					R.ReadData(Bytes.GetData(), Size);
					FElysiumIOEvent& Event = Decoded.Queue.AddDefaulted_GetRef();
					if (!GameRowFromBytes(Bytes, Decoded.SchemaVersion, Decoded.SaveBase, Event)) { Context->bRowDecodeFailed = true; return false; }
				}
				return true;
			}

			FGameContext* Context = nullptr;
		};

		// --- "Physics" (CPhysSaveRestoreBlockHandler 0x1044943c, object 0x106bda60) --------------------
		// VPhysics object state keyed by entity and field. No port body: decisions.md D6 leaves the
		// physics serializer to the owner (the base slot answers `ShouldSavePhysics == false`). The
		// handler is registered so the dispatch order is retail's; every slot is the default no-op.
		class FPhysicsBlockHandler final : public IBlockHandler
		{
		public:
			virtual const char* GetBlockName() const override { return "Physics"; } // 0x10539b54
		};

		// --- "AI" (CAI_SaveRestoreBlockHandler 0x1049dfb0, object 0x10936b5c) -------------------------
		// L4 upward hook: NPC memory, patrol paths, interesting-place markers. Registered here (the L0
		// registration), bodies owned by L4 (`hooks.tsv` carries no row for 0x1011a0c0 -> 0x1030c390 yet).
		class FAiBlockHandler final : public IBlockHandler
		{
		public:
			virtual const char* GetBlockName() const override { return "AI"; } // 0x106129e0
		};

		// --- "Python" (CPython_SaveRestoreBlockHandler 0x10476ac8, object 0x1072b354) ------------------
		// L5 upward hook: the pickled script namespaces (Save / Restore 0x1019adc0 / 0x1019b130). The
		// port's story state lives in the session block today; the handler is registered so the
		// Python block's slot order is retail's when L5 fills it.
		class FPythonBlockHandler final : public IBlockHandler
		{
		public:
			virtual const char* GetBlockName() const override { return "Python"; } // 0x1055e570
		};

		FEntitiesBlockHandler& GameEntitiesHandler() { static FEntitiesBlockHandler H; return H; }
		FEventQueueBlockHandler& GameEventQueueHandler() { static FEventQueueBlockHandler H; return H; }
		FPhysicsBlockHandler& GamePhysicsHandler() { static FPhysicsBlockHandler H; return H; }
		FAiBlockHandler& GameAiHandler() { static FAiBlockHandler H; return H; }
		FPythonBlockHandler& GamePythonHandler() { static FPythonBlockHandler H; return H; }
	}

	IBlockHandler& GameBlockHandler(EGameBlock Block)
	{
		// The four getters CreateEntityTransitionList 0x1011b590 reaches its blocks through (each a
		// `MOV EAX, imm; RET` behind a JMP thunk): FUN_100cffa0 -> 0x106e70a8 (EventQueue), FUN_10045700 ->
		// 0x106bda60 (Physics), FUN_1030c390 -> 0x10936b5c (AI), FUN_1019b360 -> 0x1072b354 (Python);
		// the Entities object 0x1072bb44 is FUN_101a3b00's. L0-r030.
		switch (Block)
		{
		case EGameBlock::EventQueue: return GameEventQueueHandler();
		case EGameBlock::Physics: return GamePhysicsHandler();
		case EGameBlock::Ai: return GameAiHandler();
		case EGameBlock::Python: return GamePythonHandler();
		case EGameBlock::Entities:
		default: return GameEntitiesHandler();
		}
	}

	void BindTransitionContext(FSaveRestoreData& Data, FGameContext& Context)
	{
		// The handlers read their world through the save data's Context on every slot; the transition
		// path hands them this one so the EventQueue body knows which path it is on.
		Context.bLevelTransition = true;
		Data.Context = &Context;
		GameEventQueueHandler().Context = &Context;
		GameEntitiesHandler().Context = &Context;
	}

	void UnbindTransitionContext()
	{
		GameEventQueueHandler().Context = nullptr;
		GameEntitiesHandler().Context = nullptr;
	}

	FBlockSet& GameBlockSet()
	{
		// The static set (ctor FUN_101a47b0 at static init, "Game") with the five `AddBlockHandler`
		// calls of DLLInit (0x1011a0c0: asm 0x1011a21e, 0x1011a235, 0x1011a24c, 0x1011a262, 0x1011a279).
		static FBlockSet Set("Game");
		static bool bRegistered = false;
		if (!bRegistered)
		{
			bRegistered = true;
			Set.AddBlockHandler(&GameEntitiesHandler());
			Set.AddBlockHandler(&GameEventQueueHandler());
			Set.AddBlockHandler(&GamePhysicsHandler());
			Set.AddBlockHandler(&GameAiHandler());
			Set.AddBlockHandler(&GamePythonHandler());
		}
		return Set;
	}

	void EncodeMapBlocks(FElysiumMapSnapshot& Snapshot, IElysiumRetailSiteSink* Sites, FElysiumEntityWorld* World)
	{
		FGameContext Context;
		Context.Source = &Snapshot;
		FSaveRestoreData Data;
		Data.Reset();
		Data.Sites = Sites;
		Data.Context = &Context;
		Data.World = World; // the entity list slot 23 scans (L0-r030); null for a snapshot re-encoded off-world
		int32 HeaderStart = 0;
		EngineSaveGameState(GameBlockSet(), Data, HeaderStart); // vfunc13 0x20096470: 17, 23, 18, 19
		Snapshot.BlockStream = MoveTemp(Data.Bytes);
		Snapshot.BlockHeaderStart = HeaderStart;
		// The global preamble's `connectionCount` and ADJACENCY rows (`docs/vtmb/savegame_format.md`):
		// written by the engine before the block set's sections, kept beside the stream here.
		if (World != nullptr)
		{
			Snapshot.Adjacency.Reset();
			for (const FAdjacencyRow& Row : Data.Adjacency)
			{
				FElysiumSavedAdjacency& Saved = Snapshot.Adjacency.AddDefaulted_GetRef();
				Saved.MapName = FString(ANSI_TO_TCHAR(Row.MapName));
				Saved.LandmarkName = FString(ANSI_TO_TCHAR(Row.LandmarkName));
				Saved.LandmarkIndex = Row.Landmark.Index;
				Saved.LandmarkOrigin = Row.LandmarkOrigin;
			}
		}
	}

	namespace
	{
		// The snapshot's scalars the handler bodies read while decoding (the section's own words,
		// outside the block set), copied before the chain runs.
		void GameCopyScalars(const FElysiumMapSnapshot& From, FElysiumMapSnapshot& To)
		{
			To.MapName = From.MapName;
			To.DefCount = From.DefCount;
			To.SaveBase = From.SaveBase;
			To.FrozenAt = From.FrozenAt;
			To.SchemaVersion = From.SchemaVersion;
			To.AbsentEntities = From.AbsentEntities;
			To.Fade = From.Fade;
			To.Weather = From.Weather;
			To.ComfortTargets = From.ComfortTargets;
			To.BlockHeaderStart = From.BlockHeaderStart;
			To.Adjacency = From.Adjacency; // the global preamble's ADJACENCY rows (L0-r030)
		}

		void GameLoadStream(const FElysiumMapSnapshot& Snapshot, FSaveRestoreData& Data)
		{
			Data.Reset();
			Data.Bytes = Snapshot.BlockStream;
			Data.Size = Data.Bytes.Num();
			Data.bGrowable = false; // the file's buffer: reads only
		}
	}

	bool DecodeMapBlocks(FElysiumMapSnapshot& Snapshot)
	{
		FElysiumMapSnapshot Decoded;
		GameCopyScalars(Snapshot, Decoded);
		FGameContext Context;
		Context.Decoded = &Decoded;
		FSaveRestoreData Data;
		GameLoadStream(Snapshot, Data);
		Data.Context = &Context;
		EngineLoadGameState(GameBlockSet(), Data, Snapshot.BlockHeaderStart, 0, 0, 0); // vfunc9 0x200975f0: 20, 21; no world
		if (Context.bRowDecodeFailed || Data.bReadPastEnd) return false;
		Snapshot.Entities = MoveTemp(Decoded.Entities);
		Snapshot.Queue = MoveTemp(Decoded.Queue);
		Snapshot.QueueNextSerial = Decoded.QueueNextSerial;
		Snapshot.QueueLastEnqueue = Decoded.QueueLastEnqueue;
		return true;
	}

	int32 RestoreMapBlocks(const FElysiumMapSnapshot& Snapshot, FElysiumEntityWorld& World, FElysiumMapSnapshot& Decoded,
		double RestoreBase, bool bLevelTransition, IElysiumRetailSiteSink* Sites)
	{
		GameCopyScalars(Snapshot, Decoded);
		FGameContext Context;
		Context.Decoded = &Decoded;
		Context.World = &World;
		Context.RestoreBase = RestoreBase;
		FSaveRestoreData Data;
		int32 HeaderStart = Snapshot.BlockHeaderStart;
		if (Snapshot.BlockStream.IsEmpty())
		{
			// A snapshot built in memory and never frozen (a harness envelope, a test row): the engine
			// hands the set a stream, so one is encoded from the arrays first -- the same chain
			// `Freeze` ran, with no world to observe it.
			FElysiumMapSnapshot Encoded;
			GameCopyScalars(Snapshot, Encoded);
			Encoded.Entities = Snapshot.Entities;
			Encoded.Queue = Snapshot.Queue;
			Encoded.QueueNextSerial = Snapshot.QueueNextSerial;
			Encoded.QueueLastEnqueue = Snapshot.QueueLastEnqueue;
			EncodeMapBlocks(Encoded, nullptr, nullptr);
			Data.Reset();
			Data.Bytes = MoveTemp(Encoded.BlockStream);
			Data.Size = Data.Bytes.Num();
			Data.bGrowable = false;
			HeaderStart = Encoded.BlockHeaderStart;
		}
		else
		{
			GameLoadStream(Snapshot, Data);
		}
		Data.Sites = Sites;
		Data.Context = &Context;
		FBlockSet& Set = GameBlockSet();
		if (!bLevelTransition)
		{
			// vfunc9 0x200975f0: slot 20 then slot 21 (p2 / p3: the engine's two dwords, unrecovered
			// meaning; the port passes 0, 0 and the set forwards them to every handler's slot 7).
			EngineLoadGameState(Set, Data, HeaderStart, 0, 0, 0);
		}
		else
		{
			// vfunc10 0x20097d00: slot 20 only. The engine then reads the entity data itself (its loop is
			// unrecovered); the port's stand-in reads the rows through the Entities handler's table and
			// the queue through the EventQueue body, then applies them as the ordinary path does. No set
			// slot 8 runs: the header vector keeps its records until the next slot 1 / 6 / 8.
			EngineLevelTransition(Set, Data, HeaderStart);
			const TArray<FBlockHeader>& Records = Set.HeaderRecords();
			FRestore R(&Data);
			R.Seek(0);
			const int32 T0 = R.Tell();
			auto FindRecord = [&Records](const char* Name) -> const FBlockHeader*
			{
				for (const FBlockHeader& Record : Records) if (FCStringAnsi::Strcmp(Record.szName, Name) == 0) return &Record;
				return nullptr;
			};
			if (const FBlockHeader* Entities = FindRecord("Entities"); Entities && Entities->locBody != -1)
			{
				R.Seek(T0 + Entities->locBody);
				if (GameEntitiesHandler().ReadRows(R)) Context.AppliedRows = World.ApplyRestoredEntities(Decoded, RestoreBase);
			}
			if (const FBlockHeader* Queue = FindRecord("EventQueue"); Queue && Queue->locBody != -1)
			{
				R.Seek(T0 + Queue->locBody);
				GameEventQueueHandler().Context = &Context;
				if (GameEventQueueHandler().ReadRows(R)) World.ApplyRestoredQueue(Decoded, RestoreBase);
				GameEventQueueHandler().Context = nullptr;
			}
			GameEntitiesHandler().Context = nullptr;
			GameEntitiesHandler().Data = nullptr;
		}
		if (Context.bRowDecodeFailed || Data.bReadPastEnd) return INDEX_NONE;
		return Context.AppliedRows;
	}
}
