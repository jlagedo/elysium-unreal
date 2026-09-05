"""Topology adaptation for a dynamic-mesh transport, preserving source records."""


def needs_split(triangles):
    edges, faces = {}, set()
    for a, b, c in triangles:
        if a == b or b == c or a == c:
            return True
        face = tuple(sorted((a, b, c)))
        if face in faces:
            return True
        faces.add(face)
        for u, v in ((a, b), (b, c), (c, a)):
            edge = tuple(sorted((u, v)))
            edges[edge] = edges.get(edge, 0) + 1
            if edges[edge] > 2:
                return True
    return False


def collision_proxy(vertices, triangles):
    """A connected input mesh for one convex hull, using the complete original point set.

    Duplicate/non-manifold PHY triangle records cannot enter DynamicMesh. Its convex
    generator consumes point positions, so an index fan connects every original point
    without changing geometry or splitting one ledge into multiple collision shapes.
    The original triangle records stay untouched in the source/provenance projection.
    """
    points, faces = list(vertices), list(triangles)
    for face in faces:
        if len(face) != 3 or any(type(i) is not int or i < 0 or i >= len(points) for i in face):
            raise ValueError("collision source triangle has an invalid vertex index")
    if not needs_split(faces):
        return points, faces
    if len(points) < 3:
        raise ValueError("collision hull has fewer than three source points")
    return points, [(0, i, i + 1) for i in range(1, len(points) - 1)]
