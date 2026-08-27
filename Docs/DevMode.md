# Dev Mode and the day/night cycle

Dev Mode is a free-camera / world-control layer for looking at Fairhaven quickly:
fly to a viewpoint 40 km away, push the sun to golden hour, drop the weather into
a storm, and read your exact coordinates off the HUD. Open it from **Escape ->
Dev Mode**.

Everything here is runtime state. Nothing in this feature is stored in the map,
so a `Build-Content.ps1` run cannot destroy it.

## Opening it

| Where | How |
|---|---|
| Menu | Escape -> **Dev Mode** (also on the main menu) |
| Console | `uegt2.Dev 1` |

The menu page has four tabs: **Player**, **World**, **Display**, **Teleport**.

Turning Dev Mode off restores normal walking, collision, jump count and game
speed. It deliberately leaves the time of day and weather where you left them —
those are world state, not player state, and resetting them under you is
surprising.

## Player tab

| Control | Effect |
|---|---|
| God Mode | `SetCanBeDamaged(false)` and unlimited air jumps |
| Fly | Free 3D flight along the camera direction |
| Noclip | Fly **and** pass through geometry (capsule collision off) |
| Speed | 1x - 50x multiplier on walk, sprint, swim and fly speed |

Noclip implies flying: selecting it turns Fly on, and clearing Fly clears Noclip.
That is why the tab shows them as separate toggles rather than one three-way
choice — you can fly *with* collision, which is the useful mode for following
terrain.

**God Mode is half forward-looking.** Fairhaven has no damage, health, drowning
or fall-damage system yet, so `SetCanBeDamaged(false)` protects against nothing
today — it is there so the toggle keeps meaning what it says the moment a hazard
exists. The observable half is the unlimited air jumps: hold Space and you climb
the mountains a jump at a time.

### Flying controls

| Action | Key |
|---|---|
| Forward / back / strafe | W A S D, along where you are looking |
| Ascend | Space (hold) |
| Descend | Left Ctrl (hold) |
| Faster | Left Shift (hold) — sprint still multiplies on top of the speed slider |

Flight follows the full camera rotation, so looking up and holding W climbs.

## World tab

| Control | Effect |
|---|---|
| Time of Day | 0-24 hours; drag it and the sun, sky, fog and moon all follow |
| Day/Night Cycle | Master switch for the moving sun |
| Day Length | Real minutes for a full 24 h cycle, 1-120 |
| Weather | Clear, Cloudy, Overcast, Foggy, Storm |
| Fog Density | Manual override on top of the weather preset |
| Game Speed | 0.1x - 5x global time dilation (slomo) |

## Display tab

Diagnostics overlay, wireframe, unlit, collision view, `stat fps`, `stat unit`,
and the interaction probe draw. These map onto engine view modes and stat
commands; they are here so you do not have to remember the console names.

## Teleport tab

Buttons for every tour viewpoint (the same list
`UUEGT2CaptureSubsystem::GetTour()` uses for screenshots), plus **Save Position**
/ **Restore Position** for a scratch bookmark, and a live coordinate readout.

Teleporting traces down from high above the target and places you 2 m above the
ground, so you never arrive inside terrain.

## The day/night cycle

`AUEGT2SkyController` already drove sun angle, colour, sky light and fog from a
single `TimeOfDay`. The cycle adds:

- **A moon.** A second directional light, spawned at runtime by the sky
  controller if the level has none. It rises as the sun sets, tracks the
  opposite side of the sky, and is the only meaningful light between dusk and
  dawn.
- **Real night.** Below the horizon the sun is switched off rather than left at
  a 2% floor, and the sky light drops to a cool blue night ambient instead of a
  dim grey day.
- **Dawn and dusk shaping.** Sun colour and intensity are driven off elevation
  through a smoothstep so the transition reads as a sunrise rather than a fade.
- **A moving exposure window.** Without this the rest of it is invisible - see
  below.

### Why the sky controller drives auto exposure

`lighting.py` bakes `auto_exposure_min_brightness = 10.5` into the post process
volume, and because `ExtendDefaultLuminanceRange` is on that number is an
**EV100 stop**, not a multiplier. 10.5 EV is daylight. A moonlit scene sits near
EV 0, so a fixed daylight floor clamps the night about ten stops too dark: the
screen renders pure black while the log stays completely clean.

So `ApplySky` lerps the whole exposure window from the day values (10.5-14.5,
exactly what `lighting.py` writes, so noon is untouched) down to the night ones
(5.0-8.0) using the same ambient alpha that drives the sky light. Fixing this by
brightening the moon alone does not work - the clamp is what is wrong, not the
light.

`MoonIntensity` is 20 lux, roughly eighty times a real full moon. That is
deliberate: the exposure floor only buys back so much, and a physically honest
moon still renders black.

### Why it does not just run by default in the map

`Tools/Python/uegt2/lighting.py` serialises `day_length_minutes = 0.0` onto the
placed sky controller, so a C++ default cannot switch the cycle on — the map
value wins on load. Instead `BeginPlay` promotes a zero day length to
`DefaultDayLengthMinutes` when `bDayNightCycleEnabled` is true. The map stays
untouched and `uegt2.TimeSpeed 0` still freezes the sun at runtime.

### Why captures still freeze it

A moving sun would make `Screenshot-Tour.ps1` and `Smoke-Packaged.ps1`
non-reproducible, which is exactly the property `DayLengthMinutes = 0` was
protecting. So `BeginPlay` force-freezes the cycle whenever
`UUEGT2CaptureSubsystem::IsCaptureRequested()` or `IsWalkSmokeRequested()` is
true. Tours and smoke runs see the same frozen 10:30 sun they always did.

## Weather

Weather presets drive the sun, sky light, fog and cloud layer together.
`GetWeatherPreset()` is a pure function over a static table, which is what the
automation tests exercise.

| Preset | Sun | Sky | Fog | Clouds |
|---|---|---|---|---|
| Clear | full, warm | full | 0.012 thin | on |
| Cloudy | 0.75x, cooler | 0.9x | 0.020 | on |
| Overcast | 0.35x, grey | 0.7x | 0.045 | on |
| Foggy | 0.5x, pale | 0.8x | 0.180 dense | on |
| Storm | 0.18x, cold grey | 0.5x | 0.090 | on |

**There is no precipitation.** Rain and snow particles would need a Niagara
system, and every asset in this project is generated by the Python content
pipeline rather than hand-authored — adding one means a new content stage and a
map rebuild. The presets change light, fog and colour only. A `storm` looks like
a storm sky, not like falling rain.

## Console commands

| Command | Effect |
|---|---|
| `uegt2.Dev 1` | Enable / disable dev mode |
| `uegt2.Dev.God 1` | God mode |
| `uegt2.Dev.Fly 1` | Fly |
| `uegt2.Dev.Noclip 1` | Noclip |
| `uegt2.Dev.Speed 12` | Speed multiplier, 1-50 |
| `uegt2.Dev.Teleport Vista` | Teleport to a named tour viewpoint |
| `uegt2.Weather Storm` | Clear / Cloudy / Overcast / Foggy / Storm |
| `uegt2.Time 18.5` | Time of day in hours (existing) |
| `uegt2.TimeSpeed 20` | Real minutes per day, 0 freezes (existing) |

## Rendering a specific sky headlessly

`Screenshot-Tour.ps1` passes its arguments through to the game, so a review shot
at any hour or weather is one flag away. Both flags freeze the cycle, which
keeps the image reproducible.

```powershell
./Scripts/Screenshot-Tour.ps1 -ExtraArgs '-UEGT2Time=22.5'
./Scripts/Screenshot-Tour.ps1 -ExtraArgs '-UEGT2Time=13 -UEGT2Weather=storm'
```

| Flag | Effect |
|---|---|
| `-UEGT2Time=<hours>` | Force the hour, 0-24, and freeze the sun |
| `-UEGT2Weather=<name>` | Force the weather preset |

## Where the code lives

```
Public/Dev/UEGT2DevModeSubsystem.h   world subsystem: owns all dev state,
Private/Dev/UEGT2DevModeSubsystem.cpp   applies it, registers console commands

Public/World/UEGT2Weather.h          weather enum + preset table (pure, tested)
Private/World/UEGT2Weather.cpp

Public/World/UEGT2SkyController.h    sun, moon, sky, fog, clouds, day/night
Private/World/UEGT2SkyController.cpp

Player/UEGT2Character.*              fly, noclip, speed multiplier, god mode
UI/SUEGT2Menu.*                      the Dev Mode page
UI/UEGT2HUD.*                        dev status line and coordinate readout
```

The subsystem owns the state; the character and sky controller own the
behaviour. The menu never touches the character or the sky directly — it reads
and writes the subsystem, which is what keeps the console commands and the menu
in agreement.
