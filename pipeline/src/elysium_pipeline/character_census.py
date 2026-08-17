"""Which clips and grids of a baked bank a body can reach, measured over the include DAG.

A body resolves a label through its own include tree, and the first model in tree order to
define a label owns it (`exporters/npc_export`). So a bank clip whose label another model in the
tree defines first is never resolved to that bank by that body, and a clip no tree resolves there
at all is content the mount carries and nothing asks for.

Bank-level reachability is already settled elsewhere: `character_cache.reached_banks` bakes only
the banks some body's clip map names, and the declared partition carries only those. This module
answers the same question one level down -- within a baked bank, which clip and which grid a body
reaches, and by which route -- because the exporter cannot. The exporter sees one container at a
time, so it decides orphanhood from that container's own autolayer table and its own grids; the
include DAG is the only place the answer actually lives.

The distinction this exists to draw: a clip with no label route of its own that ships because a
grid this bank owns names it as a cell is **reached** content -- the blend space is how retail
plays it, and the raw form has to ship for the grid to be whole. The same clip under a grid no
body reaches is not: it ships because the bake had no reason to decline it. Both look identical
to a single-container reading, and only the DAG separates them.

The population measured is the manifest's **whole** body catalogue, never the slice a run plans:
a body left out of the measurement is a route the census cannot see, so a slice would report
reached content as orphaned. `assert_reachable` refuses to judge unless the manifest covers every
body the declared partition names, and says so instead.

A cinematic bank is excluded. It is reached by a choreographed scene's anim-set root rather than
by any body's clip map (`character_cache.reached_banks`), so no include DAG has anything to say
about it and a census over one would call the whole performance orphaned.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from elysium_pipeline import character_inventory
from elysium_pipeline.character_inventory import BASE_SEPARATOR, label_key

#: A body's include tree resolves this label to this bank, so the body can ask for it by name.
BY_LABEL = "label"
#: An autolayer host in this bank declares it. It ships composed onto that host and never under
#: its plain label, so the host's own reachability is what carries it (`assert_reachable`).
BY_HOST = "host"
#: No label route and no host, but a grid this bank owns names it as a cell and a body reaches
#: that grid. The blend space is the only thing that plays it, and it is reached content.
BY_GRID = "grid"
#: A cell of grids no body reaches. The same shape as `BY_GRID` and the opposite answer.
BY_DEAD_GRID = "dead grid"
#: Nothing reaches it.
UNREACHED = "none"

#: How many rows a failure names before it stops and counts the rest.
NAMED = 4


@dataclass
class BankCensus:
    """One bank's clips and grids, each with the single route that reaches it."""

    bank: str
    #: source clip label -> one of the route constants above.
    clips: dict[str, str] = field(default_factory=dict)
    #: grid label -> `BY_HOST`, `BY_LABEL` or `UNREACHED`.
    grids: dict[str, str] = field(default_factory=dict)
    #: Host labels declaring at least one layer that no body resolves to this bank.
    unreachable_hosts: list[str] = field(default_factory=list)

    def clips_by(self, route: str) -> list[str]:
        return sorted(label for label, reason in self.clips.items() if reason == route)

    def grids_by(self, route: str) -> list[str]:
        return sorted(label for label, reason in self.grids.items() if reason == route)


def cinematic_banks(manifest: dict) -> set[str]:
    """Every bank a choreographed scene names through one of its bone roots."""
    return {root.get("bank") for record in manifest.get("cinematics", {}).values()
            for root in record.get("roots", []) if root.get("bank")}


def label_routes(manifest: dict) -> dict[str, set[str]]:
    """{owner stem: the label keys at least one body's include tree resolves to it}.

    This is `npc_export`'s own tree walk read back off its product: each body's clip map already
    states, per label, which model in its tree won. Reading the answer rather than re-resolving
    the trees keeps the census on the same fact the runtime and `character_cache.reached_banks`
    read, so the three cannot drift apart.
    """
    routes: dict[str, set[str]] = {}
    for record in manifest.get("npcs", {}).values():
        for label, owner in record.get("clips", {}).items():
            routes.setdefault(owner, set()).add(label_key(label))
    return routes


def census_bank(npc_dir: Path, manifest: dict, bank: str, routes: dict[str, set[str]]) -> BankCensus:
    """One bank's census. Reads the blend sidecar; the container is not opened."""
    result = BankCensus(bank=bank)
    reached = routes.get(bank, set())
    sidecar = character_inventory.read_blends(npc_dir, manifest, bank)
    declaring = character_inventory.hosts_by_target(sidecar)

    for label in sidecar.get("grids", {}):
        key = label_key(label)
        if declaring.get(key):
            result.grids[label] = BY_HOST
        elif key in reached:
            result.grids[label] = BY_LABEL
        else:
            result.grids[label] = UNREACHED

    # A cell reached through a live grid outranks the same cell under a dead one: a clip shared by
    # two grids is reached as long as one of them is.
    cells: dict[str, str] = {}
    for label, grid in sidecar.get("grids", {}).items():
        route = BY_DEAD_GRID if result.grids[label] == UNREACHED else BY_GRID
        for cell in grid.get("cells", ()):
            clip = cell.get("clip")
            if not clip:
                continue
            key = label_key(clip)
            if route == BY_GRID or key not in cells:
                cells[key] = route

    # The host binding outranks a label route, because it is what the bake obeyed: a layer a host
    # declares ships as `<layer>@<host>`, and where it owns the split bone the plain form is
    # withheld on purpose, so the bare label a body's clip map still carries answers with nothing
    # (`docs/architecture/animation-architecture.md` 2.1). Reporting such a clip as label-reached
    # would name the one route the mount cannot serve.
    for label in manifest.get("banks", {}).get(bank, {}).get("clips", {}):
        key = label_key(label)
        if declaring.get(key):
            result.clips[label] = BY_HOST
        elif key in reached:
            result.clips[label] = BY_LABEL
        else:
            result.clips[label] = cells.get(key, UNREACHED)

    # A host is reached like any other clip, and a cell of a fan a body reaches counts: an aim
    # layer bound to `walk_0` rides a host the body stands on through the `walk` grid, never by
    # asking for that label. So the census's own answer decides, not the label route alone.
    dead = {UNREACHED, BY_DEAD_GRID}
    routes_by_key = {label_key(label): route for label, route in result.clips.items()}
    result.unreachable_hosts = sorted(
        {host for hosts in declaring.values() for host in hosts
         if routes_by_key.get(label_key(host), UNREACHED) in dead})
    return result


def census(npc_dir: Path, manifest: dict, banks) -> dict[str, BankCensus]:
    """{bank: census}, over the banks the include DAG can speak for, in declared order."""
    routes = label_routes(manifest)
    scenes = cinematic_banks(manifest)
    return {bank: census_bank(npc_dir, manifest, bank, routes)
            for bank in sorted(dict.fromkeys(banks))
            if bank not in scenes and bank in manifest.get("banks", {})}


def _unproved(partition: dict, manifest: dict) -> list[str]:
    """Bodies the partition declares that the manifest cannot state a clip map for."""
    npcs = manifest.get("npcs", {})
    return sorted(stem for stem in partition.get("model_family_of", {})
                  if not npcs.get(stem, {}).get("clips"))


def _hosts_no_body_reaches(censuses: dict[str, BankCensus]) -> list[str]:
    """Declaring hosts no body resolves to their own bank.

    A layer bound to a host ships only as `<layer>@<host>` -- composed onto that host's pose, with
    its plain form withheld for exactly that reason (`docs/architecture/animation-architecture.md`
    2.1). A host nothing reaches therefore takes its whole layer family down with it: the derived
    assets are addressed through a sequence no body stands on, and the plain form that would have
    answered instead was never written.
    """
    return [f"{c.bank}: host '{host}'" for c in censuses.values() for host in c.unreachable_hosts]


def _grids_that_do_not_stand(censuses: dict[str, BankCensus],
                             inventories: dict[str, character_inventory.BankInventory]) -> list[str]:
    """Grids a body reaches that the projection builds no blend space for.

    A grid label stands as a blend space or it does not stand: resolving one as a plain clip
    freezes the fan onto a single cell. The runtime names that miss; a grid a body demonstrably
    reaches can be refused here instead, before an editor process starts.
    """
    out = []
    for bank, result in censuses.items():
        inventory = inventories.get(bank)
        if inventory is None:
            continue
        built = {label_key(name.partition(BASE_SEPARATOR)[0]) for name in inventory.spaces.values()}
        for label, route in sorted(result.grids.items()):
            if route != UNREACHED and label_key(label) not in built:
                out.append(f"{bank}: grid '{label}' is reached by {route} and stands as no "
                           "blend space")
    return out


def assert_reachable(npc_dir: Path, partition: dict, manifest: dict, banks,
                     inventories: dict[str, character_inventory.BankInventory]) -> dict:
    """Take the census and prove its two defect classes; raise ValueError if either holds.

    Returns the census summary. The unreached counts are part of it rather than of the failure
    path: authored content the game itself cannot reach is not a defect of ours, and naming it
    every run is what keeps a regression that multiplies it from reading as normal.
    """
    unproved = _unproved(partition, manifest)
    censuses = census(npc_dir, manifest, banks)
    summary = _summarize(censuses, unproved)
    if unproved:
        return summary

    for reason, rows in (
        ("layer host(s) no body reaches", _hosts_no_body_reaches(censuses)),
        ("grid(s) a body reaches that stand as no blend space",
         _grids_that_do_not_stand(censuses, inventories)),
    ):
        if rows:
            raise ValueError(
                f"character plan bakes {reason}: "
                + "; ".join(sorted(rows)[:NAMED])
                + (f" (+{len(rows) - NAMED} more)" if len(rows) > NAMED else "")
            )
    return summary


def _summarize(censuses: dict[str, BankCensus], unproved: list[str]) -> dict:
    routes = (BY_LABEL, BY_HOST, BY_GRID, BY_DEAD_GRID, UNREACHED)
    counts = {route: sum(len(c.clips_by(route)) for c in censuses.values()) for route in routes}
    grids = {route: sum(len(c.grids_by(route)) for c in censuses.values())
             for route in (BY_LABEL, BY_HOST, UNREACHED)}
    orphaned = sorted(
        ((c.bank, len(c.clips_by(UNREACHED)) + len(c.clips_by(BY_DEAD_GRID)))
         for c in censuses.values()),
        key=lambda row: (-row[1], row[0]),
    )
    return {
        "banks": len(censuses),
        "clips": counts,
        "grids": grids,
        "unproved": unproved,
        "worst": [row for row in orphaned if row[1]][:NAMED],
    }


def summary_line(summary: dict) -> str:
    """The one line the export prints for a taken census."""
    if summary["unproved"]:
        return ("characters: reachability not censused -- the manifest states no clip map for "
                f"{len(summary['unproved'])} declared body/bodies: "
                + ", ".join(summary["unproved"][:NAMED]))
    clips = summary["clips"]
    reached = clips[BY_LABEL] + clips[BY_HOST] + clips[BY_GRID]
    orphaned = clips[UNREACHED] + clips[BY_DEAD_GRID]
    text = (f"characters: {reached} of {reached + orphaned} clip(s) over {summary['banks']} body "
            f"bank(s) are reachable through the include DAG ({clips[BY_LABEL]} by label, "
            f"{clips[BY_HOST]} through a declaring host, {clips[BY_GRID]} as a cell of a grid a "
            f"body reaches); {summary['grids'][UNREACHED]} grid(s) no body reaches")
    if orphaned:
        text += (f"; {orphaned} clip(s) reach no body ({clips[BY_DEAD_GRID]} of them only under "
                 "a grid that reaches none), worst: "
                 + ", ".join(f"{bank} {count}" for bank, count in summary["worst"]))
    return text
