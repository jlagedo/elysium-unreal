# Lane C3 report · classification of the surviving mechanism hand bodies

(Saved by the orchestrator from the lane's final answer; port bodies only, corpus server down.)

125 rows read: **90 `seam:`** (a one-line forward, at most a guard on retail state or a retail predicate ORed onto the base answer), **35 `hand:`** (still retail's algorithm), 0 re-verdicts. Seam by class: FElysiumNpcBase 20, FElysiumNpc 18, FElysiumEntity 13, the rest species one-liners. Hand by class: FElysiumNpcBase 11, FElysiumNpc 9, FElysiumEntity 4, FElysiumNpcWerewolf 3, FElysiumAnimatingOverlay 2, one each Dog, MingXiao, SabbatLeader, TestHull, Tzimisce, Player.

**Judgement calls:** a single early-out guard plus a forward is `seam` (AddFacingTarget, CreateVPhysics, RemoveMemory, the emitter starters, FUN_102c4380/43b0); a retail predicate ORed onto the base answer is `seam` (species Should/NavIgnoreCollision, CanStandOn); where a helper holds the ladder (MaxYawSpeedMingXiao, FindGestureLayerByOwner) the caller is `seam`.

**Open algorithms by the service they want:** CMC — IsStandable, CheckOnGround (the floor/solid ladders around the measurement), SetHullSizeNormal/Small, the MaxYawSpeed ladders (Npc, Dog, Tzimisce), AutoMovement, ResolveStandingOnHead (0002/28). TraceRetail / NavRaycast / SampleFloor — ShouldCollide, HeadProbe, TraceMoveClearanceAtYaw, GetGroundpoint, both CheckStuck rows (Sabbat, Werewolf), TraceBleed, Npc Should/NavIgnoreCollision. UPathFollowingComponent / UNavigationSystem — NavigatorMoveStep (0018/5's row-for-row `CAI_Navigator::Move`, kept by contract), GetNavTargetEntity, GetNearestNodeToPlayer. Pose and anim — SetHeadDirection, AimGun, UpdatePoseParameters, CalcIdealYaw, overlay RemoveLayerByOwner and Slot266. SAVE walk — FUN_1029f610, HintOnRestore. Other — DamageDecal, TestHull::Spawn, the Player discipline-duration rescale mirror, FUN_102c43f0, SeveredTentaclesCanStandOn.

**Watch:** `0x102d2f00` (HintDeletingDestructor) sets a condition and calls ClearScheduleHint with a reuse delay other hint users observe — may deserve a `rule` re-verdict; left `hand`. `0x103a49c0` carries a warning log the dead pass should drop. `targets-C3.tsv`: 125 rows.
