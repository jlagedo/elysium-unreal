#include "ElysiumEntityWorld.h"

#include "ElysiumClipMovement.h"   // the melee stop's own recovered rule
#include "ElysiumComboChain.h"   // ElysiumCombo::In* — the FILE's own button bits the masks are in
#include "ElysiumPlayer.h"
#include "ElysiumUserCmd.h"   // EElysiumButton — the combat button field this file drains
#include "ElysiumViewState.h"
#include "Substrate/ElysiumDataObjects.h"   // the type-1 touch-link head the touch pass creates (L0-r019)
#include "Substrate/ElysiumEntityWorldShared.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumItemTable.h"   // EElysiumItemType — the weapon frame's melee/ranged split
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumWeaponClasses.h"

void FElysiumEntityWorld::RouteEntityTouch(const FElysiumEntityHandle& Brush,
	const FElysiumEntityHandle& Activator, bool bBegin)
{
	// The pair lives in two stores, written at the same retail points: r015's stamped key (what the
	// deferred untouch check `PhysicsCheckForEntityUntouch` 0x1003d490 compares against `m_touchStamp`)
	// and r019's two type-1 touch-link lists (what slot 207 `IsCurrentlyTouching` 0x1003d3d0 reads as
	// `HasDataObjectType(this, 1)`). A key is held exactly while both sides hold a node for each other.
	const uint64 TouchKey = (static_cast<uint64>(static_cast<uint32>(Brush.Index)) << 32)
		| static_cast<uint32>(Activator.Index);
	if (!bBegin)
	{
		// Collision is switched off as part of Hide/Kill, after the entity has become inert. Release
		// the physical pair before the liveness gate so a later Unhide while still intersecting can
		// produce a fresh begin edge.
		if (ActiveTouches.Remove(TouchKey) == 0)
		{
			return;
		}
		// The end edge's data-object steps (L0-r019). A pair that was retained holds a type-1 touch
		// link on each side, released here whatever the gates below answer:
		// - a live brush is `PhysicsCheckForEntityUntouch` 0x1003d490 on the mover (the activator):
		//   `PhysicsNotifyOtherOfUntouch` 0x1003d640 first unlinks the OTHER side's (the brush's) node
		//   and destroys its object when the list is then empty; then the mover's own node goes
		//   (`PhysicsRemoveToucher`) and, after the loop, the mover's object when its list is empty;
		// - a dying brush is `~CBaseEntity` -> `PhysicsRemoveTouchedList` 0x1003d8f0 on the brush: the
		//   other side (the activator) is notified first, then the brush's own nodes are freed and its
		//   object destroyed unconditionally.
		// `EndTouch` on the brush comes before its unlink (`PhysicsRemoveToucher` 0x1003d770: the
		// EndTouch, then `N[2][3] = N[3]; N[3][2] = N[2]`), so the dispatch below runs first.
		FElysiumEntity* Brushed = Resolve(Brush);
		FElysiumEntity* Mover = Resolve(Activator);
		const bool bDispatch = bActive && IsTriggerResolutionEnabled() && Brushed != nullptr && !Brushed->IsInert();
		if (bDispatch)
		{
			++TouchEndCount;
			Brushed->OnTouchEnd(Activator);
			UE_LOG(LogElysiumWorld, Verbose, TEXT("(%8.3f) touch end %s"), NowSeconds(), *Brushed->DebugString());
		}
		if (Brushed != nullptr && Brushed->IsInert())
		{
			// A dormant/dead brush cannot be touched. This is also the retail asymmetry: `~CBaseEntity`
			// reaches PhysicsRemoveTouchedList, which notifies the OTHER side of each link and frees it
			// without PhysicsRemoveToucher -- so a dying trigger never receives its own EndTouch, and a
			// self-removing trigger_once emits no final OnEndTouch to occupants still inside it
			// (entity_io.md). Kill() flips bDead before releasing contacts, which is what routes the
			// release here (`Elysium.Substrate.DyingTriggerEndTouch`).
			if (Mover != nullptr)
			{
				TouchLinkEnd(*Mover, Brush);
			}
			TouchLinkEnd(*Brushed, Activator);
			return;
		}
		if (Brushed != nullptr)
		{
			TouchLinkEnd(*Brushed, Activator);
		}
		if (Mover != nullptr)
		{
			TouchLinkEnd(*Mover, Brush);
		}
		return;
	}
	// Engine overlap callbacks can arrive while procedural collision and the pawn placement are
	// still settling. Dormant begins are deliberately forgotten: activation reconciles final
	// containment after authoritative placement. Ends still release an already-retained pair above,
	// even while the gameplay gate is closed or the brush has become inert.
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		return;
	}

	FElysiumEntity* E = Resolve(Brush);
	if (!E || E->IsInert())
	{
		// A dormant/dead brush cannot be touched.
		return;
	}

	// Begin/end are edges, not level-triggered calls. Engine movement normally supplies exactly
	// one of each, but a teleport reconciliation also asks which brushes contain the player after
	// the transform. Collapse that second observation here so an authored trigger never double-
	// fires; an end releases the pair so a later genuine re-entry remains an edge.
	if (!E->CanBeginTouch(Activator))
	{
		return;
	}
	// Each side's touchlink carries its owner's `m_touchStamp` (+0x1ac): `PhysicsMarkEntityAsTouched`
	// 0x1003dc70 writes it when the link is made (`node[1] = this->m_touchStamp`) and refreshes it
	// when an existing pair is reported again (`entity_io.md` § The touch dispatch path), which is
	// what keeps the link alive through `PhysicsCheckForEntityUntouch` 0x1003d490.
	FElysiumEntity* Mover = Resolve(Activator);
	FTouchLinkStamps Stamps;
	Stamps.Brush = E->TouchStamp;
	Stamps.Activator = Mover != nullptr ? Mover->TouchStamp : 0;
	if (FTouchLinkStamps* Existing = ActiveTouches.Find(TouchKey))
	{
		// The existing node on each side has its stamp refreshed and `PhysicsTouch` runs (the
		// lifecycle story's); no second node, no second StartTouch.
		*Existing = Stamps;
		TouchLinkBegin(*E, Activator);
		if (Mover != nullptr)
		{
			TouchLinkBegin(*Mover, Brush);
		}
		return;
	}
	ActiveTouches.Add(TouchKey, Stamps);

	// `PhysicsMarkEntitiesAsTouching` 0x1003e2e0 marks the pair in both directions, the enumerated
	// element (the brush) first: `PhysicsMarkEntityAsTouched(brush, mover)` -- the brush's type-1 head
	// and node, then its `PhysicsStartTouch` -- and then `(mover, brush)` (L0-r019; the early-outs
	// and `PhysicsTouch` belong to the touch-lifecycle story).
	TouchLinkBegin(*E, Activator);
	++TouchBeginCount;
	E->OnTouchStart(Activator);
	if (Mover != nullptr)
	{
		TouchLinkBegin(*Mover, Brush);
	}
	UE_LOG(LogElysiumWorld, Verbose, TEXT("(%8.3f) touch begin %s"), NowSeconds(), *E->DebugString());
}

namespace
{
	// `touchlink_t` list walk (0x1003dc70 / 0x1003d490 / 0x1003d640 / 0x1003d8f0): `H[+8]` is next,
	// `H[+0xC]` prev; the head is its own neighbour when the list is empty.
	FElysiumTouchLink* ElysiumWorldFindTouchLink(FElysiumTouchLink* Head, const FElysiumEntityHandle& Other)
	{
		for (FElysiumTouchLink* Node = Head->NextLink; Node != Head; Node = Node->NextLink)
		{
			if (Node->EntityTouched == Other)
			{
				return Node;
			}
		}
		return nullptr;
	}

	// Which side of a packed (brush, activator) pair names `Index`, and that side's link stamp.
	bool TouchPairSide(uint64 Key, int32 Index, bool& bOutBrushSide)
	{
		const int32 BrushIndex = static_cast<int32>(static_cast<uint32>(Key >> 32));
		const int32 ActivatorIndex = static_cast<int32>(static_cast<uint32>(Key));
		if (BrushIndex == Index) { bOutBrushSide = true; return true; }
		if (ActivatorIndex == Index) { bOutBrushSide = false; return true; }
		return false;
	}
}

void FElysiumEntityWorld::TouchLinkBegin(FElysiumEntity& Entity, const FElysiumEntityHandle& Other)
{
	// `PhysicsMarkEntityAsTouched` 0x1003dc70's data-object step: `H = GetDataObject(this, 1)`; when
	// none, `H = CreateDataObject(this, 1)` and the zero-filled block becomes the empty circular head
	// (`H[+0xC] = H; H[+8] = H`), with no null test on the create result.
	FElysiumTouchLink* Head = static_cast<FElysiumTouchLink*>(Entity.GetDataObject(FElysiumDataObjectAccessSystem::TouchLink));
	if (Head == nullptr)
	{
		Head = static_cast<FElysiumTouchLink*>(Entity.CreateDataObject(FElysiumDataObjectAccessSystem::TouchLink));
		if (Head == nullptr)
		{
			// Retail would fault here (no accessor registered). The port's registry always has slot 1,
			// so this is an entity outside a world: nothing to link.
			return;
		}
		Head->PrevLink = Head;
		Head->NextLink = Head;
	}
	// An existing link for the pair only refreshes its stamp (`node[1] = this->m_touchStamp`) and runs
	// `PhysicsTouch`: never a second begin and never a second node.
	if (FElysiumTouchLink* Existing = ElysiumWorldFindTouchLink(Head, Other))
	{
		Existing->TouchStamp = Entity.TouchStamp;
		return;
	}
	// `AllocTouchLink` (pool `DAT_106bd904`, counter `DAT_106bd9b8`): the cap is 0x200 -- retail prints
	// "AllocTouchLink: MAX_TOUCHLINKS limit" and answers no link. The pool bodies (`thunk_FUN_1013dc60`
	// / `thunk_FUN_1013dce0`) are UNRECOVERED; `new` / `delete` stand in.
	if (LiveTouchLinks >= 0x200)
	{
		UE_LOG(LogElysiumWorld, Error, TEXT("AllocTouchLink: MAX_TOUCHLINKS limit"));
		return;
	}
	++LiveTouchLinks;
	FElysiumTouchLink* Node = new FElysiumTouchLink();
	Node->EntityTouched = Other;
	// `[1]` = this entity's `m_touchStamp` (+0x1ac) at link time (`puVar9[1] = this->field_0x1ac`); the
	// stamp's producer is `SetCheckUntouch` 0x100b11d0 (slot 6, `ElysiumEntityCollision.cpp`).
	Node->TouchStamp = Entity.TouchStamp;
	// Insert at the head: `node[2] = H[2]; node[3] = H; H[2] = node; node[2][3] = node`.
	Node->NextLink = Head->NextLink;
	Node->PrevLink = Head;
	Head->NextLink = Node;
	Node->NextLink->PrevLink = Node;
	// `|= 1`: the begin dispatched (the bit `PhysicsRemoveToucher` tests before `EndTouch`).
	Node->Flags |= 1;
}

void FElysiumEntityWorld::TouchLinkEnd(FElysiumEntity& Entity, const FElysiumEntityHandle& Other)
{
	// `GetDataObject(this, 1)`; a side with no head has nothing to release.
	FElysiumTouchLink* Head = static_cast<FElysiumTouchLink*>(Entity.GetDataObject(FElysiumDataObjectAccessSystem::TouchLink));
	if (Head == nullptr)
	{
		return;
	}
	if (FElysiumTouchLink* Node = ElysiumWorldFindTouchLink(Head, Other))
	{
		// `PhysicsRemoveToucher` 0x1003d770's unlink and free: `N[2][3] = N[3]; N[3][2] = N[2]`,
		// `DAT_106bd9b8--`, the node back to the pool.
		Node->NextLink->PrevLink = Node->PrevLink;
		Node->PrevLink->NextLink = Node->NextLink;
		delete Node;
		--LiveTouchLinks;
	}
	// `PhysicsCheckForEntityUntouch` 0x1003d490 (after its loop) and `PhysicsNotifyOtherOfUntouch`
	// 0x1003d640 (for the other side): `if (H[+8] == H && H[+0xC] == H) DestroyDataObject(this, 1)`.
	if (Head->NextLink == Head && Head->PrevLink == Head)
	{
		Entity.DestroyDataObject(FElysiumDataObjectAccessSystem::TouchLink);
	}
}

void FElysiumEntityWorld::ReleaseTouchedList(FElysiumEntity& Entity)
{
	// `PhysicsRemoveTouchedList` 0x1003d8f0 (`~CBaseEntity` 0x1009df20, `DAT_10735d38` cleared around
	// it), arms in retail order:
	// 1. `H = GetDataObject(this, 1)`; none -> arm 4.
	// 2. Per node (the next captured first): `PhysicsNotifyOtherOfUntouch(this, other)` 0x1003d640 --
	//    the OTHER side's node for this entity found and `PhysicsRemoveToucher(other, node)` 0x1003d770
	//    run on it (`other->EndTouch(this)` through vslot 176 when `node[4] & 1` and the handle
	//    resolves, then its unlink and free), the other's object destroyed when its list is then
	//    empty; then this node freed to the pool (`DAT_106bd9b8--`) with NO `EndTouch` on this entity.
	// 3. `DestroyDataObject(this, 1)`, unconditional.
	// 4. `m_touchStamp (+0x1ac) = 0`.
	// A pair whose key is retained is released through the end edge above, which is arm 2 for it in
	// retail order: the key dropped, the other side's `OnTouchEnd` when it is the live brush (this
	// entity's own never: a dying brush is inert there), then the two nodes with each side's
	// destroy-if-empty. A node with no key (one made outside the touch pass) is freed below.
	if (Entity.GetDataObject(FElysiumDataObjectAccessSystem::TouchLink) != nullptr)
	{
		TArray<uint64> Keys;
		for (const TPair<uint64, FTouchLinkStamps>& Pair : ActiveTouches)
		{
			bool bBrushSide = false;
			if (TouchPairSide(Pair.Key, Entity.Handle.Index, bBrushSide))
			{
				Keys.Add(Pair.Key);
			}
		}
		Keys.Sort();   // the entity-index order every touch pass here uses in place of the list's
		for (uint64 Key : Keys)
		{
			RouteEntityTouch(FElysiumEntityHandle(static_cast<int32>(static_cast<uint32>(Key >> 32)), Epoch),
				FElysiumEntityHandle(static_cast<int32>(static_cast<uint32>(Key)), Epoch), /*bBegin*/ false);
		}
	}
	if (FElysiumTouchLink* Head = static_cast<FElysiumTouchLink*>(Entity.GetDataObject(FElysiumDataObjectAccessSystem::TouchLink)))
	{
		while (Head->NextLink != Head)
		{
			FElysiumTouchLink* Node = Head->NextLink;
			if (FElysiumEntity* OtherSide = Resolve(Node->EntityTouched))
			{
				// `PhysicsNotifyOtherOfUntouch` for a node no key names: the other side's node and its
				// destroy-if-empty; no retained pair, so no `EndTouch` to dispatch.
				TouchLinkEnd(*OtherSide, Entity.Handle);
			}
			Head->NextLink = Node->NextLink;
			Node->NextLink->PrevLink = Head;
			delete Node;
			--LiveTouchLinks;
		}
		Entity.DestroyDataObject(FElysiumDataObjectAccessSystem::TouchLink);
	}
	Entity.TouchStamp = 0;
}

void FElysiumEntityWorld::EndBrushTouches(const FElysiumEntityHandle& Brush)
{
	if (!Brush.IsSet() || Brush.Epoch != Epoch)
	{
		return;
	}
	TArray<int32> ActivatorIndices;
	for (const TPair<uint64, FTouchLinkStamps>& Pair : ActiveTouches)
	{
		const uint64 Key = Pair.Key;
		const int32 BrushIndex = static_cast<int32>(static_cast<uint32>(Key >> 32));
		if (BrushIndex == Brush.Index)
		{
			ActivatorIndices.Add(static_cast<int32>(static_cast<uint32>(Key)));
		}
	}
	ActivatorIndices.Sort();
	for (int32 ActivatorIndex : ActivatorIndices)
	{
		RouteBrushTouch(Brush, FElysiumEntityHandle(ActivatorIndex, Epoch), /*bBegin*/ false);
	}
}

// --- The touchlink list and the deferred untouch check (`walks/L0-r015.md`) -------------------

void FElysiumEntityWorld::EnqueueUntouchCheck(FElysiumEntity& Entity)
{
	// `FUN_100f8e20` 0x100f8e20, arms in retail order.
	// 1. `100f8e29`: `ent->vslot116()` (`IsMarkedForDeletion`, `m_iEFlags & 1`). True -> `100f8e31`
	//    -> return, no append.
	if (Entity.IsMarkedForDeletion())
	{
		EmitRetailSite(Entity, TEXT("untouch_enqueue"), TEXT("FUN_100f8e20"), 0x100f8e31u, TEXT("branch"),
			FString::Printf(TEXT("arm=marked_for_deletion count=%d"), UntouchCheckList.Num()));
		return;
	}
	// 2. `100f8e45`: grow when `count + 1 > capacity` (`FUN_100b4a40`: 8, then doubling) -- the
	//    container's own. 3. `100f8e63`: `count = old + 1`; `100f8e6b`: the base mirror. 4. The shift
	//    of `(old + 1) - old - 1 = 0` elements: `JLE` always taken, the memmove dead. 5. `100f8e9c`:
	//    `base[old] = ent`. Duplicates are not tested.
	UntouchCheckList.Add(Entity.Handle);
	EmitRetailSite(Entity, TEXT("untouch_enqueue"), TEXT("FUN_100f8e20"), 0x100f8e9cu, TEXT("write"),
		FString::Printf(TEXT("arm=append index=%d count=%d"), UntouchCheckList.Num() - 1, UntouchCheckList.Num()));
}

void FElysiumEntityWorld::UntouchListOnEntityDeleted(const FElysiumEntity& Entity)
{
	// `CEntityTouchManager::vfunc1` 0x100f8cf0: `ent+0x26b & 1` clear -> nothing. Else the linear
	// search from 0; found at `i` (with `0 <= i < count`, `count > 0`): copy the LAST element over it
	// (`FUN_10430fa0(base + i*4, base + (count-1)*4, 4)`), `count -= 1`. Only the first match.
	if ((Entity.EFlags & 0x1000000u) == 0)
	{
		return;
	}
	const int32 Index = UntouchCheckList.IndexOfByKey(Entity.Handle);
	if (Index != INDEX_NONE && UntouchCheckList.Num() > 0)
	{
		UntouchCheckList[Index] = UntouchCheckList.Last();
		UntouchCheckList.Pop(EAllowShrinking::No);
		EmitRetailSite(Entity, TEXT("untouch_enqueue"), TEXT("CEntityTouchManager::vfunc1"), 0x100f8cf0u, TEXT("write"),
			FString::Printf(TEXT("arm=fast_remove index=%d count=%d"), Index, UntouchCheckList.Num()));
	}
}

void FElysiumEntityWorld::FrameUpdatePostEntityThinkUntouch()
{
	// `FUN_100f8ec0` 0x100f8ec0 on `DAT_107036b0`, arms in retail order.
	// 1. `n = count`; `n == 0` -> return.
	if (UntouchCheckList.IsEmpty())
	{
		return;
	}
	// 2. The `n` pointers copied onto the stack. 3. `100f8f09`: `count = 0` -- the list is emptied
	//    before any check runs, so a check that re-enqueues lands in NEXT frame's pass.
	const TArray<FElysiumEntityHandle> Pending = UntouchCheckList;
	UntouchCheckList.Reset();
	static const FString GManagerName(TEXT("CEntityTouchManager"));
	EmitRetailSite(GManagerName, TEXT("untouch_drain"), TEXT("FUN_100f8ec0"), 0x100f8f09u, TEXT("write"),
		FString::Printf(TEXT("count=0 drained=%d"), Pending.Num()));
	// 4. For `i = 0 .. n-1`: `TEST byte [ent+0x26b], 1` (`100f8f13`); set -> `PhysicsCheckForEntityUntouch`
	//    (`100f8f1b`); clear (a `SetCheckUntouch(false)` meanwhile) -> skip (`100f8f19`). A deleted
	//    entry was already fast-removed by `OnEntityDeleted` (retail holds raw pointers; a handle that
	//    no longer resolves is the same absence).
	for (const FElysiumEntityHandle& Handle : Pending)
	{
		FElysiumEntity* Entity = Handle.IsSet() && Handle.Epoch == Epoch && EntityList.IsValidIndex(Handle.Index)
			? EntityList[Handle.Index].Get() : nullptr;
		if (Entity == nullptr)
		{
			continue;
		}
		if ((Entity->EFlags & 0x1000000u) == 0)
		{
			EmitRetailSite(*Entity, TEXT("untouch_drain"), TEXT("FUN_100f8ec0"), 0x100f8f19u, TEXT("branch"),
				FString::Printf(TEXT("arm=not_pending m_iEFlags=0x%x"), Entity->EFlagsWord()));
			continue;
		}
		EmitRetailSite(*Entity, TEXT("untouch_drain"), TEXT("FUN_100f8ec0"), 0x100f8f1bu, TEXT("call"),
			FString::Printf(TEXT("fn=CBaseEntity::PhysicsCheckForEntityUntouch m_iEFlags=0x%x m_touchStamp=%d"),
				Entity->EFlagsWord(), Entity->TouchStamp));
		Entity->PhysicsCheckForEntityUntouch();
	}
}

int32 FElysiumEntityWorld::ExpireStaleTouchLinks(FElysiumEntity& Entity)
{
	// `0x1003d490`'s loop over `GetDataObject(this, 1)`: a link whose stamp is `-1` re-fires
	// `PhysicsTouch` (no port writer makes one); a link whose stamp is not `m_touchStamp` (`1003d4f0`)
	// is untouched -- `PhysicsNotifyOtherOfUntouch(other, this)` (the other side's EndTouch and its
	// link) then `PhysicsRemoveToucher(this, link)` (this side's EndTouch and the link). The pair's
	// one routed end here does both: it drops the pair and calls the brush side's `OnTouchEnd`.
	TArray<uint64> Stale;
	for (const TPair<uint64, FTouchLinkStamps>& Pair : ActiveTouches)
	{
		bool bBrushSide = false;
		if (!TouchPairSide(Pair.Key, Entity.Handle.Index, bBrushSide))
		{
			continue;
		}
		const int32 LinkStamp = bBrushSide ? Pair.Value.Brush : Pair.Value.Activator;
		if (LinkStamp != Entity.TouchStamp)
		{
			Stale.Add(Pair.Key);
		}
	}
	Stale.Sort();   // the entity-index order every touch pass here uses in place of the list's
	for (uint64 Key : Stale)
	{
		const FElysiumEntityHandle Brush(static_cast<int32>(static_cast<uint32>(Key >> 32)), Epoch);
		const FElysiumEntityHandle Activator(static_cast<int32>(static_cast<uint32>(Key)), Epoch);
		const FElysiumEntity* Other = Resolve(Brush.Index == Entity.Handle.Index ? Activator : Brush);
		EmitRetailSite(Entity, TEXT("untouch_check"), TEXT("CBaseEntity::PhysicsCheckForEntityUntouch"), 0x1003d4f0u,
			TEXT("untouch"), FString::Printf(TEXT("other=%s link_stamp=%d m_touchStamp=%d"),
				*AiTraceName(Other),
				Brush.Index == Entity.Handle.Index ? ActiveTouches[Key].Brush : ActiveTouches[Key].Activator,
				Entity.TouchStamp));
		RouteEntityTouch(Brush, Activator, /*bBegin*/ false);
	}
	return Stale.Num();
}

// --- Player interaction ----------------------------------------------------------------

namespace
{
	constexpr double GPromptFadeInSeconds = 0.10;
	constexpr double GPromptFadeOutSeconds = 0.15;
}

void FElysiumEntityWorld::QueuePlayerUseEdge(EElysiumUseEdge Edge)
{
	if (bActive)
	{
		PendingUseEdges.Add(Edge);
	}
}

void FElysiumEntityWorld::ReconcilePlayerTouches(TConstArrayView<FElysiumEntityHandle> CurrentBrushes)
{
	if (!bActive || !Player.IsSet() || !IsTriggerResolutionEnabled())
	{
		return;
	}

	TSet<int32> CurrentIndices;
	for (const FElysiumEntityHandle& Brush : CurrentBrushes)
	{
		if (FElysiumEntity* E = Resolve(Brush); E && E->CanBeginTouch(Player))
		{
			CurrentIndices.Add(Brush.Index);
		}
	}

	TArray<int32> Ends;
	for (const TPair<uint64, FTouchLinkStamps>& Pair : ActiveTouches)
	{
		const uint64 Key = Pair.Key;
		const int32 ActivatorIndex = static_cast<int32>(static_cast<uint32>(Key));
		const int32 BrushIndex = static_cast<int32>(static_cast<uint32>(Key >> 32));
		if (ActivatorIndex == Player.Index && !CurrentIndices.Contains(BrushIndex))
		{
			Ends.Add(BrushIndex);
		}
	}
	TArray<int32> Begins = CurrentIndices.Array();
	Begins.RemoveAll([this](int32 BrushIndex)
	{
		const uint64 Key = (static_cast<uint64>(static_cast<uint32>(BrushIndex)) << 32)
			| static_cast<uint32>(Player.Index);
		return ActiveTouches.Contains(Key);
	});
	// Begins before ends, reproducing retail's frame order: the engine fires a new contact's
	// StartTouch synchronously during the move, and defers a stale contact's EndTouch to that frame's
	// post-think untouch pass — both outputs reach the same event-queue drain, so with the queue's
	// FIFO tie-break the end edge is the last writer of any shared state (retail engine.dll relink →
	// `PhysicsMarkEntityAsTouched` StartTouch vs `PhysicsCheckForEntityUntouch` EndTouch;
	// `docs/vtmb/entity_io.md`). Within each phase the entity-index sort is a deterministic substitute
	// for retail's spatial (BSP-leaf) enumeration, which no map is known to depend on.
	Begins.Sort();
	Ends.Sort();
	for (int32 BrushIndex : Begins)
	{
		RouteBrushTouch(FElysiumEntityHandle(BrushIndex, Epoch), Player, /*bBegin*/ true);
	}
	for (int32 BrushIndex : Ends)
	{
		RouteBrushTouch(FElysiumEntityHandle(BrushIndex, Epoch), Player, /*bBegin*/ false);
	}
}

void FElysiumEntityWorld::QueuePlayerFeedEdge(EElysiumUseEdge Edge)
{
	if (bActive)
	{
		PendingFeedEdges.Add(Edge);
	}
}

void FElysiumEntityWorld::SetPlayerButtons(uint64 Buttons)
{
	if (!bActive || PlayerButtons == Buttons)
	{
		return;
	}
	// The COMBAT bits are what arms the think, not the whole field. The movement bits ride the same
	// field because direction-keyed attack selection reads them, and they change on almost every
	// frame a player walks — arming a deadline-driven think off those would make it a per-frame think
	// for as long as the player is moving, which is a different scheduler than the one this arms.
	constexpr uint64 CombatBits =
		static_cast<uint64>(EElysiumButton::Attack)
		| static_cast<uint64>(EElysiumButton::Attack2)
		| static_cast<uint64>(EElysiumButton::SecondaryAtk)
		| static_cast<uint64>(EElysiumButton::Reload);
	const bool bCombatChanged = ((PlayerButtons ^ Buttons) & CombatBits) != 0;
	PlayerButtons = Buttons;
	// Arm the think on a combat change. The player's think is deadline-driven off the stealth cadence,
	// so both edges would otherwise be answered up to a tenth of a second late — long enough for a
	// released block to still be blocking when a contact lands.
	if (bCombatChanged)
	{
		if (FElysiumPlayer* PlayerEnt = FindPlayer())
		{
			PlayerEnt->NextThink = static_cast<float>(NowSeconds());
		}
	}
}

int32 FElysiumEntityWorld::PlayerSelectionStateMask() const
{
	// The direction correspondence, stated once. Nothing here is arithmetic on a bit index: the two
	// numberings agree on no bit at all, and the pairing is what each bit MEANS.
	struct FDirectionBit
	{
		EElysiumButton Ours;
		int32 Theirs;
	};
	static constexpr FDirectionBit Bits[] = {
		{ EElysiumButton::Jump,      ElysiumCombo::InJump },
		{ EElysiumButton::Forward,   ElysiumCombo::InForward },
		{ EElysiumButton::Back,      ElysiumCombo::InBack },
		{ EElysiumButton::Left,      ElysiumCombo::InLeft },
		{ EElysiumButton::Right,     ElysiumCombo::InRight },
		{ EElysiumButton::MoveLeft,  ElysiumCombo::InMoveLeft },
		{ EElysiumButton::MoveRight, ElysiumCombo::InMoveRight },
	};

	int32 Mask = 0;
	for (const FDirectionBit& Bit : Bits)
	{
		if ((PlayerButtons & static_cast<uint64>(Bit.Ours)) != 0)
		{
			Mask |= Bit.Theirs;
		}
	}
	return Mask;
}

void FElysiumEntityWorld::UpdatePlayerFeed()
{
	if (PendingFeedEdges.IsEmpty())
	{
		return;
	}
	TArray<EElysiumUseEdge, TInlineAllocator<2>> Edges = MoveTemp(PendingFeedEdges);
	PendingFeedEdges.Reset();
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		return;
	}
	FElysiumPlayer* PlayerEnt = FindPlayer();
	if (!PlayerEnt || PlayerEnt->IsInert())
	{
		return;
	}

	for (const EElysiumUseEdge Edge : Edges)
	{
		if (Edge == EElysiumUseEdge::Released)
		{
			// The low-level `-feed` edge only releases the command button. Feeding is a toggle-style
			// action: the first PRESS starts it and a later PRESS requests the paired release family.
			// Treating this edge as cancellation makes the patch's 0.1-second `vm_feed` tap abort
			// before the first blood pulse.
			continue;
		}
		// A second press while this player is the feeder requests the ordinary release transition.
		// The continuation latch belongs to the paired action; it is not the physical button's held
		// state. A victim-role player is not part of the ordinary slice and cannot cancel its attacker.
		if (PlayerEnt->IsFeedPaired())
		{
			if (!PlayerEnt->FeedState.bVictim && PlayerEnt->FeedState.bContinuation)
			{
				PlayerEnt->SetFeedContinuation(false);
				UE_LOG(LogElysiumWorld, Display, TEXT("INFO - Feed stop requested"));
			}
			continue;
		}
		// One unpaired press is one `Replenish` request. A miss does not become a held retry.
		FElysiumEntityHandle Candidate = FElysiumEntityHandle::Invalid();
		if (IElysiumEmbodiment* Bodily = Embodiment())
		{
			Candidate = Bodily->QueryFeedTarget();
		}
		FElysiumEntity* TargetEnt = Resolve(Candidate);
		FElysiumCombatCharacter* Victim = TargetEnt ? TargetEnt->AsCombatCharacter() : nullptr;
		if (!Victim)
		{
			UE_LOG(LogElysiumWorld, Display, TEXT("INFO - Feed missed: no live target"));
			UE_LOG(LogElysiumWorld, Verbose,
				TEXT("%s feed request missed: candidate #%d is not a live combat character"),
				*PlayerEnt->DebugString(), Candidate.IsSet() ? Candidate.Index : INDEX_NONE);
			continue;   // nothing in the hull, or what is there is not a character
		}
		PlayerEnt->AttemptFeed(*Victim);
	}
}

void FElysiumEntityWorld::UpdatePlayerWeaponFrame()
{
	// The world-liveness gate stands AHEAD of the edge drain, because a world that is not running is
	// not refusing a press — it is not observing one. That is the same pairing a pause relies on
	// (`AElysiumMapActor::PostMoveTick` carries `bTickEvenWhenPaused = false`, and the controller
	// publishes no command while held), and switching trigger resolution off has to behave the same
	// way rather than consuming a held button until the player releases it.
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		return;
	}

	// The press edges, and they are computed FIRST — before every refusal below, including the ones
	// that return without doing anything. A press is spent by being observed, so a frame that
	// refuses it cannot bank it for the next one and a button held across a refused frame cannot
	// re-press itself.
	const uint64 Pressed = PlayerButtons & ~ConsumedPlayerButtons;
	ConsumedPlayerButtons = PlayerButtons;
	const uint64 Held = PlayerButtons;

	FElysiumPlayer* PlayerEnt = FindPlayer();
	if (!PlayerEnt || PlayerEnt->IsInert())
	{
		return;
	}
	// Retail skips the whole live `PostThink` main body, and `ItemPostFrame` with it, while
	// `m_iPlayerLocked` is set or the player is not alive (`docs/vtmb/player-entity.md` § "Recovered
	// `PostThink` body"). This runtime publishes no such field; `IsMobile()` is the latch every other
	// producer already gates on for the states that raise it — a cutscene, a scripted beat, a
	// controller handover — so it stands in for it.
	if (!PlayerEnt->IsMobile() || PlayerEnt->HasReportedDeath())
	{
		return;
	}

	// Retail's `ItemPostFrame` gives a controlling use entity FIRST REFUSAL
	// (`docs/vtmb/player-entity.md` § "Recovered `PostThink` body"). An open sign panel is this
	// runtime's other controlling surface, and mapping the first refusal onto it is CHOSEN:
	// every VtMB popup instructs "left-click to continue", so the primary press that dismisses one
	// must not also swing. The press is spent either way — `MinShowTime` refusing the dismissal is
	// not a reason to let the click through to the weapon.
	//
	// The refusal is the WHOLE frame's, not the dismissing press's. An open panel owns the primary
	// button for as long as it is up, so a press that arrives while it is up — a second click at the
	// panel, an autofire trigger still held — must not reach the weapon and fire behind it. The edges
	// were already drained above, so a button held across the panel's whole life produces exactly one
	// spent press and nothing re-presses itself when the panel closes.
	if (GetOpenSign().IsSet())
	{
		if ((Pressed & static_cast<uint64>(EElysiumButton::Attack)) != 0)
		{
			PlayerDismissSign();
		}
		return;
	}
	// The literal controlled-use refusal: a captured session owns the player's hands.
	if (ActiveUse.IsSet())
	{
		return;
	}

	FElysiumItem* Item = PlayerEnt->Inventory.Active(*PlayerEnt);
	FElysiumWeapon* Weapon = Item ? Item->AsWeapon() : nullptr;
	if (!Weapon)
	{
		// An empty hand is an ordinary state, not a failure: the selector can legitimately hold a
		// non-weapon or nothing at all, and a click then does nothing.
		return;
	}

	EElysiumWeaponButton HeldMask = EElysiumWeaponButton::None;
	EElysiumWeaponButton PressedMask = EElysiumWeaponButton::None;
	const auto Fold = [](uint64 Bits, EElysiumWeaponButton& Out)
	{
		if ((Bits & static_cast<uint64>(EElysiumButton::Attack)) != 0)
		{
			Out |= EElysiumWeaponButton::Primary;
		}
		// Both secondary verbs reach the same route. `+wpn_secondaryatk` is a composite: its block
		// half is a standing classification on the player, and this is its ordinary secondary-fire
		// half (`docs/vtmb/controls.md` § "Attack, block and weapon commands").
		if ((Bits & (static_cast<uint64>(EElysiumButton::Attack2)
			| static_cast<uint64>(EElysiumButton::SecondaryAtk))) != 0)
		{
			Out |= EElysiumWeaponButton::Secondary;
		}
		if ((Bits & static_cast<uint64>(EElysiumButton::Reload)) != 0)
		{
			Out |= EElysiumWeaponButton::Reload;
		}
	};
	Fold(Held, HeldMask);
	Fold(Pressed, PressedMask);
	if (HeldMask == EElysiumWeaponButton::None && PressedMask == EElysiumWeaponButton::None)
	{
		return;
	}

	// The ranged victim. A firearm's shot takes an explicit handle rather than inventing a trace, and
	// the producer that supplies one is the embodiment's aim query.
	//
	// It runs only on a button this frame's record would take an attack route for, and only for a
	// mode that fires: a weapon merely being carried never traces. It is deliberately NOT gated on
	// the attack being ready as well — the deadline, the reload latch and the empty magazine are
	// `AttackIntent`'s to judge, and a second copy of any of them here is how the button route and
	// the aim route come to disagree. The cost of that choice is a held autofire trigger tracing
	// once per frame across its recovery gaps, which is a read-only query on a frame that is
	// already firing.
	//
	// Melee is excluded because it reserves its own opponent inside the swing, on the authored
	// sequence reach — a second acquisition here would reserve a different body than the one the
	// swing hits.
	//
	// An Invalid answer is ordinary and the transaction already tolerates it: acquisition is opponent
	// reservation, not a damage verdict, so an unaimed shot still animates and still spends its
	// ammunition.
	FElysiumEntityHandle AimTarget = FElysiumEntityHandle::Invalid();
	const FElysiumItemDef* WeaponRecord = Weapon->Data();
	const bool bMeleeRecord =
		WeaponRecord && WeaponRecord->Type == EElysiumItemType::WeaponMelee;
	if (!bMeleeRecord)
	{
		const bool bPrimary = Weapon->WantsPrimaryPress(HeldMask, PressedMask);
		if (bPrimary || Weapon->WantsSecondaryPress(PressedMask))
		{
			const FElysiumWeapon::EIntent Intent = bPrimary
				? FElysiumWeapon::EIntent::Primary : FElysiumWeapon::EIntent::Secondary;
			// The range is the answering mode's own authored `Range`; the weapon reports for itself
			// when the record authors none.
			const float RangeCm = Weapon->AimQueryRangeCm(Intent);
			if (IElysiumEmbodiment* Bodily = Embodiment(); Bodily != nullptr && RangeCm > 0.0f)
			{
				AimTarget = Bodily->QueryAimTarget(RangeCm);
			}
		}
	}
	const FElysiumWeapon::EVerdict Verdict =
		Weapon->ItemPostFrame(HeldMask, PressedMask, AimTarget);

	// **Logged on the EDGE, and never for `Idle`.** This frame runs on any held weapon button, not
	// only on a press, so a trigger held through an attack's recovery answers `Idle` — "no button
	// asked for anything" — on every frame of it. At frame rate that is hundreds of identical lines
	// a second, and it buries the melee timeline and everything else in the log. An unchanged verdict
	// says nothing the previous line did not, so only a change is reported; `Idle` is the resting
	// answer and is never worth a line of its own.
	const int32 VerdictKey = static_cast<int32>(Verdict);
	if (Verdict != FElysiumWeapon::EVerdict::Idle && VerdictKey != LastLoggedWeaponVerdict)
	{
		UE_LOG(LogElysiumWorld, Verbose, TEXT("(%8.3f) player weapon frame %s -> %s"),
			NowSeconds(), *Weapon->DebugString(), FElysiumWeapon::VerdictName(Verdict));
	}
	LastLoggedWeaponVerdict = VerdictKey;
}

void FElysiumEntityWorld::UpdatePlayerMeleeMovementStop(const FElysiumIdealActivityState& State)
{
	if (!bActive)
	{
		return;
	}
	FElysiumPlayer* PlayerEnt = FindPlayer();
	if (!PlayerEnt || PlayerEnt->IsInert())
	{
		return;
	}
	// The same two guards the weapon frame takes, and they are the same retail ones: the whole live
	// `PostThink` body is skipped while `m_iPlayerLocked` is set or the player is not alive, and
	// this block sits inside it.
	if (!PlayerEnt->IsMobile() || PlayerEnt->HasReportedDeath())
	{
		return;
	}
	// Asked fresh every frame, against this frame's own cycle. The recovered block keeps no state
	// between frames and neither does this.
	if (!ElysiumClipMovement::StopsMeleeTailMotion(State, PlayerSelectionStateMask()))
	{
		return;
	}
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		Bodily->StopPlayerBody();
	}
	// A headless world has no body to stop. An ordinary absence, and the same one every other
	// embodiment reader treats as one.
}

float FElysiumEntityWorld::InteractionPromptAlpha(double Now) const
{
	if (!InteractionPrompt.DisplayOwner.IsSet())
	{
		return 0.0f;
	}
	const double Duration = InteractionPrompt.bFadingIn
		? GPromptFadeInSeconds : GPromptFadeOutSeconds;
	const float Target = InteractionPrompt.bFadingIn ? 1.0f : 0.0f;
	const float T = Duration > 0.0
		? FMath::Clamp(static_cast<float>((Now - InteractionPrompt.TransitionTime) / Duration), 0.0f, 1.0f)
		: 1.0f;
	return FMath::Lerp(InteractionPrompt.StartAlpha, Target, T);
}

void FElysiumEntityWorld::TransitionUseFocus(const FElysiumUseCandidate* FocusCandidate,
	const FElysiumUseCandidate* IconCandidate)
{
	const FElysiumEntityHandle Next =
		FocusCandidate ? FocusCandidate->Owner : FElysiumEntityHandle::Invalid();
	const FElysiumEntityHandle NextIcon =
		IconCandidate ? IconCandidate->Owner : FElysiumEntityHandle::Invalid();
	const double Now = NowSeconds();
	const float CurrentAlpha = InteractionPromptAlpha(Now);

	// The prompt first, because it is the union answer and may name an entity the focus does not.
	if (FElysiumEntity* NewIcon = Resolve(NextIcon))
	{
		// A fade-in starts only on a real change: the same owner already fading in keeps its curve,
		// and one that had begun fading out restarts.
		const bool bRestart =
			NextIcon != InteractionPrompt.DisplayOwner || !InteractionPrompt.bFadingIn;
		InteractionPrompt.DisplayOwner = NextIcon;
		InteractionPrompt.Icon = NewIcon->ResolveUseIcon(Player);
		InteractionPrompt.bLocked = NewIcon->IsUseLocked();
		if (bRestart)
		{
			InteractionPrompt.bFadingIn = true;
			InteractionPrompt.StartAlpha = CurrentAlpha;
			InteractionPrompt.TransitionTime = Now;
		}
	}
	else if (InteractionPrompt.DisplayOwner.IsSet() && InteractionPrompt.bFadingIn)
	{
		InteractionPrompt.bFadingIn = false;
		InteractionPrompt.StartAlpha = CurrentAlpha;
		InteractionPrompt.TransitionTime = Now;
	}

	if (Next == FocusedUsable)
	{
		if (FocusCandidate)
		{
			FocusContext.AnchorPoint = FocusCandidate->AnchorPoint;
			FocusContext.Selection = FocusCandidate->Selection;
		}
		return;
	}

	if (FElysiumEntity* Old = Resolve(FocusedUsable))
	{
		Old->OnUseCursorLeave();
	}
	FocusedUsable = Next;
	FocusContext = FElysiumUseContext();
	FocusContext.Activator = Player;
	FocusContext.Owner = Next;
	FocusContext.TimeSeconds = Now;
	if (FocusCandidate)
	{
		FocusContext.AnchorPoint = FocusCandidate->AnchorPoint;
		FocusContext.Selection = FocusCandidate->Selection;
	}
	if (FElysiumEntity* New = Resolve(FocusedUsable))
	{
		New->OnUseCursorEnter();
	}

	UE_LOG(LogElysiumWorld, Verbose, TEXT("(%8.3f) interaction-focus -> %s"),
		Now, FocusedUsable.IsSet() ? *DescribeHandle(FocusedUsable) : TEXT("<none>"));
}

void FElysiumEntityWorld::EndActiveUse(EElysiumUseEndReason Reason)
{
	if (!ActiveUse.IsSet())
	{
		return;
	}
	const FActiveUse Ending = ActiveUse.GetValue();
	ActiveUse.Reset();
	if (FElysiumEntity* Entity = Resolve(Ending.Context.Owner))
	{
		Entity->EndPlayerUse(Ending.Context, Reason);
	}
	LastUseOutcome = (Reason == EElysiumUseEndReason::Released
		|| Reason == EElysiumUseEndReason::Completed)
		? EElysiumUseOutcome::Completed : EElysiumUseOutcome::Cancelled;
}

bool FElysiumEntityWorld::EndPlayerUseSession(const FElysiumEntityHandle& OwnerHandle,
	EElysiumUseEndReason Reason)
{
	if (!ActiveUse.IsSet() || (OwnerHandle.IsSet() && ActiveUse->Context.Owner != OwnerHandle))
	{
		return false;
	}
	EndActiveUse(Reason);
	return true;
}

FElysiumUseBeginResult FElysiumEntityWorld::BeginPlayerUseSession(
	const FElysiumEntityHandle& OwnerHandle, const FElysiumEntityHandle& Activator)
{
	if (ActiveUse.IsSet() || DialogueSession || OpenSignOwner.IsSet())
	{
		LastUseOutcome = EElysiumUseOutcome::Busy;
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Busy);
	}
	FElysiumEntity* Entity = Resolve(OwnerHandle);
	FElysiumUseContext Context;
	Context.Activator = Activator;
	Context.Owner = OwnerHandle;
	Context.TimeSeconds = NowSeconds();
	// The eye a position gate measures from. A programmatic caller may have no player body at all
	// (a script `Use`, a headless fixture), and a gate that needs an eye fails closed on that rather
	// than measuring from the world origin.
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		Context.bHasEyeOrigin = Bodily->GetPlayerUseOrigin(Context.EyeOrigin);
	}
	if (!Entity)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("programmatic +use session failed: owner %s does not resolve"),
			*OwnerHandle.ToString());
		LastUseOutcome = EElysiumUseOutcome::Unavailable;
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	if (!Entity->CanPlayerFocus(Context))
	{
		LastUseOutcome = EElysiumUseOutcome::Unavailable;
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	if (!Entity->UseFilterName.IsEmpty())
	{
		FElysiumEntity* Filter = FindByName(Entity->UseFilterName);
		if (!Filter)
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("%s use_filter_name '%s' did not resolve; allowing use"),
				*Entity->DebugString(), *Entity->UseFilterName);
		}
		if (Filter && !Filter->PassesFilter(Activator))
		{
			LastUseOutcome = EElysiumUseOutcome::Unavailable;
			return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
		}
	}
	const FElysiumUseBeginResult Result = Entity->BeginPlayerUse(Context);
	if (Result.Outcome == EElysiumUseOutcome::SessionStarted
		&& Result.SessionKind != EElysiumUseSessionKind::None)
	{
		FActiveUse Session;
		Session.Context = Context;
		Session.Kind = Result.SessionKind;
		ActiveUse = Session;
	}
	LastUseOutcome = Result.Outcome;
	return Result;
}

void FElysiumEntityWorld::UpdatePlayerInteraction()
{
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		PendingUseEdges.Reset();
		TransitionUseFocus(nullptr, nullptr);
		return;
	}

	// One eye read per frame, stamped into every context this pass builds: retail's gate reaches the
	// player's own eye slot inside the predicate, but a predicate that calls a world service is not
	// assertable headless and would cost one call per candidate per frame.
	FVector FrameEyeOrigin = FVector::ZeroVector;
	bool bFrameHasEyeOrigin = false;
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		bFrameHasEyeOrigin = Bodily->GetPlayerUseOrigin(FrameEyeOrigin);
	}

	// `CBasePlayer::PlayerUse` runs the maintenance arm (`FUN_10167e00`) from exactly two sites and
	// **both are guarded by "no rising `IN_USE` edge"** (`slice-bc-decompiles.md` §5, steps 3 and
	// 4e): the no-button arm, and the gate-passed arm that explicitly tests
	// `m_afButtonPressed & IN_USE == 0`. On the frame a second press releases the session, retail
	// runs slot 44 and `FUN_10167fd0` instead — never the pin or the view snap. This frame's queued
	// edges are read here, before the maintenance call, for that one reason.
	const bool bRisingUseEdge = PendingUseEdges.Contains(EElysiumUseEdge::Pressed);

	if (ActiveUse.IsSet())
	{
		FElysiumEntity* ActiveEntity = Resolve(ActiveUse->Context.Owner);
		FElysiumEntity* ActiveUser = Resolve(ActiveUse->Context.Activator);
		if (!ActiveEntity || ActiveEntity->IsInert() || !ActiveUser || ActiveUser->IsInert())
		{
			EndActiveUse(EElysiumUseEndReason::TargetInvalid);
		}
		else
		{
			// `CBasePlayer::PlayerUse` `0x10167850` step 1: a live held target re-runs the use gate
			// every tick and **a failed gate immediately releases** (`FUN_10167fd0`, §7.1). This is
			// how `InputDisable` and a lost screen-facing test end a live session.
			ActiveUse->Context.EyeOrigin = FrameEyeOrigin;
			ActiveUse->Context.bHasEyeOrigin = bFrameHasEyeOrigin;
			ActiveUse->Context.TimeSeconds = NowSeconds();
			if (!ActiveEntity->CanPlayerFocus(ActiveUse->Context))
			{
				EndActiveUse(EElysiumUseEndReason::TargetInvalid);
			}
			else if (!bRisingUseEdge)
			{
				// Step 2, the maintenance arm: what a held session does when nothing was pressed.
				ActiveEntity->TickPlayerUse(ActiveUse->Context);
			}
		}
	}

	FElysiumUseQueryResult Query;
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		Query = Bodily->QueryPlayerUse(FocusedUsable);
	}

	const FElysiumUseCandidate* Selected = nullptr;
	// `PlayerUseIconFilter` `0x10342590` passes on slot 32 OR slot 34 OR slot 35, so the reticle
	// icon is answered by its own walk over the same candidates: a terminal the player cannot start
	// a session on — disabled, or already held by someone else — still shows the icon from inside
	// its screen cone. Focus (and therefore what a press can act on) remains slot 32 alone.
	const FElysiumUseCandidate* IconTarget = nullptr;
	for (const FElysiumUseCandidate& Candidate : Query.Candidates)
	{
		FElysiumEntity* Entity = Resolve(Candidate.Owner);
		FElysiumUseContext Context;
		Context.Activator = Player;
		Context.Owner = Candidate.Owner;
		Context.AnchorPoint = Candidate.AnchorPoint;
		Context.Selection = Candidate.Selection;
		Context.TimeSeconds = NowSeconds();
		Context.EyeOrigin = FrameEyeOrigin;
		Context.bHasEyeOrigin = bFrameHasEyeOrigin;
		const bool bFocusable = Entity && Entity->CanPlayerFocus(Context);
		if (!IconTarget && Entity
			&& (bFocusable || Entity->CanBeUsed(Context) || Entity->HasUseIconCaps(Context)))
		{
			IconTarget = &Candidate;
		}
		if (bFocusable)
		{
			Selected = &Candidate;
			break;
		}
		// An exact entity hit fails closed. Assistance must never jump through the object under
		// the reticle to something merely close to it.
		if (Candidate.Selection == EElysiumUseSelection::Exact)
		{
			break;
		}
	}
	TransitionUseFocus(Selected, IconTarget);
	LastUseOutcome = Selected ? EElysiumUseOutcome::Completed : Query.MissOutcome;
	FElysiumPlayer* StealthPlayer = FindPlayer();
	FElysiumNpc* StealthVictim = StealthPlayer ? StealthPlayer->FindStealthKillVictim() : nullptr;
	StealthPromptTarget = StealthVictim ? StealthVictim->Handle : FElysiumEntityHandle::Invalid();

	const TArray<EElysiumUseEdge, TInlineAllocator<2>> Edges = MoveTemp(PendingUseEdges);
	PendingUseEdges.Reset();
	// PlayerUse 0x10167850 tests buttons|pressed|released, before ordinary use dispatch.
	const bool bUseThisFrame = bPlayerUseHeld || !Edges.IsEmpty();
	for (EElysiumUseEdge Edge : Edges) bPlayerUseHeld = Edge == EElysiumUseEdge::Pressed;
	if (bUseThisFrame && StealthPlayer && StealthPlayer->TryStealthKill())
	{
		StealthPromptTarget = FElysiumEntityHandle::Invalid();
		LastUseOutcome = EElysiumUseOutcome::Completed;
		return;
	}
	for (EElysiumUseEdge Edge : Edges)
	{
		if (Edge == EElysiumUseEdge::Released)
		{
			if (ActiveUse.IsSet() && ActiveUse->Kind == EElysiumUseSessionKind::WhileHeld)
			{
				EndActiveUse(EElysiumUseEndReason::Released);
			}
			continue;
		}

		if (ActiveUse.IsSet())
		{
			// `CBasePlayer::PlayerUse` step 4e: a rising `+use` edge with a handle already held asks
			// the held entity's slot 44, and a non-zero answer runs the one release body. A terminal
			// inherits `CAISound::FUN_100267b0` = `return 1`, so `E` at the machine closes it
			// (correction C11); classes whose slot 44 has not been read stay Busy.
			FElysiumEntity* Held = Resolve(ActiveUse->Context.Owner);
			if (Held && Held->ReleasesOnSecondUse())
			{
				EndActiveUse(EElysiumUseEndReason::Released);
				continue;
			}
		}
		if (ActiveUse.IsSet() || DialogueSession || OpenSignOwner.IsSet())
		{
			LastUseOutcome = EElysiumUseOutcome::Busy;
			continue;
		}
		FElysiumEntity* Entity = Resolve(FocusedUsable);
		FocusContext.EyeOrigin = FrameEyeOrigin;
		FocusContext.bHasEyeOrigin = bFrameHasEyeOrigin;
		if (!Entity || !Entity->CanPlayerFocus(FocusContext))
		{
			LastUseOutcome = EElysiumUseOutcome::Unavailable;
			continue;
		}

		if (!Entity->UseFilterName.IsEmpty())
		{
			FElysiumEntity* Filter = FindByName(Entity->UseFilterName);
			// Retail treats an unresolved filter target as no filter. A resolved filter is the
			// authoritative gate and sees the player as its activator, exactly like trigger filters.
			if (!Filter)
			{
				UE_LOG(LogElysiumWorld, Warning,
					TEXT("%s use_filter_name '%s' did not resolve; allowing use"),
					*Entity->DebugString(), *Entity->UseFilterName);
			}
			if (Filter && !Filter->PassesFilter(Player))
			{
				LastUseOutcome = EElysiumUseOutcome::Unavailable;
				continue;
			}
		}
		FocusContext.TimeSeconds = NowSeconds();
		FocusContext.EyeOrigin = FrameEyeOrigin;
		FocusContext.bHasEyeOrigin = bFrameHasEyeOrigin;
		const bool bWasLocked = Entity->IsUseLocked();
		const FElysiumUseBeginResult Result = Entity->BeginPlayerUse(FocusContext);
		LastUseOutcome = bWasLocked ? EElysiumUseOutcome::Locked : Result.Outcome;
		if (Result.Outcome == EElysiumUseOutcome::SessionStarted
			&& Result.SessionKind != EElysiumUseSessionKind::None)
		{
			FActiveUse Session;
			Session.Context = FocusContext;
			Session.Kind = Result.SessionKind;
			ActiveUse = Session;
		}
	}
}

FElysiumInteractionView FElysiumEntityWorld::GetInteractionView() const
{
	FElysiumInteractionView View;
	if (Resolve(StealthPromptTarget) && FindPlayer() && !FindPlayer()->IsGrappling())
	{
		View.bVisible = View.bActionable = true;
		View.Icon = 0x13; // FUN_10174580's stealth action, published to +0x1ea4.
		View.PromptAlpha = 1.f;
		View.Action = TEXT("StealthKill");
		return View;
	}
	if (ActiveUse.IsSet() && ActiveUse->Kind == EElysiumUseSessionKind::Explicit)
	{
		return View;
	}
	View.PromptAlpha = InteractionPromptAlpha(NowSeconds());
	View.bVisible = InteractionPrompt.DisplayOwner.IsSet()
		&& View.PromptAlpha > KINDA_SMALL_NUMBER;
	View.bActionable = FocusedUsable.IsSet() && !ActiveUse.IsSet();
	View.Icon = InteractionPrompt.Icon;
	View.bLocked = InteractionPrompt.bLocked;
	return View;
}

namespace
{
	FElysiumItemContainer* FindOpenLootContainer(FElysiumEntityWorld& World)
	{
		const FElysiumEntityHandle Player = World.PlayerHandle();
		for (const TUniquePtr<FElysiumEntity>& Candidate : World.Entities())
		{
			FElysiumItemContainer* Container = Candidate ? Candidate->AsItemContainer() : nullptr;
			if (Container && !Container->IsDead() && Container->CurrentUser == Player)
			{
				return Container;
			}
		}
		return nullptr;
	}
}

bool FElysiumEntityWorld::BuildLootView(FElysiumLootView& Out) const
{
	Out = FElysiumLootView();
	FElysiumEntityWorld& Mutable = const_cast<FElysiumEntityWorld&>(*this);
	FElysiumItemContainer* Container = FindOpenLootContainer(Mutable);
	const FElysiumPlayer* PlayerEntity = FindPlayer();
	if (!Container || !PlayerEntity)
	{
		return false;
	}
	Container->BuildLootView(Out, *PlayerEntity);
	return true;
}

bool FElysiumEntityWorld::BuildTerminalView(FElysiumTerminalView& Out) const
{
	Out = FElysiumTerminalView();
	if (!ActiveUse.IsSet() || ActiveUse->Kind != EElysiumUseSessionKind::Explicit)
	{
		return false;
	}
	const FElysiumEntity* Entity = Resolve(ActiveUse->Context.Owner);
	const FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	if (!Terminal || Terminal->CurrentUser != ActiveUse->Context.Activator)
	{
		return false;
	}
	Terminal->BuildView(Out);
	return Out.IsOpen();
}

void FElysiumEntityWorld::ListIdleTerminals(
	TArray<TPair<FElysiumEntityHandle, uint32>>& Out) const
{
	// Handle and revision only: no grid copy, no cell walk, no content view. A publisher decides
	// from this which glasses actually changed and pays for a full view only for those — the
	// alternative is `Rows * Columns` cells per terminal per frame ahead of the redraw gate.
	Out.Reset();
	IElysiumEmbodiment* Bodily = Embodiment();
	if (!Bodily)
	{
		return;   // a headless world projects nothing; there is no glass to write to
	}
	for (const TUniquePtr<FElysiumEntity>& EntPtr : EntityList)
	{
		FElysiumEntity* Entity = EntPtr.Get();
		if (!Entity || Entity->IsDead() || Entity->IsInert())
		{
			continue;
		}
		const FElysiumTerminal* Terminal = Entity->AsTerminal();
		if (!Terminal || Terminal->CurrentUser.IsSet())
		{
			continue;   // a held terminal is published as the session view, not twice
		}
		// "With a body" is asked of the embodiment rather than of the entity: the body that carries
		// the `screen` material slot is the one registered as this owner's use anchor, which is the
		// same component the projection binds — and it is the only one that exists in a world where
		// the placed-model catalogue never ran (the terminal gym).
		FBox Unused(ForceInit);
		if (!Bodily->GetUseBodyWorldBounds(Terminal->Handle, Unused))
		{
			// Named once, not skipped in silence: a live machine with no body is drawing a
			// screensaver into a buffer nothing will ever show.
			Terminal->ReportBodilessGlass();
			continue;
		}
		Out.Emplace(Terminal->Handle, Terminal->ViewRevision);
	}
}

bool FElysiumEntityWorld::BuildIdleTerminalView(const FElysiumEntityHandle& OwnerHandle,
	FElysiumTerminalView& Out) const
{
	Out = FElysiumTerminalView();
	const FElysiumEntity* Entity = Resolve(OwnerHandle);
	const FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	if (!Terminal || Terminal->CurrentUser.IsSet())
	{
		return false;
	}
	Terminal->BuildIdleView(Out);
	return true;
}

void FElysiumEntityWorld::BuildIdleTerminalViews(TArray<FElysiumTerminalView>& Out) const
{
	Out.Reset();
	TArray<TPair<FElysiumEntityHandle, uint32>> Idle;
	ListIdleTerminals(Idle);
	for (const TPair<FElysiumEntityHandle, uint32>& Entry : Idle)
	{
		FElysiumTerminalView View;
		if (BuildIdleTerminalView(Entry.Key, View))
		{
			Out.Add(MoveTemp(View));
		}
	}
}

bool FElysiumEntityWorld::SubmitTerminalCommand(const FElysiumEntityHandle& OwnerHandle,
	uint32 SessionSerial, const FString& Command)
{
	if (!ActiveUse.IsSet() || ActiveUse->Context.Owner != OwnerHandle
		|| ActiveUse->Context.Activator != Player)
	{
		return false;
	}
	FElysiumEntity* Entity = Resolve(OwnerHandle);
	FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	return Terminal && Terminal->CurrentUser == Player && Terminal->Submit(SessionSerial, Command);
}

bool FElysiumEntityWorld::SubmitActiveTerminalCommand(const FString& Command)
{
	if (!ActiveUse.IsSet())
	{
		return false;
	}
	FElysiumEntity* Entity = Resolve(ActiveUse->Context.Owner);
	FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	return Terminal && SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial, Command);
}

bool FElysiumEntityWorld::PlayerBeginTerminalHack(const FElysiumEntityHandle& OwnerHandle,
	uint32 SessionSerial)
{
	if (!ActiveUse.IsSet() || ActiveUse->Context.Owner != OwnerHandle
		|| ActiveUse->Context.Activator != Player)
	{
		return false;
	}
	FElysiumEntity* Entity = Resolve(OwnerHandle);
	FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	return Terminal && Terminal->CurrentUser == Player && Terminal->BeginHack(SessionSerial);
}

bool FElysiumEntityWorld::PlayerLootTake(int32 Slot)
{
	FElysiumItemContainer* Container = FindOpenLootContainer(*this);
	FElysiumPlayer* PlayerEntity = FindPlayer();
	if (!Container || !PlayerEntity)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("loot take failed: no open container/player for slot %d"), Slot);
		return false;
	}
	return Container->TakeToPlayer(*PlayerEntity, Slot);
}

bool FElysiumEntityWorld::PlayerLootGive(int32 Slot)
{
	FElysiumItemContainer* Container = FindOpenLootContainer(*this);
	FElysiumPlayer* PlayerEntity = FindPlayer();
	if (!Container || !PlayerEntity)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("loot give failed: no open container/player for slot %d"), Slot);
		return false;
	}
	return Container->GiveFromPlayer(*PlayerEntity, Slot);
}

bool FElysiumEntityWorld::PlayerCloseLoot()
{
	FElysiumItemContainer* Container = FindOpenLootContainer(*this);
	if (!Container)
	{
		UE_LOG(LogElysiumWorld, Warning, TEXT("loot close failed: no open container session"));
		return false;
	}
	if (!EndPlayerUseSession(Container->Handle, EElysiumUseEndReason::Completed))
	{
		UE_LOG(LogElysiumWorld, Warning, TEXT("loot close failed: %s did not own active +use"),
			*Container->DebugString());
		return false;
	}
	return true;
}
