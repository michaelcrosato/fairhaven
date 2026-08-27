#include "NPC/UEGT2NPCRoutines.h"

// Named rather than anonymous: see the note in UEGT2NPCTypes.cpp.
namespace UEGT2Routines
{
	using FEntry = FUEGT2ScheduleEntry;
	using EA = EUEGT2Activity;
	using EN = EUEGT2Anchor;

	FUEGT2Routine Make(const TCHAR* Name, TArray<FEntry> Rows)
	{
		FUEGT2Routine Routine;
		Routine.Name = FName(Name);
		Routine.Entries = MoveTemp(Rows);
		return Routine;
	}

	/**
	 * The role table, built once on first use.
	 *
	 * Every routine starts at hour 0 because EntryAt wraps backwards to the
	 * last row at or before the hour and would otherwise have nothing to land
	 * on just after midnight. UEGT2NPCTests asserts it for every row here.
	 */
	const TArray<FUEGT2Routine>& RoleTable()
	{
		static const TArray<FUEGT2Routine> Table = []
		{
			TArray<FUEGT2Routine> Result;
			Result.SetNum((int32)EUEGT2NPCRole::Count);

			Result[(int32)EUEGT2NPCRole::Villager] = Make(TEXT("Villager"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 6.50f, EA::Wake,      EN::Home },
				{ 7.00f, EA::Breakfast, EN::Home },
				{ 7.75f, EA::Stroll,    EN::Square },
				{ 9.00f, EA::Work,      EN::Work },
				{12.50f, EA::Lunch,     EN::Market },
				{13.50f, EA::Work,      EN::Work },
				{17.00f, EA::Market,    EN::Market },
				{18.50f, EA::Dinner,    EN::Home },
				{19.50f, EA::Socialise, EN::Square },
				{21.00f, EA::Tavern,    EN::Tavern },
				{22.50f, EA::HomeTime,  EN::Home },
				{23.25f, EA::Sleep,     EN::Home },
			});

			// Out before the sun and back before dark: the shape of the day
			// that makes the farmland read as worked rather than decorative.
			Result[(int32)EUEGT2NPCRole::Farmer] = Make(TEXT("Farmer"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 4.75f, EA::Wake,      EN::Home },
				{ 5.00f, EA::Breakfast, EN::Home },
				{ 5.50f, EA::Commute,   EN::Field },
				{ 6.00f, EA::Work,      EN::Field },
				{12.00f, EA::Lunch,     EN::Field },
				{12.75f, EA::Work,      EN::Field },
				{17.50f, EA::HomeTime,  EN::Home },
				{18.25f, EA::Dinner,    EN::Home },
				{19.00f, EA::Rest,      EN::Square },
				{20.50f, EA::Tavern,    EN::Tavern },
				{21.75f, EA::HomeTime,  EN::Home },
				{22.25f, EA::Sleep,     EN::Home },
			});

			// The tide, not the clock. Out at four, selling by eleven.
			Result[(int32)EUEGT2NPCRole::Fisher] = Make(TEXT("Fisher"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 3.75f, EA::Wake,      EN::Home },
				{ 4.25f, EA::Commute,   EN::Dock },
				{ 4.75f, EA::Work,      EN::Dock },
				{11.00f, EA::Market,    EN::Market },
				{13.00f, EA::Lunch,     EN::Home },
				{14.00f, EA::Work,      EN::Dock },
				{17.00f, EA::Rest,      EN::Shore },
				{18.50f, EA::Dinner,    EN::Home },
				{19.50f, EA::Tavern,    EN::Tavern },
				{22.00f, EA::HomeTime,  EN::Home },
				{22.75f, EA::Sleep,     EN::Home },
			});

			Result[(int32)EUEGT2NPCRole::Merchant] = Make(TEXT("Merchant"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 5.75f, EA::Wake,      EN::Home },
				{ 6.25f, EA::Breakfast, EN::Home },
				{ 6.75f, EA::Commute,   EN::Market },
				{ 7.25f, EA::Work,      EN::Market },
				{12.50f, EA::Lunch,     EN::Market },
				{13.25f, EA::Work,      EN::Market },
				{18.00f, EA::Errand,    EN::Square },
				{18.75f, EA::HomeTime,  EN::Home },
				{19.50f, EA::Dinner,    EN::Home },
				{20.50f, EA::Socialise, EN::Square },
				{22.00f, EA::Sleep,     EN::Home },
			});

			// Inverted on purpose: bread is baked at two in the morning and the
			// baker sleeps through the afternoon. Walk past at 03:00 and there
			// is exactly one light on in town.
			Result[(int32)EUEGT2NPCRole::Baker] = Make(TEXT("Baker"), {
				{ 0.00f, EA::Work,      EN::Work },
				{ 6.00f, EA::Market,    EN::Market },
				{11.00f, EA::HomeTime,  EN::Home },
				{11.75f, EA::Lunch,     EN::Home },
				{12.50f, EA::Sleep,     EN::Home },
				{16.00f, EA::Wake,      EN::Home },
				{16.50f, EA::Stroll,    EN::Square },
				{18.00f, EA::Dinner,    EN::Home },
				{19.00f, EA::Socialise, EN::Square },
				{20.00f, EA::Sleep,     EN::Home },
			});

			Result[(int32)EUEGT2NPCRole::Innkeeper] = Make(TEXT("Innkeeper"), {
				{ 0.00f, EA::Work,      EN::Tavern },
				{ 1.75f, EA::HomeTime,  EN::Home },
				{ 2.25f, EA::Sleep,     EN::Home },
				{ 9.50f, EA::Wake,      EN::Home },
				{10.00f, EA::Breakfast, EN::Home },
				{11.00f, EA::Errand,    EN::Market },
				{12.50f, EA::Commute,   EN::Tavern },
				{13.00f, EA::Work,      EN::Tavern },
			});

			Result[(int32)EUEGT2NPCRole::Priest] = Make(TEXT("Priest"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 5.50f, EA::Wake,      EN::Home },
				{ 6.00f, EA::Worship,   EN::Church },
				{ 8.00f, EA::Work,      EN::Church },
				{12.00f, EA::Lunch,     EN::Home },
				{13.00f, EA::Errand,    EN::Square },
				{15.00f, EA::Work,      EN::Church },
				{17.50f, EA::Worship,   EN::Church },
				{18.50f, EA::Dinner,    EN::Home },
				{19.50f, EA::Stroll,    EN::Square },
				{21.00f, EA::Sleep,     EN::Home },
			});

			Result[(int32)EUEGT2NPCRole::Smith] = Make(TEXT("Smith"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 5.75f, EA::Wake,      EN::Home },
				{ 6.25f, EA::Breakfast, EN::Home },
				{ 7.00f, EA::Commute,   EN::Work },
				{ 7.50f, EA::Work,      EN::Work },
				{12.25f, EA::Lunch,     EN::Market },
				{13.25f, EA::Work,      EN::Work },
				{18.00f, EA::HomeTime,  EN::Home },
				{18.75f, EA::Dinner,    EN::Home },
				{19.75f, EA::Tavern,    EN::Tavern },
				{22.00f, EA::HomeTime,  EN::Home },
				{22.50f, EA::Sleep,     EN::Home },
			});

			Result[(int32)EUEGT2NPCRole::Dockhand] = Make(TEXT("Dockhand"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 5.25f, EA::Wake,      EN::Home },
				{ 5.75f, EA::Commute,   EN::Dock },
				{ 6.25f, EA::Work,      EN::Dock },
				{12.00f, EA::Lunch,     EN::Dock },
				{12.75f, EA::Work,      EN::Dock },
				{17.50f, EA::Rest,      EN::Shore },
				{18.50f, EA::Tavern,    EN::Tavern },
				{21.50f, EA::HomeTime,  EN::Home },
				{22.25f, EA::Sleep,     EN::Home },
			});

			Result[(int32)EUEGT2NPCRole::Child] = Make(TEXT("Child"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 7.00f, EA::Wake,      EN::Home },
				{ 7.50f, EA::Breakfast, EN::Home },
				{ 8.25f, EA::Play,      EN::Square },
				{ 9.00f, EA::Work,      EN::Church },     // lessons in the church hall
				{12.00f, EA::Lunch,     EN::Home },
				{13.00f, EA::Play,      EN::Square },
				{15.50f, EA::Play,      EN::Park },
				{17.50f, EA::Errand,    EN::Market },
				{18.50f, EA::Dinner,    EN::Home },
				{19.25f, EA::Play,      EN::Square },
				{20.50f, EA::HomeTime,  EN::Home },
				{21.00f, EA::Sleep,     EN::Home },
			});

			Result[(int32)EUEGT2NPCRole::Elder] = Make(TEXT("Elder"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 6.00f, EA::Wake,      EN::Home },
				{ 6.50f, EA::Breakfast, EN::Home },
				{ 7.50f, EA::Stroll,    EN::Square },
				{ 9.00f, EA::Rest,      EN::Square },
				{11.00f, EA::Market,    EN::Market },
				{12.50f, EA::Lunch,     EN::Home },
				{13.50f, EA::Rest,      EN::Square },
				{16.00f, EA::Stroll,    EN::Shore },
				{17.50f, EA::Rest,      EN::Square },
				{18.50f, EA::Dinner,    EN::Home },
				{19.50f, EA::Tavern,    EN::Tavern },
				{21.00f, EA::HomeTime,  EN::Home },
				{21.50f, EA::Sleep,     EN::Home },
			});

			// --- Newhaven ---------------------------------------------------
			Result[(int32)EUEGT2NPCRole::Clerk] = Make(TEXT("Clerk"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 6.75f, EA::Wake,      EN::Home },
				{ 7.25f, EA::Breakfast, EN::Home },
				{ 8.00f, EA::Commute,   EN::Work },
				{ 8.75f, EA::Work,      EN::Work },
				{12.50f, EA::Lunch,     EN::Plaza },
				{13.50f, EA::Work,      EN::Work },
				{17.50f, EA::HomeTime,  EN::Home },
				{18.50f, EA::Dinner,    EN::Home },
				{19.50f, EA::Stroll,    EN::Plaza },
				{21.00f, EA::Tavern,    EN::Tavern },
				{22.50f, EA::HomeTime,  EN::Home },
				{23.00f, EA::Sleep,     EN::Home },
			});

			Result[(int32)EUEGT2NPCRole::Shopkeeper] = Make(TEXT("Shopkeeper"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 6.00f, EA::Wake,      EN::Home },
				{ 6.50f, EA::Breakfast, EN::Home },
				{ 7.25f, EA::Commute,   EN::Work },
				{ 7.75f, EA::Work,      EN::Work },
				{13.00f, EA::Lunch,     EN::Work },
				{13.75f, EA::Work,      EN::Work },
				{20.00f, EA::HomeTime,  EN::Home },
				{20.75f, EA::Dinner,    EN::Home },
				{21.75f, EA::Stroll,    EN::Plaza },
				{22.75f, EA::Sleep,     EN::Home },
			});

			// Never in the same place twice, which is the whole point: a city
			// with only commuters in it reads as a diagram of a city.
			Result[(int32)EUEGT2NPCRole::Courier] = Make(TEXT("Courier"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 5.50f, EA::Wake,      EN::Home },
				{ 6.00f, EA::Commute,   EN::Plaza },
				{ 6.50f, EA::Errand,    EN::Work },
				{ 9.00f, EA::Errand,    EN::Plaza },
				{11.00f, EA::Errand,    EN::Market },
				{12.75f, EA::Lunch,     EN::Plaza },
				{13.50f, EA::Errand,    EN::Work },
				{16.00f, EA::Errand,    EN::Dock },
				{18.00f, EA::Errand,    EN::Plaza },
				{19.00f, EA::HomeTime,  EN::Home },
				{19.75f, EA::Dinner,    EN::Home },
				{20.75f, EA::Tavern,    EN::Tavern },
				{22.50f, EA::Sleep,     EN::Home },
			});

			// Night shift. Walk the plaza at three in the morning and there is
			// one person out, and it is this one.
			Result[(int32)EUEGT2NPCRole::Officer] = Make(TEXT("Officer"), {
				{ 0.00f, EA::Patrol,    EN::Plaza },
				{ 2.00f, EA::Patrol,    EN::Wander },
				{ 6.00f, EA::HomeTime,  EN::Home },
				{ 6.75f, EA::Sleep,     EN::Home },
				{13.00f, EA::Wake,      EN::Home },
				{13.50f, EA::Breakfast, EN::Home },
				{14.50f, EA::Stroll,    EN::Plaza },
				{16.00f, EA::Rest,      EN::Home },
				{18.00f, EA::Dinner,    EN::Home },
				{19.00f, EA::Commute,   EN::Plaza },
				{19.75f, EA::Patrol,    EN::Plaza },
			});

			Result[(int32)EUEGT2NPCRole::Busker] = Make(TEXT("Busker"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 9.00f, EA::Wake,      EN::Home },
				{ 9.75f, EA::Breakfast, EN::Home },
				{10.50f, EA::Commute,   EN::Plaza },
				{11.00f, EA::Work,      EN::Plaza },
				{14.00f, EA::Rest,      EN::Park },
				{15.00f, EA::Work,      EN::Square },
				{18.00f, EA::Errand,    EN::Market },
				{19.00f, EA::Work,      EN::Plaza },
				{21.50f, EA::Tavern,    EN::Tavern },
				{23.50f, EA::HomeTime,  EN::Home },
			});

			Result[(int32)EUEGT2NPCRole::Gardener] = Make(TEXT("Gardener"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 5.50f, EA::Wake,      EN::Home },
				{ 6.00f, EA::Breakfast, EN::Home },
				{ 6.75f, EA::Commute,   EN::Park },
				{ 7.25f, EA::Work,      EN::Park },
				{12.00f, EA::Lunch,     EN::Park },
				{12.75f, EA::Work,      EN::Park },
				{16.50f, EA::Work,      EN::Plaza },
				{18.00f, EA::HomeTime,  EN::Home },
				{18.75f, EA::Dinner,    EN::Home },
				{19.75f, EA::Rest,      EN::Park },
				{21.50f, EA::Sleep,     EN::Home },
			});

			Result[(int32)EUEGT2NPCRole::Sailor] = Make(TEXT("Sailor"), {
				{ 0.00f, EA::Sleep,     EN::Home },
				{ 5.00f, EA::Wake,      EN::Home },
				{ 5.50f, EA::Commute,   EN::Dock },
				{ 6.00f, EA::Work,      EN::Dock },
				{12.50f, EA::Lunch,     EN::Dock },
				{13.25f, EA::Work,      EN::Dock },
				{16.00f, EA::Rest,      EN::Shore },
				{17.50f, EA::Tavern,    EN::Tavern },
				{22.50f, EA::HomeTime,  EN::Home },
				{23.25f, EA::Sleep,     EN::Home },
			});

			return Result;
		}();
		return Table;
	}

	const TArray<FUEGT2Routine>& SpeciesTable()
	{
		static const TArray<FUEGT2Routine> Table = []
		{
			TArray<FUEGT2Routine> Result;
			Result.SetNum((int32)EUEGT2NPCSpecies::Count);

			// Person is filled from the villager routine so a caller that only
			// has a species still gets something sensible.
			Result[(int32)EUEGT2NPCSpecies::Person] = RoleTable()[(int32)EUEGT2NPCRole::Villager];

			Result[(int32)EUEGT2NPCSpecies::Dog] = Make(TEXT("Dog"), {
				{ 0.00f, EA::Roost,     EN::Home },
				{ 6.00f, EA::Wake,      EN::Home },
				{ 6.50f, EA::Play,      EN::Wander },
				{ 9.00f, EA::Patrol,    EN::Wander },
				{12.00f, EA::Rest,      EN::Home },
				{13.50f, EA::Play,      EN::Square },
				{16.00f, EA::Scavenge,  EN::Market },
				{18.00f, EA::Patrol,    EN::Wander },
				{20.50f, EA::Rest,      EN::Home },
				{22.00f, EA::Roost,     EN::Home },
			});

			// Nocturnal, and asleep across the middle of the day where you are
			// most likely to walk past one.
			Result[(int32)EUEGT2NPCSpecies::Cat] = Make(TEXT("Cat"), {
				{ 0.00f, EA::Patrol,    EN::Wander },
				{ 4.50f, EA::Roost,     EN::Home },
				{ 9.00f, EA::Rest,      EN::Home },
				{12.00f, EA::Rest,      EN::Wander },
				{15.00f, EA::Forage,    EN::Wander },
				{17.50f, EA::Rest,      EN::Home },
				{19.50f, EA::Patrol,    EN::Wander },
			});

			Result[(int32)EUEGT2NPCSpecies::Chicken] = Make(TEXT("Chicken"), {
				{ 0.00f, EA::Roost,     EN::Coop },
				{ 6.25f, EA::Wake,      EN::Coop },
				{ 6.75f, EA::Forage,    EN::Wander },
				{12.00f, EA::Rest,      EN::Coop },
				{13.00f, EA::Forage,    EN::Wander },
				{18.50f, EA::Roost,     EN::Coop },
			});

			Result[(int32)EUEGT2NPCSpecies::Duck] = Make(TEXT("Duck"), {
				{ 0.00f, EA::Roost,     EN::Water },
				{ 6.50f, EA::Forage,    EN::Water },
				{11.00f, EA::Rest,      EN::Shore },
				{12.50f, EA::Forage,    EN::Water },
				{17.50f, EA::Rest,      EN::Shore },
				{19.50f, EA::Roost,     EN::Water },
			});

			Result[(int32)EUEGT2NPCSpecies::Sheep] = Make(TEXT("Sheep"), {
				{ 0.00f, EA::Roost,     EN::Pasture },
				{ 5.75f, EA::Graze,     EN::Pasture },
				{11.50f, EA::Rest,      EN::Pasture },
				{13.00f, EA::Graze,     EN::Pasture },
				{19.00f, EA::Roost,     EN::Pasture },
			});

			Result[(int32)EUEGT2NPCSpecies::Cow] = Make(TEXT("Cow"), {
				{ 0.00f, EA::Roost,     EN::Coop },
				{ 6.00f, EA::Graze,     EN::Pasture },
				{12.00f, EA::Rest,      EN::Pasture },
				{13.50f, EA::Graze,     EN::Pasture },
				{18.50f, EA::HomeTime,  EN::Coop },
				{19.50f, EA::Roost,     EN::Coop },
			});

			Result[(int32)EUEGT2NPCSpecies::Pig] = Make(TEXT("Pig"), {
				{ 0.00f, EA::Roost,     EN::Coop },
				{ 6.75f, EA::Forage,    EN::Wander },
				{12.50f, EA::Rest,      EN::Wander },
				{14.00f, EA::Forage,    EN::Wander },
				{19.00f, EA::Roost,     EN::Coop },
			});

			Result[(int32)EUEGT2NPCSpecies::Goat] = Make(TEXT("Goat"), {
				{ 0.00f, EA::Roost,     EN::Pasture },
				{ 5.50f, EA::Graze,     EN::Pasture },
				{10.00f, EA::Forage,    EN::Wander },
				{12.50f, EA::Rest,      EN::Pasture },
				{14.00f, EA::Graze,     EN::Pasture },
				{19.50f, EA::Roost,     EN::Pasture },
			});

			Result[(int32)EUEGT2NPCSpecies::Horse] = Make(TEXT("Horse"), {
				{ 0.00f, EA::Roost,     EN::Coop },
				{ 6.00f, EA::Graze,     EN::Pasture },
				{12.00f, EA::Rest,      EN::Pasture },
				{13.50f, EA::Graze,     EN::Pasture },
				{18.50f, EA::Roost,     EN::Coop },
			});

			// Follows the food: the boats at dawn, the market at noon.
			Result[(int32)EUEGT2NPCSpecies::Seagull] = Make(TEXT("Seagull"), {
				{ 0.00f, EA::Roost,     EN::Shore },
				{ 5.50f, EA::Forage,    EN::Shore },
				{ 9.00f, EA::Scavenge,  EN::Dock },
				{12.00f, EA::Scavenge,  EN::Market },
				{15.00f, EA::Forage,    EN::Water },
				{18.00f, EA::Rest,      EN::Shore },
				{20.00f, EA::Roost,     EN::Shore },
			});

			Result[(int32)EUEGT2NPCSpecies::Rabbit] = Make(TEXT("Rabbit"), {
				{ 0.00f, EA::Roost,     EN::Home },
				{ 4.50f, EA::Forage,    EN::Wander },
				{ 8.00f, EA::Roost,     EN::Home },
				{17.50f, EA::Forage,    EN::Wander },
				{21.00f, EA::Roost,     EN::Home },
			});

			return Result;
		}();
		return Table;
	}

	/** Activities during which nothing should be allowed to interrupt. */
	bool IsAsleep(EUEGT2Activity Activity)
	{
		return Activity == EUEGT2Activity::Sleep || Activity == EUEGT2Activity::Roost;
	}

	/** Outdoors and therefore rainable-on. */
	bool IsOutdoor(EUEGT2Activity Activity)
	{
		return !IsIndoorActivity(Activity)
			&& Activity != EUEGT2Activity::Sleep
			&& Activity != EUEGT2Activity::Shelter;
	}

	bool WorksOutdoors(EUEGT2NPCRole Role)
	{
		return Role == EUEGT2NPCRole::Farmer || Role == EUEGT2NPCRole::Fisher
			|| Role == EUEGT2NPCRole::Dockhand || Role == EUEGT2NPCRole::Gardener
			|| Role == EUEGT2NPCRole::Sailor || Role == EUEGT2NPCRole::Merchant;
	}
}

const FUEGT2Routine& GetRoleRoutine(EUEGT2NPCRole Role)
{
	const TArray<FUEGT2Routine>& Table = UEGT2Routines::RoleTable();
	const int32 Index = (int32)Role;
	return Table.IsValidIndex(Index) ? Table[Index] : Table[(int32)EUEGT2NPCRole::Villager];
}

const FUEGT2Routine& GetSpeciesRoutine(EUEGT2NPCSpecies Species)
{
	const TArray<FUEGT2Routine>& Table = UEGT2Routines::SpeciesTable();
	const int32 Index = (int32)Species;
	return Table.IsValidIndex(Index) ? Table[Index] : Table[(int32)EUEGT2NPCSpecies::Person];
}

float GetFleeRadius(EUEGT2NPCSpecies Species)
{
	switch (Species)
	{
	case EUEGT2NPCSpecies::Chicken: return 520.0f;
	case EUEGT2NPCSpecies::Rabbit:  return 900.0f;
	case EUEGT2NPCSpecies::Seagull: return 700.0f;
	case EUEGT2NPCSpecies::Duck:    return 460.0f;
	case EUEGT2NPCSpecies::Sheep:   return 400.0f;
	case EUEGT2NPCSpecies::Goat:    return 320.0f;
	case EUEGT2NPCSpecies::Cat:     return 380.0f;
	default:                        return 0.0f;   // cows, horses, pigs, people
	}
}

float GetEffectiveHour(float Hour, const FUEGT2Personality& Personality)
{
	return Hour + (FMath::Clamp(Personality.Punctuality, 0.0f, 1.0f) - 0.5f) * 0.6f;
}

FUEGT2ActivityDecision ResolveActivity(EUEGT2NPCRole Role, EUEGT2NPCSpecies Species,
	const FUEGT2NPCContext& Context)
{
	using namespace UEGT2Routines;

	const bool bAnimal = IsAnimalSpecies(Species);
	const FUEGT2Routine& Routine = bAnimal ? GetSpeciesRoutine(Species) : GetRoleRoutine(Role);
	const float EffectiveHour = GetEffectiveHour(Context.Hour, Context.Personality);
	const FUEGT2ScheduleEntry& Entry = Routine.EntryAt(EffectiveHour);

	FUEGT2ActivityDecision Decision;
	Decision.Activity = Entry.Activity;
	Decision.Anchor = Entry.Anchor;
	Decision.Reason = EUEGT2ActivityReason::Schedule;

	const uint32 Seed = (uint32)Context.Seed;
	// Bucketed so a decision does not flicker between two answers inside the
	// same half hour: the same NPC asked twice in one bucket gets one answer.
	const uint32 HourBucket = (uint32)FMath::FloorToInt(Context.Hour * 2.0f);

	// --- 1. Day of week -----------------------------------------------------
	// People only. Animals do not keep a calendar, which is worth stating
	// because it is the one place the two branches must not share code.
	if (!bAnimal)
	{
		if (IsMarketDay(Context.DayIndex) && Context.Hour >= 8.0f && Context.Hour < 16.0f)
		{
			const bool bDrawnToMarket = Role == EUEGT2NPCRole::Villager
				|| Role == EUEGT2NPCRole::Merchant || Role == EUEGT2NPCRole::Elder
				|| Role == EUEGT2NPCRole::Baker || Role == EUEGT2NPCRole::Fisher;
			if (bDrawnToMarket && (Decision.Activity == EUEGT2Activity::Work
				|| Decision.Activity == EUEGT2Activity::Stroll
				|| Decision.Activity == EUEGT2Activity::Rest))
			{
				Decision.Activity = EUEGT2Activity::Market;
				Decision.Anchor = EUEGT2Anchor::Market;
				Decision.Reason = EUEGT2ActivityReason::DayOfWeek;
			}
		}
		else if (IsRestDay(Context.DayIndex) && Decision.Activity == EUEGT2Activity::Work)
		{
			if (Context.Hour >= 9.0f && Context.Hour < 12.0f)
			{
				Decision.Activity = EUEGT2Activity::Worship;
				Decision.Anchor = EUEGT2Anchor::Church;
				Decision.Reason = EUEGT2ActivityReason::DayOfWeek;
			}
			else if (Role != EUEGT2NPCRole::Innkeeper && Role != EUEGT2NPCRole::Officer
				&& Role != EUEGT2NPCRole::Baker)
			{
				// The fields and the offices empty; the square fills.
				Decision.Activity = EUEGT2Activity::Rest;
				Decision.Anchor = EUEGT2Anchor::Square;
				Decision.Reason = EUEGT2ActivityReason::DayOfWeek;
			}
		}
	}

	// --- 2. Detour ----------------------------------------------------------
	// A small, stable chance of doing something else instead. Stable is the
	// point: the same curious villager takes the same detour every day, which
	// is a habit, where a fresh random roll every time is just noise.
	if (!bAnimal && !IsAsleep(Decision.Activity))
	{
		const bool bInterruptible = Decision.Activity == EUEGT2Activity::Work
			|| Decision.Activity == EUEGT2Activity::Stroll
			|| Decision.Activity == EUEGT2Activity::Rest
			|| Decision.Activity == EUEGT2Activity::Market;
		const float Roll = UEGT2HashUnit(Seed, HourBucket, 0x0EA71u);
		if (bInterruptible && Roll < Context.Personality.Curiosity * 0.16f)
		{
			static const EUEGT2Anchor Detours[] = {
				EUEGT2Anchor::Market, EUEGT2Anchor::Square, EUEGT2Anchor::Shore,
				EUEGT2Anchor::Church, EUEGT2Anchor::Park };
			const int32 Pick = (int32)(UEGT2HashSeed(Seed, HourBucket, 0x1EA71u)
				% UE_ARRAY_COUNT(Detours));
			Decision.Activity = EUEGT2Activity::Errand;
			Decision.Anchor = Detours[Pick];
			Decision.Reason = EUEGT2ActivityReason::Detour;
		}
	}

	// --- 3. Needs -----------------------------------------------------------
	if (!IsAsleep(Decision.Activity))
	{
		const bool bWorking = Decision.Activity == EUEGT2Activity::Work
			|| Decision.Activity == EUEGT2Activity::Errand
			|| Decision.Activity == EUEGT2Activity::Patrol;

		if (Context.Needs.Fed < 0.25f && bWorking
			&& Context.Hour >= 11.0f && Context.Hour < 15.0f)
		{
			Decision.Activity = bAnimal ? EUEGT2Activity::Forage : EUEGT2Activity::Lunch;
			Decision.Anchor = bAnimal ? EUEGT2Anchor::Wander : EUEGT2Anchor::Market;
			Decision.Reason = EUEGT2ActivityReason::Need;
		}
		else if (Context.Needs.Energy < 0.14f && Context.Hour >= 20.0f)
		{
			Decision.Activity = EUEGT2Activity::HomeTime;
			Decision.Anchor = EUEGT2Anchor::Home;
			Decision.Reason = EUEGT2ActivityReason::Need;
		}
		else if (!bAnimal && Context.Needs.Company < 0.15f
			&& Context.Personality.Sociability > 0.5f
			&& (Decision.Activity == EUEGT2Activity::Work
				|| Decision.Activity == EUEGT2Activity::Stroll
				|| Decision.Activity == EUEGT2Activity::Rest))
		{
			Decision.Activity = EUEGT2Activity::Socialise;
			Decision.Anchor = EUEGT2Anchor::Square;
			Decision.Reason = EUEGT2ActivityReason::Need;
		}
	}

	// --- 4. The player ------------------------------------------------------
	if (!bAnimal)
	{
		const float GreetRange = 480.0f + Context.Personality.Sociability * 420.0f;
		if (Context.PlayerDistance < GreetRange
			&& Context.Personality.Sociability > 0.42f
			&& !IsAsleep(Decision.Activity)
			&& !IsIndoorActivity(Decision.Activity))
		{
			Decision.Activity = EUEGT2Activity::Socialise;
			Decision.Reason = EUEGT2ActivityReason::Player;
			// Anchor is left alone: they stop where they are rather than
			// walking to the square to talk to someone standing next to them.
		}
	}
	else
	{
		const float FleeRadius = GetFleeRadius(Species);
		// Bravery widens or narrows the radius by half, so one chicken in a
		// flock holds its ground while the rest scatter.
		const float Effective = FleeRadius * (1.5f - Context.Personality.Bravery);
		const bool bWokenByProximity = IsAsleep(Decision.Activity) && Context.PlayerDistance < 260.0f;

		if (FleeRadius > 0.0f && Context.PlayerDistance < Effective
			&& (!IsAsleep(Decision.Activity) || bWokenByProximity))
		{
			Decision.Activity = EUEGT2Activity::Flee;
			Decision.Anchor = EUEGT2Anchor::Wander;
			Decision.Reason = EUEGT2ActivityReason::Player;
		}
		else if (Species == EUEGT2NPCSpecies::Dog && Context.PlayerDistance < 1100.0f
			&& Context.Personality.Bravery > 0.3f && !IsAsleep(Decision.Activity))
		{
			Decision.Activity = EUEGT2Activity::Follow;
			Decision.Anchor = EUEGT2Anchor::Wander;
			Decision.Reason = EUEGT2ActivityReason::Player;
		}
	}

	// --- 5. Weather ---------------------------------------------------------
	// Last, because getting out of a storm beats every other plan. Fleeing is
	// the one exception: an animal already running does not stop for rain.
	if (IsWetWeather(Context.Weather) && Context.bExposed
		&& Decision.Activity != EUEGT2Activity::Flee
		&& IsOutdoor(Decision.Activity)
		&& !IsAsleep(Decision.Activity))
	{
		// The stubborn stay out: high bravery, and the trades whose work does
		// not stop for weather. A fisher in a storm is still a fisher.
		const bool bStubborn = Context.Personality.Bravery > 0.78f
			|| (!bAnimal && WorksOutdoors(Role) && Decision.Activity == EUEGT2Activity::Work
				&& Context.Personality.Bravery > 0.4f);
		if (!bStubborn)
		{
			Decision.Activity = EUEGT2Activity::Shelter;
			Decision.Anchor = bAnimal ? EUEGT2Anchor::Coop : EUEGT2Anchor::Shelter;
			Decision.Reason = EUEGT2ActivityReason::Weather;
		}
	}

	return Decision;
}
