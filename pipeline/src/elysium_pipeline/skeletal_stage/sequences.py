"""Sequence/grid derivation shared by both R8 source adapters."""
from elysium_pipeline.formats.mdl_skel import Seq, MovementSummary, INCH_TO_CM
from elysium_pipeline.skeletal_stage.source_api import local_animation, read_movements
import math

def movement_summary(d, animdesc_base, frames, fps):
    """Authored cycle movement -> MovementSummary in seconds and centimetres, or ``None``.

    Source's ground-speed calculation is the complete movement vector's length divided by the
    animation duration. glTF samples frame ``i`` at ``i / fps``, so a clip with ``frames`` samples
    spans ``(frames - 1) / fps`` seconds. Zero/invalid motion stays absent and lets the runtime keep
    its established fallback speed.
    """
    movements = read_movements(d, animdesc_base)
    if not movements or frames <= 1 or not math.isfinite(fps) or fps <= 0.0:
        return None
    cycle_seconds = (frames - 1) / fps
    position = movements[-1].position
    ground_distance_cm = math.sqrt(sum(value * value for value in position)) * INCH_TO_CM
    if (not math.isfinite(cycle_seconds) or cycle_seconds <= 0.0
            or not math.isfinite(ground_distance_cm) or ground_distance_cm <= 0.0):
        return None
    return MovementSummary(
        cycle_seconds=cycle_seconds,
        ground_distance_cm=ground_distance_cm,
        ground_speed_cm_s=ground_distance_cm / cycle_seconds,
    )


def blend_clip_plan(d, clips):
    """Expand a model's sequences into the clips their blend grids need -> (extra, blends).

    A multi-cell sequence selects a different animation per pose-parameter value, and baking
    the base cell alone drops the rest — the male and female `move_and_ranged` `walk` grids
    are nine `walk_0`..`walk_315` animations behind one label. `extra` is one `mdl_skel.Seq`
    per animation an active cell selects that the base-cell bake does not already reach,
    labelled by the animation's own name, so every cell ships as its own clip. `blends` maps a
    sequence label to `{numblends, groupsize, paramindex, paramstart, paramend, cells}`, each
    cell carrying its position, the owner-local animation index it selects, the clip that
    animation baked as, and optional authored movement metadata. The index travels because it is
    the identity a contribution record names, so a cell joins back to the model image it was read
    from.

    **Nothing is blended here.** The grid rides beside the clips because the mix depends on a
    pose parameter the exporter cannot know, and because blending clips is not the same pose
    as blending the transforms they decode to — the host evaluates each cell and mixes the
    results (`docs/vtmb/animation_and_movers.md` A.3).

    A cell whose animation index falls outside the model's declaration resolves to `None`,
    which is a shortfall carried in the sidecar rather than a silently shortened grid."""
    by_base = {}
    taken = set()
    for c in clips:
        by_base.setdefault(c.base, c.label)
        taken.add(c.label.lower())

    extra, blends = [], {}
    for c in clips:
        grid = c.grid
        if len(grid.cells) <= 1:
            continue
        # A grid bound to no pose parameter on either axis cannot be driven: the engine
        # evaluates it at the parameter default, which normalizes to cell 0 on a zero-width
        # axis, so no other cell is ever reachable (crooked_cop's Walkie_Talkie trio ships
        # this way). That is a plain clip -- the base cell -- not a blend space.
        if all(index < 0 for index in grid.paramindex):
            continue
        cells = []
        for cell in grid.cells:
            found = local_animation(d, cell.anim)
            if found is None:
                cells.append(
                    {"axis": [cell.axis0, cell.axis1], "anim": cell.anim, "clip": None}
                )
                continue
            name, ab, frames, fps = found
            if ab not in by_base:
                # The animation's own name is the cell's clip name. It collides with a
                # sequence label only where content gave a sequence and an animation the
                # same string, so the index disambiguates and the common case reads
                # `walk_45` rather than a synthetic id.
                base_name = name or f"anim{cell.anim}"
                clip, suffix = base_name, 0
                while clip.lower() in taken:
                    suffix += 1
                    clip = (f"{base_name}#{cell.anim}" if suffix == 1
                            else f"{base_name}#{cell.anim}#{suffix}")
                taken.add(clip.lower())
                by_base[ab] = clip
                extra.append(Seq(label=clip, base=ab, frames=frames, fps=fps,
                                   activity="", actweight=0, flags=0,
                                   movement=read_movements(d, ab)))
            exported = {
                "axis": [cell.axis0, cell.axis1], "anim": cell.anim,
                "clip": by_base[ab],
            }
            motion = movement_summary(d, ab, frames, fps)
            if motion is not None:
                exported["motion"] = {
                    "cycle_seconds": round(motion.cycle_seconds, 6),
                    "ground_distance_cm": round(motion.ground_distance_cm, 6),
                    "ground_speed_cm_s": round(motion.ground_speed_cm_s, 6),
                }
            cells.append(exported)
        blends[c.label] = {
            "numblends": grid.numblends,
            "groupsize": list(grid.groupsize),
            "paramindex": list(grid.paramindex),
            "paramstart": [round(v, 4) for v in grid.paramstart],
            "paramend": [round(v, 4) for v in grid.paramend],
            "cells": cells,
        }
    return extra, blends
