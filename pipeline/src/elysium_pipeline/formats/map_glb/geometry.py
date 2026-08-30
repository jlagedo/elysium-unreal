"""The core glTF a map states: four scenes, their meshes and the BIN payload behind them.

What core glTF can carry, it carries -- world and brush-model geometry, displacement grids, prop
placements and the VPhysics hulls -- and the extension states beside it only what glTF has no
word for. The two never repeat one datum: a face's triangles live in the index accessor and
`faces[]` names the span they occupy, a prop's placement lives in its node's transform and
`staticProps.props[]` names the node.

The transform is the unit contract's: `(x, y, z)_gltf = (x, z, -y)_source * 0.0254` for
positions and the same axis map without the scale for directions. It is a rotation, so triangle
winding carries through unchanged and the source's own vertex order is kept.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import math
import struct
from typing import Any, Iterable, Sequence

from elysium_pipeline.formats.map_glb.model import INCH_TO_METRE

FLOAT = 5126
UNSIGNED_BYTE = 5121
UNSIGNED_INT = 5125
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963


class MapGeometryError(ValueError):
    """The lumps do not describe geometry a glTF primitive can hold."""


def position(x: float, y: float, z: float) -> tuple[float, float, float]:
    """Source inches, Z-up -> glTF metres, Y-up."""

    return (x * INCH_TO_METRE, z * INCH_TO_METRE, -y * INCH_TO_METRE)


def direction(x: float, y: float, z: float) -> tuple[float, float, float]:
    """The same axis map without the scale."""

    return (x, z, -y)


def quaternion_from_angles(pitch: float, yaw: float, roll: float) -> list[float]:
    """A Source `QAngle` as the glTF rotation of the same placement.

    The Source quaternion is built the way `AngleQuaternion` does, then carried across by the
    contract's `(x, y, z, w)_gltf = (x, z, -y, w)_source` and normalized.
    """

    half_yaw = math.radians(yaw) * 0.5
    half_pitch = math.radians(pitch) * 0.5
    half_roll = math.radians(roll) * 0.5
    sin_yaw, cos_yaw = math.sin(half_yaw), math.cos(half_yaw)
    sin_pitch, cos_pitch = math.sin(half_pitch), math.cos(half_pitch)
    sin_roll, cos_roll = math.sin(half_roll), math.cos(half_roll)
    x = sin_roll * cos_pitch * cos_yaw - cos_roll * sin_pitch * sin_yaw
    y = cos_roll * sin_pitch * cos_yaw + sin_roll * cos_pitch * sin_yaw
    z = cos_roll * cos_pitch * sin_yaw - sin_roll * sin_pitch * cos_yaw
    w = cos_roll * cos_pitch * cos_yaw + sin_roll * sin_pitch * sin_yaw
    gltf = [x, z, -y, w]
    length = math.sqrt(sum(value * value for value in gltf))
    if not length or not math.isfinite(length):
        raise MapGeometryError(f"QAngle ({pitch}, {yaw}, {roll}) is not a rotation")
    return [value / length for value in gltf]


@dataclass(slots=True)
class Builder:
    """Accumulates the BIN payload and the core arrays that read through it."""

    payload: bytearray = field(default_factory=bytearray)
    buffer_views: list[dict[str, Any]] = field(default_factory=list)
    accessors: list[dict[str, Any]] = field(default_factory=list)
    meshes: list[dict[str, Any]] = field(default_factory=list)
    nodes: list[dict[str, Any]] = field(default_factory=list)
    scenes: list[dict[str, Any]] = field(default_factory=list)
    materials: list[dict[str, Any]] = field(default_factory=list)

    def _view(self, data: bytes, target: int | None) -> int:
        while len(self.payload) % 4:
            self.payload.append(0)
        view = {"buffer": 0, "byteOffset": len(self.payload), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.payload.extend(data)
        self.buffer_views.append(view)
        return len(self.buffer_views) - 1

    def vectors(self, values: Sequence[Sequence[float]], width: int, name: str) -> int:
        shape = {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4"}[width]
        flat: list[float] = []
        for row in values:
            flat.extend(float(component) for component in row)
        for component in flat:
            if not math.isfinite(component):
                raise MapGeometryError(f"{name} holds a non-finite component")
        view = self._view(struct.pack(f"<{len(flat)}f", *flat), ARRAY_BUFFER)
        accessor = {
            "bufferView": view,
            "componentType": FLOAT,
            "count": len(values),
            "type": shape,
            "name": name,
        }
        if width == 3 and values:
            accessor["min"] = [min(row[axis] for row in values) for axis in range(3)]
            accessor["max"] = [max(row[axis] for row in values) for axis in range(3)]
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def scalars(self, values: Sequence[float], name: str) -> int:
        return self.vectors([[value] for value in values], 1, name)

    def bytes_payload(self, data: bytes, name: str) -> int:
        """One `SCALAR` `UNSIGNED_BYTE` accessor over a payload carried byte for byte."""

        view = self._view(bytes(data), None)
        self.accessors.append(
            {
                "bufferView": view,
                "componentType": UNSIGNED_BYTE,
                "count": len(data),
                "type": "SCALAR",
                "name": name,
            }
        )
        return len(self.accessors) - 1

    def indices(self, values: Sequence[int], name: str) -> int:
        view = self._view(struct.pack(f"<{len(values)}I", *values), ELEMENT_ARRAY_BUFFER)
        self.accessors.append(
            {
                "bufferView": view,
                "componentType": UNSIGNED_INT,
                "count": len(values),
                "type": "SCALAR",
                "name": name,
            }
        )
        return len(self.accessors) - 1

    def mesh(self, name: str, primitives: list[dict[str, Any]]) -> int:
        self.meshes.append({"name": name, "primitives": primitives})
        return len(self.meshes) - 1

    def node(self, node: dict[str, Any]) -> int:
        self.nodes.append(node)
        return len(self.nodes) - 1

    def scene(self, name: str, nodes: Iterable[int]) -> int:
        self.scenes.append({"name": name, "nodes": list(nodes)})
        return len(self.scenes) - 1


@dataclass(slots=True)
class FaceGeometry:
    """One face's contribution to the primitive it was placed in."""

    primitive: int | None
    first_index: int
    index_count: int
    first_vertex: int
    vertex_count: int


def face_vertices(
    face: dict[str, Any], surf_edges: Sequence[int], edges: Sequence[Sequence[int]]
) -> list[int]:
    """The face's vertex indices in the winding the source authored them in."""

    first, count = face["firstEdge"], face["numEdges"]
    if count < 0 or first < 0 or first + count > len(surf_edges):
        raise MapGeometryError(
            f"face {face['index']} names surfedges {first}..{first + count}"
        )
    corners: list[int] = []
    for step in range(count):
        surf_edge = surf_edges[first + step]
        edge_index = surf_edge if surf_edge >= 0 else -surf_edge
        if edge_index >= len(edges):
            raise MapGeometryError(f"face {face['index']} names edge {edge_index}")
        edge = edges[edge_index]
        corners.append(edge[0] if surf_edge >= 0 else edge[1])
    return corners


def winding_area(points: Sequence[Sequence[float]]) -> float:
    """The area of a polygon winding in the source's own square inches (Newell's method).

    A winding of three or more corners can still enclose nothing -- collinear corners, or two
    corners at one point -- which is the other half of the specification's `degenerate-face`.
    """

    if len(points) < 3:
        return 0.0
    x = y = z = 0.0
    for index, current in enumerate(points):
        following = points[(index + 1) % len(points)]
        x += current[1] * following[2] - current[2] * following[1]
        y += current[2] * following[0] - current[0] * following[2]
        z += current[0] * following[1] - current[1] * following[0]
    return 0.5 * math.sqrt(x * x + y * y + z * z)


def texture_coordinates(
    point: Sequence[float], vectors: Sequence[Sequence[float]], width: int, height: int
) -> tuple[float, float]:
    """`TEXCOORD_0`, normalized by the TEXDATA size the compiler authored the vectors against."""

    u = point[0] * vectors[0][0] + point[1] * vectors[0][1] + point[2] * vectors[0][2] + vectors[0][3]
    v = point[0] * vectors[1][0] + point[1] * vectors[1][1] + point[2] * vectors[1][2] + vectors[1][3]
    return (u / width if width else u, v / height if height else v)


def luxel_coordinates(
    point: Sequence[float], vectors: Sequence[Sequence[float]], mins: Sequence[int]
) -> tuple[float, float]:
    """`TEXCOORD_1`, in luxels. The contract keeps this domain in the source's own units."""

    u = point[0] * vectors[0][0] + point[1] * vectors[0][1] + point[2] * vectors[0][2] + vectors[0][3]
    v = point[0] * vectors[1][0] + point[1] * vectors[1][1] + point[2] * vectors[1][2] + vectors[1][3]
    return (u - mins[0], v - mins[1])


def displacement_grid(
    corners: Sequence[Sequence[float]],
    start_position: Sequence[float],
    power: int,
    vertices: Sequence[dict[str, Any]],
) -> tuple[list[tuple[float, float, float]], list[float], list[int]]:
    """One `dispinfo`'s vertex grid, its per-vertex alpha and its triangle list.

    The grid is spanned from the face's four corners, rotated so the corner nearest the record's
    `startPosition` is the origin, and each grid vertex is displaced by its `vector * dist`.
    """

    if len(corners) != 4:
        raise MapGeometryError(f"a displacement face has {len(corners)} corners, not 4")
    distances = [
        sum((corner[axis] - start_position[axis]) ** 2 for axis in range(3))
        for corner in corners
    ]
    origin = distances.index(min(distances))
    ordered = [corners[(origin + step) % 4] for step in range(4)]
    side = (1 << power) + 1
    if len(vertices) != side * side:
        raise MapGeometryError(
            f"a power-{power} displacement needs {side * side} verts, got {len(vertices)}"
        )
    points: list[tuple[float, float, float]] = []
    alphas: list[float] = []
    for row in range(side):
        row_fraction = row / (side - 1)
        left = [
            ordered[0][axis] + (ordered[1][axis] - ordered[0][axis]) * row_fraction
            for axis in range(3)
        ]
        right = [
            ordered[3][axis] + (ordered[2][axis] - ordered[3][axis]) * row_fraction
            for axis in range(3)
        ]
        for column in range(side):
            column_fraction = column / (side - 1)
            vertex = vertices[row * side + column]
            offset = vertex["vector"]
            distance = vertex["dist"]
            source = [
                left[axis] + (right[axis] - left[axis]) * column_fraction
                + offset[axis] * distance
                for axis in range(3)
            ]
            points.append(position(*source))
            alphas.append(float(vertex["alpha"]))
    triangles: list[int] = []
    for row in range(side - 1):
        for column in range(side - 1):
            base = row * side + column
            if (row + column) % 2:
                triangles.extend([base, base + side, base + side + 1])
                triangles.extend([base, base + side + 1, base + 1])
            else:
                triangles.extend([base, base + side, base + 1])
                triangles.extend([base + 1, base + side, base + side + 1])
    return points, alphas, triangles


def smooth_normals(
    points: Sequence[Sequence[float]], triangles: Sequence[int]
) -> list[tuple[float, float, float]]:
    """Area-weighted vertex normals of a triangle list, for a grid the source stores no normals for."""

    sums = [[0.0, 0.0, 0.0] for _ in points]
    for step in range(0, len(triangles), 3):
        a, b, c = triangles[step], triangles[step + 1], triangles[step + 2]
        first = [points[b][axis] - points[a][axis] for axis in range(3)]
        second = [points[c][axis] - points[a][axis] for axis in range(3)]
        cross = [
            first[1] * second[2] - first[2] * second[1],
            first[2] * second[0] - first[0] * second[2],
            first[0] * second[1] - first[1] * second[0],
        ]
        for corner in (a, b, c):
            for axis in range(3):
                sums[corner][axis] += cross[axis]
    normals: list[tuple[float, float, float]] = []
    for total in sums:
        length = math.sqrt(sum(value * value for value in total))
        if length:
            normals.append(tuple(value / length for value in total))
        else:
            normals.append((0.0, 1.0, 0.0))
    return normals
