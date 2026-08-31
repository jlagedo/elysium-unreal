"""The complete UP-first walk the corpus index is built from.

Every file below `<VTMB>/Vampire/` and `<VTMB>/Unofficial_Patch/`, and every member of every
`pack*.vpk`, keyed by lower-case, forward-slashed, install-relative path, with loose members
shadowing VPK members and the patch tree shadowing the retail tree. It walks every directory,
not the asset subset the map and character bakes index, because a member no seam looks at is
exactly what it exists to find.

Only two things are left out and both are named in `model.EXCLUDED_TREES`: the VPK container
files themselves, and the loose `maps/graphs/` and `maps/soundcache/` trees the retail engine
writes while it runs. The VPK-shipped `maps/graphs/*.ain` and `*.loc` members are not excluded;
they are the nav-graph seam's.

Disposition is decided by asking the seams, not by restating their path rules here: each entry of
`SEAM_CLAIMS` calls the seam's own `source_keys()` against this same complete index and maps each
key back to the member that selects it and to the companions that member owns.
"""

from __future__ import annotations

from dataclasses import dataclass
import glob
import hashlib
import os
import struct
from typing import Any, Callable, Iterable, Sequence

from elysium_pipeline.formats import vpk
from elysium_pipeline.formats.corpus_index_glb import residue as residue_rules
from elysium_pipeline.formats.corpus_index_glb.model import (
    EXCLUDED_LOOSE_PREFIXES,
    EXCLUDED_TREES,
    Member,
    MemberSource,
)
from elysium_pipeline.formats.unit_contract.origin import SOURCE_POLICY, origin_of

#: The BSP lump whose payload is the map's embedded ZIP.
PAKFILE_LUMP = 40
_BSP_HEADER_BYTES = 8 + 64 * 16 + 4


class InstallWalkError(RuntimeError):
    """The install cannot be walked as the corpus index needs it."""


# --- the seam claim table ---------------------------------------------------------------------
#
# One row per unit kind. `keys` is the seam's own key enumeration -- the exact function its plural
# command runs -- and `member_of` maps one key back to the install member that selects it.
# `companions_of` names the members that key's unit owns without selecting: a VTX or PHY by its
# model, a TTZ by its TTH, a LIP by its sound, an expression TXT by its VFE, a `.pyc` by its `.py`
# and a `.loc` by its `.ain`.


@dataclass(frozen=True, slots=True)
class SeamClaim:
    """How one seam's units and companions map back onto install members."""

    kind: str
    #: Every key the seam's plural command would export, given the complete index.
    keys: Callable[[dict], Iterable[str]]
    #: The member that selects the unit for one key, or None when the install holds none.
    member_of: Callable[[str, dict], str | None]
    #: The stable identity of one key's unit.
    asset_of: Callable[[str], str]
    #: `(index, {selecting member: its unit}) -> {companion member: the unit that owns it}`.
    #: Companions are resolved in bulk, from the index side, so a walk over 78k members costs one
    #: pass per seam rather than one index scan per unit.
    companions_of: Callable[[dict, dict], dict] | None = None


def _default_read(entry: tuple[str, Any]) -> bytes:
    """One index entry's bytes: the loose file, or the member inside its pack."""

    kind, value = entry
    if kind == "loose":
        with open(value, "rb") as handle:
            return handle.read()
    return vpk.extract(value)


def _read_key(index: dict, key: str) -> bytes | None:
    """The install's bytes for one key, read from the walk's own index.

    The seams' table readers default to `formats.install`, which resolves the real install root
    at import time; the corpus index has the whole merged index in hand already, so it hands the
    readers this instead and a synthetic install needs no environment.
    """

    entry = index.get(key)
    return None if entry is None else _default_read(entry)


def _first_present(index: dict, *candidates: str) -> str | None:
    for candidate in candidates:
        if candidate in index:
            return candidate
    return None


def _suffix_member(root: str, suffix: str):
    def member(key: str, index: dict) -> str | None:
        return _first_present(index, f"{root}{key}{suffix}")

    return member


def _identity_member(key: str, index: dict) -> str | None:
    return key if key in index else None


def _asset(kind: str):
    from elysium_pipeline.formats.unit_contract import asset_id

    return lambda key: asset_id(kind, key)


def _table_keys(kind: str, path: str):
    """The keys one cut table publishes, and the table path every one of them selects.

    A table member selects many units, so it carries `assets[]` beside its `asset`: the whole
    point of the surface-property and sound-script seams is that one file partitions into entries
    and each entry is a unit.
    """

    def keys(index: dict) -> list[str]:
        if path not in index:
            return []
        return _cut_table_keys(kind, path, index)

    return keys


def _cut_table_keys(kind: str, path: str, index: dict) -> list[str]:
    from elysium_pipeline.formats.sound_script_glb import model as script_model
    from elysium_pipeline.formats.sound_script_glb import source as script_source

    read = _read_key
    try:
        if kind == "surface-property":
            from elysium_pipeline.formats import surface_property_glb

            return list(surface_property_glb.load_table(index, read_bytes=read).names)
        if kind == "sound-script-manifest":
            return [script_model.MANIFEST_KEY]
        if kind == "sentence":
            return list(script_source.load_sentence_table(index, read_bytes=read).names)
        if kind == "dsp-preset":
            return [str(value) for value in script_source.load_dsp_table(index, read_bytes=read).ids]
        # `soundscape` is an ordinary KeyValues table, one file to one set of names. The game
        # sounds are not: two tables share one namespace, so `_game_sound_claim` asks the seam's
        # own directory instead of reading either table here.
        return list(script_source.load_kv_table(index, path, read_bytes=read).names)
    except Exception:                                    # noqa: BLE001 - a table this seam cannot
        # parse still has to appear in the index; it becomes an unclaimed member rather than
        # aborting the whole walk, which is exactly the signal the guarantee exists to raise.
        return []


def _game_sound_claim() -> SeamClaim:
    """The one game-sound claim over the two tables the seam's directory merges.

    `seam_map_sound_script.md` gives `game_sounds_surfaceproperties.txt` and `sounds.txt` one
    identity namespace, and `source.GameSoundDirectory` resolves the 16 names both declare in
    favour of the live table -- the manifest precaches only that one -- recording the dormant
    twin as the live unit's `shadowed-dormant-entry` anomaly. Two independent claims, one per
    table, would attribute those 16 units to `sounds.txt`, which is not the member their bytes
    were cut from; this is one claim over both tables applying that same resolution, so a
    shadowed name's member is the live table and `sounds.txt` claims only the entries it owns.
    Each table is still loaded on its own, so an install missing one of them keeps the other.
    """

    owners: dict[str, str] = {}

    def keys(index: dict) -> list[str]:
        from elysium_pipeline.formats.sound_script_glb import source as script_source
        from elysium_pipeline.formats.sound_script_glb.model import TABLE_PATHS

        owners.clear()
        # Dormant first, then live, so a name both tables declare ends on the live table: that is
        # `GameSoundDirectory.owner`'s rule, and the exporter cuts the unit from the same member.
        for role in ("game-sound-dormant", "game-sound-live"):
            try:
                table = script_source.load_kv_table(
                    index, TABLE_PATHS[role], read_bytes=_read_key
                )
            except Exception:                            # noqa: BLE001 - a table this seam cannot
                # parse, or does not ship, leaves its members unclaimed exactly as
                # `_cut_table_keys` does; the other table is still asked.
                continue
            for name in table.names:
                owners[name] = table.path
        return sorted(owners)

    def member_of(key: str, index: dict) -> str | None:
        return _first_present(index, owners.get(key, ""))

    return SeamClaim("sound-script", keys, member_of, _asset("sound-script"))


def _sibling_companions(suffixes: Sequence[str], owner_suffix: str):
    """The generic companion rule: a member whose stem carries `owner_suffix` in the same
    directory is owned by that member's unit."""

    def companions(index: dict, units: dict) -> dict[str, str]:
        found: dict[str, str] = {}
        for path in index:
            for suffix in suffixes:
                if not path.endswith(suffix):
                    continue
                owner = path[: -len(suffix)] + owner_suffix
                if owner in units and path not in units:
                    found[path] = units[owner]
                break
        return found

    return companions


def _model_companions(index: dict, units: dict) -> dict[str, str]:
    """Every VTX and PHY spelling beside one MDL.

    The model seam opens two VTX variants; the install also ships the odd bare `.vtx`. All of them
    are owned by the MDL's unit -- `seam_map_corpus_index.md` says "a VTX or PHY by its model" --
    so the companion rule is the stem, not the subset the decoder happens to open.
    """

    found: dict[str, str] = {}
    for path in index:
        if not path.startswith("models/"):
            continue
        if path.endswith(".vtx"):
            bare = path[: -len(".vtx")]
            # `<model>.dx80.vtx` and `<model>.dx7_2bone.vtx` carry a variant infix; `<model>.vtx`
            # does not. Both spellings are tried, so a model stem that itself carries a dot is
            # still matched by the second.
            stems = [bare.rpartition(".")[0], bare] if "." in bare.rsplit("/", 1)[-1] else [bare]
        elif path.endswith(".phy"):
            stems = [path[: -len(".phy")]]
        else:
            continue
        for stem in stems:
            owner = stem + ".mdl"
            if owner in units:
                found[path] = units[owner]
                break
    return found


def _sound_companions(index: dict, units: dict) -> dict[str, str]:
    """A LIP by its sound. Both spellings of one stem are units and both own the one LIP beside
    them, so the `.wav` unit is named where the install ships one and the `.mp3` unit otherwise."""

    found: dict[str, str] = {}
    for path in index:
        if not path.startswith("sound/") or not path.endswith(".lip"):
            continue
        stem = path[: -len(".lip")]
        for owner in (stem + ".wav", stem + ".mp3"):
            if owner in units:
                found[path] = units[owner]
                break
    return found


def _expression_member(key: str, index: dict) -> str | None:
    """The VFE selects the unit when it exists; a stem that ships only a TXT is still a unit."""

    return _first_present(index, f"expressions/{key}.vfe", f"expressions/{key}.txt")


def _script_member(key: str, index: dict) -> str | None:
    return _first_present(index, f"python/{key}.py", f"python/{key}.pyc")


def _seam_keys(module: str, attribute: str = "source_keys"):
    def keys(index: dict) -> list[str]:
        import importlib

        return list(getattr(importlib.import_module(module), attribute)(index))

    return keys


_EXPORTERS = "elysium_pipeline.exporters."
_FORMATS = "elysium_pipeline.formats."

#: Every seam whose units and companions the walk resolves, in `export_manager.GLB_SEAMS` order.
SEAM_CLAIMS: tuple[SeamClaim, ...] = (
    SeamClaim("texture", _seam_keys(_FORMATS + "texture_glb"),
              _suffix_member("materials/", ".tth"),
              _asset("texture"), _sibling_companions((".ttz",), ".tth")),
    SeamClaim("surface-property",
              _table_keys("surface-property", "scripts/surfaceproperties.txt"),
              lambda key, index: _first_present(index, "scripts/surfaceproperties.txt"),
              _asset("surface-property")),
    SeamClaim("material", _seam_keys(_FORMATS + "material_glb"),
              _suffix_member("materials/", ".vmt"), _asset("material")),
    SeamClaim("image", _seam_keys(_EXPORTERS + "image_glb"), _identity_member, _asset("image")),
    SeamClaim("sound", _seam_keys(_FORMATS + "sound_glb"),
              _suffix_member("sound/", ""), _asset("sound"), _sound_companions),
    SeamClaim("expression-table", _seam_keys(_EXPORTERS + "expression_table_glb"),
              _expression_member, _asset("expression-table"),
              _sibling_companions((".txt",), ".vfe")),
    SeamClaim("shader-source",
              _seam_keys(_EXPORTERS + "shader_program_glb", "shader_source_source_keys"),
              _suffix_member("materials/dxshaders/", ".psh"), _asset("shader-source")),
    SeamClaim("shader-program", _seam_keys(_EXPORTERS + "shader_program_glb"),
              _suffix_member("shaders/", ".vcs"), _asset("shader-program")),
    SeamClaim("particle", _seam_keys(_EXPORTERS + "particle_glb"),
              _suffix_member("particles/", ".txt"), _asset("particle")),
    SeamClaim("font", _seam_keys(_EXPORTERS + "font_glb"),
              _suffix_member("materials/fonts/", ".fnt"), _asset("font")),
    SeamClaim("font-list", lambda index: ["fontlist"],
              lambda key, index: _first_present(index, "materials/fonts/fontlist.txt"),
              _asset("font-list")),
    _game_sound_claim(),
    SeamClaim("sound-script-manifest",
              _table_keys("sound-script-manifest", "scripts/game_sounds_manifest.txt"),
              lambda key, index: _first_present(index, "scripts/game_sounds_manifest.txt"),
              _asset("sound-script-manifest")),
    SeamClaim("soundscape", _table_keys("soundscape", "scripts/soundscapes.txt"),
              lambda key, index: _first_present(index, "scripts/soundscapes.txt"),
              _asset("soundscape")),
    SeamClaim("sentence", _table_keys("sentence", "scripts/sentences.txt"),
              lambda key, index: _first_present(index, "scripts/sentences.txt"),
              _asset("sentence")),
    SeamClaim("dsp-preset", _table_keys("dsp-preset", "scripts/dsp_presets.txt"),
              lambda key, index: _first_present(index, "scripts/dsp_presets.txt"),
              _asset("dsp-preset")),
    SeamClaim("sound-scheme", _seam_keys(_FORMATS + "sound_scheme_glb"),
              _suffix_member("sound/schemes/", ".txt"), _asset("sound-scheme")),
    SeamClaim("scene", _seam_keys(_EXPORTERS + "scene_glb"),
              _suffix_member("sound/", ".vcd"), _asset("scene")),
    SeamClaim("model", _seam_keys(_EXPORTERS + "model_glb"),
              _suffix_member("models/", ".mdl"), _asset("model"), _model_companions),
    SeamClaim("dialogue", _seam_keys(_EXPORTERS + "dialogue_glb"),
              _suffix_member("dlg/", ".dlg"), _asset("dialogue")),
    SeamClaim("vdata", _seam_keys(_FORMATS + "vdata_glb"),
              _suffix_member("vdata/", ".txt"), _asset("vdata")),
    SeamClaim("ui-resource", _seam_keys(_FORMATS + "ui_resource_glb"), _identity_member,
              _asset("ui-resource")),
    SeamClaim("script", _seam_keys(_EXPORTERS + "script_glb"), _script_member,
              _asset("script"), _sibling_companions((".pyc",), ".py")),
    # One BSP selects four units: the root and its three lump-family sub-units, so its member row
    # carries all four in `assets[]`.
    SeamClaim("map", _seam_keys(_EXPORTERS + "map_glb"), _suffix_member("maps/", ".bsp"),
              _asset("map")),
    SeamClaim("map-entities", _seam_keys(_EXPORTERS + "map_entities_glb"),
              _suffix_member("maps/", ".bsp"), _asset("map-entities")),
    SeamClaim("map-lighting", _seam_keys(_EXPORTERS + "map_lighting_glb"),
              _suffix_member("maps/", ".bsp"), _asset("map-lighting")),
    SeamClaim("map-visibility", _seam_keys(_EXPORTERS + "map_visibility_glb"),
              _suffix_member("maps/", ".bsp"), _asset("map-visibility")),
    SeamClaim("nav-graph", _seam_keys(_EXPORTERS + "nav_graph_glb"),
              _suffix_member("maps/graphs/", ".ain"), _asset("nav-graph"),
              _sibling_companions((".loc",), ".ain")),
    SeamClaim("engine-config", _seam_keys(_FORMATS + "engine_config_glb"), _identity_member,
              _asset("engine-config")),
)


# --- the walk ---------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class InstallWalk:
    """One complete UP-first walk: the members, the index the seams were asked against, and the
    install this index describes."""

    #: `{install-relative key: ("loose", path) | ("vpk", (pack, offset, size))}` -- the same shape
    #: `install.build_index` produces, over every directory rather than the asset subset.
    index: dict[str, tuple[str, Any]]
    members: tuple[Member, ...]
    source_resolution: dict[str, Any]
    excluded_trees: tuple[dict[str, Any], ...] = EXCLUDED_TREES

    def by_path(self) -> dict[str, Member]:
        return {member.path: member for member in self.members}

    def counts(self) -> dict[str, int]:
        totals = {"unit": 0, "companion": 0, "residue": 0, "unclaimed": 0}
        for member in self.members:
            totals[member.disposition] += 1
        return totals

    def unclaimed(self) -> list[str]:
        return [member.path for member in self.members if member.disposition == "unclaimed"]

    def with_reference_graph(
        self, targets: Iterable[str], read_bytes: Callable[[tuple[str, Any]], bytes] | None = None
    ) -> "InstallWalk":
        """The same walk with the residue rules whose evidence is the reference graph applied.

        One residue rule cannot be decided from the install alone: a `sound/` member with no
        extension is unreachable because *nothing names it*, and that is a fact about the
        published corpus, not about the tree. The walk therefore leaves such a member unclaimed
        and the corpus index re-asks the rules once it has read every unit's dependencies --
        `targets` is the key set of `inverse`. Only `unclaimed` members are re-asked, so no
        disposition a seam decided can be overwritten here.
        """

        facts = _facts(self.index, read_bytes or _default_read, referenced=targets)
        members: list[Member] = []
        changed = False
        for member in self.members:
            classified = (
                residue_rules.classify(member.path, facts, member.source.byte_length)
                if member.disposition == "unclaimed"
                else None
            )
            if classified is None:
                members.append(member)
                continue
            changed = True
            members.append(
                Member(
                    path=member.path,
                    source=member.source,
                    shadowed=member.shadowed,
                    disposition="residue",
                    embedded=member.embedded,
                    evidence=classified,
                )
            )
        if not changed:
            return self
        members = tuple(members)
        return InstallWalk(
            index=self.index,
            members=members,
            source_resolution=self.source_resolution,
            excluded_trees=self.excluded_trees,
        )


def _facts(
    index: dict,
    read: Callable[[tuple[str, Any]], bytes],
    *,
    referenced: Iterable[str] | None = None,
) -> residue_rules.InstallFacts:
    """The install as the residue rules ask about it, over this walk's own index.

    The content-signature rules read the member they classify; the reader is handed in rather
    than resolved from the environment, so a synthetic install answers them too.
    """

    def head(key: str) -> bytes:
        entry = index.get(key)
        return b"" if entry is None else read(entry)

    return residue_rules.InstallFacts.of(index, head=head, referenced=referenced)


def _hash(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _hash_file(path: str) -> tuple[int, str]:
    digest = hashlib.sha256()
    length = 0
    with open(path, "rb") as handle:
        while True:
            block = handle.read(1 << 20)
            if not block:
                break
            length += len(block)
            digest.update(block)
    return length, digest.hexdigest()


def build_index(
    game: str | os.PathLike[str],
    patch: str | os.PathLike[str] | None = None,
) -> tuple[dict[str, tuple[str, Any]], dict[str, list[tuple[str, Any]]], list[str]]:
    """The complete UP-first index, the shadow map, and the VPK containers it was built from.

    The shadow map holds every losing source for a key, highest precedence first, so
    retail-versus-patch divergence is a query on the published index rather than a re-walk.
    """

    game = os.fspath(game)
    if not os.path.isdir(game):
        raise InstallWalkError(f"{game} is not an install root; the index would describe nothing")
    roots = [game] if patch is None else [game, os.fspath(patch)]
    containers = sorted(glob.glob(os.path.join(game, "pack*.vpk")))

    index: dict[str, tuple[str, Any]] = {}
    shadow: dict[str, list[tuple[str, Any]]] = {}

    def place(key: str, entry: tuple[str, Any]) -> None:
        """Assignment is last-writer-wins; the writer it displaced becomes a shadowed loser."""
        if key in index:
            shadow.setdefault(key, []).insert(0, index[key])
        index[key] = entry

    layers: list[dict[str, tuple[str, Any]]] = []
    for container in containers:
        try:
            entries = vpk.index_vpk(container)
        except Exception:                                # noqa: BLE001 - an unreadable or empty
            # pack is skipped exactly as `vpk.index_all` skips it; the container itself still
            # appears in `sourceResolution.members[]`.
            continue
        # One layer per pack, in pack-name order, so a member two packs both ship keeps the
        # loser rather than being silently overwritten by the later pack.
        layers.append({key: ("vpk", value) for key, value in entries.items()})

    for root in roots:
        loose: dict[str, tuple[str, Any]] = {}
        for directory, subdirectories, files in os.walk(root):
            relative = os.path.relpath(directory, root).replace("\\", "/").lower()
            prefix = "" if relative == "." else relative + "/"
            # The retail engine writes its own caches below `maps/` while it runs, so those two
            # loose trees are pruned before they are descended into. The VPK-shipped
            # `maps/graphs/` members are unaffected: they came from the pack layer.
            subdirectories[:] = [
                name
                for name in subdirectories
                if f"{prefix}{name.lower()}/" not in EXCLUDED_LOOSE_PREFIXES
            ]
            if prefix in EXCLUDED_LOOSE_PREFIXES:
                continue
            for name in files:
                key = prefix + name.lower()
                if not prefix and key.startswith("pack") and key.endswith(".vpk"):
                    continue                     # the container is the source of its members
                loose[key] = ("loose", os.path.join(directory, name))
        layers.append(loose)

    for layer in layers:
        for key, entry in layer.items():
            place(key, entry)
    return index, shadow, containers


def _source_of(entry: tuple[str, Any], read_bytes) -> MemberSource:
    kind, value = entry
    if kind == "loose":
        length, digest = _hash_file(value)
        return MemberSource(origin_of(entry).to_json(), length, digest)
    data = read_bytes(entry)
    return MemberSource(origin_of(entry).to_json(), len(data), _hash(data))


def _source_resolution(
    roots: Sequence[tuple[str, str]], containers: Sequence[str]
) -> dict[str, Any]:
    """The install roots and every VPK container with its byte length and SHA-256, so the index
    states which install it describes."""

    members: list[dict[str, Any]] = []
    for role, root in roots:
        members.append({"role": role, "path": str(root), "kind": "install-root"})
    for container in containers:
        length, digest = _hash_file(container)
        members.append(
            {
                "role": "vpk-container",
                "path": os.path.basename(container).lower(),
                "kind": "vpk",
                "byteLength": length,
                "sha256": digest,
            }
        )
    return {"policy": SOURCE_POLICY, "members": members}


def _pakfile_members(
    data: bytes,
    map_key: str,
    bsp_origin: dict[str, Any],
    claimed: set[str],
) -> list[dict[str, Any]]:
    """The PAKFILE members one BSP carries, each with the unit it became.

    PAKFILE members are not install members: they appear under their map's `embedded[]` with their
    `bsp-pakfile` origin and the material or texture unit each became. `claimed` is the set of
    identities the seams claim over this same install, so `asset` names a unit only where one
    exists; an embedded member whose key no seam publishes became nothing and says so.
    """

    from elysium_pipeline.formats.map_glb import pakfile as pakfile_reader
    from elysium_pipeline.formats.unit_contract import asset_id

    if len(data) < _BSP_HEADER_BYTES:
        return []
    offset, length = struct.unpack_from("<ii", data, 8 + PAKFILE_LUMP * 16)
    if length <= 0 or offset < 0 or offset + length > len(data):
        return []
    try:
        container = pakfile_reader.parse(data, offset, length, [])
    except Exception:                                    # noqa: BLE001 - a map whose PAKFILE this
        # reader cannot parse is the map seam's failure to report, not the index's; the index
        # states the map member and leaves `embedded[]` empty.
        return []
    rows: list[dict[str, Any]] = []
    for member in container.members:
        name = member.name.replace("\\", "/").lower()
        extension = member.extension
        if extension == ".vmt":
            stem = name[len("materials/"):] if name.startswith("materials/") else name
            unit = asset_id("material", stem[: -len(".vmt")])
        elif extension in (".tth", ".ttz"):
            stem = name[len("materials/"):] if name.startswith("materials/") else name
            unit = asset_id("texture", stem[: -len(extension)])
        else:
            unit = None
        if unit is not None and unit not in claimed:
            unit = None
        rows.append(
            {
                "member": member.name,
                "origin": {
                    "kind": "bsp-pakfile",
                    "map": map_key,
                    "member": member.name,
                    "origin": bsp_origin,
                },
                "byteLength": int(member.uncompressed_size),
                "sha256": member.sha256,
                "asset": unit,
            }
        )
    return rows


def _claims(
    index: dict, seams: Sequence[str] | None
) -> tuple[dict[str, list[str]], dict[str, str], set[str]]:
    """`(path -> owning unit assets)` for selecting members, `(path -> owning unit)` for
    companions, and the set of every asset a seam claims, by asking each seam its own
    `source_keys()`.

    A key a seam publishes need not select an install member -- the PAKFILE-only texture and
    material units SF-1.3/1.4 add have no member below `materials/` at all, only bytes inside a
    BSP's own zip -- so `claimed` is built from every key's asset unconditionally, while the
    `units`/`own` install-member bookkeeping is kept only where a selecting member exists.
    """

    units: dict[str, list[str]] = {}
    companions: dict[str, str] = {}
    claimed: set[str] = set()
    for claim in SEAM_CLAIMS:
        if seams is not None and claim.kind not in seams:
            continue
        own: dict[str, str] = {}
        for key in claim.keys(index):
            asset = claim.asset_of(key)
            claimed.add(asset)
            selecting = claim.member_of(key, index)
            if selecting is None:
                continue
            owners = units.setdefault(selecting, [])
            if asset not in owners:
                owners.append(asset)
            own.setdefault(selecting, asset)
        if claim.companions_of is not None:
            for path, asset in claim.companions_of(index, own).items():
                companions.setdefault(path, asset)
    # A member that selects a unit is never also a companion: `unit` wins, so a font page that is
    # both the font's page and the texture seam's own `.tth` unit is published as the unit it is.
    for path in units:
        companions.pop(path, None)
    return units, companions, claimed


def collect(
    index: dict[str, tuple[str, Any]] | None = None,
    *,
    shadow: dict[str, list[tuple[str, Any]]] | None = None,
    game: str | os.PathLike[str] | None = None,
    patch: str | os.PathLike[str] | None = None,
    containers: Sequence[str] | None = None,
    seams: Sequence[str] | None = None,
    read_bytes: Callable[[tuple[str, Any]], bytes] | None = None,
) -> InstallWalk:
    """Walk the install, hash every winning member, and give each one its disposition.

    With no arguments this walks the real install `elysium_pipeline.paths.vtmb_root()` names.
    `game`/`patch` walk a synthetic install; `index`/`shadow` skip the walk entirely and describe
    an install a caller already resolved.
    """

    read = read_bytes or _default_read
    roots: list[tuple[str, str]] = []
    if index is None:
        if game is None:
            from elysium_pipeline.paths import vtmb_root

            root = os.fspath(vtmb_root())
            game = os.path.join(root, "Vampire")
            patch = os.path.join(root, "Unofficial_Patch")
        index, shadow, walked = build_index(game, patch)
        containers = walked if containers is None else containers
        roots = [("retail", str(game))]
        if patch is not None:
            roots.append(("patch", str(patch)))
    shadow = shadow or {}
    containers = list(containers or ())

    unit_claims, companion_claims, claimed = _claims(index, seams)
    facts = _facts(index, read)
    #: Every identity some seam claims, so a PAKFILE member names the unit it became only where
    #: a seam publishes that key and null otherwise -- including a PAKFILE-only key, which
    #: selects no install member and so never reaches `unit_claims`.

    members: list[Member] = []
    for path in sorted(index):
        entry = index[path]
        source = _source_of(entry, read)
        losers = tuple(_source_of(loser, read) for loser in shadow.get(path, ()))
        embedded: tuple[dict[str, Any], ...] = ()
        owners = unit_claims.get(path)
        if owners:
            disposition, asset = "unit", owners[0]
            evidence = None
        elif path in companion_claims:
            disposition, asset, evidence = "companion", companion_claims[path], None
        else:
            classified = residue_rules.classify(path, facts, source.byte_length)
            if classified is not None:
                disposition, asset, evidence = "residue", None, classified
            else:
                disposition, asset, evidence = "unclaimed", None, None
        if path.startswith("maps/") and path.endswith(".bsp"):
            embedded = tuple(
                _pakfile_members(
                    read(entry), path[len("maps/"):-len(".bsp")], source.origin, claimed
                )
            )
        members.append(
            Member(
                path=path,
                source=source,
                shadowed=losers,
                disposition=disposition,
                asset=asset,
                assets=tuple(owners) if owners and len(owners) > 1 else (),
                embedded=embedded,
                evidence=evidence,
            )
        )

    return InstallWalk(
        index=index,
        members=tuple(members),
        source_resolution=_source_resolution(roots, containers),
    )
