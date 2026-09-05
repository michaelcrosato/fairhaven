// Fairhaven (UEGT2) - automation tests over money and the amenities.
//
// The player has the same four needs and the same purse as everybody else, and
// both are advanced by one shared function. That makes the interesting question
// a purely arithmetic one, answerable with no world and no map: does a day's
// work cover a day's living? These tests answer it by running whole days.
//
// The last test is the one worth keeping honest. It closes the loop - needs
// drive ResolveActivity, ResolveActivity drives the needs and the purse - and
// runs every trade through three days of it. A wage table that starves the
// bakers shows up there and nowhere else.
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Interaction/UEGT2Amenity.h"
#include "Misc/ScopeExit.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2NPCRoutines.h"
#include "NPC/UEGT2NPCTypes.h"

namespace UEGT2EconomyTests
{
	/** Every trade a person can have. Animals have no purse. */
	const TArray<EUEGT2NPCRole>& AllRoles()
	{
		static const TArray<EUEGT2NPCRole> Roles = []()
		{
			TArray<EUEGT2NPCRole> Result;
			for (int32 Index = 0; Index < (int32)EUEGT2NPCRole::Count; ++Index)
			{
				Result.Add((EUEGT2NPCRole)Index);
			}
			return Result;
		}();
		return Roles;
	}

	FString RoleName(EUEGT2NPCRole Role)
	{
		return GetRoleDisplayName(Role).ToString();
	}

	/** One inhabitant, simulated: needs, purse and the decision that links them. */
	struct FLife
	{
		EUEGT2NPCRole Role = EUEGT2NPCRole::Villager;
		FUEGT2NPCNeeds Needs;
		FUEGT2Purse Purse;
		FUEGT2Personality Personality;
		int32 Seed = 4242;

		float LowestFed = 1.0f;
		float LowestEnergy = 1.0f;
		float LowestRelief = 1.0f;
		float LowestCoins = 1.0e9f;
		/** Hours spent in each activity, so a failure says what they were doing. */
		float Spent[(int32)EUEGT2Activity::Count] = {};
		/** World hours in which something they wanted could not be paid for. */
		float RefusedHours = 0.0f;

		/**
		 * The longest unbroken stretch spent with a need on the floor.
		 *
		 * This, and not the lowest value, is the question worth asking. Waking
		 * up desperate is true to life and happens to everybody: relief drains
		 * even asleep and there is nothing to be done about it in bed. Being
		 * desperate for six hours means the answer never arrived, which is
		 * either a wage that does not cover a life or a rule that traps people.
		 */
		float WorstZeroRun[3] = {};      // fed, energy, relief

		explicit FLife(EUEGT2NPCRole InRole, int32 InSeed = 4242)
			: Role(InRole), Seed(InSeed)
		{
			Purse.Coins = UEGT2StartingCoins(InRole);
			// Median in every trait, so the schedule is not shifted and the
			// test can talk about exact hours.
			Personality.Sociability = 0.5f;
			Personality.Punctuality = 0.5f;
			Personality.Energy = 0.5f;
			Personality.Curiosity = 0.0f;    // no detours: they are not the point here
			Personality.Bravery = 0.5f;
		}

		/** How long the current stretch on the floor has been running. */
		float ZeroRun[3] = {};

		/** The three activities they gave the most time to, for a failure message. */
		FString Busiest() const
		{
			TArray<TPair<float, EUEGT2Activity>> Ranked;
			for (int32 Index = 0; Index < (int32)EUEGT2Activity::Count; ++Index)
			{
				if (Spent[Index] > 0.0f)
				{
					Ranked.Emplace(Spent[Index], (EUEGT2Activity)Index);
				}
			}
			Ranked.Sort([](const TPair<float, EUEGT2Activity>& A,
				const TPair<float, EUEGT2Activity>& B) { return A.Key > B.Key; });

			FString Out;
			for (int32 Index = 0; Index < FMath::Min(4, Ranked.Num()); ++Index)
			{
				Out += FString::Printf(TEXT("%s %.0fh  "),
					*GetActivityDisplayName(Ranked[Index].Value).ToString(), Ranked[Index].Key);
			}
			return Out;
		}

		/** Run Days whole days at Step world hours a tick, following the routine. */
		void Live(int32 Days, float Step)
		{
			for (int32 Day = 0; Day < Days; ++Day)
			{
				for (float Hour = 0.0f; Hour < 24.0f; Hour += Step)
				{
					FUEGT2NPCContext Context;
					Context.Hour = Hour;
					Context.DayIndex = Day;
					Context.Weather = EUEGT2Weather::Clear;
					Context.PlayerDistance = 1.0e9f;
					Context.Personality = Personality;
					Context.Needs = Needs;
					Context.Purse = Purse;
					Context.Seed = Seed;

					const FUEGT2ActivityDecision Decision =
						ResolveActivity(Role, EUEGT2NPCSpecies::Person, Context);
					if (!UEGT2AdvanceLife(Step, Decision.Activity, Role, Needs, Purse))
					{
						RefusedHours += Step;
					}
					Spent[(int32)Decision.Activity] += Step;

					LowestFed = FMath::Min(LowestFed, Needs.Fed);
					LowestEnergy = FMath::Min(LowestEnergy, Needs.Energy);
					LowestRelief = FMath::Min(LowestRelief, Needs.Relief);
					LowestCoins = FMath::Min(LowestCoins, Purse.Coins);

					const float Values[3] = { Needs.Fed, Needs.Energy, Needs.Relief };
					for (int32 Index = 0; Index < 3; ++Index)
					{
						ZeroRun[Index] = Values[Index] <= 0.001f ? ZeroRun[Index] + Step : 0.0f;
						WorstZeroRun[Index] = FMath::Max(WorstZeroRun[Index], ZeroRun[Index]);
					}
				}
			}
		}
	};
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2WageTableTest,
	"UEGT2.Economy.WageTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2WageTableTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2EconomyTests;

	for (EUEGT2NPCRole Role : AllRoles())
	{
		const float Wage = UEGT2WagePerHour(Role);
		TestTrue(FString::Printf(TEXT("%s wage is finite and not negative"), *RoleName(Role)),
			FMath::IsFinite(Wage) && Wage >= 0.0f);

		// Work and Patrol are the two activities the routines use to mean "at
		// it". Both must pay at least the trade's rate, or somebody works for
		// nothing. At least, rather than exactly, because the two allowances
		// are paid for any waking hour and a working one is one of those.
		TestTrue(FString::Printf(TEXT("%s is paid for Work"), *RoleName(Role)),
			UEGT2WageFor(Role, EUEGT2Activity::Work) >= Wage);
		TestTrue(FString::Printf(TEXT("%s is paid for Patrol"), *RoleName(Role)),
			UEGT2WageFor(Role, EUEGT2Activity::Patrol) >= Wage);

		// Nobody is paid in their sleep. This is the one that stops an
		// allowance quietly becoming an income of twenty-four hours a day.
		TestEqual(FString::Printf(TEXT("%s is not paid to sleep"), *RoleName(Role)),
			UEGT2WageFor(Role, EUEGT2Activity::Sleep), 0.0f);

		// Everybody has an income of some kind, or their purse only goes down.
		TestTrue(FString::Printf(TEXT("%s has an income"), *RoleName(Role)),
			Wage > 0.0f || UEGT2AllowancePerHour(Role) > 0.0f);

		// Everybody starts with something. A purse that can be empty at spawn
		// is a citizen who cannot use a public convenience on day one.
		TestTrue(FString::Printf(TEXT("%s starts with coins"), *RoleName(Role)),
			UEGT2StartingCoins(Role) > 0.0f);
	}

	// The child's "Work" entry is lessons in the church hall. They get pocket
	// money instead, which is not the same thing and is not paid for lessons.
	TestEqual(TEXT("a child earns no wage"),
		UEGT2WagePerHour(EUEGT2NPCRole::Child), 0.0f);
	TestTrue(TEXT("but a child has pocket money"),
		UEGT2AllowancePerHour(EUEGT2NPCRole::Child) > 0.0f);
	TestTrue(TEXT("and an elder has an allowance"),
		UEGT2AllowancePerHour(EUEGT2NPCRole::Elder) > 0.0f);
	TestEqual(TEXT("a villager in work has no allowance"),
		UEGT2AllowancePerHour(EUEGT2NPCRole::Villager), 0.0f);

	// A courier's whole day is Errand and nobody else's is.
	TestTrue(TEXT("a courier is paid for their errands"),
		UEGT2WageFor(EUEGT2NPCRole::Courier, EUEGT2Activity::Errand) > 0.0f);
	TestEqual(TEXT("a smith is not paid for theirs"),
		UEGT2WageFor(EUEGT2NPCRole::Smith, EUEGT2Activity::Errand), 0.0f);

	// The market is the one activity whose meaning depends on who you are.
	TestTrue(TEXT("a merchant is paid at the market"),
		UEGT2WageFor(EUEGT2NPCRole::Merchant, EUEGT2Activity::Market) > 0.0f);
	TestEqual(TEXT("a villager is not paid at the market"),
		UEGT2WageFor(EUEGT2NPCRole::Villager, EUEGT2Activity::Market), 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NPCStartingPurseTest,
	"UEGT2.Economy.NPCStartingPurse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2NPCStartingPurseTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("isolated actor world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AUEGT2NPCActor* Authored = World->SpawnActor<AUEGT2NPCActor>();
	if (!TestNotNull(TEXT("authoring template"), Authored)) { return false; }
	Authored->ConfigureNPC(TEXT("Test smith"), EUEGT2NPCRole::Smith,
		EUEGT2NPCSpecies::Person, 4242);

	// A template copies the baked UPROPERTY identity into a fresh actor, just
	// as loading the generated map does. The non-UPROPERTY purse cannot ride
	// along with it: a test that only calls ConfigureNPC would miss that loss.
	FActorSpawnParameters Spawn;
	Spawn.Template = Authored;
	AUEGT2NPCActor* Loaded = World->SpawnActor<AUEGT2NPCActor>(Spawn);
	AUEGT2NPCActor* Again = World->SpawnActor<AUEGT2NPCActor>(Spawn);
	if (!TestNotNull(TEXT("loaded NPC"), Loaded)
		|| !TestNotNull(TEXT("second loaded NPC"), Again)) { return false; }
	TestEqual(TEXT("baked identity survives loading"), Loaded->GetSeed(), 4242);
	Loaded->DispatchBeginPlay();
	Again->DispatchBeginPlay();
	const float StartingCoins = Loaded->GetPurse().Coins;
	TestTrue(TEXT("a loaded citizen starts able to buy a meal"),
		StartingCoins >= UEGT2PriceFor(EUEGT2NPCRole::Smith, EUEGT2Activity::Eat));
	TestTrue(TEXT("starting money follows the trade's seeded range"),
		StartingCoins >= UEGT2StartingCoins(EUEGT2NPCRole::Smith) * 0.55f
		&& StartingCoins < UEGT2StartingCoins(EUEGT2NPCRole::Smith) * 1.45f);
	TestEqual(TEXT("the same identity starts with the same purse"),
		Again->GetPurse().Coins, StartingCoins);

	AUEGT2NPCActor* Animal = World->SpawnActor<AUEGT2NPCActor>();
	if (!TestNotNull(TEXT("animal"), Animal)) { return false; }
	Animal->ConfigureNPC(TEXT("Test goat"), EUEGT2NPCRole::Smith,
		EUEGT2NPCSpecies::Goat, 4242);
	Animal->DispatchBeginPlay();
	TestEqual(TEXT("an animal does not inherit a human purse"), Animal->GetPurse().Coins, 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2PriceTableTest,
	"UEGT2.Economy.Prices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2PriceTableTest::RunTest(const FString& Parameters)
{
	const EUEGT2NPCRole Anyone = EUEGT2NPCRole::Villager;

	TestTrue(TEXT("eating out costs"), UEGT2PriceFor(Anyone, EUEGT2Activity::Eat) > 0.0f);
	TestTrue(TEXT("the tavern costs more than a meal"),
		UEGT2PriceFor(Anyone, EUEGT2Activity::Tavern)
		> UEGT2PriceFor(Anyone, EUEGT2Activity::Eat));
	TestTrue(TEXT("a public convenience costs a little"),
		UEGT2PriceFor(Anyone, EUEGT2Activity::Washroom) > 0.0f);
	TestTrue(TEXT("a washroom is the cheapest thing you can buy"),
		UEGT2PriceFor(Anyone, EUEGT2Activity::Washroom)
		< UEGT2PriceFor(Anyone, EUEGT2Activity::Eat));

	// The scheduled meals happen at home, out of a larder nothing models. If
	// they ever start costing money, every routine in the game gets dearer by
	// three meals a day at once and the whole town goes broke quietly.
	for (EUEGT2Activity Free : { EUEGT2Activity::Breakfast, EUEGT2Activity::Lunch,
		EUEGT2Activity::Dinner, EUEGT2Activity::Sleep, EUEGT2Activity::Rest,
		EUEGT2Activity::Work, EUEGT2Activity::Stroll, EUEGT2Activity::Socialise,
		EUEGT2Activity::Worship, EUEGT2Activity::Idle })
	{
		TestEqual(FString::Printf(TEXT("%s is free"),
			*GetActivityDisplayName(Free).ToString()),
			UEGT2PriceFor(Anyone, Free), 0.0f);
	}

	TestEqual(TEXT("a merchant does not pay to stand at their own stall"),
		UEGT2PriceFor(EUEGT2NPCRole::Merchant, EUEGT2Activity::Market), 0.0f);
	TestTrue(TEXT("everybody else pays at the market"),
		UEGT2PriceFor(Anyone, EUEGT2Activity::Market) > 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2PurseTest,
	"UEGT2.Economy.Purse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2PurseTest::RunTest(const FString& Parameters)
{
	FUEGT2Purse Purse;
	Purse.Coins = 10.0f;

	TestTrue(TEXT("spending what is there succeeds"), Purse.Spend(4.0f));
	TestEqual(TEXT("and takes it"), Purse.Coins, 6.0f);

	// All or nothing: a half-paid meal would be a tab, and there are no tabs.
	TestFalse(TEXT("spending what is not there fails"), Purse.Spend(9.0f));
	TestEqual(TEXT("and takes nothing"), Purse.Coins, 6.0f);

	TestTrue(TEXT("spending nothing always succeeds"), Purse.Spend(0.0f));
	TestTrue(TEXT("an exact payment succeeds"), Purse.Spend(6.0f));
	TestEqual(TEXT("leaving an empty purse"), Purse.Coins, 0.0f);
	TestFalse(TEXT("an empty purse cannot afford a penny"), Purse.CanAfford(1.0f));

	Purse.Earn(2.75f);
	TestEqual(TEXT("earning adds the fraction"), Purse.Coins, 2.75f);
	TestEqual(TEXT("but only whole coins are shown"), Purse.Whole(), 2);

	Purse.Earn(-100.0f);
	TestEqual(TEXT("earning cannot be used to take money away"), Purse.Coins, 2.75f);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AdvanceLifeTest,
	"UEGT2.Economy.AdvanceLife",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2AdvanceLifeTest::RunTest(const FString& Parameters)
{
	// An hour of work pays the wage and costs energy.
	{
		FUEGT2NPCNeeds Needs;
		FUEGT2Purse Purse;
		const bool bPaid = UEGT2AdvanceLife(1.0f, EUEGT2Activity::Work,
			EUEGT2NPCRole::Smith, Needs, Purse);
		TestTrue(TEXT("work needs no money"), bPaid);
		TestEqual(TEXT("an hour at the forge pays the smith's rate"),
			Purse.Coins, UEGT2WagePerHour(EUEGT2NPCRole::Smith));
		TestTrue(TEXT("and takes it out of you"), Needs.Energy < 1.0f);
	}

	// A meal fills you up and is paid for.
	{
		FUEGT2NPCNeeds Needs;
		Needs.Fed = 0.2f;
		FUEGT2Purse Purse;
		Purse.Coins = 20.0f;
		const bool bPaid = UEGT2AdvanceLife(1.0f, EUEGT2Activity::Eat,
			EUEGT2NPCRole::Villager, Needs, Purse);
		TestTrue(TEXT("a meal you can pay for is served"), bPaid);
		TestTrue(TEXT("and it feeds you"), Needs.Fed > 0.2f);
		TestEqual(TEXT("and it costs the menu price"),
			Purse.Coins, 20.0f - UEGT2PriceFor(EUEGT2NPCRole::Villager, EUEGT2Activity::Eat));
	}

	// The point of money existing at all: with none, the need does not move.
	{
		FUEGT2NPCNeeds Needs;
		Needs.Fed = 0.2f;
		FUEGT2Purse Purse;                       // empty
		const bool bPaid = UEGT2AdvanceLife(1.0f, EUEGT2Activity::Eat,
			EUEGT2NPCRole::Villager, Needs, Purse);
		TestFalse(TEXT("a meal you cannot pay for is refused"), bPaid);
		TestTrue(TEXT("and you get hungrier standing there"), Needs.Fed < 0.2f);
		TestEqual(TEXT("and the empty purse stays empty"), Purse.Coins, 0.0f);
	}

	// Zero elapsed time must be a no-op, because the tick that calls this can
	// legitimately be handed a zero when the clock is frozen.
	{
		FUEGT2NPCNeeds Needs;
		FUEGT2Purse Purse;
		Purse.Coins = 5.0f;
		TestTrue(TEXT("no time passing is not a failure"),
			UEGT2AdvanceLife(0.0f, EUEGT2Activity::Eat, EUEGT2NPCRole::Villager, Needs, Purse));
		TestEqual(TEXT("and charges nothing"), Purse.Coins, 5.0f);
		TestEqual(TEXT("and changes nothing"), Needs.Fed, 1.0f);
	}
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AmenityMappingTest,
	"UEGT2.Economy.Amenities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2AmenityMappingTest::RunTest(const FString& Parameters)
{
	// Every kind must map to something, and no two kinds to the same thing:
	// two amenities that do the same job means one of them cannot be found by
	// anyone looking for the other.
	TSet<EUEGT2Activity> Seen;
	for (int32 Index = 0; Index < (int32)EUEGT2AmenityKind::Count; ++Index)
	{
		const EUEGT2AmenityKind Kind = (EUEGT2AmenityKind)Index;
		const EUEGT2Activity Activity = UEGT2ActivityForAmenity(Kind);
		TestTrue(FString::Printf(TEXT("%s does something"), UEGT2AmenityKindName(Kind)),
			Activity != EUEGT2Activity::Idle);
		TestFalse(FString::Printf(TEXT("%s is not a duplicate"), UEGT2AmenityKindName(Kind)),
			Seen.Contains(Activity));
		Seen.Add(Activity);
	}

	// All four needs have somewhere to answer them. This is the check that
	// catches a kind being deleted and a need quietly becoming unanswerable.
	FUEGT2NPCNeeds Drained;
	Drained.Energy = 0.0f;
	Drained.Fed = 0.0f;
	Drained.Relief = 0.0f;
	Drained.Company = 0.0f;

	for (const TCHAR* Label : { TEXT("Fed"), TEXT("Energy"), TEXT("Relief"), TEXT("Company") })
	{
		bool bAnswered = false;
		for (int32 Index = 0; Index < (int32)EUEGT2AmenityKind::Count; ++Index)
		{
			FUEGT2NPCNeeds Needs = Drained;
			Needs.Advance(1.0f, UEGT2ActivityForAmenity((EUEGT2AmenityKind)Index));
			const float After = FCString::Strcmp(Label, TEXT("Fed")) == 0 ? Needs.Fed
				: FCString::Strcmp(Label, TEXT("Energy")) == 0 ? Needs.Energy
				: FCString::Strcmp(Label, TEXT("Relief")) == 0 ? Needs.Relief
				: Needs.Company;
			bAnswered |= After > 0.0f;
		}
		TestTrue(FString::Printf(TEXT("some amenity answers %s"), Label), bAnswered);
	}
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2LivingWageTest,
	"UEGT2.Economy.LivingWage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2LivingWageTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2EconomyTests;

	// Three days of the real loop, per trade: the routine decides, the needs
	// bend the decision, the decision spends and earns, and round again.
	for (EUEGT2NPCRole Role : AllRoles())
	{
		FLife Life(Role);
		const float Started = Life.Purse.Coins;
		Life.Live(3, 0.05f);

		const FString Who = RoleName(Role);
		AddInfo(FString::Printf(
			TEXT("%-11s coins %5.0f -> %5.0f (low %5.0f)  low fed %.2f energy %.2f relief %.2f  "
				 "stuck %.1f/%.1f/%.1fh  refused %.1fh   %s"),
			*Who, Started, Life.Purse.Coins, Life.LowestCoins,
			Life.LowestFed, Life.LowestEnergy, Life.LowestRelief,
			Life.WorstZeroRun[0], Life.WorstZeroRun[1], Life.WorstZeroRun[2],
			Life.RefusedHours, *Life.Busiest()));

		// Nothing anybody wanted went unpaid for long. A little is honest - a
		// purse can be a penny short for a moment - but hours of it means the
		// trade does not pay for the life it has to live, which is the failure
		// this whole test exists to catch.
		TestTrue(FString::Printf(TEXT("%s can pay their way (refused %.1f hours, low %.1f coins)"),
			*Who, Life.RefusedHours, Life.LowestCoins), Life.RefusedHours < 0.5f);

		// Nobody stays starving, collapsed or desperate. Hitting the floor is
		// allowed - you wake up hungry - but the answer has to arrive. Company
		// is left out: answering it needs somebody else to be standing there,
		// and this simulation has nobody in it but one person.
		const TCHAR* NeedNames[3] = { TEXT("starving"), TEXT("exhausted"), TEXT("desperate") };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s is never %s for long (%.1f hours)"),
				*Who, NeedNames[Index], Life.WorstZeroRun[Index]),
				Life.WorstZeroRun[Index] < 2.0f);
		}

		// The working trades end better off than they started. The two that do
		// not earn - a child at school and an elder on a parish allowance - are
		// meant to run down, and are checked against destitution above instead.
		if (UEGT2WagePerHour(Role) >= 4.0f)
		{
			TestTrue(FString::Printf(TEXT("%s is better off after three days (%.0f -> %.0f)"),
				*Who, Started, Life.Purse.Coins), Life.Purse.Coins > Started);
		}
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
