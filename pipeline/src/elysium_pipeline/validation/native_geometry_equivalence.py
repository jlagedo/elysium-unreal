"""Exact source-vertex equivalence, including the complete sparse morph signature.

No nearest-position search, rounded coordinates, or native quantization tolerance is
used to establish aliases. Original source IDs remain separate inventory identities.
"""
from collections import Counter
import hashlib
import json


def _triangle(section, vertices):
    a, b, c = vertices
    return section, *min((a, b, c), (b, c, a), (c, a, b))


def vertex_signatures(geometry):
    tangents = getattr(geometry, "tangents", None)
    if tangents is None or tangents.shape != (len(geometry.positions), 4):
        raise ValueError("exact source alias proof requires the complete TANG channel")
    morphs = [[] for _ in geometry.positions]
    for name, values in sorted(geometry.morphs.items()):
        for vertex, delta in values.items():
            # Keep explicit sparse membership, even for an all-zero record. This is
            # stronger than merely comparing a dense normal/position at one weight.
            morphs[vertex].append((name, *map(float, delta)))
    bases, signatures = [], []
    for i, position in enumerate(geometry.positions):
        skin = Counter()
        for bone, weight in zip(geometry.joints[i], geometry.weights[i]):
            if weight > 0:
                skin[int(bone)] += float(weight)
        base = (tuple(map(float, position)), tuple(map(float, geometry.normals[i])),
                tuple(map(float, geometry.uvs[i])), tuple(sorted(skin.items())), tuple(map(float, tangents[i])))
        bases.append(base)
        signatures.append((*base, tuple(morphs[i])))
    return bases, signatures


def audit_vertex_equivalence(geometry, render):
    """Return exact class representatives plus compact acceptance/diagnostic evidence.

    Native geometry attributes are checked separately by the numeric comparator.
    Here a native source index is only an identity claim; every missing source ID
    needs an exactly equivalent represented source and identical triangle multisets.
    """
    bases, signatures = vertex_signatures(geometry)
    representative, classes = {}, []
    for vertex, signature in enumerate(signatures):
        classes.append(representative.setdefault(signature, vertex))
    claimed = [r["sourceVertex"] for r in render["vertices"]]
    if any(type(v) is not int or not 0 <= v < len(classes) for v in claimed):
        raise ValueError("render source vertex mapping bounds")
    present = set(claimed)
    missing = sorted(set(range(len(classes))) - present)
    represented = {}
    base_candidates, position_candidates = {}, {}
    for vertex in sorted(present):
        represented.setdefault(classes[vertex], vertex)
        base_candidates.setdefault(bases[vertex], []).append(vertex)
        position_candidates.setdefault(bases[vertex][0], []).append(vertex)
    aliases, failures, counts = [], [], Counter()
    for vertex in missing:
        if classes[vertex] in represented:
            aliases.append([vertex, represented[classes[vertex]]])
            continue
        exact_base = base_candidates.get(bases[vertex], [])
        candidates = exact_base or position_candidates.get(bases[vertex][0], [])
        kind = "morph-signature" if exact_base else "base-attributes" if candidates else "no-exact-position"
        counts[kind] += 1
        if len(failures) < 12:
            row = {"sourceVertex": vertex, "reason": kind, "candidateSourceIds": candidates[:4]}
            if candidates:
                other = candidates[0]
                row["differentChannels"] = [name for name, a, b in zip(
                    ("position", "normal", "uv", "skin", "tangentXYZ/sign", "sparseMorphs"), signatures[vertex], signatures[other]) if a != b]
                if exact_base:
                    left, right = dict((r[0], r[1:]) for r in signatures[vertex][-1]), dict((r[0], r[1:]) for r in signatures[other][-1])
                    names = sorted(set(left) | set(right))
                    row["differentMorphs"] = [name for name in names if left.get(name) != right.get(name)][:8]
            failures.append(row)
    expected = Counter(_triangle(t[0], [classes[v] for v in t[1:]]) for t in geometry.triangles)
    actual = Counter()
    indices, used = render["indices"], set()
    for section in render["sections"]:
        base, count = section["baseIndex"], section["numTriangles"]
        if base < 0 or count < 0 or base + 3 * count > len(indices):
            raise ValueError("render equivalence section index bounds")
        for offset in range(base, base + 3 * count, 3):
            if any(i in used for i in range(offset, offset + 3)):
                raise ValueError("render equivalence overlapping section index ranges")
            used.update(range(offset, offset + 3))
            triangle = indices[offset:offset + 3]
            if any(type(v) is not int or not 0 <= v < len(claimed) for v in triangle):
                raise ValueError("render equivalence triangle index bounds")
            actual[_triangle(section["material"], [classes[claimed[v]] for v in triangle])] += 1
    if len(used) != len(indices):
        raise ValueError("render equivalence unsectioned indices")
    lost, extra = sum((expected - actual).values()), sum((actual - expected).values())
    evidence = {"passed": not counts and not lost and not extra,
                "originalSourceVertices": len(classes), "representedSourceIds": len(present),
                "missingSourceIds": len(missing), "exactEquivalenceClasses": len(representative),
                "provenAliasCount": len(aliases), "unprovenSourceIds": sum(counts.values()),
                "unprovenReasons": dict(counts), "missingEquivalentTriangles": lost, "extraEquivalentTriangles": extra,
                "examples": failures, "sourceAliases": aliases,
                "sourceAliasSha256": hashlib.sha256(json.dumps(aliases, separators=(",", ":")).encode()).hexdigest()}
    return classes, evidence
