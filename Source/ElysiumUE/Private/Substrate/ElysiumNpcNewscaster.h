#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VNewscaster` (primary vtable `0x104bf634`), built by `npc_VNewscaster` factory
// `0x1039fef0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcNewscaster : public FElysiumNpc
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VNewscaster", FElysiumNpc)

	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual void UpdateOnRemove() override;
	virtual int32 IRelationType(FElysiumEntity* Candidate) override;
	virtual int32 DrawDebugTextOverlays() override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	/** `CNPC_VNewscaster`'s story-queue overlay (`0x103a0ff0`), whose lines the newscaster's slot-124
	 *  body adds to the SAME budget — the return is a COUNT, not a line index. **SEAM**: the body is
	 *  family Species' row (band 5–9, `FElysiumNpcNewscaster::FUN_103a0ff0`) and is already ported there; this
	 *  answers 0 until the two are wired, so the newscaster's return is the Troika body's unchanged. */
	int32 NewscasterStoryOverlayLines(int32 FirstLine);

	// From `ElysiumNpcSpecies.inl`.
	// The two queues and their cursors. `CNPC_VNewscaster` has no datamap in the corpus, so every name
	// here is WALKED off the bodies that touch it (`0x103a0270`, `0x103a0670`, `0x103a0ab0`,
	// `0x103a0d50`, `0x103a0ff0`) and says so.
	TArray<FNewscasterStory> NewscasterMainStories;   // +0x665c, count +0x6668 (walked)
	TArray<FNewscasterStory> NewscasterSideStories;   // +0x6670, count +0x667c (walked)
	int32 NewscasterPlayingSide = 0;                  // +0x668c — non-zero selects the SIDE queue (walked)
	int32 NewscasterMainCursor = INDEX_NONE;          // +0x6684 (walked)
	int32 NewscasterSideCursor = INDEX_NONE;          // +0x6688 (walked)
	bool bNewscasterStoryActive = false;              // +0x6690, cleared by the teardown (walked)
	/** `0x103a0d50` / `0x103a0ff0` — `CNPC_VNewscaster`'s story-queue teardown and debug listing. */
	void FUN_103a0d50();
	int32 FUN_103a0ff0(int32 FirstLine, TArray<FString>& OutLines) const;

	// From `ElysiumNpcSpeciesMisc10.inl`.
	/** `CNPC_VNewscaster::PlayNextNewscasterStory` (`0x103a0670`), 302 bytes, no slot.
	 *
	 *  **CLASS ATTRIBUTION CORRECTED** — its batch filed it under the Ming Xiao family; it is
	 *  `CNPC_VNewscaster`, whose loader `0x103a0ab0` and teardown `0x103a0d50` touch the same six words.
	 *
	 *  Order: with `m_bStoriesLoaded` (`+0x6690`) clear and `UTIL_PlayerByIndex(1)` (`0x101cd9e0`)
	 *  answering an entity, load, then seed both cursors with `RandomInt(0, count - 1)`; a missing
	 *  player returns WITHOUT loading. Then, unless `IsInDialog` (`0x102c1170`), and only when
	 *  `count0 + count1` is non-zero, roll `RandomInt(0, count0 + count1)` — an INCLUSIVE upper bound,
	 *  so the roll can equal the sum — and write 1 to `m_bPlayMainStory` (`+0x668c`) when it lands below
	 *  `count0` and 0 otherwise. A zero `+0x668c` or an empty main queue advances the SIDE cursor
	 *  (returning early on an empty side queue), anything else the MAIN cursor, each modulo its count;
	 *  the advanced row's SELECTED VERSION filename is then played through `0x102c0520`
	 *  (`OnDialogFilePlayed`) when it is non-null. */
	void PlayNextNewscasterStory();
	/** `CNPC_VNewscaster::LoadNewscasterStories` (`0x103a0ab0`), 525 bytes.
	 *
	 *  **PATH CORRECTED**: `0x1064aadc` and `0x1064aac0` are the FORMAT strings `"%sNewscaster_Main.txt"`
	 *  and `"%sNewscaster_Side.txt"`, handed to `UTIL_VarArgs` (`0x101d3730`) with `"vdata\system\"`
	 *  (`0x105a0f80`) — so the files are `vdata/system/Newscaster_Main.txt` and `…_Side.txt`, not the
	 *  `\s…` the walk read out of the `%s`.
	 *
	 *  Order: tear both queues down through `0x103a0d50` FIRST; open the main file as KeyValues and, for
	 *  each child key, zero a ten-word `0x28` scratch row, fill it with `0x103a07f0` and append it only
	 *  when that answers true; release the KeyValues; repeat verbatim for the side file; set
	 *  `m_bStoriesLoaded`. */
	void LoadNewscasterStories();
	/** Each `0x102c0520(this, filename, 0, 0)` the play body reached — `OnDialogFilePlayed`, family
	 *  Dialogue's row. Recorded here because nothing in this substrate plays a VCD from the kernel and
	 *  the CHOICE (which queue, which cursor, which version) is the whole of what this body decides. */
	TArray<FString> NewscasterPlayedFiles;
};
