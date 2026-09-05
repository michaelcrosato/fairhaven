"""Pure, terrain-fitted geometry for Fairhaven's lower river crossing.

The town stage owns this world-derived mesh. The catalog's small bridge remains
an independent exemplar. Water elevations come from the actual river triangles
passed by the caller, so changing water authoring cannot silently submerge it.
"""
from dataclasses import dataclass
import math

from .meshkit import MeshBuilder
from . import palette as pal


MESH_NAME = "SM_Bridge_LowerRiver"
ACTOR_TAG = "UEGT2.Crossing.LowerRiver"
SOCKET_NAMES = ("ApproachA", "DeckA", "DeckB", "ApproachB")
DECK_THICKNESS = 34.0
WATER_CLEARANCE = 50.0


@dataclass
class BridgeProfile:
    center: tuple
    axis: tuple
    width: float
    wet_start: float
    wet_end: float
    max_water: float
    deck_z: float
    # Each row is (along, lateral centre, absolute heights across the lanes).
    rows: list
    lanes: list
    supports: list
    thickness: float = DECK_THICKNESS
    clearance: float = WATER_CLEARANCE

    def xy(self, along, across):
        return (self.center[0] + self.axis[0]*along - self.axis[1]*across,
                self.center[1] + self.axis[1]*along + self.axis[0]*across)

    def sockets(self):
        middle = len(self.lanes)//2
        return {name: (u, v, heights[middle]-self.deck_z)
                for name, (u, v, heights) in zip(SOCKET_NAMES, self.rows)}

    @property
    def yaw(self):
        return math.degrees(math.atan2(self.axis[1], self.axis[0]))


def _cross(a, b):
    return a[0]*b[1] - a[1]*b[0]


def _crossing(world):
    road = next((r for r in world.roads if r["name"] == "MountainRoad"), None)
    if not road:
        raise ValueError("lower bridge: MountainRoad missing")
    found = []
    for a, b in zip(road["points"], road["points"][1:]):
        d = b[0]-a[0], b[1]-a[1]
        for c, e in zip(world.river["points"], world.river["points"][1:]):
            s = e[0]-c[0], e[1]-c[1]
            denominator = _cross(d, s)
            if abs(denominator) < 1e-9:
                continue
            delta = c[0]-a[0], c[1]-a[1]
            t, u = _cross(delta, s)/denominator, _cross(delta, d)/denominator
            if 0 <= t <= 1 and 0 <= u <= 1:
                point = a[0]+t*d[0], a[1]+t*d[1]
                length = math.hypot(*d)
                found.append((point, (d[0]/length, d[1]/length)))
    if not found:
        raise ValueError("lower bridge: MountainRoad does not cross the river")
    return min(found, key=lambda value: (math.dist(value[0], world.town["center"]), value))


def _clip(poly, axis, boundary, greater):
    result = []
    for a, b in zip(poly, poly[1:]+poly[:1]):
        inside_a = a[axis] >= boundary if greater else a[axis] <= boundary
        inside_b = b[axis] >= boundary if greater else b[axis] <= boundary
        if inside_a:
            result.append(a)
        if inside_a != inside_b:
            t = (boundary-a[axis])/(b[axis]-a[axis])
            result.append(tuple(a[j]+t*(b[j]-a[j]) for j in range(3)))
    return result


def _water_polygons(mesh, center, axis):
    polygons = []
    if mesh is not None:
        for triangle in mesh.triangles:
            poly = []
            for index in triangle:
                x, y, z = mesh.vertices[index]
                dx, dy = x-center[0], y-center[1]
                poly.append((dx*axis[0]+dy*axis[1], -dx*axis[1]+dy*axis[0], z))
            if not all(math.isfinite(value) for point in poly for value in point):
                raise ValueError("lower bridge: non-finite water geometry")
            polygons.append(poly)
    return polygons


def wet_span(polygons, width):
    """Exact full-width footprint component containing the selected crossing."""
    intervals = []
    for poly in polygons:
        clipped = _clip(_clip(poly, 1, -width/2, True), 1, width/2, False)
        if len(clipped) >= 3:
            intervals.append((min(p[0] for p in clipped), max(p[0] for p in clipped),
                              max(p[2] for p in clipped)))
    groups = []
    for lo, hi, height in sorted(intervals):
        if groups and lo <= groups[-1][1]+1e-5:
            groups[-1][1] = max(groups[-1][1], hi)
            groups[-1][2] = max(groups[-1][2], height)
        else:
            groups.append([lo, hi, height])
    local = [group for group in groups if group[0] <= 0 <= group[1]]
    if len(local) != 1:
        raise ValueError("lower bridge: no unique wet footprint at the crossing")
    return local[0]


def _dry(polygons, width, a, b):
    for poly in polygons:
        # Shear into the ramp's corridor before clipping, including its turn.
        shape = [(u, v-a[1]-(b[1]-a[1])*(u-a[0])/(b[0]-a[0]), z)
                 for u, v, z in poly]
        for axis, boundary, greater in ((0, a[0], True), (0, b[0], False),
                                        (1, -width/2, True), (1, width/2, False)):
            shape = _clip(shape, axis, boundary, greater)
        if len(shape) >= 3:
            return False
    return True


def _road_offset(world, profile, along):
    points = []
    road = next(r for r in world.roads if r["name"] == "MountainRoad")
    for x, y, *_ in road["points"]:
        dx, dy = x-profile.center[0], y-profile.center[1]
        points.append((dx*profile.axis[0]+dy*profile.axis[1],
                       -dx*profile.axis[1]+dy*profile.axis[0]))
    found = []
    for a, b in zip(points, points[1:]):
        if min(a[0], b[0]) <= along <= max(a[0], b[0]) and abs(b[0]-a[0]) > 1e-6:
            found.append(a[1]+(along-a[0])/(b[0]-a[0])*(b[1]-a[1]))
    if not found or max(found)-min(found) > 1e-4:
        raise ValueError("lower bridge: road doubles back or ends at the approach")
    return found[0]


def floor_height(a, b, lanes, along, across):
    """Height on the actual two floor triangles; across is relative to the row."""
    s = (along-a[0])/(b[0]-a[0])
    for i, (left, right) in enumerate(zip(lanes, lanes[1:])):
        if across <= right+1e-7:
            t = min(1.0, max(0.0, (across-left)/(right-left)))
            za, zb = a[2], b[2]
            if t <= s:
                return (1-s)*za[i] + (s-t)*zb[i] + t*zb[i+1]
            return (1-t)*za[i] + s*zb[i+1] + (t-s)*za[i+1]
    raise ValueError("lower bridge: floor query outside its width")


def _ramp_valid(profile, world, a, b):
    for i, (left, right) in enumerate(zip(profile.lanes, profile.lanes[1:])):
        # Each triangle's plane has its own along/cross gradient. The shear at
        # a road bend contributes to the along gradient as well.
        for heights, corner in ((a[2], i+1), (b[2], i)):
            cross_grade = (heights[i+1]-heights[i])/(right-left)
            along_grade = (b[2][corner]-a[2][corner]-cross_grade*(b[1]-a[1]))/(b[0]-a[0])
            if not math.isfinite(along_grade) or math.hypot(cross_grade, along_grade) > 0.25:
                return False
    count = max(1, math.ceil((b[0]-a[0])/50.0))
    for j in range(count+1):
        t = j/count
        u, offset = a[0]+t*(b[0]-a[0]), a[1]+t*(b[1]-a[1])
        for i in range(33):
            v = profile.width*i/32-profile.width/2
            ground = world.height_uu(*profile.xy(u, v+offset))
            if not math.isfinite(ground) or floor_height(a, b, profile.lanes, u, v) < ground+1.0:
                return False
    return True


def make_profile(world, river_mesh, width=520.0):
    if not math.isfinite(width) or not 100.0 < width <= 2000.0:
        raise ValueError("lower bridge: invalid width")
    center, axis = _crossing(world)
    polygons = _water_polygons(river_mesh, center, axis)
    lo, hi, max_water = wet_span(polygons, width)
    if hi-lo > 12000.0:
        raise ValueError("lower bridge: wet span exceeds the bounded 120m crossing")
    lanes = [width*i/8-width/2 for i in range(9)]
    profile = BridgeProfile(center, axis, width, lo, hi, max_water,
                            max_water+DECK_THICKNESS+WATER_CLEARANCE, [], lanes, [])
    deck_a = (lo-200.0, 0.0, [profile.deck_z]*len(lanes))
    deck_b = (hi+200.0, 0.0, [profile.deck_z]*len(lanes))
    ends = []
    for side, edge in ((-1, deck_a), (1, deck_b)):
        for step in range(4, 161):  # 2–80m, deterministic and strictly bounded.
            along = edge[0]+side*step*50.0
            offset = _road_offset(world, profile, along)
            heights = [world.height_uu(*profile.xy(along, v+offset))+3.0 for v in lanes]
            end = (along, offset, heights)
            a, b = sorted((edge, end))
            extra = (along+side*300.0, offset*(along+side*300.0-edge[0])/(along-edge[0]))
            dry_a, dry_b = sorted(((edge[0], edge[1]), extra))
            if _dry(polygons, width, dry_a, dry_b) and _ramp_valid(profile, world, a, b):
                ends.append(end)
                break
        else:
            raise ValueError("lower bridge: no dry terrain-connected ramp within 80m")
    profile.rows = [ends[0], deck_a, deck_b, ends[1]]
    if not _ramp_valid(profile, world, deck_a, deck_b):
        raise ValueError("lower bridge: terrain intrudes into the deck")
    for a, b in zip(profile.rows, profile.rows[1:]):
        count = max(1, math.ceil(math.hypot(b[0]-a[0], b[1]-a[1])/750.0))
        for i in range(1, count+1):
            t = i/(count+1)
            u, offset = a[0]+t*(b[0]-a[0]), a[1]+t*(b[1]-a[1])
            for v in (-width*0.39, width*0.39):
                top = floor_height(a, b, lanes, u, v)-profile.thickness
                bottom = min(world.height_uu(*profile.xy(u+dx, v+offset+dy))
                             for dx in (-35, 0, 35) for dy in (-35, 0, 35))-30.0
                if top > bottom+10.0:
                    profile.supports.append((u, v+offset, bottom, top))
    return profile


def make_mesh(profile):
    """One closed floor, shared ramp seams, outside-width rails and sunk piers."""
    mesh = MeshBuilder()
    def point(u, v, z):
        return u, v, z-profile.deck_z
    def face(a, b, c, d, colour=pal.WOOD_PLANK):
        mesh.add_triangle(a, b, c, colour)
        mesh.add_triangle(a, c, d, colour)
    def rail(a, b, side, height):
        # A sloped rectangular prism with its inner edge on the deck boundary.
        va, vb = a[1]+side*profile.width/2, b[1]+side*profile.width/2
        za, zb = a[2][0 if side < 0 else -1], b[2][0 if side < 0 else -1]
        top = [point(a[0], va, za+height), point(b[0], vb, zb+height),
               point(b[0], vb+side*16, zb+height), point(a[0], va+side*16, za+height)]
        if side < 0:
            top.reverse()
        bottom = [(x, y, z-12) for x, y, z in top]
        face(*top, pal.WOOD_DARK)
        face(*reversed(bottom), pal.WOOD_DARK)
        for i in range(4):
            j = (i+1)%4
            face(top[i], bottom[i], bottom[j], top[j], pal.WOOD_DARK)
    for a, b in zip(profile.rows, profile.rows[1:]):
        for i, (left, right) in enumerate(zip(profile.lanes, profile.lanes[1:])):
            top = [point(a[0], left+a[1], a[2][i]), point(b[0], left+b[1], b[2][i]),
                   point(b[0], right+b[1], b[2][i+1]), point(a[0], right+a[1], a[2][i+1])]
            face(*top)
            face(*reversed([(x, y, z-profile.thickness) for x, y, z in top]))
        for index in (0, len(profile.lanes)-1):
            p = point(a[0], profile.lanes[index]+a[1], a[2][index])
            q = point(b[0], profile.lanes[index]+b[1], b[2][index])
            low_p, low_q = (p[0], p[1], p[2]-profile.thickness), (q[0], q[1], q[2]-profile.thickness)
            face(*((p, low_p, low_q, q) if index == 0 else (q, low_q, low_p, p)))
        for side in (-1, 1):
            rail(a, b, side, 55.0)
            rail(a, b, side, 110.0)
        count = max(1, math.ceil(math.hypot(b[0]-a[0], b[1]-a[1])/300.0))
        # The next segment owns a shared seam post; avoid coincident duplicates.
        for i in range(count+(1 if b is profile.rows[-1] else 0)):
            t = i/count
            u, offset = a[0]+t*(b[0]-a[0]), a[1]+t*(b[1]-a[1])
            for side in (-1, 1):
                z = floor_height(a, b, profile.lanes, u, side*profile.width/2)
                post_offset = profile.width/2+10*(1+abs((b[1]-a[1])/(b[0]-a[0])))
                mesh.box(point(u, offset+side*post_offset, z+50),
                         (20, 20, 120), pal.WOOD_DARK)
    for index in (0, -1):
        u, offset, heights = profile.rows[index]
        for i, (left, right) in enumerate(zip(profile.lanes, profile.lanes[1:])):
            p, q = point(u, left+offset, heights[i]), point(u, right+offset, heights[i+1])
            low_p, low_q = (p[0], p[1], p[2]-profile.thickness), (q[0], q[1], q[2]-profile.thickness)
            face(*((q, low_q, low_p, p) if index == 0 else (p, low_p, low_q, q)))
    for u, v, bottom, top in profile.supports:
        mesh.box(point(u, v, (bottom+top)/2), (70, 70, top-bottom), pal.STONE_DARK)
    return mesh
