"""Check every generated mesh without starting Unreal.

    python Tools/Python/check_meshes.py

The mesh generators in ``uegt2`` are pure maths on tuples - boxes, prisms and
vertex colours - and only ``meshkit.build_dynamic_mesh`` and
``meshkit.create_static_mesh`` actually touch the engine. So the geometry can be
built and inspected by plain Python with the ``unreal`` module stubbed out, which
turns "did I get that box right" from a three minute content build into a two
second run.

What it catches, all of which have cost real time in this project:

- a generator that raises, or produces no triangles at all
- degenerate (zero area) triangles, which render nothing and can upset collision
- vertex, normal and colour buffers falling out of step
- triangle indices pointing past the end of the vertex buffer
- geometry that has silently escaped its own bounding box, which is how a wall
  with a doorway in it goes wrong
- meshes that have grown past their triangle budget

It does NOT catch anything about materials, collision cooking, winding or how a
thing looks. Those need the editor and a screenshot; see AGENTS.md.
"""
from __future__ import annotations

import math
import os
import sys
import types


# ---------------------------------------------------------------------------
# The engine stub
# ---------------------------------------------------------------------------
def _install_unreal_stub():
    """Enough of the `unreal` module for the geometry half of uegt2 to import."""
    stub = types.ModuleType("unreal")

    class LinearColor(object):
        def __init__(self, r, g, b, a=1.0):
            self.r, self.g, self.b, self.a = r, g, b, a

    class _Paths(object):
        @staticmethod
        def convert_relative_path_to_full(path):
            return path

        @staticmethod
        def project_dir():
            return _repo_root()

    stub.LinearColor = LinearColor
    stub.Vector = lambda x=0.0, y=0.0, z=0.0: (x, y, z)
    stub.Rotator = lambda roll=0.0, pitch=0.0, yaw=0.0: (roll, pitch, yaw)
    stub.Paths = _Paths
    stub.log = lambda message: None
    stub.log_warning = lambda message: None
    stub.log_error = lambda message: None
    sys.modules["unreal"] = stub


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------
class Report(object):
    def __init__(self):
        self.failures = 0
        self.checked = 0
        self.triangles = 0

    def fail(self, name, problem):
        self.failures += 1
        print("  FAIL  %-30s %s" % (name, problem))

    def mesh(self, name, mesh, budget=None, expect_within=None):
        """Check one built mesh. ``expect_within`` is (x, y, z) half-extents."""
        self.checked += 1
        problems = []

        tris = mesh.triangle_count
        self.triangles += tris
        if tris == 0:
            problems.append("no triangles")
        if budget is not None and tris > budget:
            problems.append("%d triangles, budget %d" % (tris, budget))

        if not (len(mesh.vertices) == len(mesh.normals) == len(mesh.colors)):
            problems.append("vertex/normal/colour buffers out of step (%d/%d/%d)"
                            % (len(mesh.vertices), len(mesh.normals), len(mesh.colors)))

        limit = len(mesh.vertices)
        if any(max(t) >= limit for t in mesh.triangles):
            problems.append("a triangle indexes past the end of the vertex buffer")

        degenerate = sum(1 for t in mesh.triangles if _is_degenerate(mesh, t))
        if degenerate:
            problems.append("%d degenerate triangles" % degenerate)

        if expect_within and mesh.vertices:
            extents = _half_extents(mesh)
            for axis, got, allowed in zip("xyz", extents, expect_within):
                if allowed is not None and got > allowed + 1.0:
                    problems.append("reaches %.0f on %s, expected within %.0f"
                                    % (got, axis, allowed))

        for problem in problems:
            self.failures += 1
            print("  FAIL  %-30s %s" % (name, problem))
        if not problems:
            print("  ok    %-30s %5d tris" % (name, tris))
        return not problems

    def build(self, name, factory, budget=None, expect_within=None):
        """Build a mesh inside a try, so one broken generator does not stop the run."""
        try:
            mesh = factory()
        except Exception as exc:                                # noqa: BLE001
            self.checked += 1
            self.fail(name, "raised %s: %s" % (type(exc).__name__, exc))
            return False
        return self.mesh(name, mesh, budget, expect_within)


def _is_degenerate(mesh, triangle):
    a, b, c = (mesh.vertices[i] for i in triangle)
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx = uy * vz - uz * vy
    ny = uz * vx - ux * vz
    nz = ux * vy - uy * vx
    return math.sqrt(nx * nx + ny * ny + nz * nz) < 1e-6


def _half_extents(mesh):
    return tuple(max(abs(v[i]) for v in mesh.vertices) for i in range(3))


def _blocking(mesh, box):
    """Triangles whose bounding box overlaps ``box`` = (x0, x1, y0, y1, z0, z1).

    Conservative on purpose: a triangle that merely passes near the volume is
    counted. For checking that a doorway is empty that is exactly what is
    wanted, because complex-as-simple collision will stop the pawn on anything
    that is actually there.
    """
    x0, x1, y0, y1, z0, z1 = box
    hits = 0
    for triangle in mesh.triangles:
        verts = [mesh.vertices[i] for i in triangle]
        if (min(v[0] for v in verts) < x1 and max(v[0] for v in verts) > x0
                and min(v[1] for v in verts) < y1 and max(v[1] for v in verts) > y0
                and min(v[2] for v in verts) < z1 and max(v[2] for v in verts) > z0):
            hits += 1
    return hits


def check_interiors(report, interior, builder_cls):
    """Every fit-out the catalog builds, across more seeds than it ships.

    The one thing an interior must never do is come through the wall of the
    house it is in, and the way that happens is arithmetic drift: a piece of
    furniture gets two centimetres wider and the layout still believes the old
    number. Twelve seeds a plan rather than the two the catalog actually ships,
    because a layout bug usually only shows on some seeds and finding it here
    costs a second instead of a content build.
    """
    from uegt2 import meshbuild

    print("")
    print("interior fit-outs")
    heaviest = (0, "")
    for (name, width, depth, storeys) in meshbuild.INTERIOR_PLANS:
        # Floors and ceilings deliberately run gen_interior.TUCK into the wall
        # so their edge is not coplanar with it, and anything that far in is
        # inside masonry and invisible. Past that is a real escape.
        inner_hx = width * 0.5 - 22.0 + interior.TUCK   # gen_town.WALL_T
        inner_hy = depth * 0.5 - 22.0 + interior.TUCK
        ceiling = 26.0 + 320.0 * storeys       # PLINTH_H + STOREY_H per floor
        worst = -1e9
        problems = []
        for variant in range(12):
            seed = 9001 + variant * 131
            solid, glow = interior.fit_out(width, depth, storeys, seed)
            report.triangles += solid.triangle_count + glow.triangle_count
            if solid.triangle_count > heaviest[0]:
                heaviest = (solid.triangle_count, "%s/%d" % (name, seed))
            if not solid.vertices:
                problems.append("seed %d built nothing" % seed)
                continue
            for vertex in solid.vertices:
                worst = max(worst, abs(vertex[0]) - inner_hx,
                            abs(vertex[1]) - inner_hy)
            # Nothing may reach above the top ceiling either: the roof prism
            # starts there, so a wardrobe through it is visible from the street.
            top = max(v[2] for v in solid.vertices)
            if top > ceiling + 1.0:
                problems.append("seed %d reaches z=%.0f, ceiling %.0f"
                                % (seed, top, ceiling))
            # Walking in the front door has to be possible. check_doorway tests
            # the shell, which knows nothing about the partitions and furniture
            # the fit-out puts on the other side of the opening - and a room
            # partition landing across the doorway is exactly the kind of thing
            # that looks fine in every bounds check and is a sealed house.
            half_d = depth * 0.5 - 22.0
            step_in = _blocking(solid, (-34.0, 34.0,
                                        -half_d - 10.0, -half_d + 130.0,
                                        26.0 + 50.0, 26.0 + 180.0))
            if step_in:
                problems.append("seed %d: %d triangles inside the front door"
                                % (seed, step_in))
        if worst > 1.0:
            problems.append("escapes its shell by %.1f cm" % worst)

        report.checked += 1
        for problem in problems:
            report.failures += 1
            print("  FAIL  %-30s %s" % (name, problem))
        if not problems:
            print("  ok    %-30s 12 seeds, flush to %.1f cm" % (name, -worst))

    # The stair is the one thing in here the player has to be able to use.
    scratch = builder_cls()
    interior.stair_run(scratch, 320.0, seed=5)
    treads = sorted({round(v[2], 1) for v in scratch.vertices if v[2] > 0.1})
    rises = [b - a for a, b in zip([0.0] + treads, treads)]
    worst_rise = max(rises) if rises else 999.0
    report.checked += 1
    if worst_rise > 45.0:
        report.failures += 1
        print("  FAIL  %-30s a %.0f cm riser, the pawn steps 45" % ("stair rise", worst_rise))
    else:
        print("  ok    %-30s worst riser %.0f cm, pawn steps 45" % ("stair rise", worst_rise))
    print("  ..    %-30s heaviest fit-out, %d tris" % (heaviest[1], heaviest[0]))


def check_city_interiors(report, interior, builder_cls):
    """Every Newhaven trade and every outbuilding, against its own shell.

    Same test as the houses and the same reason: the fit-out and the shell agree
    only by arithmetic, and the arithmetic is different for each - a shopfront
    has 34 cm walls and a 3.8 m ceiling, a church has 40 and 5.2. One number
    wrong and a bookcase stands in the street.
    """
    from uegt2 import meshbuild

    print("")
    print("newhaven and outbuilding interiors")
    plans = [(arch, w, d, h, 0.0, 34.0, v, 7100 + i * 97 + len(arch) * 13)
             for (arch, w, d, h, venues) in meshbuild.CITY_INTERIORS
             for i, v in enumerate(venues)]
    plans += [(name, w, d, h, b, t, k, 8300 + len(name) * 29)
              for (name, w, d, h, b, t, k) in meshbuild.EXTRA_INTERIORS]

    worst_all = -1e9
    heaviest = (0, "")
    for (name, width, depth, height, base, wall, kind, seed) in plans:
        inner_hx = width * 0.5 - wall + interior.TUCK
        inner_hy = depth * 0.5 - wall + interior.TUCK
        solid, glow = interior.fit_out(width, depth, 1, seed, base_z=base,
                                       ground_kind=kind, wall_t=wall,
                                       storey_h=height, ceiling=False)
        report.checked += 1
        report.triangles += solid.triangle_count + glow.triangle_count
        if solid.triangle_count > heaviest[0]:
            heaviest = (solid.triangle_count, "%s/%s" % (name, kind))

        problems = []
        if not solid.vertices:
            problems.append("built nothing")
        else:
            worst = max(max(abs(v[0]) - inner_hx, abs(v[1]) - inner_hy)
                        for v in solid.vertices)
            worst_all = max(worst_all, worst)
            if worst > 1.0:
                problems.append("escapes its shell by %.1f cm" % worst)
            top = max(v[2] for v in solid.vertices)
            if top > base + height + 1.0:
                problems.append("reaches z=%.0f, ceiling is %.0f"
                                % (top, base + height))
            # The way in has to stay open, exactly as for a house.
            half_d = depth * 0.5 - wall
            blocked = _blocking(solid, (-34.0, 34.0,
                                        -half_d - 10.0, -half_d + 140.0,
                                        base + 50.0, base + 180.0))
            if blocked:
                problems.append("%d triangles inside the doorway" % blocked)

        for problem in problems:
            report.failures += 1
            print("  FAIL  %-30s %s" % (name + "/" + kind, problem))
        if not problems:
            print("  ok    %-30s %5d tris" % (name + "/" + kind,
                                              solid.triangle_count))
    print("  ..    %-30s heaviest, %d tris" % (heaviest[1], heaviest[0]))


def check_doorway(report, name, mesh, width, depth, storeys=1):
    """The doorway has to be a hole the pawn fits through.

    This is the whole feature: a house with a sealed door is a house with an
    interior nobody will ever see. The volume tested is the pawn's own
    footprint - 68 across, 180 tall - centred in the opening, with the wall
    thickness either side.
    """
    report.checked += 1
    half_d = depth * 0.5
    # From above step height to the top of the pawn's head. Starting at the
    # floor would flag the porch deck and the stoop, which are things you walk
    # *on*: the pawn steps up 45 cm without noticing.
    pawn = (-34.0, 34.0,
            -half_d - 40.0, -half_d + 40.0,
            26.0 + 50.0, 26.0 + 180.0)
    hits = _blocking(mesh, pawn)
    if hits:
        report.failures += 1
        print("  FAIL  %-30s %d triangles block the doorway" % (name, hits))
        return False
    print("  ok    %-30s doorway clear for the pawn" % name)
    return True


# ---------------------------------------------------------------------------
def main():
    _install_unreal_stub()
    sys.path.insert(0, os.path.join(_repo_root(), "Tools", "Python"))

    from uegt2.meshkit import MeshBuilder
    from uegt2 import gen_city as city
    from uegt2 import gen_fauna as fauna
    from uegt2 import gen_interior as interior
    from uegt2 import gen_nature as nature
    from uegt2 import gen_town as town

    report = Report()

    print("meshkit primitives")
    # A wall with a door and two windows is the shape every hollowed building
    # depends on, so it is checked against its own bounds explicitly.
    def front_wall():
        mesh = MeshBuilder()
        mesh.wall((0.0, 0.0, 0.0), "x", 760.0, 320.0, 22.0, 0xE2D4B7,
                  openings=[(-220.0, 110.0, 110.0, 115.0),
                            (0.0, 120.0, 0.0, 215.0),
                            (220.0, 110.0, 110.0, 115.0)])
        return mesh

    report.build("wall, door + 2 windows", front_wall,
                 expect_within=(380.0, 11.0, 320.0))

    def blank_wall():
        mesh = MeshBuilder()
        mesh.wall((0.0, 0.0, 0.0), "y", 580.0, 320.0, 22.0, 0xE2D4B7)
        return mesh

    report.build("wall, no openings", blank_wall, budget=12,
                 expect_within=(11.0, 290.0, 320.0))

    print("\ninterior fittings")
    for name in ("bed", "bedside", "wardrobe", "chest", "table", "chair", "stool",
                 "settle", "counter", "shelf", "dresser", "desk", "bookshelf",
                 "shop_stand", "hearth", "hearth_fire", "ceiling_lamp",
                 "ceiling_lamp_glow", "rug", "barrel_small", "crate_small", "sack",
                 "pot", "firewood", "washstand"):
        item = getattr(interior, name)
        report.build(name, lambda fn=item: _one(MeshBuilder, fn), budget=260)

    def stairs():
        mesh = MeshBuilder()
        interior.stair_run(mesh, 346.0, seed=3)
        return mesh

    report.build("stair_run", stairs, budget=220)

    def partition():
        mesh = MeshBuilder()
        interior.partition(mesh, 500.0, 320.0, door_at=80.0)
        return mesh

    report.build("partition + doorway", partition, budget=80)

    print("\ntown")
    check_interiors(report, interior, MeshBuilder)
    check_city_interiors(report, interior, MeshBuilder)

    houses = [("house A", 211, 760.0, 580.0, 1, True),
              ("house B", 223, 700.0, 620.0, 2, False),
              ("house C", 227, 900.0, 560.0, 1, False),
              ("house D", 229, 640.0, 640.0, 2, True),
              ("house E", 233, 820.0, 700.0, 1, False),
              ("cottage", 239, 560.0, 460.0, 1, False)]
    for (label, seed, w, d, storeys, porch) in houses:
        built = town.house(seed, w, d, storeys, porch=porch)
        report.mesh(label, built)
        check_doorway(report, label + " doorway", built, w, d, storeys)
    report.build("barn", lambda: town.barn(241))
    report.build("church", lambda: town.church(251))
    report.build("market stall", lambda: town.market_stall(277))
    report.build("villager", lambda: town.person(401, "plain"))
    report.build("child", lambda: town.person(509, "child", 0.62))

    print("\nnewhaven")
    report.build("tower", lambda: city.tower(431, 1800.0, 1600.0, 20))
    report.build("office", lambda: city.office_block(449, 2100.0, 1700.0, 9))
    report.build("apartment", lambda: city.apartment(461, 1700.0, 1500.0, 6))
    report.build("shophouse", lambda: city.shophouse(479, 1050.0, 1250.0, 3))
    report.build("city hall", lambda: city.city_hall(499))

    print("\nnature and fauna")
    report.build("oak", lambda: nature.broadleaf_tree(11, 1050.0))
    report.build("pine", lambda: nature.conifer_tree(53, 1520.0))
    report.build("grass", lambda: nature.grass_clump(127, 42.0))
    report.build("dog", lambda: fauna.dog(601))
    report.build("cow", lambda: fauna.cow(659))
    report.build("seagull", lambda: fauna.seagull(701))

    print("\n%d meshes, %d triangles, %d failures"
          % (report.checked, report.triangles, report.failures))
    if report.failures:
        print("FAILED")
        return 1
    print("All mesh checks passed.")
    return 0


def _one(builder_cls, item):
    mesh = builder_cls()
    item(mesh, seed=7)
    return mesh


if __name__ == "__main__":
    sys.exit(main())
