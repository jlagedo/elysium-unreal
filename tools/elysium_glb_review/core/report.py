"""Corpus integrity sweep.

Reads every unit's JSON chunk once and reports what does not hold together: references
to units that were never exported, a unit's own declaration that it failed to account
for something, and the seam-specific warnings each exporter records about itself.

This is the tool's centre of gravity for review. It needs no Blender, and a full pass
over the corpus costs about half a minute, almost all of it spent parsing the model
extension JSON.

Two categories are deliberately not findings. A sentinel identity names a studio texture
that resolves to no VMT, and a render-target parameter names an image the engine makes
at runtime; both are complete statements about the source, so they are counted and
reported as facts rather than raised as problems.
"""

from __future__ import annotations

import time
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterator

from . import glb, ids, seams

#: The corpus directories this scan reads. The export publishes more families than
#: these; units under the rest are neither scanned nor available to resolve a
#: reference against, which is what makes a reference to one uncheckable here.
SEAM_DIRECTORIES = ("models", "materials", "textures", "surface-properties")

SENTINEL_PREFIX = "vtmb:missing-material:"


@dataclass(frozen=True)
class Finding:
    """One thing that does not hold together."""

    kind: str
    seam: str
    #: The unit that carries the problem, by identity where one exists, else path.
    unit: str
    detail: str

    def __str__(self) -> str:
        return "%-26s %-18s %s: %s" % (self.kind, self.seam, self.unit, self.detail)


@dataclass
class Report:
    """The outcome of one sweep."""

    root: Path
    files: Counter = field(default_factory=Counter)
    counts: Counter = field(default_factory=Counter)
    findings: list[Finding] = field(default_factory=list)
    seconds: float = 0.0

    @property
    def ok(self) -> bool:
        return not self.findings

    def add(self, kind: str, seam: str, unit: str, detail: str) -> None:
        self.findings.append(Finding(kind, seam, unit, detail))
        self.counts[kind] += 1


def iter_units(root: Path) -> Iterator[tuple[str, Path]]:
    """Every exported unit, as a (seam, path) pair."""
    for seam in SEAM_DIRECTORIES:
        directory = root / seam
        if not directory.is_dir():
            continue
        for path in sorted(directory.rglob("*.glb")):
            yield seam, path


def _existing_units(root: Path) -> set[str]:
    """Corpus-relative paths of every unit, for reference checks without stat calls."""
    return {path.relative_to(root).as_posix().lower() for _seam, path in iter_units(root)}


def _check_references(
    report: Report, seam: str, unit: str, document: dict, present: set[str]
) -> None:
    # A material names each of its textures twice, once in dependencies and once in
    # textureBindings. That is one missing texture, not two, so findings collapse per
    # identity and carry every place the unit named it.
    missing: dict[str, list[str]] = {}
    roles: dict[str, str] = {}

    for reference in seams.outgoing_references(document):
        asset = ids.parse(reference.identity)
        if asset is None:
            report.add("unparsable-identity", seam, unit, reference.identity)
            continue
        if asset.is_sentinel:
            report.counts["sentinel-reference"] += 1
            continue
        if not asset.resolvable:
            # The identity names a kind this reviewer does not index -- either one no
            # seam exports (an effect), or a family that publishes but is not in
            # `SEAM_DIRECTORIES` (a sound, an expression table). Counted, not checked:
            # a missing one of these would not be seen here.
            report.counts["reference-outside-corpus"] += 1
            continue

        relative = ids.relative_path(asset)
        if relative is None or relative.as_posix().lower() not in present:
            missing.setdefault(reference.identity, []).append(reference.origin)
            roles[reference.identity] = reference.role
        if not reference.resolved:
            report.counts["binding-unresolved-at-export"] += 1

    for identity, origins in missing.items():
        report.add(
            "missing-reference",
            seam,
            unit,
            "%s (%s) named by %s" % (identity, roles[identity], ", ".join(origins)),
        )


def _check_coverage(report: Report, seam: str, unit: str, payload: dict) -> None:
    cov = seams.coverage(payload)
    if cov.unresolved:
        report.add("coverage-unresolved", seam, unit, "%d row(s)" % len(cov.unresolved))
    if cov.unsupported:
        report.add("coverage-unsupported", seam, unit, "%d row(s)" % len(cov.unsupported))
    report.counts["omitted-proven"] += len(cov.omitted_proven)
    report.counts["typed-unidentified"] += len(cov.typed_unidentified)


def _check_material(report: Report, unit: str, payload: dict) -> None:
    resolution = payload.get("shaderResolution") or {}
    if not resolution.get("resolved", True):
        report.counts["shader-unresolved"] += 1
        report.counts["shader-reason:%s" % resolution.get("reason", "?")] += 1
    report.counts["shader:%s" % payload.get("shader", "?")] += 1
    for anomaly in payload.get("anomalies") or []:
        report.add("material-anomaly", "materials", unit, str(anomaly.get("role", anomaly)))


def _check_texture(report: Report, unit: str, payload: dict) -> None:
    block = payload.get("payload") or {}
    report.counts["texture-format:%s" % block.get("vkFormat", "?")] += 1
    report.counts["texture-type:%s" % block.get("textureType", "?")] += 1
    for omission in payload.get("omissions") or []:
        report.counts["texture-omission:%s" % omission.get("role", "?")] += 1

    dimensions = payload.get("dimensions") or {}
    source = payload.get("sourceFormat") or {}
    width, height = dimensions.get("width"), dimensions.get("height")
    source_width, source_height = source.get("sourceWidth"), source.get("sourceHeight")
    if None not in (width, height, source_width, source_height) and (
        width != source_width or height != source_height
    ):
        # The largest complete level is smaller than the source declared, so showing it
        # at face value would present a degraded image as the full-resolution one.
        report.add(
            "texture-below-declared-size",
            "textures",
            unit,
            "%sx%s decoded from a declared %sx%s"
            % (width, height, source_width, source_height),
        )


def _sentinel_slots(document: dict) -> int:
    total = 0
    for material in document.get("materials") or []:
        identity = seams.reference_identity(material, seams.MATERIAL_REFERENCE_EXTENSION)
        if str(identity or "").startswith(SENTINEL_PREFIX):
            total += 1
    return total


def _check_model(report: Report, unit: str, document: dict, payload: dict) -> None:
    sentinels = _sentinel_slots(document)
    if sentinels:
        report.counts["bodies-with-sentinels"] += 1
        report.counts["sentinel-slots"] += sentinels

    includes_model = any(
        reference.role == "model" for reference in seams.dependencies(payload)
    )
    if includes_model and not document.get("animations"):
        report.counts["include-stub-banks"] += 1


def scan(
    root: str | Path,
    *,
    progress: Callable[[int, int, Path], None] | None = None,
) -> Report:
    """Sweep the whole corpus and report everything that does not hold together."""
    root = Path(root)
    report = Report(root=root)
    started = time.perf_counter()

    present = _existing_units(root)
    units = list(iter_units(root))

    for index, (seam, path) in enumerate(units):
        if progress is not None:
            progress(index, len(units), path)
        report.files[seam] += 1
        relative = path.relative_to(root).as_posix()

        try:
            document = glb.read_json(path)
        except Exception as error:  # a unit that will not parse is itself the finding
            report.add("unreadable", seam, relative, str(error))
            continue

        found = seams.extension_of(document)
        if found is None:
            report.add("no-seam-extension", seam, relative, "no ELYSIUM extension")
            continue
        name, payload = found
        unit = seams.asset_id(document) or relative

        expected = seams.expected_extensions(seam, path.name)
        if expected and name not in expected:
            report.add("seam-mismatch", seam, unit, "carries %s" % name)

        _check_coverage(report, seam, unit, payload)
        _check_references(report, seam, unit, document, present)

        if name == seams.MATERIAL_EXTENSION:
            _check_material(report, unit, payload)
        elif name == seams.TEXTURE_EXTENSION:
            _check_texture(report, unit, payload)
        elif name == seams.MODEL_EXTENSION:
            _check_model(report, unit, document, payload)

    report.seconds = time.perf_counter() - started
    return report


#: Counters worth showing even when nothing is wrong, because they describe the corpus
#: rather than accusing it.
FACTS = (
    ("sentinel slots", "sentinel-slots"),
    ("bodies with sentinels", "bodies-with-sentinels"),
    ("include-stub banks", "include-stub-banks"),
    ("shaders not transcribed", "shader-unresolved"),
    ("proven omissions", "omitted-proven"),
    ("typed but unidentified", "typed-unidentified"),
    ("references to unindexed families", "reference-outside-corpus"),
    ("bindings unresolved at export", "binding-unresolved-at-export"),
)


def summary(report: Report, *, max_findings: int = 40) -> str:
    """A readable digest of a sweep."""
    lines = [
        "corpus: %s" % report.root,
        "scanned %d unit(s) in %.1fs" % (sum(report.files.values()), report.seconds),
        "  " + "  ".join("%s=%d" % pair for pair in sorted(report.files.items())),
        "",
        "facts (not problems):",
    ]
    for label, key in FACTS:
        lines.append("  %-32s %d" % (label, report.counts.get(key, 0)))

    lines.append("")
    if report.ok:
        lines.append("no findings")
        return "\n".join(lines)

    lines.append("findings: %d" % len(report.findings))
    for kind, count in Counter(f.kind for f in report.findings).most_common():
        lines.append("  %-32s %d" % (kind, count))
    lines.append("")
    for finding in report.findings[:max_findings]:
        lines.append("  " + str(finding))
    if len(report.findings) > max_findings:
        lines.append("  ... and %d more" % (len(report.findings) - max_findings))
    return "\n".join(lines)


def to_dict(report: Report) -> dict:
    """The sweep as plain data, for writing beside the corpus."""
    return {
        "root": str(report.root),
        "seconds": round(report.seconds, 3),
        "files": dict(sorted(report.files.items())),
        "counts": dict(sorted(report.counts.items())),
        "findings": [
            {
                "kind": finding.kind,
                "seam": finding.seam,
                "unit": finding.unit,
                "detail": finding.detail,
            }
            for finding in report.findings
        ],
    }
