#include "NPC/UEGT2Dialogue.h"

#include "NPC/UEGT2NPCTypes.h"

#define LOCTEXT_NAMESPACE "UEGT2Dialogue"

namespace
{
	/**
	 * Stable choice between phrasings, so one person always answers in
	 * character and a street full of them does not speak in chorus.
	 */
	FText Choose(int32 Seed, uint32 Salt, TArray<FText> Options)
	{
		if (Options.IsEmpty())
		{
			return FText::GetEmpty();
		}
		const float Unit = UEGT2HashUnit((uint32)Seed, Salt);
		const int32 Index = FMath::Clamp((int32)(Unit * Options.Num()), 0, Options.Num() - 1);
		return Options[Index];
	}

	/**
	 * A need, in words.
	 *
	 * The thresholds are the ones ResolveActivity acts on, so what somebody says
	 * and what they are about to do agree. Saying "I am fine" and then walking
	 * off to eat is the kind of small lie that makes a whole world feel fake.
	 */
	FText Describe(float Value, const FText& Fine, const FText& Middling, const FText& Bad)
	{
		if (Value > 0.6f)
		{
			return Fine;
		}
		return Value > 0.26f ? Middling : Bad;
	}

	/** "working" -> "Working". Activity names are written for a status line. */
	FText Sentence(const FText& Text)
	{
		FString Raw = Text.ToString();
		if (!Raw.IsEmpty())
		{
			Raw[0] = FChar::ToUpper(Raw[0]);
		}
		return FText::FromString(Raw);
	}

	FText SettlementName(const FUEGT2DialogueState& State)
	{
		return State.bCityDweller
			? LOCTEXT("Newhaven", "Newhaven")
			: LOCTEXT("Fairhaven", "Fairhaven");
	}
}

// ---------------------------------------------------------------------------
FText UEGT2DialogueGreeting(const FUEGT2DialogueState& State)
{
	if (IsAnimalSpecies(State.Species))
	{
		return FText::Format(
			LOCTEXT("AnimalGreeting", "{0} looks up at you."),
			GetSpeciesDisplayName(State.Species));
	}

	// Somebody in real trouble leads with it. It is the first thing a person
	// mentions, and it tells the player the needs are not decoration.
	if (State.Needs.Relief < 0.18f)
	{
		return LOCTEXT("GreetBursting", "Good day - forgive me, I am in something of a hurry.");
	}
	if (State.Needs.Fed < 0.18f)
	{
		return LOCTEXT("GreetStarving", "Hello. You have not seen a bakehouse open, have you?");
	}
	if (State.Needs.Energy < 0.18f)
	{
		return LOCTEXT("GreetShattered", "Evening. Or morning. I have rather lost track.");
	}

	if (State.bFollowing)
	{
		return LOCTEXT("GreetFollowing", "Still with you. Where to?");
	}

	if (State.Hour < 11.0f)
	{
		return Choose(State.Seed, 0x101u, TArray<FText>{
			LOCTEXT("GreetM1", "Morning to you."),
			LOCTEXT("GreetM2", "You are up early."),
			LOCTEXT("GreetM3", "Good morning. Fine one, isn't it.") });
	}
	if (State.Hour < 18.0f)
	{
		return Choose(State.Seed, 0x102u, TArray<FText>{
			LOCTEXT("GreetA1", "Good day."),
			LOCTEXT("GreetA2", "Afternoon. Something I can do for you?"),
			LOCTEXT("GreetA3", "Hello there.") });
	}
	return Choose(State.Seed, 0x103u, TArray<FText>{
		LOCTEXT("GreetE1", "Evening."),
		LOCTEXT("GreetE2", "Good evening to you."),
		LOCTEXT("GreetE3", "Getting dark. Evening, anyway.") });
}

// ---------------------------------------------------------------------------
FText UEGT2DialogueAnswer(const FUEGT2DialogueState& State, EUEGT2DialogueTopic Topic)
{
	if (IsAnimalSpecies(State.Species))
	{
		// Animals do not talk. They do respond, and the response still comes
		// off their real state - a hungry dog behaves like a hungry dog.
		switch (Topic)
		{
		case EUEGT2DialogueTopic::Hunger:
			return State.Needs.Fed < 0.3f
				? LOCTEXT("AnimalHungry", "It noses hopefully at your hands.")
				: LOCTEXT("AnimalFed", "It is not interested in your hands.");
		case EUEGT2DialogueTopic::Rest:
			return State.Needs.Energy < 0.3f
				? LOCTEXT("AnimalTired", "It settles down where it stands.")
				: LOCTEXT("AnimalAwake", "It is wide awake and watching you.");
		case EUEGT2DialogueTopic::Follow:
			return LOCTEXT("AnimalFollow", "It falls in beside you, for its own reasons.");
		case EUEGT2DialogueTopic::Dismiss:
			return LOCTEXT("AnimalDismiss", "It wanders off.");
		default:
			return FText::Format(LOCTEXT("AnimalDoing", "The {0} is {1}."),
				GetSpeciesDisplayName(State.Species),
				GetActivityDisplayName(State.Activity));
		}
	}

	switch (Topic)
	{
	case EUEGT2DialogueTopic::Doing:
	{
		const FText What = Sentence(GetActivityDisplayName(State.Activity));
		if (State.Reason == EUEGT2ActivityReason::Need)
		{
			return FText::Format(
				LOCTEXT("DoingNeed", "{0}, if I am honest - and not because I planned to."),
				What);
		}
		if (State.Reason == EUEGT2ActivityReason::Weather)
		{
			return FText::Format(LOCTEXT("DoingWeather", "{0}. The weather decided it, not me."), What);
		}
		if (State.Reason == EUEGT2ActivityReason::Detour)
		{
			return FText::Format(LOCTEXT("DoingDetour", "{0}. I had a mind to go the long way."), What);
		}
		return FText::Format(LOCTEXT("DoingSchedule", "{0}, same as most days at this hour."), What);
	}

	case EUEGT2DialogueTopic::Wellbeing:
	{
		// The one that is worst gets said first, then the rest in a breath.
		TArray<FText> Parts;
		if (State.Needs.Fed < 0.26f) { Parts.Add(LOCTEXT("WellHungry", "hungry")); }
		if (State.Needs.Energy < 0.26f) { Parts.Add(LOCTEXT("WellTired", "tired")); }
		if (State.Needs.Relief < 0.26f) { Parts.Add(LOCTEXT("WellUncomfortable", "in need of a moment to myself")); }
		if (State.Needs.Company < 0.26f) { Parts.Add(LOCTEXT("WellLonely", "short of company")); }

		if (Parts.IsEmpty())
		{
			return Choose(State.Seed, 0x201u, TArray<FText>{
				LOCTEXT("WellFine1", "Well enough, thank you. Nothing wanting."),
				LOCTEXT("WellFine2", "Can't complain. Fed, rested, and the day is holding."),
				LOCTEXT("WellFine3", "Very well, as it happens.") });
		}
		FText Joined = Parts[0];
		for (int32 Index = 1; Index < Parts.Num(); ++Index)
		{
			Joined = FText::Format(
				Index == Parts.Num() - 1
					? LOCTEXT("WellAnd", "{0} and {1}")
					: LOCTEXT("WellComma", "{0}, {1}"),
				Joined, Parts[Index]);
		}
		return FText::Format(LOCTEXT("WellSome", "Since you ask - {0}."), Joined);
	}

	case EUEGT2DialogueTopic::Hunger:
		return Describe(State.Needs.Fed,
			LOCTEXT("FedFine", "Just eaten, thank you."),
			LOCTEXT("FedMid", "I could eat. It will keep a while yet."),
			LOCTEXT("FedBad", "Famished. I was on my way to find something, in fact."));

	case EUEGT2DialogueTopic::Rest:
		return Describe(State.Needs.Energy,
			LOCTEXT("EnergyFine", "Fresh as anything."),
			LOCTEXT("EnergyMid", "A bit foot-sore. I'll sit down presently."),
			LOCTEXT("EnergyBad", "Dead on my feet. I need to sit, or better, lie down."));

	case EUEGT2DialogueTopic::Comfort:
		return Describe(State.Needs.Relief,
			LOCTEXT("ReliefFine", "Quite comfortable, thank you for asking."),
			LOCTEXT("ReliefMid", "Now that you mention it, I should find a washroom before long."),
			LOCTEXT("ReliefBad", "Desperately, and you are standing between me and it."));

	case EUEGT2DialogueTopic::Company:
		return Describe(State.Needs.Company,
			LOCTEXT("CompanyFine", "I have had my fill of talk today, but you are welcome."),
			LOCTEXT("CompanyMid", "It is good to have someone to talk to."),
			LOCTEXT("CompanyBad", "I have not spoken to a soul all day. Stay a minute."));

	case EUEGT2DialogueTopic::Trade:
		return FText::Format(
			Choose(State.Seed, 0x301u, TArray<FText>{
				LOCTEXT("Trade1", "I am {0} here. It keeps me busy."),
				LOCTEXT("Trade2", "{0}, for my sins."),
				LOCTEXT("Trade3", "You are talking to the {0}.") }),
			GetRoleDisplayName(State.Role));

	case EUEGT2DialogueTopic::Place:
		return State.bCityDweller
			? Choose(State.Seed, 0x401u, TArray<FText>{
				LOCTEXT("PlaceC1", "Newhaven. Everything is on a ground floor somewhere - the trick is knowing which."),
				LOCTEXT("PlaceC2", "The city. Shops on the outer streets, offices in the middle, and the hall on the square."),
				LOCTEXT("PlaceC3", "Newhaven. You can climb any of these blocks to the roof, if you have the legs.") })
			: Choose(State.Seed, 0x402u, TArray<FText>{
				LOCTEXT("PlaceT1", "Fairhaven. The high street has most of what anyone needs, and the rest is fields."),
				LOCTEXT("PlaceT2", "A small place. Market on the square, church at the end of the lane, harbour past that."),
				LOCTEXT("PlaceT3", "Fairhaven. Quiet, and I would not swap it.") });

	case EUEGT2DialogueTopic::World:
		return Choose(State.Seed, 0x501u, TArray<FText>{
			FText::Format(LOCTEXT("World1", "Beyond {0}? Coast one way, mountains the other, and a long road between them."),
				SettlementName(State)),
			LOCTEXT("World2", "There is a lighthouse east along the shore, and farms west of here. South it gets warm - palms, a lagoon."),
			LOCTEXT("World3", "The high road climbs into the mountains. Cold up there, and worth seeing once.") });

	case EUEGT2DialogueTopic::Follow:
		return State.Needs.Energy < 0.2f
			? LOCTEXT("FollowTired", "I will walk with you, though not far. I am worn out.")
			: Choose(State.Seed, 0x601u, TArray<FText>{
				LOCTEXT("Follow1", "Aye, go on then. Lead the way."),
				LOCTEXT("Follow2", "Why not. I was going nowhere in particular."),
				LOCTEXT("Follow3", "For a while, then. I have my own day to get back to.") });

	case EUEGT2DialogueTopic::Dismiss:
		return Choose(State.Seed, 0x701u, TArray<FText>{
			LOCTEXT("Dismiss1", "Right you are. I have things to be getting on with."),
			LOCTEXT("Dismiss2", "Fair enough. Mind how you go."),
			LOCTEXT("Dismiss3", "Back to it, then.") });

	case EUEGT2DialogueTopic::Farewell:
		return State.Hour >= 18.0f
			? LOCTEXT("Bye1", "Good night to you.")
			: LOCTEXT("Bye2", "Good day, then.");

	default:
		return FText::GetEmpty();
	}
}

// ---------------------------------------------------------------------------
FText UEGT2DialoguePrompt(EUEGT2DialogueTopic Topic)
{
	switch (Topic)
	{
	case EUEGT2DialogueTopic::Doing:     return LOCTEXT("AskDoing", "What are you doing?");
	case EUEGT2DialogueTopic::Wellbeing: return LOCTEXT("AskWell", "How are you keeping?");
	case EUEGT2DialogueTopic::Hunger:    return LOCTEXT("AskHunger", "Are you hungry?");
	case EUEGT2DialogueTopic::Rest:      return LOCTEXT("AskRest", "Are you tired?");
	case EUEGT2DialogueTopic::Comfort:   return LOCTEXT("AskComfort", "Do you need a moment?");
	case EUEGT2DialogueTopic::Company:   return LOCTEXT("AskCompany", "Have you had company today?");
	case EUEGT2DialogueTopic::Trade:     return LOCTEXT("AskTrade", "What do you do here?");
	case EUEGT2DialogueTopic::Place:     return LOCTEXT("AskPlace", "Tell me about this place.");
	case EUEGT2DialogueTopic::World:     return LOCTEXT("AskWorld", "What is out there, beyond here?");
	case EUEGT2DialogueTopic::Follow:    return LOCTEXT("AskFollow", "Will you walk with me?");
	case EUEGT2DialogueTopic::Dismiss:   return LOCTEXT("AskDismiss", "You can go on without me.");
	case EUEGT2DialogueTopic::Farewell:  return LOCTEXT("AskBye", "Good day to you.");
	default: return FText::GetEmpty();
	}
}

bool UEGT2DialogueIsAction(EUEGT2DialogueTopic Topic)
{
	return Topic == EUEGT2DialogueTopic::Follow || Topic == EUEGT2DialogueTopic::Dismiss;
}

// ---------------------------------------------------------------------------
void UEGT2DialogueTopics(const FUEGT2DialogueState& State,
	TArray<FUEGT2DialogueOption>& OutOptions)
{
	OutOptions.Reset();
	const auto Add = [&OutOptions](EUEGT2DialogueTopic Topic)
	{
		OutOptions.Emplace(Topic, UEGT2DialoguePrompt(Topic));
	};

	if (IsAnimalSpecies(State.Species))
	{
		// You can look a dog over and you can call it after you. You cannot
		// ask it about the mountains.
		Add(EUEGT2DialogueTopic::Doing);
		Add(EUEGT2DialogueTopic::Hunger);
		Add(EUEGT2DialogueTopic::Rest);
		Add(State.bFollowing ? EUEGT2DialogueTopic::Dismiss : EUEGT2DialogueTopic::Follow);
		Add(EUEGT2DialogueTopic::Farewell);
		return;
	}

	Add(EUEGT2DialogueTopic::Doing);
	Add(EUEGT2DialogueTopic::Wellbeing);
	Add(EUEGT2DialogueTopic::Hunger);
	Add(EUEGT2DialogueTopic::Rest);
	Add(EUEGT2DialogueTopic::Comfort);
	Add(EUEGT2DialogueTopic::Company);
	Add(EUEGT2DialogueTopic::Trade);
	Add(EUEGT2DialogueTopic::Place);
	Add(EUEGT2DialogueTopic::World);
	Add(State.bFollowing ? EUEGT2DialogueTopic::Dismiss : EUEGT2DialogueTopic::Follow);
	Add(EUEGT2DialogueTopic::Farewell);
}

#undef LOCTEXT_NAMESPACE
