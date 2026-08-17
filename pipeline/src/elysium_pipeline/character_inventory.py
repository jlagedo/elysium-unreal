"""The packages a character bake will write for the shared banks, and the invariant over them.

A bank is recorded on one rig and played by every compatible body, so its packages are addressed
by owner alone -- `Anims/_banks/<bank>` -- and their count is a function of the bank containers
and of nothing else. The moment a body family enters that arithmetic the mount multiplies: two
banks carry 4,551 of the cast's distinct clips, and rebuilding them per rig family once produced
95 GB before the output size exposed it.

`character_sweep.assert_shared_bank_layout` refuses the *shape* of that cross-product -- a bank
folder addressed below a body family. This module answers the harder half, the *count*: it
projects the exact package set each bank produces and proves every source clip is packaged once.
The projection is the bake's own arithmetic stated offline, so it can be checked before an editor
process starts rather than after twenty minutes of authoring.

Three rules decide a bank sequence package, and all three come from
`UElysiumSkeletalBuildLibrary::BuildAnimSequencesFromSource`:

- one package per clip payload the container carries, named `A_<BakedAssetName(label)>`;
- a payload with no frames or no tracks is not built;
- a `_delta` whose payload names no base is not built -- a delta no host declares has nothing to
  be a difference from, and retail only ever reaches one through the autolayer binding that names
  its base.

A blend space follows `BuildBlendSpacesFromGrids`: one per grid, or one per declaring host when
the autolayer table binds the grid, and only where at least two of its cells resolve to a
sequence this same projection packaged.

Reading a bank container's payload headers costs a pass over the file. The whole 136-bank corpus
scans in about a second and a half cold, which is the price of the guard.
"""
from __future__ import annotations

from dataclasses import dataclass, field
import json
from pathlib import Path

from elysium_pipeline import asset_names, character_cache
from elysium_pipeline.formats import eskm

#: `STUDIO_DELTA`. A clip whose tracks state a difference from a base pose rather than a pose of
#: its own (`docs/vtmb/animation_and_movers.md`).
DELTA_SEQUENCE = 0x4

#: Separates a derived form's own label from the host it was composed onto, as
#: `UE_mdl_skeletal.BASE_SEPARATOR` writes it.
BASE_SEPARATOR = "@"

SEQUENCE_PREFIX = "A_"
SPACE_PREFIX = "BS_"


@dataclass
class BankInventory:
    """Every package one bank produces, and what each was produced from."""

    bank: str
    #: package object path -> the payload labels that named it. More than one is a fold collision.
    sequences: dict[str, list[str]] = field(default_factory=dict)
    #: package object path -> the grid label and host suffix it was built over.
    spaces: dict[str, str] = field(default_factory=dict)
    #: Payload labels deliberately not built: a `_delta` whose payload names no base.
    unbound_additives: list[str] = field(default_factory=list)
    #: Payload labels with no frames or no tracks. The bake writes nothing for one and says
    #: nothing about it, so this is a hole rather than a decision -- kept apart from the
    #: deliberate case so it reaches the failure path instead of being excused by it.
    empty: list[str] = field(default_factory=list)
    #: Source clip labels, lowercased, packaged only in derived `<label>@<host>` form.
    derived_only: list[str] = field(default_factory=list)

    @property
    def packages(self) -> int:
        return len(self.sequences) + len(self.spaces)


def _fold(label: str) -> str:
    return asset_names.baked_asset_name(label)


def label_key(label: str) -> str:
    """A clip label as both halves of the pipeline compare it.

    Case-folded because `FName` is, and stripped of the leading `@` a raw animation name can carry
    on disk -- the exporter strips it when it names a payload, so the manifest's spelling and the
    container's have to be compared with it gone from both.
    """
    return label.lstrip(BASE_SEPARATOR).lower()


def read_blends(npc_dir: Path, manifest: dict, bank: str) -> dict:
    """One bank's blend sidecar, or `{}` when it declares none.

    A bank that authors neither a grid nor an autolayer binding ships no sidecar, which is an
    absence rather than a fault. One it declares and cannot be read is a fault, and it is named
    here so both readers of the document say the same thing about it.
    """
    relative = manifest.get("banks", {}).get(bank, {}).get("blends", "")
    if not relative:
        return {}
    path = npc_dir / relative
    try:
        with path.open(encoding="utf-8-sig") as handle:
            return json.load(handle)
    except (OSError, ValueError) as error:
        raise ValueError(
            f"bank '{bank}' declares blend sidecar '{relative}' and it cannot be read: {error}"
        ) from error


def hosts_by_target(sidecar: dict) -> dict[str, list[str]]:
    """{layer label key: the host labels declaring it}, from a blend sidecar's autolayer table."""
    out: dict[str, list[str]] = {}
    for host, targets in sidecar.get("autolayers", {}).items():
        for target in targets:
            out.setdefault(label_key(target), []).append(host)
    return out


def project_bank(npc_dir: Path, manifest: dict, bank: str) -> BankInventory | None:
    """Every package `bank` produces, or None when its container is not on disk.

    An absent container is not reported here: the bake names that failure itself, per clip owner,
    and a second voice saying the same thing sends the operator looking for two faults.
    """
    container = npc_dir / "banks" / f"{bank}.eskm"
    if not container.is_file():
        return None

    inventory = BankInventory(bank=bank)
    package_root = character_cache.bank_clips_object_path(bank)
    packaged: set[str] = set()
    layered: set[str] = set()
    for payload in eskm.clip_payloads(eskm.read(container)):
        if payload.frames <= 0 or payload.tracks <= 0:
            inventory.empty.append(payload.name)
            continue
        if payload.flags & DELTA_SEQUENCE and not payload.base:
            inventory.unbound_additives.append(payload.name)
            continue
        package = f"{package_root}/{SEQUENCE_PREFIX}{_fold(payload.name)}"
        inventory.sequences.setdefault(package, []).append(payload.name)
        packaged.add(label_key(payload.name))
        layer, separator, _host = payload.name.partition(BASE_SEPARATOR)
        if separator:
            layered.add(label_key(layer))

    source = {label_key(label) for label in manifest.get("banks", {})
              .get(bank, {}).get("clips", {})}
    inventory.derived_only = sorted((layered & source) - packaged)
    _project_spaces(npc_dir, manifest, bank, inventory, package_root)
    return inventory


def _project_spaces(npc_dir: Path, manifest: dict, bank: str, inventory: BankInventory,
                    package_root: str) -> None:
    """The blend spaces the bake writes for one bank, over the sequences it just projected."""
    sidecar = read_blends(npc_dir, manifest, bank)
    if not sidecar:
        return
    declaring = hosts_by_target(sidecar)

    for label, grid in sorted(sidecar.get("grids", {}).items()):
        hosts = sorted(declaring.get(label_key(label), ()))
        for suffix in [BASE_SEPARATOR + host for host in hosts] or [""]:
            resolved = sum(
                1 for cell in grid.get("cells", ())
                if cell.get("clip")
                and f"{package_root}/{SEQUENCE_PREFIX}{_fold(cell['clip'] + suffix)}"
                in inventory.sequences
            )
            # One sample is a clip, not a blend space, and the bake writes none for it.
            if resolved < 2:
                continue
            inventory.spaces[
                f"{package_root}/{SPACE_PREFIX}{_fold(label + suffix)}"] = label + suffix


def project(npc_dir: Path, manifest: dict, banks) -> tuple[dict[str, BankInventory], list[str]]:
    """({bank: inventory}, banks whose container is absent), in declared order."""
    inventories: dict[str, BankInventory] = {}
    absent: list[str] = []
    for bank in sorted(dict.fromkeys(banks)):
        inventory = project_bank(npc_dir, manifest, bank)
        if inventory is None:
            absent.append(bank)
        else:
            inventories[bank] = inventory
    return inventories, absent


def _collisions(inventories: dict[str, BankInventory]) -> list[str]:
    """Packages two payload labels both fold onto: the second silently overwrites the first."""
    out = []
    for inventory in inventories.values():
        for package, labels in sorted(inventory.sequences.items()):
            if len(labels) > 1:
                out.append(f"{package} <- {', '.join(sorted(labels))}")
    return out


def _unaccounted(inventories: dict[str, BankInventory], manifest: dict) -> list[str]:
    """Packaged payloads the manifest's own clip list does not explain.

    Both halves of a derived form are checked, because `<layer>@<host>` is only a correct name
    when the container and the manifest agree on the two clips it was composed from. The two come
    off separate decodes of the same model, so a divergence is a real defect rather than a
    tolerance -- and it reaches the mount as an asset nothing ever asks for.
    """
    out = []
    for bank, inventory in inventories.items():
        source = {label_key(label) for label in manifest.get("banks", {})
                  .get(bank, {}).get("clips", {})}
        for labels in inventory.sequences.values():
            for label in labels:
                layer, _, host = label.partition(BASE_SEPARATOR)
                unknown = [part for part in ((layer, "layer"), (host, "host"))
                           if part[0] and label_key(part[0]) not in source]
                if unknown:
                    out.append(f"{bank}: '{label}' names "
                               + ", ".join(f"{kind} '{name}'" for name, kind in unknown)
                               + ", which the manifest does not carry")
    return out


def _unpackaged(inventories: dict[str, BankInventory], manifest: dict) -> list[str]:
    """Source clips that reach no package at all, delta-with-no-host excepted."""
    out = []
    for bank, inventory in inventories.items():
        packaged = {label_key(label)
                    for labels in inventory.sequences.values() for label in labels}
        derived = set(inventory.derived_only)
        unbound = {label_key(label) for label in inventory.unbound_additives}
        for label, meta in sorted(manifest.get("banks", {}).get(bank, {})
                                  .get("clips", {}).items()):
            key = label_key(label)
            if key in packaged or key in derived:
                continue
            # A delta the container carries and no host declares is deliberately absent, and the
            # container is what says so -- the manifest's flag alone cannot, because a delta a host
            # DOES declare ships in derived form under the same flag.
            if key in unbound and int(meta.get("flags", 0)) & DELTA_SEQUENCE:
                continue
            out.append(f"{bank}: '{label}' produces no package")
    return out


def assert_cardinality(npc_dir: Path, partition: dict, manifest: dict, banks,
                       projected: tuple[dict[str, BankInventory], list[str]] | None = None) -> dict:
    """Prove the projected bank inventory before the editor starts; raise ValueError if not.

    Returns a summary of what it proved. The count is deliberately part of that summary rather
    than only of the failure path: a cross-product announces itself in the total long before any
    individual assertion has anything to say about it.

    `projected` hands in a projection the caller already made, so a run that also takes the
    reachability census over the same inventories reads the containers once.
    """
    declared = partition.get("bank_family_of", {})
    stray = sorted(bank for bank in dict.fromkeys(banks) if bank not in declared)
    if stray:
        raise ValueError(
            "character plan bakes bank(s) the declared partition does not name: "
            + ", ".join(stray[:4])
        )

    inventories, absent = projected if projected is not None else project(npc_dir, manifest, banks)
    root = character_cache.BANKS + "/"
    misplaced = sorted(
        package
        for inventory in inventories.values()
        for package in (*inventory.sequences, *inventory.spaces)
        if not package.startswith(root + inventory.bank + "/")
    )
    if misplaced:
        raise ValueError(
            "character plan addresses bank package(s) outside the shared bank namespace: "
            + ", ".join(misplaced[:4])
        )

    empty = sorted(f"{inventory.bank}: '{label}'"
                   for inventory in inventories.values() for label in inventory.empty)
    for reason, rows in (
        ("two payload labels fold onto one package", _collisions(inventories)),
        ("packaged clip(s) the manifest does not account for",
         _unaccounted(inventories, manifest)),
        ("clip payload(s) carry no frame or no track", empty),
        ("source bank clip(s) reach no package", _unpackaged(inventories, manifest)),
    ):
        if rows:
            raise ValueError(
                f"character plan is not one package per source bank clip -- {reason}: "
                + "; ".join(rows[:4])
                + (f" (+{len(rows) - 4} more)" if len(rows) > 4 else "")
            )

    return {
        "banks": len(inventories),
        "absent": absent,
        "sequences": sum(len(i.sequences) for i in inventories.values()),
        "blend_spaces": sum(len(i.spaces) for i in inventories.values()),
        "derived_only": sum(len(i.derived_only) for i in inventories.values()),
        "unbound_additives": sum(len(i.unbound_additives) for i in inventories.values()),
        "packages": sum(i.packages for i in inventories.values()),
    }


def summary_line(summary: dict) -> str:
    """The one line the export prints for a proved inventory."""
    text = (f"characters: {summary['packages']} bank package(s) over {summary['banks']} bank(s) "
            f"({summary['sequences']} sequence(s), {summary['blend_spaces']} blend space(s), "
            f"{summary['unbound_additives']} additive(s) no host declares)")
    if summary["absent"]:
        text += (f"; {len(summary['absent'])} bank(s) have no container and were not proved: "
                 + ", ".join(summary["absent"][:4]))
    return text
