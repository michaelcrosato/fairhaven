// Fairhaven (UEGT2) - automation tests over the inhabitants.
//
// The whole life system was written so that the interesting half of it - the
// decision - is a pure function over a struct. That is what these tests exist
// to exploit: "a timid chicken with the player two metres away" is three lines
// here and would be a map, a pawn and a stopwatch otherwise.
//
// Actor behavior uses a small isolated world. Pathing over the baked network,
// population placement and rendered bubbles are covered by UEGT2ContentTests
// (which loads the map) and by looking at a screenshot.
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2NPCRoutines.h"
#include "NPC/UEGT2NPCSpeech.h"
#include "NPC/UEGT2NPCTypes.h"

namespace UEGT2NPCTests
{
	/** A context with nothing interesting happening: midday, clear, alone. */
	FUEGT2NPCContext Plain(float Hour)
	{
		FUEGT2NPCContext Context;
		Context.Hour = Hour;
		Context.DayIndex = 0;             // neither market day nor rest day
		Context.Weather = EUEGT2Weather::Clear;
		Context.PlayerDistance = 1.0e9f;
		Context.Seed = 12345;
		// Everything at 0.5 so GetEffectiveHour does not shift the hour and the
		// tests can name an exact time.
		Context.Personality.Sociability = 0.5f;
		Context.Personality.Punctuality = 0.5f;
		Context.Personality.Energy = 0.5f;
		Context.Personality.Curiosity = 0.0f;   // no detours in a fixed test
		Context.Personality.Bravery = 0.5f;
		return Context;
	}

	const TCHAR* ActivityName(EUEGT2Activity Activity)
	{
		static FString Buffer;
		Buffer = GetActivityDisplayName(Activity).ToString();
		return *Buffer;
	}
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RoutineTableTest,
	"UEGT2.NPC.RoutineTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RoutineTableTest::RunTest(const FString& Parameters)
{
	auto CheckRoutine = [this](const FUEGT2Routine& Routine, const FString& Label)
	{
		if (!TestTrue(FString::Printf(TEXT("%s has entries"), *Label), Routine.IsValid()))
		{
			return;
		}
		TestFalse(FString::Printf(TEXT("%s is named"), *Label), Routine.Name.IsNone());

		// EntryAt wraps backwards from any hour to the last row at or before
		// it. Without a row at zero there is nothing to wrap to just after
		// midnight, and the whole routine silently becomes the fallback.
		TestEqual(FString::Printf(TEXT("%s starts at hour 0"), *Label),
			Routine.Entries[0].StartHour, 0.0f);

		float Previous = -1.0f;
		for (const FUEGT2ScheduleEntry& Entry : Routine.Entries)
		{
			TestTrue(FString::Printf(TEXT("%s hours strictly increase"), *Label),
				Entry.StartHour > Previous);
			TestTrue(FString::Printf(TEXT("%s hours in range"), *Label),
				Entry.StartHour >= 0.0f && Entry.StartHour < 24.0f);
			TestTrue(FString::Printf(TEXT("%s activity in range"), *Label),
				(int32)Entry.Activity < (int32)EUEGT2Activity::Count);
			TestTrue(FString::Printf(TEXT("%s anchor in range"), *Label),
				(int32)Entry.Anchor < (int32)EUEGT2Anchor::Count);
			Previous = Entry.StartHour;
		}
	};

	for (int32 Index = 0; Index < (int32)EUEGT2NPCRole::Count; ++Index)
	{
		const EUEGT2NPCRole Role = (EUEGT2NPCRole)Index;
		CheckRoutine(GetRoleRoutine(Role), GetRoleDisplayName(Role).ToString());
	}
	for (int32 Index = 0; Index < (int32)EUEGT2NPCSpecies::Count; ++Index)
	{
		const EUEGT2NPCSpecies Species = (EUEGT2NPCSpecies)Index;
		CheckRoutine(GetSpeciesRoutine(Species), GetSpeciesDisplayName(Species).ToString());
	}
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RoutineLookupTest,
	"UEGT2.NPC.RoutineLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RoutineLookupTest::RunTest(const FString& Parameters)
{
	const FUEGT2Routine& Farmer = GetRoleRoutine(EUEGT2NPCRole::Farmer);

	TestEqual(TEXT("farmer is asleep at 02:00"),
		Farmer.EntryAt(2.0f).Activity, EUEGT2Activity::Sleep);
	TestEqual(TEXT("farmer is in the field at 08:00"),
		Farmer.EntryAt(8.0f).Activity, EUEGT2Activity::Work);
	TestEqual(TEXT("farmer works the field anchor"),
		Farmer.EntryAt(8.0f).Anchor, EUEGT2Anchor::Field);
	TestEqual(TEXT("farmer eats at 12:10"),
		Farmer.EntryAt(12.17f).Activity, EUEGT2Activity::Lunch);

	// The wrap cases: an hour of exactly 24 is hour 0, and out-of-range hours
	// fold rather than falling off the end of the table.
	TestEqual(TEXT("hour 24 is hour 0"),
		Farmer.EntryAt(24.0f).Activity, Farmer.EntryAt(0.0f).Activity);
	TestEqual(TEXT("hour 26 is hour 2"),
		Farmer.EntryAt(26.0f).Activity, Farmer.EntryAt(2.0f).Activity);
	TestEqual(TEXT("hour -1 is hour 23"),
		Farmer.EntryAt(-1.0f).Activity, Farmer.EntryAt(23.0f).Activity);

	// The baker is the awkward one: they work through midnight, so the row in
	// force at 01:00 is the row that starts at 00:00 and nothing earlier.
	const FUEGT2Routine& Baker = GetRoleRoutine(EUEGT2NPCRole::Baker);
	TestEqual(TEXT("baker bakes at 03:00"),
		Baker.EntryAt(3.0f).Activity, EUEGT2Activity::Work);
	TestEqual(TEXT("baker sleeps in the afternoon"),
		Baker.EntryAt(14.0f).Activity, EUEGT2Activity::Sleep);

	// NextChangeHour is what the director would use to know when to look again.
	TestTrue(TEXT("next change is later than now"), Farmer.NextChangeHour(8.0f) > 8.0f);
	TestEqual(TEXT("next change after the last row is midnight"),
		Farmer.NextChangeHour(23.5f), 24.0f);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2PersonalityTest,
	"UEGT2.NPC.Personality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2PersonalityTest::RunTest(const FString& Parameters)
{
	const FUEGT2Personality A = FUEGT2Personality::FromSeed(4242);
	const FUEGT2Personality B = FUEGT2Personality::FromSeed(4242);
	TestEqual(TEXT("the same seed is the same person"), A.Sociability, B.Sociability);
	TestEqual(TEXT("the same seed is the same person (bravery)"), A.Bravery, B.Bravery);

	// A trait that came out constant, or clamped to one end, would quietly turn
	// the whole population into one person.
	float MinTrait = 1.0f, MaxTrait = 0.0f, Sum = 0.0f;
	const int32 Samples = 400;
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const FUEGT2Personality P = FUEGT2Personality::FromSeed(1000 + Index * 17);
		const float Traits[] = { P.Sociability, P.Punctuality, P.Energy, P.Curiosity, P.Bravery };
		for (float Trait : Traits)
		{
			TestTrue(TEXT("trait in 0..1"), Trait >= 0.0f && Trait <= 1.0f);
			MinTrait = FMath::Min(MinTrait, Trait);
			MaxTrait = FMath::Max(MaxTrait, Trait);
		}
		Sum += P.Sociability;
	}
	TestTrue(TEXT("traits span most of the range"), MinTrait < 0.1f && MaxTrait > 0.9f);
	const float Mean = Sum / Samples;
	TestTrue(FString::Printf(TEXT("sociability averages near 0.5 (got %.3f)"), Mean),
		Mean > 0.4f && Mean < 0.6f);

	// Two traits drawn from the same seed must not be the same number, or
	// "brave" and "sociable" would always travel together.
	TestNotEqual(TEXT("traits are independent"), A.Sociability, A.Bravery);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NeedsTest,
	"UEGT2.NPC.Needs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2NeedsTest::RunTest(const FString& Parameters)
{
	FUEGT2NPCNeeds Needs;
	Needs.Energy = 0.2f;
	Needs.Advance(4.0f, EUEGT2Activity::Sleep);
	TestTrue(TEXT("sleeping restores energy"), Needs.Energy > 0.9f);
	TestTrue(TEXT("energy never exceeds 1"), Needs.Energy <= 1.0f);

	FUEGT2NPCNeeds Hungry;
	Hungry.Fed = 0.1f;
	Hungry.Advance(1.0f, EUEGT2Activity::Lunch);
	TestTrue(TEXT("eating feeds"), Hungry.Fed > 0.8f);

	FUEGT2NPCNeeds Working;
	Working.Advance(8.0f, EUEGT2Activity::Work);
	TestTrue(TEXT("a working day makes you hungry"), Working.Fed < 0.3f);
	TestTrue(TEXT("needs never go below zero"), Working.Fed >= 0.0f);

	FUEGT2NPCNeeds Lonely;
	Lonely.Company = 0.05f;
	Lonely.Advance(1.0f, EUEGT2Activity::Tavern);
	TestTrue(TEXT("the tavern is company"), Lonely.Company > 0.4f);

	// A zero or negative slice must be a no-op rather than running backwards.
	FUEGT2NPCNeeds Frozen;
	const float Before = Frozen.Fed;
	Frozen.Advance(0.0f, EUEGT2Activity::Work);
	Frozen.Advance(-3.0f, EUEGT2Activity::Work);
	TestEqual(TEXT("no time, no change"), Frozen.Fed, Before);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ScheduleDecisionTest,
	"UEGT2.NPC.ScheduleDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ScheduleDecisionTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NPCTests;

	// An ordinary day: the routine, and nothing but the routine.
	const FUEGT2ActivityDecision Morning = ResolveActivity(EUEGT2NPCRole::Farmer,
		EUEGT2NPCSpecies::Person, Plain(6.5f));
	TestEqual(TEXT("farmer works at 06:30"), Morning.Activity, EUEGT2Activity::Work);
	TestEqual(TEXT("farmer is in the field"), Morning.Anchor, EUEGT2Anchor::Field);
	TestEqual(TEXT("and the reason is the routine"), Morning.Reason,
		EUEGT2ActivityReason::Schedule);

	const FUEGT2ActivityDecision Night = ResolveActivity(EUEGT2NPCRole::Villager,
		EUEGT2NPCSpecies::Person, Plain(1.0f));
	TestEqual(TEXT("villager sleeps at 01:00"), Night.Activity, EUEGT2Activity::Sleep);
	TestEqual(TEXT("at home"), Night.Anchor, EUEGT2Anchor::Home);

	// Punctuality shifts the transition, which is what stops the whole town
	// changing activity on the same frame.
	FUEGT2NPCContext Early = Plain(8.9f);
	Early.Personality.Punctuality = 1.0f;
	FUEGT2NPCContext Late = Plain(8.9f);
	Late.Personality.Punctuality = 0.0f;
	const EUEGT2Activity EarlyActivity =
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, Early).Activity;
	const EUEGT2Activity LateActivity =
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, Late).Activity;
	TestEqual(TEXT("the punctual villager has already started work"),
		EarlyActivity, EUEGT2Activity::Work);
	TestEqual(TEXT("the unpunctual one is still out strolling"),
		LateActivity, EUEGT2Activity::Stroll);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2WeatherDecisionTest,
	"UEGT2.NPC.WeatherDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2WeatherDecisionTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NPCTests;

	FUEGT2NPCContext Storm = Plain(10.0f);
	Storm.Weather = EUEGT2Weather::Storm;

	const FUEGT2ActivityDecision Caught = ResolveActivity(EUEGT2NPCRole::Villager,
		EUEGT2NPCSpecies::Person, Storm);
	TestEqual(TEXT("a storm drives a villager under cover"),
		Caught.Activity, EUEGT2Activity::Shelter);
	TestEqual(TEXT("and toward a shelter anchor"), Caught.Anchor, EUEGT2Anchor::Shelter);
	TestEqual(TEXT("for the weather"), Caught.Reason, EUEGT2ActivityReason::Weather);

	// The stubborn stay out. Two ways to be stubborn: brave, or a trade whose
	// work does not stop for weather.
	FUEGT2NPCContext BraveStorm = Storm;
	BraveStorm.Personality.Bravery = 0.95f;
	TestEqual(TEXT("the brave stay out in it"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, BraveStorm).Activity,
		EUEGT2Activity::Work);

	FUEGT2NPCContext FisherStorm = Storm;
	FisherStorm.Personality.Bravery = 0.6f;
	TestEqual(TEXT("a fisher in a storm is still a fisher"),
		ResolveActivity(EUEGT2NPCRole::Fisher, EUEGT2NPCSpecies::Person, FisherStorm).Activity,
		EUEGT2Activity::Work);

	// Animals shelter in the coop, not in a doorway.
	const FUEGT2ActivityDecision Hen = ResolveActivity(EUEGT2NPCRole::Villager,
		EUEGT2NPCSpecies::Chicken, Storm);
	TestEqual(TEXT("a hen gets into the coop"), Hen.Activity, EUEGT2Activity::Shelter);
	TestEqual(TEXT("the coop, specifically"), Hen.Anchor, EUEGT2Anchor::Coop);

	// Clear weather changes nothing, which is the case that would silently
	// break if the rain rule ever stopped checking the weather.
	TestEqual(TEXT("clear weather leaves the routine alone"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, Plain(10.0f)).Activity,
		EUEGT2Activity::Work);

	// Somebody already indoors is not rained on.
	FUEGT2NPCContext Inside = Storm;
	Inside.bExposed = false;
	TestEqual(TEXT("being inside already is enough"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, Inside).Activity,
		EUEGT2Activity::Work);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ProximityDecisionTest,
	"UEGT2.NPC.ProximityDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ProximityDecisionTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NPCTests;

	FUEGT2NPCContext Close = Plain(10.0f);
	Close.PlayerDistance = 200.0f;

	FUEGT2NPCContext Sociable = Close;
	Sociable.Personality.Sociability = 0.9f;
	const FUEGT2ActivityDecision Chat = ResolveActivity(EUEGT2NPCRole::Villager,
		EUEGT2NPCSpecies::Person, Sociable);
	TestEqual(TEXT("a sociable villager stops to talk"),
		Chat.Activity, EUEGT2Activity::Socialise);
	TestEqual(TEXT("because of the player"), Chat.Reason, EUEGT2ActivityReason::Player);

	FUEGT2NPCContext Shy = Close;
	Shy.Personality.Sociability = 0.1f;
	TestEqual(TEXT("a shy one keeps working"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, Shy).Activity,
		EUEGT2Activity::Work);

	// Nobody is woken up to be sociable at. This is the rule that stops the
	// town from standing up at three in the morning when you walk through it.
	FUEGT2NPCContext CloseAtNight = Sociable;
	CloseAtNight.Hour = 2.0f;
	TestEqual(TEXT("standing over a sleeping villager does not wake them"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, CloseAtNight).Activity,
		EUEGT2Activity::Sleep);

	// Animals: timid ones bolt, dogs come over, cattle do not care.
	const FUEGT2ActivityDecision Hen = ResolveActivity(EUEGT2NPCRole::Villager,
		EUEGT2NPCSpecies::Chicken, Close);
	TestEqual(TEXT("a chicken bolts"), Hen.Activity, EUEGT2Activity::Flee);

	FUEGT2NPCContext DogClose = Close;
	DogClose.PlayerDistance = 700.0f;
	TestEqual(TEXT("a dog comes over"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Dog, DogClose).Activity,
		EUEGT2Activity::Follow);

	TestEqual(TEXT("a cow carries on grazing"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Cow, Close).Activity,
		EUEGT2Activity::Graze);

	// Bravery widens the flee radius, so a flock scatters raggedly instead of
	// all at once.
	FUEGT2NPCContext BraveHen = Close;
	BraveHen.PlayerDistance = 500.0f;
	BraveHen.Personality.Bravery = 1.0f;
	TestEqual(TEXT("the bold hen holds its ground"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Chicken, BraveHen).Activity,
		EUEGT2Activity::Forage);

	// Flee radii must be positive for the timid species and zero for the rest,
	// or the whole rule silently applies to nobody.
	TestTrue(TEXT("chickens have a flee radius"), GetFleeRadius(EUEGT2NPCSpecies::Chicken) > 0.0f);
	TestEqual(TEXT("cows do not"), GetFleeRadius(EUEGT2NPCSpecies::Cow), 0.0f);
	TestEqual(TEXT("people do not"), GetFleeRadius(EUEGT2NPCSpecies::Person), 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NeedDecisionTest,
	"UEGT2.NPC.NeedDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2NeedDecisionTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NPCTests;

	// Hunger. It used to be answered only between eleven and three, which meant
	// a villager could be starving at half nine and keep working.
	FUEGT2NPCContext Hungry = Plain(12.0f);
	Hungry.Needs.Fed = 0.05f;
	const FUEGT2ActivityDecision Meal = ResolveActivity(EUEGT2NPCRole::Villager,
		EUEGT2NPCSpecies::Person, Hungry);
	TestEqual(TEXT("a hungry worker goes to eat"), Meal.Activity, EUEGT2Activity::Eat);
	TestEqual(TEXT("at the nearest food"), Meal.Anchor, EUEGT2Anchor::Food);
	TestEqual(TEXT("driven by a need"), Meal.Reason, EUEGT2ActivityReason::Need);

	FUEGT2NPCContext HungryEarly = Hungry;
	HungryEarly.Hour = 9.5f;
	TestEqual(TEXT("and is still hungry at half nine"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, HungryEarly).Activity,
		EUEGT2Activity::Eat);

	// The bathroom.
	FUEGT2NPCContext Bursting = Plain(14.0f);
	Bursting.Needs.Relief = 0.02f;
	const FUEGT2ActivityDecision Wash = ResolveActivity(EUEGT2NPCRole::Clerk,
		EUEGT2NPCSpecies::Person, Bursting);
	TestEqual(TEXT("a desperate clerk finds a washroom"), Wash.Activity,
		EUEGT2Activity::Washroom);
	TestEqual(TEXT("at the nearest one"), Wash.Anchor, EUEGT2Anchor::Washroom);

	// Tiredness in the day is answered by sitting down, at night by going to bed.
	FUEGT2NPCContext Weary = Plain(13.0f);
	Weary.Needs.Energy = 0.03f;
	const FUEGT2ActivityDecision SitDown = ResolveActivity(EUEGT2NPCRole::Villager,
		EUEGT2NPCSpecies::Person, Weary);
	TestEqual(TEXT("a tired villager sits down"), SitDown.Activity, EUEGT2Activity::Rest);
	TestEqual(TEXT("on the nearest bench"), SitDown.Anchor, EUEGT2Anchor::Seat);

	FUEGT2NPCContext Exhausted = Plain(21.5f);
	Exhausted.Needs.Energy = 0.03f;
	const FUEGT2ActivityDecision Nightly = ResolveActivity(EUEGT2NPCRole::Villager,
		EUEGT2NPCSpecies::Person, Exhausted);
	// At night the *place* changes and the activity must not. It used to become
	// HomeTime, which restores nothing - so Energy stayed the worst need for
	// ever, the answer never came, and Fed and Relief ran to zero behind it
	// while the villager walked home for six hours. UEGT2.Economy.LivingWage is
	// what found that; this is what stops it coming back.
	TestEqual(TEXT("but at night rests at home"), Nightly.Activity, EUEGT2Activity::Rest);
	TestEqual(TEXT("in their own chair, not on a bench in the dark"),
		Nightly.Anchor, EUEGT2Anchor::Home);

	// Company.
	FUEGT2NPCContext Lonely = Plain(10.0f);
	Lonely.Needs.Company = 0.02f;
	Lonely.Personality.Sociability = 0.9f;
	TestEqual(TEXT("a lonely villager goes looking for company"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, Lonely).Activity,
		EUEGT2Activity::Socialise);

	// The worst need wins, and nothing fires while everything is comfortable.
	FUEGT2NPCContext Both = Plain(12.0f);
	Both.Needs.Fed = 0.20f;      // under its threshold, but only just
	Both.Needs.Relief = 0.02f;   // desperate
	TestEqual(TEXT("the worse of two needs wins"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, Both).Activity,
		EUEGT2Activity::Washroom);

	FUEGT2NPCContext Content = Plain(12.0f);
	Content.Needs.Energy = 0.9f;
	Content.Needs.Fed = 0.9f;
	Content.Needs.Relief = 0.9f;
	Content.Needs.Company = 0.9f;
	TestEqual(TEXT("a contented villager just works"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, Content).Activity,
		EUEGT2Activity::Work);

	// Money changes what you can do about a need. Somebody who cannot pay for
	// the answer goes and earns instead of standing at a counter being refused.
	FUEGT2NPCContext Broke = Plain(12.0f);
	Broke.Needs.Fed = 0.05f;
	Broke.Purse.Coins = 0.0f;
	const FUEGT2ActivityDecision Skint = ResolveActivity(EUEGT2NPCRole::Villager,
		EUEGT2NPCSpecies::Person, Broke);
	TestEqual(TEXT("a hungry villager with no coin goes to work"),
		Skint.Activity, EUEGT2Activity::Work);
	TestEqual(TEXT("at their workplace"), Skint.Anchor, EUEGT2Anchor::Work);
	TestEqual(TEXT("and it is still the need driving it"),
		Skint.Reason, EUEGT2ActivityReason::Need);

	// The free answers are unaffected by an empty purse: a bench costs nothing.
	FUEGT2NPCContext BrokeAndTired = Plain(13.0f);
	BrokeAndTired.Needs.Energy = 0.03f;
	BrokeAndTired.Purse.Coins = 0.0f;
	TestEqual(TEXT("but sitting down is still free"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, BrokeAndTired).Activity,
		EUEGT2Activity::Rest);

	// The bathroom need does not run down while you are asleep.
	FUEGT2NPCNeeds Sleeping;
	Sleeping.Relief = 0.5f;
	Sleeping.Advance(6.0f, EUEGT2Activity::Sleep);
	TestTrue(TEXT("you do not need the bathroom in your sleep"), Sleeping.Relief > 0.3f);

	FUEGT2NPCNeeds Used;
	Used.Relief = 0.05f;
	Used.Advance(0.5f, EUEGT2Activity::Washroom);
	TestTrue(TEXT("and a washroom answers it"), Used.Relief > 0.7f);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2WeekdayDecisionTest,
	"UEGT2.NPC.WeekdayDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2WeekdayDecisionTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NPCTests;

	// Exactly one of each per week, and the modulo must survive a negative
	// index rather than moving market day.
	int32 MarketDays = 0, RestDays = 0;
	for (int32 Day = 0; Day < UEGT2DaysPerWeek; ++Day)
	{
		MarketDays += IsMarketDay(Day) ? 1 : 0;
		RestDays += IsRestDay(Day) ? 1 : 0;
	}
	TestEqual(TEXT("one market day a week"), MarketDays, 1);
	TestEqual(TEXT("one rest day a week"), RestDays, 1);
	TestEqual(TEXT("the week repeats"), IsMarketDay(2), IsMarketDay(2 + UEGT2DaysPerWeek));
	TestEqual(TEXT("and folds backwards"), IsMarketDay(2), IsMarketDay(2 - UEGT2DaysPerWeek));

	FUEGT2NPCContext Market = Plain(10.0f);
	Market.DayIndex = 2;
	const FUEGT2ActivityDecision Stall = ResolveActivity(EUEGT2NPCRole::Villager,
		EUEGT2NPCSpecies::Person, Market);
	TestEqual(TEXT("market day empties the workshops"), Stall.Activity, EUEGT2Activity::Market);
	TestEqual(TEXT("into the market"), Stall.Anchor, EUEGT2Anchor::Market);
	TestEqual(TEXT("because of the day"), Stall.Reason, EUEGT2ActivityReason::DayOfWeek);

	FUEGT2NPCContext Rest = Plain(10.0f);
	Rest.DayIndex = 5;
	const FUEGT2ActivityDecision Church = ResolveActivity(EUEGT2NPCRole::Villager,
		EUEGT2NPCSpecies::Person, Rest);
	TestEqual(TEXT("rest day morning fills the church"),
		Church.Activity, EUEGT2Activity::Worship);
	TestEqual(TEXT("at the church"), Church.Anchor, EUEGT2Anchor::Church);

	// The innkeeper works the rest day, because that is when the trade is.
	FUEGT2NPCContext InnRest = Plain(14.0f);
	InnRest.DayIndex = 5;
	TestEqual(TEXT("the innkeeper works the rest day"),
		ResolveActivity(EUEGT2NPCRole::Innkeeper, EUEGT2NPCSpecies::Person, InnRest).Activity,
		EUEGT2Activity::Work);

	// Animals keep no calendar.
	FUEGT2NPCContext SheepRest = Plain(10.0f);
	SheepRest.DayIndex = 5;
	TestEqual(TEXT("sheep do not observe the rest day"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Sheep, SheepRest).Activity,
		EUEGT2Activity::Graze);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2DetourTest,
	"UEGT2.NPC.Detours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2DetourTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NPCTests;

	// Curiosity produces errands. The point of the test is that it produces
	// them for *some* seeds and not all: a rate of zero means the rule is dead,
	// and a rate of one means nobody ever does their job.
	int32 Detours = 0;
	const int32 Samples = 300;
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		FUEGT2NPCContext Curious = Plain(10.0f);
		Curious.Seed = 7000 + Index * 13;
		Curious.Personality.Curiosity = 1.0f;
		const FUEGT2ActivityDecision Decision = ResolveActivity(EUEGT2NPCRole::Villager,
			EUEGT2NPCSpecies::Person, Curious);
		if (Decision.Activity == EUEGT2Activity::Errand)
		{
			TestEqual(TEXT("a detour is reported as a detour"), Decision.Reason,
				EUEGT2ActivityReason::Detour);
			++Detours;
		}
	}
	TestTrue(FString::Printf(TEXT("curiosity produces some detours (got %d/%d)"),
		Detours, Samples), Detours > 5);
	TestTrue(FString::Printf(TEXT("but most people still work (got %d/%d)"),
		Detours, Samples), Detours < Samples / 3);

	// Zero curiosity never detours, and the same seed always makes the same
	// choice - a habit, not a dice roll.
	FUEGT2NPCContext Dull = Plain(10.0f);
	Dull.Personality.Curiosity = 0.0f;
	TestEqual(TEXT("no curiosity, no detour"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, Dull).Activity,
		EUEGT2Activity::Work);

	FUEGT2NPCContext Repeat = Plain(10.0f);
	Repeat.Personality.Curiosity = 1.0f;
	Repeat.Seed = 999;
	TestEqual(TEXT("the same person makes the same choice"),
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, Repeat).Activity,
		ResolveActivity(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, Repeat).Activity);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2SpeechCoverageTest,
	"UEGT2.NPC.SpeechCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2SpeechCoverageTest::RunTest(const FString& Parameters)
{
	int32 Checked = 0;
	int32 Longest = 0;
	FString LongestLine;

	for (int32 RoleIndex = 0; RoleIndex < (int32)EUEGT2NPCRole::Count; ++RoleIndex)
	{
		for (int32 ActivityIndex = 0; ActivityIndex < (int32)EUEGT2Activity::Count; ++ActivityIndex)
		{
			for (int32 MoodIndex = 0; MoodIndex < (int32)EUEGT2SpeechMood::Count; ++MoodIndex)
			{
				const EUEGT2NPCRole Role = (EUEGT2NPCRole)RoleIndex;
				const EUEGT2Activity Activity = (EUEGT2Activity)ActivityIndex;
				const EUEGT2SpeechMood Mood = (EUEGT2SpeechMood)MoodIndex;

				const TArray<FText>& Pool = GetSpeechPool(Role, EUEGT2NPCSpecies::Person,
					Activity, Mood, EUEGT2Weather::Clear, 12.0f);
				if (!TestTrue(FString::Printf(TEXT("pool for role %d activity %d mood %d is not empty"),
					RoleIndex, ActivityIndex, MoodIndex), Pool.Num() > 0))
				{
					continue;
				}
				for (const FText& Line : Pool)
				{
					const FString Text = Line.ToString();
					TestFalse(TEXT("no empty lines"), Text.IsEmpty());
					if (Text.Len() > Longest)
					{
						Longest = Text.Len();
						LongestLine = Text;
					}
					++Checked;
				}
			}
		}
	}

	// The bubble wraps at roughly forty characters a row. Anything past the
	// budget turns a message into a monologue hanging over the town square.
	TestTrue(FString::Printf(TEXT("longest line fits the bubble: %d chars, \"%s\""),
		Longest, *LongestLine), Longest <= UEGT2MaxSpeechLength);
	AddInfo(FString::Printf(TEXT("%d lines checked, longest %d chars"), Checked, Longest));

	// Every animal makes a noise, whatever it is doing.
	for (int32 Index = 1; Index < (int32)EUEGT2NPCSpecies::Count; ++Index)
	{
		const EUEGT2NPCSpecies Species = (EUEGT2NPCSpecies)Index;
		const FText Line = GetSpeechLine(EUEGT2NPCRole::Villager, Species,
			EUEGT2Activity::Idle, EUEGT2SpeechMood::Idle, EUEGT2Weather::Clear, 12.0f, 5u);
		TestFalse(FString::Printf(TEXT("%s makes a sound"),
			*GetSpeciesDisplayName(Species).ToString()), Line.IsEmpty());
	}

	// Weather comments must actually differ by weather, or a storm sounds like
	// a clear day.
	const FText Clear = GetSpeechLine(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person,
		EUEGT2Activity::Stroll, EUEGT2SpeechMood::Comment, EUEGT2Weather::Clear, 12.0f, 3u);
	const FText Storm = GetSpeechLine(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person,
		EUEGT2Activity::Stroll, EUEGT2SpeechMood::Comment, EUEGT2Weather::Storm, 12.0f, 3u);
	TestNotEqual(TEXT("a storm is remarked on differently"),
		Clear.ToString(), Storm.ToString());
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2SpeechSelectionTest,
	"UEGT2.NPC.SpeechSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2SpeechSelectionTest::RunTest(const FString& Parameters)
{
	// Same situation, same seed, same words: an NPC that said something
	// different every time you looked at it would not read as a person.
	const FText First = GetSpeechLine(EUEGT2NPCRole::Farmer, EUEGT2NPCSpecies::Person,
		EUEGT2Activity::Work, EUEGT2SpeechMood::Announce, EUEGT2Weather::Clear, 8.0f, 4242u);
	const FText Again = GetSpeechLine(EUEGT2NPCRole::Farmer, EUEGT2NPCSpecies::Person,
		EUEGT2Activity::Work, EUEGT2SpeechMood::Announce, EUEGT2Weather::Clear, 8.0f, 4242u);
	TestEqual(TEXT("the same person says the same thing"), First.ToString(), Again.ToString());

	// Across a population the pool must actually be used, not collapse onto one
	// favourite line.
	TSet<FString> Distinct;
	for (uint32 Seed = 0; Seed < 200u; ++Seed)
	{
		Distinct.Add(GetSpeechLine(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person,
			EUEGT2Activity::Market, EUEGT2SpeechMood::Announce,
			EUEGT2Weather::Clear, 12.0f, Seed * 37u).ToString());
	}
	TestTrue(FString::Printf(TEXT("the pool is spread over the population (got %d)"),
		Distinct.Num()), Distinct.Num() >= 4);

	// Variation is what makes the second half of a conversation, and asking
	// twice, land on something new.
	TSet<FString> Variations;
	for (uint32 Variation = 0; Variation < 12u; ++Variation)
	{
		Variations.Add(GetSpeechLine(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person,
			EUEGT2Activity::Work, EUEGT2SpeechMood::Reply,
			EUEGT2Weather::Clear, 12.0f, 77u, Variation).ToString());
	}
	TestTrue(TEXT("variation moves through the pool"), Variations.Num() > 1);

	// A trade voice must displace the generic one, or every worker in town says
	// "back at it".
	const FString FarmerWork = GetSpeechLine(EUEGT2NPCRole::Farmer, EUEGT2NPCSpecies::Person,
		EUEGT2Activity::Work, EUEGT2SpeechMood::Announce, EUEGT2Weather::Clear, 8.0f, 11u).ToString();
	const FString SmithWork = GetSpeechLine(EUEGT2NPCRole::Smith, EUEGT2NPCSpecies::Person,
		EUEGT2Activity::Work, EUEGT2SpeechMood::Announce, EUEGT2Weather::Clear, 8.0f, 11u).ToString();
	TestNotEqual(TEXT("a farmer and a smith do not describe the same job"),
		FarmerWork, SmithWork);

	// Greetings follow the clock.
	const FString Morning = GetSpeechLine(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person,
		EUEGT2Activity::Stroll, EUEGT2SpeechMood::Greet, EUEGT2Weather::Clear, 9.0f, 5u).ToString();
	const FString Midnight = GetSpeechLine(EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person,
		EUEGT2Activity::Stroll, EUEGT2SpeechMood::Greet, EUEGT2Weather::Clear, 23.0f, 5u).ToString();
	TestNotEqual(TEXT("a greeting at nine is not a greeting at eleven at night"),
		Morning, Midnight);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NPCUtilityTest,
	"UEGT2.NPC.Utility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2NPCUtilityTest::RunTest(const FString& Parameters)
{
	// The hash underpins every stable choice in the system: which line, which
	// jitter, who is hidden at 50% crowd density. A biased or clamped hash
	// would show up as the same villagers always being the ones who talk.
	float Min = 1.0f, Max = 0.0f, Sum = 0.0f;
	const int32 Samples = 2000;
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const float Value = UEGT2HashUnit((uint32)Index, 17u, 99u);
		TestTrue(TEXT("hash in [0,1)"), Value >= 0.0f && Value < 1.0f);
		Min = FMath::Min(Min, Value);
		Max = FMath::Max(Max, Value);
		Sum += Value;
	}
	const float Mean = Sum / Samples;
	TestTrue(FString::Printf(TEXT("hash is roughly uniform (mean %.3f)"), Mean),
		Mean > 0.45f && Mean < 0.55f);
	TestTrue(TEXT("hash spans the range"), Min < 0.02f && Max > 0.98f);
	TestEqual(TEXT("hash is stable"), UEGT2HashSeed(1u, 2u, 3u), UEGT2HashSeed(1u, 2u, 3u));
	TestNotEqual(TEXT("hash separates its arguments"),
		UEGT2HashSeed(1u, 2u, 3u), UEGT2HashSeed(3u, 2u, 1u));

	// Pace: every activity has to move at some positive speed, and the ones
	// that are supposed to be urgent have to actually be faster.
	for (int32 Index = 0; Index < (int32)EUEGT2Activity::Count; ++Index)
	{
		TestTrue(TEXT("pace is positive"), GetActivityPace((EUEGT2Activity)Index) > 0.0f);
	}
	TestTrue(TEXT("fleeing beats strolling"),
		GetActivityPace(EUEGT2Activity::Flee) > GetActivityPace(EUEGT2Activity::Stroll));
	TestTrue(TEXT("running for shelter beats a commute"),
		GetActivityPace(EUEGT2Activity::Shelter) > GetActivityPace(EUEGT2Activity::Commute));
	TestTrue(TEXT("grazing is slow"),
		GetActivityPace(EUEGT2Activity::Graze) < GetActivityPace(EUEGT2Activity::Work));

	// Indoor activities are the ones that hide the actor. Getting this wrong
	// leaves people standing inside walls, or standing outside all night.
	TestTrue(TEXT("sleeping is indoors"), IsIndoorActivity(EUEGT2Activity::Sleep));
	TestTrue(TEXT("dinner is indoors"), IsIndoorActivity(EUEGT2Activity::Dinner));
	TestFalse(TEXT("working is not"), IsIndoorActivity(EUEGT2Activity::Work));
	TestFalse(TEXT("sheltering is not - you can see them under the awning"),
		IsIndoorActivity(EUEGT2Activity::Shelter));

	TestTrue(TEXT("a dog is an animal"), IsAnimalSpecies(EUEGT2NPCSpecies::Dog));
	TestFalse(TEXT("a person is not"), IsAnimalSpecies(EUEGT2NPCSpecies::Person));
	TestTrue(TEXT("a storm is wet"), IsWetWeather(EUEGT2Weather::Storm));
	TestFalse(TEXT("fog is not"), IsWetWeather(EUEGT2Weather::Foggy));

	// Display names: the bubble header and the dev overlay both print these.
	for (int32 Index = 0; Index < (int32)EUEGT2Activity::Count; ++Index)
	{
		TestFalse(TEXT("every activity has a name"),
			GetActivityDisplayName((EUEGT2Activity)Index).IsEmpty());
	}
	for (int32 Index = 0; Index < (int32)EUEGT2NPCRole::Count; ++Index)
	{
		TestFalse(TEXT("every role has a name"),
			GetRoleDisplayName((EUEGT2NPCRole)Index).IsEmpty());
	}
	for (int32 Index = 0; Index < (int32)EUEGT2Anchor::Count; ++Index)
	{
		TestTrue(TEXT("every anchor has a name"),
			FCString::Strlen(GetAnchorName((EUEGT2Anchor)Index)) > 0);
	}
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2FullDayTest,
	"UEGT2.NPC.FullDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2FullDayTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NPCTests;

	// Walk a whole day for every role and check the shape of it. This is the
	// test that catches a routine edited into nonsense - a trade that never
	// sleeps, or one that never leaves the house.
	for (int32 RoleIndex = 0; RoleIndex < (int32)EUEGT2NPCRole::Count; ++RoleIndex)
	{
		const EUEGT2NPCRole Role = (EUEGT2NPCRole)RoleIndex;
		const FString Name = GetRoleDisplayName(Role).ToString();

		TSet<EUEGT2Activity> Seen;
		TSet<EUEGT2Anchor> Anchors;
		int32 SleepSamples = 0;
		int32 OutdoorSamples = 0;

		for (int32 Step = 0; Step < 96; ++Step)          // every quarter hour
		{
			FUEGT2NPCContext Context = Plain(Step * 0.25f);
			const FUEGT2ActivityDecision Decision = ResolveActivity(Role,
				EUEGT2NPCSpecies::Person, Context);
			Seen.Add(Decision.Activity);
			Anchors.Add(Decision.Anchor);
			SleepSamples += (Decision.Activity == EUEGT2Activity::Sleep) ? 1 : 0;
			OutdoorSamples += IsIndoorActivity(Decision.Activity) ? 0 : 1;
		}

		TestTrue(FString::Printf(TEXT("%s sleeps at some point"), *Name), SleepSamples > 0);
		TestTrue(FString::Printf(TEXT("%s does not sleep all day"), *Name), SleepSamples < 80);
		TestTrue(FString::Printf(TEXT("%s leaves the house"), *Name), OutdoorSamples > 24);
		TestTrue(FString::Printf(TEXT("%s does more than three things (got %d)"),
			*Name, Seen.Num()), Seen.Num() >= 4);
		TestTrue(FString::Printf(TEXT("%s visits more than one place (got %d)"),
			*Name, Anchors.Num()), Anchors.Num() >= 3);
	}

	// Animals too, but a sheep is allowed a much smaller day than a courier.
	for (int32 Index = 1; Index < (int32)EUEGT2NPCSpecies::Count; ++Index)
	{
		const EUEGT2NPCSpecies Species = (EUEGT2NPCSpecies)Index;
		const FString Name = GetSpeciesDisplayName(Species).ToString();
		TSet<EUEGT2Activity> Seen;
		int32 RoostSamples = 0;
		for (int32 Step = 0; Step < 96; ++Step)
		{
			const FUEGT2ActivityDecision Decision = ResolveActivity(EUEGT2NPCRole::Villager,
				Species, Plain(Step * 0.25f));
			Seen.Add(Decision.Activity);
			RoostSamples += (Decision.Activity == EUEGT2Activity::Roost) ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("%s rests at some point"), *Name), RoostSamples > 0);
		TestTrue(FString::Printf(TEXT("%s does more than one thing (got %d)"),
			*Name, Seen.Num()), Seen.Num() >= 2);
	}
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NPCFollowingTest,
	"UEGT2.NPC.Following",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2NPCFollowingTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("isolated actor world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AUEGT2NPCActor* NPC = World->SpawnActor<AUEGT2NPCActor>();
	AUEGT2NPCActor* Target = World->SpawnActor<AUEGT2NPCActor>();
	if (!TestNotNull(TEXT("companion"), NPC)
		|| !TestNotNull(TEXT("follow target"), Target)) { return false; }
	NPC->ConfigureNPC(TEXT("Test smith"), EUEGT2NPCRole::Smith,
		EUEGT2NPCSpecies::Person, 4242);
	NPC->AddAnchor(EUEGT2Anchor::Work, FVector(5000.0, 0.0, 0.0));
	NPC->DispatchBeginPlay();
	NPC->SetLOD(EUEGT2NPCLOD::Near);
	FUEGT2NPCContext Context = UEGT2NPCTests::Plain(10.0f);
	NPC->EvaluateSchedule(Context, true);
	TestTrue(TEXT("the old route leads away from the player"),
		FVector::Dist2D(NPC->GetActorLocation(), NPC->GetDestination()) > 1000.0);
	Target->SetActorLocation(FVector(200.0, 0.0, 0.0));
	const FVector WaitingAt = NPC->GetActorLocation();
	NPC->SetFollowTarget(Target);
	TestEqual(TEXT("agreeing to follow changes activity immediately"),
		NPC->GetActivity(), EUEGT2Activity::Follow);
	NPC->EvaluateSchedule(Context, false);
	TestEqual(TEXT("the routine cannot replace a companion's activity"),
		NPC->GetActivity(), EUEGT2Activity::Follow);
	const float Coins = NPC->GetPurse().Coins;
	NPC->AdvanceNeeds(0.1f);
	TestEqual(TEXT("walking with the player does not earn a smith's wage"),
		NPC->GetPurse().Coins, Coins);

	// Long enough for the old job's arrival drift to fire, if either the stale
	// destination or the routine activity survived the follow request.
	for (int32 Tick = 0; Tick < 100; ++Tick) { NPC->Tick(0.1f); }
	TestTrue(TEXT("a nearby companion stays put instead of following the old route"),
		FVector::Dist2D(NPC->GetActorLocation(), WaitingAt) < 1.0);
	Target->SetActorLocation(FVector(1200.0, 0.0, 0.0));
	for (int32 Tick = 0; Tick < 20; ++Tick) { NPC->Tick(0.1f); }
	TestTrue(TEXT("the companion resumes walking when the target moves away"),
		NPC->GetActorLocation().X > WaitingAt.X + 100.0);

	NPC->SetFollowTarget(nullptr);
	NPC->EvaluateSchedule(Context, false);
	TestFalse(TEXT("dismissal stops following"), NPC->IsFollowing());
	TestNotEqual(TEXT("dismissal restores the routine activity"),
		NPC->GetActivity(), EUEGT2Activity::Follow);
	TestTrue(TEXT("dismissal restores the workplace route"), NPC->GetDestination().X > 4000.0);

	NPC->SetFollowTarget(Target);
	NPC->AdvanceNeeds(10.0f);
	Context.PlayerDistance = 100.0f;
	NPC->EvaluateSchedule(Context, false);
	TestFalse(TEXT("a desperate companion breaks off even beside the player"), NPC->IsFollowing());
	TestEqual(TEXT("the need survives the proximity greeting"),
		NPC->GetActivityReason(), EUEGT2ActivityReason::Need);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NPCFollowingSafetyTest,
	"UEGT2.NPC.FollowingSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2NPCFollowingSafetyTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("isolated actor world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AUEGT2NPCActor* Target = World->SpawnActor<AUEGT2NPCActor>();
	if (!TestNotNull(TEXT("follow target"), Target)) { return false; }
	const auto Companion = [World, Target](EUEGT2NPCSpecies Species, bool bBrave)
	{
		const int32 Seed = bBrave ? 13 : 1;
		AUEGT2NPCActor* NPC = World->SpawnActor<AUEGT2NPCActor>();
		if (NPC)
		{
			NPC->ConfigureNPC(TEXT("Companion"), EUEGT2NPCRole::Smith, Species, Seed);
			NPC->AddAnchor(EUEGT2Anchor::Home, FVector(8000.0, 0.0, 0.0));
			NPC->AddAnchor(EUEGT2Anchor::Shelter, FVector(-5000.0, 0.0, 0.0));
			NPC->DispatchBeginPlay();
			NPC->SetLOD(EUEGT2NPCLOD::Near);
			NPC->SetFollowTarget(Target);
		}
		return NPC;
	};

	AUEGT2NPCActor* Timid = Companion(EUEGT2NPCSpecies::Person, false);
	AUEGT2NPCActor* Brave = Companion(EUEGT2NPCSpecies::Person, true);
	AUEGT2NPCActor* Dog = Companion(EUEGT2NPCSpecies::Dog, false);
	if (!TestNotNull(TEXT("timid companion"), Timid) || !TestNotNull(TEXT("brave companion"), Brave)
		|| !TestNotNull(TEXT("animal companion"), Dog)) { return false; }
	TestTrue(TEXT("timid identity answers bad weather"), Timid->GetPersonality().Bravery < 0.3f);
	TestTrue(TEXT("brave identity tolerates bad weather"), Brave->GetPersonality().Bravery > 0.85f);
	FUEGT2NPCContext Context = UEGT2NPCTests::Plain(10.0f);
	Context.Weather = EUEGT2Weather::Storm;
	Timid->EvaluateSchedule(Context, false);
	TestFalse(TEXT("a companion answers the storm"), Timid->IsFollowing());
	TestEqual(TEXT("shelter keeps the resolver's priority"), Timid->GetActivityReason(), EUEGT2ActivityReason::Weather);
	TestEqual(TEXT("the companion heads for shelter"), Timid->GetActivity(), EUEGT2Activity::Shelter);
	TestTrue(TEXT("the shelter route replaces the follow route"), Timid->GetDestination().X < -4000.0);
	Brave->EvaluateSchedule(Context, false);
	TestTrue(TEXT("a brave companion who would stay outdoors keeps following"), Brave->IsFollowing());

	Context = UEGT2NPCTests::Plain(2.0f);
	Timid->SetFollowTarget(Target);
	Timid->EvaluateSchedule(Context, false);
	TestFalse(TEXT("scheduled sleep ends companionship"), Timid->IsFollowing());
	TestEqual(TEXT("the companion actually sleeps"), Timid->GetActivity(), EUEGT2Activity::Sleep);
	TestTrue(TEXT("the sleeping companion goes indoors"), Timid->IsIndoors());
	TestTrue(TEXT("the sleeping companion returns home"), Timid->GetActorLocation().Equals(FVector(8000.0, 0.0, 0.0)));
	Dog->EvaluateSchedule(Context, false);
	TestFalse(TEXT("animals keep their resting schedule too"), Dog->IsFollowing());
	TestEqual(TEXT("the dog settles for the night"), Dog->GetActivity(), EUEGT2Activity::Roost);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NPCMovementHitchTest,
	"UEGT2.NPC.MovementHitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2NPCMovementHitchTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("isolated actor world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	for (float Seconds : { 2.0f, 7.0f })
	{
		AUEGT2NPCActor* NPC = World->SpawnActor<AUEGT2NPCActor>();
		if (!TestNotNull(TEXT("walking inhabitant"), NPC)) { return false; }
		NPC->ConfigureNPC(TEXT("Test smith"), EUEGT2NPCRole::Smith, EUEGT2NPCSpecies::Person, 4242);
		NPC->AddAnchor(EUEGT2Anchor::Work, FVector(10000.0, 0.0, 0.0));
		NPC->DispatchBeginPlay();
		NPC->SetLOD(EUEGT2NPCLOD::Near);
		NPC->EvaluateSchedule(UEGT2NPCTests::Plain(10.0f), true);
		const FVector Start = NPC->GetActorLocation();
		const FVector Destination = NPC->GetDestination();
		NPC->Tick(Seconds);
		// The old stuck timer counted this tick's time but not its movement,
		// so a hitch changed the destination or teleported straight to it.
		const double Travelled = FVector::Dist2D(Start, NPC->GetActorLocation());
		TestTrue(TEXT("a long tick still makes walking progress"), Travelled > 0.0);
		TestTrue(TEXT("a long tick cannot teleport to the end of the route"),
			Travelled < NPC->BaseSpeed * 2.0f * Seconds);
		TestTrue(TEXT("a long tick keeps the requested destination"), NPC->GetDestination().Equals(Destination));
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
