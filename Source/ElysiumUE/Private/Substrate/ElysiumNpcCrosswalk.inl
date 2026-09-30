// 0018 story 7, the crosswalk lane: the pedestrian route's curb legs, the obstruction sink that
// queues a walker behind a waiter, and the pedestrian link's save words. Included inside
// `class FElysiumNpc` by `Substrate/ElysiumNpcDialogueBodies.inl` (after `FDialogPedWaypoint`, which
// these declarations use); the bodies are in `Substrate/ElysiumNpcCrosswalk.cpp`, the tests in
// `Tests/ElysiumNpcCrosswalkTests.cpp`. The wait test `0x102a0bc0`, its helper `0x102a0b90` and the
// per-think rule `0x102a0d20` stay with family Dialogue (`ElysiumNpcDialogueBodies.cpp`).
//
// `+0x630c m_pPedestrianLink` is `PedestrianPair` and `FElysiumNpc::MovementSinkObstructed`
// (`0x10298340`) is declared beside it in `ElysiumNpc.h`.

/** One waypoint of a pedestrian route, as the pedestrian chain `0x102fcd00` builds it and the wait
 *  test reads it: the position (`wp+0x00..+0x08`), the graph node (`wp+0x10`, -1 for the goal) and
 *  the flag word (`wp+0x28`: `4` bits_WP_TO_NODE on every node waypoint, `0x20`
 *  bits_WP_DONT_SIMPLIFY on both curbs of a crosswalk pair, `8` the goal). */
struct FPedestrianLeg
{
	FVector DestCm = FVector::ZeroVector;
	int32 NodeId = INDEX_NONE;
	int32 Flags = 0;
	// 0018/7 (doors lane, additive): `wp+0x24`, the door a `bits_WP_TO_DOOR` (`0x10`) waypoint
	// carries -- the one `OnObstructingDoor`'s arm 7 splices (`SpliceDoorWaypoint`) and
	// `AdvancePath 0x102f0400`'s door arm opens. Unset on every other leg.
	FElysiumEntityHandle Door;
};

/** The pedestrian route's waypoints still ahead, head first, the goal last: [curb A, curb B, ...,
 *  goal] (decision 2's splice, `NavLayPedestrianLegs`). Empty for every route that is not a type-8
 *  goal's -- or that slot 531 has not spliced a door waypoint into (0018/7, `SpliceDoorWaypoint`:
 *  [stand point `0x30`, the old head chain...], the same list and the same pop). It lives on the Troika NPC and not on `FElysiumNpcNavigator` because the navigator keeps
 *  no waypoint list by retail's layout -- the list is the path's `+0x24` chain, which the port's
 *  body follows as one Unreal path per leg -- and because the only reader of a waypoint's node words
 *  is Troika code (`0x102a0bc0`, reached through `npc+0x98`). The base route build empties it with
 *  the path (`NavBuildRoute`, `NavClearRoute`, `InstallPathNoGoal`). */
TArray<FPedestrianLeg> PedestrianLegs;

/** Decision 2's splice, the NAMED MODERNIZATION that stands the NavMesh route for the pedestrian
 *  A* chain `0x102fcd00` (built by `BuildNodeRoute 0x10304e00` for a path whose pedestrian byte is
 *  set). Asks the body the route to `Request`'s destination under the same pedestrian pricing
 *  (`QueryRoute`, its corner points), finds the walkable crosswalk curbs the route passes within
 *  48 units (2-D) of, and lays [curb, curb, ..., goal] for every run of consecutive curbs that are
 *  a walkable pair, in route order, each curb flagged `4 | 0x20` with its node and the goal `8`.
 *  Retail's start-node rule rides it: the chain begins at `NearestNodeToNPC` (`0x102f3c10`), so a
 *  pedestrian standing near a curb has that curb first, and a start node that IS the goal node
 *  builds no chain and no curb. A route away from every crosswalk lays the goal alone. Issues the
 *  head leg; the answer is `NavIssueLeg`'s.
 *
 *  What the modernization does not carry: retail's chain has a waypoint at EVERY graph node, the
 *  legs only at the curbs and the goal. So `path+0x44` (`LastNodePassed`, written by the pop
 *  `0x1030ba90` for each node waypoint) is written only at a curb, and the "next waypoint"
 *  `wp+0x30` that `TASK_FACE_NEXT_NODE` faces is the far curb or the goal, not the next graph node
 *  on the far side. The wait test itself reads exactly the words retail's does. */
bool NavLayPedestrianLegs(const FElysiumNpcMoveRequest& Request);

/** The pop `0x1030ba90` on a pedestrian route: the reached head leg leaves the list, its node
 *  becomes `path+0x44` (`LastNodePassed`), and the next leg is issued with the head request's words
 *  (the pedestrian multiplier as drawn at the build: `0x102fe9f0` draws once per route). Answers
 *  whether a head stands, as `NavAdvancePath` does. */
bool NavPedestrianAdvance();

/** The navigator's current waypoint (`path+0x24`) in the words `0x102a0bc0` reads: a pedestrian
 *  route's head leg (node, flags, the next leg's node), else a waypoint that is no node, at the head
 *  request's destination. False, `Out` left a no-node waypoint, when no head waypoint stands. */
bool PedestrianHeadWaypoint(FDialogPedWaypoint& Out) const;

/** The body parked (NAMED MODERNIZATION, see `NavParkBody`): its request stopped with the head leg
 *  kept, to be re-issued on the first `Move` pass that may move again. A new route clears it. */
bool bNavBodyParked = false;

/** Retail's body translates only inside `CAI_Navigator::Move`; the port's walks its leg on the
 *  actor tick. Where retail's `Move` does not move the body -- the paused path's early return
 *  (`0x102effab`) and the obstruction sink's swallowed step (`0x10298340` arm ii, `*result = 0`) --
 *  the port parks it: the request stopped (`IElysiumNpcMotor::Stop`, which leaves a turn-in-place
 *  free to run, as `MotorUpdateYaw` goes on turning a paused retail NPC), the head leg kept, and the
 *  motor's ideal yaw handed back so a turn the same think asked for survives the stop. Once. */
void NavParkBody();

/** The park's other half: on a `Move` pass past the pause that may move (`m_bShouldMove`), the
 *  kept head leg is issued again (`NavReissueHeadLeg`: same request, the drawn multiplier kept). */
void NavUnparkBody();

/** `CAI_BaseNPCTroika::Save`'s tail and `Restore`'s pair (the archive is the mechanism): the byte
 *  `+0x630c != 0`, then -- only when set -- the link's two node ids (`link+4`, `link+8`), read back
 *  into `m_iRestorePedLinkNode` (`+0x6310`) / `m_iRestorePedLinkDestNode` (`+0x6314`). The link's
 *  signal is not saved. Both directions; called from `FElysiumNpc::Serialize` right after the base
 *  half, where retail's tail runs after `CAI_BaseNPC::Save`. */
void SerializePedestrianLink(FElysiumSaveArchive& Ar);

/** `OnRestore 0x102998c0`'s pedestrian arm: with both restore ids set (neither -1), a first id
 *  outside the network counts `DAT_106c994c`; otherwise `0x102f96e0(node, dest)` re-finds the link
 *  into `+0x630c`. The ids are never reset. */
void RestorePedestrianLink();
