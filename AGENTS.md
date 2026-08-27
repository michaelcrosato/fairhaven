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
./Scripts/Test.ps1                              # automation tests
./Scripts/Package.ps1                           # playable build
./Scripts/Screenshot-Tour.ps1                   # 14 viewpoints -> PNG
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
4. **`./Scripts/Test.ps1`.** Guards materials, vertex colours and world
   composition.
5. **Read `Saved/Logs/ContentBuild.log`** when a stage misbehaves. The build
   script echoes only the `[UEGT2]` lines; the full log has everything.

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

## Adding things

| To add | Do this |
|---|---|
| A mesh | Generator in `gen_nature.py` / `gen_town.py`, one line in `meshbuild._catalog()` |
| A plant or rock species | A `Species(...)` entry in `nature._rules()` |
| A usable object | Subclass `AUEGT2InteractableActor`, implement `OnInteract`, place it in `gameplay.py` |
| A setting | `UPROPERTY(Config)` on `UUEGT2GameUserSettings` + getter/setter + a row in `SUEGT2Menu.cpp` |
| A colour | `Tools/Python/uegt2/palette.py` — it is the only place colours live |
| A world feature (road, region, river) | `Tools/Terrain/world_config.py`, then re-run the terrain script |
| A screenshot viewpoint | `UUEGT2CaptureSubsystem::GetTour()` |
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
- `Content/` holds generated `.uasset`/`.umap` binaries and IS committed, so a
  clone can be opened and packaged without a full regeneration.
- `Tools/Terrain/Output/` and `Tools/Audio/Output/` are NOT committed. Recreate
  them with `Scripts/Setup-Project.ps1`, or the two `generate_*.py` scripts
  directly; the content build needs them and will say so if they are missing.
- `Binaries/`, `Intermediate/`, `Saved/`, `LocalBuilds/`, `Build/` and
  `DerivedDataCache/` are ignored. `Build/` matters: the cook writes file-open
  order logs there containing absolute user paths.
- Never commit credentials or absolute engine paths.

### The map is big

`Content/Maps/L_Fairhaven.umap` is ~54 MB and is rewritten *whole* by every
content build. It is committed directly (no LFS) so a clone is immediately
usable and nobody needs git-lfs. The cost is that each committed rebuild adds
another ~54 MB of history.

So: **do not commit a map rebuild unless the world actually changed.** If the
history does start to grow, the options in order of preference are

1. stop committing `Content/` and rely on `Scripts/Setup-Project.ps1`, which
   regenerates the entire world in about two minutes, or
2. move `*.umap` / `*.uasset` to Git LFS — noting that on a *public* repo every
   clone draws on the owner's LFS bandwidth quota.
