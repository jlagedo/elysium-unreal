"""Decoded-record access for shared pose rules during the R8 producer parity gate."""
from elysium_pipeline.formats import mdl_skel as legacy

SOURCE_AXES = legacy.SOURCE_AXES
INCH_TO_CM = legacy.INCH_TO_CM
Seq = legacy.Seq


def read_movements(source, base):
    if hasattr(source, "movements"):
        return source.movements(base)
    return legacy.read_movements(source, base)


def read_anim(source, bones, base, frames):
    if hasattr(source, "animation_frames"):
        return source.animation_frames(base, frames)
    return legacy.read_anim(source, bones, base, frames)


def local_animation(source, index):
    if hasattr(source, "local_animation"):
        return source.local_animation(index)
    return legacy.local_animation(source, index)


def attachments(source):
    if hasattr(source, "attachments"):
        return source.attachments
    return legacy.attachments(source)


def flex_descs(source):
    return legacy.flex_descs(source)


def mesh_flexes(source, model_base, mesh_index):
    return legacy.mesh_flexes(source, model_base, mesh_index)


def vert_anims(source, flex, anorms):
    return legacy.vert_anims(source, flex, anorms)


def bone_weights(source, clip, bones):
    if hasattr(source, "bone_weights"):
        values = source.bone_weights(clip.base)
    else:
        import struct
        at = clip.base + struct.unpack_from("<i", source, clip.base + 48)[0]
        values = [struct.unpack_from("<f", source, at + bone.index * 32)[0] for bone in bones]
    if len(values) != len(bones) or any(value not in (0.0, 1.0) for value in values):
        raise ValueError(f"{clip.label}: bone mask is not binary or does not cover every bone")
    return values


def authored_channels(source, bones, clip):
    if hasattr(source, "authored_channels"):
        return source.authored_channels(clip.base)
    import struct
    at = clip.base + struct.unpack_from("<i", source, clip.base + 48)[0]
    rows = [struct.unpack_from("<7i", source, at + bone.index * 32 + 4) for bone in bones]
    return [(any(row[:3]), any(row[3:])) for row in rows]
