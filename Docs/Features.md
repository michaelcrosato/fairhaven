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
player explicitly saves the new visit. F004 adds optional autosaves in separate
slots; it never replaces this manual checkpoint.

Restoring ends transient activity and resumes on foot. Conversations, followers,
carried props, door/lamp state and individual NPC state are not checkpointed;
the town resumes its routines at the restored calendar time. An obstructed saved
position uses a safe placement fallback. Invalid or incompatible data must not
partially change the running session.

**Player switch.** Settings → Gameplay → Save Progress. It defaults to On.
Turning it off prevents new checkpoint IO and retains existing saves. An F004
write already submitted to disk may finish. The persisted preference is
`bSaveProgressEnabled` in the
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

## F003 — Sleep until a chosen hour

Status: implemented and verified on `feature/sleep-until`, 2026-09-05.

**What it adds.** Use the existing bed beside the lodgings to choose a wake hour.
The paused panel defaults to 06:00, offers all 24 whole hours, and shows the
duration and resulting date before committing. The next occurrence is used:
choosing the current exact hour means sleeping for 24 hours. Cancel or Escape
returns to play without advancing time.

Sleep restores energy through the shared life ledger. Hunger, relief and
company still decline, and the player earns no coins while asleep. The town
continues its routines, including meals, wages, animals and weekday habits.
Weather stays at its current preset. Waking leaves the player at the same
position, ends transient activity and releases any carried prop. Sleeping does
not request a checkpoint; F001 can save the resulting state normally. F004's
interval counts play time, so skipped hours do not accelerate autosaving.

**Player switch.** Settings → Gameplay → Sleep Until. Default On;
`bSleepUntilEnabled` in `[/Script/UEGT2.UEGT2GameUserSettings]`.
Turning it off restores ordinary bed sleep, which runs continuously until the
player gets up or feels rested. Existing checkpoints remain unchanged.

**Maintainer switch.** In `Config/DefaultGame.ini`:

```ini
[/Script/UEGT2.UEGT2RestSubsystem]
bFeatureEnabled=False
```

Set it back to `True` to enable it. The player cannot override this gate. Standard
captures, walk smoke and flight soak retain ordinary sleep automatically.
Chosen wake times require a live clock, running schedules and a standing player
within the bed's use range. Frozen or unready worlds cannot skip time.

**Implementation.** `Rest/UEGT2RestSubsystem` validates the bed and player, then
coordinates a bounded interval with the NPC director. The director prepares
candidate life states before applying them, settles pending live time once,
advances schedules in steps of at most one world minute, and resets each
inhabitant's elapsed-time baseline. Density-suppressed inhabitants retain the
existing rule of no needs or money advancement. Final placement suppresses
proximity greetings and performs no discarded route search. `LogUEGT2Rest`
records panel and sleep outcomes. No map, mesh or save schema changes are needed.

**Research.** Epic's [frame timing API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FGameTime)
distinguishes paused world time from real time, and its [ticking guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-ticking-in-unreal-engine)
describes the frame phases that include physics and timers. The implementation
therefore advances Fairhaven's calendar and life ledger explicitly; it does not
fast-forward Unreal's frame clock. The repository's `RestoreCalendar` intentionally
adds no elapsed life, so sleep uses a separate interval operation.

**Verification.** Both Development targets compile with adaptive unity disabled.
All 78 automation tests pass, including six rest simulation tests and four rest
service tests. They cover calendar rollovers, scheduled work and meals, animals,
invalid-state atomicity, pending live time, density suppression, paused previews,
bed eligibility, commit guards and disabled-feature fallback. The route-search
counter remains unchanged during final rest placement. The final package completed
in 2m03s.

The packaged rest smoke passes at 1920×1080 and 1280×720. It uses the real bed
interaction probe, checks that Cancel preserves paused state, and navigates from
the initially focused Cancel button through the hour controls to Sleep using
D-pad and A events without assigning button focus. It verifies the displayed
07:00 and 08:00 selections before committing a full day. All 1,188 inhabitants
advance, taking 60.1 ms and 62.2 ms respectively, while the player's 137.625 coins
remain exact. Three seconds of live NPC updates with the clock frozen produce
no deferred or duplicate life charge; the subsequent running clock advances
needs normally. Both off switches restore ordinary bed sleep and getting up.
Neither run writes a checkpoint. All four panel/waking screenshots were inspected.
The wrapper's ten failure-handling cases pass under PowerShell 7 and 5.1.

The four-phase progress regression and packaged survey regression pass. The
ordinary walk smoke moves the player 23.02 m through real input. The ten-minute
flight soak completes 63 legs with zero stalls, a 42.6 ms worst frame and a
3.9 ms longest garbage collection. Its object count stays at 60,190 after the
initial collection; memory peaks at 2,649 MB and ends at 2,605 MB. There are no
hangs, route cycles or errors. Existing Far-tier ground-probe and engine renderer
warnings remain documented in [Audit.md](Audit.md).

Local evidence uses `Saved/Logs/Rest*`. Screenshots are under
`Saved/Screenshots/RestSmoke/746b6af8c9a24445bf9cb3ae7d82b1b4/` (1080p) and
`Saved/Screenshots/RestSmoke/ab73b7c35a6f4030aafc4b608419b2ba/` (720p).

## F004 — Optional autosave

Status: implemented and verified on `feature/optional-autosave`, 2026-09-05.

**What it adds.** Opt in to a separate recovery checkpoint every five minutes
of unpaused play. A due save waits while menus or dialogue are open, during
flight, crouching or a jump, and while the player or world is unready. Failed
attempts remain due and retry at a bounded cadence. New Visit and loading a
checkpoint start a fresh interval, as does changing either persistence
preference. Sleep's skipped calendar hours do not count.
Main → Continue Autosave restores this recovery checkpoint explicitly. Manual
Continue and Save Progress retain their existing behavior and files.

**Player switch.** Settings → Gameplay → Autosave. Default Off;
`bAutosaveEnabled` in `[/Script/UEGT2.UEGT2GameUserSettings]`. Save Progress must
also be On. Turning either preference off prevents new autosave IO and hides
Continue Autosave while keeping the data. A disk write already submitted may
finish; disabling does not delete or roll back a completed write.

**Maintainer switch.** In `Config/DefaultGame.ini`:

```ini
[/Script/UEGT2.UEGT2AutosaveSubsystem]
bFeatureEnabled=False
```

Set it back to `True` to permit player opt-in. The F001 hard gate must also permit
persistence. Standard captures and smoke runs use the existing diagnostic gates;
the dedicated autosave smoke validates an isolated user directory and slot base.

**Data contract.** Autosaves rotate between `Fairhaven_Journey_Auto_A.sav` and
`Fairhaven_Journey_Auto_B.sav`, preserving the last valid recovery point during
a write. They reuse F001's envelope, schema, content revision, validation and
restoration, including its exclusions for transient and individual NPC state.
Manual and automatic sequence numbers are compared within their own pairs.
There is no autosave on quit or immediately on New Visit.

**Implementation.** `Autosave/UEGT2AutosaveSubsystem` owns the active-play interval
and eligibility. The progress game-instance subsystem owns asynchronous byte IO,
so callbacks can safely retire across world changes. Reads, writes and verification
are chained explicitly; a newer visit cannot consume an older operation's result.
The main-menu row reads cached availability and appears without a page rebuild
or focus change. `LogUEGT2Autosave` records requests and completion outcomes.

**Research.** Epic's [saving guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/saving-and-loading-your-game-in-unreal-engine)
recommends asynchronous disk work during play and a completion callback.
The [byte-level save API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/ISaveGameSystem/SaveGameAsync)
allows Fairhaven to preserve its existing integrity envelope. The installed
UE5.8 implementation serializes objects on the game thread, performs file work
on a worker and delivers completion through the game-thread ticker; its task
pipe does not guarantee ordering, so each stage starts the next explicitly.

**Verification.** Both Development targets compile with adaptive unity disabled.
All 85 automation tests pass, including seven autosave tests covering rotation,
corrupt and unreadable files, failed/partial writes, delayed callbacks, shutdown,
world and journey changes, settings changes while paused, availability caching
and recovery, active-play timing and isolated diagnostic arguments. The final
package completed in 2m32s.

The packaged three-phase autosave smoke passes at 1920×1080 and 1280×720.
Each run observes two real periodic writes using its validated two-second test
interval, preserves both manual files byte for byte, and verifies no periodic
write during pause or Main. A separate process loads the older valid autosave
after the newest is corrupted. The existing Main row appears after asynchronous
availability completes without rebuilding the page or changing New Visit focus;
natural gamepad Up and A then restore the exact checkpoint. Player, autosave
maintainer and parent persistence switches preserve both file pairs. All four
Main/settings screenshots were inspected at their native resolutions.

Observed async busy intervals range from 28.8 to 67.9 ms, including frame timing
and worker completion. The worst live frames during the two write waits are
40.2 ms at 1080p and 32.0 ms at 720p; these are not measurements of serialization
alone. The wrapper's eleven success/failure cases pass under PowerShell 7 and
5.1. The four-phase manual progress, survey and rest regressions pass, and the
ordinary walk smoke moves the player 23.02 m through real input.

Local evidence uses `Saved/Logs/Autosave*`. Screenshots are under
`Saved/Screenshots/AutosaveSmoke/f85cc1da1be54df183bc66f5418b8a4c/` (1080p) and
`Saved/Screenshots/AutosaveSmoke/753120f89a4046d7b81e266ecd9476fa/` (720p).
