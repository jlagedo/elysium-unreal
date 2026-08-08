#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"
#include "imgui.h"

// P8 8.2 glTFRuntime spike window. The debug surface for the runtime skeletal path: pick a VtMB NPC
// .glb from out/npc, choose a clip (or the first animation), and Load -- UElysiumNpcSubsystem runs it
// through glTFRuntime into a USkeletalMesh + UAnimSequence and spawns it near the player. The table
// reports what came back per load (bone count, animations in the glb, applied clip, load ms, spawn
// location); per-clip buttons re-play any animation. Clear destroys them. Scriptable echo: elysium.npc.*.
class FElysiumCogWindow_Npc : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;

private:
	// The map's live npc_*/npc_maker entities (B3) — what stands where and its latch state. Rendered
	// above the glTF test harness; reads the entity world, not the elysium.npc.load spike.
	void RenderLiveNpcs();

	// The facial flex rig (12.3): one slider per flex controller on every live rigged body, and the
	// weights the rules and ramps derive from them. Sliding `blink` closes the lids through four RPN
	// rules; the flexdesc and morph tables beside it are the two derived layers, live. Scriptable
	// echo: elysium.npc.flex / elysium.npc.flex_dump.
	void RenderFacial();

	// The body sample both producers publish (CCC1): the player's mover and every live NPC motor
	// filling one `FElysiumLocomotionSample`. Side by side on purpose — the contract's whole claim is
	// that the cast's locomotion and the player's are the same record, and two rows that disagree
	// about what a field means are visible here before they are visible in a pose.
	void RenderLocomotion();

	FString PendingStem;             // stem in the input box (glb under out/npc)
	FString PendingAnim;             // clip name in the input box ("" = first animation)
	FString LastError;               // last Load failure, shown inline
	bool bStemsDirty = true;         // rescan out/npc on first open / Rescan
	TArray<FString> Stems;           // cached available glb stems
	FString FacialFilter;            // stem substring the Facial tab addresses ("" = every body)
	bool bFacialNonZeroOnly = true;  // hide the flexdesc/morph rows sitting at rest
};

#endif // ENABLE_COG
