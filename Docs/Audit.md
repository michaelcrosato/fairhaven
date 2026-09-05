# Repository audit, September 2026

This is a continuing audit. The first pass covers verification scripts, content
selection and mesh validation, NPC startup and following, dialogue, audio
settings, and documentation. It does not claim that every remaining issue has
been resolved.

## First pass

- Verification scripts reject stale reports, abnormal process exits, unfinished
  tests, incomplete screenshots and failed amenity interactions. Test and
  package timeouts are enforced. Capture cleanup preserves unrelated files.
- Content stage selection validates complete names before changing anything.
  Default builds exclude the optional showcase. Geometry checks traverse the
  whole catalog and report bad indices without aborting later checks.
- NPCs receive their starting purse when the map loads. Companions keep the
  Follow activity, stop beside their target, resume their routine on dismissal,
  and can leave to answer an urgent need.
- Dialogue accepts Unreal's number keys, updates live state without replacing
  unchanged topic buttons, and releases its viewport widget on teardown.
- Audio buses apply their own gain; the sound-class hierarchy applies Master
  once. Unused Python helpers and stale documentation have been removed.

Validation on Windows with Unreal Engine 5.8.2:

- Both Editor and Game targets built with `-DisableAdaptiveUnity`, with no
  compiler warnings or errors. All 39 Unreal automation tests passed.
- All 7 Python pipeline regressions and 391 mesh checks passed, including the
  full 304-entry catalog. The script harness passed 32 simulated cases and four
  native command-line cases under both PowerShell 7 and Windows PowerShell 5.1.
- A full content build completed in 603.5 seconds without project Python
  warnings or material compilation failures. The resulting map contains 1,188
  NPCs and 189 amenities. Packaging completed successfully.
- The packaged walk smoke moved 2,300 cm through the real input path. All 33
  world, six menu, two dialogue and six amenity screenshots were produced at
  1920 by 1080. Every amenity was found and used through the interaction probe;
  the logged needs and coins changed as expected.
- Selected screenshots were inspected, including the town square, market,
  Newhaven plaza, house interior, dialogue, menu and work interaction. This
  exposed the window defects below; successful capture does not establish that
  the whole scene is visually correct.
- A ten-minute packaged flight soak completed 63 route legs with zero stalls.
  The worst frame was 41.4 ms and the longest of nine garbage collections was
  5.6 ms; no hang, route cycle or runaway allocation was reported.

Logs and screenshots from this pass are local artifacts under `Saved/Logs`
(`Audit*`) and `Saved/Screenshots/Audit`. Engine warnings about tangent bases
and nearly zero binormals still need review. Machine-specific performance is
not a benchmark for the target GPU.

## Next passes

These leads have source evidence but still need focused implementation and
verification. They are not waived by the first pass's passing tests.

- The packaged house interior is dark even in daylight. `gen_town._glaze` and
  `_windows_on_wall` use opaque boxes spanning the whole window opening for
  frames, sealing the opening around the separate translucent pane. Separately,
  `meshkit.wall` fills vertically stacked apertures back in; this also blocks
  HouseB and HouseD. Outbuilding panes and wall cuts are not consistently
  aligned. Replace the slabs with perimeter frames and partition wall openings
  in both dimensions. Add clear-sightline regressions, rebuild meshes, cook and
  inspect both interior and exterior views. The local reproducer is
  `Saved/Audit/window_diagnostic.py`.
- `UEGT2NPCDirector::RunScheduleSlice` charges each visited NPC six times the
  latest slice interval. Small populations take fewer than six passes, and a
  hitch charges different slices different elapsed time. Account for each
  inhabitant's actual elapsed simulation time.
- The foliage distance setting changes `foliage.LODDistanceScale`, which does
  not scale the scatter components' authored end distances. Retain unscaled
  layer distances and apply the setting without compounding repeated changes.
- `SetToDefaults` omits the persisted needs/almanac visibility and Fahrenheit
  settings. Test the real reset operation over every project setting.
- `OnUnPossess` resets a binding flag but leaves the previous pawn's action
  bindings on the controller. Test repeated possession and remove stale bindings.
- Following overrides scheduled sleep, while the resolver suppresses urgent
  needs during scheduled sleep. Verify an overnight companion can still answer
  needs. The movement stuck-recovery branch also contains a reset that makes
  its later timeout unreachable.
- Check route orphan handling: `FinaliseNetwork` documents dropping unreachable
  stubs, but still includes them in the spatial index.
- Audit unused Water/Landmass/Niagara plugins and direct module dependencies.
  Source and generated asset names show no current use, but removal still needs
  both-target, content, cook and runtime verification.
- Verify sound-class parent links survive a fresh audio build: authoring assigns
  Master's children but saves only Master after the hierarchy change.
- `Resolve-Engine.ps1` accepts unrelated HKCU build paths before trying the
  expected version's conventional install path. Match the project association
  explicitly, support the launcher's `LauncherInstalled.dat`, and test
  GUID-associated source installations and version mismatches.
- Review project-specific warnings from a full content build and the packaged
  flight profile before changing rendering or population budgets.
