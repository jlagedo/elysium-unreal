// `CAI_BaseNPC`'s declarations of the `Senses` family (story 5 step 5),
// moved from `ElysiumNpcSenses*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSenses.cpp`.

/** SEAM for `CAI_Navigator::MarkNodeUnreachable` (`0x102f1fa0`), which `OnDoorBlocked`
 *  (`0x1027de00`) calls with `5.0` or `20.0` seconds and the blocking door. It reaches the AI
 *  NETWORK — it looks the door's nav link up in the node graph and stamps `node+0x64 |= 1`,
 *  `node+0x68 = curtime + seconds`. This runtime has no node graph, so the call is COUNTED and
 *  marks nothing; the retail word it stands for is the node's own unreachable-until stamp. */
int32 NavigatorUnreachableMarks = 0;

int32 SquadFocusWrites = 0;

int32 DoorNextTryWrites = 0;
