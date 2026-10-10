#include "ElysiumSaveRestoreBlocks.h"
#include "ElysiumRetailSite.h"
#include "ElysiumTransitionState.h" // L0-r030: slot 23 inside the engine's save and transition orders

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSaveBlocks, Log, All);

// The block framework of L0-r029 (`walks/L0-r029.md`). Every body below was read before it was
// written (`vtmb_code` / `vtmb_asm`); the address at each line is the retail body it reproduces.

namespace ElysiumSaveRestore
{
	namespace
	{
		constexpr int32 GBlockNameBytes = 0x20;   // the Q_strncpy length of set slot 1
		constexpr int32 GRecordBytes = 0x28;      // SaveRestoreBlockHeader_t stride

		const TCHAR* GSetFn = TEXT("CSaveRestoreBlockSet");

		// The two bytewise string compares of slots 6 and 7 (inlined strcmp: byte, then byte+1, ...).
		bool SaveBlockNamesEqual(const char* A, const char* B)
		{
			return FCStringAnsi::Strcmp(A, B) == 0;
		}

		FString SaveBlockNameText(const char* Name)
		{
			return FString(ANSI_TO_TCHAR(Name));
		}
	}

	// --- the engine's save-data struct ---------------------------------------------------------

	void FSaveRestoreData::Reset(int32 Capacity)
	{
		Bytes.Reset();
		Offset = 0;
		ConnectionCount = 0;      // +0x18 and the rows: FUN_101a3b20 allocates the struct zeroed (L0-r030)
		Adjacency.Reset();
		EntityTable.Reset();
		TransferredEntities.Reset();
		bReadPastEnd = false;
		bGrowable = Capacity <= 0;
		if (!bGrowable) Bytes.SetNumZeroed(Capacity);
		Size = Bytes.Num();
	}

	// --- CSave (vftable 0x104625f4 / 0x10476fbc) -----------------------------------------------

	int32 FSave::Tell() const
	{
		return Data ? Data->Offset : 0; // CSave::vfunc0 0x1019fa10: struct[2]
	}

	void FSave::Seek(int32 Pos)
	{
		// CSave::vfunc1 0x1019fa30: `if (0 <= pos < struct[3]) { struct[2] = pos; struct[1] = struct[0] + pos }`,
		// otherwise a silent no-op.
		if (Data && Pos >= 0 && Pos < Data->Size) Data->Offset = Pos;
	}

	void FSave::WriteInt(const int32* Values, int32 Count)
	{
		WriteData(Values, Count * 4); // CSave::vfunc10 0x1019fa80 tail-jumps into vfunc12(ptr, 4n)
	}

	void FSave::WriteData(const void* Ptr, int32 Bytes)
	{
		// CSave::vfunc12 0x101a07b0: `if (struct == 0) return; if (n <= size − offset) { memcpy at the
		// cursor; cursor += n; offset += n } else { offset := size; Warning("Save/Restore overflow!") }`.
		if (Data == nullptr) return;
		if (Data->bGrowable && Bytes > Data->Remaining())
		{
			// Engine-side: the port's buffer grows where the engine's allocation would already be large
			// enough (its size is engine.dll's, unrecovered). Retail's overflow arm stays reachable with a
			// pinned capacity (`FSaveRestoreData::Reset(Capacity)`).
			Data->Bytes.SetNumZeroed(Data->Offset + Bytes);
			Data->Size = Data->Bytes.Num();
		}
		if (Bytes <= Data->Remaining())
		{
			FMemory::Memcpy(Data->Bytes.GetData() + Data->Offset, Ptr, Bytes);
			Data->Offset += Bytes;
			return;
		}
		Data->Offset = Data->Size;
		UE_LOG(LogElysiumSaveBlocks, Warning, TEXT("Save/Restore overflow!")); // 0x10593000
	}

	// --- CRestore (vftable 0x104626bc / 0x10477084) --------------------------------------------

	int32 FRestore::Tell() const
	{
		return Data ? Data->Offset : 0; // CRestore::vfunc0 0x101a1490
	}

	void FRestore::Seek(int32 Pos)
	{
		if (Data && Pos >= 0 && Pos < Data->Size) Data->Offset = Pos; // CRestore::vfunc1 0x101a14b0, same rule as CSave's
	}

	void FRestore::ReadData(void* Out, int32 Bytes)
	{
		// FUN_101a1f90, the raw read behind slot 15: `Bytes` from the cursor. What retail does past the
		// end of the buffer is not walked; the port reads what is there, zero-fills the rest and flags it.
		if (Data == nullptr || Bytes <= 0) return;
		const int32 Available = FMath::Clamp(Data->Size - Data->Offset, 0, Bytes);
		if (Available > 0) FMemory::Memcpy(Out, Data->Bytes.GetData() + Data->Offset, Available);
		if (Available < Bytes)
		{
			FMemory::Memzero(static_cast<uint8*>(Out) + Available, Bytes - Available);
			Data->bReadPastEnd = true;
		}
		Data->Offset += Available;
	}

	int32 FRestore::ReadInt()
	{
		int32 Value = 0;
		ReadData(&Value, 4); // CRestore::vfunc14 0x101a1ea0: four bytes, returned as the int
		return Value;
	}

	int32 FRestore::ReadInt(int32* Out, int32 Count, int32 ByteLimit)
	{
		// CRestore::vfunc15 0x101a2090: `n = 4·count; if (limit != 0 && limit <= n) n = limit;
		// read n bytes; if (n < limit) skip (limit − n); return n >> 2`.
		int32 Bytes = Count * 4;
		if (ByteLimit != 0 && ByteLimit <= Bytes) Bytes = ByteLimit;
		ReadData(Out, Bytes);
		if (Bytes < ByteLimit && Data != nullptr)
		{
			Data->Offset = FMath::Min(Data->Offset + (ByteLimit - Bytes), Data->Size); // FUN_101a2020, the skip
		}
		return Bytes >> 2;
	}

	// --- the vector ops (CUtlVectorDataOps 0x1047719c) -----------------------------------------

	void WriteHeaderVector(FSave& S, const TArray<FBlockHeader>& Vector)
	{
		// vfunc0 0x101a5330: one dword (the vector's size, vec+0xc) with ISave slot 10, then each of
		// the size elements through ISave slot 2 (the datamap writer, encoding unrecovered -- Open 2).
		// Named modernization: an element is its 0x28 raw bytes, the one fixed size that lets slot 3's
		// Seek-back rewrite land byte for byte on the placeholder it wrote first.
		const int32 Count = Vector.Num();
		S.WriteInt(&Count, 1);
		for (const FBlockHeader& Record : Vector) S.WriteData(&Record, GRecordBytes);
	}

	void ReadHeaderVector(FRestore& R, TArray<FBlockHeader>& Vector)
	{
		// vfunc1 0x101a5440: count through IRestore slot 14; size := 0; if count != 0 reserve count and
		// add count elements (0x1000ff5b, 0x1001037f); read each element through IRestore slot 2.
		const int32 Count = R.ReadInt();
		Vector.Reset();
		if (Count > 0)
		{
			Vector.Reserve(Count);
			Vector.AddDefaulted(Count);
			for (FBlockHeader& Record : Vector) R.ReadData(&Record, GRecordBytes);
		}
	}

	// --- CSaveRestoreBlockSet ----------------------------------------------------------------------

	FBlockSet::FBlockSet(const char* InName)
	{
		// FUN_101a47b0: vftable, the two vectors zeroed, `Q_strncpy(this+0x04, "Game", 0x20)` (0x10593294).
		FMemory::Memzero(Name, sizeof(Name));
		FCStringAnsi::Strncpy(Name, InName, GBlockNameBytes);
	}

	void FBlockSet::Site(const TCHAR* Tag, const TCHAR* Fn, uint32 Va, const TCHAR* Phase, const FString& Payload) const
	{
		if (Sites != nullptr) Sites->Site(Tag, Fn, Va, Phase, Payload);
	}

	int32 FBlockSet::FindRecord(const char* BlockName) const
	{
		// The lookup of slots 6 and 7: `j` = the first record whose szName compares equal, bytewise,
		// among the N (+0x4c) records; N is re-read every step. −1 = miss.
		for (int32 J = 0; J < Headers.Num(); ++J)
		{
			if (SaveBlockNamesEqual(Headers[J].szName, BlockName)) return J;
		}
		return INDEX_NONE;
	}

	void FBlockSet::PreSave(FSaveRestoreData* Arg)
	{
		// vfunc1 0x101a4850: this+0x4c := 0; InsertMultipleBefore(0, count, NULL) on the header vector
		// (thunk 0x10001b9f → 0x101a5800; count = this+0x30), so N = the handler count after the call;
		// then per handler i: `Q_strncpy(rec[i], handler_i.slot0(), 0x20)`; `handler_i.slot1(arg)`.
		Headers.Reset();
		Headers.AddDefaulted(Handlers.Num());
		for (FBlockHeader& Record : Headers)
		{
			// InsertMultipleBefore leaves the new records uninitialised; locHeader / locBody are only
			// written by slots 3 / 2. The port zero-fills the name and keeps the two offsets at −1.
			FMemory::Memzero(Record.szName, sizeof(Record.szName));
		}
		Site(TEXT("save_pre"), GSetFn, 0x101a4850u, TEXT("entry"),
			FString::Printf(TEXT("count=%d N=%d"), Handlers.Num(), Headers.Num()));
		for (int32 I = 0; I < Handlers.Num(); ++I)
		{
			FCStringAnsi::Strncpy(Headers[I].szName, Handlers[I]->GetBlockName(), GBlockNameBytes);
			Site(TEXT("save_pre"), GSetFn, 0x101a4850u, TEXT("block"),
				FString::Printf(TEXT("i=%d name=%s"), I, *SaveBlockNameText(Headers[I].szName)));
			Handlers[I]->PreSave(Arg);
		}
	}

	void FBlockSet::Save(FSave& S)
	{
		// vfunc2 0x101a48e0: t0 = Tell; per block i: rec[i].locBody (+0x24) := Tell − t0;
		// handler_i.slot2(adapter). Then this+0x3c := Tell − t0.
		const int32 T0 = S.Tell();
		for (int32 I = 0; I < Handlers.Num(); ++I)
		{
			const int32 Rel = S.Tell() - T0;
			if (Headers.IsValidIndex(I)) Headers[I].locBody = Rel;
			Site(TEXT("save_block"), GSetFn, 0x101a48e0u, TEXT("block"),
				FString::Printf(TEXT("i=%d name=%s rel=%d"), I, *SaveBlockNameText(Handlers[I]->GetBlockName()), Rel));
			Handlers[I]->Save(S);
		}
		DataLength = S.Tell() - T0;
		Site(TEXT("save_block"), GSetFn, 0x101a48e0u, TEXT("return"), FString::Printf(TEXT("len=%d"), DataLength));
	}

	void FBlockSet::WriteSaveHeaders(FSave& S)
	{
		// vfunc3 0x101a4970 (asm; the decompile drops a block). t0 = Tell. A temp vector grown to N =
		// this+0x4c elements and filled with 0xFF over N·0x28 bytes. Two ISave slot-10 writes of the SAME
		// stack word 0xFFFFFFFF (placeholders for this+0x38 and this+0x3c). ops.vfunc0 on the temp (N, then
		// N elements). Per block i: rec[i].locHeader (+0x20) := Tell − t0; handler_i.slot3(adapter).
		// this+0x38 := Tell − t0; tEnd = Tell. Seek(t0); slot10(&this+0x38, 1); slot10(&this+0x3c, 1);
		// ops.vfunc0 on this+0x40 (overwriting the temp's bytes in place). Seek(tEnd). Free the temp.
		const int32 T0 = S.Tell();
		TArray<FBlockHeader> Temp;
		Temp.AddUninitialized(Headers.Num());
		if (Temp.Num() > 0) FMemory::Memset(Temp.GetData(), 0xFF, Temp.Num() * GRecordBytes);
		const int32 Placeholder = -1; // 0xFFFFFFFF
		S.WriteInt(&Placeholder, 1);
		S.WriteInt(&Placeholder, 1);
		Site(TEXT("save_header"), GSetFn, 0x101a4970u, TEXT("placeholder"),
			FString::Printf(TEXT("words=2 value=0xffffffff N=%d"), Temp.Num()));
		WriteHeaderVector(S, Temp);
		for (int32 I = 0; I < Handlers.Num(); ++I)
		{
			const int32 Rel = S.Tell() - T0;
			if (Headers.IsValidIndex(I)) Headers[I].locHeader = Rel;
			Site(TEXT("save_header"), GSetFn, 0x101a4970u, TEXT("header"),
				FString::Printf(TEXT("i=%d name=%s rel=%d"), I, *SaveBlockNameText(Handlers[I]->GetBlockName()), Rel));
			Handlers[I]->WriteSaveHeaders(S);
		}
		HeaderLength = S.Tell() - T0;
		const int32 TEnd = S.Tell();
		S.Seek(T0);
		S.WriteInt(&HeaderLength, 1);
		S.WriteInt(&DataLength, 1);
		WriteHeaderVector(S, Headers);
		S.Seek(TEnd);
		Site(TEXT("save_header"), GSetFn, 0x101a4970u, TEXT("return"),
			FString::Printf(TEXT("hdr_len=%d data_len=%d N=%d t0=%d end=%d cursor=%d"),
				HeaderLength, DataLength, Headers.Num(), T0, TEnd, S.Tell()));
	}

	void FBlockSet::PurgeHeaders(const TCHAR* Tag, const TCHAR* Fn, uint32 Va)
	{
		// The shared tail of vfunc4 and vfunc8: this+0x4c := 0; if this+0x48 != −1 { if this+0x40 != 0
		// { Plat_Free(this+0x40); this+0x40 := 0 }; this+0x44 := 0 }. Always this+0x50 := this+0x40.
		// The retail object's grow marker (+0x48) is 0 (ctor), so the vector is owned and freed here.
		const bool bOwned = true;
		Site(Tag, Fn, Va, TEXT("purge"), FString::Printf(TEXT("owned=%d N_before=%d N=0"), bOwned ? 1 : 0, Headers.Num()));
		Headers.Empty();
	}

	void FBlockSet::PostSave()
	{
		// vfunc4 0x101a4bf0: per block i: handler_i.slot4(); then the purge tail.
		for (int32 I = 0; I < Handlers.Num(); ++I)
		{
			Site(TEXT("save_post"), GSetFn, 0x101a4bf0u, TEXT("post"),
				FString::Printf(TEXT("i=%d name=%s"), I, *SaveBlockNameText(Handlers[I]->GetBlockName())));
			Handlers[I]->PostSave();
		}
		PurgeHeaders(TEXT("save_post"), GSetFn, 0x101a4bf0u);
	}

	void FBlockSet::PreRestore()
	{
		// vfunc5 0x101a4c70: per block i: handler_i.slot5().
		for (int32 I = 0; I < Handlers.Num(); ++I)
		{
			Site(TEXT("restore_pre"), GSetFn, 0x101a4c70u, TEXT("pre"),
				FString::Printf(TEXT("i=%d name=%s"), I, *SaveBlockNameText(Handlers[I]->GetBlockName())));
			Handlers[I]->PreRestore();
		}
	}

	void FBlockSet::ReadRestoreHeaders(FRestore& R)
	{
		// vfunc6 0x101a4cb0: t0 = Tell. Read dword this+0x38, then dword this+0x3c (IRestore slot 15,
		// (ptr, 1, 0)). ops.vfunc1 reads the header vector into this+0x40 (sets N). Per block i: name :=
		// handler_i.slot0(); j = the first record with an equal name, j < N. If found and rec[j].locHeader
		// != −1: Seek(t0 + locHeader); handler_i.slot6(adapter). Else a silent skip. Finally Seek(t0 + this+0x38).
		const int32 T0 = R.Tell();
		R.ReadInt(&HeaderLength, 1, 0);
		R.ReadInt(&DataLength, 1, 0);
		ReadHeaderVector(R, Headers);
		Site(TEXT("restore_read"), GSetFn, 0x101a4cb0u, TEXT("entry"),
			FString::Printf(TEXT("t0=%d hdr_len=%d data_len=%d N=%d"), T0, HeaderLength, DataLength, Headers.Num()));
		for (int32 I = 0; I < Handlers.Num(); ++I)
		{
			const char* BlockName = Handlers[I]->GetBlockName();
			const int32 J = FindRecord(BlockName);
			const bool bFound = J != INDEX_NONE && Headers[J].locHeader != -1;
			Site(TEXT("restore_read"), GSetFn, 0x101a4cb0u, TEXT("lookup"),
				FString::Printf(TEXT("i=%d name=%s j=%s seek=%s"), I, *SaveBlockNameText(BlockName),
					J == INDEX_NONE ? TEXT("miss") : *FString::FromInt(J),
					bFound ? *FString::FromInt(T0 + Headers[J].locHeader) : TEXT("skip")));
			if (bFound)
			{
				R.Seek(T0 + Headers[J].locHeader);
				Handlers[I]->ReadRestoreHeaders(R);
			}
		}
		R.Seek(T0 + HeaderLength);
		Site(TEXT("restore_read"), GSetFn, 0x101a4cb0u, TEXT("return"), FString::Printf(TEXT("seek=%d cursor=%d"), T0 + HeaderLength, R.Tell()));
	}

	void FBlockSet::Restore(FRestore& R, int32 P2, int32 P3)
	{
		// vfunc7 0x101a4e70: t0 = Tell. Per block i: name := handler_i.slot0(); j = first match, j < N. If
		// found and rec[j].locBody != −1: Seek(t0 + locBody); handler_i.slot7(adapter, p2, p3) (asm pushes
		// (adapter, p2, p3); the decompile's `(param_1, param_1, param_2)` is wrong). Else a silent skip.
		// Finally Seek(t0 + this+0x3c).
		const int32 T0 = R.Tell();
		for (int32 I = 0; I < Handlers.Num(); ++I)
		{
			const char* BlockName = Handlers[I]->GetBlockName();
			const int32 J = FindRecord(BlockName);
			const bool bFound = J != INDEX_NONE && Headers[J].locBody != -1;
			Site(TEXT("restore_block"), GSetFn, 0x101a4e70u, TEXT("restore"),
				FString::Printf(TEXT("i=%d name=%s j=%s seek=%s args=(A,%d,%d)"), I, *SaveBlockNameText(BlockName),
					J == INDEX_NONE ? TEXT("miss") : *FString::FromInt(J),
					bFound ? *FString::FromInt(T0 + Headers[J].locBody) : TEXT("skip"), P2, P3));
			if (bFound)
			{
				R.Seek(T0 + Headers[J].locBody);
				Handlers[I]->Restore(R, P2, P3);
			}
		}
		R.Seek(T0 + DataLength);
		Site(TEXT("restore_block"), GSetFn, 0x101a4e70u, TEXT("return"), FString::Printf(TEXT("seek=%d cursor=%d"), T0 + DataLength, R.Tell()));
	}

	void FBlockSet::PostRestore()
	{
		// vfunc8 0x101a4fa0: per block i: handler_i.slot8(); then the slot-4 tail.
		for (int32 I = 0; I < Handlers.Num(); ++I)
		{
			Site(TEXT("restore_post"), GSetFn, 0x101a4fa0u, TEXT("post"),
				FString::Printf(TEXT("i=%d name=%s"), I, *SaveBlockNameText(Handlers[I]->GetBlockName())));
			Handlers[I]->PostRestore();
		}
		PurgeHeaders(TEXT("restore_post"), GSetFn, 0x101a4fa0u);
	}

	void FBlockSet::AddBlockHandler(IBlockHandler* Handler)
	{
		// vfunc9 0x101a5020: a CUtlVector append -- grow (8, then ×2 or +grow; Plat_Alloc/Realloc), size
		// +0x30 += 1, this+0x34 := this+0x24, the element written at the old size.
		if (Handler == nullptr) return;
		Handlers.Add(Handler);
		Site(TEXT("register"), GSetFn, 0x101a5020u, TEXT("add"),
			FString::Printf(TEXT("name=%s count=%d"), *SaveBlockNameText(Handler->GetBlockName()), Handlers.Num()));
	}

	void FBlockSet::RemoveBlockHandler(IBlockHandler* Handler)
	{
		// vfunc10 0x101a5100: walk the vector for the pointer; not found → return (no write); found →
		// shift the tail down one (FUN_10430fa0, a memmove) and size −= 1.
		const int32 Index = Handlers.IndexOfByKey(Handler);
		Site(TEXT("register"), GSetFn, 0x101a5100u, TEXT("remove"),
			FString::Printf(TEXT("name=%s found=%d count=%d"), Handler ? *SaveBlockNameText(Handler->GetBlockName()) : TEXT("-"),
				Index != INDEX_NONE ? 1 : 0, Index != INDEX_NONE ? Handlers.Num() - 1 : Handlers.Num()));
		if (Index != INDEX_NONE) Handlers.RemoveAt(Index);
	}

	// --- CServerGameDLL slots 17..21 -----------------------------------------------------------------

	void PreSave(FBlockSet& Set, FSaveRestoreData* Data)
	{
		// vtable slot 17, 0x1011b890: `ECX = [0x10592db0]; JMP [[ECX]+4]` -- a tail call into set slot 1
		// with the engine's one stack argument (consumed by the slot's RET 4).
		Set.Sites = Data ? Data->Sites : nullptr;
		if (Set.Sites) Set.Sites->Site(TEXT("save_top"), TEXT("CServerGameDLL::vfunc17"), 0x1011b890u, TEXT("tail"), TEXT("to=CSaveRestoreBlockSet::vfunc1"));
		Set.PreSave(Data);
		Set.Sites = nullptr;
	}

	int32 Save(FBlockSet& Set, FSaveRestoreData* Data)
	{
		// CServerGameDLL::Save 0x1011b080 (slot 18): profiler scope (instrumentation only); A =
		// CSave(param_1) (thunk 0x10009bf6 → FUN_1019f870); set->slot2(A); the adapter's CUtlVector
		// cleanup (thunks 0x100047aa / 0x10007275 and the inline repeat); profiler exit; EAX = 1 on
		// every path. The adapter's +0x04 buffer (Plat_Alloc(0x80), 0x20 dwords) has no port: its use
		// is unrecovered (Open 8) and nothing the set calls touches it.
		Set.Sites = Data ? Data->Sites : nullptr;
		FSave Adapter(Data);
		Set.Save(Adapter);
		const int32 Result = 1;
		if (Set.Sites) Set.Sites->Site(TEXT("save_top"), TEXT("CServerGameDLL::Save"), 0x1011b080u, TEXT("return"), FString::Printf(TEXT("result=%d"), Result));
		Set.Sites = nullptr;
		return Result;
	}

	void WriteSaveHeaders(FBlockSet& Set, FSaveRestoreData* Data)
	{
		// CServerGameDLL::vfunc19 0x1011b8b0 (slot 19): A = CSave(param_1); set->slot3(A); set->slot4();
		// the inline cleanup of the adapter's vector (no thunk calls here); RET 4, void. No result is
		// tested between the calls. The engine reaches it only after slot 18 returned nonzero.
		Set.Sites = Data ? Data->Sites : nullptr;
		FSave Adapter(Data);
		Set.WriteSaveHeaders(Adapter);
		Set.PostSave();
		if (Set.Sites) Set.Sites->Site(TEXT("save_top"), TEXT("CServerGameDLL::vfunc19"), 0x1011b8b0u, TEXT("return"), FString::Printf(TEXT("cursor=%d"), Adapter.Tell()));
		Set.Sites = nullptr;
	}

	void ReadRestoreHeaders(FBlockSet& Set, FSaveRestoreData* Data)
	{
		// CServerGameDLL::vfunc20 0x1011b950 (slot 20): A = CRestore(param_1) (thunk 0x1000a3f3 →
		// FUN_101a12a0); set->slot5(); set->slot6(A); the inline cleanup; RET 4, void.
		Set.Sites = Data ? Data->Sites : nullptr;
		FRestore Adapter(Data);
		Set.PreRestore();
		Set.ReadRestoreHeaders(Adapter);
		if (Set.Sites) Set.Sites->Site(TEXT("save_top"), TEXT("CServerGameDLL::vfunc20"), 0x1011b950u, TEXT("return"), FString::Printf(TEXT("cursor=%d"), Adapter.Tell()));
		Set.Sites = nullptr;
	}

	void Restore(FBlockSet& Set, FSaveRestoreData* Data, int32 P2, int32 P3)
	{
		// CServerGameDLL::Restore 0x1011b300 (slot 21): profiler scope; A = CRestore(param_1);
		// set->slot7(A, param_2, param_3); set->slot8(); the cleanup (thunks, then the inline repeat);
		// profiler exit; RET 0xC, void. p2 / p3 come from the engine's CSaveRestore::vfunc9 (p2 its own
		// second argument, p3 a dword whose low byte is `DAT_212af2f8 != 0`); their meaning is
		// unrecovered (Open 7) and they are forwarded unchanged, as retail does.
		Set.Sites = Data ? Data->Sites : nullptr;
		FRestore Adapter(Data);
		Set.Restore(Adapter, P2, P3);
		Set.PostRestore();
		if (Set.Sites) Set.Sites->Site(TEXT("save_top"), TEXT("CServerGameDLL::Restore"), 0x1011b300u, TEXT("return"), FString::Printf(TEXT("p2=%d p3=%d cursor=%d"), P2, P3, Adapter.Tell()));
		Set.Sites = nullptr;
	}

	// --- the engine's orchestration ------------------------------------------------------------------

	bool EngineSaveGameState(FBlockSet& Set, FSaveRestoreData& Data, int32& OutHeaderStart)
	{
		// CSaveRestore::vfunc13 0x20096470: slot17(pSaveData) (0x2009649f); slot23(0, 0)
		// (BuildAdjacentMapList, 0x200964ae -- not this story's); FUN_200962c0 (unrecovered); slot18
		// (0x200964c2) and abort with 0 if it returns 0 (0x200964c5–0x200964c7); slot19 (0x200964ed).
		// The data section ends where slot 3's t0 begins: the header section the engine later moves
		// ahead of the data in the file (`Rebase`; `docs/vtmb/savegame_format.md` § `.HL1`).
		PreSave(Set, &Data);
		// Slot 23 at 0x200964ae: `PUSH 0; PUSH 0; CALL [EAX+0x5c]` -- BuildAdjacentMapList(NULL, NULL) over
		// the save data slot 17 just filled (its entity table exists, so the transition flags land on the
		// rows the header slot writes next). L0-r030.
		ElysiumTransitionState::BuildAdjacentMapList(Data, nullptr, nullptr);
		const int32 SaveResult = Save(Set, &Data);
		if (SaveResult == 0)
		{
			OutHeaderStart = Data.Offset;
			return false;
		}
		OutHeaderStart = Data.Offset;
		WriteSaveHeaders(Set, &Data);
		return true;
	}

	void EngineLoadGameState(FBlockSet& Set, FSaveRestoreData& Data, int32 HeaderStart, int32 DataStart, int32 P2, int32 P3)
	{
		// CSaveRestore::vfunc9 0x200975f0: FUN_20097070 (positions the adapter on the header area --
		// unrecovered); slot20 (0x20097627); FUN_20097280, FUN_20097520 (unrecovered; the data area);
		// slot21 (0x200976b9) with the two forwarded dwords. The engine also pushes a fourth dword the
		// callee's RET 0xC never pops (Open 7).
		FRestore Position(&Data);
		Position.Seek(HeaderStart);
		ReadRestoreHeaders(Set, &Data);
		Position.Seek(DataStart);
		Restore(Set, &Data, P2, P3);
	}

	void EngineLevelTransition(FBlockSet& Set, FSaveRestoreData& Data, int32 HeaderStart,
		const char* OldLevel, const char* LandmarkName)
	{
		// CSaveRestore::vfunc10 0x20097d00: slot 23 first, with the level being left and the landmark
		// crossed (0x20097d63-0x20097d73: `PUSH [ESP+0x1504]; PUSH [ESP+0x1500]; CALL [EDX+0x5c]`; L0-r030),
		// then slot20 (0x20097e25 and 0x20098173) and never slot 21, so the PostRestore purge does not run
		// on this path: the header vector keeps its memory until the next slot 1 / slot 6 / slot 8.
		ElysiumTransitionState::BuildAdjacentMapList(Data, OldLevel, LandmarkName);
		FRestore Position(&Data);
		Position.Seek(HeaderStart);
		ReadRestoreHeaders(Set, &Data);
	}
}
