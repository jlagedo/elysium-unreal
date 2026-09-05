"""Installed-source identity and animation metadata for the retail comparators.

Only the winning install members and the low-level MDL readers are authorities here.
Capture names remain body basenames and path-folded bank names; neither a producer's
catalogue nor its decoded products may resolve them. Ambiguous aliases are errors,
never a first directory match. VTX members are not needed to evaluate a skeleton or
an animation: even the shared banks can carry placeholder geometry.
"""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Callable, Mapping

from elysium_pipeline.formats import mdl_skel as S


def _key(value: str) -> str:
    return value.replace("\\", "/").lower()


def _fold(value: str) -> str:
    # The capture's existing path-safe spelling, independent of either exporter.
    return "".join(c if c.isalnum() or c in "._-" else "_" for c in value.lower())


def bank_stem(key: str) -> str:
    return _fold(_key(key).removeprefix("models/").removesuffix(".mdl"))


class OracleSource:
    """One read-only install index, with lazy MDL and metadata caches.

    ``index`` and ``read`` together allow synthetic installed-byte tests. Normal
    callers pass neither; install is imported lazily so arithmetic-only tests do
    not need machine roots. ``read`` has the same signature as ``install.read``.
    """

    def __init__(self, *, index: Mapping | None = None,
                 read: Callable | None = None):
        if (index is None) != (read is None):
            raise ValueError("index and read must be supplied together")
        if index is None:
            from elysium_pipeline.formats import install
            index = install.build_index(verbose=False)
            read = install.read
        self.index = index
        self._read = read
        self._aliases: dict[str, set[str]] = defaultdict(set)
        for raw in index:
            key = _key(raw)
            if not key.startswith("models/") or not key.endswith(".mdl"):
                continue
            self._aliases[bank_stem(key)].add(key)
            self._aliases[_fold(key.rsplit("/", 1)[-1][:-4])].add(key)
        self._bytes: dict[str, bytes | None] = {}
        self._sequences: dict[str, dict] = {}
        self._numbers: dict[str, dict[int, tuple[str, str]]] = {}
        self._owners: dict[str, dict[str, tuple[str, str]]] = {}
        self._tracks: dict[tuple[str, str], frozenset[str] | None] = {}

    def model_key(self, owner: str) -> str:
        normalized = _key(owner)
        # A caller may already carry the raw model path (or the canonical id).
        normalized = normalized.removeprefix("vtmb:model:")
        if "/" in normalized or normalized.endswith(".mdl"):
            candidate = normalized.removesuffix(".mdl") + ".mdl"
            if not candidate.startswith("models/"):
                candidate = "models/" + candidate
            if candidate in self.index:
                return candidate
        matches = self._aliases.get(normalized, set())
        if len(matches) == 1:
            return next(iter(matches))
        if matches:
            raise KeyError(f"ambiguous installed model owner '{owner}': {', '.join(sorted(matches))}")
        raise KeyError(f"no installed MDL for owner '{owner}'")

    def read_model(self, key: str) -> bytes | None:
        key = _key(key)
        if key not in self._bytes:
            data = self._read(self.index, key)
            if data is not None and (len(data) < 412 or data[:4] != b"IDST"
                                     or S._i32(data, 4) != 2531):
                raise ValueError(f"'{key}' is not a v2531 installed MDL")
            self._bytes[key] = data
        return self._bytes[key]

    def data(self, owner: str) -> bytes:
        key = self.model_key(owner)
        data = self.read_model(key)
        if data is None:
            raise KeyError(f"installed MDL '{key}' cannot be read")
        return data

    def sequences(self, owner: str) -> dict:
        key = self.model_key(owner)
        if key not in self._sequences:
            self._sequences[key] = {s.label.lower(): s for s in S.local_sequences(self.data(key))}
        return self._sequences[key]

    def _resolve(self, stem: str) -> None:
        if stem in self._numbers:
            return
        root = self.model_key(stem)
        numbers, owners = {}, {}
        for key, data, base in S.first_reference_bases(self.read_model, root):
            key = _key(key)
            owner = stem if key == root else bank_stem(key)
            # Keep all local positions, including duplicate/unplayable descriptors.
            # Repeated include blocks advance the counter in first_reference_bases;
            # only a model's first block is a first-match lookup identity.
            for local, label in enumerate(S.local_sequence_labels(data)):
                if label:
                    numbers[base + local] = (owner, label)
            for label, seq in self.sequences(key).items():
                owners.setdefault(label, (owner, seq.label))
        self._numbers[stem], self._owners[stem] = numbers, owners

    def resolve_global(self, stem: str, sequence: int) -> tuple[str | None, str | None]:
        self._resolve(stem)
        return self._numbers[stem].get(sequence, (None, None))

    def sequence_labels(self, stem: str) -> dict[int, str]:
        self._resolve(stem)
        # Capture comparison speaks the first spelling a label lookup encounters,
        # even when a later owner's descriptor uses different case.
        return {number: self._owners[stem].get(label.lower(), (_owner, label))[1]
                for number, (_owner, label) in self._numbers[stem].items()}

    def find_sequence(self, stem: str, label: str) -> tuple[str, str]:
        self._resolve(stem)
        try:
            return self._owners[stem][label.lower()]
        except KeyError:
            raise KeyError(f"'{stem}' does not play '{label}' in the install") from None

    def tracked(self, owner: str, label: str) -> set[str] | None:
        """Bones the committed clip's animation owns, or None for an unknown clip.

        A sequence label denotes its base cell; a named grid sample denotes that
        local animation. Weight, not RLE-offset presence, determines ownership:
        a nonzero-weight record with no offsets is an authored donor-bind pose.
        No pose decoding or producer-added split-root/anchor tracks enter this set.
        """
        key = self.model_key(owner)
        cache_key = (key, label.lower())
        if cache_key not in self._tracks:
            data = self.data(key)
            seq = self.sequences(key).get(label.lower())
            anim = (seq.base, seq.frames, seq.fps) if seq else S.find_anim(data, label)
            tracked = None
            if anim is not None:
                bones = S.read_bones(data)
                records = anim[0] + S._i32(data, anim[0] + 48)
                tracked = frozenset(b.name for b in bones
                                    if S._f32(data, records + b.index * 32) != 0.0)
            self._tracks[cache_key] = tracked
        got = self._tracks[cache_key]
        return None if got is None else set(got)
