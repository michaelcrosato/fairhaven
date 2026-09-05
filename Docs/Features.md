# Feature log

Each feature has a stable ID, a player control where useful, and an independent
maintainer switch. Turning a feature off must leave the baseline game playable
and preserve any data the player may want again later. New entries record the
behavior, off path, compatibility and the checks actually run.

## F001 — Player progress

Status: implemented and verified on `feature/player-progress`, 2026-09-04.

**What it adds.** Pause → Save Progress records a manual checkpoint. Continue
on the main menu restores the player's position and view, four needs,
fractional coin balance, trade, surveyed landmarks, day, hour and weather.
New Visit starts a fresh world; it preserves the previous checkpoint until the
player explicitly saves the new visit. There is no autosave in this version.

Restoring ends transient activity and resumes on foot. Conversations, followers,
carried props, door/lamp state and individual NPC state are not checkpointed;
the town resumes its routines at the restored calendar time. An obstructed saved
position uses a safe placement fallback. Invalid or incompatible data must not
partially change the running session.

**Player switch.** Settings → Gameplay → Save Progress. It defaults to On.
Turning it off immediately disables checkpoint reads and writes and retains
existing saves. The persisted preference is `bSaveProgressEnabled` in the
`[/Script/UEGT2.UEGT2GameUserSettings]` section of `GameUserSettings.ini`.

**Maintainer switch.** In `Config/DefaultGame.ini`:

```ini
[/Script/UEGT2.UEGT2ProgressSubsystem]
bFeatureEnabled=False
```

Set it back to `True` to enable the feature. This gate is independent of the
player preference, so an existing player's On setting cannot override it.
Captures, walk smoke and flight soak disable progress IO automatically. The
dedicated progress smoke uses an isolated user directory and temporary slots.

**Data contract.** `UUEGT2ProgressSave` carries schema version 1, content revision
1 and map identity. A fixed header, payload length and checksum reject damaged
bytes before Unreal decodes variable-length fields. The single logical checkpoint
rotates between two physical slots so a failed write can retain the previous
valid checkpoint. The files are `Fairhaven_Journey_A.sav` and
`Fairhaven_Journey_B.sav` under Unreal's `Saved/SaveGames` directory. Landmark
identity comes from explicit IDs in the generated content, not actor names or
translated display text. Change the content revision when geography or those
IDs become incompatible with saved positions and discoveries.

**Implementation.** `Source/UEGT2/{Public,Private}/Progress/`, the needs
component, the director's calendar restore, landmark state, and the existing
controller/Slate menu. Runtime outcomes use `LogUEGT2Progress`; settings logs
record the player's switch. `Scripts/Smoke-Progress.ps1` exercises the packaged
save/continue/off paths in separate processes.

**Research.** Epic's [saving and loading guide](https://dev.epicgames.com/documentation/unreal-engine/saving-and-loading-your-game-in-unreal-engine)
describes explicit `USaveGame` snapshots and allows synchronous writes for
small payloads while paused. That fits a manual checkpoint; autosaving during
active play would need a separate asynchronous design.

**Verification.** Both Development targets compile with adaptive unity disabled.
The full automation suite passes 63 tests, including five progress tests,
landmark restoration/isolation and pickup release. The landmark tests were
rerun after correcting their world-context fixture and pass without warnings.
Progress coverage includes exact fractional restoration, invalid values,
truncated/altered bytes, fallback recovery, crouching and blocked placement,
midnight handling, manual-save lifecycle, both off switches and diagnostic gates.

The gameplay stage rebuilt all 11 IDs without Python warnings. The 11 Python
pipeline tests pass. Packaging completed in 1m44s; the four-phase packaged smoke
passed Save → separate-process Continue → New Visit → Disabled. It verified a
137.625-coin balance, four distinct needs, trade, position/view, calendar,
discovery, repeated loading, continued live needs decay, preserved checkpoint
bytes on New Visit, and unchanged data when saving is off. The wrapper's six
failure-handling cases pass under PowerShell 7 and 5.1. The ordinary packaged
walk smoke moved the player 23.04 m through real input.

The Continue, Save Progress and Gameplay setting screenshots were inspected at
1920×1080. Local evidence is under `Saved/Logs/Progress*` and
`Saved/Screenshots/ProgressSmoke/83c43fb398d54764b2e23583c2298a5f/`.

## F002 — Survey journal and directions

Status: implemented and verified on `feature/survey-journal`, 2026-09-05.

**What it adds.** Press J or gamepad Back, or choose Pause → Survey Journal,
to review the 11 survey landmarks. The shortcut can be rebound in Controls.
Track a surveyed place to see its name, compass direction and horizontal distance
on the HUD. The arrow points relative to the player's view. Within 10 metres,
a Nearby indicator replaces the arrow. Distances are straight lines, so the
player still chooses a walkable route. Stop Tracking clears the cue.

The journal pauses the world. Its shortcut, Escape, or Close Journal returns
to play. Unsurveyed places appear in the list but cannot be tracked. The journal
uses the landmarks' existing discovery state and works while F001 saving is
off. The tracked target is session state and is not checkpointed; New Visit or
relaunch clears it. Discoveries still persist through F001's manual checkpoint.

**Player switch.** Settings → Gameplay → Survey Journal. Default On;
`bSurveyJournalEnabled` in `[/Script/UEGT2.UEGT2GameUserSettings]`.
Turning it off hides journal access and directions without changing discoveries.

**Maintainer switch.** In `Config/DefaultGame.ini`:

```ini
[/Script/UEGT2.UEGT2SurveySubsystem]
bFeatureEnabled=False
```

Set it back to `True` to enable it. A player's On setting cannot override the
maintainer gate. F001 and F002 can each be disabled independently.

**Implementation.** `Survey/UEGT2SurveySubsystem` reads the roster on request
and retains one weak tracked actor for HUD queries; it has no tick and no second
discovery ledger. `SUEGT2SurveyJournal` uses the existing Slate menu and shared
style. Input, settings and the Canvas HUD remain C++. `LogUEGT2Survey` records
opening, closing and tracking changes. The save schema and generated content
are unchanged.

**Research.** The world convention is +X north, +Y east (`world_config.py` and
the screenshot tour). Directions use `atan2(Y, X)` and horizontal centimetres
converted to metres. Epic's [in-game Slate guide](https://dev.epicgames.com/documentation/unreal-engine/using-slate-in-game-in-unreal-engine)
describes adding C++ widgets to the game viewport, which matches this project's
existing menu. The journal reuses that path and avoids binary UI assets.

**Verification.** Both Development targets compile with adaptive unity disabled.
All 68 automation tests pass, including five survey tests covering direction
math, invalid coordinates, duplicate IDs, discovery eligibility, destroyed
targets, world isolation and both off switches. The final package completed in 1m56s.

The packaged survey smoke passes at 1920×1080 and 1280×720. It checks rebound
input through Enhanced Input, paused Slate closing, tracking with saving off,
preserved discoveries and both off switches. Enter closes the journal even
when a Track button has keyboard focus, without activating that button. Neither
run writes a checkpoint. All ten screenshots were inspected: empty and surveyed
rosters, tracking HUD, the seven-button pause menu and Gameplay settings at both
resolutions. The wrapper's ten failure-handling cases pass under PowerShell 7
and 5.1.

A final packaged input regression also verifies Enter opens the journal from
the focused pause-root Settings button without activating it, then closes from
a focused Track button. Evidence: `SurveySmoke-8195b21ebafe4335ade955003215b408.log`.

The four-phase progress regression passes with the journal installed, and the
ordinary packaged walk smoke moves the player 23.03 m through real input.

Local evidence is under `Saved/Logs/Survey*` and `Saved/Screenshots/SurveySmoke/`:
`888dcf14e22a4bf79c2fc541353ae607` (1080p) and
`5b96b766012240e3ab40d8ffd1ed4cd1` (720p).

## Next candidates

- Optional autosaving with its own switch and explicit write-completion handling.
- Sleep until a chosen hour, advancing the shared town/player ledger together.
