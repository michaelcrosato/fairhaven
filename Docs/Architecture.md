# Fairhaven architecture

The organising idea: **the world is generated, not authored**. Nothing about the
terrain, the meshes, the materials or the sound is hand-placed in the editor.
Every one of them comes from a script that can be re-run, and the map is a
build artifact of those scripts. That is what makes it safe for an AI agent to
change one number and rebuild.

## The pipeline

```
                    Tools/Terrain/world_config.py        <- the single source of truth
                                 |
        python Tools/Terrain/generate_terrain.py         (numpy, no Unreal)
                                 |
        heightmap.r16  weight_<Layer>.r8  world_features.json  preview_*.png
                                 |
        python Tools/Audio/generate_audio.py  ->  12 .wav
                                 |
        UnrealEditor-Cmd -run=pythonscript Tools/Python/build_content.py
                                 |
                    /Game/Maps/L_Fairhaven + /Game/Fairhaven/*
```

`world_features.json` is the contract. It carries the landscape parameters, the
town centre and radius, the road and street splines with their final elevations,
the river polyline with per-point elevation, the coastline samples, the ponds
and the lagoon. Every placement stage reads it; none of them re-derive geometry.

Move the town, re-roll the seed or redraw a road in `world_config.py`, re-run the
terrain script, then `./Scripts/Build-Content.ps1 -Stages all`. Buildings, docks,
signs, vegetation, fences and ambience all move with it.

## Content build stages

Run individually with `./Scripts/Build-Content.ps1 -Stages <names>`. They run in
dependency order regardless of the order given.

| Stage | Module | Produces |
|---|---|---|
| `materials` | `uegt2/materials.py` | 5 master materials, no textures |
| `meshes` | `uegt2/meshbuild.py` | 155 static meshes from the catalog |
| `audio` | `uegt2/audio.py` | sound classes, imported waves, ambience |
| `level` | `build_content.py` | opens/resets `L_Fairhaven` |
| `landscape` | `uegt2/landscape.py` | imports heightmap + 7 weightmaps |
| `water` | `uegt2/water.py` | sea plane, river ribbon, ponds, swim volume |
| `lighting` | `uegt2/lighting.py` | sun, sky, clouds, fog, post process |
| `town` | `uegt2/town.py` | buildings, docks, landmarks, farms, fences |
| `city` | `uegt2/city.py` | Newhaven: blocks, towers, street furniture, wharf |
| `nature` | `uegt2/nature.py` | ~650,000 instanced plants and rocks |
| `gameplay` | `uegt2/gameplay.py` | player start and interactables |
| `npc` | `uegt2/npc.py` | the road graph, ~930 people and ~285 animals |
| `showcase` | `uegt2/showcase.py` | dev-only grid of every mesh |

`showcase` is excluded from `-Stages all` on purpose.

## Modules

**`Source/UEGT2`** — runtime. Ships in the game. Never references editor code.

```
AUEGT2GameMode
├── AUEGT2Character            camera on a capsule; no skeletal mesh, no anim
│   └── UUEGT2InteractionComponent ── IUEGT2Interactable
├── AUEGT2PlayerController     input, menu ownership, diagnostics toggle
│   └── SUEGT2Menu             front end, pause, settings (Slate, in code)
├── AUEGT2HUD                  crosshair, prompt, messages, F3 overlay
└── UUEGT2GameUserSettings     every persisted setting, one save file

World actors:
  AUEGT2SkyController          drives sun/sky/fog from one TimeOfDay value
  AUEGT2ScatterField           hierarchical instanced mesh layers
  AUEGT2InteractableActor      base; Sign, Door, Lamp, Pickup, Landmark
  AUEGT2NPCActor               one inhabitant, person or animal
  AUEGT2RouteNetwork           the baked walkable road graph
  UUEGT2NPCDirector            LOD tiers, schedule slices, the speech budget
  UUEGT2CaptureSubsystem       headless screenshot tours
```

**The inhabitants** get their own document: [NPCs.md](NPCs.md). The organising
idea there is the same one as everywhere else in this project - the interesting
part is a pure function over plain data. `ResolveActivity` takes an hour, a
weather, a personality and a distance to the player and returns what somebody
should be doing, without touching the world, which is why the whole behaviour
model is covered by unit tests rather than by playing the game and hoping.

**`Source/UEGT2Editor`** — authoring only.

```
UUEGT2LandscapeTools      ALandscape::Import wrapper (heightmap + weightmaps)
UUEGT2AuthoringLibrary    material usage flags, mesh build settings,
                          simple box collision, text file output
UEGT2ContentTests         automation tests over the generated content
```

## Decisions worth knowing

**One material for everything, and one exception.** Props, buildings and
characters all use `M_Prop`, driven by vertex colour. The exception is
`M_Glass`, which is translucent, and it earns the exception twice: a window that
is an opaque painted panel reads as a blank canvas from inside a room, and it
seals the building against daylight. Because a static mesh carries exactly one
material, every glazed building is generated as *two* meshes from one call - the
shell and its panes - and `town.Placer._glaze` hangs the panes on the shell's
transform automatically, so no call site has to remember. The panes cast no
shadow, which is what lets the sun through them. The whole palette lives in
`Tools/Python/uegt2/palette.py`. This keeps draw calls low, removes every
texture dependency, and means a colour change is a one-line edit.

**Meshes are explicit vertex buffers, not primitives.** `meshkit.py` builds
flat-shaded faces with their own normals and colours, then hands the lot to
GeometryScript in one call per asset. Two traps are baked into that file and
must not be undone:

- *Winding.* Geometry is authored right-handed, and `_emit` swaps the indices
  because Unreal takes the opposite winding. Getting this wrong makes
  two-sided foliage render pure black, because Unreal flips the shading normal
  on what it thinks are back faces.
- *Wind weight lives in UV1.x*, not vertex alpha. Every `VertexColor` output pin
  in Unreal is named `""`, so only the float3 RGB pin is reachable by name and
  the alpha cannot be addressed from script.

**A house is a shell plus a fit-out, on one transform.** `gen_town.house()`
builds the outside - four wall panels with real openings punched through them by
`meshkit.wall()`, a roof, a chimney - and `gen_interior.fit_out()` builds
everything inside as a *separate* mesh dropped on the same actor transform. They
are separate because an interior is 650-2,000 triangles that nobody can see from
the street, so it is drawn only within 90 m while the shell stays visible across
the valley. Its collision is not culled with it, so an upper floor stays solid
under an NPC in a house nobody is looking at.

Four things hold the two halves together, and all four have already been the
cause of a bug:

- *One set of constants.* `gen_town` owns `WALL_T`, `PLINTH_H` and `STOREY_H`;
  `gen_interior` imports them. Two copies of a contract is one copy and a guess.
- *Furniture is placed off measured geometry.* `gen_interior._measure()` builds
  a piece and reads its real bounding box. The hand-written dimension table it
  replaced had drifted, and put a counter through the front wall.
- *The front doorway is sacred.* A room partition is nudged clear of it in
  `_split`, because a 16 cm partition down the middle of a 120 cm opening leaves
  52 cm either side and the pawn is 68 across. That made a third of the houses
  unenterable, and every bounds check passed.
- *The house sits on the highest ground under it*, not the lowest
  (`town.Placer.ground_range`), with a 2.4 m foundation and a flight of steps to
  take up the slack downhill. Sitting it on the lowest corner put the hillside
  through the floor of 72 of 114 houses and buried 25 front doors.

**Interiors are lit, not daylit.** There is no translucent material, so a window
pane is an opaque panel and no light comes through it. Each room gets a point
light at its ceiling lamp instead. They are unphysically bright - 45,000 lumens -
because exposure is one global setting shared with the outdoors and floored at
EV 7 so that night still reads as night; a room has to reach about EV 10 to be
visible at all under that floor.

**A tall building is one mesh per floor, placed once per storey.** A twenty-two
storey interior built as a single mesh is 43,000 triangles and a 48 MB build,
and the editor died somewhere around the twentieth tower cooking them. So
`gen_interior.fit_out(..., stair_up=True, floor_hole=True)` builds ONE floor -
its own slab with the stairwell punched through it, a flight up out of it, and
enough furniture to say what the floor is for - and `city._place_interior`
places that one mesh on every storey. A tower costs twenty-one actors and one
mesh, each floor culls on its own, and no asset is bigger than about 2,000
triangles.

The stair column is the thing that makes it stack: `gen_interior.stair_column`
puts the flight against the +X wall from the footprint alone, so every floor
agrees where it is without being told, and `plan()` is handed the stairwell as a
keep-out so no partition is ever built through it. The roof reads the same
rectangle back (`gen_city._roof_hole`) to punch its deck and stand a stair head
on it, which is what makes a roof somewhere you can walk out onto.

**Every building in the world opens.** The town's houses have a full
multi-storey fit-out; every other building - the barn, the church, the
warehouse, the shed, and all 334 blocks of Newhaven plus the city hall - has a
walkable ground floor. Above that, a tower stays solid: you can walk into a
building, not up it. That is the same bargain the roof makes with the attic, and
it keeps a 31 storey tower from costing 31 storeys of furniture.

Newhaven's ground floors are **trades**. `meshbuild.CITY_INTERIORS` names the
businesses each archetype can host and `city._venue_for` deals them out, one
mesh per trade rather than one per building, so a city with a dentist in it
costs one extra asset and not one per block. The count is kept per archetype:
on a single global counter an archetype with four trades and an unlucky
placement order skips one entirely, which is how the city first came out with no
dentist in it. `gen_interior.VENUE_RECIPES` is the furniture, and `BACK_OF_HOUSE`
is what sits behind the shop floor - a kitchen behind a restaurant, a stock room
behind a grocer - because four identical shop floors in a row read as a
warehouse.

**Four needs, and somewhere to answer each of them.** Every inhabitant carries
Energy, Fed, Relief and Company in `FUEGT2NPCNeeds`, seeded to a different value
each so the town does not all get hungry at once. `Needs::Worst` returns the one
furthest past its own threshold, and `ResolveActivity` acts on it *at any hour* -
the old code answered hunger only between eleven and three, which meant a
villager could be starving at half nine and keep working.

Each need has an anchor the content build bakes per inhabitant from the nearest
handful of real places: `Food` (a stall, a tavern, a bakery, a grocer, a
restaurant), `Washroom` (a privy, a public convenience, a gym), `Seat` (a bench)
and `Square` for company. Home is the fallback for all of them, because a house
has a chair, a kitchen and a washroom in it - so an inhabitant who cannot find a
public one goes home, which is what a person does. There are twelve public
conveniences in the town and eighteen in Newhaven, spread across the squares,
the quay, the farms and every park, because a need answered in exactly one place
is a need that sends the whole town to the same doorstep.

**You can talk to anyone, and what they say is true.** `UEGT2Dialogue.h` is a
set of pure functions over `FUEGT2DialogueState` - a snapshot of one
inhabitant's needs, activity, role, hour and seed. No world, no actor, no
widget, exactly like `ResolveActivity`, which is what lets the whole
conversation be tested without opening the editor.

Every answer is derived from real state. Ask whether someone is hungry and the
reply is chosen by the same `Fed` value, against the same thresholds, that
`ResolveActivity` uses to decide whether to send them to eat - so what they say
and what they then do agree. Saying "I'm fine" and walking off to the bakehouse
is the sort of small lie that makes a whole world feel fake, and the thresholds
are shared precisely so it cannot happen.

The panel is `SUEGT2Dialogue`, Slate like the rest of the UI, sharing its
palette with the menu through `UEGT2UIStyle.h`. It sits low on the screen rather
than filling it - you are talking to somebody standing in front of you - and it
shows the four needs as bars beside the topic list, so you can see how someone
is *before* you ask and then hear them say the same thing. The world is not
paused while it is open: the bars move while you talk.

Two topics change the world rather than describe it. "Will you walk with me?"
sets a follow target on the NPC; `AdvanceFollowing` repaths toward the player
every second or so and stops at a couple of metres. Following overrides where
they go but not what they need - a companion who gets hungry still says so and
still breaks off, because `EvaluateSchedule` only lets following win when the
decision was not need-driven.

**The date, the clock and the temperature are computed, not stored.**
`UEGT2Almanac.h` turns the director's running day count into a calendar - six
day weeks, thirty day months, twelve months, 360 day years - whose weekday names
agree with `IsMarketDay` and `IsRestDay`, so the HUD never says Marketday on a
day the market is shut. There is a test that walks 400 days checking exactly
that.

There is no thermometer in the world, so the temperature is modelled from the
things that would actually set it: the west of the map is tropical and the
north-east is a 440 m mountain range (both read from the same constants in
`Tools/Terrain/world_config.py` that shaped the land), plus the standard 6.5
degrees per kilometre lapse rate for altitude, a diurnal swing peaking at three
in the afternoon, a seasonal swing peaking at midsummer, and an offset for the
weather. Walk up the mountain road and it drops; walk south-west and it climbs.

**Landscape, not World Partition.** One level, one landscape, no streaming. At
4 km with instanced scatter this loads in seconds and keeps the content build
simple. Streaming is the obvious next step if the world grows, and nothing here
prevents it.

**Nanite is off for the landscape.** It bakes an ~8M triangle mesh into the map
(about 150 MB) for little gain at this resolution. Flip
`landscape.ENABLE_NANITE` if that trade changes.

**Static lighting is disabled project-wide.** Generated meshes have no authored
lightmap UVs, and UV1 carries wind weight. Everything is Lumen + virtual shadow
maps.

**Water is generated surfaces, not the Water plugin.** See the note at the top
of `uegt2/water.py` for the trade-off.

## Performance shape

Target: 1920×1080, 60 fps on an RTX 3060-class GPU.

- 304 unique meshes, 241,867 triangles total; the heaviest asset is 7,184
  triangles (`SM_Tower_D`, a thirty-one storey shell). No interior is bigger
  than about 4,400, because a tall building's floors are one mesh placed many
  times rather than one mesh containing them all.
- About 7,700 actors in the level: 1,440 in the town, 6,090 in Newhaven, 1,190
  inhabitants. Newhaven's count is dominated by stacked floors - a tower is
  twenty-one floor actors - and by ~1,700 interior point lights. The lights are
  movable and cast shadows, and are affordable only because `max_draw_distance`
  is 42 m, so a handful are ever submitted.
- ~650,000 scattered instances across 13 species, all in hierarchical instanced
  components with per-species cull distances (grass 70 m, trees 700–900 m).
- ~1,215 inhabitants as movable static mesh actors. Measured cost: **nothing
  measurable** - 9.9 fps with the whole population present and walking against
  9.1 fps with 707 of them hidden, in the same view. (Both figures are low because they come
  through `-RenderOffscreen`; the point is the difference.) Four things buy that:
  distance tiers, dormant NPCs teleporting along their schedule instead of
  walking it, the population being walked in six slices per pass, and small
  animals casting no shadow and culling at 90 m.
- Only trees, rocks and fences have collision. Grass, crops and ferns have none.
  NPCs are query-only against the Visibility channel: you walk through people.
- Lamps are unshadowed point lights.
- The landscape material samples no textures: it is a 7-layer blend of flat
  colours times two octaves of procedural noise.

Measure before raising instance counts, adding shadowed lights, or increasing
Lumen quality.

## Extension rules

1. **Never hand-place content in the editor.** Add it to a stage so it survives
   the next rebuild. A rebuild wipes the map.
2. **Add a mesh** by writing a generator in `gen_nature.py` or `gen_town.py` and
   one line in `meshbuild._catalog()`. Placement stages look it up by name.
3. **Add a usable object** by subclassing `AUEGT2InteractableActor` and
   implementing `OnInteract`. Do not touch the player or the HUD.
   **Add a kind of inhabitant** by adding an `EUEGT2NPCRole`, a routine in
   `UEGT2NPCRoutines.cpp` and a `_ROLE_LOOK` row in `npc.py`. The tests will tell
   you if the routine is malformed before you ever load the map.
4. **Add a setting** as a `UPROPERTY(Config)` on `UUEGT2GameUserSettings`, a
   getter/setter pair, and a row in the matching tab in `SUEGT2Menu.cpp`.
5. **Anything that simulates physics needs `collision="simple"`** in the mesh
   catalog. Complex-as-simple cannot be simulated, and the cook will fail.
6. **Build both targets** (`-Target Both`). The Game target is what catches
   editor-only API use.
7. **Look at it.** `./Scripts/Preview.ps1` builds, packages and renders the tour.
   Several bugs in this project rendered perfectly clean logs.

## Diagnostics

- `F3` in game: frame time, position in metres, speed, focused interactable,
  quality levels.
- `LogUEGT2`, `LogUEGT2Player`, `LogUEGT2Interaction`, `LogUEGT2Settings`,
  `LogUEGT2UI`, `LogUEGT2World`, `LogUEGT2Diag`, `LogUEGT2Dev`, `LogUEGT2NPC` —
  one channel per system.
- **The population report.** Twelve seconds into any run, `LogUEGT2NPC` prints
  how many inhabitants exist, how many are outdoors, how many are walking, how
  many are within 100 m and the mean frame rate over the preceding six seconds.
  It exists because "769 placed, none of them anywhere near the player" is what a
  movement bug looks like from the outside, and a headless capture would
  otherwise report it as a perfectly clean run over an empty town.
- `Saved/Logs/ContentBuild.log` — every content build; the build script echoes
  only the `[UEGT2]` lines and fails on any material that does not compile.
- `Saved/Screenshots/Tour` and `Saved/Screenshots/Menu` — the visual record.
- `Tools/Terrain/Output/preview_relief.png` and `preview_biomes.png` — inspect
  terrain changes without opening Unreal.
