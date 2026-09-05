# Fairhaven v0.1 playtest guide

This is a playable world with exploration, conversation, companions, needs and
paid work. There is no quest objective, progression system or game-over state.
Test whether it feels good to move through, whether the inhabitants' routines
make sense, and whether living alongside them is worth doing.

A short visit can cover the town; allow longer to explore Newhaven, its
interiors and the outer regions.

## Launch

```
LocalBuilds\Windows-Development\UEGT2\Binaries\Win64\UEGT2.exe
```

Run that exe directly rather than the one in the archive root — Windows
Application Control blocks the launcher stub on some machines.

You will land on the main menu, shown over a live view of the town. Choose
**New Visit** to start fresh, or **Continue** to restore your last checkpoint.
When progress saving is disabled, the start button is **Play**.

Pause with Escape and choose **Save Progress** before leaving. This is a manual
checkpoint: quitting does not save automatically. Starting a new visit keeps
the old checkpoint until you explicitly save the new one. Settings → Gameplay
→ Save Progress turns saving and Continue off without deleting existing saves.

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
| Survey Journal | J | Back / View |

The keyboard bindings can be changed in **Settings → Controls**. Sprint can be
made a toggle in the same place. Mouse look and gamepad mappings are fixed.

## You live here

You have the same four needs every inhabitant has, the same purse, and the same
places to answer them. The panel bottom left is your trade, your coin, what you
are doing, and how you are keeping. Turn it off in **Settings → Gameplay**.

Press **E** at any of these. Pressing it again, or walking away, stops.

| Where | What it does | Cost |
|---|---|---|
| A shop doorway or a stall offering food | eat | 5 an hour |
| The eat prompt beside your lodgings | eat at home | free |
| The sleep prompt beside your lodgings | sleep, and it is the fastest way to rest | free |
| A privy or a public convenience | relief | 1 an hour |
| Any bench | sit down | free |
| The inn | a drink: feeds you and keeps you company | 6 an hour |
| The church | sit quietly | free |
| A warehouse, farm, pier, wharf, office or shop | put in a shift | **pays 6-12 an hour** |
| A stall offering work | mind it, as a merchant would | **pays 10 an hour** |

Costs and wages are per world hour and accrue for the time you actually spend.
Taking work changes your trade until you take another job, and the wage with it.
Save Progress keeps your needs, coin and trade for Continue. New Visit resets
them. Talking to
anybody keeps you company while the conversation is open - which is why they
stop to talk too. Ask them how they are off for coin and the answer is their
real purse.

Run out and the counter will not serve you: go and earn. Run yourself into the
ground and you will feel it in your legs long before you read it on the panel.

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
   Try `E` on a villager: the conversation panel shows their activity and needs,
   and lets you ask questions or invite them along. Stand still for a minute
   and read what people say as they set off somewhere.
2. **East to the harbour** (about 300 m) — docks, boats, the beach. Walk into
   the sea: you should start swimming at chest depth.
3. **North-east to the lighthouse** — the tallest landmark on the coast. Survey
   it with `E`.
4. **North up the High Road** — follow the river valley into the mountains.
   Check the bridge where the road crosses the river.
5. **West to the farmland** — hedged fields, wheat, barns, scarecrows, the
   windmill, and a pond.
6. **South** — the land gets warmer: palms, ferns, jungle trees, and a lagoon.
7. **Further south to Newhaven** — visit the civic square, the wharf and a
   shophouse street. Enter an office block or tower and try its stairs.

There are 11 survey landmarks. Each shows a count when you use it. Save Progress
keeps these discoveries with the rest of your checkpoint.

Press **J** or choose **Survey Journal** from Pause to review them. Surveyed
places have a **Track** button. Close the journal and follow the HUD arrow,
compass direction and distance to return; the distance is a straight line,
so choose your own route around water and buildings. **Stop Tracking** removes
the cue. You can turn the journal off separately in Settings → Gameplay, and
use it without saving enabled. The selected target resets on a new visit.

## What to look at

**Scale.** Does a house feel house-sized next to you? Do the villagers read as
people? Is the walk from town to the coast the right length, or too long/short?

**Inside the houses.** Town street houses have working front doors and
furnished interiors. Walk up, press the interact key, and go in.
What to judge:

- Does the room read as somewhere a person lives, or as a box with props in it?
- Is it the right size? A cottage is one room; the bigger houses are two or
  three, and two of the six archetypes have a staircase and bedrooms above.
- Can you get everywhere without fighting the geometry - through the door,
  around the furniture, up the stairs and back down?
- Is it bright enough to read, and does the light look like it comes from the
  fire and the ceiling lamp you can see?
- On a sloping street, does the house still sit on the ground properly from the
  outside, and is the step up to its door climbable?

Dev Mode -> Teleport has four buttons that put you straight inside one:
HouseInterior, HouseUpstairs, CottageInterior and HouseHearth.

**Climb one.** Newhaven's towers, offices, apartments and shophouses have
stacked interiors with stairs leading to an accessible roof or terrace. On a
setback tower, the decorative shaft continues above the terrace you can reach.
What to judge: is the climb worth making, does a stair put you somewhere you
cannot get out of, and can you see well enough on upper floors at night?

**Fairhaven's high street.** Twelve trades, each with a signpost outside saying
what it is: a grocery, a bakehouse, an ironmonger, a draper, a barber, a
physician, a dentist, a spectacle maker, a solicitor, a bookseller, a post
office and a bank. The church is bigger than it was and has a door you can open.

**The almanac.** Top left of the screen: the time, the date and what it is
doing outside, with the temperature. It is not a debug readout - it is how you
tell a wet morning in Thawmoon from a wet evening in Harvest. Walk up the
mountain road and watch the temperature fall; walk south-west into the tropics
and watch it climb. Settings -> Gameplay turns it off, or switches it to
Fahrenheit.

**Talk to people.** Walk up to anyone and press the interact key. A panel opens
with their name, their trade, what they are doing, and four bars showing how
rested, fed, comfortable and sociable they are. Ask them anything: what they are
doing, how they are keeping, whether they are hungry or tired, what they do for
a living, about the town, about what lies beyond it - or ask them to walk with
you, and later to go on without you.

What to judge: does the panel read well and get out of the way? Do the answers
match the bars, and match what they then go and do? Is asking someone to follow
you worth doing? Try it on someone whose bars are already low, and on a dog.

**What people need.** Every inhabitant now carries four needs - energy, hunger,
the bathroom and company - and acts on whichever is worst. Dev Mode -> Life ->
Show Plans reports the reason for what they are doing; look for "Need". Follow
someone for a few minutes of world time and you should see them break off to
eat, to sit down, to find a washroom or to find someone to talk to. What to
judge: does it read as people with lives, or as people twitching between errands?
Are there enough places to do each of those things near where they are?

**Newhaven's ground floors.** Shops, offices, apartments and towers have
ground-floor venues, with businesses distributed by building archetype:
a grocer, a baker, a pharmacy, a hardware shop, a clothier, a
bookshop, a furniture showroom, an electrical shop, a restaurant, a coffee
house, a tavern, a barber, an optometrist, a post office, a gymnasium, a bank,
a solicitor, a doctor, a dentist, a police station, a library, a school, a
museum, offices and apartment lobbies. The city hall has its own civic interior.

Walk a shophouse street and go into three or four in a row. What to judge:

- Can you tell what a shop is from the inside without being told?
- Does the mix feel like a city, or like the same shop repeated?
- Retail is on the outer ring, professional services on the office blocks
  downtown, lobbies in the towers and apartments. Does that read on foot?
- The city hall on the civic square opens too.

**Windows.** Look through the panes from outside and inside a house, including
both storeys. Check the barn, church and warehouse too. Frames should surround
the opening and leave the view clear. Compare the town and city glazing, and
judge how much daylight reaches the room alongside its lamps.

**The town's day.** Escape → Dev Mode → World → Time of Day, and drag it.
Look for fishers heading to the boats before dawn, a working town in the
morning, and more people gathering after work. Bakers and constables keep
different hours, and needs or bad weather can interrupt any routine. Does it
read as a town keeping hours, or as actors hitting marks? Dev Mode → Life →
**Show Plans** tells you what each inhabitant is doing and why.

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
Try Foliage Draw Distance from Low to Ultra and back: vegetation should draw
farther away at higher levels, while fences keep their own distance.

## Known limits in 0.1

These are known. Reporting them again costs you time.

- **No quests, inventory or combat.** Needs, paid work,
  conversation and following are playable. Talking interrupts an inhabitant's
  routine, and inviting them along changes where they go until they leave or
  need something. Manual checkpoints preserve the player's needs, purse, trade,
  surveyed places, position and world calendar. They do not preserve individual
  NPC state, followers, carried props, or changes to doors and lamps. Continue
  resumes on foot with no activity in progress.
- **Water is a stylised surface, not the Water plugin.** No waves, no buoyancy,
  no underwater post-process. Swimming works because there is a physics volume
  under the sea plane. The river is a generated ribbon following the valley.
- **The windmill sails do not turn**, and there is no skeletal animation
  anywhere. A walking figure is a static mesh with a bob and a sway. That is
  deliberate for this milestone.
- **You walk through people.** NPC collision is query-only, on purpose: the
  player starts in the busiest part of town and a solid crowd there means getting
  wedged.
- **NPC home activities are represented at the doorstep.** Sleeping and eating at
  home are still modelled by the inhabitant walking to their own doorstep and
  vanishing. Now that you can follow them in and see the room they are supposed
  to be in, that reads worse than it used to. Walking them through the door and
  sitting them at their own table is the obvious next job.
- **Room lighting is still being tuned.** Town interiors and city ground
  floors have point lights at their ceiling lamps. Window openings are clear
  in the geometry checks; assess the rooms in daylight and at night to judge
  their brightness.
  Upper city floors currently have emissive lamp meshes without those point
  lights. Report floors that are too dark to navigate, especially at night.
- **The kiosks, bus shelters and market stalls are not buildings** and have no
  inside. Neither do the lighthouse or the windmill.
- **Vegetation pops** at its cull distance rather than fading.
- **The town is one density everywhere** — no separate districts, and building
  placement is a simple street-side rule with overlap rejection.
- **No level streaming.** The whole 4.03 km square map is one level, loaded
  at once. Loading time and memory usage depend on the machine and build.
- **Ambient audio is generated and simple.** It is meant to prove the mix and
  the settings plumbing, not to be final.
- **The asset showcase is opt-in.** A normal full content build excludes the
  inspection grid. Developers can add it with `-Stages showcase`; its two tour
  viewpoints show ordinary farmland when the grid is absent.

## Reporting back

The most useful things to send:

1. Your GPU, and the frame rate you saw in the busiest places (F3).
2. Anywhere you got stuck, fell through, or could not climb something you should
   have been able to.
3. The first moment the world stopped feeling believable, and why.
4. Whether the scale of the town and the distances between regions feel right.
5. Which of the four outer regions is weakest, and what it needs.
