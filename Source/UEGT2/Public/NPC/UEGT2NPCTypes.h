// Fairhaven (UEGT2) - the vocabulary the whole life system is written in.
//
// Everything here is plain data: enums, small structs and pure functions over
// them. No actors, no world, no engine subsystems. That is deliberate - the
// interesting part of an NPC is the decision, and a decision that only exists
// inside a Tick cannot be tested. UEGT2NPCTests covers this file directly.
//
// The three axes:
//   Role      what a person is, which picks their routine and their voice
//   Species   what an animal is; Person is a species too, so one actor serves both
//   Activity  what anyone is doing right now, which picks their anchor and lines
#pragma once

#include "CoreMinimal.h"
#include "World/UEGT2Weather.h"
#include "UEGT2NPCTypes.generated.h"

/**
 * What a person does for a living. This picks the routine and flavours the
 * speech; it is not a stat block. Animals use EUEGT2NPCSpecies instead and
 * carry Role = Villager, which is never read for them.
 */
UENUM(BlueprintType)
enum class EUEGT2NPCRole : uint8
{
	Villager,      // no particular trade: the body of the town
	Farmer,        // out in the fields from first light
	Fisher,        // tide first, town second
	Merchant,      // runs a market stall
	Baker,         // up before everyone, done by mid afternoon
	Innkeeper,     // opens late, closes later
	Priest,        // the church and the bell
	Smith,         // the forge, loud and hot
	Dockhand,      // cargo, tides and the warehouse
	Child,         // school-ish hours, then the square until dark
	Elder,         // slow circuits of bench, square and church
	Clerk,         // Newhaven: office hours, commutes
	Shopkeeper,    // Newhaven: shophouse shutters up at eight
	Courier,       // Newhaven: never in the same place twice
	Officer,       // Newhaven: walks a beat
	Busker,        // plays the plaza when there is a crowd
	Gardener,      // parks and planters
	Sailor,        // the wharf, the boats, the tavern
	Count UMETA(Hidden)
};

/** What kind of body an NPC has. Person is one of these on purpose. */
UENUM(BlueprintType)
enum class EUEGT2NPCSpecies : uint8
{
	Person,
	Dog,
	Cat,
	Chicken,
	Duck,
	Sheep,
	Cow,
	Pig,
	Goat,
	Horse,
	Seagull,
	Rabbit,
	Count UMETA(Hidden)
};

/**
 * What someone is doing. The routine says which of these the hour asks for;
 * conditions can override it (see ResolveActivity).
 */
UENUM(BlueprintType)
enum class EUEGT2Activity : uint8
{
	Sleep,        // at home, not moving
	Wake,         // up, still at home, stretching
	Breakfast,
	Commute,      // travelling to work
	Work,
	Market,       // buying or selling at a stall
	Lunch,
	Errand,       // a short trip somewhere else and back
	Socialise,    // standing with someone, talking
	Worship,
	Play,         // children, dogs
	Stroll,       // walking with no destination that matters
	Rest,         // sitting on a bench
	Dinner,
	Tavern,
	HomeTime,     // walking home
	Shelter,      // rain: get under something
	Flee,         // animal: the player got too close
	Follow,       // animal: the player is interesting
	Graze,
	Roost,        // animal night: coop, roof, nest
	Forage,
	Patrol,
	Scavenge,
	Idle,         // the fallback; also what a dormant NPC reports
	Count UMETA(Hidden)
};

/**
 * A named place an NPC can be sent to. The content build bakes a world position
 * for each anchor an NPC actually uses; anything unbaked falls back to Home.
 */
UENUM(BlueprintType)
enum class EUEGT2Anchor : uint8
{
	Home,
	Work,
	Market,
	Square,
	Church,
	Dock,
	Field,
	Tavern,
	Park,
	Plaza,
	Shore,
	Water,      // a pond or the sea edge, for ducks
	Coop,       // a roost for birds, a barn for stock
	Pasture,
	Shelter,    // the nearest thing with a roof that is not home
	Wander,     // no fixed point: roam near the current one
	Count UMETA(Hidden)
};

/** How busy an NPC is being simulated, chosen from distance to the player. */
UENUM(BlueprintType)
enum class EUEGT2NPCLOD : uint8
{
	Near,       // full rate: walking, bobbing, speaking
	Mid,        // 10 Hz: walking, no bubbles of its own
	Far,        // 2 Hz: coarse movement, no cosmetics
	Dormant,    // not ticking; snapped to wherever the schedule says
	Count UMETA(Hidden)
};

/** Which register a line is in. The bubble looks the same; the words do not. */
UENUM(BlueprintType)
enum class EUEGT2SpeechMood : uint8
{
	Announce,   // "heading down to the boats before the tide turns"
	Comment,    // about the weather, the hour, the town
	Greet,      // the player walked up
	Reply,      // the player talked to them, or another NPC opened
	Idle,       // muttering to themselves
	Count UMETA(Hidden)
};

/**
 * Stable per-NPC traits in 0..1, rolled once from the NPC's seed.
 *
 * These are what stop two bakers with the same routine from being the same
 * person: how early they leave, whether they talk to you, how far they wander,
 * whether rain sends them running.
 */
USTRUCT(BlueprintType)
struct UEGT2_API FUEGT2Personality
{
	GENERATED_BODY()

	/** Talks to the player and to other NPCs. Low means they keep walking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|NPC") float Sociability = 0.5f;

	/** How close to the scheduled hour they actually change activity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|NPC") float Punctuality = 0.5f;

	/** Walk speed scale and how much they fidget standing still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|NPC") float Energy = 0.5f;

	/** Detours: the odds of an errand instead of the scheduled thing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|NPC") float Curiosity = 0.5f;

	/** Stays out in the rain, lets the player get close, does not flee. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|NPC") float Bravery = 0.5f;

	/** Roll a personality from a seed. Same seed, same person, every run. */
	static FUEGT2Personality FromSeed(int32 Seed);
};

/**
 * Slow drives that bend the routine without replacing it.
 *
 * All three run 0..1 where 1 is "satisfied". They decay while the NPC is doing
 * something that does not feed them, so a villager who has been working all
 * morning is hungry by noon and will take the lunch the schedule offers -
 * whereas one who ate a late breakfast walks past it.
 */
USTRUCT(BlueprintType)
struct UEGT2_API FUEGT2NPCNeeds
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|NPC") float Energy = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|NPC") float Fed = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|NPC") float Company = 0.6f;

	/** Advance by InHours of world time given what they are doing. */
	void Advance(float InHours, EUEGT2Activity Activity);
};

/** One row of a routine: from StartHour, do Activity at Anchor. */
USTRUCT(BlueprintType)
struct UEGT2_API FUEGT2ScheduleEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|NPC") float StartHour = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|NPC") EUEGT2Activity Activity = EUEGT2Activity::Idle;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|NPC") EUEGT2Anchor Anchor = EUEGT2Anchor::Home;

	FUEGT2ScheduleEntry() = default;
	FUEGT2ScheduleEntry(float InStartHour, EUEGT2Activity InActivity, EUEGT2Anchor InAnchor)
		: StartHour(InStartHour), Activity(InActivity), Anchor(InAnchor) {}
};

/**
 * A whole day, as a list of entries sorted by StartHour.
 *
 * The first entry must start at 0: the lookup wraps backwards from any hour to
 * the last entry at or before it, and an empty prefix would have nothing to
 * wrap to. UEGT2NPCTests asserts that for every routine in the table.
 */
USTRUCT(BlueprintType)
struct UEGT2_API FUEGT2Routine
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|NPC") FName Name;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|NPC") TArray<FUEGT2ScheduleEntry> Entries;

	/** The entry in force at Hour. Hour is wrapped into 0..24 first. */
	const FUEGT2ScheduleEntry& EntryAt(float Hour) const;

	/** Hour at which the entry in force at Hour ends. May be 24. */
	float NextChangeHour(float Hour) const;

	bool IsValid() const { return Entries.Num() > 0; }
};

/**
 * Everything ResolveActivity is allowed to look at.
 *
 * Passing a struct rather than the NPC keeps the decision testable: a test can
 * describe "a timid farmer at 14:00 in a storm with the player two metres away"
 * without a world, a map or a pawn.
 */
USTRUCT(BlueprintType)
struct UEGT2_API FUEGT2NPCContext
{
	GENERATED_BODY()

	/** World hour, 0..24. */
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2|NPC") float Hour = 12.0f;

	/** Whole days since the game started, for weekday habits. */
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2|NPC") int32 DayIndex = 0;

	UPROPERTY(BlueprintReadWrite, Category = "UEGT2|NPC") EUEGT2Weather Weather = EUEGT2Weather::Clear;

	/** Centimetres to the player. Large when there is no player. */
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2|NPC") float PlayerDistance = 1.0e9f;

	/** Where the player is, for the two activities that steer by it. */
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2|NPC") FVector PlayerLocation = FVector::ZeroVector;

	/** True when the NPC currently has nothing over its head. */
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2|NPC") bool bExposed = true;

	UPROPERTY(BlueprintReadWrite, Category = "UEGT2|NPC") FUEGT2Personality Personality;
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2|NPC") FUEGT2NPCNeeds Needs;

	/** Per-NPC seed, so "which villagers skip church" is stable across runs. */
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2|NPC") int32 Seed = 0;
};

/** What ResolveActivity decided, and why. The reason drives the speech line. */
UENUM(BlueprintType)
enum class EUEGT2ActivityReason : uint8
{
	Schedule,      // the routine asked for it
	Weather,       // driven inside, or driven out by good weather
	Player,        // the player is close enough to matter
	Need,          // hungry, tired or lonely enough to change plan
	DayOfWeek,     // market day, rest day
	Detour,        // curiosity: an errand instead
	Count UMETA(Hidden)
};

/** The output of one decision. */
USTRUCT(BlueprintType)
struct UEGT2_API FUEGT2ActivityDecision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|NPC") EUEGT2Activity Activity = EUEGT2Activity::Idle;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|NPC") EUEGT2Anchor Anchor = EUEGT2Anchor::Home;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|NPC") EUEGT2ActivityReason Reason = EUEGT2ActivityReason::Schedule;
};

// --- Display and parsing ----------------------------------------------------
UEGT2_API FText GetActivityDisplayName(EUEGT2Activity Activity);
UEGT2_API FText GetRoleDisplayName(EUEGT2NPCRole Role);
UEGT2_API FText GetSpeciesDisplayName(EUEGT2NPCSpecies Species);
UEGT2_API const TCHAR* GetAnchorName(EUEGT2Anchor Anchor);

/** True for the species that walk on four legs (or none) rather than talking. */
UEGT2_API bool IsAnimalSpecies(EUEGT2NPCSpecies Species);

/** Rain or worse: the weather that sends people indoors. */
UEGT2_API bool IsWetWeather(EUEGT2Weather Weather);

/** Activities during which the NPC is inside a building and should be hidden. */
UEGT2_API bool IsIndoorActivity(EUEGT2Activity Activity);

/** Walk speed scale for an activity: hurrying home beats an evening stroll. */
UEGT2_API float GetActivityPace(EUEGT2Activity Activity);

/**
 * A cheap stable hash. Used everywhere a choice has to be repeatable without
 * carrying a stream: line selection, jitter, "which of the crowd talks".
 */
UEGT2_API uint32 UEGT2HashSeed(uint32 A, uint32 B = 0u, uint32 C = 0u);

/** UEGT2HashSeed folded into 0..1. */
UEGT2_API float UEGT2HashUnit(uint32 A, uint32 B = 0u, uint32 C = 0u);

// --- Weekday ----------------------------------------------------------------
/** Fairhaven keeps a six day week; the sixth is the rest day. */
inline constexpr int32 UEGT2DaysPerWeek = 6;

/** Day 3 of every week is market day: the square fills, the stalls open early. */
UEGT2_API bool IsMarketDay(int32 DayIndex);

/** Day 6: the church fills, the fields empty, the tavern opens at noon. */
UEGT2_API bool IsRestDay(int32 DayIndex);
