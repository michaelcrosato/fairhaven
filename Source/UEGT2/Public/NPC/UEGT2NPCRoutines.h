// Fairhaven (UEGT2) - the daily routines, and the rules that bend them.
//
// A routine is what someone does on an ordinary day. The rules below are what
// make it a habit rather than a timetable: the same farmer walks the same road
// at the same hour every day, and then it rains, or it is market day, or you
// walk up to him, and he does something else.
//
// Everything here is a pure function over FUEGT2NPCContext. Nothing touches the
// world. That is what makes UEGT2NPCTests able to describe a situation in three
// lines and assert on the decision.
#pragma once

#include "CoreMinimal.h"
#include "NPC/UEGT2NPCTypes.h"

/** The routine for a person's trade. Always valid; never empty. */
UEGT2_API const FUEGT2Routine& GetRoleRoutine(EUEGT2NPCRole Role);

/** The routine for an animal. Person returns the plain villager routine. */
UEGT2_API const FUEGT2Routine& GetSpeciesRoutine(EUEGT2NPCSpecies Species);

/**
 * How close the player has to get before this species bolts, in centimetres.
 * Zero for the species that do not care (cows, horses, pigs, and people).
 */
UEGT2_API float GetFleeRadius(EUEGT2NPCSpecies Species);

/**
 * The whole decision: routine, then day of week, then detours, then needs,
 * then the player, then the weather. Later stages override earlier ones, so
 * the ordering is the priority ordering, and safety is last because it wins.
 */
UEGT2_API FUEGT2ActivityDecision ResolveActivity(EUEGT2NPCRole Role,
	EUEGT2NPCSpecies Species, const FUEGT2NPCContext& Context);

/**
 * The hour the routine wants, shifted by how punctual this NPC is.
 *
 * Punctual people are up to eighteen minutes early, unpunctual ones up to
 * eighteen minutes late. It is a small thing and it is most of why a town of
 * two hundred people on nineteen routines does not move like a parade.
 */
UEGT2_API float GetEffectiveHour(float Hour, const FUEGT2Personality& Personality);

/**
 * Advance a copied life through its routines in steps no longer than a minute.
 * Player proximity is ignored while resting. Invalid inputs leave Context intact.
 * Zero hours validates without advancing; the largest permitted interval is a day.
 */
UEGT2_API bool UEGT2AdvanceScheduledLife(EUEGT2NPCRole Role,
	EUEGT2NPCSpecies Species, float WorldHours, FUEGT2NPCContext& Context);
