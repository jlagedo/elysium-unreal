"""Representation selection shared by skeletal staging and material consumer routing."""


def skeletal_candidate(identity, bone_count):
    """Characters retain their skeletal representation even when their geometry is rigid.

    Shape describes the source mesh. It does not remove a character's bones, attachments or
    rest clip from the native lane. Other skeletal shapes are admitted regardless of role.
    Explicit cinematic/wield selections and include closure are joined by the stage caller.
    """
    return bool(bone_count and (identity.get("family") == "character" or identity["shape"] == "skeletal"))
