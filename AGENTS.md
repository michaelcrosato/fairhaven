# Fairhaven (UEGT2) engineering guide

Read this before changing anything. [Docs/Architecture.md](Docs/Architecture.md)
explains how the project fits together; this file is the working contract.

## Project contract

- Native Windows Unreal Engine 5.8 C++ project, Win64 / DX12 / SM6, targeting an
  RTX 3060-class GPU at 1920×1080.
- **The world is generated, never hand-authored.** The map is a build artifact.
  Anything you place by hand in the editor is destroyed by the next content
  build. Put it in a stage instead.
- C++ for durable runtime systems, Python for authoring content. No Blueprints
  and no binary UI assets: the whole project should stay diffable.
- Keep it launchable after every substantial change.

## Standard commands

```powershell
./Scripts/Setup-Project.ps1                     # fresh clone -> playable build
./Scripts/Build.ps1 -Target Both                # ALWAYS both targets
./Scripts/Build-Content.ps1 -Stages all         # rebuild the world
./Scripts/Build-Content.ps1 -Stages nature      # rebuild one stage
./Scripts/Build-Content.ps1 -Stages npc         # re-roll the population + road graph
./Scripts/Test.ps1                              # automation tests
./Scripts/Package.ps1                           # playable build
./Scripts/Screenshot-Tour.ps1                   # 19 viewpoints -> PNG
./Scripts/Screenshot-Tour.ps1 -Menu             # menu + settings -> PNG
./Scripts/Preview.ps1 -Stages lighting          # build + package + screenshot
python Tools/Terrain/generate_terrain.py        # re-roll terrain (+ PNG previews)
python Tools/Audio/generate_audio.py            # re-generate sounds
```

## Verification, in order of how much it has actually caught

1. **`-Target Both`.** The Game target rejects editor-only APIs the editor build
   accepts. `ADirectionalLight::GetComponent()` compiled fine in the editor and
   broke the game target.
2. **Look at a screenshot.** `./Scripts/Preview.ps1`. Several bugs here produced
   a completely clean log and a completely broken image: inverted triangle
   winding, an unconnected BaseColor, blown-out exposure.
3. **Package it.** The cook is the only place material shaders actually compile
   and the only place bad physics collision is reported. `Build-Content.ps1` and
   `Package.ps1` both fail the build on a material that does not compile.
4. **`./Scripts/Test.ps1`.** Guards materials, vertex colours, world composition,
   and the whole NPC behaviour model - the routines, the rules that override
   them, the speech pools and the baked population.
   `UEGT2.NPC.*` needs no map and runs in seconds.
5. **Read `Saved/Logs/ContentBuild.log`** when a stage misbehaves. The build
   script echoes only the `[UEGT2]` lines; the full log has everything.
6. **Read the population report.** Any run logs one `LogUEGT2NPC` line twelve
   seconds in: how many inhabitants exist, how many are out, how many are
   walking, how many are near the player, and the mean frame rate. "769 placed,
   none of them anywhere near the player" is what a movement bug looks like from
   the outside, and it renders as a perfectly clean log over an empty town.

## Traps already paid for

Do not undo these without understanding why they are there.

- **Triangle winding** (`meshkit._emit`): geometry is authored right-handed and
  the indices are swapped on the way out, because Unreal takes the opposite
  winding. Undoing this makes all two-sided foliage render pure black.
- **Wind weight is in UV1.x**, not vertex alpha. Every `VertexColor` output pin
  is named `""`, so only the RGB pin is reachable by name.
- **Lightmap UV generation is off** (`ConfigureGeneratedMesh`) because it would
  overwrite UV1.
- **Auto-exposure min/max are EV100 stops**, not multipliers, because
  `ExtendDefaultLuminanceRange` is on. Small values there blow the image to pure
  white. See the comment in `lighting.py`.
- **`MaterialEditingLibrary` returns `False` for a bad pin name** instead of
  raising. `materials._to` / `_link` warn loudly on failure; keep them.
- **Physics needs `collision="simple"`** in the mesh catalog. Complex-as-simple
  cannot be simulated and the cook fails.
- **Never pass the `.uproject` to a packaged build.** It makes the game look for
  uncooked content through a Zen server and fail to start.
- **`-nozenstore` in `Package.ps1`** keeps incremental cooks producing
  self-contained pak files.
- **Unreal's `-script=` argument processes backslash escapes.** Pass Python
  script paths with forward slashes or `\u` in the project path corrupts them.
- **`ctx.set_prop` warns instead of failing** on an unknown engine property.
  Every warning it prints is a real TODO. Check the log after a content build.
- **File-local helpers go in a named namespace, not an anonymous one.**
  `SUEGT2Menu.cpp` ends its style block with a file-scope `using namespace
  UEGT2Menu;`, and a unity build concatenates it ahead of other files in the
  module. An anonymous namespace puts its contents at global scope, where
  `Ink`/`Muted`/`Accent` then collide with the menu's (C2872). This is invisible
  locally, because adaptive unity compiles *modified* files on their own - it
  only breaks on a clean checkout. Verify with `-DisableAdaptiveUnity`.
- **The day/night cycle is force-frozen during captures.**
  `AUEGT2SkyController::BeginPlay` sets `DayLengthMinutes = 0` when
  `UUEGT2CaptureSubsystem::IsCaptureRequested()` or `IsWalkSmokeRequested()` is
  true. Without it `Screenshot-Tour.ps1` produces a different image every run.
- **The map wins over C++ defaults on placed actors.** `lighting.py` serialises
  `day_length_minutes = 0.0`, so the cycle is switched on by promoting a zero
  value in `BeginPlay`, not by changing the property default.
- **`polyline_field` must be given a margin.** Without one it scans the entire
  grid per road segment: at 4033 x 4033 that is 16 million samples times roughly
  1500 segments, allocating six full-size temporaries each time, and the road
  pass does not finish. Windowed to each segment plus a margin it is 177 seconds
  for the whole terrain. The margin must exceed the widest falloff the caller
  applies, or distant samples silently keep distance 1e12.
- **`is_street` includes the Newhaven grid.** Anything that means "town street"
  has to filter on `not is_city` too, or it will lay cottages down city avenues.
- **Fixed attempt counts do not survive a bigger map.** Scatter loops written as
  `range(900)` quarter in density when the extent doubles. Scale by area.
- **An NPC's mesh must not be its root component.** The walk cycle is a relative
  offset applied to the mesh every frame, and a relative move on the *root* is a
  world move: every NPC inside LOD range teleports to the world origin, which is
  under the town square. `AUEGT2NPCActor` puts a plain scene root above the mesh
  for exactly this reason. The symptom is a town that is full when frozen for a
  capture and empty the moment anything ticks.
- **NPC ground traces query `ECC_WorldStatic` by object type, not the Visibility
  channel.** Every NPC blocks Visibility so the interaction probe can find them,
  and a channel trace lands one NPC on another's head.
- **NPCs do not block the player.** Query-only collision, Visibility alone. The
  player starts in the town square where the crowd is thickest, and a solid crowd
  there means getting wedged between four villagers - and a packaged walk smoke
  that fails because somebody stood in front of the pawn.
- **Shared NPC anchors are picked from the closest few by seed, not by rank.**
  The town has five market stalls and ninety villagers; "nearest" sends all
  ninety to the same one and produces a single writhing mass of people.
- **The NPC director tracks the player's view point, not the pawn.** They differ
  during a screenshot tour, which parks the pawn at the player start and flies a
  separate camera around.
- **Screenshot tours freeze the population**, for the same reason they freeze the
  sun. `-UEGT2LiveNPCs` opts out and logs every line spoken; it is not
  reproducible, which is why it is not the default.
- **Auto exposure has to follow the time of day, or night renders pure black.**
  `lighting.py` bakes `auto_exposure_min_brightness = 10.5` *EV100* into the post
  process volume - that is a daylight floor. A moonlit scene sits near EV 0, so a
  fixed floor clamps it about ten stops too dark and the screen is black with a
  completely clean log. `AUEGT2SkyController::ApplySky` slides the min/max EV
  window down with the sun; the day values match what lighting.py writes, so
  noon is unchanged. Brightening the moon alone does not fix this.

## Adding things

| To add | Do this |
|---|---|
| A mesh | Generator in `gen_nature.py` / `gen_town.py`, one line in `meshbuild._catalog()` |
| A plant or rock species | A `Species(...)` entry in `nature._rules()` |
| A usable object | Subclass `AUEGT2InteractableActor`, implement `OnInteract`, place it in `gameplay.py` |
| A setting | `UPROPERTY(Config)` on `UUEGT2GameUserSettings` + getter/setter + a row in `SUEGT2Menu.cpp` |
| A dev mode control | Getter/setter on `UUEGT2DevModeSubsystem`, then a row in the matching `BuildDev*Tab()` |
| A weather preset | An `EUEGT2Weather` entry + a row in `UEGT2WeatherTable::Presets`; the tests check the table |
| A city building | A generator in `gen_city.py`, one line in `meshbuild._catalog()`, one entry in `city.CITY_BUILDINGS` and its ring |
| More map | `COMPONENT_COUNT` in `world_config.py`; it must stay `count * sections * quads + 1` |
| A colour | `Tools/Python/uegt2/palette.py` — it is the only place colours live |
| A world feature (road, region, river) | `Tools/Terrain/world_config.py`, then re-run the terrain script |
| A screenshot viewpoint | `UUEGT2CaptureSubsystem::GetTour()` |
| A trade, or a change to one's day | A routine in `UEGT2NPCRoutines.cpp`, plus a `_ROLE_LOOK` row in `npc.py` |
| An animal | A generator in `gen_fauna.py`, a `meshbuild._catalog()` line, a species routine |
| Something an NPC says | A pool in `UEGT2NPCSpeech.cpp`; the tests check every pool is filled |
| A rule that overrides a routine | `ResolveActivity` in `UEGT2NPCRoutines.cpp` |
| A content stage | A module in `uegt2/`, then register it in `build_content.py` |

## Style

- Match the surrounding code. Comments explain *why*, especially where an engine
  behaviour is surprising; the code already says what.
- One log channel per system (`UEGT2LogChannels.h`). Never `LogTemp`.
- Python: no numpy inside Unreal (it is not there). Heavy maths goes in
  `Tools/Terrain` or `Tools/Audio`, which run under system Python.
- Determinism matters: use `meshkit._SmallRng` or `ctx.Rng` seeded from
  `world_data.seed`, never `random`. The same seed must give the same world.

## Git

- Work on focused branches; keep `main` buildable.
- `Content/` holds the generated `.uasset` binaries and IS committed (~5 MB), so
  the materials, meshes and audio come with a clone. **The map does not.** See
  below.
- `Tools/Terrain/Output/` and `Tools/Audio/Output/` are NOT committed. Recreate
  them with `Scripts/Setup-Project.ps1`, or the two `generate_*.py` scripts
  directly; the content build needs them and will say so if they are missing.
- `Binaries/`, `Intermediate/`, `Saved/`, `LocalBuilds/`, `Build/` and
  `DerivedDataCache/` are ignored. `Build/` matters: the cook writes file-open
  order logs there containing absolute user paths.
- Never commit credentials or absolute engine paths.

### The map is not committed

`Content/Maps/L_Fairhaven.umap` is a **build artifact** and is gitignored.

It used to be committed at ~54 MB, on the reasoning that a clone should be
immediately usable. Growing the landscape to 4033 ended that: the map is now
~195 MB, which is past GitHub's **100 MB per-file hard limit**, so it cannot be
pushed at all. This is option 1 of the two that used to be listed here, applied
to the file that actually grows rather than to all of `Content/`:

- everything else under `Content/` is small and stable (~5 MB of materials,
  meshes and audio) and stays committed;
- the map is rewritten *whole* by every content build, so committing it added
  its full size to history every time.

Git LFS was the other option and was rejected for the reason already noted: on a
*public* repo every clone draws on the owner's LFS bandwidth quota, and at
195 MB a handful of clones would exhaust the free tier.

**What a clone has to do:** run `./Scripts/Setup-Project.ps1`, which generates
the terrain, builds both targets, rebuilds the world and packages it. Budget
about ten minutes. `./Scripts/Build-Content.ps1 -Stages all` alone rebuilds just
the map, in about two minutes, once the terrain output exists.

If the map ever needs to ship with the repo again, LFS is the route, and it is
one `git lfs track "*.umap"` away.
