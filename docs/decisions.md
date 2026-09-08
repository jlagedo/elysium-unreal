# Decisions

## 2026-09-08 — AIN jump traversal uses Unreal's navigation and flight service

Spec `0005-park-stealth` authorizes Unreal navigation queries and requirement 19's baked
`NavLinkProxy` connections. The map bake projects the decoded human-hull AIN jump links into
native smart-link actors. Recast/Detour owns path following; Unreal's
`SuggestProjectileVelocity_CustomArc` chooses a ballistic arc, and `CharacterMovement` integrates
and collides the capsule. This changes the numerical trajectory and collision solver from
retail's `CAI_MoveProbe`/`CAI_Motor`; it does not reproduce retail's numerical jump arc.

The retained retail contract is the AIN's link-info/hull-motion filtering, hull-adjusted endpoints,
shared bidirectional connection, `Jump` during flight, `Ground` before arrival/failure, and
`STOP_MOVING` clearing the navigation goal while preserving airborne velocity and the special
navigation type for the substrate's grounded/stuck task arms. A link cannot be completed by the
normal XY-arrival shortcut while airborne. An invalid launch, lost movement service, wrong landing,
or aborted path request is diagnosable and fails the engine movement request; entity identity,
schedule order, failure handling and semantic timers remain in the plain-C++ host. A native
capsule arc probe must succeed before publishing Jump. There is no additional flight watchdog;
only actual path/movement failures and the substrate's own stuck task terminate a failed flight.

## 2026-09-08 — NPC AI runs on the plain-C++ schedule host, not Unreal's AI stack

Spec `0005-park-stealth`'s NPC AI keeps retail's cadence, thresholds, memory, conditions and
their order in the project's own schedule kernel (`ElysiumSchedule`) and NPC mind
(`FElysiumNpc`). Unreal answers queries only: line and sky traces, light sampling from the
authored worldlights, NavMesh paths, animation playback. No AIPerception, no Behavior Trees, no
State Trees. Retail bugs the programs were authored against are reproduced verbatim (the
`TaskFail` obliviousness leak, the dead `UNKNOWN_HOLDING` condition, the alert ladder's
once-per-life latch, the unreachable loiter and interact programs).
