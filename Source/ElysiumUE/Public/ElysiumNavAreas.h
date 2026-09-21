#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "NavAreas/NavArea_Null.h"

#include "ElysiumNavAreas.generated.h"

/**
 * The roadway a pedestrian is priced to AVOID.
 *
 * The sign was backwards here and in 0018 story 5 until 2026-09-21 (story 21-8), and it would
 * have had story 5 build its filter inside out. Retail's A* `FUN_102fe9f0` reads the flag once,
 * `fVar16 = cost * local_20` where `local_20 = RandomInt(5, 10)`, and relaxes a node only when
 * the new total is SMALLER -- so multiplying is a penalty. A pedestrian keeps to the pavement
 * and pays 5-10x to step into the road, which is also the only reading that makes physical
 * sense of a roadway volume. Non-pedestrian routes pass `param_3 == 0` (`FUN_103055b0`) and pay
 * the unpenalised cost, so the road is not a wall to anyone else.
 *
 * Retail marks it with a brush whose contents carry `0x2000` and NO clip bit. Game-wide only 47
 * of the 2,192 `0x2000` brushes are clip-free -- 9 on `sm_hub_1` -- and they are the ones that
 * reach a link: with each brush's box grown by hull 0, 455 of the hub's 461 flagged links cross
 * one of those 9, and 0 of its 1,185 unflagged ground links does
 * (`navigation-jump-links.md` section "The `0x2000`-only brushes are the ones that flag links").
 * A brush that also carries `MONSTERCLIP` stops `InitLinks`' own walk test, so no ground link is
 * ever built across it and its `0x2000` never reaches a link -- which is why this area goes over
 * the `---p` signature alone and not over `PN-p` or `-N-p`.
 *
 * Cost 1, the same as the default: the volume is not harder to walk, it is walkable ground that
 * only the pedestrian QUERY FILTER prices up. That filter is story 5's -- it prices this area at
 * one `RandomInt(5, 10)` drawn per request -- so this class exists to be named by it, and marking
 * the area now is what lets story 5 be a filter change rather than a re-bake.
 *
 * Where retail carries the flag, recovered 2026-09-21 (`seam_map_nav_graph.md` section "Link
 * stream"): the brush's `0x2000` lands in `CAI_Link::m_LinkInfo` at `link+0x64`, the unit's
 * `fields[0]`. It is the only value that field ever takes -- 1,331 links over 40 maps -- and on
 * `sm_hub_1` exactly 461 links carry it against 1,185 unflagged hull-0 ground links, the two
 * numbers this comment already stated from the brush side. So the area mark and the link flag
 * are two views of one fact, and story 5 can check its filter against the published link set
 * map by map rather than against a brush sweep.
 */
UCLASS()
class ELYSIUMUE_API UElysiumNavArea_Pedestrian : public UNavArea
{
	GENERATED_BODY()

public:
	UElysiumNavArea_Pedestrian(const FObjectInitializer& ObjectInitializer);
};

/**
 * A doorway the graph does not run through, cut out of every agent's mesh.
 *
 * Retail's door rule, from the masks: `MOVEABLE 0x4000` is the bit that hits a door, and the
 * graph-build mask `0x2000b` is the only one of the three movement masks without it. So
 * `InitLinks` builds straight through a standing door, and at run time every probe finds that same
 * door solid. A door the designers gave a link is one NPCs use; a door with no link is a wall, and
 * this area is how a wall gets said to Recast.
 *
 * `UNavArea_Null` rather than a cost: an NPC must not path through the doorway at all, and a high
 * cost would only make it a last resort.
 *
 * Note this is a SUBCLASS of `UNavArea_Null`, not that class, so Recast does not give it the
 * reserved null-area id (`GetNewAreaID` compares the exact class). It still cuts, because it
 * inherits `AreaFlags = 0` and no default query filter accepts a zero-flag polygon -- but the
 * polygons are generated and then rejected rather than never rasterised, so `NavAreaAt` sees
 * nothing walkable there while a raw tile dump still shows geometry.
 *
 * The doors that DO carry a link -- 8 of the tutorial's 36, the hub's smoke-shop pair -- are not
 * cut here. They get their cut and their smart link together in story 7, so that no commit
 * in between turns a door NPCs use into a wall.
 */
UCLASS()
class ELYSIUMUE_API UElysiumNavArea_DoorCut : public UNavArea_Null
{
	GENERATED_BODY()

public:
	UElysiumNavArea_DoorCut(const FObjectInitializer& ObjectInitializer);
};
