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
and nearly zero binormals prompted the UV repair in the second pass.
Machine-specific performance is not a benchmark for the target GPU.

## Second pass

- Town window frames are perimeter rails. Walls subtract the union of openings
  in both dimensions, so stacked windows remain open. Barn, church and warehouse
  panes share their position definitions with the wall cutouts. Nine building
  shells and three outbuilding glass assets change geometry; 292 catalog meshes
  retain identical geometric buffers. The catalog now contains 246,967 triangles.
- UV0 projects each flat face onto its dominant plane. The old XY projection
  collapsed 150,601 catalog triangles to UV lines, causing tangent warnings.
  UV1 still carries the original wind weights. Upload UV0 changes in 302 meshes.
- The NPC director integrates simulation time and charges each inhabitant its
  actual elapsed interval. Registry changes and crowd suppression preserve that
  accounting. The player also charges full elapsed time; an obsolete quarter-hour
  cap made its ledger diverge during hitches and at fast day lengths. Disabling
  the day/night cycle stops the shared life rate. Unused private timing arguments
  in the speech update functions are removed.
- Companions respect weather, sleep and roosting decisions. Ineffective stuck
  detection that could reroute or teleport an NPC after a hitch is removed.
- Unlinked route nodes remain addressable by ID but cannot mask nearby roads
  in spatial queries. New links invalidate the index; wander targets respect
  their requested radius from the first node onward.
- Nature layers opt into the foliage-distance setting and retain unscaled
  baselines. Repeated changes cannot compound the distances. Nature rebuilding
  preserves the town's separate fence field, and fences keep their own culling.
- Resetting settings restores needs/almanac visibility and Celsius. Unpossession
  removes the old pawn's delegates while preserving controller actions.
- Audio authoring repairs one sound-class parent edge per editor change event
  and saves every affected class. The loaded-asset test checks both directions
  of the Master/child relationship.
- Engine discovery matches the exact association, supports launcher manifests
  and source-build GUIDs, verifies automatic release major/minor versions and
  fails clearly on an invalid explicit override. Package/capture output paths
  follow PowerShell's current location; editor launch quotes the project path.
- Unused direct module dependencies are removed; GeometryScripting is restricted
  to editor authoring. Final Editor and Game receipts exclude Water, Landmass,
  ChaosCloth and Buoyancy. Niagara remains a transitive engine-tooling dependency.

Validation of the final second-pass snapshot:

Runs used Unreal Engine 5.8.2 and an RTX 4070 SUPER. Frame timings describe this
machine; they do not establish the RTX 3060 target budget.

- Both Editor and Game targets built with `-DisableAdaptiveUnity`; all 53 Unreal
  automation tests passed.
- A full content build completed in 577.6 seconds with zero tangent, binormal
  or project Python warnings. Its 302 changed mesh assets and three repaired
  sound classes are retained; unchanged assets were restored to avoid
  serialization-only diffs. Water/audio authoring also passed after removing
  the unused plugins, and the saved audio hierarchy passed the loaded-asset test.
- Python passed 11 regression methods and 400 checks, including 567 clear
  sightlines through 63 actual panes and UV areas across the full catalog.
  Engine discovery passed 22 fixture cases under PowerShell 7 and Windows
  PowerShell 5.1. The main script harness passed 35 simulated cases and six
  native argument checks in both shells.
- Packaging completed in 3 minutes 19 seconds with no logged warnings or errors.
  The archive contains all 48 staged files and no obsolete build files; its
  additional files are runtime settings and logs. The staged manifest excludes
  Water, Landmass, ChaosCloth, Buoyancy and runtime GeometryScripting.
- The packaged walk smoke moved 2,300 cm. All 33 world, six menu, two dialogue
  and six amenity captures passed at 1920 by 1080. Every amenity was found and
  used through the interaction probe; the needs and coin deltas matched the
  activity. The dialogue shows live following state and the dismissal option.
- Visual comparison with the first pass confirms open windows and daylight
  in the house, upstairs room, cottage and church. The house furnishings are
  visibly readable, and the street view retains its exterior frames and glazing.
  Six additional city, water and nature views show no new missing faces or
  shading regressions.
- The ten-minute packaged flight soak completed 63 route legs with zero stalls.
  The worst frame was 68.1 ms and the longest of nine garbage collections was
  5.1 ms. The object count stayed stable after the initial collection, memory
  returned to about 2.4 GB, and no hang, route cycle or runaway allocation was
  reported.

Second-pass logs are under `Saved/Logs/Audit2*`, with screenshots in
`Saved/Screenshots/Audit2`.

## Audit outcome

No unresolved corrective findings remain from these two passes. Final
independent runtime, content and script reviews found no additional concrete
repair in the audited scope. Existing milestone feature limits remain documented
in [Playtest-0.1.md](Playtest-0.1.md).
