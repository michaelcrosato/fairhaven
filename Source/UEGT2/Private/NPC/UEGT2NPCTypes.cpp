#include "NPC/UEGT2NPCTypes.h"

#define LOCTEXT_NAMESPACE "UEGT2NPC"

// Named rather than anonymous: SUEGT2Menu.cpp leaks a file-scope using-directive
// into whatever a unity build concatenates after it, and file-local helpers at
// global scope are how that turns into an ambiguous-symbol error.
namespace UEGT2NPCTypes
{
	/** One round of a 32 bit integer mix (the finalizer from MurmurHash3). */
	uint32 Mix(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x85EBCA6Bu;
		Value ^= Value >> 13;
		Value *= 0xC2B2AE35u;
		Value ^= Value >> 16;
		return Value;
	}
}

uint32 UEGT2HashSeed(uint32 A, uint32 B, uint32 C)
{
	uint32 Value = UEGT2NPCTypes::Mix(A + 0x9E3779B9u);
	Value = UEGT2NPCTypes::Mix(Value ^ (B * 0x85EBCA77u));
	Value = UEGT2NPCTypes::Mix(Value ^ (C * 0xC2B2AE3Du));
	return Value;
}

float UEGT2HashUnit(uint32 A, uint32 B, uint32 C)
{
	// 24 bits is plenty and keeps the division exact in float.
	return (UEGT2HashSeed(A, B, C) >> 8) / float(1 << 24);
}

FUEGT2Personality FUEGT2Personality::FromSeed(int32 Seed)
{
	const uint32 S = (uint32)Seed;
	FUEGT2Personality Out;
	// Each trait gets its own salt so two traits never correlate.
	Out.Sociability = UEGT2HashUnit(S, 0x5011u);
	Out.Punctuality = UEGT2HashUnit(S, 0x7011u);
	Out.Energy      = UEGT2HashUnit(S, 0x9011u);
	Out.Curiosity   = UEGT2HashUnit(S, 0xB011u);
	Out.Bravery     = UEGT2HashUnit(S, 0xD011u);
	return Out;
}

void FUEGT2NPCNeeds::Advance(float InHours, EUEGT2Activity Activity)
{
	if (InHours <= 0.0f)
	{
		return;
	}
	// Rates are per world hour. A day is 20 real minutes by default, so an hour
	// is 50 seconds: these numbers are felt within a single play session.
	float EnergyRate = -0.06f;
	float FedRate = -0.11f;
	float CompanyRate = -0.07f;

	switch (Activity)
	{
	case EUEGT2Activity::Sleep:
		EnergyRate = 0.34f; FedRate = -0.03f; CompanyRate = -0.01f;
		break;
	case EUEGT2Activity::Rest:
	case EUEGT2Activity::Roost:
		EnergyRate = 0.15f;
		break;
	case EUEGT2Activity::Breakfast:
	case EUEGT2Activity::Lunch:
	case EUEGT2Activity::Dinner:
	case EUEGT2Activity::Graze:
	case EUEGT2Activity::Forage:
	case EUEGT2Activity::Scavenge:
		FedRate = 0.85f;
		break;
	case EUEGT2Activity::Tavern:
		FedRate = 0.35f; CompanyRate = 0.5f; EnergyRate = -0.02f;
		break;
	case EUEGT2Activity::Socialise:
	case EUEGT2Activity::Market:
	case EUEGT2Activity::Worship:
	case EUEGT2Activity::Play:
		CompanyRate = 0.45f;
		break;
	case EUEGT2Activity::Work:
	case EUEGT2Activity::Commute:
	case EUEGT2Activity::Patrol:
		EnergyRate = -0.13f;
		break;
	default:
		break;
	}

	Energy = FMath::Clamp(Energy + EnergyRate * InHours, 0.0f, 1.0f);
	Fed = FMath::Clamp(Fed + FedRate * InHours, 0.0f, 1.0f);
	Company = FMath::Clamp(Company + CompanyRate * InHours, 0.0f, 1.0f);
}

const FUEGT2ScheduleEntry& FUEGT2Routine::EntryAt(float Hour) const
{
	static const FUEGT2ScheduleEntry Fallback(0.0f, EUEGT2Activity::Idle, EUEGT2Anchor::Home);
	if (Entries.Num() == 0)
	{
		return Fallback;
	}

	const float Wrapped = FMath::Fmod(FMath::Fmod(Hour, 24.0f) + 24.0f, 24.0f);
	// Walk backwards to the last entry that has already started. Entries[0]
	// starts at 0 by construction, so this always terminates on a real row.
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		if (Wrapped >= Entries[Index].StartHour)
		{
			return Entries[Index];
		}
	}
	return Entries[0];
}

float FUEGT2Routine::NextChangeHour(float Hour) const
{
	if (Entries.Num() == 0)
	{
		return 24.0f;
	}
	const float Wrapped = FMath::Fmod(FMath::Fmod(Hour, 24.0f) + 24.0f, 24.0f);
	for (const FUEGT2ScheduleEntry& Entry : Entries)
	{
		if (Entry.StartHour > Wrapped)
		{
			return Entry.StartHour;
		}
	}
	return 24.0f;
}

// ---------------------------------------------------------------------------
FText GetActivityDisplayName(EUEGT2Activity Activity)
{
	switch (Activity)
	{
	case EUEGT2Activity::Sleep:     return LOCTEXT("ActSleep", "asleep");
	case EUEGT2Activity::Wake:      return LOCTEXT("ActWake", "waking up");
	case EUEGT2Activity::Breakfast: return LOCTEXT("ActBreakfast", "having breakfast");
	case EUEGT2Activity::Commute:   return LOCTEXT("ActCommute", "on the way to work");
	case EUEGT2Activity::Work:      return LOCTEXT("ActWork", "working");
	case EUEGT2Activity::Market:    return LOCTEXT("ActMarket", "at the market");
	case EUEGT2Activity::Lunch:     return LOCTEXT("ActLunch", "having lunch");
	case EUEGT2Activity::Errand:    return LOCTEXT("ActErrand", "running an errand");
	case EUEGT2Activity::Socialise: return LOCTEXT("ActSocialise", "talking");
	case EUEGT2Activity::Worship:   return LOCTEXT("ActWorship", "at the church");
	case EUEGT2Activity::Play:      return LOCTEXT("ActPlay", "playing");
	case EUEGT2Activity::Stroll:    return LOCTEXT("ActStroll", "out for a walk");
	case EUEGT2Activity::Rest:      return LOCTEXT("ActRest", "sitting down");
	case EUEGT2Activity::Dinner:    return LOCTEXT("ActDinner", "having dinner");
	case EUEGT2Activity::Tavern:    return LOCTEXT("ActTavern", "at the tavern");
	case EUEGT2Activity::HomeTime:  return LOCTEXT("ActHomeTime", "heading home");
	case EUEGT2Activity::Shelter:   return LOCTEXT("ActShelter", "sheltering from the rain");
	case EUEGT2Activity::Flee:      return LOCTEXT("ActFlee", "bolting");
	case EUEGT2Activity::Follow:    return LOCTEXT("ActFollow", "following you");
	case EUEGT2Activity::Graze:     return LOCTEXT("ActGraze", "grazing");
	case EUEGT2Activity::Roost:     return LOCTEXT("ActRoost", "roosting");
	case EUEGT2Activity::Forage:    return LOCTEXT("ActForage", "foraging");
	case EUEGT2Activity::Patrol:    return LOCTEXT("ActPatrol", "on patrol");
	case EUEGT2Activity::Scavenge:  return LOCTEXT("ActScavenge", "scavenging");
	default:                        return LOCTEXT("ActIdle", "idling");
	}
}

FText GetRoleDisplayName(EUEGT2NPCRole Role)
{
	switch (Role)
	{
	case EUEGT2NPCRole::Farmer:     return LOCTEXT("RoleFarmer", "Farmer");
	case EUEGT2NPCRole::Fisher:     return LOCTEXT("RoleFisher", "Fisher");
	case EUEGT2NPCRole::Merchant:   return LOCTEXT("RoleMerchant", "Merchant");
	case EUEGT2NPCRole::Baker:      return LOCTEXT("RoleBaker", "Baker");
	case EUEGT2NPCRole::Innkeeper:  return LOCTEXT("RoleInnkeeper", "Innkeeper");
	case EUEGT2NPCRole::Priest:     return LOCTEXT("RolePriest", "Priest");
	case EUEGT2NPCRole::Smith:      return LOCTEXT("RoleSmith", "Smith");
	case EUEGT2NPCRole::Dockhand:   return LOCTEXT("RoleDockhand", "Dockhand");
	case EUEGT2NPCRole::Child:      return LOCTEXT("RoleChild", "Child");
	case EUEGT2NPCRole::Elder:      return LOCTEXT("RoleElder", "Elder");
	case EUEGT2NPCRole::Clerk:      return LOCTEXT("RoleClerk", "Clerk");
	case EUEGT2NPCRole::Shopkeeper: return LOCTEXT("RoleShopkeeper", "Shopkeeper");
	case EUEGT2NPCRole::Courier:    return LOCTEXT("RoleCourier", "Courier");
	case EUEGT2NPCRole::Officer:    return LOCTEXT("RoleOfficer", "Constable");
	case EUEGT2NPCRole::Busker:     return LOCTEXT("RoleBusker", "Busker");
	case EUEGT2NPCRole::Gardener:   return LOCTEXT("RoleGardener", "Gardener");
	case EUEGT2NPCRole::Sailor:     return LOCTEXT("RoleSailor", "Sailor");
	default:                        return LOCTEXT("RoleVillager", "Villager");
	}
}

FText GetSpeciesDisplayName(EUEGT2NPCSpecies Species)
{
	switch (Species)
	{
	case EUEGT2NPCSpecies::Dog:     return LOCTEXT("SpDog", "Dog");
	case EUEGT2NPCSpecies::Cat:     return LOCTEXT("SpCat", "Cat");
	case EUEGT2NPCSpecies::Chicken: return LOCTEXT("SpChicken", "Chicken");
	case EUEGT2NPCSpecies::Duck:    return LOCTEXT("SpDuck", "Duck");
	case EUEGT2NPCSpecies::Sheep:   return LOCTEXT("SpSheep", "Sheep");
	case EUEGT2NPCSpecies::Cow:     return LOCTEXT("SpCow", "Cow");
	case EUEGT2NPCSpecies::Pig:     return LOCTEXT("SpPig", "Pig");
	case EUEGT2NPCSpecies::Goat:    return LOCTEXT("SpGoat", "Goat");
	case EUEGT2NPCSpecies::Horse:   return LOCTEXT("SpHorse", "Horse");
	case EUEGT2NPCSpecies::Seagull: return LOCTEXT("SpSeagull", "Seagull");
	case EUEGT2NPCSpecies::Rabbit:  return LOCTEXT("SpRabbit", "Rabbit");
	default:                        return LOCTEXT("SpPerson", "Person");
	}
}

const TCHAR* GetAnchorName(EUEGT2Anchor Anchor)
{
	switch (Anchor)
	{
	case EUEGT2Anchor::Home:    return TEXT("Home");
	case EUEGT2Anchor::Work:    return TEXT("Work");
	case EUEGT2Anchor::Market:  return TEXT("Market");
	case EUEGT2Anchor::Square:  return TEXT("Square");
	case EUEGT2Anchor::Church:  return TEXT("Church");
	case EUEGT2Anchor::Dock:    return TEXT("Dock");
	case EUEGT2Anchor::Field:   return TEXT("Field");
	case EUEGT2Anchor::Tavern:  return TEXT("Tavern");
	case EUEGT2Anchor::Park:    return TEXT("Park");
	case EUEGT2Anchor::Plaza:   return TEXT("Plaza");
	case EUEGT2Anchor::Shore:   return TEXT("Shore");
	case EUEGT2Anchor::Water:   return TEXT("Water");
	case EUEGT2Anchor::Coop:    return TEXT("Coop");
	case EUEGT2Anchor::Pasture: return TEXT("Pasture");
	case EUEGT2Anchor::Shelter: return TEXT("Shelter");
	default:                    return TEXT("Wander");
	}
}

bool IsAnimalSpecies(EUEGT2NPCSpecies Species)
{
	return Species != EUEGT2NPCSpecies::Person;
}

bool IsWetWeather(EUEGT2Weather Weather)
{
	return Weather == EUEGT2Weather::Storm;
}

bool IsIndoorActivity(EUEGT2Activity Activity)
{
	// Sleeping and eating happen behind a wall. Hiding the actor rather than
	// standing it inside geometry is the only honest option without interiors.
	return Activity == EUEGT2Activity::Sleep
		|| Activity == EUEGT2Activity::Breakfast
		|| Activity == EUEGT2Activity::Dinner;
}

float GetActivityPace(EUEGT2Activity Activity)
{
	switch (Activity)
	{
	case EUEGT2Activity::Flee:      return 2.3f;
	case EUEGT2Activity::Shelter:   return 1.7f;
	case EUEGT2Activity::Follow:    return 1.45f;
	case EUEGT2Activity::Commute:   return 1.15f;
	case EUEGT2Activity::HomeTime:  return 1.1f;
	case EUEGT2Activity::Errand:    return 1.05f;
	case EUEGT2Activity::Patrol:    return 0.85f;
	case EUEGT2Activity::Play:      return 1.3f;
	case EUEGT2Activity::Stroll:    return 0.7f;
	case EUEGT2Activity::Graze:     return 0.35f;
	case EUEGT2Activity::Forage:    return 0.45f;
	case EUEGT2Activity::Scavenge:  return 0.6f;
	default:                        return 1.0f;
	}
}

bool IsMarketDay(int32 DayIndex)
{
	// Negative day indices are not expected, but a modulo that goes negative
	// would silently move market day, so fold it first.
	const int32 Day = ((DayIndex % UEGT2DaysPerWeek) + UEGT2DaysPerWeek) % UEGT2DaysPerWeek;
	return Day == 2;
}

bool IsRestDay(int32 DayIndex)
{
	const int32 Day = ((DayIndex % UEGT2DaysPerWeek) + UEGT2DaysPerWeek) % UEGT2DaysPerWeek;
	return Day == 5;
}

#undef LOCTEXT_NAMESPACE
