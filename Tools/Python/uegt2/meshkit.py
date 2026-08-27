"""Low-poly mesh construction for Fairhaven.

Geometry is authored as explicit flat-shaded vertex/triangle buffers and handed
to GeometryScript in one call per asset. Every face carries its own vertices and
face normal, which is what gives the chunky faceted look, and its own vertex
colour, which is how a single master material paints the whole world.

Wind weight (0 at anchored parts like trunks and ground, rising toward 1 at
leaf tips) is stored in BOTH vertex alpha and UV channel 1. The material reads
UV1.x: every VertexColor output pin in Unreal is named "", so only the float3
RGB pin can be reached by name and the alpha is not addressable from script.
See materials.build_foliage_material.

All maths is plain Python tuples; conversion to Unreal types happens once in
``build_dynamic_mesh``.
"""
from __future__ import annotations

import math

import unreal

from . import ctx
from . import palette as pal

Vec = tuple


# ---------------------------------------------------------------------------
# Small vector helpers
# ---------------------------------------------------------------------------
def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def length(a):
    return math.sqrt(max(dot(a, a), 0.0))


def normalize(a):
    n = length(a)
    if n < 1e-9:
        return (0.0, 0.0, 1.0)
    return (a[0] / n, a[1] / n, a[2] / n)


def rotate_z(point, degrees):
    r = math.radians(degrees)
    c, s = math.cos(r), math.sin(r)
    return (point[0] * c - point[1] * s, point[0] * s + point[1] * c, point[2])


def rotate_y(point, degrees):
    r = math.radians(degrees)
    c, s = math.cos(r), math.sin(r)
    return (point[0] * c + point[2] * s, point[1], -point[0] * s + point[2] * c)


def rotate_x(point, degrees):
    r = math.radians(degrees)
    c, s = math.cos(r), math.sin(r)
    return (point[0], point[1] * c - point[2] * s, point[1] * s + point[2] * c)


# ---------------------------------------------------------------------------
# Builder
# ---------------------------------------------------------------------------
class MeshBuilder(object):
    """Accumulates flat-shaded faces, then emits one dynamic mesh."""

    def __init__(self):
        self.vertices = []
        self.normals = []
        self.colors = []
        self.triangles = []

    # -- low level ----------------------------------------------------------
    def _push(self, position, normal, colour_rgba):
        self.vertices.append(position)
        self.normals.append(normal)
        self.colors.append(colour_rgba)
        return len(self.vertices) - 1

    def _emit(self, i0, i1, i2):
        """Append a triangle in Unreal's winding order.

        Geometry here is authored with the right-handed convention: for
        (p0, p1, p2) the outward normal is cross(p1-p0, p2-p0). Unreal takes the
        opposite winding, so the indices are swapped on the way out. Getting
        this wrong makes two-sided foliage render pure black, because Unreal
        flips the shading normal on what it considers back faces.
        """
        self.triangles.append((i0, i2, i1))

    def add_triangle(self, p0, p1, p2, colour, wind=0.0):
        normal = normalize(cross(sub(p1, p0), sub(p2, p0)))
        rgba = _rgba(colour, wind)
        i0 = self._push(p0, normal, rgba)
        i1 = self._push(p1, normal, rgba)
        i2 = self._push(p2, normal, rgba)
        self._emit(i0, i1, i2)

    def add_quad(self, p0, p1, p2, p3, colour, wind=0.0):
        """Counter-clockwise quad, split into two triangles sharing a normal."""
        normal = normalize(cross(sub(p1, p0), sub(p3, p0)))
        rgba = _rgba(colour, wind)
        i0 = self._push(p0, normal, rgba)
        i1 = self._push(p1, normal, rgba)
        i2 = self._push(p2, normal, rgba)
        i3 = self._push(p3, normal, rgba)
        self._emit(i0, i1, i2)
        self._emit(i0, i2, i3)

    # -- primitives ---------------------------------------------------------
    def box(self, center, size, colour, wind=0.0, yaw=0.0, pitch=0.0, roll=0.0):
        """Axis-aligned box, optionally rotated about its own centre."""
        hx, hy, hz = size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
        corners = [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
                   (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]

        def place(point):
            p = point
            if roll:
                p = rotate_x(p, roll)
            if pitch:
                p = rotate_y(p, pitch)
            if yaw:
                p = rotate_z(p, yaw)
            return add(p, center)

        c = [place(p) for p in corners]
        self.add_quad(c[4], c[5], c[6], c[7], colour, wind)   # top
        self.add_quad(c[3], c[2], c[1], c[0], colour, wind)   # bottom
        self.add_quad(c[0], c[1], c[5], c[4], colour, wind)   # -Y
        self.add_quad(c[2], c[3], c[7], c[6], colour, wind)   # +Y
        self.add_quad(c[1], c[2], c[6], c[5], colour, wind)   # +X
        self.add_quad(c[3], c[0], c[4], c[7], colour, wind)   # -X

    def frustum(self, center, bottom_size, top_size, height, colour, wind=0.0,
                yaw=0.0, top_offset=(0.0, 0.0)):
        """Tapered box: the workhorse for trunks, chimneys, hulls and towers."""
        bx, by = bottom_size[0] * 0.5, bottom_size[1] * 0.5
        tx, ty = top_size[0] * 0.5, top_size[1] * 0.5
        ox, oy = top_offset

        bottom = [(-bx, -by, 0.0), (bx, -by, 0.0), (bx, by, 0.0), (-bx, by, 0.0)]
        top = [(-tx + ox, -ty + oy, height), (tx + ox, -ty + oy, height),
               (tx + ox, ty + oy, height), (-tx + ox, ty + oy, height)]

        def place(point):
            return add(rotate_z(point, yaw) if yaw else point, center)

        b = [place(p) for p in bottom]
        t = [place(p) for p in top]

        self.add_quad(t[0], t[1], t[2], t[3], colour, wind)
        self.add_quad(b[3], b[2], b[1], b[0], colour, wind)
        for i in range(4):
            j = (i + 1) % 4
            self.add_quad(b[i], b[j], t[j], t[i], colour, wind)

    def prism(self, center, size, colour, wind=0.0, yaw=0.0):
        """Gable roof: a triangular prism ridged along local X."""
        hx, hy, hz = size[0] * 0.5, size[1] * 0.5, size[2]
        pts = [(-hx, -hy, 0.0), (hx, -hy, 0.0), (hx, hy, 0.0), (-hx, hy, 0.0),
               (-hx, 0.0, hz), (hx, 0.0, hz)]

        def place(point):
            return add(rotate_z(point, yaw) if yaw else point, center)

        p = [place(v) for v in pts]
        self.add_quad(p[0], p[1], p[5], p[4], colour, wind)   # -Y slope
        self.add_quad(p[2], p[3], p[4], p[5], colour, wind)   # +Y slope
        self.add_quad(p[3], p[2], p[1], p[0], colour, wind)   # underside
        self.add_triangle(p[1], p[2], p[5], colour, wind)     # +X gable
        self.add_triangle(p[3], p[0], p[4], colour, wind)     # -X gable

    def cylinder(self, center, radius, height, colour, wind=0.0, sides=8,
                 top_radius=None, yaw=0.0):
        """N-gon cylinder. Low side counts are the point: 6-8 reads as stylised."""
        top_radius = radius if top_radius is None else top_radius
        bottom, top = [], []
        for i in range(sides):
            a = math.radians(360.0 * i / sides + yaw)
            bottom.append(add(center, (math.cos(a) * radius, math.sin(a) * radius, 0.0)))
            top.append(add(center, (math.cos(a) * top_radius, math.sin(a) * top_radius, height)))

        centre_top = add(center, (0.0, 0.0, height))
        for i in range(sides):
            j = (i + 1) % sides
            self.add_quad(bottom[i], bottom[j], top[j], top[i], colour, wind)
            self.add_triangle(top[i], top[j], centre_top, colour, wind)
            self.add_triangle(bottom[j], bottom[i], center, colour, wind)

    def cone(self, center, radius, height, colour, wind=0.0, sides=8, yaw=0.0):
        ring = []
        for i in range(sides):
            a = math.radians(360.0 * i / sides + yaw)
            ring.append(add(center, (math.cos(a) * radius, math.sin(a) * radius, 0.0)))
        apex = add(center, (0.0, 0.0, height))
        for i in range(sides):
            j = (i + 1) % sides
            self.add_triangle(ring[i], ring[j], apex, colour, wind)
            self.add_triangle(ring[j], ring[i], center, colour, wind)

    def icosphere(self, center, radius, colour, wind=0.0, subdivisions=0,
                  squash=1.0, jitter=0.0, seed=1):
        """Faceted blob. Used for tree canopies, bushes and boulders."""
        t = (1.0 + math.sqrt(5.0)) / 2.0
        base = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0),
                (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
                (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
        faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
                 (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
                 (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
                 (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
        points = [normalize(p) for p in base]

        for _ in range(max(subdivisions, 0)):
            new_faces = []
            cache = {}

            def midpoint(i, j):
                key = (min(i, j), max(i, j))
                if key not in cache:
                    points.append(normalize(mul(add(points[i], points[j]), 0.5)))
                    cache[key] = len(points) - 1
                return cache[key]

            for (a, b, c) in faces:
                ab, bc, ca = midpoint(a, b), midpoint(b, c), midpoint(c, a)
                new_faces += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
            faces = new_faces

        rng = _SmallRng(seed)
        scales = [1.0 + (rng.uniform(-jitter, jitter) if jitter > 0.0 else 0.0)
                  for _ in points]
        placed = [add(center, (p[0] * radius * scales[i],
                               p[1] * radius * scales[i],
                               p[2] * radius * squash * scales[i]))
                  for i, p in enumerate(points)]

        for (a, b, c) in faces:
            self.add_triangle(placed[a], placed[b], placed[c], colour, wind)

    def plane_cross(self, center, width, height, colour, wind_top=1.0, yaw=0.0):
        """Two crossed quads: the cheap way to make grass and small fronds."""
        hw = width * 0.5
        for angle in (yaw, yaw + 90.0):
            p0 = add(center, rotate_z((-hw, 0.0, 0.0), angle))
            p1 = add(center, rotate_z((hw, 0.0, 0.0), angle))
            p2 = add(center, rotate_z((hw, 0.0, height), angle))
            p3 = add(center, rotate_z((-hw, 0.0, height), angle))
            normal = normalize(cross(sub(p1, p0), sub(p3, p0)))
            rgba_bottom = _rgba(colour, 0.0)
            rgba_top = _rgba(colour, wind_top)
            i0 = self._push(p0, normal, rgba_bottom)
            i1 = self._push(p1, normal, rgba_bottom)
            i2 = self._push(p2, normal, rgba_top)
            i3 = self._push(p3, normal, rgba_top)
            self._emit(i0, i1, i2)
            self._emit(i0, i2, i3)

    def blade(self, center, width, height, colour, wind_top=1.0, yaw=0.0,
              lean=0.0, taper=0.35):
        """A single tapered grass blade: wide at the base, pointed at the tip.

        Rectangles read as slabs at this scale; a taper is what makes a quad
        look like grass without a texture. Wind weight ramps 0 -> wind_top up
        the blade, so the tip moves and the root does not.
        """
        half = width * 0.5
        mid_h = height * 0.55
        base_l = add(center, rotate_z((-half, 0.0, 0.0), yaw))
        base_r = add(center, rotate_z((half, 0.0, 0.0), yaw))
        mid_l = add(center, rotate_z((-half * taper + lean * 0.5, 0.0, mid_h), yaw))
        mid_r = add(center, rotate_z((half * taper + lean * 0.5, 0.0, mid_h), yaw))
        tip = add(center, rotate_z((lean, 0.0, height), yaw))

        normal = normalize(cross(sub(base_r, base_l), sub(tip, base_l)))
        c_base = _rgba(colour, 0.0)
        c_mid = _rgba(colour, wind_top * 0.55)
        c_tip = _rgba(colour, wind_top)

        i_bl = self._push(base_l, normal, c_base)
        i_br = self._push(base_r, normal, c_base)
        i_ml = self._push(mid_l, normal, c_mid)
        i_mr = self._push(mid_r, normal, c_mid)
        i_tip = self._push(tip, normal, c_tip)

        self._emit(i_bl, i_br, i_mr)
        self._emit(i_bl, i_mr, i_ml)
        self._emit(i_ml, i_mr, i_tip)

    # -- stats --------------------------------------------------------------
    @property
    def triangle_count(self):
        return len(self.triangles)


class _SmallRng(object):
    """Tiny deterministic RNG so mesh jitter is reproducible."""

    def __init__(self, seed):
        self.state = (seed * 2654435761 + 1) & 0xFFFFFFFF

    def next(self):
        self.state = (self.state * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.state / 4294967296.0

    def uniform(self, lo, hi):
        return lo + (hi - lo) * self.next()


def _rgba(colour, wind):
    """Accept a 0xRRGGBB int or an (r,g,b) float tuple; alpha carries wind."""
    if isinstance(colour, int):
        r = ((colour >> 16) & 0xFF) / 255.0
        g = ((colour >> 8) & 0xFF) / 255.0
        b = (colour & 0xFF) / 255.0
        r, g, b = pal._srgb_to_linear(r), pal._srgb_to_linear(g), pal._srgb_to_linear(b)
    else:
        r, g, b = colour[0], colour[1], colour[2]
    return (r, g, b, max(0.0, min(1.0, wind)))


# ---------------------------------------------------------------------------
# Asset emission
# ---------------------------------------------------------------------------
def build_dynamic_mesh(builder):
    """Convert a MeshBuilder into a UDynamicMesh."""
    mesh = unreal.new_object(unreal.DynamicMesh)
    buffers = unreal.GeometryScriptSimpleMeshBuffers()
    buffers.set_editor_property("vertices", [unreal.Vector(v[0], v[1], v[2]) for v in builder.vertices])
    buffers.set_editor_property("normals", [unreal.Vector(n[0], n[1], n[2]) for n in builder.normals])
    buffers.set_editor_property("vertex_colors",
                                [unreal.LinearColor(c[0], c[1], c[2], c[3]) for c in builder.colors])
    buffers.set_editor_property("triangles",
                                [unreal.IntVector(t[0], t[1], t[2]) for t in builder.triangles])
    # UV0 exists only to keep the static mesh build happy; the materials are
    # colour-driven and never sample a texture.
    buffers.set_editor_property("uv0", [unreal.Vector2D(v[0] * 0.01, v[1] * 0.01)
                                        for v in builder.vertices])
    # UV1.x carries the wind weight, which is how the foliage material reaches it.
    buffers.set_editor_property("uv1", [unreal.Vector2D(c[3], 0.0)
                                        for c in builder.colors])

    unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(mesh, buffers)
    return mesh


def create_static_mesh(builder, asset_path, material, collision="complex",
                       recompute_normals=False):
    """Emit a MeshBuilder as a StaticMesh asset and assign its material.

    ``collision`` is:
        "complex"  walkable geometry, complex-as-simple
        "simple"   a box primitive; REQUIRED for anything that simulates physics
        "none"     foliage and decoration
    """
    if builder.triangle_count == 0:
        ctx.fail("mesh %s has no triangles" % asset_path)

    mesh = build_dynamic_mesh(builder)

    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    ctx.set_prop(options, "enable_recompute_normals", recompute_normals)
    ctx.set_prop(options, "enable_recompute_tangents", True)
    ctx.set_prop(options, "enable_nanite", False)
    ctx.set_prop(options, "enable_collision", collision != "none")
    if collision == "complex":
        # Generated meshes have no authored simple collision, so let the player
        # collide with the triangles directly. These meshes are tiny.
        ctx.set_prop(options, "collision_mode",
                     unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    elif collision == "simple":
        ctx.set_prop(options, "collision_mode", unreal.CollisionTraceFlag.CTF_USE_DEFAULT)

    if ctx.asset_exists(asset_path):
        unreal.EditorAssetLibrary.delete_asset(asset_path)

    result = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(
        mesh, asset_path, options)
    static_mesh = ctx.unwrap(result)
    if static_mesh is None:
        ctx.fail("could not create static mesh %s" % asset_path)

    if material is not None:
        try:
            static_mesh.set_material(0, material)
        except Exception:                                       # noqa: BLE001
            ctx.set_prop(static_mesh, "static_materials",
                         [unreal.StaticMaterial(material_interface=material)])

    unreal.UEGT2AuthoringLibrary.configure_generated_mesh(static_mesh, False, 1.0)
    if collision == "simple":
        unreal.UEGT2AuthoringLibrary.add_simple_box_collision(static_mesh)
    ctx.save_asset(asset_path)
    return static_mesh
