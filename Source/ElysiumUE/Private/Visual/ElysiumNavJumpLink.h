#pragma once

#include "CoreMinimal.h"
#include "Navigation/NavLinkProxy.h"
#include "ElysiumNavJumpLink.generated.h"

class UNavLinkCustomComponent;

/** One used hull's verdict on one AIN jump link, as the bake recorded it (0018/7). Every field is
 *  the staged / baked value (`map_jump_links.py`, `bake_jump_links.py`), written through
 *  `SetHullVerdicts`; editable only so the bake's Python can build it. */
USTRUCT(BlueprintType)
struct FElysiumNavJumpHullVerdict
{
	GENERATED_BODY()

	/** The retail hull index (`link+0x0c+4*hull`). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Navigation")
	int32 Hull = INDEX_NONE;
	/** The hull's motion word: 1 ground, 2 jump, 4 fly, 8 climb (`InitLinks 0x102fb4e0`). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Navigation")
	int32 MotionWord = 0;
	/** The word is exactly 2 -- the only word `0x102ff960` would hand to `IsJumpLegal`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Navigation")
	bool bJumpOnly = false;
	/** `0x102ff960` step 2 passes for some NPC -- the staged `capabilityUsable`. False on every
	 *  shipped graph: no NPC holds bit 2 (`map_jump_links.JUMP_CAPABILITY_HELD`). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Navigation")
	bool bCapabilityUsable = false;
	/** Slot 521's geometry (80 / 250 / 160) src -> dst at this hull's endpoints; asked only when
	 *  bJumpOnly, and moot because step 2 refuses first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Navigation")
	bool bLegalForward = false;
	/** The same, dst -> src. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elysium|Navigation")
	bool bLegalBack = false;
};

// The map's record of one AIN jump link -- NEVER a route.
//
// Retail's link predicate `0x102ff960` ANDs the NPC's capabilities (slot 513 `CapabilitiesGet`,
// `+0x804/4`, `m_afCapability` +0x5cec) with the link's per-hull word (`link+0x0c+4*hull`,
// `102ff98d`) and refuses zero (`102ff995`); only exactly 2 (`102ff9c8`) reaches `IsJumpLegal`
// (slot 521, `+0x824/4`). No shipped NPC holds bit 2 -- every `CapabilitiesAdd` literal lacks
// it, the fly toggles `0x1038c170` / `0x103580d0` pass 1 or 4 -- so every jump-only link is refused
// at the capability step for every NPC (`docs/vtmb/navigation-jump-links.md` "The link predicate").
// A traversable link would add routes retail does not have and so change event order. Every
// instance therefore carries an EMPTY agent selector and a disabled smart link: no agent's mesh
// gets the link, and the path follower never reaches `OnJumpLinkReached`. The actor keeps the
// endpoints, the pair, `bBidirectional` and the per-hull verdicts for the debugger view (0018/19).
UCLASS(NotBlueprintable)
class AElysiumNavJumpLink final : public ANavLinkProxy
{

	GENERATED_BODY()

public:
	AElysiumNavJumpLink(const FObjectInitializer& ObjectInitializer);

	/** The endpoints and the pair; also makes the link non-traversable (no agents, disabled). */
	UFUNCTION(BlueprintCallable, Category="Elysium|Bake")
	void ConfigureJumpLink(FVector RelativeStart, FVector RelativeEnd,
		int32 InSourceLinkIndex, int32 InSourceNode, int32 InDestinationNode);

	/** The bake's per-hull verdicts, one per used hull, as staged and asked -- stored verbatim.
	 *  False and nothing written when a verdict is self-contradictory: a legal direction on a hull
	 *  whose word is not jump-only (step 4 is only ever asked of a word of exactly 2). */
	UFUNCTION(BlueprintCallable, Category="Elysium|Bake")
	bool SetHullVerdicts(const TArray<FElysiumNavJumpHullVerdict>& Verdicts);

	/** The staged pair's `bidirectional` -- data only; nothing routes on it. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Bake")
	void SetBidirectional(bool bInBidirectional) { bBidirectional = bInBidirectional; }

	/** Whether any engine path could use this actor: a simple link, an agent, or an enabled
	 *  smart link. The bake and `verify nav` both require false. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Navigation")
	bool IsTraversable() const;

	/** The smart link's agent bits (without the initialised bit); 0 on every baked link. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Navigation")
	int32 SupportedAgentBits() const;

	UPROPERTY(VisibleAnywhere, Category="Elysium|Navigation")
	int32 SourceLinkIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Navigation")
	int32 SourceNode = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Navigation")
	int32 DestinationNode = INDEX_NONE;
	/** Retail installs one link at both endpoints (`0x102f626f` / `0x102f629f`); data only. */
	UPROPERTY(VisibleAnywhere, Category="Elysium|Navigation")
	bool bBidirectional = true;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Navigation")
	TArray<FElysiumNavJumpHullVerdict> HullVerdicts;

private:
	void MakeNonTraversable();
	void OnJumpLinkReached(UNavLinkCustomComponent* Link, UObject* PathingAgent,
		const FVector& Destination);
};
