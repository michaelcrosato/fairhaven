// Fairhaven (UEGT2) - what an inhabitant says when you talk to them.
//
// Pure functions over a snapshot of one NPC, exactly like ResolveActivity: no
// world, no actor, no widget. That is what lets the whole conversation be
// tested without opening the editor, and it is why the answers can be trusted
// to reflect what the NPC is actually doing rather than a canned line.
//
// Every answer here is derived from real state. If someone says they are
// famished it is because their Fed need is genuinely low, and if they say they
// are on their way to the bakehouse it is because that is where they are
// walking. A conversation system that made its answers up would be a very
// convincing way to hide the simulation behind it.
#pragma once

#include "CoreMinimal.h"
#include "NPC/UEGT2NPCTypes.h"
#include "UEGT2Dialogue.generated.h"

/** What the player can ask about. */
UENUM()
enum class EUEGT2DialogueTopic : uint8
{
	Doing,       // what are you doing now
	Wellbeing,   // how are you keeping - all four needs at once
	Hunger,
	Rest,
	Comfort,     // the bathroom need, asked politely
	Company,
	Trade,       // what do you do
	Coin,        // how are you off for money, and what does the work pay
	Place,       // tell me about here
	World,       // what is out there
	Follow,      // walk with me
	Dismiss,     // go on without me
	Farewell,
	Count UMETA(Hidden)
};

/** One thing the player can say, and the topic it opens. */
USTRUCT()
struct UEGT2_API FUEGT2DialogueOption
{
	GENERATED_BODY()

	EUEGT2DialogueTopic Topic = EUEGT2DialogueTopic::Doing;
	FText Prompt;

	FUEGT2DialogueOption() = default;
	FUEGT2DialogueOption(EUEGT2DialogueTopic InTopic, const FText& InPrompt)
		: Topic(InTopic), Prompt(InPrompt) {}
};

/**
 * Everything the conversation is allowed to know about who it is talking to.
 *
 * A snapshot rather than a pointer, so the answers are a function of state and
 * nothing else - which is the whole reason this is testable.
 */
USTRUCT()
struct UEGT2_API FUEGT2DialogueState
{
	GENERATED_BODY()

	EUEGT2NPCRole Role = EUEGT2NPCRole::Villager;
	EUEGT2NPCSpecies Species = EUEGT2NPCSpecies::Person;
	FUEGT2NPCNeeds Needs;
	/** What they actually have on them. The answer about money comes off this. */
	FUEGT2Purse Purse;
	EUEGT2Activity Activity = EUEGT2Activity::Idle;
	EUEGT2ActivityReason Reason = EUEGT2ActivityReason::Schedule;
	EUEGT2Anchor Anchor = EUEGT2Anchor::Home;

	/** 0-24. Used for greetings and for "what will you do next". */
	float Hour = 12.0f;

	/** Stable per NPC. Picks between phrasings so a street is not a chorus. */
	int32 Seed = 0;

	/** True while they are already walking with the player. */
	bool bFollowing = false;

	/** Which settlement they belong to, for the questions about here. */
	bool bCityDweller = false;

	FText DisplayName;
};

/** The line they open with, which depends on the hour and on how they are. */
UEGT2_API FText UEGT2DialogueGreeting(const FUEGT2DialogueState& State);

/** What they say to one question. */
UEGT2_API FText UEGT2DialogueAnswer(const FUEGT2DialogueState& State,
	EUEGT2DialogueTopic Topic);

/** Everything the player may ask this particular person right now. */
UEGT2_API void UEGT2DialogueTopics(const FUEGT2DialogueState& State,
	TArray<FUEGT2DialogueOption>& OutOptions);

/** The player's side of the question, for the transcript. */
UEGT2_API FText UEGT2DialoguePrompt(EUEGT2DialogueTopic Topic);

/** True for the two topics that change the world rather than just describe it. */
UEGT2_API bool UEGT2DialogueIsAction(EUEGT2DialogueTopic Topic);
