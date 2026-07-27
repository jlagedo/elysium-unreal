#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

// P2.7 — a bounded ring of recent log lines, so a caller that was not watching when something
// happened can still read it back. Same shape as the entity world's I/O ring buffer
// (`FElysiumRingBufferSink`) and for the same reason: postmortem forensics without having had
// logging on. Backs the `elysium_log_tail` MCP tool and the output capture around
// `elysium_console_exec`.
//
// Installed on GLog for the process lifetime by UElysiumMcpSubsystem. Log serialization can come
// from any thread, so the ring is mutex-guarded.
class FElysiumLogTap final : public FOutputDevice
{
public:
	struct FLine
	{
		FString Category;
		FString Verbosity;
		FString Text;
	};

	explicit FElysiumLogTap(int32 InCapacity = 2000);
	virtual ~FElysiumLogTap() override;

	// --- FOutputDevice ---
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

	// Add to / remove from GLog. Install is idempotent.
	void Install();
	void Remove();
	bool IsInstalled() const { return bInstalled; }

	// The last N lines in chronological order (N <= 0 = all held). `CategoryFilter` keeps only
	// lines whose category contains it (case-insensitive); empty keeps everything.
	void CollectOrdered(int32 LastN, const FString& CategoryFilter, TArray<FLine>& Out) const;

	// A monotonic counter of lines seen. Take it before an operation and pass it to
	// CollectSince to read back exactly what that operation logged.
	uint64 Cursor() const;
	void CollectSince(uint64 SinceCursor, TArray<FLine>& Out) const;

	int32 Capacity() const { return Buffer.Num(); }

private:
	mutable FCriticalSection Lock;
	TArray<FLine> Buffer;   // fixed-size ring
	int32 Head = 0;         // next write slot
	int32 Count = 0;        // entries written (saturates at Capacity)
	uint64 Total = 0;       // lines ever seen (the cursor)
	bool bInstalled = false;
};

// The one process-wide tap. UElysiumMcpSubsystem installs it at engine init and removes it at
// teardown; the MCP tools read it. Constructed on first call, so a caller never sees a null.
FElysiumLogTap& ElysiumLogTapGet();
