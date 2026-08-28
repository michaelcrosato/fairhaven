# Fairhaven v0.1 playtest guide

This is the first playable foundation. It is a **place to walk around**, not a
game: there is no objective, no progression and no failure state. What is being
tested is whether the world reads at the right scale, whether it feels good to
move through, and whether the technical foundation is worth building on.

Expect 10–20 minutes to see everything worth seeing.

## Launch

```
LocalBuilds\Windows-Development\UEGT2\Binaries\Win64\UEGT2.exe
```

Run that exe directly rather than the one in the archive root — Windows
Application Control blocks the launcher stub on some machines.

You will land on the main menu, shown over a live view of the town. Choose
**Play**.

## Controls

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Move | W A S D | Left stick |
| Look | Mouse | Right stick |
| Jump | Space | Bottom face button |
| Sprint | Left Shift (hold) | Left stick click |
| Crouch | Left Ctrl (toggle) | Right face button |
| Interact | E | Left face button |
| Menu / pause | Escape | Start |
| Diagnostics overlay | F3 | — |

Every one of those can be rebound in **Settings → Controls**. Sprint can be made
a toggle in the same place.

Useful console commands (backtick to open the console):

| Command | Effect |
|---|---|
| `uegt2.Time 18.5` | Set the time of day in hours (sunrise 6, sunset 18) |
| `uegt2.TimeSpeed 4` | Real minutes per full day cycle; `0` freezes the sun |
| `uegt2.Debug.DrawInteraction 1` | Draw the interaction probe |
| `stat fps` / `stat unit` | Frame timing |

## A suggested route

You start just off the town square, facing east toward the harbour.

1. **Town square** — the well, the market stalls, and the crowd. Try `E` on a
   crate: you pick it up, carry it, and `E` again throws it. Open a house door.
   Try `E` on a villager: they answer, and the HUD tells you what they are
   actually doing. Stand still for a minute and read what people say as they set
   off somewhere.
2. **East to the harbour** (about 300 m) — docks, boats, the beach. Walk into
   the sea: you should start swimming at chest depth.
3. **North-east to the lighthouse** — the tallest landmark on the coast. Survey
   it with `E`.
4. **North up the High Road** — follow the river valley into the mountains.
   Check the bridge where the road crosses the river.
5. **West to the farmland** — hedged fields, wheat, barns, scarecrows, the
   windmill, and a pond.
6. **South** — the land gets warmer: palms, ferns, jungle trees, and a lagoon.

There are 7 survey landmarks. Each shows a count when you use it.

## What to look at

**Scale.** Does a house feel house-sized next to you? Do the villagers read as
people? Is the walk from town to the coast the right length, or too long/short?

**The town's day.** This is the main new question. Escape → Dev Mode → World →
Time of Day, and drag it. Half four in the morning should be a fisher walking to
the boats and nobody else; nine should be a working town; six in the evening
should be a full square; one in the morning should be empty except for the
constable and a light in the bakery. Does it read as a town keeping hours, or as
actors hitting marks? Dev Mode → Life → **Show Plans** tells you what each of
them thinks it is doing, and why.

**Crowds.** Does the market read as a market rather than a queue? Is anybody
standing on a roof or in the air? (`F3`, or the `LogUEGT2NPC` population report
in the log, will say.) Newhaven's civic square fills at lunchtime and its
avenues are quieter - that is intended, but say if the balance feels wrong.

**The bubbles.** Do they arrive often enough to notice and rarely enough to be
worth reading? Do people say what they then visibly go and do? Settings →
Gameplay turns them off if they are too much.

**Movement.** Walk, sprint, crouch, jump. Does the head bob feel right (it is
adjustable in Settings → Gameplay)? Can you climb the terrain you expect to, and
are you correctly stopped by the terrain you expect to be stopped by?

**Visual direction.** This is the main question for 0.1: does "stylised low-poly
objects in a believable natural world" actually land? Look at the treeline from a
distance, the town silhouette, the mountains, the water edge. Try
`uegt2.Time 7` and `uegt2.Time 19` for low sun.

**Transitions.** Walk from farmland into forest into town into beach. The brief
asked for regions that blend rather than feel like separate test maps — judge
that on foot, not from the map.

**Performance.** Press F3. On an RTX 3060-class GPU at 1080p the target is a
comfortable 60. Note where it dips: dense forest, the town, or looking down from
the mountains.

**Settings.** Change quality levels, resolution scale, FOV, volumes, and a key
binding. Quit and relaunch: everything should persist.

## Known limits in 0.1

These are known. Reporting them again costs you time.

- **No gameplay.** No objectives, inventory, combat or saving of world state.
  Only settings persist. The inhabitants keep a routine and react to the hour,
  the weather, the day and to you, but there is nothing to *do* with them beyond
  talking; nothing you do changes their day.
- **Water is a stylised surface, not the Water plugin.** No waves, no buoyancy,
  no underwater post-process. Swimming works because there is a physics volume
  under the sea plane. The river is a flat ribbon.
- **The windmill sails do not turn**, and there is no skeletal animation
  anywhere. A walking figure is a static mesh with a bob and a sway. That is
  deliberate for this milestone.
- **You walk through people.** NPC collision is query-only, on purpose: the
  player starts in the busiest part of town and a solid crowd there means getting
  wedged.
- **Nobody goes inside.** There are no interiors, so sleeping and eating at home
  are modelled by the inhabitant standing on their own doorstep and vanishing.
- **Interiors do not exist.** Doors swing open onto a solid wall.
- **Vegetation pops** at its cull distance rather than fading.
- **The town is one density everywhere** — no separate districts, and building
  placement is a simple street-side rule with overlap rejection.
- **No level streaming.** The whole 2 km map is one level, loaded at once.
  Loading takes a few seconds and memory is a few hundred MB.
- **Landmark discovery does not persist** between sessions.
- **Ambient audio is generated and simple.** It is meant to prove the mix and
  the settings plumbing, not to be final.
- **The asset showcase** (a grid of every mesh, in the farmland at roughly
  X +27,000 / Y −53,000) is a development aid that is still in the map. Say if
  you would rather it were not.

## Reporting back

The most useful things to send:

1. Your GPU, and the frame rate you saw in the busiest places (F3).
2. Anywhere you got stuck, fell through, or could not climb something you should
   have been able to.
3. The first moment the world stopped feeling believable, and why.
4. Whether the scale of the town and the distances between regions feel right.
5. Which of the four outer regions is weakest, and what it needs.
