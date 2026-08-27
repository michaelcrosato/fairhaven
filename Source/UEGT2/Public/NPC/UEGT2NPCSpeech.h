// Fairhaven (UEGT2) - what people say, and how a line gets chosen.
//
// Lines are written to read like a text message rather than dialogue: lower
// case, short, present tense, mostly about what the speaker is about to do.
// That is the whole trick of the bubbles - a villager who announces the thing
// you are then watching them walk off and do reads as having a life, where the
// same villager saying "greetings, traveller" reads as a vending machine.
//
// Selection is a hash of the speaker's seed, so the same person makes the same
// remark in the same situation on every run. UEGT2NPCTests asserts that every
// pool is populated and that every line fits the bubble.
#pragma once

#include "CoreMinimal.h"
#include "NPC/UEGT2NPCTypes.h"

/**
 * The pool a given situation draws from. Never empty: unhandled combinations
 * fall through to a generic pool rather than returning nothing.
 */
UEGT2_API const TArray<FText>& GetSpeechPool(EUEGT2NPCRole Role, EUEGT2NPCSpecies Species,
	EUEGT2Activity Activity, EUEGT2SpeechMood Mood, EUEGT2Weather Weather, float Hour);

/**
 * One line, chosen deterministically from the pool.
 *
 * ``Variation`` lets a caller ask for a different line in the same situation -
 * the second half of a two-line conversation, or a repeat visit - without
 * losing determinism.
 */
UEGT2_API FText GetSpeechLine(EUEGT2NPCRole Role, EUEGT2NPCSpecies Species,
	EUEGT2Activity Activity, EUEGT2SpeechMood Mood, EUEGT2Weather Weather,
	float Hour, uint32 Seed, uint32 Variation = 0u);

/**
 * The longest a line may be. Anything past this wraps to a third row in the
 * bubble, which starts to look like a monologue instead of a message.
 */
inline constexpr int32 UEGT2MaxSpeechLength = 84;
