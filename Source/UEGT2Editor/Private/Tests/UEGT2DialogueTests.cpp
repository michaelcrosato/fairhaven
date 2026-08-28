// Fairhaven (UEGT2) - the conversation, tested without opening the editor.
//
// The point of these is not that the writing is good. It is that what somebody
// says is a function of what is actually true about them: ask a starving
// villager whether they are hungry and the answer has to differ from the one a
// fed villager gives, or the whole conversation is set dressing over a
// simulation the player can never actually see.
#include "Misc/AutomationTest.h"
#include "NPC/UEGT2Dialogue.h"
#include "NPC/UEGT2NPCTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UEGT2DialogueTests
{
	FUEGT2DialogueState Person(float Hour = 12.0f)
	{
		FUEGT2DialogueState State;
		State.Role = EUEGT2NPCRole::Villager;
		State.Species = EUEGT2NPCSpecies::Person;
		State.Activity = EUEGT2Activity::Work;
		State.Reason = EUEGT2ActivityReason::Schedule;
		State.Hour = Hour;
		State.Seed = 4242;
		State.DisplayName = FText::FromString(TEXT("Anne"));
		State.Needs.Energy = 0.9f;
		State.Needs.Fed = 0.9f;
		State.Needs.Relief = 0.9f;
		State.Needs.Company = 0.9f;
		return State;
	}

	bool Has(const TArray<FUEGT2DialogueOption>& Options, EUEGT2DialogueTopic Topic)
	{
		return Options.ContainsByPredicate([Topic](const FUEGT2DialogueOption& Option)
		{
			return Option.Topic == Topic;
		});
	}
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2DialogueAnswersTest,
	"UEGT2.NPC.DialogueAnswers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2DialogueAnswersTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2DialogueTests;

	// Every topic has to say something. An empty answer is a button that looks
	// like it works and does nothing.
	FUEGT2DialogueState State = Person();
	for (int32 Index = 0; Index < (int32)EUEGT2DialogueTopic::Count; ++Index)
	{
		const EUEGT2DialogueTopic Topic = (EUEGT2DialogueTopic)Index;
		TestFalse(FString::Printf(TEXT("topic %d has an answer"), Index),
			UEGT2DialogueAnswer(State, Topic).IsEmpty());
		TestFalse(FString::Printf(TEXT("topic %d has a prompt"), Index),
			UEGT2DialoguePrompt(Topic).IsEmpty());
	}
	TestFalse(TEXT("and a greeting"), UEGT2DialogueGreeting(State).IsEmpty());

	// The answers track the needs. This is the whole contract.
	FUEGT2DialogueState Hungry = Person();
	Hungry.Needs.Fed = 0.05f;
	TestNotEqual(TEXT("a hungry answer differs from a fed one"),
		UEGT2DialogueAnswer(Hungry, EUEGT2DialogueTopic::Hunger).ToString(),
		UEGT2DialogueAnswer(State, EUEGT2DialogueTopic::Hunger).ToString());

	FUEGT2DialogueState Weary = Person();
	Weary.Needs.Energy = 0.05f;
	TestNotEqual(TEXT("a tired answer differs from a rested one"),
		UEGT2DialogueAnswer(Weary, EUEGT2DialogueTopic::Rest).ToString(),
		UEGT2DialogueAnswer(State, EUEGT2DialogueTopic::Rest).ToString());

	FUEGT2DialogueState Bursting = Person();
	Bursting.Needs.Relief = 0.05f;
	TestNotEqual(TEXT("the bathroom answer tracks the need"),
		UEGT2DialogueAnswer(Bursting, EUEGT2DialogueTopic::Comfort).ToString(),
		UEGT2DialogueAnswer(State, EUEGT2DialogueTopic::Comfort).ToString());

	FUEGT2DialogueState Lonely = Person();
	Lonely.Needs.Company = 0.05f;
	TestNotEqual(TEXT("the company answer tracks the need"),
		UEGT2DialogueAnswer(Lonely, EUEGT2DialogueTopic::Company).ToString(),
		UEGT2DialogueAnswer(State, EUEGT2DialogueTopic::Company).ToString());

	// "How are you keeping" names what is actually wrong, and only that.
	FUEGT2DialogueState Rough = Person();
	Rough.Needs.Fed = 0.05f;
	Rough.Needs.Energy = 0.05f;
	const FString Summary = UEGT2DialogueAnswer(Rough, EUEGT2DialogueTopic::Wellbeing).ToString();
	TestTrue(TEXT("the summary mentions hunger"), Summary.Contains(TEXT("hungry")));
	TestTrue(TEXT("and tiredness"), Summary.Contains(TEXT("tired")));
	TestFalse(TEXT("but not the needs that are met"),
		Summary.Contains(TEXT("short of company")));

	// What they are doing comes from the decision, and says so when a need
	// drove it rather than the schedule.
	FUEGT2DialogueState Driven = Person();
	Driven.Activity = EUEGT2Activity::Eat;
	Driven.Reason = EUEGT2ActivityReason::Need;
	TestNotEqual(TEXT("a need-driven answer differs from a scheduled one"),
		UEGT2DialogueAnswer(Driven, EUEGT2DialogueTopic::Doing).ToString(),
		UEGT2DialogueAnswer(State, EUEGT2DialogueTopic::Doing).ToString());

	// Morning and evening greetings are not the same line.
	TestNotEqual(TEXT("morning and evening greetings differ"),
		UEGT2DialogueGreeting(Person(8.0f)).ToString(),
		UEGT2DialogueGreeting(Person(21.0f)).ToString());
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2DialogueTopicsTest,
	"UEGT2.NPC.DialogueTopics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2DialogueTopicsTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2DialogueTests;

	TArray<FUEGT2DialogueOption> Options;
	FUEGT2DialogueState State = Person();
	UEGT2DialogueTopics(State, Options);

	TestTrue(TEXT("there is something to ask"), Options.Num() > 4);
	TestTrue(TEXT("you can ask what they are doing"),
		Has(Options, EUEGT2DialogueTopic::Doing));
	TestTrue(TEXT("you can ask about the world"),
		Has(Options, EUEGT2DialogueTopic::World));
	TestTrue(TEXT("you can ask them along"),
		Has(Options, EUEGT2DialogueTopic::Follow));
	TestFalse(TEXT("but not send away someone who is not with you"),
		Has(Options, EUEGT2DialogueTopic::Dismiss));
	TestTrue(TEXT("and you can say goodbye"),
		Has(Options, EUEGT2DialogueTopic::Farewell));

	// Every option carries the text the button will show.
	for (const FUEGT2DialogueOption& Option : Options)
	{
		TestFalse(TEXT("every option is labelled"), Option.Prompt.IsEmpty());
	}

	// Once they are with you, the ask becomes a dismissal.
	State.bFollowing = true;
	UEGT2DialogueTopics(State, Options);
	TestTrue(TEXT("a companion can be sent on"),
		Has(Options, EUEGT2DialogueTopic::Dismiss));
	TestFalse(TEXT("and cannot be asked again"),
		Has(Options, EUEGT2DialogueTopic::Follow));

	// A dog is not a conversationalist.
	FUEGT2DialogueState Dog = Person();
	Dog.Species = EUEGT2NPCSpecies::Dog;
	UEGT2DialogueTopics(Dog, Options);
	TestFalse(TEXT("you cannot ask a dog about the mountains"),
		Has(Options, EUEGT2DialogueTopic::World));
	TestFalse(TEXT("nor what it does for a living"),
		Has(Options, EUEGT2DialogueTopic::Trade));
	TestTrue(TEXT("but you can call it after you"),
		Has(Options, EUEGT2DialogueTopic::Follow));
	TestTrue(TEXT("and it still answers"),
		!UEGT2DialogueAnswer(Dog, EUEGT2DialogueTopic::Hunger).IsEmpty());

	// Following and dismissing are the two that change the world.
	TestTrue(TEXT("follow is an action"),
		UEGT2DialogueIsAction(EUEGT2DialogueTopic::Follow));
	TestTrue(TEXT("dismiss is an action"),
		UEGT2DialogueIsAction(EUEGT2DialogueTopic::Dismiss));
	TestFalse(TEXT("asking about the weather is not"),
		UEGT2DialogueIsAction(EUEGT2DialogueTopic::World));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
