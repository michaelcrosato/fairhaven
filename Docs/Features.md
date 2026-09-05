# Feature log

Each feature has a stable ID, a player control where useful, and an independent
maintainer switch. Turning a feature off must leave the baseline game playable
and preserve any data the player may want again later. New entries record the
behavior, off path, compatibility and the checks actually run.

All player controls below are in **Settings → Gameplay**. Maintainer switches
live in `Config/DefaultGame.ini`; set the named property to `False` under its
`[/Script/UEGT2.<class>]` section. Each entry below has the exact config block.

| Feature | Player control | Maintainer class and property |
|---|---|---|
| [F001 · Player progress](#f001--player-progress) | Save Progress | `UEGT2ProgressSubsystem.bFeatureEnabled` |
| [F002 · Survey journal](#f002--survey-journal-and-directions) | Survey Journal | `UEGT2SurveySubsystem.bFeatureEnabled` |
| [F003 · Sleep until](#f003--sleep-until-a-chosen-hour) | Sleep Until | `UEGT2RestSubsystem.bFeatureEnabled` |
| [F004 · Optional autosave](#f004--optional-autosave) | Autosave | `UEGT2AutosaveSubsystem.bFeatureEnabled` |
| [F005 · HUD size](#f005--hud-size) | HUD Size → Normal | `UEGT2HUD.bHudScalingEnabled` |
| [F006 · Optional auto-walk](#f006--optional-auto-walk) | Auto-walk Control | `UEGT2Character.bAutoWalkFeatureEnabled` |
| [F007 · Nearby services](#f007--nearby-services) | Nearby Services | `UEGT2ServicesSubsystem.bFeatureEnabled` |
| [F008 · Town survey contract](#f008--town-survey-contract) | Town Survey Contract | `UEGT2SurveyContractSubsystem.bFeatureEnabled` |

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

**Data contract.** `UUEGT2ProgressSave` carries schema version 2 (since F008), content revision
1 and map identity. A fixed header, payload length and checksum reject damaged
bytes before Unreal decodes variable-length fields. The single logical checkpoint
rotates between two physical slots so a failed write can retain the previous
valid checkpoint. The files are `Fairhaven_Journey_A.sav` and
`Fairhaven_Journey_B.sav` under Unreal's `Saved/SaveGames` directory. Landmark
identity comes from explicit IDs in the generated content, not actor names or
translated display text. Change the content revision when geography or those
IDs become incompatible with saved positions and discoveries.
Schema-1 checkpoints migrate with an unpaid town survey contract. Schema 2 stores
its paid flag alongside the matching purse and discoveries; see F008 below.

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

## F005 — HUD size

Status: implemented and verified on `feature/hud-size`, 2026-09-05.

**What it adds.** Larger in-game labels, needs bars, directions, messages and
speech bubbles. Text, padding and panel geometry scale together while panels
remain anchored to the viewport. Larger choices fit the available space and
wrap long text. The aiming reticle, dev diagnostics and Slate menus retain their
existing size.

**Player switch.** Settings → Gameplay → HUD Size. Normal (100%) is the default;
Large (125%) and Larger (150%) opt in. Selecting Normal restores the original
layout. `HudSizeLevel=0`, `1` or `2` in
`[/Script/UEGT2.UEGT2GameUserSettings]` stores the choice. Recommended Defaults
returns to Normal.

**Maintainer switch.** In `Config/DefaultGame.ini`:

```ini
[/Script/UEGT2.UEGT2HUD]
bHudScalingEnabled=False
```

Off restores the original size and disables the settings row while retaining
the player's choice. Set it back to `True` to allow larger sizes. This feature
does not change checkpoints, world content, input bindings or needs arithmetic.

**Implementation.** The Canvas HUD uses a shared scale and viewport layout
helper. Text measurements and drawing use matching font scales. World-projected
speech positions remain in physical screen coordinates; panel offsets, bars,
arrows and bubble tails scale around their anchors. Normal and hard-off use the
baseline geometry; viewport fitting applies to the larger choices.

**Research.** Epic's [HUD API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/AHUD)
exposes explicit text scale and position-scaling controls. Its
[DPI guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/dpi-scaling-in-unreal-engine)
describes automatic UI scaling from viewport dimensions. The installed engine
routes Fairhaven's Slate menus through that DPI system, while Canvas text uses
its explicit draw scale. The HUD control therefore leaves menu DPI unchanged.

**Verification.** Both Development targets compile with adaptive unity disabled.
All 90 automation tests pass, including four HUD layout tests and one settings
test. They cover baseline geometry, scaled text measurement, bottom panel and
message clearance, crowded speech bubbles, invalid layout inputs, config
clamping and recommended defaults. The final package completed in 2m29s.

The packaged HUD smoke passes at 1920×1080 and 1280×720. Each run captures
Normal, Large, Larger and hard-off in the same paused scene with real interaction
prompts, speech, needs, survey tracking and messages. It checks the exact camera,
player and speaker positions, world time, calendar, needs and 137.625 coins
throughout. Hard-off restores 100% while retaining the 150% preference. A fifth
image verifies the actual HUD Size settings row and its visible geometry. All ten
images were inspected: the larger HUD remains readable and fits at both sizes,
and Normal and hard-off preserve the baseline layout. No checkpoint is written.

The wrapper's eleven success/failure cases pass under PowerShell 7 and 5.1.
The packaged survey regression passes its rebound-key, Slate navigation, tracking
and both off-switch checks; its settings screenshot still shows Survey Journal.
All three autosave regression phases pass. The ordinary walk smoke moves the
player 23.03 m through real input. Existing engine and ground-fixture warnings
remain documented in [Audit.md](Audit.md).

Local evidence uses `Saved/Logs/HudSize*`. Screenshots are under
`Saved/Screenshots/HudSizeSmoke/36b97824f6664c30b6502d1629fa7ea0/` (1080p) and
`Saved/Screenshots/HudSizeSmoke/c4d983822e804c66bd3d476a2e7db59b/` (720p).

## F006 — Optional auto-walk

Status: implemented and verified on 2026-09-05, on `feature/optional-auto-walk`.

**What it adds.** Toggle ordinary forward walking without holding a movement
key or stick. Looking steers the player; collision, walking speed and fatigue
still apply. Press the toggle again or use a movement key or stick to stop.
Jump, sprint, crouch, interaction, menus, dialogue, the console and window focus
loss also cancel it. Leaving ground movement or teleporting cancels it; landing, returning
to the game and loading a checkpoint do not resume it. The player chooses the
route and watches for water and ledges.

**Player switch.** Settings → Gameplay → Auto-walk Control. Default Off;
`bAutoWalkEnabled=False` in `[/Script/UEGT2.UEGT2GameUserSettings]`.
When enabled, **V** or right-stick click toggles walking. Settings → Controls
can rebind the keyboard action. An active HUD cue shows the stop binding even
when the needs panel is hidden, and follows the HUD Size preference.

**Maintainer switch.** In `Config/DefaultGame.ini`:

```ini
[/Script/UEGT2.UEGT2Character]
bAutoWalkFeatureEnabled=False
```

Off cancels assistance and disables its settings row while keeping the player's
preference. Set it back to `True` to permit player opt-in. Neither enabling the
switch nor restoring defaults starts movement.

**Implementation.** The character owns transient active state. The controller
applies forward input after normal input and view updates, before movement.
Cancellation clears assistance without changing ordinary inactive controls.
The input action also requires a fresh physical press: a repeat reconciled by
the engine after an input flush cannot restart walking when a menu closes.
State changes use `LogUEGT2Player`. There is no pathfinding, additional ticking
subsystem, world-content change or checkpoint schema change.

**Research.** Epic's [Enhanced Input context options](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/EnhancedInput/FModifyContextOptions)
describe suppressing held keys during mapping rebuilds, and its
[controller focus API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/APlayerController/ShouldFlushKeysWhenViewportFocus-)
documents the key flush on viewport focus loss. The installed engine processes
controller input and view rotation before character movement. These behaviors
determine where assisted input is added and where it must be cancelled.

**Verification.** Both Development targets compile with adaptive unity disabled.
All 95 automation tests pass, including four auto-walk tests and a checkpoint
integration test. They exercise actual Enhanced Input mapping and delegates,
rebound keyboard and gamepad controls, small stick drift, manual input priority,
quick taps, held keys across menus and focus flushes, simulated input rejection,
view updates, physical ground movement, shared fatigue, movement modes, teleports,
venue occupancy and both off switches. The checkpoint test saves after assisted
walking, restores while another walk is active and confirms landing adds no
movement. Recommended Defaults restores player opt-out.

The packaged wrapper's eleven success/failure cases pass under PowerShell 7
and 5.1. The package completed in 2m18s. Packaged smoke passes at 1920×1080 and
1280×720 through real keyboard and gamepad input: rebound forward movement,
exactly 90 degrees of view steering, manual takeover, held keys across menus,
focus flushes and the console, and ordinary manual movement with either switch
off. All six screenshots were inspected, including Normal and Larger active
cues with needs hidden and the actual settings row. No checkpoint is written.

The packaged bed regression starts auto-walk before the real interaction probe,
then confirms that opening Sleep Until, cancelling and waking leave no assisted
movement or queued input. Its 1,188-inhabitant calendar advance and exact
137.625-coin checks pass. All four manual checkpoint phases, all three autosave
phases, survey input/tracking, and the 720p HUD comparison pass. The ordinary
packaged walk smoke moves the player 23.01 m. The survey and all five HUD
regression images were inspected. Existing engine and ground-fixture warnings
remain documented in [Audit.md](Audit.md).

Local evidence uses `Saved/Logs/AutoWalk*`. Auto-walk screenshots are under
`Saved/Screenshots/AutoWalkSmoke/da983d227f9e4f1e93aa5970ebbbc587/` (1080p) and
`Saved/Screenshots/AutoWalkSmoke/0f81006076274b128ad9c8aff6b578b2/` (720p).

## F007 — Nearby services

Status: implemented and verified on 2026-09-05, on `feature/nearby-services`.

**What it adds.** Pause → Nearby Services lists the nearest food counter,
washroom, seat, paid work, home kitchen and bed. Each row shows the real venue,
horizontal distance and service rate. Free food at home and sleep have their own
rows, so a nearby commercial venue cannot hide them. Missing categories say no
place was found. Work shows the offered trade and wage; opening or tracking a
row never changes the player's trade or purse.

Track a place, then Resume to follow its direction arrow. Selecting a service
replaces landmark directions; selecting a surveyed landmark replaces service
directions. The selected place stays fixed while walking. Directions show a
straight line; the player chooses the route and uses the ordinary interaction
prompt on reaching the place. Nearby indicates distance, not accessibility.

**Player switch.** Settings → Gameplay → Nearby Services. Default On;
`bNearbyServicesEnabled=True` in `[/Script/UEGT2.UEGT2GameUserSettings]`.
The guide is opened explicitly and adds no HUD until the player tracks a place.
Turning it off clears service tracking and hides the Pause action.

**Maintainer switch.** In `Config/DefaultGame.ini`:

```ini
[/Script/UEGT2.UEGT2ServicesSubsystem]
bFeatureEnabled=False
```

Off disables the guide's settings row while retaining the player preference.
Ordinary amenities, landmark discovery and the journal remain available under
their own controls. Set the gate back to `True` to allow the guide again;
directions require a new selection.

**Implementation.** A world subsystem scans live amenity actors once when the
page opens and keeps one nearest candidate per category. Rates come from the
shared life ledger. Active guidance validates one weak actor; it has no ticking
subsystem, repeated actor scan, pathfinding, content rebuild or disk work.
Destroyed or reconfigured targets are dropped. Service tracking is transient:
it is lost with the world and is never stored in either checkpoint channel.
A same-world Continue retains a still-valid current target, as the journal does;
loading a checkpoint cannot restore an old target. Opening the guide cancels
auto-walk through the normal Pause path.

**Research.** The generated world already authors invisible amenities at the
town's real service anchors. Their runtime getters provide venue, activity and
hiring trade; no editor labels or second price table are needed. Epic's
[world subsystem API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UWorldSubsystem)
ties a subsystem to its world, and its
[object pointer guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/object-pointers-in-unreal-engine)
describes weak references for objects that may disappear. Those lifetimes fit
session-only directions to generated actors.

**Verification.** Both Development targets compile with adaptive unity disabled.
All 100 automation tests pass, including four service tests and a checkpoint
integration test. They cover nearest categories and real ledger rates, ties,
missing and invalid actors, explicit refresh, fixed target identity, world
isolation, reconfiguration, destruction, invalid directions, both handoff orders,
failed selections and off/on retirement. Exact needs, fractional coins, trade
and discoveries remain unchanged by guide operations. The checkpoint test
confirms current tracking survives same-world Continue, cleared tracking stays
cleared, and failed restore changes neither the target nor life.

The package completed in 1m32s after fixing UAT's shared-build-mutex handling
([Audit.md](Audit.md#shared-build-mutex-2026-09-05)). The isolated packaged guide
smoke passes at 1920×1080 and 1280×720. From the live start it finds the Bakehouse,
washroom, seat, paid work at the Solicitor, home kitchen and bed. Real Pause and
D-pad input select Food at home, retain focus on Tracking and Resume normally.
The real interaction probe starts and stops the free larder and paid workplace;
the latter adopts its offered trade. Both off gates preserve ordinary larder
use, discoveries and the exact 36.625-coin baseline. Opening Pause cancels
auto-walk. All eight guide, Normal/Larger HUD and settings images were inspected;
the selected row and controls remain readable, with scrolling for other rows.
The wrapper's eleven success/failure cases pass in PowerShell 7 and 5.1.

Packaged regressions pass for auto-walk, survey input/tracking, all HUD sizes at
720p, sleep with 1,188 inhabitants, all four manual-checkpoint phases, all three
autosave phases and an ordinary 23.02 m walk. Existing engine, distant-ground
and ground-fixture warnings remain documented in [Audit.md](Audit.md).

Local evidence uses `Saved/Logs/Services*`. Guide screenshots are under
`Saved/Screenshots/ServicesSmoke/6afb7376a84e45dd9b86361e18c5fca6/` (1080p) and
`Saved/Screenshots/ServicesSmoke/30247e01fd154ab88b6f999994a4ab36/` (720p).

## F008 — Town survey contract

Status: implemented and verified on `feature/town-survey-contract`, 2026-09-05.

**What it adds.** Read the generated signpost near Fairhaven Square. Its paused
page asks for surveys of The Harbour (`fairhaven_harbour`), Fairhaven Light
(`fairhaven_light`) and Mill Rise (`mill_rise`). Use the ordinary markers, then
return to the signpost and choose Claim Payment. Existing discoveries count;
there is no acceptance flag or deadline. The three rows show survey status and
current straight-line distances and compass directions. Opening the page leaves
journal/service tracking alone. Resume initially has focus.

Payment is two Courier Errand world hours from the shared wage table: currently
18 coins, once per journey. It changes neither the player's trade nor elapsed
time, needs or activity. Repeated claims cannot pay again. The signpost is a new
native interactable using the existing sign mesh, generated by the gameplay
stage; no hand-authored map or new binary UI asset is needed.

**Player switch.** Settings → Gameplay → Town Survey Contract, default On;
`bTownSurveyContractEnabled` in `[/Script/UEGT2.UEGT2GameUserSettings]`.
Turning it off leaves a readable unavailable notice and preserves discoveries
and paid state. Ordinary surveys, paid work and other features remain usable.

**Maintainer switch.** In `Config/DefaultGame.ini`:

```ini
[/Script/UEGT2.UEGT2SurveyContractSubsystem]
bFeatureEnabled=False
```

Set `True` to enable it again. The world subsystem exists while disabled so
checkpoints can still capture and restore payment state.

**Persistence.** Schema 2 adds `bTownSurveyContractPaid` beside the existing purse
and discovery snapshot. Paid checkpoints require all three IDs in that same
snapshot. Both manual and automatic checkpoints use this contract. Loading
replaces the flag and purse together after validation; it never awards a reward.
Loading a checkpoint from before payment rolls both back. New Visit resets the
contract. With Save Progress off, payment lasts for the current visit only.

Content revision remains 1: the eleven landmark IDs and geography are unchanged,
and the additional sign has query-only collision. Schema-1 files migrate with
unpaid state. Old files often omit the version fields because Unreal skipped
default values, so loading starts from the legacy defaults and new serialization
writes every snapshot property explicitly. Unknown schema versions still fail.
`Tests/Fixtures/ProgressSchema1.h` contains genuine old packaged bytes as text,
exported before the schema change, with their source commit and checksum.

**Implementation.** `Contracts/UEGT2SurveyContractSubsystem` has no tick, timer
or IO. Explicit page/claim requests resolve three unique live landmarks. A claim
validates the local possessed player, nearby live signpost, paused page ownership,
discoveries, unpaid state and a bounded purse before applying one credit and
setting paid, with no delegate or latent work between them. `UEGT2TryCredit` in
the shared life ledger owns one-off currency arithmetic. Ordinary float rounding
up to 0.001 coin is accepted; a purse at its existing cap, or too large to
represent the reward within that tolerance, leaves payment unclaimed.
`LogUEGT2Contracts`
records page and claim outcomes; settings logs record the switch.

**Research.** The existing durable landmark IDs made a fixed survey objective
possible without adding inventory or persistent NPC identity. Epic's
[SaveGame guide](https://dev.epicgames.com/documentation/unreal-engine/saving-and-loading-your-game-in-unreal-engine)
supports custom per-journey snapshots. The installed UE 5.8 implementation of
`GameplayStatics::SaveGameToMemory`/`LoadGameFromMemory` and
`UStruct::SerializeTaggedProperties`, together with the genuine old fixture,
established the missing-default migration requirement; the
[archive API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/FArchiveState)
documents delta property serialization. The reward is a design choice derived
from the existing Courier wage, not a claim about measured route completion time.

**Verification.** Both Development targets compile with adaptive unity disabled
and no C++ warnings. All 110 automation tests pass. The ten new tests cover the
shared credit, real board/page transaction, malformed identities and observers,
gates, paired manual/autosave restoration, genuine old-schema migration, strict
diagnostic slot isolation and generated sign clearance. The new viewport fixture
uses UE 5.8's owned overlay API. Existing ground-fixture warnings are unchanged.

The gameplay stage rebuilt the sign at `(-1200,-2800,1549)` cm without Python
warnings; its two standing approaches pass real capsule and Visibility queries.
The 11 Python pipeline tests and 400 mesh checks pass. Packaging completed in
2m03s at 1086.1 MB without cook warnings or errors.

`Scripts/Smoke-Contract.ps1 -Capture` passes at 1920×1080 and 1280×720. The real
interaction probe surveys all three markers and opens the sign's paused page;
natural gamepad Resume, Right and Claim controls pay 137.625 → 155.625 coins.
Needs, trade and calendar remain unchanged. Repeated claims refuse payment.
Separate Read, New Visit and Disabled processes prove persisted paid state,
reset on a new world, ordinary disabled-sign interaction and preserved save
bytes. Both gates retain payment. All ten sign, unfinished/ready/paid page and
hard-off Gameplay setting captures were inspected: rows, buttons and the
retained-preference explanation fit at both resolutions. The wrapper's twelve
success/failure cases pass under PowerShell 7 and 5.1.

A separate new packaged process loaded the genuine schema-1 checkpoint exported
from `5e9fd2a`, restoring its exact purse, needs, trade, view, calendar and survey,
then continuing the live ledger. Original checkpoint bytes stayed unchanged.
The isolated Read helper's nine failure-handling cases pass in both shells.

Packaged regressions pass for all four manual-save phases, all three autosave
phases, nearby services, sleep through 24 hours with 1,188 inhabitants,
auto-walk, survey controls, HUD sizes at 720p and an ordinary 23.04 m walk.
Four additional service screenshots show no layout regression. Existing engine
and distant-ground warnings remain documented in [Audit.md](Audit.md).

The contract smoke proves the actual interactions and state transitions by
positioning near each marker. It does not measure a continuous walking route,
travel time or reward balance. The sign uses the same plain mesh as the other
town signs and identifies the contract through its nearby interaction prompt.

Local evidence is under `Saved/Logs/Contract*`, with legacy Read run
`6ab507dcc76d444db1a8de7c0e524169`. Contract images are under
`Saved/Screenshots/ContractSmoke/987225c1b88c4acc9de98e14be651ea7/` (1080p) and
`Saved/Screenshots/ContractSmoke/ae4f20f47fd14e60aa1ee755e94432e1/` (720p).

**Subsequent walking audit.** `Scripts/Smoke-ContractWalk.ps1` now verifies a
continuous 2.46 km board → harbour → lighthouse → mill → board journey with
ordinary input and collision, both bridge crossings, live needs/calendar and
the native 36 → 54 coin claim. It took 11m11s including startup and board
handling, or 13.226 world hours. Food, relief and company reached zero before
return; Energy ended at 0.1985. The reward remains a design choice, with the
trip cost now measured. The audit also repaired blocked street junctions and
missing optician/bank work amenities. See [Audit.md](Audit.md#ordinary-survey-circuit-and-street-junctions)
for the route, limits and verification. The diagnostic is inactive without its
flag and its wrapper isolates the run from player data.
