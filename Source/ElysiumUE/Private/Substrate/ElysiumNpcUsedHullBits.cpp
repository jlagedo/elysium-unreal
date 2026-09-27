#include "Substrate/ElysiumNpcUsedHullBits.h"

namespace ElysiumNpcUsedHullBits
{
	namespace Storage
	{
		// `DAT_10610be8`. Global in retail, so global here.
		int32 GUsedHullBits = 0;
		// `DAT_1093412c`, the second word `0x102f9900` clears with it. Nothing in the kernel reads
		// it, so what it IS stays **unrecovered**; it is cleared because the retail body clears it.
		int32 GUsedHullCompanion = 0;
	}

	int32 Get()
	{
		return Storage::GUsedHullBits;
	}

	void Clear()
	{
		Storage::GUsedHullBits = 0;
		Storage::GUsedHullCompanion = 0;
	}

	void Add(int32 Bits)
	{
		Storage::GUsedHullBits |= Bits;
	}
}
