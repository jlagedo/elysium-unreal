"""Synthetic contract tests for the isolated Corpus-index GLB exporter.

Every install here is built in a temporary directory -- loose retail tree, loose patch tree and a
hand-built `pack*.vpk` -- and every published unit is written directly through the unit contract's
own container, so nothing in this module needs an install or another seam's decoder to run.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import zipfile

import pytest

from elysium_pipeline.exporters import corpus_index_glb as exporter
from elysium_pipeline.formats.corpus_index_glb import (
    ASSET_ID,
    CHECK_NAMES,
    CORPUS_INDEX_EXTENSION,
    RESIDUE_CATEGORIES,
    backfill,
    checks,
    decode_corpus_index,
    graph,
    residue,
    walk as install_walk,
    write_inverse,
)
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    coverage_block,
    dependency,
    encode_glb,
    extension_root,
    identity_block,
    read_glb,
)
from elysium_pipeline.validation import corpus_index_glb as validation


# --- synthetic install ------------------------------------------------------------------------


def write(base: Path, relative: str, data: bytes = b"payload") -> Path:
    path = base / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def build_vpk(path: Path, members: dict[str, bytes]) -> Path:
    """One `pack*.vpk` in the retail layout `formats/vpk.py` reads.

    Data from offset 0, then a flat directory of `(name length, name, offset, size)`, then a
    footer whose last five bytes are the directory offset and a trailing NUL.
    """

    blob = bytearray()
    directory = bytearray()
    for name, data in members.items():
        offset = len(blob)
        blob += data
        encoded = name.encode("ascii")
        directory += struct.pack("<I", len(encoded)) + encoded
        directory += struct.pack("<II", offset, len(data))
    start = len(blob)
    blob += directory
    blob += struct.pack("<I", start) + b"\0"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(blob))
    return path


def bsp(pakfile: bytes = b"", revision: int = 7) -> bytes:
    """A VBSP v17 header whose lump 40 carries `pakfile`, and nothing else."""

    lumps = bytearray(64 * 16)
    header_bytes = 8 + len(lumps) + 4
    if pakfile:
        struct.pack_into("<iiI4s", lumps, 40 * 16, header_bytes, len(pakfile), 0, b"\0\0\0\0")
    return (
        b"VBSP"
        + struct.pack("<i", 17)
        + bytes(lumps)
        + struct.pack("<i", revision)
        + pakfile
    )


def zip_pakfile(members: dict[str, bytes]) -> bytes:
    import io

    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_STORED) as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    return buffer.getvalue()


SURFACE_TABLE = b'"concrete"\r\n{\r\n\t"impact"\t"Concrete.Impact"\r\n}\r\n'
GAME_SOUNDS = b'"Concrete.Impact"\r\n{\r\n\t"wave"\t"surfaces/concrete/impact1.wav"\r\n}\r\n'

#: One member of every disposition, and one of every residue category. The keys are what a real
#: install ships; only the payloads are synthetic.
RETAIL_MEMBERS = (
    "materials/wall.tth",
    "materials/wall.ttz",
    "materials/wall.vmt",
    "materials/fonts/tahoma_12_400_000.fnt",
    "materials/fonts/fontlist.txt",
    "materials/dxshaders/eyes.psh",
    "shaders/psh/lightmapped.vcs",
    "particles/spark.txt",
    "particles/spark.tga",
    "expressions/joe.vfe",
    "expressions/joe.txt",
    "sound/step.wav",
    "sound/step.lip",
    "sound/schemes/ch_cloud.txt",
    "sound/talk.vcd",
    "models/prop.mdl",
    "models/prop.dx80.vtx",
    "models/prop.phy",
    "dlg/jack.dlg",
    "vdata/system/settings.txt",
    "resource/gamemenu.res",
    "python/main.py",
    "python/main.pyc",
    "cfg/user.cfg",
    "lights.rad",
)

#: One member per residue category, in `RESIDUE_CATEGORIES` order.
RESIDUE_MEMBERS = {
    "authoring-leftover": "sound/hum.sfk",
    "unreachable-member": "unpacked 0.74/shovelhead/old.mdl",
    "engine-binary": "dlls/vampire.dll",
    "user-data": "save/quick.sav",
    "foreign-file": "cfg/elysium_capture.cfg",
    "excluded-by-decision": "media/logo.bik",
}


def install(tmp_path: Path, *, pakfile: bytes = b"", extra: dict[str, bytes] | None = None):
    """A complete synthetic install: retail loose, one pack, and the patch tree over both."""

    game = tmp_path / "Vampire"
    patch = tmp_path / "Unofficial_Patch"
    for relative in RETAIL_MEMBERS:
        write(game, relative, relative.encode())
    for relative in RESIDUE_MEMBERS.values():
        write(game, relative, relative.encode())
    write(game, "scripts/surfaceproperties.txt", SURFACE_TABLE)
    write(game, "scripts/game_sounds_surfaceproperties.txt", GAME_SOUNDS)
    write(game, "maps/tutorial.bsp", bsp(pakfile))
    # The two loose trees the retail engine writes while it runs; both are excluded.
    write(game, "maps/graphs/tutorial.ain", b"runtime cache")
    write(game, "maps/soundcache/tutorial.cache", b"runtime cache")
    build_vpk(
        game / "pack000.vpk",
        {
            "maps/graphs/tutorial.ain": b"packed nav graph",
            "maps/graphs/tutorial.loc": b"7\r\n",
            "materials/floor.tth": b"packed tth",
            "materials/floor.ttz": b"packed ttz",
            "materials/wall.vmt": b"retail wall",
        },
    )
    write(patch, "materials/wall.vmt", b"patched wall")
    for relative, data in (extra or {}).items():
        write(game, relative, data)
    return game, patch


def collect(tmp_path: Path, **kwargs):
    game, patch = install(tmp_path, **kwargs)
    return install_walk.collect(game=game, patch=patch)


# --- the walk ---------------------------------------------------------------------------------


def test_the_walk_keys_every_member_lower_case_and_forward_slashed(tmp_path):
    result = collect(tmp_path)
    paths = [member.path for member in result.members]
    assert paths == sorted(paths)
    assert all(path == path.lower() and "\\" not in path for path in paths)
    assert "materials/floor.tth" in paths            # a VPK member is a member
    assert "lights.rad" in paths                     # so is a file at the install root


def test_the_containers_and_the_runtime_cache_trees_are_the_only_omissions(tmp_path):
    result = collect(tmp_path)
    paths = {member.path for member in result.members}
    assert "pack000.vpk" not in paths
    assert "maps/soundcache/tutorial.cache" not in paths
    # The VPK-shipped `maps/graphs/` members are not excluded; they are the nav-graph seam's.
    assert "maps/graphs/tutorial.ain" in paths
    assert result.by_path()["maps/graphs/tutorial.ain"].source.origin["kind"] == "vpk"
    assert [row["path"] for row in result.excluded_trees] == [
        "pack*.vpk", "maps/graphs/", "maps/soundcache/"
    ]


def test_a_shadowed_member_keeps_every_loser_highest_precedence_first(tmp_path):
    result = collect(tmp_path)
    member = result.by_path()["materials/wall.vmt"]
    assert member.source.origin == {"kind": "loose", "root": "Unofficial_Patch"}
    assert [loser.origin["kind"] for loser in member.shadowed] == ["loose", "vpk"]
    assert member.shadowed[0].origin["root"] == "Vampire"
    assert member.source.sha256 == hashlib.sha256(b"patched wall").hexdigest()
    assert member.shadowed[1].sha256 == hashlib.sha256(b"retail wall").hexdigest()


def test_source_resolution_states_which_install_the_index_describes(tmp_path):
    result = collect(tmp_path)
    members = result.source_resolution["members"]
    assert result.source_resolution["policy"] == "up-first"
    assert [row["role"] for row in members] == ["retail", "patch", "vpk-container"]
    container = members[-1]
    assert container["path"] == "pack000.vpk"
    assert container["byteLength"] > 0 and len(container["sha256"]) == 64


@pytest.mark.parametrize(
    "path, disposition, asset",
    [
        ("materials/wall.tth", "unit", "vtmb:texture:wall"),
        ("materials/wall.ttz", "companion", "vtmb:texture:wall"),
        ("models/prop.dx80.vtx", "companion", "vtmb:model:prop"),
        ("models/prop.phy", "companion", "vtmb:model:prop"),
        ("sound/step.lip", "companion", "vtmb:sound:step.wav"),
        ("expressions/joe.txt", "companion", "vtmb:expression-table:joe"),
        ("python/main.pyc", "companion", "vtmb:script:main"),
        ("maps/graphs/tutorial.loc", "companion", "vtmb:nav-graph:tutorial"),
        ("sound/hum.sfk", "residue", None),
        ("lights.rad", "unit", "vtmb:engine-config:lights.rad"),
    ],
)
def test_every_disposition_is_decided_by_asking_the_seams(tmp_path, path, disposition, asset):
    member = collect(tmp_path).by_path()[path]
    assert member.disposition == disposition
    assert member.asset == asset


def test_a_table_member_names_every_unit_it_selects(tmp_path):
    member = collect(tmp_path).by_path()["scripts/surfaceproperties.txt"]
    assert member.disposition == "unit"
    assert member.asset == "vtmb:surface-property:concrete"


def test_one_bsp_selects_all_four_of_its_units(tmp_path):
    member = collect(tmp_path).by_path()["maps/tutorial.bsp"]
    assert member.disposition == "unit"
    assert list(member.assets) == [
        "vtmb:map:tutorial",
        "vtmb:map-entities:tutorial",
        "vtmb:map-lighting:tutorial",
        "vtmb:map-visibility:tutorial",
    ]
    assert member.asset == "vtmb:map:tutorial"


def test_a_complete_install_leaves_nothing_unclaimed(tmp_path):
    result = collect(tmp_path)
    assert result.unclaimed() == []
    counts = result.counts()
    assert counts["unclaimed"] == 0
    assert counts["unit"] and counts["companion"] and counts["residue"]
    assert sum(counts.values()) == len(result.members)


#: The shape retail ships: 16 names `scripts/sounds.txt` repeats from the live table, and one
#: only it declares. `game_sounds_manifest.txt` precaches only the live table, so the live entry
#: takes the shared identity and the dormant twin is the live unit's `shadowed-dormant-entry`.
DORMANT_SOUNDS = (
    b'"Concrete.Impact"\r\n{\r\n\t"wave"\t"surfaces/hl2/impact1.wav"\r\n}\r\n'
    b'"Dormant.Only"\r\n{\r\n\t"wave"\t"surfaces/hl2/dormant1.wav"\r\n}\r\n'
)


def test_a_name_two_sound_script_tables_declare_is_claimed_by_the_table_it_is_cut_from(tmp_path):
    """`seam_map_sound_script.md` gives both tables one identity namespace and the seam resolves
    a repeated name in favour of the live table. Asking each table for its own names would
    attribute the shared units to `scripts/sounds.txt`, whose bytes they were not cut from."""

    walk = collect(tmp_path, extra={"scripts/sounds.txt": DORMANT_SOUNDS})
    members = walk.by_path()
    live = members["scripts/game_sounds_surfaceproperties.txt"]
    dormant = members["scripts/sounds.txt"]
    assert live.disposition == dormant.disposition == "unit"
    assert live.asset == "vtmb:sound-script:concrete.impact"
    assert dormant.asset == "vtmb:sound-script:dormant.only"
    # The shared name is claimed once, by the live table alone.
    assert "vtmb:sound-script:concrete.impact" not in (dormant.assets or (dormant.asset,))


def test_a_dormant_table_on_its_own_still_claims_its_entries(tmp_path):
    """The negative: with no live table the dormant one owns every name it declares."""

    game, patch = install(tmp_path, extra={"scripts/sounds.txt": DORMANT_SOUNDS})
    (game / "scripts" / "game_sounds_surfaceproperties.txt").unlink()
    alone = install_walk.collect(game=game, patch=patch)
    member = alone.by_path()["scripts/sounds.txt"]
    assert member.disposition == "unit"
    assert set(member.assets) == {
        "vtmb:sound-script:concrete.impact", "vtmb:sound-script:dormant.only"
    }
    assert "scripts/game_sounds_surfaceproperties.txt" not in alone.by_path()


def test_a_member_no_seam_claims_is_unclaimed(tmp_path):
    result = collect(tmp_path, extra={"vdata/system/stealth.qqq": b"?"})
    assert result.unclaimed() == ["vdata/system/stealth.qqq"]
    assert result.by_path()["vdata/system/stealth.qqq"].evidence is None


# --- residue ----------------------------------------------------------------------------------


@pytest.mark.parametrize("category", RESIDUE_CATEGORIES)
def test_every_residue_category_is_evidence_backed(tmp_path, category):
    member = collect(tmp_path).by_path()[RESIDUE_MEMBERS[category]]
    assert member.disposition == "residue"
    assert member.evidence["category"] == category
    assert member.evidence["evidence"]
    # Residue is a classification, not an omission from the index.
    assert member.source.byte_length and len(member.source.sha256) == 64


#: The install the two sibling-anchored rules are proved against: a model beside the texture
#: configuration below `models/`, and the material beside the one below `materials/`.
RESIDUE_FACTS = residue.InstallFacts.of(
    ("models/scenery/x/lamp.mdl", "materials/plaster/wlle.vmt", "materials/wood/burnt.vmt")
)


@pytest.mark.parametrize(
    "path, category",
    [
        ("sound/x.sfk", "authoring-leftover"),
        ("sound/x.pk", "authoring-leftover"),
        ("models/scenery/x/cmdseq.wc", "authoring-leftover"),
        ("models/bad_models.txt", "authoring-leftover"),
        ("models/scenery/x/lamp.txt", "authoring-leftover"),
        ("materials/plaster/wlle.vmt.txt", "authoring-leftover"),
        ("materials/wood/burnt.txt", "authoring-leftover"),
        ("scripts/liblist.gam~", "authoring-leftover"),
        ("python/warehouse/warehouse.old", "authoring-leftover"),
        ("scripts/hl2_scripts.dsp", "authoring-leftover"),
        ("vdata/system/stealth.xls", "authoring-leftover"),
        ("unpacked 0.74/shovelhead/shovelhead_short.mdl", "unreachable-member"),
        ("models/character/monster/x/eyeball_l.vmt", "unreachable-member"),
        ("cl_dlls/client.dll", "engine-binary"),
        ("dlls/vampire.dll.12", "engine-binary"),
        ("save/vampire-000.sav", "user-data"),
        ("logs/console.log", "user-data"),
        ("cfg/elysium_load.cfg", "foreign-file"),
        ("media/troika.bik", "excluded-by-decision"),
    ],
)
def test_the_residue_rules_are_the_seam_maps_own_table(path, category):
    row = residue.classify(path, RESIDUE_FACTS)
    assert row is not None and row["category"] == category


@pytest.mark.parametrize(
    "path",
    [
        "models/scenery/x/lamp.txt",          # no .mdl in the directory the .txt sits in
        "materials/wood/burnt.txt",           # no materials/wood/burnt.vmt beside it
        "materials/plaster/wlle.vmt.txt",     # no materials/plaster/wlle.vmt beside it
    ],
)
def test_a_texture_configuration_with_no_neighbour_is_not_claimed(path):
    """The rule's evidence is the member it sits beside, so without that member there is none."""

    elsewhere = residue.InstallFacts.of(("models/other/lamp.mdl", "materials/wood/other.vmt"))
    assert residue.classify(path, elsewhere) is None
    assert residue.classify(path) is None


@pytest.mark.parametrize(
    "path",
    [
        "materials/fonts/fontlist.txt",       # the font registry unit, not a compiler config
        "vdata/system/settings.txt",          # an ordinary vdata unit
        "save/notes.txt",                     # the tree is user data; a .txt in it is not
        "cfg/user.cfg",                       # shipped, so not a foreign file
        "media/readme.txt",                   # the decision covers the videos, not the tree
    ],
)
def test_no_rule_claims_a_member_outside_its_own_evidence(path):
    assert residue.classify(path) is None


# --- the rules whose evidence is a neighbour, a signature, a length or the graph ----------------


def facts(*members: str, contents=None, referenced=None):
    """`InstallFacts` for a synthetic key set, with the bytes and graph the rules may ask for."""

    contents = dict(contents or {})
    return residue.InstallFacts.of(
        set(members) | set(contents),
        head=lambda key: contents.get(key, b""),
        referenced=referenced,
    )


ORPHAN_MODEL = "models/scenery/misc/plates/platedirty"


@pytest.mark.parametrize(
    "path",
    [
        f"{ORPHAN_MODEL}.dx80.vtx",
        f"{ORPHAN_MODEL}.dx7_2bone.vtx",
        f"{ORPHAN_MODEL}.vtx",
        f"{ORPHAN_MODEL}.phy",
    ],
)
def test_a_vtx_or_phy_whose_stem_ships_no_mdl_is_unreachable(path):
    """The model loader opens a VTX or PHY only beside a loaded MDL, so the evidence is the
    absence of that MDL -- computed from the index, never from a list of paths."""

    row = residue.classify(path, facts("models/scenery/misc/plates/platesclean.mdl"))
    assert row["category"] == "unreachable-member"
    assert f"{ORPHAN_MODEL}.mdl is absent" in row["evidence"]


@pytest.mark.parametrize(
    "path",
    [
        f"{ORPHAN_MODEL}.dx80.vtx",
        f"{ORPHAN_MODEL}.dx7_2bone.vtx",
        f"{ORPHAN_MODEL}.vtx",
        f"{ORPHAN_MODEL}.phy",
    ],
)
def test_a_vtx_or_phy_beside_its_mdl_is_no_rule_of_this_seams(path):
    """The negative: with the MDL in the index the member is the model's companion, and residue
    claims nothing. Without any facts at all it claims nothing either."""

    assert residue.classify(path, facts(f"{ORPHAN_MODEL}.mdl")) is None
    assert residue.classify(path) is None


ORPHAN_LIP = "sound/character/dlg/santa monica/trip/line311_col_e .lip"


def test_a_lip_with_no_sound_twin_is_unreachable():
    row = residue.classify(ORPHAN_LIP, facts("sound/character/dlg/santa monica/trip/line1.wav"))
    assert row["category"] == "unreachable-member"
    assert "line311_col_e .wav" in row["evidence"] and "line311_col_e .mp3" in row["evidence"]


@pytest.mark.parametrize("twin", [".wav", ".mp3"])
def test_a_lip_beside_either_spelling_of_its_sound_is_no_rule_of_this_seams(twin):
    assert residue.classify(ORPHAN_LIP, facts(ORPHAN_LIP[: -len(".lip")] + twin)) is None
    assert residue.classify(ORPHAN_LIP) is None


LINE_TABLE = b"{comfort_1.wav}{You okay man?}\r\n{comfort_2.wav}{Hey you, you all right?}\r\n"
PHONEME_AUDIT = (
    b"| Microsoft Speech API \t Words \t Phonemes \t Time \t| LipSync API \t Words \t"
    b" Phonemes \t Time \t| \t Filename \t Line Text \r\n"
)


@pytest.mark.parametrize(
    "path, payload",
    [
        ("sound/character/male/young_thug/young_thug_sound.txt", LINE_TABLE),
        ("sound/character/male/sabbat_thug/young_thug_sound.txt", LINE_TABLE),
        ("sound/character/male/young_thug/young_thug_sound_phonemeaudit.txt", PHONEME_AUDIT),
        ("sound/character/male/sabbat_thug/young_thug_sound_phonemeaudit.txt", PHONEME_AUDIT),
    ],
)
def test_a_lip_sync_tool_product_is_claimed_by_its_content_signature(path, payload):
    row = residue.classify(path, facts(contents={path: payload}))
    assert row["category"] == "authoring-leftover"
    assert "lip-sync tool" in row["evidence"]


@pytest.mark.parametrize(
    "path, payload",
    [
        # The spelling alone proves nothing: the same name over other bytes is not the tool's.
        ("sound/character/male/young_thug/young_thug_sound.txt", b"npc_thug\r\nvolume 1\r\n"),
        ("sound/character/male/young_thug/young_thug_sound_phonemeaudit.txt", b"nothing\r\n"),
        # A signature outside the tree the evidence is about claims nothing either.
        ("vdata/young_thug_sound.txt", LINE_TABLE),
    ],
)
def test_a_sound_text_with_no_signature_is_not_an_authoring_leftover(path, payload):
    assert residue.classify(path, facts(contents={path: payload})) is None
    # Without a reader the rule has no evidence, so it claims nothing.
    assert residue.classify(path, facts(path)) is None


@pytest.mark.parametrize(
    "path",
    [
        "sound/disciplines/thaumaturgy/blood salvo/icon_",
        "stats.txt",
        "models/scenery/furniture/sabortooth/sabortooth512.txt",
    ],
)
def test_a_zero_length_member_is_an_authoring_leftover(path):
    row = residue.classify(path, facts(path), 0)
    assert row == {"category": "authoring-leftover", "evidence": "zero-length member"}


@pytest.mark.parametrize(
    "path",
    [
        "sound/disciplines/thaumaturgy/blood salvo/icon_",
        "stats.txt",
        "models/scenery/furniture/sabortooth/sabortooth512.txt",
    ],
)
def test_the_same_member_with_bytes_in_it_is_not_claimed_by_length(path):
    assert residue.classify(path, facts(path), 11) is None
    assert residue.classify(path, facts(path)) is None


LOOSE_WAVE = "sound/disciplines/thaumaturgy/blood shield/hit_1"


def test_an_extensionless_sound_member_nothing_names_is_unreachable():
    row = residue.classify(LOOSE_WAVE, facts(LOOSE_WAVE, referenced=frozenset()), 20314)
    assert row["category"] == "unreachable-member"
    assert "vtmb:sound:disciplines/thaumaturgy/blood shield/hit_1" in row["evidence"]


def test_an_extensionless_sound_member_the_graph_names_is_not_claimed():
    named = frozenset({"vtmb:sound:disciplines/thaumaturgy/blood shield/hit_1"})
    assert residue.classify(LOOSE_WAVE, facts(LOOSE_WAVE, referenced=named), 20314) is None
    # And with no graph in hand the rule has no evidence at all.
    assert residue.classify(LOOSE_WAVE, facts(LOOSE_WAVE), 20314) is None


def test_the_graph_anchored_rule_runs_only_once_the_corpus_has_been_read(tmp_path):
    """The walk leaves the member unclaimed; the index re-asks the rules with the graph, and
    only the members no seam claimed are re-asked."""

    walk = collect(tmp_path, extra={LOOSE_WAVE: b"RIFF____WAVEfmt "})
    assert walk.unclaimed() == [LOOSE_WAVE]
    claimed = walk.with_reference_graph(())
    assert claimed.unclaimed() == []
    assert claimed.by_path()[LOOSE_WAVE].evidence["category"] == "unreachable-member"
    assert claimed.by_path()["materials/wall.tth"].disposition == "unit"
    # A graph that names it leaves it unclaimed, because the evidence is that nothing does.
    named = walk.with_reference_graph(
        ["vtmb:sound:disciplines/thaumaturgy/blood shield/hit_1"]
    )
    assert named.unclaimed() == [LOOSE_WAVE]


# --- embedded PAKFILE members -----------------------------------------------------------------


def test_pakfile_members_are_embedded_rather_than_install_members(tmp_path):
    pakfile = zip_pakfile(
        {
            "materials/maps/tutorial/patched.vmt": b'"LightmappedGeneric"{}',
            "materials/maps/tutorial/cubemap.tth": b"tth",
            "readme.md": b"neither",
        }
    )
    result = collect(tmp_path, pakfile=pakfile)
    member = result.by_path()["maps/tutorial.bsp"]
    embedded = {row["member"]: row for row in member.embedded}
    assert set(embedded) == {
        "materials/maps/tutorial/patched.vmt",
        "materials/maps/tutorial/cubemap.tth",
        "readme.md",
    }
    # SF-1.5: the texture and material seams' own `source_keys()` enumerate every BSP's PAKFILE
    # `.vmt`/`.tth`/`.ttz`, so both are claimed even though the install ships neither of them
    # below `materials/` at all -- "the unit it became" no longer needs a selecting install
    # member, only a key the seam's own plural export would publish.
    assert embedded["materials/maps/tutorial/patched.vmt"]["asset"] == (
        "vtmb:material:maps/tutorial/patched"
    )
    assert embedded["materials/maps/tutorial/cubemap.tth"]["asset"] == (
        "vtmb:texture:maps/tutorial/cubemap"
    )
    # `readme.md` is neither kind the map seam's own PAKFILE routing recognises, so it names no
    # unit regardless of what the seams claim.
    assert embedded["readme.md"]["asset"] is None
    origin = embedded["readme.md"]["origin"]
    assert origin["kind"] == "bsp-pakfile" and origin["map"] == "tutorial"
    assert origin["origin"] == member.source.origin
    # A packed member is not an install member.
    assert "materials/maps/tutorial/patched.vmt" not in result.by_path()


def test_an_embedded_member_names_the_unit_it_became_where_a_seam_publishes_one(tmp_path):
    """A BSP that packs its own copy of a member the install also ships names that unit."""

    result = collect(tmp_path, pakfile=zip_pakfile({"materials/wall.vmt": b"packed wall"}))
    member = result.by_path()["maps/tutorial.bsp"]
    assert [row["asset"] for row in member.embedded] == ["vtmb:material:wall"]


# --- the published corpus ---------------------------------------------------------------------


def publish(export_root: Path, relative: str, asset: str, *, dependencies=(),
            identity_extra=None, sources=(), **extra) -> Path:
    """One published unit, written through the unit contract's own container."""

    kind = asset.split(":")[1]
    name = "ELYSIUM_vtmb_" + kind.replace("-", "_")
    root = extension_root(
        schema_version="1.0.0",
        identity=identity_block(asset, f"{kind}/source", **(identity_extra or {})),
        source_resolution={"policy": "up-first", "members": list(sources)},
        dependencies=list(dependencies),
        coverage=coverage_block(mapped=["identity"]),
        **extra,
    )
    document = {
        "asset": asset_block(kind.title()),
        "extensionsUsed": [name],
        "extensionsRequired": [name],
        "extensions": {name: root},
    }
    path = export_root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encode_glb(document, b""))
    return path


def source_rows(walk, *paths) -> list[dict]:
    """`sourceResolution.members[]` for one unit, as the walk hashed the members it names.

    A path carrying `#` names a cut of the member before it, so the row states a span and the
    cut's own length -- which is what the index compares on its origin alone.
    """

    members = walk.by_path()
    rows = []
    for path in paths:
        key, _, cut = path.partition("#")
        member = members[key]
        row = {
            "role": key.rsplit(".", 1)[-1],
            "path": path,
            "origin": member.source.origin,
            "byteLength": member.source.byte_length,
            "sha256": member.source.sha256,
        }
        if cut:
            # A cut states its own bytes, not the member's. A lump names the span it was cut
            # from; a table entry names only itself, exactly as the two seams publish them.
            row["sha256"] = hashlib.sha256(path.encode()).hexdigest()
            span = CUT_SPANS.get(path)
            if span is None:
                row["byteLength"] = len(cut)
            else:
                row["byteLength"] = span[1]
                row["span"] = {"offset": span[0], "length": span[1]}
        rows.append(row)
    return rows


#: The spans the synthetic map's three sub-units are cut from, and the partition the map root
#: delegates them through. A table entry states no span; its cut is the entry, not a byte range.
CUT_SPANS = {
    "maps/tutorial.bsp#entities": (8, 16),
    "maps/tutorial.bsp#lighting": (24, 16),
    "maps/tutorial.bsp#visibility": (40, 16),
}


def bsp_partition(total: int) -> list[dict]:
    """The four-unit partition of the synthetic BSP, tiling it once end to end."""

    rows = [{"offset": 0, "length": 8, "state": "mapped", "owner": "bsp.header", "unit": "map"}]
    for path, unit in (
        ("maps/tutorial.bsp#entities", "map-entities"),
        ("maps/tutorial.bsp#lighting", "map-lighting"),
        ("maps/tutorial.bsp#visibility", "map-visibility"),
    ):
        offset, length = CUT_SPANS[path]
        rows.append({"offset": offset, "length": length, "state": "mapped",
                     "owner": f"bsp.{unit}", "unit": unit})
    tail = max(row["offset"] + row["length"] for row in rows)
    rows.append({"offset": tail, "length": total - tail, "state": "padding-zero",
                 "owner": "bsp.tail", "unit": "map"})
    return rows


def scene_actors(*events) -> list[dict]:
    """The nesting a scene unit publishes its events in: `actors[].channels[].events[]`."""

    return [{"name": "actor", "active": True,
             "channels": [{"name": "channel", "active": True, "events": list(events)}]}]


def corpus(export_root: Path, walk=None) -> Path:
    """A small published corpus that satisfies every cross-unit check.

    Given the walk of the install it was cut from, it is also the *whole* corpus that install
    claims: every unit some member selects is published, and each states the members it was cut
    from, which is what the index reconciles the member table against.
    """

    def sources(*paths):
        return source_rows(walk, *paths) if walk is not None else []

    publish(export_root, "textures/wall.glb", "vtmb:texture:wall",
            sources=sources("materials/wall.tth", "materials/wall.ttz"))
    publish(
        export_root,
        "materials/wall.glb",
        "vtmb:material:wall",
        dependencies=[dependency("texture", "vtmb:texture:wall", "materials/wall.tth", True)],
        shaderResolution={"programs": [{"pixelShader": "lightmapped", "vertexShader": None}]},
        sources=sources("materials/wall.vmt"),
    )
    publish(export_root, "shader-programs/psh/lightmapped.glb",
            "vtmb:shader-program:psh/lightmapped", selectedBy=[],
            sources=sources("shaders/psh/lightmapped.vcs"))
    publish(export_root, "surface-properties/concrete.glb",
            "vtmb:surface-property:concrete",
            dependencies=[dependency("sound-script", "vtmb:sound-script:concrete.impact",
                                     "scripts/surfaceproperties.txt#concrete", True)],
            sources=sources("scripts/surfaceproperties.txt#concrete"))
    publish(export_root, "sound-scripts/concrete.impact.glb",
            "vtmb:sound-script:concrete.impact",
            sources=sources("scripts/game_sounds_surfaceproperties.txt#concrete.impact"))
    publish(export_root, "sounds/step.wav.glb", "vtmb:sound:step.wav",
            identity_referenced=False, sources=sources("sound/step.wav", "sound/step.lip"))
    publish(export_root, "expression-tables/joe.glb", "vtmb:expression-table:joe",
            table={"rows": [{"name": "smile"}]}, selectedBy=[],
            sources=sources("expressions/joe.vfe", "expressions/joe.txt"))
    publish(
        export_root,
        "scenes/talk.glb",
        "vtmb:scene:talk",
        dependencies=[
            dependency("expression-table", "vtmb:expression-table:joe",
                       "expressions/joe.vfe", True),
            dependency("sound", "vtmb:sound:step.wav", "sound/step.wav", True),
        ],
        actors=scene_actors({"type": "expression", "expressionTable": "joe",
                             "expressionName": "smile"}),
        sources=sources("sound/talk.vcd"),
    )
    publish(export_root, "models/prop.glb", "vtmb:model:prop",
            sources=sources("models/prop.mdl", "models/prop.dx80.vtx", "models/prop.phy"))
    publish(export_root, "fonts/tahoma_12_400_000.glb", "vtmb:font:tahoma_12_400_000",
            sources=sources("materials/fonts/tahoma_12_400_000.fnt"))
    publish(
        export_root,
        "fonts/fontlist.glb",
        "vtmb:font-list:fontlist",
        dependencies=[dependency("font", "vtmb:font:tahoma_12_400_000",
                                 "materials/fonts/fontlist.txt", True)],
        sources=sources("materials/fonts/fontlist.txt"),
    )
    publish(
        export_root,
        "dialogues/jack.glb",
        "vtmb:dialogue:jack",
        dependencies=[dependency("sound", "vtmb:sound:step.wav", "sound/step.wav", True,
                                 resolution="mp3-first")],
        sources=sources("dlg/jack.dlg"),
    )
    if walk is None:
        return export_root

    publish(export_root, "textures/floor.glb", "vtmb:texture:floor",
            sources=sources("materials/floor.tth", "materials/floor.ttz"))
    publish(export_root, "shader-sources/eyes.glb", "vtmb:shader-source:eyes",
            sources=sources("materials/dxshaders/eyes.psh"))
    publish(export_root, "particles/spark.glb", "vtmb:particle:spark",
            sources=sources("particles/spark.txt"))
    publish(export_root, "images/spark.tga.glb", "vtmb:image:particles/spark.tga",
            sources=sources("particles/spark.tga"))
    publish(export_root, "sound-schemes/ch_cloud.glb", "vtmb:sound-scheme:ch_cloud",
            sources=sources("sound/schemes/ch_cloud.txt"))
    publish(export_root, "vdata/settings.glb", "vtmb:vdata:system/settings",
            sources=sources("vdata/system/settings.txt"))
    publish(export_root, "ui-resources/gamemenu.glb", "vtmb:ui-resource:resource/gamemenu.res",
            sources=sources("resource/gamemenu.res"))
    publish(export_root, "scripts/main.glb", "vtmb:script:main",
            sources=sources("python/main.py", "python/main.pyc"))
    publish(export_root, "engine-configs/user.glb", "vtmb:engine-config:cfg/user.cfg",
            sources=sources("cfg/user.cfg"))
    publish(export_root, "engine-configs/lights.glb", "vtmb:engine-config:lights.rad",
            sources=sources("lights.rad"))
    publish(export_root, "nav-graphs/tutorial.glb", "vtmb:nav-graph:tutorial", stamp=None,
            sources=sources("maps/graphs/tutorial.ain", "maps/graphs/tutorial.loc"))
    for kind in ("entities", "lighting", "visibility"):
        publish(export_root, f"maps/tutorial.{kind}.glb", f"vtmb:map-{kind}:tutorial",
                sources=sources(f"maps/tutorial.bsp#{kind}"))
    publish(export_root, "maps/tutorial.glb", "vtmb:map:tutorial",
            header={"mapRevision": 7},
            partition=bsp_partition(walk.by_path()["maps/tutorial.bsp"].source.byte_length),
            subUnits=[], sources=sources("maps/tutorial.bsp"))
    return export_root


def indexed(tmp_path: Path, **kwargs):
    export_root = tmp_path / "exports_v2"
    export_root.mkdir()
    result = collect(tmp_path, **kwargs)
    corpus(export_root, result)
    return result, export_root


def root_of(path: Path) -> dict:
    document, binary = read_glb(path)
    assert binary == b"", "the corpus index carries no BIN chunk"
    return document["extensions"][CORPUS_INDEX_EXTENSION]


# --- units, references, orphans and the census ------------------------------------------------


def test_units_are_hashed_from_the_files_on_disk(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    rows = {row["asset"]: row for row in root_of(published)["units"]}
    assert rows["vtmb:texture:wall"]["kind"] == "texture"
    assert rows["vtmb:texture:wall"]["path"] == "textures/wall.glb"
    on_disk = (export_root / "textures/wall.glb").read_bytes()
    assert rows["vtmb:texture:wall"]["sha256"] == hashlib.sha256(on_disk).hexdigest()
    assert rows["vtmb:texture:wall"]["byteLength"] == len(on_disk)


def test_a_published_unit_is_read_once_to_hash_and_parse_it(tmp_path, monkeypatch):
    # The digest a `units[]` row publishes and the extension root the rest of the row is read
    # out of have to be the same bytes; a second open of the file could describe another
    # revision of it, and over the whole corpus it doubles the read.
    export_root = tmp_path / "exports_v2"
    export_root.mkdir()
    unit = publish(export_root, "textures/wall.glb", "vtmb:texture:wall")
    reads: list[str] = []
    real = Path.read_bytes

    def counting(self):
        reads.append(str(self))
        return real(self)

    monkeypatch.setattr(Path, "read_bytes", counting)
    row, root = graph.read_unit(unit, export_root)

    assert reads.count(str(unit)) == 1
    monkeypatch.undo()
    assert row.sha256 == hashlib.sha256(unit.read_bytes()).hexdigest()
    assert root["identity"]["asset"] == "vtmb:texture:wall"


def test_references_are_every_dependency_row_and_inverse_is_their_transpose(tmp_path):
    result, export_root = indexed(tmp_path)
    root = root_of(exporter.export(result, export_root))
    edges = {(row["from"], row["role"], row["to"]) for row in root["references"]}
    assert ("vtmb:material:wall", "texture", "vtmb:texture:wall") in edges
    assert root["inverse"]["vtmb:texture:wall"] == [
        {"from": "vtmb:material:wall", "role": "texture"}
    ]
    assert root["inverse"]["vtmb:sound:step.wav"] == [
        {"from": "vtmb:dialogue:jack", "role": "sound"},
        {"from": "vtmb:scene:talk", "role": "sound"},
    ]


def test_references_carry_the_parameter_a_material_texture_binding_names(tmp_path):
    """SF-1.5: `graph.references()` passes a dependency row's `parameter` through to the edge,
    published only where the row carries one -- material units already write it on their texture
    dependency rows."""

    export_root = tmp_path / "exports_v2"
    export_root.mkdir()
    publish(export_root, "textures/wall.glb", "vtmb:texture:wall")
    publish(export_root, "models/prop.glb", "vtmb:model:prop")
    publish(
        export_root,
        "materials/wall.glb",
        "vtmb:material:wall",
        dependencies=[
            {"role": "texture", "parameter": "$basetexture", "asset": "vtmb:texture:wall",
             "sourcePath": "materials/wall.tth", "resolved": True},
            dependency("model", "vtmb:model:prop", "materials/wall.vmt", True),
        ],
    )
    units, roots = graph.read_corpus(export_root)
    texture_edge = next(edge for edge in graph.references(roots) if edge.role == "texture")
    assert texture_edge.parameter == "$basetexture"
    assert texture_edge.to_json()["parameter"] == "$basetexture"
    model_edge = next(edge for edge in graph.references(roots) if edge.role == "model")
    assert model_edge.parameter is None
    assert "parameter" not in model_edge.to_json()


def test_dangling_references_are_grouped_by_role(tmp_path):
    result, export_root = indexed(tmp_path)
    publish(export_root, "materials/floor.glb", "vtmb:material:floor",
            dependencies=[dependency("texture", "vtmb:texture:floor",
                                     "materials/floor.tth", False)])
    root = root_of(exporter.export(result, export_root))
    rows = {row["role"]: row for row in root["danglingReferences"]}
    assert rows["texture"]["count"] == 1
    assert rows["texture"]["edges"] == [
        {"from": "vtmb:material:floor", "to": "vtmb:texture:floor",
         "sourcePath": "materials/floor.tth"}
    ]


def test_orphans_are_listed_by_kind(tmp_path):
    result, export_root = indexed(tmp_path)
    root = root_of(exporter.export(result, export_root))
    rows = {row["kind"]: row for row in root["orphans"]}
    assert rows["model"]["assets"] == ["vtmb:model:prop"]
    assert "vtmb:texture:wall" not in rows["texture"]["assets"]   # the material binds it
    assert "sound" not in rows            # the scene and the dialogue name it


def test_the_census_restates_the_walk_and_the_corpus(tmp_path):
    result, export_root = indexed(tmp_path)
    root = root_of(exporter.export(result, export_root))
    extensions = {row["extension"]: row for row in root["census"]["byExtension"]}
    assert extensions[".vmt"]["count"] == 1
    assert extensions[".vmt"]["byOrigin"] == {"vpk": 0, "retail-loose": 0, "patch-loose": 1}
    assert extensions[".tth"]["count"] == 2
    assert extensions[".tth"]["byOrigin"] == {"vpk": 1, "retail-loose": 1, "patch-loose": 0}
    kinds = {row["kind"]: row for row in root["census"]["byKind"]}
    assert kinds["texture"]["count"] == 2
    assert root["census"]["byDisposition"] == result.counts()


def test_summary_counts_embedded_pakfile_members_unclaimed_by_extension(tmp_path):
    """SF-1.1/SF-1.5: the gap of PAKFILE members no seam claims is visible in `summary`, not
    silent -- and SF-1.5 drives it to (near) zero for the three extensions a BSP ever packs.

    `materials/wall.vmt` is claimed the ordinary way (the install ships it too, so a
    `vtmb:material:wall` unit exists); the texture and material seams' own `source_keys()` now
    also claim `.vmt`/`.tth`/`.ttz` PAKFILE keys with no install member at all, so
    `materials/probe.tth` is claimed too (`vtmb:texture:probe`, published below). Only
    `readme.md` -- a kind the map seam's own PAKFILE routing does not recognise at all -- stays
    unclaimed.
    """

    result, export_root = indexed(
        tmp_path,
        pakfile=zip_pakfile(
            {
                "materials/wall.vmt": b"packed wall",
                "materials/probe.tth": b"packed probe",
                "readme.md": b"no seam claims this",
            }
        ),
    )
    publish(export_root, "textures/probe.glb", "vtmb:texture:probe")
    root = root_of(exporter.export(result, export_root))
    summary = root["summary"]
    assert summary["embeddedMembers"] == 3
    assert summary["embeddedUnclaimed"] == 1
    assert summary["embeddedUnclaimedByExtension"] == {".md": 1}


def test_summary_embedded_counters_are_zero_with_no_pakfile_members(tmp_path):
    result, export_root = indexed(tmp_path)
    root = root_of(exporter.export(result, export_root))
    summary = root["summary"]
    assert summary["embeddedMembers"] == 0
    assert summary["embeddedUnclaimed"] == 0
    assert summary["embeddedUnclaimedByExtension"] == {}


def test_a_pakfile_only_vmt_and_tth_are_claimed_by_the_texture_and_material_seams(tmp_path):
    """SF-1.5: `_claims` computes an asset for a key even where it selects no install member, so
    a PAKFILE-embedded `.vmt`/`.tth` the texture and material seams' own `source_keys()` publish
    is claimed even though the install carries no `materials/maps/**` member at all."""

    result, export_root = indexed(
        tmp_path,
        pakfile=zip_pakfile(
            {
                "materials/maps/tutorial/wall_1_2_3.vmt": b'"LightmappedGeneric"{}',
                "materials/maps/tutorial/c1_2_3.tth": b"tth",
            }
        ),
    )
    member = result.by_path()["maps/tutorial.bsp"]
    embedded = {row["member"]: row["asset"] for row in member.embedded}
    assert embedded["materials/maps/tutorial/wall_1_2_3.vmt"] == (
        "vtmb:material:maps/tutorial/wall_1_2_3"
    )
    assert embedded["materials/maps/tutorial/c1_2_3.tth"] == "vtmb:texture:maps/tutorial/c1_2_3"
    publish(export_root, "materials/maps_tutorial_wall_1_2_3.glb",
            "vtmb:material:maps/tutorial/wall_1_2_3")
    publish(export_root, "textures/maps_tutorial_c1_2_3.glb",
            "vtmb:texture:maps/tutorial/c1_2_3")
    root = root_of(exporter.export(result, export_root))
    assert root["summary"]["embeddedUnclaimed"] == 0


def test_the_index_declares_one_corpus_unit_dependency_per_unit(tmp_path):
    result, export_root = indexed(tmp_path)
    root = root_of(exporter.export(result, export_root))
    assert len(root["dependencies"]) == len(root["units"])
    pins = {row["asset"]: row for row in root["dependencies"]}
    for unit in root["units"]:
        pin = pins[unit["asset"]]
        assert pin["role"] == "corpus-unit"
        assert pin["sourcePath"] == unit["path"]
        assert pin["sha256"] == unit["sha256"] and pin["byteLength"] == unit["byteLength"]


# --- the guarantee ----------------------------------------------------------------------------


def test_the_export_fails_while_a_member_is_unclaimed(tmp_path):
    result, export_root = indexed(tmp_path, extra={"vdata/system/stealth.qqq": b"?"})
    with pytest.raises(exporter.CorpusIndexGlbError) as error:
        exporter.export(result, export_root)
    assert "1 unclaimed member(s)" in str(error.value)
    assert "vdata/system/stealth.qqq" in str(error.value)
    assert not (export_root / "index.glb").exists()


def test_coverage_unresolved_is_the_unclaimed_list(tmp_path):
    result, export_root = indexed(tmp_path)
    root = root_of(exporter.export(result, export_root))
    assert root["coverage"]["unresolved"] == []
    assert root["coverage"]["byteLedger"] == []
    assert root["summary"]["unclaimed"] == 0


def test_the_export_fails_on_a_failed_cross_unit_check(tmp_path):
    result, export_root = indexed(tmp_path)
    publish(export_root, "surface-properties/marble.glb", "vtmb:surface-property:marble",
            dependencies=[dependency("surface-property", "vtmb:surface-property:granite",
                                     "scripts/surfaceproperties.txt#marble", True)])
    with pytest.raises(exporter.CorpusIndexGlbError) as error:
        exporter.export(result, export_root)
    assert "surface-property-inheritance failed" in str(error.value)


def test_the_export_fails_on_a_member_naming_a_unit_no_seam_published(tmp_path):
    """The member table and the unit table are two reads of one install; a `unit` member whose
    unit nobody published is the corpus being incomplete over a member the index calls decoded."""

    result, export_root = indexed(tmp_path)
    (export_root / "models/prop.glb").unlink()
    with pytest.raises(exporter.CorpusIndexGlbError) as error:
        exporter.export(result, export_root)
    assert "naming a unit no seam published" in str(error.value)
    assert "vtmb:model:prop" in str(error.value)
    assert not (export_root / "index.glb").exists()


def test_the_export_fails_on_a_member_whose_unit_was_decoded_from_other_bytes(tmp_path):
    """The seam read one file and the walk hashed another -- the shape of a seam reading the
    install through an index that resolves a member differently."""

    result, export_root = indexed(tmp_path)
    publish(export_root, "materials/wall.glb", "vtmb:material:wall",
            dependencies=[dependency("texture", "vtmb:texture:wall", "materials/wall.tth", True)],
            shaderResolution={"programs": [{"pixelShader": "lightmapped", "vertexShader": None}]},
            sources=[{"role": "vmt", "path": "materials/wall.vmt",
                      "origin": {"kind": "vpk", "container": "pack000.vpk", "offset": 0,
                                 "size": 11},
                      "byteLength": 11, "sha256": hashlib.sha256(b"retail wall").hexdigest()}])
    with pytest.raises(exporter.CorpusIndexGlbError) as error:
        exporter.export(result, export_root)
    assert "decoded from other bytes" in str(error.value)
    assert "materials/wall.vmt" in str(error.value)


def test_the_export_fails_on_a_unit_that_names_no_member_it_was_cut_from(tmp_path):
    result, export_root = indexed(tmp_path)
    publish(export_root, "models/prop.glb", "vtmb:model:prop")
    with pytest.raises(exporter.CorpusIndexGlbError) as error:
        exporter.export(result, export_root)
    assert "field='path'" in str(error.value)


def test_the_checks_are_judged_again_over_what_the_back_fill_wrote(tmp_path, monkeypatch):
    """The back-fill rewrites units after the first refusal, so the model the document is built
    from is judged too: a check that only fails once the fields are written still fails."""

    from elysium_pipeline.formats.corpus_index_glb import decode as decode_module

    result, export_root = indexed(tmp_path)
    real = decode_module.checks.run

    def once_written(units, edges, roots):
        rows = real(units, edges, roots)
        written = any(
            (root.get("identity") or {}).get("referencedBy") for root in roots.values()
        )
        if not written:
            return rows
        return [
            dict(row, passed=False, failures=[{"reason": "the inverse was written"}])
            if row["name"] == "font-list" else row
            for row in rows
        ]

    monkeypatch.setattr(decode_module.checks, "run", once_written)
    with pytest.raises(exporter.CorpusIndexGlbError, match="font-list"):
        exporter.export(result, export_root)


# --- the nine cross-unit checks ---------------------------------------------------------------


def named(rows, name):
    return next(row for row in rows if row["name"] == name)


def run_checks(export_root: Path):
    units, roots = graph.read_corpus(export_root)
    edges = graph.references(roots)
    return checks.run(units, edges, roots)


def test_every_cross_unit_check_passes_on_a_coherent_corpus(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    rows = run_checks(export_root)
    assert [row["name"] for row in rows] == list(CHECK_NAMES)
    assert all(row["passed"] for row in rows), [row for row in rows if not row["passed"]]


def test_surface_property_inheritance_fails_on_a_cycle(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    for name, base in (("a", "b"), ("b", "a")):
        publish(export_root, f"surface-properties/{name}.glb", f"vtmb:surface-property:{name}",
                dependencies=[dependency("surface-property", f"vtmb:surface-property:{base}",
                                         f"scripts/surfaceproperties.txt#{name}", True)])
    row = named(run_checks(export_root), "surface-property-inheritance")
    assert not row["passed"]
    assert any("cyclic" in failure["reason"] for failure in row["failures"])


def test_model_include_tree_fails_on_an_unpublished_include(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "models/npc.glb", "vtmb:model:npc",
            dependencies=[dependency("model", "vtmb:model:shared/bank",
                                     "models/shared/bank.mdl", True)])
    row = named(run_checks(export_root), "model-include-tree")
    assert not row["passed"]
    assert row["failures"][0]["to"] == "vtmb:model:shared/bank"


def test_scene_expression_rows_reports_a_row_the_table_does_not_carry_as_dangling(tmp_path):
    """Retail ships two scenes animating `Concern No Deform` against a table whose 33 rows do not
    include it. The unit contract's non-canonical storage rule makes that a disagreement to
    record with its evidence, not a reason to refuse the corpus, so the check passes and the row
    is published in `danglingReferences[]`."""

    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "scenes/other.glb", "vtmb:scene:other",
            dependencies=[dependency("expression-table", "vtmb:expression-table:joe",
                                     "expressions/joe.vfe", True)],
            actors=scene_actors({"type": "expression", "expressionTable": "joe",
                                 "expressionName": "frown"}))
    row = named(run_checks(export_root), "scene-expression-rows")
    assert row["passed"] and row["failures"] == []
    assert [item["row"] for item in row["observations"]] == ["frown"]
    assert row["observations"][0]["reason"] == "the table carries no such row"
    assert row["observations"][0]["from"] == "vtmb:scene:other"
    assert row["observations"][0]["to"] == "vtmb:expression-table:joe"


def test_an_absent_expression_row_is_published_in_dangling_references(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "scenes/other.glb", "vtmb:scene:other",
            dependencies=[dependency("expression-table", "vtmb:expression-table:joe",
                                     "expressions/joe.vfe", True)],
            actors=scene_actors(
                {"type": "expression", "expressionTable": "joe", "expressionName": "frown"},
                # The same row twice is one dangling reference, not two: retail's own scenes
                # animate the missing row from more than one event.
                {"type": "expression", "expressionTable": "joe", "expressionName": "frown"},
            ))
    units, roots = graph.read_corpus(export_root)
    edges = graph.references(roots)
    rows = checks.run(units, edges, roots)
    grouped = {row["role"]: row for row in graph.dangling(edges, checks.dangling_rows(rows))}
    assert grouped[checks.EXPRESSION_ROW_ROLE]["count"] == 1
    assert grouped[checks.EXPRESSION_ROW_ROLE]["edges"][0]["row"] == "frown"


def test_scene_expression_rows_still_fails_on_a_table_no_unit_publishes(tmp_path):
    """What the check owns is unchanged: a table the scene resolves must be a published unit."""

    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "scenes/other.glb", "vtmb:scene:other",
            dependencies=[dependency("expression-table", "vtmb:expression-table:ghost",
                                     "expressions/ghost.vfe", True)],
            actors=scene_actors({"type": "expression", "expressionTable": "ghost",
                                 "expressionName": "frown"}))
    row = named(run_checks(export_root), "scene-expression-rows")
    assert not row["passed"]
    assert row["failures"][0]["reason"] == "the scene names no published expression table"


def test_scene_expression_rows_leaves_an_unshipped_table_to_the_dangling_report(tmp_path):
    """Every shipped scene names the stem `dialog`, which no `expressions/` member answers; an
    event whose table the install does not ship is a dangling reference, not a corpus defect."""

    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "scenes/other.glb", "vtmb:scene:other",
            dependencies=[dependency("expression-table", "vtmb:expression-table:dialog",
                                     "expressions/dialog.vfe", False)],
            actors=scene_actors({"type": "expression", "expressionTable": "dialog",
                                 "expressionName": "Joy"}))
    assert named(run_checks(export_root), "scene-expression-rows")["passed"]
    units, roots = graph.read_corpus(export_root)
    dangling = {row["role"]: row["count"] for row in graph.dangling(graph.references(roots))}
    assert dangling["expression-table"] == 1


#: One hand-built `.vcd` whose single expression event names a row `vtmb:expression-table:joe`
#: does not carry, exported through the scene seam itself.
#: A choreo file's line ending is CRLF; spelling it by code point keeps this literal free of
#: escapes a reader has to count.
CRLF = bytes((13, 10))
SCENE_VCD = CRLF.join([
    b"// Choreo version 1",
    b'actor "Jack"',
    b"{",
    b'  channel "Face"',
    b"  {",
    b'    event expression "Frown"',
    b"    {",
    b"      time 1.000000 2.000000",
    b'      param "joe"',
    b'      param2 "Frown"',
    b"    }",
    b"  }",
    b"}",
    b"",
    b"fps 60",
    b"snap off",
    b"",
])


def test_scene_expression_rows_reads_a_scene_the_scene_seam_wrote(tmp_path):
    """The check walks the nesting the scene exporter writes, not a shape invented here."""

    from elysium_pipeline.exporters import scene_glb as scene_exporter

    export_root = corpus(tmp_path / "exports_v2")
    key = "character/dlg/tutorial/frown"
    source_path = f"sound/{key}.vcd"
    index = {source_path: ("loose", "/fake/" + source_path),
             "expressions/joe.vfe": ("loose", "/fake/expressions/joe.vfe")}
    scene_exporter.export(
        index, key, export_root, read_bytes=lambda _index, path: SCENE_VCD
    )
    row = named(run_checks(export_root), "scene-expression-rows")
    assert row["passed"]
    assert [item["row"] for item in row["observations"]] == ["Frown"]
    assert row["observations"][0]["from"] == f"vtmb:scene:{key}"


def test_surface_sound_scripts_accepts_a_physics_key_that_names_a_wave(tmp_path):
    """`gargoyle` and `quiet` write `"impact" "null.wav"`. The surface-property seam classifies a
    literal `.wav` as the `vtmb:sound:` reference it is, so no `sound-script` edge is produced
    and the check has nothing to refuse."""

    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "sounds/null.wav.glb", "vtmb:sound:null.wav")
    publish(export_root, "surface-properties/quiet.glb", "vtmb:surface-property:quiet",
            dependencies=[dependency("sound", "vtmb:sound:null.wav", "null.wav", True)],
            sounds={"impact": [{"path": "null.wav", "asset": "vtmb:sound:null.wav",
                                "resolved": True, "parameter": 0}]})
    row = named(run_checks(export_root), "surface-sound-scripts")
    assert row["passed"] and row["failures"] == []


def test_surface_sound_scripts_fails_on_an_undeclared_entry(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "surface-properties/marble.glb", "vtmb:surface-property:marble",
            dependencies=[dependency("sound-script", "vtmb:sound-script:marble.impact",
                                     "scripts/surfaceproperties.txt#marble", True)])
    row = named(run_checks(export_root), "surface-sound-scripts")
    assert not row["passed"]
    assert row["failures"][0]["to"] == "vtmb:sound-script:marble.impact"


def test_nav_graph_stamp_records_the_pair_and_does_not_assert_the_unfound_rule(tmp_path):
    """`seam_map_nav_graph.md` documents the stamp's meaning as unidentified and states that the
    value on sp_tutorial_1 is not the map's revision, so the index records the pair rather than
    failing on it."""

    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "maps/tutorial.glb", "vtmb:map:tutorial", header={"mapRevision": 7},
            partition=[], subUnits=[])
    publish(export_root, "nav-graphs/tutorial.glb", "vtmb:nav-graph:tutorial",
            stamp={"raw": "7", "value": 7})
    row = named(run_checks(export_root), "nav-graph-stamp")
    assert row["passed"] and row["failures"] == []
    assert row["observations"] == [
        {"from": "vtmb:nav-graph:tutorial", "to": "vtmb:map:tutorial", "stamp": 7,
         "mapRevision": 7, "agrees": True}
    ]

    publish(export_root, "nav-graphs/tutorial.glb", "vtmb:nav-graph:tutorial",
            stamp={"raw": "9", "value": 9})
    row = named(run_checks(export_root), "nav-graph-stamp")
    assert row["passed"] and row["failures"] == []
    assert row["observations"] == [
        {"from": "vtmb:nav-graph:tutorial", "to": "vtmb:map:tutorial", "stamp": 9,
         "mapRevision": 7, "agrees": False}
    ]


def test_nav_graph_stamp_observes_nothing_without_a_loc(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "maps/tutorial.glb", "vtmb:map:tutorial", header={"mapRevision": 7},
            partition=[], subUnits=[])
    publish(export_root, "nav-graphs/tutorial.glb", "vtmb:nav-graph:tutorial", stamp=None)
    row = named(run_checks(export_root), "nav-graph-stamp")
    assert row["passed"] and row["observations"] == []


def test_font_list_fails_on_a_row_that_matches_no_font(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "fonts/fontlist.glb", "vtmb:font-list:fontlist",
            dependencies=[dependency("font", "vtmb:font:arial_10_400_000",
                                     "materials/fonts/fontlist.txt", True)])
    row = named(run_checks(export_root), "font-list")
    assert not row["passed"]
    assert row["failures"][0]["to"] == "vtmb:font:arial_10_400_000"


def test_dialogue_line_audio_fails_when_a_resolved_line_has_no_sound_unit(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "dialogues/jack.glb", "vtmb:dialogue:jack",
            dependencies=[dependency("sound", "vtmb:sound:line1.mp3",
                                     "sound/line1.wav", True, resolution="mp3-first")])
    row = named(run_checks(export_root), "dialogue-line-audio")
    assert not row["passed"]
    assert row["failures"][0]["to"] == "vtmb:sound:line1.mp3"


def map_units(
    export_root: Path,
    *,
    entities=(10, 10),
    lighting=(20, 10),
    visibility=(30, 10),
    cut=None,
    total=40,
):
    """One map's four units: the root's partition and the three sub-units' spans.

    `cut` overrides what a sub-unit says it was cut from, so a test can make the root's
    delegation and the sub-unit's own span disagree.
    """

    regions = [
        {"offset": 0, "length": entities[0], "state": "mapped", "owner": "bsp.header",
         "unit": "map"},
        {"offset": entities[0], "length": entities[1], "state": "mapped",
         "owner": "bsp.lump[0]", "unit": "map-entities"},
        {"offset": lighting[0], "length": lighting[1], "state": "mapped",
         "owner": "bsp.lump[8]", "unit": "map-lighting"},
        {"offset": visibility[0], "length": visibility[1], "state": "mapped",
         "owner": "bsp.lump[4]", "unit": "map-visibility"},
    ]
    tail = visibility[0] + visibility[1]
    if tail < total:
        regions.append({"offset": tail, "length": total - tail, "state": "padding-zero",
                        "owner": "bsp.tail", "unit": "map"})
    spans = dict(cut or {})
    for kind, span in (("map-entities", entities), ("map-lighting", lighting),
                       ("map-visibility", visibility)):
        name = kind[len("map-"):]
        offset, length = spans.get(kind, span)
        publish_with_members(
            export_root,
            f"maps/tutorial.{name}.glb",
            f"vtmb:{kind}:tutorial",
            {"policy": "up-first",
             "members": [{"role": name, "path": f"maps/tutorial.bsp#{name}",
                          "origin": {"kind": "loose", "root": "Vampire"},
                          "byteLength": length, "sha256": "0" * 64,
                          "span": {"offset": offset, "length": length}}]},
        )
    # The map's own `sourceResolution` states the BSP's length, which the partition must reach.
    publish_with_members(
        export_root,
        "maps/tutorial.glb",
        "vtmb:map:tutorial",
        {"policy": "up-first",
         "members": [{"role": "bsp", "path": "maps/tutorial.bsp",
                      "origin": {"kind": "loose", "root": "Vampire"},
                      "byteLength": total, "sha256": "0" * 64}]},
        header={"mapRevision": 7}, partition=regions, subUnits=[],
    )
    return export_root


def publish_with_members(export_root: Path, relative: str, asset: str, resolution, **extra):
    kind = asset.split(":")[1]
    name = "ELYSIUM_vtmb_" + kind.replace("-", "_")
    root = extension_root(
        schema_version="1.0.0",
        identity=identity_block(asset, f"{kind}/source"),
        source_resolution=resolution,
        dependencies=[],
        coverage=coverage_block(mapped=["identity"]),
        **extra,
    )
    document = {
        "asset": asset_block(kind.title()),
        "extensionsUsed": [name],
        "extensionsRequired": [name],
        "extensions": {name: root},
    }
    path = export_root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encode_glb(document, b""))
    return path


def test_map_partition_passes_when_the_root_and_its_sub_units_agree(tmp_path):
    export_root = map_units(corpus(tmp_path / "exports_v2"))
    assert named(run_checks(export_root), "map-partition")["passed"]


def test_map_partition_fails_when_the_partition_leaves_a_gap(tmp_path):
    export_root = map_units(corpus(tmp_path / "exports_v2"), visibility=(31, 9))
    row = named(run_checks(export_root), "map-partition")
    assert not row["passed"]
    assert "gap" in row["failures"][0]["reason"]


def test_map_partition_fails_when_a_sub_unit_cut_a_span_the_root_did_not_delegate(tmp_path):
    export_root = map_units(
        corpus(tmp_path / "exports_v2"), cut={"map-lighting": (20, 6)}
    )
    row = named(run_checks(export_root), "map-partition")
    assert not row["passed"]
    assert row["failures"][0]["unit"] == "vtmb:map-lighting:tutorial"
    assert row["failures"][0]["delegated"] == [[20, 10]]
    assert row["failures"][0]["cut"] == [[20, 6]]


def test_map_partition_fails_when_the_map_publishes_fewer_than_four_units(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "maps/tutorial.glb", "vtmb:map:tutorial",
            header={"mapRevision": 7}, partition=[], subUnits=[])
    row = named(run_checks(export_root), "map-partition")
    assert not row["passed"]
    assert row["failures"][0]["missing"] == ["map-entities", "map-lighting", "map-visibility"]


def test_texture_material_roles_fails_when_no_material_binds_a_bound_texture(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "textures/floor.glb", "vtmb:texture:floor")
    publish(export_root, "models/lamp.glb", "vtmb:model:lamp",
            dependencies=[dependency("texture", "vtmb:texture:floor",
                                     "materials/floor.tth", True)])
    row = named(run_checks(export_root), "texture-material-roles")
    assert not row["passed"]
    assert row["failures"][0]["to"] == "vtmb:texture:floor"


def test_a_texture_nothing_binds_is_an_orphan_rather_than_a_failure(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "textures/floor.glb", "vtmb:texture:floor")
    assert named(run_checks(export_root), "texture-material-roles")["passed"]


def test_map_references_published_fails_on_a_map_naming_an_unpublished_asset(tmp_path):
    """SF-1.5: the index has the whole unit set in hand, so it answers what a map's own export
    could not -- whether the corpus actually published the unit a texture, cubemap or PAKFILE
    entry names."""

    export_root = corpus(tmp_path / "exports_v2")
    publish(
        export_root,
        "maps/tutorial.glb",
        "vtmb:map:tutorial",
        header={"mapRevision": 7},
        textures=[{"asset": "vtmb:material:ghost"}],
        cubemaps=[{"asset": "vtmb:texture:maps/tutorial/c1_2_3"}],
        pakfile={"entries": [{"unit": "vtmb:material:maps/tutorial/wall_1_2_3"}]},
    )
    row = named(run_checks(export_root), "map-references-published")
    assert not row["passed"]
    targets = {failure["to"] for failure in row["failures"]}
    assert targets == {
        "vtmb:material:ghost",
        "vtmb:texture:maps/tutorial/c1_2_3",
        "vtmb:material:maps/tutorial/wall_1_2_3",
    }
    fields = {failure["field"] for failure in row["failures"]}
    assert fields == {"textures", "cubemaps", "pakfile"}


def test_map_references_published_passes_when_every_named_asset_is_published(tmp_path):
    export_root = corpus(tmp_path / "exports_v2")
    publish(export_root, "textures/maps_tutorial_c1_2_3.glb",
            "vtmb:texture:maps/tutorial/c1_2_3")
    publish(export_root, "materials/maps_tutorial_wall_1_2_3.glb",
            "vtmb:material:maps/tutorial/wall_1_2_3")
    publish(
        export_root,
        "maps/tutorial.glb",
        "vtmb:map:tutorial",
        header={"mapRevision": 7},
        textures=[{"asset": "vtmb:material:wall"}],
        cubemaps=[{"asset": "vtmb:texture:maps/tutorial/c1_2_3"}],
        pakfile={"entries": [{"unit": "vtmb:material:maps/tutorial/wall_1_2_3"}]},
    )
    # "vtmb:material:wall" is already published by the `corpus()` fixture.
    assert named(run_checks(export_root), "map-references-published")["passed"]


# --- the back-fill ----------------------------------------------------------------------------


def model_role_corpus(export_root: Path) -> Path:
    """One referrer of every kind that assigns a model role, with the field each is read from."""

    export_root.mkdir(parents=True, exist_ok=True)
    for key, shape, family in (
        ("shared/bank", "bank", "shared"),
        ("shared/include", "skeletal", "shared"),
        ("scenery/crate", "static", "scenery"),
        ("scenery/bush", "static", "scenery"),
        ("scenery/grass", "static", "scenery"),
        ("character/npc/jack", "skeletal", "character"),
        ("scenery/door", "static", "scenery"),
        ("weapons/v_gun", "static", "weapons"),
        ("weapons/w_gun", "static", "weapons"),
        ("weapons/g_gun", "static", "weapons"),
        ("scenery/scripted", "static", "scenery"),
    ):
        publish(export_root, f"models/{key.replace('/', '_')}.glb", f"vtmb:model:{key}",
                identity_extra={"shape": shape, "family": family})

    def model_dependency(key):
        return dependency("model", f"vtmb:model:{key}", f"models/{key}.mdl", True)

    publish(export_root, "models/host.glb", "vtmb:model:character/npc/host",
            identity_extra={"shape": "skeletal", "family": "character"},
            dependencies=[model_dependency("shared/bank"), model_dependency("shared/include")])
    publish(export_root, "maps/tutorial.glb", "vtmb:map:tutorial",
            dependencies=[model_dependency("scenery/crate"), model_dependency("scenery/bush")],
            staticProps={"props": [{"asset": "vtmb:model:scenery/crate"}]},
            detailProps={"records": [{"asset": "vtmb:model:scenery/bush"}]})
    publish(export_root, "map-entities/tutorial.glb", "vtmb:map-entities:tutorial",
            dependencies=[model_dependency("character/npc/jack"), model_dependency("scenery/door")],
            entities=[
                {"classname": "npc_generic",
                 "references": [{"role": "model", "asset": "vtmb:model:character/npc/jack"}]},
                {"classname": "prop_physics",
                 "references": [{"role": "model", "asset": "vtmb:model:scenery/door"}]},
            ])
    publish(export_root, "vdata/gun.glb", "vtmb:vdata:gun",
            dependencies=[model_dependency("weapons/v_gun"), model_dependency("weapons/w_gun"),
                          model_dependency("weapons/g_gun")],
            projection={"fields": {"models": {
                "viewmodel": {"asset": "vtmb:model:weapons/v_gun"},
                "playermodel": {"asset": "vtmb:model:weapons/w_gun"},
                "infomodel": {"asset": "vtmb:model:weapons/g_gun"},
            }}})
    publish(export_root, "engine-configs/detail.glb", "vtmb:engine-config:detail",
            dependencies=[model_dependency("scenery/grass"),
                          model_dependency("cinematic/courtroom_bip1")],
            detailTypes=[{"name": "grass", "groups": [{"name": "0", "models": [
                {"model": "models/scenery/grass.mdl",
                 "modelNormalized": "models/scenery/grass.mdl"}]}]}])
    publish(export_root, "scripts/spawn.glb", "vtmb:script:spawn",
            dependencies=[model_dependency("scenery/scripted")])
    return export_root


def test_identity_roles_are_the_model_seams_closed_vocabulary(tmp_path):
    export_root = model_role_corpus(tmp_path / "exports_v2")
    units, roots = graph.read_corpus(export_root)
    assigned = backfill.model_roles(graph.references(roots), roots)
    assert assigned == {
        "vtmb:model:shared/bank": ["animation-bank"],
        "vtmb:model:shared/include": ["include-only"],
        "vtmb:model:scenery/crate": ["static-prop"],
        "vtmb:model:scenery/bush": ["placed-prop"],
        "vtmb:model:scenery/grass": ["placed-prop"],
        "vtmb:model:character/npc/jack": ["character-body"],
        "vtmb:model:scenery/door": ["placed-prop"],
        "vtmb:model:weapons/v_gun": ["view-model"],
        "vtmb:model:weapons/w_gun": ["wield"],
        "vtmb:model:weapons/g_gun": ["ground-item"],
    }
    # A referrer no rule covers assigns no role; the edge is still in the reference graph. A
    # script naming a model path is one, and so is an engine config naming a model its own
    # `detailTypes[]` does not scatter -- the seam emits a `model` edge for any localized path.
    assert "vtmb:model:scenery/scripted" not in assigned
    assert "vtmb:model:cinematic/courtroom_bip1" not in assigned


def test_an_entity_classifies_a_body_from_the_key_not_the_published_target(tmp_path):
    """`identity.family` is the first segment of the model key, so an entity's model is
    classified whether or not that model's own unit is part of the corpus being indexed."""

    export_root = tmp_path / "exports_v2"
    export_root.mkdir()
    publish(export_root, "map-entities/tutorial.glb", "vtmb:map-entities:tutorial",
            dependencies=[dependency("model", "vtmb:model:character/npc/jack",
                                     "models/character/npc/jack.mdl", True)],
            entities=[{"classname": "npc_generic",
                       "references": [{"role": "model",
                                       "asset": "vtmb:model:character/npc/jack"}]}])
    units, roots = graph.read_corpus(export_root)
    assigned = backfill.model_roles(graph.references(roots), roots)
    assert assigned == {"vtmb:model:character/npc/jack": ["character-body"]}


def test_a_model_two_referrers_place_differently_carries_both_roles(tmp_path):
    export_root = model_role_corpus(tmp_path / "exports_v2")
    publish(export_root, "maps/second.glb", "vtmb:map:second",
            dependencies=[dependency("model", "vtmb:model:scenery/crate",
                                     "models/scenery/crate.mdl", True)],
            staticProps={"props": []},
            detailProps={"records": [{"asset": "vtmb:model:scenery/crate"}]})
    units, roots = graph.read_corpus(export_root)
    assigned = backfill.model_roles(graph.references(roots), roots)
    assert assigned["vtmb:model:scenery/crate"] == ["placed-prop", "static-prop"]


def test_the_index_writes_identity_roles_as_the_vocabulary_and_re_hashes(tmp_path):
    export_root = model_role_corpus(tmp_path / "exports_v2")
    model = write_inverse(decode_corpus_index(collect(tmp_path), export_root))
    document, _ = read_glb(export_root / "models/scenery_crate.glb")
    assert document["extensions"]["ELYSIUM_vtmb_model"]["identity"]["roles"] == ["static-prop"]
    rows = {unit.asset: unit for unit in model.units}
    on_disk = (export_root / "models/scenery_crate.glb").read_bytes()
    assert rows["vtmb:model:scenery/crate"].sha256 == hashlib.sha256(on_disk).hexdigest()
    assert rows["vtmb:model:scenery/crate"].byte_length == len(on_disk)



def test_the_index_writes_the_fields_no_unit_can_write_about_itself(tmp_path):
    result, export_root = indexed(tmp_path)
    before = (export_root / "sounds/step.wav.glb").read_bytes()
    published = exporter.export(result, export_root)

    model_root, _ = read_glb(export_root / "models/prop.glb")
    assert model_root["extensions"]["ELYSIUM_vtmb_model"]["identity"]["roles"] == []

    sound_root, _ = read_glb(export_root / "sounds/step.wav.glb")
    referenced = sound_root["extensions"]["ELYSIUM_vtmb_sound"]["identity"]["referencedBy"]
    assert referenced == [
        {"from": "vtmb:dialogue:jack", "role": "sound"},
        {"from": "vtmb:scene:talk", "role": "sound"},
    ]
    assert (export_root / "sounds/step.wav.glb").read_bytes() != before

    table_root, _ = read_glb(export_root / "expression-tables/joe.glb")
    assert table_root["extensions"]["ELYSIUM_vtmb_expression_table"]["selectedBy"] == [
        {"from": "vtmb:scene:talk", "role": "expression-table"}
    ]

    program_root, _ = read_glb(export_root / "shader-programs/psh/lightmapped.glb")
    assert program_root["extensions"]["ELYSIUM_vtmb_shader_program"]["selectedBy"] == [
        {"from": "vtmb:material:wall", "role": "pixelShader"}
    ]

    rows = {row["asset"]: row for row in root_of(published)["units"]}
    on_disk = (export_root / "sounds/step.wav.glb").read_bytes()
    assert rows["vtmb:sound:step.wav"]["sha256"] == hashlib.sha256(on_disk).hexdigest()
    assert rows["vtmb:sound:step.wav"]["byteLength"] == len(on_disk)


def die_on_rename(*_args, **_kwargs):
    """A machine that goes down between the temporary sibling and the rename."""

    raise OSError("the rename never happened")


def test_a_back_fill_interrupted_before_the_rename_leaves_the_unit_it_rewrites(tmp_path,
                                                                              monkeypatch):
    # `seam_map_unit_contract.md` ("Validation") writes a unit to a temporary sibling and renames
    # it over the destination. The back-fill rewrites an already-published unit whose bytes the
    # index has recorded a hash for, so it lands the same way: a half-written unit is exactly
    # what the rename rule exists to prevent.
    export_root = tmp_path / "exports_v2"
    export_root.mkdir()
    unit = publish(export_root, "sounds/step.wav.glb", "vtmb:sound:step.wav")
    before = unit.read_bytes()

    monkeypatch.setattr(os, "replace", die_on_rename)
    with pytest.raises(OSError):
        backfill.rewrite(unit, "vtmb:sound:step.wav",
                         [{"from": "vtmb:scene:talk", "role": "sound"}])

    monkeypatch.undo()
    assert unit.read_bytes() == before


def test_a_second_run_over_one_corpus_rewrites_nothing(tmp_path):
    result, export_root = indexed(tmp_path)
    exporter.export(result, export_root)
    after_first = {
        path: path.read_bytes() for path in graph.published_files(export_root)
    }
    exporter.export(result, export_root)
    assert {path: path.read_bytes() for path in graph.published_files(export_root)} == after_first


def test_the_back_fill_refuses_a_unit_with_no_identity_object(tmp_path):
    export_root = tmp_path / "exports_v2"
    export_root.mkdir()
    path = publish(export_root, "sounds/step.wav.glb", "vtmb:sound:step.wav")
    document, binary = read_glb(path)
    document["extensions"]["ELYSIUM_vtmb_sound"]["identity"] = "not an object"
    path.write_bytes(encode_glb(document, binary))
    with pytest.raises(backfill.BackfillError):
        backfill.rewrite(path, "vtmb:sound:step.wav", [])


# --- round trip and validation ----------------------------------------------------------------


def test_the_published_index_round_trips_through_standalone_validation(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    assert published == export_root / "index.glb"
    summary = validation.validate(published)
    assert summary["asset"] == ASSET_ID
    assert summary["unclaimed"] == [] and summary["failedChecks"] == []
    assert summary["members"] == len(result.members)
    assert summary["byDisposition"] == result.counts()
    assert summary["units"] == len(graph.published_files(export_root))


def test_the_index_is_scene_less_with_no_bin_chunk(tmp_path):
    result, export_root = indexed(tmp_path)
    document, binary = read_glb(exporter.export(result, export_root))
    assert binary == b""
    for key in ("scenes", "nodes", "meshes", "images", "textures", "samplers", "buffers"):
        assert key not in document
    assert document["extensionsRequired"] == [CORPUS_INDEX_EXTENSION]


def test_the_extension_root_opens_with_the_contracts_keys(tmp_path):
    result, export_root = indexed(tmp_path)
    root = root_of(exporter.export(result, export_root))
    assert list(root)[:5] == [
        "schemaVersion", "identity", "sourceResolution", "dependencies", "coverage"
    ]
    assert list(root)[5:] == [
        "excludedTrees", "members", "units", "references", "inverse", "danglingReferences",
        "orphans", "crossUnitChecks", "census", "summary",
    ]


def test_validation_rejects_a_unit_whose_file_no_longer_matches_its_row(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    (export_root / "textures/wall.glb").write_bytes(b"glTF" + b"\0" * 16)
    with pytest.raises(validation.CorpusIndexValidationError, match="indexed"):
        validation.validate(published)


def test_validation_rejects_a_member_table_out_of_key_order(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    document, binary = read_glb(published)
    root = document["extensions"][CORPUS_INDEX_EXTENSION]
    root["members"] = list(reversed(root["members"]))
    published.write_bytes(encode_glb(document, binary))
    with pytest.raises(validation.CorpusIndexValidationError, match="out of key order"):
        validation.validate(published)


def test_validation_rejects_a_summary_that_disagrees_with_the_tables(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    document, binary = read_glb(published)
    document["extensions"][CORPUS_INDEX_EXTENSION]["summary"]["unclaimed"] = 3
    published.write_bytes(encode_glb(document, binary))
    with pytest.raises(validation.CorpusIndexValidationError, match="summary.unclaimed"):
        validation.validate(published)


def test_validation_rejects_an_inverse_that_is_not_the_transpose(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    document, binary = read_glb(published)
    document["extensions"][CORPUS_INDEX_EXTENSION]["inverse"] = {}
    published.write_bytes(encode_glb(document, binary))
    with pytest.raises(validation.CorpusIndexValidationError, match="transpose"):
        validation.validate(published)


def test_validation_rejects_residue_with_no_evidence(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    document, binary = read_glb(published)
    for row in document["extensions"][CORPUS_INDEX_EXTENSION]["members"]:
        if row["disposition"] == "residue":
            row.pop("evidence")
            break
    published.write_bytes(encode_glb(document, binary))
    with pytest.raises(validation.CorpusIndexValidationError, match="residue"):
        validation.validate(published)


def test_validation_rejects_a_member_naming_a_unit_the_index_does_not_publish(tmp_path):
    """The in-document half of the same reconciliation, re-derived from the published tables."""

    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    document, binary = read_glb(published)
    root = document["extensions"][CORPUS_INDEX_EXTENSION]
    root["units"] = [row for row in root["units"] if row["asset"] != "vtmb:model:prop"]
    published.write_bytes(encode_glb(document, binary))
    with pytest.raises(validation.CorpusIndexValidationError, match="vtmb:model:prop"):
        validation.validate(published)


def test_validation_rejects_a_member_whose_unit_states_other_bytes(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    document, binary = read_glb(published)
    for row in document["extensions"][CORPUS_INDEX_EXTENSION]["members"]:
        if row["path"] == "materials/wall.vmt":
            row["sha256"] = "0" * 64
            break
    published.write_bytes(encode_glb(document, binary))
    with pytest.raises(validation.CorpusIndexValidationError, match="decoded from"):
        validation.validate(published)


def test_export_time_validation_rejects_an_edge_no_unit_declares(tmp_path):
    """`references[]` is every unit's `dependencies` and nothing else -- the pass that belongs to
    the run that wrote both tables, which is why a later single-unit refresh is not held to it."""

    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    document, binary = read_glb(published)
    root = document["extensions"][CORPUS_INDEX_EXTENSION]
    root["references"] = [row for row in root["references"] if row["role"] != "texture"]
    root["inverse"] = {
        target: rows for target, rows in root["inverse"].items()
        if target != "vtmb:texture:wall"
    }
    root["summary"]["references"] = len(root["references"])
    published.write_bytes(encode_glb(document, binary))
    validation.validate(published)                       # standalone validation is unchanged
    with pytest.raises(validation.CorpusIndexValidationError, match="dependency row"):
        validation.validate_document(document, binary, export_root=export_root,
                                     export_time=True)


def test_validation_rejects_a_byte_ledger(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    document, binary = read_glb(published)
    document["extensions"][CORPUS_INDEX_EXTENSION]["coverage"]["byteLedger"] = [{"ranges": []}]
    published.write_bytes(encode_glb(document, binary))
    with pytest.raises(validation.CorpusIndexValidationError, match="byte ledger"):
        validation.validate(published)


# --- the single-unit refresh ------------------------------------------------------------------


def test_a_single_unit_command_refreshes_that_units_row(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    before = {row["asset"]: dict(row) for row in root_of(published)["units"]}

    changed = publish(export_root, "textures/wall.glb", "vtmb:texture:wall",
                      resolution={"width": 64},
                      sources=source_rows(result, "materials/wall.tth", "materials/wall.ttz"))
    assert exporter.refresh_unit(export_root, changed) is True

    after = {row["asset"]: dict(row) for row in root_of(published)["units"]}
    assert after["vtmb:texture:wall"] != before["vtmb:texture:wall"]
    assert after["vtmb:texture:wall"]["sha256"] == hashlib.sha256(
        changed.read_bytes()
    ).hexdigest()
    assert set(after) == set(before)
    validation.validate(published)


def test_a_refresh_adds_a_unit_the_index_had_never_seen(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    added = publish(export_root, "textures/extra.glb", "vtmb:texture:extra")
    assert exporter.refresh_unit(export_root, added) is True
    root = root_of(published)
    assert "vtmb:texture:extra" in {row["asset"] for row in root["units"]}
    assert root["summary"]["units"] == len(root["units"])
    validation.validate(published)


def test_a_refresh_recounts_the_census_over_the_units_it_rewrote(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    added = publish(export_root, "textures/extra.glb", "vtmb:texture:extra")
    before = {row["kind"]: dict(row) for row in root_of(published)["census"]["byKind"]}
    assert exporter.refresh_unit(export_root, added) is True
    after = {row["kind"]: dict(row) for row in root_of(published)["census"]["byKind"]}
    assert after["texture"]["count"] == before["texture"]["count"] + 1
    assert after["texture"]["byteLength"] == (
        before["texture"]["byteLength"] + added.stat().st_size
    )
    validation.validate(published)


def test_a_census_that_contradicts_the_unit_table_is_refused(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    document, binary = read_glb(published)
    root = document["extensions"][CORPUS_INDEX_EXTENSION]
    root["census"]["byKind"][0]["count"] += 1
    published.write_bytes(encode_glb(document, binary))
    with pytest.raises(validation.CorpusIndexValidationError):
        validation.validate(published)


def test_a_refresh_interrupted_before_the_rename_leaves_the_published_index(tmp_path,
                                                                             monkeypatch):
    # The refresh republishes the index, so it publishes the way the contract publishes: the
    # file on disk holds either the index it held before or the whole refreshed one.
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    before = published.read_bytes()
    added = publish(export_root, "textures/extra.glb", "vtmb:texture:extra")

    monkeypatch.setattr(os, "replace", die_on_rename)
    with pytest.raises(OSError):
        exporter.refresh_unit(export_root, added)

    monkeypatch.undo()
    assert published.read_bytes() == before
    validation.validate(published)


def test_a_refresh_that_would_publish_an_incoherent_index_writes_nothing(tmp_path):
    # Every other unit write is validated before it lands; the refresh is no exception, and its
    # caller reports the refusal rather than publishing an index that contradicts its own tables.
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    document, binary = read_glb(published)
    document["extensions"][CORPUS_INDEX_EXTENSION]["summary"]["members"] += 1
    published.write_bytes(encode_glb(document, binary))
    incoherent = published.read_bytes()

    changed = publish(export_root, "textures/wall.glb", "vtmb:texture:wall",
                      resolution={"width": 64},
                      sources=source_rows(result, "materials/wall.tth", "materials/wall.ttz"))
    with pytest.raises(validation.CorpusIndexValidationError):
        exporter.refresh_unit(export_root, changed)

    assert published.read_bytes() == incoherent


def test_a_refresh_before_the_first_index_does_nothing(tmp_path):
    export_root = tmp_path / "exports_v2"
    export_root.mkdir()
    unit = publish(export_root, "textures/wall.glb", "vtmb:texture:wall")
    assert exporter.refresh_unit(export_root, unit) is False


# --- the decode model -------------------------------------------------------------------------


def test_the_summary_counts_what_a_report_line_needs(tmp_path):
    result, export_root = indexed(tmp_path)
    model = decode_corpus_index(result, export_root)
    assert model.summary["members"] == len(result.members)
    assert model.summary["unclaimed"] == 0
    assert model.summary["checksFailed"] == 0
    assert model.summary["checksPassed"] == len(CHECK_NAMES)
    assert model.summary["units"] == len(model.units)
    assert model.summary["references"] == len(model.references)


def test_one_corpus_yields_one_byte_identical_index(tmp_path):
    result, export_root = indexed(tmp_path)
    published = exporter.export(result, export_root)
    data = published.read_bytes()
    document, binary = read_glb(published)
    # The encoder is fixed -- compact separators, writer key order, no NaN -- so re-encoding the
    # document the validator parsed reproduces the file byte for byte.
    assert encode_glb(document, binary) == data
    payload = data[20:20 + struct.unpack_from("<I", data, 12)[0]].decode("utf-8").rstrip(" ")
    assert json.loads(payload) is not None
