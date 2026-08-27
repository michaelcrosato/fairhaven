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
| `meshes` | `uegt2/meshbuild.py` | 131 static meshes from the catalog |
| `audio` | `uegt2/audio.py` | sound classes, imported waves, ambience |
| `level` | `build_content.py` | opens/resets `L_Fairhaven` |
| `landscape` | `uegt2/landscape.py` | imports heightmap + 7 weightmaps |
| `water` | `uegt2/water.py` | sea plane, river ribbon, ponds, swim volume |
| `lighting` | `uegt2/lighting.py` | sun, sky, clouds, fog, post process |
| `town` | `uegt2/town.py` | buildings, docks, landmarks, farms, fences |
| `city` | `uegt2/city.py` | Newhaven: blocks, towers, street furniture, wharf |
| `nature` | `uegt2/nature.py` | ~650,000 instanced plants and rocks |
| `gameplay` | `uegt2/gameplay.py` | player start and interactables |
| `npc` | `uegt2/npc.py` | the road graph, ~500 people and ~290 animals |
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

**One material for everything.** Props, buildings and characters all use
`M_Prop`, driven by vertex colour. The whole palette lives in
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

- 131 unique meshes, 21,043 triangles total; the heaviest asset is 744 triangles.
- ~650,000 scattered instances across 13 species, all in hierarchical instanced
  components with per-species cull distances (grass 70 m, trees 700–900 m).
- ~790 inhabitants as movable static mesh actors. Measured cost: **nothing
  measurable** - 9.9 fps with all 786 present and walking against 9.1 fps with
  707 of them hidden, in the same view. (Both figures are low because they come
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
