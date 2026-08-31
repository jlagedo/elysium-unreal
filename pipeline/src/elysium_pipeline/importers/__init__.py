"""Deploy published `export_v2` units into the loose corpus the running game reads.

An importer is the mirror of an exporter: it reads `$ELYSIUM_EXPORT_V2_ROOT/<family>/**.glb` and
writes what the units carry into `Content/ElysiumCorpus/`. It needs no VtMB install and no Unreal
Engine, because the source bytes travel inside the unit's own source capsule
(`formats/unit_contract/capsule.py`).
"""
