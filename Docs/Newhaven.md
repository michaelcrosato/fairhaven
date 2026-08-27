# The bigger map and Newhaven

The world was a 2.016 km square with one coastal town. It is now a **4.032 km
square** with a second, very different settlement in the south: **Newhaven**, a
procedurally generated city of towers, avenues and a container wharf.

Fairhaven is painted render, gable roofs and hedgerows. Newhaven is concrete,
glass and flat tar on a rectilinear grid. The drive south is meant to feel like
going somewhere else.

## What changed in the map

| | Before | After |
|---|---|---|
| Landscape | 2017 x 2017 verts | 4033 x 4033 verts |
| Components | 16 x 16 | 32 x 32 |
| Extent | 2.016 km square | 4.032 km square |
| Heightmap | 7.8 MB | 31.0 MB |
| Mountains reach full height | 920 m north | 1.68 km north |
| Peak | 372 m | 440 m |
| Ponds | 2 | 4 |

Landscape sizes are not free-form. `SIZE` must be

    COMPONENT_COUNT * SECTIONS_PER_COMPONENT * QUADS_PER_SECTION + 1

which is why the map grows by component count. Doubling the side quadruples the
area, the heightmap, the scatter counts and the cooked map, so `COMPONENT_COUNT`
in `world_config.py` is the first knob to turn back if a build gets too heavy.

The biome bands were stretched to match. Without that the original world would
have sat in the middle quarter of the new map with three quarters of empty
ocean and featureless plain around it.

## The southern shelf

Newhaven needed flat coastal land that did not exist. The coastline already had
a headland in the north and a bay in the south; a third term swings it back out
to sea below x = -88 km:

```python
SOUTH_SHELF_AMOUNT = 46000.0
SOUTH_SHELF_START_X = -88000.0
SOUTH_SHELF_FULL_X = -152000.0
```

That shelf is the only reason the southern half of the larger map is land at all.

## How the city is generated

Nothing about the city is hand-drawn. `world_config.py` holds a dozen numbers;
everything else falls out of them.

```
CITY_CENTER, CITY_RADIUS, CITY_ANGLE      where and how big
CITY_BLOCK_U, CITY_BLOCK_V               block size
CITY_AVENUE_EVERY                        every third grid line is an avenue
CITY_CORE_RADIUS, CITY_MID_RADIUS        the height rings
```

`city_streets()` emits the grid, clipped to a circle. Because the rotation is a
pure rotation about the centre, clipping a grid line to the city boundary is
just a chord length in local coordinates.

`city_blocks()` emits the land between the streets, each block tagged with a
`ring`: 0 downtown, 1 mid-rise, 2 outer. **The skyline is a property of the
layout, not of the spawner** — that is what keeps the silhouette stable if the
placement code changes.

The terrain generator carves the grid exactly like the town streets, paves it
(high `Dirt` weight, since there is no Paving layer), and exports the blocks into
`world_features.json`. `Tools/Python/uegt2/city.py` then fills them in.

### Why the blocks fill the way they do

`city.py` walks each block edge placing buildings that face the street, and
rejects any that overlap one already placed. That single rule produces the shape
of the city with no special cases:

- A downtown block is 36.8 m deep between streets and a tower is about 20 m
  deep, so two rows will not fit. The second row is rejected and downtown blocks
  end up with a single row of towers.
- An outer block holds 15 m shophouses, so all four frontages fit and the block
  gets a full perimeter with a yard in the middle.

At the current settings that is **346 buildings over 82 blocks**, with 274
candidates rejected. The rejections are not waste; they are the mechanism.

One block nearest the centre becomes the civic plaza (city hall, fountain,
benches). Every ninth block becomes a park, because a city with no gaps reads as
a wall rather than a place.

## The buildings

20 generated meshes, 6,908 triangles all together, all box primitives with
vertex colours on the same opaque material as everything else.

| Mesh | Floors | Height |
|---|---|---|
| SM_Tower_D | 31 | 122 m |
| SM_Tower_B | 26 | 103 m |
| SM_Tower_A | 20 | 81 m |
| SM_Tower_C | 15 | 62 m |
| SM_Office_B | 12 | 49 m |
| SM_Office_A | 9 | 38 m |
| SM_Apartment_B | 8 | 31 m |
| SM_Apartment_A | 6 | 24 m |
| SM_ParkingDeck_A | 4 | 17 m |
| SM_Apartment_C | 4 | 16 m |
| SM_Shophouse_A | 3 | 11 m |
| SM_Shophouse_B | 2 | 8 m |

Towers are a banded shaft rather than modelled windows: one solid body plus a
thin slab at every floor line. The horizontal banding alone is what makes a box
read as a storeyed building, and a tower is only ever seen from far enough away
for that to be all you need.

Plus `SM_CityHall_A` (portico, wings and a copper dome) and street furniture:
traffic lights, tall steel lamps, kiosks, bus shelters, a fountain and planters.

## Traps this work paid for

- **`polyline_field` scanned the entire grid once per road segment.** With 37
  road corridors over a 4033 x 4033 grid that is roughly 1500 segments times 16
  million samples, allocating six full-size temporaries each time. It ran for
  over eleven minutes without finishing the road pass. It now windows each
  segment to its own bounding box plus a caller-supplied margin, and the whole
  terrain build takes 177 seconds. The windowed result is bit-exact wherever any
  caller uses it, because every caller smoothsteps its influence to zero well
  inside the margin it passes.
- **`is_street` is not the same as "town street".** The city grid is flagged
  `is_street` too, and the town stage would happily have lined Newhaven avenues
  with thatched cottages. It now filters on `is_city` as well.
- **Fixed attempt counts silently thin out when the map grows.** The scarecrow
  scatter was `range(900)` over the old extent; on a map with four times the area
  that is a quarter of the density. It now scales with area.
- **The city hall is 34 m deep, not 19 m.** Measuring the main block alone and
  ignoring the portico and the entrance steps put the columns out in the avenue.
- **Trees will grow through towers.** The paving weight thins the scatter but not
  to zero at the edges, so `nature.py` cuts an explicit hole at the city radius.

## Rebuilding

```powershell
python Tools/Terrain/generate_terrain.py     # about 3 minutes at 4033
./Scripts/Build-Content.ps1 -Stages all      # the whole world
./Scripts/Build-Content.ps1 -Stages city     # just Newhaven
```

Moving the city is a one-line change to `CITY_CENTER`, then those two commands.
The streets, the blocks, the buildings, the wharf, the landmarks and the road in
from Fairhaven all follow.
