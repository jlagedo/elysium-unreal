#pragma once

#include "CoreMinimal.h"

// P2.7/2.9 — the one viewport-capture path, shared by the `elysium_screenshot` MCP tool (which
// returns the PNG inline to the agent) and the `-ElysiumShots` regression harness (which writes
// it to tools/out/_shots). Both need the same two things: a frame's back buffer as an FColor
// bitmap, and that bitmap as PNG bytes.
//
// Capture is inherently deferred — the engine services a screenshot request at the end of the
// NEXT rendered frame — so Request is callback-shaped, with a frame budget so a caller can never
// hang forever on a viewport that stops presenting (a minimised window, a stalled RHI).
namespace ElysiumScreenshot
{
	// Bitmap is row-major top-down, Width*Height entries. Alpha is already forced opaque (the
	// back buffer's alpha is meaningless and reads as fully transparent in most PNG viewers).
	// A failed/timed-out capture calls back with an empty bitmap and 0x0 dimensions.
	using FOnCaptured = TFunction<void(int32 Width, int32 Height, const TArray<FColor>& Bitmap)>;

	// Ask the game viewport for its next frame. `OnCaptured` runs on the game thread, exactly once.
	// Returns false (without ever calling back) when there is no game viewport to capture.
	// `TimeoutFrames` bounds the wait; the default covers a slow first frame after a map load.
	// bShowUI selects whether Slate/UMG is composited into the capture. It defaults to **false**
	// because that is what the screenshot-regression harness needs: a shot must not change when a
	// Cog window happens to be open. The agent-facing MCP tool passes **true** — its job is to show
	// what the player sees, and since 8.6 that includes the menu and every other UMG screen. The
	// Canvas HUD draws with the world and appears either way.
	bool Request(const FOnCaptured& OnCaptured, int32 TimeoutFrames = 300, bool bShowUI = false);

	// Encode a captured bitmap as PNG. Returns false on an empty bitmap or an encoder failure.
	bool EncodePng(int32 Width, int32 Height, const TArray<FColor>& Bitmap, TArray64<uint8>& OutPng);

	// Encode and write to AbsolutePath (creating the directory tree). Returns false on either half.
	bool SavePng(int32 Width, int32 Height, const TArray<FColor>& Bitmap, const FString& AbsolutePath);
}
