"""Builds every generated static mesh asset for Fairhaven.

The catalog below is the single list of what exists in the world. Adding an
asset means adding one line here plus a generator function in gen_nature.py or
gen_town.py; the scatter and town placement stages then look it up by name.

Collision policy:
    "complex"  walkable / blocking geometry (buildings, props, trunks, rocks)
    "none"     things the player walks through (grass, ferns, small foliage)
Generated meshes have no authored simple collision, so "complex" means
complex-as-simple. These meshes are tiny, so that is cheap.
"""
from __future__ import annotations

from . import ctx
from . import gen_city as city
from . import gen_fauna as fauna
from . import gen_nature as nat
from . import gen_town as town
from . import meshkit

P_NATURE = ctx.P_MESH + "/Nature"
P_TOWN = ctx.P_MESH + "/Town"
P_PROPS = ctx.P_MESH + "/Props"
P_CITY = ctx.P_MESH + "/City"
P_FAUNA = ctx.P_MESH + "/Fauna"


def _catalog():
    """(name, folder, builder, material_key, collision) for every mesh."""
    return [
        # -- Trees ----------------------------------------------------------
        ("SM_Tree_Oak_A", P_NATURE, lambda: nat.broadleaf_tree(11, 1050.0), "foliage", "complex"),
        ("SM_Tree_Oak_B", P_NATURE, lambda: nat.broadleaf_tree(23, 1280.0, blobs=5, lean=40.0), "foliage", "complex"),
        ("SM_Tree_Oak_C", P_NATURE, lambda: nat.broadleaf_tree(37, 860.0, blobs=3), "foliage", "complex"),
        ("SM_Tree_Autumn_A", P_NATURE, lambda: nat.broadleaf_tree(41, 980.0, leaf=0xC07A2A), "foliage", "complex"),
        ("SM_Tree_Pine_A", P_NATURE, lambda: nat.conifer_tree(53, 1520.0), "foliage", "complex"),
        ("SM_Tree_Pine_B", P_NATURE, lambda: nat.conifer_tree(59, 1900.0), "foliage", "complex"),
        ("SM_Tree_Pine_C", P_NATURE, lambda: nat.conifer_tree(61, 1180.0), "foliage", "complex"),
        ("SM_Tree_Birch_A", P_NATURE, lambda: nat.birch_tree(67, 940.0), "foliage", "complex"),
        ("SM_Tree_Birch_B", P_NATURE, lambda: nat.birch_tree(71, 1120.0), "foliage", "complex"),
        ("SM_Tree_Palm_A", P_NATURE, lambda: nat.palm_tree(73, 1150.0), "foliage", "complex"),
        ("SM_Tree_Palm_B", P_NATURE, lambda: nat.palm_tree(79, 1420.0), "foliage", "complex"),
        ("SM_Tree_Jungle_A", P_NATURE, lambda: nat.jungle_tree(83, 1750.0), "foliage", "complex"),
        ("SM_Tree_Jungle_B", P_NATURE, lambda: nat.jungle_tree(89, 2100.0), "foliage", "complex"),
        ("SM_Tree_Dead_A", P_NATURE, lambda: nat.dead_tree(97, 720.0), "foliage", "complex"),

        # -- Undergrowth ----------------------------------------------------
        ("SM_Bush_A", P_NATURE, lambda: nat.bush(101, 140.0), "foliage", "none"),
        ("SM_Bush_B", P_NATURE, lambda: nat.bush(103, 190.0), "foliage", "none"),
        ("SM_Bush_Jungle_A", P_NATURE, lambda: nat.bush(107, 210.0, leaf=0x2F6B3B), "foliage", "none"),
        ("SM_Fern_A", P_NATURE, lambda: nat.fern(109, 95.0), "foliage", "none"),
        ("SM_Fern_B", P_NATURE, lambda: nat.fern(113, 140.0), "foliage", "none"),
        ("SM_Grass_A", P_NATURE, lambda: nat.grass_clump(127, 42.0), "foliage", "none"),
        ("SM_Grass_B", P_NATURE, lambda: nat.grass_clump(131, 62.0, blades=9), "foliage", "none"),
        ("SM_Reeds_A", P_NATURE, lambda: nat.reeds(137, 160.0), "foliage", "none"),
        ("SM_Crop_Wheat_A", P_NATURE, lambda: nat.crop_row(139, 110.0), "foliage", "none"),
        ("SM_Crop_Green_A", P_NATURE, lambda: nat.crop_row(149, 88.0, colour=0x8FA84A), "foliage", "none"),

        # -- Rock -----------------------------------------------------------
        ("SM_Rock_S", P_NATURE, lambda: nat.rock(151, 70.0), "prop", "complex"),
        ("SM_Rock_M", P_NATURE, lambda: nat.rock(157, 150.0), "prop", "complex"),
        ("SM_Rock_L", P_NATURE, lambda: nat.rock(163, 280.0, squash=0.62), "prop", "complex"),
        ("SM_Boulder_A", P_NATURE, lambda: nat.boulder_cluster(167, 300.0), "prop", "complex"),
        ("SM_Cliff_A", P_NATURE, lambda: nat.cliff_slab(173, 430.0), "prop", "complex"),
        ("SM_Cliff_B", P_NATURE, lambda: nat.cliff_slab(179, 680.0), "prop", "complex"),
        ("SM_Driftwood_A", P_NATURE, lambda: nat.driftwood(181, 190.0), "prop", "complex"),

        # -- Buildings ------------------------------------------------------
        ("SM_House_A", P_TOWN, lambda: town.house(211, 760.0, 580.0, 1, porch=True), "prop", "complex"),
        ("SM_House_B", P_TOWN, lambda: town.house(223, 700.0, 620.0, 2), "prop", "complex"),
        ("SM_House_C", P_TOWN, lambda: town.house(227, 900.0, 560.0, 1), "prop", "complex"),
        ("SM_House_D", P_TOWN, lambda: town.house(229, 640.0, 640.0, 2, porch=True), "prop", "complex"),
        ("SM_House_E", P_TOWN, lambda: town.house(233, 820.0, 700.0, 1), "prop", "complex"),
        ("SM_Cottage_A", P_TOWN, lambda: town.house(239, 560.0, 460.0, 1, chimney=True), "prop", "complex"),
        ("SM_Barn_A", P_TOWN, lambda: town.barn(241), "prop", "complex"),
        ("SM_Church_A", P_TOWN, lambda: town.church(251), "prop", "complex"),
        ("SM_Lighthouse_A", P_TOWN, lambda: town.lighthouse(257), "prop", "complex"),
        ("SM_Lighthouse_Glow", P_TOWN, lambda: town.lighthouse_glow(257), "emissive", "none"),
        ("SM_Windmill_A", P_TOWN, lambda: town.windmill(263), "prop", "complex"),
        ("SM_Warehouse_A", P_TOWN, lambda: town.warehouse(269), "prop", "complex"),
        ("SM_Shed_A", P_TOWN, lambda: town.shed(271), "prop", "complex"),
        ("SM_MarketStall_A", P_TOWN, lambda: town.market_stall(277), "prop", "complex"),
        ("SM_Door_A", P_TOWN, lambda: town.door_leaf(279), "prop", "complex"),

        # -- Props ----------------------------------------------------------
        ("SM_Fence_A", P_PROPS, lambda: town.fence_section(281), "prop", "complex"),
        ("SM_StoneWall_A", P_PROPS, lambda: town.stone_wall_section(283), "prop", "complex"),
        ("SM_LampPost_A", P_PROPS, lambda: town.lamp_post(293), "prop", "complex"),
        ("SM_LampPost_Glow", P_PROPS, lambda: town.lamp_glow(293), "emissive", "none"),
        ("SM_SignPost_A", P_PROPS, lambda: town.sign_post(307), "prop", "complex"),
        ("SM_Bench_A", P_PROPS, lambda: town.bench(311), "prop", "complex"),
        ("SM_Crate_A", P_PROPS, lambda: town.crate(313), "prop", "simple"),
        ("SM_Barrel_A", P_PROPS, lambda: town.barrel(317), "prop", "simple"),
        ("SM_Cart_A", P_PROPS, lambda: town.cart(331), "prop", "complex"),
        ("SM_Well_A", P_PROPS, lambda: town.well(337), "prop", "complex"),
        ("SM_Haybale_A", P_PROPS, lambda: town.haybale(347), "prop", "complex"),
        ("SM_Scarecrow_A", P_PROPS, lambda: town.scarecrow(349), "prop", "complex"),
        ("SM_Planter_A", P_PROPS, lambda: town.planter(353), "prop", "complex"),

        # -- Waterfront -----------------------------------------------------
        ("SM_Dock_A", P_TOWN, lambda: town.dock_section(359), "prop", "complex"),
        ("SM_DockPost_A", P_TOWN, lambda: town.dock_post(367), "prop", "complex"),
        ("SM_Rowboat_A", P_TOWN, lambda: town.rowboat(373), "prop", "complex"),
        ("SM_FishingBoat_A", P_TOWN, lambda: town.fishing_boat(379), "prop", "complex"),
        ("SM_Bridge_A", P_TOWN, lambda: town.bridge_section(383), "prop", "complex"),

        # -- Newhaven -------------------------------------------------------
        # Floor counts are what set the skyline: towers downtown, offices and
        # apartments in the middle ring, shophouses on the edge.
        ("SM_Tower_A", P_CITY, lambda: city.tower(431, 1800.0, 1600.0, 20), "prop", "complex"),
        ("SM_Tower_B", P_CITY, lambda: city.tower(433, 2000.0, 1750.0, 26), "prop", "complex"),
        ("SM_Tower_C", P_CITY, lambda: city.tower(439, 1600.0, 1500.0, 15), "prop", "complex"),
        ("SM_Tower_D", P_CITY, lambda: city.tower(443, 2200.0, 1500.0, 31), "prop", "complex"),
        ("SM_Office_A", P_CITY, lambda: city.office_block(449, 2100.0, 1700.0, 9), "prop", "complex"),
        ("SM_Office_B", P_CITY, lambda: city.office_block(457, 2400.0, 1600.0, 12), "prop", "complex"),
        ("SM_Apartment_A", P_CITY, lambda: city.apartment(461, 1700.0, 1500.0, 6), "prop", "complex"),
        ("SM_Apartment_B", P_CITY, lambda: city.apartment(463, 1900.0, 1400.0, 8), "prop", "complex"),
        ("SM_Apartment_C", P_CITY, lambda: city.apartment(467, 1500.0, 1300.0, 4), "prop", "complex"),
        ("SM_Shophouse_A", P_CITY, lambda: city.shophouse(479, 1050.0, 1250.0, 3), "prop", "complex"),
        ("SM_Shophouse_B", P_CITY, lambda: city.shophouse(487, 950.0, 1150.0, 2), "prop", "complex"),
        ("SM_ParkingDeck_A", P_CITY, lambda: city.parking_deck(491, 2200.0, 1800.0, 4), "prop", "complex"),
        ("SM_CityHall_A", P_CITY, lambda: city.city_hall(499), "prop", "complex"),

        # -- City street furniture ------------------------------------------
        ("SM_TrafficLight_A", P_CITY, lambda: city.traffic_light(503), "prop", "complex"),
        ("SM_CityLamp_A", P_CITY, lambda: city.city_lamp(509), "prop", "complex"),
        ("SM_CityLamp_Glow", P_CITY, lambda: city.city_lamp_glow(509), "emissive", "none"),
        ("SM_Kiosk_A", P_CITY, lambda: city.kiosk(521), "prop", "complex"),
        ("SM_BusShelter_A", P_CITY, lambda: city.bus_shelter(523), "prop", "complex"),
        ("SM_Fountain_A", P_CITY, lambda: city.fountain(541), "prop", "complex"),
        ("SM_PlanterLong_A", P_CITY, lambda: city.planter_long(547), "prop", "complex"),

        # -- Townsfolk ------------------------------------------------------
        # Collision is "complex" for the same reason the props are: the
        # interaction probe traces on Visibility, and a figure with no collision
        # geometry cannot be looked at or talked to. The NPC actor then narrows
        # that to query-only so the player walks through a crowd rather than
        # getting wedged in one.
        ("SM_Villager_A", P_TOWN, lambda: town.person(401, "plain"), "prop", "complex"),
        ("SM_Villager_B", P_TOWN, lambda: town.person(409, "plain"), "prop", "complex"),
        ("SM_Villager_C", P_TOWN, lambda: town.person(419, "plain"), "prop", "complex"),
        ("SM_Villager_D", P_TOWN, lambda: town.person(421, "plain"), "prop", "complex"),
        ("SM_Villager_E", P_TOWN, lambda: town.person(431, "plain"), "prop", "complex"),
        ("SM_Villager_F", P_TOWN, lambda: town.person(433, "hat"), "prop", "complex"),
        ("SM_Villager_G", P_TOWN, lambda: town.person(439, "coat"), "prop", "complex"),
        ("SM_Villager_H", P_TOWN, lambda: town.person(443, "pack"), "prop", "complex"),
        ("SM_Farmer_A", P_TOWN, lambda: town.person(449, "hat"), "prop", "complex"),
        ("SM_Farmer_B", P_TOWN, lambda: town.person(457, "hat"), "prop", "complex"),
        ("SM_Fisher_A", P_TOWN, lambda: town.person(461, "coat"), "prop", "complex"),
        ("SM_Fisher_B", P_TOWN, lambda: town.person(463, "coat"), "prop", "complex"),
        ("SM_Merchant_A", P_TOWN, lambda: town.person(467, "apron"), "prop", "complex"),
        ("SM_Merchant_B", P_TOWN, lambda: town.person(479, "apron"), "prop", "complex"),
        ("SM_Priest_A", P_TOWN, lambda: town.person(487, "robe"), "prop", "complex"),
        ("SM_Dockhand_A", P_TOWN, lambda: town.person(491, "pack"), "prop", "complex"),
        ("SM_Elder_A", P_TOWN, lambda: town.person(499, "coat"), "prop", "complex"),
        ("SM_Elder_B", P_TOWN, lambda: town.person(503, "coat"), "prop", "complex"),
        # Children are the same figure at 62%, which is the whole reason
        # person() takes a scale: a separate short mesh would drift out of step.
        ("SM_Child_A", P_TOWN, lambda: town.person(509, "child", 0.62), "prop", "complex"),
        ("SM_Child_B", P_TOWN, lambda: town.person(521, "child", 0.58), "prop", "complex"),
        ("SM_Child_C", P_TOWN, lambda: town.person(523, "child", 0.66), "prop", "complex"),

        # -- Newhaven citizens ----------------------------------------------
        ("SM_Citizen_A", P_CITY, lambda: town.person(541, "suit"), "prop", "complex"),
        ("SM_Citizen_B", P_CITY, lambda: town.person(547, "suit"), "prop", "complex"),
        ("SM_Citizen_C", P_CITY, lambda: town.person(557, "suit"), "prop", "complex"),
        ("SM_Citizen_D", P_CITY, lambda: town.person(563, "coat"), "prop", "complex"),
        ("SM_Citizen_E", P_CITY, lambda: town.person(569, "plain"), "prop", "complex"),
        ("SM_Officer_A", P_CITY, lambda: town.person(571, "uniform"), "prop", "complex"),
        ("SM_Courier_A", P_CITY, lambda: town.person(577, "hivis"), "prop", "complex"),
        ("SM_Shopkeeper_A", P_CITY, lambda: town.person(587, "apron"), "prop", "complex"),

        # -- Animals ---------------------------------------------------------
        ("SM_Dog_A", P_FAUNA, lambda: fauna.dog(601), "prop", "complex"),
        ("SM_Dog_B", P_FAUNA, lambda: fauna.dog(607), "prop", "complex"),
        ("SM_Cat_A", P_FAUNA, lambda: fauna.cat(613), "prop", "complex"),
        ("SM_Cat_B", P_FAUNA, lambda: fauna.cat(617), "prop", "complex"),
        ("SM_Chicken_A", P_FAUNA, lambda: fauna.chicken(619), "prop", "complex"),
        ("SM_Chicken_B", P_FAUNA, lambda: fauna.chicken(631), "prop", "complex"),
        ("SM_Duck_A", P_FAUNA, lambda: fauna.duck(641), "prop", "complex"),
        ("SM_Duck_B", P_FAUNA, lambda: fauna.duck(643), "prop", "complex"),
        ("SM_Sheep_A", P_FAUNA, lambda: fauna.sheep(647), "prop", "complex"),
        ("SM_Sheep_B", P_FAUNA, lambda: fauna.sheep(653), "prop", "complex"),
        ("SM_Cow_A", P_FAUNA, lambda: fauna.cow(659), "prop", "complex"),
        ("SM_Cow_B", P_FAUNA, lambda: fauna.cow(661), "prop", "complex"),
        ("SM_Pig_A", P_FAUNA, lambda: fauna.pig(673), "prop", "complex"),
        ("SM_Goat_A", P_FAUNA, lambda: fauna.goat(677), "prop", "complex"),
        ("SM_Horse_A", P_FAUNA, lambda: fauna.horse(683), "prop", "complex"),
        ("SM_Horse_B", P_FAUNA, lambda: fauna.horse(691), "prop", "complex"),
        ("SM_Seagull_A", P_FAUNA, lambda: fauna.seagull(701), "prop", "complex"),
        ("SM_Rabbit_A", P_FAUNA, lambda: fauna.rabbit(709), "prop", "complex"),
    ]


def build_all(prop_material, emissive_material, foliage_material, world_data):
    """Generate every mesh asset. Returns {name: UStaticMesh}."""
    materials = {
        "prop": prop_material,
        "emissive": emissive_material,
        "foliage": foliage_material,
    }
    for folder in (P_NATURE, P_TOWN, P_PROPS, P_CITY, P_FAUNA):
        ctx.ensure_directory(folder)

    built = {}
    total_triangles = 0
    heaviest = ("", 0)

    for name, folder, builder_fn, material_key, collision in _catalog():
        builder = builder_fn()
        triangles = builder.triangle_count
        total_triangles += triangles
        if triangles > heaviest[1]:
            heaviest = (name, triangles)

        path = "%s/%s" % (folder, name)
        built[name] = meshkit.create_static_mesh(
            builder, path, materials[material_key], collision=collision)

    ctx.log("meshes built: %d assets, %d triangles total, heaviest %s (%d tris)"
            % (len(built), total_triangles, heaviest[0], heaviest[1]))
    return built


def load_all():
    """Load previously generated meshes without rebuilding them."""
    built = {}
    missing = []
    for name, folder, _builder, _material, _collision in _catalog():
        asset = ctx.load_asset("%s/%s" % (folder, name))
        if asset is None:
            missing.append(name)
        else:
            built[name] = asset
    if missing:
        ctx.warn("missing %d meshes (run the 'meshes' stage): %s"
                 % (len(missing), ", ".join(missing[:6])))
    return built
