#include "ElysiumKeyValuesLoader.h"

#include "ElysiumRetailSite.h"

#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"

namespace ElysiumKeyValuesLoader
{
using ElysiumKeyValues::EKvType;
using ElysiumKeyValues::FileTypeCode;
using ElysiumKeyValues::Shown;

// --- The engine file system -----------------------------------------------------------------------

FKvFileHandle FKvDiskFileSystem::Open(const FString& Name, const ANSICHAR* Mode, int32 PathID)
{
	// Slot `+0x00`: `Open(name, "rb", pathID)`. The mode is always `rb` here and the one search path
	// is the deployed corpus (`PathID` named modernization, header).
	(void)Mode;
	(void)PathID;
	const FString Path = Resolve ? Resolve(Name) : Name;
	IFileHandle* Handle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path);
	return reinterpret_cast<FKvFileHandle>(Handle);
}

void FKvDiskFileSystem::Close(FKvFileHandle Handle)
{
	// Slot `+0x04`.
	delete reinterpret_cast<IFileHandle*>(Handle);
}

int32 FKvDiskFileSystem::Read(uint8* Out, int32 Count, FKvFileHandle Handle)
{
	// Slot `+0x08`: `Read(buf, n, h)`.
	IFileHandle* File = reinterpret_cast<IFileHandle*>(Handle);
	return File != nullptr && File->Read(Out, Count) ? Count : 0;
}

int32 FKvDiskFileSystem::Size(FKvFileHandle Handle)
{
	// Slot `+0x18`.
	IFileHandle* File = reinterpret_cast<IFileHandle*>(Handle);
	return File != nullptr ? static_cast<int32>(File->Size()) : 0;
}

FString WidenBytes(const TArray<uint8>& Bytes)
{
	FString Out;
	Out.Reserve(Bytes.Num());
	for (const uint8 B : Bytes)
	{
		Out.AppendChar(static_cast<TCHAR>(B));
	}
	return Out;
}

namespace
{
	// Open `Name` through `Fs` and read it whole plus a NUL (`0x102480f0` arm 4 and the cache's miss
	// arm share the four slots in this order: Open, Size, Read, Close, `buf[n] = 0`).
	bool ReadWholeFile(IKvFileSystem& Fs, const FString& Name, int32 PathID, int32& OutSize, FString& OutText)
	{
		FKvFileHandle Handle = Fs.Open(Name, "rb", PathID);   // `0x105596cc`
		if (Handle == nullptr)
		{
			OutSize = 0;
			return false;
		}
		OutSize = Fs.Size(Handle);
		TArray<uint8> Bytes;
		Bytes.SetNumZeroed(OutSize);
		if (OutSize > 0)
		{
			Fs.Read(Bytes.GetData(), OutSize, Handle);
		}
		Fs.Close(Handle);
		OutText = WidenBytes(Bytes);
		return true;
	}

	void LoadSite(IElysiumRetailSiteSink* Sites, const TCHAR* Fn, uint32 Va, const TCHAR* Phase, const FString& Payload)
	{
		if (Sites != nullptr)
		{
			Sites->Site(TEXT("kv.load"), Fn, Va, Phase, Payload);
		}
	}
}

// --- `0x102482f0`: the whole-file text cache -------------------------------------------------------

uint32 FKvFileTextCache::HashName(const FString& Name)
{
	// `0x1023f040`: `crc = 0xffffffff`. `0x1023f0c0`: per byte `crc = table[(crc ^ b) & 0xff] ^ (crc >> 8)`
	// over `strlen(name)` bytes with the 1024-byte table at `0x10496f58` (entry 1 is `0x77073096`: the
	// reflected IEEE polynomial `0xEDB88320`, generated here). `0x1023f060`: `crc = ~crc`.
	static const TArray<uint32> Table = []()
	{
		TArray<uint32> Built;
		Built.SetNumUninitialized(256);
		for (uint32 I = 0; I < 256; ++I)
		{
			uint32 C = I;
			for (int32 K = 0; K < 8; ++K)
			{
				C = (C & 1u) ? (0xEDB88320u ^ (C >> 1)) : (C >> 1);
			}
			Built[I] = C;
		}
		return Built;
	}();
	uint32 Crc = 0xffffffffu;
	const auto Ansi = StringCast<ANSICHAR>(*Name);
	for (const ANSICHAR* P = Ansi.Get(); *P != '\0'; ++P)
	{
		Crc = Table[(Crc ^ static_cast<uint8>(*P)) & 0xffu] ^ (Crc >> 8);
	}
	return ~Crc;
}

const FKvFileTextCache::FEntry* FKvFileTextCache::Lookup(const FString& Name, IKvFileSystem& Fs, IElysiumRetailSiteSink* Sites)
{
	// 1. The hash of the name.
	const uint32 Hash = HashName(Name);
	// 2. Linear search of `DAT_10753768[0 .. DAT_10856fa8)`; a hit returns `DAT_107547a8[i]`. The key
	//    is the hash alone (a different file whose name collides returns this text).
	for (const FEntry& Entry : Entries)
	{
		if (Entry.Hash == Hash)
		{
			return &Entry;
		}
	}
	// 3. Miss: `Open(name, "rb", 0)` -- pathID 0, not the caller's. A failed open returns NULL.
	FKvFileHandle Handle = Fs.Open(Name, "rb", 0);
	if (Handle == nullptr)
	{
		LoadSite(Sites, TEXT("FUN_102482f0"), 0x102482f0u, TEXT("open"), TEXT("ok=0 size=0 interned=0"));
		return nullptr;
	}
	const int32 Size = Fs.Size(Handle);
	// `n + 1 >= remaining` (the arena, `DAT_105c530c`, 1 MiB at start) -> `Close(h)`, NULL.
	if (Size + 1 >= Remaining)
	{
		Fs.Close(Handle);
		LoadSite(Sites, TEXT("FUN_102482f0"), 0x102482f0u, TEXT("open"),
			FString::Printf(TEXT("ok=1 size=%d interned=0"), Size));
		return nullptr;
	}
	// `Read(arena, n, h)`, `Close(h)`, `arena[n] = 0`; record hash and pointer at `DAT_10856fa8`, count
	// up, `remaining -= n + 1`. Retail's tables (1040 hashes, 512 pointers) have no bound check; the
	// port's array grows instead of overrunning them.
	TArray<uint8> Bytes;
	Bytes.SetNumZeroed(Size);
	if (Size > 0)
	{
		Fs.Read(Bytes.GetData(), Size, Handle);
	}
	Fs.Close(Handle);
	FEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Hash = Hash;
	Entry.Size = Size;
	Entry.Text = MakeShared<FString>(WidenBytes(Bytes));
	Remaining -= Size + 1;
	LoadSite(Sites, TEXT("FUN_102482f0"), 0x102482f0u, TEXT("open"),
		FString::Printf(TEXT("ok=1 size=%d interned=1"), Size));
	return &Entry;
}

FKvFileTextCache& FileTextCache()
{
	static FKvFileTextCache Cache;
	return Cache;
}

// --- `0x102482b0`: the token wrapper -------------------------------------------------------------------

const FString& ReadToken(FKvReader& R, uint8* Quoted)
{
	// `r = 0x10247280(*cursor, 0x10754fa8, quoted); *cursor = r; return 0x10754fa8`. After end of text
	// the cursor is NULL and the buffer keeps its last contents (`""` from the EOF read).
	R.Pos = ElysiumKeyValues::NextToken(R, R.Token, Quoted);
	return R.Token;
}

// --- `0x10248510`: the block parser --------------------------------------------------------------------

void ParseBlock(FKvNode& Node, FKvReader& R)
{
	// Loop head `0x1024851b`; `local_c = this` is the block being filled.
	for (;;)
	{
		// 1. `key = read(cursor, NULL)`; `key == NULL || *key == 0` -> return. `""` is end of text and
		//    equally a quoted empty key, which ends the block early.
		const FString Key = ElysiumKeyValuesLoader::ReadToken(R, nullptr);
		if (Key.IsEmpty())
		{
			break;
		}
		// 2. `key == "}"` (2-byte compare against `0x10547b64`); the quote flag was not asked for.
		if (Key == TEXT("}"))
		{
			break;
		}
		// 3. `child = CreateChild(this, key)` (`0x10248870`), before the value is read.
		TSharedPtr<FKvNode> Child = ElysiumKeyValues::CreateChild(Node, Key);
		// 4. `val = read(cursor, &quoted)`. Copied: the recursion below reuses the shared buffer.
		uint8 Quoted = 0;
		const FString Val = ElysiumKeyValuesLoader::ReadToken(R, &Quoted);
		// 5. `val == "{"` (2-byte compare against `0x10547b78`; a quoted `"{"` opens a block too) ->
		//    recurse into the child, then back to 1. The child's type word stays unwritten.
		if (Val == TEXT("{"))
		{
			if (R.Sites != nullptr)
			{
				R.Sites->Site(TEXT("kv.pair"), TEXT("FUN_10248510"), 0x10248510u, TEXT("branch"),
					FString::Printf(TEXT("key=%s value={ quoted=%d block=1 type=unset"), *Shown(Key), Quoted));
			}
			ElysiumKeyValuesLoader::ParseBlock(*Child, R);
			continue;
		}
		// 6. Value: `free(child->+0x04)` (dead for a fresh child); `child->+0x04 = copy(val)`. A `}` in
		//    value position is a value, not a block end.
		Child->StringValue = Val;
		FString Numbers;
		if (Quoted != 0)
		{
			// 7. Quoted -> `+0x0C = 0`, no numeric parse.
			Child->Type = EKvType::String;
		}
		else
		{
			// 8. `ei = strtol(val, &end_i, 10)` (`0x104319a8`); `ef = strtod(val, &end_f)` (`0x1043190e`);
			//    unsigned end-pointer compares (`JBE` at `0x1024860c`, `0x1024861f`).
			int32 EndI = 0;
			const int32 IntValue = ElysiumKeyValues::RetailStrtol(Val, EndI);
			int32 EndF = 0;
			const double DoubleValue = ElysiumKeyValues::RetailStrtod(Val, EndF);
			Numbers = FString::Printf(TEXT(" ei=%d ef=%g"), IntValue, DoubleValue);
			if (EndF > EndI)
			{
				// `end_f > end_i` -> `+0x08 = (float)ef` (`FSTP float`), `+0x0C = 2`.
				Child->FloatValue = static_cast<float>(DoubleValue);
				Child->Type = EKvType::Float;
			}
			else if (EndI > 0)
			{
				// `end_i > val` -> `+0x08 = ei`, `+0x0C = 1`.
				Child->IntValue = IntValue;
				Child->Type = EKvType::Int;
			}
			else
			{
				// Neither consumed -> `+0x0C = 0`.
				Child->Type = EKvType::String;
			}
		}
		if (R.Sites != nullptr)
		{
			R.Sites->Site(TEXT("kv.pair"), TEXT("FUN_10248510"), 0x10248510u, TEXT("branch"),
				FString::Printf(TEXT("key=%s value=%s quoted=%d%s type=%s"), *Shown(Key), *Shown(Val), Quoted,
					*Numbers, FileTypeCode(Child->Type)));
		}
		// 9. Loop.
	}
	// The port's lookup index over the list this block now holds.
	ElysiumKeyValues::Reindex(Node);
}

// --- `0x102480f0`: the file loader ----------------------------------------------------------------------

void ParseRoots(const TSharedPtr<FKvNode>& Target, FKvReader& R, TArray<TSharedPtr<FKvNode>>& OutRoots)
{
	// Arm 6. `this_cur` starts as `this`, `prev` as NULL; the cursor advances in place.
	TSharedPtr<FKvNode> ThisCur = Target;
	int32 Index = 0;
	for (;;)
	{
		// `tok = read(&cursor, NULL)`; `cursor == NULL` -> break (an empty or all-comment text: no roots).
		const FString Tok = ElysiumKeyValuesLoader::ReadToken(R, nullptr);
		if (R.Pos == INDEX_NONE)
		{
			break;
		}
		TSharedPtr<FKvNode> Root;
		bool bReused = false;
		if (!ThisCur.IsValid())
		{
			// `operator_new(0x1c)`, ctor `0x10247ba0(node, tok)`; `0x10248a50(prev, node)` links it after
			// the previous root (`prev->+0x10`): `OutRoots`' order.
			Root = MakeShared<FKvNode>();
			Root->Name = Tok;
		}
		else
		{
			// The first root with `this` non-NULL: `free(root->+0x00)`, then the name setter `0x10247cf0`
			// (`FUN_10247cf0`): a copy of `tok` as the name; `+0x14` (children), `+0x10` (next), `+0x04`
			// (value text) and `+0x18` (chain) zeroed without being freed; `DAT_10753f68[0] = 0`. The
			// type word `+0x0C` is not written.
			Root = ThisCur;
			Root->Name = Tok;
			Root->Children.Reset();
			Root->StringValue.Reset();
			Root->Chain.Reset();
			ElysiumKeyValues::Reindex(*Root);
			bReused = true;
		}
		OutRoots.Add(Root);
		// `peek = read(&cursor, NULL)`. `root->+0x00 != NULL` (a copied name, always) `&& peek != NULL`
		// (the buffer, always) `&& peek[0] == '{'` (`CMP byte [EAX],0x7B`: the first byte only, so a
		// quoted `"{abc"` opens a block, unlike the 2-byte compare in `0x10248510`) -> the block;
		// otherwise the peeked token is consumed and dropped.
		const FString Peek = ElysiumKeyValuesLoader::ReadToken(R, nullptr);
		const bool bBrace = !Root->Name.IsEmpty() && Peek.Len() >= 1 && Peek[0] == TEXT('{');
		if (R.Sites != nullptr)
		{
			R.Sites->Site(TEXT("kv.root"), TEXT("FUN_102480f0"), 0x102480f0u, TEXT("branch"),
				FString::Printf(TEXT("index=%d name=%s reused=%d brace=%d%s"), Index, *Shown(Tok), bReused ? 1 : 0,
					bBrace ? 1 : 0, bBrace ? TEXT("") : *FString::Printf(TEXT(" dropped=%s"), *Shown(Peek))));
		}
		if (bBrace)
		{
			ElysiumKeyValuesLoader::ParseBlock(*Root, R);
		}
		// `prev = root; this_cur = NULL; loop`.
		ThisCur = nullptr;
		++Index;
	}
}

bool LoadFile(const TSharedPtr<FKvNode>& Target, const FKvLoadArgs& Args, TArray<TSharedPtr<FKvNode>>& OutRoots)
{
	// 1. `root = ECX; cursor = NULL; local_8 = NULL`.
	LoadSite(Args.Sites, TEXT("FUN_102480f0"), 0x102480f0u, TEXT("entry"),
		FString::Printf(TEXT("file=%s cache=%d"), *Shown(Args.FileName), Args.Cache != 0 ? 1 : 0));
	TSharedPtr<FString> Text;
	int32 Size = 0;
	// 2. `cache != 0`: `strstr(filename, ".txt")` (`0x10546e4c`; case-sensitive, anywhere in the name).
	//    Found -> `cursor = hook = 0x102482f0(filename, fs)`; a pointer skips the loader's own open.
	if (Args.Cache != 0)
	{
		const bool bNeedle = Args.FileName.Contains(TEXT(".txt"), ESearchCase::CaseSensitive);
		if (bNeedle && Args.Fs != nullptr)
		{
			if (const FKvFileTextCache::FEntry* Cached = FileTextCache().Lookup(Args.FileName, *Args.Fs, Args.Sites))
			{
				Text = Cached->Text;
				Size = Cached->Size;
			}
		}
		LoadSite(Args.Sites, TEXT("FUN_102480f0"), 0x102480f0u, TEXT("hook"),
			FString::Printf(TEXT("needle=%d result=%d"), bNeedle ? 1 : 0, Text.IsValid() ? 1 : 0));
	}
	FString Owned;
	if (!Text.IsValid())
	{
		// 3. `h = fs->Open(filename, "rb", pathID)`; `h == 0` -> return 0.
		// 4. `n = Size(h)`; `buf = malloc(n + 1)`; `Read(buf, n, h)`; `Close(h)`; `buf[n] = 0`; `local_8 =
		//    buf` (freed at return -- a hook text never is).
		const bool bOpened = Args.Fs != nullptr && ReadWholeFile(*Args.Fs, Args.FileName, Args.PathID, Size, Owned);
		LoadSite(Args.Sites, TEXT("FUN_102480f0"), 0x102480f0u, TEXT("open"),
			FString::Printf(TEXT("ok=%d size=%d"), bOpened ? 1 : 0, Size));
		if (!bOpened)
		{
			LoadSite(Args.Sites, TEXT("FUN_102480f0"), 0x102480f0u, TEXT("return"), TEXT("result=0 roots=0"));
			return false;
		}
	}
	// 5. `cursor = buf`. 6. The root loop.
	FKvReader Reader(Text.IsValid() ? *Text : Owned, Args.Sites);
	ParseRoots(Target, Reader, OutRoots);
	// 7. `local_8 != NULL -> free(local_8)`; return 1 (parse success is not reported).
	LoadSite(Args.Sites, TEXT("FUN_102480f0"), 0x102480f0u, TEXT("return"),
		FString::Printf(TEXT("result=1 roots=%d"), OutRoots.Num()));
	return true;
}

} // namespace ElysiumKeyValuesLoader
