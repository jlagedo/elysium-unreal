#include "Audio/ElysiumSoundScript.h"

#include "ElysiumRetailSite.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSoundScript, Log, All);

namespace
{
	// The channel table at 0x10598c78 (stride 8: value, then the name pointer), decoded from the PE;
	// `FUN_101b24d0`'s loop runs `i = 0..6` (`CMP ESI,7`).
	struct FChannelName
	{
		int32 Value;
		const TCHAR* Name;
	};
	const FChannelName GChannelTable[] =
	{
		{ 0, TEXT("CHAN_AUTO") },     // 0x105990bc
		{ 1, TEXT("CHAN_WEAPON") },   // 0x105990ac
		{ 2, TEXT("CHAN_VOICE") },    // 0x1059909c
		{ 3, TEXT("CHAN_ITEM") },     // 0x10599090
		{ 4, TEXT("CHAN_BODY") },     // 0x10599084
		{ 5, TEXT("CHAN_STREAM") },   // 0x10599074
		{ 6, TEXT("CHAN_STATIC") },   // 0x10599064
	};
	constexpr int32 GChannelTableCount = 7;

	// The prefix at 0x10599140, lowercase; its length (5) is taken by `SCASB` at run time.
	const TCHAR* const GChannelPrefix = TEXT("chan_");

	// The four default names the constructor copies (0x10599058, 0x105990bc, 0x10598f28, 0x10599048) and
	// the three float constants (`0x3f800000`, `0x42c80000`, `0x42960000`).
	const TCHAR* const GVolNorm = TEXT("VOL_NORM");
	const TCHAR* const GChanAuto = TEXT("CHAN_AUTO");
	const TCHAR* const GSndLvlNorm = TEXT("SNDLVL_NORM");
	const TCHAR* const GPitchNorm = TEXT("PITCH_NORM");
	constexpr float GDefaultVolume = 1.0f;
	constexpr float GDefaultPitch = 100.0f;      // the engine's pitch PERCENTAGE, not a multiplier
	constexpr float GDefaultSoundLevel = 75.0f;  // SNDLVL_NORM

	// vstdlib `Q_strncasecmp(a, b, n)`: 0 when the first `n` characters agree case-insensitively, or
	// when both strings end together before `n`; else -1. It upper-cases ASCII `a`-`z` only, so `""`
	// against `"chan_"` differs at the first character and `"chan"` at the fifth.
	int32 QStrncasecmp(const TCHAR* A, const TCHAR* B, int32 N)
	{
		auto Upper = [](TCHAR C) -> TCHAR { return (C >= TEXT('a') && C <= TEXT('z')) ? static_cast<TCHAR>(C - 32) : C; };
		for (int32 I = 0; I < N; ++I)
		{
			const TCHAR CA = A[I];
			const TCHAR CB = B[I];
			if (Upper(CA) != Upper(CB))
			{
				return -1;
			}
			if (CA == TEXT('\0'))
			{
				return 0;   // both ended together
			}
		}
		return 0;
	}

	// vstdlib `Q_strcasecmp`: a full case-insensitive compare, the same ASCII fold.
	bool QStrcasecmpEqual(const TCHAR* A, const TCHAR* B)
	{
		auto Upper = [](TCHAR C) -> TCHAR { return (C >= TEXT('a') && C <= TEXT('z')) ? static_cast<TCHAR>(C - 32) : C; };
		for (int32 I = 0;; ++I)
		{
			if (Upper(A[I]) != Upper(B[I]))
			{
				return false;
			}
			if (A[I] == TEXT('\0'))
			{
				return true;
			}
		}
	}

	void ParseSite(IElysiumRetailSiteSink* Sites, const TCHAR* Text, int32 Ret, int32 Warn)
	{
		if (Sites != nullptr)
		{
			Sites->Site(TEXT("sndchan.parse"), TEXT("Global::FUN_101b24d0"), 0x101b24d0u, TEXT("return"),
				FString::Printf(TEXT("text=%s ret=%d warn=%d"), Text != nullptr ? Text : TEXT("null"), Ret, Warn));
		}
	}
}

namespace ElysiumSoundScript
{
	void CopyName(TCHAR (&Dst)[NameBufferSize], const TCHAR* Src)
	{
		// `Q_strncpy(dst, src, 0x20)` (vstdlib 0x100037b0): `strncpy` -- the name's characters, then
		// zero padding to 32 -- and `dst[0x1f] = 0`.
		const TCHAR* S = Src != nullptr ? Src : TEXT("");
		int32 I = 0;
		for (; I < NameBufferSize && S[I] != TEXT('\0'); ++I)
		{
			Dst[I] = S[I];
		}
		for (; I < NameBufferSize; ++I)
		{
			Dst[I] = TEXT('\0');
		}
		Dst[NameBufferSize - 1] = TEXT('\0');
	}

	FParams& Construct(FParams& Params, IElysiumRetailSiteSink* Sites)
	{
		// The sixteen writes of `FUN_101b30d0`, in retail order (`walks/L0-r007.md` § 0x101b30d0).
		// 1.  Dwords +0x20..+0x44 = 0: the two vectors start empty (0x101b30e2-0x101b3102).
		Params.Waves.Reset();
		Params.SecondList.Reset();
		// 2.  +0x00 = 0 (CHAN_AUTO).
		Params.Channel = 0;
		// 3.  `Q_strncpy(+0x69, "CHAN_AUTO", 0x20)`.
		CopyName(Params.ChannelText, GChanAuto);
		// 4.  +0x04 = 1.0f.  5. +0x08 = 0.
		Params.Volume.Start = GDefaultVolume;
		Params.Volume.Range = 0.f;
		// 6.  `Q_strncpy(+0x49, "VOL_NORM", 0x20)`.
		CopyName(Params.VolumeText, GVolNorm);
		// 7.  +0x0c = 100.0f.  8. +0x10 = 0.
		Params.Pitch.Start = GDefaultPitch;
		Params.Pitch.Range = 0.f;
		// 9.  `Q_strncpy(+0xa9, "PITCH_NORM", 0x20)`.
		CopyName(Params.PitchText, GPitchNorm);
		// 10. +0x14 = 75.0f.  11. +0x18 = 0.
		Params.SoundLevel.Start = GDefaultSoundLevel;
		Params.SoundLevel.Range = 0.f;
		// 12. `Q_strncpy(+0x89, "SNDLVL_NORM", 0x20)`.
		CopyName(Params.SoundLevelText, GSndLvlNorm);
		// 13. byte +0x1c = 0 (`play_to_owner_only`).  14. byte +0x1d = 1 (`precache`).  15. byte +0x48 = 0.
		Params.bPlayToOwnerOnly = 0;
		Params.bPrecache = 1;
		Params.Flag48 = 0;
		// 16. `MOV EAX,ESI` (0x101b3162): return `desc`.
		if (Sites != nullptr)
		{
			Sites->Site(TEXT("sndscript.defaults"), TEXT("Global::FUN_101b30d0"), 0x101b30d0u, TEXT("return"),
				FString::Printf(TEXT("channel=%d volume=%.1f pitch=%.0f level=%.0f flags=%d,%d,%d"), Params.Channel,
					Params.Volume.Start, Params.Pitch.Start, Params.SoundLevel.Start, Params.bPlayToOwnerOnly,
					Params.bPrecache, Params.Flag48));
		}
		return Params;
	}

	int32 TextToChannel(const TCHAR* Name, IElysiumRetailSiteSink* Sites)
	{
		// Arm 1. `name == NULL` (test 0x101b24d8, `JZ` 0x101b24db): 0, no warning (`RET` 0x101b253f).
		if (Name == nullptr)
		{
			ParseSite(Sites, nullptr, 0, 0);
			return 0;
		}
		// Arm 2. `Q_strncasecmp(name, "chan_", strlen("chan_")) != 0` -> `atoi(name)` (call 0x101b24ff,
		//        `RET` 0x101b250a): `""` -> 0, `"chan"` -> 0, `" 3"` -> 3, `"3x"` -> 3, `"-2"` -> -2.
		if (QStrncasecmp(Name, GChannelPrefix, 5) != 0)
		{
			const int32 Value = FCString::Atoi(Name);
			ParseSite(Sites, Name, Value, 0);
			return Value;
		}
		// Arm 3. The prefix matches: `Q_strcasecmp` against the seven table names in order; a hit returns
		//        the table value (`MOV` 0x101b2540, `RET` 0x101b254a).
		for (int32 I = 0; I < GChannelTableCount; ++I)
		{
			if (QStrcasecmpEqual(Name, GChannelTable[I].Name))
			{
				ParseSite(Sites, Name, GChannelTable[I].Value, 0);
				return GChannelTable[I].Value;
			}
		}
		// Arm 4. No table name matches: `DevMsg("CSoundEmitterSystem:  Warning, unknown channel type in
		//        sounds.txt (%s)\n", name)` (string 0x105990e8, call 0x101b2531) and 0 (`XOR EAX,EAX`
		//        0x101b253a). tier0's `DevMsg` prints only when the `developer` spew level is at least 1
		//        (`tier0.dll` 0x100011ae -> `IsSpewActive` 0x1000146a); the port's gate is the log
		//        category's verbosity -- a named instrumentation swap. The site records the call.
		UE_LOG(LogElysiumSoundScript, Verbose, TEXT("CSoundEmitterSystem:  Warning, unknown channel type in sounds.txt (%s)"),
			Name);
		ParseSite(Sites, Name, 0, 1);
		return 0;
	}

	void SetChannel(FParams& Params, const TCHAR* Name, IElysiumRetailSiteSink* Sites)
	{
		// `*desc = FUN_101b24d0(name); Q_strncpy(desc + 0x69, name, 0x20);` -- the text is kept as the
		// key spelled it (`"chan_voice"` stays lowercase; a numeric `"2"` is kept as `"2"`).
		Params.Channel = TextToChannel(Name, Sites);
		CopyName(Params.ChannelText, Name);
	}
}
