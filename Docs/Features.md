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

## Next candidates

- A survey journal with directions to discovered landmarks, using the stable
  IDs and durable discoveries above.
- Optional autosaving with its own switch and explicit write-completion handling.
- Sleep until a chosen hour, advancing the shared town/player ledger together.
